// Copyright (c) 2026 @sitxovski (Discord)
// AsdaTitleSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaTitleSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaTitleSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaTitleSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaTitleSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "UI/AsdaHudSubsystem.h"       // ★ ShowTitleBanner — баннер «звание получено» (тип 2)
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaMarkup.h"          // UAsdaRichText::ApplyFormatted — единственный показ тегов <FC=..>
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaWindowSubsystem.h"

#include "Components/TextBlock.h"
#include "UI/AsdaCraftSubsystem.h"   // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)

// Категория журнала частей AsdaTitleSubsystem*.cpp: определение — AsdaTitleSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaTitle, Log, All );

namespace {
	const FName  kWindowId(TEXT("Title_Win"));
	const FName  kNoticeId(TEXT("Title_Notice_BG"));
}

namespace {
namespace TLoc {
// ── Нотисы (все id сняты по эталону, см. таблицу в шапке заголовка) ──
		constexpr int32 kGradeBase    = 1450;   // 1450+grade: Обычный…Мифический (0…8)
		constexpr int32 kGradeMax     = 1458;
		constexpr int32 kCategoryBase = 1460;   // 1460+cat: Монстр/Уровень/Предмет/Элитный/Друг/Прочие
		constexpr int32 kCategoryMax  = 1465;   // ★ D6: дальше идут «Преф.»/«Оконч.» — не наши строки
		constexpr int32 kPositionBase = 1466;   // 1466 «Преф.», 1467 «Оконч.»

		constexpr int32 kRank         = 2401;   // «Ранг: [sd1]»
		constexpr int32 kTotal        = 2402;   // «Всего: [ss1]»
		constexpr int32 kTotalRate    = 2403;   // «Всего собрано %:»
		constexpr int32 kRelease      = 2404;   // «Убрать указанное звание»
		constexpr int32 kTab0         = 2405;   // «Обычные» (было «Обычный» — переименовано В ДАННЫХ 2026-08-30)
		constexpr int32 kTab1         = 2406;   // «Соревнование»
		constexpr int32 kTab2         = 2407;   // «Секретные»
		constexpr int32 kTabRate      = 2408;   // «Собрано %:»
		constexpr int32 kColNum       = 2409;   // «Нет.» (№)
		constexpr int32 kColCategory  = 2410;   // «Категория»
		constexpr int32 kColName      = 2411;   // «Имя»
		constexpr int32 kColPosition  = 2412;   // «Позиц.»
		constexpr int32 kColGrade     = 2413;   // «Ранг»
		constexpr int32 kColPoint     = 2414;   // «Очки»
		constexpr int32 kBtnApply     = 2415;   // «Применить»
		constexpr int32 kBtnDelete    = 2416;   // «Очистить»
		constexpr int32 kLeftTimeCap  = 2417;   // «Времени на изменения:»
		constexpr int32 kLeftTimeFmt  = 2418;   // «[sd1] мин. [sd2] сек.»
		constexpr int32 kInfoCap      = 2419;   // «Подробнее о звании»
		constexpr int32 kInfoGrade    = 2420;   // «Ранг: [ss1]»
		constexpr int32 kInfoPoint    = 2421;   // «Очки: [sd1]»
		constexpr int32 kInfoPosition = 2422;   // «Место: [ss1]»
		constexpr int32 kBtnOk        = 2423;   // «ОК»
		constexpr int32 kBtnCancel    = 2424;   // «Отмена»
		constexpr int32 kCooldown     = 2425;   // «Менять звания разрешается 1 раз в 10 минут.» (D5)
		constexpr int32 kNotEarned    = 2426;   // «Чтобы носить звание, нужно его заслужить.» (D4)
		constexpr int32 kConfirmDel   = 2427;   // «Чтобы удалить все выбранные звания, нажми…»
		constexpr int32 kCompetition  = 2428;   // «Звания, полученные за соревнования, удалять нельзя.»
		constexpr int32 kDelNotOwned  = 2429;   // «Чтобы удалить звание, нужно сначала его заслужить.»
		constexpr int32 kDelWorn      = 2430;   // «Нельзя удалять звание, которое ты носишь.»
		constexpr int32 kLeftTimeNone = 2431;   // «Возможные изменения»
		constexpr int32 kColState     = 2434;   // «Состояние»

		constexpr int32 kTipApply     = 20400;  // тело подсказки «Применить»
		constexpr int32 kTipDelete    = 20401;
		constexpr int32 kTipOk        = 20402;
		constexpr int32 kTipCancel    = 20403;
		constexpr int32 kTipApplyCap  = 20404;  // заголовок подсказки
		constexpr int32 kTipDeleteCap = 20405;
		constexpr int32 kTipOkCap     = 20406;
		constexpr int32 kTipCancelCap = 20407;
		constexpr int32 kStateOwned   = 20408;  // «Получено»
		constexpr int32 kStateNone    = 20409;  // «Не получено»
		constexpr int32 kStateFound   = 20410;  // «Найдено»

		constexpr int32 kNoticeNew    = 20500;  // «У тебя новое звание.»
		constexpr int32 kNoticeSecret = 20501;  // «Тебе стал известен секрет получения нового звания.»
		constexpr int32 kNoticeLost   = 20502;  // «Звание потеряно после обновления системы званий.»
		constexpr int32 kNoticeSystem = 20503;  // «Система званий и рангов обновлена.»
		constexpr int32 kNoticeCap    = 20504;  // «Уведомление»

		// ★★★ НАША СТРОКА (диапазон порта > 990000, решение владельца 2026-08-30).
		//   «Ты утратил звание „[ss1]“.» — слот `[ss1]` СТРОКОВЫЙ (закон `loc-string-placeholders`:
		//   `[ss1..5]` строка, `[sd1..5]` целое). Кавычки-лапки „“ взяты НЕ случайно: это не U+0022,
		//   поэтому поле CSV не требует экранирования и цепочка импорта его не рвёт.
		//   ★ Оригинальная 20502 НЕ тронута и остаётся запасным вариантом.
		constexpr int32 kNoticeLostNamed = 990110;
}
}

namespace {
	void SetTxt(UTextBlock* Tb, const FString& S)
	{
		if (Tb) { Tb->SetText(FText::FromString(S)); }
	}
}
