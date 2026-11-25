// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ФАНТОМНЫЙ УРОН: 1 УДАР = 1 УРОН ПРИ ЛЮБОМ МИКРОХОДЕ» (25.10.2026, `Reports/phantom_damage/REPORT.md`).
//   [ИСПРАВЛЕНИЕ]
//
// ★ ЗАЧЕМ. Баг игроков: «Performing a normal attack followed by micro-movement queues phantom damage instances, causing 1 attack
//   to hit 4 times». Разбор: ответ 4029 на 4026 несёт 4 значения очереди NpcServer, применены из них только `+65117` (ближний бой — 2),
//   а свой замах ставил на кадры все 4 и не снимал их ходом; плюс 4026 сервер принимает без проверки скорости, и «атака → ход →
//   клик» давал применённые удары быстрее скорости атаки. Самотест штатным путём (бой кликом, исходящие — в перехватчик, входящие —
//   `DebugFeedPacket` = тот же разбор, что у сокета) проверяет:
//   • меч: 4026 → ответ [10 11 12 13] (HP −21) → микроход до кадра удара → показаны ровно 10 и 11, 4028 нет, полоса = серверная;
//   • меч, «спам»: атака → ход → клик → ход: второй 4026 не раньше серверной длительности прошлого удара (`asda.Combat.RestartSeriesGate`),
//     показаны 10, 11, 20, 21 — по два применённых на каждый 4026;
//   • меч без перерыва: ответ на 4026 и пустой ответ на 4028 (сервер дописал 12, 13) — показаны все 4 значения, фантомов нет;
//   • баллиста: ответ [10 11 12 13] на 4026 и ход — показан один выстрел (10): у дальнего боя 1 значение за удар;
//   • контроль: ключи `asda.Combat.OwnHitsServerCommit 0` и `asda.Combat.RestartSeriesGate 0` — дефект виден (8 цифр, второй 4026
//     раньше длительности).
//
// Консоль: `asda.Combat.SelfTestPhantom [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-ФАНТОМ», итог — «САМОТЕСТ-ФАНТОМ ИТОГ».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"                         // запись полей тел пакетов
#include "AsdaCVarSave.h"                      // ключи на время прогона (вернутся в итоге)
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — офлайн повторно на отложенном старте   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"                   // запись 4013
#include "Test/AsdaSelfTestKit.h"              // каркас: аргументы, отказ, перехват, итог, выход
#include "World/AsdaPlayerController.h"        // бой кликом, ход по земле
#include "World/AsdaPlayerCharacter.h"         // свой персонаж
#include "World/AsdaWorldSubsystem.h"          // показ цифр (наблюдения самотеста)
#include "World/AsdaAttackRhythmSubsystem.h"   // серверные часы атаки: требование к следующему удару
#include "World/AsdaCoord.h"                   // игра ↔ UE, клетки
#include "Items/AsdaItemTableSubsystem.h"      // оружие подтипа
#include "Npc/AsdaMobActor.h"                  // моб
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPhantomTest, Log, All);

int32 AsdaOwnPhantomDroppedCount();   // `AsdaWorldSubsystem_Hit.cpp`: сколько своих непримененных значений не показано

namespace AsdaPhantomTestLocal
{
	constexpr int32 kMobType = 1;         // тип моба (волк alpen) — как у `asda.Combat.SelfTestHitFrame`
	constexpr int32 kSpdDenom = 250;      // знаменатель скорости (бег сервера)
	constexpr int32 kMobHp = 100;         // HP моба из записи 4013 (`AsdaTestPackets::MobSpawn4013`)
	const TCHAR* const kCmd = TEXT("asda.Combat.SelfTestPhantom");   // имя команды

	// Вид сценария.
	enum class EKind : uint8
	{
		MoveAfter26,     // меч: 4026 → [10 11 12 13] → ход через 0.15 с
		SpamRestart,     // меч: 4026 → ход → клик → 4026 → ход (дважды)
		Uninterrupted,   // меч: 4026 → [10 11 12 13], 4028 → [] — ход только после ответа на 4028
		RangedMove,      // баллиста: 4026 → [10 11 12 13] → ход через 0.15 с
		// ★ Ревизия 25.10 (М-7):
		DodgeMove,       // меч: 4026 → [−1 11 12 13] → ход: уворот — тоже запись очереди сервера (применены −1 и 11)
		CritPairMove,    // ближний бой с нечётным StepCount 3: 4026 → [10 11 крит12 5] → ход: пара крита (12+5) не применена
		DeathMid,        // меч: 4026 → [60 40 12 13] с HP 0 — цель умирает посреди серии, хвост не показывается
		SwitchInRange,   // меч: 4026 → [10 11 12 13] → клик по второму мобу рядом без хода (ревизия В-1)
	};

	struct FStep
	{
		FString Name;                  // имя шага (в строках журнала)
		float TimeoutSec = 10.f;       // потолок шага (с реального времени)
		TFunction<void()> Start;       // начало шага (один раз)
		TFunction<bool()> Poll;        // опрос каждый тик: true — шаг завершён
		TFunction<void()> Check;       // проверки после завершения (или таймаута)
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UAsdaNetSubsystem> Net;
		TArray<FStep> Steps;
		int32 StepIdx = 0;
		bool bStepStarted = false;
		double StepStartS = 0.0;
		FTimerHandle Tick;
		bool bQuit = false;
		FAsdaSelfTestRunGate Gate{ kCmd, false };
		int32 Ok = 0, Fail = 0;
		uint32 Gen = 0;                     // перехват исходящих
		TArray<double> T26;                 // моменты ухода 4026 (FPlatformTime) за сценарий
		int32 N28 = 0;                      // ушло 4028 за сценарий
		int32 N4006 = 0;                    // ушло 4006 за сценарий
		FVector2D Home = FVector2D::ZeroVector;
		FIntPoint HomeTile = FIntPoint::ZeroValue;
		int32 SavedWeapon = 0, SavedEquipped = 99;
		bool bWasInGame = false;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		TArray<int32> Rids;                 // мобы прогона (убрать в итоге)
		int32 NextRid = 31951;
		// Сценарий.
		int32 Rid = -1;
		int32 Rid2 = -1;                    // ★ ревизия: второй моб (смена цели в дальности)
		int32 PairWt = -1;                  // ★ ревизия: ближнее оружие с StepCount 3 (пара крита); −1 — нет в данных
		int32 Phase = 0;
		double PhaseS = 0.0;                // момент входа в фазу (FPlatformTime)
		int32 SeenFrom = 0;                 // начало наблюдений `HitResultsSeen` сценария
		int32 Dropped0 = 0;                 // счётчик фантомов на старте сценария
		int32 ReqMs = -1;                   // требование часов к следующему удару сразу после первого 4026 (мс)
		int32 Restarts = 0;                 // сделано перезапусков (спам)

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaAttackRhythmSubsystem* Rh() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr; }
		AAsdaMobActor* Mob(int32 R) const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(R) : nullptr; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaPhantomTest, Log, TEXT("САМОТЕСТ-ФАНТОМ %s %s: %s"), bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		int32 NewRid() { const int32 R = NextRid++; Rids.Add(R); return R; }
		void SetPhase(int32 P) { Phase = P; PhaseS = FPlatformTime::Seconds(); }
		double InPhase() const { return FPlatformTime::Seconds() - PhaseS; }
		// Значения, показанные по цели сценария своим ударом (наблюдения `ShowHitResult`).
		TArray<int32> Shown() const { return ShownFor(Rid); }
		// То же по любой цели; удары, погашенные по трупу (`bOnCorpse`), не считаются показанными.
		TArray<int32> ShownFor(int32 R) const
		{
			TArray<int32> Out;
			if (const UAsdaWorldSubsystem* W = WS())
			{
				for (int32 i = SeenFrom; i < W->HitResultsSeen.Num(); ++i)
				{
					const UAsdaWorldSubsystem::FAsdaHitResultSeen& E = W->HitResultsSeen[i];
					if (E.Rid == R && E.bOurs && !E.bOnCorpse) { Out.Add(E.Raw); }
				}
			}
			return Out;
		}
	};

	// Тело 4029 (49 б, `Recv_CastState@0xF41620`): @0 i16 атакующий, @2 i16 rid, @4 i32 HP цели, @8 i32[4] очередь (0 — пусто),
	//   @24 sub (0 — удар принят), @25 i16[4] второй канал (−1), @33 i32[4] (0).
	using AsdaTestPackets::Body4029;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.

	// Первое оружие подтипа (тип 9, не удочка) с дальностью. 0 — нет.
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

	static FString Join(const TArray<int32>& A)
	{
		FString S;
		for (int32 V : A) { S += FString::Printf(TEXT("%s%d"), S.IsEmpty() ? TEXT("") : TEXT(" "), V); }
		return TEXT("[") + S + TEXT("]");
	}

	static const TCHAR* KindName(EKind K)
	{
		switch (K)
		{
		case EKind::MoveAfter26:   return TEXT("меч: 4026 → [10 11 12 13] → микроход до кадра удара");
		case EKind::SpamRestart:   return TEXT("меч: атака → ход → клик → атака → ход («спам» перезапуска)");
		case EKind::Uninterrupted: return TEXT("меч: 4026 → [10 11 12 13], 4028 → [] — ход после ответа на 4028");
		case EKind::RangedMove:    return TEXT("баллиста: 4026 → [10 11 12 13] → микроход");
		case EKind::DodgeMove:     return TEXT("меч: 4026 → [−1 11 12 13] → микроход (уворот)");
		case EKind::CritPairMove:  return TEXT("ближний бой StepCount 3: 4026 → [10 11 крит12 5] → микроход (пара крита)");
		case EKind::DeathMid:      return TEXT("меч: 4026 → [60 40 12 13], HP 0 — смерть цели посреди серии");
		case EKind::SwitchInRange: return TEXT("меч: 4026 → [10 11 12 13] → клик по второму мобу рядом без хода");
		}
		return TEXT("?");
	}
}

struct FAsdaCombatPhantomSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaCombatPhantomSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaPhantomTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 4026, 4028 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { UE_LOG(LogAsdaPhantomTest, Warning, TEXT("САМОТЕСТ-ФАНТОМ: нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate.bQuit = KitArgs.bQuit;

	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout;
		St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	auto Feed = [S](int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = S->N()) { Nn->DebugFeedPacket(Op, B); } };
	auto T = [S](int32 Dx, int32 Dz) { return S->HomeTile + FIntPoint(Dx, Dz); };
	// Сброс боя и сценария (мобы прошлых сценариев убираются; часы атаки — с нуля, чтобы сценарии не зависели друг от друга).
	auto Reset = [S]()
	{
		AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
		if (PC)
		{
			PC->bAutoAttack = false; PC->bCombatStarted = false; PC->bInCombatStance = false; PC->bWaitingCombatAck = false;
			PC->bArmImpactOnSwingStart = false;
			PC->NextAttackTickTime = 0.0; PC->TargetActor = nullptr;
			PC->GetWorldTimerManager().ClearTimer(PC->DeferredAttackSendTimer);
			PC->CancelRoute();
		}
		if (UAsdaNetSubsystem* Nn = S->N()) { Nn->ClearMoveRequestInFlight(TEXT("самотест фантома: сброс сценария")); Nn->CancelPending4028(TEXT("самотест фантома: сброс")); }
		if (UAsdaAttackRhythmSubsystem* R = S->Rh()) { R->DebugReset(); }
		if (C) { C->StopAttackAnim(); C->AttackFaceTarget = nullptr; C->NetSnap(S->Home); }
		for (const int32 R : S->Rids) { if (AAsdaMobActor* M = S->Mob(R)) { M->Destroy(); } }
		S->Rids.Reset();
		S->Rid = -1; S->Rid2 = -1; S->SetPhase(0); S->T26.Reset(); S->N28 = 0; S->N4006 = 0; S->ReqMs = -1; S->Restarts = 0;
		S->SeenFrom = S->WS() ? S->WS()->HitResultsSeen.Num() : 0;
		S->Dropped0 = AsdaOwnPhantomDroppedCount();
	};
	// Микроход: то, что делает ветка клика по земле `OnLeftClick` (выход из боя: флаги, стоп замаха, мирный покой), и ход по
	//   маршруту на полклетки вбок (в дальности удара) — 4006 уходит штатным отправителем (он же снимает поставленный 4028).
	auto MicroMove = [S]()
	{
		AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
		if (!PC || !C) { return; }
		PC->bAutoAttack = false; PC->bCombatStarted = false; PC->bInCombatStance = false; PC->bWaitingCombatAck = false;
		C->StopAttackAnim();
		PC->ExitCombatStance();
		PC->StartRoutedMove(S->Home + FVector2D(0.0, 50.0));   // полклетки (игровые единицы: клетка = 100)
	};
	// Возврат к цели: грант хода (офлайн 4007 нет) и снова клик по мобу — штатный вход в бой.
	auto ReEngage = [S]()
	{
		AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch(); AAsdaMobActor* M = S->Mob(S->Rid);
		if (!PC || !C || !M) { return; }
		if (UAsdaNetSubsystem* Nn = S->N()) { Nn->ClearMoveRequestInFlight(TEXT("самотест фантома: грант хода")); }
		PC->CancelRoute();
		C->NetSnap(S->Home);
		PC->SetTarget(M);
		PC->EngageMobFromClick(M);
	};

	// ─────────────────────── Подготовка ───────────────────────
	Add(TEXT("подготовка"), 1.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* PC = S->PC();
		if (!C || !PC) { S->Expect(false, TEXT("нет своего персонажа/контроллера после FakeSelfSpawn")); return; }
		S->Home = AsdaCoord::TileCenterGame(AsdaCoord::GameToTile(C->GetGamePosXZ()));
		S->HomeTile = AsdaCoord::GameToTile(S->Home);
		C->NetSnap(S->Home);
		S->SavedWeapon = Net->SelfWeaponItemId;
		S->SavedEquipped = C->EquippedWeaponSubType;
		S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = 4321; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		TWeakPtr<FState> WSp = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [WSp](uint16 Op, const uint8*, int32, bool, TCHAR)
		{
			TSharedPtr<FState> St = WSp.Pin();
			if (!St) { return; }
			if (Op == 4026) { St->T26.Add(FPlatformTime::Seconds()); }
			if (Op == 4028) { ++St->N28; }
			if (Op == 4006) { ++St->N4006; }
		}, (uint16)0, kCmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected;
		Net->bInGameConnected = true;
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.ServerRhythm"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.RhythmFromAnim"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.EvenSwingTempo"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.HitFrameAllChannels"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.OwnHitsFifo"), 1);
		if (UAsdaWorldSubsystem* WSs = S->WS()) { WSs->bRecordHitResults = true; }
		UE_LOG(LogAsdaPhantomTest, Log, TEXT("САМОТЕСТ-ФАНТОМ дом: клетка (%d,%d)"), S->HomeTile.X, S->HomeTile.Y);
	}, []() { return true; }, []() {});

	// ─────────────────────── Бой: сценарий × ключи ───────────────────────
	//   Key = 1 — оба ключа исправления включены; 0 — контроль (`OwnHitsServerCommit 0`, `RestartSeriesGate 0`): дефект виден.
	auto AddFight = [S, Add, Feed, T, Reset, MicroMove, ReEngage](EKind Kind, int32 Key)
	{
		const bool bRanged = Kind == EKind::RangedMove;
		const int32 Wt0 = bRanged ? 11 : 1;   // баллиста / одноручный меч (пара крита — своё оружие, ищется на старте)
		Add(FString::Printf(TEXT("%s, ключи %d%s"), KindName(Kind), Key, Key == 0 ? TEXT(" (контроль)") : TEXT("")), 14.f,
		[S, Feed, T, Reset, Wt0, Key, bRanged, Kind]()
		{
			Reset();
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.OwnHitsServerCommit"), Key);
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.RestartSeriesGate"), Key);
			UGameInstance* G = S->GI(); AAsdaPlayerCharacter* C = S->Ch(); UAsdaNetSubsystem* Nn = S->N();
			if (!G || !C || !Nn) { return; }
			int32 Wt = Wt0;
			if (Kind == EKind::CritPairMove)
			{
				// Пара крита снимается на последнем шаге НЕЧЁТНОГО StepCount у ближнего боя (`AsdaTakesCritPair`): ищем такое оружие.
				S->PairWt = -1;
				for (int32 W = 0; W <= 5 && S->PairWt < 0; ++W)
				{
					if (C->GetComboTiming(W).bValid && C->GetComboTiming(W).StepCount == 3 && FindWeapon(G->GetSubsystem<UAsdaItemTableSubsystem>(), W) > 0) { S->PairWt = W; }
				}
				if (S->PairWt < 0) { S->SetPhase(99); return; }   // в данных нет — сценарий не применим (итог скажет)
				Wt = S->PairWt;
			}
			Nn->SelfWeaponItemId = FindWeapon(G->GetSubsystem<UAsdaItemTableSubsystem>(), Wt);
			C->EquippedWeaponSubType = Wt;
			S->Expect(Nn->SelfWeaponItemId > 0, FString::Printf(TEXT("оружие подтипа %d: предмет %d"), Wt, Nn->SelfWeaponItemId));
			S->Rid = S->NewRid();
			Feed(4013, AsdaTestPackets::MobSpawn4013(S->Rid, kMobType, T(bRanged ? 3 : 1, 0), /*State*/1, kSpdDenom));
			if (Kind == EKind::SwitchInRange)
			{
				S->Rid2 = S->NewRid();   // второй моб рядом — в дальности меча
				Feed(4013, AsdaTestPackets::MobSpawn4013(S->Rid2, kMobType, T(0, 1), /*State*/1, kSpdDenom));
			}
		},
		[S, Feed, MicroMove, ReEngage, Kind]()
		{
			if (S->Phase == 99) { return true; }   // сценарий не применим к данным
			AAsdaMobActor* M = S->Mob(S->Rid); AAsdaPlayerController* PC = S->PC();
			const UAsdaNetSubsystem* Nn = S->N();
			const int32 Self = Nn ? (int32)Nn->SelfUserNum : 0;
			switch (S->Phase)
			{
			case 0:   // спаун → клик по мобу
				if (M && PC && S->InPhase() > 0.6)
				{
					PC->SetTarget(M);
					PC->EngageMobFromClick(M);
					S->SetPhase(1);
				}
				return false;
			case 1:   // ушёл 4026 → ответ сервера: 4 значения, применены первые 2 (меч) / 1 (баллиста)
				if (S->T26.Num() >= 1)
				{
					if (const UAsdaAttackRhythmSubsystem* R = S->Rh()) { S->ReqMs = R->GetRequiredMs(); }
					int32 Hp = (Kind == EKind::RangedMove) ? kMobHp - 10 : kMobHp - 21;
					TArray<int32> Q = { 10, 11, 12, 13 };
					if (Kind == EKind::DodgeMove)    { Q = { -1, 11, 12, 13 }; Hp = kMobHp - 11; }
					if (Kind == EKind::CritPairMove) { Q = { 10, 11, (int32)(0x80000000u | 12u), 5 }; }
					if (Kind == EKind::DeathMid)     { Q = { 60, 40, 12, 13 }; Hp = 0; }
					Feed(4029, Body4029(Self, S->Rid, Hp, Q));
					S->SetPhase(2);
				}
				return false;
			case 2:
				if (Kind == EKind::Uninterrupted)
				{
					// Серия идёт: ждём 4028 (точка отправки отрезка), сервер дописывает 12 и 13, новой пачки нет.
					if (S->N28 >= 1) { Feed(4029, Body4029(Self, S->Rid, kMobHp - 46, {})); S->SetPhase(3); }
					return false;
				}
				if (Kind == EKind::DeathMid) { S->SetPhase(9); return false; }   // без хода: смерть на ударе, доводящем полосу до 0
				if (Kind == EKind::SwitchInRange)
				{
					if (S->InPhase() >= 0.15)
					{
						// Клик по второму мобу рядом — без хода (ревизия В-1: серия по первому брошена сменой цели).
						if (AAsdaMobActor* M2 = S->Mob(S->Rid2)) { PC->SetTarget(M2); PC->EngageMobFromClick(M2); }
						S->SetPhase(9);
					}
					return false;
				}
				if (S->InPhase() >= 0.15) { MicroMove(); S->SetPhase(3); }
				return false;
			case 3:
				if (Kind == EKind::Uninterrupted)
				{
					if (S->InPhase() >= 0.15) { MicroMove(); S->SetPhase(9); }
					return false;
				}
				if (Kind == EKind::SpamRestart && S->InPhase() >= 0.2)
				{
					ReEngage();   // «клик по мобу» сразу после хода
					S->SetPhase(4);
					return false;
				}
				if (Kind != EKind::SpamRestart) { S->SetPhase(9); }
				return false;
			case 4:   // ждём второй 4026 (под гейтом — к сроку часов)
				if (S->T26.Num() >= 2)
				{
					Feed(4029, Body4029(Self, S->Rid, kMobHp - 21 - 41, { 20, 21, 22, 23 }));
					S->SetPhase(5);
				}
				return false;
			case 5:
				if (S->InPhase() >= 0.15) { MicroMove(); S->SetPhase(9); }
				return false;
			case 9:   // все поставленные цифры успевают выйти (кадры меча до ~2 с, полёт снаряда баллисты)
				return S->InPhase() >= 2.6;
			default:
				return false;
			}
		},
		[S, Kind, Key]()
		{
			const TArray<int32> Shown = S->Shown();
			const int32 Dropped = AsdaOwnPhantomDroppedCount() - S->Dropped0;
			AAsdaMobActor* M = S->Mob(S->Rid);
			const int32 Bar = M ? M->CurHp : -1;
			const double Gap26 = (S->T26.Num() >= 2) ? (S->T26[1] - S->T26[0]) * 1000.0 : -1.0;
			UE_LOG(LogAsdaPhantomTest, Log, TEXT("САМОТЕСТ-ФАНТОМ замер: показано %s, фантомов снято %d, 4026 ушло %d (интервал %.0f мс, требование часов %d мс), 4028 ушло %d, 4006 ушло %d, полоса %d"),
				*Join(Shown), Dropped, S->T26.Num(), Gap26, S->ReqMs, S->N28, S->N4006, Bar);
			switch (Kind)
			{
			case EKind::MoveAfter26:
				if (Key == 1)
				{
					S->Expect(Shown == TArray<int32>({ 10, 11 }), FString::Printf(TEXT("показаны ровно применённые сервером 10 и 11 (%s)"), *Join(Shown)));
					S->Expect(Dropped == 2, FString::Printf(TEXT("непримененные 12 и 13 сняты (фантомов %d, ждём 2)"), Dropped));
					S->Expect(Bar == kMobHp - 21, FString::Printf(TEXT("полоса = серверное HP %d (сейчас %d)"), kMobHp - 21, Bar));
				}
				else
				{
					S->Expect(Shown.Num() == 4, FString::Printf(TEXT("КОНТРОЛЬ: дефект виден — после хода показаны все 4 значения %s"), *Join(Shown)));
				}
				S->Expect(S->N28 == 0, FString::Printf(TEXT("после хода 4028 не уходил (%d)"), S->N28));
				break;
			case EKind::SpamRestart:
				S->Expect(S->T26.Num() == 2, FString::Printf(TEXT("два 4026 за сценарий (%d)"), S->T26.Num()));
				if (Key == 1)
				{
					S->Expect(Gap26 + 1.0 >= (double)S->ReqMs && S->ReqMs > 0,
						FString::Printf(TEXT("второй 4026 не раньше серверной длительности прошлого удара: %.0f мс ≥ %d мс"), Gap26, S->ReqMs));
					S->Expect(Shown == TArray<int32>({ 10, 11, 20, 21 }), FString::Printf(TEXT("по два применённых на каждый 4026: %s (ждём [10 11 20 21])"), *Join(Shown)));
					S->Expect(Dropped == 4, FString::Printf(TEXT("непримененные 12 13 22 23 сняты (фантомов %d, ждём 4)"), Dropped));
					S->Expect(Bar == kMobHp - 62, FString::Printf(TEXT("полоса = серверное HP %d (сейчас %d)"), kMobHp - 62, Bar));
				}
				else
				{
					S->Expect(Gap26 >= 0.0 && Gap26 < (double)S->ReqMs, FString::Printf(TEXT("КОНТРОЛЬ: дефект виден — второй 4026 раньше длительности (%.0f < %d мс)"), Gap26, S->ReqMs));
					S->Expect(Shown.Num() == 8, FString::Printf(TEXT("КОНТРОЛЬ: дефект виден — показаны все 8 значений %s"), *Join(Shown)));
				}
				break;
			case EKind::Uninterrupted:
				S->Expect(S->N28 == 1, FString::Printf(TEXT("серия: ровно один 4028 до хода (%d)"), S->N28));
				S->Expect(Shown == TArray<int32>({ 10, 11, 12, 13 }), FString::Printf(TEXT("все 4 значения применены сервером и показаны: %s"), *Join(Shown)));
				S->Expect(Dropped == 0, FString::Printf(TEXT("ложных снятий нет (фантомов %d)"), Dropped));
				S->Expect(Bar == kMobHp - 46, FString::Printf(TEXT("полоса = серверное HP %d (сейчас %d)"), kMobHp - 46, Bar));
				break;
			case EKind::RangedMove:
				S->Expect(Shown == TArray<int32>({ 10 }), FString::Printf(TEXT("баллиста: один выстрел, одно значение (%s)"), *Join(Shown)));
				S->Expect(Dropped == 0, FString::Printf(TEXT("баллиста: снимать нечего — показывается одно значение за ответ (фантомов %d)"), Dropped));
				break;
			case EKind::DodgeMove:
				S->Expect(Shown == TArray<int32>({ -1, 11 }), FString::Printf(TEXT("уворот −1 — запись очереди сервера: показаны DODGE и 11 (%s)"), *Join(Shown)));
				S->Expect(Dropped == 2, FString::Printf(TEXT("непримененные 12 и 13 сняты (фантомов %d, ждём 2)"), Dropped));
				S->Expect(Bar == kMobHp - 11, FString::Printf(TEXT("полоса = серверное HP %d (сейчас %d)"), kMobHp - 11, Bar));
				break;
			case EKind::CritPairMove:
				if (S->PairWt < 0)
				{
					S->Expect(true, TEXT("ближнего оружия с StepCount 3 в данных нет — пара крита на один ответ не выпадает (сценарий не применим)"));
					break;
				}
				S->Expect(Shown == TArray<int32>({ 10, 11 }), FString::Printf(TEXT("оружие %d: показаны 10 и 11, пара крита (12+5) — значения №2–3, не применены (%s)"), S->PairWt, *Join(Shown)));
				S->Expect(Dropped == 1, FString::Printf(TEXT("пара крита снята одной цифрой (фантомов %d, ждём 1)"), Dropped));
				break;
			case EKind::DeathMid:
				S->Expect(Shown == TArray<int32>({ 60, 40 }), FString::Printf(TEXT("смерть посреди серии: показаны 60 и 40, хвост 12/13 — нет (%s)"), *Join(Shown)));
				S->Expect(M && M->IsDead(), TEXT("цель мертва (смерть на ударе, доводящем полосу до 0)"));
				break;
			case EKind::SwitchInRange:
			{
				const TArray<int32> Shown2 = S->ShownFor(S->Rid2);
				UE_LOG(LogAsdaPhantomTest, Log, TEXT("САМОТЕСТ-ФАНТОМ замер: по второму мобу показано %s"), *Join(Shown2));
				if (Key == 1)
				{
					S->Expect(Shown == TArray<int32>({ 10, 11 }), FString::Printf(TEXT("смена цели без хода: по первому мобу только применённые 10 и 11 (%s)"), *Join(Shown)));
					S->Expect(Dropped >= 2, FString::Printf(TEXT("непримененные 12 и 13 первого моба сняты (фантомов %d)"), Dropped));
					S->Expect(Bar == kMobHp - 21, FString::Printf(TEXT("полоса первого моба = серверное HP %d (сейчас %d)"), kMobHp - 21, Bar));
				}
				else
				{
					S->Expect(Shown.Num() == 4, FString::Printf(TEXT("КОНТРОЛЬ: дефект виден — по первому мобу показаны все 4 (%s)"), *Join(Shown)));
				}
				break;
			}
			}
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.OwnHitsServerCommit"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.RestartSeriesGate"), 1);
		});
	};
	AddFight(EKind::MoveAfter26, 1);
	AddFight(EKind::MoveAfter26, 0);
	AddFight(EKind::SpamRestart, 1);
	AddFight(EKind::SpamRestart, 0);
	AddFight(EKind::Uninterrupted, 1);
	AddFight(EKind::RangedMove, 1);
	AddFight(EKind::DodgeMove, 1);
	AddFight(EKind::CritPairMove, 1);
	AddFight(EKind::DeathMid, 1);
	AddFight(EKind::SwitchInRange, 1);
	AddFight(EKind::SwitchInRange, 0);

	// ─────────────────────── Итог ───────────────────────
	Add(TEXT("итог"), 1.f, [S, Reset, Net]()
	{
		Reset();
		if (UAsdaWorldSubsystem* WSs = S->WS()) { WSs->bRecordHitResults = false; }
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		Net->SelfWeaponItemId = S->SavedWeapon;
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = S->SavedEquipped; }
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), kCmd, false, LogAsdaPhantomTest, TEXT("САМОТЕСТ-ФАНТОМ"), S->Ok, S->Fail, TEXT(""));
	}, []() { return true; }, []() {});

	// ─────────────────────── Спавн и исполнитель ───────────────────────
	TWeakObjectPtr<UWorld> WW(W);
	{
		FTimerHandle H;
		TWeakPtr<FState> WSg = S;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [WW, WSg]()
		{
			UWorld* Wd = WW.Get();
			const TSharedPtr<FState> Sg = WSg.Pin();
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
		if (!St->Steps.IsValidIndex(St->StepIdx))
		{
			Wd->GetTimerManager().ClearTimer(St->Tick);
			AsdaSelfTestKit::Quit(Wd, St->bQuit);
			return;
		}
		const double Now = FPlatformTime::Seconds();
		FStep& Stp = St->Steps[St->StepIdx];
		if (!St->bStepStarted)
		{
			St->bStepStarted = true; St->StepStartS = Now;
			UE_LOG(LogAsdaPhantomTest, Log, TEXT("САМОТЕСТ-ФАНТОМ шаг %s"), *Stp.Name);
			if (Stp.Start) { Stp.Start(); }
			return;
		}
		const double El = Now - St->StepStartS;
		const bool bDone = !Stp.Poll || Stp.Poll();
		if (!bDone && El < Stp.TimeoutSec) { return; }
		if (!bDone) { St->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с (фаза %d)"), Stp.TimeoutSec, St->Phase)); }
		if (Stp.Check) { Stp.Check(); }
		++St->StepIdx; St->bStepStarted = false;
	}), 0.01f, true, KitArgs.Delay + 2.5f);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCombatPhantom(
	AsdaPhantomTestLocal::kCmd,
	TEXT("Самотест «фантомный урон»: меч — 4026 и микроход (показаны только применённые сервером 2 из 4), «спам» атака → ход → клик "
	     "(второй 4026 не раньше серверных часов, по два применённых на 4026), серия без перерыва (все 4, фантомов нет); баллиста — "
	     "одно значение за ответ. Контроль — ключи asda.Combat.OwnHitsServerCommit 0 и asda.Combat.RestartSeriesGate 0 (дефект виден). "
	     "Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, AsdaPhantomTestLocal::kCmd);
		FAsdaCombatPhantomSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
