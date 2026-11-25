// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWindowSubsystem_TopPress.cpp — ★ 06.11.2026 часть UAsdaWindowSubsystem: НАЖАТИЕ ПОЛУЧАЕТ ТОЛЬКО ВЕРХНЕЕ ОКНО (`asda.UI.TopWindowPress`).
//   [ОРИГИНАЛ] `uiWindowMgrDispatchMouse@0x12AF540` (ui_core.c:104134) обходит окна сверху вниз; первое, у которого
//   `uiContainerHandleMouse@0x12DCC50` вернул 1 (нажатие ЛКМ/ПКМ/СКМ в плане его контрола, флаги 0x400/0x2000/0x10000), съедает событие.
//   Окна ниже нажатия не видят.
//   [ПОРТ] Планы тела окна импортёр делает `SelfHitTestInvisible`, поэтому для Slate тело прозрачно: нажатие по телу сумки над кнопкой
//   другого окна уходит в эту кнопку. `FindWindowCatchingAt` (порт хит-теста эталона) при этом отвечает «сумка». Ассеты не меняем
//   (планы `Visible` закрыли бы и наведение, но это перегенерация всех окон). Решение — препроцессор Slate: он видит нажатие раньше
//   маршрутизации и съедает его, только если лист пути Slate лежит в окне НИЖЕ поймавшего.
//   Мир, ручки, подъём и фокус работают по опросу кнопок (`FAsdaMouseFrame`), а `PressedMouseButtons` Slate пополняет ДО препроцессоров
//   (SlateApplication.cpp, ProcessMouseButtonDownEvent) — съеденное нажатие опрос видит как обычно.
//   Съеденному нажатию съедается и его отпускание: `SButton` с методом `MouseUp` кликает без нажатия. Движок при съеденном отпускании
//   не снимает захват ОС — снимаем сами.
//   Наведение Slate гейт не трогает: подсветка нижней кнопки под телом окна остаётся (см. `asda.UI.SelfTestBodyPress`).
//   Вторая ветка того же дефекта — контролы с покадровым опросом (слоты панели, ячейки сеток, вкладки чата): они спрашивают только свой
//   прямоугольник. `IsCursorOverHigherWindow` стоял лишь у части подсистем, поэтому проверка верхнего окна встала в общую точку
//   `AsdaUi::IsCursorOverWidget` (`asda.UI.TopWindowPoll`). Окна-оверлеи с NoSelect у корня и корень `HitTestInvisible` мышь не держат.
//
// ★ 07.11.2026 ЩЕЛЧОК МИМО МЕНЮ (`asda.UI.ClickAwayMenu`). Тот же препроцессор видит каждое нажатие раньше маршрутизации и раньше опроса.
//   [ОРИГИНАЛ] Меню вкладок 2670 гаснет от пункта (chat.c:2097-2117), от ESC (ui_core.c:12430-12433) и от щелчка по земле
//   (`Input_WorldClickHandler@0x968890`, input.c:493-494, ветка без цели). Других вызовов нет, потерю фокуса оригинал не обрабатывает.
//   Меню по нику 80000 гаснет от пункта и от ЛКМ по ленте 2521 (4 вызова `sub_93393C`). Щелчок по окнам интерфейса меню не гасил.
//   [ПОРТ] Щелчка по земле в порту не было, поэтому 2670 закрывалось только пунктом и ESC.
//   [ИСПРАВЛЕНИЕ] требование владельца: меню гаснет от щелчка мимо него, где бы тот ни пришёлся, и от потери фокуса приложения.
//   Нажатие не съедается: щелчок мимо закрывает меню и делает своё дело (выбирает вкладку, нажимает кнопку, ведёт персонажа).
//   ПКМ по вкладке при открытом меню гасит его на нажатии и открывает заново на отпускании. Выпадающих списков здесь нет: щелчок по их
//   телу — переключатель, закрытие на нажатии ломало бы его (они гаснут сами в своём опросе).
#include "UI/AsdaWindowSubsystem.h"
#include "AsdaDevConsole.h"   // ASDA_DEV_CVAR — дев-ключи вне Shipping
#include "AsdaWindowSubsystemInternal.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericApplication.h"
#include "Layout/WidgetPath.h"
#include "UI/AsdaUiHelpers.h"   // ★ 08.11.2026: живой прямоугольник панели (`AsdaUi::WidgetAbsRect`)
#include "Components/Widget.h"

static ASDA_DEV_CVAR(int32, CVarTopWindowPress,
	TEXT("asda.UI.TopWindowPress"), 1,
	TEXT("1 (по умолчанию) — нажатие мыши по телу окна не доходит до контролов окон ниже (как `uiWindowMgrDispatchMouse`); ")
	TEXT("0 — прежнее: тело окна прозрачно для Slate, нажатие проходит в кнопку нижнего окна."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarTopWindowPoll,
	TEXT("asda.UI.TopWindowPoll"), 1,
	TEXT("1 (по умолчанию) — покадровый опрос (`AsdaUi::IsCursorOverWidget`) не видит контрол под телом окна выше; ")
	TEXT("0 — прежнее: слоты, ячейки и вкладки ловят мышь сквозь окно поверх них."),
	ECVF_Default);

// ★ 08.11.2026 (пачка 18, баг 2): тело панели внутри окна и модальный узел ловят нажатие (разбор — у `RegisterPressPanel` в .h).
static ASDA_DEV_CVAR(int32, CVarPanelPress,
	TEXT("asda.UI.PanelPress"), 1,
	TEXT("1 (по умолчанию) — нажатие по телу показанной панели окна (15515, 6333) не доходит до кнопок окна под ней, а при модальном ")
	TEXT("узле (6296, 74200) кнопки окна-владельца вне узла нажатия не получают (эталон `uiContainerHandleMouse` / `+0x1A0`); ")
	TEXT("0 — прежнее: нажатие проходит сквозь панель в кнопку под ней."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarClickAwayMenu,
	TEXT("asda.UI.ClickAwayMenu"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) — всплывающее меню гаснет от нажатия мимо него и от потери фокуса приложения; ")
	TEXT("0 — прежнее: меню закрывается только своими пунктами, ESC и ЛКМ по ленте чата."),
	ECVF_Default);

// Препроцессор: только кнопки мыши, решения принимает подсистема.
class FAsdaTopPressGate : public IInputProcessor
{
public:
	TWeakObjectPtr<UAsdaWindowSubsystem> Owner;   // подсистема окон
	virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
	virtual bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent& Ev) override
	{
		return Owner.IsValid() && Owner->HandleTopWindowPress(Ev);
	}
	// Второе нажатие двойного щелчка Slate шлёт отдельным событием, `SButton` считает его нажатием.
	virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication&, const FPointerEvent& Ev) override
	{
		return Owner.IsValid() && Owner->HandleTopWindowPress(Ev);
	}
	virtual bool HandleMouseButtonUpEvent(FSlateApplication&, const FPointerEvent& Ev) override
	{
		return Owner.IsValid() && Owner->HandleTopWindowRelease(Ev);
	}
	virtual const TCHAR* GetDebugName() const override { return TEXT("AsdaTopPressGate"); }
};

void UAsdaWindowSubsystem::RegisterTopPressGate()
{
	if (!FSlateApplication::IsInitialized()) { return; }
	TSharedPtr<FAsdaTopPressGate> Gate = MakeShared<FAsdaTopPressGate>();
	Gate->Owner = this;
	TopPressGate = Gate;
	FSlateApplication::Get().RegisterInputPreProcessor(TopPressGate);
	ClickAwayActivationHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddWeakLambda(this, [this](bool bActive)
	{
		if (!bActive) { CloseClickAwayMenus(nullptr); }
	});
}

void UAsdaWindowSubsystem::UnregisterTopPressGate()
{
	if (TopPressGate.IsValid() && FSlateApplication::IsInitialized()) { FSlateApplication::Get().UnregisterInputPreProcessor(TopPressGate); }
	if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ClickAwayActivationHandle); }
	ClickAwayActivationHandle.Reset();
	ClickAwayMenus.Reset();
	TopPressGate.Reset();
	TopPressEaten.Reset();
}

void UAsdaWindowSubsystem::RegisterClickAwayMenu(FName ZoneId, FSimpleDelegate Closer)
{
	if (!ZoneId.IsNone() && Closer.IsBound()) { ClickAwayMenus.Add(ZoneId, MoveTemp(Closer)); }
}

bool UAsdaWindowSubsystem::DebugGetClickAwayRect(FName ZoneId, FVector2D& OutMin, FVector2D& OutMax) const
{
	const FAsdaUiZoneRect* Rect = UiZones.Find(ZoneId);
	return Rect && Rect->IsBound() && Rect->Execute(OutMin, OutMax);
}

void UAsdaWindowSubsystem::CloseClickAwayMenus(const FVector2D* PressPt)
{
	if (CVarClickAwayMenu.GetValueOnGameThread() == 0) { return; }
	TArray<FSimpleDelegate> Closers;   // закрыватель снимает зону из реестра: обход и закрытие раздельно
	for (const TPair<FName, FSimpleDelegate>& Kv : ClickAwayMenus)
	{
		FVector2D Mn, Mx;
		if (!DebugGetClickAwayRect(Kv.Key, Mn, Mx)) { continue; }   // меню не показано
		if (PressPt && PressPt->X >= Mn.X && PressPt->X <= Mx.X && PressPt->Y >= Mn.Y && PressPt->Y <= Mx.Y) { continue; }   // нажатие по самому меню
		Closers.Add(Kv.Value);
		UE_LOG(LogAsdaWindow, Log, TEXT("[щелчок мимо] меню «%s» гаснет: %s"), *Kv.Key.ToString(), PressPt ? TEXT("нажатие вне зоны") : TEXT("приложение потеряло фокус"));
	}
	for (const FSimpleDelegate& Closer : Closers) { Closer.ExecuteIfBound(); }
}

bool UAsdaWindowSubsystem::FindLowerWindowUnderTopAt(FVector2D Pt, FName& OutTop, FName& OutLower) const
{
	OutTop = FindWindowCatchingAt(Pt);
	OutLower = NAME_None;
	if (OutTop.IsNone() || !FSlateApplication::IsInitialized()) { return false; }
	FSlateApplication& App = FSlateApplication::Get();
	const FWidgetPath Path = App.LocateWindowUnderMouse(Pt, App.GetInteractiveTopLevelWindows());
	if (!Path.IsValid()) { return false; }
	OutLower = FindWindowOwningSlate(Path.Widgets.Last().Widget);
	return IsBelowCatcher(OutTop, OutLower);
}

bool UAsdaWindowSubsystem::IsBelowCatcher(FName TopId, FName LowId) const
{
	if (TopId.IsNone() || LowId.IsNone() || LowId == TopId || AsdaIsNoSelectRootWindow(TopId)) { return false; }   // мир, виджет вне реестра, само окно, оверлей
	const FAsdaWindowEntry* Top = Windows.Find(TopId);
	const FAsdaWindowEntry* Low = Windows.Find(LowId);
	if (!Top || !Low || !Top->Widget || !Low->Widget || Top->Widget == Low->Widget || Low->ZOrder >= Top->ZOrder) { return false; }
	if (Top->Widget->GetVisibility() == ESlateVisibility::HitTestInvisible) { return false; }   // тултипы 9099 мышь у оригинала не ловят
	// Окно, вложенное в другое, — одно окно для игрока: z реестра его порядок не описывает.
	const TSharedPtr<SWidget> TopRoot = Top->Widget->GetCachedWidget();
	const TSharedPtr<SWidget> LowRoot = Low->Widget->GetCachedWidget();
	for (TSharedPtr<SWidget> P = TopRoot; P.IsValid(); P = P->GetParentWidget()) { if (P == LowRoot) { return false; } }
	for (TSharedPtr<SWidget> P = LowRoot; P.IsValid(); P = P->GetParentWidget()) { if (P == TopRoot) { return false; } }
	return true;
}

void UAsdaWindowSubsystem::RegisterPressPanel(FName PanelId, FName OwnerWindowId, TFunction<bool()> IsShown, TFunction<const UWidget*()> Node)
{
	if (PanelId.IsNone() || OwnerWindowId.IsNone() || !IsShown || !Node) { return; }
	FPressPanel P;
	P.Owner = OwnerWindowId;
	P.IsShown = MoveTemp(IsShown);
	P.Node = MoveTemp(Node);
	PressPanels.Add(PanelId, MoveTemp(P));
}

void UAsdaWindowSubsystem::UnregisterPressPanel(FName PanelId)
{
	PressPanels.Remove(PanelId);
}

// Лист пути Slate лежит в поддереве `Root`.
static bool AsdaSlateUnder(const TSharedPtr<SWidget>& Leaf, const TSharedPtr<SWidget>& Root)
{
	if (!Root.IsValid()) { return false; }
	for (TSharedPtr<SWidget> P = Leaf; P.IsValid(); P = P->GetParentWidget()) { if (P == Root) { return true; } }
	return false;
}

bool UAsdaWindowSubsystem::IsPressCaughtByPanel(FName LeafWindow, const TSharedPtr<SWidget>& Leaf, FVector2D Pt) const
{
	if (CVarPanelPress.GetValueOnGameThread() == 0 || LeafWindow.IsNone() || !Leaf.IsValid()) { return false; }
	// Узел панели ловит нажатие: лист не в его поддереве и (немодальная панель) точка в его прямоугольнике или (модальный узел) где угодно.
	auto Catches = [&Leaf, &Pt](const UWidget* N, bool bModal) -> bool
	{
		if (!N) { return false; }
		const ESlateVisibility V = N->GetVisibility();
		if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return false; }
		const TSharedPtr<SWidget> Root = N->GetCachedWidget();
		if (!Root.IsValid() || AsdaSlateUnder(Leaf, Root)) { return false; }
		if (bModal) { return true; }
		FVector2D Mn, Mx;
		return AsdaUi::WidgetAbsRect(N, Mn, Mx) && Pt.X >= Mn.X && Pt.X <= Mx.X && Pt.Y >= Mn.Y && Pt.Y <= Mx.Y;
	};
	for (const TPair<FName, FPressPanel>& Kv : PressPanels)
	{
		const FPressPanel& P = Kv.Value;
		if (P.Owner != LeafWindow || !P.IsShown || !P.IsShown() || !P.Node) { continue; }
		if (Catches(P.Node(), false)) { return true; }
	}
	// [ОРИГИНАЛ] модальный контрол `+0x1A0` (`helper_setupFields`) получает ВСЮ мышь: кнопки окна-владельца вне узла глухи.
	for (const TPair<FName, FOrigModal>& Kv : EscModals)
	{
		const FOrigModal& M = Kv.Value;
		if (M.Owner != LeafWindow || !M.Node || !IsOrigModalShown(M)) { continue; }
		if (Catches(M.Node(), true)) { return true; }
	}
	return false;
}

bool UAsdaWindowSubsystem::IsWidgetBelowCatcher(const UWidget* W, FVector2D Pt) const
{
	if (!W) { return false; }
	// ★ 08.11.2026 (пачка 18, баг 2): покадровый опрос контрола под телом панели того же окна / вне модального узла — та же глухота.
	{
		const TSharedPtr<SWidget> Sw = W->GetCachedWidget();
		if (Sw.IsValid() && IsPressCaughtByPanel(FindWindowOwningSlate(Sw), Sw, Pt)) { return true; }
	}
	if (CVarTopWindowPoll.GetValueOnGameThread() == 0) { return false; }
	const FName Top = FindWindowCatchingAt(Pt);
	return !Top.IsNone() && IsBelowCatcher(Top, FindWindowOwningSlate(W->GetCachedWidget()));
}

bool UAsdaWindowSubsystem::HandleTopWindowPress(const FPointerEvent& Ev)
{
	const FKey Button = Ev.GetEffectingButton();
	TopPressEaten.Remove(Button);   // отпускание могло потеряться (потеря фокуса окна ОС): новое нажатие снимает пометку
	if (!Ev.IsTouchEvent()) { const FVector2D PressPt = Ev.GetScreenSpacePosition(); CloseClickAwayMenus(&PressPt); }   // до гейта: меню держит чат поднятым
	if (CVarTopWindowPress.GetValueOnGameThread() == 0 || Ev.IsTouchEvent() || !FSlateApplication::IsInitialized()) { return false; }
	FSlateApplication& App = FSlateApplication::Get();
	// Захват мыши и перенос Slate разводит сам: нажатие уходит держателю, а не листу под курсором.
	if (App.IsDragDropping() || App.HasUserMouseCapture(Ev.GetUserIndex())) { return false; }
	FName Top, Lower;
	if (!FindLowerWindowUnderTopAt(Ev.GetScreenSpacePosition(), Top, Lower))
	{
		// ★ 08.11.2026 (пачка 18, баг 2): внутри ОДНОГО окна — тело показанной панели / модальный узел (`IsPressCaughtByPanel`).
		const FWidgetPath Path = App.LocateWindowUnderMouse(Ev.GetScreenSpacePosition(), App.GetInteractiveTopLevelWindows());
		if (!Path.IsValid()) { return false; }
		const TSharedPtr<SWidget> Leaf = Path.Widgets.Last().Widget;
		const FName LeafWin = FindWindowOwningSlate(Leaf);
		if (!IsPressCaughtByPanel(LeafWin, Leaf, Ev.GetScreenSpacePosition())) { return false; }
		Top = LeafWin;
		Lower = LeafWin;
	}
	TopPressEaten.Add(Button);
	++TopPressEatenCount;
	UE_LOG(LogAsdaWindow, Log, TEXT("[верхнее окно] нажатие %s в (%.0f,%.0f) поймало «%s», контрол окна «%s» ниже его не получает"),
		*Button.ToString(), Ev.GetScreenSpacePosition().X, Ev.GetScreenSpacePosition().Y, *Top.ToString(), *Lower.ToString());
	// [ОРИГИНАЛ] `uiContainerHandleMouse` снимает фокус с полей окон, которые обошёл, включая поймавшее (ui_core.c:107815). Окна ниже
	//   не обходятся и фокус держат; чат эталон держит в начале списка. Slate нажатия не видел, поэтому снимаем сами.
	const TSharedPtr<SWidget> Focused = App.GetKeyboardFocusedWidget();
	const FName FocusWin = FindWindowOwningSlate(Focused);
	static const TSet<FName> ChatBottom = { FName(TEXT("CHAT_NEW")), FName(TEXT("CHAT_NEW_SYSTEM")), FName(TEXT("CHAT_btn_Win")) };
	const FAsdaWindowEntry* FocusEntry = Windows.Find(FocusWin);
	const FAsdaWindowEntry* TopEntry = Windows.Find(Top);
	if (FocusEntry && TopEntry && !ChatBottom.Contains(FocusWin) && FocusEntry->ZOrder >= TopEntry->ZOrder && Focused->GetType().ToString().Contains(TEXT("Editable")))
	{
		App.SetAllUserFocusToGameViewport();
	}
	return true;
}

bool UAsdaWindowSubsystem::HandleTopWindowRelease(const FPointerEvent& Ev)
{
	if (TopPressEaten.Remove(Ev.GetEffectingButton()) == 0) { return false; }
	FSlateApplication& App = FSlateApplication::Get();
	if (App.GetPressedMouseButtons().Num() == 0 && App.GetPlatformApplication().IsValid()) { App.GetPlatformApplication()->SetCapture(nullptr); }
	return true;
}
