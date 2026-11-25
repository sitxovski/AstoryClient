// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ СТРЕЛА НА ТЕТИВЕ — ЖИЗНЕННЫЙ ЦИКЛ (05.11.2026, баг игрока «стрела торчит из руки после смены оружия»). [ИСПРАВЛЕНИЕ]
//   Эталон держит стрелу на тетиве в пуле из шести слотов персонажа (`effectSlots[113].gap_11C`, флаг «на тетиве» slot[124],
//   «в полёте» slot[255], занятость [i+4086]). Снимают её:
//     • смена/снятие оружия — `charEffectSlots_ClearSix@0xA58CC0` из `CCharacter_LoadEquipPart@0x9E6D80` (entity_char.c:14357,
//       14680) и `CCharacter_SetupModelFromItem@0x9E97A0` (entity_char.c:15064, 15636; его же зовёт тик уборки оружия за спину —
//       город/седло/эмоция);
//     • выход из атаки — `clearActiveEffectSlots@0xA585F0` (снимает слоты, стрела которых НЕ в полёте) из `CCharacter_SetAction`
//       case 0 (покой/стоп, entity_char.c:16589), case 14 (16957), 22/24 (17143) и `CChar_SetState` case 14 (19707), 22/24 (19770);
//     • создание/уничтожение персонажа и сброс менеджера при смене карты — `playerCharObject_Init/Cleanup`,
//       `charManager_ResetKeepLocalPlayer` (entity_char.c:22023, 21935, 7437).
//   Порт показывал стрелу компонентом `AsdaNockedArrow` на кости «8_b1» и гасил её ТОЛЬКО таймером после серии по мобу:
//   смена оружия посреди боя, PvP-серия (таймера нет вовсе) и выстрел после смены оружия оставляли меш навсегда — у нового
//   оружия кость «8_b1» тела = левая кисть. Ключ `asda.Bow.NockLifecycle` (1 = как эталон, 0 = прежнее поведение порта).
#pragma once

#include "CoreMinimal.h"

class AActor;

namespace AsdaNockedArrow
{
	// Ключ `asda.Bow.NockLifecycle` включён (снятие стрелы по событиям эталона).
	ASDAREBORN_API bool LifecycleOn();
	// Снять стрелу на тетиве с персонажа (аналог очистки пула эталона); `Why` — причина для журнала. Без компонента — ничего.
	ASDAREBORN_API void Hide(AActor* Character, const TCHAR* Why);
	// Видна ли сейчас стрела на тетиве у персонажа (для самотестов и журнала).
	ASDAREBORN_API bool IsVisible(const AActor* Character);
}
