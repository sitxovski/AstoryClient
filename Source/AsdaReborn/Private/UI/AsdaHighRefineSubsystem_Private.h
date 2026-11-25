// Copyright (c) 2026 @sitxovski (Discord)
// AsdaHighRefineSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaHighRefineSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaHighRefineSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaHighRefineSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaHighRefineSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "UI/AsdaJuiWindowHandle.h"   // каркас окна JUI (Ensure + зона рамки + заливка полосы)
#include "UI/AsdaNotice.h"            // фасад нотисов (Loc / ChatColored)

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "UI/AsdaAnimatedPlane.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaItemUpgradeSubsystem.h"
#include "UI/AsdaItemResetSubsystem.h"   // самотест волны 9: звук кнопки 20370 окна 20200
#include "UI/AsdaCraftWindowSubsystem.h" // самотест: закрыть 76000, открытое кнопкой 452 (волна В6 ремесла)
#include "UI/AsdaDisassemblySubsystem.h"  // ★ волна 2-А: «ОК» 78291 не работает, пока идёт разборка 32000 (`g_bRefineInProgress`)
#include "UI/AsdaPremiumPostSubsystem.h"   // ★ 11.10.2026 (В3 S1): закрытие элитного ящика, открытого кнопкой 10082 самотеста
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaWindowSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "TimerManager.h"
#include "Containers/Ticker.h"         // ★ волна 11: второй клик двойного щелчка — сразу после отпускания первого
#include "UnrealClient.h"             // FScreenshotRequest — именованный кадр самотеста
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "UI/AsdaItemRecordWriter.h"   // ★ U17 (В1 дедупа №2): синтетическая запись предмета 57 б

// Категория журнала частей AsdaHighRefineSubsystem*.cpp: определение — AsdaHighRefineSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaHighRefine, Log, All );

// Ключ CVarWheelPages: определение — в домашней части (★ R3-c 12.11.2026, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarWheelPages;
#else
extern TAsdaConstCVar<int32> CVarWheelPages;
#endif

namespace {
	// Окно и его виджет. Ассет уже импортирован заходом JUI (не перегенерировать!).
	const FName  kWindowId(TEXT("Smelting3_Win"));

	// Положение слота канваса (Left/Top) или ноль. ★ R5-b 12.11.2026: тело — общая точка `AsdaUi::SlotPos` (дословно то же).
	using AsdaUi::SlotPos;

	// Показ одноразового эффекта (`CControl_SetVisible(c, 1) + CControl_Show(c)`) / скрытие без события.
	void ShowPlane(UAsdaAnimatedPlane* P, bool bShow)
	{
		if (!P) { return; }
		AsdaUi::SetShownNoHit(P, bShow);   // U3 (дедуп №2, В2-A, 11.10.2026): правило «показать без хит-теста» — общая `AsdaUi::SetShownNoHit` (вынос, поведение то же)
		if (bShow) { P->RestartAnim(); } else { P->StopAnim(); }
	}

// ═════════════════════════════ КОНСОЛЬ И САМОТЕСТ ═════════════════════════════════════════════
	UAsdaHighRefineSubsystem* HighRefineFromWorld(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaHighRefineSubsystem>() : nullptr;
	}
}

// ★ R3-c 12.11.2026: многоимённая константа без состояния — нужна нескольким частям (перенесено дословно из AsdaHighRefineSubsystem.cpp)
namespace {
	// Виды наведения (для тултипа).
	enum : int32 { kTipGrid = 0, kTipResult = 1, kTipMat = 2 };
}
