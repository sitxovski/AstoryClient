// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «АТАКА ВЕРХОМ» (06.11.2026): `asda.SelfTest.MountAttack [quit]`.
//   Баг игрока: «выбрал моба верхом — модель транспорта пропала, нотис 2038 "You cannot attack while riding a vehicle.", атака не идёт,
//   пока не сядешь и не слезешь снова» (живой журнал 03.11 11:21:11). Разбор — у ключа `asda.Mount.ListHoldsModel`
//   (AsdaWorldSubsystem_Peers.cpp). Офлайн, путём игрока: клик `EngageMobFromClick`/`EngagePlayerFromClick`, навык `DispatchByKind`,
//   исходящие — перехват, входящие — штатный разбор `DebugFeedPacket` (5453, 5454, 6071, 4027, 4029, 4202, 4021).
//   Ветки:
//     A) верхом клик по мобу: 4026 ушёл, замаха нет, модель на месте; 4027 код 8 → нотис, автоатака снята, модель на месте, повторов нет;
//     B) настоящая высадка 5454 + 6071 → модель снята, клик → 4026, замах, 4029: защёлки нет;
//     C) ключ 0: прежнее — модель снята на замахе при живом транспорте (воспроизведение бага);
//     D) навык верхом: нотис, 5256 нет; после 5454 — 5256 ушёл;
//     E) PvP верхом: 4201 ушёл, замаха нет; 4202 код 8 → атака снята, модель на месте;
//     F) смена вещей верхом (своя и 4021 соседа): модель остаётся (эталон `SetupModelFromItem`); ключ 0 — снималась;
//     G) смерть верхом при живом транспорте: модель остаётся до 5454;
//     P) снятие вещи верхом путём игрока (`UnequipItem`, двойной клик, ответ 5001, ключ `asda.Mount.DismountOnUnequip`): подтверждённое снятие
//        шлёт 5455, 5454 + 6071 ссаживают; отказ, молчание, переход, замена вещи и ключ 0 — верхом, 5455 нет; на ходу — пересчёт 4006;
//     W) нажатие «слезть» (5455): запись снята сразу, навык и атака в окне до 5454 не получают отказа (ключ `asda.Mount.SelfListGate`);
//     H) подход к мобу верхом, удар сбил с транспорта (5454 + 6071): 4006 пересчёта к той же цели (`asda.Move.SpeedChangeResync`),
//        автоатака жива, дошли — 4026 и замах.
//   Итог: «АТАКА-ВЕРХОМ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"   // ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaSkillUseSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"
#include "Npc/AsdaMobActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMountAtkTest, Log, All);

namespace AsdaMountAtkTest
{
	const TCHAR* const kCmd = TEXT("asda.SelfTest.MountAttack");   // имя команды
	const TCHAR* const kPrefix = TEXT("АТАКА-ВЕРХОМ");             // префикс строк
	const TCHAR* const kKey = TEXT("asda.Mount.ListHoldsModel");   // ключ правки
	constexpr int32 kSelfFallback = 4324;   // свой номер, если не известен
	constexpr int32 kMount = 522;           // транспорт (SubType 15)
	constexpr int32 kPeer = 7832;           // сосед
	constexpr int32 kMobType = 1;           // волк alpen
	constexpr int32 kSpdDenom = 250;        // знаменатель скорости 4013
	constexpr int32 kMobHp = 5000;          // HP моба
	constexpr int32 kSword = 1;             // подтип «одноручный меч»
	constexpr int32 kSkill = 503;           // навык для ветки D
	constexpr float kMountSpeed = 0.56f;    // скорость верхом
	constexpr float kRunSpeed = 0.35f;      // скорость пешком

	// Ловит строки нотисов отказа (категории CombatRefused/SkillRefused) — проверка не зависит от языка.
	struct FNoticeTap : public FOutputDevice
	{
		int32 Combat = 0, Skill = 0;   // показано нотисов
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName& Cat) override
		{
			if (Cat != FName(TEXT("LogAsdaMsg"))) { return; }
			const FString L(V);
			if (L.Contains(TEXT("CombatRefused"))) { ++Combat; }
			if (L.Contains(TEXT("SkillRefused")))  { ++Skill; }
		}
	};

	// Шаг прогона.
	struct FStep { FString Name; float TimeoutSec = 10.f; TFunction<void()> Start; TFunction<bool()> Poll; TFunction<void()> Check; };

	// Состояние прогона.
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
		FNoticeTap Tap;
		bool bWasInGame = false, bWasStandSkill = false;
		int32 Self = -1; FIntPoint HomeTile = FIntPoint::ZeroValue; FVector2D Home = FVector2D::ZeroVector;
		int32 SavedWeapon = 0, SavedEquipped = -1, Sword = 0, Rid = -1, NextRid = 31971, Phase = 0;
		int32 N26 = 0, N28 = 0, N4201 = 0, N5256 = 0, N4006 = 0, N5455 = 0, Mark = 0, Mark2 = 0;
		FString OutDir; int32 Chest = 0;   // папка кадров, нагрудник ветки P
		FVector2D LastCur = FVector2D::ZeroVector, LastDst = FVector2D::ZeroVector;
		TArray<int32> Rids;

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaSkillUseSubsystem* SU() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AAsdaMobActor* Mob(int32 R) const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(R) : nullptr; }
		AAsdaPlayerCharacter* Peer() const { const UAsdaWorldSubsystem* W = WS(); return W ? Cast<AAsdaPlayerCharacter>(W->ActorByTarget(EAsdaTargetKind::Character, kPeer)) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		static bool Swinging(const AAsdaPlayerCharacter* C) { return C && (C->IsAttackAnimActive() || C->IsAttackLoopAlive()); }
		static bool Shown(const AAsdaPlayerCharacter* C) { return C && C->IsMounted() && C->MountMesh && C->MountMesh->IsRegistered() && C->MountMesh->IsVisible(); }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			const TCHAR* Nm = Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?");
			if (bOk) { UE_LOG(LogAsdaMountAtkTest, Log, TEXT("%s ok   %s: %s"), kPrefix, Nm, *What); }
			else     { UE_LOG(LogAsdaMountAtkTest, Error, TEXT("%s FAIL %s: %s"), kPrefix, Nm, *What); }
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }
		void Speed(int32 Uidx, float V)
		{
			TArray<uint8> B; B.SetNumZeroed(10);
			AsdaBytes::WrI16(&B[0], (int16)Uidx); FMemory::Memcpy(&B[6], &V, 4);
			Feed(6071, B);
		}
		void Mount()
		{
			TArray<uint8> B; B.SetNumZeroed(10);
			AsdaBytes::WrI16(&B[0], (int16)Self); AsdaBytes::WrI32(&B[2], kMount); AsdaBytes::WrI32(&B[6], -1);
			Feed(5453, B); Speed(Self, kMountSpeed);
		}
		void Dismount()
		{
			TArray<uint8> B; B.SetNumZeroed(6);
			AsdaBytes::WrI16(&B[0], (int16)Self); AsdaBytes::WrI32(&B[2], kMount);
			Feed(5454, B); Speed(Self, kRunSpeed);
		}
		// 4027 (53 б): код@0, атакующий@1, цель@3.
		void Refuse4027(int32 Code)
		{
			TArray<uint8> B; B.SetNumZeroed(53);
			B[0] = (uint8)Code; AsdaBytes::WrI16(&B[1], (int16)Self); AsdaBytes::WrI16(&B[3], -1);
			for (int32 i = 0; i < 4; ++i) { AsdaBytes::WrI32(&B[9 + 4 * i], -1); }
			Feed(4027, B);
		}
		// 4202 (53 б): код@0, атакующий@1, цель@3.
		void Refuse4202(int32 Code)
		{
			TArray<uint8> B; B.SetNumZeroed(53);
			B[0] = (uint8)Code; AsdaBytes::WrI16(&B[1], (int16)Self); AsdaBytes::WrI16(&B[3], (int16)kPeer);
			Feed(4202, B);
		}
		// 4029 (49 б): принятый удар.
		void Accept4029(int32 R)
		{
			TArray<uint8> B; B.SetNumZeroed(49);
			AsdaBytes::WrI16(&B[0], (int16)Self); AsdaBytes::WrI16(&B[2], (int16)R); AsdaBytes::WrI32(&B[4], kMobHp - 10);
			for (int32 i = 0; i < 4; ++i) { AsdaBytes::WrI32(&B[8 + 4 * i], 10); }
			for (int32 i = 0; i < 4; ++i) { AsdaBytes::WrI16(&B[25 + 2 * i], -1); }
			Feed(4029, B);
		}
		// 4206 (13 б): свой userIdx, режим@7 = 2, команда@12 (1 — включить, 0 — выключить).
		void Pvp4206(uint8 Cmd)
		{
			TArray<uint8> B; B.SetNumZeroed(13);
			AsdaBytes::WrI16(&B[0], (int16)Self); B[7] = 2; B[12] = Cmd;
			Feed(4206, B);
		}
		UAsdaInventorySubsystem* Inv() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaInventorySubsystem>() : nullptr; }
		// 5001 (24 б): итог 1, переезд 2, откуда (ячейка, контейнер), куда (ячейка, контейнер) — штатный разбор и очередь инвентаря.
		void Move5001(int32 SrcCont, int32 SrcCell, int32 DstCont, int32 DstCell)
		{
			TArray<uint8> B; B.SetNumZeroed(24);
			B[0] = 1; B[1] = 2; AsdaBytes::WrI16(&B[2], (int16)SrcCell); B[6] = (uint8)SrcCont;
			AsdaBytes::WrI16(&B[13], (int16)DstCell); B[17] = (uint8)DstCont;
			Feed(5001, B);
			if (UAsdaInventorySubsystem* I = Inv()) { I->DebugPumpPendingPackets(); }
		}
		void Shot(const TCHAR* Tag)
		{
			if (OutDir.IsEmpty()) { return; }
			FScreenshotRequest::RequestScreenshot(OutDir / FString::Printf(TEXT("%s_%s.png"), FAsdaLang::Code(FAsdaLang::Current()), Tag), /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		}
		void Grant4007(float V) { if (UAsdaNetSubsystem* Nn = N()) { Nn->OnEntityMove.Broadcast(Self, 1, LastCur, LastDst, V, false, 2, -1); } }
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
			if (UAsdaNetSubsystem* Nn = N()) { Nn->ClearMoveRequestInFlight(TEXT("самотест: сброс сценария")); }
			if (C) { C->StopAttackAnim(); C->AttackFaceTarget = nullptr; C->NetSnap(Home); }
			for (const int32 R : Rids) { if (AAsdaMobActor* M = Mob(R)) { M->Destroy(); } }
			Rids.Reset(); Rid = -1; Phase = 0;
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

	// Оружие подтипа Wt: тип 9, не вариант 1, с дальностью.
	static int32 FindWeapon(const UAsdaItemTableSubsystem* IT, int32 Wt, int32 Skip = 0)
	{
		if (!IT) { return 0; }
		for (int32 Code = 20000; Code < 80000; ++Code)
		{
			const FAsdaItemRecord* R = IT->GetItem(Code);
			if (Code != Skip && R && R->ItemType == 9 && R->ItemSubType == Wt && R->ItemVariant != 1 && R->AttackRangeTiles > 0) { return Code; }
		}
		return 0;
	}
	// 5251 — изученный навык.
	static TArray<uint8> Body5251(int32 Code)
	{
		TArray<uint8> B; B.SetNumZeroed(34); AsdaBytes::WrI16(B.GetData(), (int16)Code); B[2] = 5; B[3] = 1; return B;
	}
}

struct FAsdaMountAttackSelfTest { static void Run(const TArray<FString>& Args, UWorld* W); };

void FAsdaMountAttackSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaMountAtkTest;
	const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 4024, 4026, 4028, 4201, 4203, 5256 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net; S->World = W; S->bQuit = KA.bQuit; S->Gate.bQuit = KA.bQuit;
	for (const FString& A : KA.Pos) { if (A.StartsWith(TEXT("out="))) { S->OutDir = A.Mid(4); } }
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout; St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	auto T = [S](int32 Dx, int32 Dy) { return S->HomeTile + FIntPoint(Dx, Dy); };

	// ── подготовка ──
	Add(TEXT("0 подготовка"), 5.f, [S, Net, T]()
	{
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* P = S->PC();
		if (!C || !P || !S->WS()) { S->Expect(false, TEXT("нет своего персонажа/мира")); return; }
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
			if (Op == 4201) { ++St->N4201; }
			if (Op == 5256) { ++St->N5256; }
			if (Op == 5455) { ++St->N5455; }
			if (Op == 4006 && Pl && Len >= 37)
			{
				++St->N4006;
				St->LastCur = FVector2D(AsdaBytes::RdF32(Pl + 16 + 5), AsdaBytes::RdF32(Pl + 16 + 9));
				St->LastDst = FVector2D(AsdaBytes::RdF32(Pl + 16 + 13), AsdaBytes::RdF32(Pl + 16 + 17));
			}
		}, (uint16)0, kCmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected; Net->bInGameConnected = true;
		S->bWasStandSkill = Net->bStandCaptureSkillUse; Net->bStandCaptureSkillUse = true;
		S->SavedWeapon = Net->SelfWeaponItemId; S->SavedEquipped = C->EquippedWeaponSubType;
		S->Sword = FindWeapon(S->GI()->GetSubsystem<UAsdaItemTableSubsystem>(), kSword);
		Net->SelfWeaponItemId = S->Sword;
		C->EquippedWeaponSubType = kSword;
		S->Expect(S->Sword > 0, FString::Printf(TEXT("меч: предмет %d"), S->Sword));
		S->Feed(5251, Body5251(kSkill));
		Net->SetVitalsMp(S->Self, 99999, 99999, TEXT("самотест атаки верхом"), true);
		AsdaCVarSave::SetForTest(kKey, 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Move.SpeedChangeResync"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.ServerRhythm"), 1);
		GLog->AddOutputDevice(&S->Tap);
		S->WS()->DebugSpawnFakePeer(kPeer, TEXT("СоседВерхом"), AsdaCoord::TileCenterGame(T(-1, 1)));
	}, [S]() { return S->El() > 1.0 && S->Peer(); }, [S]() { S->Expect(S->Peer() != nullptr, TEXT("сосед на месте")); });

	// ── A. Верхом клик по мобу ──
	Add(TEXT("A клик по мобу верхом"), 6.f, [S, T]()
	{
		S->ResetFight(); S->Mount(); S->Rid = S->SpawnMob(T(1, 0)); S->Mark = S->N26; S->Mark2 = S->Tap.Combat;
	}, [S]()
	{
		AAsdaMobActor* M = S->Mob(S->Rid); AAsdaPlayerController* P = S->PC();
		if (S->Phase == 0 && M && P && S->El() > 0.5) { P->SetTarget(M); P->EngageMobFromClick(M); S->Phase = 1; }
		if (S->Phase == 1 && S->N26 > S->Mark)
		{
			AAsdaPlayerCharacter* C = S->Ch();
			S->Expect(S->N26 == S->Mark + 1, TEXT("A1 ушёл один 4026 (как эталон: верхом заявка уходит)"));
			S->Expect(FState::Shown(C), TEXT("A2 на отправке модель транспорта на месте"));
			S->Expect(!FState::Swinging(C), TEXT("A3 замаха нет (ждём ответа сервера)"));
			S->Refuse4027(8);
			S->Phase = 2; S->StepStartS = FPlatformTime::Seconds();
		}
		return S->Phase == 2 && S->El() > 1.6;
	}, [S]()
	{
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* P = S->PC();
		S->Expect(S->Tap.Combat == S->Mark2 + 1, FString::Printf(TEXT("A4 4027 код 8: нотис показан один раз (%d)"), S->Tap.Combat - S->Mark2));
		S->Expect(FState::Shown(C), TEXT("A5 после отказа модель на месте, всадник в седле"));
		S->Expect(P && !P->bAutoAttack && !FState::Swinging(C), TEXT("A6 автоатака снята, замаха нет"));
		S->Expect(S->N26 == S->Mark + 1 && S->N28 == 0, FString::Printf(TEXT("A7 за 1.6 с повторных заявок нет (4026 +%d, 4028 %d)"), S->N26 - S->Mark, S->N28));
		S->Expect(S->N() && !S->N()->IsMoveRequestInFlight(), TEXT("A8 бит «запрос хода в пути» снят (клик не застрянет в очереди)"));
	});

	// ── B. Настоящая высадка: защёлки нет ──
	Add(TEXT("B высадка и атака"), 6.f, [S]()
	{
		S->Dismount(); S->Mark = S->N26; S->Phase = 0;
	}, [S]()
	{
		AAsdaMobActor* M = S->Mob(S->Rid); AAsdaPlayerController* P = S->PC();
		if (S->Phase == 0 && M && P && S->El() > 0.3) { P->SetTarget(M); P->EngageMobFromClick(M); S->Phase = 1; }
		if (S->Phase == 1 && S->N26 > S->Mark) { S->Accept4029(S->Rid); S->Phase = 2; S->StepStartS = FPlatformTime::Seconds(); }
		return S->Phase == 2 && S->El() > 0.4;
	}, [S]()
	{
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* P = S->PC();
		S->Expect(C && !C->IsMounted(), TEXT("B1 5454: транспорт снят"));
		S->Expect(S->N26 > S->Mark, TEXT("B2 клик после высадки: 4026 ушёл"));
		S->Expect(FState::Swinging(C), TEXT("B3 замах идёт"));
		S->Expect(P && P->bAutoAttack, TEXT("B4 автоатака жива — никакой защёлки"));
	});

	// ── C. Ключ 0: исходный баг ──
	Add(TEXT("C ключ 0"), 6.f, [S, T]()
	{
		S->ResetFight(); AsdaCVarSave::SetForTest(kKey, 0); S->Mount(); S->Rid = S->SpawnMob(T(1, 0)); S->Mark = S->N26;
	}, [S]()
	{
		AAsdaMobActor* M = S->Mob(S->Rid); AAsdaPlayerController* P = S->PC();
		if (S->Phase == 0 && M && P && S->El() > 0.5) { P->SetTarget(M); P->EngageMobFromClick(M); S->Phase = 1; }
		return S->Phase == 1 && S->N26 > S->Mark;
	}, [S]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		S->Expect(C && !C->IsMounted(), TEXT("C1 ключ 0: модель снята на отправке — баг воспроизведён"));
		S->Refuse4027(8);
		AsdaCVarSave::SetForTest(kKey, 1);
		S->Dismount();
	});

	// ── D. Навык верхом ──
	Add(TEXT("D навык верхом"), 3.f, [S]()
	{
		S->ResetFight(); S->Mount(); S->Mark = S->N5256; S->Mark2 = S->Tap.Skill;
		if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DispatchByKind(kSkill, -1); }
	}, [S]() { return S->El() > 0.3; }, [S]()
	{
		S->Expect(S->N5256 == S->Mark, TEXT("D1 верхом 5256 не ушёл"));
		S->Expect(S->Tap.Skill == S->Mark2 + 1, FString::Printf(TEXT("D2 нотис отказа навыка (%d)"), S->Tap.Skill - S->Mark2));
		S->Expect(FState::Shown(S->Ch()), TEXT("D3 модель на месте"));
		S->Dismount();
		// Навык уходит отложенным действием 102: здесь проверяем, что гейты пройдены (нотиса 2038 больше нет).
		const int32 NoticeWas = S->Tap.Skill;
		UAsdaSkillUseSubsystem* Us = S->SU();
		const bool bDispatched = Us && Us->DispatchByKind(kSkill, -1);
		S->Expect(bDispatched && S->Tap.Skill == NoticeWas, FString::Printf(TEXT("D4 после 5454 гейты навыка пройдены (принят %d, новых нотисов %d)"), bDispatched ? 1 : 0, S->Tap.Skill - NoticeWas));
		if (UAsdaSkillUseSubsystem* U = S->SU()) { U->SimulateCastResult5257(0, kSkill); }
	});

	// ── E. PvP верхом ──
	Add(TEXT("E PvP верхом"), 4.f, [S]()
	{
		S->ResetFight(); S->Mount(); S->Mark = S->N4201; S->Mark2 = S->Tap.Combat;
		S->Pvp4206(1);   // режим PvP включён (без него клик по игроку не ведёт к атаке)
		AAsdaPlayerController* P = S->PC(); AAsdaPlayerCharacter* B = S->Peer();
		if (P && B) { P->SetTarget(B); P->EngagePlayerFromClick(B); }
	}, [S]()
	{
		if (S->Phase == 0 && S->N4201 > S->Mark)
		{
			AAsdaPlayerCharacter* C = S->Ch();
			S->Expect(FState::Shown(C) && !FState::Swinging(C), TEXT("E1 4201 ушёл: модель на месте, замаха нет"));
			S->Refuse4202(8);
			S->Phase = 1; S->StepStartS = FPlatformTime::Seconds();
		}
		return S->Phase == 1 && S->El() > 0.5;
	}, [S]()
	{
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* P = S->PC();
		S->Expect(FState::Shown(C), TEXT("E2 4202 код 8: всадник в седле"));
		S->Expect(P && !P->bPvpAttack && !FState::Swinging(C), TEXT("E3 PvP-атака снята"));
		S->Pvp4206(0);
	});

	// ── F. Смена вещей верхом ──
	Add(TEXT("F смена вещей верхом"), 3.f, [S]()
	{
		S->ResetFight();
		UAsdaItemTableSubsystem* IT = S->GI()->GetSubsystem<UAsdaItemTableSubsystem>();
		const int32 Other = FindWeapon(IT, kSword, S->Sword);
		AAsdaPlayerCharacter* C = S->Ch();
		S->WS()->ApplySelfEquipPart(9, Other);
		S->Expect(FState::Shown(C), FString::Printf(TEXT("F1 смена оружия на %d: модель транспорта на месте"), Other));
		S->WS()->ApplySelfEquipPart(9, 0);
		S->Expect(FState::Shown(C), TEXT("F2 снятие оружия: модель на месте"));
		S->WS()->ApplySelfEquipPart(9, S->Sword);
		// Сосед верхом, часть экипа 4021.
		TArray<uint8> Bm; Bm.SetNumZeroed(10);
		AsdaBytes::WrI16(&Bm[0], (int16)kPeer); AsdaBytes::WrI32(&Bm[2], kMount); AsdaBytes::WrI32(&Bm[6], -1);
		S->Feed(5453, Bm);
		S->Expect(FState::Shown(S->Peer()), TEXT("F3 сосед сел (5453)"));
		TArray<uint8> Bp; Bp.SetNumZeroed(9);
		AsdaBytes::WrI16(&Bp[0], (int16)kPeer); AsdaBytes::WrI16(&Bp[2], 6); AsdaBytes::WrI32(&Bp[4], Other);
		S->Feed(4021, Bp);
	}, [S]() { return S->El() > 0.4; }, [S]()
	{
		S->Expect(FState::Shown(S->Peer()), TEXT("F4 4021 соседа: модель транспорта на месте"));
		// Ключ 0 — прежнее: смена вещей снимала модель.
		AsdaCVarSave::SetForTest(kKey, 0);
		AAsdaPlayerCharacter* C = S->Ch();
		S->WS()->ApplySelfEquipPart(9, 0);
		S->Expect(C && !C->IsMounted(), TEXT("F5 ключ 0: смена вещей сняла модель (воспроизведение)"));
		S->WS()->ApplySelfEquipPart(9, S->Sword);
		AsdaCVarSave::SetForTest(kKey, 1);
		S->Dismount();
		TArray<uint8> Bx; Bx.SetNumZeroed(6);
		AsdaBytes::WrI16(&Bx[0], (int16)kPeer); AsdaBytes::WrI32(&Bx[2], kMount);
		S->Feed(5454, Bx);
		AAsdaPlayerCharacter* B = S->Peer();
		S->Expect(B && !B->IsMounted(), TEXT("F6 5454 соседа снимает транспорт"));
	});

	// ── G. Смерть верхом при живом транспорте ──
	Add(TEXT("G смерть верхом"), 2.f, [S]()
	{
		S->ResetFight(); S->Mount();
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->SetAnimState(EAsdaPlayerState::Dead); }
	}, [S]() { return S->El() > 0.2; }, [S]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		S->Expect(FState::Shown(C), TEXT("G1 смерть при живой записи транспорта: модель на месте (эталон)"));
		if (C) { C->SetAnimState(EAsdaPlayerState::Idle); }
		S->Dismount();
		S->Expect(C && !C->IsMounted(), TEXT("G2 5454: снят"));
	});

	// ── P. Снятие и смена вещей верхом, путь игрока ──
	Add(TEXT("P снятие вещей верхом: подготовка"), 2.f, [S]()
	{
		S->ResetFight();
		UAsdaInventorySubsystem* I = S->Inv();
		UAsdaItemTableSubsystem* IT = S->GI()->GetSubsystem<UAsdaItemTableSubsystem>();
		for (int32 Code = 20000; Code < 80000 && IT && !S->Chest; ++Code)
		{
			const FAsdaItemRecord* R = IT->GetItem(Code);
			if (R && R->ItemType == 1 && R->CategoryFlag != 3) { S->Chest = Code; }
		}
		S->Expect(I && S->Chest > 0, FString::Printf(TEXT("P0 нагрудник %d"), S->Chest));
		if (!I) { return; }
		using EM = UAsdaInventorySubsystem::EExternalRecordMode;
		I->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(S->Sword, 3, 9, 1), EM::AsIs);
		I->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(S->Chest, 3, 1, 1), EM::AsIs);
		I->ApplyExternalRecord(FAsdaItemRecordWriter::ClearCell(1, 8), EM::AsIs);
		I->ApplyExternalRecord(FAsdaItemRecordWriter::ClearCell(1, 9), EM::AsIs);
		S->WS()->ApplySelfEquipPart(9, S->Sword); S->WS()->ApplySelfEquipPart(1, S->Chest);
		S->Mount(); S->Mark = S->N5455;
	}, [S]() { return S->El() > 0.6; }, [S]() { S->Expect(FState::Shown(S->Ch()), TEXT("P1 верхом, вещи надеты")); S->Shot(TEXT("P_before")); });
	Add(TEXT("P снятие: 5455 и 5454"), 3.f, [S]()
	{
		UAsdaInventorySubsystem* I = S->Inv();
		S->Expect(I && I->UnequipItem(9), TEXT("P2 запрос снятия оружия ушёл"));
		S->Expect(S->N5455 == S->Mark, TEXT("P3 до ответа 5001 команды «слезть» нет"));
		S->Move5001(3, 9, 1, 8);
		const FAsdaItemInstance* C = I ? I->GetCell(3, 9) : nullptr;
		S->Expect(C && C->IsEmpty(), TEXT("P4 5001: оружие снято"));
		S->Expect(S->N5455 == S->Mark + 1, FString::Printf(TEXT("P5 после снятия ушёл один 5455 (+%d)"), S->N5455 - S->Mark));
		S->Expect(FState::Shown(S->Ch()) && S->Ch()->IsMountHeldByServer(), TEXT("P6 до 5454 всадник верхом, запись (15, 2) не снята заранее"));
		S->Expect(I && I->UseByDoubleClick(3, 1), TEXT("P7 двойной клик по броне: запрос ушёл"));
		S->Move5001(3, 1, 1, 9);
		S->Expect(S->N5455 == S->Mark + 1, TEXT("P8 второе снятие до 5454: повторного 5455 нет"));
		S->Dismount();
	}, [S]() { return S->El() > 0.5; }, [S]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		S->Expect(C && !C->IsMounted() && !C->IsMountHeldByServer(), TEXT("P9 5454: спешен, клиент и сервер согласованы"));
		S->Expect(S->N() && FMath::IsNearlyEqual(S->N()->LastServerSpeed, kRunSpeed, 0.001f), FString::Printf(TEXT("P10 6071: скорость %.4f"), S->N() ? S->N()->LastServerSpeed : -1.f));
		S->Shot(TEXT("P_dismounted"));
	});
	Add(TEXT("P отказ сервера"), 4.f, [S]()
	{
		S->Move5001(1, 8, 3, 9);
		S->Move5001(1, 9, 3, 1);
		S->Mount(); S->Mark = S->N5455;
	}, [S]() { return S->El() > 2.2; }, [S]()
	{
		UAsdaInventorySubsystem* I = S->Inv();
		S->Expect(I && I->UnequipItem(9), TEXT("P11 запрос снятия ушёл"));
		TArray<uint8> B; B.SetNumZeroed(24);
		B[0] = 2; AsdaBytes::WrI16(&B[2], 9); B[6] = 3; B[17] = 1;
		S->Feed(5001, B);
		if (I) { I->DebugPumpPendingPackets(); }
		const FAsdaItemInstance* C = I ? I->GetCell(3, 9) : nullptr;
		S->Expect(C && !C->IsEmpty() && S->N5455 == S->Mark && FState::Shown(S->Ch()), TEXT("P12 отказ 5001: вещь на месте, 5455 нет, верхом"));
	});
	Add(TEXT("P молчание сервера"), 3.f, [S]()
	{
		UAsdaInventorySubsystem* I = S->Inv();
		S->Expect(I && I->UnequipItem(9), TEXT("P13 запрос снятия ушёл"));
	}, [S]() { return S->El() > 0.6; }, [S]()
	{
		S->Expect(S->N5455 == S->Mark && FState::Shown(S->Ch()), TEXT("P14 ответа нет: 5455 нет, верхом"));
		S->Move5001(3, 9, 1, 8);
		S->Expect(S->N5455 == S->Mark + 1, TEXT("P15 поздний ответ: 5455 ушёл"));
		S->Expect(FState::Shown(S->Ch()) && S->Ch()->IsMountHeldByServer(), TEXT("P16 5454 не пришёл: клиент верхом, как сервер (не защёлкнуто)"));
	});
	Add(TEXT("P повтор после молчания"), 4.f, [S]()
	{
		S->Mark = S->N5455;
		S->Move5001(1, 8, 3, 9);
	}, [S]() { return S->El() > 2.2; }, [S]()
	{
		UAsdaInventorySubsystem* I = S->Inv();
		if (I) { I->UnequipItem(9); }
		S->Move5001(3, 9, 1, 8);
		S->Expect(S->N5455 == S->Mark + 1, TEXT("P17 следующее снятие через 2 с шлёт 5455 снова"));
		S->Move5001(1, 8, 3, 9);
	});
	Add(TEXT("P замена вещи"), 4.f, [S]()
	{
		S->Mark = S->N5455;
		UAsdaInventorySubsystem* I = S->Inv();
		UAsdaItemTableSubsystem* IT = S->GI()->GetSubsystem<UAsdaItemTableSubsystem>();
		const int32 Other = FindWeapon(IT, kSword, S->Sword);
		if (I)
		{
			I->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(Other, 1, 8, 1), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
			TArray<uint8> B; B.SetNumZeroed(24);   // надевание поверх: обмен, подвид 1
			B[0] = 1; B[1] = 1; AsdaBytes::WrI16(&B[2], 8); B[6] = 1; AsdaBytes::WrI16(&B[13], 9); B[17] = 3;
			S->Feed(5001, B); I->DebugPumpPendingPackets();
		}
	}, [S]() { return S->El() > 2.2; }, [S]()
	{
		UAsdaInventorySubsystem* I = S->Inv();
		S->Expect(S->N5455 == S->Mark && FState::Shown(S->Ch()), TEXT("P18 надевание поверх: 5455 нет, верхом"));
		// Своя заявка «снять», а сервер ответил обменом: слот занят — не ссаживаем.
		if (I) { I->UnequipItem(9); }
		TArray<uint8> B; B.SetNumZeroed(24);
		B[0] = 1; B[1] = 1; AsdaBytes::WrI16(&B[2], 9); B[6] = 3; AsdaBytes::WrI16(&B[13], 8); B[17] = 1;
		S->Feed(5001, B);
		if (I) { I->DebugPumpPendingPackets(); }
		S->Expect(S->N5455 == S->Mark && FState::Shown(S->Ch()), TEXT("P19 снятие с обменом (слот занят): 5455 нет, верхом"));
	});
	Add(TEXT("P смена карты и ключ 0"), 3.f, [S]()
	{
		UAsdaInventorySubsystem* I = S->Inv();
		UAsdaNetSubsystem* N = S->N();
		if (I) { I->UnequipItem(9); }
		if (N) { N->bTransferInProgress = true; }
		S->Move5001(3, 9, 1, 8);
		if (N) { N->bTransferInProgress = false; }
		S->Expect(S->N5455 == S->Mark && FState::Shown(S->Ch()), TEXT("P20 ответ во время перехода: 5455 нет, верхом"));
		S->Move5001(1, 8, 3, 9);
		AsdaCVarSave::SetForTest(TEXT("asda.Mount.DismountOnUnequip"), 0);
		if (I) { I->UnequipItem(9); }
		S->Move5001(3, 9, 1, 8);
	}, [S]() { return S->El() > 0.3; }, [S]()
	{
		S->Expect(S->N5455 == S->Mark && FState::Shown(S->Ch()), TEXT("P21 ключ 0: как оригинал, игрок верхом"));
		AsdaCVarSave::SetForTest(TEXT("asda.Mount.DismountOnUnequip"), 1);
		S->Move5001(1, 8, 3, 9);
		S->Dismount();
		UAsdaInventorySubsystem* I = S->Inv();
		if (I) { I->UnequipItem(9); }
		S->Move5001(3, 9, 1, 8);
		S->Expect(S->N5455 == S->Mark, TEXT("P22 пешком снятие вещи 5455 не шлёт"));
		using EM = UAsdaInventorySubsystem::EExternalRecordMode;
		if (I)
		{
			I->ApplyExternalRecord(FAsdaItemRecordWriter::ClearCell(3, 1), EM::AsIs);
			I->ApplyExternalRecord(FAsdaItemRecordWriter::ClearCell(1, 8), EM::AsIs);
			I->ApplyExternalRecord(FAsdaItemRecordWriter::ClearCell(1, 9), EM::AsIs);
		}
		S->WS()->ApplySelfEquipPart(9, S->Sword); S->WS()->ApplySelfEquipPart(1, 0);
	});
	// Снятие на подходе к мобу: 5454 + 6071 пересчитывают ход к той же цели, автоатака жива.
	Add(TEXT("P снятие на ходу"), 14.f, [S, T]()
	{
		S->ResetFight();
		if (UAsdaInventorySubsystem* I = S->Inv()) { I->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(S->Chest, 3, 1, 1), UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
		S->Mount(); S->Rid = S->SpawnMob(T(10, 0)); S->Mark = S->N26; S->Mark2 = S->N4006;
	}, [S]()
	{
		AAsdaMobActor* M = S->Mob(S->Rid); AAsdaPlayerController* P = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
		UAsdaInventorySubsystem* I = S->Inv();
		if (S->Phase == 0 && M && P && S->El() > 2.2) { P->SetTarget(M); P->EngageMobFromClick(M); S->Phase = 1; }
		if (S->Phase == 1 && S->N4006 > S->Mark2) { S->Grant4007(kMountSpeed); S->Mark2 = S->N4006; S->Phase = 2; S->StepStartS = FPlatformTime::Seconds(); }
		if (S->Phase == 2 && S->El() > 0.3)
		{
			const int32 Was = S->N5455; const int32 RidWas = C ? C->LastSent4006TargetRid : -2;
			if (I) { I->UnequipItem(1); }
			S->Move5001(3, 1, 1, 9);
			S->Expect(S->N5455 == Was + 1, TEXT("P23 снятие на ходу: 5455 ушёл"));
			S->Dismount();
			S->Expect(S->N4006 == S->Mark2 + 1 && C && C->LastSent4006TargetRid == RidWas, FString::Printf(TEXT("P24 5454 + 6071: 4006 пересчёта к той же цели (+%d)"), S->N4006 - S->Mark2));
			S->Expect(P && P->bAutoAttack && C && !C->IsMounted(), TEXT("P25 спешен, автоатака жива"));
			S->Grant4007(kRunSpeed);
			S->Phase = 3;
		}
		if (S->Phase == 3 && S->N26 > S->Mark) { S->Accept4029(S->Rid); S->Phase = 4; S->StepStartS = FPlatformTime::Seconds(); }
		return S->Phase == 4 && S->El() > 0.3;
	}, [S]()
	{
		S->Expect(FState::Swinging(S->Ch()), TEXT("P26 дошли: 4026 и замах"));
		if (UAsdaInventorySubsystem* I = S->Inv()) { I->ApplyExternalRecord(FAsdaItemRecordWriter::ClearCell(1, 9), UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
	});

	// ── W. Окно между 5455 и 5454 ──
	Add(TEXT("W слезть: окно до 5454"), 6.f, [S, T]()
	{
		S->ResetFight();
		if (UAsdaInventorySubsystem* I = S->Inv()) { I->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(kMount, 1, 8, 1), UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
		S->Mount(); S->Rid = S->SpawnMob(T(1, 0)); S->Mark = S->N5455; S->Mark2 = S->N26;
	}, [S]()
	{
		AAsdaMobActor* M = S->Mob(S->Rid); AAsdaPlayerController* P = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
		if (S->Phase == 0 && M && P && S->El() > 0.5)
		{
			if (UAsdaInventorySubsystem* I = S->Inv()) { I->UseByDoubleClick(1, 8); }
			S->Expect(S->N5455 == S->Mark + 1, FString::Printf(TEXT("W1 «слезть»: 5455 ушёл (+%d)"), S->N5455 - S->Mark));
			S->Expect(C && C->IsMounted() && !C->IsMountHeldByServer(), TEXT("W2 модель ещё на месте, запись (15, 2) снята сразу (эталон)"));
			const int32 NoticeWas = S->Tap.Skill;
			UAsdaSkillUseSubsystem* U = S->SU();
			const bool bOk = U && U->DispatchByKind(kSkill, -1);
			S->Expect(bOk && S->Tap.Skill == NoticeWas, FString::Printf(TEXT("W3 навык в окне до 5454 не отказан (принят %d, нотисов %d)"), bOk ? 1 : 0, S->Tap.Skill - NoticeWas));
			if (U) { U->SimulateCastResult5257(0, kSkill); }
			P->SetTarget(M); P->EngageMobFromClick(M);
			S->Phase = 1;
		}
		if (S->Phase == 1 && S->N26 > S->Mark2)
		{
			S->Dismount();
			S->Accept4029(S->Rid);
			S->Phase = 2; S->StepStartS = FPlatformTime::Seconds();
		}
		return S->Phase == 2 && S->El() > 0.4;
	}, [S]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		S->Expect(C && !C->IsMounted() && FState::Swinging(C), TEXT("W4 атака в окне: 4026 ушёл, 5454 снял модель, замах идёт"));
		if (UAsdaInventorySubsystem* I = S->Inv()) { I->ApplyExternalRecord(FAsdaItemRecordWriter::ClearCell(1, 8), UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
	});

	// ── H. Подход верхом, удар сбил: пересчёт 4006 и атака ──
	Add(TEXT("H подход и удар"), 14.f, [S, T]()
	{
		S->ResetFight(); S->Mount(); S->Rid = S->SpawnMob(T(10, 0)); S->Mark = S->N26; S->Mark2 = S->N4006;
	}, [S]()
	{
		AAsdaMobActor* M = S->Mob(S->Rid); AAsdaPlayerController* P = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
		if (S->Phase == 0 && M && P && S->El() > 0.5) { P->SetTarget(M); P->EngageMobFromClick(M); S->Phase = 1; }
		if (S->Phase == 1 && S->N4006 > S->Mark2) { S->Grant4007(kMountSpeed); S->Mark2 = S->N4006; S->Phase = 2; S->StepStartS = FPlatformTime::Seconds(); }
		if (S->Phase == 2 && S->El() > 0.3)
		{
			const int32 RidWas = C ? C->LastSent4006TargetRid : -2;
			S->Dismount();
			S->Expect(S->N4006 == S->Mark2 + 1, FString::Printf(TEXT("H1 5454 + 6071 на подходе: 4006 пересчёта ушёл (+%d)"), S->N4006 - S->Mark2));
			S->Expect(C && C->LastSent4006TargetRid == RidWas, TEXT("H2 цель 4006 та же"));
			S->Expect(P && P->bAutoAttack, TEXT("H3 автоатака жива"));
			S->Grant4007(kRunSpeed);
			S->Phase = 3;
		}
		if (S->Phase == 3 && S->N26 > S->Mark) { S->Accept4029(S->Rid); S->Phase = 4; S->StepStartS = FPlatformTime::Seconds(); }
		return S->Phase == 4 && S->El() > 0.3;
	}, [S]()
	{
		S->Expect(S->N26 > S->Mark, TEXT("H4 дошли: 4026 ушёл"));
		S->Expect(FState::Swinging(S->Ch()), TEXT("H5 замах идёт"));
	});

	// ── итог ──
	Add(TEXT("итог"), 3.f, [S, Net]()
	{
		S->ResetFight();
		TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kPeer); S->Feed(4011, B);
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(nullptr); }
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = S->SavedEquipped; }
		Net->SelfWeaponItemId = S->SavedWeapon;
	}, [S]() { return S->El() > 0.5; }, [S, Net]()
	{
		GLog->RemoveOutputDevice(&S->Tap);
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame; Net->bStandCaptureSkillUse = S->bWasStandSkill;
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), kCmd, false, LogAsdaMountAtkTest, kPrefix, S->Ok, S->Fail);
	});

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
		}), FMath::Max(0.01f, KA.Delay), false);
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
			UE_LOG(LogAsdaMountAtkTest, Log, TEXT("%s шаг %s"), kPrefix, *Stp.Name);
			if (Stp.Start) { Stp.Start(); }
			return;
		}
		const double El = Now - St->StepStartS;
		const bool bDone = !Stp.Poll || Stp.Poll();
		if (!bDone && El < Stp.TimeoutSec) { return; }
		if (!bDone) { St->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с"), Stp.TimeoutSec)); }
		if (Stp.Check) { Stp.Check(); }
		++St->StepIdx; St->bStepStarted = false;
	}), 0.01f, true, KA.Delay + 2.5f);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestMountAttack(
	TEXT("asda.SelfTest.MountAttack"),
	TEXT("Самотест атаки верхом: клик по мобу и игроку, отказ 8, навык, смена вещей, смерть, подход с ударом, ключ asda.Mount.ListHoldsModel 0. ")
	TEXT("Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.SelfTest.MountAttack"));
		FAsdaMountAttackSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
