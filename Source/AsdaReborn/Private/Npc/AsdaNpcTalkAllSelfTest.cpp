// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ SELFTEST: EVERY NPC OF A MAP IS CLICKABLE AND TALKABLE — `asda.SelfTest.NpcTalkAll map=<id> [npc=<id>] [level=N] [quit]`
//   (04.11.2026, owner bug "some NPCs are uninteractable" — Sunny Coast and Silaris, e.g. Wizard Chmella, NPC 20).
//
//   Offline (`-AsdaNoNet`), on the baked level of `map` (batch: `--map /Game/Map/<m>/<m>?game=/Script/AsdaReborn.AsdaWorldGameMode`).
//   Player path, step by step for each baked, not disabled NPC of the level:
//     A  adoption by a real 4067 record (`DebugFeedPacket`: renderId, NpcId, the actor's own cell) — the actor must become shown,
//        collidable and owned by the network (`bNetSpawned`), exactly as on the server's area-of-interest entry;
//     P  pick: own character stands 400 units from the NPC in each of 8 directions (on ground), the camera looks at the NPC
//        (arm 700, pitch −20 — the in-game default); the screen is scanned around the NPC with the SAME ray the click uses
//        (`TraceScreenSkippingUnrenderedChars`, visibility, complex). Clickable = at least one direction where the ray hits this NPC;
//        for a blocked direction the first hit at the NPC centre is logged (what covers it);
//     T1 talk inside the zone: character inside the talk box (`IsInsideNpcTalkZone`), the click branch (`StartNpcTalk`, the same
//        call the double click makes) — the NPC dialog must open for this NpcId (board NPCs, class 5 — their board window);
//     T2 talk from outside: from each walkable stand point 400 units away, `StartNpcTalk` → the run-in route (with replans) → the
//        arrival tick opens the dialog (≤ 8 s); passes if any direction opens it, failed directions are listed.
//   Own level/class are substituted (`level=`, default 50): the offline character is level 0 and scripts would select nothing.
//   Lines «NPC-TALK ok/FAIL», summary «NPC-TALK ИТОГ: ok=… fail=…». Map 19 (army battle) has no dialog by design (6723) — T1/T2 skipped.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Npc/AsdaNpcActor.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "Npc/AsdaNpcGmOnly.h"               // NPCs shown only to GM_ characters are not on the player path
#include "AsdaNetSubsystem.h"                   // DebugFeedPacket — 4067 through the real receiver
#include "AsdaBytes.h"                          // 4067 record fields
#include "AsdaCVarSave.h"                       // ArmWorldGuard
#include "Quest/AsdaBoardQuestSubsystem.h"      // class 5 opens the board window
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaGroundTrace.h"              // ground height for the stand points
#include "World/AsdaCoord.h"                    // UeToGame2D
#include "World/AsdaWalkGrid.h"                 // stand points only on walkable cells
#include "Test/AsdaSelfTestKit.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaNpcTalkAll, Log, All);

namespace AsdaNpcTalkAll
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.NpcTalkAll");   // command
	static const TCHAR* const kPrefix = TEXT("NPC-TALK");                 // log prefix
	static constexpr float kStand = 400.f;                                // stand distance from the NPC, world units
	static constexpr int32 kRidBase = 8100;                               // renderIds of the test adoptions
	static constexpr int32 kCampWarMap = 19;                              // army battle map: no dialog (6723)

	// Run state.
	struct FCtx
	{
		TWeakObjectPtr<UWorld> W;                     // run world
		bool bQuit = false;                           // quit at the end
		int32 MapId = -1;                             // map of the run
		int32 OnlyNpc = 0;                            // `npc=` filter (0 — all)
		int32 Level = 50;                             // `level=` own level for the script conditions
		int32 Ok = 0, Fail = 0;                       // counters
		TArray<TWeakObjectPtr<AAsdaNpcActor>> Npcs;   // baked NPCs of the level
		int32 Next = 0;                               // next NPC
		TArray<FString> Bad;                          // short list of failed NPCs for the summary
		FVector2D StartGame = FVector2D::ZeroVector;  // own start point (restored at the end)

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaNpcTalkAll, Log, TEXT("%s ok   %s"), kPrefix, *What); }
			else { ++Fail; UE_LOG(LogAsdaNpcTalkAll, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		template <class T> T* Sub() const { return W.IsValid() && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<T>() : nullptr; }
		AAsdaPlayerController* Pc() const { return W.IsValid() ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = Pc(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Stand point in direction i (0..7, 45° steps from the NPC heading) on the ground and on a walkable cell (a player cannot stand
	//   on a blocked one); false — no such point.
	static bool StandAt(UWorld* W, const AAsdaNpcActor* A, int32 i, FVector& Out, float& LookYaw, const AAsdaPlayerCharacter* Ch = nullptr)
	{
		const FVector O = A->GetActorLocation();
		const float Yaw = A->GetActorRotation().Yaw + 45.f * i;
		const FVector P = O + FRotator(0.f, Yaw, 0.f).Vector() * kStand;
		const float G = AsdaGround::GroundZ(W, (float)P.X, (float)P.Y, NAN);
		if (FMath::IsNaN(G)) { return false; }
		if (Ch && Ch->WalkGrid)
		{
			const FVector2D Gm = AsdaCoord::UeToGame2D(P);
			if (!Ch->WalkGrid->IsWorldWalkable((float)Gm.X, (float)Gm.Y)) { return false; }
		}
		Out = FVector(P.X, P.Y, G);
		LookYaw = Yaw + 180.f;
		return true;
	}

	// Place own character at Stand and the camera at the in-game default looking at the NPC.
	static void Place(const FCtxRef& C, const FVector& Stand, float LookYaw)
	{
		AAsdaPlayerCharacter* Ch = C->Ch();
		AAsdaPlayerController* P = C->Pc();
		if (!Ch || !P) { return; }
		Ch->StopMove();
		Ch->SetActorLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
		P->SetOrbitAnglesForBench(FRotator::NormalizeAxis(LookYaw - (float)Ch->GetActorRotation().Yaw), -20.f);
		if (Ch->CameraArm) { Ch->CameraArm->TargetArmLength = 700.f; }
	}

	// Screen scan around the NPC with the click ray: true — some pixel hits this NPC. OutBlock — first hit at the centre.
	static bool ScanPick(const FCtxRef& C, AAsdaNpcActor* A, FString& OutBlock)
	{
		AAsdaPlayerController* P = C->Pc();
		if (!P || !A) { return false; }
		int32 VX = 0, VY = 0; P->GetViewportSize(VX, VY);
		FBox Box(ForceInit);
		if (A->PickHitbox) { Box += A->PickHitbox->Bounds.GetBox(); }
		if (A->Mesh) { Box += A->Mesh->Bounds.GetBox(); }
		FVector2D S;
		if (!Box.IsValid || !P->ProjectWorldLocationToScreen(Box.GetCenter(), S)) { OutBlock = TEXT("NPC behind the camera"); return false; }
		{
			FHitResult H;
			if (P->TraceScreenSkippingUnrenderedChars(S, H))
			{
				OutBlock = FString::Printf(TEXT("%s/%s at %.0f"), H.GetActor() ? *H.GetActor()->GetName() : TEXT("-"),
					H.GetComponent() ? *H.GetComponent()->GetName() : TEXT("-"), H.Distance);
				if (H.GetActor() == A) { return true; }
			}
			else { OutBlock = TEXT("ray hit nothing"); }
		}
		for (int32 R = 3; R <= 120; R += 3)
		{
			for (int32 dy = -R; dy <= R; dy += 3)
			{
				for (int32 dx = -R; dx <= R; dx += 3)
				{
					if (FMath::Max(FMath::Abs(dx), FMath::Abs(dy)) != R) { continue; }
					const FVector2D Px(S.X + dx, S.Y + dy);
					if (Px.X < 4 || Px.Y < 4 || Px.X > VX - 4 || Px.Y > VY - 4) { continue; }
					FHitResult H;
					if (P->TraceScreenSkippingUnrenderedChars(Px, H) && H.GetActor() == A) { return true; }
				}
			}
		}
		return false;
	}

	static void Finish(const FCtxRef& C)
	{
		if (AAsdaPlayerCharacter* Ch = C->Ch()) { Ch->StopMove(); Ch->NetSnap(C->StartGame); }
		if (UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>()) { if (D->IsDialogOpen()) { D->Close(); } }
		if (GEngine && C->W.IsValid()) { GEngine->Exec(C->W.Get(), TEXT("asda.Quest.FakeSelf")); }   // level substitution removed
		const FString Tail = FString::Printf(TEXT(" map=%d NPC=%d failed: %s"), C->MapId, C->Npcs.Num(),
			C->Bad.Num() ? *FString::Join(C->Bad, TEXT(", ")) : TEXT("none"));
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaNpcTalkAll, kPrefix, C->Ok, C->Fail, *Tail);
	}

	static void NextNpc(const FCtxRef& C);
	static void WaitT2(const FCtxRef& C, TWeakObjectPtr<AAsdaNpcActor> WA, TSharedRef<int32> Dir, TSharedRef<FString> Fails, double T0, FString Tag);

	// Close whatever the talk opened.
	static void CloseTalk(const FCtxRef& C)
	{
		if (UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>()) { if (D->IsDialogOpen()) { D->Close(); } }
		if (UAsdaBoardQuestSubsystem* B = C->Sub<UAsdaBoardQuestSubsystem>()) { if (B->IsOpen()) { B->Close(); } }
		if (AAsdaPlayerController* P = C->Pc()) { P->ClearTarget(); }
	}

	// The talk opened for this NPC (dialog for this NpcId, or the board window for a class 5 board).
	static bool TalkOpened(const FCtxRef& C, const AAsdaNpcActor* A)
	{
		const UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>();
		const UAsdaBoardQuestSubsystem* B = C->Sub<UAsdaBoardQuestSubsystem>();
		return (D && D->IsDialogOpen() && D->GetCurrentNpcId() == A->NpcId) || (B && B->IsOpen() && B->IsBoardNpc(A->NpcId));
	}

	// T2 for direction *Dir and the next ones: the talk branch from the stand, then wait for the arrival tick to open the talk.
	static void TryT2(const FCtxRef& C, TWeakObjectPtr<AAsdaNpcActor> WA, TSharedRef<int32> Dir, TSharedRef<FString> Fails, FString Tag)
	{
		AAsdaNpcActor* A = WA.Get();
		UWorld* W = C->W.Get();
		if (!A || !W || !C->Pc()) { ++C->Next; NextNpc(C); return; }
		FVector St; float Yaw = 0.f;
		while (*Dir < 8 && !StandAt(W, A, *Dir, St, Yaw, C->Ch())) { ++*Dir; }
		if (*Dir >= 8)
		{
			C->Expect(false, FString::Printf(TEXT("T2 NPC %d %s: run-in from %.0f units opened the talk from no direction:%s"),
				A->NpcId, *Tag, kStand, Fails->IsEmpty() ? TEXT(" (no walkable stand point)") : **Fails));
			C->Bad.AddUnique(FString::Printf(TEXT("%d(T2)"), A->NpcId));
			CloseTalk(C);
			++C->Next;
			At(C, 0.2f, [C]() { NextNpc(C); });
			return;
		}
		Place(C, St, Yaw);
		At(C, 0.2f, [C, WA, Dir, Fails, Tag]()
		{
			AAsdaNpcActor* A2 = WA.Get();
			AAsdaPlayerController* P2 = C->Pc();
			if (!A2 || !P2) { ++C->Next; NextNpc(C); return; }
			P2->StartNpcTalk(A2);
			WaitT2(C, WA, Dir, Fails, FPlatformTime::Seconds(), Tag);
		});
	}

	static void WaitT2(const FCtxRef& C, TWeakObjectPtr<AAsdaNpcActor> WA, TSharedRef<int32> Dir, TSharedRef<FString> Fails, double T0, FString Tag)
	{
		AAsdaNpcActor* A = WA.Get();
		AAsdaPlayerController* P = C->Pc();
		if (!A || !P) { ++C->Next; NextNpc(C); return; }
		const bool bOpen = TalkOpened(C, A);
		const double Dt = FPlatformTime::Seconds() - T0;
		if (!bOpen && (P->IsNpcTalkPending() || P->IsMovingNow()) && Dt < 8.0)
		{
			At(C, 0.25f, [C, WA, Dir, Fails, T0, Tag]() { WaitT2(C, WA, Dir, Fails, T0, Tag); });
			return;
		}
		AAsdaPlayerCharacter* Ch = C->Ch();
		const FVector L = Ch ? Ch->GetActorLocation() : FVector::ZeroVector;
		if (bOpen)
		{
			C->Expect(true, FString::Printf(TEXT("T2 NPC %d %s: run-in from %.0f units (direction %d) opened the talk in %.1f s%s"),
				A->NpcId, *Tag, kStand, *Dir, Dt, Fails->IsEmpty() ? TEXT("") : *FString::Printf(TEXT("; before that failed:%s"), **Fails)));
			CloseTalk(C);
			++C->Next;
			At(C, 0.2f, [C]() { NextNpc(C); });
			return;
		}
		*Fails += FString::Printf(TEXT(" [dir %d: %.1f s, stopped %.0f from the NPC, pending %d]"), *Dir, Dt,
			FVector::Dist2D(L, A->GetActorLocation()), P->IsNpcTalkPending() ? 1 : 0);
		if (Ch) { Ch->StopMove(); }
		P->ClearPendingNpcTalk(TEXT("selftest: next direction"));
		CloseTalk(C);
		++*Dir;
		TryT2(C, WA, Dir, Fails, Tag);
	}

	static void RunNpc(const FCtxRef& C, AAsdaNpcActor* A)
	{
		UWorld* W = C->W.Get();
		AAsdaPlayerController* P = C->Pc();
		const FString Tag = FString::Printf(TEXT("«%s» cls %d cell (%d,%d)"), *A->NpcName.Left(24), A->TypeFunc, A->TileX, A->TileZ);
		// P: pick from 8 directions.
		int32 Clickable = 0, Tried = 0, FirstOk = -1;
		FString Blocks;
		for (int32 i = 0; i < 8; ++i)
		{
			FVector St; float Yaw = 0.f;
			if (!StandAt(W, A, i, St, Yaw, C->Ch())) { continue; }
			++Tried;
			Place(C, St, Yaw);
			// The camera arm follows the pawn this frame; the ray reads the view of the last rendered frame — refresh it.
			if (P->PlayerCameraManager) { P->PlayerCameraManager->UpdateCamera(0.f); }
			FString Block;
			if (ScanPick(C, A, Block)) { ++Clickable; if (FirstOk < 0) { FirstOk = i; } }
			else if (Blocks.Len() < 400) { Blocks += FString::Printf(TEXT(" [dir %d: %s]"), i, *Block); }
		}
		C->Expect(Clickable > 0, FString::Printf(TEXT("P NPC %d %s: clickable from %d of %d directions%s"),
			A->NpcId, *Tag, Clickable, Tried, Clickable < Tried ? *Blocks : TEXT("")));
		if (Clickable == 0) { C->Bad.AddUnique(FString::Printf(TEXT("%d(P)"), A->NpcId)); }
		if (C->MapId == kCampWarMap) { ++C->Next; NextNpc(C); return; }
		// T1: inside the talk box.
		CloseTalk(C);
		{
			FVector In = A->GetActorLocation() + FRotator(0.f, A->GetActorRotation().Yaw, 0.f).Vector() * 120.f;
			// ★ 04.11.2026: static objects (classes 1/3/4/5) have their own talk box (bulletin boards: ×0.5, half 105×119) — stand
			//   halfway between its centre and its edge along the NPC heading instead of the fixed 120 units.
			{
				FBox TB;
				if (A->GetTalkBoxWorld(TB))
				{
					const FVector Fw = FRotator(0.f, A->GetActorRotation().Yaw, 0.f).Vector();
					const double Half = FMath::Abs(Fw.X) * TB.GetExtent().X + FMath::Abs(Fw.Y) * TB.GetExtent().Y;
					In = FVector(TB.GetCenter().X, TB.GetCenter().Y, 0.0) + Fw * (0.5 * Half);
				}
			}
			const float G = AsdaGround::GroundZ(W, (float)In.X, (float)In.Y, NAN);
			In.Z = FMath::IsNaN(G) ? A->GetActorLocation().Z : G;
			Place(C, In, A->GetActorRotation().Yaw + 180.f);
			const bool bIn = P->IsInsideNpcTalkZone(A);
			P->StartNpcTalk(A);
			const bool bOpen = TalkOpened(C, A);
			C->Expect(bIn && bOpen, FString::Printf(TEXT("T1 NPC %d %s: inside the talk zone %d, talk opened %d"), A->NpcId, *Tag, bIn ? 1 : 0, bOpen ? 1 : 0));
			if (!bOpen) { C->Bad.AddUnique(FString::Printf(TEXT("%d(T1)"), A->NpcId)); }
			CloseTalk(C);
		}
		// T2: run-in from every walkable direction; one opened talk passes, failed directions are listed.
		TryT2(C, A, MakeShared<int32>(0), MakeShared<FString>(), Tag);
	}

	static void NextNpc(const FCtxRef& C)
	{
		if (C->Next >= C->Npcs.Num()) { Finish(C); return; }
		AAsdaNpcActor* A = C->Npcs[C->Next].Get();
		if (!A) { ++C->Next; NextNpc(C); return; }
		// A: adoption by a real 4067 record.
		if (!A->bNetSpawned)
		{
			if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>())
			{
				TArray<uint8> Body; Body.SetNumZeroed(108);
				AsdaBytes::WrI16(Body.GetData() + 0, (int16)(kRidBase + C->Next));
				AsdaBytes::WrI16(Body.GetData() + 2, (int16)A->NpcId);
				AsdaBytes::WrI16(Body.GetData() + 8, (int16)A->TileX);
				AsdaBytes::WrI16(Body.GetData() + 10, (int16)A->TileZ);
				Net->DebugFeedPacket(4067, Body);
			}
		}
		TWeakObjectPtr<AAsdaNpcActor> WA(A);
		At(C, 0.5f, [C, WA]()
		{
			AAsdaNpcActor* A2 = WA.Get();
			if (!A2) { ++C->Next; NextNpc(C); return; }
			const bool bAdopted = A2->bNetSpawned && !A2->IsHidden() && A2->GetActorEnableCollision();
			C->Expect(bAdopted, FString::Printf(TEXT("A NPC %d «%s» cell (%d,%d): adopted by 4067, shown %d, collision %d"),
				A2->NpcId, *A2->NpcName.Left(24), A2->TileX, A2->TileZ, A2->IsHidden() ? 0 : 1, A2->GetActorEnableCollision() ? 1 : 0));
			if (!bAdopted) { C->Bad.AddUnique(FString::Printf(TEXT("%d(A)"), A2->NpcId)); ++C->Next; NextNpc(C); return; }
			RunNpc(C, A2);
		});
	}

	static void Run(const FCtxRef& C)
	{
		UWorld* W = C->W.Get();
		if (!W || !C->Pc() || !C->Ch()) { C->Expect(false, TEXT("world, controller and own character exist")); Finish(C); return; }
		C->StartGame = AsdaCoord::UeToGame2D(C->Ch()->GetActorLocation());
		// Own level/class as a real character (the offline one is level 0 — scripts select nothing and close the talk by design).
		if (GEngine) { GEngine->Exec(W, *FString::Printf(TEXT("asda.Quest.FakeSelf %d 1 0"), C->Level)); }
		for (TActorIterator<AAsdaNpcActor> It(W); It; ++It)
		{
			AAsdaNpcActor* A = *It;
			if (!A || A->bDisabled || A->NpcId <= 0 || A->bNetSpawned) { continue; }
			if (C->OnlyNpc > 0 && A->NpcId != C->OnlyNpc) { continue; }
			// ★ 11.11.2026 (D4, red selftests of the 7.6.0 baseline): since 09.11.2026 (owner's decision, Npc/AsdaNpcGmOnly.h) NPCs of
			//   the GM-only list stay hidden and without collision for a character whose name has no GM_ prefix — adoption by 4067
			//   keeps them hidden on purpose. The test expected «adopted and shown» for them (Alpen: 2, 37, 98, 99 → 4 failures).
			//   They are not on the player path, so they are skipped (run with a GM_ name to cover them).
			if (AsdaNpcGmOnly::HiddenForSelfOnMap(W->GetGameInstance(), A->NpcId, C->MapId))
			{
				UE_LOG(LogAsdaNpcTalkAll, Log, TEXT("NPC-TALK skip NPC %d: GM-only on map %d, hidden for this character"), A->NpcId, C->MapId);
				continue;
			}
			C->Npcs.Add(A);
		}
		C->Expect(C->Npcs.Num() > 0, FString::Printf(TEXT("baked NPCs on map %d: %d"), C->MapId, C->Npcs.Num()));
		NextNpc(C);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestNpcTalkAll(
	AsdaNpcTalkAll::kCmd,
	TEXT("asda.SelfTest.NpcTalkAll map=<id> [npc=<id>] [level=N] [quit] — every NPC of the level: adoption by 4067, click ray from 8 sides, ")
	TEXT("talk inside the zone and after the run-in. Offline, on the baked level of `map`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaNpcTalkAll;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		int32 MapId = -1, Only = 0, Level = 50;
		for (const FString& A : Args)
		{
			if (A.StartsWith(TEXT("map="))) { MapId = FCString::Atoi(*A.Mid(4)); }
			if (A.StartsWith(TEXT("npc="))) { Only = FCString::Atoi(*A.Mid(4)); }
			if (A.StartsWith(TEXT("level="))) { Level = FCString::Atoi(*A.Mid(6)); }
		}
		if (MapId < 0) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("map=<id> is required")); return; }
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, *FString::Printf(TEXT("Asda.Load.CurrentMap %d"), MapId));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s map=%d npc=%d level=%d selfready%s"), kCmd, MapId, Only, Level, KA.bQuit ? TEXT(" quit") : TEXT(""));
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
		C->Level = FMath::Clamp(Level, 1, 150);
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.5f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
