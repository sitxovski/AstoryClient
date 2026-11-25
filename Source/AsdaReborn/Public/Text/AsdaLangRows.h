// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ [ПОРТ] 09.10.2026 (мультиязычность, этап 1 «А. Данные» плана `Reports/owner_1009/multilang_plan.md` §2.2).
//   ПЕРЕВОД ТЕКСТА СТРОК DataTable В ТОЧКЕ ВЫДАЧИ — общая обвязка над `FAsdaLang::ResolveRow` (`Text/AsdaLang.h`).
//
//   ЗАКОН: строки DataTable В ПАМЯТИ не подменяются никогда (редактор сохранил бы перевод в RU-ассет). Здесь —
//     только два безопасных приёма:
//     1) `Text(Dt, RowPtr, Field, Ru)` — перевод одного поля строки, на которую у вызывающего есть указатель
//        (имя строки находится обратным индексом «указатель → RowName», он строится один раз на таблицу);
//     2) `Localize(Dt, RowName, Copy)` — перевод текстовых полей КОПИИ строки (геттеры `UAsdaItemTableSubsystem`
//        и собственные кэши подсистем отдают копии: копия не принадлежит таблице, редактор её не сохранит).
//   Русская сессия — ни одного поиска: оба приёма сразу возвращают/оставляют русский текст (путь как до 09.10).
//
//   КЛЮЧИ (их же строит генератор оверлеев; сводка — `Reports/owner_1009/multilang/stage1_keys.md`):
//     таблица оверлея = имя ассета без `DT_`, ключ = `<RowName>.<UPROPERTY-поле>`; элементы массивов —
//     `<RowName>.<Массив>.<индекс>[.<поле элемента>]` (например `108npc_63.Steps.0.Caption`, `0_Qst_90.Steps.3`).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"

// Строки, у которых переводятся поля (объявления — в своих заголовках; здесь нужны только ссылки на них).
struct FAsdaItemTooltipRow;
struct FAsdaItemoptiontableRow;
struct FAsdaMakingRecipeRow;
struct FAsdaSpecialItemlistRow;
struct FAsdaSpecialitemTooltiplistRow;
struct FAsdaSpecialitemBuftooltiplistRow;
struct FAsdaHairtableRow;
struct FAsdaJobtypeRow;
struct FAsdaSowellRow;
struct FAsdaSowellTooltipRow;
struct FAsdaTitleRow;
struct FAsdaTitleOptionRow;
struct FAsdaSoulmateskilltableRow;
struct FAsdaSoulsoulmatetextRow;
struct FAsdaPetexptableRow;
struct FAsdaPetoptiontableRow;
struct FAsdaPetRow;
struct FAsdaSkilltooltipRow;
struct FAsdaEmotiontipRow;
struct FAsdaLoginhelpertextRow;
struct FAsdaStateinfotipRow;
struct FAsdaSoulstateinfotipRow;
struct FAsdaSkillInfoRow;
struct FAsdaNsrPageRow;
struct FAsdaQuestBookRow;

namespace AsdaLangRows
{
	// ★★★ [ИСПРАВЛЕНИЕ] 07.11.2026 (баг владельца «титул начинается с лишней *», ключ asda.Title.StripStar): у 18 титулов id 99–116
	//   имя в данных `title_list` (RU и EN) начинается со служебной «*». Оригинал её не вырезает — печатает поле +16 как есть
	//   (`NetHandler_UpdateCharGuildTitle@0xE9AD90`, network.c, "<FC=%08x>%s</F>"), то есть это дефект данных оригинала.
	//   Единая точка показа имени титула: снимает ведущие «*» и пробелы за ними. Ключ 0 — имя как в таблице.
	ASDAREBORN_API FString TitleDisplayName(const FString& Raw);

	// Перевод данных включён: язык сессии не русский. Для геттеров подсистем и окон (рантайм GameInstance).
	ASDAREBORN_API bool Active();

	// То же, но ещё и НЕ мир редактора вне PIE. Для путей, которые зовёт `OnConstruction` запечённых акторов
	//   (имена NPC/мобов): там результат пишется в UPROPERTY актора/плашки и уехал бы в карту при сохранении.
	ASDAREBORN_API bool ActiveForBakeable();

	// ★ 10.10.2026 (en_completeness): охрана `OnConstruction` акторов. Пока объект жив и актор `Ctx` — в мире РЕДАКТОРА
	//   (не игровом мире PIE), `ActiveForBakeable` отвечает «нет» даже во время сеанса PIE: перевод не попадёт в
	//   сохраняемые поля запечённых акторов. Для актора игрового мира охрана ничего не делает.
	struct ASDAREBORN_API FEditorWorldScope
	{
		explicit FEditorWorldScope(const UObject* Ctx);
		~FEditorWorldScope();
		bool bActive = false;   // охрана взведена (актор мира редактора)
	};

	// Имя строки таблицы по указателю на её данные (`FindRow`/`GetRowMap` отдают именно такие указатели).
	//   Обратный индекс строится один раз на таблицу и перестраивается, если таблицу пересобрали.
	//   NAME_None — указатель не из этой таблицы.
	ASDAREBORN_API FName RowNameOf(const UDataTable* Dt, const void* RowPtr);

	// Перевод поля `Field` строки `RowPtr` таблицы `Dt` (ключ `<RowName>.<Field>`). Русская сессия / нет таблицы /
	//   строка не найдена — возвращается `Ru` как есть.
	ASDAREBORN_API FString Text(const UDataTable* Dt, const void* RowPtr, const TCHAR* Field, const FString& Ru);

	// ★ [ИСПРАВЛЕНИЕ] 05.11.2026 (gold-boast): уходит ли RU-заглушка «нет/Нет/НЕТ» в пустую строку, когда перевода нет
	//   (ключ `asda.Lang.DropRuPlaceholder`; только не-русская сессия). Зовёт `FAsdaLang::Resolve` (все таблицы, GameText,
	//   подписи NPC) и самотесты — одна точка.
	ASDAREBORN_API bool DropsRuPlaceholder(const FString& Ru);

	// ── Перевод текстовых полей КОПИИ строки (поле → ключ `<RowName>.<поле>`). Нетекстовые поля не трогаются. ──
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaItemTooltipRow& R);             // Description
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaItemoptiontableRow& R);         // FmtString
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaMakingRecipeRow& R);            // RecipeName
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaSpecialItemlistRow& R);         // Name
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaSpecialitemTooltiplistRow& R);  // Name, Tooltip
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaSpecialitemBuftooltiplistRow& R); // Name, Desc
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaHairtableRow& R);               // Name, Description
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaJobtypeRow& R);                 // JobName, ParentJob
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaSowellRow& R);                  // Name
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaSowellTooltipRow& R);           // Text
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaTitleRow& R);                   // Name, Desc
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaTitleOptionRow& R);             // OptionStatName
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaSoulmateskilltableRow& R);      // DisplayName, Text
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaSoulsoulmatetextRow& R);        // Text
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaPetexptableRow& R);             // StageName
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaPetoptiontableRow& R);          // Format
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaPetRow& R);                     // Name
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaSkilltooltipRow& R);            // Name, Text
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaEmotiontipRow& R);              // Name
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaLoginhelpertextRow& R);         // Text
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaStateinfotipRow& R);            // Name, Text
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaSoulstateinfotipRow& R);        // Name, Text
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaSkillInfoRow& R);               // Name
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaNsrPageRow& R);                 // Text, Steps.<i>.Caption
	ASDAREBORN_API void Localize(const UDataTable* Dt, const FName& Row, FAsdaQuestBookRow& R);               // Title, Desc, Steps.<i>

	// Строки без текста для игрока (цены, коды, пути ассетов) — ничего не делать. Выбирается компилятором для всех
	//   типов, у которых нет своей перегрузки выше (точное совпадение не-шаблона всегда предпочтительнее).
	template<typename TRow>
	inline void Localize(const UDataTable* /*Dt*/, const FName& /*Row*/, TRow& /*R*/) {}

	// Копия строки `Src` таблицы `Dt` с переведёнными полями: имя строки находится по указателю.
	template<typename TRow>
	inline TRow CopyLocalized(const UDataTable* Dt, const TRow& Src)
	{
		TRow Out = Src;
		if (Active() && Dt) { Localize(Dt, RowNameOf(Dt, &Src), Out); }
		return Out;
	}
}
