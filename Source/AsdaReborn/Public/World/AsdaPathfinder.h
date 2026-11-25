// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPathfinder — ПОИСК ПУТИ ПО СЕТКЕ ПРОХОДИМОСТИ (порт клиентского пути AsdaStory).
//
// ★ ЧТО ДЕЛАЕТ ОРИГИНАЛ (реверс, адреса — RE/docs/client/logic/movement_pathing.md §1):
//   mapClick_BeginPathMove @0xFBC9A0 — вход по клику (тайлы 0<t<512, проверка walkable у ЦЕЛИ),
//     start = тайл игрока (fxEmitter_SetSpawnPosGuarded @0xFBCDD0 пишет this[28]/[29]),
//     goal  = тайл клика (this[30]/[31]).
//   pathFindAStar @0xFBC3B0 — сам поиск: 8 соседей (dword_14690F8 @0x14690F8:
//     i0(0,−1) i1(1,0) i2(0,1) i3(−1,0) | i4(1,−1) i5(1,1) i6(−1,1) i7(−1,−1)),
//     стоимость шага = ВЕС СОСЕДА(cell+0x14) × flt_1469138[i] (@0x1469138 = [1,1,1,1, √2,√2,√2,√2]),
//     g накопительно (cell+4), реоткрытие при g_new < g, ключ очереди = g_new + g_new = **2·g**
//     (эвристики в ключе НЕТ → это взвешенный Dijkstra, а не A*).
//   grid2dInitCells512 @0xFBBB30 — базовый вес ячейки = 1.0, walkable = 1.
//   terrainStampCollisionCost @0xFBDF70 — «штамп» препятствия: центр → walkable = 0,
//     кольцо 5×5 вокруг: внешний слой (|dx|==2 || |dy|==2) вес += 1.0, внутренний вес += 2.0.
//   Pathfind_BuildCheckpointRoute @0xFBCE50 — цепочка parent от цели к старту (список строится
//     push_front → выходит в порядке старт→цель), сжатие по СМЕНЕ НАПРАВЛЕНИЯ, затем при длине
//     > 15 точек (или включённом авто-следовании) — LOS-сглаживание mapTraceWalkLineBlocked @0xFBD5A0
//     (шаг 0.5 тайла, блок при cell[0x14] > 1.0).
//   Готовый список чек-пойнтов = self+43 (= dword_351F9AC); тик игры (game_main.c:1592-1644)
//     берёт ПЕРЕДНЮЮ точку, шлёт к ней ход, по достижении её тайла делает pop_front и шлёт к следующей.
//     ⇒ **оригинал шлёт СЕРИЮ ходов, по одному на узел маршрута**, а не одну точку.
//
// ★ ЧТО ЗДЕСЬ СДЕЛАНО ИНАЧЕ И ПОЧЕМУ (осознанные отступления, см. reports/pathfinding_re.md):
//   D1. Ключ очереди = g + h (октильная эвристика, h допустимая при мин.весе 1.0) вместо 2·g.
//       Дают ОДИНАКОВЫЙ оптимальный путь, но A* разворачивает на 1-2 порядка меньше узлов.
//   D2. Лимит развёрнутых узлов (кап). Оригинал при недостижимой цели заливает весь связный
//       компонент (до 262144 ячеек × 8) прямо в кадре — заметный фриз.
//   D3. Запрет «срезки углов» по диагонали (обе ортогонали должны быть проходимы) — оригинал
//       позволял просочиться между двумя блоками по диагонали.
//   D4. Цель непроходима → ближайший проходимый тайл (спираль). Оригинал просто отказывал.
//   D5. LOS-сглаживание применяется ВСЕГДА (у оригинала только при длине > 15 узлов).
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AsdaPathfinder.generated.h"

class UAsdaWalkGrid;

// Диагностика одного поиска (для лога/консольной проверки).
struct FAsdaPathStats
{
	int32 Expanded = 0;        // сколько узлов извлечено из очереди (закрыто)
	int32 Pushed = 0;          // сколько раз клали в очередь
	int32 RawLen = 0;          // длина сырой цепочки parent (в тайлах)
	int32 TurnPoints = 0;      // после сжатия по смене направления
	int32 Waypoints = 0;       // после LOS-сглаживания (итог)
	float CostG = 0.f;         // накопленная стоимость до цели
	double MilliSec = 0.0;     // время поиска, мс
	bool bReachedGoal = false; // цель достигнута (иначе — лучший приближённый узел)
	bool bGoalMoved = false;   // цель была непроходима, взят ближайший проходимый тайл
};

namespace AsdaPath
{
	// Вес ячейки по «штампу» препятствий (порт terrainStampCollisionCost @0xFBDF70).
	// База 1.0; вокруг каждого НЕпроходимого тайла кольцо 5×5: внутренний слой +2.0, внешний +1.0.
	// Считается один раз на карту и кэшируется по указателю сетки.
	ASDAREBORN_API const TArray<float>& GetCostGrid(const UAsdaWalkGrid* Grid);

	// Сбросить кэш весов (смена карты / переимпорт ассета).
	ASDAREBORN_API void InvalidateCostGrid();

	// ★ ПОИСК ПУТИ в ТАЙЛАХ. Возвращает true, если маршрут построен (в т.ч. приближённый).
	// OutTiles — узлы маршрута БЕЗ стартового тайла, последний = целевой тайл.
	ASDAREBORN_API bool FindPathTiles(const UAsdaWalkGrid* Grid, const FIntPoint& Start, const FIntPoint& Goal,
		TArray<FIntPoint>& OutTiles, FAsdaPathStats* OutStats = nullptr);

	// ★ ПОИСК ПУТИ в МИРОВЫХ координатах игры (game XZ, 100 юнитов = тайл).
	// OutPoints — точки центров тайлов (t*100+50), КРОМЕ последней: последняя = ТОЧНАЯ точка клика
	// DstGame (чтобы персонаж встал ровно туда, куда кликнули, а не в центр тайла).
	ASDAREBORN_API bool FindPathWorld(const UAsdaWalkGrid* Grid, const FVector2D& StartGame, const FVector2D& DstGame,
		TArray<FVector2D>& OutPoints, FAsdaPathStats* OutStats = nullptr);

	// Прямая видимость по сетке (порт mapTraceWalkLineBlocked @0xFBD5A0): шаг 0.5 тайла,
	// блок на первом непроходимом тайле. true = линия свободна.
	ASDAREBORN_API bool IsLineWalkable(const UAsdaWalkGrid* Grid, const FVector2D& FromTile, const FVector2D& ToTile);
}

// Доступ к поиску пути из Blueprint и из headless-Python (проверка маршрутов и замер производительности
//   на РЕАЛЬНОМ игровом коде, а не на сторонней реализации).
UCLASS()
class ASDAREBORN_API UAsdaPathfindLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	// Построить маршрут по тайлам. OutReport — готовая строка с числами (длина/стоимость/узлы/мс/
	//   сколько узлов в стене и сколько отрезков перекрыто; последние два обязаны быть 0).
	UFUNCTION(BlueprintCallable, Category="Asda|Path")
	static bool FindPath(const UAsdaWalkGrid* Grid, FIntPoint Start, FIntPoint Goal,
		TArray<FIntPoint>& OutTiles, FString& OutReport);
};
