// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPremiumGachaSubsystemInternal.h — ★ R2 10.11.2026 (разрез AsdaPremiumGachaSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaPremiumGachaSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaPremiumGachaSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaPremiumGachaSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaPremiumPostSubsystem.h"   // отложенный показ приза: сообщаем ящику итог выбора
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaGachaSceneActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"                          // TActorIterator — поиск актора сцены для диагностики
#include "Kismet/KismetRenderingLibrary.h"   // ★ 24.09: выгрузка буфера сцены гачи в PNG (asda.Gacha.DumpRt)
#include "Components/Image.h"
#include "Engine/World.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Engine/DataTable.h"
#include "Items/AsdaItemDbRows.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaAnimatedPlane.h"      // 68058 `Apple_Make3` — кадровый анимплан (RestartAnim = `CControl_Show`)
// общая плашка подсказки + владелец показа   ★ R3: комментарий снятого включения "UI/AsdaItemTooltipSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaInventorySubsystem.h"   // ★ общая точка модели контейнеров: ApplyExternalRecord (6582)
#include "UI/AsdaNotice.h"          // ★ 07.11.2026: окно-сообщение ONEBUT_MSGBOX (`CItem_BuildTooltipText(…, 0, -1, …)`)
#include "UI/AsdaUiHelpers.h"
#include "Components/AudioComponent.h"
#include "HAL/IConsoleManager.h"   // ★ 08.11.2026: ключ asda.Gacha.IdleLoop2D
#include "UI/AsdaJuiComboBox.h"   // ★ ОБЩАЯ точка: выпадающий список скина Bk_ComboBox
#include "UI/AsdaJuiWindowMeta.h"   // мета окон: ручка перетаскивания живёт в данных
#include "Framework/Application/SlateApplication.h"   // курсор для зоны колеса над окном 68310
#include "GameFramework/PlayerController.h"
#include "World/AsdaCoord.h"   // ★ единый перевод UE→игра: тайл входа в сцену (шаг 9)
#include "UI/AsdaCraftSubsystem.h"         // ★ гейт входа: открыты ли окна ремесла 76000/77500 (волна В2)
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "UI/AsdaItemRecordWriter.h"   // ★ U17 (В1 дедупа №2): синтетическая запись предмета 57 б

// Категория журнала частей AsdaPremiumGachaSubsystem*.cpp: определение — AsdaPremiumGachaSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaGacha, Log, All );

namespace {
	// Семь окон сцены. Порядок = порядок показа в `premiumGacha_BeginRoll`.
	struct FWin { const TCHAR* Id; const TCHAR* Wbp; int32 Z; };
	const FWin kRoot   { TEXT("Premium_PickBAR_Win"),        TEXT("WBP_Premium_PickBAR_Win"),        400 };
	const FWin kExit   { TEXT("Premium_PickBAR_daybtn_X_B"), TEXT("WBP_Premium_PickBAR_daybtn_X_B"), 402 };
	const FWin kSelect { TEXT("Premium_Pick_select"),        TEXT("WBP_Premium_Pick_select"),        404 };
	const FWin kClick  { TEXT("Premium_Pick_Click_Win"),     TEXT("WBP_Premium_Pick_Click_Win"),     404 };
	const FWin kView   { TEXT("Premium_Pick_View_Win"),      TEXT("WBP_Premium_Pick_View_Win"),      406 };
	const FWin kMsg    { TEXT("Premium_Pick_mssageBOX"),     TEXT("WBP_Premium_Pick_mssageBOX"),     408 };
	const FWin kSel2   { TEXT("Premium_Pick_select_2"),      TEXT("WBP_Premium_Pick_select_2"),      408 };

// ══════════════════ ИМЕНА ВИДЖЕТОВ (сняты ДАМПОМ реальных WBP) ══════════════════
//   `Tools\asda_ui_dump_window.py` + `ASDA_DUMP_WBP`, лог `Reports/headless_gacha_dump.log`.
//   Закон `jui-importer-widget-naming`: точное имя `idc` есть только у интерактива и Text,
//   планы/картинки получают `{idc|скин}_{juiIdx}_{план}`. У контролов с ПУСТЫМ `ID` в
//   `gamemain_jui.ini` (сетка призов, кнопки «Закрыть» 68357 и «ОК» 68511) имени idc нет вовсе,
//   поэтому они адресуются именем скина с idx-суффиксом — иначе их не найти.
	// 25 ячеек витрины 68311…68335 (пять групп-строк по пять). Порядок = порядок IDNum.
	const TCHAR* kGridCells[25] = {
		TEXT("NoImage_icon_16_0"), TEXT("NoImage_icon_17_0"), TEXT("NoImage_icon_18_0"), TEXT("NoImage_icon_19_0"), TEXT("NoImage_icon_20_0"), // 68311…68315
		TEXT("NoImage_icon_28_0"), TEXT("NoImage_icon_29_0"), TEXT("NoImage_icon_30_0"), TEXT("NoImage_icon_31_0"), TEXT("NoImage_icon_32_0"), // 68316…68320
		TEXT("NoImage_icon_40_0"), TEXT("NoImage_icon_41_0"), TEXT("NoImage_icon_42_0"), TEXT("NoImage_icon_43_0"), TEXT("NoImage_icon_44_0"), // 68321…68325
		TEXT("NoImage_icon_52_0"), TEXT("NoImage_icon_53_0"), TEXT("NoImage_icon_54_0"), TEXT("NoImage_icon_55_0"), TEXT("NoImage_icon_56_0"), // 68326…68330
		TEXT("NoImage_icon_64_0"), TEXT("NoImage_icon_65_0"), TEXT("NoImage_icon_66_0"), TEXT("NoImage_icon_67_0"), TEXT("NoImage_icon_68_0")  // 68331…68335
	};

	// Шаг прокрутки витрины = 5 ЗАПИСЕЙ, а не 25: эталон `uiRefreshItemGridPage@0xB6EE70` пропускает
	//   `5 * scrollPage` записей и рисует следующие ≤25, а диапазон полосы ставит `ceil(count / 5)`.
	constexpr int32 kGridPageStep = 5;
	constexpr int32 kGridCellCount = 25;

	constexpr int32 kLocNoMoney       = 110624;  // «У тебя недостаточно денег. Чтобы пополнить счет, нажми…»
}

// Ключ CVarGachaPrizeCloseLock: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
extern TAutoConsoleVariable<int32> CVarGachaPrizeCloseLock;
#else
extern TAsdaConstCVar<int32> CVarGachaPrizeCloseLock;
#endif

// Читатель ключа выше.
static bool AsdaGachaPrizeCloseLockOn() { return CVarGachaPrizeCloseLock.GetValueOnGameThread() != 0; }
