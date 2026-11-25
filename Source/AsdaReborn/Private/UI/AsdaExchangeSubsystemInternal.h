// Copyright (c) 2026 @sitxovski (Discord)
// AsdaExchangeSubsystemInternal.h — ★ R2 10.11.2026 (разрез AsdaExchangeSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaExchangeSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaExchangeSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaExchangeSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: окно обмена 2008 открыто — гейт режимов и отложенное золото 9913

#include "AsdaBytes.h"
#include "AsdaPacketWriter.h"   // ★ U6 (В1 дедупа №2): записи 9901/9905/9909 через общий писатель
// GetCurrentNpcId — NPC-аукционщик в шапке 99xx (+10)   ★ R3: комментарий снятого включения "Npc/AsdaNpcDialogSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaJuiComboBox.h"
// имя предмета и подстановка аргументов нотиса   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
// общая точка подсказки предмета   ★ R3: комментарий снятого включения "UI/AsdaItemTooltipSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaCommunityFriendSubsystem.h"   // ★ общая точка AsdaCommunityUi::ShownVisibilityFor
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaWarehouseSubsystem.h"   // ★ пачка 20, жалоба 10: склад 4000 открыт — рынок не открывается
#include "World/AsdaWorldSubsystem.h"      // GetSelfCharName — имя игрока в записи 9901

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableText.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "Login/AsdaLoginData.h"   // [ПОРТ] 08.10.2026: общий кодек провода (гибрид RU+EN+AR)

// Категория журнала частей AsdaExchangeSubsystem*.cpp: определение — AsdaExchangeSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaExchange, Log, All );

// Ключ CVarExchangeLegacyLevelGate: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
extern TAutoConsoleVariable<int32> CVarExchangeLegacyLevelGate;
#else
extern TAsdaConstCVar<int32> CVarExchangeLegacyLevelGate;
#endif

namespace {
	// Окно и его виджет. Ассет импортирован заходом JUI.
	const FName  kWindowId(TEXT("EXCHANGE_WIN"));

	// Размеры панелей — ровно как в эталоне (`CItemMediation_BuildUI@0xD40100`).
	constexpr int32 kSellCells = 60;   // 13064…13123
	constexpr int32 kRegRows   = 5;    // 13015…13054
	constexpr int32 kMyRows    = 8;    // 13205…13244
	constexpr int32 kLotRows   = 8;    // 13429…13468
	constexpr int32 kLotSowels = 5;    // 13511…13585, по 5 на строку

	// ★ ЛИМИТЫ ЗАПИСЕЙ СЕРВЕРА (§9.4 отчёта). «Мои лоты» (9908) и снятие (9909) — ДО 10 записей
	//   на ВОСЕМЬ строк панели: ровно для этого у списка и есть полоса прокрутки 13249.
	constexpr int32 kMyMaxRecs  = 10;
	constexpr int32 kLotMaxRecs = 8;

	// Сетка выставления: 6 столбцов × 10 видимых строк (60 ячеек 32×32, шаг 33 — ровно 198×330).
	constexpr int32 kSellCols = 6;

	constexpr int32 kRec9904 = 80;

	// ── Нотисы (§6 и §7.5 отчёта) ────────────────────────────────────────────────────────────
	constexpr int32 kLocBusyOther     = 12555;  // активен другой режим / аукцион заблокирован

	constexpr int32 kLocLevel24       = 97101;

	// Прочитать беззнаковый байт из тела с проверкой границ.
	FORCEINLINE int32 U8(const TArray<uint8>& B, int32 Off)
	{
		return B.IsValidIndex(Off) ? (int32)B[Off] : 0;
	}
	FORCEINLINE int32 I32(const TArray<uint8>& B, int32 Off)
	{
		return B.IsValidIndex(Off + 3) ? AsdaBytes::RdI32(B.GetData() + Off) : 0;
	}
}
