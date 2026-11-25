// Copyright (c) 2026 @sitxovski (Discord)
// ★★ SELFTEST: MOB CLIP BONES BOUND LIKE THE ORIGINAL — `asda.SelfTest.MobClipBones [shots=<dir>] [quit]`
//   (04.11.2026, follow-up of the NPC talk-animation bug; report `Reports/npc_talk_anim/REPORT.md`).
//
//   The original binds mesh bones by name once against clip `_0` and reads every other clip BY INDEX
//   (`mobEffect_LoadArchiveStep@0xF81B90` → `modelBuildMeshFromCache(…, modelDataPtr)` → `meshResolveBoneIndex@0x11A8C80`).
//   The converter mapped by name, so a bone renamed in a clip got no track and stood frozen in its bind pose (UE fills a missing track
//   with the reference pose); a skill clip of another rig that lacks a bone left it frozen too. Fixed in the builder
//   (`asda_skin_build.align_clip_bones_by_index` / `fill_missing_bones_from_parent`) and in the assets
//   (`Tools/asda_fix_npc_clip_bone_index.py`, root MobSkel).
//   Offline (`-AsdaNoNet`), world map: each case spawns the mob type through the network spawn point
//   (`UAsdaWorldSubsystem::SpawnNetMobDynamic`) on the own character's spot, plays clip 0 then the fixed clip file (loop, through the
//   blending instance), samples component-space positions of the bone and of its partner every frame (engine delta). FAIL when the
//   bone is frozen (moves < 0.5 world units over the clip) while its partner/body moves (> 3 units) — the signature of the defect;
//   with no partner (madgear wheel) the bone itself must travel ≥ 10 units (the data says ~295).
//   Frames `mobclip_<type>_<clip>_<n>.png` at 25/50/75 % of the clip. Summary «MOB-CLIPBONES ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Npc/AsdaMobActor.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaBlendAnimInstance.h"        // AsdaAnimSmooth::PlayClip
#include "Resources/AsdaModelAnimSubsystem.h"   // LoadClipChecked
#include "AsdaCVarSave.h"                       // ArmWorldGuard
#include "Test/AsdaSelfTestKit.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "GameFramework/SpringArmComponent.h"
#include "UnrealClient.h"                       // FScreenshotRequest
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMobClipBones, Log, All);

namespace AsdaMobClipBones
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.MobClipBones");   // command
	static const TCHAR* const kPrefix = TEXT("MOB-CLIPBONES");              // log prefix
	static constexpr float kFrozen = 0.5f;                                   // world units: a bone moving less than this is frozen
	static constexpr float kMoving = 3.f;                                    // world units: the partner/body really moves
	static constexpr float kAlone = 10.f;                                    // no partner: the bone itself must travel at least this

	// One case: mob type, clip FILE number, bone that was frozen before the fix, partner bone ("" — the body's root motion bone).
	struct FCase { int32 MobType; int32 ClipFile; const TCHAR* Bone; const TCHAR* Partner; const TCHAR* Why; };
	static const FCase kCases[] = {
		{ 87,  6,  TEXT("b2"),                   TEXT("object02"),     TEXT("kcat02: bone #2 renamed `object01` in clips 1/5–8 (index rule), clip 6") },
		{ 87,  7,  TEXT("b2"),                   TEXT("object02"),     TEXT("kcat02: same, clip 7") },
		{ 242, 4,  TEXT("mon_s_0_madgear-a_01"), TEXT(""),             TEXT("madgear: wheel bone renamed `wheel` in clip 4 (index rule; in the data the wheel travels ~295 units)") },
		{ 763, 11, TEXT("bip01-spine2"),         TEXT("bip01-spine1"), TEXT("omush05 (clips of omush02): skill clip 11 of another rig lacks the bone (follows parent)") },
		{ 763, 13, TEXT("bone10"),               TEXT("bip01-head"),   TEXT("omush05: skill clip 13 lacks `bone10` (follows parent)") },
		{ 57,  11, TEXT("bone52"),               TEXT("bip01-spine2"), TEXT("opara01: clip 11 of another rig lacks 17 bones (follows parent)") },
	};

	struct FCtx
	{
		TWeakObjectPtr<UWorld> W;                 // run world
		TWeakObjectPtr<UAsdaWorldSubsystem> WS;   // world subsystem (network spawn point)
		bool bQuit = false;                       // quit at the end
		FString ShotDir;                          // PNG folder ("" — none)
		int32 Ok = 0, Fail = 0;                   // counters
		int32 Next = 0;                           // next case
		TWeakObjectPtr<AAsdaMobActor> Mob;        // mob under test
		int32 BoneIdx = INDEX_NONE, PartIdx = INDEX_NONE;   // bone indices on the mesh
		bool bSampling = false;                   // per-frame sampling on
		FBox BoneBox = FBox(ForceInit), PartBox = FBox(ForceInit);   // ranges of positions (world units)
		int32 Frames = 0;                         // sampled frames
		FDelegateHandle Hook;                     // per-frame hook
		FVector2D StartGame = FVector2D::ZeroVector;

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaMobClipBones, Log, TEXT("%s ok   %s"), kPrefix, *What); }
			else { ++Fail; UE_LOG(LogAsdaMobClipBones, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		AAsdaPlayerCharacter* Ch() const
		{
			APlayerController* P = W.IsValid() ? W->GetFirstPlayerController() : nullptr;
			return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr;
		}
	};
	using FCtxRef = TSharedRef<FCtx>;

	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// UE lower-cases imported bone names and replaces spaces with '-': try the name as given and both spellings.
	static int32 FindBone(const USkeletalMeshComponent* M, const TCHAR* Name)
	{
		if (!M) { return INDEX_NONE; }
		const FString N(Name);
		for (const FString& V : { N, N.Replace(TEXT("-"), TEXT(" ")), N.Replace(TEXT(" "), TEXT("-")) })
		{
			const int32 I = M->GetBoneIndex(FName(*V));
			if (I != INDEX_NONE) { return I; }
		}
		return INDEX_NONE;
	}

	static void Sample(const FCtxRef& C)
	{
		AAsdaMobActor* M = C->Mob.Get();
		if (!C->bSampling || !M || !M->Mesh) { return; }
		const TArray<FTransform>& CS = M->Mesh->GetComponentSpaceTransforms();
		const float Scale = (float)M->Mesh->GetComponentScale().GetAbs().X;
		if (CS.IsValidIndex(C->BoneIdx)) { C->BoneBox += CS[C->BoneIdx].GetLocation() * Scale; }
		if (CS.IsValidIndex(C->PartIdx)) { C->PartBox += CS[C->PartIdx].GetLocation() * Scale; }
		++C->Frames;
	}

	static void Shot(const FCtxRef& C, const FCase& K, int32 N)
	{
		if (C->ShotDir.IsEmpty()) { return; }
		const FString F = FPaths::Combine(C->ShotDir, FString::Printf(TEXT("mobclip_%d_%02d_%d.png"), K.MobType, K.ClipFile, N));
		FScreenshotRequest::RequestScreenshot(F, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaMobClipBones, Log, TEXT("%s кадр %s"), kPrefix, *F);
	}

	static void Finish(const FCtxRef& C)
	{
		if (C->Hook.IsValid()) { FWorldDelegates::OnWorldPostActorTick.Remove(C->Hook); C->Hook.Reset(); }
		if (AAsdaMobActor* M = C->Mob.Get()) { M->Destroy(); }
		if (AAsdaPlayerCharacter* Ch = C->Ch()) { Ch->SetActorHiddenInGame(false); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaMobClipBones, kPrefix, C->Ok, C->Fail, TEXT(""));
	}

	static void NextCase(const FCtxRef& C);

	// Play clip file `File` looped on the mob through the blending instance (no blend: the test measures the clip itself).
	static UAnimSequence* PlayFile(AAsdaMobActor* M, int32 File)
	{
		if (!M || !M->Mesh || !M->Mesh->GetSkeletalMeshAsset()) { return nullptr; }
		FString Base = M->Mesh->GetSkeletalMeshAsset()->GetPathName();
		int32 Dot; if (Base.FindChar('.', Dot)) { Base = Base.Left(Dot); }
		UAnimSequence* A = UAsdaModelAnimSubsystem::LoadClipChecked(FString::Printf(TEXT("%sanim_%02d"), *Base, File));
		if (A) { AsdaAnimSmooth::PlayClip(M->Mesh, A, /*bLoop=*/true, /*бленд*/0.f); M->Mesh->SetPlayRate(1.f); }
		return A;
	}

	static void RunCase(const FCtxRef& C, const FCase& K)
	{
		AAsdaPlayerCharacter* Me = C->Ch();
		UAsdaWorldSubsystem* WS = C->WS.Get();
		AAsdaMobActor* M = (Me && WS) ? WS->SpawnNetMobDynamic(K.MobType, Me->GetActorTransform()) : nullptr;
		C->Mob = M;
		C->Expect(M != nullptr && M->Mesh && M->Mesh->GetSkeletalMeshAsset(), FString::Printf(TEXT("type %d spawned (%s)"), K.MobType,
			M && M->Mesh && M->Mesh->GetSkeletalMeshAsset() ? *M->Mesh->GetSkeletalMeshAsset()->GetName() : TEXT("-")));
		if (!M) { ++C->Next; At(C, 0.2f, [C]() { NextCase(C); }); return; }
		M->SetActorTickEnabled(false);   // the mob's own state machine must not switch the clip under the test
		if (Me->CameraArm) { Me->CameraArm->TargetArmLength = FMath::Clamp(M->Mesh->Bounds.SphereRadius * 2.6f, 200.f, 1600.f); }
		C->BoneIdx = FindBone(M->Mesh, K.Bone);
		C->PartIdx = *K.Partner ? FindBone(M->Mesh, K.Partner) : INDEX_NONE;
		C->Expect(C->BoneIdx != INDEX_NONE && (C->PartIdx != INDEX_NONE || !*K.Partner),
			FString::Printf(TEXT("type %d: bones «%s» (%d) and «%s» (%d) on the mesh"), K.MobType, K.Bone, C->BoneIdx, K.Partner, C->PartIdx));
		UAnimSequence* A = PlayFile(M, K.ClipFile);
		C->Expect(A != nullptr, FString::Printf(TEXT("type %d: clip file %d loaded"), K.MobType, K.ClipFile));
		if (!A || C->BoneIdx == INDEX_NONE) { M->Destroy(); ++C->Next; At(C, 0.2f, [C]() { NextCase(C); }); return; }
		const float Len = FMath::Clamp(A->GetPlayLength(), 0.5f, 8.f);
		C->BoneBox.Init(); C->PartBox.Init(); C->Frames = 0;
		At(C, 0.3f, [C]() { C->bSampling = true; });
		for (int32 i = 1; i <= 3; ++i) { At(C, 0.3f + Len * 0.25f * i, [C, K, i]() { Shot(C, K, i); }); }
		At(C, 0.3f + Len, [C, K, Len]()
		{
			C->bSampling = false;
			const float BoneMove = C->BoneBox.IsValid ? (float)C->BoneBox.GetSize().GetMax() : 0.f;
			const float PartMove = C->PartBox.IsValid ? (float)C->PartBox.GetSize().GetMax() : 0.f;
			const bool bFrozen = *K.Partner ? (BoneMove < kFrozen && PartMove > kMoving) : (BoneMove < kAlone);
			C->Expect(!bFrozen, FString::Printf(TEXT("type %d clip %d: bone «%s» moves %.1f u, partner «%s» %.1f u over %.2f s (%d frames) — %s; %s"),
				K.MobType, K.ClipFile, K.Bone, BoneMove, K.Partner, PartMove, Len, C->Frames, bFrozen ? TEXT("FROZEN (defect)") : TEXT("follows"), K.Why));
			if (AAsdaMobActor* M2 = C->Mob.Get()) { M2->Destroy(); }
			C->Mob.Reset();
			++C->Next;
			At(C, 0.3f, [C]() { NextCase(C); });
		});
	}

	static void NextCase(const FCtxRef& C)
	{
		if (C->Next >= UE_ARRAY_COUNT(kCases)) { Finish(C); return; }
		RunCase(C, kCases[C->Next]);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestMobClipBones(
	AsdaMobClipBones::kCmd,
	TEXT("asda.SelfTest.MobClipBones [shots=<dir>] [quit] — mob clips whose bones were frozen by name mapping (kcat02, madgear, omush05, opara01): ")
	TEXT("the bone must move with the body. Offline, world map."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaMobClipBones;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		FString Shots;
		for (const FString& A : Args) { if (A.StartsWith(TEXT("shots="))) { Shots = A.Mid(6); } }
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s%s selfready%s"), kCmd,
				Shots.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" shots=%s"), *Shots), KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!WS) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("no world subsystem")); return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->WS = WS;
		C->bQuit = KA.bQuit;
		if (!Shots.IsEmpty()) { C->ShotDir = FPaths::ConvertRelativePathToFull(Shots); IFileManager::Get().MakeDirectory(*C->ShotDir, true); }
		if (AAsdaPlayerCharacter* Ch = C->Ch()) { Ch->SetActorHiddenInGame(true); }
		TWeakPtr<FCtx> WC = C;
		C->Hook = FWorldDelegates::OnWorldPostActorTick.AddLambda([WC](UWorld* InW, ELevelTick, float)
		{
			TSharedPtr<FCtx> P = WC.Pin();
			if (P && P->W.Get() == InW) { Sample(P.ToSharedRef()); }
		});
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			NextCase(C);
		}), FMath::Max(0.5f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
