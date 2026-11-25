// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ПОД ОГЛУШЕНИЕМ НЕ АТАКУЕТ» `asda.Combat.SelfTestStunAttack` (05.11.2026, баг игроков «персонаж под оглушением
//   атакует и наносит урон», сессия stun-attack; отчёт AstoryServer/Docs/GameServer/cant-act-gate-p107.md). [ПОРТ]
//   Только офлайн (`-AsdaNoNet`): входящие — штатный приём `DebugFeedPacket` (= `HandlePacket` сокета), исходящие — перехват
//   (`AsdaSelfTestKit::ArmCapture`, настоящие отправители 4026/4028/5256). Путь игрока: цель — `SetTarget`, бой — второй клик
//   (`EngageMobFromClick`), навык — `DispatchByKind` (вход быстрой панели). Свой персонаж — `Asda.World.FakeSelfSpawn`, сосед —
//   `DebugSpawnFakePeer`, мобы — запись 4013.
//   Сценарии:
//     1) свой бой мечом: 4029 с очередью [10 10 10 10], на первой цифре — 5272 «Слабость» (3): замах гаснет в том же кадре,
//        автоатака снята, ни 4028, ни новых цифр;
//     2) под оглушением второй клик по мобу — 4026 не уходит, замаха нет;
//     3) навык 503 «Удар в прыжке» под оглушением — заявки 5256 нет; навык 736 «Сосредоточение» (снимает оглушение) — заявка уходит
//        (эталонное исключение 639/615/736/984 у `skillUseOnTargetDispatch`); контроль `asda.Skill.TargetControlGate 0` — не уходит;
//     4) 5268 снимает оглушение — второй клик снова даёт 4026 и замах;
//     5) сосед бьёт моба (4029 соседа) — 5272 «Слабость» соседу: его замах гаснет, очередь сброшена;
//     6) остальные состояния набора эталона (0 лёд, 1 окаменение, 4 страх, 8 сон, 14 мощное оглушение) обрывают серию;
//        «Ловушка» 2 (только обездвиживает) — серия идёт;
//     7) контрольная пара `asda.Combat.ControlStopsAttack 0`: под оглушением замах доигрывает (дефект виден, проверка не слепая).
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"   // ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"                   // запись 4013 (спаун моба)
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"      // оружие (меч)
#include "UI/AsdaEffectsSubsystem.h"           // списки состояний
#include "UI/AsdaSkillUseSubsystem.h"          // DispatchByKind — вход навыка игрока
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"
#include "Npc/AsdaMobActor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaStunAtkTest, Log, All);

namespace AsdaStunAtkTestLocal
{
	const TCHAR* const kCmd = TEXT("asda.Combat.SelfTestStunAttack");   // имя команды
	constexpr int32 kSelfFallback = 4323;    // свой userNum, если не известен
	constexpr int32 kMobType = 1;            // волк alpen
	constexpr int32 kSpdDenom = 250;         // знаменатель скорости 4013
	constexpr int32 kMobHp = 5000;           // HP мобов (серия из многих ударов не добивает)
	constexpr int32 kPeer = 7831;            // сосед
	constexpr int32 kSword = 1;              // подтип оружия «одноручный меч»

	struct FStep { FString Name; float TimeoutSec = 10.f; TFunction<void()> Start; TFunction<bool()> Poll; TFunction<void()> Check; };

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UAsdaNetSubsystem> Net;
		TArray<FStep> Steps;
		int32 StepIdx = 0; bool bStepStarted = false; double StepStartS = 0.0;
		FTimerHandle Tick; bool bQuit = false;
		FAsdaSelfTestRunGate Gate{ kCmd, false };
		int32 Ok = 0, Fail = 0;
		uint32 Gen = 0;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		bool bWasInGame = false, bWasStandSkill = false;
		int32 Self = -1; FIntPoint HomeTile = FIntPoint::ZeroValue; FVector2D Home = FVector2D::ZeroVector;
		int32 SavedWeapon = 0, SavedEquipped = -1;
		TArray<int32> Rids;
		int32 NextRid = 30971;
		// Наблюдение шага
		int32 N26 = 0, N28 = 0, N5256 = 0;          // исходящие за прогон
		TArray<int32> Skills5256;                   // коды навыков заявок 5256
		int32 Phase = 0; int32 Rid = -1;
		int32 DigitsAtStun = -1, N28AtStun = -1, N26AtStun = -1;
		double TStun = -1.0, TStop = -1.0;
		bool bSwingBefore = false;
		int32 SwingAt02 = -1;                       // замах шёл через 0.2 с после наложения (−1 — замера ещё не было)

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaEffectsSubsystem* Fx() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr; }
		UAsdaSkillUseSubsystem* SU() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AAsdaMobActor* Mob(int32 R) const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(R) : nullptr; }
		AAsdaPlayerCharacter* Peer() const { const UAsdaWorldSubsystem* W = WS(); return W ? Cast<AAsdaPlayerCharacter>(W->ActorByTarget(EAsdaTargetKind::Character, kPeer)) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		double Now() const { return FPlatformTime::Seconds(); }
		int32 Digits() const { const UAsdaWorldSubsystem* W = WS(); return W ? W->HitResultsShown : 0; }
		static bool Swinging(const AAsdaPlayerCharacter* C) { return C && (C->IsAttackAnimActive() || C->IsAttackLoopAlive()); }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaStunAtkTest, Log, TEXT("САМОТЕСТ-СТАН-АТАКА %s %s: %s"), bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }
		void ResetObs() { Phase = 0; DigitsAtStun = N28AtStun = N26AtStun = -1; TStun = TStop = -1.0; bSwingBefore = false; SwingAt02 = -1; }
		// Сброс боя (как у самотеста кадра удара): покой, цель снята, мобы прошлых сценариев убраны.
		void ResetFight()
		{
			AAsdaPlayerController* P = PC(); AAsdaPlayerCharacter* C = Ch();
			if (P)
			{
				P->bAutoAttack = false; P->bCombatStarted = false; P->bInCombatStance = false; P->bWaitingCombatAck = false;
				P->NextAttackTickTime = 0.0; P->TargetActor = nullptr;
				P->GetWorldTimerManager().ClearTimer(P->DeferredAttackSendTimer);
				P->CancelRoute();
			}
			if (UAsdaNetSubsystem* Nn = N()) { Nn->ClearMoveRequestInFlight(TEXT("самотест стана: сброс сценария")); }
			if (C) { C->StopAttackAnim(); C->AttackFaceTarget = nullptr; C->NetSnap(Home); }
			for (const int32 R : Rids) { if (AAsdaMobActor* M = Mob(R)) { M->Destroy(); } }
			Rids.Reset(); Rid = -1;
			ResetObs();
		}
		int32 SpawnMob(const FIntPoint& Tile)
		{
			const int32 R = NextRid++;
			TArray<uint8> B = AsdaTestPackets::MobSpawn4013(R, kMobType, Tile, 1, kSpdDenom);
			AsdaBytes::WrI32(&B[9], kMobHp);
			Feed(4013, B); Rids.Add(R);
			return R;
		}
	};

	// 4029 (49 б, Recv_CastState): атакующий@0, цель@2, HP@4, урон i32[4]@8, код@24 (0 = обычный), второй канал −1.
	using AsdaTestPackets::Body4029;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.
	// 5272 (14 б): userIdx@0, состояние@2, вид@9 = 1, остаток с@11.
	static TArray<uint8> Body5272(int32 Uidx, int32 State, int32 RemainSec)
	{
		TArray<uint8> B; B.SetNumZeroed(14);
		AsdaBytes::WrI16(&B[0], Uidx); AsdaBytes::WrI16(&B[2], State); B[9] = 1; AsdaBytes::WrI16(&B[11], RemainSec);
		return B;
	}
	// 5268: userIdx@0, состояние@2 — снятие.
	static TArray<uint8> Body5268(int32 Uidx, int32 State)
	{
		TArray<uint8> B; B.SetNumZeroed(4); AsdaBytes::WrI16(&B[0], Uidx); AsdaBytes::WrI16(&B[2], State); return B;
	}
	// 5251 — изученные навыки (34 б на запись: код, уровень 5, «можно вкачивать»).
	using AsdaTestPackets::Body5251;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.
	// Оружие подтипа Wt (как у самотеста кадра удара): тип 9, не вариант 1, с дальностью.
	static int32 FindWeapon(const UAsdaItemTableSubsystem* IT, int32 Wt)
	{
		if (!IT) { return 0; }
		for (int32 Code = 20000; Code < 80000; ++Code)
		{
			const FAsdaItemRecord* R = IT->GetItem(Code);
			if (R && R->ItemType == 9 && R->ItemSubType == Wt && R->ItemVariant != 1 && R->AttackRangeTiles > 0) { return Code; }
		}
		return 0;
	}
}

struct FAsdaStunAttackSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaStunAttackSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaStunAtkTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 4024, 4026, 4028, 5256 };
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
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* P = S->PC();
		if (!C || !P || !S->WS() || !S->Fx()) { S->Expect(false, TEXT("нет своего персонажа/мира/эффектов после FakeSelfSpawn")); return; }
		S->Home = AsdaCoord::TileCenterGame(AsdaCoord::GameToTile(C->GetGamePosXZ()));
		S->HomeTile = AsdaCoord::GameToTile(S->Home);
		C->NetSnap(S->Home);
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = kSelfFallback; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		S->Self = (int32)Net->SelfUserNum;
		TWeakPtr<FState> WSp = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [WSp](uint16 Op, const uint8* Pl, int32 Len, bool, TCHAR)
		{
			TSharedPtr<FState> St = WSp.Pin(); if (!St) { return; }
			if (Op == 4026) { ++St->N26; }
			if (Op == 4028) { ++St->N28; }
			if (Op == 5256) { ++St->N5256; St->Skills5256.Add((Pl && Len >= 26) ? AsdaBytes::RdI16(Pl + 24) : -1); }
		}, (uint16)0, kCmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected; Net->bInGameConnected = true;
		S->bWasStandSkill = Net->bStandCaptureSkillUse; Net->bStandCaptureSkillUse = true;
		S->SavedWeapon = Net->SelfWeaponItemId; S->SavedEquipped = C->EquippedWeaponSubType;
		Net->SelfWeaponItemId = FindWeapon(S->GI()->GetSubsystem<UAsdaItemTableSubsystem>(), kSword);
		C->EquippedWeaponSubType = kSword;
		S->Expect(Net->SelfWeaponItemId > 0, FString::Printf(TEXT("меч: предмет %d"), Net->SelfWeaponItemId));
		S->Feed(5251, Body5251({ 503, 736 }));
		Net->SetVitalsMp(S->Self, 99999, 99999, TEXT("самотест стана"), true);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.ControlStopsAttack"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.ServerRhythm"), 1);
		if (UAsdaWorldSubsystem* Ws = S->WS()) { Ws->DebugSpawnFakePeer(kPeer, TEXT("СоседСтан"), AsdaCoord::TileCenterGame(T(-1, 1))); }
	}, [S]() { return S->El() > 1.0 && S->Peer(); }, [S]()
	{
		S->Expect(S->Peer() != nullptr, TEXT("сосед на месте"));
	});

	// ── свой бой: вход в бой кликом, ответ 4029 с очередью, по первой цифре — состояние ──
	//   bStopExpected: серия обязана оборваться; иначе (ловушка 2 / ключ 0) — замах идёт.
	auto AddOwnFight = [S, Add, T](const FString& Name, int32 State, bool bStopExpected, int32 KeyValue)
	{
		Add(Name, 9.f, [S, T, KeyValue]()
		{
			S->ResetFight();
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.ControlStopsAttack"), KeyValue);
			S->Rid = S->SpawnMob(T(1, 0));
		}, [S, State, bStopExpected]()
		{
			AAsdaMobActor* M = S->Mob(S->Rid); AAsdaPlayerController* P = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
			if (S->Phase == 0 && M && P && S->El() > 0.5)
			{
				S->N26AtStun = S->N26;
				P->SetTarget(M); P->EngageMobFromClick(M);   // второй клик: бой
				S->Phase = 1;
			}
			if (S->Phase == 1 && S->N26 > S->N26AtStun)
			{
				S->Feed(4029, Body4029(S->Self, S->Rid, kMobHp - 10, { 10, 10, 10, 10 }));   // ответ на 4026 (как живой сервер — сразу)
				S->DigitsAtStun = S->Digits();
				S->Phase = 2;
			}
			// Первая цифра показана (кадр удара) — накладываем состояние.
			if (S->Phase == 2 && (S->Digits() > S->DigitsAtStun || S->El() > 4.0))
			{
				S->bSwingBefore = FState::Swinging(C);
				S->DigitsAtStun = S->Digits(); S->N28AtStun = S->N28;
				S->Feed(5272, Body5272(S->Self, State, 30));
				S->TStun = S->Now();
				S->Phase = 3;
			}
			if (S->Phase == 3 && S->TStop < 0.0 && !FState::Swinging(C) && P && !P->bAutoAttack) { S->TStop = S->Now(); }
			if (S->Phase == 3 && S->SwingAt02 < 0 && S->Now() - S->TStun >= 0.2) { S->SwingAt02 = FState::Swinging(C) ? 1 : 0; }
			return S->Phase == 3 && S->Now() - S->TStun > (bStopExpected ? 2.5 : 1.0);
		}, [S, State, bStopExpected]()
		{
			const int32 NewDigits = S->Digits() - S->DigitsAtStun;
			const int32 New28 = S->N28 - S->N28AtStun;
			S->Expect(S->bSwingBefore, FString::Printf(TEXT("до состояния %d замах шёл (бой начат кликом, 4029 принят)"), State));
			if (bStopExpected)
			{
				const double Dt = S->TStop >= 0.0 ? S->TStop - S->TStun : -1.0;
				S->Expect(Dt >= 0.0 && Dt < 0.15, FString::Printf(TEXT("состояние %d: замах погас и автоатака снята через %.3f с (ждём < 0.15)"), State, Dt));
				S->Expect(New28 == 0, FString::Printf(TEXT("состояние %d: 4028 после наложения не уходил (%d)"), State, New28));
				// Меч: принятый 4026 сервер применяет 2 значения очереди (`+65117`, `ServerHitsPerSwing`) — первое показано до наложения,
				//   второе уже снято с HP моба и показывается честно; два неприменённых (сервер их больше не спишет) — нет.
				S->Expect(NewDigits <= 1, FString::Printf(TEXT("состояние %d: после наложения показаны только применённые сервером значения (%d ≤ 1), неприменённых нет"), State, NewDigits));
			}
			else
			{
				S->Expect(S->SwingAt02 == 1, FString::Printf(TEXT("состояние %d: через 0.2 с после наложения замах идёт (%d) — серия не оборвана"), State, S->SwingAt02));
				if (State == 2) { S->Expect(NewDigits > 0, FString::Printf(TEXT("состояние %d: цифры очереди после наложения идут (%d)"), State, NewDigits)); }
			}
			S->Feed(5268, Body5268(S->Self, State));
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.ControlStopsAttack"), 1);
		});
	};

	// ── 1. оглушение посреди своей серии ──
	AddOwnFight(TEXT("1 своя серия: оглушение 3 на первой цифре"), 3, true, 1);

	// ── 2. клик по мобу под оглушением ──
	Add(TEXT("2 клик под оглушением"), 4.f, [S, T]()
	{
		S->ResetFight();
		S->Rid = S->SpawnMob(T(1, 0));
		S->Feed(5272, Body5272(S->Self, 3, 30));
	}, [S]()
	{
		AAsdaMobActor* M = S->Mob(S->Rid); AAsdaPlayerController* P = S->PC();
		if (S->Phase == 0 && M && P && S->El() > 0.5) { S->N26AtStun = S->N26; P->SetTarget(M); P->EngageMobFromClick(M); S->Phase = 1; S->TStun = S->Now(); }
		return S->Phase == 1 && S->Now() - S->TStun > 1.5;
	}, [S]()
	{
		S->Expect(S->N26 == S->N26AtStun, FString::Printf(TEXT("4026 под оглушением не ушёл (%d)"), S->N26 - S->N26AtStun));
		S->Expect(!FState::Swinging(S->Ch()) && S->PC() && !S->PC()->bAutoAttack, TEXT("замаха и автоатаки нет"));
	});

	// ── 3. навыки под оглушением ──
	Add(TEXT("3 навык 503 под оглушением"), 3.f, [S]()
	{
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(S->Mob(S->Rid)); }
		S->N28AtStun = S->N5256;
		if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DispatchByKind(503, -1); }
	}, [S]() { return S->El() > 1.0; }, [S]()
	{
		S->Expect(S->N5256 == S->N28AtStun, FString::Printf(TEXT("503 под оглушением: заявок 5256 нет (%d)"), S->N5256 - S->N28AtStun));
		if (UAsdaSkillUseSubsystem* U = S->SU()) { U->SimulateCastResult5257(0, 503); }
	});
	// 736 «Сосредоточение» (путь «по цели», снимает оглушение; лук — маска оружия 0xE00): под оглушением заявка 5256 уходит —
	//   эталонное исключение 639/615/736/984 у `skillUseOnTargetDispatch` (ключ `asda.Skill.TargetControlGate`).
	Add(TEXT("3б навык 736 под оглушением"), 3.f, [S]()
	{
		UAsdaNetSubsystem* Nn = S->N(); AAsdaPlayerCharacter* C = S->Ch();
		if (Nn && C) { Nn->SelfWeaponItemId = FindWeapon(S->GI()->GetSubsystem<UAsdaItemTableSubsystem>(), 10); C->EquippedWeaponSubType = 10; }
		AsdaCVarSave::SetForTest(TEXT("asda.Skill.TargetControlGate"), 1);
		S->N28AtStun = S->N5256; S->Skills5256.Reset();
		if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DispatchByKind(736, -1); }
	}, [S]() { return S->El() > 1.0; }, [S]()
	{
		S->Expect(S->N5256 > S->N28AtStun && S->Skills5256.Contains(736), FString::Printf(TEXT("736 под оглушением: заявка 5256 ушла (%d, навыки %d)"),
			S->N5256 - S->N28AtStun, S->Skills5256.Num() > 0 ? S->Skills5256.Last() : -1));
		if (UAsdaSkillUseSubsystem* U = S->SU()) { U->SimulateCastResult5257(0, 736); }
	});
	// Контрольная пара: ключ 0 — прежний порт, 736 под оглушением не уходит (дефект виден).
	Add(TEXT("3в контроль asda.Skill.TargetControlGate 0"), 4.f, [S]() { S->Phase = 0; }, [S]()
	{
		if (S->Phase == 0 && S->El() > 1.2)
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Skill.TargetControlGate"), 0);
			S->N28AtStun = S->N5256;
			if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DispatchByKind(736, -1); }
			S->Phase = 1;
		}
		return S->Phase == 1 && S->El() > 2.2;
	}, [S]()
	{
		S->Expect(S->N5256 == S->N28AtStun, FString::Printf(TEXT("ключ 0: 736 под оглушением не уходит (%d) — дефект прежнего порта виден"), S->N5256 - S->N28AtStun));
		AsdaCVarSave::SetForTest(TEXT("asda.Skill.TargetControlGate"), 1);
		if (UAsdaSkillUseSubsystem* U = S->SU()) { U->SimulateCastResult5257(0, 736); }
		UAsdaNetSubsystem* Nn = S->N(); AAsdaPlayerCharacter* C = S->Ch();
		if (Nn && C) { Nn->SelfWeaponItemId = FindWeapon(S->GI()->GetSubsystem<UAsdaItemTableSubsystem>(), kSword); C->EquippedWeaponSubType = kSword; }
		S->Feed(5268, Body5268(S->Self, 3));
	});

	// ── 4. снятие оглушения: бой снова начинается ──
	Add(TEXT("4 после снятия — бой снова"), 4.f, [S, T]()
	{
		S->ResetFight();
		S->Rid = S->SpawnMob(T(1, 0));
	}, [S]()
	{
		AAsdaMobActor* M = S->Mob(S->Rid); AAsdaPlayerController* P = S->PC();
		if (S->Phase == 0 && M && P && S->El() > 0.5) { S->N26AtStun = S->N26; P->SetTarget(M); P->EngageMobFromClick(M); S->Phase = 1; }
		return S->Phase == 1 && S->N26 > S->N26AtStun;
	}, [S]()
	{
		S->Expect(S->N26 > S->N26AtStun, TEXT("после снятия оглушения второй клик — 4026 ушёл"));
		S->Expect(S->PC() && S->PC()->bAutoAttack, TEXT("автоатака взведена"));
	});

	// ── 5. сосед под оглушением ──
	Add(TEXT("5 сосед: оглушение посреди серии"), 6.f, [S, T]()
	{
		S->ResetFight();
		S->Rid = S->SpawnMob(T(-1, 2));
	}, [S]()
	{
		AAsdaPlayerCharacter* Pr = S->Peer(); const UAsdaWorldSubsystem* Ws = S->WS();
		if (S->Phase == 0 && S->Mob(S->Rid) && Pr && S->El() > 0.5)
		{
			S->Feed(4029, Body4029(kPeer, S->Rid, kMobHp - 10, { 10, 10, 10, 10 }));
			S->Phase = 1;
		}
		if (S->Phase == 1 && S->El() > 1.0)
		{
			S->bSwingBefore = FState::Swinging(Pr);
			S->N26AtStun = Ws ? Ws->GetDamageQueueLen(kPeer) : -1;   // очередь соседа до состояния
			S->Feed(5272, Body5272(kPeer, 3, 30));
			S->TStun = S->Now(); S->Phase = 2;
		}
		if (S->Phase == 2 && S->TStop < 0.0 && !FState::Swinging(Pr)) { S->TStop = S->Now(); }
		return S->Phase == 2 && S->Now() - S->TStun > 1.0;
	}, [S]()
	{
		const UAsdaWorldSubsystem* Ws = S->WS();
		const double Dt = S->TStop >= 0.0 ? S->TStop - S->TStun : -1.0;
		S->Expect(S->bSwingBefore, FString::Printf(TEXT("до оглушения сосед замахивался (очередь %d)"), S->N26AtStun));
		S->Expect(Dt >= 0.0 && Dt < 0.15, FString::Printf(TEXT("сосед: замах погас через %.3f с (ждём < 0.15)"), Dt));
		S->Expect(Ws && Ws->GetDamageQueueLen(kPeer) == 0, FString::Printf(TEXT("сосед: очередь ударов сброшена (%d)"), Ws ? Ws->GetDamageQueueLen(kPeer) : -1));
		S->Feed(5268, Body5268(kPeer, 3));
	});

	// ── 6. прочие состояния эталонного набора и «Ловушка» ──
	static const int32 kStopStates[] = { 0, 1, 4, 8, 14 };
	for (const int32 St : kStopStates) { AddOwnFight(FString::Printf(TEXT("6 своя серия: состояние %d"), St), St, true, 1); }
	AddOwnFight(TEXT("6 своя серия: ловушка 2 (не обрывает)"), 2, false, 1);

	// ── 7. контрольная пара: ключ 0 ──
	AddOwnFight(TEXT("7 контроль asda.Combat.ControlStopsAttack 0"), 3, false, 0);

	// ── итог ──
	Add(TEXT("итог"), 3.f, [S, Net]()
	{
		S->ResetFight();
		for (const int32 St : { 0, 1, 2, 3, 4, 8, 14 }) { S->Feed(5268, Body5268(S->Self, St)); S->Feed(5268, Body5268(kPeer, St)); }
		TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kPeer); S->Feed(4011, B);
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(nullptr); }
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = S->SavedEquipped; }
		Net->SelfWeaponItemId = S->SavedWeapon;
	}, [S]() { return S->El() > 0.5; }, [S, Net]()
	{
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame; Net->bStandCaptureSkillUse = S->bWasStandSkill;
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), kCmd, false, LogAsdaStunAtkTest, TEXT("САМОТЕСТ-СТАН-АТАКА"), S->Ok, S->Fail);
	});

	// ── спавн и исполнитель (как у `asda.Skill.SelfTestBowFx`) ──
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
			St->bStepStarted = true; St->StepStartS = Now;
			UE_LOG(LogAsdaStunAtkTest, Log, TEXT("САМОТЕСТ-СТАН-АТАКА шаг %s"), *Stp.Name);
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

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestStunAttack(
	TEXT("asda.Combat.SelfTestStunAttack"),
	TEXT("Самотест «под оглушением не атакует»: своя серия мечом и соседская — 5272 0/1/3/4/8/14 обрывают замах и очередь, 4026/4028 "
	     "не уходят, навык 503 не уходит, 736 уходит (исключение эталона), контроль asda.Skill.TargetControlGate 0, снятие 5268 — бой снова; ловушка 2 и ключ asda.Combat.ControlStopsAttack 0 "
	     "— серия идёт. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Combat.SelfTestStunAttack"));
		FAsdaStunAttackSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
