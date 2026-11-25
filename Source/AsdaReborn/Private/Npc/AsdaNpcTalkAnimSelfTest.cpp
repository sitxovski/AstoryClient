// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ SELFTEST: NPC TALK ANIMATION ON THE PLAYER PATH — `asda.SelfTest.NpcTalkAnim map=<id> [npc=<id>|npc=all] [shots=<dir>] [quit]`
//   (04.11.2026, owner bug "talking to the shop NPC Ian glitches the NPC animation"; report `Reports/npc_talk_anim/REPORT.md`).
//
//   Offline (`-AsdaNoNet`), on the baked level of `map`. For each selected baked NPC (default: Ian, NpcId 3):
//     A  adoption by a real 4067 record (`DebugFeedPacket`) — the same entry the server's area-of-interest uses;
//     I  1.5 s of idle;
//     T  the click branch `StartNpcTalk` from inside the talk zone (the same call the double click makes) — the dialog must open,
//        the NPC must go to state 1 (greeting, clip 2), then 2 (talk loop, clip 3); 3 s of talk;
//     C  the dialog closes (`Close`, the same exit as the window's X) — state 3 (farewell, clip 4), then back to 0 (idle clip 0);
//   every frame the NPC pose is sampled (component-space bone positions × component scale = world units, MEDIAN over bones per frame).
//   A hard clip switch is a one-frame spike: that frame's speed is several times its neighbours'; a cross-fade has none, and fast
//   authored motion at a clip start is not a spike (the next frames are as fast).
//   Checks: the state chain 1→2→3→0 was walked (types 0/4; other types — no talk states by design, `uiPanelSelectTab`), each change
//   went through the blending instance (`UAsdaBlendAnimInstance`, last blend > 0 s), no spike (> 3× neighbours and > 150 u/s) within
//   0.15 s of a change (frames next to a hitch > 0.1 s are counted, not judged). `shots=<dir>` — PNG frames without UI (idle, talk +0.05/+0.15/+0.4/+1.5 s, farewell, back to idle);
//   `yaw=` — camera orbit offset, degrees (default 90); `burst=N` — PNG of N consecutive frames from the talk start.
//   Lines «NPC-ANIM ok/FAIL», summary «NPC-ANIM ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Npc/AsdaNpcActor.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "AsdaNetSubsystem.h"                   // DebugFeedPacket — 4067 through the real receiver
#include "AsdaBytes.h"                          // 4067 record fields
#include "AsdaCVarSave.h"                       // ArmWorldGuard
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaGroundTrace.h"              // ground height for the stand point
#include "World/AsdaCoord.h"                    // UeToGame2D
#include "World/AsdaBlendAnimInstance.h"        // blend instance diagnostics
#include "Test/AsdaSelfTestKit.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "UnrealClient.h"                       // FScreenshotRequest
#include "Misc/Paths.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaNpcTalkAnim, Log, All);

namespace AsdaNpcTalkAnim
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.NpcTalkAnim");   // command
	static const TCHAR* const kPrefix = TEXT("NPC-ANIM");                  // log prefix
	static int32 GNextRid = 8300;                                          // renderIds of the test adoptions (unique per process: a known rid is ignored by 4067)
	static constexpr float kIdleSec = 3.0f;                                // idle before the talk (spawn warp effect of the own character fades first)
	static constexpr float kTalkSec = 3.0f;                                // talk before the close
	static constexpr float kBackSec = 10.0f;                               // limit for farewell → idle
	static constexpr float kHitchSec = 0.1f;                               // s: a longer frame is a hitch (pose spike there is not judged)
	static constexpr float kMinPopSpeed = 150.f;                           // u/s (median bone): below this a transition frame is never a pop

	// One sampled frame of the NPC under test.
	struct FFrame
	{
		double T = 0.0;       // GAME seconds since the NPC run started (sum of the engine's frame deltas)
		int32 State = -1;     // EntityAnimState in this frame
		float Speed = 0.f;    // MEDIAN bone speed, u/s (a pop moves the whole body; one fast helper bone does not count)
		float MaxSpeed = 0.f; // max bone speed, u/s (diagnostics)
		float Dt = 0.f;       // engine frame delta, s — the time the animation advanced by (a long frame = hitch: screenshot, first dialog open)
		FString Clip;         // clip shown
	};

	// Run state.
	struct FCtx
	{
		TWeakObjectPtr<UWorld> W;                     // run world
		bool bQuit = false;                           // quit at the end
		int32 MapId = -1;                             // map of the run
		int32 OnlyNpc = 3;                            // NpcId to test (0 — all of the level)
		FString ShotDir;                              // PNG folder ("" — no frames)
		float CamYaw = 90.f;                          // camera orbit offset from the NPC-facing view, degrees (`yaw=`)
		int32 Burst = 0;                              // `burst=N`: PNG of N consecutive frames from the talk start
		int32 BurstLeft = 0;                          // frames of the burst still to shoot
		int32 BurstIdx = 0;                           // next burst frame number
		int32 Ok = 0, Fail = 0;                       // counters
		TArray<TWeakObjectPtr<AAsdaNpcActor>> Npcs;   // NPCs to test
		int32 Next = 0;                               // next NPC
		TArray<FString> Bad;                          // failed NPCs for the summary
		FVector2D StartGame = FVector2D::ZeroVector;  // own start point (restored at the end)
		// Per-NPC sampling.
		TWeakObjectPtr<AAsdaNpcActor> Cur;            // NPC being sampled
		double T0 = 0.0;                              // (unused since 04.11 v4: time is the sum of engine deltas)
		TArray<FTransform> PrevCS;                    // previous frame pose
		double PrevT = 0.0;                           // previous frame time
		TArray<FFrame> Frames;                        // samples
		TArray<int32> StatesSeen;                     // state chain (deduplicated consecutive)
		TArray<float> BlendSecs;                      // blend length of each state change (blend instance)
		FDelegateHandle Hook;                         // per-frame hook

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaNpcTalkAnim, Log, TEXT("%s ok   %s"), kPrefix, *What); }
			else { ++Fail; UE_LOG(LogAsdaNpcTalkAnim, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		template <class T> T* Sub() const { return W.IsValid() && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<T>() : nullptr; }
		AAsdaPlayerController* Pc() const { return W.IsValid() ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = Pc(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	static void Shot(const FCtxRef& C, const AAsdaNpcActor* A, const TCHAR* Tag)
	{
		if (C->ShotDir.IsEmpty() || !A) { return; }
		const FString F = FPaths::Combine(C->ShotDir, FString::Printf(TEXT("npc%03d_%s.png"), A->NpcId, Tag));
		FScreenshotRequest::RequestScreenshot(F, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);   // the NPC, not the dialog windows
		UE_LOG(LogAsdaNpcTalkAnim, Log, TEXT("%s кадр %s"), kPrefix, *F);
	}

	// Per-frame sample of the NPC pose (after the actor tick: the pose of this frame is final).
	//   ★ 04.11.2026 v4: speed = movement / ENGINE frame delta (the time the animation really advanced by). Wall-clock time between
	//   samples (the earlier version) differs from it on irregular frames (dialog open, screenshots) and inflated the speed of one
	//   frame — a false «pop» on fast-moving models (crystal NPC 52: 3.9×).
	static void Sample(const FCtxRef& C, float EngineDt)
	{
		AAsdaNpcActor* A = C->Cur.Get();
		if (!A || !A->Mesh) { return; }
		const double Now = C->PrevT + (double)EngineDt;
		const TArray<FTransform>& CS = A->Mesh->GetComponentSpaceTransforms();
		const float Scale = (float)A->Mesh->GetComponentScale().GetAbs().X;
		FFrame F;
		F.T = Now;
		F.State = FMath::Max(0, A->EntityAnimState);   // −1 = never set = idle
		const UAnimSingleNodeInstance* I = A->Mesh->GetSingleNodeInstance();
		F.Clip = I && I->GetAnimationAsset() ? I->GetAnimationAsset()->GetName() : FString();
		const float Dt = EngineDt;
		F.Dt = Dt;
		if (C->PrevCS.Num() == CS.Num() && Dt > 1e-4f)
		{
			TArray<float> V; V.Reserve(CS.Num());
			for (int32 i = 0; i < CS.Num(); ++i)
			{
				const float Sp = (float)((CS[i].GetLocation() - C->PrevCS[i].GetLocation()).Size() * Scale) / Dt;
				V.Add(Sp);
				F.MaxSpeed = FMath::Max(F.MaxSpeed, Sp);
			}
			V.Sort();
			F.Speed = V.Num() ? V[V.Num() / 2] : 0.f;
		}
		C->PrevCS = CS;
		C->PrevT = Now;
		if (C->StatesSeen.Num() == 0 || C->StatesSeen.Last() != F.State)
		{
			C->StatesSeen.Add(F.State);
			const UAsdaBlendAnimInstance* BI = Cast<UAsdaBlendAnimInstance>(A->Mesh->GetAnimInstance());
			C->BlendSecs.Add(BI ? BI->GetLastBlendSec() : -1.f);
		}
		C->Frames.Add(MoveTemp(F));
		if (C->BurstLeft > 0)
		{
			--C->BurstLeft;
			Shot(C, A, *FString::Printf(TEXT("1b_talk_f%02d_state%d"), C->BurstIdx++, FMath::Max(0, A->EntityAnimState)));
		}
	}

	static void Finish(const FCtxRef& C)
	{
		if (C->Hook.IsValid()) { FWorldDelegates::OnWorldPostActorTick.Remove(C->Hook); C->Hook.Reset(); }
		if (AAsdaPlayerCharacter* Ch = C->Ch()) { Ch->SetActorHiddenInGame(false); Ch->StopMove(); Ch->NetSnap(C->StartGame); }
		if (UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>()) { if (D->IsDialogOpen()) { D->Close(); } }
		if (GEngine && C->W.IsValid()) { GEngine->Exec(C->W.Get(), TEXT("asda.Quest.FakeSelf")); }
		const FString Tail = FString::Printf(TEXT(" map=%d NPC=%d failed: %s"), C->MapId, C->Npcs.Num(),
			C->Bad.Num() ? *FString::Join(C->Bad, TEXT(", ")) : TEXT("none"));
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaNpcTalkAnim, kPrefix, C->Ok, C->Fail, *Tail);
	}

	static void NextNpc(const FCtxRef& C);

	// Verdict for one NPC from the samples.
	static void Judge(const FCtxRef& C, AAsdaNpcActor* A, bool bTalkStates)
	{
		const FString Tag = FString::Printf(TEXT("NPC %d «%s» %s cls %d"), A->NpcId, *A->NpcName.Left(24),
			A->Mesh && A->Mesh->GetSkeletalMeshAsset() ? *A->Mesh->GetSkeletalMeshAsset()->GetName() : TEXT("-"), A->TypeFunc);
		if (C->StatesSeen.Num() == 0 || C->Frames.Num() < 3)
		{
			C->Expect(false, FString::Printf(TEXT("%s: pose samples taken (%d frames)"), *Tag, C->Frames.Num()));
			C->Bad.AddUnique(FString::Printf(TEXT("%d(samples)"), A->NpcId));
			return;
		}
		TArray<FString> Chain;
		for (int32 s : C->StatesSeen) { Chain.Add(FString::FromInt(s)); }
		const FString ChainS = FString::Join(Chain, TEXT("→"));
		// The chain must contain 1, 2, 3 and end in 0 (types 0/4); other types stay in 0.
		bool bChain = true;
		if (bTalkStates)
		{
			int32 Want[] = { 1, 2, 3, 0 };
			int32 k = 0;
			for (int32 s : C->StatesSeen) { if (k < 4 && s == Want[k]) { ++k; } }
			bChain = (k == 4) && C->StatesSeen.Last() == 0;
		}
		else
		{
			bChain = C->StatesSeen.Num() == 1 && C->StatesSeen[0] == 0;
		}
		C->Expect(bChain, FString::Printf(TEXT("%s: state chain %s%s"), *Tag, *ChainS, bTalkStates ? TEXT(" (want 1→2→3→0)") : TEXT(" (no talk states by design)")));
		if (!bChain) { C->Bad.AddUnique(FString::Printf(TEXT("%d(chain)"), A->NpcId)); }
		// Every state change went through the blending instance.
		bool bBlend = true;
		FString BlendS;
		for (int32 i = 1; i < C->BlendSecs.Num(); ++i)
		{
			BlendS += FString::Printf(TEXT(" %d:%.2f"), C->StatesSeen[i], C->BlendSecs[i]);
			if (C->BlendSecs[i] <= 0.f) { bBlend = false; }
		}
		if (bTalkStates)
		{
			C->Expect(bBlend, FString::Printf(TEXT("%s: blend on each state change (state:seconds)%s"), *Tag, *BlendS));
			if (!bBlend) { C->Bad.AddUnique(FString::Printf(TEXT("%d(blend)"), A->NpcId)); }
		}
		// Pops: a hard clip switch is a ONE-FRAME spike of the median bone speed (the body jumps, the next frames move normally);
		//   a cross-fade has none. Fast authored motion at a clip start is not a spike (the following frames are as fast).
		//   Frames within [change − 0.02 s, change + 0.15 s]: ratio = speed / max(neighbours ±1..2 frames, 1 u/s).
		TArray<double> Changes;
		for (int32 i = 1; i < C->Frames.Num(); ++i) { if (C->Frames[i].State != C->Frames[i - 1].State) { Changes.Add(C->Frames[i].T); } }
		float WorstRatio = 0.f, WorstSpeed = 0.f;
		double WorstT = 0.0;
		bool bPop = false;
		int32 Hitch = 0;
		for (int32 i = 3; i + 2 < C->Frames.Num(); ++i)
		{
			const FFrame& F = C->Frames[i];
			bool bNear = false;
			for (double t : Changes) { if (F.T >= t - 0.02 && F.T <= t + 0.15) { bNear = true; } }
			if (!bNear) { continue; }
			// A hitch (frame > 0.1 s next to this one) lets the whole blend pass between two samples: speed there says nothing.
			if (FMath::Max3(C->Frames[i - 1].Dt, F.Dt, C->Frames[i + 1].Dt) > kHitchSec) { ++Hitch; continue; }
			const float Nb = FMath::Max(FMath::Max(C->Frames[i - 1].Speed, C->Frames[i - 2].Speed), FMath::Max(C->Frames[i + 1].Speed, C->Frames[i + 2].Speed));
			const float R = F.Speed / FMath::Max(Nb, 1.f);
			if (R > WorstRatio) { WorstRatio = R; WorstSpeed = F.Speed; WorstT = F.T; }
			if (R > 3.f && F.Speed > kMinPopSpeed) { bPop = true; }
		}
		// Frames around the worst one (game time, engine delta, state, clip, median and max bone speed) — the evidence in the log.
		for (int32 i = 0; i < C->Frames.Num(); ++i)
		{
			const FFrame& F = C->Frames[i];
			if (FMath::Abs(F.T - WorstT) > 0.08) { continue; }
			UE_LOG(LogAsdaNpcTalkAnim, Log, TEXT("%s кадр NPC %d: t=%.3f dt=%.4f состояние %d клип %s медиана %.0f макс %.0f u/s%s"), kPrefix, A->NpcId,
				F.T, F.Dt, F.State, *F.Clip, F.Speed, F.MaxSpeed, F.T == WorstT ? TEXT("  ← худший") : TEXT(""));
		}
		C->Expect(!bPop, FString::Printf(TEXT("%s: no one-frame pose spike at state changes (worst: median bone speed %.0f u/s, %.1f× its neighbours, at %.2f s; spike = >3× and >%.0f u/s); changes %d, frames %d, hitch frames not judged %d"),
			*Tag, WorstSpeed, WorstRatio, WorstT, kMinPopSpeed, Changes.Num(), C->Frames.Num(), Hitch));
		if (bPop) { C->Bad.AddUnique(FString::Printf(TEXT("%d(pop)"), A->NpcId)); }
	}

	static void RunNpc(const FCtxRef& C, AAsdaNpcActor* A)
	{
		UWorld* W = C->W.Get();
		AAsdaPlayerController* P = C->Pc();
		AAsdaPlayerCharacter* Ch = C->Ch();
		if (!W || !P || !Ch) { Finish(C); return; }
		// Stand inside the talk zone, 30 units from the NPC (the orbit camera then circles the NPC); own character hidden so the frames show the NPC.
		FVector In = A->GetActorLocation() + FRotator(0.f, A->GetActorRotation().Yaw, 0.f).Vector() * 30.f;
		const float G = AsdaGround::GroundZ(W, (float)In.X, (float)In.Y, NAN);
		In.Z = FMath::IsNaN(G) ? A->GetActorLocation().Z : G;
		Ch->StopMove();
		Ch->SetActorLocation(In, false, nullptr, ETeleportType::TeleportPhysics);
		const float Look = A->GetActorRotation().Yaw + 180.f;
		P->SetOrbitAnglesForBench(FRotator::NormalizeAxis(Look + C->CamYaw - (float)Ch->GetActorRotation().Yaw), -8.f);
		if (Ch->CameraArm) { Ch->CameraArm->TargetArmLength = 260.f; }
		Ch->SetActorHiddenInGame(true);
		C->Cur = A;
		C->Frames.Reset();
		C->StatesSeen.Reset();
		C->BlendSecs.Reset();
		C->PrevCS.Reset();
		C->T0 = FPlatformTime::Seconds();
		C->PrevT = 0.0;
		const bool bTalkStates = (A->TypeFunc == 0 || A->TypeFunc == 4);
		C->Expect(AsdaAnimSmooth::IsInstalled(A->Mesh), FString::Printf(TEXT("NPC %d: blending anim instance installed on the mesh"), A->NpcId));
		TWeakObjectPtr<AAsdaNpcActor> WA(A);
		At(C, kIdleSec - 0.5f, [C, WA]() { Shot(C, WA.Get(), TEXT("0_idle")); });
		At(C, kIdleSec, [C, WA, bTalkStates]()
		{
			AAsdaNpcActor* A2 = WA.Get();
			AAsdaPlayerController* P2 = C->Pc();
			if (!A2 || !P2) { ++C->Next; NextNpc(C); return; }
			C->BurstLeft = C->ShotDir.IsEmpty() ? 0 : C->Burst;   // `burst=N`: the next N frames from here
			C->BurstIdx = 0;
			P2->StartNpcTalk(A2);
			const UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>();
			const bool bOpen = D && D->IsDialogOpen() && D->GetCurrentNpcId() == A2->NpcId;
			C->Expect(bOpen, FString::Printf(TEXT("NPC %d: talk opened by the click branch, state %d"), A2->NpcId, A2->EntityAnimState));
			At(C, 0.05f, [C, WA]() { Shot(C, WA.Get(), TEXT("1_talk+005")); });
			At(C, 0.15f, [C, WA]() { Shot(C, WA.Get(), TEXT("2_talk+015")); });
			At(C, 0.40f, [C, WA]() { Shot(C, WA.Get(), TEXT("3_talk+040")); });
			At(C, 1.50f, [C, WA]() { Shot(C, WA.Get(), TEXT("4_talk+150")); });
			At(C, kTalkSec, [C, WA, bTalkStates]()
			{
				if (UAsdaNpcDialogSubsystem* D2 = C->Sub<UAsdaNpcDialogSubsystem>()) { if (D2->IsDialogOpen()) { D2->Close(); } }
				const double TClose = FPlatformTime::Seconds();
				// Wait for the farewell to end (state back to 0), shooting the farewell and the return.
				TSharedRef<bool> bFare = MakeShared<bool>(false);
				TSharedRef<TFunction<void()>> Poll = MakeShared<TFunction<void()>>();
				*Poll = [C, WA, TClose, bFare, Poll, bTalkStates]()
				{
					AAsdaNpcActor* A3 = WA.Get();
					if (!A3) { ++C->Next; NextNpc(C); return; }
					if (!*bFare && A3->EntityAnimState == 3) { *bFare = true; At(C, 0.05f, [C, WA]() { Shot(C, WA.Get(), TEXT("5_farewell+005")); }); }
					const bool bBack = A3->EntityAnimState <= 0;
					if ((bBack || !bTalkStates) && FPlatformTime::Seconds() - TClose > 0.3)
					{
						At(C, 0.05f, [C, WA]() { Shot(C, WA.Get(), TEXT("6_back+005")); });
						At(C, 0.8f, [C, WA, bTalkStates]()
						{
							Shot(C, WA.Get(), TEXT("7_back+080"));
							AAsdaNpcActor* A4 = WA.Get();
							C->Cur = nullptr;
							if (A4) { Judge(C, A4, bTalkStates); }
							if (AAsdaPlayerController* P4 = C->Pc()) { P4->ClearTarget(); }
							++C->Next;
							At(C, 0.3f, [C]() { NextNpc(C); });
						});
						return;
					}
					if (FPlatformTime::Seconds() - TClose > kBackSec)
					{
						C->Expect(false, FString::Printf(TEXT("NPC %d: back to idle within %.0f s after close (state %d)"), A3->NpcId, kBackSec, A3->EntityAnimState));
						C->Bad.AddUnique(FString::Printf(TEXT("%d(back)"), A3->NpcId));
						C->Cur = nullptr;
						++C->Next;
						At(C, 0.3f, [C]() { NextNpc(C); });
						return;
					}
					At(C, 0.05f, *Poll);
				};
				(*Poll)();
			});
		});
	}

	static void NextNpc(const FCtxRef& C)
	{
		if (C->Next >= C->Npcs.Num()) { Finish(C); return; }
		AAsdaNpcActor* A = C->Npcs[C->Next].Get();
		if (!A) { ++C->Next; NextNpc(C); return; }
		if (!A->bNetSpawned)
		{
			if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>())
			{
				TArray<uint8> Body; Body.SetNumZeroed(108);
				AsdaBytes::WrI16(Body.GetData() + 0, (int16)(GNextRid++));
				AsdaBytes::WrI16(Body.GetData() + 2, (int16)A->NpcId);
				AsdaBytes::WrI16(Body.GetData() + 8, (int16)A->TileX);
				AsdaBytes::WrI16(Body.GetData() + 10, (int16)A->TileZ);
				Net->DebugFeedPacket(4067, Body);
			}
		}
		TWeakObjectPtr<AAsdaNpcActor> WA(A);
		At(C, 1.2f, [C, WA]()   // adoption fade-in is 1 s
		{
			AAsdaNpcActor* A2 = WA.Get();
			if (!A2) { ++C->Next; NextNpc(C); return; }
			const bool bAdopted = A2->bNetSpawned && !A2->IsHidden();
			C->Expect(bAdopted, FString::Printf(TEXT("NPC %d «%s»: adopted by 4067 and shown"), A2->NpcId, *A2->NpcName.Left(24)));
			if (!bAdopted) { C->Bad.AddUnique(FString::Printf(TEXT("%d(A)"), A2->NpcId)); ++C->Next; NextNpc(C); return; }
			RunNpc(C, A2);
		});
	}

	static void Run(const FCtxRef& C)
	{
		UWorld* W = C->W.Get();
		if (!W || !C->Pc() || !C->Ch()) { C->Expect(false, TEXT("world, controller and own character exist")); Finish(C); return; }
		C->StartGame = AsdaCoord::UeToGame2D(C->Ch()->GetActorLocation());
		if (GEngine) { GEngine->Exec(W, TEXT("asda.Quest.FakeSelf 50 1 0")); }   // scripts select a page (offline character is level 0)
		for (TActorIterator<AAsdaNpcActor> It(W); It; ++It)
		{
			AAsdaNpcActor* A = *It;
			if (!A || A->bDisabled || A->NpcId <= 0 || A->bNetSpawned) { continue; }
			if (C->OnlyNpc > 0 && A->NpcId != C->OnlyNpc) { continue; }
			C->Npcs.Add(A);
		}
		C->Expect(C->Npcs.Num() > 0, FString::Printf(TEXT("NPCs to test on map %d: %d"), C->MapId, C->Npcs.Num()));
		TWeakPtr<FCtx> WC = C;
		C->Hook = FWorldDelegates::OnWorldPostActorTick.AddLambda([WC](UWorld* InW, ELevelTick, float Dt)
		{
			TSharedPtr<FCtx> P = WC.Pin();
			if (P && P->W.Get() == InW && P->Cur.IsValid()) { Sample(P.ToSharedRef(), Dt); }
		});
		NextNpc(C);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestNpcTalkAnim(
	AsdaNpcTalkAnim::kCmd,
	TEXT("asda.SelfTest.NpcTalkAnim map=<id> [npc=<id>|npc=all] [shots=<dir>] [yaw=<deg>] [burst=N] [quit] — NPC talk animation on the player path: adoption, idle, ")
	TEXT("click-branch talk, close; state chain 1→2→3→0, blend on every change, no pose pop. Offline, on the baked level of `map`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaNpcTalkAnim;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		int32 MapId = -1, Only = 3;
		float CamYaw = 90.f;
		int32 Burst = 0;
		FString Shots;
		for (const FString& A : Args)
		{
			if (A.StartsWith(TEXT("map="))) { MapId = FCString::Atoi(*A.Mid(4)); }
			if (A.StartsWith(TEXT("npc="))) { Only = A.Mid(4).Equals(TEXT("all"), ESearchCase::IgnoreCase) ? 0 : FCString::Atoi(*A.Mid(4)); }
			if (A.StartsWith(TEXT("shots="))) { Shots = A.Mid(6); }
			if (A.StartsWith(TEXT("yaw="))) { CamYaw = FCString::Atof(*A.Mid(4)); }
			if (A.StartsWith(TEXT("burst="))) { Burst = FMath::Clamp(FCString::Atoi(*A.Mid(6)), 0, 60); }
		}
		if (MapId < 0) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("map=<id> is required")); return; }
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, *FString::Printf(TEXT("Asda.Load.CurrentMap %d"), MapId));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s map=%d npc=%d yaw=%.0f burst=%d%s selfready%s"), kCmd, MapId, Only, CamYaw, Burst,
				Shots.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" shots=%s"), *Shots), KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		C->MapId = MapId;
		C->OnlyNpc = Only;
		C->ShotDir = Shots;
		C->CamYaw = CamYaw;
		C->Burst = Burst;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.5f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
