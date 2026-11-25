// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ SELFTEST: BULLETIN BOARD INTERACTION ZONE 2× SMALLER — `asda.SelfTest.BoardZone map=<id> [shot] [quit]`
//   `all` (04.11.2026, second pass): every static NPC of the level (classes 1/3/4/5) — non-boards are checked against the key
//   `asda.Npc.StaticZoneFromGeometry` (OLD 0 / NEW 1): capsule/ring never bigger, model inside the column, talk box unchanged,
//   model centre clickable, neighbours not eaten (block N below).
//   (04.11.2026, owner request "bulletin board NPCs have a really big interaction trigger zone, make it 2x smaller";
//   rule and reasoning — above `AAsdaNpcActor::GetTalkBoxWorld`, key `asda.Npc.BoardZoneScale`).
//
//   Offline (`-AsdaNoNet`), on the baked level of `map` (batch: `--map /Game/Map/<m>/<m>?game=/Script/AsdaReborn.AsdaWorldGameMode`).
//   Every bulletin board (NPC class 5) of the level and every other NPC within kNear of a board are adopted by a real 4067 record
//   (`DebugFeedPacket`), then for each board, with the key at 1 (OLD) and at its value (NEW):
//     M  measure: skinned component bounds vs the asset geometry vs the physics-asset bounds (why the old zone was so big),
//        bone box, select box, talk box, click capsule; NEW = OLD × scale horizontally, same centre and height;
//     R  ring: own target set on the board (`SetTarget` — the click path) → world scale of the attached ring, NEW = OLD × scale;
//     C  click with the click ray (`TraceScreenSkippingUnrenderedChars`) from every walkable side: board centre and a point at
//        0.8 R(new) beside it — the board; a point at 1.3 R(new) — NOT the board now, the board before (OLD);
//     T  talk: own character at 0.85 of the new talk half-extent along the board facing — inside, the board window opens on the
//        click branch (`StartNpcTalk`); at 1.25 — outside now (inside before); run-in from 400 units on every walkable side
//        (`StartNpcTalk` → route → arrival tick) opens the board window;
//     N  neighbours (e.g. Wizard Chmella behind the Silaris board): from 8 sides the neighbour's silhouette is scanned with the
//        click ray (pixels where the neighbour is hit when the board is ignored); "eaten" = pixels where the click ray hits the
//        board's click capsule instead. NEW eaten ≤ OLD eaten, and the neighbour is clickable from every side it is visible from.
//   Lines «BOARD-ZONE ok/FAIL», summary «BOARD-ZONE ИТОГ: ok=… fail=…». `shot` — frames OLD/NEW with the ring into
//   `Reports/bulletin_board_zone/`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Npc/AsdaNpcActor.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "AsdaNetSubsystem.h"                   // DebugFeedPacket — 4067 through the real receiver
#include "AsdaBytes.h"                          // 4067 record fields
#include "AsdaCVarSave.h"                       // ArmWorldGuard
#include "AsdaFxNames.h"                        // AsdaCharFxTags::kTargetRing
#include "Quest/AsdaBoardQuestSubsystem.h"      // class 5 opens the board window
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaGroundTrace.h"              // ground height for the stand points
#include "World/AsdaCoord.h"                    // UeToGame2D
#include "World/AsdaWalkGrid.h"                 // stand points only on walkable cells
#include "Test/AsdaSelfTestKit.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UnrealClient.h"                       // FScreenshotRequest

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBoardZone, Log, All);

namespace AsdaBoardZoneTest
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.BoardZone");   // command
	static const TCHAR* const kPrefix = TEXT("BOARD-ZONE");              // log prefix
	static const TCHAR* const kKey = TEXT("asda.Npc.BoardZoneScale");   // the key under test
	static const TCHAR* const kGeoKey = TEXT("asda.Npc.StaticZoneFromGeometry");   // the second key (non-board static NPCs)
	static bool IsStaticClass(int32 Cls) { return Cls == 1 || Cls == 3 || Cls == 4 || Cls == 5; }
	static constexpr float kStand = 400.f;                              // stand distance, world units
	static constexpr float kNear = 800.f;                               // neighbour radius around a board
	static constexpr int32 kRidBase = 8300;                             // renderIds of the test adoptions
	static constexpr float kFaceZ = 200.f;                              // height of the board face middle over its root

	struct FCtx
	{
		TWeakObjectPtr<UWorld> W;                          // run world
		bool bQuit = false;                                // quit at the end
		bool bShot = false;                                // frames into the report folder
		int32 MapId = -1;                                  // map of the run
		int32 Ok = 0, Fail = 0;                            // counters
		float NewScale = 0.5f;                             // key value at start (restored at the end)
		int32 NewGeo = 1;                                  // `asda.Npc.StaticZoneFromGeometry` at start (restored at the end)
		bool bAll = false;                                 // `all`: every static NPC (classes 1/3/4/5), not only boards
		TArray<TWeakObjectPtr<AAsdaNpcActor>> Boards;      // targets: class 5 NPCs (or all static ones with `all`)
		TArray<TWeakObjectPtr<AAsdaNpcActor>> Neighbours;  // other NPCs within kNear of a board
		int32 Next = 0;                                    // next board
		FVector2D StartGame = FVector2D::ZeroVector;       // own start point

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaBoardZone, Log, TEXT("%s ok   %s"), kPrefix, *What); }
			else { ++Fail; UE_LOG(LogAsdaBoardZone, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		void Info(const FString& What) const { UE_LOG(LogAsdaBoardZone, Log, TEXT("%s info %s"), kPrefix, *What); }
		template <class T> T* Sub() const { return W.IsValid() && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<T>() : nullptr; }
		AAsdaPlayerController* Pc() const { return W.IsValid() ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = Pc(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// S == 1 → OLD zones (board scale 1 and `asda.Npc.StaticZoneFromGeometry 0`), otherwise NEW (both keys as at start).
	//   Rebuilds the click capsules of all targets (the ring and the talk box read the keys on use).
	static void SetScale(const FCtxRef& C, float S)
	{
		const bool bOld = (S == 1.f);
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(kKey)) { V->Set(bOld ? 1.f : C->NewScale, ECVF_SetByConsole); }
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(kGeoKey)) { V->Set(bOld ? 0 : C->NewGeo, ECVF_SetByConsole); }
		for (const TWeakObjectPtr<AAsdaNpcActor>& B : C->Boards) { if (B.IsValid()) { B->ApplyStaticPickBox(); } }
	}

	// Stand point at `Dist` from `O` in direction i (0..7, 45° steps from `Yaw0`), on the ground and on a walkable cell.
	static bool StandAt(const FCtxRef& C, const FVector& O, float Yaw0, int32 i, float Dist, FVector& Out, float& LookYaw)
	{
		UWorld* W = C->W.Get();
		const float Yaw = Yaw0 + 45.f * i;
		const FVector P = O + FRotator(0.f, Yaw, 0.f).Vector() * Dist;
		const float G = AsdaGround::GroundZ(W, (float)P.X, (float)P.Y, NAN);
		if (FMath::IsNaN(G)) { return false; }
		const AAsdaPlayerCharacter* Ch = C->Ch();
		if (Ch && Ch->WalkGrid)
		{
			const FVector2D Gm = AsdaCoord::UeToGame2D(P);
			if (!Ch->WalkGrid->IsWorldWalkable((float)Gm.X, (float)Gm.Y)) { return false; }
		}
		Out = FVector(P.X, P.Y, G);
		LookYaw = Yaw + 180.f;
		return true;
	}

	// Own character at Stand, the camera at the in-game default (arm 700, pitch −20) looking along LookYaw; view refreshed.
	static void Place(const FCtxRef& C, const FVector& Stand, float LookYaw)
	{
		AAsdaPlayerCharacter* Ch = C->Ch();
		AAsdaPlayerController* P = C->Pc();
		if (!Ch || !P) { return; }
		Ch->StopMove();
		Ch->SetActorLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
		P->SetOrbitAnglesForBench(FRotator::NormalizeAxis(LookYaw - (float)Ch->GetActorRotation().Yaw), -20.f);
		if (Ch->CameraArm) { Ch->CameraArm->TargetArmLength = 700.f; }
		if (P->PlayerCameraManager) { P->PlayerCameraManager->UpdateCamera(0.f); }
	}

	// The click ray at a world point: what it hits (actor + component).
	static bool RayAt(const FCtxRef& C, const FVector& World, FHitResult& Out)
	{
		AAsdaPlayerController* P = C->Pc();
		FVector2D S;
		if (!P || !P->ProjectWorldLocationToScreen(World, S)) { return false; }
		int32 VX = 0, VY = 0; P->GetViewportSize(VX, VY);
		if (S.X < 2 || S.Y < 2 || S.X > VX - 2 || S.Y > VY - 2) { return false; }
		return P->TraceScreenSkippingUnrenderedChars(S, Out);
	}
	static FString HitDesc(const FHitResult& H)
	{
		return FString::Printf(TEXT("%s/%s"), H.GetActor() ? *H.GetActor()->GetName() : TEXT("-"), H.GetComponent() ? *H.GetComponent()->GetName() : TEXT("-"));
	}

	static void CloseTalk(const FCtxRef& C)
	{
		if (UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>()) { if (D->IsDialogOpen()) { D->Close(); } }
		if (UAsdaBoardQuestSubsystem* B = C->Sub<UAsdaBoardQuestSubsystem>()) { if (B->IsOpen()) { B->Close(); } }
		if (AAsdaPlayerController* P = C->Pc()) { P->ClearTarget(); }
	}
	static bool BoardOpened(const FCtxRef& C, const AAsdaNpcActor* A)
	{
		const UAsdaBoardQuestSubsystem* B = C->Sub<UAsdaBoardQuestSubsystem>();
		const UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>();
		return (B && B->IsOpen() && B->IsBoardNpc(A->NpcId)) || (D && D->IsDialogOpen() && D->GetCurrentNpcId() == A->NpcId);
	}

	// World scale of the target ring on the actor (0 — no ring).
	static float RingScale(const AActor* A)
	{
		TArray<AActor*> Att;
		A->GetAttachedActors(Att);
		for (const AActor* X : Att) { if (X && X->ActorHasTag(FName(AsdaCharFxTags::kTargetRing))) { return (float)X->GetActorScale3D().X; } }
		return 0.f;
	}

	struct FZone { FBox Sel = FBox(ForceInit), Talk = FBox(ForceInit); float R = 0.f, HH = 0.f; float Ring = 0.f; };
	static FZone Measure(const FCtxRef& C, AAsdaNpcActor* A)
	{
		FZone Z;
		A->GetSelectBoxWorld(Z.Sel);
		A->GetTalkBoxWorld(Z.Talk);
		if (A->PickHitbox) { Z.R = A->PickHitbox->GetScaledCapsuleRadius(); Z.HH = A->PickHitbox->GetScaledCapsuleHalfHeight(); }
		if (AAsdaPlayerController* P = C->Pc())
		{
			P->ClearTarget();
			P->SetTarget(A);   // the click path puts the ring
			Z.Ring = RingScale(A);
			P->ClearTarget();
		}
		return Z;
	}
	static FString BoxDesc(const FBox& B)
	{
		const FVector E = B.GetExtent(), O = B.GetCenter();
		return FString::Printf(TEXT("centre (%.0f,%.0f,%.0f) half (%.0f,%.0f,%.0f)"), O.X, O.Y, O.Z, E.X, E.Y, E.Z);
	}

	static void Shot(const FCtxRef& C, const AAsdaNpcActor* A, const TCHAR* Suffix)
	{
		if (!C->bShot || !C->W.IsValid()) { return; }
		const FString Map = C->W->GetMapName().Replace(TEXT("UEDPIE_0_"), TEXT(""));
		const FString Name = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / FString::Printf(
			TEXT("../Reports/bulletin_board_zone/shot_%s_npc%d_%s.png"), *Map, A->NpcId, Suffix));
		FScreenshotRequest::RequestScreenshot(Name, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		C->Info(FString::Printf(TEXT("frame %s"), *Name));
	}

	static void Finish(const FCtxRef& C)
	{
		SetScale(C, C->NewScale);
		CloseTalk(C);
		if (AAsdaPlayerCharacter* Ch = C->Ch()) { Ch->StopMove(); Ch->NetSnap(C->StartGame); }
		const FString Tail = FString::Printf(TEXT(" map=%d boards=%d neighbours=%d scale=%.2f"), C->MapId, C->Boards.Num(), C->Neighbours.Num(), C->NewScale);
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaBoardZone, kPrefix, C->Ok, C->Fail, *Tail);
	}

	static void NextBoard(const FCtxRef& C);

	// ── N: neighbours' silhouettes vs the board capsule, OLD and NEW ──
	struct FEat { int32 Visible = 0, Eaten = 0, EatenMesh = 0, Hidden = 0, Sides = 0, SidesClickable = 0; FString PerSide; };

	// The click ray towards a neighbour pixel passes through the board's real geometry (asset bounds, oriented with the mesh)
	//   before reaching the neighbour — then the neighbour is visually hidden by the board there and the board wins fairly.
	static bool BehindBoardGeometry(const AAsdaPlayerController* P, const AAsdaNpcActor* Board, const FVector2D& Px, const FVector& NeighbourHit)
	{
		const USkeletalMesh* SK = Board->Mesh ? Board->Mesh->GetSkeletalMeshAsset() : nullptr;
		FVector O, D;
		if (!SK || !P->DeprojectScreenPositionToWorld(Px.X, Px.Y, O, D)) { return false; }
		const FTransform& T = Board->Mesh->GetComponentTransform();
		const FVector A = T.InverseTransformPosition(O), B = T.InverseTransformPosition(NeighbourHit);
		return FMath::LineBoxIntersection(SK->GetBounds().GetBox(), A, B, B - A);
	}
	static FEat ScanNeighbour(const FCtxRef& C, AAsdaNpcActor* Board, AAsdaNpcActor* N)
	{
		FEat E;
		AAsdaPlayerController* P = C->Pc();
		if (!P || !N || !N->Mesh) { return E; }
		int32 VX = 0, VY = 0; P->GetViewportSize(VX, VY);
		for (int32 i = 0; i < 8; ++i)
		{
			FVector St; float Yaw = 0.f;
			if (!StandAt(C, N->GetActorLocation(), 0.f, i, kStand, St, Yaw)) { continue; }
			Place(C, St, Yaw);
			// Neighbour's screen rectangle from its body bounds.
			const FBox NB = N->Mesh->Bounds.GetBox();
			FVector2D Mn(1e9, 1e9), Mx(-1e9, -1e9);
			bool bAny = false;
			for (int32 k = 0; k < 8; ++k)
			{
				const FVector Cn((k & 1) ? NB.Max.X : NB.Min.X, (k & 2) ? NB.Max.Y : NB.Min.Y, (k & 4) ? NB.Max.Z : NB.Min.Z);
				FVector2D S;
				if (P->ProjectWorldLocationToScreen(Cn, S)) { Mn = FVector2D::Min(Mn, S); Mx = FVector2D::Max(Mx, S); bAny = true; }
			}
			if (!bAny) { continue; }
			int32 Vis = 0, Hit = 0;
			for (double y = FMath::Max(2.0, Mn.Y); y <= FMath::Min((double)VY - 2, Mx.Y); y += 4.0)
			{
				for (double x = FMath::Max(2.0, Mn.X); x <= FMath::Min((double)VX - 2, Mx.X); x += 4.0)
				{
					// Silhouette pixel: the ray hits the neighbour when the board is not in the scene.
					FCollisionQueryParams Q(SCENE_QUERY_STAT(AsdaBoardZoneScan), true);
					Q.AddIgnoredActor(Board);
					FHitResult H0;
					if (!P->GetHitResultAtScreenPosition(FVector2D(x, y), ECC_Visibility, Q, H0) || H0.GetActor() != N) { continue; }
					if (BehindBoardGeometry(P, Board, FVector2D(x, y), H0.ImpactPoint)) { ++E.Hidden; continue; }   // board really in front
					++Vis;
					FHitResult H;
					if (!P->TraceScreenSkippingUnrenderedChars(FVector2D(x, y), H)) { continue; }
					if (H.GetActor() == N) { ++Hit; }
					else if (H.GetActor() == Board)
					{
						if (H.GetComponent() == Board->PickHitbox) { ++E.Eaten; } else { ++E.EatenMesh; }
					}
				}
			}
			if (Vis > 0) { ++E.Sides; if (Hit > 0) { ++E.SidesClickable; } }
			E.PerSide += FString::Printf(TEXT(" [%d: %d/%d]"), i, Hit, Vis);
			E.Visible += Vis;
		}
		return E;
	}

	static void RunNeighbours(const FCtxRef& C, AAsdaNpcActor* Board, const FString& Tag)
	{
		for (const TWeakObjectPtr<AAsdaNpcActor>& WN : C->Neighbours)
		{
			AAsdaNpcActor* N = WN.Get();
			if (!N || N == Board || FVector::Dist2D(N->GetActorLocation(), Board->GetActorLocation()) > kNear) { continue; }
			SetScale(C, 1.f);
			const FEat O = ScanNeighbour(C, Board, N);
			SetScale(C, C->NewScale);
			const FEat Nw = ScanNeighbour(C, Board, N);
			const FString NTag = FString::Printf(TEXT("NPC %d «%s» %.0f from board %d"), N->NpcId, *N->NpcName.Left(24),
				FVector::Dist2D(N->GetActorLocation(), Board->GetActorLocation()), Board->NpcId);
			C->Info(FString::Printf(TEXT("N %s: OLD open silhouette px %d (hidden behind the board model %d), eaten by the board capsule %d, by the board mesh %d, ")
				TEXT("clickable sides %d/%d, per side [side: hit/open]%s; NEW open px %d, eaten capsule %d, mesh %d, clickable sides %d/%d,%s"),
				*NTag, O.Visible, O.Hidden, O.Eaten, O.EatenMesh, O.SidesClickable, O.Sides, *O.PerSide,
				Nw.Visible, Nw.Eaten, Nw.EatenMesh, Nw.SidesClickable, Nw.Sides, *Nw.PerSide));
			C->Expect(Nw.Eaten <= O.Eaten, FString::Printf(TEXT("N %s: pixels eaten by the board capsule NEW %d ≤ OLD %d"), *NTag, Nw.Eaten, O.Eaten));
			C->Expect(Nw.Sides == 0 || Nw.SidesClickable == Nw.Sides,
				FString::Printf(TEXT("N %s: clickable from every side it is visible from (%d/%d)"), *NTag, Nw.SidesClickable, Nw.Sides));
		}
	}

	// ── T: run-in from every walkable side ──
	static void RunIn(const FCtxRef& C, TWeakObjectPtr<AAsdaNpcActor> WA, int32 Dir, TSharedRef<int32> Tried, TSharedRef<int32> Opened,
		TSharedRef<FString> Fails, FString Tag);
	static void WaitRunIn(const FCtxRef& C, TWeakObjectPtr<AAsdaNpcActor> WA, int32 Dir, TSharedRef<int32> Tried, TSharedRef<int32> Opened,
		TSharedRef<FString> Fails, FString Tag, double T0)
	{
		AAsdaNpcActor* A = WA.Get();
		AAsdaPlayerController* P = C->Pc();
		if (!A || !P) { ++C->Next; NextBoard(C); return; }
		const bool bOpen = BoardOpened(C, A);
		const double Dt = FPlatformTime::Seconds() - T0;
		if (!bOpen && (P->IsNpcTalkPending() || P->IsMovingNow()) && Dt < 8.0)
		{
			At(C, 0.25f, [=]() { WaitRunIn(C, WA, Dir, Tried, Opened, Fails, Tag, T0); });
			return;
		}
		const AAsdaPlayerCharacter* Ch = C->Ch();
		const float Stop = Ch ? (float)FVector::Dist2D(Ch->GetActorLocation(), A->GetActorLocation()) : -1.f;
		if (bOpen) { ++*Opened; C->Info(FString::Printf(TEXT("T run-in %s side %d: opened in %.1f s, stopped %.0f from the board"), *Tag, Dir, Dt, Stop)); }
		else { *Fails += FString::Printf(TEXT(" [side %d: %.1f s, stopped %.0f, pending %d]"), Dir, Dt, Stop, P->IsNpcTalkPending() ? 1 : 0); }
		if (AAsdaPlayerCharacter* Ch2 = C->Ch()) { Ch2->StopMove(); }
		P->ClearPendingNpcTalk(TEXT("selftest: next side"));
		CloseTalk(C);
		RunIn(C, WA, Dir + 1, Tried, Opened, Fails, Tag);
	}
	static void RunIn(const FCtxRef& C, TWeakObjectPtr<AAsdaNpcActor> WA, int32 Dir, TSharedRef<int32> Tried, TSharedRef<int32> Opened,
		TSharedRef<FString> Fails, FString Tag)
	{
		AAsdaNpcActor* A = WA.Get();
		if (!A) { ++C->Next; NextBoard(C); return; }
		FVector St; float Yaw = 0.f;
		while (Dir < 8 && !StandAt(C, A->GetActorLocation(), A->GetActorRotation().Yaw, Dir, kStand, St, Yaw)) { ++Dir; }
		if (Dir >= 8)
		{
			C->Expect(*Tried > 0 && *Opened == *Tried, FString::Printf(TEXT("T %s: run-in from %.0f units opened the board window from %d of %d walkable sides%s"),
				*Tag, kStand, *Opened, *Tried, Fails->IsEmpty() ? TEXT("") : **Fails));
			RunNeighbours(C, A, Tag);
			++C->Next;
			At(C, 0.2f, [C]() { NextBoard(C); });
			return;
		}
		++*Tried;
		Place(C, St, Yaw);
		At(C, 0.2f, [=]()
		{
			AAsdaNpcActor* A2 = WA.Get();
			AAsdaPlayerController* P2 = C->Pc();
			if (!A2 || !P2) { ++C->Next; NextBoard(C); return; }
			P2->StartNpcTalk(A2);
			WaitRunIn(C, WA, Dir, Tried, Opened, Fails, Tag, FPlatformTime::Seconds());
		});
	}

	static void RunBoard(const FCtxRef& C, AAsdaNpcActor* A)
	{
		AAsdaPlayerController* P = C->Pc();
		const FString Tag = FString::Printf(TEXT("board NPC %d «%s» cell (%d,%d)"), A->NpcId, *A->NpcName.Left(28), A->TileX, A->TileZ);
		const float S = C->NewScale;
		C->Expect(A->GetZoneScaleXY() == S, FString::Printf(TEXT("M %s: class %d, zone scale %.2f (key %.2f)"), *Tag, A->GetEntityClass(), A->GetZoneScaleXY(), S));

		// M: why the old zone was so big — skinned component bounds vs asset geometry vs physics asset.
		{
			const FTransform Tm = A->Mesh->GetComponentTransform();
			const USkeletalMesh* SK = A->Mesh->GetSkeletalMeshAsset();
			const FBox Geo = SK ? SK->GetBounds().GetBox().TransformBy(Tm) : FBox(ForceInit);
			const UPhysicsAsset* PA = A->Mesh->GetPhysicsAsset();
			const FBox Phys = PA ? PA->CalcAABB(A->Mesh, Tm) : FBox(ForceInit);
			FBox Bones; A->GetPickBoxWorld(Bones);
			C->Info(FString::Printf(TEXT("M %s: component bounds %s | asset geometry %s | physics asset %s | bones %s | component collision %d"),
				*Tag, *BoxDesc(A->Mesh->Bounds.GetBox()), Geo.IsValid ? *BoxDesc(Geo) : TEXT("-"), Phys.IsValid ? *BoxDesc(Phys) : TEXT("-"),
				*BoxDesc(Bones), (int32)A->Mesh->GetCollisionEnabled()));
		}
		SetScale(C, 1.f);
		const FZone O = Measure(C, A);
		SetScale(C, S);
		const FZone N = Measure(C, A);
		C->Info(FString::Printf(TEXT("M %s OLD: select %s, capsule R %.0f H/2 %.0f, ring ×%.2f (R≈%.0f), talk %s"),
			*Tag, *BoxDesc(O.Sel), O.R, O.HH, O.Ring, O.Ring * 50.f, *BoxDesc(O.Talk)));
		C->Info(FString::Printf(TEXT("M %s NEW: select %s, capsule R %.0f H/2 %.0f, ring ×%.2f (R≈%.0f), talk %s"),
			*Tag, *BoxDesc(N.Sel), N.R, N.HH, N.Ring, N.Ring * 50.f, *BoxDesc(N.Talk)));
		auto Near = [](double a, double b, double tol) { return FMath::Abs(a - b) <= tol; };
		C->Expect(Near(N.R, O.R * S, 1.0) && Near(N.Sel.GetCenter().X, O.Sel.GetCenter().X, 0.5) && Near(N.Sel.GetCenter().Y, O.Sel.GetCenter().Y, 0.5),
			FString::Printf(TEXT("M %s: click capsule radius %.0f → %.0f (×%.2f), same centre in plan"), *Tag, O.R, N.R, S));
		{
			// Click column: ends at the model top now (was +npc_list height above the bones), the whole model still inside.
			const USkeletalMesh* SK = A->Mesh->GetSkeletalMeshAsset();
			const FBox Geo = SK ? SK->GetBounds().GetBox().TransformBy(A->Mesh->GetComponentTransform()) : FBox(ForceInit);
			const float Top = A->PickHitbox ? (float)(A->PickHitbox->GetComponentLocation().Z + A->PickHitbox->GetScaledCapsuleHalfHeight()) : 0.f;
			const float Bot = A->PickHitbox ? (float)(A->PickHitbox->GetComponentLocation().Z - A->PickHitbox->GetScaledCapsuleHalfHeight()) : 0.f;
			const float OldTop = (float)(O.Sel.GetCenter().Z + O.HH);
			C->Expect(Geo.IsValid && Top >= Geo.Max.Z - 1.f && Bot <= Geo.Min.Z + 1.f && Top < OldTop,
				FString::Printf(TEXT("M %s: click column top %.0f over the root (was %.0f), model geometry %.0f…%.0f inside it"), *Tag,
					Top - A->GetActorLocation().Z, OldTop - A->GetActorLocation().Z, Geo.Min.Z - A->GetActorLocation().Z, Geo.Max.Z - A->GetActorLocation().Z));
		}
		C->Expect(O.Ring > 0.f && Near(N.Ring, FMath::Max(1.f, O.Ring * S), 0.02),
			FString::Printf(TEXT("R %s: selection ring scale %.2f → %.2f (radius ≈%.0f → ≈%.0f)"), *Tag, O.Ring, N.Ring, O.Ring * 50.f, N.Ring * 50.f));
		C->Expect(Near(N.Talk.GetExtent().X, O.Talk.GetExtent().X * S, 0.5) && Near(N.Talk.GetExtent().Y, O.Talk.GetExtent().Y * S, 0.5)
			&& Near(N.Talk.GetExtent().Z, O.Talk.GetExtent().Z, 0.5),
			FString::Printf(TEXT("T %s: talk zone half (%.0f,%.0f) → (%.0f,%.0f), height kept %.0f"), *Tag,
				O.Talk.GetExtent().X, O.Talk.GetExtent().Y, N.Talk.GetExtent().X, N.Talk.GetExtent().Y, N.Talk.GetExtent().Z * 2));

		// C: click ray from every walkable side.
		const FVector Ctr(N.Sel.GetCenter().X, N.Sel.GetCenter().Y, A->GetActorLocation().Z + kFaceZ);
		int32 Sides = 0, Good = 0;
		FString Bad;
		bool bShotDone = false;      // first fully passed side remembered for the frames
		FVector ShotStand = FVector::ZeroVector;
		float ShotYaw = 0.f;
		for (int32 i = 0; i < 8; ++i)
		{
			FVector St; float Yaw = 0.f;
			if (!StandAt(C, A->GetActorLocation(), A->GetActorRotation().Yaw, i, kStand, St, Yaw)) { continue; }
			Place(C, St, Yaw);
			FHitResult H0;
			if (!RayAt(C, Ctr, H0) || H0.GetActor() != A) { Bad += FString::Printf(TEXT(" [side %d: centre covered by %s]"), i, *HitDesc(H0)); continue; }
			++Sides;
			const FVector Right = P->PlayerCameraManager ? FRotator(0.f, P->PlayerCameraManager->GetCameraRotation().Yaw, 0.f).Quaternion().GetRightVector() : FVector::RightVector;
			bool bSide = true;
			for (const float Sg : { -1.f, 1.f })
			{
				FHitResult Hin, Hout, Hold;
				const FVector In = Ctr + Right * (Sg * 0.8f * N.R);
				const FVector Out = Ctr + Right * (Sg * 1.3f * N.R);
				const bool bIn = RayAt(C, In, Hin) && Hin.GetActor() == A;
				const bool bOutNew = RayAt(C, Out, Hout) && Hout.GetActor() == A;
				SetScale(C, 1.f);
				const bool bOutOld = RayAt(C, Out, Hold) && Hold.GetActor() == A;
				SetScale(C, S);
				if (!bIn || bOutNew) { bSide = false; }
				C->Info(FString::Printf(TEXT("C %s side %d %s: 0.8R → %s (%s); 1.3R → %s (%s), OLD → %s"), *Tag, i, Sg < 0 ? TEXT("left") : TEXT("right"),
					bIn ? TEXT("board") : TEXT("miss"), *HitDesc(Hin), bOutNew ? TEXT("board") : TEXT("miss"), *HitDesc(Hout), bOutOld ? TEXT("board") : TEXT("miss")));
			}
			if (bSide) { ++Good; if (!bShotDone) { bShotDone = true; ShotStand = St; ShotYaw = Yaw; } }
			else { Bad += FString::Printf(TEXT(" [side %d: edge test failed]"), i); }
		}
		C->Expect(Sides > 0 && Good == Sides, FString::Printf(TEXT("C %s: click inside the new edge → board, outside → not the board, from %d of %d open sides%s"),
			*Tag, Good, Sides, Bad.IsEmpty() ? TEXT("") : *Bad));

		const TWeakObjectPtr<AAsdaNpcActor> WA(A);
		auto TalkPart = [C, WA, N, O, S, Tag]()
		{
			AAsdaNpcActor* A2 = WA.Get();
			AAsdaPlayerController* P2 = C->Pc();
			if (!A2 || !P2) { ++C->Next; NextBoard(C); return; }
			CloseTalk(C);
			// T: inside / outside the new talk box along the board facing.
			const FVector Fw = FRotator(0.f, A2->GetActorRotation().Yaw, 0.f).Vector();
			const FVector TC = N.Talk.GetCenter();
			const float HalfAlong = (float)(FMath::Abs(Fw.X) * N.Talk.GetExtent().X + FMath::Abs(Fw.Y) * N.Talk.GetExtent().Y);
			auto Ground = [C, A2](FVector V) { const float G = AsdaGround::GroundZ(C->W.Get(), (float)V.X, (float)V.Y, NAN); V.Z = FMath::IsNaN(G) ? A2->GetActorLocation().Z : G; return V; };
			const FVector In = Ground(FVector(TC.X, TC.Y, 0) + Fw * (0.85f * HalfAlong));
			const FVector Out = Ground(FVector(TC.X, TC.Y, 0) + Fw * (1.25f * HalfAlong));
			Place(C, In, A2->GetActorRotation().Yaw + 180.f);
			const bool bIn = P2->IsInsideNpcTalkZone(A2);
			P2->StartNpcTalk(A2);
			const bool bOpen = BoardOpened(C, A2);
			C->Expect(bIn && bOpen, FString::Printf(TEXT("T %s: at %.0f from the zone centre (0.85 of the new half %.0f) inside %d, board window opened %d"),
				*Tag, 0.85f * HalfAlong, HalfAlong, bIn ? 1 : 0, bOpen ? 1 : 0));
			CloseTalk(C);
			const bool bOutNew = P2->IsPointInNpcTalkZone(A2, Out);
			SetScale(C, 1.f);
			const bool bOutOld = P2->IsPointInNpcTalkZone(A2, Out);
			SetScale(C, S);
			C->Expect(!bOutNew && bOutOld, FString::Printf(TEXT("T %s: at %.0f from the zone centre (1.25 of the new half) — outside now %d, inside before %d"),
				*Tag, 1.25f * HalfAlong, bOutNew ? 0 : 1, bOutOld ? 1 : 0));
			RunIn(C, WA, 0, MakeShared<int32>(0), MakeShared<int32>(0), MakeShared<FString>(), Tag);
		};
		if (C->bShot && bShotDone)
		{
			// Frames from one side: ring on the board with the NEW zone, then with the OLD one (the ring stays while the target is set).
			Place(C, ShotStand, ShotYaw);
			P->ClearTarget();
			P->SetTarget(A);
			At(C, 1.0f, [C, WA, TalkPart]()
			{
				AAsdaNpcActor* A2 = WA.Get();
				AAsdaPlayerController* P2 = C->Pc();
				if (!A2 || !P2) { ++C->Next; NextBoard(C); return; }
				Shot(C, A2, TEXT("new"));
				At(C, 1.0f, [C, WA, TalkPart]()
				{
					AAsdaNpcActor* A3 = WA.Get();
					AAsdaPlayerController* P3 = C->Pc();
					if (!A3 || !P3) { ++C->Next; NextBoard(C); return; }
					P3->ClearTarget();
					SetScale(C, 1.f);
					P3->SetTarget(A3);
					At(C, 1.0f, [C, WA, TalkPart]()
					{
						if (AAsdaNpcActor* A4 = WA.Get()) { Shot(C, A4, TEXT("old")); }
						At(C, 1.0f, [C, TalkPart]() { SetScale(C, C->NewScale); if (AAsdaPlayerController* P5 = C->Pc()) { P5->ClearTarget(); } TalkPart(); });
					});
				});
			});
			return;
		}
		TalkPart();
	}

	// ── Non-board static NPC (classes 1/3/4, `all` mode): click capsule and ring from the model geometry (OLD = physics-asset bounds
	//   and the npc_list height rise). Talk box must be unchanged (the original's bones +200); centre click works from open sides.
	static void RunStatic(const FCtxRef& C, AAsdaNpcActor* A)
	{
		const FString Tag = FString::Printf(TEXT("static NPC %d «%s» class %d model %s"), A->NpcId, *A->NpcName.Left(24), A->GetEntityClass(),
			A->Mesh && A->Mesh->GetSkeletalMeshAsset() ? *A->Mesh->GetSkeletalMeshAsset()->GetName() : TEXT("-"));
		SetScale(C, 1.f);
		const FZone O = Measure(C, A);
		const float OTop = A->PickHitbox ? (float)(A->PickHitbox->GetComponentLocation().Z + A->PickHitbox->GetScaledCapsuleHalfHeight()) : 0.f;
		SetScale(C, 0.f);
		const FZone N = Measure(C, A);
		const float NTop = A->PickHitbox ? (float)(A->PickHitbox->GetComponentLocation().Z + A->PickHitbox->GetScaledCapsuleHalfHeight()) : 0.f;
		const float Z0 = (float)A->GetActorLocation().Z;
		const USkeletalMesh* SK = A->Mesh ? A->Mesh->GetSkeletalMeshAsset() : nullptr;
		const FBox Geo = SK ? SK->GetBounds().GetBox().TransformBy(A->Mesh->GetComponentTransform()) : FBox(ForceInit);
		const bool bHasBox = O.Sel.IsValid && N.Sel.IsValid;
		C->Info(FString::Printf(TEXT("M %s: OLD capsule R %.0f top %.0f ring ×%.2f | NEW capsule R %.0f top %.0f ring ×%.2f | geometry half (%.0f,%.0f) top %.0f | talk OLD %s NEW %s"),
			*Tag, O.R, OTop - Z0, O.Ring, N.R, NTop - Z0, N.Ring, Geo.IsValid ? Geo.GetExtent().X : 0.0, Geo.IsValid ? Geo.GetExtent().Y : 0.0,
			Geo.IsValid ? Geo.Max.Z - Z0 : 0.0, *BoxDesc(O.Talk), *BoxDesc(N.Talk)));
		if (!bHasBox)
		{
			C->Info(FString::Printf(TEXT("M %s: no interaction-box row for this model (generic NPC capsule, unchanged)"), *Tag));
		}
		else
		{
			// The column may rise only up to the model's real top: where the physics asset was shorter than the model (obelisks,
			//   colonies) the old column cut the model's top off, and covering it is the fix, not a bigger zone.
			const float TopCap = FMath::Max(OTop, Geo.IsValid ? (float)Geo.Max.Z : OTop);
			// Same for the width: where the physics asset was narrower than the model (statue 116, boat 123, well 149) the old capsule
			//   and ring did not cover the model; growing up to the model's own half-width is the fix.
			const float RCap = FMath::Max(O.R, Geo.IsValid ? (float)FMath::Max(Geo.GetExtent().X, Geo.GetExtent().Y) : O.R);
			C->Expect(N.R <= RCap + 1.f && NTop <= TopCap + 1.f && N.Ring <= FMath::Max(O.Ring, RCap / 50.f) + 0.02f,
				FString::Printf(TEXT("M %s: click capsule R %.0f → %.0f, column top %.0f → %.0f, ring ×%.2f → ×%.2f (never bigger than before or than the model)"),
					*Tag, O.R, N.R, OTop - Z0, NTop - Z0, O.Ring, N.Ring));
			C->Expect(Geo.IsValid && NTop >= Geo.Max.Z - 1.f && N.R + 1.f >= FMath::Min(Geo.GetExtent().X, Geo.GetExtent().Y),
				FString::Printf(TEXT("M %s: the model (top %.0f) stays inside the click column"), *Tag, Geo.IsValid ? Geo.Max.Z - Z0 : 0.0));
			C->Expect(O.Talk.Min.Equals(N.Talk.Min, 0.5) && O.Talk.Max.Equals(N.Talk.Max, 0.5), FString::Printf(TEXT("T %s: talk zone unchanged (original bones +200)"), *Tag));
		}
		// C: the model is clickable from the player's open sides, NEW not worse than OLD. Aim points: the model centre and a low point
		//   (huge trees/obelisks have the centre off-screen from a standing player); the own character is taken out of the ray
		//   (it stands between the camera and tiny models like colonies — a camera fact, not the zone).
		const FVector Ctr = Geo.IsValid ? Geo.GetCenter() : A->GetActorLocation() + FVector(0, 0, 80);
		const float GeoH = Geo.IsValid ? (float)(Geo.Max.Z - Z0) : 160.f;
		const FVector Low(Ctr.X, Ctr.Y, Z0 + FMath::Clamp(0.25f * GeoH, 20.f, 200.f));
		const float StandDist = kStand + (Geo.IsValid ? FMath::Max(Geo.GetExtent().X, Geo.GetExtent().Y) : 0.f);
		// A side counts as lost only when another entity's pick zone takes the ray (zone eating zone). Map scenery in front of the real
		//   model (tall grass over tombstone 72) is the model being covered there; the old oversized capsule just poked through it.
		int32 LostToZone = 0;
		auto CountSides = [&](int32& OutOpen, FString& OutBad) -> int32
		{
			int32 G = 0; OutOpen = 0; LostToZone = 0;
			for (int32 i = 0; i < 8; ++i)
			{
				FVector St; float Yaw = 0.f;
				if (!StandAt(C, A->GetActorLocation(), A->GetActorRotation().Yaw, i, StandDist, St, Yaw)) { continue; }
				Place(C, St, Yaw);
				++OutOpen;
				AActor* Me = C->Ch();
				const bool bMeCol = Me && Me->GetActorEnableCollision();
				if (Me) { Me->SetActorEnableCollision(false); }
				FHitResult H1, H2;
				const bool b1 = RayAt(C, Ctr, H1) && H1.GetActor() == A;
				const bool b2 = !b1 && RayAt(C, Low, H2) && H2.GetActor() == A;
				if (Me) { Me->SetActorEnableCollision(bMeCol); }
				if (b1 || b2) { ++G; }
				else if ((H1.GetActor() && Cast<UCapsuleComponent>(H1.GetComponent()) && H1.GetActor() != A)
					|| (H2.GetActor() && Cast<UCapsuleComponent>(H2.GetComponent()) && H2.GetActor() != A)) { ++LostToZone; }
				if (!(b1 || b2) && OutBad.Len() < 300) { OutBad += FString::Printf(TEXT(" [side %d: %s | %s]"), i, *HitDesc(H1), *HitDesc(H2)); }
			}
			return G;
		};
		int32 OpenOld = 0, OpenNew = 0;
		FString BadOld, BadNew;
		SetScale(C, 1.f);
		const int32 GoodOld = CountSides(OpenOld, BadOld);
		SetScale(C, 0.f);
		const int32 GoodNew = CountSides(OpenNew, BadNew);
		C->Expect(OpenNew == 0 || (GoodNew > 0 && (GoodNew >= GoodOld || LostToZone == 0)),
			FString::Printf(TEXT("C %s: model clicks the NPC from %d of %d walkable sides now, %d before; sides lost to another pick zone %d%s"),
				*Tag, GoodNew, OpenNew, GoodOld, LostToZone, *BadNew));
		RunNeighbours(C, A, Tag);
		CloseTalk(C);
		++C->Next;
		At(C, 0.2f, [C]() { NextBoard(C); });
	}

	static void NextBoard(const FCtxRef& C)
	{
		if (C->Next >= C->Boards.Num()) { Finish(C); return; }
		AAsdaNpcActor* A = C->Boards[C->Next].Get();
		if (!A) { ++C->Next; NextBoard(C); return; }
		if (A->GetEntityClass() == 5) { RunBoard(C, A); } else { RunStatic(C, A); }
	}

	static void Adopt(const FCtxRef& C, AAsdaNpcActor* A, int32 Idx)
	{
		if (!A || A->bNetSpawned) { return; }
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>())
		{
			TArray<uint8> Body; Body.SetNumZeroed(108);
			AsdaBytes::WrI16(Body.GetData() + 0, (int16)(kRidBase + Idx));
			AsdaBytes::WrI16(Body.GetData() + 2, (int16)A->NpcId);
			AsdaBytes::WrI16(Body.GetData() + 8, (int16)A->TileX);
			AsdaBytes::WrI16(Body.GetData() + 10, (int16)A->TileZ);
			Net->DebugFeedPacket(4067, Body);
		}
	}

	static void Run(const FCtxRef& C)
	{
		UWorld* W = C->W.Get();
		if (!W || !C->Pc() || !C->Ch()) { C->Expect(false, TEXT("world, controller and own character exist")); Finish(C); return; }
		C->StartGame = AsdaCoord::UeToGame2D(C->Ch()->GetActorLocation());
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(kKey)) { C->NewScale = V->GetFloat(); }
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(kGeoKey)) { C->NewGeo = V->GetInt(); }
		TArray<AAsdaNpcActor*> All;
		for (TActorIterator<AAsdaNpcActor> It(W); It; ++It)
		{
			AAsdaNpcActor* A = *It;
			if (!A || A->bDisabled || A->NpcId <= 0 || A->bNetSpawned) { continue; }
			All.Add(A);
			const int32 Cls = A->GetEntityClass();
			if (Cls == 5 || (C->bAll && IsStaticClass(Cls))) { C->Boards.Add(A); }
		}
		for (AAsdaNpcActor* A : All)
		{
			for (const TWeakObjectPtr<AAsdaNpcActor>& B : C->Boards)
			{
				if (B.IsValid() && B.Get() != A && FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()) <= kNear) { C->Neighbours.Add(A); break; }
			}
		}
		FString List;
		for (const TWeakObjectPtr<AAsdaNpcActor>& B : C->Boards) { List += FString::Printf(TEXT(" %d«%s»c%d"), B->NpcId, *B->NpcName.Left(28), B->GetEntityClass()); }
		for (const TWeakObjectPtr<AAsdaNpcActor>& B : C->Neighbours) { List += FString::Printf(TEXT(" n%d«%s»"), B->NpcId, *B->NpcName.Left(20)); }
		C->Expect(C->Boards.Num() > 0 || C->bAll, FString::Printf(TEXT("%s on map %d: %d, neighbours within %.0f: %d —%s"),
			C->bAll ? TEXT("static NPCs (classes 1/3/4/5)") : TEXT("bulletin boards (class 5)"), C->MapId, C->Boards.Num(), kNear, C->Neighbours.Num(), *List));
		int32 Idx = 0;
		for (const TWeakObjectPtr<AAsdaNpcActor>& B : C->Boards) { Adopt(C, B.Get(), Idx++); }
		for (const TWeakObjectPtr<AAsdaNpcActor>& B : C->Neighbours) { Adopt(C, B.Get(), Idx++); }
		At(C, 0.8f, [C]()
		{
			for (const TWeakObjectPtr<AAsdaNpcActor>& B : C->Boards)
			{
				if (B.IsValid()) { C->Expect(B->bNetSpawned && !B->IsHidden() && B->GetActorEnableCollision(), FString::Printf(TEXT("NPC %d (class %d) adopted by 4067, shown, collision"), B->NpcId, B->GetEntityClass())); }
			}
			NextBoard(C);
		});
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestBoardZone(
	AsdaBoardZoneTest::kCmd,
	TEXT("asda.SelfTest.BoardZone map=<id> [shot] [quit] — bulletin boards (class 5): click capsule, ring and talk zone ×asda.Npc.BoardZoneScale, ")
	TEXT("click at the new edge / outside, talk inside / outside / run-in, neighbours' clicks not eaten. Offline, on the baked level of `map`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaBoardZoneTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		int32 MapId = -1;
		for (const FString& A : Args) { if (A.StartsWith(TEXT("map="))) { MapId = FCString::Atoi(*A.Mid(4)); } }
		if (MapId < 0) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("map=<id> is required")); return; }
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, *FString::Printf(TEXT("Asda.Load.CurrentMap %d"), MapId));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s map=%d selfready%s%s"), kCmd, MapId, KA.Has(TEXT("shot")) ? TEXT(" shot") : (KA.Has(TEXT("all")) ? TEXT(" all") : TEXT("")), KA.bQuit ? TEXT(" quit") : TEXT(""));
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
		C->bShot = KA.Has(TEXT("shot"));
		C->bAll = KA.Has(TEXT("all"));
		C->MapId = MapId;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.5f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
