// Copyright (c) 2026 @sitxovski (Discord)
// AsdaDevMenuSubsystem_Build.cpp — ★ R3-c 12.11.2026 (разрез AsdaDevMenuSubsystem.cpp, дословный перенос): меню: открыть/закрыть, строки, вкладки, BuildMenu, замер кадра.
//   Общее для частей — в AsdaDevMenuSubsystem_Private.h; шапка и реверс — в AsdaDevMenuSubsystem.cpp.
#include "UI/AsdaDevMenuSubsystem.h"
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "Login/AsdaButtonRelay.h"

#include "Fonts/FontMeasure.h"          // FSlateFontMeasure — ширина подписи без переноса (проверка ru-text-wider-than-field)
#include "Rendering/SlateRenderer.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"          // ряд кнопок-табов (переносится на 2 строки, если не влезает)
#include "Components/WrapBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Blueprint/WidgetLayoutLibrary.h"   // размер/масштаб вьюпорта → ширина панели и высота прокрутки

#include "AsdaDevMenuSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaDevMenuSubsystem*.cpp

// (★ R3-c чистка 12.11.2026: перенесено из AsdaDevMenuSubsystem_Private.h — нужно только этой части.)
// ★ 19.10.2026: те же подписи вкладок для английского клиента (порядок = GDevTabNames).
static const TCHAR* GDevTabNamesEn[(int32)EAsdaDevTab::Count] = {
	TEXT("Display"), TEXT("Quality"), TEXT("Sky & Fog"), TEXT("Relief"), TEXT("Characters"), TEXT("Reset")
};

// Подпись вкладки на языке сессии (кнопка-таб и кнопки сброса).
static const TCHAR* DevTabTitle(int32 i) { return DmT(GDevTabNames[i], GDevTabNamesEn[i]); }

// ───────────────────────────── Меню (рантайм-панель) ─────────────────────────────
// ★ 25.10.2026 (ui_toggles) [ИСПРАВЛЕНИЕ]: разбор — у объявления `IsMenuOpen`. 0 = прежнее (по указателю).
static ASDA_DEV_CVAR(int32, CVarDevMenuToggleFromFact,
	TEXT("asda.DevMenu.ToggleFromFact"), 1,
	TEXT("1 (по умолчанию) = меню Ctrl+N «открыто», только пока его панель во вьюпорте (после смены карты травел её снимает — ")
	TEXT("Ctrl+N открывает с первого раза, Esc и хоткеи не съедаются); 0 = прежнее (указатель панели пережил карту)."));

bool UAsdaDevMenuSubsystem::IsMenuOpen() const
{
	if (CVarDevMenuToggleFromFact.GetValueOnGameThread() == 0) { return MenuWidget != nullptr; }
	return MenuWidget && MenuWidget->IsInViewport();
}

void UAsdaDevMenuSubsystem::ToggleMenu()
{
	// Панель, которую травел снял с вьюпорта, — уже закрыта: чистим её состояние (строки, реле) и открываем заново.
	if (MenuWidget && !IsMenuOpen())
	{
		UE_LOG(LogAsdaDevMenu, Log, TEXT("[меню] панель снята с вьюпорта сменой карты — состояние «открыто» снято, открываем заново"));
		CloseMenu();
	}
	if (MenuWidget) { CloseMenu(); }
	else            { BuildMenu(); }
}

void UAsdaDevMenuSubsystem::CloseMenu()
{
	if (MenuWidget)
	{
		MenuWidget->RemoveFromParent();
		MenuWidget = nullptr;
	}
	Rows.Reset();
	RowRelayIndex.Reset();
	Relays.Reset();
	TabBoxes.Reset();
	TabButtons.Reset();
	TabTexts.Reset();
}

void UAsdaDevMenuSubsystem::RefreshRows()
{
	for (const FRow& R : Rows)
	{
		if (R.Text.IsValid() && R.Label) { R.Text->SetText(FText::FromString(R.Label())); }
		// Ручка ползунка — из текущего CVar (сброс/консоль при открытой панели; В-1). USlider::SetValue при изменении
		// значения СТРЕЛЯЕТ OnValueChanged (UE 5.8 Slider.cpp:159) — колбэк заглушён флагом, иначе накат+персист по кругу.
		if (R.Slider.IsValid() && R.SliderT01)
		{
			bSliderSetFromCode = true;
			R.Slider->SetValue(R.SliderT01());
			bSliderSetFromCode = false;
		}
	}
}

// Активная вкладка: её контейнер Visible, остальные Collapsed; кнопка-таб подсвечена. Персист [AsdaVideo] DevMenuTab.
void UAsdaDevMenuSubsystem::SetActiveTab(EAsdaDevTab Tab)
{
	ActiveTab = Tab;
	const FLinearColor ActiveBg(1.f, 0.85f, 0.4f, 1.f);      // активный таб — тёплый жёлтый (цвет секций панели)
	const FLinearColor IdleBg(0.35f, 0.35f, 0.42f, 1.f);     // остальные — приглушённый серый
	for (int32 i = 0; i < TabBoxes.Num(); ++i)
	{
		const bool bOn = (i == (int32)Tab);
		if (TabBoxes[i].IsValid())   { AsdaUi::SetShownDecor(TabBoxes[i].Get(), bOn); }
		if (TabButtons[i].IsValid()) { TabButtons[i]->SetBackgroundColor(bOn ? ActiveBg : IdleBg); }
		if (TabTexts[i].IsValid())   { TabTexts[i]->SetColorAndOpacity(FSlateColor(bOn ? FLinearColor::Black : FLinearColor::White)); }
	}
	// Персист только при смене значения — не писать файл на каждый клик по уже активному табу (ревизия М-7).
	if (GConfig)
	{
		int32 Saved = -1;
		GConfig->GetInt(TEXT("AsdaVideo"), TEXT("DevMenuTab"), Saved, GGameUserSettingsIni);
		if (Saved != (int32)Tab)
		{
			GConfig->SetInt(TEXT("AsdaVideo"), TEXT("DevMenuTab"), (int32)Tab, GGameUserSettingsIni);
			GConfig->Flush(false, GGameUserSettingsIni);
		}
	}
}

void UAsdaDevMenuSubsystem::BuildMenu()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaVideoSettingsSubsystem* Video = GI ? GI->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr;
	if (!GI || !Video) { return; }

	// CreateWidget из базового UUserWidget запрещён (абстрактный) → пустой WBP_ResPicker (root CanvasPanel),
	// дерево наполняется в рантайме — тот же приём и стиль, что у прежнего логин-пикера.
	UClass* PickerClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/AsdaAssets/UI/Dev/WBP_ResPicker.WBP_ResPicker_C"));
	if (!PickerClass) { UE_LOG(LogAsdaDevMenu, Warning, TEXT("WBP_ResPicker не загружен")); return; }
	UUserWidget* W = CreateWidget<UUserWidget>(GI, PickerClass);
	if (!W || !W->WidgetTree) { return; }
	UWidgetTree* Tree = W->WidgetTree;
	UCanvasPanel* Root = Cast<UCanvasPanel>(Tree->RootWidget);
	if (!Root) { return; }

	// Размеры — от вьюпорта (ревизия 05.09 п.4.2/М-6): ширина содержимого = 58 % ширины экрана в slate-единицах (500…760):
	// на 1280 px семь табов встают в одну строку (нужно ≈712), на 1024 px — в две; макс. высота прокрутки = высота экрана
	// минус заголовок/табы/паддинги (300…900) — панель не обрезается на 1024×600. Пиксели → slate через DPI-масштаб.
	FVector2D ViewLocal(1280.f, 720.f);   // запас на случай отсутствия вьюпорта
	if (UWorld* World = GI->GetWorld())
	{
		const FVector2D Px = UWidgetLayoutLibrary::GetViewportSize(World);
		const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(World), 0.01f);
		if (Px.X > 0.f && Px.Y > 0.f) { ViewLocal = Px / Scale; }
	}
	PanelContentWidth = FMath::Clamp(ViewLocal.X * 0.58f, 500.f, 760.f);   // ширина содержимого (табы + строки); подписи проверяются против неё
	PanelMaxContentHeight = FMath::Clamp(ViewLocal.Y - 130.f, 300.f, 900.f);
	UE_LOG(LogAsdaDevMenu, Log, TEXT("Панель: вьюпорт %.0f×%.0f slate-ед. → ширина содержимого %.0f, макс. высота %.0f"), ViewLocal.X, ViewLocal.Y, PanelContentWidth, PanelMaxContentHeight);
	UBorder* Panel = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
	Panel->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.04f, 0.9f));
	Panel->SetPadding(FMargin(18.f));
	if (UCanvasPanelSlot* PS = Cast<UCanvasPanelSlot>(Root->AddChild(Panel)))
	{
		PS->SetAnchors(FAnchors(0.5f, 0.5f)); PS->SetAlignment(FVector2D(0.5f, 0.5f)); PS->SetAutoSize(true);
	}
	UVerticalBox* Outer = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Outer"));
	Panel->SetContent(Outer);

	auto AddLabelTo = [&](UVerticalBox* Target, const FString& Txt, int32 Sz, FLinearColor Col)
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		T->SetText(FText::FromString(Txt));
		FSlateFontInfo F = T->GetFont(); F.Size = Sz; T->SetFont(F);
		T->SetColorAndOpacity(FSlateColor(Col));
		T->SetAutoWrapText(true);
		if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Target->AddChildToVerticalBox(T))) { VS->SetPadding(FMargin(0, 4, 0, 2)); }
	};

	const FLinearColor SecCol(1.f, 0.85f, 0.4f, 1.f);    // цвет секций/активного таба — тёплый жёлтый
	const FLinearColor SubCol(0.65f, 0.8f, 1.f, 1.f);    // подзаголовки групп строк — светло-голубой
	const FLinearColor NoteCol(0.7f, 0.7f, 0.7f, 1.f);   // примечания («требует перезапуска», «рычага нет») — серый

	// ── Заголовок ──
	AddLabelTo(Outer, DmT(TEXT("Настройки (dev) — Ctrl+N / Esc закрыть"), TEXT("Settings (dev) — Ctrl+N / Esc to close")), 15, FLinearColor::White);

	// ── Ряд кнопок-табов (WrapBox: перенос по ФАКТИЧЕСКОЙ ширине панели — bExplicitWrapSize=false → UseAllottedSize;
	//    раньше WrapSize=500 фиксирован → 4+3 при любом разрешении, ревизия 4.2) ──
	USizeBox* TabsSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("TabsClamp"));
	TabsSize->SetWidthOverride(PanelContentWidth);
	UWrapBox* Tabs = Tree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("Tabs"));
	Tabs->SetExplicitWrapSize(false);
	Tabs->SetInnerSlotPadding(FVector2D(3.f, 3.f));
	TabsSize->SetContent(Tabs);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Outer->AddChildToVerticalBox(TabsSize))) { VS->SetPadding(FMargin(0, 6, 0, 6)); }

	// ── Содержимое: SizeBox(ширина+макс. высота) → ScrollBox → по одному VerticalBox на вкладку ──
	USizeBox* Size = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Clamp"));
	Size->SetMaxDesiredHeight(PanelMaxContentHeight);
	Size->SetWidthOverride(PanelContentWidth);
	UScrollBox* Scroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("Scroll"));
	Size->SetContent(Scroll);
	UVerticalBox* Content = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Content"));
	Scroll->AddChild(Content);
	Outer->AddChildToVerticalBox(Size);

	TabBoxes.SetNum((int32)EAsdaDevTab::Count);
	TabButtons.SetNum((int32)EAsdaDevTab::Count);
	TabTexts.SetNum((int32)EAsdaDevTab::Count);
	for (int32 i = 0; i < (int32)EAsdaDevTab::Count; ++i)
	{
		// Кнопка-таб
		UButton* B = Tree->ConstructWidget<UButton>(UButton::StaticClass());
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		T->SetText(FText::FromString(DevTabTitle(i)));
		FSlateFontInfo F = T->GetFont(); F.Size = 12; T->SetFont(F);
		B->AddChild(T);
		AsdaUi::BindButton(this, B, i, [this](int32 Idx) { SetActiveTab((EAsdaDevTab)Idx); }, Relays);   // ★ U20: реле клика — общая точка
		Tabs->AddChildToWrapBox(B);
		TabButtons[i] = B;
		TabTexts[i] = T;
		// Контейнер содержимого вкладки
		UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), *FString::Printf(TEXT("Tab%d"), i));
		Content->AddChildToVerticalBox(Box);
		TabBoxes[i] = Box;
	}

	// Текущая вкладка при наполнении строк (AddCycle/AddLabel пишут в её контейнер).
	EAsdaDevTab Cur = EAsdaDevTab::Screen;
	auto Box = [&]() -> UVerticalBox* { return TabBoxes[(int32)Cur].Get(); };
	auto AddLabel = [&](const FString& Txt, int32 Sz, FLinearColor Col) { AddLabelTo(Box(), Txt, Sz, Col); };

	// Кнопка с живой подписью: Label() даёт текст с ТЕКУЩИМ значением, клик применяет следующее
	// значение цикла/тумблера и обновляет подписи ВСЕХ строк (RefreshRows).
	auto AddCycle = [&](TFunction<FString()> Label, TFunction<void()> OnClick)
	{
		UButton* B = Tree->ConstructWidget<UButton>(UButton::StaticClass());
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		T->SetText(FText::FromString(Label()));
		FSlateFontInfo F = T->GetFont(); F.Size = 12; T->SetFont(F);
		T->SetJustification(ETextJustify::Center);
		T->SetAutoWrapText(true);   // длинная русская подпись переносится внутри кнопки, а не вылезает за панель
		B->AddChild(T);
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = [this, OnClick](int32) { if (OnClick) { OnClick(); } RefreshRows(); };
		B->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, B);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
		Rows.Add({ T, Label, Cur });
		RowRelayIndex.Add(Relays.Num() - 1);
		if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Box()->AddChildToVerticalBox(B))) { VS->SetPadding(FMargin(0, 1)); }
	};
	// ★ Строка-ПОЛЗУНОК (USlider): живая подпись «<Title>: ×<val>» над слайдером; движение ползунка накатывает
	//   значение CVar (Min..Max) и обновляет подпись, персист — по отпусканию (OnMouseCaptureEnd / OnControllerCaptureEnd);
	//   изменение без захвата (клавиатура без Enter) персистится отложенно (ScheduleSliderPersist, М-1). Слайдер работает в
	//   нормированных 0..1 (значение = Min + t×(Max−Min)) — не полагаемся на устаревающие SetMinValue/SetMaxValue.
	//   Ручка хранится в FRow: RefreshRows ставит её из CVar после сброса/консоли (В-1).
	//   Ширина = ширина поля панели (HAlign_Fill в VerticalBox = PanelContentWidth ≤ поля; ru-text-wider-than-field:
	//   меряется только подпись, она короткая). Стиль панели: жёлтая ручка, тёмная полоса.
	auto AddSlider = [&](const TCHAR* Name, float Min, float Max, float Def, const FString& Title)
	{
		// Живая подпись над ползунком (обновляется и при движении, и через RefreshRows).
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		auto Label = [Name, Title, Def]() { return FString::Printf(TEXT("%s: ×%.2f"), *Title, GetCVarFloat(Name, Def)); };
		T->SetText(FText::FromString(Label()));
		FSlateFontInfo F = T->GetFont(); F.Size = 12; T->SetFont(F);
		T->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		T->SetAutoWrapText(true);
		if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Box()->AddChildToVerticalBox(T))) { VS->SetPadding(FMargin(0, 4, 0, 0)); }
		RowRelayIndex.Add(-1);   // у ползунка отдельный обработчик (не UButton) — в SelfTestClickRows не участвует

		USlider* Sld = Tree->ConstructWidget<USlider>(USlider::StaticClass());
		Sld->SetStepSize(0.01f);
		Sld->SetSliderBarColor(FLinearColor(0.25f, 0.25f, 0.30f, 1.f));    // тёмная полоса под цвет панели
		Sld->SetSliderHandleColor(FLinearColor(1.f, 0.85f, 0.4f, 1.f));    // тёплый жёлтый — как активный таб/секции
		const float Range = FMath::Max(Max - Min, KINDA_SMALL_NUMBER);
		// Положение ручки 0..1 из текущего CVar — и при построении, и в RefreshRows (В-1).
		auto T01 = [Name, Min, Range, Def]() { return FMath::Clamp((GetCVarFloat(Name, Def) - Min) / Range, 0.f, 1.f); };
		Sld->SetValue(T01());
		Rows.Add({ T, Label, Cur, Sld, T01 });
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		const TWeakObjectPtr<UTextBlock> WT = T;
		R->SliderCallback = [this, Name, Min, Range, Def, WT, Label](float T01In, bool bCaptured)
		{
			if (bSliderSetFromCode) { return; }   // ручку поставил RefreshRows из CVar — значение уже там, персист не нужен
			const float V = Min + FMath::Clamp(T01In, 0.f, 1.f) * Range;
			// Живой накат без записи ini (колбэк CVar пересчитает туман; лог тише, RefreshRows не зовётся — ручка уже на месте).
			bFogSliderLive = true;
			if (IConsoleVariable* CV = FindCVar(Name)) { CV->Set(V, ECVF_SetByConsole); }
			bFogSliderLive = false;
			if (WT.IsValid()) { WT->SetText(FText::FromString(Label())); }
			// Без захвата (клавиатура/геймпад без Enter) «отпускания» не будет — персист отложенно (М-1).
			if (!bCaptured) { ScheduleSliderPersist(Name, Def); }
		};
		// Отпускание ручки (мышь) / Enter или потеря фокуса (клавиатура): одна строка лога + персист в ini.
		R->SliderReleaseCallback = [this, Name, Def]() { ApplyCVarPersist(Name, FString::SanitizeFloat(GetCVarFloat(Name, Def))); };
		Sld->OnValueChanged.AddDynamic(R, &UAsdaButtonRelay::OnSliderValue);
		Sld->OnMouseCaptureBegin.AddDynamic(R, &UAsdaButtonRelay::OnSliderCaptureBegin);
		Sld->OnMouseCaptureEnd.AddDynamic(R, &UAsdaButtonRelay::OnSliderCaptureEnd);
		Sld->OnControllerCaptureBegin.AddDynamic(R, &UAsdaButtonRelay::OnSliderCaptureBegin);
		Sld->OnControllerCaptureEnd.AddDynamic(R, &UAsdaButtonRelay::OnSliderCaptureEnd);
		AsdaUi::AnchorRelay(Relays, R, Sld);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
		if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Box()->AddChildToVerticalBox(Sld)))
		{
			VS->SetPadding(FMargin(0, 0, 0, 3));
			VS->SetHorizontalAlignment(HAlign_Fill);   // ширина ползунка = ширина поля панели (≤ поля)
		}
	};
	// Простая кнопка-действие (без живой подписи).
	auto AddButton = [&](const FString& Label, TFunction<void()> OnClick)
	{
		AddCycle([Label]() { return Label; }, MoveTemp(OnClick));
	};
	// Циклическая смена значения int-CVar по списку (текущее → следующее по кругу).
	auto CycleInt = [this](const TCHAR* Name, TArray<int32> Vals)
	{
		const int32 Cur = GetCVarInt(Name);
		int32 Idx = Vals.IndexOfByKey(Cur);
		Idx = (Idx == INDEX_NONE) ? 0 : (Idx + 1) % Vals.Num();
		ApplyCVarPersist(Name, FString::FromInt(Vals[Idx]));
	};
	auto CycleFloat = [this](const TCHAR* Name, TArray<float> Vals)
	{
		const float Cur = GetCVarFloat(Name);
		int32 Idx = INDEX_NONE;
		for (int32 i = 0; i < Vals.Num(); ++i) { if (FMath::IsNearlyEqual(Vals[i], Cur, 0.01f)) { Idx = i; break; } }
		Idx = (Idx == INDEX_NONE) ? 0 : (Idx + 1) % Vals.Num();
		ApplyCVarPersist(Name, FString::SanitizeFloat(Vals[Idx]));
	};
	// Цикл параметра MPC_AsdaNormals по списку значений (живьём + персист [AsdaNormals]).
	auto CycleNormal = [this](const FName& Param, TArray<float> Vals)
	{
		const float Cur = GetNormalValue(Param);
		int32 Idx = INDEX_NONE;
		for (int32 i = 0; i < Vals.Num(); ++i) { if (FMath::IsNearlyEqual(Vals[i], Cur, 0.01f)) { Idx = i; break; } }
		Idx = (Idx == INDEX_NONE) ? 0 : (Idx + 1) % Vals.Num();
		SetNormalPersist(Param, Vals[Idx]);
	};
	auto OnOff = [](bool b) { return b ? DmT(TEXT("вкл"), TEXT("on")) : DmT(TEXT("выкл"), TEXT("off")); };
	// Строка «тумблер int-CVar 0/1» одной командой.
	auto AddToggle = [&](const FString& Title, const TCHAR* Name, int32 Def)
	{
		AddCycle(
			[Title, Name, Def, OnOff]() { return FString::Printf(TEXT("%s: %s"), *Title, OnOff(GetCVarInt(Name, Def) != 0)); },
			[CycleInt, Name]() { CycleInt(Name, { 1, 0 }); });
	};
	// Строка sg-группы качества (UGameUserSettings, 0..4; штатный персист).
	auto AddSg = [&](const FString& Title, TFunction<int32(UGameUserSettings*)> Get, TFunction<void(UGameUserSettings*, int32)> Set)
	{
		AddCycle(
			[Title, Get]() {
				UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr;
				return FString::Printf(TEXT("%s (sg): %d / 4"), *Title, S ? Get(S) : 3);
			},
			[Get, Set]() {
				if (UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr)
				{
					Set(S, (Get(S) + 1) % 5);
					S->ApplyNonResolutionSettings();
					S->SaveSettings();
				}
			});
	};
	// ═══════════════ ЭКРАН ═══════════════
	Cur = EAsdaDevTab::Screen;
	AddLabel(DmT(TEXT("Разрешение (оконный режим — окно = точный размер):"), TEXT("Resolution (windowed mode — window = exact size):")), 12, SubCol);
	for (const FIntPoint& Res : Video->GetAvailableResolutions())
	{
		const FIntPoint R = Res;
		AddButton(FString::Printf(TEXT("%d x %d"), R.X, R.Y), [Video, R]() { Video->ApplyResolution(R, /*Windowed*/2); });
	}
	AddButton(DmT(TEXT("[ Во весь экран (desktop) ]"), TEXT("[ Fullscreen (desktop) ]")),
		[Video]() { Video->ApplyResolution(Video->GetDesktopResolution(), /*WindowedFullscreen*/1); });

	AddLabel(DmT(TEXT("Кадр:"), TEXT("Frame:")), 12, SubCol);
	// Масштаб рендера.
	AddCycle(
		[]() { return FString::Printf(TEXT("%s: %d%%"), DmT(TEXT("Масштаб рендера"), TEXT("Render scale")), GetCVarInt(TEXT("r.ScreenPercentage"), 100)); },
		// ★ 19.09.2026: 125/150 ВОЗВРАЩЕНЫ (и 200) — значения выше 100 % профиль держит своим полем
		//   `RenderScaleAbove100` поверх штатного потолка (см. `UAsdaGameUserSettings::SetRenderScalePercent`).
		[CycleInt]() { CycleInt(TEXT("r.ScreenPercentage"), { 50, 75, 100, 125, 150, 200 }); });

	// Лимит FPS (штатный UGameUserSettings → t.MaxFPS; 0 = без лимита).
	AddCycle(
		[]() {
			UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr;
			const int32 L = S ? FMath::RoundToInt(S->GetFrameRateLimit()) : 0;
			const TCHAR* Title = DmT(TEXT("Лимит FPS"), TEXT("FPS limit"));
			return (L <= 0) ? FString::Printf(TEXT("%s: %s"), Title, DmT(TEXT("выкл"), TEXT("off"))) : FString::Printf(TEXT("%s: %d"), Title, L);
		},
		[]() {
			if (UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr)
			{
				static const int32 Vals[] = { 0, 30, 60, 120, 144, 240 };
				const int32 Cur = FMath::RoundToInt(S->GetFrameRateLimit());
				int32 Idx = 0;
				for (int32 i = 0; i < UE_ARRAY_COUNT(Vals); ++i) { if (Vals[i] == Cur) { Idx = (i + 1) % UE_ARRAY_COUNT(Vals); break; } }
				S->SetFrameRateLimit((float)Vals[Idx]);
				S->ApplyNonResolutionSettings();
				S->SaveSettings();
			}
		});

	// VSync (штатный UGameUserSettings — сам применяет r.VSync и персистит).
	AddCycle(
		[OnOff]() {
			UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr;
			return FString::Printf(TEXT("VSync: %s"), OnOff(S && S->IsVSyncEnabled()));
		},
		[]() {
			if (UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr)
			{
				S->SetVSyncEnabled(!S->IsVSyncEnabled());
				S->ApplyNonResolutionSettings();
				S->SaveSettings();
			}
		});

	// Гамма (GEngine->DisplayGamma, UE-дефолт 2.2).
	// ★ 19.09.2026 (фаза 2): гамма = «Яркость» профиля игрока (полоса 70058 окна настроек), см. ApplyGammaPersist.
	AddCycle(
		[]() { return FString::Printf(TEXT("%s: %.1f"), DmT(TEXT("Гамма (= «Яркость» окна настроек)"), TEXT("Gamma (= \"Brightness\" in Options)")), GEngine ? GEngine->DisplayGamma : 2.2f); },
		[this]() {
			static const float Vals[] = { 1.8f, 2.0f, 2.2f, 2.4f, 2.6f };
			const float Cur = GEngine ? GEngine->DisplayGamma : 2.2f;
			int32 Idx = 2;
			for (int32 i = 0; i < UE_ARRAY_COUNT(Vals); ++i) { if (FMath::IsNearlyEqual(Vals[i], Cur, 0.05f)) { Idx = (i + 1) % UE_ARRAY_COUNT(Vals); break; } }
			ApplyGammaPersist(Vals[Idx]);
		});

	AddLabel(DmT(TEXT("Требует перезапуска (★ = отличается от активного):"), TEXT("Requires restart (★ = differs from the active one):")), 11, NoteCol);
	// Графический API: DX12 (SM6, VSM — красивые тени) ↔ DX11 (SM5, CSM-фолбэк).
	AddCycle(
		[]() {
			const bool bPend = GetPendingRHIIsDx12();
			const bool bAct  = GetActiveRHIIsDx12();
			FString L = FString::Printf(TEXT("%s: %s"), DmT(TEXT("Графический API"), TEXT("Graphics API")),
				bPend ? DmT(TEXT("DX12 (VSM-тени)"), TEXT("DX12 (VSM shadows)")) : DmT(TEXT("DX11 (CSM-тени)"), TEXT("DX11 (CSM shadows)")));
			if (bPend != bAct) { L += FString::Printf(TEXT("  ★ %s %s"), DmT(TEXT("сейчас"), TEXT("now")), bAct ? TEXT("DX12") : TEXT("DX11")); }
			return L;
		},
		[]() { SetPendingRHI(!GetPendingRHIIsDx12()); });

	// ═══════════════ КАЧЕСТВО ═══════════════
	Cur = EAsdaDevTab::Quality;
	// ★ E-0b: пресет = уровень sg.* честно (Low 0 / Medium 1 / High 2 / Epic 3); Cinematic (4) в цикле нет — только консолью
	//   asda.Video.Preset cine; если он активен или группы вразнобой — подпись говорит об этом, клик ведёт на Low.
	AddCycle(
		[Video]() {
			const EAsdaQualityPreset P = Video->GetQualityPreset();
			FString L = FString::Printf(TEXT("%s: %s (sg %d)"), DmT(TEXT("Пресет качества"), TEXT("Quality preset")), UAsdaVideoSettingsSubsystem::PresetName(P), UAsdaVideoSettingsSubsystem::PresetToLevel(P));
			if (Video->IsQualityMixed()) { L += DmT(TEXT(" — группы вразнобой"), TEXT(" — mixed groups")); }
			else if (P == EAsdaQualityPreset::Cinematic) { L += DmT(TEXT(" — только консолью"), TEXT(" — console only")); }
			return L;
		},
		[Video]() {
			const int32 Next = Video->IsQualityMixed() ? 0 : ((int32)Video->GetQualityPreset() + 1) % 4;   // …→Epic→Low; Cinematic(4)→Low
			Video->SetQualityPreset((EAsdaQualityPreset)Next);
		});
	// ★★★ 12.10.2026 (решение владельца): «Определить автоматически» = ТОТ ЖЕ замер настоящего кадра по лестнице [AsdaBenchLadder],
	//   что кнопка «Автоподбор» окна настроек 69500 (UAsdaSystemOptionSubsystem::OnAutoDetect → UAsdaBenchSubsystem::StartInPlace):
	//   модальное окно хода, применение по «ОК», «Пропустить» возвращает прежнее. Синтетический пробник железа
	//   (UAsdaVideoSettingsSubsystem::RunHardwareBenchmarkNow, SynthBenchmark ~5 с) из меню УБРАН — два разных «авто» путали
	//   (01_designer.md §1 п.9). Запуск отложен на следующий тик: панель закрывается, рушить её из обработчика своей же кнопки нельзя.
	AddButton(DmT(TEXT("[ Определить автоматически (замер кадра, как «Автоподбор») ]"), TEXT("[ Auto Detect (frame benchmark, same as \"Auto Detect\" in Options) ]")), [this]()
	{
		TWeakObjectPtr<UAsdaDevMenuSubsystem> WeakThis(this);
		AsdaDevMenuAfter(0.f, [WeakThis]() { if (UAsdaDevMenuSubsystem* M = WeakThis.Get()) { M->StartFrameBenchFromMenu(); } });
	});

	// ★★★ УРОВЕНЬ СВЕЧЕНИЯ 09.09.2026 — пункт оригинала LEVEL OF GLOW (config[32], 0..4). До этой волны он двигал
	//   только r.BloomQuality (число ступеней размытия, фактически «есть/нет»); теперь меняет и СИЛУ ореола: пара
	//   «интенсивность + порог» из таблицы [AsdaGlow] Config/DefaultScalability.ini едет в служебный PP-объём
	//   «PP_Glow» и действует и на картах, и на сцене логина. Ступень 4 = КАЛИБРОВКА ВЛАДЕЛЬЦА 09.09.2026
	//   (вариант v3_rec: интенсивность 4.0, порог 2.5), ступень 0 = свечения нет вовсе.
	//   Сброс вкладки возвращает 4 сам: ResetTab(Quality) → ApplyDefaultQuality → SetGlowLevel(4).
	// ★ 19.09.2026 (фаза 2): пункты — по эталону «на чём светится» (разбор в UAsdaVideoSettingsSubsystem::SetGlowLevel);
	//   хранилище одно — профиль игрока, его же пишет комбо 70121 окна настроек.
	// ★ 12.10.2026: строка «Bloom (штатный, пункт свечения 4)» убрана — она щёлкала тот же пункт 4↔0 и путала («Bloom выкл» = свечение 0).
	AddCycle(
		[Video]() {
			static const TCHAR* Names[5] = { TEXT("нет"), TEXT("персонаж (всё, кроме мобов)"), TEXT("монстр"),
			                                 TEXT("персонаж+монстр"), TEXT("все (кадр целиком)") };
			static const TCHAR* NamesEn[5] = { TEXT("none"), TEXT("characters (all but monsters)"), TEXT("monsters"),
			                                   TEXT("characters+monsters"), TEXT("all (whole frame)") };   // ★ 19.10.2026: EN-клиент
			const int32 L = Video->GetGlowLevel();
			const int32 Li = FMath::Clamp(L, 0, 4);
			const TCHAR* Title = DmT(TEXT("Свечение (LEVEL OF GLOW)"), TEXT("Glow (LEVEL OF GLOW)"));
			float I = 0.f, T = 0.f;
			return UAsdaVideoSettingsSubsystem::ReadGlowStep(L, I, T)
				? FString::Printf(TEXT("%s: %d — %s, %s %.2f, %s %.2f"), Title, L, DmT(Names[Li], NamesEn[Li]),
					DmT(TEXT("сила"), TEXT("intensity")), I, DmT(TEXT("порог"), TEXT("threshold")), T)
				: FString::Printf(TEXT("%s: %d — %s, %s"), Title, L, DmT(Names[Li], NamesEn[Li]),
					DmT(TEXT("нет строки в [AsdaGlow]"), TEXT("no row in [AsdaGlow]")));
		},
		[Video]() { Video->SetGlowLevel((Video->GetGlowLevel() + 1) % 5); });

	// ★ 12.10.2026: 8 ДЕЙСТВУЮЩИХ групп. «Листва» (foliage.DensityScale) и «Шейдинг» (hair strands / r.AnisotropicMaterials) убраны —
	//   на ISM-мир порта не действуют (perf2k §2.6: DC 7 700 ± 30 при любом уровне), пресет ставит их ровно уровню.
	//   Анизотропия (r.MaxAnisotropy) теперь только через «Текстуры» (BS TextureQuality@N) — отдельной строки нет.
	AddLabel(DmT(TEXT("Группы качества (0 Low · 1 Medium · 2 High · 3 Epic · 4 Cinematic):"), TEXT("Quality groups (0 Low · 1 Medium · 2 High · 3 Epic · 4 Cinematic):")), 12, SubCol);
	AddSg(DmT(TEXT("Текстуры"), TEXT("Textures")),       [](UGameUserSettings* S) { return S->GetTextureQuality(); },       [](UGameUserSettings* S, int32 L) { S->SetTextureQuality(L); });
	AddSg(DmT(TEXT("Тени"), TEXT("Shadows")),           [](UGameUserSettings* S) { return S->GetShadowQuality(); },        [](UGameUserSettings* S, int32 L) { S->SetShadowQuality(L); });
	AddSg(DmT(TEXT("Эффекты"), TEXT("Effects")),        [](UGameUserSettings* S) { return S->GetVisualEffectQuality(); },  [](UGameUserSettings* S, int32 L) { S->SetVisualEffectQuality(L); });
	AddSg(DmT(TEXT("Пост-обработка"), TEXT("Post-processing")), [](UGameUserSettings* S) { return S->GetPostProcessingQuality(); },[](UGameUserSettings* S, int32 L) { S->SetPostProcessingQuality(L); });
	AddSg(DmT(TEXT("Дальность"), TEXT("View distance")),      [](UGameUserSettings* S) { return S->GetViewDistanceQuality(); },  [](UGameUserSettings* S, int32 L) { S->SetViewDistanceQuality(L); });
	AddSg(DmT(TEXT("Сглаживание"), TEXT("Anti-aliasing")),    [](UGameUserSettings* S) { return S->GetAntiAliasingQuality(); },  [](UGameUserSettings* S, int32 L) { S->SetAntiAliasingQuality(L); });
	AddSg(DmT(TEXT("Отражения"), TEXT("Reflections")),      [](UGameUserSettings* S) { return S->GetReflectionQuality(); },    [](UGameUserSettings* S, int32 L) { S->SetReflectionQuality(L); });
	// ★ E-0b: группа UE, которой у оригинала нет — секции [GlobalIlluminationQuality@N] подготовлены в DefaultScalability.
	// ★ E-3 (07.09.2026): метод GI проекта = 1 (Lumen SW); уровни 0/1 держат r.Lumen.DiffuseIndirect.Allow=0 (DefaultScalability) — подпись
	//   «0 выкл · 1 DFAO · 2 Lumen · 3 Lumen+» соответствует фактическому поведению (setE3_report.md §3).
	AddSg(DmT(TEXT("Глобальное освещение"), TEXT("Global illumination")), [](UGameUserSettings* S) { return S->GetGlobalIlluminationQuality(); }, [](UGameUserSettings* S, int32 L) { S->SetGlobalIlluminationQuality(L); });
	AddLabel(GetCVarInt(TEXT("r.DynamicGlobalIlluminationMethod"), 0) == 0
		? DmT(TEXT("  (метод GI проекта выкл — уровень готовит секции для Lumen, E-5)"), TEXT("  (project GI method is off — the level prepares sections for Lumen, E-5)"))
		: DmT(TEXT("  (0 выкл · 1 DFAO · 2 Lumen · 3 Lumen+)"), TEXT("  (0 off · 1 DFAO · 2 Lumen · 3 Lumen+)")), 11, NoteCol);

	// ★★★ ОТСЕЧКА ДАЛЬНОСТИ ПРОРИСОВКИ (08.09.2026, drawdistance_report.md). До той волны группа «Дальность» выше НЕ ДЕЙСТВОВАЛА
	//   вовсе: у примитивов мира MaxDrawDistance = 0, а r.ViewDistanceScale уровня множит ноль (review_perf2k.md §5). Базовые
	//   дальности ставит UAsdaCullDistanceSubsystem, здесь — ползунок-множитель поверх них (поле профиля asda.Cull.Scale).
	//   ★ 12.10.2026 (владелец): ползунок ОСТАЁТСЯ как dev-инструмент подбора; рубильник «Отсечка дальности» (asda.Cull.Enable) и
	//   строка-диагностика «множитель уровня × ползунок» (asda.Cull.Dump) убраны в консоль. Грабля USlider::SetValue → OnValueChanged
	//   закрыта общим гейтом bSliderSetFromCode (см. RefreshRows).
	AddLabel(DmT(TEXT("Дальность прорисовки геометрии (множитель поверх r.ViewDistanceScale уровня):"), TEXT("Geometry draw distance (multiplier on top of the level's r.ViewDistanceScale):")), 12, SubCol);
	AddSlider(TEXT("asda.Cull.Scale"), 0.5f, 2.0f, 1.f, DmT(TEXT("Дальность прорисовки"), TEXT("Draw distance")));
	// ★ 14.10.2026 (античит, блок Б): в Shipping консоли и команды asda.Cull.Dump нет — подсказка без имени дев-команды
	//   (строка имени в бинаре не нужна; найдено проверкой строк Shipping-бинаря, wSHIP_logs/strings_ship.txt).
	AddLabel(ASDA_DEV_TEXT(DmT(TEXT("  радиусы по классам — Config/DefaultGame.ini; откат asda.Cull.Enable 0, таблица asda.Cull.Dump — консоль"),
			TEXT("  per-class radii — Config/DefaultGame.ini; revert: asda.Cull.Enable 0, table: asda.Cull.Dump — console")),
		DmT(TEXT("  радиусы по классам — Config/DefaultGame.ini"), TEXT("  per-class radii — Config/DefaultGame.ini"))), 10, FLinearColor(0.55f, 0.55f, 0.55f, 1.f));

	// ★ 12.10.2026: из «Сглаживание и резкость» + «Пост-эффекты» остались метод AA, резкость и motion blur (стандарт большинства игр);
	//   Bloom (дубль свечения), Lens flare (решение 10.09 «блики выключены совсем»), анизотропия (→ «Текстуры») и смещение мипов
	//   (dev-тонкость, с FXAA отрицательный bias мерцает) убраны. Секция «Нагрузка на процессор» (asda.Vis.Enable, шаг облаков) — в консоль.
	AddLabel(DmT(TEXT("Сглаживание и пост-эффекты:"), TEXT("Anti-aliasing and post effects:")), 12, SubCol);
	// Сглаживание (0 выкл / 1 FXAA / 2 TAA / 4 TSR; 3=MSAA только forward — пропускаем). Поле профиля игрока (= E1 окна настроек).
	AddCycle(
		[]() {
			const TCHAR* Title = DmT(TEXT("Метод сглаживания"), TEXT("Anti-aliasing method"));
			switch (GetCVarInt(TEXT("r.AntiAliasingMethod"), 2))
			{
				case 0: return FString::Printf(TEXT("%s: %s"), Title, DmT(TEXT("выкл"), TEXT("off")));
				case 1: return FString::Printf(TEXT("%s: FXAA"), Title);
				case 2: return FString::Printf(TEXT("%s: TAA"), Title);
				case 4: return FString::Printf(TEXT("%s: TSR"), Title);
				default: return FString::Printf(TEXT("%s: %d"), Title, GetCVarInt(TEXT("r.AntiAliasingMethod")));
			}
		},
		[CycleInt]() { CycleInt(TEXT("r.AntiAliasingMethod"), { 4, 2, 1, 0 }); });
	// Резкость тонмаппера (решение владельца 05.09: 1.2 или 0.6; средняя 0.9 для сравнения). Поле профиля (= E2).
	AddCycle(
		[]() { return FString::Printf(TEXT("%s: %.1f"), DmT(TEXT("Резкость (Sharpen)"), TEXT("Sharpness (Sharpen)")), GetCVarFloat(TEXT("r.Tonemapper.Sharpen"), 1.2f)); },
		[CycleFloat]() { CycleFloat(TEXT("r.Tonemapper.Sharpen"), { 0.6f, 0.9f, 1.2f }); });
	// Motion blur (у проекта выключен через r.DefaultFeature.MotionBlur=False → включаем override-амаунтом). Поле профиля (= E5).
	AddCycle(
		[OnOff]() { return FString::Printf(TEXT("Motion blur: %s"), OnOff(GetCVarFloat(TEXT("r.MotionBlur.Amount"), 0.f) > 0.f)); },
		[this]() {
			const bool bOn = GetCVarFloat(TEXT("r.MotionBlur.Amount"), 0.f) > 0.f;
			ApplyCVarPersist(TEXT("r.MotionBlur.Amount"), bOn ? TEXT("0") : TEXT("0.5"));
		});

	// ═══════════════ НЕБО И ТУМАН ═══════════════
	Cur = EAsdaDevTab::Sky;
	// ★ 12.10.2026: рубильник «Туман (r.Fog)» убран — туман часть вида карты (perf2k §2.5 «нельзя»), ключ остаётся консольным;
	//   «Тени облаков» (asda.Clouds.Shadows) убраны — уровень решает через r.LightFunctionQuality (BS 0/1/1/1/1).
	// ★ Ползунки тумана (единая механика USlider): плотность и дальность начала поверх авторских значений карты.
	//   Дальность: StartDistance = авторская × k (туман ближе при <1, дальше при >1); плотность делится на k, чтобы
	//   дальний туман не сгущался (см. ApplyFogScale). Совместно с плотностью: FogDensity = авторская × DensityScale / DistanceScale.
	AddLabel(DmT(TEXT("Туман (dev-подбор поверх авторских значений карты):"), TEXT("Fog (dev tuning on top of the map's authored values):")), 12, SubCol);
	AddSlider(TEXT("asda.Fog.DensityScale"),  0.25f, 2.0f, 1.f, DmT(TEXT("Плотность тумана"), TEXT("Fog density")));
	AddSlider(TEXT("asda.Fog.DistanceScale"), 0.5f,  2.0f, 1.f, DmT(TEXT("Дальность тумана"), TEXT("Fog distance")));
	AddLabel(DmT(TEXT("Облака:"), TEXT("Clouds:")), 12, SubCol);
	AddToggle(DmT(TEXT("3D-облака"), TEXT("3D clouds")), TEXT("asda.Clouds.Field"), 1);   // поле профиля (= E7 «Объёмные облака»)
	// Купол неба (SkyBoost/StarIntensity) — ПЕР-КАРТОЧНЫЕ MIC MI_skydome без глобального MPC: глобального рычага нет,
	// правится в редакторе (sky_audit_report.md §4.1). Здесь строки нет намеренно.
	AddLabel(DmT(TEXT("Яркость купола и звёзды — в MIC карты (MI_skydome), глобального рычага нет."), TEXT("Sky dome brightness and stars are set in the map's MIC (MI_skydome); there is no global control.")), 10, NoteCol);

	// ═══════════════ РЕЛЬЕФ (MPC_AsdaNormals) ═══════════════
	Cur = EAsdaDevTab::Relief;
	// ★ 12.10.2026: из шести параметров осталась одна ручка — сила нормали (дефолт ассета 0.5 — выбор владельца). Enable и
	//   RoughFromMap ставит уровень [AsdaQuality@N] (Low: рельеф выкл; Medium: шероховатость из карты выкл); AoAmount/Delight/
	//   EmissiveGain — авторские «0 = рекомендуемое», правятся в ассете. Старые ключи [AsdaNormals] убранных строк снимает чистка при старте.
	AddLabel(DmT(TEXT("Рельеф из карт нормалей (MPC_AsdaNormals; включение и шероховатость — по уровню):"), TEXT("Relief from normal maps (MPC_AsdaNormals; enable and roughness follow the quality level):")), 12, SubCol);
	AddCycle(
		[this]() { return FString::Printf(TEXT("%s: %.1f"), DmT(TEXT("Сила нормали (NormalStrength)"), TEXT("Normal strength (NormalStrength)")), GetNormalValue(GNormalParams[kNormalStrengthIdx])); },
		[CycleNormal]() { CycleNormal(GNormalParams[kNormalStrengthIdx], { 0.5f, 1.f, 1.5f }); });
	AddLabel(DmT(TEXT("  Enable/RoughFromMap — [AsdaQuality@N] Config/DefaultScalability.ini; AO/делайтинг/свечение из карты — дефолты ассета"), TEXT("  Enable/RoughFromMap — [AsdaQuality@N] Config/DefaultScalability.ini; AO/delighting/emissive from the map — asset defaults")), 10, FLinearColor(0.55f, 0.55f, 0.55f, 1.f));

	// ═══════════════ ПЕРСОНАЖИ ═══════════════
	Cur = EAsdaDevTab::Chars;
	AddLabel(DmT(TEXT("Рим-подсветка (MPC_AsdaToon, значения актора Rim_<карта>):"), TEXT("Rim light (MPC_AsdaToon, values of the Rim_<map> actor):")), 12, SubCol);
	AddCycle(
		// ★ 09.11.2026: при «Без рима (как в оригинале)» = вкл рим фактически выключен — подпись говорит об этом.
		[]() { return FString::Printf(TEXT("%s: %s%s"), DmT(TEXT("Рим"), TEXT("Rim")), GetCVarInt(TEXT("asda.Rim.Enable"), 1) != 0 ? DmT(TEXT("по карте"), TEXT("per map")) : DmT(TEXT("выкл"), TEXT("off")),
			GetCVarInt(TEXT("asda.Rim.OriginalOff"), 1) != 0 ? DmT(TEXT(" (заглушён строкой ниже)"), TEXT(" (muted by the row below)")) : TEXT("")); },
		[CycleInt]() { CycleInt(TEXT("asda.Rim.Enable"), { 1, 0 }); });
	// ★ 09.11.2026 [ОРИГИНАЛ] dungeon_mob_purple: рима в оригинале нет; 1 (умолч.) глушит рим на всех картах.
	AddToggle(DmT(TEXT("Без рима (как в оригинале)"), TEXT("No rim (as in the original)")), TEXT("asda.Rim.OriginalOff"), 1);
	AddCycle(
		[]() { return FString::Printf(TEXT("  %s: ×%.1f"), DmT(TEXT("сила рима"), TEXT("rim strength")), GetCVarFloat(TEXT("asda.Rim.Scale"), 1.f)); },
		[CycleFloat]() { CycleFloat(TEXT("asda.Rim.Scale"), { 0.5f, 1.f, 1.5f }); });
	// Капсульные тени тел (asda.Shadow.Capsule / CapsuleMinVisibility). ★ 12.10.2026: ЕДИНСТВЕННАЯ строка — дубль во вкладке
	//   теней убран вместе с вкладкой (01_designer.md §1 п.40/58). r.CapsuleShadows движка ставит уровень (BS 0/1/1/1/1),
	//   MinVisibility 0.1 — решение владельца 05.09 (дефолт регистрации CVar).
	AddLabel(DmT(TEXT("Тени персонажей:"), TEXT("Character shadows:")), 12, SubCol);
	AddToggle(DmT(TEXT("Капсульные тени тел"), TEXT("Capsule body shadows")), TEXT("asda.Shadow.Capsule"), 1);
	AddCycle(
		[]() { return FString::Printf(TEXT("  %s: %.2f"), DmT(TEXT("сила пятна (MinVisibility)"), TEXT("shadow strength (MinVisibility)")), GetCVarFloat(TEXT("asda.Shadow.CapsuleMinVisibility"), 0.1f)); },
		[CycleFloat]() { CycleFloat(TEXT("asda.Shadow.CapsuleMinVisibility"), { 0.1f, 0.25f, 0.5f }); });
	// Листва TwoSidedFoliage — StaticSwitch в 85 MIC листвы (sky_audit_report.md §4.1): рантайм-рычага нет.
	//   ★ 12.10.2026: строка «Skin cache (рестарт)» убрана — r.SkinCache.CompileShaders всегда 1 (выключение = хитчи первого кадра).
	//   Её прежняя запись в ПОЛЬЗОВАТЕЛЬСКИЙ Engine.ini снимается при старте (SweepRetiredSkinCache, ревизия СРЕДН-2).
	AddLabel(DmT(TEXT("Листва TwoSidedFoliage — StaticSwitch в MIC, живого рычага нет."), TEXT("Foliage TwoSidedFoliage is a StaticSwitch in the MIC; there is no live control.")), 10, NoteCol);

	// ═══════════════ СБРОС ═══════════════
	Cur = EAsdaDevTab::Reset;
	AddLabel(DmT(TEXT("Сбросить вкладку к дефолтам проекта:"), TEXT("Reset a tab to project defaults:")), 12, SubCol);
	for (int32 i = 0; i < (int32)EAsdaDevTab::Reset; ++i)
	{
		const EAsdaDevTab T = (EAsdaDevTab)i;
		AddButton(FString::Printf(TEXT("[ %s: %s ]"), DmT(TEXT("Сбросить"), TEXT("Reset")), DevTabTitle(i)), [this, T]() { ResetTab(T); });
	}
	AddLabel(TEXT(" "), 6, FLinearColor::White);
	AddButton(DmT(TEXT("[ Сбросить ВСЁ к дефолтам проекта ]"), TEXT("[ Reset ALL to project defaults ]")), [this]() { ResetToProjectDefaults(); });

	W->AddToViewport(5000);
	MenuWidget = W;
	SetActiveTab(ActiveTab);
}

// ★ 12.10.2026: «Определить автоматически» — тот же замер кадра на месте, что «Автоподбор» окна настроек (OnAutoDetect).
//   Панель закрывается до запуска: окно замера модальное, его «ОК/Пропустить» сами пишут профиль; меню после замера
//   открывается заново Ctrl+N и покажет новые уровни/масштаб.
void UAsdaDevMenuSubsystem::StartFrameBenchFromMenu()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaBenchSubsystem* Bench = GI ? GI->GetSubsystem<UAsdaBenchSubsystem>() : nullptr;
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	if (!Bench || !W) { UE_LOG(LogAsdaDevMenu, Warning, TEXT("[меню] «Определить автоматически»: нет подсистемы замера или мира — замер не запущен")); return; }
	if (Bench->IsRunning()) { UE_LOG(LogAsdaDevMenu, Warning, TEXT("[меню] «Определить автоматически»: замер уже идёт")); return; }
	CloseMenu();
	if (Bench->StartInPlace(W)) { UE_LOG(LogAsdaDevMenu, Log, TEXT("[меню] «Определить автоматически»: запущен замер кадра на месте (окно замера, как «Автоподбор» окна настроек)")); }
	else                        { UE_LOG(LogAsdaDevMenu, Warning, TEXT("[меню] «Определить автоматически»: замер на месте не запустился (StartInPlace=false)")); }
}

// ─────────────────────────── Самопроверка путём игрока ───────────────────────────
// Ширины подписей без переноса против доступной ширины (содержимое − паддинг кнопки UButton 12+12 − рамка).
int32 UAsdaDevMenuSubsystem::LogRowWidths()
{
	if (!MenuWidget || !FSlateApplication::IsInitialized()) { UE_LOG(LogAsdaDevMenu, Warning, TEXT("SelfTest: меню закрыто")); return -1; }
	const float Avail = PanelContentWidth - 24.f - 4.f;
	TSharedRef<FSlateFontMeasure> FM = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	int32 Wide = 0;
	if (UWidget* Panel = MenuWidget->WidgetTree ? MenuWidget->WidgetTree->FindWidget(TEXT("Panel")) : nullptr)
	{
		const FGeometry& G = Panel->GetCachedGeometry();
		UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest: панель экран %.0f×%.0f @ (%.0f,%.0f); ширина содержимого %.0f, макс. высота %.0f"), G.GetAbsoluteSize().X, G.GetAbsoluteSize().Y, G.GetAbsolutePosition().X, G.GetAbsolutePosition().Y, PanelContentWidth, PanelMaxContentHeight);
	}
	if (UWidget* Tabs = MenuWidget->WidgetTree ? MenuWidget->WidgetTree->FindWidget(TEXT("Tabs")) : nullptr)
	{
		const FGeometry& G = Tabs->GetCachedGeometry();
		UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest: ряд табов экран %.0f×%.0f (одна строка ≈ 30 px, две ≈ 60)"), G.GetAbsoluteSize().X, G.GetAbsoluteSize().Y);
	}
	for (int32 i = 0; i < TabTexts.Num(); ++i)
	{
		if (!TabTexts[i].IsValid()) { continue; }
		const FGeometry& G = TabTexts[i]->GetCachedGeometry();
		UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest: таб %d «%s» экран %.0f×%.0f"), i, *TabTexts[i]->GetText().ToString(), G.GetAbsoluteSize().X, G.GetAbsoluteSize().Y);
	}
	for (const FRow& R : Rows)
	{
		if (!R.Text.IsValid()) { continue; }
		const FString Txt = R.Text->GetText().ToString();
		const float Wd = FM->Measure(Txt, R.Text->GetFont()).X;
		const bool bWide = Wd > Avail;
		Wide += bWide ? 1 : 0;
		UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest: [вкл %d] %s%.0f/%.0f «%s»"), (int32)R.Tab, bWide ? TEXT("ШИРЕ ") : TEXT(""), Wd, Avail, *Txt);
	}
	UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest: строк %d, шире поля (перенос на 2 строки): %d"), Rows.Num(), Wide);
	return Wide;
}
