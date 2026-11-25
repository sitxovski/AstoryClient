// Copyright (c) 2026 @sitxovski (Discord)
// ★ ЕДИНАЯ таблица цветов грейда предмета/титула (drop-палитра оригинала).
//
//   ЭТАЛОН: CItemManager_GetDropColor @0x9C45C0 (RE/etalon/item_inventory.c:1722):
//       if (a2 > 9) return this->dropColorTable;        // индекс > 9 → запись [0] (белый)
//       else        return *(DWORD*)&this->charStr1[4*a2 + 12];
//   Инициализация записей [0..8] — CItemManager_InitDropColors @0x9C4510 (структура CItemManager
//   +0x0C38 dropColorTable .. +0x0C58 dropColor8).
//   Запись [9] лежит на +0x0C5C, т.е. в НАЧАЛЕ соседнего массива gradeColor (его [0] пишет
//   colorTableInitDefaults @0x9C4760 значением -1644826 = 0xFFE6E6E6). Индекс 9 в оригинале
//   ДОСТИЖИМ (граница `> 9`), поэтому это не «выход за массив», а реальная 10-я запись палитры.
//
//   ПОДТВЕРЖДЕНО дампом live.dmp (таблица @0x2907A28, 16 dword подряд):
//       [0] FFFFFFFF  [1] FFFFFF46  [2] FFFF96DC  [3] FF96FF32  [4] FFFF7800
//       [5] FF4141FF  [6] FFBE50FF  [7] FFFA1E46  [8] FF9BEBEB  [9] FFE6E6E6
//       (далее [10]=FFE0D845, [11]=FFAE7293 … — это уже gradeColor[1..], в GetDropColor недостижимо)
//
//   Единственная точка правды: AAsdaLootActor::GradeColor и UAsdaNameplateComponent::GradeColor
//   делегируют СЮДА (до 2026-08-01 у плашки было 9 записей с Clamp(0,8) → грейд 9 красился
//   голубым вместо светло-серого, а у лута — правильно).
#pragma once
#include "CoreMinimal.h"

struct FAsdaGradeColor
{
	// Число записей палитры (индексы 0..9 достижимы в оригинале).
	static constexpr int32 Num = 10;

	// sRGB-байты записи палитры (как в памяти оригинала, без альфы).
	static const FColor& Raw(int32 Grade)
	{
		static const FColor kPalette[Num] = {
			FColor(0xFF, 0xFF, 0xFF, 0xFF),   // 0 белый (и любой индекс > 9)
			FColor(0xFF, 0xFF, 0x46, 0xFF),   // 1 жёлтый
			FColor(0xFF, 0x96, 0xDC, 0xFF),   // 2 розовый
			FColor(0x96, 0xFF, 0x32, 0xFF),   // 3 зелёный
			FColor(0xFF, 0x78, 0x00, 0xFF),   // 4 оранжевый
			FColor(0x41, 0x41, 0xFF, 0xFF),   // 5 синий
			FColor(0xBE, 0x50, 0xFF, 0xFF),   // 6 фиолетовый
			FColor(0xFA, 0x1E, 0x46, 0xFF),   // 7 алый / красный
			FColor(0x9B, 0xEB, 0xEB, 0xFF),   // 8 бледно-циан / голубой
			FColor(0xE6, 0xE6, 0xE6, 0xFF),   // 9 светло-серый (запись @+0x0C5C, live.dmp 0x2907A4C)
		};
		// Оригинал: индекс вне [0..9] → запись [0]. Отрицательный (нет данных) — тоже [0].
		const int32 I = (Grade >= 0 && Grade < Num) ? Grade : 0;
		return kPalette[I];
	}

	// Цвет для UI/материалов UE.
	static FLinearColor Get(int32 Grade)
	{
		return FLinearColor(Raw(Grade));
	}

	// ★ Цвет грейда как hex-аргумент inline-тега <FC=AARRGGBB> (разметка FAsdaMarkup, sRGB-байты).
	//   Эталон САМ так строит имя предмета: item_inventory.c:45330
	//     sprintf(nameStr, "%s + <FC=%08x>%d</F>", name, gradeColor, enhanceLevel);
	//   т.е. тег с 8-значным ARGB — родной способ оригинала подкрасить кусок строки.
	static FString HexARGB(int32 Grade)
	{
		const FColor C = Raw(Grade);
		return FString::Printf(TEXT("FF%02X%02X%02X"), C.R, C.G, C.B);
	}
};
