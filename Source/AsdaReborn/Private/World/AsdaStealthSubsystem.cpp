// Copyright (c) 2026 @sitxovski (Discord)
// AsdaStealthSubsystem.cpp — вид невидимости навыка (112/186). Разбор эталона — в шапке заголовка.
#include "World/AsdaStealthSubsystem.h"
#include "AsdaDevConsole.h"                 // ASDA_DEV_CVAR — дев-ключи вне Shipping
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaSoulGuardianActor.h"   // хранитель души скрывается вместе с невидимым (как при GM-скрытии)
#include "UI/AsdaEffectsSubsystem.h"        // список состояний персонажа (112/186/154)
#include "UI/AsdaPartySubsystem.h"          // член отряда наблюдателя — α 0.2/0.3 (FAsdaPartyMember.CharId)
#include "UI/AsdaSoulmateSubsystem.h"       // Друг наблюдателя — α 0.4/0.3
#include "UI/AsdaSkillDatabase.h"           // AoeRadius навыка, давшего 154
#include "AsdaNetSubsystem.h"               // свой уровень (SelfLevel)
#include "AsdaEffectActor.h"                // kAsdaGmHiddenTagName — метка GM-скрытия на акторе
#include "World/AsdaAttachedFxHide.h"       // ★ R5: AsdaSetAttachedFxHidden — подвесные эффекты гасятся вместе с владельцем
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

// Подвесные эффект-акторы (крылья, свечение оружия, ауры) — самостоятельные акторы, `SetActorHiddenInGame` родителя их не
//   трогает. Тот же приём, что у мира: рекурсивно, эффект на эффекте тоже.
// ★ R5 (12.11.2026): своя дословная копия `AsdaStealthSetAttachedFxHidden` снята — общая `AsdaSetAttachedFxHidden`
//   из `World/AsdaAttachedFxHide.h` (та же, что у мира).

DEFINE_LOG_CATEGORY_STATIC(LogAsdaStealth, Log, All);

static ASDA_DEV_CVAR(int32, CVarAsdaStealthDetectRealDist,
	TEXT("asda.Stealth.DetectRealDist"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — радиус «Обнаружения маскировки» (154) сравнивается с расстоянием между персонажами; 0 — как эталон "
	     "`skillCheckRangeVsDist@0xA38750`: с разностью длин векторов позиций от начала координат."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarAsdaStealthClearTarget,
	TEXT("asda.Stealth.ClearTarget"), 1,
	TEXT("[ОРИГИНАЛ] 1 — невидимый (112/186) персонаж-цель снимается каждый кадр (эталон game.c:10153); 0 — не снимается."),
	ECVF_Default);
// Значение `asda.Stealth.Visual` (ключ объявлен в AsdaPlayerCharacter.cpp).
bool AsdaStealthVisualOn();

namespace AsdaStealth
{
	// Числа эталона (мс-часы; deltaTime эталона — миллисекунды).
	constexpr float kFadeOutRate   = 0.0005f;    // 0x10000: α −= 0.0005·dt (charEntityUpdateTick)
	constexpr float kFadeOutMs     = 2000.f;     // … фаза снимается через 2000 мс
	constexpr float kFadeInRate    = 0.001f;     // 0x20000: α += 0.001·dt
	constexpr float kFadeInMs      = 1000.f;     // … через 1000 мс α = 1
	// ★ Ревизия 24.10 (ВАЖНО-1): CharManager_UpdateAll гасит шагом 0.00025·dt и БЕЗ детектора (`0x9D5E91` fld [0x13A91C4]) —
	//   множителя ×10 нет. Вместе с фазой 0x10000 (0.0005) α 1→0 ≈ 1.33 с (0.00075/мс суммарно).
	constexpr float kOthersRate    = 0.00025f;   // CharManager_UpdateAll, без детектора
	constexpr float kDetectRate    = 0.00025f;   // … у наблюдателя с 154
	constexpr float kGmViewerAlpha = 0.5f;       // наблюдатель под GM-скрытием видит невидимок (game.c:55656-55664)
	constexpr float kSelfFloor     = 0.3f;       // свой / отряд / Друг на время затухания
	constexpr float kPartyAlpha    = 0.2f;       // член отряда наблюдателя (CharManager_UpdateAll)
	constexpr float kMateAlpha     = 0.4f;       // Друг наблюдателя
	constexpr float kDetectAlpha   = 0.5f;       // наблюдатель с 154, в радиусе
	constexpr float kRemoveAlpha   = 0.5f;       // снятие посреди затухания: α := 0.5 (cChar_removeSkillEffect)
}

void UAsdaStealthSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWorldSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaEffectsSubsystem::StaticClass());
	bInitialized = true;
}

void UAsdaStealthSubsystem::Deinitialize()
{
	bInitialized = false;
	States.Reset();
	Super::Deinitialize();
}

void UAsdaStealthSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Персонажи прошлой карты/сессии: их состояния (и наши скрытия) больше не действуют. Акторы живы только при Disconnected —
	//   там вернём им вид, чтобы экран входа не унаследовал скрытых.
	for (TPair<TWeakObjectPtr<AAsdaPlayerCharacter>, FAsdaStealthState>& KV : States)
	{
		if (AAsdaPlayerCharacter* Ch = KV.Key.Get())
		{
			Ch->SetSkillStealth(false);
			Ch->SetStealthVisual(1.f, true);
			if (KV.Value.bHiddenByUs) { ApplyRender(Ch, KV.Value, true); }
			Ch->Tags.Remove(FName(kAsdaStealthHiddenTagName));
		}
	}
	States.Reset();
	DebugInvisible.Reset(); DebugParty.Reset(); DebugMate.Reset(); DebugDetectorRange = 0.f;
	UE_LOG(LogAsdaStealth, Log, TEXT("[невидимость] сброс сессии (%d)"), (int32)Reason);
}

bool UAsdaStealthSubsystem::GetStateFor(const AAsdaPlayerCharacter* Ch, float& OutAlpha, int32& OutPhase, bool& OutRender) const
{
	const FAsdaStealthState* S = States.Find(TWeakObjectPtr<AAsdaPlayerCharacter>(const_cast<AAsdaPlayerCharacter*>(Ch)));
	if (!S) { return false; }
	OutAlpha = S->Alpha; OutPhase = S->Phase; OutRender = S->bRender;
	return true;
}

float UAsdaStealthSubsystem::GetPhaseMsFor(const AAsdaPlayerCharacter* Ch) const
{
	const FAsdaStealthState* S = States.Find(TWeakObjectPtr<AAsdaPlayerCharacter>(const_cast<AAsdaPlayerCharacter*>(Ch)));
	return S ? S->PhaseMs : -1.f;
}

void UAsdaStealthSubsystem::DebugForceInvisible(const AAsdaPlayerCharacter* Ch, int32 Force)
{
	const TWeakObjectPtr<AAsdaPlayerCharacter> K(const_cast<AAsdaPlayerCharacter*>(Ch));
	if (Force < 0) { DebugInvisible.Remove(K); } else { DebugInvisible.Add(K, Force ? 1 : 0); }
}

void UAsdaStealthSubsystem::DebugForceRelation(const AAsdaPlayerCharacter* Ch, int32 bParty, int32 bMate)
{
	const TWeakObjectPtr<AAsdaPlayerCharacter> K(const_cast<AAsdaPlayerCharacter*>(Ch));
	if (bParty < 0) { DebugParty.Remove(K); } else { DebugParty.Add(K, bParty); }
	if (bMate < 0)  { DebugMate.Remove(K); }  else { DebugMate.Add(K, bMate); }
}

bool UAsdaStealthSubsystem::IsInvisible(const AAsdaPlayerCharacter* Ch, int32 UserIdx) const
{
	if (const int32* F = DebugInvisible.Find(TWeakObjectPtr<AAsdaPlayerCharacter>(const_cast<AAsdaPlayerCharacter*>(Ch)))) { return *F != 0; }
	const UAsdaEffectsSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	if (!Fx || UserIdx < 0) { return false; }
	// Эталон `skillHasBuffType112or186@0xA386F0`: listContainsByTypeId(+638, 112, 0) || (…, 186, 0).
	return Fx->FindByTypeSub(UserIdx, 112, 0) != nullptr || Fx->FindByTypeSub(UserIdx, 186, 0) != nullptr;
}

float UAsdaStealthSubsystem::DetectorRange(int32 OtherLevel) const
{
	if (DebugDetectorRange > 0.f) { return DebugDetectorRange; }
	const UGameInstance* GI = GetGameInstance();
	const UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const UAsdaSkillDatabase* Db = GI ? GI->GetSubsystem<UAsdaSkillDatabase>() : nullptr;
	if (!Fx || !Net || !Db) { return 0.f; }
	// Эталон `skillCheckRangeVsDist@0xA38750`: запись 154 своего персонажа → навык записи (+2) → AoeRadius (+0x37) × 100.
	const FAsdaEffectEntry* E = Fx->FindByTypeSub(Fx->SelfUserIdx(), 154, 0);
	if (!E) { return 0.f; }
	const FAsdaSkillDef* Def = Db->Get(E->IdFlag);
	if (!Def) { return 0.f; }
	const int32 Base = Def->AoeRadius * 100;
	int32 Range = Base;
	const int32 N = (int32)Net->SelfLevel - OtherLevel;   // свой уровень (+0x43A) − уровень цели
	if (N > 0)      { Range += FMath::Min(N, 5) * (20 * Base / 100); }
	else if (N < 0) { Range -= FMath::Min(-N, 5) * (10 * Base / 100); }
	return (float)Range;
}

void UAsdaStealthSubsystem::ApplyRender(AAsdaPlayerCharacter* Ch, FAsdaStealthState& S, bool bRender)
{
	S.bRender = bRender;
	// ★ Ревизия 24.10 (К1): метка «скрыт невидимостью» — по ней гасят себя эффекты, следующие за персонажем через FollowActor
	//   (attach-3/5/6/9/10/12), появившиеся ПОСЛЕ скрытия, и удочка рыбалки (`AsdaHostHiddenByTag`).
	static const FName kStTag(kAsdaStealthHiddenTagName);
	if (!bRender) { Ch->Tags.AddUnique(kStTag); } else { Ch->Tags.Remove(kStTag); }
	static const FName kGmTag(kAsdaGmHiddenTagName);
	const bool bGmHidden = Ch->ActorHasTag(kGmTag) && !Ch->IsGmHideTranslucent();   // соседа GM-скрытие прячет целиком
	const bool bHidden = !bRender || bGmHidden;
	Ch->SetActorHiddenInGame(bHidden);
	AsdaSetAttachedFxHidden(Ch, bHidden);
	if (UAsdaWorldSubsystem* Ws = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		if (AAsdaSoulGuardianActor* G = Ws->FindSoulGuardianByOwner(Ch)) { G->SetActorHiddenInGame(bHidden); AsdaSetAttachedFxHidden(G, bHidden); }
	}
	S.bHiddenByUs = !bRender;
	UE_LOG(LogAsdaStealth, Log, TEXT("[невидимость] %s: %s"), *Ch->GetName(), bHidden ? TEXT("не рисуется") : TEXT("рисуется"));
}

void UAsdaStealthSubsystem::StepCharacter(AAsdaPlayerCharacter* Ch, bool bSelf, int32 Uidx, int32 CharId, float DtMs, const AAsdaPlayerCharacter* Self)
{
	using namespace AsdaStealth;
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* Ws = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const bool bInv = IsInvisible(Ch, Uidx);
	Ch->SetSkillStealth(bInv);   // анимация: присед донора / walk01 эталона

	FAsdaStealthState& S = States.FindOrAdd(TWeakObjectPtr<AAsdaPlayerCharacter>(Ch));
	if (!AsdaStealthVisualOn())
	{
		// Ключ отката: вид как до 24.10 — α 1, рисуется; вернуть скрытых нами.
		S.bInvisible = bInv; S.Alpha = 1.f; S.Phase = 0; S.PhaseMs = 0.f;
		Ch->SetStealthVisual(1.f, true);
		if (S.bHiddenByUs) { ApplyRender(Ch, S, true); }
		return;
	}

	// ── переходы состояния (обработчики начала/снятия) ──
	if (bInv && !S.bInvisible)
	{
		// sub_9B1890 / charClearTargetAndOpenBag: шла фаза проявления — α := 1, фаза снята, часы 0, α := 0.5; затем 0x10000.
		if (S.Phase == 2) { S.Alpha = kRemoveAlpha; }
		S.Phase = 1; S.PhaseMs = 0.f;
	}
	else if (!bInv && S.bInvisible)
	{
		// cChar_removeSkillEffect case 112/186: шло затухание — снять, α := 0.5; затем 0x20000.
		if (S.Phase == 1) { S.Alpha = kRemoveAlpha; }
		S.Phase = 2; S.PhaseMs = 0.f;
	}
	S.bInvisible = bInv;
	bool bRender = S.bRender;

	// ── CharManager_UpdateAll (только чужие) ──
	bool bParty = false, bMate = false;
	if (!bSelf)
	{
		if (const int32* P = DebugParty.Find(TWeakObjectPtr<AAsdaPlayerCharacter>(Ch))) { bParty = *P != 0; }
		else if (const UAsdaPartySubsystem* Party = GI ? GI->GetSubsystem<UAsdaPartySubsystem>() : nullptr)
		{
			// ★ Ревизия 24.10 (ВАЖНО-3): `Party_FindMemberByUserIdx@0x9CD090` — миснейм: сравнивает member+4 = CharId с entity[263] = charId.
			if (Party->IsInParty() && CharId > 0)
			{
				for (const FAsdaPartyMember& Mb : Party->GetMembers()) { if (Mb.CharId == CharId) { bParty = true; break; } }
			}
		}
		if (const int32* M = DebugMate.Find(TWeakObjectPtr<AAsdaPlayerCharacter>(Ch))) { bMate = *M != 0; }
		else if (const UAsdaSoulmateSubsystem* Sm = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr)
		{
			bMate = Sm->GetFriendSlot() > 0 && CharId > 0 && Sm->GetMateCharId() == CharId;   // gap_878[47] > 0 && charId == gap_8B4
		}
		// ★ Ревизия 24.10 (ВАЖНО-2): game.c:55656-55664 — α == 0 и наблюдатель под GM-скрытием (`sub_93B218` → бит 1 +259)
		//   → α := 0.5: скрытый оператор видит невидимок. Иначе такой персонаж не рисуется (дальше решит ветка невидимости).
		if (S.Alpha <= 0.f && Self && Self->IsGmHideTranslucent()) { S.Alpha = kGmViewerAlpha; bRender = true; }
		if (bInv)
		{
			float T = 0.f;
			if (bParty) { T = kPartyAlpha; }
			if (bMate)  { T = kMateAlpha; }
			int32 OtherLevel = 0;
			if (Ws && Uidx >= 0) { FString Nm; int32 Cls = 0, Clan = 0; Ws->GetNetPlayerInfo(Uidx, Nm, Cls, OtherLevel, Clan); }
			const float Range = Self ? DetectorRange(OtherLevel) : 0.f;
			bool bInRange = false;
			if (Range > 0.f && Self)
			{
				const FVector2D A = Self->GetGamePosXZ(), B = Ch->GetGamePosXZ();
				const float D = (CVarAsdaStealthDetectRealDist.GetValueOnGameThread() != 0)
					? (float)FVector2D::Distance(A, B) : FMath::Abs((float)A.Size() - (float)B.Size());
				bInRange = Range >= D;
			}
			if (Range > 0.f && bInRange)
			{
				if (S.Alpha < kDetectAlpha) { S.Alpha = FMath::Min(S.Alpha + kDetectRate * DtMs, kDetectAlpha); }
				bRender = true;
			}
			else
			{
				const float Rate = (Range > 0.f) ? kDetectRate : kOthersRate;
				if (S.Alpha > 0.f)
				{
					S.Alpha -= Rate * DtMs;
					// Эталон: с детектором вне радиуса «ниже цели → 0» (следующий кадр поднимет до цели), без — «ниже цели → цель».
					if (S.Alpha < T) { S.Alpha = (Range > 0.f) ? 0.f : T; }
					bRender = true;
				}
				else if (S.Alpha >= T) { S.Alpha = 0.f; bRender = false; }
				else { S.Alpha = T; bRender = true; }
			}
		}
		else
		{
			S.Alpha = 1.f;   // не невидимый — α 1 сразу (CharManager_UpdateAll)
		}
	}

	// ── charEntityUpdateTick: фазы 0x10000 / 0x20000 (и свой, и чужие) ──
	if (S.Phase == 1)
	{
		S.PhaseMs += DtMs;
		const float Floor = (bSelf || bParty || bMate) ? kSelfFloor : 0.f;
		S.Alpha -= kFadeOutRate * DtMs;
		if (S.Alpha < Floor) { S.Alpha = Floor; }
		if (S.PhaseMs >= kFadeOutMs)
		{
			S.Phase = 0;
			if (!bSelf && Floor <= 0.f) { S.Alpha = 0.f; bRender = false; }   // pChar[7] = 0 — не рисуется
		}
	}
	else if (S.Phase == 2)
	{
		bRender = true;
		S.PhaseMs += DtMs;
		S.Alpha = FMath::Min(1.f, S.Alpha + kFadeInRate * DtMs);
		if (S.PhaseMs >= kFadeInMs) { S.Alpha = 1.f; S.Phase = 0; }
	}
	else if (!bInv && bSelf)
	{
		S.Alpha = 1.f;
	}
	if (bSelf) { bRender = true; }   // свой персонаж не пропадает (эталон: ветки «не рисуется» — только у чужих)
	S.Alpha = FMath::Clamp(S.Alpha, 0.f, 1.f);

	Ch->SetStealthVisual(S.Alpha, bRender);
	if (bRender != S.bRender || S.bHiddenByUs != !bRender) { ApplyRender(Ch, S, bRender); }
}

void UAsdaStealthSubsystem::Tick(float DeltaTime)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* Ws = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!Ws || !GI->GetWorld()) { return; }
	const float DtMs = FMath::Clamp(DeltaTime, 0.f, 0.25f) * 1000.f;
	const AAsdaPlayerCharacter* Self = IsValid(Ws->LocalPlayerActor) ? Cast<AAsdaPlayerCharacter>(Ws->LocalPlayerActor.Get()) : nullptr;
	bool bSelfStepped = false;
	// ★ Ревизия 24.10: обход по реестру с userIdx (без обратного поиска актор → userIdx на каждого — было O(N²)).
	Ws->ForEachPlayerActorWithIds([this, DtMs, Self, &bSelfStepped](int32 Uidx, int32 CharId, AActor* A, bool bSelf)
	{
		if (AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(A))
		{
			StepCharacter(Ch, bSelf || Ch == Self, Uidx, CharId, DtMs, Self);
			if (Ch == Self) { bSelfStepped = true; }
		}
	});
	// Свой при неизвестном номере сессии (офлайн до 4003) — отдельным шагом (каждый кадр).
	if (Self && !bSelfStepped) { StepCharacter(const_cast<AAsdaPlayerCharacter*>(Self), true, Ws->FindUserIdxByActor(Self), 0, DtMs, Self); }
	// Цель-невидимка снимается (эталон game.c:10153 — каждый кадр, без учёта детектора).
	if (CVarAsdaStealthClearTarget.GetValueOnGameThread() != 0)
	{
		if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GI->GetFirstLocalPlayerController(GI->GetWorld())))
		{
			if (const AAsdaPlayerCharacter* T = Cast<AAsdaPlayerCharacter>(PC->TargetActor.Get()))
			{
				if (T != Self && T->IsSkillStealth())
				{
					UE_LOG(LogAsdaStealth, Log, TEXT("[невидимость] цель %s невидима — выбор снят"), *T->GetName());
					PC->ClearTarget();
				}
			}
		}
	}
	// Ушедшие персонажи.
	for (auto It = States.CreateIterator(); It; ++It) { if (!It->Key.IsValid()) { It.RemoveCurrent(); } }
}
