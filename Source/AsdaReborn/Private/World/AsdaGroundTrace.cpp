// Copyright (c) 2026 @sitxovski (Discord)
// Реализация AsdaGround — см. шапку AsdaGroundTrace.h (полный реверс механизма высоты).
#include "World/AsdaGroundTrace.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaPerf.h"   // ★ R4, доля a: область замера поиска рельефа
#include "World/AsdaWalkGrid.h"

#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaGround, Log, All);

// ── Диагностические CVar ────────────────────────────────────────────────────────────────────────
// asda.Ground.ForceTrace 1 — считать высоту старым способом (трейс по мешу террейна). Для сравнения.
static ASDA_DEV_CVAR(int32, CVarGroundForceTrace,
	TEXT("asda.Ground.ForceTrace"), 0,
	TEXT("1 = высота земли трейсом по рендер-мешу террейна вместо сетки .TER (диагностика)."),
	ECVF_Default);

// asda.Ground.Debug 1 — на каждый запрос логировать «сетка vs меш» и разницу.
static ASDA_DEV_CVAR(int32, CVarGroundDebug,
	TEXT("asda.Ground.Debug"), 0,
	TEXT("1 = логировать высоту земли: сетка .TER, рендер-меш и расхождение."),
	ECVF_Default);

namespace
{
	// Текущая сетка карты и кэш компонента террейна (валидируется по миру).
	TWeakObjectPtr<const UAsdaWalkGrid>   GGrid;
	TWeakObjectPtr<UStaticMeshComponent>  GTerrain;
	TWeakObjectPtr<UWorld>                GTerrainWorld;
	// ★ Волна В4 рыбалки: кэш компонента воды (`SM_water`) и мира, для которого он найден.
	TWeakObjectPtr<UStaticMeshComponent>  GWater;
	// Мир, в котором искали воду (поиск не повторяется, пока мир тот же, даже если воды нет).
	TWeakObjectPtr<UWorld>                GWaterWorld;
	// Признак «в этом мире воду уже искали и не нашли» (карты без .WAT не перебираются каждый вызов).
	bool                                  bGWaterMissing = false;
	// ★ R4, доля a (14.11.2026): «в этом мире рельеф (`SM_terrain*`) уже искали и не нашли» — как у воды. Без признака каждый вызов
	//   на карте без меша рельефа обходил все статические меши уровня (`TActorIterator`), а зовут его и луч заслонения плашек
	//   (`AsdaOverheadOcclusion::TraceNow`), и запасная высота (`TerrainMeshZ`). Признак снимается сменой карты (`SetGrid`) и сам —
	//   через 2 с (рельеф, появившийся позже, будет найден не позднее этого срока). Запись 11 (15.11.2026): прежний путь и ключ убраны.
	bool                                  bGTerrainMissing = false;
	double                                GTerrainMissAtS = 0.0;      // время неудачного поиска рельефа, с (`FPlatformTime::Seconds`)

}

void AsdaGround::SetGrid(const UAsdaWalkGrid* Grid)
{
	GGrid = Grid;
	// Смена карты — кэш террейна прежнего уровня недействителен.
	GTerrain.Reset(); GTerrainWorld.Reset(); bGTerrainMissing = false;   // ★ R4: и признак «рельефа нет»
	GWater.Reset(); GWaterWorld.Reset(); bGWaterMissing = false;   // ★ В4 рыбалки: вода прежнего уровня тоже недействительна
	if (Grid)
	{
		UE_LOG(LogAsdaGround, Log, TEXT("Сетка высот карты: %dx%d, шаг %.0f, Y %.0f..%.0f, вершин %d"),
			Grid->Width, Grid->Height, Grid->CellSize, Grid->HeightMin, Grid->HeightMax, Grid->VertHeights.Num());
	}
	else
	{
		UE_LOG(LogAsdaGround, Warning, TEXT("Сетка высот карты СНЯТА — высота уйдёт в фолбэк-трейс по мешу"));
	}
}

const UAsdaWalkGrid* AsdaGround::GetGrid()
{
	return GGrid.Get();
}

UStaticMeshComponent* AsdaGround::FindTerrainComponent(UWorld* W)
{
	if (!W) { return nullptr; }
	ASDA_PERF_SCOPE("Ground.FindTerrain");   // R4, доля a: поиск меша рельефа (в Shipping пусто)
	if (GTerrainWorld.Get() == W)
	{
		if (UStaticMeshComponent* C = GTerrain.Get()) { return C; }
		// ★ R4, доля a (14.11.2026): недавно (меньше 2 с назад) искали и не нашли — не обходить меши уровня снова.
		if (bGTerrainMissing && FPlatformTime::Seconds() - GTerrainMissAtS < 2.0) { return nullptr; }
	}
	GTerrain.Reset(); GTerrainWorld = W;
	bGTerrainMissing = true;                       // снимется ниже, если рельеф найден
	GTerrainMissAtS = FPlatformTime::Seconds();
	for (TActorIterator<AStaticMeshActor> It(W); It; ++It)
	{
		UStaticMeshComponent* C = It->GetStaticMeshComponent();
		const UStaticMesh* SM = C ? C->GetStaticMesh() : nullptr;
		if (SM && SM->GetName().StartsWith(TEXT("SM_terrain")))
		{
			GTerrain = C;
			bGTerrainMissing = false;
			return C;
		}
	}
	return nullptr;
}

float AsdaGround::TerrainMeshZ(UWorld* W, float UeX, float UeY)
{
	UStaticMeshComponent* C = FindTerrainComponent(W);
	if (!C) { return NAN; }
	// Трейс ПО ОДНОМУ компоненту: сущности (лут/игроки/мобы/NPC) физически не участвуют в запросе.
	FHitResult H;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(AsdaTerrainMeshZ), /*bTraceComplex*/ true);
	if (C->LineTraceComponent(H, FVector(UeX, UeY, 1.0e5f), FVector(UeX, UeY, -1.0e5f), Q))
	{
		return H.ImpactPoint.Z;
	}
	return NAN;
}

// ★ Волна В4 рыбалки (08.10.2026): компонент воды — по имени меша `SM_water` (единственный водный ассет карты).
UStaticMeshComponent* AsdaGround::FindWaterComponent(UWorld* W)
{
	if (!W) { return nullptr; }
	if (GWaterWorld.Get() == W)
	{
		if (UStaticMeshComponent* C = GWater.Get()) { return C; }
		if (bGWaterMissing) { return nullptr; }
	}
	GWater.Reset(); GWaterWorld = W; bGWaterMissing = false;
	for (TActorIterator<AStaticMeshActor> It(W); It; ++It)
	{
		UStaticMeshComponent* C = It->GetStaticMeshComponent();
		const UStaticMesh* SM = C ? C->GetStaticMesh() : nullptr;
		if (SM && SM->GetName().StartsWith(TEXT("SM_water")))
		{
			GWater = C;
			return C;
		}
	}
	bGWaterMissing = true;
	return nullptr;
}

// ★ Ревизия В4 МЕЛК-4б: сброс кэша воды (см. .h).
void AsdaGround::ResetWaterCache()
{
	GWater.Reset(); GWaterWorld.Reset(); bGWaterMissing = false;
}

// ★ Волна В4 рыбалки: высота поверхности воды (эталон `sub_926A07` → `pickSpriteQuadRayTest@0x116B570`, см. .h).
float AsdaGround::WaterSurfaceZ(UWorld* W, float UeX, float UeY)
{
	UStaticMeshComponent* C = FindWaterComponent(W);
	if (!C) { return NAN; }
	FHitResult H;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(AsdaWaterSurfaceZ), /*bTraceComplex*/ true);
	// ★ Порядок трейсов ИЗМЕРЕН (проба `asda.Fish.WaterProbe`, alpen, 08.10.2026): у `SM_water` простой коллизии нет, а
	//   сложная (треугольники) для луча СВЕРХУ не попадает — лицевая сторона треугольников после свопа осей смотрит вниз,
	//   Chaos отбрасывает обратные грани. Луч СНИЗУ даёт поверхность (спот 0 Альпии: вода 889.2 над дном 790.0).
	//   Сверху — запасной путь на случай карт с другой ориентацией треугольников.
	if (C->LineTraceComponent(H, FVector(UeX, UeY, -1.0e5f), FVector(UeX, UeY, 1.0e5f), Q))
	{
		return H.ImpactPoint.Z;
	}
	if (C->LineTraceComponent(H, FVector(UeX, UeY, 1.0e5f), FVector(UeX, UeY, -1.0e5f), Q))
	{
		return H.ImpactPoint.Z;
	}
	return NAN;
}

float AsdaGround::GroundZ(UWorld* W, float UeX, float UeY, float RefZ, const AActor* /*Ignore*/)
{
	if (!W) { return RefZ; }

	const bool bDebug = CVarGroundDebug.GetValueOnAnyThread() != 0;
	const bool bForceTrace = CVarGroundForceTrace.GetValueOnAnyThread() != 0;

	// ── ОСНОВНОЙ ПУТЬ: логическая сетка .TER (как оригинал) ──
	float GridZ = NAN;
	if (const UAsdaWalkGrid* Grid = GGrid.Get())
	{
		// UE→game: gameX = UE.Y, gameZ = −UE.X (прямое (x,y,z)game → (−z,x,y)UE, масштаб 1:1).
		float Y = 0.f;
		if (Grid->GetGroundY(UeY, -UeX, Y)) { GridZ = Y; }
	}

	if (bDebug)
	{
		const float MeshZ = TerrainMeshZ(W, UeX, UeY);
		UE_LOG(LogAsdaGround, Log, TEXT("Высота (%.0f, %.0f): сетка %.1f  меш %.1f  Δ %.1f"),
			UeX, UeY, GridZ, MeshZ, (FMath::IsFinite(GridZ) && FMath::IsFinite(MeshZ)) ? (GridZ - MeshZ) : 0.f);
	}

	if (!bForceTrace && FMath::IsFinite(GridZ)) { return GridZ; }

	// ── ФОЛБЭК: трейс ТОЛЬКО по мешу террейна (сетки карты нет либо форс-режим) ──
	const float MeshZ = TerrainMeshZ(W, UeX, UeY);
	if (FMath::IsFinite(MeshZ)) { return MeshZ; }
	return FMath::IsFinite(GridZ) ? GridZ : RefZ;
}
