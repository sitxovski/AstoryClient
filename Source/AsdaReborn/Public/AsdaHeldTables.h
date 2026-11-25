// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ УДЕРЖАНИЕ ТАБЛИЦ НА ВРЕМЯ ЖИЗНИ ВЛАДЕЛЬЦА (15.09.2026, отчёт `Reports/dedup_audit/dt_gc_hold.md`).
//
// ПРОБЛЕМА. `AsdaDt::Cached` держит `UDataTable` СЛАБО (так надо редактору, см. `AsdaDataTableCache.h`).
//   В -game/cooked сборщик мусора выгружает такую таблицу, как только на неё нет сильной ссылки,
//   и следующий спаун / навык / открытие окна синхронно грузит её заново — рывок кадра.
//   Доказано контролем `asda.DtHold.GcCheck` (без удержания после `obj gc` таблица уходит из памяти).
//
// РЕШЕНИЕ. Владелец с временем жизни игровой сессии (подсистема `UGameInstance`) держит этот
//   массив как `UPROPERTY` и лениво, в месте первого использования, кладёт в него объекты таблиц
//   своей группы. Объекты берутся через ТУ ЖЕ `AsdaDt::Cached` — второй загрузки, своих путей
//   разбора и изменения чтения строк здесь нет; это только сильная ссылка для GC.
//   Образец — `UAsdaWorldSubsystem::HoldEntityNameTables` (волна 3 п.14); он оставлен как есть.
//
// ⚠ Редакторная семантика `AsdaDt::Cached` не меняется: `OnConstruction` запечённых акторов через
//   подсистемы не ходит, а массив живёт только в `UGameInstance` (игра / PIE).
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AsdaHeldTables.generated.h"

// Набор сильно удерживаемых таблиц одного владельца, разбитый на именованные группы.
USTRUCT()
struct ASDAREBORN_API FAsdaHeldTables
{
	GENERATED_BODY()

	// Удержать таблицы группы по объектным путям. Первый вызов группы грузит их через `AsdaDt::Cached`
	//   (отсутствующий ассет пропускается); повторный ничего не грузит. Возврат — сколько таблиц группы удержано.
	int32 Hold(FName Group, TConstArrayView<const TCHAR*> Paths);

	// Удержать уже найденную таблицу (когда путь вычисляет сам потребитель, например поиск текст-таблицы
	//   по категориям в `FAsdaText`). Однократно на группу; nullptr не кладётся. Возврат — 0 или 1.
	int32 HoldTable(FName Group, UDataTable* Table);

	// Была ли попытка удержания группы.
	bool IsHeld(FName Group) const { return GroupCounts.Contains(Group); }

	// Сколько таблиц удержано группой (−1 — группа ещё не удерживалась).
	int32 GroupNum(FName Group) const { const int32* N = GroupCounts.Find(Group); return N ? *N : -1; }

	// Все удержанные объекты владельца (для стенда: сверка «что заявлено ↔ что держится»).
	const TArray<TObjectPtr<UDataTable>>& GetTables() const { return Tables; }

private:
	// Сильные ссылки — ради них структура и существует (без дублей: одна таблица может входить в две группы).
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDataTable>> Tables;

	// Группа → число удержанных ею таблиц. Ключ есть ⇒ попытка была (как `bEntityNameTablesHeld` у мира).
	TMap<FName, int32> GroupCounts;
};
