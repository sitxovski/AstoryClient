// Copyright (c) 2026 @sitxovski (Discord)
// Реализация утилиты физассета тени героя. Зачем и почему C++ — в заголовке AsdaShadowPhysicsAssetLib.h.
#include "Editor/AsdaShadowPhysicsAssetLib.h"

#include "Engine/SkeletalMesh.h"
#include "PhysicsAssetUtils.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "ReferenceSkeleton.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "AssetRegistry/AssetRegistryModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaShadowPA, Log, All);

namespace
{
	// max радиус среди сфер/капсул тела (0 — фигур нет)
	float MaxRadiusOf(const USkeletalBodySetup* Body)
	{
		float R = 0.0f;
		for (const FKSphylElem& S : Body->AggGeom.SphylElems) { R = FMath::Max(R, S.Radius); }
		for (const FKSphereElem& S : Body->AggGeom.SphereElems) { R = FMath::Max(R, S.Radius); }
		return R;
	}
}

TArray<FString> UAsdaShadowPhysicsAssetLib::DescribeBodies(UPhysicsAsset* PhysicsAsset, USkeletalMesh* Mesh)
{
	TArray<FString> Out;
	if (PhysicsAsset == nullptr)
	{
		return Out;
	}
	for (int32 i = 0; i < PhysicsAsset->SkeletalBodySetups.Num(); ++i)
	{
		const USkeletalBodySetup* Body = PhysicsAsset->SkeletalBodySetups[i];
		if (Body == nullptr)
		{
			Out.Add(FString::Printf(TEXT("%d|<null body>"), i));
			continue;
		}
		int32 RefIdx = INDEX_NONE;
		float RefZ = 0.0f;
		if (Mesh != nullptr)
		{
			RefIdx = Mesh->GetRefSkeleton().FindBoneIndex(Body->BoneName);
			if (RefIdx != INDEX_NONE)
			{
				// позиция кости в референс-позе в пространстве меша (та же матрица, что берёт прокси тени)
				RefZ = static_cast<float>(Mesh->GetComposedRefPoseMatrix(RefIdx).GetOrigin().Z);
			}
		}
		float R = 0.0f, L = 0.0f;
		if (Body->AggGeom.SphylElems.Num() > 0)
		{
			R = Body->AggGeom.SphylElems[0].Radius;
			L = Body->AggGeom.SphylElems[0].Length;
		}
		else if (Body->AggGeom.SphereElems.Num() > 0)
		{
			R = Body->AggGeom.SphereElems[0].Radius;
		}
		Out.Add(FString::Printf(TEXT("%s|%d|%.1f|%d|%d|%d|%d|%.3f|%.3f"),
			*Body->BoneName.ToString(), RefIdx, RefZ,
			Body->AggGeom.SphylElems.Num(), Body->AggGeom.SphereElems.Num(),
			Body->AggGeom.BoxElems.Num(), Body->AggGeom.ConvexElems.Num(), R, L));
	}
	return Out;
}

int32 UAsdaShadowPhysicsAssetLib::ScaleCapsules(UPhysicsAsset* PhysicsAsset, float RadiusScale, float MinRadius,
                                                float MaxRadius, float DropBelowRadius, float LengthScale)
{
	if (PhysicsAsset == nullptr || RadiusScale <= 0.0f || MaxRadius < MinRadius)
	{
		UE_LOG(LogAsdaShadowPA, Error, TEXT("ScaleCapsules: неверные аргументы (asset=%s scale=%.3f min=%.1f max=%.1f)"),
			PhysicsAsset ? *PhysicsAsset->GetPathName() : TEXT("null"), RadiusScale, MinRadius, MaxRadius);
		return -1;
	}
	PhysicsAsset->Modify();

	// 1) тела с вырожденными фигурами — вон (вместе с их констрейнтами)
	TSet<FName> Dropped;
	for (int32 i = PhysicsAsset->SkeletalBodySetups.Num() - 1; i >= 0; --i)
	{
		USkeletalBodySetup* Body = PhysicsAsset->SkeletalBodySetups[i];
		if (Body == nullptr || MaxRadiusOf(Body) < DropBelowRadius)
		{
			if (Body != nullptr)
			{
				Dropped.Add(Body->BoneName);
				UE_LOG(LogAsdaShadowPA, Log, TEXT("  тело '%s' удалено (r=%.3f < %.3f)"), *Body->BoneName.ToString(), MaxRadiusOf(Body), DropBelowRadius);
			}
			PhysicsAsset->SkeletalBodySetups.RemoveAt(i);
		}
	}
	if (Dropped.Num() > 0)
	{
		for (int32 c = PhysicsAsset->ConstraintSetup.Num() - 1; c >= 0; --c)
		{
			UPhysicsConstraintTemplate* CS = PhysicsAsset->ConstraintSetup[c];
			if (CS == nullptr || Dropped.Contains(CS->DefaultInstance.ConstraintBone1) || Dropped.Contains(CS->DefaultInstance.ConstraintBone2))
			{
				PhysicsAsset->ConstraintSetup.RemoveAt(c);
			}
		}
	}

	// 2) укрупнение фигур
	int32 NumShapes = 0;
	for (USkeletalBodySetup* Body : PhysicsAsset->SkeletalBodySetups)
	{
		Body->Modify();
		for (FKSphylElem& S : Body->AggGeom.SphylElems)
		{
			S.Radius = FMath::Clamp(S.Radius * RadiusScale, MinRadius, MaxRadius);
			S.Length = FMath::Max(0.0f, S.Length * LengthScale);
			++NumShapes;
		}
		for (FKSphereElem& S : Body->AggGeom.SphereElems)
		{
			S.Radius = FMath::Clamp(S.Radius * RadiusScale, MinRadius, MaxRadius);
			++NumShapes;
		}
		Body->InvalidatePhysicsData();
	}
	PhysicsAsset->UpdateBodySetupIndexMap();
	PhysicsAsset->UpdateBoundsBodiesArray();
	PhysicsAsset->RefreshPhysicsAssetChange();
	UE_LOG(LogAsdaShadowPA, Log, TEXT("ScaleCapsules %s: тел %d, фигур %d, удалено тел %d, r'=clamp(r*%.2f, %.0f, %.0f)"),
		*PhysicsAsset->GetPathName(), PhysicsAsset->SkeletalBodySetups.Num(), NumShapes, Dropped.Num(), RadiusScale, MinRadius, MaxRadius);
	return PhysicsAsset->SkeletalBodySetups.Num();
}

UPhysicsAsset* UAsdaShadowPhysicsAssetLib::CreateSphylFromMesh(USkeletalMesh* Mesh, const FString& PackagePath,
                                                               const FString& AssetName, float MinBoneSize)
{
	if (Mesh == nullptr || AssetName.IsEmpty() || !FPackageName::IsValidLongPackageName(PackagePath / AssetName))
	{
		UE_LOG(LogAsdaShadowPA, Error, TEXT("CreateSphylFromMesh: неверные аргументы (mesh=%s, path=%s, name=%s)"),
			Mesh ? *Mesh->GetPathName() : TEXT("null"), *PackagePath, *AssetName);
		return nullptr;
	}
	const FString PackageName = PackagePath / AssetName;
	UPackage* Package = CreatePackage(*PackageName);
	if (Package == nullptr)
	{
		return nullptr;
	}
	Package->FullyLoad();
	UPhysicsAsset* PA = NewObject<UPhysicsAsset>(Package, *AssetName, RF_Public | RF_Standalone);
	if (PA == nullptr)
	{
		return nullptr;
	}
	FPhysAssetCreateParams Params;             // дефолты движка (Sphyl, DominantWeight, AutoOrient)
	Params.MinBoneSize = MinBoneSize;
	Params.GeomType = EFG_Sphyl;
	Params.bCreateConstraints = false;         // тени констрейнты не нужны
	FText Err;
	// bSetToMesh=false: обычный PhysicsAsset меша НЕ трогаем; bShowProgress=false — headless без окон
	const bool bOk = FPhysicsAssetUtils::CreateFromSkeletalMesh(PA, Mesh, Params, Err, /*bSetToMesh=*/false, /*bShowProgress=*/false);
	if (!bOk)
	{
		UE_LOG(LogAsdaShadowPA, Error, TEXT("CreateSphylFromMesh %s: %s"), *PackageName, *Err.ToString());
		return nullptr;
	}
	PA->SetPreviewMesh(Mesh);
	PA->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(PA);
	UE_LOG(LogAsdaShadowPA, Log, TEXT("CreateSphylFromMesh %s: тел %d"), *PackageName, PA->SkeletalBodySetups.Num());
	return PA;
}
