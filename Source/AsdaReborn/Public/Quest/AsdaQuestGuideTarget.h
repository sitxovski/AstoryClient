// Copyright (c) 2026 @sitxovski (Discord)
// ★ [ИСПРАВЛЕНИЕ] 31.10.2026 (баг 6 «Следовать ведёт не туда»): ОБЩАЯ ТОЧКА «где цель строки гида задания».
//
// Гид `QDT.bin` (`DT_QuestGuide`) даёт строке маршрута клетку (X, Y). У 35 строк оригинала координат нет (−1): цель —
//   моб без точки (22 строки: дворец королевы, замок шахмат, горы Пламении, Флэммио 566/568) или NPC (13 строк). Эталон
//   на такой строке либо уводил персонажа в точку −50 (опечатка `a4 != 1`), либо (порт, FIX-ORIG D1) не вёл никуда,
//   хотя карта мира (значки целей 31.10) показывает места этих мобов. Здесь — место такой цели для кнопки «Следовать»
//   журнала (705…708) из ТОГО ЖЕ источника, что у значков карты мира, чтобы они не расходились:
//     моб — зоны спавна `DT_QuestTargetArea` (серверные `Monster.ai` + `<карта>.svr`, `Tools/asda_quest_target_areas.py`);
//       карта мира у такого шага показывает все эти зоны, кнопка ведёт в одну из них;
//     NPC — запись `DT_NpcQuestNpc` (`npc_list.BIN`) этого NPC на карте строки (значка на карте мира у разговорного шага нет:
//       значки — только у отслеживаемых заданий, а шаг без строк целей журнал в трекер не ставит).
//   И NPC СДАЧИ выполненного шага (`TurnInNpcs`) — общая точка кнопок журнала и значка «NPC сдачи» карты мира.
//   Строки С координатами сюда не попадают: их точка — гид, 1:1 с эталоном.
// Разбор и аудит всех строк — `Reports/bug_quest_follow/` (`Tools/asda_quest_follow_audit.py`).
#pragma once
#include "CoreMinimal.h"

class UAsdaQuestMarkerSubsystem;   // вычислитель условий ячейки сдачи (`EvalSnqIctCond`)

namespace AsdaQuestGuideTarget
{
	// Одно место цели: карта, клетка, вес (число точек спавна зоны; у NPC — 1).
	struct FPlace
	{
		int32 MapId = -1;      // карта места
		int32 TileX = -1;      // клетка карты
		int32 TileY = -1;
		int32 Weight = 0;      // точек спавна в зоне (выбор «самой населённой» на чужой карте)
	};

	// Ключ `asda.Quest.FollowNoCoordTarget`: 1 — строка гида без координат ведёт к месту цели [ИСПРАВЛЕНИЕ]; 0 — как было.
	ASDAREBORN_API bool FollowNoCoordOn();

	// Все места цели строки гида без координат. Kind/TargetId — поля строки (`+5`, `+8`), RouteMapId — её карта (`+12`).
	//   Моб: зоны его типа (строка + 1) на карте строки → зоны любых засчитываемых за шаг мобов на карте строки →
	//   зоны его типа на других картах. NPC: его запись на карте строки → на любой карте.
	ASDAREBORN_API void Places(int32 QuestNo, int32 Stage, int32 Kind, int32 TargetId, int32 RouteMapId, TArray<FPlace>& Out);

	// ★ [ИСПРАВЛЕНИЕ] 31.10.2026 (решение владельца по багу 6): цель шага ВЫПОЛНЕНА — кнопки маршрута показывают NPC сдачи
	//   («=> <NPC> - <карта>, x:…, y: …», формат строки оригинала) и ведут к нему; источник тот же, что у значка «NPC сдачи»
	//   карты мира. Ключ `asda.Quest.FollowDoneToNpc`: 1 — так; 0 — как в оригинале (строки гида шага, к мобам).
	ASDAREBORN_API bool FollowDoneToNpcOn();

	// NPC, который примет сдачу задания. bByStep = true (ключ `asda.Quest.FollowDoneToNpc` 1): правило СЕРВЕРА (NpcServer
	//   `Process_Quest@0x420C10`) по таблице `DT_QuestTurnIn` — ячейка (задание, шаг +66) == (задание, стадия слота Stage) и хоть
	//   одно её условие проходит (клиентский вычислитель облачка `EvalSnqIctCond`); место — серверный спавн NPC.
	//   bByStep = false — прежний отбор значка «NPC сдачи» карты мира: любая ячейка задания клиентского `DT_NpcSnq` с ключом > 0,
	//   место из `npc_list`. Порядок — таблицы, без повторов.
	struct FTurnIn
	{
		int32 NpcId = -1;      // NPC, который примет сдачу
		int32 Action = -1;     // действие ячейки (2 — завершает, 1 — стадия +1, 0 — выдача предмета); −1 — прежний отбор
		FPlace Place;          // где он стоит
	};
	ASDAREBORN_API void TurnInNpcs(const UAsdaQuestMarkerSubsystem* QM, int32 QuestNo, int32 Stage, bool bByStep, TArray<FTurnIn>& Out);

	// Один NPC из кандидатов (сервер принимает у любого — законные пары 104/2, 112/0): на своей карте ближайший
	//   к персонажу, иначе первый. false — кандидатов нет.
	ASDAREBORN_API bool PickTurnIn(const TArray<FTurnIn>& Npcs, int32 CurMapId, const FIntPoint& PlayerTile, FTurnIn& Out);

	// ★ [ИСПРАВЛЕНИЕ] 31.10.2026 (bug-quest-follow, патч NpcServer N22): сколько ячеек «ВЫДАЁТ»/«ПРИНИМАЕТ» NPC перебирают
	//   облачко сдачи/выдачи (`BuildQuestLists`) и «!» над NPC (`CollectDeliverableForNsr`). Ключ `asda.Npc.CellsBeyond10`:
	//   1 — все ячейки строки `DT_NpcSnq` (слоты ≥ 10 — таблица расширения `QuestNpcTableExt.Bin`), строк облачка без предела,
	//   высота блока ≤ 10 строк с прокруткой; 0 — как было (эталон: 10 ячеек, ≤ 10 строк в блоке, высота по числу строк).
	ASDAREBORN_API bool CellsBeyond10On();
	// Предел ячеек/строк облачка: INT32_MAX при ключе 1, 10 при ключе 0.
	ASDAREBORN_API int32 NpcCellLimit();

	// Место NPC (карта −1 = любая): запись `DT_NpcQuestNpc` (`npc_list.BIN`).
	ASDAREBORN_API void NpcPlaces(int32 NpcId, int32 MapId, TArray<FPlace>& Out);

	// Одна точка для автопути: на текущей карте — ближайшее к персонажу место (PlayerTile), иначе — самая населённая зона
	//   на карте строки (на ней и кончится межкартовый автопуть). false — мест нет.
	ASDAREBORN_API bool PickForFollow(const TArray<FPlace>& Places, int32 CurMapId, const FIntPoint& PlayerTile, FPlace& Out);
}
