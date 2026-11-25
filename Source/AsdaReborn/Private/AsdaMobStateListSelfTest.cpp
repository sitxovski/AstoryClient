// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ИММУНИТЕТ МОБА ВИДЕН НА ЦЕЛИ» `asda.Buff.SelfTestMobStates` (06.11.2026, баг Player5 27.10 «Spade Immunity do not show»). [ПОРТ]
//   Только офлайн (`-AsdaNoNet`), входящие — штатный приём `DebugFeedPacket`. Разбор и ключи — у `AsdaMobAddListedBuffs` (AsdaEffectsSubsystem.cpp).
//   Тела: запись 4013 Spade — живая раскладка (Player52 01.11: тип 290, HP 66150, список @63 `9A 00 FF FF…`), 8012 — живое тело навыка 29
//   по себе (Player6 20.10: шапка `00 1D 00 19 00 01 00 19 00`, запись `00 19 00 00 00 00 00 03…`; у Spade код 0x14 — навык 20), 4017 — список @8.
//   Сценарии:
//     1) контроль: оба ключа 0 — ни запись 4013, ни 4017 усиление не кладут (дефект виден);
//     2) Spade входит в обзор записью 4013 со списком [154] — состояние на мобе (путь оригинала `Recv_MobSpawn`);
//     3) Spade на глазах: 8012 «самоиммунитет» + 4017 [154, 5] — состояние 5 на мобе, значок под полосой HP цели;
//     4) 4017 [154] — иммунитет снят, значка нет;
//     5) шахматный моб (305): 8012 навык 29 + 4017 [5, 6] — оба значка;
//     6) вредное в списке (3 оглушение, 68 кровь: buff0SubType ≠ 1) записей не заводит — их каналы 4102/6079/свой удар;
//     7) повторная 4013 по живущему мобу несёт список, 4019 по живущему — нет (`Recv_MobSpawnLoop`), 4019 с новым мобом — да.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"   // ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"                   // запись 4013
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaEffectsSubsystem.h"           // списки состояний
#include "UI/AsdaTargetFrameSubsystem.h"       // значок под полосой HP цели
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

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMobStatesTest, Log, All);

namespace AsdaMobStatesTestLocal
{
	const TCHAR* const kCmd = TEXT("asda.Buff.SelfTestMobStates");   // имя команды
	constexpr int32 kSelfFallback = 4324;    // свой userNum, если не известен
	constexpr int32 kSpade = 290;            // тип моба Spade
	constexpr int32 kBishop = 305;           // шахматный моб с навыком 29
	constexpr int32 kSpdDenom = 250;         // знаменатель скорости 4013
	constexpr int32 kRid0 = 30991, kRid1 = 30992, kRid2 = 30993, kRid3 = 30994, kRid4 = 30995;   // мобы сценариев

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
		bool bWasInGame = false;
		FIntPoint HomeTile = FIntPoint::ZeroValue;
		TArray<int32> Mobs;

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaEffectsSubsystem* Fx() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr; }
		UAsdaTargetFrameSubsystem* TF() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaTargetFrameSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AAsdaMobActor* Mob(int32 R) const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(R) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaMobStatesTest, Log, TEXT("САМОТЕСТ-МОБ-СОСТОЯНИЯ %s %s: %s"), bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }
		bool MobHas(int32 Rid, int32 State) const
		{
			const UAsdaEffectsSubsystem* F = Fx(); if (!F) { return false; }
			for (const FAsdaEffectEntry& E : F->GetMobList(Rid)) { if (E.Type == 0 && E.Id == State) { return true; } }
			return false;
		}
		// Значок состояния на панели цели (слоты 0..27).
		bool IconOf(int32 State) const
		{
			const UAsdaTargetFrameSubsystem* F = TF(); if (!F) { return false; }
			for (int32 i = 0; i < 28; ++i) { if (F->DebugEnemyStateId(i) == State && F->DebugEnemyStateIconShown(i)) { return true; } }
			return false;
		}
		void Keys(int32 Spawn, int32 List)
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Buff.MobSpawnStates"), Spawn);
			AsdaCVarSave::SetForTest(TEXT("asda.Buff.MobListBuffs"), List);
		}
		void Target(int32 Rid) { if (AAsdaPlayerController* P = PC()) { P->SetTarget(Mob(Rid)); } }
	};

	// Запись 4013 со списком усилений @63 (id) и глубинами @119.
	static TArray<uint8> Record(int32 Rid, int32 Type, const FIntPoint& Tile, const TArray<int32>& States)
	{
		TArray<uint8> B = AsdaTestPackets::MobSpawn4013(Rid, Type, Tile, 1, kSpdDenom);
		AsdaBytes::WrI32(&B[9], 66150);
		for (int32 i = 0; i < 28; ++i) { AsdaBytes::WrI16(&B[63 + 2 * i], States.IsValidIndex(i) ? States[i] : -1); }
		return B;
	}
	// 8012 (шапка 9 б + запись 29 б): навык на себя — вид цели 0, цель = моб, запись вид 0, индекс = моб, флаги 3.
	static TArray<uint8> Body8012Self(int32 Skill, int32 Rid)
	{
		TArray<uint8> B; B.SetNumZeroed(9 + 29);
		AsdaBytes::WrI16(&B[1], Skill); AsdaBytes::WrI16(&B[3], Rid); B[5] = 1; AsdaBytes::WrI16(&B[7], Rid);
		uint8* R = B.GetData() + 9;
		AsdaBytes::WrI16(R + 1, Rid); R[7] = 3; AsdaBytes::WrI16(R + 19, -1);
		return B;
	}
	// 4017 (100 б) flags=3 — живой моб, список состояний, HP@92.
	static TArray<uint8> Body4017(int32 Type, int32 Rid, const TArray<int32>& States)
	{
		TArray<uint8> B; B.SetNumZeroed(100);
		AsdaBytes::WrI16(&B[0], Type); AsdaBytes::WrI16(&B[2], Rid); AsdaBytes::WrI32(&B[4], 3);
		for (int32 i = 0; i < 28; ++i) { AsdaBytes::WrI16(&B[8 + 2 * i], States.IsValidIndex(i) ? States[i] : -1); }
		AsdaBytes::WrI32(&B[92], 66150);
		return B;
	}
}

struct FAsdaMobStatesSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaMobStatesSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaMobStatesTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 4026, 4028 };
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
	auto Wait = [S](double Sec) { return [S, Sec]() { return S->El() > Sec; }; };

	// ── 0. подготовка ──
	Add(TEXT("подготовка"), 5.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		if (!C || !S->PC() || !S->WS() || !S->Fx()) { S->Expect(false, TEXT("нет своего персонажа/мира/эффектов после FakeSelfSpawn")); return; }
		S->HomeTile = AsdaCoord::GameToTile(C->GetGamePosXZ());
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = kSelfFallback; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [](uint16, const uint8*, int32, bool, TCHAR) {}, (uint16)0, kCmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected; Net->bInGameConnected = true;
	}, Wait(0.3), []() {});

	// ── 1. контроль: оба ключа 0 — дефект виден ──
	Add(TEXT("1 контроль: ключи 0"), 4.f, [S, T]()
	{
		S->Keys(0, 0);
		S->Feed(4013, Record(kRid0, kSpade, T(2, 0), { 154 })); S->Mobs.AddUnique(kRid0);
	}, [S]() { return S->Mob(kRid0) && S->El() > 0.6; }, [S]()
	{
		S->Target(kRid0);
		S->Feed(8012, Body8012Self(20, kRid0));
		S->Feed(4017, Body4017(kSpade, kRid0, { 154, 5 }));
		S->Expect(S->Mob(kRid0) != nullptr, TEXT("Spade в мире"));
		S->Expect(!S->MobHas(kRid0, 154), TEXT("ключ 0: список записи 4013 не читается (прежний порт)"));
		S->Expect(!S->MobHas(kRid0, 5), TEXT("ключ 0: иммунитет из 4017 не заводится — значка нет (дефект воспроизведён)"));
		S->Keys(1, 1);
		TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kRid0); S->Feed(4011, B);
	});

	// ── 2. вход в обзор с усилением ──
	Add(TEXT("2 Spade входит в обзор: 4013 [154]"), 4.f, [S, T]()
	{
		S->Feed(4013, Record(kRid1, kSpade, T(2, 1), { 154 })); S->Mobs.AddUnique(kRid1);
	}, [S]() { return S->Mob(kRid1) && S->El() > 0.6; }, [S]()
	{
		S->Expect(S->MobHas(kRid1, 154), TEXT("состояние 154 из записи 4013 на мобе (путь `Recv_MobSpawn`)"));
	});

	// ── 3. иммунитет на глазах ──
	Add(TEXT("3 Spade: 8012 навык 20 + 4017 [154, 5]"), 3.f, [S]()
	{
		S->Target(kRid1);
		S->Feed(8012, Body8012Self(20, kRid1));
		S->Feed(4017, Body4017(kSpade, kRid1, { 154, 5 }));
	}, Wait(0.6), [S]()
	{
		S->Expect(S->MobHas(kRid1, 5), TEXT("иммунитет 5 на мобе"));
		S->Expect(S->IconOf(5), TEXT("значок иммунитета под полосой HP цели"));
		S->Expect(S->IconOf(154), TEXT("значок состояния 154 рядом"));
	});

	// ── 4. снятие ──
	Add(TEXT("4 4017 [154]: иммунитет снят"), 3.f, [S]() { S->Feed(4017, Body4017(kSpade, kRid1, { 154 })); }, Wait(0.5), [S]()
	{
		S->Expect(!S->MobHas(kRid1, 5) && !S->IconOf(5), TEXT("иммунитет снят, значка нет"));
		S->Expect(S->MobHas(kRid1, 154), TEXT("154 остался"));
	});

	// ── 5. шахматный моб ──
	Add(TEXT("5 шахматный 305: навык 29 + 4017 [5, 6]"), 4.f, [S, T]()
	{
		S->Feed(4013, Record(kRid2, kBishop, T(-2, 0), {})); S->Mobs.AddUnique(kRid2);
	}, [S]() { return S->Mob(kRid2) && S->El() > 0.6; }, [S]()
	{
		S->Target(kRid2);
		S->Feed(8012, Body8012Self(29, kRid2));
		S->Feed(4017, Body4017(kBishop, kRid2, { 5, 6 }));
		S->Expect(S->MobHas(kRid2, 5) && S->MobHas(kRid2, 6), TEXT("оба состояния 5 и 6 на мобе"));
	});
	Add(TEXT("5б значки 5 и 6"), 2.f, nullptr, Wait(0.5), [S]()
	{
		S->Expect(S->IconOf(5) && S->IconOf(6), TEXT("значки 5 и 6 под полосой HP цели"));
	});

	// ── 6. вредное в списке записей не заводит ──
	Add(TEXT("6 4017 [3, 68]: вредное не заводится"), 2.f, [S]()
	{
		S->Feed(4017, Body4017(kBishop, kRid2, { 3, 68 }));
	}, Wait(0.3), [S]()
	{
		S->Expect(!S->MobHas(kRid2, 3) && !S->MobHas(kRid2, 68), TEXT("оглушение и кровь (buff0SubType ≠ 1) из списка не заведены — их каналы 4102/6079/свой удар"));
		S->Expect(!S->MobHas(kRid2, 5) && !S->MobHas(kRid2, 6), TEXT("5 и 6 сняты списком без них"));
	});

	// ── 7. повторная 4013 и 4019 ──
	Add(TEXT("7 повторные записи"), 4.f, [S, T]()
	{
		S->Feed(4013, Record(kRid2, kBishop, T(-2, 0), { 51 }));   // живущий моб: повторная 4013 — список применяется
		S->Feed(4019, Record(kRid2, kBishop, T(-2, 0), { 38 }));   // живущий моб: 4019 — нет (`Recv_MobSpawnLoop`)
		S->Feed(4019, Record(kRid3, kBishop, T(-3, 0), { 38 })); S->Mobs.AddUnique(kRid3);   // новый моб: 4019 — да
	}, [S]() { return S->Mob(kRid3) && S->El() > 0.6; }, [S]()
	{
		S->Expect(S->MobHas(kRid2, 51), TEXT("повторная 4013 по живущему: щит 51 на мобе"));
		S->Expect(!S->MobHas(kRid2, 38), TEXT("4019 по живущему список не применяет"));
		S->Expect(S->MobHas(kRid3, 38), TEXT("4019 с новым мобом: 38 на мобе"));
	});

	// ── итог ──
	Add(TEXT("итог"), 3.f, [S]()
	{
		for (const int32 Rid : S->Mobs) { TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], Rid); S->Feed(4011, B); }
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(nullptr); }
	}, Wait(0.5), [S, Net]()
	{
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), kCmd, false, LogAsdaMobStatesTest, TEXT("САМОТЕСТ-МОБ-СОСТОЯНИЯ"), S->Ok, S->Fail);
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
			UE_LOG(LogAsdaMobStatesTest, Log, TEXT("САМОТЕСТ-МОБ-СОСТОЯНИЯ шаг %s"), *Stp.Name);
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

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestMobStates(
	TEXT("asda.Buff.SelfTestMobStates"),
	TEXT("Самотест «иммунитет моба виден на цели»: список усилений записи 4013/4019 и 4017 → состояние и значок цели (Spade 290, навык 20; шахматный "
	     "305, навык 29), снятие 4017, вредное не заводится; контроль ключами asda.Buff.MobSpawnStates / asda.Buff.MobListBuffs 0. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Buff.SelfTestMobStates"));
		FAsdaMobStatesSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
