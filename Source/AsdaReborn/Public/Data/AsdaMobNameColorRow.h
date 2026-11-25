// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMobNameColorRow.h — строка DataTable `DT_MobNameColors`: ЦВЕТ ИМЕНИ МОБА ПО РАЗНИЦЕ УРОВНЕЙ.
//
// ★ ЭТАЛОН (реверс 2026-08-03). Одна и та же лесенка встречается в ТРЁХ местах клиента:
//     • game_main.c:4544  (uiWorldOverlayUpdate, hover/наведённая цель — контрол g_pChatColorLabel)
//     • game_main.c:4763  (плашка ВЫБРАННОЙ цели — g_pNameplateLvlDiffLabel)
//     • ui_core.c:75770   (hudShowMobTargetInfo — окно инфо о цели: имя 517 И цифры уровня 542)
//   Считается ОДИНАКОВО:  levelDiff = уровень ИГРОКА − уровень МОБА
//     (плюс = моб слабее; уровень моба берётся из сущности +0x1DC, туда его кладёт CMob_Init
//      из Mob list.BIN +0x98; уровень игрока — CClass_GetField142C(&g_InventoryBag)->level).
//   Ветка выбирается по БАЙТУ АГРЕССИИ моба (сущность +6979 ← Mob list.BIN +0xFC):
//     • 1, 4, 5 → лесенка по разнице уровней (см. таблицу ниже);
//     • 2       → ФИКСИРОВАННЫЙ цвет −1946933 = 0xFFE24ACB (розово-сиреневый);
//     • 3       → ФИКСИРОВАННЫЙ цвет −3954584 = 0xFFC3A868 (песочный);
//     • иное    → цвет НЕ трогается (остаётся тем, что был).
//   Лесенка (ARGB эталона, sRGB):
//     diff ≥  7          → −8355712  = 0xFF808080  серый   («моб не даёт опыта»)
//     diff  2 … 6        → −16711936 = 0xFF00FF00  зелёный
//     diff −1 … 1        → −1        = 0xFFFFFFFF  белый   (в т.ч. РАВЕНСТВО уровней)
//     diff −5 … −2       → −24064    = 0xFFFFA200  оранжевый
//     diff ≤ −6          → −65536    = 0xFFFF0000  красный
//   ⚠ Диапазоны эталона НЕ покрывают ничего мимо: −1..1 включает 0, разрывов нет.
//   ⚠ ЕДИНСТВЕННОЕ расхождение между местами: в ОКНЕ ИНФО О ЦЕЛИ (ui_core.c:75779) верхняя
//     ступень не 0xFF808080, а −5592406 = 0xFFAAAAAA. Над-головной плашки это не касается.
//   ⚠ Красится ТОЛЬКО ТЕКСТ ИМЕНИ (и, в окне инфо, цифры уровня). Полоса HP и фон плашки —
//     НЕ красятся ни в одной ветке.
//
// ★ ГАММА (закон [[ui-srgb-gamma-rule]]): цвета JuiSkin/клиента заданы в sRGB, UE ждёт linear —
//   поэтому в рантайме используется FLinearColor(FColor), которая делает sRGB→linear.
//   В таблице цвет хранится КАК В ЭТАЛОНЕ (sRGB, FColor).
//
// Импортёр: Tools/asda_build_mob_namecolor_dt.py.
// Применение: UAsdaNameplateComponent::ResolveMobNameColor() (зовётся из RefreshNameplate).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AsdaMobNameColorRow.generated.h"

USTRUCT(BlueprintType)
struct FAsdaMobNameColorRow : public FTableRowBase
{
	GENERATED_BODY()

	// ★ Правило по АГРЕССИИ: −1 = «обычная ветка» (агрессия 1/4/5, работают MinDiff/MaxDiff);
	//   2 или 3 = ФИКСИРОВАННЫЙ цвет для этого значения байта агрессии (MinDiff/MaxDiff игнорируются).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobNameColor") int32 AggressionOverride = -1;

	// Диапазон разницы уровней (ВКЛЮЧИТЕЛЬНО), diff = уровень игрока − уровень моба.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobNameColor") int32 MinDiff = -32768;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobNameColor") int32 MaxDiff = 32767;

	// Цвет имени КАК В ЭТАЛОНЕ (sRGB). В рантайме переводится в linear.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobNameColor") FColor NameColor = FColor::White;

	// Исходная константа эталона (для сверки; в рантайме не используется).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobNameColor") int32 EtalonArgb = 0;

	// Короткая подпись правила (видно в редакторе).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobNameColor") FString Note;
};
