// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWarehouseSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaWarehouseSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaWarehouseSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaWarehouseSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaWarehouseSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaExchangeSubsystem.h"   // ★ пачка 20, жалоба 10: рынок 13000 открыт — склад не открывается
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 05.11.2026: гейт «своя/чужая лавка открыта» перед 5008/5000 (`asda.Warehouse.SelfStoreGate`)
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaCharaInfoSubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaUiSlotDrift.h"   // ★ 25.10.2026: D19 не теряет перетаскивание окна (учёт внешнего сдвига слота)
#include "AsdaNetSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"   // ★ whwheel: курсор для колеса над списком 4189
#include "UI/AsdaCloseGear.h"   // ★ 08.11.2026: шестерёнка у крестика — общая точка (asda.UI.CloseGearHover)

// Категория журнала частей AsdaWarehouseSubsystem*.cpp: определение — AsdaWarehouseSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaWare, Log, All );

// ═════════════════════════ КОНСТАНТЫ ОКОН ═════════════════════════════════════════════════════
static const FName  kWareWindowId(TEXT("WARE_WIN"));           // IDNum 4000
static const FName  kRecallWindowId(TEXT("Warehouse_Win"));    // IDNum 5500

// ★ ГЕОМЕТРИЯ СПИСКА-ПРОКРУТКИ 4189 `WAREKEEP_LIST` (JUI: 51,206 198×165, скин `WheelWin_Nomal`).
//   Самого контрола в WBP НЕТ: у колёсного окна полосу создаёт движок в рантайме
//   (`uiControl_AttachVertScrollbar@0x12F3160` читает у скина `ScrollBarFileNameVert = "Nomal_Scr"`),
//   а импортёр разбирает только `gamemain_jui.ini`. Собираем её общей точкой `FAsdaScrollBarNomal`
//   ровно там, где она стояла бы, — у правого края области списка. Числа из `.JUI`, не подобраны.
static constexpr float kListWidth  = 198.f;
static constexpr float kListHeight = 165.f;

namespace WareLoc {
	constexpr int32 kCatSpend     = 1204;     // 4188 пункт 0 «General/Sowel»
	constexpr int32 kCatShell     = 1205;     // 4188 пункт 1 «Gear/Premium»
}

// Ключ CVarWareSelfStoreGate: определение — в домашней части (★ R3-c 12.11.2026, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarWareSelfStoreGate;
#else
extern TAsdaConstCVar<int32> CVarWareSelfStoreGate;
#endif

// Номер строки «Ты в своей лавке» — эталонный нотис гейтов лавки (`uiWindow_OpenIfAllowed`, `uiQuestGuide_Open` и др.).
static constexpr int32 kWareLocOwnShop = 107022;

// Гейт лавки перед пакетом склада, который сервер отвергнет по причине 4 (`Who` — для журнала). true — пакет не слать.
static bool WareBlockedBySelfStore(const UObject* Ctx, const TCHAR* Who)
{
	if (CVarWareSelfStoreGate.GetValueOnGameThread() == 0) { return false; }
	if (!UAsdaTradeStateSubsystem::IsSelfStoreOpen(Ctx)) { return false; }
	UAsdaTradeStateSubsystem::ChatStoreGate(Ctx, kWareLocOwnShop, Who);
	return true;
}

namespace WareIds {
// ═════════════════════════ ТАБЛИЦЫ IDNUM (§4.1, `itemCodeToGroupIndexA@0xE896C0`) ══════════════
//   База ячеек каждой вкладки и базовые idnum служебных контролов окон-сумок.
	// Первый idnum ячейки вкладки: 0 — сетка окна 4000, 1…8 — окна-сумки.
	static const int32 kTrayBase[UAsdaWarehouseSubsystem::kStoreTabs] =
		{ 4008, 4055, 4089, 4123, 4157, 75411, 75511, 75611, 75711 };
	// Первый idnum ячейки КОРЗИНЫ переноса (эталонная «вкладка 9»).
	static constexpr int32 kBasketBase = 4039;
	// idnum чек-кнопок вкладок 1…8 (4004…4007 — первый ряд, 4301…4304 — второй).
	static const int32 kBagButton[UAsdaWarehouseSubsystem::kBags] =
		{ 4004, 4005, 4006, 4007, 4301, 4302, 4303, 4304 };
	// idnum окон-сумок (для дампов и журналов).
	static const int32 kBagWindow[UAsdaWarehouseSubsystem::kBags] =
		{ 4052, 4086, 4120, 4154, 75400, 75500, 75600, 75700 };
}
