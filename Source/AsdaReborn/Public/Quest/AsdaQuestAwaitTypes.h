// Copyright (c) 2026 @sitxovski (Discord)
// Строки двух таблиц подсказки робота «Минопио» («Тебя кое-кто разыскивает…», окно 599).
//   Полный реверс — `Reports/minopio_window.md`.
//
// ЗАЧЕМ ОТДЕЛЬНЫЙ ФАЙЛ. Обе таблицы обслуживают ровно одну функцию — сбор списка карт, где игрока
//   ждёт НОВЫЙ квест (`itemFilterValidRecipeList@0xD97D30`). В `AsdaQuestTypes.h` они были бы
//   чужеродны: там раскладка журнала и сценариев `.NSR`.
//
// ★ ВТОРОГО ХРАНИЛИЩА КВЕСТОВОГО СОСТОЯНИЯ ЗДЕСЬ НЕТ И БЫТЬ НЕ ДОЛЖНО. Эти две таблицы — чистые
//   СПРАВОЧНИКИ КАРТ из `.BIN` оригинала; «ждут ли на карте» считает `EvalMarkForNsr`
//   (= `questFindMatchingActionMode2@0xD8ACE0`), единственный вычислитель условий в порту.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AsdaQuestAwaitTypes.generated.h"

// ── `Data\NPC\TIM_List.BIN` (тег "VIL", 8 записей, шаг 204) ──────────────────────────────────────
// Загрузчик оригинала — `loadNpcTimList@0xD98F80` (`npc_mob_pet.c:2388`), указатель `CQuestMgr+1472`,
//   счётчик `+1476`. Список карт, которые ВООБЩЕ можно назвать в подсказке, и их отображаемые имена.
//   Поиск в эталоне ЛИНЕЙНЫЙ (`204*j + base`, сравнение `+0` с id карты) — порядок записей в файле
//   не совпадает с id и значения не имеет.
// ★ В чистой RU-сборке оригинала все 8 имён остались АНГЛИЙСКИМИ (дефект оригинала §7.7 отчёта);
//   рабочая сборка `_MINE` переведена, её и импортируем.
USTRUCT(BlueprintType)
struct FAsdaTimListRow : public FTableRowBase
{
	GENERATED_BODY()

	// `+0` — id карты (тот же, что в `DT_MapTable` и в `npc_list.BIN +0x6C`).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 MapId = -1;

	// `+4` — название местности (cp1251), ровно то, что вставляется в строку подсказки.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") FString VillageName;
};

// ── `Data\NPC\NMS_list.BIN` (тег "NSL", 12 записей, шаг 44) ──────────────────────────────────────
// Загрузчик оригинала — `npcLoadNameList@0xD99130` (`npc_mob_pet.c:2423`), указатель `CQuestMgr+1464`,
//   счётчик `+1460`. Для КАЖДОЙ карты — список из 10 карт, о которых с неё можно рассказать
//   (`−1` = пусто). ★ Эталон адресуется ПРЯМО индексом (`44 * mapId`), без поиска и без проверки
//   границ — отсюда дефект §7.3 отчёта (карты с id ≥ 12 читают память за буфером).
USTRUCT(BlueprintType)
struct FAsdaNmsListRow : public FTableRowBase
{
	GENERATED_BODY()

	// `+0` — id карты. В файле совпадает с индексом записи (проверено по байтам всех 12 строк).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 MapId = -1;

	// `+4 … +40` — 10 связанных карт в порядке файла; `−1` = слот пуст.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> Related;
};
