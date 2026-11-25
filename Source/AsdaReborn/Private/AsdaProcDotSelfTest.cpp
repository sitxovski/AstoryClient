// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «КРОВОТЕЧЕНИЕ ОТ ОБЫЧНОЙ АТАКИ» `asda.Skill.SelfTestProcDot` (05.11.2026, баг Player52 22.10 «Зазубренная стрела
//   не накладывает кровотечение»). [ПОРТ] Только офлайн (`-AsdaNoNet`), входящие — штатный приём `DebugFeedPacket`.
//   Поток оригинала (разбор — `Reports/status_effect_expiry/SAWTOOTH_PROC.md`):
//     навык 787 «Зазубренная стрела» — бафф на себя, состояние 169 (5272); при обычной атаке NpcServer
//     (`DamageCalculation_PlayerToMonster` sub_45CEC0, NpcServer.exe.c:62191) бросает шанс 20 % (лук/арбалет, класс оружия 9/10/11)
//     и ставит шагу очереди код 169 → 4029 @25; при расходе шага (следующий 4028) кладёт мобу состояние 68 и шлёт 6079
//     (обработчик `ProcessSkill_DotBleed_Percent` sub_43BBD0 → sub_434E80). Клиент: 6079 → `buff_ApplySkillBuff` + `skillInitDotSlot`.
//   Тела 6079 — ДОСЛОВНО живые (меняются только кастер и renderId):
//     silver 01.11 22:57:33 (навык 169): `01 03 00 00 EE 00 A9 00 44 00 01 10 27 00 00 F4 01 0B 00 00 00`;
//     Player50 01.11 15:49:57 (навык 207 «Заточка»): `01 00 00 00 2C 00 CF 00 44 00 05 20 4E 00 00 F4 01 42 01 00 00`.
//   Шаги:
//     1) 4029 с кодом 169 на шаге 2 — состояние НЕ появляется само (эталон: код второго канала даёт только цифру);
//     2) 6079 (навык 169) + 4017 [68] — состояние 68, значок под полосой HP цели, слот тиков 220 по 11, тики и цифры;
//     3) «Заточка» 207: 6079 ДО 4029 (порядок живого журнала) — состояние 68, слот 40 × 322;
//     4) 4017 без 68 снимает состояние; поздний 4029 с кодом 169 его не возвращает.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"   // ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"                   // запись 4013 (спаун моба)
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaEffectsSubsystem.h"           // списки состояний, слоты урона по времени
#include "UI/AsdaTargetFrameSubsystem.h"       // значок под полосой HP цели
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaFloatingTextSubsystem.h"
#include "World/AsdaCoord.h"
#include "Npc/AsdaMobActor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaProcDotTest, Log, All);

namespace AsdaProcDotTestLocal
{
	const TCHAR* const kCmd = TEXT("asda.Skill.SelfTestProcDot");   // имя команды
	constexpr int32 kSelfFallback = 4322;    // свой userNum, если не известен
	constexpr int32 kMobType = 1;            // волк alpen (как у прочих самотестов)
	constexpr int32 kSpdDenom = 250;         // знаменатель скорости 4013
	constexpr int32 kRidSaw = 30951;         // моб «Зазубренной стрелы»
	constexpr int32 kRidSharp = 30952;       // моб «Заточки»
	constexpr int32 kMobHp = 5000;           // HP мобов (живой тик «Заточки» 322 не должен добить моба за шаг)

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
		int32 Self = -1; FIntPoint HomeTile = FIntPoint::ZeroValue;
		TArray<int32> Mobs;
		int32 FloatMax = 0, FloatBase = 0;   // живые надписи: максимум за шаг и база на старте
		bool bFed = false;                   // тело шага уже подано

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaEffectsSubsystem* Fx() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr; }
		UAsdaFloatingTextSubsystem* FT() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaFloatingTextSubsystem>() : nullptr; }
		UAsdaTargetFrameSubsystem* TF() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaTargetFrameSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AAsdaMobActor* Mob(int32 Rid) const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(Rid) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaProcDotTest, Log, TEXT("САМОТЕСТ-PROCDOT %s %s: %s"), bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }
		void Mark() { FloatMax = 0; FloatBase = FT() ? FT()->DebugLiveCount() : 0; bFed = false; }
		void NoteFloats() { if (UAsdaFloatingTextSubsystem* F = FT()) { FloatMax = FMath::Max(FloatMax, F->DebugLiveCount() - FloatBase); } }
		bool MobHas(int32 Rid, int32 State) const
		{
			const UAsdaEffectsSubsystem* F = Fx(); if (!F) { return false; }
			for (const FAsdaEffectEntry& E : F->GetMobList(Rid)) { if (E.Type == 0 && E.Id == State) { return true; } }
			return false;
		}
		int32 MobSource(int32 Rid, int32 State) const   // навык-источник записи (поле +2 эталона)
		{
			const UAsdaEffectsSubsystem* F = Fx(); if (!F) { return -2; }
			for (const FAsdaEffectEntry& E : F->GetMobList(Rid)) { if (E.Type == 0 && E.Id == State) { return E.IdFlag; } }
			return -2;
		}
		void SpawnMob(int32 Rid, const FIntPoint& Tile)
		{
			TArray<uint8> B = AsdaTestPackets::MobSpawn4013(Rid, kMobType, Tile, 1, kSpdDenom);
			AsdaBytes::WrI32(&B[9], kMobHp);   // HP записи 4013 (@9)
			Feed(4013, B); Mobs.AddUnique(Rid);
		}
	};

	// Живое тело 6079 (21 б) с подставленными кастером (@1) и renderId (@4).
	static TArray<uint8> Live6079(const uint8 (&Raw)[21], int32 Caster, int32 Rid)
	{
		TArray<uint8> B; B.Append(Raw, 21);
		AsdaBytes::WrI16(&B[1], Caster); AsdaBytes::WrI16(&B[4], Rid);
		return B;
	}
	static const uint8 kSaw6079[21]   = { 0x01, 0x03, 0x00, 0x00, 0xEE, 0x00, 0xA9, 0x00, 0x44, 0x00, 0x01, 0x10, 0x27, 0x00, 0x00, 0xF4, 0x01, 0x0B, 0x00, 0x00, 0x00 };
	static const uint8 kSharp6079[21] = { 0x01, 0x00, 0x00, 0x00, 0x2C, 0x00, 0xCF, 0x00, 0x44, 0x00, 0x05, 0x20, 0x4E, 0x00, 0x00, 0xF4, 0x01, 0x42, 0x01, 0x00, 0x00 };

	// 4029 (53 б, Recv_CastState): кастер@0, цель@2, HP@4, урон i32[4]@8, код@24 (0 = обычный), второй канал i16[4]@25 и i32[4]@33.
	static TArray<uint8> Body4029(int32 Caster, int32 Rid, int32 Hp, const TArray<int32>& Dmg, const TArray<int32>& Codes)
	{
		TArray<uint8> B; B.SetNumZeroed(53);
		AsdaBytes::WrI16(&B[0], Caster); AsdaBytes::WrI16(&B[2], Rid); AsdaBytes::WrI32(&B[4], Hp);
		for (int32 k = 0; k < 4; ++k)
		{
			AsdaBytes::WrI32(&B[8 + 4 * k], Dmg.IsValidIndex(k) ? Dmg[k] : 0);
			AsdaBytes::WrI16(&B[25 + 2 * k], Codes.IsValidIndex(k) ? Codes[k] : -1);
		}
		return B;
	}
	// 4017 (100 б) flags=3 — живой моб, список состояний (первые `States`, остальное −1), HP@92.
	static TArray<uint8> Body4017(int32 Rid, const TArray<int32>& States, int32 Hp)
	{
		TArray<uint8> B; B.SetNumZeroed(100);
		AsdaBytes::WrI16(&B[0], kMobType); AsdaBytes::WrI16(&B[2], Rid); AsdaBytes::WrI32(&B[4], 3);
		for (int32 i = 0; i < 28; ++i) { AsdaBytes::WrI16(&B[8 + 2 * i], States.IsValidIndex(i) ? States[i] : -1); }
		AsdaBytes::WrI32(&B[92], Hp);
		return B;
	}
}

struct FAsdaProcDotSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaProcDotSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaProcDotTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 4026, 4028, 8226 };
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
		// Исходящие (замах 4026/4028, 8226) — только в перехват: офлайн ничего не уходит.
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [](uint16, const uint8*, int32, bool, TCHAR) {}, (uint16)0, kCmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected; Net->bInGameConnected = true;
		AsdaCVarSave::SetForTest(TEXT("asda.Dot.Slots"), 1);
		S->SpawnMob(kRidSaw, T(2, 0)); S->SpawnMob(kRidSharp, T(-2, 0));
	}, [S]() { return S->El() > 1.0 && S->Mob(kRidSaw) && S->Mob(kRidSharp); }, [S]()
	{
		S->Expect(S->Mob(kRidSaw) && S->Mob(kRidSharp), TEXT("мобы на месте"));
	});

	// ── 1. 4029 с кодом 169: состояние само не появляется ──
	Add(TEXT("1 Зазубренная стрела: 4029 с кодом 169"), 2.f, [S]()
	{
		S->Mark();
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(S->Mob(kRidSaw)); }
		// Живой 4029 (silver 01.11 22:57:30): очередь [205 217 291 260], второй канал [-1 -1 169 -1].
		S->Feed(4029, Body4029(S->Self, kRidSaw, kMobHp - 205, { 205, 217, 291, 260 }, { -1, -1, 169, -1 }));
	}, [S]() { return S->El() > 1.3; }, [S]()
	{
		S->Expect(!S->MobHas(kRidSaw, 68), TEXT("код 169 во втором канале 4029 сам кровотечение не кладёт (эталон: только цифра)"));
	});

	// ── 2. 6079 (навык 169) + 4017 [68]: состояние, значок, слот, тики ──
	Add(TEXT("2 Зазубренная стрела: 6079 + 4017 [68]"), 4.f, [S]()
	{
		S->Mark();
		S->Feed(6079, Live6079(kSaw6079, S->Self, kRidSaw));
		const AAsdaMobActor* M = S->Mob(kRidSaw);
		S->Feed(4017, Body4017(kRidSaw, { 68 }, M ? M->CurHp : kMobHp));
	}, [S]() { S->NoteFloats(); return S->El() > 1.3; }, [S]()
	{
		const UAsdaTargetFrameSubsystem* F = S->TF();
		const FAsdaDotSlot* Sl = S->Fx()->FindDotSlot(true, kRidSaw, 68);
		S->Expect(S->MobHas(kRidSaw, 68) && S->MobSource(kRidSaw, 68) == 169,
			FString::Printf(TEXT("состояние 68 «Кровотечение» на мобе, источник навык %d (ждём 169)"), S->MobSource(kRidSaw, 68)));
		S->Expect(F && F->DebugEnemyStateId(0) == 68 && F->DebugEnemyStateIconShown(0),
			FString::Printf(TEXT("значок под полосой HP цели: слот 0 = %d, виден %d"), F ? F->DebugEnemyStateId(0) : -2, (F && F->DebugEnemyStateIconShown(0)) ? 1 : 0));
		S->Expect(Sl && Sl->TickDamage == 11 && Sl->Remaining < 220 && Sl->Remaining >= 0 && (220 - Sl->Remaining) % 11 == 0,
			FString::Printf(TEXT("слот тиков: 10000/500 × 11 = 220, остаток %d, тик %d (тики идут)"), Sl ? Sl->Remaining : -1, Sl ? Sl->TickDamage : -1));
		S->Expect(S->FloatMax >= 1, FString::Printf(TEXT("цифры тиков над мобом: живых надписей до %d"), S->FloatMax));
	});

	// ── 3. «Заточка» 207: 6079 ДО 4029 ──
	Add(TEXT("3 Заточка 207: 6079 до 4029"), 4.f, [S]()
	{
		S->Mark();
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(S->Mob(kRidSharp)); }
		S->Feed(6079, Live6079(kSharp6079, S->Self, kRidSharp));
		S->Feed(4029, Body4029(S->Self, kRidSharp, kMobHp - 300, { 300 }, { -1, 207, -1, -1 }));
		S->Feed(4017, Body4017(kRidSharp, { 68 }, kMobHp - 300));
	}, [S]() { S->NoteFloats(); return S->El() > 1.3; }, [S]()
	{
		const FAsdaDotSlot* Sl = S->Fx()->FindDotSlot(true, kRidSharp, 68);
		S->Expect(S->MobHas(kRidSharp, 68) && S->MobSource(kRidSharp, 68) == 207, TEXT("состояние 68 от навыка 207 на мобе"));
		S->Expect(Sl && Sl->TickDamage == 322 && Sl->Remaining < 12880 && (12880 - Sl->Remaining) % 322 == 0,
			FString::Printf(TEXT("слот тиков: 20000/500 × 322 = 12880, остаток %d"), Sl ? Sl->Remaining : -1));
		S->Expect(S->FloatMax >= 1, FString::Printf(TEXT("цифры тиков над мобом: живых надписей до %d"), S->FloatMax));
	});

	// ── 4. 4017 без 68 снимает; поздний 4029 с кодом 169 не возвращает ──
	Add(TEXT("4 снятие 4017 и поздний код 169"), 2.f, [S]()
	{
		S->Mark();
		const AAsdaMobActor* M = S->Mob(kRidSaw);
		S->Feed(4017, Body4017(kRidSaw, {}, M ? M->CurHp : kMobHp));
		S->Expect(!S->MobHas(kRidSaw, 68), TEXT("4017 без 68: кровотечение снято"));
		S->Feed(4029, Body4029(S->Self, kRidSaw, M ? M->CurHp - 100 : kMobHp, { 100 }, { 169, -1, -1, -1 }));
	}, [S]() { return S->El() > 0.6; }, [S]()
	{
		S->Expect(!S->MobHas(kRidSaw, 68), TEXT("поздний 4029 с кодом 169 состояние не вернул"));
	});

	// ── итог ──
	Add(TEXT("итог"), 3.f, [S]()
	{
		for (const int32 Rid : S->Mobs) { TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], Rid); S->Feed(4011, B); }
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(nullptr); }
	}, [S]() { return S->El() > 0.5; }, [S, Net]()
	{
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), kCmd, false, LogAsdaProcDotTest, TEXT("САМОТЕСТ-PROCDOT"), S->Ok, S->Fail);
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
			UE_LOG(LogAsdaProcDotTest, Log, TEXT("САМОТЕСТ-PROCDOT шаг %s"), *Stp.Name);
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

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestProcDot(
	TEXT("asda.Skill.SelfTestProcDot"),
	TEXT("Самотест кровотечения от обычной атаки («Зазубренная стрела» 787 → 169, «Заточка» 207): живые тела 6079, 4029 со вторым "
	     "каналом, 4017 со списком; состояние 68, значок цели, слот тиков, снятие 4017. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Skill.SelfTestProcDot"));
		FAsdaProcDotSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
