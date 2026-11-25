// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ SELFTEST «HIDE» `asda.Skill.SelfTestHide` (04.11.2026, owner/player bug «Hide does nothing»). [ПОРТ] Offline only (`-AsdaNoNet`).
//   Unlike `asda.SelfTest.Crouch` (section G forces the state through `UAsdaStealthSubsystem::DebugForceInvisible`), this test drives the
//   REAL chain end to end: cast through the player entry `UAsdaSkillUseSubsystem::DispatchByKind` (outgoing 5256 captured), server replies
//   fed through the real receiver `UAsdaNetSubsystem::DebugFeedPacket` with the byte layout of the live journal (Player5 02.11.2026
//   23:50:26: 5272 `00 00 70 00 70 00 F7 02 01 00 02 1E 00 01 …`, 5271, 5257 result 1; refusal 03.11 00:03:33: 5257 `1C 01 00 F7 02 00 FF FF`).
//   Server facts (GameServer `RecvChar_SkillUse@0x46F900`, GameServer.c:46164): skill 759 is refused with code 28 while the 10 s battle
//   flag (`sub_556D10`, set by `sub_556C60` on every mob/PvP hit, 4205) is up — notice 1157 «Cannot use during battle.»
//   Steps:
//     H0  first real cast refused with 5257 code 28 → system line = notice 1157 in the session language, no state;
//     H1  cast 759 → 5256 leaves with code 759 (self cast, 1 record);
//     H2  live reply 5272{112}+5271+5257 → own state 112, buff-bar icon (MINISTATE slot id 112);
//     H3  own fade: after ~1.8 s α 0.3, translucent, still drawn (etalon `charEntityUpdateTick`);
//     H4  own walk under stealth = donor crouch walk anim_239; standing — not anim_239 (donor has no crouch stance);
//     H5  peer 5272{112} → peer fades out and is not drawn, target on it is dropped (game.c:10153);
//     H6  hidden peer walking — crouch clip anim_239;
//     H7  break (server removal 5273 — attack/damage/skill/mount/premium item all end in it) — peer visible again, α 1;
//     H8  own 5273 → reappear within 1 s, icon gone, crouch off;
//     H10 own 5272 with 2 s left → the client timer ends it (`charBuffUpdateExpired`), α back to 1;
//     H11 map change (session reset MapChanged) while hidden → stealth and its view cleared.
//   Console: `asda.Skill.SelfTestHide [delay=N] [quit]`. Lines — «САМОТЕСТ-HIDE», result — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"   // ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaSessionReset.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaMiniStateSubsystem.h"
#include "UI/AsdaSkillUseSubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaNotice.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaStealthSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"
#include "World/AsdaCrouchStand.h"           // ★ 05.11.2026: H4б — поза приседа стоя под Hide (ключ asda.Crouch.StandPose)
#include "Components/SkeletalMeshComponent.h"   // H4б: темп клипа тела
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaHideTest, Log, All);

namespace AsdaHideTestLocal
{
	const TCHAR* const kCmd = TEXT("asda.Skill.SelfTestHide");   // console command
	constexpr int32 kSelfFallback = 4322;   // own userNum when not known offline
	constexpr int32 kSkillHide = 759;       // «Hide» / «Убежище» (.sif buff0 = state 112)
	constexpr int32 kStateHide = 112;       // state «Hide» (stateinfotip 112, state_Icon_2 cell 12)
	constexpr int32 kLocBattle = 1157;      // «Cannot use during battle.» (fail table code 28)
	constexpr int32 kPeer = 7841;           // fake peer userNum

	struct FOut { uint16 Op = 0; TArray<uint8> Payload; };   // captured outgoing packet

	// One timed step: Start once, Poll until true or timeout, then Check.
	struct FStep
	{
		FString Name; float TimeoutSec = 10.f;
		TFunction<void()> Start; TFunction<bool()> Poll; TFunction<void()> Check;
	};

	// Run state shared by the timers.
	struct FState
	{
		const TCHAR* Tag = TEXT("САМОТЕСТ-HIDE");   // log prefix
		TWeakObjectPtr<UWorld> World;                 // game world
		TWeakObjectPtr<UAsdaNetSubsystem> Net;        // network subsystem
		TArray<FStep> Steps;                          // step list
		int32 StepIdx = 0; bool bStepStarted = false; double StepStartS = 0.0;   // step cursor and its start time
		FTimerHandle Tick; bool bQuit = false;        // step timer, quit at the end
		FAsdaSelfTestRunGate Gate{ TEXT(""), false }; // stops the run when the world goes away
		int32 Ok = 0, Fail = 0;                       // counters
		uint32 Gen = 0; TArray<FOut> Outs; int32 OutFrom = 0;   // outgoing capture
		TUniquePtr<FAsdaNetSelfScope> SelfScope;      // own-number snapshot (restored at the end)
		bool bWasInGame = false;                      // previous bInGameConnected
		int32 Self = -1;                              // own userNum
		int32 WasWeaponSub = 99;                      // previous weapon subtype
		bool bFlag = false;                           // per-step latch
		bool bWasStand = false;                       // previous bStandCaptureSkillUse
		int32 WasMp = -1;                             // own MP before the run (759 costs 10 MP)
		int32 ChatMark = -1;                          // count of notice-1157 lines in the system journal before the refusal

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaEffectsSubsystem* Fx() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr; }
		UAsdaSkillUseSubsystem* SU() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr; }
		UAsdaStealthSubsystem* St() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaStealthSubsystem>() : nullptr; }
		UAsdaMiniStateSubsystem* Mini() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr; }
		UAsdaChatSubsystem* Chat() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaChatSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Me() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AAsdaPlayerCharacter* Peer() const { const UAsdaWorldSubsystem* W = WS(); return W ? Cast<AAsdaPlayerCharacter>(W->ActorByTarget(EAsdaTargetKind::Character, kPeer)) : nullptr; }
		// Step clock in GAME seconds: the stealth fade runs on game DeltaTime, and the offscreen game lags wall time.
		double GameNow() const { const UWorld* W = World.Get(); return W ? (double)W->GetTimeSeconds() : FPlatformTime::Seconds(); }
		double El() const { return GameNow() - StepStartS; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaHideTest, Log, TEXT("%s %s %s: %s"), Tag, bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }
		bool Has112(int32 Uidx) const { const UAsdaEffectsSubsystem* F = Fx(); return F && F->FindByTypeSub(Uidx, kStateHide, 0) != nullptr; }
		bool IconShown() const
		{
			const UAsdaMiniStateSubsystem* M = Mini(); if (!M) { return false; }
			for (int32 i = 0; i < 28; ++i) { if (M->DebugBuffSlotId(i) == kStateHide) { return true; } }
			return false;
		}
		float Alpha(const AAsdaPlayerCharacter* C) const
		{
			float A = 1.f; int32 Ph = 0; bool bR = true;
			const UAsdaStealthSubsystem* S = St();
			return (S && C && S->GetStateFor(C, A, Ph, bR)) ? A : 1.f;
		}
		static bool ClipIs239(const AAsdaPlayerCharacter* C)
		{
			return C && C->GetPlayingClipName().EndsWith(FString::Printf(TEXT("anim_%d"), AAsdaPlayerCharacter::kClipCrouchWalk));
		}
		FString View(const AAsdaPlayerCharacter* C) const
		{
			if (!C) { return TEXT("нет актора"); }
			return FString::Printf(TEXT("α=%.2f translucent=%d drawn=%d hidden=%d stealth=%d clip=%s"), Alpha(C), C->IsStealthTranslucencyActive() ? 1 : 0,
				C->IsStealthRendered() ? 1 : 0, C->IsHidden() ? 1 : 0, C->IsSkillStealth() ? 1 : 0, *C->GetPlayingClipName());
		}
	};

	// 5272 exactly as the live journal (Player5 02.11 23:50:26): userIdx@0, state@2, raw: state@4, flag(skill)@6, @8=1, stack@9=0,
	//   kind@10=2, remain@11 (s), byte9@13=1.
	static TArray<uint8> Body5272(int32 Uidx, int32 RemainSec)
	{
		TArray<uint8> B; B.SetNumZeroed(16);
		AsdaBytes::WrI16(&B[0], Uidx); AsdaBytes::WrI16(&B[2], kStateHide); AsdaBytes::WrI16(&B[4], kStateHide);
		AsdaBytes::WrI16(&B[6], kSkillHide); B[8] = 1; B[9] = 0; B[10] = 2; AsdaBytes::WrI16(&B[11], RemainSec); B[13] = 1;
		return B;
	}
	// 5273 (full removal, live 23:50:58 `00 00 70 00 …`): userIdx@0, state@2.
	static TArray<uint8> Body5273(int32 Uidx)
	{
		TArray<uint8> B; B.SetNumZeroed(11); AsdaBytes::WrI16(&B[0], Uidx); AsdaBytes::WrI16(&B[2], kStateHide); return B;
	}
	// 5271 (live `01 00 00 F7 02 01 00`): result@0, userIdx@1, skill@3, flag@5.
	static TArray<uint8> Body5271(int32 Uidx)
	{
		TArray<uint8> B; B.SetNumZeroed(7); B[0] = 1; AsdaBytes::WrI16(&B[1], Uidx); AsdaBytes::WrI16(&B[3], kSkillHide); B[5] = 1; return B;
	}
	// 5257 success, one self record (live 37 b): result 1, caster, skill, 1, level 1; record kind 1 (character) = self, flags 3, −1 @19.
	static TArray<uint8> Body5257Ok(int32 Uidx)
	{
		TArray<uint8> B; B.SetNumZeroed(8 + 29);
		B[0] = 1; AsdaBytes::WrI16(&B[1], Uidx); AsdaBytes::WrI16(&B[3], kSkillHide); B[5] = 1; AsdaBytes::WrI16(&B[6], 1);
		uint8* R = B.GetData() + 8; R[0] = 1; AsdaBytes::WrI16(R + 1, Uidx); R[7] = 3; R[8] = 1; AsdaBytes::WrI16(R + 19, -1);
		return B;
	}
	// 5257 refusal (live 03.11 00:03:33 `1C 01 00 F7 02 00 FF FF`): result 28, caster, skill, 0 records, level −1.
	static TArray<uint8> Body5257Refuse(int32 Uidx, int32 Code)
	{
		TArray<uint8> B; B.SetNumZeroed(8); B[0] = (uint8)Code; AsdaBytes::WrI16(&B[1], Uidx); AsdaBytes::WrI16(&B[3], kSkillHide);
		B[5] = 0; AsdaBytes::WrI16(&B[6], -1); return B;
	}
	// 5251 — learned skills (34 b per record): code, level, «can raise».
	static TArray<uint8> Body5251(int32 Code)
	{
		TArray<uint8> B; B.SetNumZeroed(34); AsdaBytes::WrI16(&B[0], Code); B[2] = 1; B[3] = 1; return B;
	}

	// Step runner (same shape as the other offline selftests): own character via FakeSelfSpawn, steps on a 10 ms timer.
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
			TSharedPtr<FState> Sp = WSt.Pin(); UWorld* Wd = WW.Get();
			if (!Sp || !Wd) { return; }
			if (!Sp->Gate.Pass(Wd)) { Wd->GetTimerManager().ClearTimer(Sp->Tick); return; }
			if (!Sp->Steps.IsValidIndex(Sp->StepIdx)) { Wd->GetTimerManager().ClearTimer(Sp->Tick); AsdaSelfTestKit::Quit(Wd, Sp->bQuit); return; }
			const double Now = Sp->GameNow();
			FStep& Stp = Sp->Steps[Sp->StepIdx];
			if (!Sp->bStepStarted)
			{
				Sp->bStepStarted = true; Sp->StepStartS = Now; Sp->bFlag = false;
				UE_LOG(LogAsdaHideTest, Log, TEXT("%s шаг %s"), Sp->Tag, *Stp.Name);
				if (Stp.Start) { Stp.Start(); }
				return;
			}
			const double El = Now - Sp->StepStartS;
			const bool bDone = !Stp.Poll || Stp.Poll();
			if (!bDone && El < Stp.TimeoutSec) { return; }
			if (!bDone) { Sp->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с"), Stp.TimeoutSec)); }
			if (Stp.Check) { Stp.Check(); }
			++Sp->StepIdx; Sp->bStepStarted = false;
		}), 0.01f, true, KitArgs.Delay + 2.5f);
	}
}

// Console entry (struct keeps the body out of the lambda).
struct FAsdaHideSkillSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaHideSkillSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaHideTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 5256 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate = FAsdaSelfTestRunGate{ kCmd, KitArgs.bQuit };
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout; St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};

	// ── preparation: capture, own number, «in game», one-handed sword (759 mask 0xE = subtypes 1..3), learned 759, peer ──
	Add(TEXT("подготовка"), 5.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Me();
		if (!C || !S->WS() || !S->Fx() || !S->SU() || !S->St()) { S->Expect(false, TEXT("нет своего персонажа/подсистем после FakeSelfSpawn")); return; }
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = kSelfFallback; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		S->Self = (int32)Net->SelfUserNum;
		{ UAsdaNetSubsystem::FAsdaVitals& V = Net->VitalsByUserIdx.FindOrAdd(S->Self); S->WasMp = V.MpCur; V.MpCur = 1000; if (V.MpMax < 1000) { V.MpMax = 1000; } }
		TWeakPtr<FState> WSp = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [WSp](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			TSharedPtr<FState> Sp = WSp.Pin(); if (!Sp) { return; }
			FOut& O = Sp->Outs.AddDefaulted_GetRef(); O.Op = Op; if (P && Len > 0) { O.Payload.Append(P, Len); }
		}, (uint16)0, kCmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected; Net->bInGameConnected = true;
		S->bWasStand = Net->bStandCaptureSkillUse; Net->bStandCaptureSkillUse = true;   // offline 5256 goes to the capture, not the socket
		S->WasWeaponSub = C->EquippedWeaponSubType; C->EquippedWeaponSubType = 1;
		AsdaCVarSave::SetForTest(TEXT("asda.Stealth.Visual"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Crouch.Enable"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Crouch.HideAnim"), 1);
		S->Feed(5251, Body5251(kSkillHide));
		if (UAsdaWorldSubsystem* Ws = S->WS())
		{
			Ws->DebugSpawnFakePeer(kPeer, TEXT("Hider"), C->GetGamePosXZ() + FVector2D(300.f, 0.f));
		}
	}, [S]() { return S->El() > 1.0 && S->Peer() != nullptr; }, [S]()
	{
		S->Expect(S->Peer() != nullptr, TEXT("сосед создан"));
	});

	// ── H0 (first real cast, before the success starts the cooldown): refusal code 28 (battle) ──
	Add(TEXT("H0 отказ 28 «в бою»"), 3.f, [S, Net]()
	{
		const UAsdaChatSubsystem* Ch = S->Chat();
		const FString Txt = AsdaNotice::Loc(S->GI(), kLocBattle);
		// ★ 08.11.2026: отказ навыка — плашкой, как оригинал (`asda.Skill.FailNoticePlank`); считаем строки плашки.
		S->ChatMark = Ch ? Ch->DebugCountWarLines(Txt) : -1;
		Net->ClearMoveRequestInFlight(TEXT("самотест Hide: отказ"));
		S->OutFrom = S->Outs.Num();
		if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DispatchByKind(kSkillHide, -1); }
	}, [S]()
	{
		if (!S->bFlag)
		{
			for (int32 i = S->OutFrom; i < S->Outs.Num(); ++i) { if (S->Outs[i].Op == 5256) { S->bFlag = true; S->Feed(5257, Body5257Refuse(S->Self, 28)); break; } }
			return false;
		}
		return S->El() > 0.6;
	}, [S]()
	{
		const UAsdaChatSubsystem* Ch = S->Chat();
		const FString Txt = AsdaNotice::Loc(S->GI(), kLocBattle);
		const int32 Cnt = Ch ? Ch->DebugCountWarLines(Txt) : -1;
		S->Expect(S->bFlag && !Txt.IsEmpty() && Cnt > S->ChatMark, FString::Printf(TEXT("плашка нотиса 1157 «%s» (строк %d, до — %d)"), *Txt, Cnt, S->ChatMark));
		S->Expect(!S->Has112(S->Self), TEXT("состояние 112 не наложено"));
	});

	// ── H1: cast through the player entry → 5256 with 759 ──
	Add(TEXT("H1 каст 759 → 5256"), 3.f, [S, Net]()
	{
		S->OutFrom = S->Outs.Num();
		Net->ClearMoveRequestInFlight(TEXT("самотест Hide: каст"));
		if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DispatchByKind(kSkillHide, -1); }
	}, [S]()
	{
		for (int32 i = S->OutFrom; i < S->Outs.Num(); ++i) { if (S->Outs[i].Op == 5256) { return true; } }
		return false;
	}, [S]()
	{
		bool bCode = false; int32 Len = 0;
		for (int32 i = S->OutFrom; i < S->Outs.Num(); ++i)
		{
			if (S->Outs[i].Op != 5256) { continue; }
			const TArray<uint8>& P = S->Outs[i].Payload; Len = P.Num();
			for (int32 k = 0; k + 1 < P.Num() && k < 48; ++k) { if (AsdaBytes::RdI16(&P[k]) == kSkillHide) { bCode = true; break; } }
		}
		S->Expect(bCode, FString::Printf(TEXT("5256 ушёл с кодом 759 (тело %d б)"), Len));
	});

	// ── H2: live reply → state 112 + icon ──
	Add(TEXT("H2 ответ 5272/5271/5257 → состояние и значок"), 2.f, [S]()
	{
		S->Feed(5272, Body5272(S->Self, 30));
		S->Feed(5271, Body5271(S->Self));
		S->Feed(5257, Body5257Ok(S->Self));
	}, [S]() { return S->El() > 0.3 && S->Has112(S->Self) && S->IconShown(); }, [S]()
	{
		S->Expect(S->Has112(S->Self), TEXT("своё состояние 112 в списке эффектов"));
		S->Expect(S->IconShown(), TEXT("значок 112 в ряду состояний MINISTATE"));
	});

	// ── H3: own fade to 0.3 ──
	Add(TEXT("H3 свой вид — α 0.3"), 4.f, nullptr, [S]() { return S->El() > 1.8; }, [S]()
	{
		AAsdaPlayerCharacter* C = S->Me();
		S->Expect(C && FMath::IsNearlyEqual(S->Alpha(C), 0.3f, 0.02f) && C->IsStealthTranslucencyActive() && C->IsStealthRendered() && !C->IsHidden(),
			FString::Printf(TEXT("свой полупрозрачен и виден себе: %s"), *S->View(C)));
	});

	// ── H4: own crouch walk / stance ──
	Add(TEXT("H4 свой ход вприсядку"), 2.f, [S]()
	{
		if (AAsdaPlayerCharacter* C = S->Me()) { const FVector2D P = C->GetGamePosXZ(); C->NetMove(P, P + FVector2D(0.f, 600.f), 0.2433f); }
	}, [S]() { return S->El() > 0.25; }, [S]()
	{
		AAsdaPlayerCharacter* C = S->Me();
		S->Expect(C && C->IsSkillStealth() && C->IsMoving() && FState::ClipIs239(C), FString::Printf(TEXT("ход под Hide — anim_239: %s"), *S->View(C)));
		if (C) { C->StopMove(); }
	});
	Add(TEXT("H4б свой покой"), 2.f, nullptr, [S]() { return S->El() > 0.3; }, [S]()
	{
		AAsdaPlayerCharacter* C = S->Me();
		// ★ 05.11.2026 (присед стоя, `World/AsdaCrouchStand.h`): стоя под Hide — поза приседа (кадр anim_239, темп 0); при
		//   `asda.Crouch.StandPose 0` — прежний обычный покой.
		USkeletalMeshComponent* Sk = nullptr;
		if (C) { TArray<USkeletalMeshComponent*> Cs; C->GetComponents<USkeletalMeshComponent>(Cs); Sk = Cs.Num() ? Cs[0] : nullptr; }
		const bool bPose = AsdaCrouchStand::PoseOn();
		const bool bHeld = Sk && FMath::IsNearlyZero(Sk->GetPlayRate());
		S->Expect(C && !C->IsMoving() && (bPose ? (FState::ClipIs239(C) && bHeld) : !FState::ClipIs239(C)),
			FString::Printf(TEXT("в покое под Hide — %s: %s, темп %.2f"), bPose ? TEXT("поза приседа стоя") : TEXT("обычный покой (ключ позы 0)"),
				*S->View(C), Sk ? Sk->GetPlayRate() : -1.f));
	});

	// ── H5/H6: peer goes hidden ──
	Add(TEXT("H5 сосед 5272 → не рисуется, цель снята"), 4.f, [S]()
	{
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(S->Peer()); }
		S->Feed(5272, Body5272(kPeer, 30));
	}, [S]() { return S->El() > 1.8; }, [S]()
	{
		AAsdaPlayerCharacter* P = S->Peer(); AAsdaPlayerController* Pc = S->PC();
		S->Expect(P && P->IsSkillStealth() && !P->IsStealthRendered() && P->IsHidden(), FString::Printf(TEXT("сосед-невидимка не рисуется: %s"), *S->View(P)));
		S->Expect(Pc && Pc->TargetActor.Get() != P, TEXT("цель-невидимка снята"));
	});
	Add(TEXT("H6 ход соседа-невидимки"), 2.f, [S]()
	{
		if (AAsdaPlayerCharacter* P = S->Peer()) { const FVector2D X = P->GetGamePosXZ(); P->NetMove(X, X + FVector2D(0.f, 600.f), 0.2433f); }
	}, [S]() { return S->El() > 0.25; }, [S]()
	{
		AAsdaPlayerCharacter* P = S->Peer();
		S->Expect(P && P->IsMoving() && FState::ClipIs239(P), FString::Printf(TEXT("сосед под Hide идёт вприсядку: %s"), *S->View(P)));
		if (P) { P->StopMove(); }
	});

	// ── H7: break (server removal) — peer visible ──
	Add(TEXT("H7 снятие у соседа (удар/навык → 5273)"), 2.f, [S]() { S->Feed(5273, Body5273(kPeer)); }, [S]() { return S->El() > 0.2; }, [S]()
	{
		AAsdaPlayerCharacter* P = S->Peer();
		S->Expect(P && !P->IsSkillStealth() && P->IsStealthRendered() && !P->IsHidden() && S->Alpha(P) >= 0.999f,
			FString::Printf(TEXT("сосед снова виден, α 1: %s"), *S->View(P)));
	});

	// ── H8: own removal ──
	Add(TEXT("H8 своё снятие 5273"), 3.f, [S]() { S->Feed(5273, Body5273(S->Self)); }, [S]() { return S->El() > 1.3; }, [S]()
	{
		AAsdaPlayerCharacter* C = S->Me();
		S->Expect(!S->Has112(S->Self) && !S->IconShown(), TEXT("состояния и значка 112 больше нет"));
		S->Expect(C && !C->IsSkillStealth() && !C->IsStealthTranslucencyActive() && S->Alpha(C) >= 0.999f,
			FString::Printf(TEXT("свой проявился за 1 с: %s"), *S->View(C)));
	});

	// ── H10: own expiry by the client timer ──
	Add(TEXT("H10 истечение по таймеру"), 7.f, [S]()
	{
		if (UAsdaSkillUseSubsystem* U = S->SU()) { U->SimulateCastResult5257(0, kSkillHide); }
		S->Feed(5272, Body5272(S->Self, 2));
	}, [S]() { return S->El() > 0.5 && !S->Has112(S->Self) && S->Alpha(S->Me()) >= 0.999f; }, [S]()
	{
		AAsdaPlayerCharacter* C = S->Me();
		S->Expect(!S->Has112(S->Self) && S->El() < 6.0, FString::Printf(TEXT("состояние с остатком 2 с снято таймером через %.1f с"), S->El()));
		S->Expect(C && !C->IsSkillStealth() && S->Alpha(C) >= 0.999f, FString::Printf(TEXT("вид вернулся: %s"), *S->View(C)));
	});

	// ── H11: map change while hidden. Own states are kept by design (the server keeps them and does not resend after the portal logon,
	//   AsdaEffectsSubsystem::ResetSessionState); peers of the old map are dropped; no stale view stays on the actors. ──
	Add(TEXT("H11 смена карты под Hide"), 6.f, [S]() { S->Feed(5272, Body5272(S->Self, 30)); S->Feed(5272, Body5272(kPeer, 30)); }, [S]()
	{
		if (!S->bFlag && S->El() > 2.2)
		{
			AAsdaPlayerCharacter* C = S->Me(); AAsdaPlayerCharacter* P = S->Peer();
			S->Expect(C && C->IsSkillStealth() && C->IsStealthTranslucencyActive(), FString::Printf(TEXT("до смены карты свой скрыт: %s"), *S->View(C)));
			S->Expect(P && P->IsHidden(), FString::Printf(TEXT("до смены карты сосед не рисуется: %s"), *S->View(P)));
			if (UAsdaSessionResetSubsystem* R = S->GI() ? S->GI()->GetSubsystem<UAsdaSessionResetSubsystem>() : nullptr)
			{
				R->Broadcast(EAsdaSessionResetReason::MapChanged);
			}
			S->bFlag = true;
		}
		return S->bFlag && S->El() > 2.6;
	}, [S]()
	{
		AAsdaPlayerCharacter* P = S->Peer();
		S->Expect(S->Has112(S->Self), TEXT("своё 112 переносится на новую карту (сервер его хранит, как все свои состояния)"));
		S->Expect(!S->Has112(kPeer), TEXT("список соседа прошлой карты снят"));
		S->Expect(!P || (!P->IsSkillStealth() && !P->IsHidden()), FString::Printf(TEXT("у актора соседа не осталось вида невидимости: %s"), *S->View(P)));
		S->Feed(5273, Body5273(S->Self));
	});

	Add(TEXT("итог"), 2.f, [S, Net]()
	{
		{ TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kPeer); S->Feed(4011, B); }
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(nullptr); }
		if (AAsdaPlayerCharacter* C = S->Me()) { C->EquippedWeaponSubType = S->WasWeaponSub; C->StopMove(); }
		if (UAsdaSkillUseSubsystem* U = S->SU()) { U->SimulateCastResult5257(0, kSkillHide); }
		Net->ClearMoveRequestInFlight(TEXT("самотест Hide: итог"));
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		Net->bStandCaptureSkillUse = S->bWasStand;
		if (UAsdaNetSubsystem::FAsdaVitals* V = Net->VitalsByUserIdx.Find(S->Self)) { V->MpCur = S->WasMp; }
		S->SelfScope.Reset();
	}, [S]() { return S->El() > 0.3; }, [S]()
	{
		AsdaSelfTestKit::Finish(S->World.Get(), kCmd, false, LogAsdaHideTest, S->Tag, S->Ok, S->Fail);
	});
	RunSteps(S, W, KitArgs, Net);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestHide(
	TEXT("asda.Skill.SelfTestHide"),
	TEXT("Selftest of Hide 759 on the real path (04.11): cast → 5256, live 5272/5271/5257 → state 112 + icon, own α 0.3, crouch walk, "
	     "peer not drawn + target dropped, removal 5273, refusal 28 → notice 1157, timer expiry, map change. Offline only. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Skill.SelfTestHide"));
		FAsdaHideSkillSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
