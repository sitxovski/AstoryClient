// Copyright (c) 2026 @sitxovski (Discord)
// AsdaBoosterShopSubsystem_Window.cpp — ★ R3-c 12.11.2026 (разрез AsdaBoosterShopSubsystem.cpp, дословный перенос): окно: высота панели, виджет, зона, контролы, ячейки, открыть/закрыть, категория и страница.
//   Общее для частей — в AsdaBoosterShopSubsystem_Private.h; шапка и реверс — в AsdaBoosterShopSubsystem.cpp.
#include "UI/AsdaBoosterShopSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaNetSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaLoginData.h"            // ★ 02.11.2026 P101: обрезка имени 15519 по байтам провода (LimitText 20)
#include "UI/AsdaBoosterPanelSubsystem.h"   // ★ окно предпросмотра 15600 — общая точка, уже есть
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::ShownVisibilityFor — общая точка видимости узла
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaJuiComboBox.h"
#include "UI/AsdaPurchaseBoxSubsystem.h"    // ★ общая модалка 1559, режим F
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/EditableText.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "Internationalization/Regex.h"
#include "TimerManager.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "UI/AsdaCashGiftSubsystem.h"       // ★ 01.11.2026 P101_CashGift: подарок 5072/5074 — общее состояние g_pShopWnd
#include "AsdaBoosterShopSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaBoosterShopSubsystem*.cpp

namespace {
	const TCHAR* kWbpPath  = TEXT("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_BOOSTER_SHOP_WIN");

	const TCHAR* kMsgBaseWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_MsgBase");

	// ── Статические подписи окна и панели покупки ──
	constexpr int32 kLocSexChk    = 12015;  // 15304 «Искать для своего пола»
	constexpr int32 kLocCashLbl   = 12904;  // 15510 «Сейчас GXP»
	constexpr int32 kLocPreCash   = 12959;  // 40004 «Деньги в подарок»
	constexpr int32 kLocCellGift  = 6821;   // 15307 «Подарок»
	constexpr int32 kLocCellOk    = 6944;   // 15311 «OK»

	constexpr int32 kLocQtyLbl    = 13233;  // 15539 «Количество (шт.)»
	constexpr int32 kLocPriceLbl  = 13234;  // 15521 «Цена»
	constexpr int32 kLocCashLbl2  = 13235;  // 15524 «Золото:» (★ на деле GXP)

	constexpr int32 kLocWare      = 6822;   // 15529 «В хранилище»
	// ★ 15527 — подпись кнопки покупки. НОВАЯ строка **990001 «Купить»**, заведена в
	//   `RawAssets/Text/GameText.csv` по требованию владельца 2026-08-28. В ОРИГИНАЛЕ здесь
	//   loc 6857 «OK», но её нельзя переименовывать: тот же id читают `UAsdaPurchaseBoxSubsystem`
	//   (подпись 1561 общей модалки покупки 1559 — её делят премиум-магазин 6000, магазин
	//   снаряжения и гача) и `UAsdaWarShopSubsystem` (кнопка покупки карточки). Поэтому заведена
	//   отдельная строка, и на неё переведён ТОЛЬКО бустер-шоп; те окна остались на 6857.
	//   Id 990001 взят выше всего занятого диапазона таблицы (максимум был 900002).
	constexpr int32 kLocBtnOk     = 990001; // 15527 «Купить» (в оригинале 6857 «OK»)
	constexpr int32 kLocBtnCancel = 2289;   // 15530 «Отмена»
	constexpr int32 kLocBtnConfirm= 2288;   // 15520 «ОК»
	constexpr int32 kLocBtnPrev   = 13237;  // 15547 «Вид»
	constexpr int32 kLocGuide0    = 6960;   // 15590 «-»
	constexpr int32 kLocGuide1    = 6961;   // 15591 «[Предмет нельзя вернуть или обменять]»
	constexpr int32 kLocMsgBase   = 60000;  // 461 «Оружие развито до предела.»

	// ═══════════ КЛЮЧИ ОТКАТА ДЕФЕКТОВ ОРИГИНАЛА (закон `fix-original-defects`) ═══════════
	// Д-2 — панель категорий 15545: `Visible="0"` в `.JUI`, `SetVisible` в коде НЕТ ⇒ семь вкладок
	//   и вся ветка «Популярные» (Fpremium) игроку недоступны, хотя обработчик и фильтр живые.
	static ASDA_DEV_CVAR(int32, CVarShowCategories,
		TEXT("asda.PremiumMall.ShowBoosterCategories"), 1,
		TEXT("1 = показывать панель категорий 15545 магазина бустеров (исправление Д-2: в оригинале "
		     "она Visible=0 и SetVisible к ней нет ⇒ 7 категорий недоступны); 0 = как оригинал."),
		ECVF_Default);

	// ── Собрать поддерево виджета целиком (общая идиома окна 6000) ──
	void CollectTree(UWidget* Root, TArray<TObjectPtr<UWidget>>& Out)
	{
		if (!Root) { return; }
		Out.Add(Root);
		TArray<UWidget*> Stack; Stack.Add(Root);
		while (Stack.Num() > 0)
		{
			UWidget* Cur = Stack.Pop(EAllowShrinking::No);
			if (UPanelWidget* Pan = Cast<UPanelWidget>(Cur))
			{
				for (int32 i = 0; i < Pan->GetChildrenCount(); ++i)
				{
					if (UWidget* Kid = Pan->GetChildAt(i)) { Out.Add(Kid); Stack.Add(Kid); }
				}
			}
		}
	}
}

// ★ Порт пересчёта высоты подложки 15599 (`ida.c:421847-421852`): `h = 98 * (cells / 3) + 48`.
//
// ★★★ ДЕФЕКТ ПОРТА (жалоба владельца 2026-08-28: «белая рамка не подстраивается под содержимое и
//   заканчивается раньше бустеров»). Эталон зовёт ОДИН `CControl::SetSize(15599, w, h)`, и девять
//   кусков скина `Win_Tab` движок пересчитывает сам. У нас иерархия .JUI РАСПЛЮЩЕНА (закон
//   `jui-flattened-hierarchy-geometry`): рамка — девять отдельных `UImage` с АБСОЛЮТНЫМИ слотами
//   внутри группы, и смена размера группы на них не влияет. Поэтому рамка оставалась ровно той
//   высоты, что записана в .JUI (550 px = 5 + 534 + 11), а карточки уходили ниже неё: при 21
//   ячейке нижний ряд карточек кончается на 716, а рамка — на 550, то есть обрывалась на 6-м ряду.
//   Правим В КОДЕ, а не в ассете, потому что высота ЗАВИСИТ ОТ РАНТАЙМА: число ячеек на странице
//   даёт `CellsPerPage()` по высоте экрана (21 при ≥1024, иначе 15) ⇒ 734 либо 538 px.
void UAsdaBoosterShopSubsystem::ApplyPanelHeight()
{
	if (!Part2) { return; }
	UCanvasPanelSlot* S = AsdaUi::SlotOf(Part2.Get());
	if (!S) { return; }
	const int32 PerPage = CellsPerPage();
	const float H = 98.f * (float)(PerPage / 3) + 48.f;
	if (FMath::IsNearlyEqual(PanelHeightApplied, H)) { return; }
	PanelHeightApplied = H;

	FAnchorData Lay = S->GetLayout();
	FMargin Off = Lay.Offsets;
	Off.Bottom = H;
	Lay.Offsets = Off;
	S->SetLayout(Lay);
	// Сама рамка — общей точкой (её же телом чинится подложка 6983 в магазине снаряжения).
	const bool bFrame = AsdaUi::StretchFramePlanesHeight(PanelPlanes, H);
	UE_LOG(LogAsdaBoosterShop, Log,
		TEXT("[магазин бустеров] подложка 15599: ячеек на странице %d ⇒ высота %.0f (98*%d+48), ")
		TEXT("рамка 9-slice %s"),
		PerPage, H, PerPage / 3, bFrame ? TEXT("растянута") : TEXT("НЕ НАЙДЕНА"));
}

// ═══════════════════════════════ ВИДЖЕТ ════════════════════════════════════════════════════════
bool UAsdaBoosterShopSubsystem::EnsureWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWbpPath, kShopZ).Ensure(GetGameInstance(), Widget, bReady,
		[this](UAsdaWindowSubsystem& WM)
		{
			WM.UnregisterUiZone(kWindowId);
			WM.CloseWindow(kWindowId);
			Widget = nullptr; bReady = false; bOpen = false;
		},
		[this](UAsdaWindowSubsystem&)
		{
			SetupControls();
			RegisterZone();
		},
		[]()
		{
			UE_LOG(LogAsdaBoosterShop, Error, TEXT("[магазин бустеров] WBP_BOOSTER_SHOP_WIN не загрузился (%s)"), kWbpPath);
		},
		[this](UAsdaWindowSubsystem&)
		{
			bOpen = false;
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bReady;
}

void UAsdaBoosterShopSubsystem::RegisterZone()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// ★ Волна 3: зона по планам рамки — общая точка `FAsdaJuiWindowHandle::RegisterFrameZone` (`!bOpen || !Widget` → нет зоны).
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kWindowId, this, Widget, bOpen, TEXT("BOOSTER_SHOP_WIN_0_%d"), 9);
	// ★ Окно NPC: игрок тронулся с места ⇒ закрыть (порт `UI_resetAllWindowsAndLeave_5045`,
	//   который гасит бустер-шоп вызовом `boosterShopCloseWindow`). Регистрируем ЗАКРЫВАТЕЛЬ в
	//   общем реестре окон NPC — своей копии проверки движения не заводим.
	WM->RegisterNpcWindowCloser(kWindowId, FAsdaNpcWindowCloser::CreateWeakLambda(this,
		[this]() { if (bOpen) { CloseWindow(); } }));
}

void UAsdaBoosterShopSubsystem::SetupControls()
{
	bReady = false;
	Relays.Reset();
	HiddenByCode.Reset();
	PurTree.Reset();
	CatIdleColors.Reset();
	// ★ Виджет пересоздан ⇒ рамка подложки снова той высоты, что записана в .JUI (550 px).
	//   Без сброса кэша `ApplyPanelHeight` посчитал бы высоту «уже применённой» и вышел бы сразу,
	//   и при ПОВТОРНОМ открытии магазина рамка опять обрывалась бы на 6-м ряду.
	PanelHeightApplied = -1.f;
	if (!Widget || !Widget->WidgetTree) { return; }

	auto F = [this](const FString& N) -> UWidget* { return AsdaUi::FindNodeByName(Widget, *N, nullptr); };
	auto T = [&F](const FString& N) -> UTextBlock* { return Cast<UTextBlock>(F(N)); };
	auto SetLoc = [this](UTextBlock* Tb, int32 Id)
	{
		if (!Tb) { return; }
		Tb->SetAutoWrapText(false);
		const FString S = Loc(Id);
		if (!S.IsEmpty()) { Tb->SetText(FText::FromString(S)); }
	};

	Part2      = F(TEXT("Booster_Shop_Part2_grp"));
	// ★★★ ДЕВЯТЬ ПЛАНОВ РАМКИ подложки 15599 (`Booster_Shop_Part2_11_0…8`, скин `Win_Tab` 3×3).
	//   Имя плана = `{idc}_{индекс контрола в .JUI}_{номер плана}` (закон `jui-importer-widget-
	//   naming`), а индекс контрола (`11`) прибит к порядку .JUI ⇒ не хардкодим его: берём ПРЯМЫХ
	//   детей группы и разбираем их имена штатной точкой `ParsePlaneName`.
	PanelPlanes.Reset();
	PanelPlanes.SetNum(9);
	if (UPanelWidget* PP = Cast<UPanelWidget>(Part2.Get()))
	{
		for (int32 i = 0; i < PP->GetChildrenCount(); ++i)
		{
			UWidget* Ch = PP->GetChildAt(i);
			FString Idc; int32 JuiIdx = -1, Plane = -1;
			if (!Ch || !AsdaUi::ParsePlaneName(Ch->GetName(), Idc, JuiIdx, Plane)) { continue; }
			if (Idc != TEXT("Booster_Shop_Part2") || Plane < 0 || Plane > 8) { continue; }
			PanelPlanes[Plane] = Ch;
		}
	}
	for (const TObjectPtr<UWidget>& W : PanelPlanes)
	{
		if (!W) { PanelPlanes.Reset(); break; }
	}
	BtnClose   = Cast<UButton>(F(TEXT("BOOSTER_SHOP_CLOSE")));
	BtnNext    = Cast<UButton>(F(TEXT("BOOSTER_SHOP_NEXTP")));
	BtnBack    = Cast<UButton>(F(TEXT("BOOSTER_SHOP_BACP")));
	// ★★★ ИМЕНА КОНТРОЛОВ ВРУТ (Д-5, закон «именам контролов не верь»):
	//   `FIRSTP` 15505 ведёт на ПОСЛЕДНЮЮ страницу (`self[46] = self[47]`, `ida.c:422576-422579`),
	//   `ENDP`   15507 — на ПЕРВУЮ (`self[46] = 1`, `ida.c:422580-422583`).
	BtnLast    = Cast<UButton>(F(TEXT("BOOSTER_SHOP_FIRSTP")));
	BtnFirst   = Cast<UButton>(F(TEXT("BOOSTER_SHOP_ENDP")));
	TxtPage    = T(TEXT("BOOSTER_SHOP_NUM1"));
	TxtPages   = T(TEXT("BOOSTER_SHOP_NUM2"));
	TxtCashLbl = T(TEXT("BOOSTER_SHOP_TXT1"));
	TxtCash    = T(TEXT("BOOSTER_SHOP_MONEY"));
	BtnCharge  = Cast<UButton>(F(TEXT("BOOSTER_SHOP_CHARGE")));
	BtnBalance = Cast<UButton>(F(TEXT("Booster_CashCheck_Btn")));
	// ★ Рамка выбора 15496 — восемь картинок `BOOSTER_ITEM_SEL_<n>_k` (группы у неё НЕТ).
	SelFrame.Reset();
	SelFrameDelta.Reset();
	{
		TArray<UWidget*> AllW; Widget->WidgetTree->GetAllWidgets(AllW);
		for (UWidget* W : AllW)
		{
			if (W && W->GetName().StartsWith(TEXT("BOOSTER_ITEM_SEL_"))) { SelFrame.Add(W); }
		}
		if (SelFrame.Num() > 0)
		{
			FVector2D Base(0.f, 0.f);
			if (const UCanvasPanelSlot* S0 = AsdaUi::SlotOf(SelFrame[0].Get()))
			{
				Base = FVector2D(S0->GetLayout().Offsets.Left, S0->GetLayout().Offsets.Top);
			}
			for (const TObjectPtr<UWidget>& W : SelFrame)
			{
				const UCanvasPanelSlot* S = AsdaUi::SlotOf(W.Get());
				const FVector2D P = S ? FVector2D(S->GetLayout().Offsets.Left, S->GetLayout().Offsets.Top) : Base;
				SelFrameDelta.Add(P - Base);
			}
		}
	}
	CatPanel   = F(TEXT("BOOSTER_SHOP_MENU_grp"));

	SetLoc(TxtCashLbl, kLocCashLbl);
	if (TxtPage)  { TxtPage->SetAutoWrapText(false); }
	if (TxtPages) { TxtPages->SetAutoWrapText(false); }
	if (TxtCash)  { TxtCash->SetAutoWrapText(false); }

	// ── Категории 15545: 7 кнопок + 7 подписей (подписи 2…7 кладёт шаг `asda_ui_tabbar_captions`) ──
	for (int32 i = 0; i < kCats; ++i)
	{
		CatButtons[i] = Cast<UButton>(F(FString::Printf(TEXT("BOOSTER_SHOP_MENU_OPT%d"), i)));
		CatLabels[i]  = T(i == 0 ? FString(TEXT("BOOSTER_SHOP_MENU_Text"))
		                         : FString::Printf(TEXT("BOOSTER_SHOP_MENU_Text%d"), i + 1));
		SetLoc(CatLabels[i].Get(), kCatLoc[i]);
	}
	// [ПОРТ] 05.11.2026: сегменты-фоны кнопок и торцевая плашка полосы (структура — проба 05.11: `BOOSTER_SHOP_MENU_332_<i>`
	//   L=80·i, кнопки `OPT<i>` L=80·i, торец `BOOSTER_SHOP_MENU_RBg_333_0` L=560 = 80·7). Их гасит/двигает `ApplyTabLayout`.
	CatSegments.SetNum(kCats);
	for (int32 i = 0; i < kCats; ++i) { CatSegments[i] = F(FString::Printf(TEXT("BOOSTER_SHOP_MENU_332_%d"), i)); }
	CatEndCap = F(TEXT("BOOSTER_SHOP_MENU_RBg_333_0"));
	CatEndCapBase = FVector2D::ZeroVector;
	if (const UCanvasPanelSlot* S = AsdaUi::SlotOf(CatEndCap.Get()))
	{
		CatEndCapBase = FVector2D(S->GetLayout().Offsets.Left, S->GetLayout().Offsets.Top);
	}
	CatSlotStep = 0.f;
	{
		const UCanvasPanelSlot* S0 = AsdaUi::SlotOf(CatButtons[0].Get());
		const UCanvasPanelSlot* S1 = AsdaUi::SlotOf(CatButtons[1].Get());
		if (S0 && S1) { CatSlotStep = S1->GetLayout().Offsets.Left - S0->GetLayout().Offsets.Left; }
	}
	ApplyTabLayout();
	// ★★★ Д-2: в оригинале панель категорий `Visible="0"` и `SetVisible` к ней НЕТ.
	if (CatPanel)
	{
		const bool bShow = CVarShowCategories.GetValueOnGameThread() != 0;
		AsdaUi::SetShownDecor(CatPanel, bShow);
		if (!bShow) { HiddenByCode.Add(CatPanel.Get()); }
	}

	// ── Что эталон ГАСИТ в Init ──
	//   40004/40005 «Деньги в подарок» (`ida.c:420382-420384`, Д-11) и чекбокс пола 15303/15304
	//   (`Visible=0` в `.JUI`, `SetVisible` нет; фильтр по полу не реализован — Д-3/Д-4).
	for (const TCHAR* N : { TEXT("Booster_PreCashTxt_grp"), TEXT("Booster_PreCash_grp"),
	                        TEXT("BOOSTER_SHOP_SEXCHK_grp") })
	{
		if (UWidget* W = F(N)) { W->SetVisibility(ESlateVisibility::Collapsed); HiddenByCode.Add(W); }
	}
	// Подписи, которые эталон всё равно ставит (даже у скрытых узлов) — 1:1.
	SetLoc(T(TEXT("BOOSTER_SHOP_SEXTXT")), kLocSexChk);
	SetLoc(T(TEXT("Booster_PreCashTxt")),  kLocPreCash);

	SetupCells();

	// ── Панель покупки 15515 ──
	CollectTree(F(TEXT("BOOSTER_PUR_WIN_grp")), PurTree);
	// ★ 08.11.2026 (пачка 18, баг 2) [ОРИГИНАЛ]: 15515 — контрол внутри окна (`uiPopulateItemComposeDialog@0xB81E70` только
	//   `SetVisible`, ui_core.c:9762; модальной её эталон не делает). Нажатие по её телу эталон отдаёт ей (`uiContainerHandleMouse`), а не
	//   кнопкам ячеек под ней; у порта тело прозрачно для Slate — щелчок уходил в «Купить»/«Подарок» ячейки под панелью (проба
	//   `asda.Shop.ModalProbeSelfTest` Б: 1188 точек). Общая точка — `UAsdaWindowSubsystem::RegisterPressPanel` (ключ `asda.UI.PanelPress`).
	if (UAsdaWindowSubsystem* WMp = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		TWeakObjectPtr<UAsdaBoosterShopSubsystem> WeakSelf(this);
		WMp->RegisterPressPanel(FName(TEXT("BOOSTER_PUR_WIN")), kWindowId, [WeakSelf]()
		{
			const UAsdaBoosterShopSubsystem* S = WeakSelf.Get();
			return S && S->bOpen && S->PurMode != 0;
		},
		[WeakSelf]() -> const UWidget*
		{
			const UAsdaBoosterShopSubsystem* S = WeakSelf.Get();
			return (S && S->Widget) ? S->Widget->GetWidgetFromName(TEXT("BOOSTER_PUR_WIN_grp")) : nullptr;
		});
	}
	PurItemName = T(TEXT("BOOSTER_PUR_ITEM_NAME"));
	PurNarr     = T(TEXT("BOOSTER_PUR_NARR"));
	PurWeight   = T(TEXT("BOOSTER_PUR_TXT7"));
	PurIcon     = Cast<UImage>(F(TEXT("BOOSTER_PUR_ICON_289_0")));
	if (!PurIcon)
	{
		// Индекс узла импортёра меняется при переимпорте — ищем по регулярному выражению.
		TArray<UWidget*> All; Widget->WidgetTree->GetAllWidgets(All);
		const FRegexPattern Pat(TEXT("^BOOSTER_PUR_ICON_\\d+_0$"));
		for (UWidget* W : All)
		{
			if (!W) { continue; }
			FRegexMatcher M(Pat, W->GetName());
			if (M.FindNext()) { PurIcon = Cast<UImage>(W); break; }
		}
	}
	PurTxt1     = T(TEXT("BOOSTER_PUR_TXT1"));
	PurName     = Cast<UEditableText>(F(TEXT("BOOSTER_PUR_CHRNAME")));
	// ★ 02.11.2026 P101 (ревизия Н-1) [ОРИГИНАЛ]: LimitText 20 поля 15519 (.JUI) — общий ограничитель поля (символы и байты провода).
	GiftLimiters.Reset();
	if (PurName)
	{
		UAsdaTextLimiter* Lim = NewObject<UAsdaTextLimiter>(this);
		const TWeakObjectPtr<UEditableText> WeakEd = PurName.Get();
		Lim->Callback = [WeakEd](const FText& NewText)
		{
			UEditableText* Fld = WeakEd.Get();
			if (!Fld) { return; }
			const FString S = NewText.ToString();
			const FString Cut = FAsdaLoginData::TruncateToWireLimit(S.Left(20), 20, 20);
			if (Cut != S) { Fld->SetText(FText::FromString(Cut)); }
		};
		PurName->OnTextChanged.AddDynamic(Lim, &UAsdaTextLimiter::OnTextChanged);
		AsdaUi::AnchorRelay(GiftLimiters, Lim, PurName.Get());
	}
	PurNameBase = F(TEXT("BOOSTER_PUR_CHRNAME_Base_grp"));
	PurConfirm  = Cast<UButton>(F(TEXT("BOOSTER_PUR_CONFIRM")));
	PurTxt4     = T(TEXT("BOOSTER_PUR_TXT4"));
	PurTxt5     = T(TEXT("BOOSTER_PUR_TXT5"));
	PurTxt6     = T(TEXT("BOOSTER_PUR_TXT6"));
	PurPrice    = T(TEXT("BOOSTER_PUR_PRI1"));
	PurCash     = T(TEXT("BOOSTER_PUR_PRI2"));
	PurWareChk  = Cast<UCheckBox>(F(TEXT("BOOSTER_PUR_CHK1")));
	PurWareGrp  = F(TEXT("BOOSTER_PUR_WARE_Base_grp"));
	PurOk       = Cast<UButton>(F(TEXT("BOOSTER_PUR_OKBTN")));
	PurCancel   = Cast<UButton>(F(TEXT("BOOSTER_PUR_CANCEL")));
	PurPreview  = Cast<UButton>(F(TEXT("BOOSTER_PUR_PREV")));
	// ★ Д-19: значок «минус» на кнопке 15527 — план без id, ищем по префиксу имени.
	PurOkIcon   = AsdaUi::FindNodeByPrefix(Widget, TEXT("BOOSTER_PUR_OKBTN_Icon"));
	PurConfirmCap = T(TEXT("BOOSTER_PUR_CONFIRM_Text"));   // Д-21: гаснет вместе с кнопкой 15520
	PurGuide0   = T(TEXT("BOOSTER_PUR_GuideTxt00"));
	PurGuide1   = T(TEXT("BOOSTER_PUR_GuideTxt01"));

	SetLoc(T(TEXT("BOOSTER_PUR_TXT2")),  kLocPriceLbl);
	SetLoc(T(TEXT("BOOSTER_PUR_TXT3")),  kLocCashLbl2);
	SetLoc(PurTxt6, kLocQtyLbl);
	SetLoc(T(TEXT("BOOSTER_PUR_WARE")),  kLocWare);
	SetLoc(T(TEXT("BOOSTER_PUR_OKBTN_Text")),   kLocBtnOk);
	SetLoc(T(TEXT("BOOSTER_PUR_CANCEL_Text")),  kLocBtnCancel);
	SetLoc(T(TEXT("BOOSTER_PUR_CONFIRM_Text")), kLocBtnConfirm);
	SetLoc(T(TEXT("BOOSTER_PUR_PREV_Text")),    kLocBtnPrev);
	SetLoc(PurGuide0, kLocGuide0);
	SetLoc(PurGuide1, kLocGuide1);
	if (PurNarr)   { PurNarr->SetAutoWrapText(false); }
	if (PurWeight) { PurWeight->SetAutoWrapText(false); }
	if (PurPrice)  { PurPrice->SetAutoWrapText(false); }
	if (PurCash)   { PurCash->SetAutoWrapText(false); }
	if (PurItemName) { PurItemName->SetAutoWrapText(false); }
	if (PurName)   { PurName->SetJustification(ETextJustify::Left); }

	// ── ТРИ КОМБОБОКСА — ОБЩАЯ ТОЧКА `UAsdaJuiComboBox` (скин `Bk_ComboBox` + `DarkGrey_List`) ──
	//   Второй копии дропдауна не заводим (закон `port-shared-points`).
	if (!ComboJob)   { ComboJob   = NewObject<UAsdaJuiComboBox>(this); }
	if (!ComboLevel) { ComboLevel = NewObject<UAsdaJuiComboBox>(this); }
	if (!ComboQty)   { ComboQty   = NewObject<UAsdaJuiComboBox>(this); }
	ComboJob->Reset(); ComboLevel->Reset(); ComboQty->Reset();
	ComboJob->Setup(Widget,   TEXT("BOOSTER_PUR_COMBO1"), kWindowId, /*bOpenUp*/false);
	ComboLevel->Setup(Widget, TEXT("BOOSTER_PUR_COMBO2"), kWindowId, /*bOpenUp*/false);
	ComboQty->Setup(Widget,   TEXT("BOOSTER_PUR_COMBO3"), kWindowId, /*bOpenUp*/false);
	// ★ Комбо 15540 «Количество» зашито в Init эталона строками «1»…«7» (`ida.c:420480-420488`).
	//   ⚠ ИСТОЧНИКА В ДАННЫХ НЕТ: ни `Bpremium.BIN`, ни `item_list`, ни серверный `item_table.txt`
	//   не несут «максимум за покупку» — предел 7 задан РАЗМЕРОМ ПАКЕТА 5070 (в теле ровно семь
	//   слотов кода по `+3 + 4*i`, и сервер читает ровно семь: `for (i = 0; i < 7; ++i)`).
	//   Поэтому это не хардкод витрины, а граница протокола; редактор магазинов её менять не может.
	{
		TArray<FString> Q;
		for (int32 i = 1; i <= kMaxQty; ++i) { Q.Add(FString::Printf(TEXT("%d"), i)); }
		ComboQty->SetItems(Q);
		ComboQty->SetSelectedIndex(0);
	}
	ComboJob->OnPicked.BindWeakLambda(this,   [this](int32 Idx) { SetVariantJob(Idx); });
	ComboLevel->OnPicked.BindWeakLambda(this, [this](int32 Idx) { SetVariantLevel(Idx); });
	ComboQty->OnPicked.BindWeakLambda(this,   [this](int32 Idx) { SetQuantity(Idx + 1); });

	// ── Кнопки окна ──
	auto Bind = [this](UButton* B, int32 IdNum, TFunction<void(int32)> Cb)
	{
		if (!B) { return; }
		AsdaUi::BindButton(this, B, IdNum, MoveTemp(Cb), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnClose,   15301, [this](int32) { CloseWindow(); });
	Bind(BtnNext,    15501, [this](int32) { SetPage(Page + 1); });
	Bind(BtnBack,    15503, [this](int32) { SetPage(Page - 1); });
	Bind(BtnLast,    15505, [this](int32) { SetPage(MaxPage); });    // ★ `FIRSTP` — В КОНЕЦ
	Bind(BtnFirst,   15507, [this](int32) { SetPage(1); });          // ★ `ENDP`   — В НАЧАЛО
	Bind(BtnCharge,  15513, [this](int32) { OnChargeClicked(); });
	Bind(BtnBalance, 15514, [this](int32) { OnBalanceClicked(); });
	Bind(PurOk,      15527, [this](int32) { OnPurOkClicked(); });
	Bind(PurCancel,  15530, [this](int32) { OnPurCancelClicked(); });
	Bind(PurPreview, 15547, [this](int32) { OnPurPreviewClicked(); });
	Bind(PurConfirm, 15520, [this](int32) { OnPurConfirmClicked(); });
	// [ПОРТ] 05.11.2026: кнопка i → вкладка с фильтром из таблицы раскладки (эталонная раскладка — фильтр i).
	for (int32 i = 0; i < kCats; ++i) { Bind(CatButtons[i].Get(), 15545, [this, i](int32) { OnTabSlotClicked(i); }); }
	if (PurWareChk)
	{
		// Чекбокс — не кнопка; читаем его состояние в `CollectSelections`.
		PurWareChk->SetIsChecked(false);
	}

	// Панель покупки при инициализации СКРЫТА (эталон: `SetVisible(this[528], bShow != 0)`).
	PurMode = 0;
	for (const TObjectPtr<UWidget>& W : PurTree) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } }
	for (const TObjectPtr<UWidget>& W : SelFrame) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } }

	bReady = (BtnClose != nullptr) || (Cells.Num() > 0 && Cells[0].ItemIcon != nullptr);
	UE_LOG(LogAsdaBoosterShop, Log, TEXT("%s"), *DumpControls());
}

void UAsdaBoosterShopSubsystem::SetupCells()
{
	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	TArray<FName> IconNames; IconNames.SetNum(kCells);
	int32 Resolved = 0;

	// Регулярки ролей внутри ячейки: индексы узлов импортёра (`_20_0`) меняются при переимпорте,
	//   поэтому ищем по ВЫРАЖЕНИЮ, а не по вычисленному номеру (закон `jui-importer-widget-naming`).
	const FRegexPattern PatDigit(TEXT("^BOOSTER_Stamp([1-9])(_.*)?$"));

	for (int32 i = 0; i < kCells; ++i)
	{
		FAsdaBoosterCellUi& C = Cells[i];
		C.Parts.Reset(); C.Conditional.Reset(); C.Digits.Reset();
		C.Digits.SetNum(kDigits);
		// ★ 04.11.2026 (смена карты): окно пересоздаётся при OpenLevel, а ниже иконка, бейджи и плашка
		//   берутся под гардом `if (!C.X)` — без сброса они оставались на узлах СНЕСЁННОГО окна:
		//   иконки товаров красились в невидимое старое окно, бейджи гасились там же
		//   (самотест `asda.SelfTest.IconsAfterTravel`). Сбрасываем все указатели на узлы; ItemCode — данные, живёт.
		C.NameText = nullptr; C.PriceText = nullptr; C.ItemIcon = nullptr;
		C.BuyButton = nullptr; C.BuyCaption = nullptr; C.GiftButton = nullptr; C.GiftCaption = nullptr;
		C.BadgeNew = nullptr; C.BadgeHot = nullptr; C.Stamp = nullptr;

		// ★ ВСЁ ПОДДЕРЕВО ЯЧЕЙКИ — от её канваса `BOOSTER_ITEM{nn}_grp`. Именно так, а не по
		//   префиксу имени: у ячейки узлы зовутся `BOOSTER_ITEM_TITLE{nn}`, `BOOSTER_ITEM_PUR{nn}` —
		//   общего строкового префикса с `BOOSTER_ITEM{nn}` у них нет (закон `jui-cell-chain-nesting`).
		UWidget* Grp = AsdaUi::FindNodeByName(Widget, *FString::Printf(TEXT("BOOSTER_ITEM%02d_grp"), i), nullptr);
		if (!Grp)
		{
			UE_LOG(LogAsdaBoosterShop, Error,
				TEXT("[магазин бустеров] нет узла BOOSTER_ITEM%02d_grp — ячейка %d не покажется"), i, i);
			continue;
		}
		CollectTree(Grp, C.Parts);

		auto InCell = [&C](const TCHAR* Exact) -> UWidget*
		{
			for (const TObjectPtr<UWidget>& W : C.Parts) { if (W && W->GetName() == Exact) { return W.Get(); } }
			return nullptr;
		};
		C.NameText   = Cast<UTextBlock>(InCell(*FString::Printf(TEXT("BOOSTER_ITEM_TITLE%02d_Text"), i)));
		C.PriceText  = Cast<UTextBlock>(InCell(*FString::Printf(TEXT("BOOSTER_ITEM_PRICE%02d"), i)));
		C.BuyButton  = Cast<UButton>(InCell(*FString::Printf(TEXT("BOOSTER_ITEM_PUR%02d"), i)));
		C.BuyCaption = Cast<UTextBlock>(InCell(*FString::Printf(TEXT("BOOSTER_ITEM_PUR%02d_Text"), i)));
		C.GiftButton = Cast<UButton>(InCell(*FString::Printf(TEXT("BOOSTER_ITEM_PRESENT%02d"), i)));
		C.GiftCaption= Cast<UTextBlock>(InCell(*FString::Printf(TEXT("BOOSTER_ITEM_PRESENT%02d_Text"), i)));

		// Иконка, бейджи, плашка скидки и цифры — по образцу имени внутри ЭТОЙ ячейки.
		const FString IconPref = FString::Printf(TEXT("BOOSTER_ITEM_ICON%02d_"), i);
		const FString NewPref  = FString::Printf(TEXT("BOOSTER_ITEM_NEW%02d"), i);
		const FString HotPref  = FString::Printf(TEXT("BOOSTER_ITEM_HOT%02d"), i);
		for (const TObjectPtr<UWidget>& W : C.Parts)
		{
			if (!W) { continue; }
			const FString N = W->GetName();
			if (!C.ItemIcon && N.StartsWith(IconPref) && W->IsA<UImage>()) { C.ItemIcon = Cast<UImage>(W.Get()); }
			if (!C.BadgeNew && N.StartsWith(NewPref)) { C.BadgeNew = W.Get(); }
			if (!C.BadgeHot && N.StartsWith(HotPref)) { C.BadgeHot = W.Get(); }
			if (!C.Stamp && N.StartsWith(TEXT("BOOSTER_Stamp_Base"))) { C.Stamp = W.Get(); }
			FRegexMatcher M(PatDigit, N);
			if (M.FindNext())
			{
				const int32 D = FCString::Atoi(*M.GetCaptureGroup(1)) - 1;
				if (C.Digits.IsValidIndex(D) && !C.Digits[D]) { C.Digits[D] = W.Get(); }
			}
		}
		// Подписи кнопок ячейки — loc 6944 «OK» и 6821 «Подарок» (`ida.c:420578-420589`).
		if (C.BuyCaption)
		{
			C.BuyCaption->SetAutoWrapText(false);
			const FString S = Loc(kLocCellOk);
			if (!S.IsEmpty()) { C.BuyCaption->SetText(FText::FromString(S)); }
		}
		if (C.GiftCaption)
		{
			C.GiftCaption->SetAutoWrapText(false);
			const FString S = Loc(kLocCellGift);
			if (!S.IsEmpty()) { C.GiftCaption->SetText(FText::FromString(S)); }
		}
		if (C.NameText)  { C.NameText->SetAutoWrapText(false); }
		if (C.PriceText) { C.PriceText->SetAutoWrapText(false); }

		// Условные узлы: кнопка подарка (Д-1 + мёртвый 5074), бейджи и плашка скидки.
		if (C.GiftButton)  { C.Conditional.Add(C.GiftButton.Get()); }
		if (C.GiftCaption) { C.Conditional.Add(C.GiftCaption.Get()); }
		if (C.BadgeNew)    { C.Conditional.Add(C.BadgeNew.Get()); }
		if (C.BadgeHot)    { C.Conditional.Add(C.BadgeHot.Get()); }
		if (C.Stamp)       { C.Conditional.Add(C.Stamp.Get()); }
		for (const TObjectPtr<UWidget>& D : C.Digits) { if (D) { C.Conditional.Add(D.Get()); } }

		if (C.ItemIcon) { IconNames[i] = FName(*C.ItemIcon->GetName()); ++Resolved; }
		if (!C.NameText)
		{
			UE_LOG(LogAsdaBoosterShop, Warning,
				TEXT("[магазин бустеров] ячейка %d: нет узла BOOSTER_ITEM_TITLE%02d_Text — имя товара "
				     "не покажется. Прогнать шаг Tools/asda_ui_booster_cell_nodes.py"), i, i);
		}
		if (C.BuyButton)
		{
			UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
			R->Index = i;
			R->Callback = [this](int32 Idx) { OnCellBuyClicked(Idx); };
			C.BuyButton->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
			AsdaUi::AnchorRelay(Relays, R, C.BuyButton);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
		}
		else
		{
			// ★ «Ничего не происходит» быть не должно: если кнопки нет, карточка не кликается вовсе
			//   (класс дефекта из магазина 6000 — мусорный `ID` в .JUI ⇒ имя узла не совпало).
			UE_LOG(LogAsdaBoosterShop, Error,
				TEXT("[магазин бустеров] ячейка %d: НЕТ кнопки BOOSTER_ITEM_PUR%02d — покупка с этой "
				     "карточки НЕВОЗМОЖНА (проверить ID в .JUI и шаг asda_ui_booster_cell_nodes.py)"), i, i);
		}
		if (C.GiftButton)
		{
			UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
			R->Index = i;
			R->Callback = [this](int32 Idx) { OnCellGiftClicked(Idx); };
			C.GiftButton->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
			AsdaUi::AnchorRelay(Relays, R, C.GiftButton);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
		}
	}
	if (Resolved != kCells)
	{
		UE_LOG(LogAsdaBoosterShop, Warning,
			TEXT("[магазин бустеров] сетка: разобрано %d иконок из %d"), Resolved, kCells);
	}

	// Иконки товара — ОБЩАЯ ТОЧКА `UAsdaIconCellGrid` (иконка 64×64, `ida.c:420601-420604`).
	if (!IconGrid) { IconGrid = NewObject<UAsdaIconCellGrid>(this); }
	IconGrid->SetOverlayTag(TEXT("BoosterShop"));
	IconGrid->SetCellSizePx(64.f);
	IconGrid->SetupNamed(Widget, IconNames, /*bMale*/true,
		[this](int32 Cell) -> int32 { return Cells.IsValidIndex(Cell) ? Cells[Cell].ItemCode : 0; },
		[this](int32 Cell) { OnCellIconClicked(Cell); });
}

// ═══════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ═══════════════════════════════════════════
// Порт `uiStorageWindow_Open@0xB83B60` (`ida.c:421789-421875`).
void UAsdaBoosterShopSubsystem::OpenWindow()
{
	if (bOpen)
	{
		// Эталонный гейт 2: повторное открытие ИГНОРИРУЕТСЯ (`if (!*self)`).
		UE_LOG(LogAsdaBoosterShop, Log, TEXT("[магазин бустеров] окно уже открыто — повтор игнорирован (как эталон)"));
		return;
	}
	if (!LoadAssortment())
	{
		UE_LOG(LogAsdaBoosterShop, Error, TEXT("[магазин бустеров] ассортимент не прочитан — окно не открываем"));
		return;
	}
	if (!EnsureWidget()) { return; }

	// `uiStorageWindow_ResetState@0xB83930`: сброс состояния при открытии.
	// [ПОРТ] 05.11.2026: эталон ставит вкладку 0 — у нас первая ВИДИМАЯ кнопка полосы (при эталонной раскладке это 0).
	LoadTabs();
	ApplyTabLayout();
	Cat = TabFilters.Num() > 0 ? TabFilters[0] : 0;
	Page = 1;
	SelCell = -1;
	// ★ 12.11.2026 [ИСПРАВЛЕНИЕ, дефект оригинала]: `uiStorageWindow_ResetState@0xB83930` сбрасывает выбранную ячейку, а рамку
	//   15496 не гасит (её трогает только `uiSlotControlPlaceAndShow@0xB88720`, всегда «показать») — после повторного открытия
	//   рамка стояла без выбранной ячейки. Ключ `asda.BoosterShop.SelFrameResetOnOpen` 0 — как оригинал.
	if (CVarAsdaBoosterSelFrameResetOnOpen.GetValueOnGameThread() != 0)
	{
		for (const TObjectPtr<UWidget>& W : SelFrame) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } }
	}
	bPending = false;
	PurMode = 0;
	PurItemCode = 0;
	PurBaseCode = 0;
	PurQty = 1;
	bPurToWarehouse = false;
	PendingBuyCodes.Reset();
	BuyRun.Stop();   // [ИСПРАВЛЕНИЕ] 25.10.2026: при открытии очереди поштучной покупки нет

	ApplyPanelHeight();          // ★ высота подложки 15599 = 98 * (ячеек/3) + 48
	PopulatePurPanel(0, 0, false);
	BuildPage();
	RefreshTabState();

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->SetWindowVisible(kWindowId, true);
			WM->BringWindowToFront(kWindowId);
			// ★ Подложка 460: эталон её ГАСИТ в Init и НИ РАЗУ не показывает — механизм сообщений
			//   здесь мёртв. Открываем окно ассета один раз и оставляем скрытым (1:1).
			if (UUserWidget* Msg = WM->OpenWindowByPath(kMsgBaseId, kMsgBaseWbp, kShopZ - 1,
				ESlateVisibility::Collapsed))
			{
				if (UTextBlock* Tb = Cast<UTextBlock>(AsdaUi::FindNodeByName(Msg, TEXT("SHELLITEM_Msg00"), nullptr)))
				{
					const FString S = Loc(kLocMsgBase);
					if (!S.IsEmpty()) { Tb->SetText(FText::FromString(S)); }
				}
				WM->SetWindowVisible(kMsgBaseId, false);
			}
		}
		// ★ Эталон при открытии СРАЗУ шлёт запрос баланса (`ida.c:421832`).
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->SendPremiumBalanceQuery5466(Net->SelectedCharId);
		}
	}
	// ★ Правило владельца: состояние защёлкиваем ПОСЛЕ показа.
	bOpen = true;
	UE_LOG(LogAsdaBoosterShop, Log,
		TEXT("[магазин бустеров] окно 15300 открыто: ячеек на странице %d, товаров %d, страниц %d"),
		CellsPerPage(), ActiveSource().Num(), MaxPage);
}

void UAsdaBoosterShopSubsystem::CloseWindow()
{
	if (!bOpen) { return; }
	bOpen = false;
	bPending = false;
	// ★ 02.11.2026 P101 (ревизия С-2) [ИСПРАВЛЕНИЕ] Д-К5: окно закрыто — флаги «в полёте» 15300 не переживают окно.
	if (UAsdaCashGiftSubsystem* GiftR = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCashGiftSubsystem>() : nullptr) { GiftR->OnBoosterPanelReset(true); }
	if (BuyRun.bRun) { StopBuyRun(TEXT("окно 15300 закрыто")); }   // [ИСПРАВЛЕНИЕ] 25.10.2026: остаток не отправляем
	PurMode = 0;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->SetWindowVisible(kWindowId, false);
			WM->SetWindowVisible(kMsgBaseId, false);
		}
		// Наша модалка 1559 могла остаться взведённой — закрываем без ответа.
		if (UAsdaPurchaseBoxSubsystem* Box = GI->GetSubsystem<UAsdaPurchaseBoxSubsystem>())
		{
			if (Box->IsOpen() && Box->GetMode() == EAsdaPurchaseMode::Booster) { Box->Close(); }
		}
		// ★ Эталон при закрытии зовёт `boosterWindowPopulate(this, 0, −1)` — гасит окно «Вид» 15600.
		if (UAsdaBoosterPanelSubsystem* Prev = GI->GetSubsystem<UAsdaBoosterPanelSubsystem>())
		{
			Prev->ClosePrevWindow();
		}
		if (UAsdaItemTooltipSubsystem* Tip = GI->GetSubsystem<UAsdaItemTooltipSubsystem>())
		{
			Tip->HideTooltipFor(EAsdaTipOwner::BoosterShop);
			TipCell = -1;
		}
	}
	UE_LOG(LogAsdaBoosterShop, Log, TEXT("[магазин бустеров] окно 15300 закрыто"));
}

void UAsdaBoosterShopSubsystem::EscClose()
{
	if (!bOpen) { return; }
	// Эталон, событие 0x15 (shop_trade.c:1703-1706, ui_core.c:10517-10533):
	//     if (!self[616][292] && !self[528][292]) uiStorageWindow_Close(self);   // «Вид» и панель скрыты → окно
	//     if (+615)      boosterWindowPopulate(this, 0, −1);                     // открыт «Вид» 15600 → закрыть его
	//     else if (+527) uiPopulateItemComposeDialog(this, 0, 0, 0);             // открыта панель 15515 → закрыть её
	//     else           uiCloseItemComposeDialog(this);                         // закрыть магазин целиком
	//   Одно нажатие снимает ровно один слой. «Вид» — окно `BOOSTER_PREV_WIN` общей точки `UAsdaBoosterPanelSubsystem`.
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (WM && WM->IsWindowVisible(FName(TEXT("BOOSTER_PREV_WIN"))))
	{
		if (UAsdaBoosterPanelSubsystem* Prev = GI->GetSubsystem<UAsdaBoosterPanelSubsystem>()) { Prev->ClosePrevWindow(); }
		UE_LOG(LogAsdaBoosterShop, Log, TEXT("[магазин бустеров] ESC: закрыто окно «Вид» 15600, магазин открыт"));
		return;
	}
	if (PurMode != 0)
	{
		PopulatePurPanel(0, 0, false);
		UE_LOG(LogAsdaBoosterShop, Log, TEXT("[магазин бустеров] ESC: закрыта панель покупки 15515, магазин открыт"));
		return;
	}
	UE_LOG(LogAsdaBoosterShop, Log, TEXT("[магазин бустеров] ESC: закрываю окно 15300"));
	CloseWindow();
}

// ═══════════════════════════════ ВКЛАДКИ И СТРАНИЦЫ ════════════════════════════════════════════
// Порт `boosterShopOnMessage@0xB876F0`, `case 15545` (`ida.c:423259-423268`): при СМЕНЕ вкладки
//   гасятся окно «Вид» и панель покупки, страница сбрасывается в 1.
void UAsdaBoosterShopSubsystem::SetCategory(int32 NewCat)
{
	if (NewCat < 0 || NewCat >= kCats) { return; }
	if (Cat == NewCat) { return; }
	Cat = NewCat;
	Page = 1;
	SelCell = -1;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaBoosterPanelSubsystem* Prev = GI->GetSubsystem<UAsdaBoosterPanelSubsystem>()) { Prev->ClosePrevWindow(); }
	}
	PopulatePurPanel(0, 0, false);
	BuildPage();
	RefreshTabState();
	for (const TObjectPtr<UWidget>& W : SelFrame) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } }
}

void UAsdaBoosterShopSubsystem::SetPage(int32 NewPage)
{
	// Эталонные ограничители: `NEXTP` — `< self[47]`, `BACP` — `> 1` (`ida.c:422559-422575`).
	const int32 Clamped = FMath::Clamp(NewPage, 1, FMath::Max(1, MaxPage));
	if (Clamped == Page) { return; }
	Page = Clamped;
	BuildPage();
}
