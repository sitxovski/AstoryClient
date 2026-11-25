// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEquipCellText — ОБЩАЯ ТОЧКА надписи на ячейке НАДЕТОГО предмета (08.11.2026, пачка 17, баг 6).
//
// Правило — эталон сумки `itemUI_DrawEquipSlotIcon@0xC6B1B0` (порт: `UAsdaInventorySubsystem::UpdateEquipVisual`):
//   • запись+24 (уровень заточки) != 0 ⇒ «+N%», N = запись+27 (i16); цвет — по уровню заточки (`asda.Inven.EnchantTextGradeColor`);
//   • слот 10 (стрелы) ⇒ количество записи+9 белым; при количестве > 0 оно стоит поверх «+N%».
//   Пустая запись или `asda.Inven.EquipCellText 0` ⇒ пустая строка.
// Потребители: надетые ячейки сумки и окно сведений о персонаже 4200 (11 записей по 57 б из 7006 / своего контейнера 3).
#pragma once

#include "CoreMinimal.h"

struct FAsdaItemInstance;

namespace AsdaEquipCellText
{
	/** Текст надетой ячейки `Slot` (0…10) для записи `I`; `OutColorArgb` — цвет ARGB (sRGB), 0 = текста нет. */
	ASDAREBORN_API FString Build(int32 Slot, const FAsdaItemInstance& I, uint32& OutColorArgb);
}
