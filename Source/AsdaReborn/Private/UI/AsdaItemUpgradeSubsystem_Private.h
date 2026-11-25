// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemUpgradeSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaItemUpgradeSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaItemUpgradeSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaItemUpgradeSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaItemUpgradeSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // каркас окна JUI (Ensure + зона рамки)
#include "UI/AsdaNotice.h"            // фасад нотисов (Loc / LocRaw / MsgBox / ChatColored)
#include "UI/AsdaUiHelpers.h"         // ★ U3 (дедуп №2, В2-A): AsdaUi::SetShownNoHit
#include "Misc/FileHelper.h"          // ★ Д-18: `DefaultAsdaUpgrade.ini` — коды «сервер — не снаряжение»

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Items/AsdaItemDbRows.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "UI/AsdaAnimatedPlane.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaMarkup.h"            // UAsdaRichText::ApplyFormatted — единая точка тегов <FC=…>
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaHighRefineSubsystem.h"   // ★ волна 7: кнопка 20197 «Особый» → окно 78200
#include "Resources/AsdaSoundSubsystem.h" // ★ волна 5: звуки окна — общая звуковая система порта
#include "World/AsdaWorldSubsystem.h"     // ★ волна 5: самотест связи «ступень заточки → свечение оружия»

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "TimerManager.h"
#include "UnrealClient.h"             // FScreenshotRequest — именованный кадр самотеста
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотестов — запомнить и вернуть (значение + SetBy)

// Категория журнала частей AsdaItemUpgradeSubsystem*.cpp: определение — AsdaItemUpgradeSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaUpgrade, Log, All );

// Ключ CVarEnhancePanel: определение — в домашней части (★ R3-c 12.11.2026, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarEnhancePanel;
#else
extern TAsdaConstCVar<int32> CVarEnhancePanel;
#endif

// Ключ CVarHonestNoUpgradeGuide: определение — в домашней части (★ R3-c 12.11.2026, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarHonestNoUpgradeGuide;
#else
extern TAsdaConstCVar<int32> CVarHonestNoUpgradeGuide;
#endif

// Ключ CVarGearOnlyGrid: определение — в домашней части (★ R3-c 12.11.2026, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarGearOnlyGrid;
#else
extern TAsdaConstCVar<int32> CVarGearOnlyGrid;
#endif

// Ключ CVarSingleInFlight: определение — в домашней части (★ R3-c 12.11.2026, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarSingleInFlight;
#else
extern TAsdaConstCVar<int32> CVarSingleInFlight;
#endif

// Ключ CVarResultNeverLost: определение — в домашней части (★ R3-c 12.11.2026, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarResultNeverLost;
#else
extern TAsdaConstCVar<int32> CVarResultNeverLost;
#endif

namespace {
	// Окна и их виджеты. Ассеты уже импортированы заходом JUI (не перегенерировать!).
	const FName  kWindowId(TEXT("Item_Upgrade_Win"));
	const FName  kPopupId(TEXT("Get_UpgradeOtion"));

	// Положение слота канваса (Left/Top) или ноль. ★ R5-b 12.11.2026: тело — общая точка `AsdaUi::SlotPos` (дословно то же).
	using AsdaUi::SlotPos;

// ═════════════════════════════ КОНСОЛЬ ════════════════════════════════════════════════════════
	UAsdaItemUpgradeSubsystem* UpgradeFromWorld(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaItemUpgradeSubsystem>() : nullptr;
	}
}

// ★ R3-c 12.11.2026: многоимённая константа без состояния — нужна нескольким частям (перенесено дословно из AsdaItemUpgradeSubsystem.cpp)
namespace {
	// Виды ячеек для наведения/тултипа.
	enum : int32 { kTipGrid = 0, kTipScroll = 1, kTipH97 = 2, kTipH98 = 3, kTipCard = 4, kTipTarget = 5, kTipHint = 6 };
}
