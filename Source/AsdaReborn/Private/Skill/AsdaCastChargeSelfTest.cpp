// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «НАВЫК С КАСТОМ НЕ СРАБАТЫВАЕТ РАНЬШЕ КОНЦА КАСТА» `asda.Skill.SelfTestCastCharge` (06.11.2026). [ПОРТ] Только офлайн
//   (`-AsdaNoNet`): ответы сервера 5270/5257 — штатный приём `DebugFeedPacket`, момент удара — появление цифры (кадр удара показа).
//   Разбор — у ключа `asda.Skill.CastCharge` (AsdaSkillFxSubsystem.cpp). Сценарии: 11 навыков с зарядом — удар не раньше конца заряда;
//   контроль ключа 0 (702 — удар сразу, прежний дефект); контроль 5257 (702 — заряда нет, как эталон); контроль 501 (заряда нет: COMBO0).
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"   // ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"                   // запись 4013 (спаун моба)
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaSkillUseSubsystem.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCastChargeTest, Log, All);

void AsdaSkillFxDebugChargeTimes(double& OutPreS, double& OutLaunchS);   // моменты «снаряд у кости» и «вылет» (AsdaSkillFxSubsystem.cpp)

namespace AsdaCastChargeTestLocal
{
	const TCHAR* const kCmd = TEXT("asda.Skill.SelfTestCastCharge");   // имя команды
	const TCHAR* const kTag = TEXT("САМОТЕСТ-ЗАРЯД-КАСТА");            // префикс строк журнала
	constexpr int32 kSelfFallback = 4326;    // свой userNum, если не известен
	constexpr int32 kRid = 31501;            // моб-цель
	constexpr int32 kMobHp = 500000;         // HP моба (сценарии его не добивают)

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
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		bool bWasInGame = false; int32 WasWeaponSub = 99;
		int32 Self = -1; FIntPoint HomeTile = FIntPoint::ZeroValue;
		int32 HpWant = kMobHp; double FedS = 0.0; double HitAfterS = -1.0;   // замер шага: HP моба после удара, момент ответа, момент удара

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaSkillUseSubsystem* SU() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AAsdaMobActor* Mob() const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(kRid) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		bool HitLanded() const { const AAsdaMobActor* M = Mob(); return M && M->CurHp == HpWant; }   // запись урона выпущена кадром удара
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaCastChargeTest, Log, TEXT("%s %s %s: %s"), kTag, bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }
	};

	// Запись по мобу (29 б): вид 0, цель@1, урон@3, флаги@7 (бит 0 — попадание), HP после@11.
	static void WriteRec(uint8* R, int32 Rid, int32 HpAfter)
	{
		R[0] = 0; AsdaBytes::WrI16(R + 1, Rid); AsdaBytes::WrI32(R + 3, 5); R[7] = 0x03; AsdaBytes::WrI32(R + 11, HpAfter);
		AsdaBytes::WrI16(R + 19, -1); AsdaBytes::WrI16(R + 27, -1);
	}
	// 5270 (7 + 29·N): кастер@0, навык@2, уровень@4, записей@6.
	static TArray<uint8> Body5270(int32 Caster, int32 Skill, int32 HpAfter)
	{
		TArray<uint8> B; B.SetNumZeroed(7 + 29);
		AsdaBytes::WrI16(&B[0], Caster); AsdaBytes::WrI16(&B[2], Skill); AsdaBytes::WrI16(&B[4], 1); B[6] = 1;
		WriteRec(B.GetData() + 7, kRid, HpAfter);
		return B;
	}
	// 5257 (8 + 29·N): код@0, кастер@1, навык@3, 1@5, уровень@6.
	static TArray<uint8> Body5257(int32 Caster, int32 Skill, int32 HpAfter)
	{
		TArray<uint8> B; B.SetNumZeroed(8 + 29);
		B[0] = 1; AsdaBytes::WrI16(&B[1], Caster); AsdaBytes::WrI16(&B[3], Skill); B[5] = 1; AsdaBytes::WrI16(&B[6], 1);
		WriteRec(B.GetData() + 8, kRid, HpAfter);
		return B;
	}
}

struct FAsdaCastChargeSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaCastChargeSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaCastChargeTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 5256 };
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

	Add(TEXT("подготовка"), 5.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		if (!C || !S->PC() || !S->WS()) { S->Expect(false, TEXT("нет своего персонажа/мира после FakeSelfSpawn")); return; }
		S->HomeTile = AsdaCoord::GameToTile(C->GetGamePosXZ());
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = kSelfFallback; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		S->Self = (int32)Net->SelfUserNum;
		S->bWasInGame = Net->bInGameConnected; Net->bInGameConnected = true;
		S->WasWeaponSub = C->EquippedWeaponSubType;
		AsdaCVarSave::SetForTest(TEXT("asda.Skill.CastCharge"), 1);
		TArray<uint8> B = AsdaTestPackets::MobSpawn4013(kRid, 1, S->HomeTile + FIntPoint(3, 0), 1, 250);
		AsdaBytes::WrI32(&B[9], kMobHp);
		S->Feed(4013, B);
	}, [S]() { return S->El() > 1.0 && S->Mob(); }, [S]() { S->Expect(S->Mob() != nullptr, TEXT("моб на месте")); });

	// Один замер: ответ сервера Op по навыку Skill (оружие Wsub); MinS/MaxS — окно момента удара от ответа, с.
	auto AddShot = [S, Add](const FString& Name, int32 Op, int32 Skill, int32 Wsub, int32 Key, double MinS, double MaxS)
	{
		Add(Name, 7.f, [S, Op, Skill, Wsub, Key]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Skill.CastCharge"), Key);
			if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = Wsub; C->SkillAnimUntilS = 0.0; }
			if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(S->Mob()); }
			if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DebugSetCastState(0, -1); }
			S->HpWant -= 5; S->HitAfterS = -1.0; S->FedS = FPlatformTime::Seconds();
			S->Feed(Op, Op == 5270 ? Body5270(S->Self, Skill, S->HpWant) : Body5257(S->Self, Skill, S->HpWant));
		}, [S]()
		{
			if (S->HitAfterS < 0.0 && S->HitLanded()) { S->HitAfterS = FPlatformTime::Seconds() - S->FedS; }
			return S->HitAfterS >= 0.0 && S->El() > S->HitAfterS + 0.1;
		}, [S, Op, Skill, Key, MinS, MaxS]()
		{
			S->Expect(S->HitAfterS >= MinS && S->HitAfterS <= MaxS, FString::Printf(TEXT("%d по навыку %d, ключ %d: удар через %.3f с (ждём %.2f…%.2f)"),
				Op, Skill, Key, S->HitAfterS, MinS, MaxS));
			AsdaCVarSave::SetForTest(TEXT("asda.Skill.CastCharge"), 1);
			if (UAsdaSkillUseSubsystem* U = S->SU()) { U->SimulateCastEnd5271(1, Skill); }
		});
		Add(TEXT("пауза"), 6.f, nullptr, [S]() { const AAsdaPlayerCharacter* C = S->Ch(); return S->El() > 0.3 && C && !C->IsSkillAnimActive(); }, nullptr);
	};
	struct FCase { int32 Skill, Wsub; double ChargeS, PreS; };
	static const FCase kCases[] = { {702, 9, 2.0, 0.1}, {703, 9, 2.0, 0.1}, {704, 9, 2.0, 0.1}, {708, 9, 2.0, 0.1}, {709, 9, 2.0, 0.1}, {711, 9, 2.0, 0.1},
		{712, 9, 2.0, 0.1}, {714, 9, 2.0, 0.1}, {905, 5, 1.2, 0.3}, {910, 5, 2.0, 0.1}, {915, 5, 3.36, 0.55} };
	for (const FCase& C : kCases)
	{
		AddShot(FString::Printf(TEXT("заряд %d"), C.Skill), 5270, C.Skill, C.Wsub, 1, C.ChargeS * 0.97, C.ChargeS + 0.9);
		// Порог cb4: снаряд появился у кости и снят вылетом с разницей ≈ порогу (допуск — кадр прогона).
		const int32 Skill = C.Skill; const double PreS = C.PreS;
		Add(FString::Printf(TEXT("снаряд у кости до вылета %d"), C.Skill), 1.f, nullptr, nullptr, [S, Skill, PreS]()
		{
			double P = -1.0, L = -1.0; AsdaSkillFxDebugChargeTimes(P, L);
			const double Lead = (P > 0.0 && L > 0.0) ? L - P : -1.0;
			// Нижней границы по порогу нет: момент снимается после рождения модели, а кадр прогона -RenderOffscreen длиннее порога 100 мс.
			S->Expect(P >= S->FedS && Lead > 0.0 && Lead <= PreS + 0.2, FString::Printf(TEXT("навык %d: снаряд у кости за %.3f с до вылета (порог %.2f)"), Skill, Lead, PreS));
		});
	}
	AddShot(TEXT("контроль ключ 0: 702 — удар сразу"), 5270, 702, 9, 0, 0.0, 0.7);
	AddShot(TEXT("контроль 5257: 702 — заряда нет"), 5257, 702, 9, 1, 0.0, 0.7);
	AddShot(TEXT("контроль 501 — заряда нет, кадр COMBO0"), 5270, 501, 1, 1, 0.6, 1.7);

	Add(TEXT("итог"), 3.f, [S]()
	{
		TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kRid); S->Feed(4011, B);
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(nullptr); }
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = S->WasWeaponSub; }
		if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DebugSetCastState(0, -1); }
	}, [S]() { return S->El() > 0.3; }, [S, Net]()
	{
		Net->bInGameConnected = S->bWasInGame;
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), kCmd, false, LogAsdaCastChargeTest, kTag, S->Ok, S->Fail);
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
			UE_LOG(LogAsdaCastChargeTest, Log, TEXT("%s шаг %s"), kTag, *Stp.Name);
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

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCastCharge(
	TEXT("asda.Skill.SelfTestCastCharge"),
	TEXT("Самотест «навык с кастом не срабатывает раньше конца каста» (06.11): 11 навыков с зарядом 13/27 — удар не раньше конца заряда; контроль "
	     "asda.Skill.CastCharge 0, контроль 5257, контроль 501. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Skill.SelfTestCastCharge"));
		FAsdaCastChargeSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
