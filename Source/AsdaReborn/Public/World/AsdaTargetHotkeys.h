// Copyright (c) 2026 @sitxovski (Discord)
// AsdaTargetHotkeys — клавиши раскладки «атаковать цель» (cmd 59) и «выбрать ближайшего врага» (cmd 60). [ОРИГИНАЛ]
//
// ★ 25.10.2026 (баг игроков «ATTACK / NEAR_ENEMY / пробел-подбор не работают ни по умолчанию, ни после переназначения»).
//   ПРИЧИНА: у `AAsdaPlayerController::ExecuteCommand` не было веток 56/59/60 — команды падали в `default:` →
//   `OnGameCommand`, а у него для этих кодов нет ни одного подписчика. Раскладка распознавалась и сохранялась верно,
//   исполнять было нечего. Разбор и аудит всех действий окна — `Reports/keybinds/AUDIT.md`.
//
// ЭТАЛОН (сборка IDB 13 344 768 б):
//   · cmd 59 «ACTION ATTACK» — `input_HotkeyDispatchWithContextMenu@0xE3DAF0` (input.c:4481) →
//     `UI_DispatchTargetContextMenu(player, 2, 4)` (ui_core.c:32201) → `Input_HandleTargetInteractClick@0xC55CC0`
//     (input.c:1320-1682). Та же функция — у пункта «атака» вкладки «Функции» окна эмоций (код 204) и у
//     `inputTargetInteractByDistance@0xC56B90` (после своего навыка). Тело — `AttackSelectedTarget`.
//   · cmd 60 «ACTION TARGET NEAR_ENEMY» — `hotkeyDispatchAction@0xE3CD90` case 60 → `targetPick_updateSelection@0xE3E990`
//     (input.c:4678-4825). Тело — `SelectNearestEnemy`.
//   · cmd 56 «ACTION GET ITEM» — обёртка `0xE3DAF0` (`if (g_GameState == 1)`) → `UI_DispatchTargetContextMenu(2, 1)` →
//     `itemPickupGroundItem@0xC54ED0`. Порт: `UAsdaCharaInfoSubsystem::PlayEmotion(2/1)` → `PickupGroundItemEtalon` (уже были).
//
// Ключ отката всех трёх веток — `asda.Keys.TargetActions 0` (прежнее: команды уходили в `OnGameCommand`, ничего не делали).
#pragma once

#include "CoreMinimal.h"

class AAsdaPlayerController;

namespace AsdaTargetHotkeys
{
	// Ветки клавиш 56/59/60 включены (ключ `asda.Keys.TargetActions`, 1 по умолчанию; 0 — прежнее поведение).
	ASDAREBORN_API bool IsEnabled();
	// ★ 25.10.2026: клавиши отряда/цели 48–50, 61–64 и функции эмоций 1/0…1/3, 2/5, 2/6 исполняются (ключ `asda.Keys.PartyActions`,
	//   1 по умолчанию; 0 — прежнее: команды уходили в `OnGameCommand`, эмоции — «не портировано»).
	ASDAREBORN_API bool IsPartyActionsEnabled();

	// Порт `Input_HandleTargetInteractClick@0xC55CC0`: атаковать ВЫБРАННУЮ цель (моб — обычный бой, персонаж — PvP 4201).
	//   Гейты эталона по порядку (склад-свиток, авто-бур 111204, удочка 110707, нет оружия 1004, элитный магазин 6811,
	//   окна ремесла, цель мертва/уже бьём её, занятые состояния). Бой ведёт ТОТ ЖЕ движок, что второй клик мышью
	//   (`EngageMobFromClick` / `EngagePlayerFromClick`). true — атака начата (или уже идёт по этой цели).
	ASDAREBORN_API bool AttackSelectedTarget(AAsdaPlayerController* PC, const TCHAR* Why);

	// Порт `targetPick_updateSelection@0xE3E990`: выбрать ближайшего врага. Свой PvP-режим выключен — ближайший ЖИВОЙ моб
	//   (3D-квадрат расстояния < 2 300 000, текущая цель-моб пропускается — повторное нажатие чередует двух ближайших);
	//   включён — ближайший персонаж-противник по правилам режима (2D-квадрат < 2 000 000). true — цель выбрана.
	ASDAREBORN_API bool SelectNearestEnemy(AAsdaPlayerController* PC, const TCHAR* Why);
}
