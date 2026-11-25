// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaSystemMenuSubsystem — реализация панели главного меню `SYSTEMMAIN_NEW_WIN` (73400).
//   Полный разбор эталона — в шапке `AsdaSystemMenuSubsystem.h`, отчёт — `Reports/systemmain_win_port.md`.
#include "UI/AsdaSystemMenuSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaUiHelpers.h"                 // ★ D1: единый sRGB→linear (закон ui-srgb-gamma-rule)

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaMiniStateSubsystem.h"        // один общий тултип на HUD (эталонный g_ItemTooltip)
#include "UI/AsdaChatWindowSubsystem.h"       // гейт «чат-ввод в фокусе» (Input_IsTextInputFocused)
#include "UI/AsdaAnimatedPlane.h"
#include "Login/AsdaButtonRelay.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/IConsoleManager.h"
#include "World/AsdaFishingSubsystem.h"     // ★ 08.10.2026 (рыбалка В4): гейт «идёт рыбалка» кнопок 73401…73410 и свёртки
#include "UI/AsdaDeathInputLock.h"          // ★ 05.11.2026: меню смерти — открывается только 73406 (эталон ui_core.c:79464)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSysMenu, Log, All);

static const FName kSysMenuWindowId(TEXT("SYSTEMMAIN_NEW_WIN"));
static const TCHAR* kStateSection = TEXT("AsdaSystemMenu");
static const TCHAR* kStateKey     = TEXT("Expanded");   // порт-аналог строки «SMO» из WinState.bin

// ── Флаги отката к дефектам оригинала (закон fix-original-defects: чиним, но откат оставляем) ──
static ASDA_DEV_CVAR(int32, CVarLegacyNoRestore,
	TEXT("asda.SysMenu.LegacyNoRestore"), 0,
	TEXT("Д1: 1 = как в оригинале — сохранённое состояние панели НЕ восстанавливать (UI_LoadWindowState теряет «SMO»)"));

static ASDA_DEV_CVAR(int32, CVarLegacyMuteToggle,
	TEXT("asda.SysMenu.LegacyMuteToggle"), 0,
	TEXT("Д2: 1 = как в оригинале — сворачивание/разворот кнопками 73425/73426 БЕЗ звука (ShowHide зовётся с a3=0)"));

static ASDA_DEV_CVAR(int32, CVarLegacyTipLoop11,
	TEXT("asda.SysMenu.LegacyTipLoop11"), 0,
	TEXT("Д3: 1 = как в оригинале — цикл подсказок обрывается на i<11, подсказка 73411 (нотис 9802) недостижима"));

static ASDA_DEV_CVAR(int32, CVarLegacyPickStays,
	TEXT("asda.SysMenu.LegacyPickStays"), 0,
	TEXT("Д4: 1 = как в оригинале — CUIPanel_SetExpanded при сворачивании НЕ прячет кнопку гачи 73410"));

// ★ fishfix 09.10 (проверка книги рыбака 74000 путём игрока, ревизия МЕЛК-4): клик по окну, которое лежит НАД панелью
//   73400 (книга 1010 px у 1024×768 закрывает ряд её кнопок 588…860 × 634…666), доходил до чекбокса панели — фон окон
//   мышь в Slate не ловит, и под наградой книги открывался премиум-магазин 73407 (под старой раскладкой 810 px — журнал
//   квестов 73404 под наградой 3). Эталон `uiWindowMgrDispatchMouse@0x12AF540` отдаёт мышь ТОЛЬКО верхнему корню под
//   курсором — окну ниже событие не приходит вовсе. Порт: клик МЫШЬЮ по кнопкам панели, когда курсор над окном выше
//   панели (`IsCursorOverHigherWindow`) или над чужим всплывающим слоем (`IsPointerBlockedByTopmost`), — не команда,
//   кадр кнопки возвращается. Гейт стоит только на мышь (`OnCheckStateChanged` / `OnClicked`); горячие клавиши (cmd → окно
//   напрямую, напр. cmd34 → `UAsdaQuestLogSubsystem::Toggle`) и `ClickButton` его не касаются.
//   ★ fishfix2 09.10 (МЕЛК-2): тот же гейт — и на наведение (подсказка кнопки панели сквозь окно не всплывает, `TickHover`).
static ASDA_DEV_CVAR(int32, CVarLegacyClickThrough,
	TEXT("asda.SysMenu.LegacyClickThrough"), 0,
	TEXT("1 = прежний порт: кнопки панели 73400 принимают клик и сквозь окно, лежащее над ними; 0 (по умолчанию) = как ")
	TEXT("эталон uiWindowMgrDispatchMouse — мышь получает только верхнее окно под курсором."));

static ASDA_DEV_CVAR(int32, CVarLegacyStickyCheck,
	TEXT("asda.SysMenu.LegacyStickyCheck"), 0,
	TEXT("Д7: 1 = как в оригинале — кнопка остаётся «нажатой», даже если окно так и не открылось "
	     "(в эталоне кадр меняет сам checkBox_OnToggle, а гейты стоят уже в открывающей функции)"));

// ★ 25.10.2026 (ui_toggles, `Reports/ui_toggles/TOGGLE_AUDIT.md`) [ОРИГИНАЛ]: направление команды кнопки — из ФАКТИЧЕСКОЙ
//   видимости её окна, а не из латча чекбокса. Эталон: латч +608 кнопки меняет `checkBox_OnToggle@0x12FDF70`, но КАЖДОЕ окно
//   в конце своего Open/Close (и всех путей закрытия: крестик, ESC, пакет, смена карты) зовёт `uiPanel_SetTabVisible@0xE8FB10`,
//   поэтому латч у оригинала всегда равен видимости окна (инвариант), и щелчок по кнопке открытого окна — это 67074 «закрыть».
//   В порту латч отставал от окна у кнопок, чьё окно не наблюдалось (73407 — магазин закрыт ESC/крестиком, кнопка «горит»), и
//   следующий щелчок просил ЗАКРЫТЬ уже закрытое окно — открывалось только со второго раза. 0 = прежнее (по латчу чекбокса).
static ASDA_DEV_CVAR(int32, CVarToggleFromFact,
	TEXT("asda.SysMenu.ToggleFromFact"), 1,
	TEXT("1 (по умолчанию) = щелчок по кнопке панели 73400 открывает/закрывает окно по его ФАКТИЧЕСКОЙ видимости (эталонный ")
	TEXT("инвариант: латч +608 = видимость окна); 0 = прежнее — по латчу чекбокса (после ESC/крестика открывалось со второго раза)."));

static ASDA_DEV_CVAR(int32, CVarLegacyShowTab9,
	TEXT("asda.SysMenu.LegacyShowTab9"), 0,
	TEXT("Д5: 1 = как в оригинале — «разрешить» таб 9 (доп-меню) на самом деле ЗАПРЕЩАЕТ кнопку"));

// ══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaSystemMenuSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	BuildButtonTable();
	LoadState();
	UE_LOG(LogAsdaSysMenu, Log, TEXT("[меню] подсистема поднята: кнопок в таблице %d, состояние панели «%s»"),
		Buttons.Num(), bExpanded ? TEXT("развёрнута") : TEXT("свёрнута"));
}

void UAsdaSystemMenuSubsystem::Deinitialize()
{
	SaveState();
	ResetWidget();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaSystemMenuSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Раскладка панели меню — настройка ИГРОКА, а не персонажа: сохраняем её на диск и
	// только потом снимаем виджеты умершего мира (их пересоберёт вход на новую карту).
	SaveState();
	ResetWidget();
	bStateLoaded = false;
	(void)Reason;
}

// ★ Таблица РОВНО по эталону: привязки `CSystemMainWnd_InitControls@0xE8EC80`, индексы табов
//   `uiPanel_SetTabVisible@0xE8FB10`, нотисы `uiWindowMsgProc@0xE90220` (cmdIdTable/noticeIdTable),
//   цели — диспетчеры `uiHotkey_DispatchCommand`/`uiMainMenu_OnCommand`.
//   Порядок записей = порядок кнопок на панели слева направо (Origin X 20…325).
void UAsdaSystemMenuSubsystem::BuildButtonTable()
{
	Buttons.Reset();

	auto Add = [this](int32 IdNum, const TCHAR* Widget, int32 Tab, int32 Notice,
		int32 TargetId, const TCHAR* TargetName, bool bCollapse, bool bVis)
	{
		FAsdaSysMenuButton B;
		B.IdNum = IdNum;
		B.Widget = FName(Widget);
		B.TabIndex = Tab;
		B.NoticeId = Notice;
		B.TargetWindowId = TargetId;
		B.TargetWindowName = TargetName;
		B.bInCollapseSet = bCollapse;
		B.bVisibleByDefault = bVis;
		// ★ Имя окна из таблицы эталона — оно же id окна в `UAsdaWindowSubsystem` (INVEN_WIN,
		//   SKILL_WIN, QUEST_WIN…). Для записей без окна (73407/73408/73411, TargetWindowId = 0)
		//   привязки нет — там в имени человеческая подпись, а не id.
		B.BoundWindowId = (TargetId != 0) ? FName(TargetName) : NAME_None;
		Buttons.Add(B);
	};

	//   IDNum  виджет                          таб нотис цель  имя цели                      сворач. видима
	Add(73401, TEXT("NEW_SYSTEMMAIN_CHARINFOBUT"), 0, 2271,   900, TEXT("CHARAINFO_WIN"),        true,  true);
	// ★★★ ИСПРАВЛЕНО 2026-08-08 (реверс `Reports/inventory_re_A.md` §0): кнопка открывает
	//   СТАРОЕ окно `INVEN_WIN` (400), а не `INVEN_NEW_WIN` (83500). Путь показа:
	//   `ITEMBUT 73402 → uiToggleInventoryWindow@0xE8FA10 → uiOpenInventoryWindow@0xCFF300` →
	//   `SetVisible(FindControlByID(g_pWindowMgr, 400))`; числа 83500 в цепочке НЕТ, а сам dword
	//   83500 не встречается в эталонном клиенте ни разу. Прежняя запись была ошибкой разбора меню.
	Add(73402, TEXT("NEW_SYSTEMMAIN_ITEMBUT"),     1, 2272,   400, TEXT("INVEN_WIN"),            true,  true);
	Add(73403, TEXT("NEW_SYSTEMMAIN_SKILLBUT"),    7, 9418, 12000, TEXT("SKILL_WIN"),            true,  true);
	Add(73404, TEXT("NEW_SYSTEMMAIN_QUESTBUT"),    2, 2273,   700, TEXT("QUEST_WIN"),            true,  true);
	Add(73405, TEXT("NEW_SYSTEMMAIN_COMUNITYBUT"), 5, 2275,  6400, TEXT("COMMUNITY_WIN"),        true,  true);
	Add(73409, TEXT("NEW_ETC_M_BUT"),              9, 9800, 74600, TEXT("ETC_NEW_WIN"),          true,  true);
	Add(73407, TEXT("NEW_NEW_Primumshop_M_BUT"),   3, 2281,     0, TEXT("премиум-магазин"),      true,  true);
	Add(73408, TEXT("NEW_Serial_M"),              -1, 2277,     0, TEXT("—"),                    false, false);
	Add(73410, TEXT("NEW_Primiumpick"),           10, 9801, 68100, TEXT("Premium_Pick_select"),  true,  true);
	// ★★★ ИСПРАВЛЕНО 2026-09-17 (реверс окон группы «System», разбор в шапке
	//   `Public/UI/AsdaSystemOptionSubsystem.h`): кнопка открывает **ESC-МЕНЮ 69400**
	//   `SYSTEM_MenuWIN_NEW`, а НЕ окно настроек 69500. Оба диспетчера кнопки
	//   (`uiHotkey_DispatchCommand@0xE8F670` ui_core.c:79495 и `uiMainMenu_OnCommand@0xE8F880`
	//   ui_core.c:79553) на `case 73406` уходят в `uiPanelToggleOrClearTabs@0xE8FA70`
	//   (ui_core.c:79578), а тот тоггает `g_pOptionsWnd[0]` — и это контрол 69400
	//   (`uiNoticeDialogInitControls@0xED3080`, ui_core.c:88077). Окно настроек 69500 открывает
	//   ТОЛЬКО кнопка `SYSTEM_MENU_btn3` (69403) внутри самого ESC-меню. Прежняя запись 69500
	//   была ошибкой разбора (та же ошибка — в `Docs/UI/System/SYSTEM_MenuWIN_NEW.md`).
	Add(73406, TEXT("NEW_SYSTEMMAIN_SYSINFOBUT"),  6, 2276, 69400, TEXT("SYSTEM_MenuWIN_NEW"),   true,  true);
	Add(73411, TEXT("NEW_Webmall"),               -1, 9802,     0, TEXT("—"),                    false, false);
}

// ══════════════════════════════ ОКНО И ДЕРЕВО ══════════════════════════════════════════════════

bool UAsdaSystemMenuSubsystem::EnsureWidget()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (WM->IsWindowLive(MenuWidget))
	{
		return true;
	}
	ResetWidget();
	// Корень SelfHitTestInvisible: растянутый на вьюпорт UUserWidget с Visible съедает клики по миру.
	MenuWidget = WM->OpenWindowByPath(kSysMenuWindowId, WindowAssetPath, kSysMenuZ,
		ESlateVisibility::SelfHitTestInvisible);
	if (!MenuWidget)
	{
		UE_LOG(LogAsdaSysMenu, Error, TEXT("[меню] WBP_SYSTEMMAIN_NEW_WIN не загрузился (%s)"), *WindowAssetPath);
		return false;
	}
	SetupControls();
	ApplyCheckStyles();
	// Панель пересобрана (первый вход, смена карты) — состояние кнопок берём заново из окон.
	SyncCheckedFromWindows(/*bForce=*/true);
	// Эталон заканчивает InitControls вызовом `CUIWindow_ShowHide(this, 1, 0)`; поверх него —
	//   ВОССТАНОВЛЕНИЕ сохранённой свёрнутости (лечение дефекта Д1).
	const bool bWant = (CVarLegacyNoRestore.GetValueOnGameThread() != 0) ? true : bExpanded;
	bExpanded = bWant;
	ApplyExpandedVisibility(/*bIncludePick=*/true);
	SetTutorialArrowVisible(bArrowVisible);
	SetMessageText(FString());
	UE_LOG(LogAsdaSysMenu, Log, TEXT("[меню] окно открыто (z=%d), контролы разобраны: %s, панель %s"),
		kSysMenuZ, bControlsReady ? TEXT("да") : TEXT("НЕТ"),
		bExpanded ? TEXT("развёрнута") : TEXT("свёрнута"));
	return true;
}

void UAsdaSystemMenuSubsystem::ResetWidget()
{
	for (FAsdaSysMenuButton& B : Buttons)
	{
		if (B.Box) { B.Box->OnCheckStateChanged.Clear(); }
		B.Box = nullptr;
		B.bHovered = false;
		B.SettleFrames = 0;   // виджета нет — ждать реакции не от кого
	}
	Relays.Reset();
	MenuWidget = nullptr;
	BtnExpand = nullptr; BtnCollapse = nullptr;
	MessageGroup = nullptr; MessageText = nullptr;
	ArrowGroup = nullptr; ArrowImage = nullptr; ArrowAnims.Reset();
	RootMarker = nullptr; RootHitZone = nullptr;
	bControlsReady = false;
	TipShownFor = -1;
}

// Имена — по закону `jui-importer-widget-naming`: интерактив/Text = ТОЧНОЕ имя idc,
//   план/image = `{idc}_{ctrlIdx}_{plane}`, контейнер узла с детьми = `{idc или имя скина}_grp`.
//   Дерево снято headless-дампом (`Reports/headless_sysmain_dump.log`, 27 узлов).
void UAsdaSystemMenuSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!MenuWidget || !MenuWidget->WidgetTree) { return; }

	TArray<UWidget*> All;
	MenuWidget->WidgetTree->GetAllWidgets(All);

	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();

		// ── 10 кнопок-тогглов (CheckButton → UCheckBox с точным именем idc) ──
		if (UCheckBox* Cb = Cast<UCheckBox>(W))
		{
			for (int32 i = 0; i < Buttons.Num(); ++i)
			{
				if (N == Buttons[i].Widget.ToString()) { Buttons[i].Box = Cb; break; }
			}
			continue;
		}
		// ── 2 кнопки свернуть/развернуть (PushButton → UButton) ──
		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("NEW_SYSTEMMAIN_MENUBUT_Down")) { BtnExpand = Btn;   continue; }
			if (N == TEXT("NEW_SYSTEMMAIN_MENUBUT_Up"))   { BtnCollapse = Btn; continue; }
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			// Однострочный UI: autowrap в CanvasPanel ломает раскладку (закон ui-autowrap-canvas-gotcha).
			Tb->SetAutoWrapText(false);
			if (N == TEXT("SYSTEMMAIN_NEW_message")) { MessageText = Tb; continue; }
			if (N == TEXT("SYSTEMMAIN_NEW_WIN"))     { RootMarker = Tb;  continue; }
			continue;
		}
		if (N == TEXT("SYSTEMMAIN_NEW_message_grp")) { MessageGroup = Cast<UPanelWidget>(W); continue; }
		if (N == TEXT("NEW_SKILL_POP_grp"))          { ArrowGroup   = Cast<UPanelWidget>(W); continue; }
		if (N == TEXT("SYSTEMMAIN_NEW_WIN_0_HIT"))   { RootHitZone  = W; continue; }
		if (UAsdaAnimatedPlane* Ap = Cast<UAsdaAnimatedPlane>(W))
		{
			// `Ani_Gear_50_InnerCircle_5_0` (4 кадра 50×50, 300 мс) и `Small_Highlight_7_0` (4×34×34, 200 мс).
			if (N.StartsWith(TEXT("Ani_Gear_50_InnerCircle")) || N.StartsWith(TEXT("Small_Highlight")))
			{
				ArrowAnims.Add(Ap);
			}
			continue;
		}
		if (UImage* Img = Cast<UImage>(W))
		{
			if (N.StartsWith(TEXT("NEW_SKILL_POP_"))) { ArrowImage = Img; }
			continue;
		}
	}

	// ── Привязка событий ──
	for (int32 i = 0; i < Buttons.Num(); ++i)
	{
		FAsdaSysMenuButton& B = Buttons[i];
		if (!B.Box)
		{
			// Эталон в этом месте показывает MessageBox «Point NULL» — у нас честная строка в лог.
			UE_LOG(LogAsdaSysMenu, Warning, TEXT("[меню] контрол %d (%s) в WBP не найден"),
				B.IdNum, *B.Widget.ToString());
			continue;
		}
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = i;
		R->CheckCallback = [this](int32 Idx, bool bOn) { OnButtonToggledByMouse(Idx, bOn); };   // ★ fishfix 09.10: гейт мыши
		B.Box->OnCheckStateChanged.Clear();
		B.Box->OnCheckStateChanged.AddDynamic(R, &UAsdaButtonRelay::OnCheckChanged);
		AsdaUi::AnchorRelay(Relays, R, B.Box);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
		B.Box->SetIsChecked(B.bChecked);
	}
	if (BtnExpand)
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = 0;
		R->Callback = [this](int32 Which) { if (!IsMouseOverHigherWindow(TEXT("73425"))) { OnCollapseButton(Which); } };
		BtnExpand->OnClicked.Clear();
		BtnExpand->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, BtnExpand);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}
	if (BtnCollapse)
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = 1;
		R->Callback = [this](int32 Which) { if (!IsMouseOverHigherWindow(TEXT("73426"))) { OnCollapseButton(Which); } };
		BtnCollapse->OnClicked.Clear();
		BtnCollapse->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, BtnCollapse);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}

	// 73408 гасится прямо в `CSystemMainWnd_InitControls` (`SetVisible(control_73408, 0)`).
	for (FAsdaSysMenuButton& B : Buttons)
	{
		if (!B.bVisibleByDefault && B.Box)
		{
			B.Box->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	bControlsReady = (BtnExpand && BtnCollapse);
	for (const FAsdaSysMenuButton& B : Buttons)
	{
		if (B.bVisibleByDefault && !B.Box) { bControlsReady = false; }
	}
}

// ★ ЛЕЧЕНИЕ ДЕФЕКТА Д6 (наш, портовый). `UAsdaUmgBuilder::AddCheckBox` кладёт в `FCheckBoxStyle`
//   только кадры 0 и 2, БЕЗ тинтов, и одну и ту же кисть на hovered/pressed. У этих 10 кнопок UV
//   всех четырёх кадров ОДИНАКОВ, а различие — ровно в `Diffuse`, поэтому импортированные кнопки
//   выглядели мёртвыми: ни наведения, ни отличия «окно открыто» (нарушение закона
//   `ui-button-hover-click-rule`). Берём текстуру и UV из уже стоящей кисти и пересобираем ВСЕ
//   шесть состояний Slate из четырёх кадров скина (`button_states_re.md`):
//     кадр0 → Unchecked, кадр1 → UncheckedHovered, кадр2 → UncheckedPressed и Checked,
//     кадр3 → CheckedHovered и CheckedPressed.
void UAsdaSystemMenuSubsystem::ApplyCheckStyles()
{
	const FLinearColor CNormal       = AsdaUi::SrgbHexToLinear(Tints.Normal);
	const FLinearColor CHover        = AsdaUi::SrgbHexToLinear(Tints.Hover);
	const FLinearColor CChecked      = AsdaUi::SrgbHexToLinear(Tints.Checked);
	const FLinearColor CCheckedHover = AsdaUi::SrgbHexToLinear(Tints.CheckedHover);

	for (FAsdaSysMenuButton& B : Buttons)
	{
		if (!B.Box) { continue; }
		FCheckBoxStyle St = B.Box->GetWidgetStyle();
		// Эталонная база: кисть кадра 0 (её импортёр положил и в Unchecked, и в Checked).
		FSlateBrush Base = St.UncheckedImage;
		if (!Base.GetResourceObject())
		{
			UE_LOG(LogAsdaSysMenu, Warning,
				TEXT("[меню] у кнопки %d (%s) в стиле нет текстуры — тинты состояний не применяю"),
				B.IdNum, *B.Widget.ToString());
			continue;
		}
		auto Tinted = [&Base](const FLinearColor& C)
		{
			FSlateBrush Br = Base;
			Br.TintColor = FSlateColor(C);
			return Br;
		};
		St.UncheckedImage        = Tinted(CNormal);
		St.UncheckedHoveredImage = Tinted(CHover);
		St.UncheckedPressedImage = Tinted(CChecked);
		St.CheckedImage          = Tinted(CChecked);
		St.CheckedHoveredImage   = Tinted(CCheckedHover);
		St.CheckedPressedImage   = Tinted(CCheckedHover);
		// «Неопределённого» состояния у JUI-чекбокса нет — рисуем как обычный.
		St.UndeterminedImage        = Tinted(CNormal);
		St.UndeterminedHoveredImage = Tinted(CHover);
		St.UndeterminedPressedImage = Tinted(CChecked);
		B.Box->SetWidgetStyle(St);
		B.Box->SetCheckedState(B.bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
	}
}

void UAsdaSystemMenuSubsystem::ApplyCheckedVisual(FAsdaSysMenuButton& B)
{
	if (!B.Box) { return; }
	if (B.Box->IsChecked() != B.bChecked)
	{
		// SetIsChecked НЕ шлёт OnCheckStateChanged — рекурсии не будет (это путь «окно сообщило»).
		B.Box->SetIsChecked(B.bChecked);
	}
}

// ══════════════════════════════ ВИДИМОСТЬ / СВОРАЧИВАНИЕ ═══════════════════════════════════════

void UAsdaSystemMenuSubsystem::SetWindowShown(bool bShown)
{
	bWindowShown = bShown;
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kSysMenuWindowId, bShown, ESlateVisibility::SelfHitTestInvisible);
	}
}

// Списки видимости — 1:1 из `CUIWindow_ShowHide@0xE8F370` / `CUIPanel_SetExpanded@0xE8F150`.
//   Разворот: показать все кнопки набора + 73426, спрятать 73425.
//   Сворачивание: спрятать плашку 73421 и все кнопки набора + 73426, показать 73425.
//   bIncludePick=false воспроизводит дефект Д4 (SetExpanded забывает 73410).
void UAsdaSystemMenuSubsystem::ApplyExpandedVisibility(bool bIncludePick)
{
	// ★ Волна 1 (14.09.2026, В3): общая точка `AsdaUi::SetShownHit` — режим прежний (кнопки и галочки ловят мышь).
	auto SetVis = [](UWidget* W, bool bOn) { AsdaUi::SetShownHit(W, bOn); };

	for (FAsdaSysMenuButton& B : Buttons)
	{
		if (!B.Box || !B.bInCollapseSet) { continue; }
		// 73410 (гача) в свёрнутом виде остаётся только при включённом флаге легаси (Д4).
		const bool bSkipHide = (!bExpanded && !bIncludePick && B.IdNum == 73410);
		SetVis(B.Box, bExpanded || bSkipHide);
	}
	SetVis(BtnCollapse, bExpanded);    // 73426 виден развёрнутым
	SetVis(BtnExpand,  !bExpanded);    // 73425 виден свёрнутым
	if (!bExpanded)
	{
		// Эталон при сворачивании гасит плашку-подпись 73421.
		SetMessageText(FString());
	}
}

void UAsdaSystemMenuSubsystem::ShowHide(bool bShow, bool bPlaySound)
{
	if (!EnsureWidget()) { bExpanded = bShow; SaveState(); return; }
	const bool bWas = bExpanded;
	bExpanded = bShow;
	ApplyExpandedVisibility(/*bIncludePick=*/true);
	if (bShow)
	{
		// [ОРИГИНАЛ] `listMoveToEnd(g_pWindowMgr, pMainWindow)` (ui_core.c:79409) — МИСНЕЙМ: вставка через `sub_12B53D0` =
		//   `insert(begin())`, то есть панель уходит В НАЧАЛО списка — ВНИЗ стопки. Здесь прежде стоял подъём наверх (неверное
		//   прочтение имени). Ревизия подъёма окон (МЕЛК-4, 07.10.2026): после подъёма щелчком панель при показе возвращается на свой
		//   исходный z (kSysMenuZ = 93 — слой HUD, под обычными окнами).
		UGameInstance* GI = GetGameInstance();
		if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			if (WM->GetWindowZOrder(kSysMenuWindowId) != kSysMenuZ) { WM->SetWindowZOrder(kSysMenuWindowId, kSysMenuZ); }
		}
	}
	if (bPlaySound) { PlayUiSound(bShow ? SoundOpen : SoundCollapse); }
	if (bWas != bShow) { SaveState(); }
}

void UAsdaSystemMenuSubsystem::SetExpanded(bool bOn, bool bPlaySound)
{
	if (!EnsureWidget()) { bExpanded = bOn; SaveState(); return; }
	const bool bWas = bExpanded;
	bExpanded = bOn;
	// Д4: эталон в этой ветке НЕ прячет 73410. Чиним, откат — `asda.SysMenu.LegacyPickStays 1`.
	const bool bIncludePick = (CVarLegacyPickStays.GetValueOnGameThread() == 0);
	ApplyExpandedVisibility(bIncludePick);
	if (bPlaySound) { PlayUiSound(bOn ? SoundOpen : SoundCollapse); }
	if (bWas != bOn) { SaveState(); }
}

// ══════════════════════════════ ЧЕКНУТОСТЬ И ЗАПРЕТ ════════════════════════════════════════════

// Единственная точка смены латча +608. Эталон меняет кадр ТОЛЬКО если состояние отличается
//   (`if (getFlagByte_p608 != a3)`) — сохраняем это условие здесь, а не в каждом вызывающем.
void UAsdaSystemMenuSubsystem::ApplyChecked(FAsdaSysMenuButton& B, bool bChecked, const TCHAR* Why)
{
	if (B.bChecked == bChecked) { return; }
	B.bChecked = bChecked;
	ApplyCheckedVisual(B);
	UE_LOG(LogAsdaSysMenu, Verbose, TEXT("[меню] кнопка %d (таб %d) → %s (%s)"),
		B.IdNum, B.TabIndex, bChecked ? TEXT("окно открыто") : TEXT("окно закрыто"), Why);
}

void UAsdaSystemMenuSubsystem::SetTabChecked(int32 TabIndex, bool bChecked)
{
	for (FAsdaSysMenuButton& B : Buttons)
	{
		if (B.TabIndex != TabIndex) { continue; }
		ApplyChecked(B, bChecked, TEXT("прямой вызов по табу"));
		return;
	}
	UE_LOG(LogAsdaSysMenu, Warning, TEXT("[меню] таба %d в таблице эталона нет (есть 0,1,2,3,5,6,7,9,10)"), TabIndex);
}

void UAsdaSystemMenuSubsystem::SetButtonChecked(int32 ButtonIdNum, bool bChecked)
{
	if (FAsdaSysMenuButton* B = FindButtonMutable(ButtonIdNum))
	{
		ApplyChecked(*B, bChecked, TEXT("прямой вызов по кнопке"));
		return;
	}
	UE_LOG(LogAsdaSysMenu, Warning, TEXT("[меню] кнопки %d в панели нет"), ButtonIdNum);
}

// ══════════════════════════════ ★★★ ПРИВЯЗКА «КНОПКА ↔ ОКНО» ══════════════════════════════════
//   Разбор эталона и обоснование единого механизма — в шапке `AsdaSystemMenuSubsystem.h`.

void UAsdaSystemMenuSubsystem::RegisterWindow(int32 ButtonIdNum, FName WindowId)
{
	FAsdaSysMenuButton* B = FindButtonMutable(ButtonIdNum);
	if (!B)
	{
		UE_LOG(LogAsdaSysMenu, Warning, TEXT("[меню] окно просит кнопку %d, которой в панели нет"), ButtonIdNum);
		return;
	}
	if (!WindowId.IsNone()) { B->BoundWindowId = WindowId; }
	if (B->BoundWindowId.IsNone() && !WindowOpenOverrides.Contains(ButtonIdNum))
	{
		UE_LOG(LogAsdaSysMenu, Warning,
			TEXT("[меню] кнопка %d: id окна не задан ни таблицей, ни регистрацией — подсветку выводить не из чего"),
			ButtonIdNum);
		return;
	}
	if (B->bBound) { return; }
	B->bBound = true;
	B->SettleFrames = 0;
	// Стартовое состояние берём сразу, чтобы кнопка не «моргнула» на первом кадре.
	B->bWindowOpenObserved = ObserveWindowOpen(*B);
	ApplyChecked(*B, B->bWindowOpenObserved, TEXT("регистрация окна"));
	UE_LOG(LogAsdaSysMenu, Log, TEXT("[меню] кнопка %d подключена к окну «%s» (сейчас %s)"),
		ButtonIdNum, *B->BoundWindowId.ToString(),
		B->bWindowOpenObserved ? TEXT("открыто") : TEXT("закрыто"));
}

void UAsdaSystemMenuSubsystem::RegisterWindowPredicate(int32 ButtonIdNum, TFunction<bool()> IsOpen)
{
	if (!IsOpen)
	{
		UE_LOG(LogAsdaSysMenu, Warning, TEXT("[меню] кнопка %d: пустой предикат «окно открыто»"), ButtonIdNum);
		return;
	}
	WindowOpenOverrides.Add(ButtonIdNum, MoveTemp(IsOpen));
	RegisterWindow(ButtonIdNum, NAME_None);
}

void UAsdaSystemMenuSubsystem::UnregisterWindow(int32 ButtonIdNum)
{
	WindowOpenOverrides.Remove(ButtonIdNum);
	if (FAsdaSysMenuButton* B = FindButtonMutable(ButtonIdNum))
	{
		B->bBound = false;
		B->SettleFrames = 0;
		B->bWindowOpenObserved = false;
	}
}

bool UAsdaSystemMenuSubsystem::IsWindowRegistered(int32 ButtonIdNum) const
{
	const FAsdaSysMenuButton* B = FindButton(ButtonIdNum);
	return B && B->bBound;
}

bool UAsdaSystemMenuSubsystem::ObserveWindowOpen(const FAsdaSysMenuButton& B) const
{
	if (const TFunction<bool()>* Pred = WindowOpenOverrides.Find(B.IdNum))
	{
		return (*Pred) ? (*Pred)() : false;
	}
	if (B.BoundWindowId.IsNone()) { return false; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && WM->IsWindowVisible(B.BoundWindowId);
}

// ★★★ ЕДИНОЕ МЕСТО, откуда берётся подсветка кнопок. Работает ПО ИЗМЕНЕНИЮ видимости окна —
//   это ровно то, что в эталоне делает вызов `uiPanel_SetTabVisible` из конца Open/Close самого
//   окна, только покрывает все пути разом: клавиша, кнопка, крестик, пакет, смерть, смена карты.
//   Плюс лечение дефекта эталона Д7: если после нажатия кнопки окно так и не открылось (гейт
//   отказал), через `kSettleFrames` подсветка приводится к факту, а не остаётся висеть.
void UAsdaSystemMenuSubsystem::SyncCheckedFromWindows(bool bForce)
{
	const bool bSticky = (CVarLegacyStickyCheck.GetValueOnGameThread() != 0);
	for (FAsdaSysMenuButton& B : Buttons)
	{
		// ★★★ КНОПКА БЕЗ ОКНА (система в порт ещё не перенесена либо окно не зарегистрировалось).
		//   Открыться было нечему ⇒ факт всегда «закрыто». Через те же `kSettleFrames` возвращаем
		//   подсветку к факту — иначе латч «окно открыто» держится вечно и следующее нажатие
		//   просит ЗАКРЫТЬ несуществующее окно (жалоба владельца 2026-08-30 по кнопке 73405).
		//   Отсрочка обязательна: окно порта может зарегистрироваться на 1-2 кадра позже клика.
		if (!B.bBound)
		{
			// Наблюдать факт можно двумя способами: по имени окна из ТАБЛИЦЫ ЭТАЛОНА (оно же id
			//   в реестре окон — `COMMUNITY_WIN`, `SYSTEM_Option_NEW`) либо по предикату. Плюс
			//   две кнопки, у которых в эталоне обработчика НЕТ ВОВСЕ (73408/73411): у них факт
			//   всегда «закрыто», потому что команда никуда не уходит.
			const bool bObservable = !B.BoundWindowId.IsNone() || WindowOpenOverrides.Contains(B.IdNum);
			const bool bNoHandler  = (B.TargetWindowId == 0 && (B.IdNum == 73408 || B.IdNum == 73411));
			// ⚠ 73407 (премиум-молл): окно у него ЕСТЬ, но ни id окна, ни предиката нет — факт
			//   наблюдать нечем, и «привести к закрыто» здесь означало бы погасить подсветку у
			//   открытого окна. Такую кнопку не трогаем (закон «не выдумывать факт»).
			//   ★ 25.10.2026 (ui_toggles): 73407 подключён предикатом `UAsdaPremiumMallSubsystem::IsOpen` (факт молла) — сюда
			//   больше не попадает; ветка осталась для кнопок, чьё окно ещё не подключилось.
			if (!bObservable && !bNoHandler) { B.SettleFrames = 0; continue; }
			const bool bNow = bObservable ? ObserveWindowOpen(B) : false;
			if (B.SettleFrames > 0 && --B.SettleFrames == 0 && B.bChecked != bNow && !bSticky)
			{
				ApplyChecked(B, bNow, TEXT("окно не подключено, подсветка выведена из факта"));
				UE_LOG(LogAsdaSysMenu, Log,
					TEXT("[меню] кнопка %d (%s): окна «%s» в порту нет — подсветка возвращена к факту (%s)"),
					B.IdNum, *B.Widget.ToString(), *B.TargetWindowName,
					bNow ? TEXT("открыто") : TEXT("закрыто"));
			}
			continue;
		}
		const bool bNow = ObserveWindowOpen(B);
		if (bForce || bNow != B.bWindowOpenObserved)
		{
			B.bWindowOpenObserved = bNow;
			B.SettleFrames = 0;
			ApplyChecked(B, bNow, TEXT("окно сменило видимость"));
			continue;
		}
		if (B.SettleFrames > 0 && --B.SettleFrames == 0 && B.bChecked != bNow && !bSticky)
		{
			ApplyChecked(B, bNow, TEXT("команда кнопки окно не открыла/не закрыла"));
			UE_LOG(LogAsdaSysMenu, Log,
				TEXT("[меню] кнопка %d: окно «%s» на команду не отреагировало — подсветка приведена к факту (%s)"),
				B.IdNum, *B.BoundWindowId.ToString(), bNow ? TEXT("открыто") : TEXT("закрыто"));
		}
	}
}

FAsdaSysMenuButton* UAsdaSystemMenuSubsystem::FindButtonMutable(int32 ButtonIdNum)
{
	for (FAsdaSysMenuButton& B : Buttons)
	{
		if (B.IdNum == ButtonIdNum) { return &B; }
	}
	return nullptr;
}

bool UAsdaSystemMenuSubsystem::IsButtonChecked(int32 ButtonIdNum) const
{
	const FAsdaSysMenuButton* B = FindButton(ButtonIdNum);
	return B ? B->bChecked : false;
}

void UAsdaSystemMenuSubsystem::SetTabEnabled(int32 TabIndex, bool bEnabled)
{
	// Д5: `uiPanel_ShowTab` case 9 вместо разрешения ЗАПРЕЩАЕТ кнопку доп-меню (единственный case,
	//   зовущий `sub_9350A2`, тогда как остальные зовут `ClearField609`). Чиним; откат — CVar.
	bool bWant = bEnabled;
	if (bEnabled && TabIndex == 9 && CVarLegacyShowTab9.GetValueOnGameThread() != 0)
	{
		bWant = false;
	}
	for (FAsdaSysMenuButton& B : Buttons)
	{
		if (B.TabIndex == TabIndex) { B.bEnabled = bWant; return; }
	}
	UE_LOG(LogAsdaSysMenu, Warning, TEXT("[меню] таба %d в таблице эталона нет"), TabIndex);
}

void UAsdaSystemMenuSubsystem::SetAllButtonsEnabled(bool bEnabled)
{
	// Эталон: `uiPanelClearAllTabFlags` (разрешить) / `uiPanel_DestroyChildControls` (запретить).
	//   В обоих списках 73401,73402,73404,73405,73406,73407,73409,73403 — кнопка гачи 73410
	//   и служебные 73408/73411 в них НЕ значатся, поэтому их не трогаем (1:1).
	for (FAsdaSysMenuButton& B : Buttons)
	{
		if (B.IdNum == 73410 || B.IdNum == 73408 || B.IdNum == 73411) { continue; }
		B.bEnabled = bEnabled;
	}
}

// ══════════════════════════════ КЛИКИ ══════════════════════════════════════════════════════════

bool UAsdaSystemMenuSubsystem::IsTextInputFocused() const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaChatWindowSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr;
	// ★★ Волна В4 рыбалки (08.10.2026): `Input_IsTextInputFocused(g_pChatInputWnd)` эталона — МИСНЕЙМ, это fishState ≠ 0
	//   (world_anim §5 п.6-7: `uiHotkey_DispatchCommand` вызов 0xE8F6A7 g:488438 — кнопки 73401…73410; `uiWindowMsgProc`
	//   0x10401 вызов 0xE902AE g:488802 — свёртка 73425/73426). Чат-гейт порта оставлен, рыбалка добавлена.
	return (Chat && Chat->IsInputActive()) || UAsdaFishingSubsystem::IsFishingNow(GI);   // ★ В3 S7 (N14): одна точка «рыбалка»
}

// Эталон: `checkBox_OnToggle@0x12FDF70` шлёт окну 67073 (включили) / 67074 (выключили),
//   `uiWindowMsgProc@0xE90220` на 67073 играет `S_e_button06.wav` и зовёт `uiHotkey_DispatchCommand`,
//   на 67074 — `uiMainMenu_OnCommand`. Гейты диспетчеров воспроизведены ниже.
// ★ fishfix 09.10: гейт мыши (обоснование — у ключа `asda.SysMenu.LegacyClickThrough`).
bool UAsdaSystemMenuSubsystem::IsPanelMouseBlocked() const
{
	if (CVarLegacyClickThrough.GetValueOnGameThread() != 0) { return false; }
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && (WM->IsCursorOverHigherWindow(kSysMenuWindowId) || WM->IsPointerBlockedByTopmost(kSysMenuWindowId));
}

bool UAsdaSystemMenuSubsystem::IsMouseOverHigherWindow(const TCHAR* What) const
{
	if (!IsPanelMouseBlocked()) { return false; }
	++MouseRefusedCount;   // ★ R5-fix8-b 14.11.2026: счёт для самотеста (поведение то же)
	UE_LOG(LogAsdaSysMenu, Log, TEXT("[меню] клик по %s — курсор над окном выше панели: мышь получает верхнее окно (эталон), команды нет"), What);
	return true;
}

void UAsdaSystemMenuSubsystem::OnButtonToggledByMouse(int32 TableIndex, bool bChecked)
{
	if (Buttons.IsValidIndex(TableIndex) && IsMouseOverHigherWindow(*FString::FromInt(Buttons[TableIndex].IdNum)))
	{
		FAsdaSysMenuButton& B = Buttons[TableIndex];
		if (B.Box) { B.Box->SetIsChecked(B.bChecked); }   // кадр — назад: событие было не для панели
		return;
	}
	OnButtonToggled(TableIndex, bChecked);
}

void UAsdaSystemMenuSubsystem::OnButtonToggled(int32 TableIndex, bool bChecked)
{
	if (!Buttons.IsValidIndex(TableIndex)) { return; }
	FAsdaSysMenuButton& B = Buttons[TableIndex];

	// Запрещённая кнопка (латч +609): `checkBox_OnToggle` выходит ДО смены кадра ⇒ визуал
	//   откатываем обратно — это ровно эталонное поведение, а не наша страховка.
	if (!B.bEnabled)
	{
		if (B.Box) { B.Box->SetIsChecked(B.bChecked); }
		UE_LOG(LogAsdaSysMenu, Log, TEXT("[меню] кнопка %d запрещена (латч disabled) — команда не идёт"), B.IdNum);
		return;
	}

	// ★ 25.10.2026 [ОРИГИНАЛ] (ключ `asda.SysMenu.ToggleFromFact`, разбор — у ключа): у кнопки с НАБЛЮДАЕМЫМ окном (подключено
	//   `RegisterWindow` / предикатом) направление щелчка — «окно сейчас видно → закрыть, иначе → открыть». Отставший латч
	//   чекбокса (окно закрыли ESC/крестиком/другим окном, а подсветка ещё не догнала) больше не превращает щелчок в «закрыть
	//   закрытое». Кадр кнопки — сразу по направлению (`SetIsChecked` события не шлёт). Неподключённые кнопки — как прежде.
	if (CVarToggleFromFact.GetValueOnGameThread() != 0 && B.bBound)
	{
		const bool bFactOpen = ObserveWindowOpen(B);
		if (bChecked == bFactOpen)
		{
			UE_LOG(LogAsdaSysMenu, Log,
				TEXT("[меню] кнопка %d: латч кнопки отстал от окна «%s» (окно %s) — команда по факту: %s"),
				B.IdNum, *B.BoundWindowId.ToString(), bFactOpen ? TEXT("видно") : TEXT("скрыто"),
				bFactOpen ? TEXT("ЗАКРЫТЬ") : TEXT("ОТКРЫТЬ"));
		}
		bChecked = !bFactOpen;
		if (B.Box) { B.Box->SetIsChecked(bChecked); }
		B.bWindowOpenObserved = bFactOpen;   // факт до команды — по его смене `SyncCheckedFromWindows` поставит подсветку
	}

	// ★ Кадр кнопки эталон меняет ВСЕГДА (это делает сам `checkBox_OnToggle` до отправки события),
	//   а фильтры стоят уже в диспетчере команды. Поэтому состояние латча пишем здесь, а гейты —
	//   ниже, и при отказе кнопка остаётся «нажатой» ровно как в оригинале.
	B.bChecked = bChecked;
	// ★ Даём окну несколько кадров на реакцию: окна порта открываются отложенно. Не отреагирует —
	//   `SyncCheckedFromWindows` приведёт подсветку к факту (Д7).
	// ★★★ ОТСРОЧКУ СТАВИМ ВСЕМ КНОПКАМ, А НЕ ТОЛЬКО ПОДКЛЮЧЁННЫМ (дефект 2026-08-30).
	//   Раньше у кнопки БЕЗ окна (система в порт ещё не перенесена, например 73405 COMMUNITY_WIN)
	//   отсрочки не было вовсе, поэтому подсветку не возвращал никто: латч «окно открыто»
	//   защёлкивался навсегда и КАЖДОЕ следующее нажатие читалось как «ЗАКРЫТЬ» окно, которого
	//   нет. Класс дефекта — `no-state-latch-before-shown`: состояние ставится только по факту
	//   реального показа, а не по факту клика.
	B.SettleFrames = kSettleFrames;
	if (bChecked) { PlayUiSound(SoundOpen); }   // эталон: звук ТОЛЬКО на 67073

	// Эталонный гейт: `Input_IsTextInputFocused(g_pChatInputWnd)` стоит ТОЛЬКО в
	//   `uiHotkey_DispatchCommand` (67073). Закрытие (`uiMainMenu_OnCommand`, 67074) его НЕ имеет.
	//   Ветка `sub_91E14F` (режим перетаскивания предмета, пропускает лишь 73402) в порту
	//   неактивна — такого режима ещё нет.
	if (bChecked && IsTextInputFocused())
	{
		UE_LOG(LogAsdaSysMenu, Log, TEXT("[меню] чат-ввод в фокусе — открытие по кнопке %d подавлено (эталон)"), B.IdNum);
		return;
	}

	// ★★★ 10.10.2026 ЗАМОК МОДАЛКИ (решение владельца, `Reports/owner_1010/modal_lock.md`). Эталон: открытие (67073,
	//   `uiHotkey_DispatchCommand@0xE8F670`, ui_core.c:79464) и закрытие (67074, `uiMainMenu_OnCommand@0xE8F880`, ui_core.c:79528)
	//   при `sub_930E71(g_pWindowMgr)` = флаг модального контрола `CWindowMgr+0x200` не делают НИЧЕГО: исключение 73406 требует
	//   `!sub_930E71`, то есть под модалкой недостижимо. Кадр кнопки уже сменён (`checkBox_OnToggle` до диспетча) — как у оригинала;
	//   подсветку к факту вернёт `SyncCheckedFromWindows` (отсрочка выше). Общая точка флага — `IsAnyModalOpen`.
	//   Прочие два условия той же строки эталона (`sub_9323E3` — меню смерти, `helper_getField1` — предмет в руке) — в реестре
	//   отложенного (`PORT_PENDING_ON_SYSTEMS.md`, раздел «ESC и модалки»).
	if (const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		if (WM->IsAnyModalOpen())
		{
			UE_LOG(LogAsdaSysMenu, Log, TEXT("[меню] открыта модалка (эталон CWindowMgr+0x200) — %s по кнопке %d подавлено"),
				bChecked ? TEXT("открытие") : TEXT("закрытие"), B.IdNum);
			return;
		}
	}
	// ★★ 05.11.2026 [ОРИГИНАЛ] (`asda.Death.InputLock`): меню смерти открыто — `uiHotkey_DispatchCommand@0xE8F670` (ОТКРЫТИЕ, 67073)
	//   при `sub_9323E3` = `CUIManager+0x44` исполняет только 73406 (вкладка системы, ui_core.c:79466); закрытие
	//   (`uiMainMenu_OnCommand@0xE8F880`, 67074) этот флаг НЕ проверяет — уже открытое окно мёртвый закрыть может.
	//   Кадр кнопки не откатываем — как у модалки выше (подсветку к факту вернёт `SyncCheckedFromWindows`).
	if (bChecked && B.IdNum != AsdaDeathLock::kAllowedSysMenuButton && AsdaDeathLock::IsInputLocked(GetGameInstance()))
	{
		UE_LOG(LogAsdaSysMenu, Log, TEXT("[меню] открыто меню смерти (эталон CUIManager+0x44) — открытие по кнопке %d подавлено"), B.IdNum);
		return;
	}

	// 73408/73411: обработчиков в эталоне НЕТ вовсе — команда никуда не уходит.
	if (B.TargetWindowId == 0 && (B.IdNum == 73408 || B.IdNum == 73411))
	{
		UE_LOG(LogAsdaSysMenu, Log, TEXT("[меню] кнопка %d: обработчика в эталоне нет — действия нет"), B.IdNum);
		return;
	}

	OnMenuCommand.Broadcast(B.IdNum, B.TargetWindowId, bChecked);

	// Целевых окон в порту ещё нет — честно сообщаем, ЧТО просят, и не выдумываем заглушку-окно.
	const FString Line = FString::Printf(
		TEXT("[меню] кнопка %d (%s): просят %s окно %s%s"),
		B.IdNum, *B.Widget.ToString(), bChecked ? TEXT("ОТКРЫТЬ") : TEXT("ЗАКРЫТЬ"),
		*B.TargetWindowName,
		B.TargetWindowId ? *FString::Printf(TEXT(" (idnum %d)"), B.TargetWindowId) : TEXT(""));
	UE_LOG(LogAsdaSysMenu, Log, TEXT("%s"), *Line);
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaMessageSubsystem* Msg = GI->GetSubsystem<UAsdaMessageSubsystem>())
		{
			Msg->PostText(EAsdaMsgCategory::Tip, Line);
		}
	}
}

// Эталон `uiWindowMsgProc` case 0x10401: реагирует только когда чат-ввод НЕ в фокусе.
//   73425 → `CUIWindow_ShowHide(this, 1, 0)`, 73426 → `CUIWindow_ShowHide(this, 0, 0)`.
//   Третий аргумент 0 = без звука — это дефект Д2, чиним (откат `asda.SysMenu.LegacyMuteToggle 1`).
void UAsdaSystemMenuSubsystem::OnCollapseButton(int32 Which)
{
	if (IsTextInputFocused())
	{
		UE_LOG(LogAsdaSysMenu, Log, TEXT("[меню] чат-ввод в фокусе — сворачивание подавлено (эталон)"));
		return;
	}
	const bool bSound = (CVarLegacyMuteToggle.GetValueOnGameThread() == 0);
	ShowHide(/*bShow=*/Which == 0, bSound);
}

void UAsdaSystemMenuSubsystem::ClickButton(int32 ButtonIdNum)
{
	for (int32 i = 0; i < Buttons.Num(); ++i)
	{
		if (Buttons[i].IdNum != ButtonIdNum) { continue; }
		const bool bNew = !Buttons[i].bChecked;
		if (Buttons[i].Box) { Buttons[i].Box->SetIsChecked(bNew); }   // визуал как от мыши
		OnButtonToggled(i, bNew);
		return;
	}
	UE_LOG(LogAsdaSysMenu, Warning, TEXT("[меню] кнопки %d в панели нет"), ButtonIdNum);
}

// ══════════════════════════════ ПЛАШКА / СТРЕЛКА / ПОДСКАЗКИ ═══════════════════════════════════

void UAsdaSystemMenuSubsystem::SetMessageText(const FString& Text)
{
	const bool bOn = !Text.IsEmpty();
	if (MessageText) { MessageText->SetText(FText::FromString(Text)); }
	// Плашка и её торцы лежат в контейнере-группе — видимость ставим ей целиком.
	//   `Visible`, а не `HitTestInvisible`: в данных у 73421 `NoSelect="0"`, т.е. в оригинале плашка
	//   ЛОВИТ курсор (прямоугольный хит-тест) — иначе клик по ней провалился бы в мир.
	if (MessageGroup)
	{
		MessageGroup->SetVisibility(bOn ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UAsdaSystemMenuSubsystem::SetTutorialArrowVisible(bool bVisible)
{
	bArrowVisible = bVisible;
	if (ArrowGroup)
	{
		ArrowGroup->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (bVisible) { RestartArrowAnimations(); }
}

void UAsdaSystemMenuSubsystem::RestartArrowAnimations()
{
	// Покадровые планы стояли в свёрнутой группе — active-timer у них не тикал; перезапускаем.
	for (UAsdaAnimatedPlane* Ap : ArrowAnims)
	{
		if (Ap) { Ap->RestartAnim(); }
	}
}

FString UAsdaSystemMenuSubsystem::Notice(int32 LocId) const
{
	// GetTextById режет теги — здесь это правильно: нотисы панели разметки не несут
	//   (закон `ministate-win-port`: Raw нужен только там, где цвет задаёт сама строка).
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` (то же `GetTextById`).
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

// Эталонное сообщение 8: таблица `cmdIdTable`→`noticeIdTable`; для 73422 — спец-ветка
//   `UI_BuildHelpTooltipText(&g_ItemTooltip, 30, nullptr)` = топик 30 = loc 9168, плашка 180×50.
void UAsdaSystemMenuSubsystem::ShowButtonTip(int32 ButtonIdNum)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr;
	if (!Mini) { return; }

	if (ButtonIdNum == kArrowIdNum)
	{
		const FString Body = Notice(kArrowHelpLocId);
		if (!Body.IsEmpty()) { Mini->ShowSharedTip(FString(), Body, EAsdaTipOwner::SysMenu); TipShownFor = ButtonIdNum; }
		return;
	}
	// Д3: эталонный цикл `i < 11` не доходит до 12-й записи (73411 → 9802). Чиним; откат — CVar.
	const bool bLegacyLoop = (CVarLegacyTipLoop11.GetValueOnGameThread() != 0);
	for (int32 i = 0; i < Buttons.Num(); ++i)
	{
		const FAsdaSysMenuButton& B = Buttons[i];
		if (B.IdNum != ButtonIdNum) { continue; }
		if (bLegacyLoop && B.IdNum == 73411) { return; }
		if (B.NoticeId < 0) { return; }
		const FString Title = Notice(B.NoticeId);
		if (!Title.IsEmpty()) { Mini->ShowSharedTip(Title, FString(), EAsdaTipOwner::SysMenu); TipShownFor = ButtonIdNum; }
		return;
	}
}

void UAsdaSystemMenuSubsystem::HideTip()
{
	if (TipShownFor < 0) { return; }
	TipShownFor = -1;
	UGameInstance* GI = GetGameInstance();
	if (UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr)
	{
		Mini->HideSharedTip(EAsdaTipOwner::SysMenu);
	}
	// Эталонное сообщение 0xA гасит и плашку-подпись 73421.
	SetMessageText(FString());
}

// У `UCheckBox` нет UMG-событий наведения (в отличие от `UButton`), поэтому опрашиваем
//   `IsHovered()` раз в кадр — это и есть эталонная пара сообщений 8 (вход) / 0xA (уход).
void UAsdaSystemMenuSubsystem::TickHover()
{
	int32 Under = -1;
	for (FAsdaSysMenuButton& B : Buttons)
	{
		if (!B.Box) { continue; }
		const bool bNow = B.Box->IsHovered() && B.Box->GetVisibility() != ESlateVisibility::Collapsed;
		B.bHovered = bNow;
		if (bNow) { Under = B.IdNum; }
	}
	if (Under < 0 && ArrowImage && bArrowVisible && ArrowImage->IsHovered())
	{
		Under = kArrowIdNum;
	}
	// ★ fishfix2 09.10 (ревизия fishing_fixes_REVIEW МЕЛК-2): наведение — тот же гейт, что и клик. Эталон
	//   `uiWindowMgrDispatchMouse@0x12AF540` отдаёт мышь (и наведение, сообщения 8/0xA) только верхнему корню под курсором:
	//   курсор над окном, лежащим выше панели, — у панели «ухода» мыши, подсказка кнопки под окном не всплывает.
	//   Ключ отката общий: `asda.SysMenu.LegacyClickThrough 1`.
	if (Under >= 0 && IsPanelMouseBlocked())
	{
		for (FAsdaSysMenuButton& B : Buttons) { B.bHovered = false; }
		Under = -1;
	}
	if (Under == TipShownFor) { return; }
	if (Under < 0) { HideTip(); return; }
	HideTip();
	ShowButtonTip(Under);
}

// ══════════════════════════════ СОСТОЯНИЕ / ЗВУК ═══════════════════════════════════════════════

// ★ Д1. Эталон писал состояние строкой «SMO %d» в `./system/WinState.bin`
//   (`UI_SaveWindowState@0xD61D40`), а `UI_LoadWindowState@0xD61F20` эту строку читал и выбрасывал.
//   Порт хранит там же, где остальная раскладка окон, — `GGameUserSettingsIni`.
void UAsdaSystemMenuSubsystem::SaveState() const
{
	if (!GConfig) { return; }
	GConfig->SetBool(kStateSection, kStateKey, bExpanded, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void UAsdaSystemMenuSubsystem::LoadState()
{
	bStateLoaded = true;
	bool bSaved = true;
	if (GConfig && GConfig->GetBool(kStateSection, kStateKey, bSaved, GGameUserSettingsIni))
	{
		bExpanded = bSaved;
	}
}

void UAsdaSystemMenuSubsystem::PlayUiSound(FName Name) const
{
	if (Name.IsNone()) { return; }
	UGameInstance* GI = GetGameInstance();
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound2D(Name);
	}
}

const FAsdaSysMenuButton* UAsdaSystemMenuSubsystem::FindButton(int32 ButtonIdNum) const
{
	for (const FAsdaSysMenuButton& B : Buttons)
	{
		if (B.IdNum == ButtonIdNum) { return &B; }
	}
	return nullptr;
}

// ══════════════════════════════ TICK ═══════════════════════════════════════════════════════════

void UAsdaSystemMenuSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.SystemMenu");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
	UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	// Панель живёт только В МИРЕ (эталон: InitControls зовётся при входе, g_GameState == 1).
	if (!LM || LM->CurrentMapId < 0 || !WS || !IsValid(WS->LocalPlayerActor)) { return; }
	if (!EnsureWidget()) { return; }
	// ★★★ Подсветка кнопок выводится из фактической видимости их окон — одним местом.
	SyncCheckedFromWindows();
	TickHover();
}

// ══════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════

FString UAsdaSystemMenuSubsystem::DumpControls() const
{
	FString S;
	S += FString::Printf(TEXT("SYSTEMMAIN_NEW_WIN (73400): окно %s, панель %s, стрелка %s, контролы %s\n"),
		MenuWidget ? TEXT("открыто") : TEXT("НЕ открыто"),
		bExpanded ? TEXT("развёрнута") : TEXT("свёрнута"),
		bArrowVisible ? TEXT("видна") : TEXT("скрыта"),
		bControlsReady ? TEXT("разобраны") : TEXT("НЕ разобраны"));
	S += FString::Printf(TEXT("  73425 развернуть: %s | 73426 свернуть: %s | 73421 плашка: %s | 73422 стрелка: %s (анимаций %d)\n"),
		BtnExpand ? TEXT("есть") : TEXT("НЕТ"),
		BtnCollapse ? TEXT("есть") : TEXT("НЕТ"),
		MessageGroup ? TEXT("есть") : TEXT("НЕТ"),
		ArrowGroup ? TEXT("есть") : TEXT("НЕТ"),
		ArrowAnims.Num());
	// ★★★ ГЕОМЕТРИЯ И ХИТ-ТЕСТ КАЖДОЙ КНОПКИ. Без этих чисел «кнопка не работает» неотличимо от
	//   трёх разных причин, и на каждую мы уже попадались: свёрнутый РОДИТЕЛЬ (`Visible="0"`
	//   протаскивается рекурсией — ломало нас четырежды), перекрытие соседом и невидимый для
	//   мыши режим (`SelfHitTestInvisible`). Печатаем прямоугольник в абсолютных координатах,
	//   собственную видимость, видимость ВСЕЙ цепочки родителей и попарные перекрытия.
	auto RectOf = [](const UWidget* W, FVector2D& Pos, FVector2D& Size) -> bool
	{
		if (!W) { return false; }
		const FGeometry& G = W->GetCachedGeometry();
		Size = FVector2D(G.GetLocalSize()) * FVector2D(G.Scale, G.Scale);
		Pos  = FVector2D(G.GetAbsolutePosition());
		return Size.X > 0.f && Size.Y > 0.f;
	};
	auto VisName = [](ESlateVisibility V) -> const TCHAR*
	{
		switch (V)
		{
		case ESlateVisibility::Visible:              return TEXT("Visible");
		case ESlateVisibility::Collapsed:            return TEXT("Collapsed");
		case ESlateVisibility::Hidden:               return TEXT("Hidden");
		case ESlateVisibility::HitTestInvisible:     return TEXT("HitTestInvisible");
		case ESlateVisibility::SelfHitTestInvisible: return TEXT("SelfHitTestInvisible");
		default:                                     return TEXT("?");
		}
	};
	// Первый родитель, который прячет поддерево целиком (Collapsed/Hidden). Ровно эта рекурсия и
	//   съедала клики: сам контрол Visible, а группа над ним — нет.
	auto HidingParent = [VisName](const UWidget* W) -> FString
	{
		for (const UWidget* P = W ? W->GetParent() : nullptr; P; P = P->GetParent())
		{
			const ESlateVisibility V = P->GetVisibility();
			if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden)
			{
				return FString::Printf(TEXT("%s(%s)"), *P->GetName(), VisName(V));
			}
		}
		return FString();
	};

	TArray<FVector2D> Pos, Size;
	Pos.SetNum(Buttons.Num()); Size.SetNum(Buttons.Num());
	TArray<bool> HasRect; HasRect.SetNum(Buttons.Num());
	for (int32 i = 0; i < Buttons.Num(); ++i) { HasRect[i] = RectOf(Buttons[i].Box, Pos[i], Size[i]); }

	for (int32 i = 0; i < Buttons.Num(); ++i)
	{
		const FAsdaSysMenuButton& B = Buttons[i];
		S += FString::Printf(TEXT("  %d %-28s таб %2d нотис %5d цель %-22s виджет:%s привязка:%s %s%s%s\n"),
			B.IdNum, *B.Widget.ToString(), B.TabIndex, B.NoticeId, *B.TargetWindowName,
			B.Box ? TEXT("есть") : TEXT("НЕТ"),
			B.bBound
				? *FString::Printf(TEXT("«%s» окно %s"), *B.BoundWindowId.ToString(),
					B.bWindowOpenObserved ? TEXT("открыто") : TEXT("закрыто"))
				: TEXT("окно не подключено"),
			B.bChecked ? TEXT("[подсвечена] ") : TEXT(""),
			B.bEnabled ? TEXT("") : TEXT("[запрещена] "),
			B.bVisibleByDefault ? TEXT("") : TEXT("[скрыта в данных]"));
		if (!B.Box) { continue; }
		// Перекрытие: с кем именно пересекается прямоугольник этой кнопки.
		FString Over;
		if (HasRect[i])
		{
			for (int32 j = 0; j < Buttons.Num(); ++j)
			{
				if (i == j || !HasRect[j] || !Buttons[j].Box) { continue; }
				if (Buttons[j].Box->GetVisibility() == ESlateVisibility::Collapsed) { continue; }
				const bool bX = Pos[i].X < Pos[j].X + Size[j].X && Pos[j].X < Pos[i].X + Size[i].X;
				const bool bY = Pos[i].Y < Pos[j].Y + Size[j].Y && Pos[j].Y < Pos[i].Y + Size[i].Y;
				if (bX && bY) { Over += FString::Printf(TEXT("%d "), Buttons[j].IdNum); }
			}
		}
		const FString Hidden = HidingParent(B.Box);
		S += FString::Printf(
			TEXT("       прямоугольник (%.0f,%.0f)-(%.0f,%.0f) видимость %s разрешён:%s родитель-пряталка:%s перекрытия:%s ⇒ КЛИК %s\n"),
			Pos[i].X, Pos[i].Y, Pos[i].X + Size[i].X, Pos[i].Y + Size[i].Y,
			VisName(B.Box->GetVisibility()),
			B.Box->GetIsEnabled() ? TEXT("да") : TEXT("НЕТ"),
			Hidden.IsEmpty() ? TEXT("нет") : *Hidden,
			Over.IsEmpty() ? TEXT("нет") : *Over.TrimEnd(),
			(HasRect[i] && Hidden.IsEmpty() && B.Box->GetIsEnabled()
				&& B.Box->GetVisibility() == ESlateVisibility::Visible && Over.IsEmpty())
				? TEXT("ДОХОДИТ") : TEXT("НЕ ДОХОДИТ"));
	}
	return S;
}

// ══════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ═════════════════════════════════════════════
//   Юзер запускает их В ИГРЕ и лог не читает — каждая строка уходит и в лог, и в системный журнал.

static UAsdaSystemMenuSubsystem* MenuOf(UWorld* W)
{
	return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaSystemMenuSubsystem>() : nullptr;
}

// ★ U13 (В1 дедупа №2): тело — общая точка `AsdaNotice::ConsoleEcho` (прежняя копия была дословной, менялась только категория лога).
#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
static void MenuEcho(UWorld* W, const FString& Text)
{
	AsdaNotice::ConsoleEcho(W, Text, LogAsdaSysMenu);
}
#endif // !UE_BUILD_SHIPPING

// «73401» или «0» (индекс в таблице) → IDNum. −1 если не разобрать.
static int32 MenuResolveId(const UAsdaSystemMenuSubsystem* M, const FString& Arg)
{
	if (!M) { return -1; }
	const int32 V = FCString::Atoi(*Arg);
	if (V >= 73401) { return V; }
	if (V >= 0 && V < M->ButtonCount()) { return M->ButtonAt(V).IdNum; }
	return -1;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMenuDump(
	TEXT("asda.SysMenu.Dump"),
	TEXT("Меню игры: дамп КАЖДОГО разобранного контрола окна SYSTEMMAIN_NEW_WIN + состояние модели"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaSystemMenuSubsystem* M = MenuOf(W)) { MenuEcho(W, M->DumpControls()); }
		else { MenuEcho(W, TEXT("подсистема меню недоступна")); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMenuShow(
	TEXT("asda.SysMenu.Show"),
	TEXT("asda.SysMenu.Show <0|1> — скрыть/показать всё окно панели меню"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSystemMenuSubsystem* M = MenuOf(W);
		if (!M || A.Num() < 1) { MenuEcho(W, TEXT("использование: asda.SysMenu.Show <0|1>")); return; }
		const bool bOn = FCString::Atoi(*A[0]) != 0;
		M->SetWindowShown(bOn);
		MenuEcho(W, FString::Printf(TEXT("панель меню %s"), bOn ? TEXT("показана") : TEXT("скрыта")));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMenuExpand(
	TEXT("asda.SysMenu.Expand"),
	TEXT("asda.SysMenu.Expand <0|1> — свернуть/развернуть панель путём кнопок 73425/73426 (CUIWindow_ShowHide)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSystemMenuSubsystem* M = MenuOf(W);
		if (!M || A.Num() < 1) { MenuEcho(W, TEXT("использование: asda.SysMenu.Expand <0|1>")); return; }
		const bool bOn = FCString::Atoi(*A[0]) != 0;
		M->ShowHide(bOn, /*bPlaySound=*/true);
		MenuEcho(W, FString::Printf(TEXT("панель %s (путь кнопок)"), bOn ? TEXT("развёрнута") : TEXT("свёрнута")));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMenuPanel(
	TEXT("asda.SysMenu.Panel"),
	TEXT("asda.SysMenu.Panel <0|1> — внешний путь CUIPanel_SetExpanded (как магазин/пати прячут HUD)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSystemMenuSubsystem* M = MenuOf(W);
		if (!M || A.Num() < 1) { MenuEcho(W, TEXT("использование: asda.SysMenu.Panel <0|1>")); return; }
		const bool bOn = FCString::Atoi(*A[0]) != 0;
		M->SetExpanded(bOn, /*bPlaySound=*/true);
		MenuEcho(W, FString::Printf(TEXT("панель %s (внешний путь)"), bOn ? TEXT("развёрнута") : TEXT("свёрнута")));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMenuCheck(
	TEXT("asda.SysMenu.Check"),
	TEXT("asda.SysMenu.Check <idnum 73401.. | индекс 0..10> <0|1> — подсветить кнопку как «её окно открыто» "
	     "(эталонный uiPanel_SetTabVisible; так это делают сами окна)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSystemMenuSubsystem* M = MenuOf(W);
		if (!M || A.Num() < 2) { MenuEcho(W, TEXT("использование: asda.SysMenu.Check <idnum|индекс> <0|1>")); return; }
		const int32 Id = MenuResolveId(M, A[0]);
		if (Id < 0) { MenuEcho(W, TEXT("не понял кнопку")); return; }
		M->SetButtonChecked(Id, FCString::Atoi(*A[1]) != 0);
		MenuEcho(W, FString::Printf(TEXT("кнопка %d: подсветка %s"), Id, FCString::Atoi(*A[1]) ? TEXT("вкл") : TEXT("выкл")));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMenuEnable(
	TEXT("asda.SysMenu.Enable"),
	TEXT("asda.SysMenu.Enable <индекс таба 0..10 | all> <0|1> — запретить/разрешить кнопку(и) "
	     "(эталонные uiPanel_Hide/ShowTab и uiPanel_DestroyChildControls/uiPanelClearAllTabFlags)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSystemMenuSubsystem* M = MenuOf(W);
		if (!M || A.Num() < 2) { MenuEcho(W, TEXT("использование: asda.SysMenu.Enable <таб|all> <0|1>")); return; }
		const bool bOn = FCString::Atoi(*A[1]) != 0;
		if (A[0].Equals(TEXT("all"), ESearchCase::IgnoreCase))
		{
			M->SetAllButtonsEnabled(bOn);
			MenuEcho(W, FString::Printf(TEXT("все кнопки %s"), bOn ? TEXT("разрешены") : TEXT("запрещены")));
			return;
		}
		M->SetTabEnabled(FCString::Atoi(*A[0]), bOn);
		MenuEcho(W, FString::Printf(TEXT("таб %s %s"), *A[0], bOn ? TEXT("разрешён") : TEXT("запрещён")));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMenuClick(
	TEXT("asda.SysMenu.Click"),
	TEXT("asda.SysMenu.Click <idnum 73401.. | индекс 0..10> — программно нажать кнопку (тот же путь 67073/67074)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSystemMenuSubsystem* M = MenuOf(W);
		if (!M || A.Num() < 1) { MenuEcho(W, TEXT("использование: asda.SysMenu.Click <idnum|индекс>")); return; }
		const int32 Id = MenuResolveId(M, A[0]);
		if (Id < 0) { MenuEcho(W, TEXT("не понял кнопку")); return; }
		M->ClickButton(Id);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMenuArrow(
	TEXT("asda.SysMenu.Arrow"),
	TEXT("asda.SysMenu.Arrow <0|1> — показать/скрыть указатель обучения NEW_SKILL_POP (73422) над кнопкой навыков"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSystemMenuSubsystem* M = MenuOf(W);
		if (!M || A.Num() < 1) { MenuEcho(W, TEXT("использование: asda.SysMenu.Arrow <0|1>")); return; }
		const bool bOn = FCString::Atoi(*A[0]) != 0;
		M->SetTutorialArrowVisible(bOn);
		MenuEcho(W, FString::Printf(TEXT("стрелка обучения %s"), bOn ? TEXT("показана") : TEXT("скрыта")));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMenuAnim(
	TEXT("asda.SysMenu.Anim"),
	TEXT("Меню игры: перезапустить покадровые анимации указателя (шестерня 4×300 мс + подсветка 4×200 мс)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaSystemMenuSubsystem* M = MenuOf(W);
		if (!M) { return; }
		M->RestartArrowAnimations();
		MenuEcho(W, TEXT("анимации указателя перезапущены"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMenuTip(
	TEXT("asda.SysMenu.Tip"),
	TEXT("asda.SysMenu.Tip <idnum|индекс|73422> — показать подсказку кнопки (таблица нотисов эталона)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSystemMenuSubsystem* M = MenuOf(W);
		if (!M || A.Num() < 1) { MenuEcho(W, TEXT("использование: asda.SysMenu.Tip <idnum|индекс>")); return; }
		const int32 Raw = FCString::Atoi(*A[0]);
		const int32 Id = (Raw == UAsdaSystemMenuSubsystem::kArrowIdNum) ? Raw : MenuResolveId(M, A[0]);
		if (Id < 0) { MenuEcho(W, TEXT("не понял кнопку")); return; }
		M->ShowButtonTip(Id);
		MenuEcho(W, FString::Printf(TEXT("подсказка кнопки %d показана"), Id));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMenuMsg(
	TEXT("asda.SysMenu.Msg"),
	TEXT("asda.SysMenu.Msg <текст> — показать плашку-подпись 73421 (без аргумента — спрятать, эталонное событие 0xA)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSystemMenuSubsystem* M = MenuOf(W);
		if (!M) { return; }
		const FString Text = FString::Join(A, TEXT(" "));
		M->SetMessageText(Text);
		MenuEcho(W, Text.IsEmpty() ? FString(TEXT("плашка скрыта")) : FString::Printf(TEXT("плашка: %s"), *Text));
	}));
#endif // ASDA_DEV_CONSOLE
