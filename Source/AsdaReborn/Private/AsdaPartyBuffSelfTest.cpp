// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ГРУППОВОЙ БАФФ ДОХОДИТ ДО СОГРУППНИКОВ» (04.11.2026, жалоба игрока «Party Howl of Stamina 932: кастер получил бафф,
//   согруппники — нет»). [ПОРТ] Только офлайн. Команда `asda.Party.SelfTestBuff [delay=N] [quit]`.
//   Путь игрока: навык — `DispatchByKind` (вход быстрой панели), исходящий 5256 — перехват `AsdaSelfTestKit::ArmCapture`, ростер отряда —
//   штатный приём 5107 (полный состав: ТОЛЬКО charId и имена, userNum участников неизвестен — ровно живой случай), соседи —
//   `DebugSpawnFakePeer` (charId = 900000 + userNum). Разбор — `UAsdaWorldSubsystem::PickPartyUserIdxsInGameBox` (ключ
//   `asda.Party.MatchByCharId`), эталон `charCollectPartyMembersInBox@0x9DB8E0` + `Party_FindMemberByUserIdx@0x9CD090` (ключ — charId).
//   Шаги:
//     1) 932 в отряде (я + согруппник П рядом, Ч — не в отряде рядом, Д — согруппник вне бокса 10 тайлов): записи 5256 = я, П;
//        Ч и Д не берутся;
//     2) контроль `asda.Party.MatchByCharId 0`: только я (дефект воспроизведён);
//     3) другие групповые баффы (585, 763) — тот же сбор;
//     4) сторона согруппника: 5272 состояния 61 (флаг 932) от кастера-соседа на меня + 4060 — мой максимум HP вырос.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"   // ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaSkillUseSubsystem.h"
#include "UI/AsdaPartySubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPartyBuffTest, Log, All);

namespace AsdaPartyBuffTestLocal
{
	constexpr int32 kSelfFallback = 4322;    // свой userNum, если не известен
	constexpr int32 kSelfCharId = 77001;     // свой charId на время прогона (глава отряда в 5107)
	constexpr int32 kP = 7851;               // согруппник рядом
	constexpr int32 kC = 7852;               // посторонний рядом (не в отряде)
	constexpr int32 kD = 7853;               // согруппник вне бокса (12 тайлов)
	static int32 CharIdOf(int32 UserNum) { return 900000 + UserNum; }   // как `DebugSpawnFakePeer`

	struct FOut { uint16 Op = 0; TArray<uint8> Payload; };   // перехваченный исходящий

	struct FStep
	{
		FString Name; float TimeoutSec = 10.f;
		TFunction<void()> Start; TFunction<bool()> Poll; TFunction<void()> Check;
	};

	struct FState
	{
		const TCHAR* Cmd = nullptr; const TCHAR* Tag = nullptr;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UAsdaNetSubsystem> Net;
		TArray<FStep> Steps;
		int32 StepIdx = 0; bool bStepStarted = false; double StepStartS = 0.0;
		FTimerHandle Tick; bool bQuit = false;
		FAsdaSelfTestRunGate Gate{ TEXT(""), false };
		int32 Ok = 0, Fail = 0;
		uint32 Gen = 0; TArray<FOut> Outs; int32 OutFrom = 0;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		bool bWasInGame = false; bool bWasStand = false; int32 WasCharId = 0;
		int32 Self = -1; FIntPoint HomeTile = FIntPoint::ZeroValue;
		int32 WasWeaponSub = 99;

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaSkillUseSubsystem* SU() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr; }
		UAsdaPartySubsystem* PS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaPartySubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AActor* Peer(int32 Idx) const { const UAsdaWorldSubsystem* W = WS(); return W ? W->ActorByTarget(EAsdaTargetKind::Character, Idx) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaPartyBuffTest, Log, TEXT("%s %s %s: %s"), Tag, bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }
		void SetKey(const TCHAR* Key, int32 V) { AsdaCVarSave::SetForTest(Key, V); }
		void CalmSkill(int32 Code) { if (UAsdaSkillUseSubsystem* S = SU()) { S->SimulateCastResult5257(0, Code); } }
		// Записи последней заявки 5256 навыка `Code`: (вид, индекс). Шапка 24 б (токен + MD5), код @24, тайл @26/@28, записи с @31 по 29 б.
		bool LastRecs(int32 Code, TArray<FIntPoint>& OutRecs) const
		{
			OutRecs.Reset();
			for (int32 i = Outs.Num() - 1; i >= OutFrom; --i)
			{
				const FOut& O = Outs[i];
				if (O.Op != 5256 || O.Payload.Num() < 26 || AsdaBytes::RdI16(&O.Payload[24]) != Code) { continue; }
				for (int32 Off = 31; Off + 29 <= O.Payload.Num(); Off += 29)
				{
					const int32 Idx = AsdaBytes::RdI16(&O.Payload[Off + 1]);
					if (Idx >= 0) { OutRecs.Emplace(O.Payload[Off], Idx); }
				}
				return true;
			}
			return false;
		}
	};

	// 5107 (144 б): шесть i32 charId (−1 = пусто, слот 0 — глава), затем шесть char[20] имён.
	static TArray<uint8> Body5107(const TArray<int32>& CharIds)
	{
		TArray<uint8> B; B.SetNumZeroed(144);
		for (int32 i = 0; i < 6; ++i) { AsdaBytes::WrI32(&B[4 * i], CharIds.IsValidIndex(i) ? CharIds[i] : -1); }
		for (int32 i = 0; i < CharIds.Num() && i < 6; ++i)
		{
			const FString Nm = FString::Printf(TEXT("Party%d"), i);
			for (int32 c = 0; c < Nm.Len() && c < 19; ++c) { B[24 + 20 * i + c] = (uint8)Nm[c]; }
		}
		return B;
	}
	// 5251 — изученные навыки (34 б на запись): код@0, уровень@2, 1@3.
	static TArray<uint8> Body5251(const TArray<int32>& Codes)
	{
		TArray<uint8> B; B.SetNumZeroed(34 * Codes.Num());
		for (int32 i = 0; i < Codes.Num(); ++i) { uint8* P = B.GetData() + 34 * i; AsdaBytes::WrI16(P, Codes[i]); P[2] = 1; P[3] = 1; }
		return B;
	}
	// 5272 (живое тело 01.11 11:47:55, сторона согруппника): @0 userIdx, @2/@4 состояние 61, @6 флаг 932, @8 1, @10 2, @11 1800 с, @13 1, @14 20 %.
	static TArray<uint8> Body5272(int32 Uidx)
	{
		TArray<uint8> B; B.SetNumZeroed(28);
		AsdaBytes::WrI16(&B[0], Uidx); AsdaBytes::WrI16(&B[2], 61); AsdaBytes::WrI16(&B[4], 61); AsdaBytes::WrI16(&B[6], 932);
		B[8] = 1; B[10] = 2; AsdaBytes::WrI16(&B[11], 1800); B[13] = 1; AsdaBytes::WrI16(&B[14], 20);
		return B;
	}
	// 4060 (68 б): +0 maxHp i32, +4 maxMp, +6 curHp i32, +10 curMp — живые числа согруппника (9946 → 11835 после 932).
	static TArray<uint8> Body4060(int32 MaxHp)
	{
		TArray<uint8> B; B.SetNumZeroed(68);
		AsdaBytes::WrI32(&B[0], MaxHp); AsdaBytes::WrI16(&B[4], 217); AsdaBytes::WrI32(&B[6], 9556); AsdaBytes::WrI16(&B[10], 146);
		AsdaBytes::WrI16(&B[12], 299); AsdaBytes::WrI16(&B[14], 483); AsdaBytes::WrI16(&B[22], 807); AsdaBytes::WrI16(&B[24], 870);
		return B;
	}

	static void RunSteps(TSharedRef<FState> S, UWorld* W, const FAsdaSelfTestArgs& KitArgs, UAsdaNetSubsystem* Net)
	{
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
				UE_LOG(LogAsdaPartyBuffTest, Log, TEXT("%s шаг %s"), St->Tag, *Stp.Name);
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
}

struct FAsdaPartyBuffSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaPartyBuffSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaPartyBuffTestLocal;
	const TCHAR* const kCmd = TEXT("asda.Party.SelfTestBuff");
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 5256 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Cmd = kCmd; S->Tag = TEXT("САМОТЕСТ-ГРУППОВОЙ-БАФФ");
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate = FAsdaSelfTestRunGate{ kCmd, KitArgs.bQuit };
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout; St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	auto Idle = [S]() { const UAsdaSkillUseSubsystem* U = S->SU(); return U && U->GetActiveSkill() < 0 && U->GetSelfCharState() == 0; };

	Add(TEXT("подготовка"), 5.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		if (!C || !S->PC() || !S->WS()) { S->Expect(false, TEXT("нет своего персонажа/мира после FakeSelfSpawn")); return; }
		S->HomeTile = AsdaCoord::GameToTile(C->GetGamePosXZ());
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = kSelfFallback; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		S->Self = (int32)Net->SelfUserNum;
		S->WasCharId = Net->SelectedCharId; Net->SelectedCharId = kSelfCharId;
		TWeakPtr<FState> WSp = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [WSp](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			TSharedPtr<FState> St = WSp.Pin(); if (!St) { return; }
			FOut& O = St->Outs.AddDefaulted_GetRef(); O.Op = Op; if (P && Len > 0) { O.Payload.Append(P, Len); }
		}, (uint16)0, S->Cmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected; Net->bInGameConnected = true;
		S->WasWeaponSub = C->EquippedWeaponSubType; C->EquippedWeaponSubType = 3;   // двуручный меч — как у живого кастера 932
		S->bWasStand = Net->bStandCaptureSkillUse; Net->bStandCaptureSkillUse = true;
		S->Feed(5251, Body5251({ 932, 585, 763 }));
		Net->SetVitalsMp(S->Self, 99999, 99999, TEXT("самотест группового баффа"), true);
		S->SetKey(TEXT("asda.Party.MatchByCharId"), 1);
		if (UAsdaWorldSubsystem* Ws = S->WS())
		{
			Ws->DebugSpawnFakePeer(kP, TEXT("ОтрядП"), AsdaCoord::TileCenterGame(S->HomeTile + FIntPoint(3, 0)));
			Ws->DebugSpawnFakePeer(kC, TEXT("ЧужойЧ"), AsdaCoord::TileCenterGame(S->HomeTile + FIntPoint(0, 3)));
			Ws->DebugSpawnFakePeer(kD, TEXT("ОтрядД"), AsdaCoord::TileCenterGame(S->HomeTile + FIntPoint(14, 0)));
		}
		// Полный состав отряда — как на живом сервере: только charId (userNum участников из 5107 неизвестен).
		S->Feed(5107, Body5107({ kSelfCharId, CharIdOf(kP), CharIdOf(kD) }));
	}, [S]() { return S->El() > 1.0 && S->Peer(kP) && S->Peer(kC) && S->Peer(kD); }, [S]()
	{
		const UAsdaPartySubsystem* P = S->PS();
		S->Expect(S->Peer(kP) && S->Peer(kC) && S->Peer(kD), TEXT("соседи на местах: П рядом, Ч рядом, Д в 14 тайлах"));
		S->Expect(P && P->IsInParty() && P->IsMemberCharId(CharIdOf(kP)) && !P->IsMemberUserNum(kP),
			TEXT("отряд из 5107: П в составе по charId, userNum П неизвестен (живой случай)"));
	});

	struct FCase { const TCHAR* Name; int32 Code; int32 Weapon; bool bKey; bool bWantP; };   // Weapon — подтип оружия живого кастера
	const FCase kCases[] = {
		{ TEXT("1 «Party Howl of Stamina» 932 — записи я + П"),            932, 3, true,  true },
		{ TEXT("2 контроль asda.Party.MatchByCharId 0 — только я"),          932, 3, false, false },
		{ TEXT("3а «Enhanced Party Strength» 585 — записи я + П"),          585, 3, true,  true },
		{ TEXT("3б «Party Lucky Spell» 763 — записи я + П"),                763, 9, true,  true },
	};
	for (const FCase& K : kCases)
	{
		Add(K.Name, 4.f, [S, K]()
		{
			S->OutFrom = S->Outs.Num();
			S->SetKey(TEXT("asda.Party.MatchByCharId"), K.bKey ? 1 : 0);
			if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = K.Weapon; }   // журналы 01.11: 932/585 — 3, 763 — 9
			if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DispatchByKind(K.Code, -1); }
		}, [S, K]() { TArray<FIntPoint> R; return S->LastRecs(K.Code, R) && S->El() > 0.3; }, [S, K]()
		{
			TArray<FIntPoint> R;
			const bool bSent = S->LastRecs(K.Code, R);
			const bool bMe = R.Contains(FIntPoint(1, S->Self)), bP = R.Contains(FIntPoint(1, kP));
			const bool bC = R.Contains(FIntPoint(1, kC)), bD = R.Contains(FIntPoint(1, kD));
			S->Expect(bSent, FString::Printf(TEXT("заявка 5256 навыка %d ушла путём игрока"), K.Code));
			S->Expect(bMe && R.IsValidIndex(0) && R[0] == FIntPoint(1, S->Self), TEXT("кастер — первой записью"));
			S->Expect(bP == K.bWantP && !bC && !bD, FString::Printf(TEXT("записей %d: П %d (ждём %d), посторонний Ч %d, Д вне бокса %d"),
				R.Num(), bP ? 1 : 0, K.bWantP ? 1 : 0, bC ? 1 : 0, bD ? 1 : 0));
			S->CalmSkill(K.Code);
			S->SetKey(TEXT("asda.Party.MatchByCharId"), 1);
		});
		Add(TEXT("пауза (навык свободен)"), 3.f, nullptr, [Idle, S]() { return Idle() && S->El() > 0.3; }, nullptr);
	}

	// ── 4. сторона согруппника: бафф соседа на меня ──
	Add(TEXT("4 согруппник: 5272 состояния 61 (932) на меня + 4060 — максимум HP вырос"), 3.f, [S, Net]()
	{
		S->Feed(4060, Body4060(9946));
		S->Feed(5272, Body5272(S->Self));
		S->Feed(4060, Body4060(11835));
	}, [S]() { return S->El() > 0.5; }, [S, Net]()
	{
		const UAsdaNetSubsystem::FAsdaVitals* V = Net->GetVitals(S->Self);
		S->Expect(V && V->HpMax == 11835, FString::Printf(TEXT("мой максимум HP %d (ждём 11835 = 9946 + 20 %%)"), V ? V->HpMax : -1));
	});

	Add(TEXT("итог"), 2.f, [S, Net]()
	{
		S->Feed(5107, Body5107({}));
		for (const int32 Idx : { kP, kC, kD }) { TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], Idx); S->Feed(4011, B); }
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(nullptr); }
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = S->WasWeaponSub; }
		Net->bStandCaptureSkillUse = S->bWasStand;
		Net->SelectedCharId = S->WasCharId;
		Net->ClearMoveRequestInFlight(TEXT("самотест группового баффа: итог"));
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		S->SelfScope.Reset();
	}, [S]() { return S->El() > 0.3; }, [S]()
	{
		AsdaSelfTestKit::Finish(S->World.Get(), S->Cmd, false, LogAsdaPartyBuffTest, S->Tag, S->Ok, S->Fail);
	});
	RunSteps(S, W, KitArgs, Net);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestPartyBuff(
	TEXT("asda.Party.SelfTestBuff"),
	TEXT("Самотест группового баффа (04.11): отряд из 5107 (только charId), навык 932/585/763 путём игрока — записи 5256 = я + согруппник "
	     "в боксе, без постороннего и дальнего; контроль asda.Party.MatchByCharId 0; сторона согруппника 5272 + 4060. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Party.SelfTestBuff"));
		FAsdaPartyBuffSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
