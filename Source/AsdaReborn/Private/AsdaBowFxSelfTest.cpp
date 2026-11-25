// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ЯД/КРОВЬ/ЛОВУШКИ И ДОПУСТИМЫЕ ЦЕЛИ НАВЫКОВ» `asda.Skill.SelfTestBowFx` (26.10.2026, баг игроков bowfx). [ПОРТ]
//   Только офлайн (`-AsdaNoNet`): входящие — штатный приём `DebugFeedPacket` (= `HandlePacket` сокета), исходящие — перехват
//   (`AsdaSelfTestKit::ArmCapture` + `bStandCaptureSkillUse` — настоящий отправитель 5256). Свой персонаж — `Asda.World.FakeSelfSpawn`,
//   соседи — `DebugSpawnFakePeer`, мобы — запись 4013. Путь игрока: цель выбирается `SetTarget`, навык — `DispatchByKind` (тот же
//   вход, что у быстрой панели), значок — виджет окна цели (`UAsdaTargetFrameSubsystem`), цифры — живые надписи `UAsdaFloatingText`.
//   Сценарии (разбор — `Reports/skill_targets/RULES.md`):
//     1) 6079 «яд наложен» (кастер — я): состояние 70 на мобе → значок под полосой HP цели и эффект; 3 тика по 20 → HP 100 → 40,
//        цифры над мобом; слот гаснет по исчерпании;
//     2) контрольная пара: `asda.Dot.Slots 0` — 6079 ничего не делает (прежний порт);
//     3) моб мёртв по серверу при остатке урона — заявка 8226 `[свой userNum][renderId]`;
//     4) свой удар 5270 «Кровавая песнь» 713 (бит 0x02) — состояние 68 на мобе на моменте удара, ДО 6079;
//     5) ловушка: 4102 навык 185 «Ледяная ловушка» по мобу → состояние 0 (а не 185), анимация стоит, ход 4015 не двигает;
//        снятие 4017 — моб снова идёт;
//     6) «Ловчий выстрел» 710 своим ударом 5270 → состояние 2 «Ловушка», ход 4015 не двигает моба;
//     7) враждебный 710 по мирному соседу вне PvP — заявки 5256 нет; контроль `asda.Skill.TargetRelation 0` — заявка уходит;
//     8) свободный бой (4206 режим 2): 710 по соседу — заявка уходит;
//     9) дуэль (4206 режим 1, соперник P): по P — заявка, по Q — нет;
//    10) навык «на игрока» 533 по мирному соседу вне PvP — заявка уходит (решает сервер, как у эталона);
//    11) смешанная площадь 705 в режиме 2: согруппник Q в записи не попадает, P попадает;
//    12–13) ★ копьё: «Удар наотмашь» 504 → кровотечение 68, «Жало» 505 → болезнь 72 — живые тела 6079/4017, тики и значок;
//    14) выборка аудита `Reports/skill_targets/DOT_AUDIT.md` (502, 901, 908 через 6079; 503, 904 своим ударом).
//   Своё состояние (номер, PvP-режим, ключи, соседи, мобы) возвращается в итоге.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"   // ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"                   // запись 4013 (спаун моба)
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaEffectsSubsystem.h"           // списки состояний, слоты урона по времени
#include "UI/AsdaSkillUseSubsystem.h"          // DispatchByKind — вход навыка игрока
#include "UI/AsdaTargetFrameSubsystem.h"       // значок под полосой HP цели
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaFloatingTextSubsystem.h"
#include "World/AsdaCoord.h"
#include "Npc/AsdaMobActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBowFxTest, Log, All);

namespace AsdaBowFxTestLocal
{
	const TCHAR* const kCmd = TEXT("asda.Skill.SelfTestBowFx");   // имя команды
	constexpr int32 kSelfFallback = 4322;    // свой userNum, если не известен
	constexpr int32 kMobType = 1;            // волк alpen (как у прочих самотестов)
	constexpr int32 kSpdDenom = 250;         // знаменатель скорости 4013/4015
	constexpr int32 kRid1 = 30881, kRid2 = 30882, kRid3 = 30883, kRid4 = 30884, kRid5 = 30885;   // мобы сценариев
	constexpr int32 kP = 7821, kQ = 7822;    // соседи: P — мирный/соперник, Q — второй

	struct FOut { uint16 Op = 0; TArray<uint8> Payload; TCHAR Env = 0; };   // перехваченный исходящий

	constexpr double kReadyMaxS = 3.0;       // предел ожидания условия готовности шага (дольше — провал)

	struct FStep
	{
		FString Name; float TimeoutSec = 10.f;
		TFunction<void()> Start; TFunction<bool()> Poll; TFunction<void()> Check;
		// Условие готовности: пока ложно, исполнитель не зовёт `Start` (и не считает время шага).
		TFunction<bool()> Ready;
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UAsdaNetSubsystem> Net;
		TArray<FStep> Steps;
		int32 StepIdx = 0; bool bStepStarted = false; double StepStartS = 0.0;
		double ReadyWaitS = 0.0;   // начало ожидания готовности текущего шага (0 — не ждём)
		FTimerHandle Tick; bool bQuit = false;
		FAsdaSelfTestRunGate Gate{ kCmd, false };
		int32 Ok = 0, Fail = 0;
		uint32 Gen = 0; TArray<FOut> Outs; int32 OutFrom = 0;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		bool bWasInGame = false, bWasStandSkill = false;
		int32 Self = -1; FIntPoint HomeTile = FIntPoint::ZeroValue;
		TArray<int32> Peers, Mobs;
		int32 FloatMax = 0, FloatBase = 0;   // живые надписи: максимум за шаг и база на старте
		FVector MobPos0 = FVector::ZeroVector;
		bool bSawState = false;

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaEffectsSubsystem* Fx() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr; }
		UAsdaSkillUseSubsystem* SU() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr; }
		UAsdaFloatingTextSubsystem* FT() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaFloatingTextSubsystem>() : nullptr; }
		UAsdaTargetFrameSubsystem* TF() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaTargetFrameSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AAsdaMobActor* Mob(int32 Rid) const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(Rid) : nullptr; }
		AActor* Peer(int32 Idx) const { const UAsdaWorldSubsystem* W = WS(); return W ? W->ActorByTarget(EAsdaTargetKind::Character, Idx) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaBowFxTest, Log, TEXT("САМОТЕСТ-BOWFX %s %s: %s"), bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }
		TArray<FOut> OutsOf(uint16 Op) const { TArray<FOut> R; for (int32 i = OutFrom; i < Outs.Num(); ++i) { if (Outs[i].Op == Op) { R.Add(Outs[i]); } } return R; }
		void Mark() { OutFrom = Outs.Num(); FloatMax = 0; FloatBase = FT() ? FT()->DebugLiveCount() : 0; bSawState = false; }
		bool MobHas(int32 Rid, int32 State) const
		{
			const UAsdaEffectsSubsystem* F = Fx(); if (!F) { return false; }
			for (const FAsdaEffectEntry& E : F->GetMobList(Rid)) { if (E.Type == 0 && E.Id == State) { return true; } }
			return false;
		}
		void SpawnMob(int32 Rid, const FIntPoint& Tile) { Feed(4013, AsdaTestPackets::MobSpawn4013(Rid, kMobType, Tile, 1, kSpdDenom)); Mobs.AddUnique(Rid); }
		void SpawnPeer(int32 Idx, const TCHAR* Name, const FIntPoint& Tile)
		{
			if (UAsdaWorldSubsystem* W = WS()) { W->DebugSpawnFakePeer(Idx, Name, AsdaCoord::TileCenterGame(Tile)); Peers.AddUnique(Idx); }
		}
		void SetKey(const TCHAR* Key, int32 V) { AsdaCVarSave::SetForTest(Key, V); }   // исходное значение вернёт `Finish` (RestoreAll)
		// Сброс диспетчера навыков между заявками: штатный отказ 5257 (код 0) снимает состояние 25.
		//   ⚠ Отказ только встаёт в очередь `NetQueue` — разбирает его тик подсистемы, не этот вызов.
		void CalmSkill(int32 Code) { if (UAsdaSkillUseSubsystem* S = SU()) { S->SimulateCastResult5257(0, Code); } }
		// Отказ разобран: активного навыка нет, состояние действия 0 — новое нажатие не упрётся в гейты 7/12.
		bool SkillIdle() const { const UAsdaSkillUseSubsystem* S = SU(); return S && S->GetActiveSkill() < 0 && S->GetSelfCharState() == 0; }
	};

	// 6079 (21 б) — раскладка `FAsdaDotSlotPkt`.
	static TArray<uint8> Body6079(int32 Caster, uint8 Kind, int32 Target, int32 Skill, int32 State, int32 DurMs, int32 StepMs, int32 Tick)
	{
		TArray<uint8> B; B.SetNumZeroed(21);
		B[0] = 1; AsdaBytes::WrI16(&B[1], Caster); B[3] = Kind; AsdaBytes::WrI16(&B[4], Target);
		AsdaBytes::WrI16(&B[6], Skill); AsdaBytes::WrI16(&B[8], State); B[10] = 5;
		AsdaBytes::WrI32(&B[11], DurMs); AsdaBytes::WrI16(&B[15], StepMs); AsdaBytes::WrI32(&B[17], Tick);
		return B;
	}
	// 4102 (17 б): вид@0, цель@1, навык@3, урон@5, HP@9, наложить@13, кастер@14, стек@16.
	static TArray<uint8> Body4102(int32 Rid, int32 Skill, int32 Caster)
	{
		TArray<uint8> B; B.SetNumZeroed(17);
		B[0] = 0; AsdaBytes::WrI16(&B[1], Rid); AsdaBytes::WrI16(&B[3], Skill); AsdaBytes::WrI32(&B[9], 100);
		B[13] = 1; AsdaBytes::WrI16(&B[14], Caster);
		return B;
	}
	// 5270 (шапка 7 б + 29·N): кастер@0, навык@2, уровень@4, записей@5; запись: вид@0, цель@1, урон@3, флаги@7, стеки@8..10, HP@11.
	static TArray<uint8> Body5270(int32 Caster, int32 Skill, int32 Rid, int32 Dmg, uint8 Flags, int32 HpAfter)
	{
		TArray<uint8> B; B.SetNumZeroed(7 + 29);
		AsdaBytes::WrI16(&B[0], Caster); AsdaBytes::WrI16(&B[2], Skill); B[4] = 5; AsdaBytes::WrI16(&B[5], 1);
		uint8* R = B.GetData() + 7;
		R[0] = 0; AsdaBytes::WrI16(R + 1, Rid); AsdaBytes::WrI32(R + 3, Dmg); R[7] = Flags; AsdaBytes::WrI32(R + 11, HpAfter);
		AsdaBytes::WrI16(R + 19, -1); AsdaBytes::WrI16(R + 27, -1);
		return B;
	}
	// 4015 (37 б) ход моба.
	static TArray<uint8> Body4015(int32 Rid, const FIntPoint& Cur, const FIntPoint& Dst)
	{
		TArray<uint8> B; B.SetNumZeroed(37);
		AsdaBytes::WrI16(&B[0], -1); AsdaBytes::WrI16(&B[2], kMobType); AsdaBytes::WrI16(&B[4], Rid); B[6] = 2;
		AsdaBytes::WrI16(&B[7], Cur.X); AsdaBytes::WrI16(&B[9], Cur.Y); AsdaBytes::WrI16(&B[11], Dst.X); AsdaBytes::WrI16(&B[13], Dst.Y);
		AsdaBytes::WrI16(&B[15], kSpdDenom); AsdaBytes::WrI16(&B[17], 10000); AsdaBytes::WrI16(&B[19], 10000);
		AsdaBytes::WrI32(&B[21], -1); AsdaBytes::WrI32(&B[25], 100); B[29] = 0xFF; AsdaBytes::WrI16(&B[31], -1);
		return B;
	}
	// 4017 (100 б) flags=2 — список состояний моба (пустой: все сняты).
	static TArray<uint8> Body4017Clear(int32 Rid)
	{
		TArray<uint8> B; B.SetNumZeroed(100);
		AsdaBytes::WrI16(&B[0], kMobType); AsdaBytes::WrI16(&B[2], Rid); AsdaBytes::WrI32(&B[4], 2);
		for (int32 i = 0; i < 28; ++i) { AsdaBytes::WrI16(&B[8 + 2 * i], -1); }
		AsdaBytes::WrI32(&B[92], 100);
		return B;
	}
	// 4017 (100 б) flags=3 — живой моб в бою, список состояний (первые `States`, остальное −1), HP@92.
	static TArray<uint8> Body4017States(int32 Rid, const TArray<int32>& States, int32 Hp)
	{
		TArray<uint8> B; B.SetNumZeroed(100);
		AsdaBytes::WrI16(&B[0], kMobType); AsdaBytes::WrI16(&B[2], Rid); AsdaBytes::WrI32(&B[4], 3);
		for (int32 i = 0; i < 28; ++i) { AsdaBytes::WrI16(&B[8 + 2 * i], States.IsValidIndex(i) ? States[i] : -1); }
		AsdaBytes::WrI32(&B[92], Hp);
		return B;
	}
	// 4206 (13 б): userIdx@0, [88]@6, режим@7, соперник/команда@8, код@12.
	static TArray<uint8> Body4206(int32 Uidx, uint8 Mode, int32 P8)
	{
		TArray<uint8> B; B.SetNumZeroed(13);
		AsdaBytes::WrI16(&B[0], Uidx); B[6] = Mode; B[7] = Mode; AsdaBytes::WrI32(&B[8], P8); B[12] = 1;
		return B;
	}
	// 5251 — изученные навыки (34 б на запись: код, уровень, «можно вкачивать», откат).
	using AsdaTestPackets::Body5251;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.
	// Записи заявки 5256 (тело 31 + 29·N): (вид, индекс).
	static TArray<FIntPoint> RecsOf(const FOut& O)
	{
		TArray<FIntPoint> R;
		for (int32 Off = 31; Off + 29 <= O.Payload.Num(); Off += 29) { R.Emplace(O.Payload[Off], AsdaBytes::RdI16(&O.Payload[Off + 1])); }
		return R;
	}
}

struct FAsdaBowFxSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaBowFxSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaBowFxTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 5256, 8226, 4206 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate.bQuit = KitArgs.bQuit;
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout; St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	auto T = [S](int32 Dx, int32 Dy) { return S->HomeTile + FIntPoint(Dx, Dy); };

	// ── 0. подготовка ──
	Add(TEXT("подготовка"), 5.f, [S, Net, T]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		if (!C || !S->PC() || !S->WS() || !S->Fx()) { S->Expect(false, TEXT("нет своего персонажа/мира/эффектов после FakeSelfSpawn")); return; }
		S->HomeTile = AsdaCoord::GameToTile(C->GetGamePosXZ());
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = kSelfFallback; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		S->Self = (int32)Net->SelfUserNum;
		TWeakPtr<FState> WSp = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [WSp](uint16 Op, const uint8* P, int32 Len, bool, TCHAR Env)
		{
			TSharedPtr<FState> St = WSp.Pin(); if (!St) { return; }
			FOut& O = St->Outs.AddDefaulted_GetRef(); O.Op = Op; O.Env = Env; if (P && Len > 0) { O.Payload.Append(P, Len); }
		}, (uint16)0, kCmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected; Net->bInGameConnected = true;
		S->bWasStandSkill = Net->bStandCaptureSkillUse; Net->bStandCaptureSkillUse = true;
		S->Feed(5251, Body5251({ 705, 713, 710, 533 }));
		Net->SetVitalsMp(S->Self, 99999, 99999, TEXT("самотест bowfx"), true);
		S->SetKey(TEXT("asda.Dot.Slots"), 1); S->SetKey(TEXT("asda.Skill.TargetRelation"), 1);
		S->SetKey(TEXT("asda.Mob.ControlFreeze"), 1); S->SetKey(TEXT("asda.Skill.MobStateChain"), 1); S->SetKey(TEXT("asda.Skill.OwnHitStates"), 1);
		S->SpawnMob(kRid1, T(2, 0)); S->SpawnMob(kRid2, T(-2, 0)); S->SpawnMob(kRid3, T(0, 3)); S->SpawnMob(kRid4, T(0, -3)); S->SpawnMob(kRid5, T(3, 3));
		S->SpawnPeer(kP, TEXT("МирныйП"), T(1, 1)); S->SpawnPeer(kQ, TEXT("СоседQ"), T(-1, 1));
	}, [S]() { return S->El() > 1.0 && S->Mob(kRid1) && S->Peer(kP) && S->Peer(kQ); }, [S]()
	{
		S->Expect(S->Mob(kRid1) && S->Mob(kRid5) && S->Peer(kP) && S->Peer(kQ), TEXT("мобы и соседи на месте"));
	});

	// ── 1. яд 6079: состояние, значок, эффект, тики ──
	Add(TEXT("1 яд 6079 (кастер я)"), 6.f, [S]()
	{
		S->Mark();
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(S->Mob(kRid1)); }
		S->Feed(6079, Body6079(S->Self, 0, kRid1, 705, 70, 3000, 1000, 20));
	}, [S]()
	{
		if (UAsdaFloatingTextSubsystem* F = S->FT()) { S->FloatMax = FMath::Max(S->FloatMax, F->DebugLiveCount() - S->FloatBase); }
		if (S->El() > 0.5 && S->El() < 0.9 && !S->bSawState)
		{
			S->bSawState = true;
			const FAsdaDotSlot* Sl = S->Fx()->FindDotSlot(true, kRid1, 70);
			S->Expect(S->MobHas(kRid1, 70), TEXT("состояние 70 «Яд» на мобе (buff_ApplySkillBuff из 6079)"));
			S->Expect(Sl && Sl->Remaining == 60 && Sl->TickDamage == 20, FString::Printf(TEXT("слот урона по времени: остаток %d (ждём 60), тик %d"), Sl ? Sl->Remaining : -1, Sl ? Sl->TickDamage : -1));
			const UAsdaTargetFrameSubsystem* F = S->TF();
			S->Expect(F && F->DebugEnemyStateId(0) == 70 && F->DebugEnemyStateIconShown(0),
				FString::Printf(TEXT("значок под полосой HP цели: слот 0 = %d, виден %d"), F ? F->DebugEnemyStateId(0) : -2, (F && F->DebugEnemyStateIconShown(0)) ? 1 : 0));
			bool bFx = false;
			for (const FAsdaEffectEntry& E : S->Fx()->GetMobList(kRid1)) { if (E.Id == 70) { bFx = E.Fx0.IsValid() || E.Fx1.IsValid() || E.bFxSpawned; } }
			S->Expect(bFx, TEXT("эффект состояния яда на мобе заспавнен (вторая таблица обработчиков)"));
		}
		return S->El() > 3.6;
	}, [S]()
	{
		const AAsdaMobActor* M = S->Mob(kRid1);
		S->Expect(M && M->CurHp == 40, FString::Printf(TEXT("3 тика по 20: HP моба %d (ждём 40)"), M ? M->CurHp : -1));
		S->Expect(S->FloatMax >= 1, FString::Printf(TEXT("цифры тиков над мобом: живых надписей до %d"), S->FloatMax));
		S->Expect(S->Fx()->GetDotSlotCount(true, kRid1) == 0, TEXT("слот исчерпан и снят"));
	});

	// ── 2. контрольная пара: ключ 0 ──
	Add(TEXT("2 контроль asda.Dot.Slots 0"), 2.f, [S]()
	{
		S->Mark(); S->SetKey(TEXT("asda.Dot.Slots"), 0);
		S->Feed(6079, Body6079(S->Self, 0, kRid2, 713, 68, 3000, 1000, 20));
	}, [S]() { return S->El() > 0.6; }, [S]()
	{
		S->Expect(!S->MobHas(kRid2, 68) && S->Fx()->GetDotSlotCount(true, kRid2) == 0, TEXT("прежний порт: 6079 не разобран — ни состояния, ни слота (дефект воспроизведён)"));
		S->SetKey(TEXT("asda.Dot.Slots"), 1);
	});

	// ── 3. 8226 ──
	Add(TEXT("3 моб мёртв при остатке — 8226"), 4.f, [S]()
	{
		S->Mark();
		S->Feed(6079, Body6079(S->Self, 0, kRid2, 713, 68, 2000, 500, 60));
	}, [S]() { return S->El() > 1.6; }, [S]()
	{
		const TArray<FOut> O = S->OutsOf(8226);
		const bool bBody = O.Num() >= 1 && O[0].Payload.Num() == 4 && AsdaBytes::RdI16(&O[0].Payload[0]) == S->Self && AsdaBytes::RdI16(&O[0].Payload[2]) == kRid2;
		S->Expect(bBody && O[0].Env == TEXT('5'), FString::Printf(TEXT("заявка 8226 [свой %d][rid %d], конверт MD5 без секрета: штук %d"), S->Self, kRid2, O.Num()));
	});

	// ── 4. свой удар 5270 «Кровавая песнь» ──
	Add(TEXT("4 свой удар 713 (бит 0x02)"), 5.f, [S]()
	{
		S->Mark();
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(S->Mob(kRid3)); }
		S->Feed(5270, Body5270(S->Self, 713, kRid3, 15, 0x03, 85));
	}, [S]() { return S->MobHas(kRid3, 68) || S->El() > 4.5; }, [S]()
	{
		S->Expect(S->MobHas(kRid3, 68), FString::Printf(TEXT("состояние 68 «Кровотечение» на мобе по своему удару (Skill_ApplyEffectResult), через %.2f с"), S->El()));
		S->CalmSkill(713);
	});

	// ── 5. ловушка 185 → лёд ──
	Add(TEXT("5 ловушка 4102/185 → оледенение"), 4.f, [S, T]()
	{
		S->Mark();
		S->Feed(4102, Body4102(kRid4, 185, S->Self));
		if (const AAsdaMobActor* M = S->Mob(kRid4)) { S->MobPos0 = M->GetActorLocation(); }
		S->Feed(4015, Body4015(kRid4, T(0, -3), T(6, -3)));
	}, [S]() { return S->El() > 1.5; }, [S]()
	{
		const AAsdaMobActor* M = S->Mob(kRid4);
		S->Expect(S->MobHas(kRid4, 0) && !S->MobHas(kRid4, 185), TEXT("на мобе состояние 0 «Оледенение» (buff0 навыка 185), а не 185"));
		S->Expect(S->Fx()->HasMobAnimFreeze(kRid4) && M && M->Mesh && M->Mesh->bPauseAnims, TEXT("анимация моба стоит (+6987)"));
		const float D = M ? (float)FVector::Dist2D(M->GetActorLocation(), S->MobPos0) : -1.f;
		S->Expect(M && D < 10.f, FString::Printf(TEXT("ход 4015 не двигает пойманного моба: смещение %.1f"), D));
	});
	Add(TEXT("5б снятие 4017 — моб идёт"), 4.f, [S, T]()
	{
		S->Feed(4017, Body4017Clear(kRid4));
		if (const AAsdaMobActor* M = S->Mob(kRid4)) { S->MobPos0 = M->GetActorLocation(); }
		S->Feed(4015, Body4015(kRid4, T(0, -3), T(6, -3)));
	}, [S]() { return S->El() > 1.2; }, [S]()
	{
		const AAsdaMobActor* M = S->Mob(kRid4);
		const float D = M ? (float)FVector::Dist2D(M->GetActorLocation(), S->MobPos0) : -1.f;
		S->Expect(!S->MobHas(kRid4, 0) && M && M->Mesh && !M->Mesh->bPauseAnims && D > 50.f, FString::Printf(TEXT("состояние снято, анимация идёт, моб сдвинулся на %.0f"), D));
	});

	// ── 6. «Ловчий выстрел» 710 своим ударом → ловушка 2 ──
	Add(TEXT("6 свой удар 710 → ловушка 2"), 6.f, [S]()
	{
		S->Mark();
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(S->Mob(kRid5)); }
		S->Feed(5270, Body5270(S->Self, 710, kRid5, 10, 0x03, 90));
	}, [S]() { return S->MobHas(kRid5, 2) || S->El() > 5.f; }, [S, T]()
	{
		S->Expect(S->MobHas(kRid5, 2) && S->Fx()->HasMobMoveLock(kRid5), TEXT("состояние 2 «Ловушка» на мобе, флаг +6986 «стоять»"));
		if (const AAsdaMobActor* M = S->Mob(kRid5)) { S->MobPos0 = M->GetActorLocation(); }
		S->Feed(4015, Body4015(kRid5, T(3, 3), T(9, 3)));
		S->CalmSkill(710);
	});
	Add(TEXT("6б ход под ловушкой"), 3.f, nullptr, [S]() { return S->El() > 1.2; }, [S]()
	{
		const AAsdaMobActor* M = S->Mob(kRid5);
		const float D = M ? (float)FVector::Dist2D(M->GetActorLocation(), S->MobPos0) : -1.f;
		S->Expect(M && D < 10.f && M->Mesh && !M->Mesh->bPauseAnims, FString::Printf(TEXT("пойманный ловушкой моб не идёт (смещение %.1f), анимация не заморожена"), D));
	});

	// ── 7–11: допустимые цели ──
	auto Cast = [S](int32 Code, int32 Target)
	{
		S->Mark();
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(S->Peer(Target)); }
		if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DispatchByKind(Code, -1); }
	};
	auto Sent = [S](int32 Code) { for (const FOut& O : S->OutsOf(5256)) { if (O.Payload.Num() >= 26 && AsdaBytes::RdI16(&O.Payload[24]) == Code) { return true; } } return false; };
	// ★ 30.10.2026 (флейк 38/1 на шагах 8/9/11): `CalmSkill` в проверке шага лишь ставит отказ в очередь, а исполнитель
	//   (таймер 0.01 с, за длинный кадр зовётся несколько раз) стартовал следующий шаг в ТОМ ЖЕ кадре — нажатие упиралось
	//   в гейт 7/12 (навык ещё активен, состояние 25), заявка не уходила. Шаги с нажатием ждут разбора отказа.
	auto NeedIdle = [S]() { S->Steps.Last().Ready = [S]() { return S->SkillIdle(); }; };
	Add(TEXT("7 враждебный 710 по мирному вне PvP"), 2.f, [S, Cast]() { S->Feed(4206, Body4206(S->Self, 0, 0)); Cast(710, kP); }, [S]() { return S->El() > 0.8; }, [S, Sent]()
	{
		S->Expect(!Sent(710), TEXT("заявки 5256 нет (эталон молчит вне PvP)"));
		S->CalmSkill(710);
	});
	NeedIdle();
	Add(TEXT("7б контроль asda.Skill.TargetRelation 0"), 2.f, [S, Cast]() { S->SetKey(TEXT("asda.Skill.TargetRelation"), 0); Cast(710, kP); }, [S]() { return S->El() > 0.8; }, [S, Sent]()
	{
		S->Expect(Sent(710), TEXT("прежний порт: заявка по мирному уходит (дефект воспроизведён)"));
		S->SetKey(TEXT("asda.Skill.TargetRelation"), 1); S->CalmSkill(710);
	});
	NeedIdle();
	Add(TEXT("8 свободный бой (режим 2)"), 2.f, [S, Cast]() { S->Feed(4206, Body4206(S->Self, 2, 0)); Cast(710, kP); }, [S]() { return S->El() > 0.8; }, [S, Sent]()
	{
		S->Expect(Sent(710), TEXT("в режиме 2 заявка по чужому уходит"));
		S->CalmSkill(710);
	});
	NeedIdle();
	Add(TEXT("9 дуэль: соперник P"), 2.f, [S, Cast]() { S->Feed(4206, Body4206(S->Self, 1, kP)); Cast(710, kP); }, [S]() { return S->El() > 0.8; }, [S, Sent]()
	{
		S->Expect(Sent(710), TEXT("дуэль: по сопернику заявка уходит"));
		S->CalmSkill(710);
	});
	NeedIdle();
	Add(TEXT("9б дуэль: не соперник Q"), 2.f, [S, Cast]() { Cast(710, kQ); }, [S]() { return S->El() > 0.8; }, [S, Sent]()
	{
		S->Expect(!Sent(710), TEXT("дуэль: по не-сопернику заявки нет (skill_buff.c:3705)"));
		S->CalmSkill(710);
	});
	NeedIdle();
	Add(TEXT("10 «на игрока» 533 вне PvP"), 2.f, [S, Cast]() { S->Feed(4206, Body4206(S->Self, 0, 0)); Cast(533, kP); }, [S]() { return S->El() > 0.8; }, [S, Sent]()
	{
		S->Expect(Sent(533), TEXT("навык на союзника по чужому уходит (решает сервер)"));
		S->CalmSkill(533);
	});
	NeedIdle();
	Add(TEXT("11 площадь 705 в режиме 2: согруппник не берётся"), 2.f, [S, Cast]()
	{
		S->Feed(4206, Body4206(S->Self, 2, 0));
		if (UAsdaWorldSubsystem* W = S->WS()) { W->SetPartyRoster({ kQ }); }
		Cast(705, kP);
	}, [S]() { return S->El() > 0.8; }, [S]()
	{
		bool bP = false, bQ = false, bAny = false;
		for (const FOut& O : S->OutsOf(5256))
		{
			if (O.Payload.Num() < 26 || AsdaBytes::RdI16(&O.Payload[24]) != 705) { continue; }
			bAny = true;
			for (const FIntPoint& R : RecsOf(O)) { if (R.X == 1 && R.Y == kP) { bP = true; } if (R.X == 1 && R.Y == kQ) { bQ = true; } }
		}
		S->Expect(bAny && bP && !bQ, FString::Printf(TEXT("заявка %d, P в записях %d, согруппник Q в записях %d"), bAny ? 1 : 0, bP ? 1 : 0, bQ ? 1 : 0));
		if (UAsdaWorldSubsystem* W = S->WS()) { W->SetPartyRoster({}); }
		S->CalmSkill(705);
	});
	NeedIdle();

	// ── 12–13. ★ 26.10.2026 (баг «копьё: Удар наотмашь без кровотечения, Жало без яда»): тела 6079 и 4017 — ДОСЛОВНО живые
	//   (LogDrop Player5 19.10 21:20: навык 504 → 6079 `01 03 00 00 A7 00 F8 01 44 00 05 70 17 00 00 F4 01 09 00 00 00`, 4017 со
	//   списком 0x44; навык 505 → 6079 `… F9 01 48 00 05 E0 2E 00 00 B8 0B 58 00 00 00`, 4017 со списком 0x48), кастер — я.
	struct FSpearCase { const TCHAR* Name; int32 Rid; int32 Skill; int32 State; int32 Dur; int32 Step; int32 Tick; float WaitS; int32 Ticks; };
	static const FSpearCase kSpear[] = {
		{ TEXT("12 копьё: Удар наотмашь 504 → кровотечение 68"), 30891, 504, 68,  6000,  500,  9, 1.25f, 2 },
		{ TEXT("13 копьё: Жало 505 → болезнь 72 (яд-визуал dot301)"), 30892, 505, 72, 12000, 3000, 88, 3.35f, 1 },
	};
	for (const FSpearCase& C : kSpear)
	{
		Add(C.Name, C.WaitS + 3.f, [S, C, T]()
		{
			S->Mark();
			S->SpawnMob(C.Rid, T(-3, -2));
		}, [S, C]() { return S->Mob(C.Rid) != nullptr && S->El() > 0.4; }, [S, C]()
		{
			if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(S->Mob(C.Rid)); }
			S->Feed(6079, Body6079(S->Self, 0, C.Rid, C.Skill, C.State, C.Dur, C.Step, C.Tick));
			S->Feed(4017, Body4017States(C.Rid, { C.State }, 100));   // сервер в том же кадре шлёт список с этим состоянием
		});
		Add(FString(C.Name) + TEXT(" — тики и значок"), C.WaitS + 3.f, nullptr, [S, C]()
		{
			// База — минимум за шаг: на старте над мобом прошлого сценария ещё живут цифры его тиков, и прирост от них зависел от частоты кадров.
			if (UAsdaFloatingTextSubsystem* F = S->FT())
			{
				S->FloatBase = FMath::Min(S->FloatBase, F->DebugLiveCount());
				S->FloatMax = FMath::Max(S->FloatMax, F->DebugLiveCount() - S->FloatBase);
			}
			return S->El() > C.WaitS;
		}, [S, C]()
		{
			const AAsdaMobActor* M = S->Mob(C.Rid);
			const UAsdaTargetFrameSubsystem* F = S->TF();
			const int32 WantHp = 100 - C.Tick * C.Ticks;
			S->Expect(S->MobHas(C.Rid, C.State), FString::Printf(TEXT("состояние %d на мобе (6079 + 4017 со списком)"), C.State));
			S->Expect(F && F->DebugEnemyStateId(0) == C.State && F->DebugEnemyStateIconShown(0),
				FString::Printf(TEXT("значок под полосой HP цели: слот 0 = %d (ждём %d)"), F ? F->DebugEnemyStateId(0) : -2, C.State));
			S->Expect(M && M->CurHp == WantHp, FString::Printf(TEXT("%d тик(а) по %d: HP %d (ждём %d)"), C.Ticks, C.Tick, M ? M->CurHp : -1, WantHp));
			S->Expect(S->FloatMax >= 1, FString::Printf(TEXT("цифры тиков над мобом: живых надписей до %d"), S->FloatMax));
		});
	}

	// ── 14. Выборка аудита DOT_AUDIT.md: прочие оружия — урон по времени через 6079 и контроль своим ударом (бит 0x02) ──
	struct FAuditCase { int32 Rid; int32 Skill; int32 State; bool bDot; };
	static const FAuditCase kAudit[] = {
		{ 30893, 502, 72, true  },   // копьё «Глубокий укол» → болезнь
		{ 30894, 901, 71, true  },   // посох «Гнев земли» → пламя
		{ 30895, 908, 73, true  },   // посох «Аура тьмы» → проклятие (визуала у сущности нет — noop эталона)
		{ 30896, 503,  3, false },   // меч «Удар в прыжке» → оглушение (своим ударом)
	};
	constexpr int32 kRidIce = 30897;   // посох «Водяной столб» 904 → оледенение — своим ударом ОТДЕЛЬНО (второй каст подряд снял бы показ первого)
	Add(TEXT("14 выборка аудита: подготовка"), 3.f, [S, T]()
	{
		S->Mark();
		int32 i = 0;
		for (const FAuditCase& C : kAudit) { S->SpawnMob(C.Rid, T(-4 + 2 * i, 4)); ++i; }
	}, [S]() { for (const FAuditCase& C : kAudit) { if (!S->Mob(C.Rid)) { return false; } } return S->El() > 0.4; }, [S]()
	{
		for (const FAuditCase& C : kAudit)
		{
			if (C.bDot) { S->Feed(6079, Body6079(S->Self, 0, C.Rid, C.Skill, C.State, 2000, 500, 5)); }
			else { S->Feed(5270, Body5270(S->Self, C.Skill, C.Rid, 5, 0x03, 95)); }
		}
	});
	Add(TEXT("14б выборка аудита: состояния"), 6.f, nullptr, [S]()
	{
		for (const FAuditCase& C : kAudit) { if (!S->MobHas(C.Rid, C.State)) { return false; } }
		return S->El() > 1.2;
	}, [S]()
	{
		for (const FAuditCase& C : kAudit)
		{
			const AAsdaMobActor* M = S->Mob(C.Rid);
			if (C.bDot)
			{
				S->Expect(S->MobHas(C.Rid, C.State) && M && M->CurHp < 100,
					FString::Printf(TEXT("навык %d → состояние %d и тики (HP %d)"), C.Skill, C.State, M ? M->CurHp : -1));
			}
			else
			{
				S->Expect(S->MobHas(C.Rid, C.State), FString::Printf(TEXT("навык %d своим ударом → состояние %d"), C.Skill, C.State));
			}
		}
		S->Expect(S->Fx()->HasMobMoveLock(30896), TEXT("оглушение (503) держит моба на месте (+6986)"));
		S->CalmSkill(503);
	});
	Add(TEXT("14в выборка аудита: Водяной столб 904 своим ударом"), 6.f, [S, T]()
	{
		S->bSawState = false;
		S->SpawnMob(kRidIce, T(4, -4));
	}, [S]()
	{
		if (S->Mob(kRidIce) && !S->bSawState) { S->bSawState = true; S->Feed(5270, Body5270(S->Self, 904, kRidIce, 5, 0x03, 95)); }
		return S->MobHas(kRidIce, 0);
	}, [S]()
	{
		S->Expect(S->MobHas(kRidIce, 0) && S->Fx()->HasMobAnimFreeze(kRidIce), TEXT("навык 904 своим ударом → оледенение 0, анимация моба стоит (+6987)"));
		S->CalmSkill(904);
	});

	// ── итог ──
	Add(TEXT("итог"), 3.f, [S, Net]()
	{
		S->Feed(4206, Body4206(S->Self, 0, 0));
		for (const int32 Rid : S->Mobs) { TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], Rid); S->Feed(4011, B); }
		for (const int32 Idx : S->Peers) { TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], Idx); S->Feed(4011, B); }
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(nullptr); }
	}, [S]() { return S->El() > 0.5; }, [S, Net]()
	{
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame; Net->bStandCaptureSkillUse = S->bWasStandSkill;
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), kCmd, false, LogAsdaBowFxTest, TEXT("САМОТЕСТ-BOWFX"), S->Ok, S->Fail);
	});

	// ── спавн и исполнитель (как у `asda.Combat.SelfTestPvp`) ──
	TWeakObjectPtr<UWorld> WW(W);
	{
		FTimerHandle H; TWeakPtr<FState> WSg = S;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [WW, WSg]()
		{
			UWorld* Wd = WW.Get(); const TSharedPtr<FState> Sg = WSg.Pin();
			if (Wd && Sg && !Sg->Gate.Pass(Wd)) { return; }
			APlayerController* P = Wd ? Wd->GetFirstPlayerController() : nullptr;
			if (Wd && (!P || !P->GetPawn()) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, KitArgs.Delay), false);
	}
	TWeakPtr<FState> WSt = S;
	W->GetTimerManager().SetTimer(S->Tick, FTimerDelegate::CreateWeakLambda(Net, [WSt, WW]()
	{
		TSharedPtr<FState> St = WSt.Pin(); UWorld* Wd = WW.Get();
		if (!St || !Wd) { return; }
		if (!St->Gate.Pass(Wd)) { Wd->GetTimerManager().ClearTimer(St->Tick); return; }
		if (!St->Steps.IsValidIndex(St->StepIdx)) { Wd->GetTimerManager().ClearTimer(St->Tick); AsdaSelfTestKit::Quit(Wd, St->bQuit); return; }
		const double Now = FPlatformTime::Seconds();
		FStep& Stp = St->Steps[St->StepIdx];
		if (!St->bStepStarted)
		{
			// Условие готовности (разбор отказа предыдущего шага) — ждём его кадрами, а не стартуем вслепую.
			if (Stp.Ready && !Stp.Ready())
			{
				if (St->ReadyWaitS <= 0.0) { St->ReadyWaitS = Now; }
				if (Now - St->ReadyWaitS < kReadyMaxS) { return; }
				St->Expect(false, FString::Printf(TEXT("диспетчер навыков не освободился за %.1f с: активный навык %d, состояние %d"), kReadyMaxS,
					St->SU() ? St->SU()->GetActiveSkill() : -2, St->SU() ? St->SU()->GetSelfCharState() : -2));
			}
			St->ReadyWaitS = 0.0;
			St->bStepStarted = true; St->StepStartS = Now;
			UE_LOG(LogAsdaBowFxTest, Log, TEXT("САМОТЕСТ-BOWFX шаг %s"), *Stp.Name);
			if (Stp.Start) { Stp.Start(); }
			return;
		}
		const double El = Now - St->StepStartS;
		const bool bDone = !Stp.Poll || Stp.Poll();
		if (!bDone && El < Stp.TimeoutSec) { return; }
		if (!bDone) { St->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с"), Stp.TimeoutSec)); }
		if (Stp.Check) { Stp.Check(); }
		++St->StepIdx; St->bStepStarted = false;
	}), 0.01f, true, KitArgs.Delay + 2.5f);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestBowFx(
	TEXT("asda.Skill.SelfTestBowFx"),
	TEXT("Самотест яда/крови/ловушек и допустимых целей навыков (bowfx 26.10): 6079 → состояние+значок+тики, 8226, свой удар 5270 "
	     "(бит 0x02), ловушка 4102/185 и 710 (стоп хода, лёд), гейт цели-персонажа (вне PvP, режим 2, дуэль, «на игрока»), "
	     "площадь без своих; контрольные пары asda.Dot.Slots 0 и asda.Skill.TargetRelation 0. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Skill.SelfTestBowFx"));
		FAsdaBowFxSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
