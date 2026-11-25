// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWindowSubsystem_Close.cpp — ★ 23.10.2026 часть UAsdaWindowSubsystem, вынесенная из AsdaWindowSubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaWindowSubsystem.plan.json).
//   uiCloseAllWindows/uiExpandPartyPanel, позиции модальных окон и якоря, замок модальности, ESC закрывает верхнее окно, z-порядок.
//   #include — те же, что у AsdaWindowSubsystem.cpp, в том же порядке; общее — AsdaWindowSubsystemInternal.h.
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiVisibilitySubsystem.h"   // ★ 21.10.2026: Alt+Z — флаг «интерфейс скрыт» (владелец)
#include "UI/AsdaEscChain.h"   // ★ 25.10.2026: проход ESC пишет, какое окно ответило (шаг «ESC снимает цель», флаг 0x215 эталона)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaLocIds.h"   // Дд2/Дд3: нотисы кнопок ОБЩИХ окон (ONEBUT/TWOBUT/SHOUTING) — одно место
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/EditableText.h"   // поле ввода 1583 диалога SHOUTING_WIN
#include "Components/CheckBox.h"         // ★ приёмка: галки под ручкой (INVEN_NEW_WIN 83503)
#include "Components/EditableTextBox.h"  // ★ приёмка: поля ввода под ручкой
#include "Components/MultiLineEditableText.h"  // ★ приёмка: многострочный ввод
#include "Components/ComboBoxString.h"   // ★ приёмка: выпадающие списки
#include "Components/Slider.h"           // ★ приёмка: ползунки тянут мышь сами
#include "Components/ScrollBox.h"        // ★ приёмка: прокрутка тянет мышь сама
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"   // DPI-скейл вьюпорта: абсолютная дельта курсора → юниты канвы
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Image.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"   // метрики строки для asda.UI.DumpTextMetrics   // фокус на игровой вьюпорт при базовом GameAndUI
#include "Misc/ConfigCacheIni.h"   // раскладка окон между сессиями: секция [AsdaWindowLayout] в ini игры
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaLoginData.h"   // FAsdaLoginData::Loc — подписи кнопок ТОЛЬКО из таблицы DT_GameText
#include "Text/AsdaTextSubsystem.h"   // игровые loc-подписи (2288 «ОК» / 2289 «Отмена») для TWOBUT_MSGBOX
#include "UI/AsdaUiHelpers.h"         // D1b: общая `AsdaUi::WidgetAbsRect` (хит-тест окна и рамки)
#include "UI/AsdaMessageSubsystem.h"  // D2: фолбэк однокнопочного msgbox, когда WBP не загрузился
#include "UI/AsdaHotbarSubsystem.h"      // шаг 13 `uiCloseAllWindows`: блокировка/разблокировка хотбара
#include "UI/AsdaSystemMenuSubsystem.h"  // шаги 23-24: сворачивание трея и запрет его кнопок
#include "Resources/AsdaSoundSubsystem.h" // звуки открытия/закрытия окна количества (5 / 6)
#include "UI/AsdaItemTooltipSubsystem.h"  // ★ 27.09.2026: панель 1530 для `DropTopMsgBoxBeforeNotice`
#include "UI/AsdaPurchaseBoxSubsystem.h"  // ★ 27.09.2026: панель 1559 для `DropTopMsgBoxBeforeNotice`
#include "Styling/SlateStyleRegistry.h"   // ★ гашение синей рамки фокуса: подмена кисти FocusRectangle
#include "Styling/SlateStyle.h"
#include "Styling/AppStyle.h"
#include "Brushes/SlateNoResource.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Login/AsdaText.h"                 // FAsdaText::Loc — строки диалога `asda.UI.ConfirmPreview` (07.10.2026)
#include "Login/AsdaLoginGameMode.h"          // ★ 08.10.2026: подъём при показе не трогает экран логина
#include "World/AsdaWorldKind.h"               // ★ F7 (дедуп №2, В3): общий признак «сцена логина»
#include "GameFramework/GameModeBase.h"     // GetAuthGameMode — проверка экрана логина
#include "UnrealClient.h"                   // С-1 (07.10.2026): FViewport::ViewportResizedEvent — смена размера вьюпорта
#include "Blueprint/GameViewportSubsystem.h" // М-1 (07.10.2026): слот окна во вьюпорте — полноэкранный ли он
#include "Widgets/SOverlay.h"               // ★ 08.10.2026: подъём окна БЕЗ снятия с вьюпорта (слот оверлея вьюпорта)
#include "UI/AsdaNotice.h"                  // ★ 08.10.2026 (ревизия В3 рыбалки): нотис 6835 строгого нумпада — LocRaw / ChatColored
#include "UI/AsdaChatSubsystem.h"           // ★ 08.10.2026: ColGold — цвет −11514 нотиса 6835
#include "UI/AsdaMarkup.h"                  // ★ 09.10.2026: UAsdaRichText::ApplyFormatted — теги <FC=…> в теле окон-сообщений
#include "Components/RichTextBlock.h"      // ★ 09.10.2026: тело окна-сообщения после первой подмены — URichTextBlock
#include "AsdaWindowSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWindowSubsystem*.cpp (LogAsdaWindow, чистые хелперы)
#include "UI/AsdaEnterGuard.h"            // ★ 25.10.2026: пометка «без Enter» окон-вопросов (FAsdaMsgBoxEntry::bNoEnter)


// ★★ 22.10.2026 [ИСПРАВЛЕНИЕ] «залипшая клавиша хода после модалки» — ДЕФЕКТ ОРИГИНАЛА, порт его унаследовал (ревизия В-2,
//   `Reports/wasd_2026-10-22/REPORT.md`, `REVIEW.md`).
//   Модалка ставит `FInputModeUIOnly` → `UGameViewportClient::SetIgnoreInput(true)`: ВСЕ клавиши вьюпорта (и отпускание W)
//   до контроллера не доходят (GameViewportClient.cpp:765). Если фокус клавиатуры при этом остаётся на вьюпорте (корень
//   модалки не фокусируемый — «InputMode:UIOnly focus Non-Focusable»), сброса клавиш по потере фокуса
//   (`bShouldFlushPressedKeysOnViewportFocusLost`) нет: W, отпущенная под модалкой, остаётся «зажатой» в `UPlayerInput`, и
//   после закрытия модалки персонаж сам бежит вперёд, пока W не нажать и отпустить. У ЭТАЛОНА то же: WM_KEYUP (`MainWndProc`,
//   game_main.c:6214-6230, `LABEL_302`) зовёт отпускание раскладки (`Input_OnKeyDown_Dispatch` — миснейм) только при
//   `!sub_930E71(g_pWindowMgr)` — том же флаге модалки `CWindowMgr+0x200` (`get_o200`), что и нажатие (6140); `uiCanOpenWindowGate`
//   флага модалки не читает (`Reports/owner_1010/modal_lock.md`, таблица и п. «fact»). Отпущенная под модалкой W оставляет
//   `g_bMoveFwd = 1` — после закрытия персонаж бежит. Порт: на ПЕРЕХОДЕ
//   режима мир ↔ модалка клавиши сбрасываются (`FlushPressedKeys`); физически зажатая W возвращается первым автоповтором
//   (UE 5.8 `Input.AutoReconcilePressedEventsOnFirstRepeat`). 0 — прежнее поведение (для сверки самотестом KB25).
static ASDA_DEV_CVAR(int32, CVarModalFlushKeys,
	TEXT("asda.UI.ModalFlushKeys"), 1,
	TEXT("[ИСПРАВЛЕНИЕ 22.10] Открытие/закрытие модального окна сбрасывает зажатые клавиши (1): отпускание клавиши хода под ")
	TEXT("модалкой не теряется — персонаж не бежит сам после её закрытия. 0 — прежнее поведение."),
	ECVF_Default);

// ★ 25.10.2026 [ИСПРАВЛЕНИЕ]: пересчёт режима ввода без модалки (`RefreshModalInput`, его зовёт ЛЮБОЙ показ/скрытие окна —
//   например, плашки цели PVP_TARGET при снятии или смерти цели) больше не уводит фокус клавиатуры из поля ввода, которое
//   по-прежнему на экране (строка чата, поле окна). У эталона Win32-поле фокус от показа чужого окна не теряет; у порта строка
//   чата при этом закрывалась (флаг набора снимается по уходу фокуса). Возврат фокуса вьюпорту после закрытия модалки — как был.
static ASDA_DEV_CVAR(int32, CVarKeepTextFocusOnRefresh,
	TEXT("asda.UI.KeepTextFocusOnRefresh"), 1,
	TEXT("1 — [ИСПРАВЛЕНИЕ] показ/скрытие немодального окна не отнимает фокус у поля ввода, которое ещё на экране (строка чата не ")
	TEXT("закрывается, когда прячется плашка цели); после закрытия модалки фокус, как и прежде, возвращается миру. 0 — прежнее: ")
	TEXT("любой пересчёт без модалки отдавал фокус вьюпорту."),
	ECVF_Default);

// Фокус клавиатуры — в поле ввода, которое сейчас на экране (путь до виджета находится — предки не свёрнуты, окно не снято).
static bool AsdaIsLiveTextFieldFocused()
{
	if (!FSlateApplication::IsInitialized()) { return false; }
	FSlateApplication& Slate = FSlateApplication::Get();
	const TSharedPtr<SWidget> F = Slate.GetKeyboardFocusedWidget();
	if (!F.IsValid() || !F->GetTypeAsString().Contains(TEXT("EditableText"))) { return false; }
	FWidgetPath Path;
	return Slate.FindPathToWidget(F.ToSharedRef(), Path, EVisibility::Visible);
}

// ══════════ ★★★ ПОРТ `uiCloseAllWindows@0xD61960` / `uiExpandPartyPanel@0xD61C30` ══════════
//   Обоснование, полный разбор эталонного списка и обеих прошлых регрессий — в шапке объявлений
//   (`AsdaWindowSubsystem.h`) и в отчёте `Reports/gacha_close_windows.md`.

void UAsdaWindowSubsystem::RegisterWindowCloser(FName WindowId, FSimpleDelegate Closer)
{
	if (WindowId.IsNone()) { return; }
	// Повторная регистрация (окно пересобрано после травела) обязана ЗАМЕНИТЬ старую роспись,
	//   а не добавить вторую: иначе закрыватель звался бы дважды и «закрыть» второй раз выдало бы
	//   лишний звук/лог. Место в порядке при этом сохраняем — оно от пересборки не зависит.
	for (TPair<FName, FSimpleDelegate>& Kv : WindowClosers)
	{
		if (Kv.Key == WindowId) { Kv.Value = MoveTemp(Closer); return; }
	}
	WindowClosers.Emplace(WindowId, MoveTemp(Closer));
}

void UAsdaWindowSubsystem::UnregisterWindowCloser(FName WindowId)
{
	WindowClosers.RemoveAll([WindowId](const TPair<FName, FSimpleDelegate>& Kv) { return Kv.Key == WindowId; });
}

void UAsdaWindowSubsystem::CloseAllPlayerWindows()
{
	// Шаг 1 эталона: `*(this + 17) = 1`.
	bPlayerWindowsHidden = true;

	// Шаги 2-22: у каждого окна-участника зовём ЕГО СОБСТВЕННОЕ закрытие. Копия списка — потому что
	//   закрыватель вправе тронуть реестр (снять роспись, закрыть дочернее окно).
	int32 Closed = 0;
	TArray<TPair<FName, FSimpleDelegate>> Snapshot = WindowClosers;
	TGuardValue<bool> ClosingGuard(bClosingAllPlayerWindows, true);   // волна В3: см. `IsClosingAllPlayerWindows`
	for (const TPair<FName, FSimpleDelegate>& Kv : Snapshot)
	{
		if (Kv.Value.IsBound())
		{
			Kv.Value.ExecuteIfBound();
			++Closed;
		}
	}

	// Шаг 13: `uiShowPanelControls(g_pQuickSlotBar)` — мисномер, это БЛОКИРОВКА хотбара
	//   (`bPanelHidden = 0` ⇒ `hotbarOnUiEvent` игнорирует ввод; затемняющие оверлеи показаны).
	//   Разбор мисномера уже стоит в шапке `AsdaHotbarSubsystem.h`, поэтому здесь только ссылка.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaHotbarSubsystem* Hotbar = GI->GetSubsystem<UAsdaHotbarSubsystem>())
		{
			Hotbar->SetPanelActive(false);
		}
		// Шаги 23-24: свернуть трей главного меню и ЗАПРЕТИТЬ его кнопки (кроме 73410 — её в
		//   эталонных списках нет, ею и выходят из сцены; это уже учтено в `SetAllButtonsEnabled`).
		if (UAsdaSystemMenuSubsystem* Menu = GI->GetSubsystem<UAsdaSystemMenuSubsystem>())
		{
			Menu->SetExpanded(false, /*bPlaySound*/ true);
			Menu->SetAllButtonsEnabled(false);
		}
	}

	UE_LOG(LogAsdaWindow, Log,
		TEXT("[окна] эталонное закрытие (uiCloseAllWindows): закрывателей вызвано %d из %d; ")
		TEXT("хотбар заблокирован, трей свёрнут и запрещён"),
		Closed, WindowClosers.Num());
}

void UAsdaWindowSubsystem::RestoreAfterSceneExit()
{
	// `*(this + 17) = 0`.
	bPlayerWindowsHidden = false;
	if (UGameInstance* GI = GetGameInstance())
	{
		// `uiHidePanelControls(g_pQuickSlotBar)` — мисномер: РАЗБЛОКИРОВАТЬ хотбар (оверлеи сняты).
		if (UAsdaHotbarSubsystem* Hotbar = GI->GetSubsystem<UAsdaHotbarSubsystem>())
		{
			Hotbar->SetPanelActive(true);
		}
		// `CUIPanel_SetExpanded(g_pSystemMainWnd, 1, 1)` + `uiPanelClearAllTabFlags`.
		if (UAsdaSystemMenuSubsystem* Menu = GI->GetSubsystem<UAsdaSystemMenuSubsystem>())
		{
			Menu->SetExpanded(true, /*bPlaySound*/ true);
			Menu->SetAllButtonsEnabled(true);
		}
	}
	// ★ Окна НЕ переоткрываются: снимок `uiCaptureOpenWindowsState` вход в сцену гачи не делает
	//   (world_map.c:4700-4702 — его там нет, в отличие от входа в магазин ui_core.c:61117).
	UE_LOG(LogAsdaWindow, Log,
		TEXT("[окна] выход из сцены (uiExpandPartyPanel): хотбар разблокирован, трей развёрнут и разрешён; ")
		TEXT("ранее закрытые окна НЕ восстанавливаются (так в эталоне)"));
}

// ── Модальность (аналог uiIsAnyModalWindowOpen@0xB79520: модальное окно блокирует ввод к нижним) ──

void UAsdaWindowSubsystem::SetWindowModal(FName WindowId, bool bModal)
{
	if (FAsdaWindowEntry* Found = Windows.Find(WindowId))
	{
		Found->bModal = bModal;
		if (bModal)
		{
			// ★★★ Стало модальным на ходу → поднять в зарезервированную полосу (см. kModalZ).
			if (Found->ZOrder < kModalZ) { SetWindowZOrder(WindowId, AllocateModalZ()); }
			PlaceModalByOriginal(WindowId);   // ★ 08.10.2026 (пакет 2): позиция оригинала, вне таблицы — центр
		}
		RefreshModalInput();
	}
}

// Свободный z в полосе модалок: kModalZ, либо на 1 выше самой верхней уже открытой модалки.
int32 UAsdaWindowSubsystem::AllocateModalZ() const
{
	int32 Z = kModalZ;
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		if (Kv.Value.bModal && Kv.Value.Widget) { Z = FMath::Max(Z, Kv.Value.ZOrder + 1); }
	}
	return FMath::Min(Z, kModalZ + 999);
}

void UAsdaWindowSubsystem::CenterModalContent(UUserWidget* W)
{
	// Модалка центрируется по общему bbox — ссылочного контрола у неё нет.
	CenterWindowOnScreen(W, nullptr);
}

// Ключ отката позиций модальных окон оригинала — разбор у `PlaceModalByOriginal` в заголовке.
static ASDA_DEV_CVAR(int32, CVarModalOriginalPos,
	TEXT("asda.UI.ModalOriginalPos"), 1,
	TEXT("1 — модальные окна в позиции оригинала (1580/8000 — разметка .JUI, 1530 — левый-верх 1500, 1410 — у курсора; ")
	TEXT("решение владельца 08.10.2026); 0 — прежнее правило порта: все модальные по центру экрана."),
	ECVF_Default);

bool UAsdaWindowSubsystem::UseOriginalModalPos()
{
	return CVarModalOriginalPos.GetValueOnGameThread() != 0;
}

FVector2D UAsdaWindowSubsystem::MsgBox1500TopLeft(const FVector2D& View)
{
	// Размер 1500 ONEBUT_MSGBOX из `gamemain_jui.ini` (Size "360 140"); эталон берёт его у самого 1500 (vtable+24).
	constexpr int32 k1500W = 360, k1500H = 140;
	const int32 W = FMath::RoundToInt32(View.X), H = FMath::RoundToInt32(View.Y);   // `CConfig_GetWidth/Height` — целые
	return FVector2D((double)((W >> 1) - (k1500W >> 1)), (double)((H >> 1) - (k1500H >> 1)));
}

// Разбор и таблица — у объявления (пакет 2, 08.10.2026).
void UAsdaWindowSubsystem::PlaceModalByOriginal(FName WindowId)
{
	FAsdaWindowEntry* E = Windows.Find(WindowId);
	if (!E || !E->Widget) { return; }
	bool bLogin = false;   // экран входа: свои окна, закон «Login не трогать» — прежний центр
	if (const UWorld* Wd = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		bLogin = AsdaWorldKind::IsLoginScene(Wd);   // ★ F7 (дедуп №2, В3): общий признак
	}
	if (!UseOriginalModalPos() || bLogin) { CenterModalContent(E->Widget); return; }
	static const FName kShout(TEXT("SHOUTING_WIN")), kCommon(TEXT("COMMONWIN_MSGBOX")), kSowel(TEXT("SOWELL_SELBOX"));
	if (WindowId == kShout || WindowId == kCommon)
	{
		// 1580 «ц50/ц70», 8000 «ц50/ц50»: кода-постановки у оригинала нет — окно стоит по разметке, которую несёт WBP.
		UE_LOG(LogAsdaWindow, Log, TEXT("[модаль] «%s»: позиция оригинала — разметка .JUI (центр порта не применяется)"), *WindowId.ToString());
		return;
	}
	if (WindowId == kSowel)
	{
		// 1530: Origin := левый-верх 1500 (ui_core.c:55959); якорь .JUI «5 0 0» ничего не добавляет.
		PlaceWindowKeepingSavedOffset(WindowId, kSowel, nullptr,
			[](const FVector2D& V, const FVector2D&) { return MsgBox1500TopLeft(V); });
		// ★ 08.10.2026 (пакет 3, МЕЛК-1): постановка абсолютна — при смене размера вьюпорта повторить (этой же функцией:
		//   она же учтёт снятый ключ отката).
		SetWindowRePlacer(WindowId, [this, WindowId]() { PlaceModalByOriginal(WindowId); });
		return;
	}
	if (WindowId == kNumPadWindowId) { return; }   // 1410 ставит `ShowNumPad` (у курсора / режим 6 — центр)
	CenterModalContent(E->Widget);                  // вне таблицы: центр (1500/1520/1552 — и у оригинала центр; окна порта)
}

// Разбор — у объявления (пакет 3, МЕЛК-1).
void UAsdaWindowSubsystem::SetWindowRePlacer(FName WindowId, TFunction<void()> Placer)
{
	// Новая постановка = окно уже стоит под текущий экран: отложенная до показа (пакет 5, СРЕДН-1) не нужна.
	if (FAsdaWindowEntry* E = Windows.Find(WindowId)) { E->RePlacer = MoveTemp(Placer); E->bRePlaceOnShow = false; }
}

// Ключ отката целых пикселей у окон с процентным якорем — разбор у `SnapPercentAnchors` в заголовке.
static ASDA_DEV_CVAR(int32, CVarPctAnchorTrunc,
	TEXT("asda.UI.PctAnchorTrunc"), 1,
	TEXT("1 — окна с процентным якорем .JUI стоят в целых пикселях по формуле оригинала (ftol, float; 08.10.2026); ")
	TEXT("0 — точная арифметика WBP (дробные позиции, ±1 px от оригинала)."),
	ECVF_Default);

void UAsdaWindowSubsystem::SnapPercentAnchors(FName WindowId)
{
	FAsdaWindowEntry* E = Windows.Find(WindowId);
	if (!E || !E->Widget || !E->Widget->WidgetTree) { return; }
	// 1. Снять прежние поправки (только у слотов, чей анкор не сменил чужой код) — шаг идемпотентен.
	for (const FAsdaPctSnapItem& It : E->PctSnap)
	{
		UCanvasPanelSlot* S = It.Slot.Get();
		if (!S) { continue; }
		const FAnchors A = S->GetAnchors();
		if (!A.Minimum.Equals(A.Maximum) || !A.Minimum.Equals(It.Anchor)) { continue; }
		FMargin Off = S->GetOffsets();
		Off.Left -= It.Delta.X; Off.Top -= It.Delta.Y;
		S->SetOffsets(Off);
	}
	E->PctSnap.Reset();
	if (CVarPctAnchorTrunc.GetValueOnGameThread() == 0) { return; }
	if (const UWorld* Wd = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		if (AsdaWorldKind::IsLoginScene(Wd)) { return; }   // «Login не трогать»; ★ F7 (дедуп №2, В3): общий признак
	}
	UCanvasPanel* RootCv = Cast<UCanvasPanel>(E->Widget->WidgetTree->RootWidget);
	if (!RootCv) { return; }
	const FVector2D View = GetViewportCanvasSize();
	const int32 ViewW = FMath::RoundToInt32(View.X), ViewH = FMath::RoundToInt32(View.Y);   // экран менеджера — целые
	// Формула оригинала по одной оси: ftol(a · float(экран · 0.0099999998) + Origin) − (w >> 1) — сумма в float (FPU одинарной
	//   точности), усечение ftol к нулю ПОСЛЕ сложения с Origin. ★ 08.10.2026 (пакет 3, МЕЛК-3): прежде порт усекал только
	//   произведение и прибавлял Origin целым — при отрицательной сумме ftol усекает к нулю, и выходило +1 px.
	auto OrigAxis = [](int32 Pct, int32 Screen, int32 Origin, int32 Half) -> int32
	{
		const float Scale = (float)((double)Screen * 0.0099999998);
		const float V = (float)Pct * Scale + (float)Origin;
		return (int32)V - Half;   // ftol: усечение к нулю
	};
	// Округление «половина вверх» постоянной части (Origin + относительное смещение − w/2.0 у WBP ↔ − (w >> 1) у оригинала).
	auto RoundHalfUp = [](double X) { return FMath::FloorToDouble(X + 0.5 + 1e-3); };
	int32 NumSnapped = 0;
	FVector2D MaxAbs(0.0, 0.0);
	for (int32 i = 0; i < RootCv->GetChildrenCount(); ++i)
	{
		UWidget* Ch = RootCv->GetChildAt(i);
		UCanvasPanelSlot* S = Ch ? AsdaUi::SlotOf(Ch) : nullptr;
		if (!S) { continue; }
		const FAnchors A = S->GetAnchors();
		if (!A.Minimum.Equals(A.Maximum)) { continue; }                 // растянутый контрол — не трогаем
		const FVector2D Fx = A.Minimum;
		// Процентный якорь .JUI — анкор строго внутри (0;1) и равный целому проценту (импортёр: a/100).
		auto IsPct = [](double F, int32& OutPct)
		{
			if (F <= 0.0 || F >= 1.0) { return false; }
			OutPct = FMath::RoundToInt32(F * 100.0);
			return FMath::Abs(F * 100.0 - OutPct) < 0.01;
		};
		int32 PctX = 0, PctY = 0;
		const bool bX = IsPct(Fx.X, PctX), bY = IsPct(Fx.Y, PctY);
		if (!bX && !bY) { continue; }
		const FVector2D Al = S->GetAlignment();
		FMargin Off = S->GetOffsets();
		const FVector2D Size = S->GetAutoSize() ? FVector2D::ZeroVector : FVector2D(Off.Right, Off.Bottom);
		if (S->GetAutoSize() && !Al.IsNearlyZero()) { continue; }     // авторазмер с выравниванием — размер неизвестен
		FVector2D Delta(0.0, 0.0);
		// Origin оригинала = RoundHalfUp(C) + (w >> 1): WBP вычитает w/2.0, оригинал — w >> 1 (w — ширина/высота слота, целая).
		const int32 HalfW = FMath::RoundToInt32(Size.X) >> 1, HalfH = FMath::RoundToInt32(Size.Y) >> 1;   // w >> 1, h >> 1
		if (bX)
		{
			const double C = Off.Left - Al.X * Size.X;                  // постоянная часть позиции (без доли экрана)
			const int32 Origin = (int32)RoundHalfUp(C) + HalfW;         // Origin.x слота у оригинала
			Delta.X = (double)OrigAxis(PctX, ViewW, Origin, HalfW) - (Fx.X * ViewW + C);
		}
		if (bY)
		{
			const double C = Off.Top - Al.Y * Size.Y;
			const int32 Origin = (int32)RoundHalfUp(C) + HalfH;         // Origin.y слота у оригинала
			Delta.Y = (double)OrigAxis(PctY, ViewH, Origin, HalfH) - (Fx.Y * ViewH + C);
		}
		if (Delta.IsNearlyZero(1e-4)) { continue; }
		Off.Left += Delta.X; Off.Top += Delta.Y;
		S->SetOffsets(Off);
		E->PctSnap.Add({ S, Fx, Delta });
		++NumSnapped;
		MaxAbs = FVector2D(FMath::Max(MaxAbs.X, FMath::Abs(Delta.X)), FMath::Max(MaxAbs.Y, FMath::Abs(Delta.Y)));
	}
	if (NumSnapped > 0)
	{
		UE_LOG(LogAsdaWindow, Log, TEXT("[раскладка окон] «%s»: целые пиксели «ц%%» как у оригинала — слотов %d, наибольшая поправка %.2f/%.2f при экране %dx%d"),
			*WindowId.ToString(), NumSnapped, MaxAbs.X, MaxAbs.Y, ViewW, ViewH);
	}
}

void UAsdaWindowSubsystem::CenterWindowOnScreen(UUserWidget* W, UWidget* RefWidget)
{
	// ★ Топ-контролов в WBP может быть НЕСКОЛЬКО (рамка/текст/кнопки/гадж плоско в корневой канве) — центрировать
	//   каждый по своему размеру нельзя (сваливаются в кучу). Сдвигаем ВСЕ point-анкор контролы ОДНИМ общим
	//   смещением: общий bounding-box → центр экрана, взаимная раскладка сохраняется. Расчёт в дизайн-базе
	//   1024×768 (designX = fx·1024 + offL − alignX·w) — повторный вызов даёт те же значения (идемпотентно).
	//   Растянутые (span) контролы не трогаем.
	if (!W || !W->WidgetTree) { return; }
	UCanvasPanel* RootCv = Cast<UCanvasPanel>(W->WidgetTree->RootWidget);
	if (!RootCv) { return; }
	struct FCenterItem { UCanvasPanelSlot* S; float L, T, Wd, Ht; FVector2D Align; };
	TArray<FCenterItem> Items;
	FVector2D BbMin(FLT_MAX, FLT_MAX), BbMax(-FLT_MAX, -FLT_MAX);
	bool bRefFound = false;
	FVector2D RefC(0.f, 0.f);
	for (int32 i = 0; i < RootCv->GetChildrenCount(); ++i)
	{
		UWidget* Ch = RootCv->GetChildAt(i);
		UCanvasPanelSlot* S = Ch ? AsdaUi::SlotOf(Ch) : nullptr;
		if (!S) { continue; }
		const FAnchors A = S->GetAnchors();
		if (!A.Minimum.Equals(A.Maximum)) { continue; }   // растянутый контрол (фон и т.п.) — пропуск
		const FMargin Off = S->GetOffsets();
		const float Wd = Off.Right, Ht = Off.Bottom;      // point-анкор: Right/Bottom = px-размер
		if (Wd <= 0.f || Ht <= 0.f) { continue; }         // авторазмер — не участвует
		const FVector2D Align = S->GetAlignment();
		const float L = A.Minimum.X * 1024.f + Off.Left - Align.X * Wd;   // визуальный left в дизайн-базе
		const float T = A.Minimum.Y * 768.f  + Off.Top  - Align.Y * Ht;
		Items.Add({ S, L, T, Wd, Ht, Align });
		BbMin.X = FMath::Min(BbMin.X, L);      BbMin.Y = FMath::Min(BbMin.Y, T);
		BbMax.X = FMath::Max(BbMax.X, L + Wd); BbMax.Y = FMath::Max(BbMax.Y, T + Ht);
		if (RefWidget && Ch == RefWidget) { bRefFound = true; RefC = FVector2D(L + Wd * 0.5f, T + Ht * 0.5f); }
	}
	if (Items.IsEmpty()) { return; }
	// Центр, который совместится с центром экрана: рамка окна (если её передали) либо общий bbox.
	const FVector2D C = bRefFound ? RefC : (BbMin + BbMax) * 0.5f;
	for (const FCenterItem& It : Items)
	{
		It.S->SetAnchors(FAnchors(0.5f, 0.5f));
		// alignment сохраняем; offsets так, чтобы визуальная позиция = центр экрана + (позиция − центр bbox)
		It.S->SetOffsets(FMargin(It.L - C.X + It.Align.X * It.Wd, It.T - C.Y + It.Align.Y * It.Ht, It.Wd, It.Ht));
	}
}

void UAsdaWindowSubsystem::CenterWindowKeepingSavedOffset(FName WindowId, UWidget* RefWidget)
{
	// Разбор — у объявления в `.h`.
	FAsdaWindowEntry* E = Windows.Find(WindowId);
	if (!E || !E->Widget) { return; }
	// 1. СНЯТЬ уже применённые сдвиги. Без этого шага центрирование приняло бы их за часть
	//    дизайн-позиции и затёрло — ровно то, от чего предостерегает шапка `HasSavedWindowOffset`.
	const TMap<FName, FVector2D> Applied = E->AppliedOffsets;
	for (const TPair<FName, FVector2D>& Kv : Applied) { ShiftTargetByKey(WindowId, Kv.Key, -Kv.Value); }
	// 2. Центр — общая точка, второй формулы не заводим.
	CenterWindowOnScreen(E->Widget, RefWidget);
	// 3. ВЕРНУТЬ сдвиги ПОВЕРХ центра. Теперь «дизайн-позиция» окна = центр экрана, а сохранённое
	//    пользователем смещение отсчитывается от него — как и накапливал его `AccumulateOffset`.
	for (const TPair<FName, FVector2D>& Kv : Applied) { ShiftTargetByKey(WindowId, Kv.Key, Kv.Value); }
	if (Applied.Num() > 0)
	{
		for (const TPair<FName, FVector2D>& Kv : Applied) { E->ClampPendingKeys.Add(Kv.Key); }   // М-2: поключево
		ClearClampWatch(*E);                // заявка заново: прежние подписки «ждём показа» не нужны
		ClampWindowBySlotsNow(WindowId);    // М-1: в экран ДО отрисовки — по слотам
		LiveClampNotBeforeFrame = FMath::Max(LiveClampNotBeforeFrame, GFrameCounter + 1);   // слоты сдвинуты — геометрия старая
		PendingClamp.Add(WindowId);   // живая проверка — по живой геометрии, см. TickLayoutRestore
	}
	UE_LOG(LogAsdaWindow, Log,
		TEXT("[раскладка окон] «%s»: дизайн-позиция = ЦЕНТР ЭКРАНА, сохранённых сдвигов сохранено %d"),
		*WindowId.ToString(), Applied.Num());
}

// Ключ отката дефолтных позиций оригинала — разбор у `UseOriginalDefaultPos` в заголовке.
static ASDA_DEV_CVAR(int32, CVarJuiDefaultPos,
	TEXT("asda.UI.JuiDefaultPos"), 1,
	TEXT("1 — окна стоят в дефолте оригинала (.JUI и постановки кодом оригинала, 08.10.2026); 0 — прежние постановки порта ")
	TEXT("(центр у 900/8300/69400/69500/750, без постановки у 9100/16000/1300, у курсора у 8100)."),
	ECVF_Default);

bool UAsdaWindowSubsystem::UseOriginalDefaultPos()
{
	return CVarJuiDefaultPos.GetValueOnGameThread() != 0;
}

FVector2D UAsdaWindowSubsystem::GetViewportCanvasSize() const
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	UGameViewportClient* GVC = World ? World->GetGameViewport() : nullptr;
	if (!GVC) { return FVector2D(1024.0, 768.0); }
	FVector2D Px;                                                        // вьюпорт в пикселях
	GVC->GetViewportSize(Px);
	float Scale = UWidgetLayoutLibrary::GetViewportScale(GVC);
	if (Scale <= KINDA_SMALL_NUMBER) { Scale = 1.f; }
	return (Px.X > 0.0 && Px.Y > 0.0) ? Px / Scale : FVector2D(1024.0, 768.0);
}

void UAsdaWindowSubsystem::PlaceWindowKeepingSavedOffset(FName WindowId, FName FrameIdc, UWidget* RefWidget,
	TFunctionRef<FVector2D(const FVector2D& ViewSize, const FVector2D& FrameSize)> TopLeftOf)
{
	// Разбор — у объявления в `.h`.
	FAsdaWindowEntry* E = Windows.Find(WindowId);
	if (!E || !E->Widget || !E->Widget->WidgetTree) { return; }
	UCanvasPanel* RootCv = Cast<UCanvasPanel>(E->Widget->WidgetTree->RootWidget);
	if (!RootCv) { return; }
	// 1. Снять сдвиги игрока — постановка задаёт ДИЗАЙН-позицию (как у `CenterWindowKeepingSavedOffset`).
	const TMap<FName, FVector2D> Applied = E->AppliedOffsets;
	for (const TPair<FName, FVector2D>& Kv : Applied) { ShiftTargetByKey(WindowId, Kv.Key, -Kv.Value); }
	// 2. Текущие прямоугольники детей корня на ЭТОМ экране (формула точечного анкора канваса).
	const FVector2D View = GetViewportCanvasSize();
	struct FPlaceItem { UCanvasPanelSlot* S; FVector2D Pos, Size, Align; };
	TArray<FPlaceItem> Items;
	const double Big = TNumericLimits<float>::Max();
	FVector2D BbMn(Big, Big), BbMx(-Big, -Big), FrMn(Big, Big), FrMx(-Big, -Big), RfMn, RfMx;
	bool bFrame = false, bRef = false;
	const FString FrameStr = FrameIdc.ToString();
	for (int32 i = 0; i < RootCv->GetChildrenCount(); ++i)
	{
		UWidget* Ch = RootCv->GetChildAt(i);
		UCanvasPanelSlot* S = Ch ? AsdaUi::SlotOf(Ch) : nullptr;
		if (!S) { continue; }
		const FAnchors A = S->GetAnchors();
		if (!A.Minimum.Equals(A.Maximum)) { continue; }                 // растянутый контрол — не трогаем
		const FMargin Off = S->GetOffsets();
		const FVector2D Size(Off.Right, Off.Bottom);
		if (Size.X <= 0.0 || Size.Y <= 0.0) { continue; }               // авторазмер — не участвует
		const FVector2D Al = S->GetAlignment();
		const FVector2D Pos(A.Minimum.X * View.X + Off.Left - Al.X * Size.X, A.Minimum.Y * View.Y + Off.Top - Al.Y * Size.Y);
		Items.Add({ S, Pos, Size, Al });
		BbMn = FVector2D(FMath::Min(BbMn.X, Pos.X), FMath::Min(BbMn.Y, Pos.Y));
		BbMx = FVector2D(FMath::Max(BbMx.X, Pos.X + Size.X), FMath::Max(BbMx.Y, Pos.Y + Size.Y));
		FString P; int32 J = 0, Pl = 0;
		const FString Nm = Ch->GetName();
		// Рамка — планы корневого контрола или его хит-зона `<Ключ>_<idx>_HIT` (окно без скина корня, EXTRACT_WIN).
		const bool bHit = !FrameIdc.IsNone() && Nm.StartsWith(FrameStr + TEXT("_")) && Nm.EndsWith(TEXT("_HIT"));
		if (bHit || (!FrameIdc.IsNone() && AsdaUi::ParsePlaneName(Nm, P, J, Pl) && P == FrameStr))
		{
			FrMn = FVector2D(FMath::Min(FrMn.X, Pos.X), FMath::Min(FrMn.Y, Pos.Y));
			FrMx = FVector2D(FMath::Max(FrMx.X, Pos.X + Size.X), FMath::Max(FrMx.Y, Pos.Y + Size.Y));
			bFrame = true;
		}
		if (RefWidget && Ch == RefWidget) { RfMn = Pos; RfMx = Pos + Size; bRef = true; }
	}
	if (Items.IsEmpty())
	{
		for (const TPair<FName, FVector2D>& Kv : Applied) { ShiftTargetByKey(WindowId, Kv.Key, Kv.Value); }
		return;
	}
	const FVector2D RMn = bFrame ? FrMn : (bRef ? RfMn : BbMn);
	const FVector2D RMx = bFrame ? FrMx : (bRef ? RfMx : BbMx);
	const FVector2D TL = TopLeftOf(View, RMx - RMn);
	// 3. Анкор — левый-верх экрана, смещение = точка постановки + (позиция детали − левый-верх рамки).
	for (const FPlaceItem& It : Items)
	{
		It.S->SetAnchors(FAnchors(0.f, 0.f));
		It.S->SetOffsets(FMargin(TL.X + (It.Pos.X - RMn.X) + It.Align.X * It.Size.X,
			TL.Y + (It.Pos.Y - RMn.Y) + It.Align.Y * It.Size.Y, It.Size.X, It.Size.Y));
	}
	// 4. Вернуть сдвиги игрока поверх и заявить кламп (правило «за экраном → дефолт»).
	for (const TPair<FName, FVector2D>& Kv : Applied) { ShiftTargetByKey(WindowId, Kv.Key, Kv.Value); }
	if (Applied.Num() > 0)
	{
		for (const TPair<FName, FVector2D>& Kv : Applied) { E->ClampPendingKeys.Add(Kv.Key); }
		ClearClampWatch(*E);
		ClampWindowBySlotsNow(WindowId);
		LiveClampNotBeforeFrame = FMath::Max(LiveClampNotBeforeFrame, GFrameCounter + 1);
		PendingClamp.Add(WindowId);
	}
	UE_LOG(LogAsdaWindow, Log,
		TEXT("[раскладка окон] «%s»: постановка оригинала — рамка %s %.0fx%.0f в (%.0f,%.0f) при экране %.0fx%.0f, сдвигов игрока поверх %d"),
		*WindowId.ToString(), bFrame ? *FrameStr : (bRef ? TEXT("(опорный контрол)") : TEXT("(габарит)")),
		RMx.X - RMn.X, RMx.Y - RMn.Y, TL.X, TL.Y, View.X, View.Y, Applied.Num());
}

// ★★★ 10.10.2026 ЗАМОК ВВОДА МОДАЛЬНЫМ КОНТРОЛОМ ЭТАЛОНА (решение владельца, отчёт `Reports/owner_1010/modal_lock.md`).
//   Эталон: `helper_setupFields` (thunk 0x924FA9 → `sub_12AFBB0`, ui_core.c:130269) ставит отложенный модальный контрол
//   `CWindowMgr+0x1B4`, флаг ожидания `+0x1B2` и флаг `+0x200 = 1`; в конце кадра `CControl_DispatchMessage` переносит
//   контрол в `+0x1A0`. Флаг `+0x200` читает `get_o200@0x9864A0` (`sub_930E71`) в девяти местах: KeyUp/KeyDown раскладки и
//   Alt-сочетания (`MainWndProc`, game_main.c:6140/6223/6300/6316), Enter → чат (game_main.c:9357), ESC-меню (input.c:1731),
//   слот хотбара (ui_core.c:67351), системное меню 73401…73410 (ui_core.c:79464/79528). `+0x1A0` отдаёт мышь только модалке
//   (ui_core.c:104149/104238/104315). Снимает оба поля `sub_12B2900` (thunk `sub_92E450`).
//   Порт держит модальность флагом `bModal` (полоса z 20000); окна, которые эталон показывает модально, а порт — нет
//   (1559, 74200), заведены росписью `EscModals`. Ключ диагностики ниже снимает замок для них (не для `bModal`).
//   ★★ Поправка 10.10.2026 (modal_lock2, СРЕДН-1 ревизии `modal_lock_REVIEW.md`): «мышь только модалке» у эталона — это ЛКМ,
//   перемещение и клавиатура UI. КОЛЕСО под модалкой эталон не запирает: WM_MOUSEWHEEL (game_main.c:5864) кладёт колесо в
//   очередь окон (там его получает только `+0x1A0`) и зовёт `Camera_MouseWheelZoom`, если курсор не над окном
//   (`!CControl_HitTest(g_pWindowMgr)`), без `get_o200`. Порт так же — для ВСЕХ модалок (`bModal` и росписи): колесо в мир
//   пересылает препроцессор контроллера (`AAsdaPlayerController::OnWheel`). ПКМ эталон тоже не запирает (104315-104333 без
//   `return`), но это дефект — порт ПКМ держит запертым [ИСПРАВЛЕНИЕ], откат `asda.UI.ModalLockRightClick 0`.
static ASDA_DEV_CVAR(int32, CVarModalLock,
	TEXT("asda.UI.ModalLock"), 1,
	TEXT("1 — модальный контрол эталона без флага bModal порта (общая модалка покупки 1559, окно регистрации рыбы 74200) запирает ")
	TEXT("ввод как оригинал (флаг CWindowMgr+0x200: клавиши раскладки, Alt, Enter, хотбар, системное меню, сумка; мышь — только ему); ")
	TEXT("0 — диагностика: запираются только проход ESC и ESC-меню (как до 10.10.2026)."),
	ECVF_Default);

bool UAsdaWindowSubsystem::IsAnyModalOpen() const
{
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		const FAsdaWindowEntry& E = Kv.Value;
		if (E.bModal && AsdaWindowIsEntryVisible(E))
		{
			return true;
		}
	}
	// ★ 10.10.2026: модальный контрол эталона без `bModal` (тот же флаг `+0x200`).
	FName Owner;
	int32 Z = 0;
	return FindTopOrigModal(Owner, Z);
}

bool UAsdaWindowSubsystem::IsOrigModalShown(const FOrigModal& M) const
{
	const FAsdaWindowEntry* E = Windows.Find(M.Owner);
	if (!E || !E->Widget || !AsdaWindowIsEntryVisible(*E)) { return false; }
	return !M.IsShown || M.IsShown();
}

bool UAsdaWindowSubsystem::FindTopOrigModal(FName& OutOwner, int32& OutZ) const
{
	OutOwner = NAME_None;
	OutZ = TNumericLimits<int32>::Lowest();
	if (CVarModalLock.GetValueOnGameThread() == 0) { return false; }
	for (const TPair<FName, FOrigModal>& Kv : EscModals)
	{
		if (!IsOrigModalShown(Kv.Value)) { continue; }
		const FAsdaWindowEntry* E = Windows.Find(Kv.Value.Owner);
		// Ничья по z разбирается по имени — выбор и лог воспроизводимы (порядок обхода TMap не определён).
		if (E && (E->ZOrder > OutZ || (E->ZOrder == OutZ && Kv.Value.Owner.Compare(OutOwner) > 0)))
		{
			OutZ = E->ZOrder;
			OutOwner = Kv.Value.Owner;
		}
	}
	return !OutOwner.IsNone();
}

bool UAsdaWindowSubsystem::PassesModalMouseGate(FName WindowId, const FAsdaWindowEntry& E) const
{
	// Окно `bModal` показано — прежнее правило: мышь получают только окна полосы модалок.
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		if (Kv.Value.bModal && AsdaWindowIsEntryVisible(Kv.Value)) { return E.ZOrder >= kModalZ; }
	}
	FName Owner;
	int32 Z = 0;
	if (!FindTopOrigModal(Owner, Z)) { return true; }
	// Модалка без `bModal`: мышь получает только её корень (эталон `+0x1A0`), а не всё, что лежит выше по z.
	return WindowId == Owner || E.ZOrder >= kModalZ;
}

bool UAsdaWindowSubsystem::PassesModalMouseGateAt(FName WindowId, const FAsdaWindowEntry& E, FVector2D Pt) const
{
	if (!PassesModalMouseGate(WindowId, E)) { return false; }
	// Полоса `bModal` показана — решает прежнее правило (корень росписи там мыши не получает).
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		if (Kv.Value.bModal && AsdaWindowIsEntryVisible(Kv.Value)) { return true; }
	}
	FName Owner;
	int32 Z = 0;
	if (!FindTopOrigModal(Owner, Z) || Owner != WindowId) { return true; }
	// Корень модалки: если модальный контрол — УЗЕЛ внутри него (роспись с `Node`), мышь получает только прямоугольник узла.
	bool bNested = false;   // нашлась показанная вложенная роспись этого корня с виджетом узла
	for (const TPair<FName, FOrigModal>& Kv : EscModals)
	{
		const FOrigModal& M = Kv.Value;
		if (M.Owner != WindowId || !M.IsShown || !M.Node || !IsOrigModalShown(M)) { continue; }
		bNested = true;
		const UWidget* N = M.Node();
		FVector2D Mn, Mx;
		if (N && WidgetAbsRect(N, Mn, Mx) && Pt.X >= Mn.X && Pt.X <= Mx.X && Pt.Y >= Mn.Y && Pt.Y <= Mx.Y) { return true; }
	}
	return !bNested;
}

// Ключ отката [ИСПРАВЛЕНИЯ] «ПКМ под модалкой заперт» (разбор — в объявлении `ModalRightClickReachesWorld`).
static ASDA_DEV_CVAR(int32, CVarModalLockRightClick,
	TEXT("asda.UI.ModalLockRightClick"), 1,
	TEXT("1 — [ИСПРАВЛЕНИЕ] правая кнопка мыши под модальным окном заперта и для мира (вращение камеры, пик, меню по игроку); ")
	TEXT("0 — как оригинал (WM_RBUTTONDOWN/UP без гейта get_o200): ПКМ доходит до мира. Окнам под модалкой ПКМ, наведение и ")
	TEXT("подсказки не доходят при любом значении — как у оригинала (нажатие и перемещение отдаются только модалке +0x1A0)."),
	ECVF_Default);

bool UAsdaWindowSubsystem::ModalRightClickReachesWorld() const
{
	return CVarModalLockRightClick.GetValueOnGameThread() == 0;
}

bool UAsdaWindowSubsystem::IsWorldInputCapturedByModal() const
{
	// По факту (МЕЛК-3): мир, видимая модалка и вьюпорт, который сейчас действительно не отдаёт ввод контроллеру.
	if (!bBaseInputGameAndUI || !GEngine || !GEngine->GameViewport) { return false; }
	return GEngine->GameViewport->IgnoreInput() && GetTopModalWindow() != nullptr;
}

FName UAsdaWindowSubsystem::GetOriginalModalOwner() const
{
	// Поверх стоит окно `bModal` (например, нумпад над окном регистрации) — ввод отдан ему, а не корню росписи.
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		if (Kv.Value.bModal && AsdaWindowIsEntryVisible(Kv.Value)) { return NAME_None; }
	}
	FName Owner;
	int32 Z = 0;
	return FindTopOrigModal(Owner, Z) ? Owner : NAME_None;
}

UUserWidget* UAsdaWindowSubsystem::GetTopModalWindow() const
{
	UUserWidget* Top = nullptr;
	int32 TopZ = TNumericLimits<int32>::Lowest();
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		const FAsdaWindowEntry& E = Kv.Value;
		if (E.bModal && AsdaUi::IsShown(E.Widget) && E.ZOrder >= TopZ)   // ★ волна В6 ремесла (МЕЛК-8): общая точка видимости узла
		{
			TopZ = E.ZOrder;
			Top = E.Widget;
		}
	}
	// ★ 10.10.2026: окон `bModal` нет — верхний модальный контрол эталона без `bModal` (его корень).
	if (!Top)
	{
		FName Owner;
		int32 Z = 0;
		if (FindTopOrigModal(Owner, Z)) { Top = FindWindow(Owner); }
	}
	return Top;
}

// ══════ ★★★ ESC ЗАКРЫВАЕТ ВЕРХНЕЕ ОТКРЫТОЕ ОКНО — порт `uiContainerHandleKey@0x12AF390` ══════
//   Полный разбор эталонного прохода (порядок обхода, «ровно одно окно за нажатие», что делает
//   событие 21, почему проход идёт ДО гейтов) — в шапке объявления `CloseTopmostOnEsc` в
//   `Public/UI/AsdaWindowSubsystem.h`. Здесь — данные оригинала и код.

// Ключ отката владельца: 0 — прохода нет, ESC ведёт себя как до 17.09.2026.
static ASDA_DEV_CVAR(int32, CVarEscCloseTopWindow,
	TEXT("asda.UI.EscCloseTopWindow"), 1,
	TEXT("1 — ESC сначала закрывает ВЕРХНЕЕ открытое окно (эталон uiContainerHandleKey@0x12AF390) ")
	TEXT("и только если закрывать нечего, открывает ESC-меню 69400; 0 — прохода нет (откат)."),
	ECVF_Default);

// Одна строка эталонной таблицы «окно закрывается по ESC».
struct FAsdaEscCloseWindow
{
	int32 IdNum;            // IDNum корневого контрола в .JUI
	const TCHAR* WindowId;  // имя контрола = ключ реестра окон порта
	const TCHAR* Origin;    // эталонная функция инициализации, которая поставила признак
};

// ★★★ ДАННЫЕ ОРИГИНАЛА, А НЕ НАШ ВЫБОР. Здесь все 61 вызов обёртки
//   `helper_setByteField552@0x91F8CE` (→ `set_o228@0xB61180`, поле `CControl+0x228`), снятые с
//   ОБОИХ срезов декомпила (`RE/ida/export/AsdaStory_game.c` и `AsdaStory_ida.c` — списки
//   совпали строка в строку); значение всегда 1, снимающих вызовов нет. IDNum взят из
//   присваивания ТОГО ЖЕ поля `CWindowMgr_FindControlByID(g_pWindowMgr, <IDNum>)` в той же
//   функции инициализации, имя окна — из данных `.JUI` (`Data/Decoded/jui/gamemain_jui.ini`,
//   ключ `IDNum` → `ID`). Порядок строк = порядок адресов эталонных функций (воспроизводимость).
//   ★ `BOOSTER_ITEM_SEL` (15496) — ЕДИНСТВЕННЫЙ вложенный контрол в списке (в .JUI он лежит
//   внутри `BOOSTER_SHOP_WIN`), а проход обходит только КОРНЕВЫЕ окна ⇒ в оригинале его признак
//   недостижим. Строку держим ради полноты данных, поведения она не даёт ни там, ни у нас.
static const FAsdaEscCloseWindow kEscCloseWindows[] =
{
	{ 50300, TEXT("ItemOptionValue_WIN"),    TEXT("itemUpgradeWindowInitControls@0xB60BC0") },
	{ 50500, TEXT("Strength_Part_Win"),      TEXT("clanWindowInitControls@0xB64A40") },
	{ 50100, TEXT("EventDungeon_Win"),       TEXT("uiSlotEnchantDialogInit@0xB68B40") },
	{ 68000, TEXT("Premium_PickBAR_Win"),    TEXT("premiumGatya_InitWindow@0xB6CB50") },
	{ 69200, TEXT("Tutorial_WIN"),           TEXT("tutorialUiInitControls@0xB77D10") },
	{ 19000, TEXT("My_BLOG_Win"),            TEXT("CashCharge_InitWindow@0xB7D920") },
	{ 15300, TEXT("BOOSTER_SHOP_WIN"),       TEXT("BoosterShop_InitWindow@0xB7E4A0") },
	{ 15496, TEXT("BOOSTER_ITEM_SEL"),       TEXT("BoosterShop_InitWindow@0xB7E4A0") },
	{  2670, TEXT("CHAT_SETUP_Win"),         TEXT("CChat_BuildTabUI@0xB9E490") },
	{  6400, TEXT("COMMUNITY_WIN"),          TEXT("Community_BuildUI@0xBD38E0") },
	{  6000, TEXT("SHELLITEM_STORE_WIN"),    TEXT("CShellItemStore_InitControls@0xC28820") },
	{  6800, TEXT("SHELLITEM_STORE2_WIN"),   TEXT("CShellItemStore_InitControls@0xC28820") },
	{   750, TEXT("QUEST_Confirm_Win"),      TEXT("CQuest_Episode_BuildUI@0xC5A950") },
	{   700, TEXT("QUEST_WIN"),              TEXT("CQuest_Episode_BuildUI@0xC5A950") },
	{ 15200, TEXT("ITEM_SWAP_WIN"),          TEXT("CExchangeMacExternalItemWnd_Init@0xC87110") },
	{ 16000, TEXT("EXTRACT_WIN"),            TEXT("CExtractShop_InitUI@0xC8D040") },
	{ 44000, TEXT("CAMPWAR_Win"),            TEXT("CCampWar_InitSelectWindow@0xC971A0") },
	{ 47000, TEXT("CAMPWAR_Join"),           TEXT("CCampWar_InitJoinInfo@0xC97890") },
	{ 49399, TEXT("CAMPWAR_ATTACK"),         TEXT("CCampWarUI_InitAttackListControls@0xC99070") },
	{ 15000, TEXT("HAIRSHOP_WIN"),           TEXT("boosterShopWindowReset@0xCEE440 (из CUIHairShop_InitControls@0xCEC1B0)") },
	{   400, TEXT("INVEN_WIN"),              TEXT("InventoryWindow_InitControls@0xCFC8F0") },
	{ 20000, TEXT("Item_Upgrade_Win"),       TEXT("CItemUpgradeWin_InitControls@0xD26860") },
	{ 19100, TEXT("Get_UpgradeOtion"),       TEXT("CItemUpgradeWin_InitControls@0xD26860") },
	{ 20200, TEXT("Item_Initializ_Win"),     TEXT("CItemInitializeWin_InitControls@0xD2DCA0") },
	{ 31000, TEXT("Job_Guide_Win"),          TEXT("CJobGuideWin_LoadUI@0xD5D740") },
	{  1710, TEXT("MAXMAP_WIN"),             TEXT("MiniMap_BuildUI@0xD63220") },
	{  1500, TEXT("ONEBUT_MSGBOX"),          TEXT("CMsgBoxMgr_InitControls@0xD6AD20") },
	{  1520, TEXT("TWOBUT_MSGBOX"),          TEXT("CMsgBoxMgr_InitControls@0xD6AD20") },
	{  1530, TEXT("SOWELL_SELBOX"),          TEXT("CMsgBoxMgr_InitControls@0xD6AD20") },
	{  1410, TEXT("NUMPAD_WIN"),             TEXT("CNumPadUI_InitControls@0xDA5370") },
	{  4200, TEXT("OTHERCHARA_DATA"),        TEXT("COtherCharaWnd_InitControls@0xDA6C90") },
	{ 26000, TEXT("PackageShop_Win"),        TEXT("UIPackageStampWnd_Init@0xDAEA20") },
	{ 26500, TEXT("Package_Contents_Win"),   TEXT("UIPackageStampWnd_Init@0xDAEA20") },
	{  8500, TEXT("Find_Party_WIn"),         TEXT("FindPartyWindow_InitControls@0xDB68C0") },
	{ 42000, TEXT("Pet_Hatchery_Win"),       TEXT("petWindowBuildUI@0xDBFF70") },
	{ 43000, TEXT("Pet_Cage_Win"),           TEXT("CUpgradeWindow_Init@0xDC3B90") },
	{ 22000, TEXT("StreetStall_Open_Win"),   TEXT("CSellWindow_InitControls@0xDCEF80") },
	{ 23000, TEXT("StreetStall_Win"),        TEXT("CShopDlg_InitControls@0xDD6610") },
	{ 17000, TEXT("Smelting2_Win"),          TEXT("CRefineShopUI_InitControls@0xDFE890") },
	{ 32000, TEXT("Disassembly_Win"),        TEXT("CDecomposeWindow_InitControls@0xE0D010") },
	{  1300, TEXT("NPCSHOP_WIN"),            TEXT("NPCShop_InitControls@0xE233F0") },
	{ 12000, TEXT("SKILL_WIN"),              TEXT("CSkillWindow_InitControls@0xE50DB0") },
	{ 67000, TEXT("GOLD_roullete_WIN"),      TEXT("CGoldRoulette_InitUI@0xE615E0") },
	{  8200, TEXT("Find_Soulmate_Win"),      TEXT("CFindSoulmateWin_InitControls@0xE71240") },
	{   900, TEXT("CHARAINFO_WIN"),          TEXT("CCharaInfoWin_InitControls@0xE75E70") },
	{   800, TEXT("SYSTEM_WIN"),             TEXT("CSystemOptionUI_InitControls@0xE906E0") },
	{ 18700, TEXT("Title_Win"),              TEXT("CUITitle_InitControls@0xE96430") },
	{ 13700, TEXT("SOWEL_Tooltip"),          TEXT("uiWindow_BindControls@0xEA19C0") },
	{ 18000, TEXT("CashCharge_Win"),         TEXT("CCashChargeWin_InitControls@0xEBD450") },
	{ 74600, TEXT("ETC_NEW_WIN"),            TEXT("quickSlotUI_RefreshIconSlots@0xECD870") },
	{ 69400, TEXT("SYSTEM_MenuWIN_NEW"),     TEXT("uiNoticeDialogInitControls@0xED3080") },
	{ 69500, TEXT("SYSTEM_Option_NEW"),      TEXT("uiNoticeDialogInitControls@0xED3080") },
	{ 57300, TEXT("INVEN_Repair_Win"),       TEXT("uiEquipUpgradePanelInit@0xED9310") },
	{ 43500, TEXT("Pet_Evolution_Win"),      TEXT("uiPetDialogInit@0xEDC7C0") },
	// ★ 10.10.2026 (сверка таблицы со всеми 61 вызовом, `owner_1010/esc_ranks.md`): строки 43400 `Pet_Inven_Win` и
	//   43450 `Pet_Inven_Tooltip` (волна 5) УБРАНЫ — признака у них в эталоне НЕТ. `uiPetDialogInit@0xEDC7C0` ставит
	//   `helper_setByteField552` ТОЛЬКО окну 43500 (ui_core.c:89811); 43400/43450 он лишь находит
	//   `CWindowMgr_FindControlByID` (ui_core.c:89877/89887) — в обоих срезах декомпила (`AsdaStory_ida.c` то же).
	//   Поведение не меняется: росписи у этих окон не было, проход их и так пропускал.
	{ 43300, TEXT("Pet_Tooltips_Win"),       TEXT("uiUpgradeDialogBindControls@0xEE0E30") },
	{ 44200, TEXT("Pet_LimitBreak_Win"),     TEXT("uiInitTradeWindow@0xEE12F0") },
	{ 50600, TEXT("Item_Union_Win"),         TEXT("cfusion_initWindow@0xEE43B0") },
	{ 69000, TEXT("Primium_Item_post_WIN"),  TEXT("uiInitMixWindowGrid@0xEEFE60") },
	{ 71300, TEXT("FISH_WIN"),               TEXT("fishingWindowInitControls@0xFC83E0") },
	{ 74000, TEXT("FISH_BOOK_WIN"),          TEXT("uiWindowInit_74000@0xFD2950") },
	{ 79800, TEXT("Gear_seal_WIN"),          TEXT("uiDialog_BindControls@0xFEF6B0") },
};

// ★★★ 07.11.2026 [ИСПРАВЛЕНИЕ] Окна с крестиком, которым оригинал признак ESC (`CControl+0x228`) не ставит, а игрок ждёт закрытия: жалоба владельца на
//   «Report Monster hunting results» 56450 (`uiSowelWindow_InitControls` признак не ставит, в таблице выше окна нет). Таблица оригинала не меняется:
//   здесь отдельный список, ESC нажимает КРЕСТИК окна, поэтому работает тот же обработчик, что у мыши. Не вошли: панели HUD (чат, карта, отряд, табло боя),
//   обмен 2008, окна решений и гача. Разбор всех окон: Reports/batch13/esc/REPORT.md. Нажатие такое окно тратит целиком, как окна с 0x215.
static ASDA_DEV_CVAR(int32, CVarEscExtraWindows,
	TEXT("asda.UI.EscExtraWindows"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = ESC закрывает и окна с крестиком из списка порта (доклад об охоте 56450 и другие); 0 = только окна таблицы оригинала."),
	ECVF_Default);

// ★ 07.11.2026 [ИСПРАВЛЕНИЕ] Фокус в поле ввода окна из списка: первый ESC только снимает фокус, текст остаётся; следующий ESC нажимает крестик. Иначе крестик
//   закрыл бы окно и несохранённый ввод пропал (окно 8300: поле 8302 при открытии заполняется сохранённым текстом). Оригинал такого шага не имеет: поле шлёт ESC
//   в общий проход (`ctrlEditImeWndProc`), но окна с полем он не закрывает, поэтому текст там тоже цел.
static ASDA_DEV_CVAR(int32, CVarEscExtraFieldFirst,
	TEXT("asda.UI.EscExtraFieldFirst"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = при фокусе в поле окна из списка порта первый ESC снимает фокус, текст остаётся; 0 = ESC сразу нажимает крестик."),
	ECVF_Default);

// Строка списка: окно и его крестик.
struct FAsdaEscExtraWindow
{
	int32 IdNum;              // IDNum корня .JUI
	const TCHAR* WindowId;    // ключ реестра окон
	const TCHAR* CloseBtn;    // имя кнопки-крестика в окне
};
static const FAsdaEscExtraWindow kEscExtraWindows[] =
{
	{   599, TEXT("QUEST_INFO_WIN"),        TEXT("QUEST_INFO_x") },
	{  2590, TEXT("CHAT_TAB_WIN"),          TEXT("CHAT_TAB_WIN_X") },
	{  2700, TEXT("CHAT_Color_Select_Win"), TEXT("CHAT_Color_Select_X") },
	{  4000, TEXT("WARE_WIN"),              TEXT("WARE_CLOBUT") },
	{  6610, TEXT("Soul_RMsgWin"),          TEXT("Soul_RMsg_CloseBtn") },
	{  6620, TEXT("Soul_RMsgWin2"),         TEXT("Soul_RMsg_CloseBtn2") },
	{  6630, TEXT("Soul_RMsgWin3"),         TEXT("Soul_RMsg_CloseBtn3") },
	{  5000, TEXT("AD_Win"),                TEXT("AD_ClsBtn") },
	{  8300, TEXT("My_Introduction_Win"),   TEXT("My_Intro_ClsBtn") },
	{ 10050, TEXT("BOOSTING_WIN"),          TEXT("BOOSTER_CLOSE") },
	{ 13000, TEXT("EXCHANGE_WIN"),          TEXT("EXCHANGE_TITLE_DEL") },
	{ 15600, TEXT("BOOSTER_PREV_WIN"),      TEXT("BOOSTER_PREV_WIN_CLOSE") },
	{ 28100, TEXT("Opend_Item_Win"),        TEXT("Opend_Item_ClsBtn") },
	{ 43400, TEXT("Pet_Inven_Win"),         TEXT("Pet_Inven_Win_X") },
	{ 49540, TEXT("CAMPWAR_In_List"),       TEXT("CAMPWAR_In_List_X") },
	{ 49800, TEXT("CAMPWAR_End"),           TEXT("CAMPWAR_End_X") },
	{ 56450, TEXT("Hunt_report_Win"),       TEXT("Hunt_report_X") },
	{ 67200, TEXT("HONOR_itemmall_Win"),    TEXT("HONOR_item_X") },
	{ 74500, TEXT("Constellation_S_WIN"),   TEXT("Constellation_S_Xbtn") },
	{ 75000, TEXT("Subjugate_WIN"),         TEXT("Subjugate_xxbtn") },
	{ 76000, TEXT("ITEM_making_WIN"),       TEXT("ITEM_making_XX") },
	{ 77000, TEXT("ITEM_making_R_BOX"),     TEXT("ITEM_making_B_BOX_XX") },
	{ 78200, TEXT("Smelting3_Win"),         TEXT("Smelting3_Xbtn") },
	{ 79300, TEXT("Town_as_WIN"),           TEXT("Town_as_x") },
};

// Строка списка для окна; nullptr — окна нет или ключ выключен.
static const FAsdaEscExtraWindow* AsdaFindEscExtra(FName WindowId)
{
	if (CVarEscExtraWindows.GetValueOnGameThread() == 0) { return nullptr; }
	for (const FAsdaEscExtraWindow& W : kEscExtraWindows)
	{
		if (WindowId == FName(W.WindowId)) { return &W; }
	}
	return nullptr;
}

bool UAsdaWindowSubsystem::IsEscExtraWindow(FName WindowId) const
{
	return AsdaFindEscExtra(WindowId) != nullptr;
}

// Нажать крестик окна из списка: тот же обработчик, что у мыши. false — крестика нет, он скрыт или выключен.
bool UAsdaWindowSubsystem::PressEscExtraClose(FName WindowId)
{
	const FAsdaEscExtraWindow* Row = AsdaFindEscExtra(WindowId);
	UUserWidget* Win = Row ? FindWindow(WindowId) : nullptr;
	UButton* Btn = Win ? Cast<UButton>(Win->GetWidgetFromName(FName(Row->CloseBtn))) : nullptr;
	if (!Btn || !Btn->GetIsEnabled() || !Btn->OnClicked.IsBound()) { return false; }
	if (CVarEscExtraFieldFirst.GetValueOnGameThread() != 0 && Win->WidgetTree && FSlateApplication::IsInitialized())
	{
		TArray<UWidget*> All;
		Win->WidgetTree->GetAllWidgets(All);
		for (UWidget* W : All)
		{
			const bool bField = W && (W->IsA<UEditableText>() || W->IsA<UEditableTextBox>() || W->IsA<UMultiLineEditableText>());
			if (bField && W->HasKeyboardFocus())
			{
				FSlateApplication::Get().SetAllUserFocusToGameViewport();
				UE_LOG(LogAsdaWindow, Log, TEXT("[окна] ESC снял фокус с поля «%s» окна «%s», текст остаётся [ИСПРАВЛЕНИЕ]"), *W->GetName(), *WindowId.ToString());
				return true;
			}
		}
	}
	for (const UWidget* W = Btn; W; W = W->GetParent())
	{
		const ESlateVisibility V = W->GetVisibility();
		if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return false; }
	}
	Btn->OnClicked.Broadcast();
	return true;
}

bool UAsdaWindowSubsystem::IsEscCloseWindow(FName WindowId) const
{
	// Таблица неизменна за время работы — строим набор один раз (поиск по имени идёт на каждое ESC).
	static const TSet<FName> Table = []()
	{
		TSet<FName> S;
		for (const FAsdaEscCloseWindow& W : kEscCloseWindows) { S.Add(FName(W.WindowId)); }
		return S;
	}();
	return Table.Contains(WindowId);
}

void UAsdaWindowSubsystem::RegisterEscCloser(FName WindowId, FSimpleDelegate Closer)
{
	if (WindowId.IsNone()) { return; }
	// Роспись имеет смысл ТОЛЬКО у окна из эталонной таблицы: подписать окно, которого в списке
	//   оригинала нет, — это выдумать поведение, которого у клиента не было (закон проекта).
	if (!IsEscCloseWindow(WindowId))
	{
		UE_LOG(LogAsdaWindow, Warning,
			TEXT("[окна] роспись ESC для «%s» ОТКЛОНЕНА: этого окна нет в эталонной таблице ")
			TEXT("kEscCloseWindows (признак CControl+0x228 у него оригиналом не ставится)"),
			*WindowId.ToString());
		return;
	}
	EscClosers.Add(WindowId, MoveTemp(Closer));
}

void UAsdaWindowSubsystem::UnregisterEscCloser(FName WindowId)
{
	EscClosers.Remove(WindowId);
}

void UAsdaWindowSubsystem::RegisterEscPopup(FName PopupId, FName OwnerWindowId, TFunction<bool()> IsShown)
{
	if (PopupId.IsNone() || OwnerWindowId.IsNone() || !IsShown) { return; }
	// Та же защита, что у росписи-закрывателя: слой вправе участвовать в проходе, только если оригинал ставит ему признак.
	if (!IsEscCloseWindow(PopupId))
	{
		UE_LOG(LogAsdaWindow, Warning,
			TEXT("[окна] всплывающий слой ESC «%s» ОТКЛОНЁН: его нет в эталонной таблице kEscCloseWindows"), *PopupId.ToString());
		return;
	}
	FEscPopup P;
	P.Owner = OwnerWindowId;
	P.IsShown = MoveTemp(IsShown);
	EscPopups.Add(PopupId, MoveTemp(P));
}

void UAsdaWindowSubsystem::UnregisterEscPopup(FName PopupId)
{
	EscPopups.Remove(PopupId);
}

// ★ 10.10.2026 [ОРИГИНАЛ] Роспись «эталон показывает это окно модально» (разбор — у объявления).
void UAsdaWindowSubsystem::RegisterEscModal(FName WindowId)
{
	if (WindowId.IsNone()) { return; }
	// Корневая роспись: владелец — само окно, видимость — видимость окна (пустая лямбда).
	FOrigModal M;
	M.Owner = WindowId;
	EscModals.Add(WindowId, MoveTemp(M));
}

void UAsdaWindowSubsystem::UnregisterEscModal(FName WindowId)
{
	EscModals.Remove(WindowId);
}

void UAsdaWindowSubsystem::RegisterOriginalModal(FName ModalId, FName OwnerWindowId, TFunction<bool()> IsShown, TFunction<const UWidget*()> Node)
{
	if (ModalId.IsNone() || OwnerWindowId.IsNone() || !IsShown) { return; }
	FOrigModal M;
	M.Owner = OwnerWindowId;
	M.IsShown = MoveTemp(IsShown);
	M.Node = MoveTemp(Node);   // ★ modal_lock2 (МЕЛК-2): мышь корню — только в прямоугольнике узла
	EscModals.Add(ModalId, MoveTemp(M));
}

void UAsdaWindowSubsystem::UnregisterOriginalModal(FName ModalId)
{
	EscModals.Remove(ModalId);
}

void UAsdaWindowSubsystem::NotifyModalStateChanged()
{
	RefreshModalInput();
}

bool UAsdaWindowSubsystem::IsAnyEscModalShown() const
{
	const bool bLock = CVarModalLock.GetValueOnGameThread() != 0;
	for (const TPair<FName, FOrigModal>& Kv : EscModals)
	{
		// Корневая роспись запирает ESC при любом ключе (так с 10.10.2026, день); вложенная — только с замком.
		if ((!Kv.Value.IsShown || bLock) && IsOrigModalShown(Kv.Value)) { return true; }
	}
	return false;
}

void UAsdaWindowSubsystem::RegisterEscSwallower(FName WindowId)
{
	if (WindowId.IsNone()) { return; }
	// Та же защита, что и у росписи-закрывателя: «поглощать ESC» вправе только окно, которому
	//   оригинал ставит признак `CControl+0x228`, иначе мы выдумали бы поведение.
	if (!IsEscCloseWindow(WindowId))
	{
		UE_LOG(LogAsdaWindow, Warning,
			TEXT("[окна] поглотитель ESC для «%s» ОТКЛОНЁН: этого окна нет в эталонной таблице kEscCloseWindows"),
			*WindowId.ToString());
		return;
	}
	EscSwallowers.Add(WindowId);
}

void UAsdaWindowSubsystem::UnregisterEscSwallower(FName WindowId)
{
	EscSwallowers.Remove(WindowId);
}

// ── ★ Порт ветки `uiItemUpgradeWnd_OnCommand@0xD79260`, событие 21, `a3 == 1520` ───────────────
//   Эталон: `sub_92CA8D(g_pWindowMgr, 1)` (снять модальный захват, `set_o215@0xBD4450` —
//   `CWindowMgr+0x215 = 1`) + `OnSendItemUpgrade(mgr, 1524)` — та же функция и тот же номер
//   кнопки, что у нажатия «Отмена». В порту снятие модальности делает само закрытие вида
//   очереди окон-сообщений (`AnswerMsgBox` → `CloseMsgBoxKind`), поэтому здесь остаётся ровно действие кнопки.
void UAsdaWindowSubsystem::EscCloseGameConfirm()
{
	// Проход зовёт нас только когда верхним стоит 1520, но проверяем явно — ответить «отменой»
	//   неоткрытому окну было бы хуже, чем не сделать ничего.
	if (!IsOpen(FName(TEXT("TWOBUT_MSGBOX"))))
	{
		UE_LOG(LogAsdaWindow, Warning, TEXT("[msgbox] ESC на TWOBUT_MSGBOX, но окно не открыто; не трогаем"));
		return;
	}
	AnswerMsgBox(/*bTwoBut*/true, /*bYes*/false);   // = реле кнопки TWOBUT_BUT01 «Отмена» (1524), очередь окон-сообщений
}

const TCHAR* UAsdaWindowSubsystem::ExecuteOwnClose(FName WindowId)
{
	// Везде работаем с КОПИЕЙ делегата: закрыватель вправе тронуть реестр (снять свою же роспись,
	//   закрыть дочернее окно), и указатель внутрь контейнера после этого протух бы.
	if (const FSimpleDelegate* D = EscClosers.Find(WindowId))
	{
		FSimpleDelegate Copy = *D;
		if (Copy.IsBound()) { Copy.Execute(); return TEXT("EscClosers"); }
	}
	for (const TPair<FName, FSimpleDelegate>& Kv : WindowClosers)
	{
		if (Kv.Key == WindowId && Kv.Value.IsBound())
		{
			FSimpleDelegate Copy = Kv.Value;
			Copy.Execute();
			return TEXT("WindowClosers");
		}
	}
	if (const FAsdaNpcWindowCloser* N = NpcWindowClosers.Find(WindowId))
	{
		FAsdaNpcWindowCloser Copy = *N;
		if (Copy.IsBound()) { Copy.Execute(); return TEXT("NpcWindowClosers"); }
	}
	return nullptr;
}

bool UAsdaWindowSubsystem::CloseTopmostOnEsc()
{
	// ★ 25.10.2026 [ОРИГИНАЛ]: кто ответил этому нажатию — для шага цели (`AsdaEscChain::RunAfterWindowPass`). Сброс до любого выхода.
	AsdaEscChain::NoteAnsweredWindow(NAME_None);
	if (CVarEscCloseTopWindow.GetValueOnGameThread() == 0) { return false; }

	// ── ВЕТКА «ЕСТЬ МОДАЛЬНОЕ ОКНО» — эталонное `if (*(this + 104))` ──
	//   Эталон смотрит ТОЛЬКО модальный контрол и НИ ОДНОГО другого окна; воспроизводим буквально,
	//   поэтому из этой ветки выходим в любом случае, не спускаясь к списку ниже.
	{
		FName ModalId = NAME_None;
		int32 ModalZ = TNumericLimits<int32>::Lowest();
		for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
		{
			const FAsdaWindowEntry& E = Kv.Value;
			// ★ 10.10.2026: модалка эталона без флага `bModal` порта (`RegisterEscModal`, пример — 1559) — та же ветка.
			const FOrigModal* OM = EscModals.Find(Kv.Key);
			const bool bOrigRoot = OM && !OM->IsShown && OM->Owner == Kv.Key;
			if ((!E.bModal && !bOrigRoot) || !AsdaWindowIsEntryVisible(E)) { continue; }
			// Строгое «>» плюс разбор ничьей по имени: у двух модалок с одинаковым z порядок
			//   обхода TMap не определён, а выбор и лог обязаны быть воспроизводимыми.
			if (E.ZOrder > ModalZ || (E.ZOrder == ModalZ && Kv.Key.Compare(ModalId) > 0))
			{
				ModalZ = E.ZOrder;
				ModalId = Kv.Key;
			}
		}
		if (!ModalId.IsNone())
		{
			if (!IsEscCloseWindow(ModalId) && !IsEscExtraWindow(ModalId))
			{
				UE_LOG(LogAsdaWindow, Verbose,
					TEXT("[окна] ESC: верхняя модалка «%s» (z=%d) признака CControl+0x228 не имеет — ")
					TEXT("оригинал её по ESC не закрывает, нижние окна в этой ветке не смотрятся"),
					*ModalId.ToString(), ModalZ);
				return false;
			}
			// ★ ДОКАЗАННОЕ «съедаю ESC и ничего не делаю» (ONEBUT_MSGBOX 1500): эталон вызывает
			//   событие 21, процедура для 1500 явно ничего не делает, но `CWindowMgr+0x208` уже
			//   взведён ⇒ нажатие потрачено, меню 69400 не открывается.
			if (EscSwallowers.Contains(ModalId))
			{
				UE_LOG(LogAsdaWindow, Log,
					TEXT("[окна] ESC съеден модалкой «%s» (z=%d) и НИЧЕГО не закрыл — эталон ")
					TEXT("uiItemUpgradeWnd_OnCommand@0xD79260, событие 21: `if (a3 != 1500)`"),
					*ModalId.ToString(), ModalZ);
				AsdaEscChain::NoteAnsweredWindow(ModalId);
				return true;
			}
			const TCHAR* Roster = IsEscExtraWindow(ModalId) ? (PressEscExtraClose(ModalId) ? TEXT("крестик окна") : nullptr) : ExecuteOwnClose(ModalId);
			if (!Roster)
			{
				UE_LOG(LogAsdaWindow, Verbose,
					TEXT("[окна] ESC: у модалки «%s» нет росписи закрытия — не закрываем ")
					TEXT("(слепое снятие виджета оставило бы владельцу состояние «открыто»)"),
					*ModalId.ToString());
				return false;
			}
			UE_LOG(LogAsdaWindow, Log,
				TEXT("[окна] ESC закрыл модальное окно «%s» (z=%d, роспись %s) — нажатие съедено ")
				TEXT("(эталон CWindowMgr+0x208 = 1)"), *ModalId.ToString(), ModalZ, Roster);
			AsdaEscChain::NoteAnsweredWindow(ModalId);
			return true;
		}
		// ★ 10.10.2026: модальный контрол эталона ВНУТРИ корня (`RegisterOriginalModal`, пример — 74200). Признака 0x228 у
		//   таких контролов нет (таблица `kEscCloseWindows`) ⇒ эталон ничего не закрывает и нажатие не съедает.
		FName InnerOwner;
		int32 InnerZ = 0;
		if (FindTopOrigModal(InnerOwner, InnerZ))
		{
			UE_LOG(LogAsdaWindow, Verbose,
				TEXT("[окна] ESC: модальный контрол внутри «%s» (z=%d) признака CControl+0x228 не имеет — ")
				TEXT("оригинал по ESC его не закрывает, окна не смотрятся"), *InnerOwner.ToString(), InnerZ);
			return false;
		}
	}

	// ── ВЕТКА «МОДАЛЬНОГО НЕТ»: обход сверху вниз, закрывается РОВНО ОДНО окно ──
	//   Эталон идёт обратным итератором с хвоста списка, то есть от верхнего окна вниз; у нас
	//   порядок окон задаёт `ZOrder` (его же поддерживает `BringWindowToFront`), поэтому
	//   «сверху вниз» = по убыванию z.
	TArray<TPair<FName, int32>> Candidates;
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		if (!AsdaWindowIsEntryVisible(Kv.Value)) { continue; }   // эталон: `CControl_IsVisible`
		if (!IsEscCloseWindow(Kv.Key) && !IsEscExtraWindow(Kv.Key)) { continue; }   // эталон: `get_o228`; плюс список порта [ИСПРАВЛЕНИЕ]
		Candidates.Emplace(Kv.Key, Kv.Value.ZOrder);
	}
	// ★ 10.10.2026: всплывающие слои, которые эталон регистрирует корневыми окнами (`RegisterEscPopup`), — z владельца.
	for (const TPair<FName, FEscPopup>& Kv : EscPopups)
	{
		const FAsdaWindowEntry* Owner = Windows.Find(Kv.Value.Owner);
		if (!Owner || !AsdaWindowIsEntryVisible(*Owner)) { continue; }
		if (!Kv.Value.IsShown || !Kv.Value.IsShown()) { continue; }
		if (Windows.Contains(Kv.Key)) { continue; }                // уже учтено как окно реестра
		Candidates.Emplace(Kv.Key, Owner->ZOrder);
	}
	Candidates.Sort([](const TPair<FName, int32>& A, const TPair<FName, int32>& B)
	{
		if (A.Value != B.Value) { return A.Value > B.Value; }
		return A.Key.Compare(B.Key) > 0;   // ничью по z разбираем именем — ради воспроизводимости
	});

	for (const TPair<FName, int32>& C : Candidates)
	{
		// ★ То же доказанное «съедаю и ничего не делаю» — и здесь проход ОБЯЗАН остановиться
		//   (эталонный `break` стоит сразу после события 21, независимо от его исхода).
		if (EscSwallowers.Contains(C.Key))
		{
			UE_LOG(LogAsdaWindow, Log,
				TEXT("[окна] ESC съеден окном «%s» (z=%d) и НИЧЕГО не закрыл — эталон ")
				TEXT("uiItemUpgradeWnd_OnCommand@0xD79260, событие 21: `if (a3 != 1500)`; ")
				TEXT("окна ниже в этом нажатии не смотрятся"), *C.Key.ToString(), C.Value);
			AsdaEscChain::NoteAnsweredWindow(C.Key);
			return true;
		}
		const TCHAR* Roster = IsEscExtraWindow(C.Key) ? (PressEscExtraClose(C.Key) ? TEXT("крестик окна") : nullptr) : ExecuteOwnClose(C.Key);
		if (!Roster)
		{
			// Окно из эталонной таблицы, у которого владелец в порту ещё не подписал закрыватель.
			//   Слепо снимать виджет НЕЛЬЗЯ (у подсистемы-владельца останется «открыто»), поэтому
			//   пропускаем и смотрим окно ниже — как эталон пропускает окно без признака 0x228.
			UE_LOG(LogAsdaWindow, Verbose,
				TEXT("[окна] ESC пропустил «%s» (z=%d): окно в эталонной таблице есть, а росписи ")
				TEXT("закрытия в порту нет — смотрим окно ниже"), *C.Key.ToString(), C.Value);
			continue;
		}
		UE_LOG(LogAsdaWindow, Log,
			TEXT("[окна] ESC закрыл верхнее окно «%s» (z=%d, роспись %s) — нажатие съедено ")
			TEXT("(эталон CWindowMgr+0x208 = 1); за одно нажатие закрывается ровно одно окно"),
			*C.Key.ToString(), C.Value, Roster);
		AsdaEscChain::NoteAnsweredWindow(C.Key);
		return true;   // эталонный `break`
	}
	return false;      // закрывать было нечего — вызывающий волен тоггать ESC-меню 69400
}

FString UAsdaWindowSubsystem::DebugEscWindows() const
{
	FString S = TEXT("=== ESC закрывает верхнее окно (эталон uiContainerHandleKey@0x12AF390) ===\n");
	S += FString::Printf(TEXT("  ключ отката asda.UI.EscCloseTopWindow = %d\n"),
		CVarEscCloseTopWindow.GetValueOnGameThread());
	S += FString::Printf(TEXT("  строк эталонной таблицы (вызовов helper_setByteField552@0x91F8CE): %d\n"),
		(int32)UE_ARRAY_COUNT(kEscCloseWindows));
	int32 InPort = 0, WithCloser = 0;
	for (const FAsdaEscCloseWindow& W : kEscCloseWindows)
	{
		const FName Id(W.WindowId);
		const FAsdaWindowEntry* E = Windows.Find(Id);
		// ★ 10.10.2026: всплывающий слой (`RegisterEscPopup`) — в реестре окон его нет, видимость спрашиваем у владельца.
		const FEscPopup* Pop = E ? nullptr : EscPopups.Find(Id);
		const bool bVis = (E && AsdaWindowIsEntryVisible(*E)) || (Pop && Pop->IsShown && Pop->IsShown());
		const TCHAR* Roster = TEXT("НЕТ");
		// ★ «Поглотитель» — не отсутствие росписи, а ДОКАЗАННОЕ поведение эталона «съесть ESC и
		//   ничего не делать» (сейчас это ровно ONEBUT_MSGBOX 1500); в дампе показываем отдельно.
		if (EscSwallowers.Contains(Id)) { Roster = TEXT("СЪЕДАЕТ(1500)"); }
		else if (EscClosers.Contains(Id)) { Roster = TEXT("EscClosers"); }
		else
		{
			for (const TPair<FName, FSimpleDelegate>& Kv : WindowClosers)
			{
				if (Kv.Key == Id) { Roster = TEXT("WindowClosers"); break; }
			}
			if (FCString::Strcmp(Roster, TEXT("НЕТ")) == 0 && NpcWindowClosers.Contains(Id))
			{
				Roster = TEXT("NpcWindowClosers");
			}
		}
		if (E || Pop) { ++InPort; }
		if (FCString::Strcmp(Roster, TEXT("НЕТ")) != 0) { ++WithCloser; }
		S += FString::Printf(TEXT("  %-22s IDNum %-6d %-12s %-7s роспись %-16s [%s]\n"),
			W.WindowId, W.IdNum,
			E ? TEXT("в реестре") : (Pop ? TEXT("всплывашка") : TEXT("нет в порту")),
			bVis ? TEXT("ВИДИМО") : TEXT("скрыто"),
			Roster, W.Origin);
	}
	S += FString::Printf(TEXT("  ИТОГО: в реестре порта сейчас %d, с росписью закрытия %d\n"),
		InPort, WithCloser);
	return S;
}

// Ключ фокуса поля 1412 окна количества (`asda.UI.NumPadFocus`, определён в AsdaWindowSubsystem_Dialogs.cpp).
bool AsdaNumPadFocusOn();
// Ключ фокуса поля 1583 окна ввода текста 1580 (`asda.UI.InputDialogFocus`, там же).
bool AsdaInputDialogFocusOn();
// Цель фокуса `FInputModeUIOnly` для корня окна: корень, если принимает фокус, иначе игровой вьюпорт (`asda.UI.ModalFocusNoRoot`, там же).
TSharedPtr<SWidget> AsdaUiOnlyFocusTarget(UUserWidget* Root);

void UAsdaWindowSubsystem::RefreshModalInput()
{
	UGameInstance* GI = GetGameInstance();
	// ★ 10.10.2026 (modal_lock3, МЕЛК-3): без контроллера (смена карты) раньше выходили СРАЗУ — блокер и подъём корня
	//   оставались от закрытой модалки. Теперь пересчёт идёт всегда, а режим ввода ставится, только если контроллер есть
	//   (новый получит режим мира от `UAsdaWorldSubsystem::RevealWorld`). `bDebugNoController` — имитация для самотеста.
	APlayerController* PC = (GI && !bDebugNoController) ? GI->GetFirstLocalPlayerController(GI->GetWorld()) : nullptr;

	// ★★★ 10.10.2026 ПОДЪЁМ КОРНЯ МОДАЛКИ БЕЗ `bModal` (замок ввода, `asda.UI.ModalLock`). Эталон отдаёт мышь модальному контролу
	//   независимо от z (`+0x1A0`), а блокер порта — слой Slate: всё, что выше блокера, клики получает. Корень модалки 74200
	//   (книга 74000) лежит в обычной полосе, и окна AlwaysTop / приоритетные над ним ловили бы клики. Поэтому на время модалки
	//   корень поднимается в полосу приоритетных (`SetWindowPriority`) и возвращается при снятии. [ПОРТ]: эталон окно не
	//   переставляет — книга на это время рисуется поверх окон AlwaysTop. Корень, уже стоящий в полосе приоритетных (1559),
	//   не трогается. Подъём только при закрытой полосе `bModal` — там блокер и так выше всех обычных окон.
	{
		FName OrigOwner;
		int32 OrigZ = 0;
		const bool bOrig = FindTopOrigModal(OrigOwner, OrigZ);
		if (!ModalLiftedOwner.IsNone() && (!bOrig || OrigOwner != ModalLiftedOwner))
		{
			const FName Was = ModalLiftedOwner;
			ModalLiftedOwner = NAME_None;
			if (IsOpen(Was))
			{
				SetWindowPriority(Was, false);
				UE_LOG(LogAsdaWindow, Log, TEXT("[модалка] «%s» возвращено из полосы приоритетных: модальный контрол снят"), *Was.ToString());
			}
		}
		bool bBand = false;
		for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
		{
			if (Kv.Value.bModal && AsdaWindowIsEntryVisible(Kv.Value)) { bBand = true; break; }
		}
		const bool bInPriority = OrigZ >= kPriorityZ && OrigZ <= kPriorityZMax;
		if (bOrig && !bBand && ModalLiftedOwner.IsNone() && !bInPriority && OrigZ < kModalZ)
		{
			bool bCovered = false;   // есть видимое окно выше корня (ниже всплывающих слоёв 9000) — его клики надо отсечь
			for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
			{
				if (Kv.Key == OrigOwner || !AsdaWindowIsEntryVisible(Kv.Value)) { continue; }
				if (Kv.Value.ZOrder > OrigZ && Kv.Value.ZOrder < kTopmostZ) { bCovered = true; break; }
			}
			if (bCovered)
			{
				ModalLiftedOwner = OrigOwner;
				SetWindowPriority(OrigOwner, true);
				UE_LOG(LogAsdaWindow, Log, TEXT("[модалка] «%s» поднято в полосу приоритетных на время модального контрола (z %d → %d)"),
					*OrigOwner.ToString(), OrigZ, GetWindowZOrder(OrigOwner));
			}
		}
	}

	if (UUserWidget* Modal = GetTopModalWindow())
	{
		// Есть модальное — фокус ввода ТОЛЬКО на нём (клавиатура/навигация к нижним окнам заблокирована).
		FInputModeUIOnly Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		// ★ 25.10.2026 [ОРИГИНАЛ] (`asda.UI.NumPadFocus`): верхняя модалка — окно количества 1410, и фокус клавиатуры у его поля 1412 →
		//   отложенный фокус режима ввода ставится на ПОЛЕ, а не на корень. Корень нефокусируем, Slate отдаёт фокус вьюпорту, и поле
		//   теряло его при любом пересчёте модалок (открытие/закрытие окна под нумпадом). Фокус вне поля (игрок щёлкнул по кнопке
		//   окна — у эталона поле при этом фокус теряет, ui_core.c:107815) не возвращаем: как у оригинала.
		//   ★ То же у окна ввода текста 1580 и его поля 1583 (`asda.UI.InputDialogFocus`).
		UEditableText* KeepField = nullptr;   // поле, которому сохраняем фокус (nullptr — прежнее правило: корень модалки)
		if (AsdaNumPadFocusOn() && Modal == FindWindow(kNumPadWindowId))
		{
			UEditableText* NF = NumPadField.Get();
			if (NF && NF->HasKeyboardFocus()) { KeepField = NF; }
		}
		else if (AsdaInputDialogFocusOn() && Modal == FindWindow(kInputWindowId))
		{
			UEditableText* IF = InputField.Get();
			if (IF && IF->HasKeyboardFocus()) { KeepField = IF; }
		}
		// Корень окна фокус не принимает — фокус режима ввода сразу вьюпорту (`AsdaUiOnlyFocusTarget`): Slate уводил его туда же,
		//   но с ошибкой «Attempting to focus Non-Focusable widget» в журнале.
		Mode.SetWidgetToFocus(KeepField ? TSharedPtr<SWidget>(KeepField->TakeWidget()) : AsdaUiOnlyFocusTarget(Modal));
		if (PC)   // modal_lock3: без контроллера режим не ставится (см. начало функции)
		{
			// ★ 22.10.2026 [ИСПРАВЛЕНИЕ] переход мир → модалка: отпускание под модалкой вьюпорт потеряет — сброс клавиш.
			const bool bWasIgnoring = GEngine && GEngine->GameViewport && GEngine->GameViewport->IgnoreInput();
			PC->SetInputMode(Mode);
			PC->bShowMouseCursor = true;
			if (!bWasIgnoring && CVarModalFlushKeys.GetValueOnGameThread() != 0)
			{
				PC->FlushPressedKeys();
				UE_LOG(LogAsdaWindow, Log, TEXT("[модалка] «%s»: ввод мира отдан модалке — зажатые клавиши сброшены (отпускание под модалкой не дойдёт)"),
					*Modal->GetName());
			}
		}

		// ★ Блок МЫШИ к нижним окнам (эталон захватывает и мышь): FInputModeUIOnly держит только фокус
		//   клавиатуры, клики мыши всё равно доходят до нижних виджетов. Ставим полноэкранный прозрачный
		//   hit-test блокер на z=(z верхней модалки − 1): как топовый под модалкой он поглощает клики.
		int32 ModalZ = 0;
		for (const TPair<FName, FAsdaWindowEntry>& It : Windows)
		{
			if (It.Value.bModal && It.Value.Widget && It.Value.Widget->IsInViewport()
				&& AsdaUi::IsShown(It.Value.Widget))   // ★ волна В6 ремесла (МЕЛК-8): общая точка видимости узла
			{
				ModalZ = FMath::Max(ModalZ, It.Value.ZOrder);
			}
		}
		// ★ 10.10.2026: окон `bModal` нет — блокер под корнем модального контрола эталона без `bModal` (1559, 74200).
		if (ModalZ == 0)
		{
			FName OrigOwner;
			int32 OrigZ = 0;
			if (FindTopOrigModal(OrigOwner, OrigZ)) { ModalZ = OrigZ; }
		}
		if (!ModalBlocker)
		{
			ModalBlocker = NewObject<UBorder>(this);
			ModalBlocker->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));   // прозрачный — фон не затемняем (эталон)
			ModalBlocker->SetVisibility(ESlateVisibility::Visible);          // Visible = хит-тестится → ест клики
		}
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(ModalBlocker->TakeWidget());
			GEngine->GameViewport->AddViewportWidgetContent(ModalBlocker->TakeWidget(), ModalZ - 1);
		}
		// ★ ФОКУС ВОЗВРАЩАЕМ В ПОЛЕ ВВОДА. `SetWidgetToFocus(корень модалки)` уводит клавиатуру
		//   с поля 1583, и набирать становится некуда (а раньше это ещё и «подтверждало» диалог —
		//   разбор у `CommitCallback` в ShowInputDialog). Если верхняя модалка — окно 1580,
		//   фокус сразу возвращаем полю: то же делает эталон
		//   (`CControl_SetFocus(menuWnd + 88, 1)`, ui_core.c:58840).
		//   ★ 25.10.2026: при `asda.UI.InputDialogFocus 1` фокус полю ставит `ShowInputDialog`, а держит `KeepField` выше — этот
		//   немедленный возврат (его тут же перетирала отложенная операция режима ввода) только при ключе 0, как было.
		if (!AsdaInputDialogFocusOn() && Modal == FindWindow(kInputWindowId))
		{
			if (UEditableText* Fld = InputField.Get()) { Fld->SetKeyboardFocus(); }
		}
	}
	else
	{
		// Модальных нет — снять блокер + восстановить БАЗОВЫЙ режим экрана (DIV-3): иначе после закрытия
		//   модалки фокус остаётся на закрытом виджете и базовый экран не получает ввод.
		if (ModalBlocker && GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(ModalBlocker->TakeWidget());
		}
		// modal_lock3: без контроллера режим не ставится — новый получит режим мира от `RevealWorld`. Захват мира модалкой
		//   (`IsWorldInputCapturedByModal`) считается по факту, залипать нечему.
		if (!PC) { return; }
		if (bBaseInputGameAndUI)
		{
			// ★ МИР: GameAndUI — PlayerController обязан получать клавиши (WASD опросом IsInputKeyDown)
			//   и ЛКМ (BindKey → click-to-move). UIOnly здесь = полная потеря управления персонажем.
			// ★ 22.10.2026 [ИСПРАВЛЕНИЕ] переход модалка → мир: клавиша, отпущенная под модалкой, «зажата» в `UPlayerInput`
			//   (вьюпорт игнорировал её отпускание) — сброс до возврата ввода миру.
			const bool bWasIgnoring = GEngine && GEngine->GameViewport && GEngine->GameViewport->IgnoreInput();
			FInputModeGameAndUI M;
			M.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			M.SetHideCursorDuringCapture(false);
			PC->SetInputMode(M);
			PC->bShowMouseCursor = true;
			if (bWasIgnoring && CVarModalFlushKeys.GetValueOnGameThread() != 0)
			{
				PC->FlushPressedKeys();
				UE_LOG(LogAsdaWindow, Log, TEXT("[модалка] снята: ввод возвращён миру — зажатые клавиши сброшены (отпускание под модалкой было потеряно)"));
			}
			// ★ 25.10.2026 [ИСПРАВЛЕНИЕ] (`asda.UI.KeepTextFocusOnRefresh`): модалки не было (`!bWasIgnoring`), а фокус у поля ввода на
			//   экране — пересчёт вызван показом/скрытием обычного окна, фокус полю оставляем (строка чата не закрывается).
			const bool bKeepField = !bWasIgnoring && CVarKeepTextFocusOnRefresh.GetValueOnGameThread() != 0 && AsdaIsLiveTextFieldFocused();
			if (FSlateApplication::IsInitialized() && !bKeepField) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
		}
		else
		{
			FInputModeUIOnly Mode;
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			PC->SetInputMode(Mode);
			PC->bShowMouseCursor = true;
		}
	}
}

// Базовый режим ввода экрана (см. объявление). Идемпотентно; сразу применяет режим, если модальных нет.
void UAsdaWindowSubsystem::SetBaseInputMode(bool bWorldGameAndUI)
{
	if (bBaseInputGameAndUI == bWorldGameAndUI) { return; }
	bBaseInputGameAndUI = bWorldGameAndUI;
	UE_LOG(LogAsdaWindow, Log, TEXT("Базовый режим ввода = %s"), bWorldGameAndUI ? TEXT("GameAndUI (мир)") : TEXT("UIOnly (меню)"));
	RefreshModalInput();
}

// ★★★ ПЕРЕВЕШИВАНИЕ ОКНА УБИВАЕТ НЕЗАВЕРШЁННЫЙ КЛИК — доказано по исходникам движка.
//   `UWidget::RemoveFromParent` (UMG/Private/Components/Widget.cpp:895) снимает `SConstraintCanvas`
//   окна с вьюпорта, а `AddToViewport` вешает НОВЫЙ. Путь захвата мыши, который Slate запомнил на
//   нажатии, рвётся: `FSlateUser::GetCaptorPath` (Slate/Private/Framework/Application/SlateUser.cpp:352)
//   получает от `ToWidgetPath` результат `Truncated`, тут же зовёт `ReleaseCapture`, а тот —
//   `CaptorWidget->OnMouseCaptureLost` (там же, :314). У кнопки это
//   `SButton::OnMouseCaptureLost → Release()` (Slate/Private/Widgets/Input/SButton.cpp), то есть
//   `bIsPressed = false`. На отпускании `SButton::OnMouseButtonUp` при методе `DownAndUp` требует
//   `bIsPressed` (`bMeetsPressedRequirements`) и `HasMouseCapture()` — обоих уже нет ⇒
//   `ExecuteOnClick` НЕ вызывается. Клик проглатывается молча, без единой строки в логе.
//   ⇒ логируем КАЖДОЕ фактическое перевешивание вместе с состоянием ЛКМ: строка с «ЛКМ ЗАЖАТА» —
//   это и есть съеденный клик. Диагностика дешёвая: смена z — событие редкое.
// ★★★ [ПОРТ] 08.10.2026 (ревизия ui_bugs_REVIEW М-1, «текст пропадает»): подъём окна ПЕРЕСТАНОВКОЙ слота оверлея вьюпорта,
//   без снятия окна. `RemoveFromParent` + `AddToViewport` пересоздаёт корень окна (`SObjectWidget` у UUserWidget хранится
//   слабой ссылкой и умирает вместе со слотом вьюпорта), а строка ввода с фокусом остаётся тем же `SEditableText` уже под
//   новым корнем; после такого перевешивания программная запись в поле (`SetText`) при первой потере фокуса откатывалась на
//   прежнее значение — щелчок «ОК» 9104 читал пустое имя (зонд `asda.UI.ProbeEditRaise` + `asda.UI.Front`).
//   Эталон окно не пересоздаёт — только переставляет в списке (`AddChildOrdered`), поэтому перестановка слота ближе к 1:1.
//   Ключ отката: `asda.UI.ZOrderInPlace 0` — прежнее перевешивание Remove+Add.
static ASDA_DEV_CVAR(int32, CVarZOrderInPlace,
	TEXT("asda.UI.ZOrderInPlace"), 1,
	TEXT("1 — подъём окна перестановкой слота оверлея вьюпорта (окно не снимается, фокус и текст полей живы; 08.10.2026); ")
	TEXT("0 — прежнее перевешивание RemoveFromParent+AddToViewport."),
	ECVF_Default);

// Переставить окно, висящее во вьюпорте, на новый z без снятия. false — окно не во вьюпорте / цепочка Slate не та,
//   что ставит `UGameViewportSubsystem::AddToScreen` (корень → полноэкранный канвас → оверлей вьюпорта); тогда — прежний путь.
static bool AsdaReorderViewportWidgetInPlace(UUserWidget* W, int32 ZOrder)
{
	if (!W || !W->IsInViewport()) { return false; }
	UGameViewportSubsystem* VS = UGameViewportSubsystem::Get();   // ★ R5-fix-b 13.11.2026: C4996 — `Get(World)` в движке и так зовёт `Get()` (GameViewportSubsystem.cpp:78)
	if (!VS) { return false; }
	const TSharedPtr<SWidget> Mine = W->GetCachedWidget();                                   // корень окна (SObjectWidget)
	const TSharedPtr<SWidget> Host = Mine.IsValid() ? Mine->GetParentWidget() : nullptr;     // полноэкранный SConstraintCanvas
	const TSharedPtr<SWidget> Ov = Host.IsValid() ? Host->GetParentWidget() : nullptr;       // оверлей вьюпорта
	if (!Ov.IsValid() || Ov->GetType() != FName(TEXT("SOverlay"))) { return false; }
	FChildren* Ch = Ov->GetChildren();
	if (!Ch) { return false; }
	for (int32 i = 0; i < Ch->Num(); ++i)
	{
		if (&Ch->GetChildAt(i).Get() != Host.Get()) { continue; }
		// Учёт подсистемы вьюпорта (её `GetWidgetSlot().ZOrder` читают дампы) — тот же z, что дал бы `AddToViewport(ZOrder)`.
		FGameViewportWidgetSlot VSlot = VS->GetWidgetSlot(W);
		VSlot.ZOrder = ZOrder;
		VS->SetWidgetSlot(W, VSlot);
		// `AddToScreen` кладёт полноэкранный канвас в оверлей с z + 10 — так же и здесь. Слот оверлея сам переставляется
		//   в массиве детей (`SOverlay::FOverlaySlot::SetZOrder`); равные z встают после имеющихся, как у `AddSlot`.
		SOverlay::FOverlaySlot& S = const_cast<SOverlay::FOverlaySlot&>(static_cast<const SOverlay::FOverlaySlot&>(Ch->GetSlotAt(i)));
		S.SetZOrder(ZOrder + 10);
		Ov->Invalidate(EInvalidateWidgetReason::ChildOrder);
		return true;
	}
	return false;
}

void UAsdaWindowSubsystem::SetWindowZOrder(FName WindowId, int32 ZOrder)
{
	if (FAsdaWindowEntry* Found = Windows.Find(WindowId))
	{
		const int32 OldZ = Found->ZOrder;
		Found->ZOrder = ZOrder;
		if (Found->Widget)
		{
			const bool bLmb = FSlateApplication::IsInitialized()
				&& FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
			UE_LOG(LogAsdaWindow, Log, TEXT("[z-порядок] «%s»: z %d → %d (перевешено в вьюпорте)%s"),
				*WindowId.ToString(), OldZ, ZOrder,
				bLmb ? TEXT(" ★ ЛКМ ЗАЖАТА — Slate теряет захват, текущий клик будет съеден") : TEXT(""));
			// ★ 08.10.2026 (открытие поверх всех): клавиатурный фокус ВНУТРИ перевешиваемого окна (строка ввода, получившая фокус
			//   при открытии) снимается ДО снятия окна с вьюпорта и возвращается после. Иначе поле, снятое с фокусом, при первой
			//   потере фокуса перезагружало СТАРОЕ значение (`FSlateEditableTextLayout::HandleFocusLost` → `LoadText`) — замер зонда
			//   `asda.UI.ProbeEditRaise`: записанное имя пропадало на щелчке «ОК» (5200 уходил пустым). Ключ отката: asda.UI.OpenOnTop 0
			//   (без подъёма при показе ситуации «перевешивание поля с фокусом» не возникало).
			// ★ 08.10.2026: перестановка на месте — окно не снимается, фокус и текст полей не трогаются (см. `CVarZOrderInPlace`).
			if (CVarZOrderInPlace.GetValueOnGameThread() != 0)
			{
				if (AsdaReorderViewportWidgetInPlace(Found->Widget, ZOrder)) { return; }
				// ★ 08.10.2026 (пакет 2, МЕЛК-2 `batch_fixes_REVIEW.md`): окно во вьюпорте, а перестановка на месте не вышла (цепочка
				//   Slate не та) — прежний путь Remove+Add может снова терять текст поля с фокусом. Один Warning на окно за сессию.
				static TSet<FName> GZOrderInPlaceWarned;   // окна, о которых уже предупредили (игровой поток)
				if (Found->Widget->IsInViewport() && !GZOrderInPlaceWarned.Contains(WindowId))
				{
					GZOrderInPlaceWarned.Add(WindowId);
					UE_LOG(LogAsdaWindow, Warning, TEXT("[z-порядок] «%s»: перестановка на месте не удалась (цепочка Slate не та, что у AddToViewport) — откат на Remove+Add, текст поля с фокусом может потеряться"),
						*WindowId.ToString());
				}
			}
			TSharedPtr<SWidget> FocusInside;
			if (FSlateApplication::IsInitialized())
			{
				const TSharedPtr<SWidget> F = FSlateApplication::Get().GetKeyboardFocusedWidget();
				if (F.IsValid() && FindWindowOwningSlate(F) == WindowId)
				{
					FocusInside = F;
					FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
				}
			}
			// Перевесить в вьюпорте с новым z-order (Remove+Add — UMG не даёт менять z «на месте»).
			Found->Widget->RemoveFromParent();
			Found->Widget->AddToViewport(ZOrder);
			UAsdaUiVisibilitySubsystem::TrackFor(this, Found->Widget);   // ★ 21.10.2026 (Alt+Z): перевес z — новая обёртка
			if (FocusInside.IsValid()) { FSlateApplication::Get().SetKeyboardFocus(FocusInside, EFocusCause::SetDirectly); }
		}
	}
}

// ── СИСТЕМА диалогов подтверждения (эталон MESSAGE2_WIN, IDNum 40) ──

UUserWidget* UAsdaWindowSubsystem::ShowConfirm(const FString& Message, TFunction<void(bool)> OnResult, float TimeoutSec,
	bool bTimeoutConfirms, int32 YesLocId, int32 NoLocId, bool bGateYesOnGauge, bool bNeutral)
{
	FAsdaConfirmGauge& G = GaugeOf(/*bTwoBut*/false);   // ★ 31.10.2026: своя полоса логин-диалога (`asda.UI.ConfirmGaugeSplit`)
	// ★ 25.10.2026 (Enter = кнопка согласия): пометка «без Enter» от вызывающего (удаление персонажа) и новый номер показа —
	//   окно MESSAGE2 одно на все подтверждения, Enter сверяет номер на отпускании.
	AsdaEnterGuard::NoteConfirmShown(AsdaEnterGuard::ConsumeNextNoEnter());
	// Если уже висит незакрытый диалог — отменить его (No) перед открытием нового (защита от наложения).
	if (IsOpen(ConfirmWindowId)) { CloseConfirmImpl(false, /*bAnswer*/false); }   // W4-1: замена окна — без звуков
	ConfirmWindowId = TEXT("MESSAGE2_WIN");
	G.Prefix = TEXT("MESSAGE2_GAUGE");

	// ★ Нейтральный вид — ОТДЕЛЬНЫЙ ассет-вариант того же окна (см. объявление). Полоса отсчёта в нём
	//   свёрнута, поэтому таймер/гейт с ним не сочетаются — тогда честно показываем базовое окно.
	if (bNeutral && (TimeoutSec > 0.f || bGateYesOnGauge))
	{
		UE_LOG(LogAsdaWindow, Warning, TEXT("ShowConfirm: нейтральный вид (без полосы GAUGE) несовместим с таймером %.1f с / гейтом — показано базовое MESSAGE2_WIN"), TimeoutSec);
		bNeutral = false;
	}
	static const TCHAR* kBasePath    = TEXT("/Game/AsdaAssets/UI/Login/WBP_MESSAGE2_WIN");
	static const TCHAR* kNeutralPath = TEXT("/Game/AsdaAssets/UI/Login/WBP_MESSAGE2_WIN_NEUTRAL");
	UUserWidget* Dlg = OpenWindowByPath(FName(TEXT("MESSAGE2_WIN")),
		bNeutral ? kNeutralPath : kBasePath, 25, ESlateVisibility::Visible, /*bModal*/true);
	if (!Dlg && bNeutral)
	{
		// Вариант не загрузился (ассет не собран/переименован) — диалог важнее вида: базовое окно + Warning.
		UE_LOG(LogAsdaWindow, Warning, TEXT("ShowConfirm: WBP_MESSAGE2_WIN_NEUTRAL не загружен — показано базовое MESSAGE2_WIN (пересобрать: ASDA_JUI_CLASS=login ASDA_ONLY_WIN=MESSAGE2_WIN_NEUTRAL Tools/asda_jui_generic_umg.py)"));
		Dlg = OpenWindowByPath(FName(TEXT("MESSAGE2_WIN")), kBasePath, 25, ESlateVisibility::Visible, /*bModal*/true);
		bNeutral = false;   // показали базу — лог ниже должен говорить правду (ревизия review_message2_neutral_and_rebind.md)
	}
	if (!Dlg) { if (OnResult) { OnResult(false); } return nullptr; }
	UE_LOG(LogAsdaWindow, Log, TEXT("[модаль] MESSAGE2_WIN: вид %s"), bNeutral ? TEXT("нейтральный (WBP_MESSAGE2_WIN_NEUTRAL)") : TEXT("базовый"));

	ConfirmCallback = MoveTemp(OnResult);

	// Текст диалога (вызывающий передаёт уже готовую loc-строку — система текст не выдумывает).
	if (UTextBlock* T = Cast<UTextBlock>(Dlg->GetWidgetFromName(TEXT("MESSAGE2_TXT00"))))
	{
		T->SetAutoWrapText(true);
		T->SetText(FText::FromString(Message));
	}

	// Пересобрать реле Yes/No заново (виджет мог переиспользоваться — снять старые биндинги).
	ConfirmRelays.Reset();
	G.YesButton = nullptr;
	G.bGateYes = bGateYesOnGauge;
	G.FullW = 0.f;   // сбросить запомненную ширину заливки для нового диалога
	G.LastLoggedQuarter = -1;
	// Подпись кнопки из loc-ID (текст ТОЛЬКО из таблицы DT_GameText; -1 = оставить как есть).
	auto SetBtnText = [&](const TCHAR* N, int32 LocId)
	{
		if (LocId < 0) return;
		if (UTextBlock* T = Cast<UTextBlock>(Dlg->GetWidgetFromName(FName(*(FString(N) + TEXT("_Text"))))))
			T->SetText(FText::FromString(FAsdaLoginData::Loc(LocId)));
	};
	auto BindDlg = [&](const TCHAR* N, bool bYes)
	{
		if (UButton* B = Cast<UButton>(Dlg->GetWidgetFromName(FName(N))))
		{
			B->OnClicked.Clear();
			AsdaUi::BindButton(this, B, 0, [this, bYes](int32) { CloseConfirm(bYes); }, ConfirmRelays);   // ★ U20: реле клика — общая точка
			if (bYes)
			{
				G.YesButton = B;
				if (bGateYesOnGauge) { B->SetIsEnabled(false); }   // разблокирует TickConfirmGauge при заполнении
			}
			else
			{
				// ★ 18.10.2026: окно MESSAGE2 одно на все подтверждения — кнопку «Нет», выключенную прошлым вызывающим
				//   (модалка режима входа гасит «Душа»), каждый показ возвращаем в рабочий вид.
				B->SetIsEnabled(true);
				B->SetRenderOpacity(1.f);
			}
		}
	};
	SetBtnText(TEXT("MESSAGE2_BUT00"), YesLocId);   // Yes (напр. «Удалить» loc 200000)
	SetBtnText(TEXT("MESSAGE2_BUT01"), NoLocId);    // No  (напр. «Отменить» loc 200001)
	BindDlg(TEXT("MESSAGE2_BUT00"), true);    // Yes (BUT00) → подтвердить
	BindDlg(TEXT("MESSAGE2_BUT01"), false);   // No  (BUT01) → отмена

	// Авто-исход по таймауту + анимация гейджа отсчёта (эталон uiProgressBar_AnimStart(GAUGE,10000)).
	G.Dlg = Dlg;
	G.Duration = TimeoutSec;
	G.RealStart = -1.0;   // ★ 27.09.2026: у логин-диалога заливка — по миру от показа
	if (UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		W->GetTimerManager().ClearTimer(ConfirmTimeoutTimer);
		W->GetTimerManager().ClearTimer(G.AnimTimer);
		if (TimeoutSec > 0.f)
		{
			// ★ В gate-режиме таймаут НЕ авто-закрывает — гадж лишь разблокирует Yes (анти-мисклик удаления).
			if (!bGateYesOnGauge)
				W->GetTimerManager().SetTimer(ConfirmTimeoutTimer,
					FTimerDelegate::CreateUObject(this, &UAsdaWindowSubsystem::CloseConfirm, bTimeoutConfirms), TimeoutSec, false);
			// Заливка гейджа 0→1 линейно за TimeoutSec (тик ~30 Гц; frac по мировому времени).
			G.StartTime = W->GetTimeSeconds();
			TickConfirmGaugeOf(false);   // сразу выставить 0
			W->GetTimerManager().SetTimer(G.AnimTimer, FTimerDelegate::CreateUObject(this, &UAsdaWindowSubsystem::TickConfirmGaugeOf, false), 0.033f, true);
		}
	}
	return Dlg;
}

void UAsdaWindowSubsystem::TickConfirmGaugeOf(bool bTwoBut)
{
	FAsdaConfirmGauge& G = GaugeOf(bTwoBut);   // ★ 31.10.2026: полоса своего окна (`asda.UI.ConfirmGaugeSplit`)
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	UUserWidget* Dlg = G.Dlg.Get();
	if (!W || !Dlg || G.Duration <= 0.f) { return; }
	// ★ 27.09.2026: полоса-таймер TWOBUT считается от старта ВЛАДЕЛЬЦА (реальное время, как `GetTickCount` эталона),
	//   прочие полосы — по миру от показа.
	const double Elapsed = (G.RealStart >= 0.0) ? (FPlatformTime::Seconds() - G.RealStart)
	                                                 : (double)(W->GetTimeSeconds() - G.StartTime);
	const float Frac = FMath::Clamp((float)Elapsed / G.Duration, 0.f, 1.f);
	// ★ Анимируем СУЩЕСТВУЮЩУЮ UI-полосу MESSAGE2_GAUGE (скин Log_WhiteBar), НЕ рисуем свою.
	//   Скин=ProgressBar с ДВУМЯ планами → конвертер даёт два image «MESSAGE2_GAUGE_<idx>_0/_1»: первый (_0)=заливка,
	//   остальные прячем (иначе статичная вторая полоса). Заполнение = КЛИП ширины слота заливки (fullW·Frac) —
	//   как оригинал (uiProgressBar_AnimStart клипит Rect.right), сплошным белым (diffuse Log_WhiteBar=FFFFFFFF).
	if (!Dlg->WidgetTree) { return; }
	UImage* Fill = nullptr;
	TArray<UWidget*> All; Dlg->WidgetTree->GetAllWidgets(All);
	for (UWidget* Wd : All)
	{
		UImage* Im = Cast<UImage>(Wd);
		if (!Im) { continue; }
		const FString N = Im->GetName();
		// Префикс полосы текущего диалога (MESSAGE2_GAUGE / TWOBUT_GAUGE); подложки *_Base не заливка.
		if (!N.StartsWith(G.Prefix) || N.Contains(TEXT("BASE")) || N.Contains(TEXT("Base"))) { continue; }
		if (!Fill) { Fill = Im; }                                   // _0 = заливка
		else { Im->SetVisibility(ESlateVisibility::Collapsed); }    // _1+ = лишние планы → скрыть
	}
	if (Fill)
	{
		UCanvasPanelSlot* FS = AsdaUi::SlotOf(Fill);
		if (FS)
		{
			if (G.FullW <= 0.f)
			{
				G.FullW = FS->GetSize().X;                // полная ширина (первый тик)
				FSlateBrush Br = Fill->GetBrush();                  // заливка = СПЛОШНОЙ белый (без атласного UV-серого)
				Br.SetResourceObject(nullptr);
				Br.TintColor = FSlateColor(FLinearColor::White);
				Fill->SetBrush(Br);
			}
			FVector2D Sz = FS->GetSize();
			Sz.X = G.FullW * Frac;                        // клип ширины 0→full
			FS->SetSize(Sz);
		}
	}
	else { UE_LOG(LogTemp, Warning, TEXT("[GAUGE] %s (заливка) не найдена"), *G.Prefix); }
	// ★ Рантайм-доказательство заливки: лог на 0/25/50/75/100 % (требование живого теста меню смерти).
	const int32 Quarter = FMath::Clamp((int32)(Frac * 4.f), 0, 4);
	if (G.bGateYes && Quarter != G.LastLoggedQuarter)
	{
		G.LastLoggedQuarter = Quarter;
		UE_LOG(LogAsdaWindow, Log, TEXT("[смерть] gauge: длительность=%d мс, заполнено=%d%%, кнопка согласия=%s"),
			(int32)(G.Duration * 1000.f), Quarter * 25, (Frac >= 1.f) ? TEXT("активна") : TEXT("заблокирована"));
	}
	// ★ Gate-режим: кнопка согласия разблокируется (и визуально) при полном заполнении полосы.
	if (G.bGateYes && Frac >= 1.f)
	{
		if (UButton* Y = G.YesButton.Get()) { Y->SetIsEnabled(true); Y->SetRenderOpacity(1.f); }
		if (W) { W->GetTimerManager().ClearTimer(G.AnimTimer); }   // заполнено — тик больше не нужен
	}
	// ★ 27.09.2026: полоса-таймер без гейта заполнена — тик больше не нужен (исход делает владелец записи).
	if (!G.bGateYes && Frac >= 1.f && W) { W->GetTimerManager().ClearTimer(G.AnimTimer); }
}

void UAsdaWindowSubsystem::CloseConfirm(bool bConfirmed)
{
	// Логин-диалог MESSAGE2_WIN — свой путь; игровой TWOBUT_MSGBOX (26.09) отвечает очередь окон-сообщений.
	if (IsOpen(ConfirmWindowId)) { CloseConfirmImpl(bConfirmed, /*bAnswer*/true); return; }   // кнопка «Да»/«Нет» — ответ игрока
	if (IsOpen(FName(TEXT("TWOBUT_MSGBOX")))) { AnswerMsgBox(/*bTwoBut*/true, bConfirmed); }
}

void UAsdaWindowSubsystem::CloseConfirmImpl(bool bConfirmed, bool bAnswer)
{
	FAsdaConfirmGauge& G = GaugeOf(/*bTwoBut*/false);   // ★ 31.10.2026: полоса логин-диалога, TWOBUT не трогаем
	if (UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		W->GetTimerManager().ClearTimer(ConfirmTimeoutTimer);
		W->GetTimerManager().ClearTimer(G.AnimTimer);
	}
	// Логин-окно MESSAGE2_WIN звуков менеджера окон-сообщений не имеет (это не игровое окно-сообщение);
	//   игровой TWOBUT_MSGBOX с 26.09 сюда не ходит (очередь, `AnswerMsgBox`), поэтому `bAnswer` здесь — только смысл вызова.
	(void)bAnswer;
	G.Dlg = nullptr;
	CloseWindow(ConfirmWindowId);   // MESSAGE2_WIN
	ConfirmRelays.Reset();

	// Забрать колбэк локально и обнулить ДО вызова (защита от повторного входа/двойного результата).
	TFunction<void(bool)> Cb = MoveTemp(ConfirmCallback);
	ConfirmCallback = nullptr;
	if (Cb) { Cb(bConfirmed); }
}
