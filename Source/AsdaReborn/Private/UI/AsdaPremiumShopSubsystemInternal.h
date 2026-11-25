// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPremiumShopSubsystemInternal.h — ★ R2 10.11.2026 (разрез AsdaPremiumShopSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaPremiumShopSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaPremiumShopSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaPremiumShopSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"   // ★ 06.11.2026: звуки окна 6000 (4/5/6)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaNoticeColors.h"   // ★ 02.11.2026: цвет −11514 чата 12919 (гейт `uiCanInteractWithTarget`)
#include "UI/AsdaHairShopSubsystem.h"        // ★ ревизия М-3: сессия парикмахерской (`CObject_GetField166`)
#include "UI/AsdaBoosterPanelSubsystem.h"   // ★ ревизия М-3: барабан гачи 10050 (`CObject_GetField254`); ★ 31.10.2026: окно «Вид» 15600
#include "UI/AsdaWarehouseSubsystem.h"      // ★ ревизия М-3: свиток склада (`CObject_GetField5581`)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: гейты 6947 (открытие) и `uiCanInteractWithTarget` (покупка) — окно обмена 2008

#include "AsdaDataTableCache.h"
#include "UI/AsdaEquipShopSubsystem.h"      // ★ ОБЩИЕ ТОЧКИ витрины `.she` (закон port-shared-points)
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaPackagePanelSubsystem.h"   // ★ панель состава набора 26500 — уже портирована
#include "UI/AsdaBoosterShopSubsystem.h"   // ★ самотест общий: печатает и магазин бустеров 15300
#include "UI/AsdaPremiumMallSubsystem.h"    // консольные команды дёргают и диспетчер 30000
#include "UI/AsdaPurchaseBoxSubsystem.h"    // ★ общая модалка подтверждения 1559
#include "UI/AsdaCashGiftSubsystem.h"       // ★ 01.11.2026 P101_CashGift: подарок 5072/5074 (кнопки 6085 + 9i, панели 6296/6333)
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
// ★ волна В4 ремесла: хвост 5045 при закрытии (восстановление снимка окон)   ★ R3: комментарий снятого включения "Npc/AsdaNpcDialogSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaNpcShopSubsystem.h"        // ★ волна В5: `uiInventoryOpenWindow` закрывает магазин NPC

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ComboBoxString.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "AsdaBytes.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Internationalization/Regex.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "UI/AsdaDisassemblySubsystem.h"    // ★ волна 2-А: окно разборки 32000 видно ⇒ магазин 6000 молча не открывается
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): разбор аргументов самотеста (`quit` не путается с числом)
#include "Text/AsdaLang.h"          // ★ 26.10.2026: самотест имён бустеров — язык сессии (RU/EN)
#include "UObject/UObjectIterator.h" // ★ 26.10.2026: самотест имён бустеров — обход текстов открытых окон
// ★ 26.10.2026: самотест премиум-коробок — подбор ячеек вскрытия (ReservePackageSlots)   ★ R3: комментарий снятого включения "UI/AsdaInventorySubsystem.h" (файл им не пользовался) — сохранён
#include "Misc/FileHelper.h"                // ★ 26.10.2026: самотест премиум-коробок читает Tools/data/premium_box_sets.json
#include "Misc/Paths.h"                     // ★ 26.10.2026: путь спецификации от каталога проекта
#include "Dom/JsonObject.h"                 // ★ 26.10.2026: разбор спецификации наборов
#include "Serialization/JsonReader.h"       // ★ 26.10.2026: разбор спецификации наборов
#include "Serialization/JsonSerializer.h"   // ★ 26.10.2026: разбор спецификации наборов
// ★ 24.10.2026 (выход из сидения 1:1): `ApplyEtalonSceneStop` — стоп/«встать» при входе в сцену   ★ R3: комментарий снятого включения "World/AsdaPlayerCharacter.h" (файл им не пользовался) — сохранён
#include "UI/AsdaSystemOptionSubsystem.h"      // ★ 25.10.2026 (ревизия sit108, М-3): гейт «персонаж занят» (`objStateAllowsRender`) до стопа сцены
#include "GameFramework/PlayerController.h"    // ★ 24.10.2026: свой павн для `ApplyEtalonSceneStop`

// Категория журнала частей AsdaPremiumShopSubsystem*.cpp: определение — AsdaPremiumShopSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaPremShop, Log, All );

namespace {
	const FName  kWindowId(TEXT("SHELLITEM_STORE_WIN"));

	// ── ВКЛАДКА 6009 → НОМЕР СПИСКА. Снято с `inventorySowelWin_SwitchTab@0xC3F070`
	//    (`ida.c:485945-486070`), вывод базы дескриптора — в шапке .h.
	//    ★ Сегмент 9 «Инструмент» ветки НЕ ИМЕЕТ (`default: break`) — дефект оригинала К17.
	constexpr int32 kTabToList[UAsdaPremiumShopSubsystem::kMenuTabs] =
		{ 11, 12, 13, 5, 6, 7, 8, 9, 10, -1 };

	// Подписи полосы 6009 — `CGameDialog_InitControls@0xC321D0`.
	constexpr int32 kTabLoc[UAsdaPremiumShopSubsystem::kMenuTabs] =
		{ 6915, 6916, 6917, 6443, 6844, 6845, 6846, 6847, 6848, 12907 };
	// Подписи полосы 6002 — сегмент 2 кодом НЕ заполняется (дефект К8).
	constexpr int32 kIndexLoc[UAsdaPremiumShopSubsystem::kIndexTabs] = { 12900, 6808, -1 };

	// ★★★ ТРЕТИЙ УРОВЕНЬ 58203 → значение поля `Sub2` записи `PremiumShop.BIN`.
	//   Снято со свитча `uiItemDlg_onCommand` case 0xE35B (`ida.c:478253-478300`) в паре с
	//   раскладкой векторов в `ui_rebuildCategorizedItemList` (`ida.c:473411-473440`):
	//     вектор +1659 (сегм.0 «Общий вид») — ВСЕ записи класса
	//     +1671 ← Sub2 9  (сегм.1 «Оружие»)      +1683 ← Sub2 0  (сегм.2 «Шлем»)
	//     +1695 ← Sub2 1  (сегм.3 «Доспех»)      +1707 ← Sub2 4  (сегм.4 «Перчатки»)
	//     +1719 ← Sub2 2  (сегм.5 «Штаны»)       +1731 ← Sub2 3  (сегм.6 «Ботинки»)
	//     +1743 ← Sub2 0xB(сегм.7 «Бустер»)
	//   ★ ДЕФЕКТ ДАННЫХ/ОРИГИНАЛА: в нашем `PremiumShop.BIN` встречается ещё `Sub2 == 8`
	//     (класс 1 и 3), а ветки под него в эталоне НЕТ — такие записи видны только в «Общий вид».
	constexpr int32 kSub2ToField[UAsdaPremiumShopSubsystem::kSub2Tabs] = { -1, 9, 0, 1, 4, 2, 3, 11 };

	// Категория полосы 58200 → значение поля `Category` записи (`ui_rebuildCategorizedItemList`).
	//   0 и 1 отбираются НЕ по `Category`, а по флагу `NewFlag` (+24) = 1 и 2 соответственно.
	constexpr int32 kCatToField[UAsdaPremiumShopSubsystem::kCatTabs] = { -1, -1, 1, 2, 3, 4, 5, 6 };
}

// Ключ CVarLegacyPanelHeight: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE   // [ИСПРАВЛЕНИЕ] ревизия wave_1025 М-6: перехват самотеста — только в дев-сборке
extern TAutoConsoleVariable<int32> CVarLegacyPanelHeight;
#else
extern TAsdaConstCVar<int32> CVarLegacyPanelHeight;
#endif

// Ключ CVarPremiumUiSound: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE   // [ИСПРАВЛЕНИЕ] ревизия wave_1025 М-6: перехват самотеста — только в дев-сборке
extern TAutoConsoleVariable<int32> CVarPremiumUiSound;
#else
extern TAsdaConstCVar<int32> CVarPremiumUiSound;
#endif

namespace {
	// Звук окна 6000 по id таблицы SFT (ключ `asda.PremiumShop.UiSound`).
	static void AsdaPremiumUiSound(const UGameInstance* GI, int32 SoundId)
	{
		if (CVarPremiumUiSound.GetValueOnGameThread() == 0 || !GI) { return; }
		if (UAsdaSoundSubsystem* Snd = const_cast<UGameInstance*>(GI)->GetSubsystem<UAsdaSoundSubsystem>()) { Snd->PlaySound2DById(SoundId); }
	}
}

// Ключ CVarHideIndexSeg3: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE   // [ИСПРАВЛЕНИЕ] ревизия wave_1025 М-6: перехват самотеста — только в дев-сборке
extern TAutoConsoleVariable<int32> CVarHideIndexSeg3;
#else
extern TAsdaConstCVar<int32> CVarHideIndexSeg3;
#endif

// Ключ CVarFixTab9: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE   // [ИСПРАВЛЕНИЕ] ревизия wave_1025 М-6: перехват самотеста — только в дев-сборке
extern TAutoConsoleVariable<int32> CVarFixTab9;
#else
extern TAsdaConstCVar<int32> CVarFixTab9;
#endif
