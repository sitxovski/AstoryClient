// Copyright (c) 2026 @sitxovski (Discord)
// AsdaExtraMenuRow.h — строка КЛИЕНТСКОЙ таблицы `./Data/Extra_menu_table.BIN`, из которой
//   целиком собирается окно «Прочее» (`ETC_NEW_WIN`, JUI IDNum 74600).
//
// ═══════════════════════ ОТКУДА РАСКЛАДКА (снята с бинаря, не угадана) ═════════════════════════
//   Загрузчик — `loadExtraMenuTable@0xECD2A0` (`RE/ida/export/AsdaStory_game.c:510377`):
//     путь   = `GetCurrentDir() + "\\Data\\Extra_menu_table.BIN"`;
//     хедер  = `fread(buf, 0x14, 1, f)` (20 байт), 4-байтовая сигнатура сверяется `strncmp(...,4)`;
//     записи = `new[] 116 * count`, `fread(records, 0x74, count, f)` — запись РОВНО 116 б (0x74).
//   Живой файл RU-сборки: 1296 б = 20 + 11 × 116 ⇒ **11 записей**, ровно столько пунктов и на
//   скриншоте владельца. Хедер: `i32 0 | i32 count=11 | i32 fieldCount=7 | "EMT\0" | i32 0`.
//
//   Смещения полей внутри записи взяты из ЧИТАТЕЛЕЙ, а не из порядка локалов Hex-Rays:
//     +0   i32       Idx        — `array116_LookupByIndexOrField@0xECD4C0` ищет запись по индексу;
//                                 в файле поле равно порядковому номеру 0…10 (совпало во всех 11).
//     +4   char[64]  Name       — `CControl_SetText(textCtrl, rec + 4)`
//                                 (`quickSlotUI_RefreshIconSlots@0xECD870`, подпись строки меню)
//                                 и заголовок подсказки (`tutorialShowItemNoticePopup@0xECD760`).
//                                 Кодировка RU-сборки — cp1251.
//     +68  i32       Cmd        — `uiOpenWindowDispatch(*(rec + 68))` в обработчике клика
//                                 (`uiListControlOnUiEvent@0xECDC70`, событие 0xC). ЭТО НЕ ИНДЕКС:
//                                 второй режим `array116_LookupByIndexOrField(…, a3 != 1)` ищет
//                                 запись ИМЕННО по этому полю (`rec[29*i + 17]` = байт +68).
//     +72  char[32]  IconAtlas  — `TexturePath_Format(rec + 72)` → имя текстуры-атласа («Extra_menu»)
//     +104 i32       IconX      — `32 * *(rec + 104)` = X ячейки атласа в ПИКСЕЛЯХ
//     +108 i32       IconY      — `32 * *(rec + 108)` = Y ячейки
//                                 (`CControl_DrawTextureRect(ctrl, tex, X, Y, 32, 32)`)
//     +112 i32       DescLocId  — `UI_FormatLocalizedNotice(ctx, *(rec + 112), 0, 1)` = ТЕЛО
//                                 подсказки строки меню (110925…110936 в RU-файле из Фантазиума).
//                                 ★ 09.10.2026 (мультиязычность, этап 0): в DT_ExtraMenu — id EN-оригинала той же
//                                 сборки, 110930…110941 (по Cmd из `Donors\AsdaChroniclesInterlude\Data\Extra_menu_table.BIN`;
//                                 сборщик `Tools/asda_build_extra_menu_dt.py`, DESC_FROM_ORIG). RU-описания перенесены на
//                                 эти id ведомостью `Tools/data/gametext_overrides.json`.
//   Итого 4+64+4+32+4+4+4 = 116 — сходится с `0x74` в `fread` и с `fieldCount = 7` в хедере.
//
// ★ Значения полей НЕ ДУБЛИРУЮТСЯ В КОДЕ (закон «данные из таблиц, без хардкода»): порядок,
//   состав, подписи, иконки и команды пунктов приходят только отсюда. В коде живёт лишь РАЗБОР
//   команды (`Cmd` → что открыть) — ровно как в эталоне, где это `switch` в `uiOpenWindowDispatch`,
//   а не поле таблицы.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AsdaExtraMenuRow.generated.h"

USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaExtraMenuRow : public FTableRowBase
{
	GENERATED_BODY()

	// +0. Порядковый номер записи в файле. Он же индекс, которым слот хотбара типа 5 (`ExtraMenu`)
	//   ссылается на пункт меню (`uiHotbarAssignSlot@0xECDFB0` кладёт в слот `Src[1] = выбранный`).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ДопМеню")
	int32 Idx = 0;

	// +4 char[64]. Подпись пункта — она же заголовок подсказки.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ДопМеню")
	FString Name;

	// +68. Код команды для `uiOpenWindowDispatch` (0…0xB). Разбор — в `UAsdaExtraMenuSubsystem`.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ДопМеню")
	int32 Cmd = 0;

	// +72 char[32]. Имя текстуры-атласа иконок (во всех 11 записях RU-сборки — «Extra_menu»).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ДопМеню")
	FString IconAtlas;

	// +104 / +108. Ячейка атласа В ЯЧЕЙКАХ (эталон умножает на 32 сам).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ДопМеню")
	int32 IconX = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ДопМеню")
	int32 IconY = 0;

	// +112. Id строки таблицы текста для ТЕЛА подсказки.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ДопМеню")
	int32 DescLocId = 0;

	// ★★★ ПОЛЯ В ФАЙЛЕ НЕТ — оно НАШЕ, и это честно помечено (закон `never-invent-only-original`:
	//   выдумывать нельзя данные, а служебная пометка о нашем же вмешательстве — не выдумка).
	//   true = запись ВОССТАНОВЛЕНА нами и в исходной `Extra_menu_table.BIN` её не было
	//   (сейчас такая одна — «Поиск отряда», `Cmd = 7`).
	//
	//   ЗАЧЕМ. Слот хотбара типа 5 в ОРИГИНАЛЕ ссылался на пункт ПОРЯДКОВЫМ НОМЕРОМ строки, и
	//   вставка записи сдвигает нумерацию: старая раскладка указала бы на соседний пункт. Чтобы
	//   СТАРУЮ запись всё-таки разрешить ПРАВИЛЬНО, надо уметь посчитать «N-я строка ИСХОДНОЙ
	//   таблицы» — то есть N-я среди записей с `Restored == false`. Пометка делает этот счёт
	//   ДАННЫМИ, а не хардкодом «пропусти индекс 2» в коде.
	//   Новые раскладки этим не пользуются: они хранят устойчивый `Cmd`
	//   (см. `UAsdaExtraMenuSubsystem::FindByStableKey`).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ДопМеню")
	bool Restored = false;
};
