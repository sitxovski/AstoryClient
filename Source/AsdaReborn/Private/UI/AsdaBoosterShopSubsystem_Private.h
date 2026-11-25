// Copyright (c) 2026 @sitxovski (Discord)
// AsdaBoosterShopSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaBoosterShopSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaBoosterShopSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaBoosterShopSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaBoosterShopSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "UI/AsdaBoosterPanelSubsystem.h"   // ★ окно предпросмотра 15600 — общая точка, уже есть
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::ShownVisibilityFor — общая точка видимости узла
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaPurchaseBoxSubsystem.h"    // ★ общая модалка 1559, режим F
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"

#include "Components/Button.h"
#include "Engine/Engine.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

// Категория журнала частей AsdaBoosterShopSubsystem*.cpp: определение — AsdaBoosterShopSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaBoosterShop, Log, All );

// Ключ CVarAsdaBoosterGridFollowCells: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarAsdaBoosterGridFollowCells;
#else
extern TAsdaConstCVar<int32> CVarAsdaBoosterGridFollowCells;
#endif

// Ключ CVarAsdaBoosterSelFrameResetOnOpen (рамка 15496 гаснет при сбросе выбора): определение — AsdaBoosterShopSubsystem.cpp.
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarAsdaBoosterSelFrameResetOnOpen;
#else
extern TAsdaConstCVar<int32> CVarAsdaBoosterSelFrameResetOnOpen;
#endif

// Зоны следуют за ячейками; под открытой панелью покупки/подарка зоны гаснут: панель лежит в окне ниже зон (Z=50), и часть кнопки 15520
//   открывала «Вид» карточки под ней. Зоны вне панели работают как прежде. Закрытие панели возвращает всё через тот же вызов.
static void AsdaBoosterZonesFollowPanel(UAsdaIconCellGrid* Grid, const UWidget* OpenPanel, int32 NumCells)
{
	if (!Grid || CVarAsdaBoosterGridFollowCells.GetValueOnGameThread() == 0) { return; }
	Grid->SyncClickOverlay(/*bFollowCellVisibility*/true);
	FVector2D PMn, PMx;
	if (!OpenPanel || !AsdaUi::WidgetAbsRect(OpenPanel, PMn, PMx)) { return; }
	for (int32 i = 0; i < NumCells; ++i)
	{
		UButton* Z = Grid->GetHitButton(i);
		FVector2D ZMn, ZMx;
		if (!Z || Z->GetVisibility() == ESlateVisibility::Collapsed || !AsdaUi::WidgetAbsRect(Z, ZMn, ZMx)) { continue; }
		if (ZMn.X < PMx.X && PMn.X < ZMx.X && ZMn.Y < PMx.Y && PMn.Y < ZMx.Y) { Z->SetVisibility(ESlateVisibility::Collapsed); }
	}
}

namespace {
	const FName  kWindowId(TEXT("BOOSTER_SHOP_WIN"));

	const FName  kMsgBaseId(TEXT("SHELLITEM_MsgBase"));

	// ── Подписи КАТЕГОРИЙ 15545 (`BoosterShop_InitWindow`, `ida.c:420398-420411`) ──
	constexpr int32 kCatLoc[UAsdaBoosterShopSubsystem::kCats] =
		{ 12775, 13240, 6915, 6916, 6917, 8434, 15150 };
}
