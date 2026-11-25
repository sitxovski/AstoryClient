// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ЭМБЛЕМА ЗАХВАТА ГОРОДА ЛЕЖИТ НА ЗЕМЛЕ» (09.11.2026, баг alpha-7.4.0): `asda.SelfTest.CityWarFloor [quit]`.
//   Жалоба владельца: «occupying emblems in city (all 3) floating in the air, instead of being on the ground».
//   Эмблема-площадка = меш карты с родной текстурой UNICON03 (подменяется `UAsdaWorldSubsystem::ApplyCityWarTextures`
//   на uniconLight/Dark/Chaos/NotConqureFloor). Для КАЖДОГО города (Силарис 0, Альпия 3, Пламения 7):
//     • 6743 «город за Светом» до готовности мира (вход на карту после захвата) → мир поднялся → перекраска;
//     • тень у площадки снята (`asda.War.FloorNoShadow`, причина бага: силуэт тени эмблемы на земле со сдвигом);
//     • замер: квад площадки против земли .TER по сетке 11×11 (`AsdaGround::GroundZ`, как оригинал) — размещение из данных;
//     • смена хозяина на глазах (Тьма) — замер повторяется (высота от хозяина не зависит);
//     • кадры сбоку и сверху: `Reports/batch21/city_floor/<ru|en>_<город>_<шаг>_{side,top}.png`.
//   Итог: «ПЛОЩАДКА-ГОРОДА ИТОГ: ok=N fail=M». Только офлайн (-AsdaNoNet).
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaGroundTrace.h"
#include "World/AsdaCoord.h"
#include "Resources/AsdaMaterialNames.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "StaticMeshResources.h"
#include "ProceduralMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Engine/SceneCapture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCityFloorTest, Log, All);

namespace AsdaCityWarFloorSelfTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.CityWarFloor");   // имя команды
	// Предел среднего зазора «квад площадки − земля .TER», UE-единицы: размещение — данные оригинала (Pos.POS),
	//   замер 09.11 дал 1.8…27.5; больше 30 — значит, площадку сдвинули (импорт/код), а не данные.
	static constexpr float kTolerance = 30.f;

	struct FCity { const TCHAR* Level; int32 MapId; int32 Slot; const TCHAR* Tag; };
	static const FCity kCities[3] =
	{
		{ TEXT("/Game/Map/silalis/silalis"), 0, 1, TEXT("silalis") },
		{ TEXT("/Game/Map/alpen/alpen"),     3, 0, TEXT("alpen") },
		{ TEXT("/Game/Map/flammio/flammio"), 7, 2, TEXT("flammio") },
	};

	struct FRun
	{
		TWeakObjectPtr<UGameInstance> GI;
		bool bQuit = false;
		int32 Ok = 0, Fail = 0;
		int32 City = 0;
		int32 Step = 0;                 // 0 ждём мир, 1 вход: замер+кадр сбоку, 2 кадр сверху, 3 смена хозяина: замер+кадр, 4 сверху, 5 дальше
		double StepAt = 0.0;
		TWeakObjectPtr<UWorld> OldWorld;
		TWeakObjectPtr<ACameraActor> Cam;
		FString Lang, ShotDir;
		FBox Floor = FBox(ForceInit);   // границы площадок текущего города
		TArray<FBox> Pads;              // границы каждой площадки (кадры вблизи)
		int32 Pad = 0;                  // площадка текущего кадра
		int32 Shot = 0;                 // счётчик «наводка/кадр» (чётный — наводка; 0..1 сбоку, 2..3 сверху)
		FString Phase;                  // метка шага в имени кадра
		int32 Instances = 0;            // экземпляров исходной площадки (замер по данным)
	};
	static TSharedPtr<FRun> GRun;

	static void Expect(FRun& R, bool b, const FString& What)
	{
		if (b) { ++R.Ok; UE_LOG(LogAsdaCityFloorTest, Log, TEXT("ПЛОЩАДКА-ГОРОДА ok   %s"), *What); }
		else { ++R.Fail; UE_LOG(LogAsdaCityFloorTest, Error, TEXT("ПЛОЩАДКА-ГОРОДА FAIL %s"), *What); }
	}

	// Тело 6743: город Slot за Owner, остальные не заняты.
	static void Feed(UAsdaNetSubsystem* Net, int32 Slot, int32 Owner)
	{
		TArray<uint8> B; B.SetNumZeroed(274); B[0] = 1;
		for (int32 i = 0; i < 3; ++i)
		{
			uint8* P = B.GetData() + 1 + 91 * i;
			const int16 F = (int16)(i == Slot ? Owner : 3);
			FMemory::Memcpy(P, &F, 2);
			const int16 NoClan = -1;
			FMemory::Memcpy(P + 6, &NoClan, 2);
		}
		AsdaTestNet::DebugFeedPacket(Net, 6743, B);
	}

	// Родная текстура слота (у исходного родителя, не у MID).
	static FString NativeTex(UMaterialInterface* MI)
	{
		while (UMaterialInstanceDynamic* D = Cast<UMaterialInstanceDynamic>(MI)) { MI = D->Parent; }
		UTexture* T = nullptr;
		if (!MI || !MI->GetTextureParameterValue(FName(AsdaMat::kParamTex), T) || !T) { return FString(); }
		return T->GetName();
	}

	// Замер всех площадок UNICON03 ПО ВЕРШИНАМ: компонент `I_unicon03` — набор экземпляров на всю карту (ISM), поэтому
	//   границы компонента ничего не говорят. Для каждого экземпляра каждая вершина LOD0 переводится в мир, и зазор
	//   «вершина − земля .TER в той же точке» (`AsdaGround::GroundZ` — та же сетка, по которой ходит оригинал).
	static void Measure(FRun& R, UWorld* W, const FString& Tag)
	{
		int32 Found = 0;
		R.Floor = FBox(ForceInit);
		R.Instances = 0;
		R.Pads.Reset();
		for (TActorIterator<AActor> It(W); It; ++It)
		{
			TArray<UStaticMeshComponent*> Comps;
			It->GetComponents<UStaticMeshComponent>(Comps);
			for (UStaticMeshComponent* MC : Comps)
			{
				bool bFloor = false;
				for (int32 i = 0; MC && i < MC->GetNumMaterials(); ++i)
				{
					if (NativeTex(MC->GetMaterial(i)).Equals(TEXT("UNICON03"), ESearchCase::IgnoreCase)) { bFloor = true; break; }
				}
				if (!bFloor || !MC->GetStaticMesh() || !MC->GetStaticMesh()->GetRenderData()) { continue; }
				const FPositionVertexBuffer& PB = MC->GetStaticMesh()->GetRenderData()->LODResources[0].VertexBuffers.PositionVertexBuffer;
				{
					// Раскладка квада в ЛОКАЛЬНЫХ координатах меша: позиция → UV (для правки без CPU-копии вершин).
					const FStaticMeshVertexBuffer& VB0 = MC->GetStaticMesh()->GetRenderData()->LODResources[0].VertexBuffers.StaticMeshVertexBuffer;
					FString Lay;
					for (uint32 v = 0; WITH_EDITOR && v < PB.GetNumVertices() && PB.GetVertexData() && VB0.GetTangentData(); ++v)   // собранная игра: CPU-копии нет
					{
						const FVector3f Lp = PB.VertexPosition(v); const FVector2f Uv = VB0.GetVertexUV(v, 0);
						Lay += FString::Printf(TEXT(" (%.1f,%.1f,%.1f)->(%.3f,%.3f)"), Lp.X, Lp.Y, Lp.Z, Uv.X, Uv.Y);
					}
					const FBox LB = MC->GetStaticMesh()->GetBoundingBox();
					UE_LOG(LogAsdaCityFloorTest, Log, TEXT("ПЛОЩАДКА-ГОРОДА раскладка %s: границы %s;%s"), *MC->GetStaticMesh()->GetPathName(), *LB.ToString(), *Lay);
				}
				TArray<FTransform> Xf;
				if (const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(MC))
				{
					for (int32 k = 0; k < ISM->GetInstanceCount(); ++k) { FTransform T; ISM->GetInstanceTransform(k, T, /*bWorldSpace*/ true); Xf.Add(T); }
				}
				else { Xf.Add(MC->GetComponentTransform()); }
				for (int32 k = 0; k < Xf.Num(); ++k)
				{
					++Found; ++R.Instances;
					FBox Bx(ForceInit);
					float GapMin = 1e9f, GapMax = -1e9f; double GapSum = 0.0; int32 N = 0;
					float VisMin = 1e9f, VisMax = -1e9f; double VisSum = 0.0; int32 VisN = 0; FString VisSample;
					TArray<FVector> V;
					if (WITH_EDITOR && PB.GetNumVertices() > 0 && PB.GetVertexData() != nullptr)
					{
						for (uint32 v = 0; v < PB.GetNumVertices(); ++v) { V.Add(Xf[k].TransformPosition(FVector(PB.VertexPosition(v)))); Bx += V.Last(); }
					}
					else
					{
						// Собранная игра: CPU-копии вершин нет — квад по габаритам меша (плоский, на средней высоте границ).
						const FBox LB = MC->GetStaticMesh()->GetBoundingBox();
						const float Zc = LB.GetCenter().Z;
						for (const FVector& L : { FVector(LB.Min.X, LB.Min.Y, Zc), FVector(LB.Max.X, LB.Min.Y, Zc), FVector(LB.Max.X, LB.Max.Y, Zc), FVector(LB.Min.X, LB.Max.Y, Zc) })
						{
							V.Add(Xf[k].TransformPosition(L)); Bx += V.Last();
						}
					}
					// Площадка — плоский квад: высота плоскости в любой точке по трём вершинам; сетка 11×11 внутри границ.
					const FPlane Pl = V.Num() >= 3 ? FPlane(V[0], V[1], V[2]) : FPlane(0, 0, 1, Bx.Min.Z);
					for (int32 gy = 0; gy <= 10 && FMath::Abs(Pl.Z) > 1e-3f; ++gy)
					{
						for (int32 gx = 0; gx <= 10; ++gx)
						{
							const float X = FMath::Lerp(Bx.Min.X, Bx.Max.X, gx / 10.f), Y = FMath::Lerp(Bx.Min.Y, Bx.Max.Y, gy / 10.f);
							const float Z = (Pl.W - Pl.X * X - Pl.Y * Y) / Pl.Z;
							const float G = AsdaGround::GroundZ(W, X, Y, NAN);
							if (FMath::IsNaN(G)) { continue; }
							const float Gap = Z - G;
							GapMin = FMath::Min(GapMin, Gap); GapMax = FMath::Max(GapMax, Gap); GapSum += Gap; ++N;
							// Видимая земля: трассировка по рендер-геометрии (complex), сама площадка исключена.
							FHitResult H;
							FCollisionQueryParams Q(TEXT("CityFloorVis"), /*bTraceComplex*/ true);
							Q.AddIgnoredComponent(MC);
							if (W->LineTraceSingleByChannel(H, FVector(X, Y, Z + 300.f), FVector(X, Y, Z - 3000.f), ECC_Visibility, Q))
							{
								const float GV = Z - H.ImpactPoint.Z;
								VisMin = FMath::Min(VisMin, GV); VisMax = FMath::Max(VisMax, GV); VisSum += GV; ++VisN;
								if (GV > 15.f && VisSample.Len() < 300)
								{
									VisSample += FString::Printf(TEXT(" [%.0f,%.0f]=%.0f(%s)"), X, Y, GV, H.GetComponent() ? *H.GetComponent()->GetName() : TEXT("?"));
								}
							}
						}
					}
					R.Floor += Bx;
					R.Pads.Add(Bx);
					const float Mean = N ? (float)(GapSum / N) : NAN;
					UE_LOG(LogAsdaCityFloorTest, Log,
						TEXT("ПЛОЩАДКА-ГОРОДА %s: %s экз.%d зазор до ВИДИМОЙ земли (трасса complex): мин %.1f, сред %.1f, макс %.1f, точек %d;%s"),
						*Tag, *MC->GetName(), k, VisMin, VisN ? (float)(VisSum / VisN) : NAN, VisMax, VisN, *VisSample);
					UE_LOG(LogAsdaCityFloorTest, Log,
						TEXT("ПЛОЩАДКА-ГОРОДА %s: %s экз.%d тень=%d центр (%.0f, %.0f, %.0f) размер %.0f×%.0f×%.0f; точек %d; зазор до .TER: мин %.1f, сред %.1f, макс %.1f"),
						*Tag, *MC->GetName(), k, MC->CastShadow ? 1 : 0, Bx.GetCenter().X, Bx.GetCenter().Y, Bx.GetCenter().Z,
						Bx.Max.X - Bx.Min.X, Bx.Max.Y - Bx.Min.Y, Bx.Max.Z - Bx.Min.Z, N, GapMin, Mean, GapMax);
					Expect(R, N > 0 && Mean <= kTolerance,
						FString::Printf(TEXT("%s: площадка %s экз.%d на месте из данных: средний зазор до .TER %.1f (предел %.0f), макс %.1f"), *Tag, *MC->GetName(), k, Mean, kTolerance, GapMax));
				}
			}
		}
		Expect(R, Found > 0, FString::Printf(TEXT("%s: площадок UNICON03 найдено %d"), *Tag, Found));
	}

	// ★ 10.11: ВИДИМЫЙ пол — по глубине кадра, а не по коллизии (коллизия мощения в собранной игре может лежать выше
	//   нарисованного — тогда трасса «видит» пол там, где его не видно). Ортокамера сверху над границами Bx пишет глубину
	//   сцены (SCS_SceneDepth) в Res×Res; высота пикселя = высота камеры − глубина. Hide — что не рисовать (сама эмблема),
	//   Only — если не пусто, рисовать только это (калибровка: сетка с известными вершинами). Пиксель без геометрии — NaN.
	//   Раскладка: камера смотрит вниз (pitch −90, yaw 0): вправо по кадру = +Y мира, вниз по кадру = −X мира.
	static constexpr int32 kDepthRes = 160;
	struct FHeightMap { TArray<float> Z; FVector Center = FVector::ZeroVector; float Width = 0.f; int32 Res = 0;
		bool At(float X, float Y, float& Out) const
		{
			const float Px = (Y - Center.Y) / Width * Res + Res * 0.5f, Py = (Center.X - X) / Width * Res + Res * 0.5f;
			const int32 ix = FMath::FloorToInt(Px), iy = FMath::FloorToInt(Py);
			if (ix < 0 || iy < 0 || ix >= Res || iy >= Res) { return false; }
			Out = Z[iy * Res + ix];
			return !FMath::IsNaN(Out);
		}
	};
	static bool RenderHeights(UWorld* W, const FBox& Bx, const TArray<UPrimitiveComponent*>& Hide, const TArray<UPrimitiveComponent*>& Only, FHeightMap& Out)
	{
		FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASceneCapture2D* A = W->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(), FTransform::Identity, SP);
		if (!A) { return false; }
		USceneCaptureComponent2D* SC = A->GetCaptureComponent2D();
		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(A);
		RT->RenderTargetFormat = RTF_RGBA32f;
		RT->ClearColor = FLinearColor(1e7f, 1e7f, 1e7f, 1.f);
		RT->InitAutoFormat(kDepthRes, kDepthRes);
		RT->UpdateResourceImmediate(true);
		Out.Res = kDepthRes;
		Out.Center = Bx.GetCenter();
		Out.Width = FMath::Max(Bx.GetSize().X, Bx.GetSize().Y) + 200.f;
		const float CamZ = Bx.Max.Z + 1500.f;
		SC->TextureTarget = RT;
		SC->ProjectionType = ECameraProjectionMode::Orthographic;
		SC->OrthoWidth = Out.Width;
		SC->bAutoCalculateOrthoPlanes = false;
		SC->CaptureSource = SCS_SceneDepth;
		SC->bCaptureEveryFrame = false;
		SC->bCaptureOnMovement = false;
		SC->MaxViewDistanceOverride = 5000.f;
		for (UPrimitiveComponent* C : Hide) { if (C) { SC->HideComponent(C); } }
		if (Only.Num() > 0)
		{
			SC->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
			for (UPrimitiveComponent* C : Only) { if (C) { SC->ShowOnlyComponent(C); } }
		}
		A->SetActorLocationAndRotation(FVector(Out.Center.X, Out.Center.Y, CamZ), FRotator(-90.f, 0.f, 0.f));
		SC->CaptureScene();
		TArray<FLinearColor> Px;
		FTextureRenderTargetResource* RR = RT->GameThread_GetRenderTargetResource();
		const bool bRead = RR && RR->ReadLinearColorPixels(Px) && Px.Num() == kDepthRes * kDepthRes;
		A->Destroy();
		if (!bRead) { return false; }
		Out.Z.SetNumUninitialized(Px.Num());
		for (int32 i = 0; i < Px.Num(); ++i)
		{
			const float D = Px[i].R;
			Out.Z[i] = (D > 0.f && D < 4900.f) ? CamZ - D : NAN;
		}
		return true;
	}

	// Зазор «эмблема − видимый пол» по глубине кадра. Возврат: пикселей сравнено (0 — замер не удался).
	//   Сначала калибровка: кадр ТОЛЬКО сетки против её известных вершин (проверка раскладки и перевода глубины в высоту).
	struct FGapStat { int32 N = 0, Covered = 0; float Lo = 1e9f, Hi = -1e9f, P95 = 0.f, Calib = 1e9f; double Sum = 0.0; };
	static FGapStat RenderGap(UWorld* W, UProceduralMeshComponent* PM, const TArray<UPrimitiveComponent*>& HideAll, const FBox& Bx)
	{
		FGapStat S;
		FHeightMap Patch, Floor;
		if (!RenderHeights(W, Bx, {}, { PM }, Patch) || !RenderHeights(W, Bx, HideAll, {}, Floor)) { return S; }
		// Калибровка: высота сетки по кадру против высоты её вершин.
		const FProcMeshSection* Sec = PM->GetProcMeshSection(0);
		float CalibMax = 0.f; int32 CalibN = 0;
		for (int32 v = 0; Sec && v < Sec->ProcVertexBuffer.Num(); v += 5)
		{
			const FVector P = PM->GetComponentTransform().TransformPosition(Sec->ProcVertexBuffer[v].Position);
			float Zr;
			if (Patch.At(P.X, P.Y, Zr)) { CalibMax = FMath::Max(CalibMax, FMath::Abs(Zr - P.Z)); ++CalibN; }
		}
		S.Calib = CalibN > 0 ? CalibMax : 1e9f;
		TArray<float> Gaps;
		for (int32 i = 0; i < Patch.Z.Num(); ++i)
		{
			const float Zp = Patch.Z[i], Zf = Floor.Z[i];
			if (FMath::IsNaN(Zp) || FMath::IsNaN(Zf)) { continue; }
			const float G = Zp - Zf;
			if (G < -50.f) { continue; }   // над эмблемой стоит объект (дерево, мост) — это не пол
			if (G < -0.5f) { ++S.Covered; continue; }   // эмблему здесь накрывает объект карты (настил лавки) — её не видно
			Gaps.Add(G);
			S.Lo = FMath::Min(S.Lo, G); S.Hi = FMath::Max(S.Hi, G); S.Sum += G;
		}
		S.N = Gaps.Num();
		if (S.N > 0) { Gaps.Sort(); S.P95 = Gaps[FMath::Min(S.N - 1, (int32)(S.N * 0.95f))]; }
		return S;
	}

	// Сетки площадки по земле (`asda.War.FloorConform`): ProceduralMesh с материалом площадки. Проверки: исходный квад скрыт,
	//   сеток столько же, сколько экземпляров, каждая вершина — в kLift±kConformTol над видимой землёй, тени нет, текстура = хозяин.
	static constexpr float kConformTol = 6.f;
	// Пределы зазора «эмблема − видимый пол» по глубине кадра (UE-ед.; щиколотка персонажа ≈ 10–15):
	static constexpr float kCalibTol = 3.f;      // калибровка: кадр сетки против её вершин
	static constexpr float kRenderP95 = 2.f;     // 95% видимой площади — не выше этого над полом (подъём 1)
	static constexpr float kRenderHi = 3.f;      // ни одна видимая точка не выше (щиколотка ≈ 10–15)
	static constexpr float kCoveredMax = 0.06f;  // доля эмблемы под объектами карты (настил лавок Альпии ≈ 3%)
	static void MeasurePatches(FRun& R, UWorld* W, const FString& Tag, int32 ExpectPads, const TCHAR* WantTex)
	{
		int32 Patches = 0; bool bSrcHidden = true;
		R.Pads.Reset();
		// Трасса до видимой земли не должна цеплять ни сетки, ни скрытый исходный квад (коллизия у скрытого остаётся).
		FCollisionQueryParams Q(TEXT("CityFloorPatch"), /*bTraceComplex*/ true);
		TArray<UPrimitiveComponent*> HideAll;   // все площадки и их сетки — не рисуются в кадре «видимого пола»
		for (TActorIterator<AActor> It(W); It; ++It)
		{
			TArray<UMeshComponent*> Comps;
			It->GetComponents<UMeshComponent>(Comps);
			for (UMeshComponent* MC : Comps)
			{
				if (MC && MC->GetNumMaterials() > 0 && NativeTex(MC->GetMaterial(0)).Equals(TEXT("UNICON03"), ESearchCase::IgnoreCase)) { Q.AddIgnoredComponent(MC); HideAll.Add(MC); }
			}
		}
		for (TActorIterator<AActor> It(W); It; ++It)
		{
			TArray<UMeshComponent*> Comps;
			It->GetComponents<UMeshComponent>(Comps);
			for (UMeshComponent* MC : Comps)
			{
				if (!MC || MC->GetNumMaterials() == 0 || !NativeTex(MC->GetMaterial(0)).Equals(TEXT("UNICON03"), ESearchCase::IgnoreCase)) { continue; }
				UProceduralMeshComponent* PM = Cast<UProceduralMeshComponent>(MC);
				if (!PM) { if (MC->IsVisible()) { bSrcHidden = false; } continue; }
				++Patches;
				const FProcMeshSection* Sec = PM->GetProcMeshSection(0);
				FBox Bx(ForceInit);
				float Lo = 1e9f, Hi = -1e9f; int32 N = 0;
				for (int32 v = 0; Sec && v < Sec->ProcVertexBuffer.Num(); v += 7)
				{
					const FVector P = PM->GetComponentTransform().TransformPosition(Sec->ProcVertexBuffer[v].Position);
					Bx += P;
					FHitResult H;
					if (!W->LineTraceSingleByChannel(H, P + FVector(0, 0, 200.f), P - FVector(0, 0, 2000.f), ECC_Visibility, Q)) { continue; }
					const float Gap = P.Z - H.ImpactPoint.Z;
					// Сверху над площадкой может стоять объект карты (мост, камень) — такие точки не земля.
					if (Gap < -50.f) { continue; }
					Lo = FMath::Min(Lo, Gap); Hi = FMath::Max(Hi, Gap); ++N;
				}
				R.Pads.Add(Bx);
				// Второй метод (решающий): зазор до НАРИСОВАННОГО пола по глубине кадра.
				const FGapStat Gs = RenderGap(W, PM, HideAll, Bx);
				const float CovFrac = (Gs.N + Gs.Covered) > 0 ? (float)Gs.Covered / (float)(Gs.N + Gs.Covered) : 1.f;
				UE_LOG(LogAsdaCityFloorTest, Log, TEXT("ПЛОЩАДКА-ГОРОДА %s: сетка %s зазор до ВИДИМОГО пола (глубина кадра): мин %.1f, сред %.1f, 95%% %.1f, макс %.1f, пикселей %d, под объектами %d (%.1f%%); калибровка %.1f"),
					*Tag, *PM->GetName(), Gs.Lo, Gs.N ? (float)(Gs.Sum / Gs.N) : NAN, Gs.P95, Gs.Hi, Gs.N, Gs.Covered, CovFrac * 100.f, Gs.Calib);
				Expect(R, Gs.N > 100 && Gs.Calib <= kCalibTol,
					FString::Printf(TEXT("%s: сетка %s — замер глубиной кадра состоялся (пикселей %d, калибровка %.1f, предел %.0f)"), *Tag, *PM->GetName(), Gs.N, Gs.Calib, kCalibTol));
				Expect(R, Gs.N > 100 && Gs.P95 <= kRenderP95 && Gs.Hi <= kRenderHi,
					FString::Printf(TEXT("%s: сетка %s вровень с видимым полом: зазор %.1f…%.1f, 95%% %.1f (ждём ≤ %.0f, 95%% ≤ %.0f)"),
						*Tag, *PM->GetName(), Gs.Lo, Gs.Hi, Gs.P95, kRenderHi, kRenderP95));
				Expect(R, CovFrac <= kCoveredMax,
					FString::Printf(TEXT("%s: сетка %s под объектами карты %.1f%% (предел %.0f%%)"), *Tag, *PM->GetName(), CovFrac * 100.f, kCoveredMax * 100.f));
				UTexture* T = nullptr;
				UMaterialInterface* MI = PM->GetMaterial(0);
				const FString Cur = (MI && MI->GetTextureParameterValue(FName(AsdaMat::kParamTex), T) && T) ? T->GetName() : FString();
				UE_LOG(LogAsdaCityFloorTest, Log, TEXT("ПЛОЩАДКА-ГОРОДА %s: сетка %s центр (%.0f, %.0f, %.0f), проверено вершин %d, зазор до видимой земли %.1f…%.1f, тень=%d, текстура %s"),
					*Tag, *PM->GetName(), Bx.GetCenter().X, Bx.GetCenter().Y, Bx.GetCenter().Z, N, Lo, Hi, PM->CastShadow ? 1 : 0, *Cur);
				// Данные: каждая вершина — ровно на подъём 1 над землёй .TER (та же сетка, по которой ходит персонаж).
				float TerLo = 1e9f, TerHi = -1e9f;
				for (int32 v = 0; Sec && v < Sec->ProcVertexBuffer.Num(); ++v)
				{
					const FVector P = PM->GetComponentTransform().TransformPosition(Sec->ProcVertexBuffer[v].Position);
					const float D = P.Z - AsdaGround::GroundZ(W, P.X, P.Y, P.Z);
					TerLo = FMath::Min(TerLo, D); TerHi = FMath::Max(TerHi, D);
				}
				UE_LOG(LogAsdaCityFloorTest, Log, TEXT("ПЛОЩАДКА-ГОРОДА %s: сетка %s вершин %d, над .TER %.2f…%.2f; по коллизии %.1f…%.1f (справочно)"),
					*Tag, *PM->GetName(), Sec ? Sec->ProcVertexBuffer.Num() : 0, TerLo, TerHi, Lo, Hi);
				Expect(R, Sec && Sec->ProcVertexBuffer.Num() > 0 && TerLo >= 1.f - 0.05f && TerHi <= 1.f + 0.05f,
					FString::Printf(TEXT("%s: сетка %s в плоскости земли .TER: подъём %.2f…%.2f (ждём 1.00)"), *Tag, *PM->GetName(), TerLo, TerHi));
				Expect(R, !PM->CastShadow, FString::Printf(TEXT("%s: сетка %s без тени"), *Tag, *PM->GetName()));
				Expect(R, Cur.Equals(WantTex), FString::Printf(TEXT("%s: сетка %s текстура «%s» (ждём %s)"), *Tag, *PM->GetName(), *Cur, WantTex));
			}
		}
		Expect(R, Patches == ExpectPads, FString::Printf(TEXT("%s: сеток по земле %d (ждём %d — по экземпляру)"), *Tag, Patches, ExpectPads));
		Expect(R, bSrcHidden, FString::Printf(TEXT("%s: исходный плоский квад скрыт"), *Tag));
	}

	// Камера: bSide — сбоку низко (видно зазор), иначе сверху под углом.
	static void Aim(FRun& R, UWorld* W, bool bSide)
	{
		if (!W || !R.Floor.IsValid) { return; }
		ACameraActor* Cam = R.Cam.Get();
		if (!Cam || Cam->GetWorld() != W)
		{
			FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Cam = W->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, SP);
			R.Cam = Cam;
		}
		if (!Cam) { return; }
		const FBox B = R.Pads.IsValidIndex(R.Pad) ? R.Pads[R.Pad] : R.Floor;
		const FVector C = B.GetCenter();
		const float Ext = FMath::Max(B.GetExtent().Size2D(), 150.f);
		// Сбоку — почти вдоль земли: глаз в 60 ед. над площадкой, у её края (зазор под эмблемой виден на просвет).
		const FVector Loc = bSide ? C + FVector(-Ext * 0.95f, -Ext * 0.30f, 60.f)
		                          : C + FVector(-1.f, -0.5f, 1.3f).GetSafeNormal() * Ext * 1.4f;
		Cam->SetActorLocationAndRotation(Loc, (C - Loc).Rotation());
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0)) { PC->SetViewTarget(Cam); }
	}

	static void Shot(FRun& R, const FCity& C, const TCHAR* Step, const TCHAR* Kind)
	{
		const FString File = FPaths::Combine(R.ShotDir, FString::Printf(TEXT("%s_%s_%s_%s.png"), *R.Lang, C.Tag, Step, Kind));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaCityFloorTest, Log, TEXT("ПЛОЩАДКА-ГОРОДА кадр: %s"), *File);
	}

	static void StartCity(FRun& R, UWorld* W)
	{
		UGameInstance* GI = R.GI.Get();
		const FCity& C = kCities[R.City];
		R.OldWorld = W;
		UGameplayStatics::OpenLevel(W, FName(C.Level), true, TEXT("game=/Script/AsdaReborn.AsdaWorldGameMode"));
		if (UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>()) { LM->CurrentMapId = C.MapId; }
		if (UAsdaNetSubsystem* Net = AsdaTestNet::Get(GI)) { Feed(Net, C.Slot, 0); }   // ответ на вход — до мира
		R.Step = 0;
		R.StepAt = FPlatformTime::Seconds();
	}

	static bool OnTick(float)
	{
		if (!GRun.IsValid()) { return false; }
		FRun& R = *GRun;
		UGameInstance* GI = R.GI.Get();
		UWorld* W = GI ? GI->GetWorld() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		const FCity& C = kCities[R.City];
		const double Now = FPlatformTime::Seconds();
		auto Finish = [&R, W]()
		{
			if (ACameraActor* Cam = R.Cam.Get()) { Cam->Destroy(); }
			AsdaSelfTestKit::Finish(W, kCmd, R.bQuit, LogAsdaCityFloorTest, TEXT("ПЛОЩАДКА-ГОРОДА"), R.Ok, R.Fail);
			GRun.Reset();
		};
		if (!Net || !WS) { Expect(R, false, TEXT("подсистемы сети/мира")); Finish(); return false; }
		switch (R.Step)
		{
		case 0:
		{
			const bool bNew = W && W != R.OldWorld.Get() && W->GetMapName().Contains(C.Tag) && W->HasBegunPlay();
			if (!bNew)
			{
				if (Now - R.StepAt < 120.0) { return true; }
				Expect(R, false, FString::Printf(TEXT("%s: мир не поднялся за 120 с"), C.Tag)); Finish(); return false;
			}
			if (Now - R.StepAt < 4.0) { return true; }
			WS->ApplyCityWarTextures();   // тот же вызов, что `RevealWorld` при входе
			Measure(R, W, FString::Printf(TEXT("%s вход после захвата (Свет), исходный квад"), C.Tag));
			MeasurePatches(R, W, FString::Printf(TEXT("%s вход после захвата (Свет)"), C.Tag), R.Instances, TEXT("uniconLightFloor"));
			R.Phase = TEXT("1_enter_light"); R.Pad = 0; R.Shot = 0;
			R.Step = 1; R.StepAt = Now; return true;
		}
		case 1:   // кадры: каждая площадка сбоку, затем сверху
		case 3:
		{
			if (R.Shot % 2 == 0) { if (Now - R.StepAt < 0.2) { return true; } Aim(R, W, R.Shot % 4 == 0); R.Shot++; R.StepAt = Now; return true; }
			if (Now - R.StepAt < 1.5) { return true; }
			Shot(R, C, *FString::Printf(TEXT("%s_pad%d"), *R.Phase, R.Pad), (R.Shot % 4 == 1) ? TEXT("side") : TEXT("top"));
			R.Shot++; R.StepAt = Now;
			if (R.Shot % 4 == 0) { ++R.Pad; }
			if (R.Pad < R.Pads.Num()) { return true; }
			if (R.Step == 1) { R.Step = 10; R.Pad = 0; R.Shot = 0; }   // дальше — кадры на уровне ботинок
			else { R.Step = 4; }   // пауза: последний кадр снимается в конце ЭТОГО кадра движка
			return true;
		}
		case 10:   // ★ 10.11: свой персонаж на краю каждой эмблемы, камера сбоку на высоте ботинок (жалоба: «режет ботинки»)
		{
			if (R.Pad >= R.Pads.Num()) { if (Now - R.StepAt < 1.5) { return true; } R.Step = 2; R.StepAt = Now; return true; }
			const FBox B = R.Pads[R.Pad];
			if (R.Shot == 0)
			{
				if (Now - R.StepAt < 0.3) { return true; }
				// Точка внутри эмблемы у ближнего края (−X): ботинки стоят НА эмблеме, за ними — край и пол вокруг.
				const FVector P(B.Min.X + (B.Max.X - B.Min.X) * 0.08f, B.GetCenter().Y, B.GetCenter().Z);
				const FIntPoint T = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(P));
				if (GEngine) { GEngine->Exec(W, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn %d %d"), T.X, T.Y)); }
				R.Shot = 1; R.StepAt = Now; return true;
			}
			if (R.Shot == 1)
			{
				if (Now - R.StepAt < 2.5) { return true; }
				AActor* Self = IsValid(WS->LocalPlayerActor) ? WS->LocalPlayerActor.Get() : nullptr;
				Expect(R, Self != nullptr, FString::Printf(TEXT("%s: свой персонаж у эмблемы %d (кадр у ботинок)"), C.Tag, R.Pad));
				if (Self)
				{
					const FVector F = Self->GetActorLocation();
					const float G = AsdaGround::GroundZ(W, F.X, F.Y, F.Z);
					ACameraActor* Cam = R.Cam.Get();
					if (!Cam || Cam->GetWorld() != W)
					{
						FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
						Cam = W->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, SP); R.Cam = Cam;
					}
					if (Cam)
					{
						// Сбоку, чуть сзади края: глаз на 10 ед. над полом (уровень щиколотки), смотрит на ботинки.
						const FVector Eye(F.X - 120.f, F.Y - 75.f, G + 10.f);
						Cam->SetActorLocationAndRotation(Eye, (FVector(F.X, F.Y, G + 8.f) - Eye).Rotation());
						if (APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0)) { PC->SetViewTarget(Cam); }
					}
					UE_LOG(LogAsdaCityFloorTest, Log, TEXT("ПЛОЩАДКА-ГОРОДА %s: персонаж у эмблемы %d в (%.0f, %.0f, %.1f), .TER %.1f"), C.Tag, R.Pad, F.X, F.Y, F.Z, G);
				}
				R.Shot = 2; R.StepAt = Now; return true;
			}
			if (Now - R.StepAt < 1.5) { return true; }
			Shot(R, C, *FString::Printf(TEXT("%s_pad%d"), *R.Phase, R.Pad), TEXT("boots"));
			R.Shot = 0; ++R.Pad; R.StepAt = Now;
			return true;
		}
		case 2:
		case 4:
		{
			if (Now - R.StepAt < 1.5) { return true; }
			if (R.Step == 2)
			{
				Feed(Net, C.Slot, 1);   // смена хозяина на глазах
				MeasurePatches(R, W, FString::Printf(TEXT("%s смена хозяина (Тьма)"), C.Tag), R.Instances, TEXT("uniconDarkFloor"));
				R.Phase = TEXT("2_live_dark"); R.Pad = 0; R.Shot = 0; R.Step = 3; R.StepAt = Now;
			}
			else { R.Step = 5; R.StepAt = Now; }
			return true;
		}
		default:
			if (Now - R.StepAt < 1.0) { return true; }
			if (++R.City >= UE_ARRAY_COUNT(kCities)) { Finish(); return false; }
			StartCity(R, W);
			return true;
		}
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет GameInstance")); return; }
		if (GRun.IsValid()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("самотест уже идёт")); return; }
		GRun = MakeShared<FRun>();
		FRun& R = *GRun;
		R.GI = GI; R.bQuit = Args.bQuit;
		R.Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
		// Пара «до/после»: при `asda.War.FloorConform 0` кадры помечаются «_key0» (прежний плоский квад).
		if (const IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.War.FloorConform")))
		{
			if (Cv->GetInt() == 0) { R.Lang += TEXT("_key0"); }
		}
		R.ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch21/city_floor")));
		R.City = 0;
		StartCity(R, W);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&OnTick), 0.1f);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCityWarFloor(
	TEXT("asda.SelfTest.CityWarFloor"),
	TEXT("Самотест: эмблема захвата города (площадка UNICON03) лежит на земле в Силарисе, Альпии и Пламении — при входе после "
	     "захвата и при смене хозяина; замер зазора до .TER и рендер-террейна, кадры в Reports/batch21/city_floor. Только офлайн. [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCityWarFloorSelfTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
