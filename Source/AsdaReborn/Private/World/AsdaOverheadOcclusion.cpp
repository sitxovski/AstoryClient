// Copyright (c) 2026 @sitxovski (Discord)
// Реализация скрытия надписей над головой за препятствиями. Разбор оригинала и устройство — в шапке
//   `Public/World/AsdaOverheadOcclusion.h`.
#include "World/AsdaOverheadOcclusion.h"

#include "World/AsdaGroundTrace.h"            // компоненты воды и рельефа карты, сетка высот .TER
#include "World/AsdaWalkGrid.h"               // высота рельефа по сетке .TER (дешёвый шаг вдоль луча вместо треугольников меша)
#include "Components/StaticMeshComponent.h"
#include "World/AsdaLootActor.h"
#include "World/AsdaPlayerCharacter.h"     // ★ 06.11.2026: подъём транспорта (MountHeightOffset)
#include "World/AsdaPetActor.h"
#include "World/AsdaProjectileActor.h"
#include "World/AsdaSoulGuardianActor.h"
#include "Npc/AsdaMobActor.h"
#include "Npc/AsdaNpcActor.h"
#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Components/ShapeComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

// ★ Рубильник. 1 — [ОРИГИНАЛ] надписи (плашки, вывески лавок, облачка чата) прячутся за геометрией мира; 0 — прежнее
//   поведение порта: надписи рисуются сквозь стены и крыши.
static TAutoConsoleVariable<int32> CVarOverheadOcclude(
	TEXT("asda.Overhead.Occlude"), 1,
	TEXT("1 — надписи над головой (имя, HP, вывеска лавки, облачко чата) прячутся за стенами/крышами/скалами, как z-тест оригинала; ")
	TEXT("0 — рисуются сквозь препятствия (прежнее поведение порта)"),
	ECVF_Default);

// Частота луча на одну надпись, Гц. 10 — решение за ≤ 0,1 с; дороже не нужно: на краю препятствия всё равно гистерезис.
static TAutoConsoleVariable<float> CVarOverheadOccludeHz(
	TEXT("asda.Overhead.OccludeHz"), 10.f,
	TEXT("Сколько раз в секунду каждая видимая надпись пускает луч камера → точка над головой (1..60, по умолчанию 10)"),
	ECVF_Default);

// Гистерезис: столько лучей ПОДРЯД должны разойтись с принятым состоянием, чтобы оно сменилось (оба направления).
static TAutoConsoleVariable<int32> CVarOverheadOccludeVotes(
	TEXT("asda.Overhead.OccludeVotes"), 2,
	TEXT("Сколько лучей подряд нужно для смены «видно ↔ перекрыто» (1 — сразу; по умолчанию 2 — без мигания на краю препятствия)"),
	ECVF_Default);

// Время проявления/угасания, с. 0 — мгновенно (как z-тест оригинала).
static TAutoConsoleVariable<float> CVarOverheadOccludeFade(
	TEXT("asda.Overhead.OccludeFadeSec"), 0.15f,
	TEXT("Время плавного проявления/угасания надписи при входе за препятствие и выходе из-за него, с (0 — мгновенно)"),
	ECVF_Default);

// ★★★ [ОРИГИНАЛ] 05.11.2026 (баг игроков «имена NPC видны поверх деревьев»): решение о перекрытии — по строке ТЕКСТА надписи (три луча
//   в пиксели, где текст нарисован, на глубине точки над головой — как z-тест квада у оригинала); 0 — прежнее: одна точка над головой.
static TAutoConsoleVariable<int32> CVarOverheadOccludeAtText(
	TEXT("asda.Overhead.OccludeAtText"), 1,
	TEXT("1 — перекрытие надписи над головой решается по пикселям её текста (крона дерева над NPC прячет имя, как z-тест оригинала); ")
	TEXT("0 — по одной точке над головой (прежнее поведение порта)"),
	ECVF_Default);

// ★★★ [ОРИГИНАЛ] 06.11.2026: плашка и облачко всадника поднимаются на `heightOffset` транспорта (разбор — в заголовке); 0 — прежнее
//   поведение порта: подъём только у вывески лавки.
static TAutoConsoleVariable<int32> CVarOverheadMountLift(
	TEXT("asda.Overhead.MountLift"), 1,
	TEXT("1 — плашка имени и облачко чата всадника поднимаются на высоту транспорта из таблицы предметов, как в оригинале; ")
	TEXT("0 — остаются на обычной высоте (прежнее поведение порта)"),
	ECVF_Default);

namespace
{
	// Счётчики цены (только игровой поток).
	int64  GTraces = 0;    // вызовов TraceNow
	int64  GQueries = 0;   // запросов к физике (повторы после отброшенных попаданий)
	uint64 GCycles = 0;    // тактов внутри TraceNow

	// Кэш камеры на кадр: сотни надписей не ищут контроллер и менеджер камеры каждая.
	TWeakObjectPtr<const UWorld> GCamWorld;
	uint64  GCamFrame = MAX_uint64;
	FVector GCamLoc = FVector::ZeroVector;
	bool    GCamOk = false;
	TWeakObjectPtr<const AActor> GCamPawn;   // свой павн (цель камеры — никогда не перекрывает)
	FVector GCamFwd = FVector::ForwardVector;   // оси камеры этого кадра (точка надписи на экранном сдвиге)
	FVector GCamRight = FVector::RightVector;
	FVector GCamUp = FVector::UpVector;
	float   GCamTanHalfFov = 1.f;               // tan(горизонтальный FOV / 2)
	float   GCamViewW = 0.f;                    // ширина вьюпорта, px (0 — неизвестна)

	// Попадание — НЕ препятствие: игровые сущности (их тела и хитбоксы), формы-триггеры/хитбоксы, скелеты, вода.
	//   Перекрывает только геометрия мира (статик-меши карты, рельеф, деревья, постройки).
	bool IsEntityHit(const FHitResult& H)
	{
		const UPrimitiveComponent* C = H.GetComponent();
		// Вода карты (`SM_water`, перепись 29.10: BlockAll, как рельеф) — полупрозрачная поверхность: надпись плывущего видна.
		if (C && C->GetWorld() && C == AsdaGround::FindWaterComponent(C->GetWorld())) { return true; }
		if (C && (C->IsA<UShapeComponent>() || C->IsA<USkeletalMeshComponent>())) { return true; }
		const AActor* A = H.GetActor();
		if (!A) { return false; }
		// Носители плашки (`UAsdaNameplateComponent` создают ровно эти классы: игрок — APawn, моб, NPC, лут, питомец) и прочие
		//   сущности. ★ Только проверка класса: `FindComponentByClass` на акторе карты с сотнями ISM-компонентов стоил 28 мкс
		//   на КАЖДОЕ попадание в дерево (замер `asda.Overhead.OccludeBench` 29.10.2026).
		return A->IsA<APawn>() || A->IsA<AAsdaMobActor>() || A->IsA<AAsdaNpcActor>() || A->IsA<AAsdaLootActor>()
			|| A->IsA<AAsdaPetActor>() || A->IsA<AAsdaProjectileActor>() || A->IsA<AAsdaSoulGuardianActor>();
	}

	// ★ РЕЛЬЕФ — ПО СЕТКЕ .TER, а не по мешу. Замер `asda.Overhead.OccludeBench` (alpen, 29.10.2026): луч мимо препятствий
	//   стоит 1–3 мкс, но луч, идущий низко над землёй, обходит треугольники огромного `SM_terrain` — 30…90 мкс. Высота
	//   рельефа у оригинала и у нас — логическая сетка .TER (`AsdaGroundTrace.h`), поэтому рельеф проверяется шагами вдоль
	//   отрезка камера → точка: точка шага ниже земли (с допуском) ⇒ холм/склон закрывает надпись. Концы отрезка не берутся
	//   (камера держится не ниже .TER + 60, точка над головой — над землёй сущности).
	constexpr float kTerrainStep = 75.f;   // шаг, ед. UE (тайл .TER — 100; диагональ треугольника тайла — 141)
	constexpr float kTerrainTol = 15.f;    // допуск: расхождение сетки и рендер-меша рельефа на склонах
	bool TerrainBlocks(const UAsdaWalkGrid* G, const FVector& A, const FVector& B, FVector& OutHit)
	{
		const FVector D = B - A;
		const float L = D.Size();
		if (!G || L < 1.f) { return false; }
		const int32 N = FMath::Clamp(FMath::CeilToInt(L / kTerrainStep), 2, 160);
		for (int32 i = 1; i < N; ++i)
		{
			const FVector P = A + D * ((float)i / (float)N);
			float Y = 0.f;
			// UE → игра: gameX = UE.Y, gameZ = −UE.X (как `AsdaGround::GroundZ`).
			if (G->GetGroundY(P.Y, -P.X, Y) && P.Z < Y - kTerrainTol) { OutHit = P; return true; }
		}
		return false;
	}
}

bool AsdaOverheadOcclusion::IsEnabled()
{
	return CVarOverheadOcclude.GetValueOnGameThread() != 0;
}

bool AsdaOverheadOcclusion::CameraLocation(const UWorld* W, FVector& Out)
{
	if (!W) { return false; }
	if (GCamWorld.Get() != W || GCamFrame != GFrameCounter)
	{
		GCamWorld = W;
		GCamFrame = GFrameCounter;
		GCamOk = false;
		GCamPawn = nullptr;
		APlayerController* PC = W->GetFirstPlayerController();
		if (PC && PC->PlayerCameraManager)
		{
			GCamLoc = PC->PlayerCameraManager->GetCameraLocation();
			GCamPawn = PC->GetPawn();
			GCamOk = true;
			const FRotationMatrix RM(PC->PlayerCameraManager->GetCameraRotation());
			GCamFwd = RM.GetUnitAxis(EAxis::X);
			GCamRight = RM.GetUnitAxis(EAxis::Y);
			GCamUp = RM.GetUnitAxis(EAxis::Z);
			GCamTanHalfFov = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(PC->PlayerCameraManager->GetFOVAngle(), 5.f, 170.f) * 0.5f));
			int32 VX = 0, VY = 0;
			PC->GetViewportSize(VX, VY);
			GCamViewW = (float)VX;
		}
	}
	Out = GCamLoc;
	return GCamOk;
}

bool AsdaOverheadOcclusion::TraceNow(const UWorld* W, const FVector& Point, const AActor* Owner, FAsdaOccludeHit* Out)
{
	if (Out) { *Out = FAsdaOccludeHit(); }
	FVector Cam;
	if (!CameraLocation(W, Cam)) { return false; }
	if (FVector::DistSquared(Cam, Point) < 1.0) { return false; }
	const uint64 T0 = FPlatformTime::Cycles64();
	++GTraces;
	// ★ СЛОЖНАЯ коллизия обязательна. Перепись `asda.Overhead.CollisionCensus` (alpen, 29.10.2026): все 2224 меша карты
	//   (`BlockAllDynamic`) и рельеф `SM_terrain` (`BlockAll`) блокируют Visibility и Camera, но ПРОСТОЙ геометрии у них нет
	//   (AggGeom пуст, `CTF_UseDefault`) — простой луч их не видит вовсе. Луч по треугольникам — как z-буфер оригинала.
	FCollisionQueryParams Q(SCENE_QUERY_STAT(AsdaOverheadOcclusion), /*bTraceComplex=*/true);
	if (Owner) { Q.AddIgnoredActor(Owner); }
	if (const AActor* Self = GCamPawn.Get()) { Q.AddIgnoredActor(Self); }
	// Рельеф: есть сетка .TER — меш рельефа из луча исключён (проверка шагами ниже); сетки нет — луч по мешу, как прочая геометрия.
	UWorld* MW = const_cast<UWorld*>(W);
	const UAsdaWalkGrid* Grid = AsdaGround::GetGrid();
	UStaticMeshComponent* Terrain = Grid ? AsdaGround::FindTerrainComponent(MW) : nullptr;
	if (Terrain) { Q.AddIgnoredComponent(Terrain); }
	bool bOcc = false;
	int32 Skipped = 0;
	// Не больше 4 запросов: каждое отброшенное попадание (сущность / «камера внутри меша») исключается, луч повторяется.
	for (int32 i = 0; i < 4; ++i)
	{
		FHitResult H;
		++GQueries;
		if (!W->LineTraceSingleByChannel(H, Cam, Point, ECC_Visibility, Q)) { break; }
		// Камера внутри меша (стрела оригинала препятствий не обходит): задняя сторона у оригинала отсекается и глубину не
		//   пишет — этот меш точку не закрывает. Сущность — см. IsEntityHit.
		if (H.bStartPenetrating || IsEntityHit(H))
		{
			++Skipped;
			if (H.bStartPenetrating && H.GetComponent() && !IsEntityHit(H)) { Q.AddIgnoredComponent(H.GetComponent()); }
			else if (H.GetActor()) { Q.AddIgnoredActor(H.GetActor()); }
			else if (H.GetComponent()) { Q.AddIgnoredComponent(H.GetComponent()); }
			else { break; }
			continue;
		}
		bOcc = true;
		if (Out)
		{
			Out->Location = H.ImpactPoint;
			Out->Distance = H.Distance;
			const UPrimitiveComponent* C = H.GetComponent();
			Out->What = FString::Printf(TEXT("%s / %s (%s)"), H.GetActor() ? *H.GetActor()->GetName() : TEXT("-"),
				C ? *C->GetName() : TEXT("-"), C ? *C->GetClass()->GetName() : TEXT("-"));
		}
		break;
	}
	if (!bOcc && Terrain)
	{
		FVector P;
		if (TerrainBlocks(Grid, Cam, Point, P))
		{
			bOcc = true;
			if (Out)
			{
				Out->Location = P;
				Out->Distance = FVector::Dist(Cam, P);
				Out->What = TEXT("рельеф (сетка .TER)");
			}
		}
	}
	if (Out) { Out->bOccluded = bOcc; Out->Skipped = Skipped; }
	GCycles += FPlatformTime::Cycles64() - T0;
	return bOcc;
}

float AsdaOverheadOcclusion::MountLiftZ(const AActor* Owner)
{
	if (CVarOverheadMountLift.GetValueOnGameThread() == 0) { return 0.f; }
	const AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(Owner);
	return (Ch && Ch->IsMounted()) ? Ch->MountHeightOffset : 0.f;
}

bool AsdaOverheadOcclusion::TextSamplingOn()
{
	return CVarOverheadOccludeAtText.GetValueOnGameThread() != 0;
}

bool AsdaOverheadOcclusion::PointAtScreenOffset(const UWorld* W, const FVector& Anchor, const FVector2D& OffsetPx, FVector& Out)
{
	Out = Anchor;
	FVector Cam;
	if (!CameraLocation(W, Cam) || GCamViewW < 1.f) { return false; }
	// Глубина точки вдоль взгляда; на ней один пиксель вьюпорта = 2·d·tan(FOV/2) / ширина (FOV в UE — горизонтальный).
	const float D = FVector::DotProduct(Anchor - Cam, GCamFwd);
	if (D <= 1.f) { return false; }
	const float K = 2.f * D * GCamTanHalfFov / GCamViewW;
	Out = Anchor + GCamRight * (OffsetPx.X * K) - GCamUp * (OffsetPx.Y * K);
	return true;
}

namespace
{
	// Общий ход состояния (частота лучей, гистерезис, плавность) для любой выборки «перекрыто сейчас?».
	float EvaluateCore(FAsdaOccludeState& S, const UWorld* W, TFunctionRef<bool()> Sample);
}

float AsdaOverheadOcclusion::Evaluate(FAsdaOccludeState& S, const UWorld* W, const FVector& Point, const AActor* Owner)
{
	if (!W || !IsEnabled())
	{
		if (S.bInit) { S.Reset(); }
		return 1.f;
	}
	return EvaluateCore(S, W, [W, &Point, Owner]() { return TraceNow(W, Point, Owner); });
}

float AsdaOverheadOcclusion::EvaluateText(FAsdaOccludeState& S, const UWorld* W, const FVector& Anchor, const FVector2D& CenterPx,
	float HalfWidthPx, const AActor* Owner)
{
	if (!W || !IsEnabled())
	{
		if (S.bInit) { S.Reset(); }
		return 1.f;
	}
	if (!TextSamplingOn()) { return Evaluate(S, W, Anchor, Owner); }
	return EvaluateCore(S, W, [W, &Anchor, CenterPx, HalfWidthPx, Owner]()
	{
		// Три пикселя строки текста: края (с отступом 15% от краёв — глиф, а не пустое поле контрола) и центр.
		const float H = FMath::Max(0.f, HalfWidthPx) * 0.7f;
		const FVector2D Px[3] = { CenterPx + FVector2D(-H, 0.f), CenterPx, CenterPx + FVector2D(H, 0.f) };
		int32 Occ = 0;
		for (int32 i = 0; i < 3; ++i)
		{
			FVector P;
			AsdaOverheadOcclusion::PointAtScreenOffset(W, Anchor, Px[i], P);
			Occ += AsdaOverheadOcclusion::TraceNow(W, P, Owner) ? 1 : 0;
			if (Occ >= 2 || (i == 1 && Occ == 0)) { break; }   // исход уже ясен (2 из 3) — лишний луч не нужен
		}
		return Occ >= 2;
	});
}

namespace
{
float EvaluateCore(FAsdaOccludeState& S, const UWorld* W, TFunctionRef<bool()> Sample)
{
	const double Now = W->GetTimeSeconds();
	const float Period = 1.f / FMath::Clamp(CVarOverheadOccludeHz.GetValueOnGameThread(), 1.f, 60.f);
	// Первый вызов или пропуск кадров (надпись была вне кадра / плашка заморожена): мерим сразу и ставим ответ без проявления —
	//   иначе вернувшаяся из-за края экрана надпись секунду показывала бы устаревшее «видно» (или «перекрыто»).
	if (!S.bInit || GFrameCounter > S.LastFrame + 2)
	{
		const bool bOcc = Sample();
		S.bInit = 1;
		S.bOccluded = bOcc ? 1 : 0;
		S.Votes = 0;
		S.Alpha = bOcc ? 0.f : 1.f;
		S.NextSampleS = Now + Period * FMath::FRand();   // разнос фаз: сотни надписей не бьют лучом в один кадр
		S.LastS = Now;
		S.LastFrame = GFrameCounter;
		return S.Alpha;
	}
	S.LastFrame = GFrameCounter;
	if (Now >= S.NextSampleS)
	{
		// Фаза сохраняется; отстали больше чем на период (просадка кадра) — от текущего момента.
		S.NextSampleS = (Now - S.NextSampleS > Period) ? Now + Period : S.NextSampleS + Period;
		const bool bOcc = Sample();
		if (bOcc == (S.bOccluded != 0)) { S.Votes = 0; }
		else if (++S.Votes >= FMath::Max(1, CVarOverheadOccludeVotes.GetValueOnGameThread()))
		{
			S.bOccluded = bOcc ? 1 : 0;
			S.Votes = 0;
		}
	}
	const float Dt = (float)FMath::Max(0.0, Now - S.LastS);
	S.LastS = Now;
	const float Target = S.bOccluded ? 0.f : 1.f;
	const float Fade = CVarOverheadOccludeFade.GetValueOnGameThread();
	S.Alpha = (Fade <= 0.f) ? Target : FMath::FInterpConstantTo(S.Alpha, Target, Dt, 1.f / Fade);
	return S.Alpha;
}
}   // namespace

void AsdaOverheadOcclusion::GetStats(int64& OutTraces, int64& OutQueries, uint64& OutCycles)
{
	OutTraces = GTraces;
	OutQueries = GQueries;
	OutCycles = GCycles;
}

void AsdaOverheadOcclusion::ResetStats()
{
	GTraces = 0;
	GQueries = 0;
	GCycles = 0;
}
