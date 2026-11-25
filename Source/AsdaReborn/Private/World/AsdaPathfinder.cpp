// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPathfinder — реализация. Шапка с реверсом и списком отступлений — в AsdaPathfinder.h.
#include "World/AsdaPathfinder.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "World/AsdaCoord.h"       // ★ Аудит №3 (B2): размер тайла и центр тайла — общая точка
#include "World/AsdaWalkGrid.h"
#include "World/AsdaPlayerCharacter.h"
#include "Algo/Reverse.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPath, Log, All);

namespace
{
	// ★ 8 соседей 1:1 с оригиналом (dword_14690F8 @0x14690F8) — порядок важен только для
	//   воспроизводимости порядка обхода при равных стоимостях.
	const int32 GDx[8] = { 0,  1, 0, -1,  1,  1, -1, -1 };
	const int32 GDy[8] = { -1, 0, 1,  0, -1,  1,  1, -1 };
	// ★ Стоимость направления (flt_1469138 @0x1469138): ортогональ 1.0, диагональ √2 (1.414214).
	const float GDirCost[8] = { 1.f, 1.f, 1.f, 1.f, 1.414214f, 1.414214f, 1.414214f, 1.414214f };

	constexpr float kBaseWeight = 1.0f;      // grid2dInitCells512: базовый вес ячейки = 1.0
	constexpr float kRingInner = 2.0f;       // terrainStampCollisionCost: внутреннее кольцо +2.0
	constexpr float kRingOuter = 1.0f;       // внешнее кольцо 5×5 +1.0
	constexpr int32 kMaxExpanded = 120000;   // D2: кап развёрнутых узлов (полная сетка = 262144)

	// Кэш весов текущей карты.
	const UAsdaWalkGrid* GCostGridOwner = nullptr;
	bool GCostGridSwimOpen = false;   // ★ 28.10.2026: ключ asda.Swim.OpenWater, с которым строились веса (смена — перестройка)
	bool GCostGridClosedOn = false;   // ★ 04.11.2026: ключ asda.Walk.ClosedTiles, с которым строились веса (смена — перестройка)
	TArray<float> GCostGrid;
	TArray<float> GEmptyCost;

	struct FNode
	{
		float G = 0.f;
		int32 Parent = -1;    // индекс родителя, -1 = не открыт
		uint8 bClosed = 0;
	};

	// Мин-куча по ключу приоритета.
	struct FOpenEntry
	{
		float Key;
		int32 Idx;
	};
	struct FOpenPred
	{
		bool operator()(const FOpenEntry& A, const FOpenEntry& B) const { return A.Key < B.Key; }
	};
}

const TArray<float>& AsdaPath::GetCostGrid(const UAsdaWalkGrid* Grid)
{
	if (!Grid || Grid->Width <= 0 || Grid->Height <= 0) { return GEmptyCost; }
	if (GCostGridOwner == Grid && GCostGrid.Num() == Grid->Width * Grid->Height && GCostGridSwimOpen == AsdaSwimOpenWaterOn()
		&& GCostGridClosedOn == AsdaWalkClosedTilesOn()) { return GCostGrid; }

	const int32 W = Grid->Width, H = Grid->Height;
	const double T0 = FPlatformTime::Seconds();
	GCostGrid.Reset();
	GCostGrid.Init(kBaseWeight, W * H);
	// ★ Порт terrainStampCollisionCost @0xFBDF70: у КАЖДОГО непроходимого тайла штампуем кольцо 5×5.
	//   Внутренний слой (|dx|<=1 && |dy|<=1) += 2.0, внешний (|dx|==2 || |dy|==2) += 1.0.
	//   Сам блокированный центр в поиске не участвует (walkable == 0), его вес не важен.
	int32 Blocked = 0;
	for (int32 Z = 0; Z < H; ++Z)
	{
		for (int32 X = 0; X < W; ++X)
		{
			if (Grid->IsTileWalkable(X, Z)) { continue; }
			++Blocked;
			for (int32 Dz = -2; Dz <= 2; ++Dz)
			{
				const int32 Nz = Z + Dz;
				if (Nz < 0 || Nz >= H) { continue; }
				for (int32 Dx = -2; Dx <= 2; ++Dx)
				{
					const int32 Nx = X + Dx;
					if (Nx < 0 || Nx >= W || (Dx == 0 && Dz == 0)) { continue; }
					const bool bOuter = (FMath::Abs(Dx) == 2 || FMath::Abs(Dz) == 2);
					GCostGrid[Nz * W + Nx] += bOuter ? kRingOuter : kRingInner;
				}
			}
		}
	}
	GCostGridOwner = Grid;
	GCostGridSwimOpen = AsdaSwimOpenWaterOn();
	GCostGridClosedOn = AsdaWalkClosedTilesOn();
	UE_LOG(LogAsdaPath, Log, TEXT("[путь] веса построены: %dx%d, непроходимых %d, %.1f мс"),
		W, H, Blocked, (FPlatformTime::Seconds() - T0) * 1000.0);
	return GCostGrid;
}

void AsdaPath::InvalidateCostGrid()
{
	GCostGridOwner = nullptr;
	GCostGrid.Reset();
}

bool AsdaPath::IsLineWalkable(const UAsdaWalkGrid* Grid, const FVector2D& FromTile, const FVector2D& ToTile)
{
	if (!Grid || Grid->Width <= 0) { return true; }
	// ★ Порт mapTraceWalkLineBlocked @0xFBD5A0: нормированное направление, ФИКСИРОВАННЫЙ шаг 0.5 тайла
	//   (flt_13A3680 = 0.5), на каждом шаге индекс ячейки floor(pos) и проверка проходимости.
	const FVector2D Delta = ToTile - FromTile;
	const float Len = Delta.Size();
	if (Len < KINDA_SMALL_NUMBER) { return true; }
	const FVector2D Dir = Delta / Len;
	for (float Travelled = 0.f; Travelled < Len; Travelled += 0.5f)
	{
		const FVector2D P = FromTile + Dir * Travelled;
		if (!Grid->IsTileWalkable(FMath::FloorToInt(P.X), FMath::FloorToInt(P.Y))) { return false; }
	}
	return Grid->IsTileWalkable(FMath::FloorToInt(ToTile.X), FMath::FloorToInt(ToTile.Y));
}

namespace
{
	// D4: цель непроходима → ближайший проходимый тайл (расширяющиеся квадратные кольца).
	bool FindNearestWalkable(const UAsdaWalkGrid* Grid, const FIntPoint& Goal, int32 MaxRadius, FIntPoint& Out)
	{
		if (Grid->IsTileWalkable(Goal.X, Goal.Y)) { Out = Goal; return true; }
		for (int32 R = 1; R <= MaxRadius; ++R)
		{
			int32 BestD2 = MAX_int32; FIntPoint Best(-1, -1);
			for (int32 Dz = -R; Dz <= R; ++Dz)
			{
				for (int32 Dx = -R; Dx <= R; ++Dx)
				{
					if (FMath::Max(FMath::Abs(Dx), FMath::Abs(Dz)) != R) { continue; }   // только кольцо
					const int32 X = Goal.X + Dx, Z = Goal.Y + Dz;
					if (!Grid->IsTileWalkable(X, Z)) { continue; }
					const int32 D2 = Dx * Dx + Dz * Dz;
					if (D2 < BestD2) { BestD2 = D2; Best = FIntPoint(X, Z); }
				}
			}
			if (BestD2 != MAX_int32) { Out = Best; return true; }
		}
		return false;
	}
}

bool AsdaPath::FindPathTiles(const UAsdaWalkGrid* Grid, const FIntPoint& InStart, const FIntPoint& InGoal,
	TArray<FIntPoint>& OutTiles, FAsdaPathStats* OutStats)
{
	OutTiles.Reset();
	FAsdaPathStats Stats;
	const double T0 = FPlatformTime::Seconds();
	if (!Grid || Grid->Width <= 0 || Grid->Height <= 0)
	{
		// Нет сетки — прямой отрезок (fail-open, как IsGameWalkable в павне).
		OutTiles.Add(InGoal);
		if (OutStats) { *OutStats = Stats; }
		return true;
	}
	const int32 W = Grid->Width, H = Grid->Height;
	const FIntPoint Start = InStart;
	FIntPoint Goal = InGoal;
	if (Start.X < 0 || Start.Y < 0 || Start.X >= W || Start.Y >= H) { return false; }
	if (Goal.X < 0 || Goal.Y < 0 || Goal.X >= W || Goal.Y >= H) { return false; }
	if (Start == Goal) { OutTiles.Add(Goal); if (OutStats) { *OutStats = Stats; } return true; }

	// D4: цель в стене → ближайший проходимый тайл (радиус 12 тайлов = 1200 юнитов).
	if (!Grid->IsTileWalkable(Goal.X, Goal.Y))
	{
		FIntPoint Moved;
		if (!FindNearestWalkable(Grid, Goal, 12, Moved)) { if (OutStats) { *OutStats = Stats; } return false; }
		Goal = Moved;
		Stats.bGoalMoved = true;
		if (Start == Goal) { OutTiles.Add(Goal); if (OutStats) { *OutStats = Stats; } return true; }
	}

	const TArray<float>& Cost = GetCostGrid(Grid);
	const bool bHaveCost = (Cost.Num() == W * H);

	// ── A* (D1: ключ = g + h, октиль; оригинал — 2·g) ────────────────────────────────────────────
	TArray<FNode> Nodes;
	Nodes.SetNum(W * H);       // FNode тривиален, конструктор по умолчанию заполняет Parent = −1
	TArray<FOpenEntry> Open;
	Open.Reserve(4096);

	auto Heuristic = [&](int32 X, int32 Z) -> float
	{
		// Октильная дистанция × минимальный вес ячейки (1.0) — допустимая (никогда не переоценивает).
		const int32 Dx = FMath::Abs(X - Goal.X), Dz = FMath::Abs(Z - Goal.Y);
		const int32 Lo = FMath::Min(Dx, Dz), Hi = FMath::Max(Dx, Dz);
		return kBaseWeight * (float(Hi - Lo) + 1.414214f * float(Lo));
	};

	const int32 StartIdx = Start.Y * W + Start.X;
	const int32 GoalIdx = Goal.Y * W + Goal.X;
	Nodes[StartIdx].G = 0.f;
	Nodes[StartIdx].Parent = StartIdx;   // сам себе родитель = «открыт, это старт»
	Open.HeapPush({ Heuristic(Start.X, Start.Y), StartIdx }, FOpenPred());
	++Stats.Pushed;

	int32 BestIdx = StartIdx;                          // лучший (ближайший к цели) на случай недостижимости
	float BestH = Heuristic(Start.X, Start.Y);
	bool bFound = false;

	while (Open.Num() > 0)
	{
		FOpenEntry Top;
		Open.HeapPop(Top, FOpenPred(), EAllowShrinking::No);
		const int32 Cur = Top.Idx;
		if (Nodes[Cur].bClosed) { continue; }           // ленивое удаление (как в оригинале)
		Nodes[Cur].bClosed = 1;
		++Stats.Expanded;
		if (Cur == GoalIdx) { bFound = true; break; }
		if (Stats.Expanded >= kMaxExpanded) { break; }  // D2: кап

		const int32 Cx = Cur % W, Cz = Cur / W;
		const float H0 = Heuristic(Cx, Cz);
		if (H0 < BestH) { BestH = H0; BestIdx = Cur; }

		for (int32 i = 0; i < 8; ++i)
		{
			const int32 Nx = Cx + GDx[i], Nz = Cz + GDy[i];
			if (Nx < 0 || Nz < 0 || Nx >= W || Nz >= H) { continue; }
			if (!Grid->IsTileWalkable(Nx, Nz)) { continue; }
			// D3: диагональ — только если обе ортогонали свободны (запрет просачивания через угол).
			if (i >= 4 && (!Grid->IsTileWalkable(Cx + GDx[i], Cz) || !Grid->IsTileWalkable(Cx, Cz + GDy[i])))
			{
				continue;
			}
			const int32 NIdx = Nz * W + Nx;
			if (Nodes[NIdx].bClosed) { continue; }
			// ★ Стоимость шага 1:1 с оригиналом: ВЕС СОСЕДА × стоимость направления.
			const float Step = (bHaveCost ? Cost[NIdx] : kBaseWeight) * GDirCost[i];
			const float NewG = Nodes[Cur].G + Step;
			if (Nodes[NIdx].Parent == -1 || NewG < Nodes[NIdx].G)
			{
				Nodes[NIdx].Parent = Cur;
				Nodes[NIdx].G = NewG;
				Open.HeapPush({ NewG + Heuristic(Nx, Nz), NIdx }, FOpenPred());
				++Stats.Pushed;
			}
		}
	}

	const int32 EndIdx = bFound ? GoalIdx : BestIdx;
	Stats.bReachedGoal = bFound;
	Stats.CostG = Nodes[EndIdx].G;
	if (EndIdx == StartIdx)
	{
		Stats.MilliSec = (FPlatformTime::Seconds() - T0) * 1000.0;
		if (OutStats) { *OutStats = Stats; }
		return false;   // никуда не сдвинулись — маршрута нет
	}

	// ── Разворот цепочки parent (порт Pathfind_BuildCheckpointRoute @0xFBCE50, шаг 1) ────────────
	TArray<FIntPoint> Raw;
	for (int32 I = EndIdx; I != StartIdx; I = Nodes[I].Parent)
	{
		Raw.Add(FIntPoint(I % W, I / W));
		if (Nodes[I].Parent < 0 || Raw.Num() > W * H) { break; }
	}
	Algo::Reverse(Raw);
	Stats.RawLen = Raw.Num();

	// ── Сжатие по СМЕНЕ НАПРАВЛЕНИЯ (оригинал, шаг 2: точка добавляется только если dx/dz изменились) ──
	TArray<FIntPoint> Turns;
	{
		FIntPoint Prev = Start;
		FIntPoint PrevD(MAX_int32, MAX_int32);
		for (int32 K = 0; K < Raw.Num(); ++K)
		{
			const FIntPoint D(Raw[K].X - Prev.X, Raw[K].Y - Prev.Y);
			if (D != PrevD && K > 0) { Turns.Add(Prev); }
			PrevD = D;
			Prev = Raw[K];
		}
		Turns.Add(Raw.Last());
	}
	Stats.TurnPoints = Turns.Num();

	// ── LOS-сглаживание / string-pulling (оригинал, шаг 3; D5: применяем всегда) ─────────────────
	{
		FVector2D Anchor(Start.X + 0.5f, Start.Y + 0.5f);
		FIntPoint PrevPt = Start;
		for (int32 K = 0; K < Turns.Num(); ++K)
		{
			const FVector2D Cand(Turns[K].X + 0.5f, Turns[K].Y + 0.5f);
			if (!IsLineWalkable(Grid, Anchor, Cand))
			{
				// Линия перекрыта → фиксируем ПРЕДЫДУЩУЮ точку как узел и переносим опору в неё.
				if (K > 0) { OutTiles.Add(PrevPt); Anchor = FVector2D(PrevPt.X + 0.5f, PrevPt.Y + 0.5f); }
			}
			PrevPt = Turns[K];
		}
		OutTiles.Add(Turns.Last());
	}
	Stats.Waypoints = OutTiles.Num();
	Stats.MilliSec = (FPlatformTime::Seconds() - T0) * 1000.0;
	if (OutStats) { *OutStats = Stats; }
	return true;
}

bool AsdaPath::FindPathWorld(const UAsdaWalkGrid* Grid, const FVector2D& StartGame, const FVector2D& DstGame,
	TArray<FVector2D>& OutPoints, FAsdaPathStats* OutStats)
{
	OutPoints.Reset();
	// ★ Аудит №3 (B2): игровая точка → номер тайла через общую точку.
	const FIntPoint S = AsdaCoord::GameToTile(StartGame);
	const FIntPoint G = AsdaCoord::GameToTile(DstGame);

	// ★ ПРЯМОЙ ОТРЕЗОК БЕЗ ПОИСКА, если между точками нет препятствий — ровно поведение оригинала
	//   для короткого клика (game_main.c:1637: LOS-проверка перед запуском поиска). Экономит поиск
	//   на 90% кликов и оставляет обычное движение ПОБИТОВО прежним.
	if (!Grid || Grid->Width <= 0 || IsLineWalkable(Grid, StartGame / AsdaCoord::kTileUnits, DstGame / AsdaCoord::kTileUnits))
	{
		OutPoints.Add(DstGame);
		if (OutStats) { *OutStats = FAsdaPathStats(); }
		return true;
	}

	TArray<FIntPoint> Tiles;
	FAsdaPathStats Stats;
	if (!FindPathTiles(Grid, S, G, Tiles, &Stats) || Tiles.Num() == 0)
	{
		if (OutStats) { *OutStats = Stats; }
		return false;
	}
	OutPoints.Reserve(Tiles.Num());
	// ★ Аудит №3 (B2): центр тайла в игровых координатах.
	for (const FIntPoint& T : Tiles) { OutPoints.Add(AsdaCoord::TileCenterGame(T)); }
	// Последний узел — ТОЧНАЯ точка клика (если её тайл и есть финальный тайл маршрута и он проходим).
	if (!Stats.bGoalMoved && Tiles.Last() == G) { OutPoints.Last() = DstGame; }
	if (OutStats) { *OutStats = Stats; }
	return true;
}

// ── ★ ПРОВЕРКА: asda.TestPath <startX> <startZ> <goalX> <goalZ> (в ТАЙЛАХ) ───────────────────────
//   Печатает длину маршрута, стоимость, число развёрнутых узлов, время и — главное — сколько узлов
//   маршрута попало в НЕпроходимые тайлы (обязано быть 0) и во сколько раз путь длиннее прямой.
bool UAsdaPathfindLibrary::FindPath(const UAsdaWalkGrid* Grid, FIntPoint Start, FIntPoint Goal,
	TArray<FIntPoint>& OutTiles, FString& OutReport)
{
	OutTiles.Reset();
	if (!Grid) { OutReport = TEXT("нет сетки (DA_WalkGrid)"); return false; }
	FAsdaPathStats St;
	const bool bOk = AsdaPath::FindPathTiles(Grid, Start, Goal, OutTiles, &St);
	if (!bOk)
	{
		OutReport = FString::Printf(TEXT("(%d,%d)->(%d,%d): МАРШРУТА НЕТ (развёрнуто %d, %.2f мс)"),
			Start.X, Start.Y, Goal.X, Goal.Y, St.Expanded, St.MilliSec);
		return false;
	}
	// Длина ломаной по узлам + проверка проходимости КАЖДОГО узла и КАЖДОГО отрезка (шаг 0.5 тайла).
	float PolyLen = 0.f; int32 BadNodes = 0, BadSegs = 0;
	FVector2D Prev(Start.X + 0.5f, Start.Y + 0.5f);
	for (const FIntPoint& T : OutTiles)
	{
		const FVector2D Cur(T.X + 0.5f, T.Y + 0.5f);
		PolyLen += FVector2D::Distance(Prev, Cur);
		if (!Grid->IsTileWalkable(T.X, T.Y)) { ++BadNodes; }
		if (!AsdaPath::IsLineWalkable(Grid, Prev, Cur)) { ++BadSegs; }
		Prev = Cur;
	}
	const float Straight = FVector2D::Distance(FVector2D(Start.X + 0.5f, Start.Y + 0.5f),
		FVector2D(Goal.X + 0.5f, Goal.Y + 0.5f));
	OutReport = FString::Printf(TEXT("(%d,%d)->(%d,%d): узлов=%d сырых=%d поворотов=%d | длина=%.1f тайлов, ")
		TEXT("прямая=%.1f (x%.2f) | стоимость=%.1f развёрнуто=%d очередь=%d | %.2f мс | ")
		TEXT("узлов в стене=%d, перекрытых отрезков=%d%s%s"),
		Start.X, Start.Y, Goal.X, Goal.Y, St.Waypoints, St.RawLen, St.TurnPoints, PolyLen, Straight,
		Straight > 0.f ? PolyLen / Straight : 0.f, St.CostG, St.Expanded, St.Pushed, St.MilliSec,
		BadNodes, BadSegs,
		St.bReachedGoal ? TEXT("") : TEXT(" [НЕДОСТИЖИМА]"), St.bGoalMoved ? TEXT(" [цель сдвинута]") : TEXT(""));
	return true;
}

static void AsdaTestPathCmd(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
{
	if (!World) { return; }
	const UAsdaWalkGrid* Grid = nullptr;
	if (APawn* P = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		if (const AAsdaPlayerCharacter* C = Cast<AAsdaPlayerCharacter>(P)) { Grid = C->WalkGrid; }
	}
	if (!Grid) { Ar.Logf(TEXT("[путь] нет DA_WalkGrid у персонажа")); return; }
	if (Args.Num() < 4) { Ar.Logf(TEXT("asda.TestPath <sx> <sz> <gx> <gz>  (тайлы 0..511)")); return; }
	TArray<FIntPoint> Tiles; FString Report;
	UAsdaPathfindLibrary::FindPath(Grid, FIntPoint(FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1])),
		FIntPoint(FCString::Atoi(*Args[2]), FCString::Atoi(*Args[3])), Tiles, Report);
	Ar.Logf(TEXT("[путь] %s"), *Report);
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldArgsAndOutputDevice GAsdaTestPath(
	TEXT("asda.TestPath"),
	TEXT("Проверка поиска пути: asda.TestPath <sx> <sz> <gx> <gz> (тайлы). Печатает длину/стоимость/мс/проверку обхода."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&AsdaTestPathCmd));
#endif // ASDA_DEV_CONSOLE
