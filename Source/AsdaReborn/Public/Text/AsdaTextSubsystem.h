// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaTextSubsystem — единый менеджер игрового ТЕКСТА (по образу оригинала: strTable/StringTable).
//   Оригинал (ETALON_RESOURCE_MANAGERS.md §4): текст = (таблица, ключ) — strTableGetEntryByIndex@0x13106D0
//   (UI по индексу), StringTable_GetById@0x111F4C0 (сообщения по id), str_list.mgs (group,entry).
//   В порту это уже РАБОТАЕТ через FAsdaText (loose-таблицы + DT_GameText) и UAsdaItemTableSubsystem
//   (item-БД). Эта подсистема — тонкий ЕДИНЫЙ фасад над ними: «обратился функцией → получил строку»,
//   доступно из C++ и Blueprint. Ничего рабочего не переписываем — только оборачиваем (правило never-invent).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AsdaHeldTables.h"                // FAsdaHeldTables — удержание таблиц текста на сессию (15.09.2026, dt_gc_hold)
#include "AsdaTextSubsystem.generated.h"

UCLASS()
class ASDAREBORN_API UAsdaTextSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ── UI/сообщения по числовому ID (аналог StringTable_GetById) ──
	// Loc-строка UI по string-ID из DT_GameText (error list.bin), JUI-разметка срезана. "" если нет.
	UFUNCTION(BlueprintCallable, Category="AsdaText")
	FString GetTextById(int32 Id) const;

	// ★ То же, но СЫРАЯ строка — с сохранёнными inline-тегами <FC=AARRGGBB>…</F>, <FS..>, \n.
	//   Нужна там, где цвет/кегль части строки задаёт сама игра (тултип Plus Package 110921/110922,
	//   анонсы с цветом грейда и т.п.). ⚠ GetTextById() срезает теги — с ним такой текст выходит
	//   одноцветным, и цвет «не работает» без единой ошибки в логе.
	UFUNCTION(BlueprintCallable, Category="AsdaText")
	FString GetTextByIdRaw(int32 Id) const;

	// ── Loc-строка с ПОДСТАНОВКОЙ слотов (аналог UI_FormatLocalizedNotice + CObject_SetSlotString /
	//    CClass_SetSlotPair оригинала). В таблице заглушки [ssN] = строковый слот N, [sdN] = числовой слот N
	//    (нумерация с 1). Пример: 15200 = "[ss1] - [sd1] канал" → "Альпеон - 1 канал".
	UFUNCTION(BlueprintCallable, Category="AsdaText")
	FString FormatById(int32 Id, const TArray<FString>& StringSlots, const TArray<int32>& NumberSlots) const;

	// ── ★★★ Loc-строка как ФОРМАТ-СТРОКА printf (`%s` / `%d`), а НЕ слоты [ssN]/[sdN] ─────────────
	//   В оригинале есть ДВА РАЗНЫХ способа подстановки, и путать их нельзя:
	//     (1) слоты  — `CObject_SetSlotString(ctx, i, s)` + `UI_FormatLocalizedNotice` → теги [ssN]/[sdN]
	//                  (это `FormatById` выше);
	//     (2) printf — `v3 = UI_FormatLocalizedNotice(ctx, id, 0, 1); sprintf(buf, v3, аргумент);`
	//                  то есть loc-строка ОТДАЁТСЯ В `sprintf` КАК ФОРМАТ, и подстановка идёт по `%s`.
	//   Способ (2) применяют, например: `uiItemTooltipShow@0xB70A60` (ui_core.c:7729-7731) — приз
	//   премиум-гачи, loc 110621 «…[%s]…»; чат (`AsdaFormatTwo` в AsdaNetSubsystem.cpp) и др.
	//   ⚠ У `FormatById` спецификатор `%s` НЕ заменяется ничем — строка уходит игроку как есть
	//     («Твой приз: [%s].»). Для таких строк звать ИМЕННО эту функцию.
	//   Поведение как у `sprintf`: спецификаторы заменяются слева направо по порядку аргументов;
	//   лишние аргументы игнорируются, лишние спецификаторы остаются на месте (замены нет).
	//   Поддержаны `%s` и `%d` — только они и встречаются в loc-строках оригинала.
	UFUNCTION(BlueprintCallable, Category="AsdaText")
	FString FormatPrintfById(int32 Id, const TArray<FString>& Args) const;

	// То же для УЖЕ ПОЛУЧЕННОЙ строки (когда формат взят не по id, а, например, из таблицы предметов).
	static FString FormatPrintf(const FString& Fmt, const TArray<FString>& Args);

	// ── Произвольная текст-таблица по (таблица, ID, поле) — loose Data /Game/AsdaAssets/Text/* ──
	// Table = имя без DT_ (напр. "npc_list", "Map_info"). Field = name|desc|subtitle|text|family|village|tex.
	UFUNCTION(BlueprintCallable, Category="AsdaText")
	FString GetText(const FString& Table, int32 Id, const FString& Field = TEXT("name")) const;

	// ── Имя предмета из item-БД (мост к UAsdaItemTableSubsystem → DT_item_list) ──
	UFUNCTION(BlueprintCallable, Category="AsdaText")
	FString GetItemName(int32 ItemId) const;

	// ══════════════════════════════════════════════════════════════════════════════════════════
	//  ★★★ ПОДСТАНОВКА АРГУМЕНТОВ В НОТИСЫ — ОБЩАЯ ТОЧКА ВСЕЙ ИГРЫ (порт 2026-08-28).
	//
	//  ЭТАЛОН: `UI_FormatLocalizedNotice@0x9888D0` (`ui_core.c:2055-2185`). Функция получает
	//  КОНТЕКСТ `g_pLocNoticeCtx` — он же буфер вывода, он же хранилище аргументов:
	//      ctx + 256 + 256*i   (i = 0…4)  строковые слоты  → маркеры `[ss1]`…`[ss5]`
	//      ctx + 1536 + 4*i               целые слоты      → маркеры `[sd1]`…`[sd5]`
	//      ctx + 1556 + 4*i               вещественные     → маркеры `[sf1]`…`[sf5]`
	//      ctx + 1576 + 4*i               цвет для [ssN]
	//      ctx + 1596 + 4*i               цвет для [sdN]
	//      ctx + 1616 + 4*i               цвет для [sfN]
	//  Слоты пишут `CObject_SetSlotString@0x9887A0` (строка + цвет), `CClass_SetSlotPair@0x988810`
	//  (целое + цвет) и `sub_988870@0x988870` (float + цвет); `objSlotArray_InitClear@0x988700`
	//  чистит их ПОСЛЕ форматирования (`ui_core.c:2168`), поэтому аргументы живут ровно один вызов.
	//
	//  ПРАВИЛА ЗАМЕНЫ (1:1, `ui_core.c:2100-2163`):
	//      `[/n]`  → перевод строки;
	//      `[ssN]` → цвет 0: `%s`; цвет ≠ 0: « <FC=%08x>%s</F> » (пробелы ЕСТЬ в оригинале);
	//                при `HighlightMode == 1 && HighlightIndex == N` строка ещё и в квадратных
	//                скобках: « <FC=%08x>[%s]</F> »;
	//      `[sdN]` → `%d` либо `<FC=%08x>%d</F>` (без пробелов);
	//      `[sfN]` → `%2lf` либо `<FC=%08x>%2lf</F>`.
	//  Цикл `while (UI_NoticeHasPlaceholder(...))` крутится, пока в строке остаётся ХОТЬ ОДИН из
	//  16 маркеров (`UI_NoticeHasPlaceholder@0x989020`), поэтому НЕЗАПОЛНЕННЫЙ слот всё равно
	//  подставляется — пустой строкой или нулём, а не остаётся `[sd1]` на экране.
	//  ⚠ Если маркеров нет вовсе, оригинал возвращает строку таблицы БЕЗ изменений.
	//
	//  МАСШТАБ. Маркеры есть у **195 строк из 4769** в `DT_GameText` (единственная таблица, где они
	//  встречаются; всего строк во всех таблицах порта 15487). Разбивка: `[ss1]` 154, `[ss2]` 29,
	//  `[ss3]` 2, `[sd1]` 63, `[sd2]` 8, `[sd3]` 2, `[sd4]` 1, `[sd5]` 1, `[sf1]` 1.
	//  До этой правки НИ ОДНА из них не подставлялась, если вызывающий не звал `FormatById`
	//  явно — в чат уходил сырой шаблон (жалоба владельца 2026-08-28, скрин системного чата).
	// ══════════════════════════════════════════════════════════════════════════════════════════

	/** Слоты аргументов нотиса — порт `g_pLocNoticeCtx`. Пять на каждый тип, нумерация с НУЛЯ
	 *  (маркер `[ss1]` = слот 0), как у `CObject_SetSlotString`. */
	struct FNoticeSlots
	{
		FString  Str[5];
		int32    Num[5] = { 0, 0, 0, 0, 0 };
		float    Flt[5] = { 0.f, 0.f, 0.f, 0.f, 0.f };
		uint32   StrColor[5] = { 0, 0, 0, 0, 0 };
		uint32   NumColor[5] = { 0, 0, 0, 0, 0 };
		uint32   FltColor[5] = { 0, 0, 0, 0, 0 };
		void Clear() { *this = FNoticeSlots(); }
	};

	/** `CObject_SetSlotString(ctx, Slot, S, Color)` — строковый слот и его цвет (0 = без тега). */
	void NoticeSetString(int32 Slot, const FString& S, uint32 ColorArgb = 0);
	/** `CClass_SetSlotPair(ctx, Slot, Value, Color)` — целочисленный слот и его цвет. */
	void NoticeSetInt(int32 Slot, int32 Value, uint32 ColorArgb = 0);
	/** `sub_988870(ctx, Slot, Value, Color)` — вещественный слот и его цвет. */
	void NoticeSetFloat(int32 Slot, float Value, uint32 ColorArgb = 0);
	/** `objSlotArray_InitClear(ctx)` — сбросить все слоты. */
	void NoticeClearSlots();

	/** Подставить аргументы в строку `Id` и ОЧИСТИТЬ слоты (как оригинал).
	 *  @param HighlightMode  1 = слот `HighlightIndex` дополнительно берётся в квадратные скобки.
	 *  @param HighlightIndex номер маркера (1…5), а не индекс слота. */
	UFUNCTION(BlueprintCallable, Category="AsdaText")
	FString FormatNotice(int32 Id, int32 HighlightMode = 0, int32 HighlightIndex = 1);

	/** Та же подстановка в УЖЕ ПОЛУЧЕННУЮ строку, слотами `S`. Слоты вызывающего не трогает —
	 *  этой формой пользуются точки, которые формируют аргументы у себя. */
	static FString FormatNoticeText(const FString& Src, const FNoticeSlots& S,
		int32 HighlightMode = 0, int32 HighlightIndex = 1);

	/** Есть ли в строке ХОТЬ ОДИН из 16 маркеров (`UI_NoticeHasPlaceholder`). */
	static bool HasNoticeMarkers(const FString& S);

	// Срезать inline-разметку — все 6 тегов (единый парсер FAsdaMarkup::StripTags).
	UFUNCTION(BlueprintCallable, Category="AsdaText")
	FString Strip(const FString& In) const;

	// ★ УДЕРЖАНИЕ ТАБЛИЦ ТЕКСТА (15.09.2026, отчёт dt_gc_hold). `FAsdaText` держит `DT_GameText` и текст-таблицы
	//   только СЛАБО (`AsdaDt::Cached` / свой weak-кэш) — в -game сборщик мусора их выгружал, и следующая строка
	//   интерфейса синхронно грузила таблицу заново. Подсистема держит их сильно всю игровую сессию:
	//   `DT_GameText` — лениво при первом запросе строки через подсистему (`GetTextById`/`GetTextByIdRaw`/`FormatById`/
	//   `FormatPrintfById`/`FormatNotice`), текст-таблицу — при первом `GetText` по ней. Чтение строк не менялось.
	//   Возврат — сколько таблиц группы удержано (0/1). Публичный — для стенда `asda.DtHold.GcCheck`.
	int32 HoldGameTextTable() const;
	const FAsdaHeldTables& GetHeldTables() const { return HeldTables; }

private:
	// Сильные ссылки на таблицы текста (группы: "GameText" и имя текст-таблицы `GetText`).
	UPROPERTY(Transient)
	mutable FAsdaHeldTables HeldTables;

	/** Живой контекст аргументов — порт `g_pLocNoticeCtx` (один на игру, как в оригинале). */
	FNoticeSlots Slots;
};
