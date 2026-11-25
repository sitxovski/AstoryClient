// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «СТРЕЛА НА ТЕТИВЕ НЕ ОСТАЁТСЯ В РУКЕ» `asda.Bow.SelfTestNockSwap` (05.11.2026, баг игрока arrow-stuck). [ПОРТ]
//   Жалоба: смена оружия посреди боя (с лука) оставляет меш стрелы навсегда торчащим из кисти. Разбор и правка — `World/AsdaNockedArrow.h`
//   (ключ `asda.Bow.NockLifecycle`). Только офлайн (`-AsdaNoNet`): входящие — штатный приём `DebugFeedPacket` (= `HandlePacket` сокета),
//   исходящие — перехват (`AsdaSelfTestKit::ArmCapture`). Свой персонаж — `Asda.World.FakeSelfSpawn`, сосед — `DebugSpawnFakePeer` (путь
//   снапшота 4005/4065), мобы — запись 4013. Наблюдаемое — компонент `AsdaNockedArrow` персонажа (виден/нет, `AsdaNockedArrow::IsVisible`).
//   Сценарии (каждый — с ключом 1 и контрольной парой с ключом 0, где дефект обязан быть виден):
//     1) свой бой луком по мобу путём игрока (клик → 4026, ответ 4029 из четырёх значений): стрела легла; смена оружия штатной точкой
//        своей экипировки `ApplySelfEquipPart(9, меч)` (= 5001 → контейнер 3; сервер её не рассылает) посреди серии — стрелы нет сразу
//        и ни разу за следующие 3 с (запланированные выстрелы лука её не возвращают);
//     2) сосед стреляет из лука по мобу (4021 лук, 4029), посреди серии 4021 меч — стрела снята и не возвращается;
//     3) сосед A стреляет из лука по соседу B (PvP 4202/4204); серия кончилась (пакетов больше нет, цикл замаха снят) — стрелы нет
//        (у прежнего порта в PvP-ветке снятия не было вовсе — стрела оставалась навсегда);
//     4) смерть стреляющего соседа посреди серии (4017 своего персонажа нет — путь `SetAnimState(Dead)` соседа) — стрелы нет.
//   Своё состояние (номер, оружие, флаги боя, соседи, мобы, ключи) возвращается в итоге.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"                         // запись полей тел пакетов
#include "AsdaCVarSave.h"                      // ключи на время прогона (вернутся в итоге)
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — офлайн повторно на отложенном старте   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"                   // запись 4013
#include "Test/AsdaSelfTestKit.h"              // каркас: аргументы, отказ, перехват, итог, выход
#include "World/AsdaAttackRhythmSubsystem.h"   // сброс серверных часов атаки в итоге (изоляция пакета)
#include "World/AsdaAttackExit.h"              // общий выход своего персонажа в покой
#include "World/AsdaNockedArrow.h"             // наблюдаемое: стрела на тетиве
#include "World/AsdaPlayerController.h"        // клик по цели, флаги боя
#include "World/AsdaPlayerCharacter.h"         // свой персонаж и соседи
#include "World/AsdaWorldSubsystem.h"          // мобы, соседи, своя экипировка
#include "World/AsdaCoord.h"                   // игра ↔ клетки
#include "Items/AsdaItemTableSubsystem.h"      // оружие нужного подтипа
#include "UI/AsdaInventorySubsystem.h"         // EAsdaEquipSlot — слот оружия в 4021
#include "Npc/AsdaMobActor.h"                  // моб
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaNockTest, Log, All);

namespace AsdaNockTestLocal
{
	const TCHAR* const kCmd = TEXT("asda.Bow.SelfTestNockSwap");   // имя команды
	const TCHAR* const kKey = TEXT("asda.Bow.NockLifecycle");      // ключ исправления
	constexpr int32 kMobType = 1;          // волк alpen (как у прочих самотестов боя)
	constexpr int32 kSpdDenom = 250;       // знаменатель скорости 4013
	constexpr int32 kPeerA = 7931, kPeerB = 7932;   // соседи: A — стрелок, B — цель PvP
	constexpr double kWatchS = 3.0;        // сколько следить за возвратом стрелы после события (с)

	// Шаг исполнителя.
	struct FStep
	{
		FString Name;                  // имя шага (в строках журнала)
		float TimeoutSec = 12.f;       // потолок шага (с реального времени) — дальше провал «не завершился»
		TFunction<void()> Start;       // начало шага (один раз)
		TFunction<bool()> Poll;        // опрос каждый тик: true — шаг завершён
		TFunction<void()> Check;       // проверки по завершении шага
	};

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> World;            // мир прогона
		TWeakObjectPtr<UAsdaNetSubsystem> Net;   // сеть (подача пакетов, свой номер)
		TArray<FStep> Steps;                     // шаги
		int32 StepIdx = 0;                       // текущий шаг
		bool bStepStarted = false;               // `Start` текущего шага уже вызван
		double StepStartS = 0.0;                 // начало шага (реальное время)
		FTimerHandle Tick;                       // таймер исполнителя
		bool bQuit = false;                      // выйти по окончании (`quit`)
		FAsdaSelfTestRunGate Gate{ kCmd, false };   // офлайн-гейт на отложенном старте
		int32 Ok = 0, Fail = 0;                  // счёт проверок
		uint32 Gen = 0;                          // поколение перехвата исходящих
		int32 N26 = 0;                           // перехвачено 4026 (заявка своей атаки) за сценарий
		TUniquePtr<FAsdaNetSelfScope> SelfScope; // свой номер/смерть вернутся в итоге
		bool bWasInGame = false;                 // флаг «в игре» до прогона
		int32 SavedWeapon = 0;                   // своё оружие до прогона (вернётся штатной точкой экипировки)
		FIntPoint HomeTile = FIntPoint::ZeroValue;   // клетка своего персонажа
		int32 Bow = 0, BowWt = 8, Sword = 0;     // предметы: лук (и его подтип), одноручный меч
		TArray<int32> Rids;                      // мобы прогона (убрать в итоге)
		TArray<int32> PeersUp;                   // соседи прогона (убрать штатным 4011)
		int32 NextRid = 31301;                   // следующий rid моба
		// Сценарий.
		int32 Key = 1;                           // значение ключа в сценарии
		int32 Rid = -1;                          // моб сценария
		int32 Phase = 0;                         // фаза сценария
		double TEvent = -1.0;                    // момент события (смена оружия/конец серии/смерть), реальное время
		bool bNockBefore = false;                // стрела была видна до события
		bool bNockRightAfter = false;            // стрела видна сразу после события (тот же кадр)
		int32 NockFramesAfter = 0;               // кадров опроса со стрелой после события (окно `kWatchS`)
		int32 FramesAfter = 0;                   // кадров опроса после события
		bool bNockAtEnd = false;                 // стрела видна на конце окна

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AAsdaPlayerCharacter* Peer(int32 Idx) const { const UAsdaWorldSubsystem* W = WS(); return W ? Cast<AAsdaPlayerCharacter>(W->GetPlayerActorByUserNum(Idx)) : nullptr; }
		AAsdaMobActor* Mob(int32 R) const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(R) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaNockTest, Log, TEXT("САМОТЕСТ-СТРЕЛА %s %s: %s"), bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }
		int32 NewRid() { const int32 R = NextRid++; Rids.Add(R); return R; }
		void SpawnPeer(int32 Idx, const TCHAR* Name, const FIntPoint& Tile)
		{
			if (UAsdaWorldSubsystem* W = WS()) { W->DebugSpawnFakePeer(Idx, Name, AsdaCoord::TileCenterGame(Tile)); PeersUp.AddUnique(Idx); }
		}
		// Убрать соседа штатным путём (4011 → `OnEntityDespawn`).
		void DespawnPeer(int32 Idx)
		{
			TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], (int16)Idx);
			Feed(4011, B);
			PeersUp.Remove(Idx);
		}
		// Наблюдение после события: каждый кадр опроса — видна ли стрела у персонажа.
		void Watch(const AActor* Who)
		{
			const bool bVis = AsdaNockedArrow::IsVisible(Who);
			++FramesAfter;
			if (bVis) { ++NockFramesAfter; }
			bNockAtEnd = bVis;
		}
		// Сброс сценария: свой бой, мобы прошлого сценария, наблюдения.
		void ResetScenario(int32 InKey)
		{
			if (AAsdaPlayerController* P = PC())
			{
				AsdaAttackExit::StopSelfActionToIdle(GI(), TEXT("самотест стрелы: сброс"));
				P->bAutoAttack = false; P->bCombatStarted = false; P->bInCombatStance = false; P->bWaitingCombatAck = false;
				P->NextAttackTickTime = 0.0; P->TargetActor = nullptr;
				P->GetWorldTimerManager().ClearTimer(P->DeferredAttackSendTimer);
				P->CancelRoute();
			}
			if (UAsdaNetSubsystem* Nn = N()) { Nn->ClearMoveRequestInFlight(TEXT("самотест стрелы: сброс сценария")); }
			if (AAsdaPlayerCharacter* C = Ch()) { C->StopAttackAnim(); C->AttackFaceTarget = nullptr; }
			for (const int32 R : Rids) { if (AAsdaMobActor* M = Mob(R)) { M->Destroy(); } }
			Rids.Reset();
			const TArray<int32> Copy = PeersUp;
			for (const int32 Idx : Copy) { DespawnPeer(Idx); }
			Key = InKey;
			AsdaCVarSave::SetForTest(kKey, InKey);
			Rid = -1; Phase = 0; TEvent = -1.0; N26 = 0;
			bNockBefore = bNockRightAfter = bNockAtEnd = false; NockFramesAfter = FramesAfter = 0;
		}
		// Итог окна наблюдения после события: с ключом 1 стрелы нет ни разу, с ключом 0 (контроль) дефект виден.
		void CheckAfter(const TCHAR* Event, bool bControlMustShow)
		{
			Expect(bNockBefore, FString::Printf(TEXT("до события «%s» стрела на тетиве видна (бой луком идёт)"), Event));
			if (Key != 0)
			{
				Expect(!bNockRightAfter && NockFramesAfter == 0 && !bNockAtEnd,
					FString::Printf(TEXT("ключ 1: после «%s» стрелы нет — сразу %s, кадров со стрелой %d из %d за %.1f с, на конце окна %s"),
						Event, bNockRightAfter ? TEXT("ЕСТЬ") : TEXT("нет"), NockFramesAfter, FramesAfter, kWatchS, bNockAtEnd ? TEXT("ЕСТЬ") : TEXT("нет")));
			}
			else if (bControlMustShow)
			{
				Expect(NockFramesAfter > 0,
					FString::Printf(TEXT("КОНТРОЛЬ ключ 0: дефект виден — после «%s» стрела в кисти %d кадров из %d (на конце окна %s)"),
						Event, NockFramesAfter, FramesAfter, bNockAtEnd ? TEXT("есть") : TEXT("нет")));
			}
		}
	};

	// Тело 4029 (49 б, `Recv_CastState@0xF41620`): @0 i16 атакующий, @2 i16 rid, @4 i32 HP цели, @8 i32[4] очередь, @24 sub,
	//   @25 i16[4] второй канал (−1), @33 i32[4] (0).
	using AsdaTestPackets::Body4029;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.
	// Тело 4021 (9 б, `Recv_CharFullSpawn@0xF3E970`): @0 i16 userIdx, @2 i16 слот (9 — оружие), @4 i32 предмет, @8 заточка.
	static TArray<uint8> Body4021Weapon(int32 UserIdx, int32 ItemId)
	{
		TArray<uint8> B; B.SetNumZeroed(9);
		AsdaBytes::WrI16(&B[0], (int16)UserIdx);
		AsdaBytes::WrI16(&B[2], (int16)EAsdaEquipSlot::Weapon);
		AsdaBytes::WrI32(&B[4], ItemId);
		return B;
	}
	// 4202 (53 б): @0 код, @1 i16 атакующий, @3 i16 цель, @13 i32[4] урон, @29 i16[4] навык (−1), @37 i32[4].
	static TArray<uint8> Body4202(uint8 Code, int32 Atk, int32 Tgt, const TArray<int32>& Q)
	{
		TArray<uint8> B; B.SetNumZeroed(53);
		B[0] = Code;
		AsdaBytes::WrI16(&B[1], (int16)Atk); AsdaBytes::WrI16(&B[3], (int16)Tgt);
		for (int32 i = 0; i < 4 && i < Q.Num(); ++i) { AsdaBytes::WrI32(&B[13 + 4 * i], Q[i]); }
		for (int32 i = 0; i < 4; ++i) { AsdaBytes::WrI16(&B[29 + 2 * i], -1); }
		return B;
	}
	// 4204 (49 б): @0 i16 атакующий, @2 i16 цель, @8 i32[4] урон, @24 код, @25 i16[4] навык (−1), @33 i32[4].
	static TArray<uint8> Body4204(uint8 Code, int32 Atk, int32 Tgt, const TArray<int32>& Q)
	{
		TArray<uint8> B; B.SetNumZeroed(49);
		AsdaBytes::WrI16(&B[0], (int16)Atk); AsdaBytes::WrI16(&B[2], (int16)Tgt);
		for (int32 i = 0; i < 4 && i < Q.Num(); ++i) { AsdaBytes::WrI32(&B[8 + 4 * i], Q[i]); }
		B[24] = Code;
		for (int32 i = 0; i < 4; ++i) { AsdaBytes::WrI16(&B[25 + 2 * i], -1); }
		return B;
	}
	// Первый предмет оружия подтипа `Wt` с дальностью (как у прочих самотестов боя).
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

	static void Run(const TArray<FString>& Args, UWorld* W);
}

void AsdaNockTestLocal::Run(const TArray<FString>& Args, UWorld* W)
{
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 4026, 4028, 4201, 4203 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { UE_LOG(LogAsdaNockTest, Warning, TEXT("САМОТЕСТ-СТРЕЛА: нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate.bQuit = KitArgs.bQuit;

	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout;
		St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};

	// ─────────────────────── Подготовка ───────────────────────
	Add(TEXT("подготовка"), 2.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* PC = S->PC(); UAsdaWorldSubsystem* WSs = S->WS();
		if (!C || !PC || !WSs) { S->Expect(false, TEXT("нет своего персонажа/контроллера/мира после FakeSelfSpawn")); return; }
		const FVector2D Home = AsdaCoord::TileCenterGame(AsdaCoord::GameToTile(C->GetGamePosXZ()));
		S->HomeTile = AsdaCoord::GameToTile(Home);
		C->NetSnap(Home);
		S->SavedWeapon = Net->SelfWeaponItemId;
		S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = 4321; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		TWeakPtr<FState> WSp = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [WSp](uint16 Op, const uint8*, int32, bool, TCHAR)
		{
			TSharedPtr<FState> St = WSp.Pin();
			if (St && Op == 4026) { ++St->N26; }
		}, (uint16)0, kCmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected;
		Net->bInGameConnected = true;
		const UAsdaItemTableSubsystem* IT = S->GI() ? S->GI()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		S->Sword = FindWeapon(IT, 1);
		S->Bow = FindWeapon(IT, 8); S->BowWt = 8;
		if (S->Bow <= 0) { S->Bow = FindWeapon(IT, 10); S->BowWt = 10; }
		S->Expect(S->Bow > 0 && S->Sword > 0, FString::Printf(TEXT("оружие: лук (подтип %d) %d, меч %d"), S->BowWt, S->Bow, S->Sword));
		UE_LOG(LogAsdaNockTest, Log, TEXT("САМОТЕСТ-СТРЕЛА дом: клетка (%d,%d), своё оружие до прогона %d"), S->HomeTile.X, S->HomeTile.Y, S->SavedWeapon);
	}, []() { return true; }, []() {});

	for (const int32 K : { 1, 0 })
	{
		// ─────────────── 1. Свой бой луком по мобу → смена оружия своей экипировкой посреди серии ───────────────
		Add(FString::Printf(TEXT("1 свой лук → меч посреди серии, NockLifecycle %d"), K), 14.f, [S, K]()
		{
			S->ResetScenario(K);
			UAsdaWorldSubsystem* WSs = S->WS();
			if (!WSs) { return; }
			WSs->ApplySelfEquipPart(9, S->Bow);   // путь игрока: надеть лук (5001 → контейнер 3)
			AAsdaPlayerCharacter* C = S->Ch();
			S->Expect(C && C->EquippedWeaponSubType == S->BowWt, FString::Printf(TEXT("лук надет штатной точкой: в руке подтип %d"), C ? C->EquippedWeaponSubType : -1));
			S->Rid = S->NewRid();
			S->Feed(4013, AsdaTestPackets::MobSpawn4013(S->Rid, kMobType, S->HomeTile + FIntPoint(3, 0), /*State*/1, kSpdDenom));
		},
		[S]()
		{
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch(); AAsdaMobActor* M = S->Mob(S->Rid);
			UAsdaNetSubsystem* Nn = S->N(); UAsdaWorldSubsystem* WSs = S->WS();
			if (!PC || !C || !Nn || !WSs) { return false; }
			if (S->Phase == 0 && M && S->El() > 0.5) { PC->SetTarget(M); PC->EngageMobFromClick(M); S->Phase = 1; }
			else if (S->Phase == 1 && S->N26 > 0)
			{
				// Ответ на заявку (как живой сервер): четыре значения — серия выстрелов на несколько проходов.
				S->Feed(4029, Body4029((int32)Nn->SelfUserNum, S->Rid, 60, { 10, 10, 10, 10 }));
				S->Phase = 2;
			}
			else if (S->Phase == 2 && AsdaNockedArrow::IsVisible(C))
			{
				S->bNockBefore = true;
				WSs->ApplySelfEquipPart(9, S->Sword);   // ★ смена оружия посреди серии путём игрока (сервер её не рассылает)
				S->bNockRightAfter = AsdaNockedArrow::IsVisible(C);
				S->TEvent = FPlatformTime::Seconds(); S->Phase = 3;
			}
			else if (S->Phase == 3)
			{
				S->Watch(C);
				return FPlatformTime::Seconds() - S->TEvent >= kWatchS;
			}
			return false;
		},
		[S]()
		{
			AAsdaPlayerCharacter* C = S->Ch();
			S->Expect(C && C->EquippedWeaponSubType == 1, FString::Printf(TEXT("после смены в руке меч (подтип %d)"), C ? C->EquippedWeaponSubType : -1));
			S->CheckAfter(TEXT("смена своего лука на меч"), /*bControlMustShow=*/true);
		});

		// ─────────────── 2. Сосед стреляет по мобу → 4021 меч посреди серии ───────────────
		Add(FString::Printf(TEXT("2 сосед: лук → меч (4021) посреди серии, NockLifecycle %d"), K), 14.f, [S, K]()
		{
			S->ResetScenario(K);
			S->SpawnPeer(kPeerA, TEXT("СтрелокА"), S->HomeTile + FIntPoint(0, -4));
			S->Feed(4021, Body4021Weapon(kPeerA, S->Bow));
			S->Rid = S->NewRid();
			S->Feed(4013, AsdaTestPackets::MobSpawn4013(S->Rid, kMobType, S->HomeTile + FIntPoint(3, -4), /*State*/1, kSpdDenom));
		},
		[S]()
		{
			AAsdaPlayerCharacter* P = S->Peer(kPeerA); AAsdaMobActor* M = S->Mob(S->Rid);
			if (!P || !M) { return false; }
			if (S->Phase == 0 && S->El() > 0.5)
			{
				S->Expect(P->EquippedWeaponSubType == S->BowWt, FString::Printf(TEXT("4021: у соседа в руке лук (подтип %d)"), P->EquippedWeaponSubType));
				S->Feed(4029, Body4029(kPeerA, S->Rid, 60, { 10, 10, 10, 10 }));
				S->Phase = 1;
			}
			else if (S->Phase == 1 && AsdaNockedArrow::IsVisible(P))
			{
				S->bNockBefore = true;
				S->Feed(4021, Body4021Weapon(kPeerA, S->Sword));   // ★ сосед сменил оружие (штатный 4021)
				S->bNockRightAfter = AsdaNockedArrow::IsVisible(P);
				S->TEvent = FPlatformTime::Seconds(); S->Phase = 2;
			}
			else if (S->Phase == 2)
			{
				S->Watch(P);
				return FPlatformTime::Seconds() - S->TEvent >= kWatchS;
			}
			return false;
		},
		[S]()
		{
			AAsdaPlayerCharacter* P = S->Peer(kPeerA);
			S->Expect(P && P->EquippedWeaponSubType == 1, FString::Printf(TEXT("4021: у соседа в руке меч (подтип %d)"), P ? P->EquippedWeaponSubType : -1));
			S->CheckAfter(TEXT("4021 соседа: лук → меч"), /*bControlMustShow=*/true);
		});

		// ─────────────── 3. PvP: сосед A стреляет по соседу B, серия кончилась ───────────────
		Add(FString::Printf(TEXT("3 PvP сосед A лук по B: конец серии, NockLifecycle %d"), K), 25.f, [S, K]()
		{
			S->ResetScenario(K);
			S->SpawnPeer(kPeerA, TEXT("СтрелокА"), S->HomeTile + FIntPoint(2, -1));
			S->SpawnPeer(kPeerB, TEXT("ЦельБ"), S->HomeTile + FIntPoint(5, -1));
			S->Feed(4021, Body4021Weapon(kPeerA, S->Bow));
			S->Feed(4021, Body4021Weapon(kPeerB, S->Sword));
		},
		[S]()
		{
			AAsdaPlayerCharacter* A = S->Peer(kPeerA);
			if (!A || !S->Peer(kPeerB)) { return false; }
			if (S->Phase == 0 && S->El() > 0.5)
			{
				S->Feed(4202, Body4202(1, kPeerA, kPeerB, { 7, 7, 7, 7 }));
				S->Feed(4204, Body4204(0, kPeerA, kPeerB, {}));   // замах A (очередь уже есть)
				S->Phase = 1;
			}
			else if (S->Phase == 1 && AsdaNockedArrow::IsVisible(A)) { S->bNockBefore = true; S->Phase = 2; }
			else if (S->Phase == 2 && !A->IsAttackAnimActive() && !A->IsAttackLoopAlive())
			{
				// Серия кончилась: пакетов больше нет, цикл замаха снят (эталон — переход в покой, `clearActiveEffectSlots`).
				S->bNockRightAfter = false;   // момент конца серии — по опросу, «сразу» не меряется
				S->TEvent = FPlatformTime::Seconds(); S->Phase = 3;
			}
			else if (S->Phase == 3)
			{
				// Первые 0.1 с после конца цикла — кадр снятия; дальше стрела обязана отсутствовать.
				if (FPlatformTime::Seconds() - S->TEvent > 0.1) { S->Watch(A); }
				return FPlatformTime::Seconds() - S->TEvent >= kWatchS;
			}
			return false;
		},
		[S]()
		{
			S->Expect(S->Phase == 3 || S->Phase == 2, FString::Printf(TEXT("PvP-серия соседа отстреляна (фаза %d)"), S->Phase));
			S->CheckAfter(TEXT("конец PvP-серии лука"), /*bControlMustShow=*/true);
		});

		// ─────────────── 4. Смерть стреляющего соседа посреди серии ───────────────
		Add(FString::Printf(TEXT("4 сосед: смерть посреди серии лука, NockLifecycle %d"), K), 14.f, [S, K]()
		{
			S->ResetScenario(K);
			S->SpawnPeer(kPeerA, TEXT("СтрелокА"), S->HomeTile + FIntPoint(0, -4));
			S->Feed(4021, Body4021Weapon(kPeerA, S->Bow));
			S->Rid = S->NewRid();
			S->Feed(4013, AsdaTestPackets::MobSpawn4013(S->Rid, kMobType, S->HomeTile + FIntPoint(3, -4), /*State*/1, kSpdDenom));
		},
		[S]()
		{
			AAsdaPlayerCharacter* P = S->Peer(kPeerA); AAsdaMobActor* M = S->Mob(S->Rid);
			if (!P || !M) { return false; }
			if (S->Phase == 0 && S->El() > 0.5) { S->Feed(4029, Body4029(kPeerA, S->Rid, 60, { 10, 10, 10, 10 })); S->Phase = 1; }
			else if (S->Phase == 1 && AsdaNockedArrow::IsVisible(P))
			{
				S->bNockBefore = true;
				P->SetAnimState(EAsdaPlayerState::Dead);   // смерть соседа (точка `SetAnimState`, общая для 4053/4017 игрока)
				S->bNockRightAfter = AsdaNockedArrow::IsVisible(P);
				S->TEvent = FPlatformTime::Seconds(); S->Phase = 2;
			}
			else if (S->Phase == 2)
			{
				S->Watch(P);
				return FPlatformTime::Seconds() - S->TEvent >= kWatchS;
			}
			return false;
		},
		[S]()
		{
			// Контроль ключом 0: прежний порт снимал стрелу по таймеру серии — дефект может не проявиться в окне, его не требуем.
			S->CheckAfter(TEXT("смерть стреляющего соседа"), /*bControlMustShow=*/false);
		});
	}

	// ─────────────────────── Итог ───────────────────────
	Add(TEXT("итог"), 1.f, [S, Net]()
	{
		S->ResetScenario(1);
		if (UAsdaWorldSubsystem* WSs = S->WS()) { WSs->ApplySelfEquipPart(9, S->SavedWeapon); }   // своё оружие — штатной точкой
		if (UAsdaAttackRhythmSubsystem* Rh = Net->GetGameInstance() ? Net->GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr) { Rh->DebugReset(); }
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), kCmd, false, LogAsdaNockTest, TEXT("САМОТЕСТ-СТРЕЛА"), S->Ok, S->Fail, TEXT(""));
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
			UE_LOG(LogAsdaNockTest, Log, TEXT("САМОТЕСТ-СТРЕЛА шаг %s"), *Stp.Name);
			if (Stp.Start) { Stp.Start(); }
			return;
		}
		const bool bDone = !Stp.Poll || Stp.Poll();
		if (!bDone && Now - St->StepStartS < Stp.TimeoutSec) { return; }
		if (!bDone) { St->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с (фаза %d)"), Stp.TimeoutSec, St->Phase)); }
		if (Stp.Check) { Stp.Check(); }
		++St->StepIdx; St->bStepStarted = false;
	}), 0.01f, true, KitArgs.Delay + 2.5f);
}

// Консольная команда самотеста (только вне Shipping).
static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestNockSwap(
	TEXT("asda.Bow.SelfTestNockSwap"),
	TEXT("Самотест «стрела на тетиве не остаётся в руке»: свой лук → меч штатной точкой экипировки посреди серии, сосед лук → меч "
	     "(4021), PvP-серия соседа из лука до конца, смерть стреляющего соседа; каждый — с ключом asda.Bow.NockLifecycle 1 и "
	     "контролем 0. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Bow.SelfTestNockSwap"));
		AsdaNockTestLocal::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
