// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaPremiumShopSubsystem — ★ 01.11.2026 P101_CashGift: ПОДАРОК ИЗ ОКНА 6000 (кнопки 6085 + 9i, панели 6296/6333 в режиме подарка).
//   Отдельный файл того же класса: окно, его кадр мыши и зона — у `UAsdaPremiumShopSubsystem`, общее состояние подарка
//   (имя, маршрут, флаги, сеть) — у `UAsdaCashGiftSubsystem`. Реверс — `Reports/cash_gift/re/client.md` §1.2.
//   Узлы панелей уже в ассете `WBP_SHELLITEM_STORE_WIN` (импорт .JUI; дамп `Reports/cash_gift/p101/dump_cashgift_controls.txt`);
//   статическое состояние (видимость SINGLE/MULTI, подсказки, подписи) — шаг ассета `Tools/asda_ui_cashgift_panels.py`.
//   Здесь только то, что эталон делает при показе: тексты по предмету, видимость строк режима, фокус, списки комбо.
#include "UI/AsdaPremiumShopSubsystem.h"

#include "AsdaDevConsole.h"                 // ASDA_DEV_CONSOLE (консольные команды самотеста — вне Shipping)
#include "Items/AsdaItemTableSubsystem.h"   // item_list / special_itemlist / подсказки
#include "Items/AsdaItemDbRows.h"           // строки подсказок (`itemTemplateFindByCode`, `itemGetTooltipById`)
#include "Login/AsdaButtonRelay.h"          // реле кнопок и ограничитель поля ввода
#include "Login/AsdaLoginData.h"            // обрезка имени по байтам провода (LimitText 20)
#include "Resources/AsdaSoundSubsystem.h"   // звук 4 (`csoundPlayName2DStore(…, 4, 0)`)
#include "UI/AsdaCashGiftSubsystem.h"       // общее состояние подарка (g_pShopWnd)
#include "UI/AsdaJuiComboBox.h"             // комбо профессии/уровня 6326/6324
#include "UI/AsdaNotice.h"
#include "UI/AsdaNoticeColors.h"
#include "UI/AsdaPurchaseBoxSubsystem.h"    // модалка 1559
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"         // зона ввода панели (она стоит вне рамки окна)
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPremGift, Log, All);

namespace AsdaPremGift
{
	const FName kWindowId(TEXT("SHELLITEM_STORE_WIN"));          // id окна 6000 в менеджере окон
	const FName kZoneId(TEXT("SHELLITEM_STORE_WIN_GIFTPANEL"));  // зона ввода панелей 6296/6333 (они левее рамки окна)
	constexpr int32 kNameLimit   = 20;      // LimitText 6311/6345 (.JUI) = `ctrlEditSetTextLength(…, 20)` (ui_core.c:27454/27581)
	constexpr int32 kLocGiftBtn  = 6874;    // «Подарок» на 6319/6353 в режиме подарка
	constexpr int32 kLocGiftJob  = 12912;   // «Профессия персонажа» (6325 в режиме подарка)
	constexpr int32 kLocGiftLv   = 12913;   // «Уровень персонажа» (6323 в режиме подарка)
	constexpr int32 kLocSlot     = 6788;    // «Число гнёзд» (`"%s : %d"`)
	constexpr int32 kLocNone     = 2335;    // «Нет»
	constexpr int32 kLocEffect   = 2360;    // «Эффект ЖК» (`"%s : %d ~ %d"`)
	constexpr int32 kLocWeight   = 2295;    // «Вес» (`"%s : %d"`)
	constexpr int32 kLocJobBase  = 12556;   // подпись профессии = 12556 + usageCategory
	constexpr int32 kLocSearching= 12931;   // «Идет поиск похожих предметов…» (`tradeBuildCategoryItemList`)
	constexpr int32 kLocNoPopup  = 12930;   // «Не удалось изменить настройки всплывающего окна»
	constexpr int32 kLocPanelOpen= 12929;   // открыта панель покупки/подарка
	constexpr int32 kLocModalOpen= 12933;   // открыто окно завершения покупки (модалка)
	constexpr int32 kLocNoName   = 12939;   // имя получателя не подтверждено
	constexpr int32 kLocSelf     = 12946;   // себе
	constexpr int32 kLocBadItem  = 12934;   // нет записи предмета (`shopConfirmBuyItem`)

	// Категории с подписью в `uiItemInfoShowTypeName`/`tradeRefreshHistoryList` (ui_core.c:29303, shop_trade.c:5904): 0…7, 12…18,
	//   23…29, 34…44 → loc 12556 + cat; прочие — без строки.
	bool HasJobLabel(int32 Cat)
	{
		return (Cat >= 0 && Cat <= 7) || (Cat >= 12 && Cat <= 18) || (Cat >= 23 && Cat <= 29) || (Cat >= 34 && Cat <= 44);
	}
	// Показать/скрыть узел (кнопки и поле ловят мышь, прочее — нет).
	void Vis(UWidget* W, bool bOn, bool bHit = false)
	{
		if (!W) { return; }
		W->SetVisibility(bOn ? (bHit ? ESlateVisibility::Visible : ESlateVisibility::SelfHitTestInvisible) : ESlateVisibility::Collapsed);
	}
	// Текст подписи (пусто — узла нет, молча).
	void Txt(UTextBlock* T, const FString& S) { if (T) { T->SetText(FText::FromString(S)); } }
}

using namespace AsdaPremGift;

// ═══════════════════════════════ ПРИВЯЗКА ═════════════════════════════════════════════════════════════════════════

void UAsdaPremiumShopSubsystem::SetupGiftPanels()
{
	GiftAnchors.Reset();
	GiftEquip = FAsdaShellGiftPanelUi();
	GiftFunc = FAsdaShellGiftPanelUi();
	if (!Widget || !Widget->WidgetTree) { return; }
	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	auto F = [this](const TCHAR* N) -> UWidget* { return AsdaUi::FindNodeByName(Widget, N, nullptr); };
	auto Prefixed = [&All](const FString& Pref, TArray<TObjectPtr<UWidget>>& Out)
	{
		for (UWidget* W : All) { if (W && W->GetName().StartsWith(Pref)) { Out.AddUnique(W); } }
	};

	// ── 6296 `SHELLITEM_EQUIP_WIN` ──
	FAsdaShellGiftPanelUi& E = GiftEquip;
	E.Root       = F(TEXT("SHELLITEM_EQUIP_WIN_grp"));
	E.Title      = Cast<UTextBlock>(F(TEXT("SHELL_EQUIP_TITLE")));
	E.Substance  = Cast<UTextBlock>(F(TEXT("SHELL_EQUIP_SUBSTANCE")));
	E.Txt01      = Cast<UTextBlock>(F(TEXT("SHELL_EQUIP_TXT01")));
	E.Txt02      = Cast<UTextBlock>(F(TEXT("SHELL_EQUIP_TXT02")));
	E.Txt03      = Cast<UTextBlock>(F(TEXT("SHELL_EQUIP_TXT03")));
	E.Txt04      = Cast<UTextBlock>(F(TEXT("SHELL_EQUIP_TXT04")));
	// ★ 6298 `SHELL_EQUIP_ICON` в .JUI — `Size = "0 64"` (ширина 0): импортёр картинки не создал, рисовать некуда (1:1 оригиналу).
	E.Hot        = F(TEXT("SHELL_EQUIP_HOT_902_0"));
	E.New        = F(TEXT("SHELL_EQUIP_NEW_903_0"));
	E.Price1     = Cast<UTextBlock>(F(TEXT("SHELL_EQUIP_PRICE1")));
	E.Price2     = Cast<UTextBlock>(F(TEXT("SHELL_EQUIP_PRICE2")));
	E.NameRow    = F(TEXT("SHELL_EQUIP_NAME_grp"));
	E.NameEdit   = Cast<UEditableText>(F(TEXT("SHELL_EQUIP_NINPUT")));
	Prefixed(TEXT("SHELL_EQUIP_NINPUT"), E.NameEditParts);
	E.Check      = Cast<UButton>(F(TEXT("SHELL_EQUIP_NCONFIRM")));
	E.JobTxt     = Cast<UTextBlock>(F(TEXT("SHELL_EQUIP_JOBTXT")));
	E.LvTxt      = Cast<UTextBlock>(F(TEXT("SHELL_EQUIP_LVTXT")));
	Prefixed(TEXT("SHELL_EQUIP_CHKBTN"), E.WareParts);
	Prefixed(TEXT("SHELL_EQUIP_WARETXT"), E.WareParts);
	E.PreBtn     = Cast<UButton>(F(TEXT("SHELL_EQUIP_PRE_BTN")));
	E.PreBtnText = Cast<UTextBlock>(F(TEXT("SHELL_EQUIP_PRE_BTN_Text")));
	E.CanBtn     = Cast<UButton>(F(TEXT("SHELL_EQUIP_CAN_BTN")));
	E.Job = NewObject<UAsdaJuiComboBox>(this);
	E.Lv  = NewObject<UAsdaJuiComboBox>(this);
	E.Job->Setup(Widget, TEXT("SHELL_EQUIP_JOB"), kWindowId, /*bOpenUp*/false);
	E.Lv->Setup(Widget, TEXT("SHELL_EQUIP_LV"), kWindowId, /*bOpenUp*/false);
	E.Job->OnPicked.BindWeakLambda(this, [this](int32 Idx) { GiftOnJobPicked(Idx); });   // 6326 → `uiItemListDispatchByMsg`
	E.Lv->OnPicked.BindWeakLambda(this, [this](int32 Idx) { GiftOnLvPicked(Idx); });     // 6324

	// ── 6333 `SHELLITEM_FUNC_WIN` ──
	FAsdaShellGiftPanelUi& P = GiftFunc;
	P.Root       = F(TEXT("SHELLITEM_FUNC_WIN_grp"));
	P.Title      = Cast<UTextBlock>(F(TEXT("SHELL_FUNC_TITLE")));
	P.Substance  = Cast<UTextBlock>(F(TEXT("SHELL_FUNC_SUBSTANCE")));
	P.Txt01      = Cast<UTextBlock>(F(TEXT("SHELL_FUNC_TXT01")));
	P.Txt02      = Cast<UTextBlock>(F(TEXT("SHELL_FUNC_TXT02")));
	P.Icon       = Cast<UImage>(F(TEXT("SHELL_FUNC_ICON_968_0")));
	P.Hot        = F(TEXT("SHELL_FUNC_HOT_970_0"));
	P.New        = F(TEXT("SHELL_FUNC_NEW_969_0"));
	P.Price1     = Cast<UTextBlock>(F(TEXT("SHELL_FUNC_PRICE1")));
	P.Price2     = Cast<UTextBlock>(F(TEXT("SHELL_FUNC_PRICE2")));
	P.NameRow    = F(TEXT("SHELL_FUNC_NAME_grp"));
	P.NameEdit   = Cast<UEditableText>(F(TEXT("SHELL_FUNC_NINPUT")));
	Prefixed(TEXT("SHELL_FUNC_NINPUT"), P.NameEditParts);
	P.Check      = Cast<UButton>(F(TEXT("SHELL_FUNC_NCONFIRM")));
	Prefixed(TEXT("SHELL_FUNC_NUM"), P.NumParts);   // 6357 `SHELL_FUNC_NUM` + 6358 `SHELL_FUNC_NUMINPUT` (их планы и подписи)
	Prefixed(TEXT("SHELL_FUNC_CHKBTN"), P.WareParts);
	Prefixed(TEXT("SHELL_FUNC_WARETXT"), P.WareParts);
	P.PreBtn     = Cast<UButton>(F(TEXT("SHELL_FUNC_PRE_BTN")));
	P.PreBtnText = Cast<UTextBlock>(F(TEXT("SHELL_FUNC_PRE_BTN_Text")));
	P.CanBtn     = Cast<UButton>(F(TEXT("SHELL_FUNC_CAN_BTN")));

	// ── Кнопки (эталон `uiHandleCommand@0xC23AC0`: 6312/6346 → 5072, 6319/6353 → 1559, 6322/6356 → `uiDialogCloseReset`) ──
	AsdaUi::BindButton(this, E.Check.Get(),  6312, [this](int32) { OnGiftCheckClicked(); }, GiftAnchors);
	AsdaUi::BindButton(this, P.Check.Get(),  6346, [this](int32) { OnGiftCheckClicked(); }, GiftAnchors);
	AsdaUi::BindButton(this, E.PreBtn.Get(), 6319, [this](int32) { OnGiftGiveClicked(false); }, GiftAnchors);
	AsdaUi::BindButton(this, P.PreBtn.Get(), 6353, [this](int32) { OnGiftGiveClicked(true); }, GiftAnchors);
	AsdaUi::BindButton(this, E.CanBtn.Get(), 6322, [this](int32) { CloseGiftPanels(); }, GiftAnchors);
	AsdaUi::BindButton(this, P.CanBtn.Get(), 6356, [this](int32) { CloseGiftPanels(); }, GiftAnchors);

	// ── LimitText 20 полей 6311/6345 (`ctrlEditSetTextLength(…, 20)`): символы и байты провода char[20] ──
	for (UEditableText* Ed : { E.NameEdit.Get(), P.NameEdit.Get() })
	{
		if (!Ed) { continue; }
		UAsdaTextLimiter* L = NewObject<UAsdaTextLimiter>(this);
		const TWeakObjectPtr<UEditableText> WeakEd = Ed;
		L->Callback = [WeakEd](const FText& NewText)
		{
			UEditableText* Fld = WeakEd.Get();
			if (!Fld) { return; }
			const FString S = NewText.ToString();
			const FString Cut = FAsdaLoginData::TruncateToWireLimit(S.Left(kNameLimit), kNameLimit, kNameLimit);
			if (Cut != S) { Fld->SetText(FText::FromString(Cut)); }
		};
		Ed->OnTextChanged.AddDynamic(L, &UAsdaTextLimiter::OnTextChanged);
		AsdaUi::AnchorRelay(GiftAnchors, L, Ed);
	}

	// ── Зона ввода: панели стоят ЛЕВЕЕ рамки окна (импорт .JUI: `SHELLITEM_EQUIP_WIN_grp` x = −509), рамочная зона их не накрывает ──
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->RegisterUiZone(kZoneId, FAsdaUiZoneRect::CreateWeakLambda(this, [this](FVector2D& Mn, FVector2D& Mx) -> bool
		{
			if (!bOpen) { return false; }
			const UWidget* R = (GiftEquip.Root && GiftEquip.Root->IsVisible()) ? GiftEquip.Root.Get()
				: ((GiftFunc.Root && GiftFunc.Root->IsVisible()) ? GiftFunc.Root.Get() : nullptr);
			return R && AsdaUi::WidgetAbsRect(R, Mn, Mx);
		}));
		// ★ 08.11.2026 (пачка 18, баг 2) [ОРИГИНАЛ]: 6296 показывает `ItemTooltip_PopulateWindow@0xC40A10` МОДАЛЬНО —
		//   `helper_setupFields(g_pWindowMgr, m_pEquipWin, 1)` (item_inventory.c:21454), мышь отдаётся только ей (`CWindowMgr+0x1A0`).
		//   Порт прежде держал её обычной панелью: карточки, вкладки и листание окна 6000 нажимались под ней (проба
		//   `asda.Shop.ModalProbeSelfTest` М2: 1446 точек). Роспись — общий механизм замка ввода (`RegisterOriginalModal`, узел = панель).
		TWeakObjectPtr<UAsdaPremiumShopSubsystem> WeakSelf(this);
		WM->RegisterOriginalModal(FName(TEXT("SHELLITEM_EQUIP_WIN")), kWindowId, [WeakSelf]()
		{
			const UAsdaPremiumShopSubsystem* S = WeakSelf.Get();
			return S && S->bOpen && S->GiftEquip.Root && S->GiftEquip.Root->IsVisible();
		},
		[WeakSelf]() -> const UWidget*
		{
			const UAsdaPremiumShopSubsystem* S = WeakSelf.Get();
			return S ? S->GiftEquip.Root.Get() : nullptr;
		});
		// 6333 эталон модальной не делает (вызова `helper_setupFields` у неё нет) — тело панели только ловит нажатие (`RegisterPressPanel`).
		WM->RegisterPressPanel(FName(TEXT("SHELLITEM_FUNC_WIN")), kWindowId, [WeakSelf]()
		{
			const UAsdaPremiumShopSubsystem* S = WeakSelf.Get();
			return S && S->bOpen && S->GiftFunc.Root && S->GiftFunc.Root->IsVisible();
		},
		[WeakSelf]() -> const UWidget*
		{
			const UAsdaPremiumShopSubsystem* S = WeakSelf.Get();
			return S ? S->GiftFunc.Root.Get() : nullptr;
		});
	}

	// ── События общего состояния подарка ──
	if (UAsdaCashGiftSubsystem* Gift = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCashGiftSubsystem>() : nullptr)
	{
		if (!HGiftClear.IsValid()) { HGiftClear = Gift->OnClearNameFields.AddUObject(this, &UAsdaPremiumShopSubsystem::OnGiftClearFields); }
		if (!HGiftReset.IsValid())
		{
			HGiftReset = Gift->OnWindowReset.AddWeakLambda(this, [this]()
			{
				// `uiResetWindowState` → `uiDialogCloseReset`: панели закрыты, код товара −1 (поля и имя очистил сам владелец состояния).
				GiftItemCode = -1;
				ShowGiftPanel(GiftEquip, false);
				ShowGiftPanel(GiftFunc, false);
			});
		}
	}
	ShowGiftPanel(GiftEquip, false);
	ShowGiftPanel(GiftFunc, false);
	UE_LOG(LogAsdaPremGift, Log, TEXT("[подарок 6000] панели: 6296 %s (поле %s, «Проверить» %s, «Подарок» %s, комбо %d/%d), 6333 %s (поле %s, «Проверить» %s, «Подарок» %s, иконка %s)"),
		E.Root ? TEXT("есть") : TEXT("НЕТ"), E.NameEdit ? TEXT("+") : TEXT("−"), E.Check ? TEXT("+") : TEXT("−"), E.PreBtn ? TEXT("+") : TEXT("−"),
		E.Job->IsReady() ? 1 : 0, E.Lv->IsReady() ? 1 : 0,
		P.Root ? TEXT("есть") : TEXT("НЕТ"), P.NameEdit ? TEXT("+") : TEXT("−"), P.Check ? TEXT("+") : TEXT("−"), P.PreBtn ? TEXT("+") : TEXT("−"),
		P.Icon ? TEXT("+") : TEXT("−"));
}

void UAsdaPremiumShopSubsystem::ShowGiftPanel(FAsdaShellGiftPanelUi& P, bool bShow)
{
	if (!bShow)
	{
		if (P.Job) { P.Job->SetOpen(false); }
		if (P.Lv)  { P.Lv->SetOpen(false); }
	}
	Vis(P.Root.Get(), bShow);
	// ★ 08.11.2026 (пачка 18, баг 2): показ/скрытие модальной 6296 — пересчитать замок ввода (блокер, фокус, подъём).
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr) { WM->NotifyModalStateChanged(); }
}

bool UAsdaPremiumShopSubsystem::IsGiftPanelShown() const
{
	return (GiftEquip.Root && GiftEquip.Root->IsVisible()) || (GiftFunc.Root && GiftFunc.Root->IsVisible());
}

void UAsdaPremiumShopSubsystem::TickGiftPanels()
{
	// Комбо 6326/6324 живут только в 6296; кадр мыши — общий кадр окна (`Mouse`, закон одной `FAsdaMouseFrame` на окно).
	if (!GiftEquip.Root || !GiftEquip.Root->IsVisible()) { return; }
	const bool bJobOpen = GiftEquip.Job && GiftEquip.Job->IsOpen();
	const bool bLvOpen  = GiftEquip.Lv && GiftEquip.Lv->IsOpen();
	if (GiftEquip.Job && !bLvOpen)  { GiftEquip.Job->Tick(Mouse); }
	if (GiftEquip.Lv  && !bJobOpen) { GiftEquip.Lv->Tick(Mouse); }
}

void UAsdaPremiumShopSubsystem::EscClose()
{
	// ui_core.c:25443-25455: открыта панель покупки/подарка → `uiDialogCloseReset` (только она), иначе — окно целиком.
	if (IsGiftPanelShown()) { CloseGiftPanels(); return; }
	CloseWindow();
}

// ═══════════════════════════════ ВХОД: КНОПКА «ПОДАРОК» 6085 + 9i ═════════════════════════════════════════════════

void UAsdaPremiumShopSubsystem::OnCardGiftClicked(int32 Card)
{
	if (!AsdaCashGiftOn() || !bOpen || !Cards.IsValidIndex(Card)) { return; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaPurchaseBoxSubsystem* Box = GI ? GI->GetSubsystem<UAsdaPurchaseBoxSubsystem>() : nullptr;
	// LABEL_53 (ui_core.c:25705-25717): модалка менеджера видна → чат 12933 (−11514, 10 с).
	if ((Box && Box->IsOpen()) || AsdaUi::IsModalDispatchBlocked(GI))
	{
		AsdaNotice::ChatColored(GI, EAsdaMsgCategory::Trade, Loc(kLocModalOpen), AsdaNoticeColor::Gold, 10000.f);
		return;
	}
	// `uiCheckTradePanelsClosed@0xC43390`: панель 6296/6333 уже открыта → чат 12929.
	if (IsGiftPanelShown())
	{
		AsdaNotice::ChatColored(GI, EAsdaMsgCategory::Trade, Loc(kLocPanelOpen), AsdaNoticeColor::Gold, 10000.f);
		return;
	}
	// `tradeBuyTabSelect@0xC42760` (shop_trade.c:5653-5690): звук 4; item_list → список набора + режим 0; special → режим 1.
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr) { Snd->PlaySound2DById(4); }
	const int32 Code = Cards[Card].ItemCode;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT || Code <= 0) { return; }
	if (IT->GetItem(Code))
	{
		GiftBuildSetList(Code);
		PopulateGiftPanel(Code, /*bFunc*/false);
	}
	else
	{
		FAsdaSpecialItemlistRow Sp;
		if (IT->GetSpecialItem(Code, Sp)) { PopulateGiftPanel(Code, /*bFunc*/true); }
	}
	UE_LOG(LogAsdaPremGift, Log, TEXT("[подарок 6000] карточка %d, код %d → панель %s, код панели %d"),
		Card, Code, IsGiftPanelShown() ? (GiftFunc.Root && GiftFunc.Root->IsVisible() ? TEXT("6333") : TEXT("6296")) : TEXT("НЕ показана"), GiftItemCode);
}

// ═══════════════════════════════ ЗАПОЛНЕНИЕ ПАНЕЛИ (panelMode = 1) ═══════════════════════════════════════════════

void UAsdaPremiumShopSubsystem::ResetGiftPanels()
{
	// `upgradeUiResetPanels@0xC41F50` (item_enhance.c:9-57).
	for (FAsdaShellGiftPanelUi* Pp : { &GiftEquip, &GiftFunc })
	{
		FAsdaShellGiftPanelUi& P = *Pp;
		Vis(P.Hot.Get(), false);
		Vis(P.New.Get(), false);
		for (UTextBlock* T : { P.Title.Get(), P.Substance.Get(), P.Txt01.Get(), P.Txt02.Get(), P.Txt03.Get(), P.Txt04.Get(), P.Price1.Get(), P.Price2.Get() })
		{
			Txt(T, FString());
		}
		for (const TObjectPtr<UWidget>& W : P.WareParts) { Vis(W.Get(), true, Cast<UButton>(W.Get()) != nullptr); }   // `SetVisible(ChkBtn, 1)`
		Vis(P.NameRow.Get(), true);
		if (P.NameEdit) { P.NameEdit->SetText(FText::GetEmpty()); }
		Vis(P.LvTxt.Get(), true);
		Vis(P.JobTxt.Get(), true);
		if (P.Job) { P.Job->SetItems({}); P.Job->SetSelectedIndex(-1); }
		if (P.Lv)  { P.Lv->SetItems({}); P.Lv->SetSelectedIndex(-1); }
	}
	if (GiftFunc.Icon) { GiftFunc.Icon->SetVisibility(ESlateVisibility::Collapsed); }   // `DrawTextureRect(icon, пусто)`
}

void UAsdaPremiumShopSubsystem::GiftBuildSetList(int32 BaseCode, bool bQuiet)
{
	// `tradeBuildCategoryItemList@0xC43560` (shop_trade.c:5732-5876), вызов `(…, code, setCode, 1)`.
	GiftSetCodes.Reset(); GiftJobs.Reset(); GiftLvCodes.Reset();
	bGiftVariants = false;
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Base = IT ? IT->GetItem(BaseCode) : nullptr;
	if (!IT || !Base || IT->GetAllItems().Num() <= 0)
	{
		AsdaNotice::ChatColored(GI, EAsdaMsgCategory::Trade, Loc(kLocNoPopup), AsdaNoticeColor::Gold, 10000.f);   // 12930
		return;
	}
	if (!bQuiet) { AsdaNotice::ChatColored(GI, EAsdaMsgCategory::Trade, Loc(kLocSearching), AsdaNoticeColor::Gold, 10000.f); }     // 12931 (как эталон)
	const int32 Set = Base->SetCode;
	TArray<const FAsdaItemRecord*> Same;   // коды по возрастанию (`itemCodeIter = g_ItemCodeFirst; ++itemCodeIter`)
	for (const FAsdaItemRecord& R : IT->GetAllItems())
	{
		if (Set > 0 && R.SetCode == Set && R.CategoryFlag == 3) { Same.Add(&R); }
	}
	Same.Sort([](const FAsdaItemRecord& A, const FAsdaItemRecord& B) { return A.ItemCode < B.ItemCode; });
	// Различные профессии > 0 по возрастанию (`findMinValueAbove` с порога 0 — категория 0 в список не попадает).
	for (const FAsdaItemRecord* R : Same) { if (R->UsageCategory > 0) { GiftJobs.AddUnique(R->UsageCategory); } }
	GiftJobs.Sort();
	// Перестановка списка по профессиям (записи категории 0 выпадают).
	for (const int32 J : GiftJobs) { for (const FAsdaItemRecord* R : Same) { if (R->UsageCategory == J) { GiftSetCodes.Add(R->ItemCode); } } }
	int32 Other = 0;
	for (const int32 C : GiftSetCodes) { if (C != BaseCode) { ++Other; } }
	bGiftVariants = Other > 0;   // `v26[2080] = finalItemCount > 0`
}

bool UAsdaPremiumShopSubsystem::GiftBuildMatched()
{
	// `tradeItem_BuildMatchedList@0xC41C60` (shop_trade.c:5587-5650), a2 = 1.
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT || GiftJobs.Num() == 0 || GiftJobs[0] <= 0) { return false; }
	const int32 Job = GiftJobs[0];
	int32 BestLv = 0, BestCode = 0;
	for (const int32 C : GiftSetCodes)
	{
		const FAsdaItemRecord* R = IT->GetItem(C);
		if (!R || R->UsageCategory != Job) { continue; }
		if (BestLv <= 0 || BestLv > R->RequiredLevel) { BestLv = R->RequiredLevel; BestCode = R->ItemCode; }
	}
	const FAsdaItemRecord* Pick = IT->GetItem(BestCode);
	if (!Pick) { return false; }
	GiftItemCode = BestCode;
	GiftLvCodes.Reset();
	for (const int32 C : GiftSetCodes)
	{
		const FAsdaItemRecord* R = IT->GetItem(C);
		if (R && R->UsageCategory == Pick->UsageCategory && R->SetCode == Pick->SetCode) { GiftLvCodes.Add(C); }
	}
	// `itemList_sortByRecordKey@0xC46270`: при минимальном уровне > 0 — группы по уровню по возрастанию (порядок внутри — прежний).
	int32 MinLv = -1;
	for (const int32 C : GiftLvCodes) { if (const FAsdaItemRecord* R = IT->GetItem(C)) { if (MinLv < 0 || MinLv > R->RequiredLevel) { MinLv = R->RequiredLevel; } } }
	if (MinLv > 0)
	{
		GiftLvCodes.StableSort([IT](int32 A, int32 B) { return IT->GetItem(A)->RequiredLevel < IT->GetItem(B)->RequiredLevel; });
	}
	GiftRefreshCombos();   // `tradeRefreshHistoryList@0xC43EA0`
	return true;
}

void UAsdaPremiumShopSubsystem::GiftRefreshCombos()
{
	// `tradeRefreshHistoryList@0xC43EA0` (shop_trade.c:5904-6124): комбо 6326 — подписи профессий, 6324 — уровни списка.
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	TArray<FString> Jobs, Lvs;
	for (const int32 J : GiftJobs) { if (HasJobLabel(J)) { Jobs.Add(Loc(kLocJobBase + J)); } }
	for (const int32 C : GiftLvCodes) { if (const FAsdaItemRecord* R = IT ? IT->GetItem(C) : nullptr) { Lvs.Add(FString::FromInt(R->RequiredLevel)); } }
	if (GiftEquip.Job) { GiftEquip.Job->SetItems(Jobs); }
	if (GiftEquip.Lv)  { GiftEquip.Lv->SetItems(Lvs); }
}

void UAsdaPremiumShopSubsystem::GiftFillInfo()
{
	// `UI_FillItemInfoPanel@0xC44EA0` (ui_core.c:29142-29213): строки 6303/6304/6305 и цена 6314 по коду +10728.
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* R = IT ? IT->GetItem(GiftItemCode) : nullptr;
	if (!R) { return; }
	const int32 Slots = (R->EnchantOrSocket & 0xFF) + 1;   // `LOBYTE(enchantOrSocket) + 1`
	Txt(GiftEquip.Txt01, Slots < 5 ? FString::Printf(TEXT("%s : %d"), *Loc(kLocSlot), Slots)
	                               : FString::Printf(TEXT("%s : %s"), *Loc(kLocSlot), *Loc(kLocNone)));
	Txt(GiftEquip.Txt02, FString::Printf(TEXT("%s : %d ~ %d"), *Loc(kLocEffect), R->StatBaseHi, R->StatBaseLo));   // gap_88[0] ~ gap_88[2]
	Txt(GiftEquip.Txt03, FString::Printf(TEXT("%s : %d"), *Loc(kLocWeight), R->Weight));                          // LOWORD(field72)
	Txt(GiftEquip.Price1, AsdaUi::FormatThousands(R->Price));
}

void UAsdaPremiumShopSubsystem::GiftOnJobPicked(int32 Idx)
{
	// `uiItemFilterListPopulate_49F0@0xC449F0` (ui_core.c:29034-29140): уровни выбранной профессии, выбор по прежнему тексту уровня.
	if (!GiftJobs.IsValidIndex(Idx) || GiftJobs[Idx] < 0) { return; }
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return; }
	GiftLvCodes.Reset();
	for (const int32 C : GiftSetCodes)
	{
		const FAsdaItemRecord* R = IT->GetItem(C);
		if (R && R->UsageCategory == GiftJobs[Idx] && GiftLvCodes.Num() < 30) { GiftLvCodes.Add(C); }
	}
	int32 MinLv = -1;
	for (const int32 C : GiftLvCodes) { if (const FAsdaItemRecord* R = IT->GetItem(C)) { if (MinLv < 0 || MinLv > R->RequiredLevel) { MinLv = R->RequiredLevel; } } }
	if (MinLv > 0) { GiftLvCodes.StableSort([IT](int32 A, int32 B) { return IT->GetItem(A)->RequiredLevel < IT->GetItem(B)->RequiredLevel; }); }
	if (!IT->GetItem(GiftItemCode)) { return; }
	const FString Prev = GiftEquip.Lv ? GiftEquip.Lv->GetItemLabel(GiftEquip.Lv->GetSelectedIndex()) : FString();
	TArray<FString> Lvs;
	int32 Sel = 0;
	for (int32 i = 0; i < GiftLvCodes.Num(); ++i)
	{
		const FAsdaItemRecord* R = IT->GetItem(GiftLvCodes[i]);
		if (!R) { continue; }
		const FString S = FString::FromInt(R->RequiredLevel);
		if (!Prev.IsEmpty() && S == Prev) { Sel = i; }
		Lvs.Add(S);
	}
	if (GiftEquip.Lv) { GiftEquip.Lv->SetItems(Lvs); GiftEquip.Lv->SetSelectedIndex(Lvs.Num() > 0 ? Sel : -1); }
	if (GiftLvCodes.IsValidIndex(Sel)) { GiftItemCode = GiftLvCodes[Sel]; }
	GiftFillInfo();
}

void UAsdaPremiumShopSubsystem::GiftOnLvPicked(int32 Idx)
{
	// `uiInventorySelectItemSlot@0xC45170` (ui_core.c:29215-29225).
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (IT && GiftLvCodes.IsValidIndex(Idx) && IT->GetItem(GiftLvCodes[Idx])) { GiftItemCode = GiftLvCodes[Idx]; }
	GiftFillInfo();
}

void UAsdaPremiumShopSubsystem::PopulateGiftPanel(int32 ItemCode, bool bFunc, bool bBuy)
{
	// `ItemTooltip_PopulateWindow@0xC40A10` (item_inventory.c:21141-21455) при panelMode = 1 (подарок).
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaCashGiftSubsystem* Gift = GI ? GI->GetSubsystem<UAsdaCashGiftSubsystem>() : nullptr;
	if (!IT || !Gift) { return; }
	GiftItemCode = -1;
	Gift->OnShopPanelOpened();   // +10734 = 0, [2078] = 0, [2072] = 0 (21250-21258)
	ResetGiftPanels();
	bPanelBuy = bBuy;   // ★ 06.11.2026: panelMode 0 — покупка; ResetGiftPanels сбрасывает только поля
	const FString Cash = AsdaUi::FormatThousands(CashGxp);   // `bag+gap_43B[17]`

	if (bFunc)
	{
		// tooltipMode 1: запись special_itemlist + подсказка (`itemGetTooltipById`).
		FAsdaSpecialItemlistRow Sp;
		FAsdaSpecialitemTooltiplistRow Tip;
		if (!IT->GetSpecialItem(ItemCode, Sp) || !IT->GetSpecialItemTooltip(ItemCode, Tip)) { return; }
		FAsdaShellGiftPanelUi& P = GiftFunc;
		for (const TObjectPtr<UWidget>& W : P.NumParts) { Vis(W.Get(), false); }   // 6357/6358 скрыты
		Vis(P.NameRow.Get(), true);
		for (const TObjectPtr<UWidget>& W : P.WareParts) { Vis(W.Get(), false); }  // чекбокс и подпись склада скрыты
		for (const TObjectPtr<UWidget>& W : P.NameEditParts) { Vis(W.Get(), true, W == P.NameEdit); }
		Txt(P.PreBtnText, Loc(kLocGiftBtn));
		GiftItemCode = ItemCode;
		Txt(P.Title, CardLabel(ItemCode, Tip.Name));   // ★ 06.11.2026: подпись как на карточке — подарок выдаёт ту же коробку ×N
		Txt(P.Substance, Tip.Tooltip);
		// ★ 1:1: `sprintf("%s : %d", loc 2295, LOWORD(requiredLevel))` — подпись «Вес» с полем требуемого уровня записи.
		Txt(P.Txt01, FString::Printf(TEXT("%s : %d"), *Loc(kLocWeight), Sp.RequiredLevel & 0xFFFF));
		Txt(P.Txt02, FString());
		Txt(P.Price1, AsdaUi::FormatThousands(Sp.Price));
		Txt(P.Price2, Cash);
		if (P.Icon)
		{
			if (AsdaUi::ApplyItemIcon(GI, P.Icon.Get(), ItemCode, FVector2D(64.f, 64.f), AsdaUi::IconSexSelf, AsdaUi::IsItemIconWrapFixEnabled()))
			{
				P.Icon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			}
		}
		ShowGiftPanel(GiftEquip, false);
		ShowGiftPanel(GiftFunc, true);
		if (P.NameEdit) { P.NameEdit->SetKeyboardFocus(); }   // `CControl_SetFocus(m_pFuncNInput, 1)`
		return;
	}

	// tooltipMode 0: запись item_list + шаблон (`itemTemplateFindByCode`).
	FAsdaItemTooltipRow Tmpl;
	const FAsdaItemRecord* Rec = IT->GetItem(ItemCode);
	if (!Rec || !IT->GetItemTooltip(ItemCode, Tmpl)) { return; }
	if (bGiftVariants)
	{
		// `[2080]`: варианты набора — код панели становится вариантом первой профессии с наименьшим уровнем.
		if (!GiftBuildMatched()) { return; }
		Rec = IT->GetItem(GiftItemCode);
		if (!Rec || !IT->GetItemTooltip(GiftItemCode, Tmpl)) { return; }
	}
	FAsdaShellGiftPanelUi& E = GiftEquip;
	const int32 Slots = Rec->EnchantOrSocket + 1;
	Txt(E.Txt01, Slots < 5 ? FString::Printf(TEXT("%s : %d"), *Loc(kLocSlot), Slots)
	                       : FString::Printf(TEXT("%s : %s"), *Loc(kLocSlot), *Loc(kLocNone)));
	Txt(E.Txt02, FString::Printf(TEXT("%s : %d ~ %d"), *Loc(kLocEffect), Rec->StatBaseHi, Rec->StatBaseLo));
	Txt(E.Txt03, FString::Printf(TEXT("%s : %d"), *Loc(kLocWeight), Rec->Weight));
	Txt(E.Price1, AsdaUi::FormatThousands(Rec->Price));
	Txt(E.Price2, Cash);
	// panelMode 1: имя — `<FC=FFFFFFFF>%s</F>` (белое — цвет подписи ассета), строка имени видна, подписи 12912/12913, фокус, 6874.
	Txt(E.Title, CardLabel(Rec->ItemCode, IT->GetItemName(Rec->ItemCode)));   // ★ 06.11.2026: подпись как на карточке
	// ★ 06.11.2026 panelMode 0 (item_inventory.c:21409-21420): строки имени нет, подписи 12936/12937, кнопка 6857. Чекбокс склада не показываем: покупка идёт в рюкзак.
	Vis(E.NameRow.Get(), !bBuy);
	for (const TObjectPtr<UWidget>& W : E.NameEditParts) { Vis(W.Get(), !bBuy, W == E.NameEdit); }
	Txt(E.JobTxt, Loc(bBuy ? 12936 : kLocGiftJob));
	Txt(E.LvTxt, Loc(bBuy ? 12937 : kLocGiftLv));
	Txt(E.PreBtnText, Loc(bBuy ? 6857 : kLocGiftBtn));
	for (const TObjectPtr<UWidget>& W : E.WareParts) { Vis(W.Get(), false); }   // `SetVisible(m_pEquipChkBtn, 0)`
	Txt(E.Substance, Tmpl.Description);   // `itemTemplate->name` (+4 шаблона = описание)
	ShowGiftPanel(GiftFunc, false);
	if (bGiftVariants)
	{
		// `tradeItemSyncSelection(…, 1)`: профессия — пункт текущего кода, уровень — пункт 0; затем `UI_FillItemInfoPanel`.
		const int32 JobIdx = GiftJobs.IndexOfByKey(Rec->UsageCategory);
		if (E.Job) { E.Job->SetSelectedIndex(JobIdx >= 0 ? JobIdx : -1); }
		if (E.Lv)  { E.Lv->SetSelectedIndex(GiftLvCodes.Num() > 0 ? 0 : -1); }
		GiftFillInfo();
		// ★ 06.11.2026 [ОТСТУПЛЕНИЕ]: у карточки другого класса того же набора сразу выбран её класс (оригинал ставит первый): игрок не купит чужое по ошибке.
		const FAsdaItemRecord* Card = IT->GetItem(ItemCode);
		const int32 Own = Card ? GiftJobs.IndexOfByKey(Card->UsageCategory) : -1;
		if (Own >= 0 && Own != JobIdx)
		{
			if (E.Job) { E.Job->SetSelectedIndex(Own); }
			GiftOnJobPicked(Own);
		}
	}
	else
	{
		// `uiItemInfoShowTypeName@0xC45880` (ui_core.c:29303-29519): один пункт профессии и один пункт уровня.
		if (E.Job) { E.Job->SetItems(HasJobLabel(Rec->UsageCategory) ? TArray<FString>{ Loc(kLocJobBase + Rec->UsageCategory) } : TArray<FString>{}); E.Job->SetSelectedIndex(0); }
		if (E.Lv)  { E.Lv->SetItems({ FString::FromInt(Rec->RequiredLevel) }); E.Lv->SetSelectedIndex(0); }
		GiftItemCode = ItemCode;
	}
	ShowGiftPanel(GiftEquip, true);
	if (E.NameEdit && !bBuy) { E.NameEdit->SetKeyboardFocus(); }   // `CControl_SetFocus(m_pEquipNInput, 1)`
}

// ═══════════════════════════════ КНОПКИ ПАНЕЛИ ═════════════════════════════════════════════════════════════════════

void UAsdaPremiumShopSubsystem::OnGiftCheckClicked()
{
	// 6312/6346 (ui_core.c:25893-25900): модалка видна → 12933; иначе `uiConfirmPasswordMatch` → `netSendSecureChallengeMd5`.
	UGameInstance* GI = GetGameInstance();
	const UAsdaPurchaseBoxSubsystem* Box = GI ? GI->GetSubsystem<UAsdaPurchaseBoxSubsystem>() : nullptr;
	UAsdaCashGiftSubsystem* Gift = GI ? GI->GetSubsystem<UAsdaCashGiftSubsystem>() : nullptr;
	if (!Gift) { return; }
	if (Box && Box->IsOpen())
	{
		AsdaNotice::ChatColored(GI, EAsdaMsgCategory::Trade, Loc(kLocModalOpen), AsdaNoticeColor::Gold, 10000.f);
		return;
	}
	// `uiConfirmPasswordMatch@0xC47330` (ui_core.c:29611-29655): имя из 6311 (видна 6296) или 6345 (видна 6333).
	const bool bEquip = GiftEquip.Root && GiftEquip.Root->IsVisible();
	const bool bFunc  = !bEquip && GiftFunc.Root && GiftFunc.Root->IsVisible();
	UEditableText* Ed = bEquip ? GiftEquip.NameEdit.Get() : (bFunc ? GiftFunc.NameEdit.Get() : nullptr);
	const FString Name = Ed ? Ed->GetText().ToString() : FString();
	// ui_core.c:29629-29631: своего персонажа нет (`Field142C == null`) → молча, ничего не шлём (ревизия Н-8).
	if (!Gift->IsSelfKnown()) { return; }
	if (Gift->IsSelfName(Name))
	{
		AsdaNotice::ChatColored(GI, EAsdaMsgCategory::Trade, Loc(kLocSelf), AsdaNoticeColor::Gold, 10000.f);
		// [ИСПРАВЛЕНИЕ] Д-К3 (`asda.CashGift.FixK3`): эталон чистит ТОЛЬКО 6311, даже если ввод был в 6345 (ui_core.c:29654).
		const int32 Clear = (AsdaCashGiftFixOn(3) && bFunc) ? 6345 : 6311;
		OnGiftClearFields(/*bBoth*/false, Clear);
		return;
	}
	Gift->SendCheckName(EAsdaGiftSource::Shop6000, Name);
}

void UAsdaPremiumShopSubsystem::OnGiftGiveClicked(bool bFunc)
{
	// 6319/6353 (ui_core.c:25902-25906): модалка видна → 12933; иначе `upgradeUiHandleInfoMsg@0xC422B0` (item_enhance.c:60-125).
	UGameInstance* GI = GetGameInstance();
	UAsdaPurchaseBoxSubsystem* Box = GI ? GI->GetSubsystem<UAsdaPurchaseBoxSubsystem>() : nullptr;
	UAsdaCashGiftSubsystem* Gift = GI ? GI->GetSubsystem<UAsdaCashGiftSubsystem>() : nullptr;
	if (!Box || !Gift) { return; }
	if (Box->IsOpen())
	{
		AsdaNotice::ChatColored(GI, EAsdaMsgCategory::Trade, Loc(kLocModalOpen), AsdaNoticeColor::Gold, 10000.f);
		return;
	}
	if (bPanelBuy)
	{
		// ★ 06.11.2026 panelMode 0: `upgradeUiHandleInfoMsg` при +10733 == 0 → 1559 → `shopConfirmBuyItem` → 5070 с выбранным кодом.
		if (bPending || CartRun.bRun || CartNextBatch.Num() > 0) { AsdaNotice::ChatColored(GI, EAsdaMsgCategory::Trade, Loc(1026), AsdaNoticeColor::Gold, 10000.f); return; }   // 1026 «ждём ответа»
		if (CartPickRow >= 0) { CartPickConfirm(); return; }   // ★ 06.11.2026: панель открыта кнопкой 6275 — код уходит в строку корзины
		BuyCodeChecked(-1, GiftItemCode);
		return;
	}
	if (Gift->GetConfirmedName().IsEmpty())
	{
		// Имя не подтверждено 5073 → ONEBUT 12939 и очистка поля своей панели (6319 → 6311, 6353 → 6345).
		AsdaNotice::MsgBox(GI, EAsdaMsgCategory::Trade, Loc(kLocNoName));
		OnGiftClearFields(/*bBoth*/false, bFunc ? 6345 : 6311);
		return;
	}
	// `CUpgradeUI_FillItemInfoPanel(g_pItemTooltipUI, +10728, 1, 0)` — модалка 1559 в режиме подарка: «Кому» 20196 + 12922.
	FAsdaPurchaseBoxRequest Req;
	Req.Mode          = bFunc ? EAsdaPurchaseMode::CashFunc : EAsdaPurchaseMode::CashEquip;   // режимы B / A
	Req.ItemCode      = GiftItemCode;
	Req.PanelMode     = 1;
	Req.Quantity      = 1;
	Req.TradeMode     = 1;
	Req.RecipientName = Gift->GetConfirmedName();
	if (const UAsdaItemTableSubsystem* ITl = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr)
	{
		const FString Plain = ITl->GetItemName(GiftItemCode);
		const FString Label = CardLabel(GiftItemCode, Plain);
		if (Label != Plain) { Req.NameOverride = Label; }   // ★ 06.11.2026: модалка 1559 называет подарок как карточка
	}
	Box->Open(Req, [this](bool bOk)
	{
		if (!bOk) { return; }   // 1562 «Отмена» — только закрытие
		// OK 1561 → `shopConfirmBuyItem@0xC43440`: нет записи → чат 12934; иначе при +10733 == 1 → `net_sendItemActionRequest` (5074).
		UGameInstance* G = GetGameInstance();
		const UAsdaItemTableSubsystem* IT = G ? G->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		FAsdaSpecialItemlistRow Sp;
		if (!IT || (!IT->GetItem(GiftItemCode) && !IT->GetSpecialItem(GiftItemCode, Sp)))
		{
			AsdaNotice::ChatColored(G, EAsdaMsgCategory::Trade, Loc(kLocBadItem), AsdaNoticeColor::Gold, 10000.f);
			return;
		}
		if (UAsdaCashGiftSubsystem* Gf = G ? G->GetSubsystem<UAsdaCashGiftSubsystem>() : nullptr)
		{
			// ★ 06.11.2026 (решение владельца): подарок = покупка себе. Цена проверяется по карточке, в 5074 уходит код выдачи — GameServer дарит присланный код.
			Gf->SendPresent(EAsdaGiftSource::Shop6000, GiftItemCode, 1, CashGxp, FString(), GiftWireCode(GiftItemCode));
		}
	});
}

void UAsdaPremiumShopSubsystem::CloseGiftPanels()
{
	// `uiDialogCloseReset@0xC42600` (ui_core.c:28865-28890): код −1, [2078]/[2072] = 0, поля и имя пустые, панели скрыты,
	//   модалка закрыта, фокус с полей снят.
	GiftItemCode = -1;
	bPanelBuy = false;
	CartPick.Reset();
	CartPickRow = -1;
	if (UAsdaCashGiftSubsystem* Gift = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCashGiftSubsystem>() : nullptr)
	{
		Gift->OnShopDialogCloseReset();
	}
	ShowGiftPanel(GiftEquip, false);
	ShowGiftPanel(GiftFunc, false);
	if (UAsdaPurchaseBoxSubsystem* Box = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPurchaseBoxSubsystem>() : nullptr)
	{
		if (Box->IsOpen()) { Box->Close(); }
	}
}

void UAsdaPremiumShopSubsystem::OnGiftClearFields(bool bBoth, int32 FieldIdc)
{
	if ((bBoth || FieldIdc == 6311) && GiftEquip.NameEdit) { GiftEquip.NameEdit->SetText(FText::GetEmpty()); }
	if ((bBoth || FieldIdc == 6345) && GiftFunc.NameEdit)  { GiftFunc.NameEdit->SetText(FText::GetEmpty()); }
}

void UAsdaPremiumShopSubsystem::DebugSetGiftName(const FString& Name)
{
	if (GiftEquip.Root && GiftEquip.Root->IsVisible() && GiftEquip.NameEdit) { GiftEquip.NameEdit->SetText(FText::FromString(Name)); }
	else if (GiftFunc.Root && GiftFunc.Root->IsVisible() && GiftFunc.NameEdit) { GiftFunc.NameEdit->SetText(FText::FromString(Name)); }
}

FString UAsdaPremiumShopSubsystem::DumpGiftPanels() const
{
	// Путь игрока (закон ui-check-game-path-geometry): видимость и абсолютный прямоугольник ПОСЛЕ отрисовки.
	FString S;
	auto Line = [&S](const TCHAR* Tag, const UWidget* W)
	{
		if (!W) { S += FString::Printf(TEXT("  %s: НЕТ УЗЛА\n"), Tag); return; }
		const FGeometry G = W->GetCachedGeometry();
		const FVector2D P = G.GetAbsolutePosition();
		const FVector2D Z = G.GetAbsoluteSize();
		FString T;
		if (const UTextBlock* Tb = Cast<UTextBlock>(W)) { T = Tb->GetText().ToString(); }
		if (const UEditableText* Ed = Cast<UEditableText>(W)) { T = Ed->GetText().ToString(); }
		S += FString::Printf(TEXT("  %s %s vis=%d экран=(%.0f,%.0f %.0fx%.0f) текст=«%s»\n"), Tag, *W->GetName(),
			W->IsVisible() ? 1 : 0, P.X, P.Y, Z.X, Z.Y, *T);
	};
	Line(TEXT("Подарок-карточки-0"), Cards.IsValidIndex(0) ? Cards[0].GiftButton.Get() : nullptr);
	Line(TEXT("Подарок-подпись-0"), Cards.IsValidIndex(0) ? Cards[0].GiftCaption.Get() : nullptr);
	for (const FAsdaShellGiftPanelUi* Pp : { &GiftEquip, &GiftFunc })
	{
		const FAsdaShellGiftPanelUi& P = *Pp;
		S += (Pp == &GiftEquip) ? TEXT("6296 SHELLITEM_EQUIP_WIN:\n") : TEXT("6333 SHELLITEM_FUNC_WIN:\n");
		Line(TEXT("корень"), P.Root.Get());
		Line(TEXT("имя-товара"), P.Title.Get());
		Line(TEXT("описание"), P.Substance.Get());
		Line(TEXT("стр1"), P.Txt01.Get());
		Line(TEXT("стр2"), P.Txt02.Get());
		Line(TEXT("стр3"), P.Txt03.Get());
		Line(TEXT("иконка"), P.Icon.Get());
		Line(TEXT("цена"), P.Price1.Get());
		Line(TEXT("кэш"), P.Price2.Get());
		Line(TEXT("строка-имени"), P.NameRow.Get());
		Line(TEXT("поле-имени"), P.NameEdit.Get());
		Line(TEXT("Проверить"), P.Check.Get());
		Line(TEXT("Подарок"), P.PreBtn.Get());
		Line(TEXT("Подарок-подпись"), P.PreBtnText.Get());
		Line(TEXT("Отмена"), P.CanBtn.Get());
		Line(TEXT("подпись-проф"), P.JobTxt.Get());
		Line(TEXT("подпись-ур"), P.LvTxt.Get());
	}
	S += FString::Printf(TEXT("код панели %d, вариантов %d, профессий %d, уровней %d\n"), GiftItemCode, bGiftVariants ? 1 : 0, GiftJobs.Num(), GiftLvCodes.Num());
	return S;
}

FString UAsdaPremiumShopSubsystem::DebugGiftTitle(bool bFunc) const
{
	const UTextBlock* T = (bFunc ? GiftFunc : GiftEquip).Title.Get();
	return T ? T->GetText().ToString() : FString();
}
