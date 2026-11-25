// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ SELFTEST: КЛИК ПО NPC ВЫБИРАЕТ ТОГО, ПО КОМУ ЩЁЛКНУЛИ — `asda.SelfTest.NpcClickPick map=<id> [victims=131,146] [shot] [quit]`
//   (08.11.2026, пачка 19 баг 1: щелчок по Бумибуми (NPC 131, Альпия) выбирал PrePre-v19 (NPC 146, робот в 200 ед.); правило и
//   разбор эталона — блоки над `CVarNpcClickBoxOriginal` (AsdaNpcActor.cpp) и `CVarAsdaNpcClickByBody` (AsdaPlayerController.cpp); замена тела —
//   Public/Npc/AsdaNpcBodyRay.h, таблица — генератор `asda.Npc.GenPickBones` ниже).
//
//   Офлайн (`-AsdaNoNet`), на запечённом уровне карты `map`. Все NPC уровня «удочеряются» настоящей записью 4067 (`DebugFeedPacket`),
//   затем при ключах `asda.Npc.ClickBoxOriginal` + `asda.Npc.ClickByBody` = 0 (OLD — прежнее: капсула по `Mesh->Bounds`, ближайшая
//   капсула) и = 1 (NEW — капсула класса 0 = короб оригинала, щелчок по телу):
//     M  замер: класс, высота npc_list, капсула (центр, радиус, полувысота), габариты модели (ассет) и `Mesh->Bounds` (физ-ассет).
//     S  силуэты: для каждого NPC, у которого есть сосед ближе kPair, 72 ракурса (8 сторон × свой персонаж в 400/800/1200 ед. ×
//        наклон камеры −20/−45/−5, рука 700), экранная сетка по коробу его модели. ОРАКУЛ — настоящая геометрия: вершины моделей
//        NPC, скинованные на ЦП в текущую позу (`FSkeletalMeshLODModel` редактора + `CacheRefToLocalMatrices`), луч против
//        треугольников; карта — трейс без NPC. Пиксель «виден A» = ближайший треугольник луча принадлежит A и ближе карты. Факт —
//        луч клика порта (`TraceScreenSkippingUnrenderedChars`, тот же, что `PickForClick`): A — верно, другой NPC — «украден»,
//        иначе промах. Кража у края силуэта вора (kRimPx пикс. / kRimUnits ед.) — «кайма» (точность руки и замены тела).
//        NEW: со всех видимых ракурсов щелчок по модели выбирает её, промахов ≤ kMissShare, краж вне каймы на пару
//        ≤ max(kStealAllowPx, kStealAllowShare видимых) — FAIL на каждую пару сверх.
//     C  настоящие щелчки мыши (курсор и событие Slate → вьюпорт → `UPlayerInput` → `OnLeftClick`; если вьюпорт позицию не
//        принял — `OnLeftClick` с той же экранной точкой, путь пишется в журнал) по пикселю тела жертвы, который при OLD забирал
//        сосед: OLD — выбирается вор (воспроизведение бага), NEW — жертва (`TargetActor`). Жертвы: `victims=` и до kAutoClicks
//        пар с наибольшей кражей при OLD. `shot` — кадры с кольцом цели в `Reports/batch19/npc_click/`.
//   Строки «NPC-CLICK ok/FAIL», «NPC-CLICK pair …» (список пар для отчёта), итог «NPC-CLICK ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING && WITH_EDITORONLY_DATA

#include "Npc/AsdaNpcActor.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "Npc/AsdaNpcBodyRay.h"                // разбор краж: тело вора и жертвы
#include "AsdaNetSubsystem.h"                   // DebugFeedPacket — 4067 через настоящий приёмник
#include "AsdaBytes.h"                          // поля записи 4067
#include "AsdaCVarSave.h"                       // ArmWorldGuard
#include "Text/AsdaLang.h"                      // язык прогона — в имя кадра
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaSkillCastLock.h"            // DebugWorldClick — мировой клик с экранной точкой (запасной путь)
#include "World/AsdaGroundTrace.h"
#include "World/AsdaCoord.h"
#include "World/AsdaWalkGrid.h"
#include "Test/AsdaSelfTestKit.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UnrealClient.h"                       // FScreenshotRequest
#include "Framework/Application/SlateApplication.h"
#include "AssetRegistry/AssetRegistryModule.h"   // генератор таблицы коробок костей
#include "Misc/FileHelper.h"
#include "Algo/Sort.h"
#include "Widgets/SViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaNpcClickPick, Log, All);

namespace AsdaNpcClickPickTest
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.NpcClickPick");   // команда
	static const TCHAR* const kPrefix = TEXT("NPC-CLICK");                 // префикс журнала
	static const TCHAR* const kKey = TEXT("asda.Npc.ClickBoxOriginal");    // проверяемый ключ: капсула класса 0 = короб оригинала
	static const TCHAR* const kKeyBody = TEXT("asda.Npc.ClickByBody");     // проверяемый ключ: щелчок по телу NPC
	static constexpr int32 kViews = 72;            // ракурсы: 8 сторон × дистанции {400, 800, 1200} × наклон камеры {−20, −45, −5}
	static constexpr double kRimPx = 3.0;          // кража в пределах стольких пикселей от силуэта вора — «кайма» (точность руки), не FAIL
	static constexpr int32 kStealAllowPx = 3;      // NEW: допуск краж вне каймы на пару — max(3 px, 1% видимых пикселей жертвы) — разброс позы
	static constexpr double kStealAllowShare = 0.01;
	static constexpr double kRimUnits = 6.0;       // …или в стольких ед. от поверхности вора — точность замены тела (коробки листов, `asda.Npc.GenPickBones`)
	static constexpr float kStand = 400.f;         // расстояние своего персонажа от жертвы, ед.
	static constexpr float kPair = 700.f;          // соседи ближе этого (по земле) — пара для скана
	static constexpr float kStep = 5.f;            // шаг экранной сетки, пикс.
	static constexpr float kMissShare = 0.25f;     // NEW: доля видимых пикселей, которые могут не ловиться (руки шире короба)
	static constexpr int32 kAutoClicks = 5;        // настоящих щелчков по парам с наибольшей кражей
	static constexpr int32 kRidBase = 8600;        // renderId удочерений самотеста

	// Скинованная геометрия NPC в мире.
	struct FGeo { TArray<FVector> V; TArray<uint32> I; FBox Box = FBox(ForceInit); };
	// Пара «жертва ← вор».
	struct FPair { int32 Victim = 0, Thief = 0; float Dist = 0.f; int32 VictimVis = 0; int32 OldRim = 0, NewRim = 0; int32 OldPx = 0, NewPx = 0, OldSides = 0, NewSides = 0; FVector2D OldPxAt = FVector2D(-1, -1); int32 OldSide = -1; };
	// Итог скана одного NPC при одном значении ключа.
	struct FScan { int32 BodyMiss = 0; int32 Visible = 0, Ok = 0, Miss = 0, Sides = 0, SidesOk = 0; TMap<int32, int32> Rim; TMap<int32, int32> Stolen; TMap<int32, int32> StolenSides; TMap<int32, TPair<int32, FVector2D>> StolenAt; };

	struct FCtx
	{
		TWeakObjectPtr<UWorld> W;                          // мир прогона
		bool bQuit = false, bShot = false;                 // выйти в конце / кадры
		int32 MapId = -1;                                  // карта
		int32 Ok = 0, Fail = 0;                            // счётчики
		int32 KeyAtStart = 1;                              // значение ключа на старте (вернуть)
		TArray<int32> Victims;                             // `victims=`
		TArray<TWeakObjectPtr<AAsdaNpcActor>> All;         // все удочерённые NPC уровня
		TArray<TWeakObjectPtr<AAsdaNpcActor>> Targets;     // NPC с соседом ближе kPair
		TArray<FPair> Pairs;                               // кражи
		int32 Next = 0;                                    // следующая цель скана
		TArray<TTuple<int32, int32, int32, FVector2D>> Clicks; // щелчки: жертва, вор, сторона, пиксель
		int32 NextClick = 0;                               // следующий щелчок
		int32 DiagLeft = 0;                                // сколько краж ещё разобрать в журнале (NEW)
		FVector2D StartGame = FVector2D::ZeroVector;       // своя стартовая точка

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaNpcClickPick, Log, TEXT("%s ok   %s"), kPrefix, *What); }
			else { ++Fail; UE_LOG(LogAsdaNpcClickPick, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		void Info(const FString& What) const { UE_LOG(LogAsdaNpcClickPick, Log, TEXT("%s info %s"), kPrefix, *What); }
		template <class T> T* Sub() const { return W.IsValid() && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<T>() : nullptr; }
		AAsdaPlayerController* Pc() const { return W.IsValid() ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = Pc(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AAsdaNpcActor* ById(int32 Id) const { for (const TWeakObjectPtr<AAsdaNpcActor>& A : All) { if (A.IsValid() && A->NpcId == Id) { return A.Get(); } } return nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	static FString Name(const AAsdaNpcActor* A) { return A ? FString::Printf(TEXT("%d«%s»"), A->NpcId, *A->NpcName.Left(24)) : FString(TEXT("-")); }

	static void SetKey(int32 V)
	{
		if (IConsoleVariable* X = IConsoleManager::Get().FindConsoleVariable(kKey)) { X->Set(V, ECVF_SetByConsole); }
		if (IConsoleVariable* X = IConsoleManager::Get().FindConsoleVariable(kKeyBody)) { X->Set(V, ECVF_SetByConsole); }
	}

	// OLD — оба ключа 0 (прежняя капсула по `Mesh->Bounds`, ближайшая капсула), NEW — оба 1; зона пересчитывается `ApplyStaticPickBox`.
	static void Mode(const FCtxRef& C, bool bNew)
	{
		SetKey(bNew ? 1 : 0);
		for (const TWeakObjectPtr<AAsdaNpcActor>& WA : C->All) { if (AAsdaNpcActor* A = WA.Get()) { A->ApplyStaticPickBox(); } }
	}

	// ── ОРАКУЛ: вершины модели в текущей позе (ЦП-скиннинг по данным импорта редактора) ──
	static bool Skin(const AAsdaNpcActor* A, FGeo& Out)
	{
		Out = FGeo();
		const USkeletalMeshComponent* Mc = A ? A->Mesh.Get() : nullptr;
		const USkeletalMesh* SK = Mc ? Mc->GetSkeletalMeshAsset() : nullptr;
		FSkeletalMeshModel* Model = SK ? SK->GetImportedModel() : nullptr;
		if (!Model || Model->LODModels.Num() == 0) { return false; }
		const FSkeletalMeshLODModel& L = Model->LODModels[0];
		TArray<FMatrix44f> R2L;
		Mc->CacheRefToLocalMatrices(R2L);
		const FTransform& T = Mc->GetComponentTransform();
		Out.V.SetNumZeroed((int32)L.NumVertices);
		TArray<uint8> Live; Live.SetNumZeroed((int32)L.NumVertices);   // вершина видимой секции
		for (int32 s = 0; s < L.Sections.Num(); ++s)
		{
			const FSkelMeshSection& S = L.Sections[s];
			if (S.bDisabled) { continue; }
			for (int32 v = 0; v < S.SoftVertices.Num(); ++v)
			{
				const FSoftSkinVertex& Sv = S.SoftVertices[v];
				FVector3f P(0.f);
				float Ws = 0.f;
				for (int32 k = 0; k < MAX_TOTAL_INFLUENCES; ++k)
				{
					const float Wt = (float)Sv.InfluenceWeights[k];
					if (Wt <= 0.f || Sv.InfluenceBones[k] >= S.BoneMap.Num()) { continue; }
					const int32 B = S.BoneMap[Sv.InfluenceBones[k]];
					if (!R2L.IsValidIndex(B)) { continue; }
					P += R2L[B].TransformPosition(Sv.Position) * Wt;
					Ws += Wt;
				}
				P = (Ws > 0.f) ? P / Ws : Sv.Position;
				const int32 Gi = (int32)S.BaseVertexIndex + v;
				if (!Out.V.IsValidIndex(Gi)) { continue; }
				Out.V[Gi] = T.TransformPosition(FVector(P));
				Live[Gi] = 1;
				Out.Box += Out.V[Gi];
			}
			for (uint32 t = 0; t < S.NumTriangles; ++t)
			{
				const uint32 Bi = S.BaseIndex + t * 3;
				if (Bi + 2 >= (uint32)L.IndexBuffer.Num()) { break; }
				const uint32 I0 = L.IndexBuffer[Bi], I1 = L.IndexBuffer[Bi + 1], I2 = L.IndexBuffer[Bi + 2];
				if ((int32)I0 >= Live.Num() || (int32)I1 >= Live.Num() || (int32)I2 >= Live.Num() || !Live[I0] || !Live[I1] || !Live[I2]) { continue; }
				Out.I.Add(I0); Out.I.Add(I1); Out.I.Add(I2);
			}
		}
		return Out.I.Num() > 0;
	}
	// Луч против треугольников (Мёллер–Трумбор, обе стороны): ближайшее t > 0 или −1.
	static double RayGeo(const FGeo& G, const FVector& O, const FVector& D)
	{
		if (!G.Box.IsValid || !FMath::LineBoxIntersection(G.Box.ExpandBy(1.0), O, O + D * 100000.0, D * 100000.0)) { return -1.0; }
		double Best = -1.0;
		for (int32 i = 0; i + 2 < G.I.Num(); i += 3)
		{
			const FVector& A = G.V[G.I[i]]; const FVector& B = G.V[G.I[i + 1]]; const FVector& Cc = G.V[G.I[i + 2]];
			const FVector E1 = B - A, E2 = Cc - A;
			const FVector Pv = FVector::CrossProduct(D, E2);
			const double Det = FVector::DotProduct(E1, Pv);
			if (FMath::Abs(Det) < 1e-9) { continue; }
			const double Inv = 1.0 / Det;
			const FVector Tv = O - A;
			const double U = FVector::DotProduct(Tv, Pv) * Inv;
			if (U < 0.0 || U > 1.0) { continue; }
			const FVector Qv = FVector::CrossProduct(Tv, E1);
			const double V = FVector::DotProduct(D, Qv) * Inv;
			if (V < 0.0 || U + V > 1.0) { continue; }
			const double Tt = FVector::DotProduct(E2, Qv) * Inv;
			if (Tt > 0.0 && (Best < 0.0 || Tt < Best)) { Best = Tt; }
		}
		return Best;
	}

	// Стойка в 8 сторонах (45°) на проходимой клетке.
	static float ViewPitch(int32 v) { const int32 p = v / 24; return p == 0 ? -20.f : (p == 1 ? -45.f : -5.f); }
	static bool StandAt(const FCtxRef& C, const FVector& O, int32 v, FVector& Out, float& LookYaw)
	{
		UWorld* W = C->W.Get();
		const float Yaw = 45.f * (v % 8);
		const FVector P = O + FRotator(0.f, Yaw, 0.f).Vector() * (kStand * (1 + (v / 8) % 3));
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
	// Свой персонаж в Stand, камера игры (рука 700, наклон −20) по LookYaw.
	static void Place(const FCtxRef& C, const FVector& Stand, float LookYaw, float Pitch = -20.f)
	{
		AAsdaPlayerCharacter* Ch = C->Ch();
		AAsdaPlayerController* P = C->Pc();
		if (!Ch || !P) { return; }
		Ch->StopMove();
		P->CancelRoute();
		Ch->SetActorLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
		P->SetOrbitAnglesForBench(FRotator::NormalizeAxis(LookYaw - (float)Ch->GetActorRotation().Yaw), Pitch);
		if (Ch->CameraArm) { Ch->CameraArm->TargetArmLength = 700.f; }
		if (P->PlayerCameraManager) { P->PlayerCameraManager->UpdateCamera(0.f); }
	}

	// Соседи A ближе Radius (показанные): kPair — пара для скана, kOracle — геометрия, которая может заслонить A.
	static constexpr float kOracle = 100000.f;   // вся карта
	static TArray<AAsdaNpcActor*> NeighboursOf(const FCtxRef& C, const AAsdaNpcActor* A, float Radius = kPair)
	{
		TArray<AAsdaNpcActor*> Out;
		for (const TWeakObjectPtr<AAsdaNpcActor>& B : C->All)
		{
			if (B.IsValid() && B.Get() != A && !B->IsHidden() && FVector::Dist2D(B->GetActorLocation(), A->GetActorLocation()) <= Radius) { Out.Add(B.Get()); }
		}
		return Out;
	}

	// Видит ли оракул A в пикселе Px: true и кто ближе всех (Who) — если ближайшая геометрия луча NPC и ближе карты.
	static AAsdaNpcActor* OracleRay(const FCtxRef& C, const FVector& O, const FVector& D, const TArray<TPair<AAsdaNpcActor*, FGeo>>& Geos);
	static AAsdaNpcActor* OracleAt(const FCtxRef& C, const FVector2D& Px, const TArray<TPair<AAsdaNpcActor*, FGeo>>& Geos)
	{
		AAsdaPlayerController* P = C->Pc();
		FVector O, D;
		if (!P || !P->DeprojectScreenPositionToWorld(Px.X, Px.Y, O, D)) { return nullptr; }
		return OracleRay(C, O, D, Geos);
	}
	static AAsdaNpcActor* OracleRay(const FCtxRef& C, const FVector& O, const FVector& D, const TArray<TPair<AAsdaNpcActor*, FGeo>>& Geos)
	{
		AAsdaPlayerController* P = C->Pc();
		if (!P) { return nullptr; }
		AAsdaNpcActor* Who = nullptr;
		double Best = -1.0;
		for (const TPair<AAsdaNpcActor*, FGeo>& G : Geos)
		{
			const double T = RayGeo(G.Value, O, D);
			if (T > 0.0 && (Best < 0.0 || T < Best)) { Best = T; Who = G.Key; }
		}
		if (!Who) { return nullptr; }
		// Карта: трейс без NPC и без своего персонажа.
		FCollisionQueryParams Q(SCENE_QUERY_STAT(AsdaNpcClickOracle), true);
		for (const TWeakObjectPtr<AAsdaNpcActor>& B : C->All) { if (B.IsValid()) { Q.AddIgnoredActor(B.Get()); } }
		if (C->Ch()) { Q.AddIgnoredActor(C->Ch()); }
		FHitResult H;
		if (P->GetWorld()->LineTraceSingleByChannel(H, O, O + D * 100000.0, ECC_Visibility, Q) && H.Distance + 1.0 < Best) { return nullptr; }
		return Who;
	}

	// Кайма: силуэт T не дальше kRimPx от пикселя или его поверхность не дальше kRimUnits от луча (параллельные лучи в 8 сторонах).
	static bool NearThief(const FCtxRef& C, const FVector2D& Px, const TArray<TPair<AAsdaNpcActor*, FGeo>>& Geos, const AAsdaNpcActor* T)
	{
		AAsdaPlayerController* P = C->Pc();
		if (!P || !T) { return false; }
		for (int32 k = 0; k < 8; ++k)
		{
			if (OracleAt(C, Px + FVector2D(FMath::Cos(k * PI / 4), FMath::Sin(k * PI / 4)) * kRimPx, Geos) == T) { return true; }
		}
		FVector Ro, Rd;
		if (!P->DeprojectScreenPositionToWorld(Px.X, Px.Y, Ro, Rd)) { return false; }
		FVector Ax, Ay; Rd.FindBestAxisVectors(Ax, Ay);
		for (int32 k = 0; k < 8; ++k)
		{
			if (OracleRay(C, Ro + (Ax * FMath::Cos(k * PI / 4) + Ay * FMath::Sin(k * PI / 4)) * kRimUnits, Rd, Geos) == T) { return true; }
		}
		return false;
	}

	// Скан одного NPC (все 8 сторон) при текущем режиме.
	static FScan ScanTarget(const FCtxRef& C, AAsdaNpcActor* A, const TArray<AAsdaNpcActor*>& Nb)
	{
		FScan R;
		AAsdaPlayerController* P = C->Pc();
		AAsdaPlayerCharacter* Me = C->Ch();
		if (!P || !Me) { return R; }
		int32 VX = 0, VY = 0; P->GetViewportSize(VX, VY);
		const bool bMeCol = Me->GetActorEnableCollision();
		for (int32 i = 0; i < kViews; ++i)
		{
			FVector St; float Yaw = 0.f;
			if (!StandAt(C, A->GetActorLocation(), i, St, Yaw)) { continue; }
			Place(C, St, Yaw, ViewPitch(i));
			Me->SetActorEnableCollision(false);
			TArray<TPair<AAsdaNpcActor*, FGeo>> Geos;
			{ FGeo G; if (Skin(A, G)) { Geos.Emplace(A, MoveTemp(G)); } }
			if (Geos.Num() == 0) { Me->SetActorEnableCollision(bMeCol); continue; }
			for (AAsdaNpcActor* N : Nb) { FGeo G; if (Skin(N, G)) { Geos.Emplace(N, MoveTemp(G)); } }
			const FBox AB = Geos[0].Value.Box;
			FVector2D Mn(1e9, 1e9), Mx(-1e9, -1e9);
			bool bAny = false;
			for (int32 k = 0; k < 8; ++k)
			{
				const FVector Cn((k & 1) ? AB.Max.X : AB.Min.X, (k & 2) ? AB.Max.Y : AB.Min.Y, (k & 4) ? AB.Max.Z : AB.Min.Z);
				FVector2D S;
				if (P->ProjectWorldLocationToScreen(Cn, S)) { Mn = FVector2D::Min(Mn, S); Mx = FVector2D::Max(Mx, S); bAny = true; }
			}
			int32 Vis = 0, Hit = 0;
			TSet<int32> ThievesHere;
			if (bAny)
			{
				for (double y = FMath::Max(2.0, Mn.Y); y <= FMath::Min((double)VY - 2, Mx.Y); y += kStep)
				{
					for (double x = FMath::Max(2.0, Mn.X); x <= FMath::Min((double)VX - 2, Mx.X); x += kStep)
					{
						const FVector2D Px(x, y);
						if (OracleAt(C, Px, Geos) != A) { continue; }
						++Vis;
						// Полнота замены тела: модель видна, а её коробки/треугольники (`AsdaNpcBodyRay::RayBody`) луч не задевают.
						{
							FVector Bo, Bd; double Bt = 0.0;
							if (P->DeprojectScreenPositionToWorld(Px.X, Px.Y, Bo, Bd) && !AsdaNpcBodyRay::RayBody(A->Mesh, Bo, Bd, Bt)) { ++R.BodyMiss; }
						}
						FHitResult H;
						AActor* Got = P->TraceScreenSkippingUnrenderedChars(Px, H) ? H.GetActor() : nullptr;
						if (Got == A) { ++Hit; continue; }
						if (const AAsdaNpcActor* T = Cast<AAsdaNpcActor>(Got))
						{
							// Кайма: силуэт вора в kRimPx от пикселя — граница двух силуэтов, щелчок там неоднозначен и для глаза.
							const bool bRim = NearThief(C, Px, Geos, T);
							if (bRim) { R.Rim.FindOrAdd(T->NpcId)++; continue; }
							// Разбор кражи (первые 12 за прогон): чьё тело/капсула и какая кость перехватили луч.
							if (C->DiagLeft > 0)
							{
								--C->DiagLeft;
								FVector O, Dd; P->DeprojectScreenPositionToWorld(Px.X, Px.Y, O, Dd);
								double Tb = -1.0, Ta = -1.0; FName Bone, BoneA; const int32 Kind = 0, KindA = 0;
								const bool bBody = AsdaNpcBodyRay::RayBody(T->Mesh, O, Dd, Tb, &Bone);
								const bool bBodyA = AsdaNpcBodyRay::RayBody(A->Mesh, O, Dd, Ta, &BoneA);
								const double Tg = RayGeo(Geos[0].Value, O, Dd);
								double Tt = -2.0; int32 TriT = -1; FVector GeoC = FVector::ZeroVector, GeoE = FVector::ZeroVector;
								double MinV = 1e9;   // ближайшая к лучу вершина вора (ед.)
								for (const TPair<AAsdaNpcActor*, FGeo>& G : Geos)
								{
									if (G.Key != T) { continue; }
									Tt = RayGeo(G.Value, O, Dd); TriT = G.Value.I.Num() / 3; GeoC = G.Value.Box.GetCenter() - T->GetActorLocation(); GeoE = G.Value.Box.GetExtent();
									for (const FVector& V : G.Value.V) { const double Al = FVector::DotProduct(V - O, Dd); if (Al > 0) { MinV = FMath::Min(MinV, FVector::Dist(O + Dd * Al, V)); } }
								}
								C->Info(FString::Printf(TEXT("  ближайшая вершина вора к лучу %.1f ед."), MinV));
								C->Info(FString::Printf(TEXT("  геометрия вора %.0f (треугольников оракула %d, короб от NPC (%.0f,%.0f,%.0f) ½ (%.0f,%.0f,%.0f))"), Tt, TriT, GeoC.X, GeoC.Y, GeoC.Z, GeoE.X, GeoE.Y, GeoE.Z));
								C->Info(FString::Printf(TEXT("кража %s ← %s ракурс %d пикс (%.0f,%.0f): компонент %s; тело вора %s %.0f (кость %s, форма %d); тело жертвы %s %.0f (кость %s, форма %d); геометрия жертвы %.0f"),
									*Name(A), *Name(T), i, Px.X, Px.Y, H.GetComponent() ? *H.GetComponent()->GetName() : TEXT("-"),
									bBody ? TEXT("да") : TEXT("нет"), Tb, *Bone.ToString(), Kind, bBodyA ? TEXT("да") : TEXT("нет"), Ta, *BoneA.ToString(), KindA, Tg));
							}
							R.Stolen.FindOrAdd(T->NpcId)++;
							ThievesHere.Add(T->NpcId);
							TPair<int32, FVector2D>& At0 = R.StolenAt.FindOrAdd(T->NpcId, TPair<int32, FVector2D>(-1, FVector2D(-1, -1)));
							if (At0.Key < 0) { At0 = TPair<int32, FVector2D>(i, Px); }
						}
						else { ++R.Miss; }
					}
				}
			}
			Me->SetActorEnableCollision(bMeCol);
			for (int32 Id : ThievesHere) { R.StolenSides.FindOrAdd(Id)++; }
			if (Vis > 0) { ++R.Sides; if (Hit > 0) { ++R.SidesOk; } }
			R.Visible += Vis; R.Ok += Hit;
		}
		return R;
	}

	static void Finish(const FCtxRef& C)
	{
		AsdaSkillCastLock::DebugSetClickPoint(C->Pc(), nullptr);   // R5-fix8: подменённую точку клика не оставлять контроллеру (обрыв прогона)
		for (const TWeakObjectPtr<AAsdaNpcActor>& WA : C->All) { if (WA.IsValid() && WA->Mesh) { WA->Mesh->bPauseAnims = false; } }
		SetKey(C->KeyAtStart);
		Mode(C, C->KeyAtStart != 0);
		if (AAsdaPlayerController* P = C->Pc()) { P->ClearTarget(); }
		if (AAsdaPlayerCharacter* Ch = C->Ch()) { Ch->StopMove(); Ch->NetSnap(C->StartGame); }
		const FString Tail = FString::Printf(TEXT(" map=%d npcs=%d targets=%d pairs=%d"), C->MapId, C->All.Num(), C->Targets.Num(), C->Pairs.Num());
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaNpcClickPick, kPrefix, C->Ok, C->Fail, *Tail);
	}

	// ── C: настоящий щелчок мыши (Slate) ──
	static TSharedPtr<SViewport> GameVp() { return (GEngine && GEngine->GameViewport) ? GEngine->GameViewport->GetGameViewportWidget() : nullptr; }
	static void Mouse(const FVector2D& Px, int32 Kind)   // 0 — нажатие, 1 — отпускание, 3 — движение
	{
		const TSharedPtr<SViewport> Vp = GameVp();
		if (!Vp.IsValid()) { return; }
		const FGeometry& G = Vp->GetCachedGeometry();
		const FVector2D Abs = FVector2D(G.LocalToAbsolute(FVector2f(Px / FMath::Max(G.Scale, 0.01f))));
		TSet<FKey> Pressed;
		if (Kind == 0) { Pressed.Add(EKeys::LeftMouseButton); }
		const FPointerEvent Ev(0, FSlateApplicationBase::CursorPointerIndex, Abs, Abs, Pressed, Kind == 3 ? EKeys::Invalid : EKeys::LeftMouseButton, 0.f, FModifierKeysState());
		if (Kind == 3)
		{
			// Движение: курсор платформы и маршрут Slate (обновляют позицию мыши вьюпорта), затем обработчик вьюпорта напрямую.
			FSlateApplication& App = FSlateApplication::Get();
			App.SetCursorPos(Abs);
			App.ProcessMouseMoveEvent(Ev);
			Vp->OnMouseMove(G, Ev);
		}
		else if (Kind == 0) { Vp->OnMouseButtonDown(G, Ev); }
		else { Vp->OnMouseButtonUp(G, Ev); }
	}
	static void Shot(const FCtxRef& C, int32 Victim, int32 Side, const TCHAR* Mode)
	{
		if (!C->bShot || !C->W.IsValid()) { return; }
		const FString Map = C->W->GetMapName().Replace(TEXT("UEDPIE_0_"), TEXT(""));
		const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / FString::Printf(
			TEXT("../Reports/batch19/npc_click/%s_%s_npc%d_side%d_%s.png"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en"), *Map, Victim, Side, Mode));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		C->Info(FString::Printf(TEXT("кадр %s"), *File));
	}

	static void NextClick(const FCtxRef& C);
	// Один щелчок (OLD или NEW) по пикселю Px со стороны Side: ждём кадр, нажатие/отпускание, проверка цели.
	static void ClickOnce(const FCtxRef& C, int32 Victim, int32 Thief, int32 Side, FVector2D Px, bool bNew)
	{
		AAsdaNpcActor* A = C->ById(Victim);
		AAsdaNpcActor* T = C->ById(Thief);
		AAsdaPlayerController* P = C->Pc();
		AAsdaPlayerCharacter* Me = C->Ch();
		if (!A || !P || !Me) { ++C->NextClick; NextClick(C); return; }
		Mode(C, bNew);
		FVector St; float Yaw = 0.f;
		if (!StandAt(C, A->GetActorLocation(), Side, St, Yaw)) { ++C->NextClick; NextClick(C); return; }
		Place(C, St, Yaw, ViewPitch(Side));
		P->ClearTarget();
		Me->SetActorEnableCollision(false);
		// Пиксель силуэта (оракул) — заново, поза могла сдвинуться: ищем ближайший к Px пиксель, где оракул видит жертву.
		TArray<TPair<AAsdaNpcActor*, FGeo>> Geos;
		{ FGeo G; if (Skin(A, G)) { Geos.Emplace(A, MoveTemp(G)); } }
		for (AAsdaNpcActor* N : NeighboursOf(C, A, kOracle)) { FGeo G; if (Skin(N, G)) { Geos.Emplace(N, MoveTemp(G)); } }
		FVector2D Use(-1, -1);
		// Пиксель тела жертвы вне каймы соседа (поза могла сдвинуться со скана — ищем рядом с Px, до 30 px):
		//   OLD — где луч клика забирает сосед (воспроизведение бага); NEW — тот же пиксель (если ещё годится) или ближайший такой же.
		for (int32 r = 0; r <= 30 && Use.X < 0; r += 2)
		{
			for (int32 dy = -r; dy <= r && Use.X < 0; dy += 2) { for (int32 dx = -r; dx <= r && Use.X < 0; dx += 2)
			{
				if (FMath::Max(FMath::Abs(dx), FMath::Abs(dy)) != r) { continue; }   // только кольцо радиуса r
				const FVector2D Q(Px.X + dx, Px.Y + dy);
				if (OracleAt(C, Q, Geos) != A) { continue; }
				bool bInside = true;   // щелчок «по телу», не по краю: жертва видна и в kRimPx вокруг
				for (int32 k = 0; k < 8 && bInside; ++k) { bInside = OracleAt(C, Q + FVector2D(FMath::Cos(k * PI / 4), FMath::Sin(k * PI / 4)) * kRimPx, Geos) == A; }
				if (!bInside) { continue; }
				FHitResult H;
				AActor* Got = P->TraceScreenSkippingUnrenderedChars(Q, H) ? H.GetActor() : nullptr;
				const AAsdaNpcActor* GotN = Cast<AAsdaNpcActor>(Got);
				if (!bNew && (Got == A || !GotN)) { continue; }
				if (NearThief(C, Q, Geos, bNew ? T : GotN)) { continue; }
				Use = Q;
			} }
		}
		if (Use.X < 0) { Use = Px; }
		const FString Tag = FString::Printf(TEXT("щелчок %s по %s сторона %d пиксель (%.0f,%.0f), сосед %s"), bNew ? TEXT("NEW") : TEXT("OLD"), *Name(A), Side, Use.X, Use.Y, *Name(T));
		At(C, 0.15f, [C, Victim, Thief, Side, Use, bNew, Tag]()
		{
			AAsdaPlayerController* P2 = C->Pc();
			if (!P2) { ++C->NextClick; NextClick(C); return; }
			Mouse(Use, 3);
			At(C, 0.1f, [C, Victim, Thief, Side, Use, bNew, Tag]()
			{
			// Позиция мыши контроллера после движения Slate: совпала — нажатие тем же путём Slate; нет (в `-RenderOffscreen` вьюпорт
			//   иногда не принимает позицию) — тот же мировой клик `OnLeftClick` с экранной точкой (`DebugWorldClick`). Путь — в журнал.
			AAsdaPlayerController* P2b = C->Pc();
			float MX0 = -1.f, MY0 = -1.f;
			if (P2b) { P2b->GetMousePosition(MX0, MY0); }
			const bool bSlate = FVector2D::Distance(FVector2D(MX0, MY0), Use) <= 2.0;
			// ★ R5-fix8 (14.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА, шов гейта мирового клика]: щелчок событиями Slate исполняется в следующем
			//   опросе ввода; до него пик и гейт «над окном» держатся за точку щелчка, а не за курсор ОС (мышь пользователя).
			if (bSlate) { AsdaSkillCastLock::DebugSetClickPoint(P2b, &Use); Mouse(Use, 0); Mouse(Use, 1); }
			else if (P2b) { AsdaSkillCastLock::DebugWorldClick(P2b, Use, /*bDouble=*/false); }
			C->Info(FString::Printf(TEXT("%s: путь щелчка %s (мышь контроллера (%.0f,%.0f))"), *Tag,
				bSlate ? TEXT("событие мыши Slate → вьюпорт → UPlayerInput") : TEXT("OnLeftClick с экранной точкой (Slate позицию не принял)"), MX0, MY0));
			At(C, 0.3f, [C, Victim, Thief, Side, Use, bNew, Tag]()
			{
				AAsdaPlayerController* P3 = C->Pc();
				AAsdaNpcActor* A3 = C->ById(Victim);
				AsdaSkillCastLock::DebugSetClickPoint(P3, nullptr);   // R5-fix8: щелчок исполнен — подмену точки снять
				float MX = -1.f, MY = -1.f;
				if (P3) { P3->GetMousePosition(MX, MY); }
				const AActor* Tg = P3 ? P3->TargetActor.Get() : nullptr;
				const AAsdaNpcActor* TgN = Cast<AAsdaNpcActor>(Tg);
				C->Info(FString::Printf(TEXT("%s: мышь контроллера (%.0f,%.0f), выбран %s"), *Tag, MX, MY, TgN ? *Name(TgN) : (Tg ? *Tg->GetName() : TEXT("ничего"))));
				if (bNew) { C->Expect(Tg == A3, FString::Printf(TEXT("C %s: выбран тот, по кому щёлкнули"), *Tag)); }
				else { C->Info(FString::Printf(TEXT("C %s: OLD (воспроизведение бага) — выбран %s"), *Tag, TgN ? *Name(TgN) : TEXT("не NPC"))); }
				Shot(C, Victim, Side, bNew ? TEXT("NEW") : TEXT("OLD"));
				At(C, 0.4f, [C, Victim, Thief, Side, Use, bNew]()
				{
					if (AAsdaPlayerCharacter* Me2 = C->Ch()) { Me2->SetActorEnableCollision(true); Me2->StopMove(); }
					if (AAsdaPlayerController* P4 = C->Pc()) { P4->CancelRoute(); P4->ClearTarget(); }
					if (UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>()) { if (D->IsDialogOpen()) { D->Close(); } }
					if (!bNew) { ClickOnce(C, Victim, Thief, Side, Use, true); return; }
					++C->NextClick;
					NextClick(C);
				});
			});
			});
		});
	}
	static void NextClick(const FCtxRef& C)
	{
		if (C->NextClick >= C->Clicks.Num()) { Finish(C); return; }
		const auto& K = C->Clicks[C->NextClick];
		ClickOnce(C, K.Get<0>(), K.Get<1>(), K.Get<2>(), K.Get<3>(), /*bNew=*/false);
	}

	// Список пар и щелчков после скана.
	static void AfterScan(const FCtxRef& C)
	{
		C->Pairs.Sort([](const FPair& X, const FPair& Y) { return X.OldPx > Y.OldPx; });
		for (const FPair& Pr : C->Pairs)
		{
			const AAsdaNpcActor* V = C->ById(Pr.Victim);
			const AAsdaNpcActor* T = C->ById(Pr.Thief);
			UE_LOG(LogAsdaNpcClickPick, Log, TEXT("%s pair map=%d жертва=%s класс %d вор=%s класс %d расст=%.0f OLD кража px=%d ракурсов=%d кайма=%d NEW кража px=%d ракурсов=%d кайма=%d"),
				kPrefix, C->MapId, *Name(V), V ? V->GetEntityClass() : -1, *Name(T), T ? T->GetEntityClass() : -1, Pr.Dist, Pr.OldPx, Pr.OldSides, Pr.OldRim, Pr.NewPx, Pr.NewSides, Pr.NewRim);
			const int32 Allow = FMath::Max(kStealAllowPx, FMath::FloorToInt(Pr.VictimVis * kStealAllowShare));
			C->Expect(Pr.NewPx <= Allow, FString::Printf(TEXT("S пара %s ← %s (%.0f ед.): NEW украденных пикселей дальше %.0f px / %.0f ед. от силуэта вора %d ≤ %d (из %d видимых; было %d)"),
				*Name(V), *Name(T), Pr.Dist, kRimPx, kRimUnits, Pr.NewPx, Allow, Pr.VictimVis, Pr.OldPx));
		}
		// Щелчки: заданные жертвы (их худшая пара) + до kAutoClicks пар с наибольшей кражей.
		TSet<int32> Used;
		auto AddClick = [&](const FPair& Pr) { if (Pr.OldSide >= 0 && !Used.Contains(Pr.Victim)) { Used.Add(Pr.Victim); C->Clicks.Add(MakeTuple(Pr.Victim, Pr.Thief, Pr.OldSide, Pr.OldPxAt)); } };
		for (int32 Id : C->Victims)
		{
			bool bFound = false;
			for (const FPair& Pr : C->Pairs) { if (Pr.Victim == Id) { AddClick(Pr); bFound = true; break; } }
			if (!bFound) { C->Info(FString::Printf(TEXT("жертва %d: при OLD кражи не найдено — щелчок по центру модели со стороны 0"), Id)); if (C->ById(Id)) { C->Clicks.Add(MakeTuple(Id, 0, 0, FVector2D(-1, -1))); Used.Add(Id); } }
		}
		int32 Auto = 0;
		for (const FPair& Pr : C->Pairs) { if (Auto >= kAutoClicks) { break; } if (!Used.Contains(Pr.Victim)) { AddClick(Pr); ++Auto; } }
		// Пиксель (-1,-1): центр модели на экране — вычислится в ClickOnce через поиск вокруг.
		for (auto& K : C->Clicks)
		{
			if (K.Get<3>().X >= 0) { continue; }
			AAsdaNpcActor* A = C->ById(K.Get<0>());
			FVector St; float Yaw = 0.f;
			if (A && StandAt(C, A->GetActorLocation(), K.Get<2>(), St, Yaw))
			{
				Place(C, St, Yaw, ViewPitch(K.Get<2>()));
				FVector2D S;
				if (C->Pc()->ProjectWorldLocationToScreen(A->GetActorLocation() + FVector(0, 0, 100), S)) { K.Get<3>() = S; }
			}
		}
		C->Info(FString::Printf(TEXT("настоящих щелчков: %d (по OLD и NEW)"), C->Clicks.Num()));
		// Анимация NPC на паузе на время щелчков: пиксель выбирается оракулом и нажимается через кадр — поза должна быть та же
		//   (кристалл перемещения вращается, за 0,25 с он заслонял выбранный пиксель).
		for (const TWeakObjectPtr<AAsdaNpcActor>& WA : C->All) { if (WA.IsValid() && WA->Mesh) { WA->Mesh->bPauseAnims = true; } }
		NextClick(C);
	}

	static void NextTarget(const FCtxRef& C)
	{
		if (C->Next >= C->Targets.Num()) { AfterScan(C); return; }
		AAsdaNpcActor* A = C->Targets[C->Next].Get();
		++C->Next;
		if (!A) { At(C, 0.01f, [C]() { NextTarget(C); }); return; }
		const TArray<AAsdaNpcActor*> Nb = NeighboursOf(C, A, kOracle);
		Mode(C, false);
		const FScan O = ScanTarget(C, A, Nb);
		Mode(C, true);
		C->DiagLeft = 12;
		const FScan N = ScanTarget(C, A, Nb);
		C->DiagLeft = 0;
		FString St;
		for (const TPair<int32, int32>& S : O.Stolen) { St += FString::Printf(TEXT(" %d:%d"), S.Key, S.Value); }
		FString StN;
		for (const TPair<int32, int32>& S : N.Stolen) { StN += FString::Printf(TEXT(" %d:%d"), S.Key, S.Value); }
		C->Info(FString::Printf(TEXT("S %s класс %d, NPC на карте %d: OLD видно %d, верно %d, промах %d, украдено [%s ], ракурсов %d/%d; NEW видно %d, верно %d, промах %d, украдено [%s ], ракурсов %d/%d"),
			*Name(A), A->GetEntityClass(), Nb.Num(), O.Visible, O.Ok, O.Miss, *St, O.SidesOk, O.Sides, N.Visible, N.Ok, N.Miss, *StN, N.SidesOk, N.Sides));
		C->Info(FString::Printf(TEXT("S %s: замена тела не задета на %d из %d видимых пикселей модели"), *Name(A), N.BodyMiss, N.Visible));
		C->Expect(N.Sides == 0 || N.SidesOk == N.Sides, FString::Printf(TEXT("S %s: NEW щелчок по модели выбирает его со всех видимых ракурсов (%d/%d)"), *Name(A), N.SidesOk, N.Sides));
		C->Expect(N.Visible == 0 || N.Miss <= FMath::CeilToInt(N.Visible * kMissShare) || N.Miss <= O.Miss,
			FString::Printf(TEXT("S %s: NEW промахов по модели %d из %d видимых (OLD %d)"), *Name(A), N.Miss, N.Visible, O.Miss));
		TSet<int32> Ids;
		for (const TPair<int32, int32>& S : O.Stolen) { Ids.Add(S.Key); }
		for (const TPair<int32, int32>& S : N.Stolen) { Ids.Add(S.Key); }
		for (const TPair<int32, int32>& S : O.Rim) { Ids.Add(S.Key); }
		for (const TPair<int32, int32>& S : N.Rim) { Ids.Add(S.Key); }
		for (int32 Id : Ids)
		{
			FPair Pr;
			Pr.Victim = A->NpcId; Pr.Thief = Id;
			const AAsdaNpcActor* T = C->ById(Id);
			Pr.Dist = T ? (float)FVector::Dist2D(T->GetActorLocation(), A->GetActorLocation()) : 0.f;
			Pr.OldPx = O.Stolen.FindRef(Id); Pr.NewPx = N.Stolen.FindRef(Id);
			Pr.OldRim = O.Rim.FindRef(Id); Pr.NewRim = N.Rim.FindRef(Id);
			Pr.VictimVis = N.Visible;
			Pr.OldSides = O.StolenSides.FindRef(Id); Pr.NewSides = N.StolenSides.FindRef(Id);
			if (const TPair<int32, FVector2D>* At0 = O.StolenAt.Find(Id)) { Pr.OldSide = At0->Key; Pr.OldPxAt = At0->Value; }
			C->Pairs.Add(Pr);
		}
		At(C, 0.02f, [C]() { NextTarget(C); });
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

	static FString ZoneDesc(const AAsdaNpcActor* A)
	{
		const UCapsuleComponent* Cp = A->PickHitbox;
		const FVector L = Cp ? Cp->GetComponentLocation() - A->GetActorLocation() : FVector::ZeroVector;
		const ECollisionResponse MV = A->Mesh ? A->Mesh->GetCollisionResponseToChannel(ECC_Visibility) : ECR_Ignore;
		const bool bMeshCol = A->Mesh && A->Mesh->IsCollisionEnabled() && MV == ECR_Block && A->Mesh->GetPhysicsAsset();
		return FString::Printf(TEXT("капсула R %.0f полувысота %.0f центр над NPC (%.0f,%.0f,%.0f), меш ловит луч %s"),
			Cp ? Cp->GetScaledCapsuleRadius() : 0.f, Cp ? Cp->GetScaledCapsuleHalfHeight() : 0.f, L.X, L.Y, L.Z, bMeshCol ? TEXT("да") : TEXT("нет"));
	}

	static void Run(const FCtxRef& C)
	{
		UWorld* W = C->W.Get();
		if (!W || !C->Pc() || !C->Ch()) { C->Expect(false, TEXT("мир, контроллер и свой персонаж есть")); Finish(C); return; }
		C->StartGame = AsdaCoord::UeToGame2D(C->Ch()->GetActorLocation());
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(kKey)) { C->KeyAtStart = V->GetInt(); }
		SetKey(0);   // удочерение при прежней зоне
		int32 Idx = 0;
		for (TActorIterator<AAsdaNpcActor> It(W); It; ++It)
		{
			AAsdaNpcActor* A = *It;
			if (!A || A->bDisabled || A->NpcId <= 0 || A->bNetSpawned) { continue; }
			C->All.Add(A);
			Adopt(C, A, Idx++);
		}
		At(C, 1.0f, [C]()
		{
			int32 Shown = 0;
			for (const TWeakObjectPtr<AAsdaNpcActor>& WA : C->All)
			{
				AAsdaNpcActor* A = WA.Get();
				if (!A) { continue; }
				const bool bShown = A->bNetSpawned && !A->IsHidden() && A->GetActorEnableCollision();
				Shown += bShown ? 1 : 0;
				const FString OldZ = ZoneDesc(A);
				Mode(C, true);
				const FString NewZ = ZoneDesc(A);
				Mode(C, false);
				const USkeletalMesh* SK = A->Mesh ? A->Mesh->GetSkeletalMeshAsset() : nullptr;
				const FVector GeoE = SK ? SK->GetBounds().BoxExtent : FVector::ZeroVector;
				const FVector PhysE = A->Mesh ? A->Mesh->Bounds.BoxExtent : FVector::ZeroVector;
				const UPhysicsAsset* PA = A->Mesh ? A->Mesh->GetPhysicsAsset() : nullptr;
				C->Info(FString::Printf(TEXT("M %s класс %d h %.0f модель %s: геометрия ½ (%.0f,%.0f,%.0f), Mesh->Bounds ½ (%.0f,%.0f,%.0f), тел физ-ассета %d; OLD %s; NEW %s"),
					*Name(A), A->GetEntityClass(), A->GetNpcListHeight(), SK ? *SK->GetName() : TEXT("-"), GeoE.X / 100.0, GeoE.Y / 100.0, GeoE.Z / 100.0, PhysE.X, PhysE.Y, PhysE.Z,
					PA ? PA->SkeletalBodySetups.Num() : -1, *OldZ, *NewZ));
			}
			C->Expect(Shown == C->All.Num() && Shown > 0, FString::Printf(TEXT("NPC карты %d удочерены 4067, показаны, с коллизией: %d из %d"), C->MapId, Shown, C->All.Num()));
			for (const TWeakObjectPtr<AAsdaNpcActor>& WA : C->All)
			{
				AAsdaNpcActor* A = WA.Get();
				if (A && NeighboursOf(C, A).Num() > 0) { C->Targets.Add(WA); }
			}
			C->Info(FString::Printf(TEXT("целей скана (есть сосед ближе %.0f): %d"), kPair, C->Targets.Num()));
			NextTarget(C);
		});
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestNpcClickPick(
	AsdaNpcClickPickTest::kCmd,
	TEXT("asda.SelfTest.NpcClickPick map=<id> [victims=131,146] [shot] [quit] — clicking an NPC selects that NPC: zones OLD/NEW ")
	TEXT("(asda.Npc.ClickBoxOriginal + asda.Npc.ClickByBody), silhouettes of every NPC pair vs the real model geometry, real mouse clicks. Offline, baked level of `map`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaNpcClickPickTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		int32 MapId = -1;
		FString VictimsArg;
		for (const FString& A : Args)
		{
			if (A.StartsWith(TEXT("map="))) { MapId = FCString::Atoi(*A.Mid(4)); }
			if (A.StartsWith(TEXT("victims="))) { VictimsArg = A; }
		}
		if (MapId < 0) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("map=<id> is required")); return; }
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, *FString::Printf(TEXT("Asda.Load.CurrentMap %d"), MapId));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s map=%d selfready %s%s%s"), kCmd, MapId, *VictimsArg, KA.Has(TEXT("shot")) ? TEXT(" shot") : TEXT(""), KA.bQuit ? TEXT(" quit") : TEXT(""));
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
		C->MapId = MapId;
		if (!VictimsArg.IsEmpty())
		{
			TArray<FString> Parts;
			VictimsArg.Mid(8).ParseIntoArray(Parts, TEXT(","));
			for (const FString& P : Parts) { const int32 Id = FCString::Atoi(*P); if (Id > 0) { C->Victims.Add(Id); } }
		}
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.5f, KA.Delay), false);
	}));

// ★★★ ГЕНЕРАТОР ТАБЛИЦЫ КОРОБОК КОСТЕЙ NPC — `asda.Npc.GenPickBones` (08.11.2026; разбор — Public/Npc/AsdaNpcBodyRay.h).
//   Все скелетные меши `/Game/AsdaAssets/Models/NPC` (кроме меток eff_*): LOD0 данных импорта, каждая вершина видимой секции → кость
//   с наибольшим весом → точка в системе привязки кости (`GetRefBasesInvMatrix`) → min/max по кости (+1 ед. игры запаса).
//   Пишет `Source/AsdaReborn/Private/Npc/AsdaNpcPickBones.inl` (после — пересборка). Только редактор.
static FAutoConsoleCommandWithWorldAndArgs GAsdaNpcGenPickBones(
	TEXT("asda.Npc.GenPickBones"),
	TEXT("asda.Npc.GenPickBones [quit] — per-bone tight boxes of every NPC skeletal mesh -> Private/Npc/AsdaNpcPickBones.inl (editor import data)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		AR.SearchAllAssets(true);
		TArray<FAssetData> Assets;
		AR.GetAssetsByPath(FName(TEXT("/Game/AsdaAssets/Models/NPC")), Assets, /*bRecursive=*/true);
		Assets.Sort([](const FAssetData& X, const FAssetData& Y) { return X.AssetName.LexicalLess(Y.AssetName); });
		FString Boxes, Models, BoneNames, TriRows;
		int32 NBox = 0, NModel = 0, NSkip = 0, NBone = 0, NTri = 0, NDropV = 0;
		auto W8 = [](float W) { return FMath::Clamp(FMath::RoundToInt(W * 255.f), 0, 255); };   // вес байтом
		float kBigEdge = 1500.f;         // ребро треугольника длиннее 15 ед. игры (единицы ассета) — лист пишется треугольниками; `edge=N`
		float kPad = 25.f;             // запас коробки, единицы ассета (модели импортированы ×100: 25 = 0,25 ед. игры); `pad=N`
		int32 Leaf = 12;               // треугольников в листе дерева (`leaf=N`)
		for (const FString& A : Args)
		{
			if (A.StartsWith(TEXT("leaf="))) { Leaf = FMath::Max(1, FCString::Atoi(*A.Mid(5))); }
			if (A.StartsWith(TEXT("pad="))) { kPad = FMath::Max(0.f, (float)FCString::Atof(*A.Mid(4))); }
			if (A.StartsWith(TEXT("edge="))) { kBigEdge = FMath::Max(1.f, (float)FCString::Atof(*A.Mid(5))); }
		}
		for (const FAssetData& AD : Assets)
		{
			if (AD.AssetClassPath != USkeletalMesh::StaticClass()->GetClassPathName()) { continue; }
			if (AD.AssetName.ToString().StartsWith(TEXT("eff_"))) { continue; }
			USkeletalMesh* SK = Cast<USkeletalMesh>(AD.GetAsset());
			FSkeletalMeshModel* Model = SK ? SK->GetImportedModel() : nullptr;
			if (!Model || Model->LODModels.Num() == 0) { ++NSkip; continue; }
			const FSkeletalMeshLODModel& L = Model->LODModels[0];
			const TArray<FMatrix44f>& Inv = SK->GetRefBasesInvMatrix();
			const FReferenceSkeleton& RS = SK->GetRefSkeleton();
			// Вершины модели (глобальный индекс как в IndexBuffer): позиция привязки и до трёх костей с весами (нормированы).
			struct FVx { FVector3f P; int32 B[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE }; float W[3] = { 0.f, 0.f, 0.f }; bool bLive = false; };
			TArray<FVx> Vx; Vx.SetNum((int32)L.NumVertices);
			TArray<uint32> Tris;
			for (const FSkelMeshSection& S : L.Sections)
			{
				if (S.bDisabled) { continue; }
				for (int32 v = 0; v < S.SoftVertices.Num(); ++v)
				{
					const FSoftSkinVertex& V = S.SoftVertices[v];
					const int32 Gi = (int32)S.BaseVertexIndex + v;
					if (!Vx.IsValidIndex(Gi)) { continue; }
					TArray<TPair<float, int32>, TInlineAllocator<12>> Inf;
					for (int32 k = 0; k < MAX_TOTAL_INFLUENCES; ++k)
					{
						if (V.InfluenceWeights[k] > 0 && V.InfluenceBones[k] < S.BoneMap.Num() && Inv.IsValidIndex(S.BoneMap[V.InfluenceBones[k]]))
						{
							Inf.Emplace((float)V.InfluenceWeights[k], (int32)S.BoneMap[V.InfluenceBones[k]]);
						}
					}
					if (Inf.Num() == 0) { ++NDropV; continue; }
					Inf.Sort([](const TPair<float, int32>& X, const TPair<float, int32>& Y) { return X.Key > Y.Key; });
					FVx& O = Vx[Gi];
					O.P = V.Position; O.bLive = true;
					float Ws = 0.f;
					for (int32 k = 0; k < 3 && k < Inf.Num(); ++k) { O.B[k] = Inf[k].Value; O.W[k] = Inf[k].Key; Ws += Inf[k].Key; }
					for (int32 k = 0; k < 3; ++k) { O.W[k] /= FMath::Max(Ws, 1e-6f); }
				}
				for (uint32 t = 0; t < S.NumTriangles; ++t)
				{
					const uint32 Bi = S.BaseIndex + t * 3;
					if (Bi + 2 >= (uint32)L.IndexBuffer.Num()) { break; }
					const uint32 I0 = L.IndexBuffer[Bi], I1 = L.IndexBuffer[Bi + 1], I2 = L.IndexBuffer[Bi + 2];
					if ((int32)FMath::Max3(I0, I1, I2) >= Vx.Num() || !Vx[I0].bLive || !Vx[I1].bLive || !Vx[I2].bLive) { continue; }
					Tris.Add(I0); Tris.Add(I1); Tris.Add(I2);
				}
			}
			// Треугольник → главная кость первой вершины (группа); в группе — дерево по центрам, лист ≤ Leaf треугольников.
			//   Лист: коробка точек в пространстве ПРИВЯЗКИ модели и три кости с наибольшим суммарным весом по вершинам листа.
			TMap<int64, TArray<int32>> Groups;   // ключ — пара главных костей первой вершины (веса в листе однороднее)
			for (int32 t = 0; t + 2 < Tris.Num(); t += 3) { const FVx& X = Vx[Tris[t]]; Groups.FindOrAdd((int64)X.B[0] * 100000 + (X.W[1] > 0.1f ? X.B[1] : -1)).Add(t / 3); }
			if (Groups.Num() == 0) { ++NSkip; continue; }
			TArray<int64> Keys; Groups.GetKeys(Keys); Keys.Sort();
			TArray<int32> ModelBones;   // кости модели, на которые ссылаются листья (индексы в kBones от FirstBone)
			auto LocalBone = [&](int32 B) { int32 I = ModelBones.Find(B); if (I == INDEX_NONE) { I = ModelBones.Add(B); } return I; };
			const int32 First = NBox;
			const int32 FirstTri = NTri;
			for (int64 G : Keys)
			{
				TArray<int32>& Ids = Groups[G];
				auto Ctr = [&](int32 t, int32 Ax) { return Vx[Tris[t * 3]].P[Ax] + Vx[Tris[t * 3 + 1]].P[Ax] + Vx[Tris[t * 3 + 2]].P[Ax]; };
				TFunction<void(TArrayView<int32>)> Split = [&](TArrayView<int32> V)
				{
					FBox3f Cb(ForceInit), Bx(ForceInit);
					TMap<int32, float> Wsum;
					for (int32 t : V)
					{
						for (int32 k = 0; k < 3; ++k)
						{
							const FVx& X = Vx[Tris[t * 3 + k]];
							Bx += X.P;
							for (int32 j = 0; j < 3; ++j) { if (X.B[j] != INDEX_NONE) { Wsum.FindOrAdd(X.B[j]) += X.W[j]; } }
						}
						Cb += FVector3f(Ctr(t, 0), Ctr(t, 1), Ctr(t, 2)) / 3.f;
					}
					if (V.Num() <= Leaf)
					{
						Wsum.ValueSort([](float X, float Y) { return X > Y; });
						int32 Bn[3] = { -1, -1, -1 }; float Wn[3] = { 0.f, 0.f, 0.f }; float Tot = 0.f; int32 n = 0;
						for (const TPair<int32, float>& P : Wsum) { if (n >= 3) { break; } Bn[n] = LocalBone(P.Key); Wn[n] = P.Value; Tot += P.Value; ++n; }
						for (int32 k = 0; k < 3; ++k) { Wn[k] /= FMath::Max(Tot, 1e-6f); }
						// Рыхлый лист (крупные треугольники — надгробия, кристаллы, доски): ребро длиннее kBigEdge — коробка накрыла бы пустоту,
						//   пишем сами треугольники. У персонажей треугольники мелкие — лист остаётся коробкой.
						float MaxEdge = 0.f;
						for (int32 t : V)
						{
							const FVector3f& A0 = Vx[Tris[t * 3]].P; const FVector3f& A1 = Vx[Tris[t * 3 + 1]].P; const FVector3f& A2 = Vx[Tris[t * 3 + 2]].P;
							MaxEdge = FMath::Max3(MaxEdge, FMath::Max(FVector3f::Dist(A0, A1), FVector3f::Dist(A1, A2)), FVector3f::Dist(A2, A0));
						}
						if (MaxEdge > kBigEdge)
						{
							for (int32 t : V)
							{
								const FVector3f& A0 = Vx[Tris[t * 3]].P; const FVector3f& A1 = Vx[Tris[t * 3 + 1]].P; const FVector3f& A2 = Vx[Tris[t * 3 + 2]].P;
								TriRows += FString::Printf(TEXT("{%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d},\n"), Bn[0], Bn[1], Bn[2], W8(Wn[0]), W8(Wn[1]), W8(Wn[2]),
									FMath::RoundToInt(A0.X), FMath::RoundToInt(A0.Y), FMath::RoundToInt(A0.Z), FMath::RoundToInt(A1.X), FMath::RoundToInt(A1.Y), FMath::RoundToInt(A1.Z),
									FMath::RoundToInt(A2.X), FMath::RoundToInt(A2.Y), FMath::RoundToInt(A2.Z));
								++NTri;
							}
							return;
						}
						Bx = Bx.ExpandBy(kPad);
						Boxes += FString::Printf(TEXT("{%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d},\n"), Bn[0], Bn[1], Bn[2], W8(Wn[0]), W8(Wn[1]), W8(Wn[2]),
							FMath::FloorToInt(Bx.Min.X), FMath::FloorToInt(Bx.Min.Y), FMath::FloorToInt(Bx.Min.Z), FMath::CeilToInt(Bx.Max.X), FMath::CeilToInt(Bx.Max.Y), FMath::CeilToInt(Bx.Max.Z));
						++NBox;
						return;
					}
					const FVector3f E = Cb.GetExtent();
					const int32 Ax = (E.X >= E.Y && E.X >= E.Z) ? 0 : (E.Y >= E.Z ? 1 : 2);
					Algo::Sort(V, [&](int32 X, int32 Y) { return Ctr(X, Ax) < Ctr(Y, Ax); });
					const int32 H = V.Num() / 2;
					Split(V.Left(H));
					Split(V.RightChop(H));
				};
				Split(TArrayView<int32>(Ids));
			}
			Models += FString::Printf(TEXT("\t\t{ TEXT(\"%s\"), %d, %d, %d, %d, %d, %d },\n"), *SK->GetName(), First, NBox - First, NBone, ModelBones.Num(), FirstTri, NTri - FirstTri);
			for (int32 B : ModelBones) { BoneNames += FString::Printf(TEXT("\t\tTEXT(\"%s\"),\n"), *RS.GetBoneName(B).ToString()); ++NBone; }
			++NModel;
		}
		const FString Out = FString::Printf(TEXT("// АВТОГЕНЕРАЦИЯ `asda.Npc.GenPickBones` (%s) — руками не править. Моделей %d, коробок %d, треугольников %d, костей %d.\n")
			TEXT("//   Строки: кости (индексы в kBones от FModel.FirstBone), веса ×255, координаты в единицах ассета.\n")
			TEXT("//   Коробка листа: треугольники одной главной кости, дерево по центрам (≤ leaf), min/max точек в пространстве привязки модели\n")
			TEXT("//   (единицы ассета, + запас pad) и три кости листа с наибольшим суммарным весом (индексы в kBones от FModel.FirstBone).\n")
			TEXT("namespace AsdaNpcPickBones\n{\n\tstatic const FBoneBox kBoxes[] =\n\t{\n%s{-1,-1,-1,0,0,0,0,0,0,0,0,0}\n\t};\n\tstatic const FBoneTri kTris[] =\n\t{\n%s{-1,-1,-1,0,0,0,0,0,0,0,0,0,0,0,0}\n\t};\n\tstatic const TCHAR* const kBones[] =\n\t{\n%s\t};\n\tstatic const FModel kModels[] =\n\t{\n%s\t};\n}\n"),
			*FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M")), NModel, NBox, NTri, NBone, *Boxes, *TriRows, *BoneNames, *Models);
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Source/AsdaReborn/Private/Npc/AsdaNpcPickBones.inl"));
		const bool bOk = NModel > 0 && FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8);
		UE_LOG(LogAsdaNpcClickPick, Log, TEXT("NPC-PICKBONES ИТОГ: ok=%d fail=%d моделей %d коробок %d треугольников %d пропущено %d вершин без весов %d файл %s"), bOk ? 1 : 0, bOk ? 0 : 1, NModel, NBox, NTri, NSkip, NDropV, *Path);
		AsdaSelfTestKit::Quit(W, KA.bQuit);
	}));

// Разбор секций модели (данные импорта и рендера) — `asda.Npc.DumpMeshSections <имя меша>`.
static FAutoConsoleCommandWithWorldAndArgs GAsdaNpcDumpMeshSections(
	TEXT("asda.Npc.DumpMeshSections"),
	TEXT("asda.Npc.DumpMeshSections <mesh name> [quit] — LOD0 sections of an NPC skeletal mesh (import model vs render data)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		const FString Name = KA.Str(0);
		const FString Path = FString::Printf(TEXT("/Game/AsdaAssets/Models/NPC/%s/SkeletalMeshes/%s.%s"), *Name, *Name, *Name);
		USkeletalMesh* SK = LoadObject<USkeletalMesh>(nullptr, *Path);
		FSkeletalMeshModel* Model = SK ? SK->GetImportedModel() : nullptr;
		if (!Model || Model->LODModels.Num() == 0) { UE_LOG(LogAsdaNpcClickPick, Log, TEXT("NPC-SECTIONS ИТОГ: ok=0 fail=1 нет %s"), *Path); AsdaSelfTestKit::Quit(W, KA.bQuit); return; }
		const FSkeletalMeshLODModel& L = Model->LODModels[0];
		UE_LOG(LogAsdaNpcClickPick, Log, TEXT("NPC-SECTIONS %s: LOD %d, вершин %u, индексов %d, секций %d, рендер LOD %d"), *Name, Model->LODModels.Num(), L.NumVertices, L.IndexBuffer.Num(), L.Sections.Num(),
			SK->GetResourceForRendering() ? SK->GetResourceForRendering()->LODRenderData.Num() : -1);
		for (int32 i = 0; i < L.Sections.Num(); ++i)
		{
			const FSkelMeshSection& S = L.Sections[i];
			FBox3f B(ForceInit); for (const FSoftSkinVertex& V : S.SoftVertices) { B += V.Position; }
			const FSkeletalMaterial* M = SK->GetMaterials().IsValidIndex(S.MaterialIndex) ? &SK->GetMaterials()[S.MaterialIndex] : nullptr;
			UE_LOG(LogAsdaNpcClickPick, Log, TEXT("NPC-SECTIONS  секция %d: мат %d %s, вершин %d (база %u), треуг %u (база %u), откл %d, костей %d, короб (%.0f..%.0f, %.0f..%.0f, %.0f..%.0f)"),
				i, S.MaterialIndex, M ? *M->MaterialSlotName.ToString() : TEXT("-"), S.SoftVertices.Num(), S.BaseVertexIndex, S.NumTriangles, S.BaseIndex, S.bDisabled ? 1 : 0, S.BoneMap.Num(),
				B.Min.X, B.Max.X, B.Min.Y, B.Max.Y, B.Min.Z, B.Max.Z);
		}
		if (const FSkeletalMeshRenderData* RD = SK->GetResourceForRendering())
		{
			if (RD->LODRenderData.Num() > 0)
			{
				const FSkeletalMeshLODRenderData& R = RD->LODRenderData[0];
				for (int32 i = 0; i < R.RenderSections.Num(); ++i)
				{
					UE_LOG(LogAsdaNpcClickPick, Log, TEXT("NPC-SECTIONS  рендер-секция %d: мат %d, вершин %u, треуг %u, откл %d"), i, R.RenderSections[i].MaterialIndex, R.RenderSections[i].NumVertices, R.RenderSections[i].NumTriangles, R.RenderSections[i].bDisabled ? 1 : 0);
				}
			}
		}
		UE_LOG(LogAsdaNpcClickPick, Log, TEXT("NPC-SECTIONS ИТОГ: ok=1 fail=0"));
		AsdaSelfTestKit::Quit(W, KA.bQuit);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING && WITH_EDITORONLY_DATA
