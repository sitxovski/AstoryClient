// Copyright (c) 2026 @sitxovski (Discord)
// ★ ЕДИНЫЙ доступ ко ВСЕМУ игровому тексту из UE-ассетов (Data/*.BIN → CSV → DataTable по категориям
//   /Game/AsdaAssets/Text/<Категория>/DT_<Таблица>). Правило never-invent: весь текст ТОЛЬКО отсюда,
//   не пишется руками; код ссылается на АССЕТЫ, не на .bin.
#pragma once
#include "CoreMinimal.h"

// ЕДИНСТВО текста (без дублей): ДВА асует-источника, оба из оригинала, ни одного .bin в рантайме —
//   1) item-БД (Bin.bbz, 51 таблица /Game/AsdaAssets/Data/ItemDB) — имена/тултипы предметов, pettable,
//      soulmate, skilltooltip, emotiontip, hairtable, itemoptiontable, stateinfotip, title, sowell и др.
//      Доступ: UAsdaItemTableSubsystem (типизированные структуры FAsda<Table>Row).
//   2) loose Data-файлы (/Game/AsdaAssets/Text) — npc/квесты/диалоги/карты/созвездия/туториал/магазин/
//      крафт/клан/рыба + UI-loc (DT_GameText). Доступ: FAsdaText ниже (struct FAsdaTextRow).
//   Дубли из Bin.bbz в /Text/ НЕ держим (дедуп).
struct ASDAREBORN_API FAsdaText
{
	// loc-строка UI по string-ID из DT_GameText (error list.bin, StripMarkup применён). "" если нет.
	static FString Loc(int32 Id);

	// ★ То же, но СЫРАЯ (с тегами <F..>) — для UAsdaRichText::ApplyFormatted. FAsdaText::Get уже сырой.
	static FString LocRaw(int32 Id);

	// Поле записи ЛЮБОЙ текст-таблицы по ID. Table = имя без префикса DT_ (напр. "npc_list", "Map_info",
	//   "SkillTreeTooltip"). Field = "name"|"desc"|"subtitle"|"text"|"family"|"village"|"tex" (дефолт name).
	//   Ищет DT_<Table> по категориям, кэширует. "" если нет.
	static FString Get(const FString& Table, int32 Id, const FString& Field = TEXT("name"));

	// ★ Сама текст-таблица DT_<Table>: тот же поиск по категориям и тот же слабый кэш, что у `Get` (15.09.2026,
	//   dt_gc_hold — тело вынесено из `Get` без изменений; нужно `UAsdaTextSubsystem::GetText` для удержания). nullptr — нет.
	static class UDataTable* FindTable(const FString& Table);

	// Срезать inline-разметку (все 6 тегов: <FC=>/<FS##>/<FB>/<FO>/<FD>/</F>).
	//   Реализация — FAsdaMarkup::StripTags, через FAsdaLoginData::StripMarkup.
	static FString Strip(const FString& In);
};
