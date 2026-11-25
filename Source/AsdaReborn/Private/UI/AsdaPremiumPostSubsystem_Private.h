// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPremiumPostSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaPremiumPostSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaPremiumPostSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaPremiumPostSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaPremiumPostSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "Framework/Application/SlateApplication.h"   // живой хит-тест: кого Slate реально видит под точкой
#include "UI/AsdaWindowSubsystem.h"

// Категория журнала частей AsdaPremiumPostSubsystem*.cpp: определение — AsdaPremiumPostSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaPost, Log, All );

namespace {
	// ── окно ─────────────────────────────────────────────────────────────────────────────────────
	const TCHAR* kWinId   = TEXT("Primium_Item_post_WIN");                                   // IDNum 69000

	// ── тип уведомления квик-бара (эталон `statusBarAddIcon(g_pQuickSlotBar, 15)`) ───────────────
	constexpr int32 kNoticeType = 15;
}

// Ключ CVarPostMultiItemRows: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarPostMultiItemRows;
#else
extern TAsdaConstCVar<int32> CVarPostMultiItemRows;
#endif

// Ключ CVarPostMultiSelect: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarPostMultiSelect;
#else
extern TAsdaConstCVar<int32> CVarPostMultiSelect;
#endif

// Включён ли выбор нескольких заказов: нужен и `MultiItemRows`, он делает `Order` ключом строки.
static bool PostMultiSelectOn()
{
	return CVarPostMultiSelect.GetValueOnGameThread() != 0 && CVarPostMultiItemRows.GetValueOnGameThread() != 0;
}
