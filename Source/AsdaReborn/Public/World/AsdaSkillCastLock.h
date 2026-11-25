// Copyright (c) 2026 @sitxovski (Discord)
#pragma once
// ★★★ 05.11.2026 (задача владельца «во время навыка нельзя ходить, менять цель и пить зелья», сессия skill-cast-lock).
//   ЕДИНАЯ ТОЧКА «СВОЙ НАВЫК ЗАНЯТ» И ОДНО ОТЛОЖЕННОЕ ДЕЙСТВИЕ ПОСЛЕ НАВЫКА.
//
//   ЭТАЛОН (состояние действия своего персонажа `+47551`; 25 — заявка 5256 ушла, 24 — клип навыка после 5257/5271,
//   14 — каст-ожидание после 5270, 13/27 — конец каста полосой):
//   • `Input_WorldClickHandler@0x968890` (input.c:150-160): в 15/22/14/13/27 (и состоянии 547) мировой клик ВЕСЬ
//     игнорируется — ни выбора цели, ни хода; в 24/25 пикинг идёт (цель выбирается сразу), второй клик по мобу в 5/24 —
//     отложенная атака `sub_9356E7` (+52121 = 5), в 25 — ничего;
//   • ветка земли (input.c:568): ход запрещён в 15/25/14/13/27; в 24 `CGame_HandleGroundClickAttack@0x996480`
//     (game_main.c:6693-6708) case 5/24 ставит ОТЛОЖЕННОЕ ДЕЙСТВИЕ 16 (`sub_92D316 → sub_996880`: +52121 = 16, своя точка и
//     точка клика), case 14/15/25 — `return 0` (клик потерян); отложенное действие исполняет тик эффекта навыка в конце клипа
//     (`Game_IsPausedOrMenu` → `Player_UpdateAutoActionState@0xA249A0` case 0x10: покой, 4006 в точку клика);
//   • клавиши хода: главный цикл не зовёт `updatePlayerMovement` в 5/24 (game_main.c:1506), в 14/25 тот же
//     `CGame_HandleGroundClickAttack` возвращает 0 — хода нет, очереди нет; автобег гаснет в 5/24/22/25 (math_geom.c:442);
//   • приказ автопути — тот же `CGame_HandleGroundClickAttack` (24 → действие 16), а главный тик в этих состояниях
//     снимает заявку автопути (LABEL_111, game_main.c:1668-1720);
//   • эмоция (`UI_DispatchTargetContextMenu`, дизасм 0xC53349) — только при состоянии действия 0;
//   • Tab (`targetPick_updateSelection@0xE3E990`) и предметы (`Item_UseSelectedAndSend@0xD0A060`, панель
//     `itemUseDoubleClick@0xD12580`) состояния навыка НЕ проверяют — у оригинала цель меняется и зелье пьётся посреди навыка.
//   СЕРВЕР (GameServer `CharMove@0x544EB0`, `CharSpendItemUse@0x4D8C40`) состояния навыка не проверяет: 4006 посреди навыка
//   принимается (оригинал его просто не шлёт), навык во время хода — отказ 24 (`RecvChar_SkillUse@0x46F900`).
//
//   ПОРТ ДО ПРАВКИ: клик по земле во время навыка сразу давал ход (гейта не было), состояние 25 клавиши хода не видели,
//   клик по мобу посреди клипа сразу переключал бой на новую цель, эмоция ложилась поверх клипа навыка.
//
//   КЛЮЧИ (1 — по умолчанию; 0 — прежнее поведение порта):
//   • `asda.Skill.CastLockMove` [ОРИГИНАЛ] — клик по земле/приказ автопути в 24 откладываются до конца навыка (действие 16),
//     в 25/14/13/27 — отбрасываются; в 14/13/27 мировой клик целиком игнорируется; клавиши хода видят 25/14;
//   • `asda.Skill.CastLockEmote` [ОРИГИНАЛ] — эмоция во время навыка не играет (молча);
//   • `asda.Skill.CastLockTarget` [ИСПРАВЛЕНИЕ, просьба владельца] — клик по мобу/игроку/NPC/луту и клавиши цели
//     59/60/62/64 во время навыка цель НЕ меняют: нажатие запоминается и исполняется сразу после навыка;
//   • `asda.Skill.CastLockItems` [ИСПРАВЛЕНИЕ, просьба владельца] — расходник (5028: зелья, свитки, маунты) во время навыка
//     не применяется: запоминается и применяется сразу после навыка.
//   Слотов два: действие (клик/автопуть/клавиша — последнее нажатие вытесняет прежнее, как одно поле +52121 эталона) и
//   предмет (последний). Сброс без исполнения: смерть, смена карты/персонажа, обездвиживание (для хода), 10 с без конца навыка.
#include "CoreMinimal.h"

class UGameInstance;
class AActor;
class UPrimitiveComponent;
class AAsdaPlayerController;
class UAsdaInventorySubsystem;

namespace AsdaSkillCastLock
{
	/** Фаза своего навыка в числах эталона: 0 — свободен, 25 — заявка в пути, 14 — каст-ожидание, 13/27 — конец каста
	 *  полосой, 24 — клип навыка. Ключи не учитываются. */
	ASDAREBORN_API int32 SelfSkillPhase(const UGameInstance* GI);

	/** Ключ `asda.Skill.CastLockMove` (аксессор — имя ключа в exe вне журнала не попадает). */
	ASDAREBORN_API bool MoveLockOn();
	/** Ключ `asda.Skill.CastLockTarget`. */
	ASDAREBORN_API bool TargetLockOn();
	/** Ключ `asda.Skill.CastLockItems`. */
	ASDAREBORN_API bool ItemLockOn();

	/** Фаза навыка для `KbEtalonActionState` (0, если ключ хода выключен или навыка нет). */
	ASDAREBORN_API int32 PhaseForActionState(const UGameInstance* GI);

	/** Гейт мирового клика (`OnLeftClick`, до выбора цели и хода). true — клик поглощён (отброшен или отложен). */
	ASDAREBORN_API bool GateWorldClick(AAsdaPlayerController* PC, bool bDouble);

	/** Идёт повтор отложенного клика: гейт окон `OnLeftClick` не смотрит курсор, пикинг берёт запомненный результат. */
	ASDAREBORN_API bool IsReplayingClick();
	/** Повтор без экранной точки (камера сдвинулась с момента клика): `GetClickScreenPos` отвечает «нет мыши». */
	ASDAREBORN_API bool IsReplayNoScreen();
	/** Запомненный результат пикинга для повтора (`PickForClick`): false — повтора нет. `OutKind` — `EAsdaPickKind`. */
	ASDAREBORN_API bool GetReplayPick(uint8& OutKind, AActor*& OutActor, FVector& OutPoint, UPrimitiveComponent*& OutComp);
	/** Точка хода под экранной точкой — луч по земле .TER (как ветка земли `OnLeftClick`, ключ `asda.Move.ClickRayTerrain`); false —
	 *  ключ выключен или земли под лучом нет. Определена в AsdaPlayerController.cpp (там закрытый расчёт луча). */
	ASDAREBORN_API bool TerrainPointUnderScreen(AAsdaPlayerController* PC, const FVector2D& Screen, FVector& OutUe);

	/** Гейт приказа автопути игрока (`MoveToGamePoint`). true — приказ поглощён (отложен или отброшен). */
	ASDAREBORN_API bool GateMapMove(AAsdaPlayerController* PC, const FVector2D& DstGame);

	/** Гейт клавиш цели (`ExecuteCommand` 59/60/62/64). true — нажатие отложено до конца навыка. */
	ASDAREBORN_API bool GateTargetCommand(AAsdaPlayerController* PC, int32 Cmd);

	/** Гейт применения расходника (`UseConsumable`, пакет 5028). true — применение отложено до конца навыка. */
	ASDAREBORN_API bool GateItemUse(UAsdaInventorySubsystem* Inv, int32 ContainerId, int32 CellIndex, int32 TargetUserIdx, int32 ItemCode);

	/** ★ 05.11.2026 Ключ `asda.Input.ClickQueue` [ИСПРАВЛЕНИЕ]: мировой клик, отброшенный защитой от флуда оригинала (бит 2 — 4006 ждёт
	 *  ответа 4007; троттл хода 150 мс, input.c:570), не теряется: последнее такое нажатие ложится в тот же слот действия (вытеснение)
	 *  и исполняется, как только гейт открылся. Второе нажатие двойного по земле по-прежнему хода не даёт (решение владельца 09.10, п. 4):
	 *  двойной признаётся только в той же точке (~4 px), куда персонаж уже идёт. */
	ASDAREBORN_API bool ClickQueueOn();
	/** Бит 2 в `OnLeftClick`: запомнить клик (пикинг сейчас) до ответа 4007. true — поглощён (ключ включён). */
	ASDAREBORN_API bool QueueBlockedClick(AAsdaPlayerController* PC, bool bDouble);
	/** Троттл 150 мс в `GroundClickMove`: запомнить точку хода (UE) до конца троттла. true — поглощён (ключ включён). */
	ASDAREBORN_API bool QueueGroundMove(AAsdaPlayerController* PC, const FVector& PointUe);

	/** Гейт эмоции (`PlayEmotion`): true — свой навык идёт, эмоция не играет (эталон: только состояние 0). */
	ASDAREBORN_API bool EmoteBlocked(const UGameInstance* GI);

	/** Кадр контроллера (`PlayerTick`, до боевого тика): исполнить отложенное после навыка или сбросить его. */
	ASDAREBORN_API void Tick(AAsdaPlayerController* PC);

	/** Сбросить оба слота без исполнения (`Why` — в журнал). */
	ASDAREBORN_API void Reset(const TCHAR* Why);

	/** ★ R5-fix6 (13.11.2026): новое намерение хода игрока (ход клавишами) вытесняет отложенный очередью клик хода — он не исполняется
	 *  позже. Только клик, поставленный защитой от флуда; ключ `asda.Input.ClickQueueSupersede`. Разбор — у определения.
	 *  ★ R5-fix8 (14.11.2026): `MinKey` — наименьшее значение ключа, при котором это намерение вытесняет: 1 — ход клавишами,
	 *  2 — прочие новые намерения игрока (навык, сесть/встать, подобрать, атака клавишей). */
	ASDAREBORN_API void SupersedeQueuedMove(const TCHAR* Why, int32 MinKey = 1);

	/** Для самотеста: вид отложенного действия (0 — нет, 1 — клик, 2 — автопуть, 3 — клавиша) и есть ли отложенный предмет. */
	ASDAREBORN_API int32 DebugPendingActionKind();
	/** Для самотеста: есть ли отложенный предмет. */
	ASDAREBORN_API bool DebugHasPendingItem();
	/** Для самотеста: сколько отложенных действий исполнено / отброшено за сессию. */
	ASDAREBORN_API int32 DebugExecutedCount();
	/** Для самотеста: сколько нажатий отброшено гейтами и сброшено из слотов за сессию. */
	ASDAREBORN_API int32 DebugDroppedCount();
	/** Для самотеста: мировой клик тем же путём, что от мыши (`OnLeftClick`), с подменённой экранной точкой. */
	ASDAREBORN_API void DebugWorldClick(AAsdaPlayerController* PC, const FVector2D& Screen, bool bDouble);
	/** Для самотеста (R5-fix8, шов гейта мирового клика): держать подменённую точку мирового клика несколько кадров — когда щелчок
	 *  идёт событиями мыши Slate и исполняется в следующем опросе ввода. Пик и гейт «над окном интерфейса» берут эту точку, а не
	 *  курсор ОС. `Screen == nullptr` — снять подмену. */
	ASDAREBORN_API void DebugSetClickPoint(AAsdaPlayerController* PC, const FVector2D* Screen);
	/** Для самотеста: клавиша раскладки тем же путём, что от клавиатуры (`ExecuteCommand`). */
	ASDAREBORN_API void DebugExecuteCommand(AAsdaPlayerController* PC, int32 Cmd);
	/** Для самотеста: состояние действия `KbEtalonActionState`. */
	ASDAREBORN_API int32 DebugActionState(const AAsdaPlayerController* PC);
	/** Для самотеста: вид попадания мирового клика (`EAsdaPickKind`) в экранной точке и актор попадания. */
	ASDAREBORN_API uint8 DebugPickKindAt(AAsdaPlayerController* PC, const FVector2D& Screen, AActor*& OutActor);
	/** Для самотеста (R5-fix8): точка попадания мирового клика (UE) в экранной точке — туда пойдёт персонаж по клику «земля».
	 *  Возвращает вид попадания (`EAsdaPickKind`). */
	ASDAREBORN_API uint8 DebugPickPointAt(AAsdaPlayerController* PC, const FVector2D& Screen, FVector& OutUe);
	/** Для самотеста: конечная точка текущего маршрута (game XZ); false — маршрута нет. */
	ASDAREBORN_API bool DebugRouteFinal(const AAsdaPlayerController* PC, FVector2D& OutGame);
}
