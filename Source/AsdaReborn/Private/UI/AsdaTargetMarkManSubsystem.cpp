// Copyright (c) 2026 @sitxovski (Discord)
// Реализация показа/скрытия своей плашки (`TARGETMARK_MAN` 1620; шапка — в .h).
//   Волна 2-В (06.10.2026), отчёт `Reports/owner_1006/wave2C_duel_mark_banner.md` §2 и §6 (ревизия С-2).
#include "UI/AsdaTargetMarkManSubsystem.h"
#include "AsdaPerf.h"   // R4, доля a: области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaNetSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaNameplateComponent.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTargetMarkMan, Log, All);

// ★ Ревизия 2-В С-2: по умолчанию 0 — своя плашка как чужие (закон владельца self-nameplate-same-as-others).
static ASDA_DEV_CVAR(int32, CVarSelfPlateHideOnDeath,
	TEXT("asda.Plate.SelfHideOnDeath"), 0,
	TEXT("0 (по умолчанию) — своя плашка ведёт себя как чужие: при смерти не гаснет (закон владельца «своя плашка как у других»). ")
	TEXT("1 — как окно 1620 оригинала: скрыть на смерти «на глазах» (4053, `hudHideTargetNameplate`), показать на каждом входе ")
	TEXT("своего персонажа 4056 (`hudShowTargetNameplate`) независимо от «мёртв»."),
	ECVF_Default);

void UAsdaTargetMarkManSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		DeathHandle = Net->OnCharDeath.AddUObject(this, &UAsdaTargetMarkManSubsystem::OnCharDeath);
		EntryHandle = Net->OnMapEntry4056.AddUObject(this, &UAsdaTargetMarkManSubsystem::OnMapEntryFx);   // ★ только 4056 (М-1)
	}
}

void UAsdaTargetMarkManSubsystem::Deinitialize()
{
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnCharDeath.Remove(DeathHandle);
		Net->OnMapEntry4056.Remove(EntryHandle);
	}
	Super::Deinitialize();
}

// `hudHideTargetNameplate` зовёт только `CCharacter_SetAction` case 6 (entity_char.c:16907) — переход в смерть.
//   Вход уже мёртвым (`EnterSelfDeadState(…, bAlreadyDead = true)`, 4003 regenState 200 / 6001) у нас тоже шлёт
//   `OnCharDeath`, но клипа падения не играет — его отличаем по `bSelfDeathNoClip` и плашку не трогаем.
void UAsdaTargetMarkManSubsystem::OnCharDeath(int32 UserIdx, int32 /*KillerIdx*/)
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !AsdaSelf::IsSelfUserIdx(Net, UserIdx) || Net->bSelfDeathNoClip) { return; }
	bHideLatch = true;
}

// Полный сброс сессии (разрыв, выход в выбор персонажа): новый вход начинается с видимой 1620 (`uiCharSelectInitControls`).
void UAsdaTargetMarkManSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	if (AsdaIsFullSessionReset(Reason)) { bHideLatch = false; }
}

// `hudShowTargetNameplate` — вход своего персонажа на карту (`GameNet_OnLocalPlayerMapEntry`, 4056), без гейта «мёртв».
void UAsdaTargetMarkManSubsystem::OnMapEntryFx(int32 UserIdx, bool /*bDeadArrival*/)
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !AsdaSelf::IsSelfUserIdx(Net, UserIdx)) { return; }
	bHideLatch = false;
}

UAsdaNameplateComponent* UAsdaTargetMarkManSubsystem::FindSelfPlate() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const AActor* Self = WS ? (IsValid(WS->LocalPlayerActor) ? WS->LocalPlayerActor.Get() : nullptr) : nullptr;
	return Self ? Self->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
}

void UAsdaTargetMarkManSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.TargetMarkMan");   // R4, доля a: область замера (AsdaPerf.h; в Shipping пусто)
	UAsdaNameplateComponent* NP = FindSelfPlate();
	if (!NP) { bHiddenByUs = false; LastPlate.Reset(); return; }
	if (LastPlate.Get() != NP)
	{
		// Новый павн: мир сделал его плашку видимой (`bHideForLocalPlayer = false`, AsdaWorldSubsystem.cpp:697);
		//   защёлка эталона (окно 1620 одно на сессию) применяется к нему ниже.
		LastPlate = NP;
		bHiddenByUs = false;
	}
	const bool bWantHidden = bHideLatch && (CVarSelfPlateHideOnDeath.GetValueOnGameThread() != 0);
	if (bWantHidden && !bHiddenByUs)
	{
		NP->bHideForLocalPlayer = true;    // гейт плашки «над собой не рисуем»
		bHiddenByUs = true;
		UE_LOG(LogAsdaTargetMarkMan, Log, TEXT("[своя плашка] скрыта (ключ 1: hudHideTargetNameplate, entity_char.c:16907)"));
	}
	else if (!bWantHidden && bHiddenByUs)
	{
		NP->bHideForLocalPlayer = false;   // решение владельца: своя плашка видна
		bHiddenByUs = false;
		UE_LOG(LogAsdaTargetMarkMan, Log, TEXT("[своя плашка] снова видна (вход 4056, полный сброс или ключ 0)"));
	}
}
