// Copyright (c) 2026 @sitxovski (Discord)
// Реализация UAsdaSoulSlotSubsystem — разбор в заголовке.
#include "UI/AsdaSoulSlotSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"
#include "AsdaEffectActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaLocIds.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaNoticeColors.h"
#include "UI/AsdaSoulStoneSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaSoulBaseActor.h"
#include "World/AsdaWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoulSlot, Log, All);

// ★ 06.11.2026 [ОРИГИНАЛ] Запуск и визуальное завершение слотов Дружбы: обработчики 16 слотов, постоянные эффекты слота 3, эффект и звук
//   конца слота 0, окно 8139, плашка 8128. 1 — как эталон; 0 — прежний порт: 5229 и снапшот только кладут запись, конец даёт только нотис 8155.
static ASDA_DEV_CVAR(int32, CVarSoulSlotRun, TEXT("asda.Soul.SlotRun"), 1,
	TEXT("[ОРИГИНАЛ] Запуск и завершение слотов Дружбы: 1 = как эталон; 0 = прежний порт, эффектов, звуков и духа нет."),
	ECVF_Default);

// ★ 06.11.2026 [ОРИГИНАЛ] Дух слота 0 (`g_pSoulBaseMgr`). 1 — как эталон; 0 — духа нет, остальное по `asda.Soul.SlotRun`.
static ASDA_DEV_CVAR(int32, CVarSoulSpirit, TEXT("asda.Soul.Spirit"), 1,
	TEXT("[ОРИГИНАЛ] Дух слота Дружбы 0: 1 = как эталон; 0 = духа нет."),
	ECVF_Default);

bool AsdaSoulSlotRunOn() { return CVarSoulSlotRun.GetValueOnGameThread() != 0; }

namespace
{
	// Подъём режимов 5 и 7 эталона: +15 по игровой Y.
	constexpr float kTrackRiseZ = 15.f;
	// Подъём эффекта конца слота 0 над духом (`CCharFx_SoulRite_EndSlot`: `soul + 524` + 35.0).
	constexpr float kEndFxRiseZ = 35.f;
	// Сколько ждать актора носителя, с.
	constexpr double kHostWaitSec = 10.0;
	// Кости эталона.
	const FName kBone4(TEXT("4"));
	const FName kBone8End(TEXT("8_End"));
	const FName kBone8EndB(TEXT("_8_End"));
}

void UAsdaSoulSlotSubsystem::Note(const FString& What)
{
#if !UE_BUILD_SHIPPING
	if (ShowLog.Num() < 1024) { ShowLog.Add(What); }
#endif
}

int32 UAsdaSoulSlotSubsystem::SelfIdx() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	return Fx ? Fx->SelfUserIdx() : INDEX_NONE;
}

AAsdaEffectActor* UAsdaSoulSlotSubsystem::SpawnFx(AActor* Host, USkeletalMeshComponent* Skel, const TCHAR* Jeb, int32 Attach, FName Bone, bool bPersistent)
{
	UWorld* W = Host ? Host->GetWorld() : nullptr;
	if (!W) { return nullptr; }
	EAsdaFxAttachMode Mode = EAsdaFxAttachMode::CharPos;
	float RiseZ = 0.f;
	AAsdaEffectActor::EtalonAttach(Attach, 0, kTrackRiseZ, Mode, RiseZ);
	const FVector At = (Mode == EAsdaFxAttachMode::BonePos && Skel)
		? Skel->GetSocketLocation(Bone) + FVector(0.f, 0.f, RiseZ)
		: Host->GetActorLocation() + FVector(0.f, 0.f, RiseZ);
	AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnByJebName(W, Jeb, At, FRotator::ZeroRotator, Host);
	if (!Fx)
	{
		UE_LOG(LogAsdaSoulSlot, Warning, TEXT("[слоты Дружбы] эффект %s не собран"), Jeb);
		return nullptr;
	}
	Fx->ConfigureBoneAttach(Host, Skel, Bone, Mode, FVector(0.f, 0.f, RiseZ));
	if (!bPersistent) { Fx->SetLifeSpan(Fx->NaturalLifeSpan(0.5f, 1.f, 30.f)); }
	Note(FString::Printf(TEXT("fx:%s"), Jeb));
	return Fx;
}

void UAsdaSoulSlotSubsystem::PlaySnd(AActor* Host, const TCHAR* Name)
{
	Note(FString::Printf(TEXT("snd:%s"), Name));
	UGameInstance* GI = GetGameInstance();
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound3DAs(FName(Name), Host->GetActorLocation(), EAsdaSoundClass::Magic, Host);
	}
}

void UAsdaSoulSlotSubsystem::SpawnSoul(int32 UserIdx, FAsdaSoulSlotChar& C, AActor* Host, bool bBirthFlash)
{
	if (CVarSoulSpirit.GetValueOnGameThread() == 0 || C.Soul.IsValid()) { return; }
	UWorld* W = Host->GetWorld();
	if (!W) { return; }
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAsdaSoulBaseActor* Soul = W->SpawnActor<AAsdaSoulBaseActor>(AAsdaSoulBaseActor::StaticClass(), Host->GetActorTransform(), P);
	if (!Soul) { return; }
	Soul->InitSoul(Host, UserIdx, bBirthFlash);
	C.Soul = Soul;
	Note(TEXT("soul+"));
	UE_LOG(LogAsdaSoulSlot, Log, TEXT("[слоты Дружбы] дух создан у userIdx=%d"), UserIdx);
}

void UAsdaSoulSlotSubsystem::DestroySoul(FAsdaSoulSlotChar& C)
{
	if (AAsdaSoulBaseActor* Soul = C.Soul.Get())
	{
		Soul->Destroy();
		Note(TEXT("soul-"));
	}
	C.Soul.Reset();
}

void UAsdaSoulSlotSubsystem::StopSlotFx(FAsdaSoulSlotChar& C, int32 Slot)
{
	if (AAsdaEffectActor* A = C.Fx0[Slot].Get()) { A->Destroy(); }
	if (AAsdaEffectActor* A = C.Fx1[Slot].Get()) { A->Destroy(); }
	C.Fx0[Slot].Reset();
	C.Fx1[Slot].Reset();
}

bool UAsdaSoulSlotSubsystem::Dispatch(int32 UserIdx, FAsdaSoulSlotChar& C, int32 Slot)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	AActor* Host = WS ? WS->ActorByUserIdx(UserIdx) : nullptr;
	if (!Host) { return false; }
	const AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(Host);
	USkeletalMeshComponent* Skel = Ch ? Ch->BaseSkel.Get() : nullptr;
	const auto HasBone = [Skel](FName Bone) { return Skel && Skel->DoesSocketExist(Bone); };

	switch (Slot)
	{
	case 0:   // `SkillEffect_OnAnimEventSpawnSoulRecall@0xAD3D20`
		if (C.State[0] != 2)
		{
			SpawnSoul(UserIdx, C, Host, /*bBirthFlash=*/true);
			PlaySnd(Host, TEXT("S_c_SMSoulRecal02"));
		}
		C.State[0] = 2;
		break;
	case 1:   // `SkillEffect_OnAnimEventSpawnAttBombA@0xAD3E80`
	case 2:   // `SkillEffect_OnAnimEventSpawnAttBomb@0xAD4420`
		for (const FName Bone : { kBone8End, kBone8EndB })
		{
			if (!HasBone(Bone)) { continue; }
			SpawnFx(Host, Skel, Slot == 1 ? TEXT("eff_1_char_99_attbomh-a_02") : TEXT("eff_1_char_99_attbomh-a_04"), 7, Bone, false);
			if (Slot == 2) { SpawnFx(Host, Skel, TEXT("eff_1_char_99_attbomg-a_03"), 5, NAME_None, false); }
			C.State[Slot] = 2;
		}
		PlaySnd(Host, Slot == 1 ? TEXT("S_c_OddIndexSM001") : TEXT("S_c_OddIndexSM002b"));
		break;
	case 3:   // `SkillEffect_OnAnimEventSpawnEvasion@0xAD4710`
		SpawnFx(Host, Skel, TEXT("eff_1_char_99_evasiong-a_02"), 5, NAME_None, false);
		StopSlotFx(C, 3);   // повтор без смены карты не должен оставить вторую пару постоянных эффектов
		C.Fx0[3] = SpawnFx(Host, Skel, TEXT("eff_1_char_99_evasionc-a_03"), 5, NAME_None, true);
		C.Fx1[3] = SpawnFx(Host, Skel, TEXT("eff_1_char_99_evasionc-a_04"), 5, NAME_None, true);
		C.State[3] = 2;
		PlaySnd(Host, TEXT("S_c_OddIndexSM003a"));
		PlaySnd(Host, TEXT("S_c_OddIndexSM003b"));
		break;
	case 4:   // `sub_AD4960`
		C.State[4] = 2;
		break;
	case 5:   // `SkillEffect_OnAnimEventSpawnGuard@0xAD4990`
	case 6:   // `SkillEffect_OnAnimEventSpawnPercentDamage@0xAD4B80`
		if (!HasBone(kBone4)) { break; }
		SpawnFx(Host, Skel, Slot == 5 ? TEXT("eff_1_char_99_skigurdg-a_02") : TEXT("eff_1_char_99_perdmgg-a_02"), 5, NAME_None, false);
		SpawnFx(Host, Skel, Slot == 5 ? TEXT("eff_1_char_99_skigurdu-a_03") : TEXT("eff_1_char_99_perdmgu-a_03"), 7, kBone4, false);
		PlaySnd(Host, Slot == 5 ? TEXT("S_c_OddIndexSM005") : TEXT("S_c_OddIndexSM006b"));
		C.State[Slot] = 2;
		break;
	case 7:   // `CCharFx_RedMgu_Cast@0xAD4D70`
		SpawnFx(Host, Skel, TEXT("eff_1_char_99_redmgu-a_01"), 5, NAME_None, false);
		C.State[7] = 2;
		PlaySnd(Host, TEXT("S_c_OddIndexSM007a"));
		break;
	case 8:   // `CCharFx_SkigurdC_Cast@0xAD4E60`
		SpawnFx(Host, Skel, TEXT("eff_1_char_99_skigurdc-a_04"), 5, NAME_None, false);
		C.State[8] = 2;
		PlaySnd(Host, TEXT("S_c_OddIndexSM008"));
		break;
	case 9:   // `CharSkill_SoulRecallCast@0xAD4F50`
		if (UserIdx == SelfIdx())
		{
			AsdaNotice::PlankLoc(GI, EAsdaMsgCategory::System, 8128, AsdaNoticeColor::Gold, 10000.f, 5229);
			Note(TEXT("notice:8128"));
		}
		if (!HasBone(kBone4)) { break; }
		SpawnFx(Host, Skel, TEXT("eff_1_char_99_soulritec-a_01"), 5, NAME_None, false);
		PlaySnd(Host, TEXT("S_c_SMSoulRecal01"));
		C.State[9] = 2;
		break;
	case 10:  // `charFxSpawnDamchgg@0xAD5110`
		SpawnFx(Host, Skel, TEXT("eff_1_char_99_damchgg-a_03"), 5, NAME_None, false);
		C.State[10] = 2;
		break;
	case 11: case 12: case 13: case 14: case 15:   // `skillEffectStopRecallSound*`
		if (C.State[Slot] != 2) { PlaySnd(Host, TEXT("S_c_SMSoulRecal02")); }
		C.State[Slot] = 2;
		break;
	default:  // `sub_AD3D00`: пусто
		break;
	}
	return true;
}

void UAsdaSoulSlotSubsystem::ActivateSlot(int32 UserIdx, int32 Slot)
{
	if (!AsdaSoulSlotRunOn() || Slot < 0 || Slot >= 30 || UserIdx < 0) { return; }
	FAsdaSoulSlotChar& C = Chars.FindOrAdd(UserIdx);
	if (C.State[Slot] == 2) { return; }
	C.State[Slot] = 1;
	// Часть `invSlotActivateAndDispatch` для слота 0: флаги окна сумки +637 и +640, окно 8139 своему.
	if (Slot == 0)
	{
		if (UAsdaSoulStoneSubsystem* Stone = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoulStoneSubsystem>() : nullptr) { Stone->OnSlot0Started(UserIdx == SelfIdx()); }
	}
	if (Slot == 0 && UserIdx == SelfIdx())
	{
		AsdaNotice::MsgBoxLoc(GetGameInstance(), EAsdaMsgCategory::System, 8139, AsdaLoc::kBtnOk, /*EtalonNoticeId*/-1);
		Note(TEXT("notice:8139"));
	}
	if (Dispatch(UserIdx, C, Slot))
	{
		C.Pending.Remove(Slot);
		return;
	}
	if (C.Pending.Num() == 0) { C.PendingSinceS = FPlatformTime::Seconds(); }
	C.Pending.Add(Slot);
}

void UAsdaSoulSlotSubsystem::EndSlot(int32 UserIdx, int32 Slot)
{
	FAsdaSoulSlotChar* C = Chars.Find(UserIdx);
	if (!C || Slot < 0 || Slot >= 30) { return; }
	C->Pending.Remove(Slot);
	if (C->State[Slot] != 2)
	{
		C->State[Slot] = 3;
		C->Fx0[Slot].Reset();
		C->Fx1[Slot].Reset();
		return;
	}
	if (Slot == 0)
	{
		const UGameInstance* GI = GetGameInstance();
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		AActor* Host = WS ? WS->ActorByUserIdx(UserIdx) : nullptr;
		AAsdaSoulBaseActor* Soul = C->Soul.Get();
		if (Soul && Host)
		{
			const FVector At = Soul->GetActorLocation() + FVector(0.f, 0.f, kEndFxRiseZ);
			if (AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnByJebName(Soul->GetWorld(), TEXT("eff_1_char_99_soulritec-a_04"), At, FRotator::ZeroRotator, Host))
			{
				Fx->SetLifeSpan(Fx->NaturalLifeSpan(0.5f, 1.f, 30.f));
				Note(TEXT("fx:eff_1_char_99_soulritec-a_04"));
			}
			PlaySnd(Host, TEXT("S_c_SMSoulRecal03"));
		}
		DestroySoul(*C);
		// `uiDlg_ClearControlGroup`: только у своего персонажа, как нотис 8155 рядом.
		if (UserIdx == SelfIdx() && AsdaSoulStoneOn())
		{
			if (UAsdaSoulStoneSubsystem* Stone = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoulStoneSubsystem>() : nullptr) { Stone->ClearGhost(); }
		}
	}
	C->State[Slot] = 3;
	StopSlotFx(*C, Slot);
}

void UAsdaSoulSlotSubsystem::EndAllSlots(int32 UserIdx)
{
	for (int32 i = 0; i < 30; ++i) { EndSlot(UserIdx, i); }
}

void UAsdaSoulSlotSubsystem::ForgetChar(int32 UserIdx)
{
	FAsdaSoulSlotChar* C = Chars.Find(UserIdx);
	if (!C) { return; }
	DestroySoul(*C);
	for (int32 i = 0; i < 30; ++i) { StopSlotFx(*C, i); }
	Chars.Remove(UserIdx);
}

void UAsdaSoulSlotSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	const int32 Self = SelfIdx();
	bReplayPending = false;
	for (TPair<int32, FAsdaSoulSlotChar>& It : Chars)
	{
		DestroySoul(It.Value);
		for (int32 i = 0; i < 30; ++i) { StopSlotFx(It.Value, i); }
		It.Value.Pending.Reset();
	}
	if (AsdaIsFullSessionReset(Reason))
	{
		Chars.Reset();
		CarriedSelfIdx = INDEX_NONE;
		return;
	}
	// Смена карты: акторы ушли с уровнем, соседи прошлой карты забываются, свои состояния остаются (разбор в заголовке).
	for (auto It = Chars.CreateIterator(); It; ++It)
	{
		if (It.Key() != Self) { It.RemoveCurrent(); }
	}
	CarriedSelfIdx = Chars.Contains(Self) ? Self : INDEX_NONE;
}

void UAsdaSoulSlotSubsystem::OnSelfSpawnCarry(int32 NewSelf)
{
	if (CarriedSelfIdx == INDEX_NONE || NewSelf < 0) { return; }
	if (NewSelf != CarriedSelfIdx)
	{
		FAsdaSoulSlotChar Carried;
		if (Chars.RemoveAndCopyValue(CarriedSelfIdx, Carried)) { Chars.Add(NewSelf, MoveTemp(Carried)); }
	}
	CarriedSelfIdx = INDEX_NONE;
	bReplayPending = Chars.Contains(NewSelf);
}

void UAsdaSoulSlotSubsystem::ReplayAfterMap(int32 Self, FAsdaSoulSlotChar& C)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const UAsdaEffectsSubsystem* Eff = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	AActor* Host = WS ? WS->ActorByUserIdx(Self) : nullptr;
	if (!Host || !Eff) { return; }
	bReplayPending = false;
	// `network.c:40567`: при записи слота 0 дух создаётся заново, вспышки нет.
	if (Eff->FindByTypeSub(Self, 0, 1) && C.State[0] == 2) { SpawnSoul(Self, C, Host, /*bBirthFlash=*/false); }
	// `CCharacter_StopAllActiveBuffActions@0xA2C480`: обработчик каждой записи типа 1.
	TArray<int32> Slots;
	for (const FAsdaEffectEntry& E : Eff->GetList(Self))
	{
		if (E.Type == 1 && E.Id >= 0 && E.Id < 30) { Slots.Add(E.Id); }
	}
	for (const int32 Slot : Slots) { Dispatch(Self, C, Slot); }
	UE_LOG(LogAsdaSoulSlot, Log, TEXT("[слоты Дружбы] смена карты: у userIdx=%d повторены обработчики записей: %d"), Self, Slots.Num());
}

void UAsdaSoulSlotSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.SoulSlot");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	if (bReplayPending)
	{
		const int32 Self = SelfIdx();
		if (FAsdaSoulSlotChar* C = Chars.Find(Self)) { ReplayAfterMap(Self, *C); }
		else { bReplayPending = false; }
	}
	const double Now = FPlatformTime::Seconds();
	for (TPair<int32, FAsdaSoulSlotChar>& It : Chars)
	{
		FAsdaSoulSlotChar& C = It.Value;
		if (C.Pending.Num() == 0) { continue; }
		if (Now - C.PendingSinceS > kHostWaitSec)
		{
			for (const int32 Slot : C.Pending) { C.State[Slot] = 3; }
			C.Pending.Reset();
			continue;
		}
		for (auto P = C.Pending.CreateIterator(); P; ++P)
		{
			if (Dispatch(It.Key, C, *P)) { P.RemoveCurrent(); }
		}
	}
}

int32 UAsdaSoulSlotSubsystem::GetSlotState(int32 UserIdx, int32 Slot) const
{
	const FAsdaSoulSlotChar* C = Chars.Find(UserIdx);
	return (C && Slot >= 0 && Slot < 30) ? C->State[Slot] : 3;
}

AAsdaSoulBaseActor* UAsdaSoulSlotSubsystem::FindSoul(int32 UserIdx) const
{
	const FAsdaSoulSlotChar* C = Chars.Find(UserIdx);
	return C ? C->Soul.Get() : nullptr;
}

bool UAsdaSoulSlotSubsystem::DebugPersistentFxAlive(int32 UserIdx, int32 Slot, int32 Which) const
{
	const FAsdaSoulSlotChar* C = Chars.Find(UserIdx);
	if (!C || Slot < 0 || Slot >= 30) { return false; }
	return Which == 0 ? C->Fx0[Slot].IsValid() : C->Fx1[Slot].IsValid();
}
