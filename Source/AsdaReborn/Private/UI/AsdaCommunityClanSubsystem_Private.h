// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCommunityClanSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaCommunityClanSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaCommunityClanSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaCommunityClanSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaCommunityClanSubsystem.h"

#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "HAL/IConsoleManager.h"

// Категория журнала частей AsdaCommunityClanSubsystem*.cpp: определение — AsdaCommunityClanSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaClanTab, Log, All );

// Ключ CVarHideApplicantsPage: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarHideApplicantsPage;
#else
extern TAsdaConstCVar<int32> CVarHideApplicantsPage;
#endif

namespace {
	// Право «менять/видеть управление кланом» — второй аргумент эталонного вызова
	//   `ClanRankPermission(wnd, звание, 2)` в `uiSwitchPanelMode`. Смысл бита 2 по местам проверки
	//   на сервере — «писать/удалять объявления клана» (`Recv_User_Notice_Write` / `..._Del`).
	constexpr int32 kPermManage = 2;

	constexpr int32 kRows = 6;        // строк списка членов на страницу (14120…14220, шаг IDNum +20)
	constexpr int32 kNotices = 4;     // строк объявлений в подрежиме 1 (14036…14051, шаг +5)

	// ══════════════ ПАНЕЛЬ УПРАВЛЕНИЯ 35000 «ПРЕДВОДИТЕЛЬ»: ШЕСТЬ СТРАНИЦ ═══════════════════════
	//   Эталон `uiSelectPanelTab@0xBC5C20` (сборка страниц — `CmtyClan_Manage_BuildUI@0xBBE320`).
	//   Порядок и IDNum страниц взяты ДОСЛОВНО из массива `subMenuIdBase[6]` построителя:
	//     0 → 35100 `CmtyClan_SubMenu1` «Кандидаты»            (loc 17101)
	//     1 → 35500 `CmtyClan_SubMenu2` «Права членов клана»   (loc 17102)
	//     2 → 35600 `CmtyClan_SubMenu3` «Ранги членов клана»   (loc 17103)
	//     3 → 35800 `CmtyClan_SubMenu4` «Объявления»           (loc 17104)
	//     4 → 35900 `CmtyClan_SubMenu5` «Передача полномочий»  (loc 17105)
	//     5 → 36100 `CmtyClan_SubMenu6` «Герб клана»           (loc 17106)
	constexpr int32 kManagePages   = 6;

	constexpr int32 kLocForbidden = 17006;   // «Запрещено» — нотис

	constexpr int32 kLocGradeBase = 17046;   // звание: loc 17046 − звание (1 = «Глава клана»)
}
