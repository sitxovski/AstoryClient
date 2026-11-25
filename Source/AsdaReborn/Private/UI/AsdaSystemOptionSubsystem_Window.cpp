// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSystemOptionSubsystem_Window.cpp — ★ R2 10.11.2026 (разрез AsdaSystemOptionSubsystem.cpp, дословный перенос): окно настроек: виджет, таблица флажков, SetupOptionControls, вкладки, ползунки, флажки.
//   Общее для частей — в AsdaSystemOptionSubsystemInternal.h; шапка и реверс — в AsdaSystemOptionSubsystem.cpp.
#include "UI/AsdaSystemOptionSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaEscChain.h"   // ★ 25.10.2026: шаг «ESC снимает выбранную цель» эталонной цепочки (между проходом окон и меню 69400)
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaLocIds.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaJuiComboBox.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaShowOffSubsystem.h"
#include "UI/AsdaChatBalloonSubsystem.h"
#include "UI/AsdaAfterDeathSubsystem.h"   // ★ гейт ESC, ветка 1: принудительный экран смерти
#include "Login/AsdaButtonRelay.h"
#include "Resources/AsdaVideoSettingsSubsystem.h"
#include "Resources/AsdaBenchSubsystem.h"   // ★ 12.10.2026: «Автоподбор» = замер настоящего кадра на месте
#include "Resources/AsdaGameUserSettings.h"   // ★ 19.09.2026: профиль игрока — штатное хранилище настроек окна
#include "Resources/AsdaSoundSubsystem.h"      // ★ 19.09.2026: громкость — через профиль, мимо приоритетов ключей консоли
#include "World/AsdaKeybindConfig.h"
#include "World/AsdaKeybindProfile.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Text/AsdaLang.h"   // ★ 09.10.2026: строка «Язык» (мультиязычность)
#include "Scalability.h"   // ★ фаза 2: уровни групп качества при открытии окна
#include "Test/AsdaSelfTestKit.h"   // ★ 12.10.2026: самотест asda.SysOpt.SelfTest (аргументы, отказ онлайн, итог)
// ★ 18.10.2026: самотест строки «Цифры урона» — GetDamageTextMode/ShouldShowDamageText   ★ R3: комментарий снятого включения "World/AsdaWorldSubsystem.h" (файл им не пользовался) — сохранён
#include "UnrealClient.h"                // ★ 18.10.2026: FScreenshotRequest — кадры окна в самотесте «Цифры урона»
#include "Misc/FileHelper.h"             // ★ 18.10.2026: чтение GameUserSettings.ini в самотесте «Цифры урона»
#include "Containers/Ticker.h"           // ★ 18.10.2026: пошаговый самотест «Цифры урона»
#include "UI/AsdaJuiWindowHandle.h"        // ★ Дедуп №2, В1-B (U22): каркас `EnsureLazy` окон 69400/69500
#include "World/AsdaCursorClip.h"           // ★ 01.11.2026: курсор не покидает окно игры
#include "World/AsdaOrbitCursor.h"          // ★ 07.11.2026: курсор при зажатой ПКМ скрыт и стоит на месте
#include "World/AsdaCursorSpeed.h"          // ★ 19.10.2026: «Чувствительность мыши» (строка I2) — скорость курсора
#include "AsdaSystemOptionSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaSystemOptionSubsystem*.cpp

static const TCHAR* kOptionWbpPath = TEXT("/Game/AsdaAssets/UI/Player_UI/System/WBP_SYSTEM_Option_NEW");

namespace SysOptLoc {
	// Переключатель вкладок 69502 (`uiNoticeDialogInitControls`, ui_core.c:88084-88090).
	static constexpr int32 kTab0 = 30000;   // «Настройки игры»      → вкладка ИНТЕРФЕЙС (70150)
	static constexpr int32 kTab1 = 30001;   // «Настройки системы»   → вкладка СИСТЕМА   (70000)
	static constexpr int32 kTab2 = 30002;   // «Горячие клавиши»     → вкладка КЛАВИШИ   (69510)

	static constexpr int32 kBtnApply   = 2377;    // 70441 «Применить»

	static constexpr int32 kBtnDefault = 11600;   // 69651 «По умолчанию»
	static constexpr int32 kBtnClearKey= 11601;   // 69652 «Снять привязку»

	// Списки под-страниц.
	static constexpr int32 kHkPage0 = 30011;   // «Интерфейс»            (69648 INTER)
	static constexpr int32 kSysPage0 = 30501;  // «Звук»                 (70010)
	static constexpr int32 kIntPage0 = 30400;  // «Сообщество»           (70170)

	// Подписи строк (первые id групп; дальше — подряд).
	static constexpr int32 kInt1First = 30421;   // 7 строк, панель 70170
	static constexpr int32 kInt2First = 30451;   // 3 строки (эталон подписывает ровно три), панель 70250
	static constexpr int32 kInt3First = 30471;   // 10 строк, панель 70350
	static constexpr int32 kSndFirst  = 30521;   // 2 строки, панель 70010
	static constexpr int32 kScrFirst  = 30541;   // 3 строки, панель 70050 («Разрешение/Полный экран/Яркость»)
	static constexpr int32 k3dFirst   = 30561;   // 9 строк, панель 70100
	static constexpr int32 k3dFlare   = 30570;   // подпись комбо 70121
	static constexpr int32 kFlareFirst = 12190;  // 5 пунктов комбо 70121

	// ═══ ★ ФАЗА 2 (19.09.2026): НАШИ СТРОКИ ОКНА (диапазон порта > 990000, `RawAssets/Text/GameText.csv`) ═══
	//   Подписи новых строк, пунктов списков и нотисов. Эталонных строк для них нет — это настройки UE,
	//   вынесенные из dev-окна Ctrl+N (решения владельца №2–№4).
	static constexpr int32 kPageQuality   = 990300;   // «Качество»           (4-я ячейка списка 70006)
}

bool UAsdaSystemOptionSubsystem::EnsureOptionWidget()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	// ★ Дедуп №2, В1-B (U22, 11.10.2026): каркас «открыть при первом показе» `FAsdaJuiWindowHandle::EnsureLazy` (кэш годен →
	//   true; сброс полей → открыть свёрнутым → разбор контролов → постановка). Порядок прежний, закрывателя у окна нет.
	return FAsdaJuiWindowHandle::Succeeded(FAsdaJuiWindowHandle(kOptionWindowId, kOptionWbpPath, kOptionZ).EnsureLazy(*WM, OptWidget, /*bReady*/true,
		ESlateVisibility::Collapsed,
		[this]() { ResetOptionWidget(); },
		[]() { UE_LOG(LogAsdaSysOpt, Error, TEXT("[настройки] WBP_SYSTEM_Option_NEW не загрузился (%s)"), kOptionWbpPath); },
		[this](UAsdaWindowSubsystem& Wm)
		{
			SetupOptionControls();
			// ★ 08.10.2026 [ОРИГИНАЛ] (window_default_pos.md §6 п.4): дефолт .JUI «ц50/ц50» рамки 702×512 — (161,128) при 1024×768;
			//   центр по габариту ВСЕХ детей (дети свисают ниже рамки) уводил окно вверх. Откат: `asda.UI.JuiDefaultPos 0`.
			if (!UAsdaWindowSubsystem::UseOriginalDefaultPos()) { Wm.CenterWindowOnScreen(OptWidget, nullptr); }
		}));
}

void UAsdaSystemOptionSubsystem::ResetOptionWidget()
{
	for (int32 t = 0; t < 3; ++t)
	{
		OptTabBtn[t] = nullptr; OptTabText[t] = nullptr; OptTabRoot[t] = nullptr;
		for (int32 p = 0; p < kMaxPages; ++p)
		{
			const int32 k = PageSlot(t, p);
			OptSubPage[k] = nullptr; OptListPlane[k] = nullptr; OptListBtn[k] = nullptr; OptListText[k] = nullptr;
			bOptListBrushValid[k] = false;
		}
	}
	HotkeyViewport = nullptr;
	for (int32 p = 0; p < 4; ++p) { HotkeyPageHost[p] = nullptr; }
	HotkeyPageBase.Reset();
	OptBtnApply = nullptr; OptBtnOk = nullptr; OptBtnCancel = nullptr; OptBtnClose = nullptr;
	OptBtnDefault = nullptr; OptBtnClearKey = nullptr;
	OptBtnApplyText = nullptr; OptBtnOkText = nullptr; OptBtnCancelText = nullptr;
	OptBtnDefaultText = nullptr; OptBtnClearKeyText = nullptr;
	ClickOverlay.Reset();
	ClickOverlayBase.Reset();
	HotkeyRows.Reset();
	for (FAsdaOptCheck& C : Checks) { C.Box = nullptr; C.Label = nullptr; }
	Sliders.Reset();
	if (ResolutionCombo) { ResolutionCombo->Reset(); ResolutionCombo = nullptr; }
	if (FlareCombo)      { FlareCombo->Reset();      FlareCombo = nullptr; }
	for (FAsdaOptExtraCombo& X : ExtraCombos) { if (X.Combo) { X.Combo->Reset(); } }
	ExtraCombos.Reset();
	AutoDetectBtn = nullptr;
	OptWidget = nullptr;
	bCapturing = false;
	CaptureAction = -1;
}

// ═══════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ ЯВНЫЙ ПОКАЗ ДВУХ СТРОК «ИНТЕРФЕЙС-3» — ЭТО ПОВЕДЕНИЕ ЭТАЛОНА, А НЕ ЗАПЛАТКА ИМПОРТА
// ═══════════════════════════════════════════════════════════════════════════════════════════════
// ЧТО ЗДЕСЬ БЫЛО ДО 17.09.2026 И ПОЧЕМУ СНЯТО.
//   Здесь жил ВРЕМЕННЫЙ МОСТ `RevealImportedPanel` с таблицами `kJuiHiddenIdx` / `kJuiHiddenNames`
//   и ключом `asda.SysOpt.RevealAssetGroups`. Он лечил ДЕФЕКТ ИМПОРТЁРА: `asda_jui_generic_umg.py`
//   НАСЛЕДОВАЛ скрытость вниз по дереву, и у потомков панели с `Visible = "0"` собственный
//   `Visible = "1"` терялся в ассете НАВСЕГДА — показать панель было нечем. Импортёр ПОЧИНЕН
//   (см. комментарий в его `build_tree`: узел получает СВОЙ флаг, вниз скрытость не идёт), а уже
//   собранные окна вылечены точечной записью свойства `visibility`
//   (`Tools/asda_ui_sync_own_visibility.py`, 202 узла в этом окне). Лечить то же самое ещё и в
//   рантайме — прямое нарушение закона `no-runtime-widget-fixes`, поэтому мост удалён целиком.
//   ⚠ Заодно ушла его собственная ошибка: `AsdaOptJuiIndexOf` читала УНИКАЛИЗАЦИЮ ИМЕНИ ГРУППЫ
//   (`Box_Check_grp_16`, `NoImage_grp_11`) как номер контрола `.JUI`. Из-за этого мост прятал
//   строки 0…2 вкладки «Интерфейс-2» (их эталон подписывает, цикл `j < 3`, ui_core.c:88161-88171)
//   и, наоборот, показывал строки 4…8, у которых в данных стоит ноль.
//
// ЧТО ОСТАЛОСЬ И ПОЧЕМУ ЭТО НЕ МОСТ. Ровно ДВА контрола оригинал показывает КОДОМ, вопреки своим
//   же данным: `uiDialogInitControls` (`RE/etalon/ui_core.c:88176-88187`) в цикле `k < 10` зовёт
//   `CControl_SetVisible(…, 1u)` для контролов `70361 + 2k` и `70362 + 2k`. Из двадцати
//   задействованных контролов в `gamemain_jui.ini` собственный `Visible = "0"` стоит только у
//   **70378** и **70380** — чекбоксов строк 08 и 09 вкладки «Интерфейс-3»; остальные восемнадцать
//   и так видимы и приезжают из ассета правильными. Одиннадцатая строка (70381/70382) в цикл НЕ
//   входит и обязана остаться скрытой — её мы не трогаем.
//   Чекбокс 70378/70380 в `.JUI` — КОНТЕЙНЕР (внутри лежит его подпись 70377/70379), поэтому
//   импортёр гасит его ГРУППУ `Box_Check_grp_*`; снимаем `Collapsed` с самого виджета и со всех
//   его контейнеров до корня панели — это и есть цепочечный показ, как у `CControl_SetVisible`.
void UAsdaSystemOptionSubsystem::RevealEtalonShownRows()
{
	// Чекбоксы строк 08 и 09 «Интерфейс-3» = контролы 70378 и 70380 (нумерация строк `{ЯКОРЬ}_CHKnn`
	//   даётся пост-шагом импортёра `asda_jui_parse.anchored_names`; строка 10 — это `CHK10`).
	static const TCHAR* const kEtalonShownChecks[] = {
		TEXT("SYSTEM_Op_INT_3_CHK08"),   // 70378 — k = 8 в цикле uiDialogInitControls
		TEXT("SYSTEM_Op_INT_3_CHK09"),   // 70380 — k = 9
	};
	UWidget* const PanelRoot = OptSubPage[PageSlot(0, 2)];          // SYSTEM_Op_INT_3_grp
	if (!OptWidget || !PanelRoot) { return; }
	int32 Revealed = 0;
	for (const TCHAR* Name : kEtalonShownChecks)
	{
		UWidget* Cur = OptWidget->GetWidgetFromName(Name);
		if (!Cur)
		{
			UE_LOG(LogAsdaSysOpt, Warning,
				TEXT("[настройки] узла «%s» нет в окне — строка «Интерфейс-3», которую эталон показывает явно, останется скрытой"),
				Name);
			continue;
		}
		// Поднимаемся до корня панели: сам корень НЕ трогаем — его видимостью распоряжается
		//   `ApplySubPageVisibility` (какая под-страница открыта — это состояние, как `uiTabWindowShowPage*`).
		for (UWidget* W = Cur; W && W != PanelRoot; W = Cast<UWidget>(W->GetParent()))
		{
			if (W->GetVisibility() != ESlateVisibility::Collapsed) { continue; }
			// Интерактиву нужен хит-тест на себе, контейнеру — только на детях.
			const bool bInteractive = W->IsA<UCheckBox>() || W->IsA<UButton>();
			W->SetVisibility(bInteractive ? ESlateVisibility::Visible : ESlateVisibility::SelfHitTestInvisible);
			++Revealed;
		}
	}
	if (Revealed > 0)
	{
		UE_LOG(LogAsdaSysOpt, Verbose,
			TEXT("[настройки] «Интерфейс-3»: показано узлов по циклу k<10 эталона — %d"), Revealed);
	}
}

// ── ТАБЛИЦА ЧЕКБОКСОВ. Заполняется РОВНО по эталону: idnum из `uiDialogInitControls`/
//    `uiTradeDialogInitControls`, подпись — из того же места, приёмник значения — из
//    `uiOptions_ApplyControlValue@0xED4F80`. Имена виджетов — из headless-дампа WBP.
void UAsdaSystemOptionSubsystem::BuildCheckTable()
{
	Checks.Reset();
	auto Add = [this](int32 IdNum, const FString& Widget, const FString& Label, int32 LocId)
	{
		FAsdaOptCheck C;
		C.IdNum = IdNum;
		C.WidgetName = FName(*Widget);
		C.LabelName = FName(*Label);
		C.LocId = LocId;
		Checks.Add(C);
	};

	// ── Вкладка ИНТЕРФЕЙС, панель 1 (70170): 7 строк, idnum 70172, 70174 … 70184, loc 30421…30427.
	//    Эталон `uiDialogInitControls` (ui_core.c:88132-88144): `2*i + 70172` и подпись `i + 30421`.
	for (int32 i = 0; i < 7; ++i)
	{
		Add(70172 + 2 * i, FString::Printf(TEXT("SYSTEM_Op_INT_1_CHK%02d"), i),
			FString::Printf(TEXT("SYSTEM_Op_INT_1_TXT%02d"), i), SysOptLoc::kInt1First + i);
	}
	// ── Панель 2 (70250): эталон подписывает ТРИ строки (ui_core.c:88145-88157, `j < 3`), а значения
	//    принимает у ШЕСТИ (0x11276…0x11280 = 70262…70272). Остальные три в данных `Visible = "0"`:
	//    обработчик у них есть, подписи нет — это дефект Д3, показываем ровно подписанные.
	for (int32 j = 0; j < 6; ++j)
	{
		Add(70262 + 2 * j, FString::Printf(TEXT("SYSTEM_Op_INT_2_CHK%02d"), j),
			FString::Printf(TEXT("SYSTEM_Op_INT_2_TXT%02d"), j),
			(j < 3) ? (SysOptLoc::kInt2First + j) : -1);
	}
	// ── Панель 3 (70350): 10 строк, idnum 70362 … 70380, loc 30471…30480 (ui_core.c:88158-88176).
	for (int32 k = 0; k < 10; ++k)
	{
		Add(70362 + 2 * k, FString::Printf(TEXT("SYSTEM_Op_INT_3_CHK%02d"), k),
			FString::Printf(TEXT("SYSTEM_Op_INT_3_TXT%02d"), k), SysOptLoc::kInt3First + k);
	}
	// ── Вкладка СИСТЕМА, панель ЗВУК (70010): чекбоксы 70012 и 70015 (`3*i + 70012`),
	//    подписи 70011/70014 → loc 30521/30522 (ui_core.c:88213-88228).
	Add(70012, TEXT("SYSTEM_Op_SYSTEM_SYS_11_CHK00"), TEXT("SYSTEM_Op_SYSTEM_SYS_11_TXT00"), SysOptLoc::kSndFirst + 0);
	Add(70015, TEXT("SYSTEM_Op_SYSTEM_SYS_11_CHK01"), TEXT("SYSTEM_Op_SYSTEM_SYS_11_TXT01"), SysOptLoc::kSndFirst + 1);
	// ── Панель ЭКРАН (70050). ★★★ 19.09.2026 (фаза 2, решение владельца №2): чекбокс 70055 «Полный экран»
	//    ЗАМЕНЁН списком «Режим экрана» (Полный экран / Без рамки / Окно, строка `SYSTEM_OX_S1`); строка
	//    чекбокса (`NoImage_grp_10`) свёрнута В АССЕТЕ шагом `asda_ui_option_extra_rows.py`, в таблицу он больше
	//    не входит — два контрола одной настройки противоречили бы друг другу.
	//    ⚠ Порядок подписей в ассете иной, чем порядок loc: TXT00 = полный экран (30542),
	//      TXT01 = яркость (30543), TXT02 = разрешение (30541) — так лежат контролы в .JUI.
	// ── Панель 3D (70100): 9 чекбоксов 70102 … 70118, подписи 70101 … 70117 → loc 30561…30569.
	for (int32 i = 0; i < 9; ++i)
	{
		Add(70102 + 2 * i, FString::Printf(TEXT("SYSTEM_O3_SYSTEM_SYS33_CHK%02d"), i),
			FString::Printf(TEXT("SYSTEM_O3_SYSTEM_SYS33_TXT%02d"), i), SysOptLoc::k3dFirst + i);
	}
	// ── ★ Фаза 2: флажки новых строк (вертикальная синхронизация, размытие, объёмные облака; ★ 12.10.2026 «затенение
	//    окружения» и «блики» сняты). idnum — «наши» 71000 + ключ строки; подпись — наша строка 9903xx.
	for (const SysOptExtra::FRowDef& R : SysOptExtra::kRows)
	{
		if (R.Kind != SysOptExtra::Check) { continue; }
		Add(SysOptExtra::kIdBase + R.Key, SysOptExtra::NodeName(R.Tag, TEXT("CHK")), SysOptExtra::NodeName(R.Tag, TEXT("TXT")), R.LabelLoc);
	}
}

// ── РАЗБОР ДЕРЕВА ОКНА НАСТРОЕК. Всё найденное — из headless-дампа WBP (802 виджета), ничего
//    не выдумано и ничего в разметке не правится (закон `no-runtime-widget-fixes`).
void UAsdaSystemOptionSubsystem::SetupOptionControls()
{
	if (!OptWidget || !OptWidget->WidgetTree) { return; }
	UUserWidget* W = OptWidget;

	auto ByName = [W](const TCHAR* Name) -> UWidget*
	{
		return W->GetWidgetFromName(FName(Name));
	};
	auto TxtByName = [&ByName](const TCHAR* Name) -> UTextBlock*
	{
		UTextBlock* T = Cast<UTextBlock>(ByName(Name));
		// Однострочный UI: autowrap в CanvasPanel ломает раскладку (закон `ui-autowrap-canvas-gotcha`).
		if (T) { T->SetAutoWrapText(false); }
		return T;
	};

	// ── Переключатель вкладок 69502: три кнопки-сегмента и три подписи ──
	OptTabBtn[0] = Cast<UButton>(ByName(TEXT("SYSTEM_Option_NEW_Radio_btn_OPT0")));
	OptTabBtn[1] = Cast<UButton>(ByName(TEXT("SYSTEM_Option_NEW_Radio_btn_OPT1")));
	OptTabBtn[2] = Cast<UButton>(ByName(TEXT("SYSTEM_Option_NEW_Radio_btn_OPT2")));
	OptTabText[0] = TxtByName(TEXT("SYSTEM_Option_NEW_Radio_btn_Text"));
	OptTabText[1] = TxtByName(TEXT("SYSTEM_Option_NEW_Radio_btn_Text2"));
	OptTabText[2] = TxtByName(TEXT("SYSTEM_Option_NEW_Radio_btn_Text3"));

	// ── Корни вкладок: 0 = ИНТЕРФЕЙС 70150, 1 = СИСТЕМА 70000, 2 = КЛАВИШИ 69510 ──
	OptTabRoot[0] = ByName(TEXT("SYSTEM_Op_INT_grp"));
	OptTabRoot[1] = ByName(TEXT("SYSTEM_Op_SYSTEM_grp"));
	OptTabRoot[2] = ByName(TEXT("SYSTEM_Op_HOTKEYS_grp"));

	// ── Под-панели ──
	OptSubPage[PageSlot(0, 0)] = ByName(TEXT("SYSTEM_Op_INT_1_grp"));
	OptSubPage[PageSlot(0, 1)] = ByName(TEXT("SYSTEM_Op_INT_2_grp"));
	OptSubPage[PageSlot(0, 2)] = ByName(TEXT("SYSTEM_Op_INT_3_grp"));
	OptSubPage[PageSlot(1, 0)] = ByName(TEXT("SYSTEM_Op_SYSTEM_souund_grp"));
	OptSubPage[PageSlot(1, 1)] = ByName(TEXT("SYSTEM_Op_SYSTEM_screen_grp"));
	OptSubPage[PageSlot(1, 2)] = ByName(TEXT("SYSTEM_Op_SYSTEM_3d_OP_grp"));
	// ★ Фаза 2 (решение владельца №4): две новые под-страницы «Настроек системы» — клоны панели 3D в ассете.
	OptSubPage[PageSlot(1, 3)] = ByName(TEXT("SYSTEM_Op_SYSTEM_quality_grp"));
	OptSubPage[PageSlot(1, 4)] = ByName(TEXT("SYSTEM_Op_SYSTEM_effects_grp"));
	OptSubPage[PageSlot(2, 0)] = ByName(TEXT("SYSTEM_Op_HOTKEYS_INTER_grp"));
	OptSubPage[PageSlot(2, 1)] = ByName(TEXT("SYSTEM_Op_HOTKEYS_PARTY_grp"));
	OptSubPage[PageSlot(2, 2)] = ByName(TEXT("SYSTEM_Op_HOTKEYS_CHAT_grp"));
	OptSubPage[PageSlot(2, 3)] = ByName(TEXT("SYSTEM_Op_HOTKEYS_FEEL_grp"));
	for (int32 p = 0; p < 4; ++p) { HotkeyPageHost[p] = OptSubPage[PageSlot(2, p)]; }

	// ★★★ 17.09.2026: МОСТ ВИДИМОСТИ СНЯТ. Раньше здесь по всем панелям и трём спискам под-страниц
	//   гонялся `RevealImportedPanel` — он снимал с виджетов `Collapsed`, прошитый им прежним
	//   импортёром (наследование скрытости вниз по дереву). Импортёр починен, а в ассете видимость
	//   узлов приведена к данным `.JUI` точечной записью (`Tools/asda_ui_sync_own_visibility.py`),
	//   поэтому лечить нечего: панель, показанная `ApplySubPageVisibility`, открывается с
	//   правильным содержимым сама. Группы списков (`*_DD_grp`) в данных видимы (`Visible = "1"`)
	//   и приезжают из ассета `SelfHitTestInvisible` — подпирать их тоже не нужно.
	//   Осталась ЕДИНСТВЕННАЯ правка видимости, и она из ЭТАЛОНА, а не из импорта, — две строки
	//   вкладки «Интерфейс-3», которые `uiDialogInitControls` показывает явно (см. ниже).
	RevealEtalonShownRows();

	// ── ★★★ СПИСКИ ПОД-СТРАНИЦ (`SYSTEM_Op_<X>_DD_grp`) — ячейки списка, а не «выпадающее меню» ──
	//   ЭТАЛОН. Контролы 70156 (Интерфейс), 70006 (Система), 69517 (Клавиши) со скином
	//   `TalkBar_system_D` (`JuiSkin.ini`: `Type = "RadioButton"`, один план 150×25) — это `CListBox`:
	//     `uiDialogInitControls`      (ui_core.c:88131-88143): ячейка 0 + 2 клона, loc 30400…30402;
	//     `uiTradeDialogInitControls` (ui_core.c:88208-88220): ячейка 0 + 2 клона, loc 30501…30503;
	//     `uiDlgInitControlsByID`     (ui_core.c:88344-88356): ячейка 0 + 3 клона, loc 30011…30014.
	//   Клон — `uiListAppendClonedCell@0x12F5C60`: копия предыдущей ячейки, сдвинутая на её высоту
	//   (25 px) вниз. Выбор строки приходит командой списка (`0x11176`/70006, 70156, 69517 →
	//   `uiTabWindowShowPage3` / `uiTabWindowShowPage` / `uiTabWindowSelectPage`), и те же функции
	//   отмечают строку `CControl_SetSelectedIndex`. ВСЕ строки видны ВСЕГДА — отсюда жалоба владельца
	//   «в левом списке только "Звук"»: порт принимал список за свёрнутое меню, показывал одну подпись
	//   и раскрывал строки по клику (а раскрывал он вовсе не ячейки, а четыре служебных контрола
	//   70001…70004 с `Visible = "0"`, которые эталон не показывает никогда).
	//   Ячейки 1…N−1 лежат в АССЕТЕ (шаг `Tools/asda_ui_option_list_cells.py`): план
	//   `SYSTEM_Op_<X>_DD_CELLnn_0`, кнопка `SYSTEM_Op_<X>_DD_SEL00_OPTn`, подпись `SYSTEM_Op_<X>_DD_CELLnn_txt`.
	//   Ячейка 0 — узлы импортёра (`TalkBar_system_D_<jui>_0`, `…_SEL00_OPT0`, `TalkBar_system_D_<jui>_txt`;
	//   поле `ID` у контрола пустое, поэтому план и подпись названы по СКИНУ).
	{
		const TCHAR* DdGroup[3]  = { TEXT("SYSTEM_Op_INT_DD_grp"), TEXT("SYSTEM_Op_SYSTEM_DD_grp"), TEXT("SYSTEM_Op_HOTKEYS_DD_grp") };
		const TCHAR* DdPrefix[3] = { TEXT("SYSTEM_Op_INT_DD"), TEXT("SYSTEM_Op_SYSTEM_DD"), TEXT("SYSTEM_Op_HOTKEYS_DD") };
		// ★ Фаза 2: у списка «Настроек системы» ячеек 5 (ячейки 3 и 4 — клоны того же шага ассета).
		const int32 DdCount[3]   = { PageCount(0), PageCount(1), PageCount(2) };
		for (int32 t = 0; t < 3; ++t)
		{
			// Ячейка 0 — узлы, названные импортёром по скину: ищем среди детей группы.
			if (UPanelWidget* G = Cast<UPanelWidget>(ByName(DdGroup[t])))
			{
				for (int32 c = 0; c < G->GetChildrenCount(); ++c)
				{
					UWidget* Kid = G->GetChildAt(c);
					if (!Kid) { continue; }
					const FString N = Kid->GetName();
					if (!N.StartsWith(TEXT("TalkBar_system_D"))) { continue; }
					if (N.EndsWith(TEXT("_txt"))) { OptListText[PageSlot(t, 0)] = Cast<UTextBlock>(Kid); }
					else if (N.EndsWith(TEXT("_0"))) { OptListPlane[PageSlot(t, 0)] = Cast<UImage>(Kid); }
				}
			}
			for (int32 i = 0; i < DdCount[t]; ++i)
			{
				const int32 k = PageSlot(t, i);
				OptListBtn[k] = Cast<UButton>(ByName(*FString::Printf(TEXT("%s_SEL00_OPT%d"), DdPrefix[t], i)));
				if (i > 0)
				{
					OptListPlane[k] = Cast<UImage>(ByName(*FString::Printf(TEXT("%s_CELL%02d_0"), DdPrefix[t], i)));
					OptListText[k]  = Cast<UTextBlock>(ByName(*FString::Printf(TEXT("%s_CELL%02d_txt"), DdPrefix[t], i)));
				}
				if (OptListText[k]) { OptListText[k]->SetAutoWrapText(false); }
				// Кадр покоя — как лежит в ассете; кадр «выбрано» — кадр 2 скина, его импортёр кладёт
				//   кнопке ячейки как `Pressed` (`AddOptionButton(…, кадр 1, кадр 2)`).
				bOptListBrushValid[k] = false;
				if (OptListPlane[k] && OptListBtn[k])
				{
					OptListBrushIdle[k] = OptListPlane[k]->GetBrush();
					OptListBrushSel[k] = OptListBtn[k]->GetStyle().Pressed;
					// Размер кисти — от плана: у кнопки своя кисть может быть другого размера.
					OptListBrushSel[k].ImageSize = OptListBrushIdle[k].ImageSize;
					bOptListBrushValid[k] = (OptListBrushSel[k].GetResourceObject() != nullptr);
				}
				if (!OptListPlane[k] || !OptListBtn[k] || !OptListText[k])
				{
					UE_LOG(LogAsdaSysOpt, Warning,
						TEXT("[настройки] список под-страниц %s: ячейка %d неполная (план=%d кнопка=%d подпись=%d) — ")
						TEXT("не прогнан шаг ассета asda_ui_option_list_cells?"),
						DdPrefix[t], i, OptListPlane[k] != nullptr, OptListBtn[k] != nullptr, OptListText[k] != nullptr);
				}
			}
		}
	}

	// Зона колеса и обрезки страницы клавиш (69649, 484×346 — геометрия из `.JUI`, клиппинг — в ассете).
	HotkeyViewport = ByName(TEXT("SYSTEM_Op_HOTKEYS_Scroll_grp"));

	// ── Кнопки окна ──
	OptBtnApply    = Cast<UButton>(ByName(TEXT("SYSTEM_Option_APP")));
	OptBtnOk       = Cast<UButton>(ByName(TEXT("SYSTEM_Option_OK")));
	OptBtnCancel   = Cast<UButton>(ByName(TEXT("SYSTEM_Option_NO")));
	OptBtnClose    = Cast<UButton>(ByName(TEXT("SYSTEM_Option_NEW_XX")));
	OptBtnDefault  = Cast<UButton>(ByName(TEXT("SYSTEM_default")));
	OptBtnClearKey = Cast<UButton>(ByName(TEXT("SYSTEM_cancel")));
	OptBtnApplyText    = TxtByName(TEXT("SYSTEM_Option_APP_Text"));
	OptBtnOkText       = TxtByName(TEXT("SYSTEM_Option_OK_Text"));
	OptBtnCancelText   = TxtByName(TEXT("SYSTEM_Option_NO_Text"));
	OptBtnDefaultText  = TxtByName(TEXT("SYSTEM_default_Text"));
	OptBtnClearKeyText = TxtByName(TEXT("SYSTEM_cancel_Text"));

	// ── Оверлей «нажми клавишу» 69910 (`SYSTEM_CLICK_<jui>_0…_7`) ──
	//   Эталон прячет его в `uiDlgInitControlsByID` (`CControl_SetVisible(filler[356], 0)`, ui_core.c:88315).
	OptWidget->WidgetTree->ForEachWidget([&](UWidget* Node)
	{
		if (UImage* Img = Cast<UImage>(Node))
		{
			if (Img->GetName().StartsWith(TEXT("SYSTEM_CLICK"))) { ClickOverlay.Add(Img); }
		}
	});
	// Порядок планов рамки — по имени (`…_0` … `…_7`), чтобы смещение накладывалось предсказуемо.
	ClickOverlay.Sort([](const UImage& A, const UImage& B)
	{
		return A.GetName() < B.GetName();
	});
	ClickOverlayBase.Reset();
	for (const TObjectPtr<UImage>& Img : ClickOverlay)
	{
		FVector2D P, S;
		ClickOverlayBase.Add(AsdaUi::SlotRect(Img, P, S) ? P : FVector2D::ZeroVector);
		Img->SetVisibility(ESlateVisibility::Collapsed);
	}

	// ── 84 строки привязок: контейнеры `PICK_BACK_grp`, `PICK_BACK_grp_1` … `PICK_BACK_grp_83` ──
	//   Порядок контейнеров в дереве = порядок кодов действий 1…84 (таблица
	//   `fontMapCodepointToGlyphIndex@0xED6160`, проверено сверкой страниц с их размерами:
	//   INTER 5+12+12+18 = 47, PARTY 3+15 = 18, CHAT 8, FEEL 11 — ровно 84).
	{
		TMap<int32, UWidget*> RowByIndex;
		OptWidget->WidgetTree->ForEachWidget([&RowByIndex](UWidget* Node)
		{
			if (!Node) { return; }
			const FString N = Node->GetName();
			if (!N.StartsWith(TEXT("PICK_BACK_grp"))) { return; }
			const FString Tail = N.Mid(13);   // «» либо «_17»
			int32 Idx = 0;
			if (!Tail.IsEmpty())
			{
				if (!Tail.StartsWith(TEXT("_"))) { return; }
				Idx = FCString::Atoi(*Tail.Mid(1));
			}
			RowByIndex.Add(Idx, Node);
		});
		HotkeyRows.Reset();
		// ★ Фаза 2 [ИСПРАВЛЕНИЕ ОРИГИНАЛА]: 85 строк. Действие 85 «Стойка на руках» (EMOTION UPPERARM, loc 30232)
		//   в эталонном окне поля не имеет (`uiDlgInitControlsByID`: у страницы FEEL 11 полей 69891…69901 на
		//   действия 74…84), хотя раскладка его знает и назначить клавишу было нечем. Поле — клон
		//   `PICK_BACK_grp_83` на 19 px ниже (`PICK_BACK_grp_84`, шаг ассета `asda_ui_option_extra_rows.py`).
		for (int32 Idx = 0; Idx < 85; ++Idx)
		{
			UWidget** Found = RowByIndex.Find(Idx);
			if (!Found || !*Found) { continue; }
			FAsdaOptHotkeyRow Row;
			Row.ActionId = Idx + 1;
			Row.RowGroup = *Found;
			// Страница определяется предком-контейнером (INTER / PARTY / CHAT / FEEL).
			Row.Page = 0;
			for (UWidget* P = *Found; P; P = P->GetParent())
			{
				const FString PN = P->GetName();
				if (PN.StartsWith(TEXT("SYSTEM_Op_HOTKEYS_PARTY"))) { Row.Page = 1; break; }
				if (PN.StartsWith(TEXT("SYSTEM_Op_HOTKEYS_CHAT")))  { Row.Page = 2; break; }
				if (PN.StartsWith(TEXT("SYSTEM_Op_HOTKEYS_FEEL")))  { Row.Page = 3; break; }
				if (PN.StartsWith(TEXT("SYSTEM_Op_HOTKEYS_INTER"))) { Row.Page = 0; break; }
			}
			if (UPanelWidget* RowPanel = Cast<UPanelWidget>(*Found))
			{
				for (int32 c = 0; c < RowPanel->GetChildrenCount(); ++c)
				{
					UTextBlock* T = Cast<UTextBlock>(RowPanel->GetChildAt(c));
					if (!T) { continue; }
					T->SetAutoWrapText(false);
					// `PICK_BACK_<jui>_txt` — поле клавиши; вторая подпись строки — имя действия.
					if (T->GetName().StartsWith(TEXT("PICK_BACK_")) && T->GetName().EndsWith(TEXT("_txt")))
					{
						Row.KeyText = T;
					}
					else
					{
						Row.NameText = T;
					}
				}
			}
			HotkeyRows.Add(Row);
		}
		// Диапазон прокрутки страницы 0 — эталонный `uiControlSetScrollRange(…, 1, 17)`
		//   (ui_core.c:89042); у остальных страниц диапазон 1…1, то есть прокрутки нет.
		HotkeyScrollMax = 16;
		HotkeyScroll = 0;
		HotkeyPageBase.Reset();
		for (int32 p = 0; p < 4; ++p)
		{
			FVector2D P, S;
			HotkeyPageBase.Add((HotkeyPageHost[p] && AsdaUi::SlotRect(HotkeyPageHost[p], P, S)) ? P : FVector2D::ZeroVector);
		}
	}

	// ── Чекбоксы и их подписи ──
	for (FAsdaOptCheck& C : Checks)
	{
		C.Box = Cast<UCheckBox>(ByName(*C.WidgetName.ToString()));
		C.Label = TxtByName(*C.LabelName.ToString());
		if (C.Label)
		{
			if (C.LocId > 0)
			{
				const FString S = LocUi(C.LocId);   // ★ фаза 2: с исправлениями перевода (наши 990xxx)
				if (!S.IsEmpty()) { C.Label->SetText(FText::FromString(S)); }
			}
			else
			{
				// Дефект Д3: подписи эталон не ставит ⇒ строка игроку бессмысленна. Гасим ВСЮ строку
				//   (контейнер `Box_Check_grp*`), а не только текст — иначе останется голая галочка.
				if (UWidget* Group = C.Box ? C.Box->GetParent() : nullptr)
				{
					Group->SetVisibility(ESlateVisibility::Collapsed);
				}
			}
		}
		if (!C.Box)
		{
			UE_LOG(LogAsdaSysOpt, Warning, TEXT("[настройки] чекбокс %d (%s) в WBP не найден"),
				C.IdNum, *C.WidgetName.ToString());
			continue;
		}
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = C.IdNum;
		R->CheckCallback = [this](int32 IdNum, bool bOn) { OnCheckToggled(IdNum, bOn); };
		C.Box->OnCheckStateChanged.Clear();
		C.Box->OnCheckStateChanged.AddDynamic(R, &UAsdaButtonRelay::OnCheckChanged);
		AsdaUi::AnchorRelay(Relays, R, C.Box);   // ★ 31.10.2026: подписка уже сделана — якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}
	// ★★★ РЕВИЗИЯ 2026-09-17 (М4): ЗДЕСЬ БЫЛО РАНТАЙМ-ГАШЕНИЕ ТРЁХ «ЛИШНИХ» ЧЕКБОКСОВ
	//   (`SYSTEM_Op_INT_3_CHK10`, `SYSTEM_Op_INT_2_CHK06`, `..._CHK07`) — снято.
	//   Обоснование было «обработчика нет», то есть не из данных; ключа отката у правки не было.
	//   Настоящее основание лежит В ДАННЫХ: у всех трёх контролов (70382, 70274, 70276) в
	//   `Data/Decoded/jui/gamemain_jui.ini` стоит `Visible = "0"`, а эталон их не показывает —
	//   `uiDialogInitControls` явно зовёт `CControl_SetVisible(…, 1u)` только для 70361…70380
	//   (`RE/etalon/ui_core.c:88176-88177`, цикл `k < 10`), одиннадцатая строка в него не входит,
	//   а панель «Интерфейс-2» подписывает лишь три строки (`j < 3`, там же:88145-88157).
	//   ★ 17.09.2026: теперь эти строки свёрнуты САМИМ АССЕТОМ — после починки импортёра у
	//   узлов 70382/70274/70276 в ассете лежит их собственный `Visible = "0"` из данных, и
	//   держать ради этого рантайм-таблицу (снятый мост `RevealImportedPanel`) больше не нужно.

	// ── Подписи, которые эталон ставит у НЕ-чекбоксовых строк ──
	//   Экран: TXT01 = «Яркость» (loc 30543), TXT02 = «Разрешение» (loc 30541).
	if (UTextBlock* T = TxtByName(TEXT("SYSTEM_Op_SYSTEM_SYS22_TXT01")))
	{
		const FString S = LocUi(SysOptLoc::kScrFirst + 2); if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); }
	}
	if (UTextBlock* T = TxtByName(TEXT("SYSTEM_Op_SYSTEM_SYS22_TXT02")))
	{
		const FString S = LocUi(SysOptLoc::kScrFirst + 0); if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); }
	}
	//   3D: подпись комбо 70119 → loc 30570 (ui_core.c:88274-88278); ★ фаза 2: наша строка «Свечение».
	if (UTextBlock* T = TxtByName(TEXT("SYSTEM_O3_SYSTEM_SYS33_TXT09")))
	{
		const FString S = LocUi(SysOptLoc::k3dFlare); if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); }
	}

	// ── Полосы-ползунки (скин `SideBar_Scr`, четыре плана) ──
	//   Эталон: громкость музыки 70013 и эффектов 70016 — диапазон 0…256 (`uiScrollbarSetRange`,
	//   ui_core.c:88226); яркость 70058 — диапазон 1…100 (ui_core.c:88243).
	{
		auto CollectSlider = [&](const TCHAR* HostName, int32 Nth, int32 IdNum, int32 Min, int32 Max) -> void
		{
			UWidget* Host = ByName(HostName);
			if (!Host) { return; }
			// Собираем планы `SideBar_Scr_<jui>_<plane>` под этим поддеревом, группируя по <jui>.
			TMap<int32, TArray<UImage*>> ByJui;
			TArray<UWidget*> Stack; Stack.Add(Host);
			while (Stack.Num() > 0)
			{
				UWidget* Cur = Stack.Pop();
				if (UPanelWidget* P = Cast<UPanelWidget>(Cur))
				{
					for (int32 c = 0; c < P->GetChildrenCount(); ++c) { if (UWidget* K = P->GetChildAt(c)) { Stack.Add(K); } }
				}
				UImage* Img = Cast<UImage>(Cur);
				if (!Img) { continue; }
				FString NodeIdc; int32 Jui = 0, Plane = 0;
				if (!AsdaUi::ParsePlaneName(Img->GetName(), NodeIdc, Jui, Plane)) { continue; }
				if (NodeIdc != TEXT("SideBar_Scr")) { continue; }
				TArray<UImage*>& Arr = ByJui.FindOrAdd(Jui);
				if (Arr.Num() <= Plane) { Arr.SetNum(Plane + 1); }
				Arr[Plane] = Img;
			}
			TArray<int32> Juis; ByJui.GetKeys(Juis); Juis.Sort();
			if (!Juis.IsValidIndex(Nth)) { return; }
			const TArray<UImage*>& Planes = ByJui[Juis[Nth]];
			FAsdaOptSlider S;
			S.IdNum = IdNum; S.Min = Min; S.Max = Max; S.Value = Min;
			S.Track    = Planes.IsValidIndex(0) ? Planes[0] : nullptr;
			S.BtnLeft  = Planes.IsValidIndex(1) ? Planes[1] : nullptr;
			S.BtnRight = Planes.IsValidIndex(2) ? Planes[2] : nullptr;
			S.Thumb    = Planes.IsValidIndex(3) ? Planes[3] : nullptr;
			FVector2D P, Sz;
			if (S.Track && AsdaUi::SlotRect(S.Track, P, Sz)) { S.TrackPos = P; S.TrackSize = Sz; }
			if (S.Thumb && AsdaUi::SlotRect(S.Thumb, P, Sz)) { S.ThumbSize = Sz; }
			// Видимость планов полосы отдельно НЕ трогаем: в `.JUI` у контролов 70013/70016/70058
			//   стоит `Visible = "1"`, и после починки импортёра (17.09.2026) ассет приезжает с
			//   этим флагом сам — подпирать его рантаймом больше не нужно и нельзя.
			Sliders.Add(S);
		};
		CollectSlider(TEXT("SYSTEM_Op_SYSTEM_souund_grp"), 0, 70013, 0, 256);   // громкость музыки
		CollectSlider(TEXT("SYSTEM_Op_SYSTEM_souund_grp"), 1, 70016, 0, 256);   // громкость эффектов
		if (Sliders.Num() > 0) { Sliders.Last().Tab = 1; Sliders.Last().Page = 0; }
		// ★ У ЯРКОСТИ ЭТАЛОН АДРЕСУЕТ КОНТЕЙНЕР, А НЕ ПОЛЗУНОК. Сам контрол полосы — **70058**
		//   (`pThis[66]`, `uiScrollbarSetRange(…, 1, 100)`, ui_core.c:88243), а значение приходит в
		//   `uiOptionsWindow_WndProc` веткой `case 0x111A9` = **70057**, то есть под id РОДИТЕЛЬСКОГО
		//   контейнера строки (у звука, наоборот, приходят id самих полос 70013/70016). Порт не
		//   роутит по idnum — он держит объект полосы напрямую, — поэтому расхождение на поведение
		//   не влияет; число 70058 здесь работает как локальный ключ полосы.
		CollectSlider(TEXT("SYSTEM_Op_SYSTEM_screen_grp"), 0, 70058, 1, 100);   // яркость
		for (FAsdaOptSlider& S : Sliders) { S.Tab = 1; S.Page = (S.IdNum == 70058) ? 1 : 0; }
		// ★ Фаза 2: «Дальность прорисовки» страницы «Качество» — клон полосы «Яркости», 50…200 %
		//   (множитель asda.Cull.Scale 0.5…2.0 — диапазон ползунка dev-окна Ctrl+N).
		const int32 Before = Sliders.Num();
		CollectSlider(*SysOptExtra::NodeName(TEXT("Q9"), TEXT("grp")), 0, SysOptExtra::kIdBase + SysOptExtra::Q9_Draw, 50, 200);
		if (Sliders.Num() > Before) { Sliders.Last().Tab = 1; Sliders.Last().Page = 3; }
		// ★ 19.10.2026: «Чувствительность мыши» (I2) и «…поворота камеры» (I3) страницы «Разное» — проценты 10…360 (★ v2: диапазон ×2 в обе стороны; полоса вдвое шире).
		for (const int32 Key : { (int32)SysOptExtra::I2_Cursor, (int32)SysOptExtra::I3_Camera })
		{
			const int32 N0 = Sliders.Num();
			CollectSlider(*SysOptExtra::NodeName(Key == SysOptExtra::I2_Cursor ? TEXT("I2") : TEXT("I3"), TEXT("grp")), 0,
				SysOptExtra::kIdBase + Key, UAsdaGameUserSettings::kMouseSensMin, UAsdaGameUserSettings::kMouseSensMax);
			if (Sliders.Num() > N0) { Sliders.Last().Tab = 0; Sliders.Last().Page = 2; }
		}
	}

	// ── Комбо: разрешение 70053 и «эффекты мерцания» 70121 ──
	//   У обоих контролов поле `ID` в .JUI ПУСТОЕ, поэтому планы и подпись импортёр назвал по
	//   СКИНУ (`Bk_ComboBox_<jui>_0/_1/_txt`) — для этого у `UAsdaJuiComboBox::Setup` есть
	//   отдельный аргумент имени планов.
	{
		auto PlaneIdcOf = [](UWidget* Combo) -> FString { return SysOptPlaneIdcOf(Combo); };

		UWidget* ResCmb = ByName(TEXT("SYSTEM_Op_SYSTEM_SYS22_CMB00"));
		if (ResCmb)
		{
			ResolutionCombo = NewObject<UAsdaJuiComboBox>(this);
			if (ResolutionCombo->Setup(OptWidget, TEXT("SYSTEM_Op_SYSTEM_SYS22_CMB00"), kOptionWindowId,
				/*bOpenUp*/ false, PlaneIdcOf(ResCmb)))
			{
				ResolutionList.Reset();
				TArray<FString> Labels;
				if (UAsdaVideoSettingsSubsystem* Vid = GetGameInstance()
					? GetGameInstance()->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr)
				{
					ResolutionList = Vid->GetAvailableResolutions();
				}
				if (ResolutionList.Num() == 0)
				{
					// Разрешений от адаптера нет (headless/NullRHI) — оставляем эталонное умолчание
					//   `config.txt RESOLUTION 1024 768 32` (RESOLUTION_SYSTEM_RE.md), а не пустой список.
					ResolutionList.Add(UAsdaVideoSettingsSubsystem::GetOriginalDefaultResolution());
				}
				for (const FIntPoint& R : ResolutionList)
				{
					Labels.Add(FString::Printf(TEXT("%d x %d"), R.X, R.Y));
				}
				ResolutionCombo->SetItems(Labels);
				ResolutionCombo->OnPicked.BindLambda([this](int32 Idx)
				{
					if (!ResolutionList.IsValidIndex(Idx)) { return; }
					State.ScreenW = ResolutionList[Idx].X;
					State.ScreenH = ResolutionList[Idx].Y;
					UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] выбрано разрешение %dx%d (применится по «Применить»/«ОК»)"),
						State.ScreenW, State.ScreenH);
				});
			}
			else
			{
				ResolutionCombo = nullptr;
			}
		}

		UWidget* FlareCmb = ByName(TEXT("SYSTEM_O3_SYSTEM_SYS33_CMB00"));
		if (FlareCmb)
		{
			FlareCombo = NewObject<UAsdaJuiComboBox>(this);
			if (FlareCombo->Setup(OptWidget, TEXT("SYSTEM_O3_SYSTEM_SYS33_CMB00"), kOptionWindowId,
				/*bOpenUp*/ true, PlaneIdcOf(FlareCmb)))
			{
				// Пять пунктов loc 12190…12194 — ровно как в эталоне (ui_core.c:88281-88286): «Нет / Персонаж /
				//   Монстр / Персонаж+монстр / Все» (решение владельца №1 — пункты как в оригинале; смысл по
				//   эталону разобран у `UAsdaVideoSettingsSubsystem::SetGlowLevel`).
				TArray<FString> Labels;
				for (int32 i = 0; i < 5; ++i) { Labels.Add(Loc(SysOptLoc::kFlareFirst + i)); }
				FlareCombo->SetItems(Labels);
				FlareCombo->OnPicked.BindLambda([this](int32 Idx)
				{
					// ★ Фаза 2: пункт применяется по «ОК/Применить» (эталон: `optApplySettingsFromConfig`,
					//   game_main.c:12015-12020 → `effectQuad_SetTargetCountAndBuild`) через ОБЩИЙ с dev-окном
					//   `UAsdaVideoSettingsSubsystem::SetGlowLevel` — прежде значение только сохранялось.
					State.GlowLevel = FMath::Clamp(Idx, 0, 4);
				});
			}
			else
			{
				FlareCombo = nullptr;
			}
		}
	}

	// ── Подписи вкладок, кнопок и строк списков ──
	const int32 TabLoc[3] = { SysOptLoc::kTab0, SysOptLoc::kTab1, SysOptLoc::kTab2 };
	for (int32 t = 0; t < 3; ++t)
	{
		if (OptTabText[t])
		{
			const FString S = Loc(TabLoc[t]);
			if (!S.IsEmpty()) { OptTabText[t]->SetText(FText::FromString(S)); }
		}
	}
	auto SetBtnText = [this](UTextBlock* T, int32 LocId)
	{
		if (!T) { return; }
		const FString S = Loc(LocId);
		if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); }
	};
	SetBtnText(OptBtnApplyText,    SysOptLoc::kBtnApply);
	SetBtnText(OptBtnOkText,       SysOptLoc::kBtnOk);
	SetBtnText(OptBtnCancelText,   SysOptLoc::kBtnCancel);
	SetBtnText(OptBtnDefaultText,  SysOptLoc::kBtnDefault);
	// ★ Фаза 2: у 69652 своя строка «Снять клавишу» (исправление: совпадала с «Отмена» окна).
	if (OptBtnClearKeyText)
	{
		const FString S = LocUi(SysOptLoc::kBtnClearKey);
		if (!S.IsEmpty()) { OptBtnClearKeyText->SetText(FText::FromString(S)); }
	}
	RefreshListCells();

	// ── Привязка кнопок ──
	auto BindBtn = [this](UButton* B, int32 Code, TFunction<void(int32)> Fn)
	{
		if (!B) { return; }
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = Code;
		R->Callback = Fn;
		B->OnClicked.Clear();
		B->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, B);   // ★ 31.10.2026: подписка уже сделана — якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	};
	for (int32 t = 0; t < 3; ++t)
	{
		BindBtn(OptTabBtn[t], t, [this](int32 Tab) { SelectTab(Tab); });
		// Ячейки списка под-страниц: клик = выбор строки (эталонная команда списка → `…ShowPage*`).
		for (int32 i = 0; i < PageCount(t); ++i)
		{
			BindBtn(OptListBtn[PageSlot(t, i)], PageSlot(t, i), [this](int32 Slot)
			{
				SelectSubPage(Slot / kMaxPages, Slot % kMaxPages);
			});
		}
	}
	// Эталон: 0x11329 → «Применить»; 0x1132A → «ОК»; 70443/70599 → «Отмена»/крестик (одна ветка).
	BindBtn(OptBtnApply,    0, [this](int32) { ApplyNow(); });
	BindBtn(OptBtnOk,       0, [this](int32) { CloseOptions(true); });
	BindBtn(OptBtnCancel,   0, [this](int32) { CloseOptions(false); });
	BindBtn(OptBtnClose,    0, [this](int32) { CloseOptions(false); });
	BindBtn(OptBtnDefault,  0, [this](int32) { LoadKeybindDefaults(); });
	BindBtn(OptBtnClearKey, 0, [this](int32) { ClearSelectedBind(); });

	// ── ★★★ ПОДПИСИ ДЕЙСТВИЙ НА СТРАНИЦАХ КЛАВИШ — ПО ЭТАЛОНУ (фаза 2, 19.09.2026) ──
	//   Эталон берёт их из loc-таблицы блоками (`uiDlgInitControlsByID`, ui_core.c:88359-88480):
	//   30040+ / 30060+ / 30260+ / 30090+ / 30120+ / 30140+ / 30180+ / 30220+ — у каждой группы первая строка
	//   блока — ЗАГОЛОВОК, дальше по одной на поле привязки, в порядке кодов действий (`HotkeyActionLocId`).
	//   До фазы 2 порт ставил сюда `UAsdaKeybindConfig::GetCmdName` — это КЛЮЧИ файла `Shortcutkey.ini`
	//   («INTERFACE CHARACTOER», «BAR SLOT 1», «SCREEN SHOT»), английские и с опечатками самого бинаря,
	//   а заголовков групп не ставил вовсе (была видна запечённая в ассет машинная подпись). `GetCmdName`
	//   остаётся для журналов. Дефекты русского перевода самих строк правит `LocUi` (наши 990xxx).
	for (const FAsdaOptHotkeyRow& Row : HotkeyRows)
	{
		if (!Row.NameText) { continue; }
		const FString Name = LocUi(HotkeyActionLocId(Row.ActionId));
		if (!Name.IsEmpty()) { Row.NameText->SetText(FText::FromString(Name)); }
	}
	// Заголовки восьми групп (первая подпись каждого блока). Узлы — подписи-заголовки групп `NoImage_grp*`
	//   страниц INTER/PARTY/CHAT/FEEL (сверено headless-дампом: у всех восьми жёлтый цвет заголовка JUI).
	{
		struct FHead { const TCHAR* Node; int32 Loc; };
		static const FHead kHeads[] = {
			{ TEXT("SYSTEM_Op_Hk_1_TXT00"),          30040 },   // 69520 — страницы панели (действия 1…5)
			{ TEXT("SYSTEM_Op_Hk_2_TXT00"),          30060 },   // 69540 — ячейки основной панели (6…17)
			{ TEXT("SYSTEM_Op_Hk_2_TXT13"),          30260 },   // 69554 — ячейки верхней панели (18…29)
			{ TEXT("SYSTEM_Op_Hk_3_TXT00"),          30090 },   // 69600 — открыть/закрыть окно (30…47)
			{ TEXT("SYSTEM_Op_HOTKEYS_PARTY_TXT00"), 30120 },   // 69660 — отряд (48…50)
			{ TEXT("SYSTEM_Op_HOTKEYS_PARTY_TXT04"), 30140 },   // 69680 — основные действия (51…65)
			{ TEXT("SYSTEM_Op_HOTKEYS_CHAT_TXT00"),  30180 },   // 69760 — чат (66…73)
			{ TEXT("SYSTEM_Op_HOTKEYS_FEEL_TXT00"),  30220 },   // 69860 — эмоции (74…85)
		};
		for (const FHead& Hd : kHeads)
		{
			UTextBlock* T = TxtByName(Hd.Node);
			if (!T)
			{
				UE_LOG(LogAsdaSysOpt, Warning, TEXT("[клавиши] заголовка группы «%s» нет в окне"), Hd.Node);
				continue;
			}
			const FString S = LocUi(Hd.Loc);
			if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); }
		}
	}

	// ── ★ Фаза 2: новые строки «Экрана», страницы «Качество» и «Эффекты» ──
	SetupExtraControls();

	ApplyTabVisibility();
	RefreshAllHotkeyLabels();
	ApplyStateToControls();
}

// ═══════════════════════════ ОКНО НАСТРОЕК: ПОКАЗ И ЗАКРЫТИЕ ═══════════════════════════════════
bool UAsdaSystemOptionSubsystem::IsOptionsShown() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && WM->IsWindowVisible(kOptionWindowId);
}

void UAsdaSystemOptionSubsystem::ShowOptions()
{
	if (!EnsureOptionWidget()) { return; }
	UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();

	// Порт `keybindPageShowAndRefresh@0xED5790`:
	//   1) снять флаг «нужен перезапуск» (+1228);
	//   2) qmemcpy(+1158 ← +1104, 0x36) — СНИМОК буфера настроек для «Отмены»;
	//   3) показать окно и перерисовать подписи клавиш.
	bNeedRestartNotice = false;
	bPresetWarned = false;
	LoadStateFromGame();
	Snapshot = State;
	// ★ Фаза 2: состав списков зависит от текущих значений (пункт «Ультра» у групп на уровне 4, «Своё» у
	//   пресета при группах вразнобой, нестандартное значение резкости) — собираем при открытии.
	for (int32 i = 0; i < 8; ++i) { OpenGroupLevel[i] = State.Groups[i]; }
	OpenSingleLevel = UAsdaVideoSettingsSubsystem::SingleLevelIgnoringResolution(Scalability::GetQualityLevels());
	RebuildExtraComboItems();
	WM->SetWindowVisible(kOptionWindowId, true);
	ApplyStateToControls();
	RefreshAllHotkeyLabels();
	UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] окно открыто, снимок настроек снят"));
}

void UAsdaSystemOptionSubsystem::CloseOptions(bool bApply)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	if (!WM->IsWindowVisible(kOptionWindowId)) { return; }

	// ★ ПОРЯДОК ЭТАЛОНА ВАЖЕН (`optionsWnd_ApplyAndSave@0xED5800`): окно прячется ПЕРВЫМ, и только
	//   потом проверяется захват клавиши. Если захват идёт — функция выходит, ничего не применив
	//   и не откатив: «Отмена» тогда означает «отменить захват», а не «отменить настройки».
	WM->SetWindowVisible(kOptionWindowId, false);
	CloseAllCombos();
	if (bCapturing)
	{
		CancelKeyCapture();
		UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] шёл захват клавиши — окно закрыто, настройки не тронуты"));
		return;
	}

	if (bApply)
	{
		// Эталонный ОК: сохранить ini клавиш, пересобрать подписи хотбара, применить настройки,
		//   записать конфиг (у оригинала — `system\config.txt`).
		if (UAsdaKeybindConfig* Keys = GetKeybinds())
		{
			Keys->SaveShortcutkeyIni(UAsdaKeybindConfig::ResolveShortcutkeyIniPath(Keys->Profile, /*bForWrite*/ true));
			Keys->SaveToInputSettings(/*bSaveToDisk*/ true);
		}
		if (UAsdaHotbarSubsystem* Hot = GI->GetSubsystem<UAsdaHotbarSubsystem>()) { Hot->RebuildKeyHints(); }
		ApplyStateToGame();
		SaveStateToConfig();
		UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] ОК: настройки применены и сохранены"));
	}
	else
	{
		// Эталонная ОТМЕНА: откат кейбиндов + восстановление снимка буфера + накат на контролы.
		if (UAsdaKeybindConfig* Keys = GetKeybinds()) { Keys->ReloadLayout(); }
		State = Snapshot;
		ApplyStateToControls();
		// ★ РЕВИЗИЯ 2026-09-17 (С4): без пакета 4070 и без модалки «нужен перезапуск» — см. шапку
		//   `ApplyStateToGame`. Эталон в ветке отката к серверу не обращается вовсе.
		ApplyStateToGame(/*bUserConfirmed*/ false);
		RefreshAllHotkeyLabels();
		UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] Отмена: восстановлен снимок настроек и раскладка с диска"));
	}
}

void UAsdaSystemOptionSubsystem::EscCloseOptions()
{
	// Эталон: событие 21 окна 69500 → `optionsWnd_ApplyAndSave(this, 0)` — та же функция и тот же аргумент, что у «Отмены»
	//   70443 (`optWndDispatchCommand`). Захват клавиши `CloseOptions` разбирает сам (как эталон, по `+1096`).
	if (!IsOptionsShown()) { return; }
	UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] закрыто по ESC (событие 21 → optionsWnd_ApplyAndSave(…, 0))"));
	CloseOptions(/*bApply*/ false);
}

// Порт ветки 0x11329 (`SYSTEM_Option_APP` 70441): применить, окно НЕ закрывать.
void UAsdaSystemOptionSubsystem::ApplyNow()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaKeybindConfig* Keys = GetKeybinds())
	{
		const FString Path = UAsdaKeybindConfig::ResolveShortcutkeyIniPath(Keys->Profile, /*bForWrite*/ true);
		Keys->SaveShortcutkeyIni(Path);
		Keys->SaveToInputSettings(/*bSaveToDisk*/ true);
		// ★ Эталон пишет ini ДВАЖДЫ (до и после `optApplySettingsFromConfig`). Повторять второй
		//   вызов смысла нет: между ними раскладка не меняется, файл получился бы тот же байт в байт.
	}
	if (UAsdaHotbarSubsystem* Hot = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		Hot->RebuildKeyHints();   // эталон `hotkeyBar_RebuildAndRefresh@0xDF5BC0`
	}
	ApplyStateToGame();
	SaveStateToConfig();
	// ★ Фаза 2: после применения окно перечитывает ДЕЙСТВУЮЩИЕ значения (пресет раскладывает группы, 3D-флаги
	//   двигают уровни) — иначе следующее «Применить» сравнивало бы с устаревшим снимком.
	LoadStateFromGame();
	for (int32 i = 0; i < 8; ++i) { OpenGroupLevel[i] = State.Groups[i]; }
	OpenSingleLevel = UAsdaVideoSettingsSubsystem::SingleLevelIgnoringResolution(Scalability::GetQualityLevels());
	RebuildExtraComboItems();
	ApplyStateToControls();
	// ★ [ИСПРАВЛЕНИЕ ОРИГИНАЛА] Применённое и сохранённое становится новой точкой отката. У эталона
	//   снимок (+1158) снимается только при открытии окна, и «Применить → Отмена» возвращала окну
	//   значения ДО «Применить», хотя игра и файл уже жили новыми — окно и игра расходились.
	Snapshot = State;
	UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] Применить: настройки накатаны, окно оставлено открытым"));
}

// ═══════════════════════════ ВКЛАДКИ И ПОД-СТРАНИЦЫ ════════════════════════════════════════════
void UAsdaSystemOptionSubsystem::SelectTab(int32 Tab)
{
	CloseAllCombos();   // ★ фаза 2: раскрытый список чужой страницы гаснет вместе с ней
	CurrentTab = FMath::Clamp(Tab, 0, 2);
	ApplyTabVisibility();
	UE_LOG(LogAsdaSysOpt, Verbose, TEXT("[настройки] вкладка %d"), CurrentTab);
}

void UAsdaSystemOptionSubsystem::ApplyTabVisibility()
{
	// Порт `uiTabWindowSelectTab@0xED5910`: скрыть все три корня, показать выбранный,
	//   и внутри него показать текущую под-страницу.
	for (int32 t = 0; t < 3; ++t)
	{
		if (OptTabRoot[t])
		{
			OptTabRoot[t]->SetVisibility(t == CurrentTab
				? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}
	}
	ApplySubPageVisibility(CurrentTab);
	RefreshListCells();
}

void UAsdaSystemOptionSubsystem::SelectSubPage(int32 Tab, int32 Page)
{
	const int32 T = FMath::Clamp(Tab, 0, 2);
	CloseAllCombos();
	CurrentPage[T] = FMath::Clamp(Page, 0, PageCount(T) - 1);
	if (T == 2)
	{
		// Эталон `uiTabWindowSelectPage@0xED5BB0`: смена под-страницы сбрасывает выбор строки
		//   (`uiWindowResetSelection`) и переставляет диапазон прокрутки (1…17 у страницы 0, 1…1 у прочих).
		CancelKeyCapture();
		HotkeyScrollMax = (CurrentPage[2] == 0) ? 16 : 0;
		SetHotkeyScroll(0);
	}
	ApplySubPageVisibility(T);
	RefreshListCells();
}

void UAsdaSystemOptionSubsystem::ApplySubPageVisibility(int32 Tab)
{
	const int32 T = FMath::Clamp(Tab, 0, 2);
	const int32 Count = PageCount(T);
	for (int32 p = 0; p < Count; ++p)
	{
		if (OptSubPage[PageSlot(T, p)])
		{
			OptSubPage[PageSlot(T, p)]->SetVisibility(p == CurrentPage[T]
				? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}
	}
}

// ★★★ ЯЧЕЙКИ СПИСКА ПОД-СТРАНИЦ: подписи из loc и отметка выбранной строки.
//   Подписи — эталонные `CListBox_SetItemString(list, i, loc)` (первые loc: 30400 / 30501 / 30011).
//   Отметка выбранной — `CControl_SetSelectedIndex`: у скина `TalkBar_system_D` (тип `RadioButton`)
//   выбранная ячейка рисуется КАДРОМ 2 (`JuiSkin.ini`: UV 0.840…0.994 × 0.484…0.533, тон FFFFC088,
//   подпись FFFFFF80), остальные — кадром 0 (UV × 0.375…0.424, подпись FFFFF9AC). Это СОСТОЯНИЕ
//   (какая страница открыта), а не разметка, поэтому живёт в коде; сами ячейки — в ассете.
void UAsdaSystemOptionSubsystem::RefreshListCells()
{
	// Подписи ячеек: эталонные блоки loc (30400… / 30501… / 30011…) + ★ фаза 2: ячейки 4 и 5 «Настроек системы» —
	//   наши строки «Качество» / «Эффекты».
	auto CellLoc = [](int32 t, int32 i) -> int32
	{
		if (t == 1 && i == 3) { return SysOptLoc::kPageQuality; }
		if (t == 1 && i == 4) { return SysOptLoc::kPageEffects; }
		const int32 First[3] = { SysOptLoc::kIntPage0, SysOptLoc::kSysPage0, SysOptLoc::kHkPage0 };
		return First[t] + i;
	};
	const int32 Count[3] = { PageCount(0), PageCount(1), PageCount(2) };
	// Цвета подписи кадров 0 и 2 скина `TalkBar_system_D` (`CaptionColor` SS0 = FFFFF9AC, SS2 = FFFFFF80;
	//   `RE/docs/JuiSkin.ini`, запись скина). В ассете цвета лежат линейными — переводим так же.
	const FSlateColor IdleColor(FLinearColor::FromSRGBColor(FColor(0xFF, 0xF9, 0xAC, 0xFF)));
	const FSlateColor SelColor(FLinearColor::FromSRGBColor(FColor(0xFF, 0xFF, 0x80, 0xFF)));
	for (int32 t = 0; t < 3; ++t)
	{
		for (int32 i = 0; i < Count[t]; ++i)
		{
			const int32 k = PageSlot(t, i);
			const bool bSel = (i == CurrentPage[t]);
			if (OptListText[k])
			{
				const FString S = LocUi(CellLoc(t, i));
				if (!S.IsEmpty()) { OptListText[k]->SetText(FText::FromString(S)); }
				OptListText[k]->SetColorAndOpacity(bSel ? SelColor : IdleColor);
			}
			if (OptListPlane[k] && bOptListBrushValid[k])
			{
				OptListPlane[k]->SetBrush(bSel ? OptListBrushSel[k] : OptListBrushIdle[k]);
			}
		}
	}
}

// ═══════════════════════════ КАДРОВЫЙ РАЗБОР МЫШИ ══════════════════════════════════════════════
void UAsdaSystemOptionSubsystem::TickOptionInput(const FAsdaMouseFrame& M)
{
	// ── Комбобоксы (их кадр опрашивает владелец, второй опрос съел бы фронты) ──
	//   ★ Фаза 2: опрашиваются ТОЛЬКО списки видимой страницы — у свёрнутой панели кэш геометрии остаётся
	//   прежним, и невидимый список ловил бы клики по месту, где его уже нет.
	const bool bOnScreenPg = (CurrentTab == 1 && CurrentPage[1] == 1);
	const bool bOn3dPg     = (CurrentTab == 1 && CurrentPage[1] == 2);
	if (ResolutionCombo && bOnScreenPg) { ResolutionCombo->Tick(M); }
	if (FlareCombo && bOn3dPg)          { FlareCombo->Tick(M); }
	for (FAsdaOptExtraCombo& X : ExtraCombos)
	{
		if (X.Combo && CurrentTab == X.Tab && CurrentPage[X.Tab] == X.Page) { X.Combo->Tick(M); }
	}
	// Пока раскрыт список, под ним лежат полосы и строки — их кадр не разбираем (клик принадлежит списку).
	if (IsAnyComboOpen()) { return; }

	// ── Полосы-ползунки (только на своей под-странице — иначе они не видны) ──
	for (FAsdaOptSlider& S : Sliders)
	{
		if (CurrentTab != S.Tab || CurrentPage[S.Tab] != S.Page) { S.bDragging = false; continue; }
		TickSlider(S, M);
	}

	// ── Страница клавиш: клик по строке начинает/отменяет захват ──
	//   ★ 19.09.2026: только ВНУТРИ зоны 69649. Строки, уехавшие прокруткой за рамку, обрезаны ассетом
	//   и невидимы, но геометрически лежат на прежнем месте — без этой проверки клик по «ОК» или
	//   «По умолчанию» начинал захват клавиши у спрятанной под ними строки.
	const bool bInHotkeyView = !HotkeyViewport || AsdaUi::IsCursorOverWidget(HotkeyViewport);
	if (CurrentTab == 2 && M.bLmbDown && !M.bRmb && bInHotkeyView)
	{
		for (const FAsdaOptHotkeyRow& Row : HotkeyRows)
		{
			if (Row.Page != CurrentPage[2] || !Row.RowGroup) { continue; }
			if (!AsdaUi::IsCursorOverWidget(Row.RowGroup)) { continue; }
			BeginKeyCapture(Row.ActionId);
			break;
		}
	}
}

// ═══════════════════════════ ПОЛОСЫ-ПОЛЗУНКИ ═══════════════════════════════════════════════════
FAsdaOptSlider* UAsdaSystemOptionSubsystem::FindSlider(int32 IdNum)
{
	for (FAsdaOptSlider& S : Sliders) { if (S.IdNum == IdNum) { return &S; } }
	return nullptr;
}

void UAsdaSystemOptionSubsystem::LayoutSliderThumb(FAsdaOptSlider& S)
{
	if (!S.Thumb || S.TrackSize.X <= 0.f) { return; }
	const float Span = FMath::Max(S.TrackSize.X - S.ThumbSize.X, 1.f);
	const float T = (S.Max > S.Min) ? (float)(S.Value - S.Min) / (float)(S.Max - S.Min) : 0.f;
	FVector2D P, Sz;
	if (!AsdaUi::SlotRect(S.Thumb, P, Sz)) { return; }
	AsdaUi::SetSlotPos(S.Thumb, FVector2D(S.TrackPos.X + Span * FMath::Clamp(T, 0.f, 1.f), P.Y));
}

void UAsdaSystemOptionSubsystem::SetSliderValue(FAsdaOptSlider& S, int32 NewValue)
{
	S.Value = FMath::Clamp(NewValue, S.Min, S.Max);
	LayoutSliderThumb(S);
	// ★ ЭТАЛОН ПРИМЕНЯЕТ ГРОМКОСТЬ И ГАММУ СРАЗУ, ЕЩЁ ДО «ПРИМЕНИТЬ» (`uiOptionsWindow_WndProc`
	//   ветки 0x1117D / 0x11180 / 0x111A9): игрок обязан слышать и видеть результат, пока тянет
	//   полосу. В буфер значение тоже пишется — «Отмена» вернёт его из снимка.
	switch (S.IdNum)
	{
	// ★★★ 19.09.2026: громкость — в ПРОФИЛЬ игрока и сразу в звук (`ApplyAudioSettings`), а НЕ
	//   через `IConsoleVariable::Set(…, ECVF_SetByCode)`. Прежняя запись в ключ консоли молча
	//   отбрасывалась движком, если ключ хоть раз задавали из консоли (приоритет «консоль» выше
	//   «кода»), — одна из причин жалобы «двигаешь ползунок — громкость не меняется». Вторая —
	//   в `UAsdaSoundSubsystem::PlayMusic` (фейд замораживал громкость трека), исправлена там.
	//   На диск значение уходит только по «ОК/Применить»; «Отмена» вернёт его из снимка.
	case 70013:
		State.MusicVolume = S.Value;
		if (UAsdaGameUserSettings* Profile = UAsdaGameUserSettings::Get())
		{
			Profile->MusicVolume = (float)S.Value / 256.f;
			Profile->ApplyAudioSettings();
		}
		break;
	case 70016:
		State.SfxVolume = S.Value;
		if (UAsdaGameUserSettings* Profile = UAsdaGameUserSettings::Get())
		{
			Profile->SfxVolume = (float)S.Value / 256.f;
			Profile->ApplyAudioSettings();
		}
		break;
	case SysOptExtra::kIdBase + SysOptExtra::Q9_Draw:
		// ★ Фаза 2: дальность прорисовки — только в буфер, в игру уходит по «ОК/Применить» (как прочие строки
		//   «Качества»): живое применение потребовало бы отката по «Отмене», а дальность дорога в пересчёте.
		State.DrawDistance = S.Value;
		break;
	// ★ 19.10.2026: чувствительность мыши и поворота камеры — только в буфер, в игру по «ОК/Применить» (как дальность):
	//   смена скорости курсора посреди перетаскивания самой полосы сбивала бы руку.
	case SysOptExtra::kIdBase + SysOptExtra::I2_Cursor:
		State.CursorSpeed = S.Value;
		break;
	case SysOptExtra::kIdBase + SysOptExtra::I3_Camera:
		State.CameraTurn = S.Value;
		break;
	case 70058:
		State.Gamma = S.Value;
		if (UAsdaVideoSettingsSubsystem* Vid = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr)
		{
			Vid->SetGamma(S.Value);
		}
		break;
	default:
		break;
	}
}

// ★★★ ПОВЕДЕНИЕ ПОЛОСЫ — ПО ЭТАЛОНУ `CScrollBar` (ревизия 19.09.2026).
//   `CScrollBar_ctor@0x12FE240` (ui_core.c:117957-117958): шаг строки +155 = **6**, шаг страницы
//   +156 = **50**; окно настроек их не меняет (`uiTradeDialogInitControls` зовёт только
//   `uiScrollbarSetRange`). `uiScrollbarOnButtonEvent@0x12FF2C0`: план 1 (стрелка «−») = позиция − 6,
//   план 2 (стрелка «+») = позиция + 6, план 3 (бегунок) = начать перетаскивание; перетаскивание —
//   `uiScrollbarOnMouseDrag@0x12FF020`: позиция = диапазон × (курсор − левый край жёлоба − ½ бегунка)
//   / (ширина жёлоба − ширина бегунка).
//   Прежде порт брал шаг «диапазон / 20» (12 из 256) и начинал перетаскивание с любого места жёлоба.
//   ★ ИСПРАВЛЕНИЕ ОРИГИНАЛА (закон `fix-original-defects`): у ГОРИЗОНТАЛЬНОЙ полосы клик по жёлобу
//   мимо бегунка в оригинале НЕ делает ничего — ветка `case 0` сравнивает с бегунком координату Y
//   курсора (`copyPair79(...)[1]`) даже у горизонтальной полосы, курсор всегда «внутри» по высоте, и
//   функция выходит, не листнув. Порт листает на страницу (±50) в сторону курсора по X — так, как
//   эта ветка задумана для вертикальной полосы.
bool UAsdaSystemOptionSubsystem::TickSlider(FAsdaOptSlider& S, const FAsdaMouseFrame& M)
{
	if (!S.Track) { return false; }
	static constexpr int32 kLineStep = 6;    // CScrollBar +155
	static constexpr int32 kPageStep = 50;   // CScrollBar +156
	if (M.bLmbDown)
	{
		if (S.BtnLeft && AsdaUi::IsCursorOverWidget(S.BtnLeft))  { SetSliderValue(S, S.Value - kLineStep); return true; }
		if (S.BtnRight && AsdaUi::IsCursorOverWidget(S.BtnRight)) { SetSliderValue(S, S.Value + kLineStep); return true; }
		if (S.Thumb && AsdaUi::IsCursorOverWidget(S.Thumb))
		{
			S.bDragging = true;
		}
		else if (AsdaUi::IsCursorOverWidget(S.Track))
		{
			FVector2D TMin, TMax;
			if (S.Thumb && AsdaUi::WidgetAbsRect(S.Thumb, TMin, TMax))
			{
				SetSliderValue(S, S.Value + ((M.Cursor.X < TMin.X) ? -kPageStep : kPageStep));
				return true;
			}
		}
	}
	if (M.bLmbUp) { S.bDragging = false; }
	if (!S.bDragging) { return false; }

	FVector2D Min, Max;
	if (!AsdaUi::WidgetAbsRect(S.Track, Min, Max)) { return false; }
	// Ширина бегунка в экранных пикселях (у жёлоба и бегунка общий масштаб окна).
	float ThumbW = 0.f;
	FVector2D BMin, BMax;
	if (S.Thumb && AsdaUi::WidgetAbsRect(S.Thumb, BMin, BMax)) { ThumbW = BMax.X - BMin.X; }
	const float W = FMath::Max((Max.X - Min.X) - ThumbW, 1.f);
	const float T = FMath::Clamp((M.Cursor.X - Min.X - ThumbW * 0.5f) / W, 0.f, 1.f);
	SetSliderValue(S, S.Min + FMath::RoundToInt(T * (float)(S.Max - S.Min)));
	return true;
}

// ═══════════════════════════ ЧЕКБОКСЫ → БУФЕР ══════════════════════════════════════════════════
// Порт `uiOptions_ApplyControlValue@0xED4F80`: каждый idnum кладёт значение В СВОЙ байт буфера.
//   ★ У звука лежит ИНВЕРСИЯ (`filler[1128] = (a3 == 0)`) — это «заглушено», а не «включено».
void UAsdaSystemOptionSubsystem::OnCheckToggled(int32 IdNum, bool bOn)
{
	const uint8 V = bOn ? 1 : 0;
	if (IdNum >= 70172 && IdNum <= 70184 && ((IdNum - 70172) % 2) == 0)
	{
		State.Int1[(IdNum - 70172) / 2] = V;
	}
	else if (IdNum >= 70262 && IdNum <= 70272 && ((IdNum - 70262) % 2) == 0)
	{
		State.Int2[(IdNum - 70262) / 2] = V;
	}
	else if (IdNum >= 70362 && IdNum <= 70380 && ((IdNum - 70362) % 2) == 0)
	{
		State.Int3[(IdNum - 70362) / 2] = V;
	}
	else if (IdNum >= 70102 && IdNum <= 70118 && ((IdNum - 70102) % 2) == 0)
	{
		const int32 Idx = (IdNum - 70102) / 2;
		State.D3[Idx] = V;
		// ★ ОСОБАЯ ВЕТКА ИНДЕКСА 1 (эталон, ui_core.c:88854-88870): включить можно, ТОЛЬКО если
		//   устройство это умеет (`sub_924879(g_pRenderer) & 2`); тогда же взводится бит
		//   `g_dwSystemOptionFlags |= 0x40`. В порту роль «умеет ли устройство» играет наличие
		//   живого RHI: в headless/NullRHI режим сглаживания ландшафта включать нельзя.
		if (Idx == 1 && V != 0)
		{
			const bool bDeviceCan = (GEngine != nullptr) && !IsRunningCommandlet() && FApp::CanEverRender();
			if (!bDeviceCan)
			{
				State.D3[1] = 0;
				for (FAsdaOptCheck& Chk : Checks)
				{
					if (Chk.IdNum == IdNum && Chk.Box) { Chk.Box->SetIsChecked(false); }
				}
				UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] 3D №1 не включён: устройство не поддерживает (эталон sub_924879 & 2)"));
			}
		}
	}
	// ★★★ ИСПРАВЛЕНО 19.09.2026: ОТМЕЧЕННЫЙ ДИНАМИК = «ЗАГЛУШЕНО».
	//   Доказательство (три независимых источника):
	//   (1) скин `SpeakOnOff` (`JuiSkin.ini`): кадры 0/1 (не отмечен) — UV 0.199…0.242 атласа
	//       `NE_skillbar1` = ДИНАМИК, кадры 2/3 (отмечен) — UV 0.141…0.184 = ПЕРЕЧЁРКНУТЫЙ динамик;
	//   (2) `checkBox_OnToggle@0x12FDF70`: переход в «отмечен» шлёт 67073 (a3 = 1), обратно — 67074 (a3 = 0);
	//       `uiOptions_ApplyControlValue`: `filler[1128] = (a3 == 0)` ⇒ байт буфера = «музыка ВКЛЮЧЕНА»;
	//   (3) этот байт уходит в `CSoundStream_SetEnabled@0xFA0AC0` (vtbl+8 музыкального устройства,
	//       game_main.c:12009-12010): 1 = играть трек, 0 = остановить.
	//   ⇒ отмечен (a3 = 1) → байт 0 → музыка выключена. Прежний порт считал байт «заглушено», ставил
	//   галочку при ВКЛЮЧЁННОЙ музыке и показывал перечёркнутый динамик, пока музыка играла; снятие
	//   такой «галочки» глушило звук — то есть кнопка работала наоборот.
	else if (IdNum == 70012) { State.MusicMuted = bOn ? 1 : 0; }
	else if (IdNum == 70015) { State.SfxMuted   = bOn ? 1 : 0; }
	// ★ Фаза 2: флажки новых строк («наши» idnum 71000 + ключ).
	//   ★ 12.10.2026: флажков «затенение окружения» (E4) и «блики» (E6) в окне больше нет — ветки сняты.
	else if (IdNum == SysOptExtra::kIdBase + SysOptExtra::S4_VSync)   { State.VSync = V; }
	else if (IdNum == SysOptExtra::kIdBase + SysOptExtra::E5_MB)      { State.MotionBlur = V; }
	else if (IdNum == SysOptExtra::kIdBase + SysOptExtra::E7_Clouds)  { State.Clouds = V; }
	else
	{
		UE_LOG(LogAsdaSysOpt, Verbose, TEXT("[настройки] чекбокс %d обработчика в эталоне не имеет — значение отброшено"), IdNum);
	}
}
