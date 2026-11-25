// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ «КОРОНА» ВЛАДЕЛЬЦА ДОБЫЧИ РЕЙД-БОССА (6126) — реализация. Полный разбор потока — в заголовке.
#include "World/AsdaKingCrownSubsystem.h"
#include "AsdaPerf.h"   // R4, доля a: области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "World/AsdaWorldSubsystem.h"        // ForEachPlayerActorWithCharId / FindPlayerActorByCharId — CharManager оригинала
#include "World/AsdaPlayerCharacter.h"       // фазы анимации (состояние действия) и BaseSkel носителя
#include "AsdaNetSubsystem.h"                // SelectedCharId — свой charId
#include "AsdaEffectActor.h"                 // SpawnByJebName / ConfigureBoneAttach (attach-6 = CharWorld)
#include "AsdaDataTableCache.h"              // AsdaDt::Cached — DT_CallMob (гейт приёма)
#include "Data/AsdaEffectMobTableRows.h"     // FAsdaCallMobRow
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCrown, Log, All);

namespace
{
	// Таблица `Data\Mob\CallMobTable.BIN` (та же, что у призыва 30050, `AsdaNpcDialogSubsystem.cpp`).
	const TCHAR* kDtCallMobPath = TEXT("/Game/AsdaAssets/Data/DT_CallMob.DT_CallMob");
	// Имя эффекта: `Effect_SpawnByName(g_pEffectMgr, "king_cap01.jeb", …)` (CharFx_UpdateKingCrown@0xA21110).
	const TCHAR* kCrownJeb = TEXT("king_cap01");

	// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА (см. шапку заголовка): видимость ветки «глобальный владелец».
	ASDA_DEV_CVAR(int32, CVarCrownGlobalUsesStoredByte,
		TEXT("asda.Crown.GlobalUsesStoredByte"), 1,
		TEXT("Корона 6126, ветка «персонажа не было в мире при приёме» (g_nCrownOwnerId).\n")
		TEXT("1 (по умолчанию) = [ИСПРАВЛЕНИЕ] видимость берётся из присланного байта (оригинал его запоминает в\n")
		TEXT("byte_290999C и не читает), если своя видимость персонажа 0.\n")
		TEXT("0 = как в оригинале: видимость из поля персонажа (+0x33094), у нового персонажа там 0 — корона погашена."),
		ECVF_Default);
}

void UAsdaKingCrownSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Эффекты чужих гаснут всегда: при смене карты/выходе мир умер, при разрыве — процесс оригинала кончился.
	for (TPair<int32, FCrownState>& It : ByCharId) { StopFx(It.Value); }
	ByCharId.Reset();
	// [ОРИГИНАЛ] свой при смене карты: шаг 11 загрузки гасит эффект и обнуляет +0x33088/+0x330E4, НЕ трогая +0x330D5/+0x33094.
	StopFx(SelfState);
	SelfState.Host.Reset();
	if (Reason == EAsdaSessionResetReason::LeftWorld || Reason == EAsdaSessionResetReason::EnteringWorld)
	{
		// [ОРИГИНАЛ] вход из выбора персонажа = новый CCharacter (`questCreateAndRegister@0x9D1490`, new 0x39BE0 + ctor):
		//   +0x330D5/+0x33094 = 0. `g_nCrownOwnerId` — глобал .data, живёт до конца процесса.
		SelfState = FCrownState();
	}
	if (Reason == EAsdaSessionResetReason::Disconnected)
	{
		// Разрыв = конец процесса оригинала (`original-quits-client-on-disconnect`): всё к значениям старта.
		SelfState = FCrownState();
		CrownOwnerCharId = -1;
		CrownOwnerByte = 0;
	}
	SpawnFailLogged.Reset();
	UE_LOG(LogAsdaCrown, Log, TEXT("[корона] сброс (%s): владелец charId=%d, свой ждёт=%d видимость=%d"),
		AsdaSessionResetReasonName(Reason), CrownOwnerCharId, SelfState.bPending ? 1 : 0, SelfState.Visible);
}

void UAsdaKingCrownSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.KingCrown");   // R4, доля a: область замера (AsdaPerf.h; в Shipping пусто)
	UpdateAllCrowns();
}

bool UAsdaKingCrownSubsystem::IsGateOpen() const
{
	if (GateOverrideForTest >= 0) { return GateOverrideForTest == 1; }
	// [ОРИГИНАЛ] `*(BYTE*)(dword_34A0490 + 16) == 1` — байт +16 ЗАПИСИ 0 CallMobTable (`SpawnArgByte`).
	const UDataTable* Dt = AsdaDt::Cached(kDtCallMobPath);
	if (!Dt) { return false; }   // таблицы нет — у оригинала без таблицы клиент бы не стартовал; молчим
	for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
	{
		const FAsdaCallMobRow* R = reinterpret_cast<const FAsdaCallMobRow*>(It.Value);
		if (R && R->Index == 0) { return (R->SpawnArgByte & 0xFF) == 1; }   // запись 0 = первая в файле
	}
	return false;
}

void UAsdaKingCrownSubsystem::OnCharIdByteUpdate(int32 CharId, uint8 Byte)
{
	if (!IsGateOpen())
	{
		UE_LOG(LogAsdaCrown, Log, TEXT("←6126 charId=%d: гейт CallMobTable[0].+16 != 1 — пакет пропущен (как эталон)"), CharId);
		return;
	}
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	// `CharManager_FindByCharId`: свой павн (при совпадении с SelectedCharId) либо чужой из реестра мира.
	AActor* Found = (WS && CharId != 0) ? WS->FindPlayerActorByCharId(CharId) : nullptr;
	if (Found)
	{
		const bool bSelf = Net && Net->SelectedCharId != 0 && Net->SelectedCharId == CharId;   // свой ли
		FCrownState& St = bSelf ? SelfState : ByCharId.FindOrAdd(CharId);
		if (!bSelf && St.Host.Get() != Found) { StopFx(St); St = FCrownState(); }   // новый актор = новый CCharacter
		St.Host = Found;
		St.bPending = true;          // +0x330D5 = 1
		St.Visible = (int32)Byte;    // +0x33094 = байт
		UE_LOG(LogAsdaCrown, Log, TEXT("←6126 charId=%d (%s) найден: корона ждёт показа, видимость %u"),
			CharId, bSelf ? TEXT("свой") : TEXT("чужой"), (uint32)Byte);
		return;
	}
	CrownOwnerCharId = CharId;       // g_nCrownOwnerId
	CrownOwnerByte = Byte;           // byte_290999C
	UE_LOG(LogAsdaCrown, Log, TEXT("←6126 charId=%d в мире нет: запомнен глобальный владелец (байт %u)"), CharId, (uint32)Byte);
}

void UAsdaKingCrownSubsystem::UpdateAllCrowns()
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!WS) { return; }
	TSet<int32> Seen;   // чужие, которых видели в этом проходе
	WS->ForEachPlayerActorWithCharId([&](int32 CharId, AActor* Actor, bool bSelf)
	{
		if (bSelf)
		{
			if (SelfState.Host.Get() != Actor) { StopFx(SelfState); SelfState.Host = Actor; }   // новый павн — эффект пересоздать
			UpdateOne(CharId, Actor, SelfState);
			return;
		}
		Seen.Add(CharId);
		FCrownState* St = ByCharId.Find(CharId);
		if (St && St->Host.Get() != Actor) { StopFx(*St); *St = FCrownState(); St->Host = Actor; }
		if (St) { UpdateOne(CharId, Actor, *St); return; }
		// Записи нет — у оригинала поля нового CCharacter нулевые (+0x33088 = −1), работает только ветка 2).
		if (CrownOwnerCharId >= 0 && CrownOwnerCharId == CharId)
		{
			FCrownState& New = ByCharId.Add(CharId);
			New.Host = Actor;
			UpdateOne(CharId, Actor, New);
		}
	});
	// [ОРИГИНАЛ] `CharManager_RemoveNode` гасит эффект вместе с объектом персонажа.
	for (auto It = ByCharId.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It.Key())) { StopFx(It.Value()); It.RemoveCurrent(); }
	}
}

void UAsdaKingCrownSubsystem::UpdateOne(int32 CharId, AActor* Actor, FCrownState& St)
{
	// 1) «ждёт показа» (+0x330D5) и эффекта нет (+0x33088 < 0).
	if (St.bPending && !St.Fx.IsValid())
	{
		St.Fx = SpawnCrown(Actor, CharId);
		ApplyVisible(St);
		St.bPending = false;   // эталон снимает флаг и при неудачном спавне
	}
	else if (St.Fx.IsValid())
	{
		ApplyVisible(St);
	}
	// 2) глобальный владелец.
	if (CrownOwnerCharId < 0)
	{
		if (St.Fx.IsValid()) { ApplyVisible(St); }
	}
	else if ((EtalonActionStateOf(Actor) & 1) == 0 && CrownOwnerCharId == CharId && !St.Fx.IsValid())
	{
		if (St.Visible == 0 && CVarCrownGlobalUsesStoredByte.GetValueOnGameThread() != 0)
		{
			St.Visible = (int32)CrownOwnerByte;   // [ИСПРАВЛЕНИЕ] байт пакета вместо нуля конструктора
		}
		St.Fx = SpawnCrown(Actor, CharId);
		ApplyVisible(St);
	}
	// ★ 24.10.2026 [ПОРТ]: в прыжке (донор AsdaFableClassic) тело поднимает кость корня клипа, а attach-6 держит корону на
	//   матрице КОМПОНЕНТА персонажа (на земле) — поднимаем её на ту же величину (ключ `asda.Jump.OverheadFollow`, вне прыжка 0).
	//   Сдвиг слежения задан в осях ноды (`ComputeFollowTargetWorld` → `Q.RotateVector(FollowOffset)`), поэтому мировой подъём
	//   переводим в эти оси.
	if (AAsdaEffectActor* Fx = St.Fx.Get())
	{
		const AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(Actor);
		const float Lift = Ch ? Ch->GetOverheadLiftZ() : 0.f;
		FVector Loc; FQuat Q;
		const FVector Want = (Lift != 0.f && Fx->ComputeFollowTargetWorld(Loc, Q)) ? Q.UnrotateVector(FVector(0.f, 0.f, Lift)) : FVector::ZeroVector;
		if (!Fx->FollowOffset.Equals(Want, 0.01f)) { Fx->FollowOffset = Want; }
	}
}

AAsdaEffectActor* UAsdaKingCrownSubsystem::SpawnCrown(AActor* Host, int32 CharId)
{
	UWorld* W = Host ? Host->GetWorld() : nullptr;
	if (!W) { return nullptr; }
	// Точка спавна эталона — `CObj_GetPosition(char->gap_1B0)`; при attach-6 её каждый кадр переписывает матрица персонажа.
	AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnByJebName(W, kCrownJeb, Host->GetActorLocation(), FRotator::ZeroRotator, Host);
	if (!Fx)
	{
		if (!SpawnFailLogged.Contains(CharId))
		{
			SpawnFailLogged.Add(CharId);
			UE_LOG(LogAsdaCrown, Warning, TEXT("[корона] BP_Fx_%s не собран — корона charId=%d не показана ")
				TEXT("(собери: ASDA_CHARFX_ONLY=king_cap01 Tools/asda_build_char_fx.py)"), kCrownJeb, CharId);
		}
		return nullptr;
	}
	USkeletalMeshComponent* Skel = nullptr;   // скелет носителя (для CharWorld)
	if (AAsdaPlayerCharacter* C = Cast<AAsdaPlayerCharacter>(Host)) { Skel = C->BaseSkel; }
	// attach-6 (`effectNodeFollowPlayerBone@0xF58850`, имя IDA врёт): полная мировая матрица персонажа, без кости.
	Fx->ConfigureBoneAttach(Host, Skel, NAME_None, EAsdaFxAttachMode::CharWorld, FVector::ZeroVector);
	// `fx+0x2A8 = 1` — зациклено до `StopByOwner`: срок жизни НЕ ставим.
	// ★ 08.10.2026 (ЦИКЛ 1:1, `asda.Fx.Loop`): `fx+0x2A8` — это endMode КОНТЕЙНЕРА эффекта (node+12+0x29C).
	//   В шапке `king_cap01.jeb` он 0 (7 с и конец), эталон перебивает его на 1 — рестарт всего эффекта
	//   по концу 7 с. Без этой строки рантайм цикла погасил бы застывшие меши короны через 7 с.
	Fx->SetLoopContainerEndOverride(1);
	UE_LOG(LogAsdaCrown, Log, TEXT("[корона] charId=%d: эффект %s создан (attach-6, зациклен)"), CharId, kCrownJeb);
	return Fx;
}

void UAsdaKingCrownSubsystem::ApplyVisible(const FCrownState& St)
{
	if (AAsdaEffectActor* Fx = St.Fx.Get())
	{
		const bool bHide = (St.Visible & 0xFF) == 0;   // флаг эмиттера +296: 0 — погашен
		if (Fx->IsHidden() != bHide) { Fx->SetActorHiddenInGame(bHide); }
	}
}

void UAsdaKingCrownSubsystem::StopFx(FCrownState& St)
{
	if (AAsdaEffectActor* Fx = St.Fx.Get()) { Fx->Destroy(); }
	St.Fx.Reset();
}

int32 UAsdaKingCrownSubsystem::EtalonActionStateOf(const AActor* Actor)
{
	const AAsdaPlayerCharacter* C = Cast<AAsdaPlayerCharacter>(Actor);
	if (!C) { return 0; }
	if (C->AnimState == EAsdaPlayerState::Dead) { return 6; }   // смерть
	const int32 Sit = C->GetSitPhase();
	if (Sit == 7 || Sit == 8 || Sit == 9) { return Sit; }       // сесть / сидит / встать
	if (C->AnimState == EAsdaPlayerState::Sit) { return 8; }
	if (C->IsAttackAnimActive()) { return 5; }                  // удар
	if (C->IsSkillAnimActive()) { return 24; }                  // навык
	if (C->IsFishActionActive()) { return C->GetFishAction(); } // ★ В5: рыбалка — состояние 60…67 как есть
	if (C->IsEtalonState68()) { return 68; }                    // ★ В45fix2: «добыча» (4025 130), чётное
	if (C->GetEtalonShovelState() != 0) { return C->GetEtalonShovelState(); }   // ★ 09.10.2026 (п. 5): лопата 40 / 43
	if (C->IsEmotionAnimActive()) { return 33; }                // эмоция
	if (C->IsMoving()) { return 1; }                            // бег (`+47551 == 1`)
	return 0;                                                   // покой
}

bool UAsdaKingCrownSubsystem::GetCrownState(int32 CharId, bool& bOutPending, int32& OutVisible, AAsdaEffectActor*& OutFx) const
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const FCrownState* St = (Net && Net->SelectedCharId != 0 && Net->SelectedCharId == CharId) ? &SelfState : ByCharId.Find(CharId);
	if (!St) { return false; }
	bOutPending = St->bPending;
	OutVisible = St->Visible;
	OutFx = St->Fx.Get();
	return true;
}

void UAsdaKingCrownSubsystem::DebugResetAll()
{
	for (TPair<int32, FCrownState>& It : ByCharId) { StopFx(It.Value); }
	ByCharId.Reset();
	StopFx(SelfState);
	SelfState = FCrownState();
	CrownOwnerCharId = -1;
	CrownOwnerByte = 0;
	SpawnFailLogged.Reset();
}
