// Copyright (c) 2026 @sitxovski (Discord)
// ★ ДИАГНОСТИКА «НАДПИСИ ПОВЕРХ ДЕРЕВЬЕВ» (05.11.2026, баг игроков «имена NPC видны сквозь деревья») —
//   `asda.Overhead.TreeProbe [max=N]`. Для деревьев карты (ISM-компоненты с моделью из списка листвы — префикс имени `tree`)
//   ставит синтетическую пару «камера игрока → точка над головой NPC за деревом» и пускает три луча:
//     (а) боевой — `AsdaOverheadOcclusion::TraceNow` с камерой, перенесённой в синтетическую точку (тот же код, что у плашек);
//     (б) мировой по треугольникам канала Visibility — что первым задевает;
//     (в) прямой по компоненту дерева (`LineTraceComponent`, треугольники) — есть ли у меша трассируемая геометрия вообще.
//   Плюс сведения о меше: коллизия компонента, флаг трассировки тела, число треугольных сеток Chaos, коллизия подсеток рендера
//   и режим смешивания материалов. Строки — «ДЕРЕВО-ЗОНД».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "World/AsdaOverheadOcclusion.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshResources.h"
#include "UObject/UObjectIterator.h"
#include "Test/AsdaSelfTestKit.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTreeProbe, Log, All);

namespace AsdaTreeProbeLocal
{
	// Признак дерева для зонда: имя модели (без «SM_») начинается с «tree» (tree_, treegru… — как список листвы сета B).
	static bool IsTreeMesh(const UStaticMesh* SM)
	{
		if (!SM) { return false; }
		FString N = SM->GetName();
		if (N.StartsWith(TEXT("SM_"), ESearchCase::IgnoreCase)) { N.RightChopInline(3); }
		return N.StartsWith(TEXT("tree"), ESearchCase::IgnoreCase) || N.Contains(TEXT("_tree"), ESearchCase::IgnoreCase);
	}

	// Сводка меша: коллизия подсеток LOD0 и режимы смешивания их материалов.
	static FString MeshInfo(const UInstancedStaticMeshComponent* C)
	{
		const UStaticMesh* SM = C->GetStaticMesh();
		const UBodySetup* BS = SM ? SM->GetBodySetup() : nullptr;
		FString Sec;
		if (SM && SM->GetRenderData() && SM->GetRenderData()->LODResources.Num() > 0)
		{
			const FStaticMeshLODResources& L = SM->GetRenderData()->LODResources[0];
			for (int32 i = 0; i < L.Sections.Num(); ++i)
			{
				const UMaterialInterface* M = C->GetMaterial(L.Sections[i].MaterialIndex);
				Sec += FString::Printf(TEXT("[%d: колл=%d тр=%d смеш=%d %s] "), i, L.Sections[i].bEnableCollision ? 1 : 0,
					L.Sections[i].NumTriangles, M ? (int32)M->GetBlendMode() : -1, M ? *M->GetName() : TEXT("-"));
			}
		}
		return FString::Printf(TEXT("режим=%d Vis=%d тело: трасс=%d сеток=%d простая=%d | %s"),
			(int32)C->GetCollisionEnabled(), (int32)C->GetCollisionResponseToChannel(ECC_Visibility),
			BS ? (int32)BS->CollisionTraceFlag : -1, BS ? BS->TriMeshGeometries.Num() : -1,
			BS ? BS->AggGeom.GetElementCount() : -1, *Sec);
	}

	static void Run(UWorld* W, int32 MaxSamples, bool bQuit)
	{
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		if (!PC || !PC->PlayerCameraManager) { AsdaSelfTestKit::Finish(W, TEXT("asda.Overhead.TreeProbe"), bQuit, LogAsdaTreeProbe, TEXT("ДЕРЕВО-ЗОНД"), 0, 1, TEXT("нет камеры")); return; }
		int32 Samples = 0, HitWorldTree = 0, HitComp = 0, Meshes = 0;
		TSet<const UStaticMesh*> Seen;
		for (TObjectIterator<UInstancedStaticMeshComponent> It; It && Samples < MaxSamples; ++It)
		{
			UInstancedStaticMeshComponent* C = *It;
			if (!C || C->GetWorld() != W || !C->IsRegistered() || !IsTreeMesh(C->GetStaticMesh())) { continue; }
			const int32 N = C->GetInstanceCount();
			if (N <= 0) { continue; }
			if (!Seen.Contains(C->GetStaticMesh()))
			{
				Seen.Add(C->GetStaticMesh());
				++Meshes;
				UE_LOG(LogAsdaTreeProbe, Log, TEXT("ДЕРЕВО-ЗОНД меш %s (%d шт.): %s"), *C->GetStaticMesh()->GetName(), N, *MeshInfo(C));
			}
			// По одному экземпляру на компонент (первый): дерево в мире, его рамка.
			FTransform T;
			if (!C->GetInstanceTransform(0, T, /*bWorldSpace=*/true)) { continue; }
			const FBox B = C->GetStaticMesh()->GetBoundingBox().TransformBy(T);
			const FVector Ctr = B.GetCenter();
			const float H = B.Max.Z - B.Min.Z;
			if (H < 150.f) { continue; }
			for (int32 Dir = 0; Dir < 2 && Samples < MaxSamples; ++Dir)
			{
				// Камера игрока: 1800 ед. по горизонтали от ствола, на 700 выше его основания (обычная стрела); NPC — 400 ед. за
				//   деревом, точка его надписи — 200 над землёй (как у плашки). Два направления.
				const FVector D = FRotator(0.f, 37.f + 120.f * Dir, 0.f).Vector();
				const FVector Base(Ctr.X, Ctr.Y, B.Min.Z);
				const FVector Cam = Base + D * 1800.f + FVector(0, 0, 700.f);
				const FVector Pt = Base - D * 400.f + FVector(0, 0, 200.f);
				// (в) прямой по компоненту
				FHitResult HC;
				const bool bComp = C->LineTraceComponent(HC, Cam, Pt, FCollisionQueryParams(SCENE_QUERY_STAT(AsdaTreeProbeC), true));
				// (б) мировой
				FHitResult HW;
				const bool bWorld = W->LineTraceSingleByChannel(HW, Cam, Pt, ECC_Visibility, FCollisionQueryParams(SCENE_QUERY_STAT(AsdaTreeProbeW), true));
				// Дерево карты первым на пути (это дерево или соседняя часть/соседнее дерево — ствол и кроны отдельными мешами).
				const bool bWorldTree = bWorld && HW.GetComponent() && HW.GetComponent()->IsA<UInstancedStaticMeshComponent>()
					&& IsTreeMesh(Cast<UInstancedStaticMeshComponent>(HW.GetComponent())->GetStaticMesh());
				++Samples;
				HitComp += bComp ? 1 : 0;
				HitWorldTree += bWorldTree ? 1 : 0;
				UE_LOG(LogAsdaTreeProbe, Log, TEXT("ДЕРЕВО-ЗОНД %s инст0 высота %.0f напр %d: компонент=%d (%.0f) | мир=%d %s/%s %.0f"),
					*C->GetStaticMesh()->GetName(), H, Dir, bComp ? 1 : 0, HC.Distance, bWorld ? 1 : 0,
					*GetNameSafe(HW.GetActor()), *GetNameSafe(HW.GetComponent()), HW.Distance);
			}
		}
		UE_LOG(LogAsdaTreeProbe, Log, TEXT("ДЕРЕВО-ЗОНД ИТОГ: мешей деревьев %d, проб %d, луч по компоненту задел %d, мировой луч первым задел дерево карты %d"),
			Meshes, Samples, HitComp, HitWorldTree);
		// Итог пакета: ok — проба, где мировой луч первым задел дерево; fail — дерево на пути, а луч прошёл мимо него.
		AsdaSelfTestKit::Finish(W, TEXT("asda.Overhead.TreeProbe"), bQuit, LogAsdaTreeProbe, TEXT("ДЕРЕВО-ЗОНД"), HitWorldTree, Samples - HitWorldTree);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaOverheadTreeProbe(
	TEXT("asda.Overhead.TreeProbe"),
	TEXT("asda.Overhead.TreeProbe [max=N] — диагностика: задевает ли луч надписи над головой деревья карты (компонент, мир, флаги коллизии мешей)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		int32 Max = 60;
		bool bQuit = false;
		for (const FString& Arg : A)
		{
			if (Arg.StartsWith(TEXT("max="))) { Max = FMath::Max(1, FCString::Atoi(*Arg.Mid(4))); }
			else if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		}
		AsdaTreeProbeLocal::Run(W, Max, bQuit);
	}));

#endif // !UE_BUILD_SHIPPING
