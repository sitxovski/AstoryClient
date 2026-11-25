// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSelfStoreSubsystem_Sell.cpp — ★ R3-c 12.11.2026 (разрез AsdaSelfStoreSubsystem.cpp, дословный перенос): окно продавца: виджет, контролы, сетка, вкладки, список продажи, кнопки, имя лавки; пакеты 6300–6306.
//   Общее для частей — в AsdaSelfStoreSubsystem_Private.h; шапка и реверс — в AsdaSelfStoreSubsystem.cpp.
#include "UI/AsdaSelfStoreSubsystem.h"

#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "UI/AsdaExchangeSubsystem.h"         // ★ пачка 20, жалоба 10: рынок 13000 открыт — лавка не открывается
#include "UI/AsdaTradeStateSubsystem.h"     // признаки 22000/23000 для чужих гейтов (общая точка волны 0)
#include "UI/AsdaJuiWindowHandle.h"         // каркас окна JUI (Ensure + зона рамки)
#include "UI/AsdaWindowSubsystem.h"         // окна, ESC, колесо, нумпад
#include "UI/AsdaNotice.h"                  // нотисы: msgbox 1500, плашка+чат
#include "UI/AsdaIconCellGrid.h"            // иконки предметов (общая точка)
#include "UI/AsdaItemTooltipSubsystem.h"    // подсказки предметов
#include "UI/AsdaTipPanel.h"                // EAsdaTipOwner::SelfStore
#include "UI/AsdaInventorySubsystem.h"      // ячейки, золото, закрытие сумки, записи 57 б
#include "UI/AsdaPremiumShopSubsystem.h"    // гейт 6300: элитный магазин (`helper_getField2075(g_pShopWnd)`)
#include "UI/AsdaCraftSubsystem.h"          // гейт 6300: окна ремесла (`g_pItemMainWnd` / `g_pItemQtyMakeDlg`)
#include "UI/AsdaChatWindowSubsystem.h"     // мат-фильтр `Chat_MatchBannedWord` (общая точка `IsCursed`)
#include "Npc/AsdaNpcDialogSubsystem.h"     // `g_bInventoryLocked` = открыт разговор с NPC (гейт 6302)
#include "Items/AsdaItemTableSubsystem.h"   // item_list / special_itemlist / sowell_list / booster_list
#include "Items/AsdaItemDbRows.h"
#include "World/AsdaPlayerController.h"     // поза 107/109 (`SendStallStatePose`), «мёртв»
#include "World/AsdaWorldSubsystem.h"       // имя своего персонажа
#include "Login/AsdaButtonRelay.h"
#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"                       // charId / userNum / уровень своего персонажа
#include "AsdaBytes.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Login/AsdaLoginData.h"                // DecodeCp1251 — название/имя из 6308
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "AsdaSelfStoreSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSelfStoreSubsystem*.cpp

namespace {
	const TCHAR* kSellWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_StreetStall_Open_Win.WBP_StreetStall_Open_Win_C");

	// ── ключи исправлений (закон `fix-original-defects`) ──
	ASDA_DEV_CVAR(int32, CVarFixK1, TEXT("asda.Trade.StoreFixK1"), 1,
		TEXT("Лавка, дефект К-1: 1 = 6306 применяет все 10 записей 57 б; 0 = как оригинал — только 6 (sellWindowOnSellResult, цикл i<6)."),
		ECVF_Default);
	ASDA_DEV_CVAR(int32, CVarFixK3, TEXT("asda.Trade.StoreFixK3"), 1,
		TEXT("Лавка, дефект К-3: 1 = 6306 во время «Изменить» (переоткрытие) не сбрасывает признак премиум-лавки; 0 = как оригинал."),
		ECVF_Default);

	ASDA_DEV_CVAR(int32, CVarFixK16, TEXT("asda.Trade.StoreFixK16"), 1,
		TEXT("Лавка, дефект К-16: 1 = одноразовый флаг 6302 гаснет только при реальной отправке; 0 = как оригинал — гаснет ДО проверки "
		     "названия (после 107062/9005 кнопка «OK» не работает до переоткрытия окна)."),
		ECVF_Default);

	// `atoi` после удаления запятых (`sellWnd_OnPriceButton` виды 5/6): пробелы, знак, цифры до первой не-цифры.
	//   [ПОРТ] Считается в int64 и прижимается к ±2^31 (у CRT `atoi` переполнение не определено).
	int64 AtoiNoCommas(const FString& In)
	{
		FString S = In.Replace(TEXT(","), TEXT(""));
		int32 i = 0;
		while (i < S.Len() && FChar::IsWhitespace(S[i])) { ++i; }
		bool bNeg = false;
		if (i < S.Len() && (S[i] == TEXT('-') || S[i] == TEXT('+'))) { bNeg = (S[i] == TEXT('-')); ++i; }
		int64 V = 0;
		while (i < S.Len() && FChar::IsDigit(S[i])) { V = FMath::Min<int64>(V * 10 + (S[i] - TEXT('0')), (int64)MAX_int32 + 1); ++i; }
		return bNeg ? -V : V;
	}
}

// ═════════════════════════════ ОКНО 22000: КАРКАС И РАЗБОР ════════════════════════════════════
bool UAsdaSelfStoreSubsystem::EnsureSellWidget()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kSellId, kSellWbp, kWinZ).Ensure(GetGameInstance(), SellWidget, bSellReady,
		[this](UAsdaWindowSubsystem&) { ResetSellWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupSellControls();
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kSellId, this, SellWidget, bSellOpen, TEXT("StreetStall_Open_Win_0_%d"), 9);
		},
		[]() { UE_LOG(LogAsdaSelfStore, Error, TEXT("[лавка] WBP_StreetStall_Open_Win не загрузился (%s)"), kSellWbp); });
	return FAsdaJuiWindowHandle::Succeeded(R) && bSellReady;
}

void UAsdaSelfStoreSubsystem::ResetSellWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kSellId);
		if (SellWidget) { WM->CloseWindow(kSellId); }
	}
	SellWidget = nullptr;
	bSellReady = false;
	if (bSellOpen)
	{
		bSellOpen = false;
		if (UAsdaTradeStateSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTradeStateSubsystem>() : nullptr)
		{
			TS->SetSellWindowOpen(false, TEXT("сброс виджета 22000"));
		}
	}
	BtnSellClose = BtnNameSave = BtnOk = BtnCancel = nullptr;
	EdName = nullptr;
	TxtNameTitle = TxtNameSave = TxtSumCap = TxtSum = TxtGoldCap = TxtGold = TxtOk = TxtCancel = nullptr;
	Hdrs.Reset(); GridImg.Reset(); GridTxt.Reset(); TabPlanes.Reset(); TabButtons.Reset(); TabCaptions.Reset();
	TabIdlePlanes.Reset(); TabIdleStyles.Reset(); TabIdleCaptions.Reset();
	SellRows.Reset();
	GridIcons = nullptr;
	SellIcons = nullptr;
	Found22000 = 0;
	NameCache.Reset();
	EditCache.Init(FString(), 2 * kMaxLots);
}

void UAsdaSelfStoreSubsystem::SetupSellControls()
{
	bSellReady = false;
	if (!SellWidget || !SellWidget->WidgetTree) { return; }
	Hdrs.SetNum(5); GridImg.SetNum(kGridCells); GridTxt.SetNum(kGridCells);
	TabPlanes.SetNum(2); TabButtons.SetNum(2); TabCaptions.SetNum(2);
	SellRows.SetNum(kMaxLots);
	for (FAsdaStallSellRowUi& R : SellRows) { R.Btn.SetNum(4); }
	TArray<FName> GridNames; GridNames.SetNum(kGridCells);
	TArray<FName> IconNames; IconNames.SetNum(kMaxLots);

	// `CSellWindow_InitControls@0xDCEF80` (shop_trade.c:15402-15521): все привязки по IDNum; имена — правило импортёра
	//   (`Reports/trade/w0/jui_vs_wbp.txt`).
	TArray<UWidget*> All;
	SellWidget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* B = Cast<UButton>(W))
		{
			if (N == TEXT("Stall_Open_ClsBtn"))              { BtnSellClose = B; continue; }   // 22004
			if (N == TEXT("Stall_Open_NameSave"))            { BtnNameSave = B; continue; }    // 22012
			if (N == TEXT("Stall_Open_OKBtn"))               { BtnOk = B; continue; }          // 22500
			if (N == TEXT("Stall_Open_CancelBtn"))           { BtnCancel = B; continue; }      // 22501
			if (N == TEXT("Stall_Open_InvenRadioBtn_OPT0"))  { TabButtons[0] = B; continue; }  // 22200 вкладка 0
			if (N == TEXT("Stall_Open_InvenRadioBtn_OPT1"))  { TabButtons[1] = B; continue; }  // 22200 вкладка 1
			for (int32 R = 0; R < kMaxLots; ++R)
			{
				for (int32 K = 0; K < 4; ++K)
				{
					if (N == FString::Printf(TEXT("Stall_Open_SellBtn%02d_%d"), R, K)) { SellRows[R].Btn[K] = B; }   // 22302/3/5/6 +20R
				}
			}
			continue;
		}
		if (UEditableText* E = Cast<UEditableText>(W))
		{
			if (N == TEXT("Stall_Open_NameInput")) { EdName = E; continue; }                   // 22011
			for (int32 R = 0; R < kMaxLots; ++R)
			{
				if (N == FString::Printf(TEXT("Stall_Open_SellPrice%02d"), R))  { SellRows[R].Price = E; }   // 22301+20R
				if (N == FString::Printf(TEXT("Stall_Open_SellAmount%02d"), R)) { SellRows[R].Qty = E; }     // 22304+20R
			}
			continue;
		}
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			if (N == TEXT("Stall_Open_NameInputTitle"))      { TxtNameTitle = T; continue; }   // 22010
			if (N == TEXT("Stall_Open_NameSave_Text"))       { TxtNameSave = T; continue; }    // подпись 22012
			if (N == TEXT("Stall_Open_SellTitle"))           { Hdrs[0] = T; continue; }        // 22201
			if (N == TEXT("Stall_Open_InvenTitle"))          { Hdrs[1] = T; continue; }        // 22021
			if (N == TEXT("Stall_Open_SellTxt0"))            { Hdrs[2] = T; continue; }        // 22210
			if (N == TEXT("Stall_Open_SellTxt1"))            { Hdrs[3] = T; continue; }        // 22211
			if (N == TEXT("Stall_Open_SellTxt2"))            { Hdrs[4] = T; continue; }        // 22212
			if (N == TEXT("Stall_Open_InvenRadioBtn_Text"))  { TabCaptions[0] = T; continue; } // 22200 подпись 0
			if (N == TEXT("Stall_Open_InvenRadioBtn_Text2")) { TabCaptions[1] = T; continue; } // 22200 подпись 1
			if (N == TEXT("Stall_Open_Txt0"))                { TxtSumCap = T; continue; }      // 22510
			if (N == TEXT("Stall_Open_Sum"))                 { TxtSum = T; continue; }         // 22511
			if (N == TEXT("Stall_Open_Txt1"))                { TxtGoldCap = T; continue; }     // 22512
			if (N == TEXT("Stall_Open_Gold"))                { TxtGold = T; continue; }        // 22513
			if (N == TEXT("Stall_Open_OKBtn_Text"))          { TxtOk = T; continue; }          // подпись 22500
			if (N == TEXT("Stall_Open_CancelBtn_Text"))      { TxtCancel = T; continue; }      // подпись 22501
			for (int32 i = 0; i < kGridCells; ++i)
			{
				if (N == FString::Printf(TEXT("Stall_Open_Inven%02d"), i)) { GridTxt[i] = T; }   // 22100+i
			}
			for (int32 R = 0; R < kMaxLots; ++R)
			{
				if (N == FString::Printf(TEXT("Stall_Open_SellIcon%02d"), R))   { SellRows[R].IconText = T; }   // 22308+20R
				if (N == FString::Printf(TEXT("Stall_Open_SellTPirce%02d"), R)) { SellRows[R].Total = T; }      // 22307+20R
			}
			continue;
		}
		// Планы `<idc>_<juiIdx>_<plane>`.
		FString Idc; int32 JuiIdx = 0, Plane = 0;
		if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, Plane)) { continue; }
		if (Idc == TEXT("Stall_Open_InvenRadioBtn") && Plane >= 0 && Plane < 2) { TabPlanes[Plane] = Cast<UImage>(W); continue; }
		if (Plane != 0) { continue; }
		for (int32 i = 0; i < kGridCells; ++i)
		{
			if (Idc == FString::Printf(TEXT("Stall_Open_Inven%02d"), i)) { GridImg[i] = Cast<UImage>(W); GridNames[i] = FName(*N); }
		}
		for (int32 R = 0; R < kMaxLots; ++R)
		{
			if (Idc == FString::Printf(TEXT("Stall_Open_SellIcon%02d"), R)) { SellRows[R].Icon = Cast<UImage>(W); IconNames[R] = FName(*N); }
		}
	}

	// ── Иконки — общая точка сеток (клик/наведение окно ловит само по живой геометрии, как окно 76000). ──
	const UAsdaNetSubsystem* NetG = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bMale = !NetG || NetG->ResolveSelfGender() != 2;   // `g_ItemImgOptionVariant` — атлас пола
	GridIcons = NewObject<UAsdaIconCellGrid>(this);
	GridIcons->SetCellSizePx(32.f);
	GridIcons->SetClickOverlayEnabled(false);
	GridIcons->bClearEmptyCells = true;   // `CControl_DrawTextureRect(ячейка, пустая текстура, 0,0,32,32)` при очистке
	GridIcons->SetupNamed(SellWidget, GridNames, bMale, [this](int32 C) -> int32
	{
		const FAsdaItemInstance* It = GridRef.IsValidIndex(C) ? CellItem(GridRef[C].X, GridRef[C].Y) : nullptr;
		return It ? FMath::Max(0, It->ItemCode) : 0;
	}, [](int32) {});
	SellIcons = NewObject<UAsdaIconCellGrid>(this);
	SellIcons->SetCellSizePx(32.f);
	SellIcons->SetClickOverlayEnabled(false);
	SellIcons->bClearEmptyCells = true;
	SellIcons->SetupNamed(SellWidget, IconNames, bMale, [this](int32 R) -> int32
	{
		const int32 E = RowEntry.IsValidIndex(R) ? RowEntry[R] : -1;
		const FAsdaItemInstance* It = SellList.IsValidIndex(E) ? CellItem(SellList[E].Container, SellList[E].Cell) : nullptr;
		return It ? FMath::Max(0, It->ItemCode) : 0;
	}, [](int32) {});

	// ── Кнопки (ветка 66561 `uiSellWindowMsgProc`, ui_core.c:64999-65028) ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void()>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, [F = MoveTemp(Fn)](int32) { F(); }, Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnSellClose, 22004, [this]() { SendClose6305(TEXT("крестик 22004")); });
	Bind(BtnNameSave,  22012, [this]() { OnNameSaveClicked(); });
	Bind(BtnOk,        22500, [this]() { OnOkClicked(); });
	Bind(BtnCancel,    22501, [this]() { SendClose6305(TEXT("«Отмена» 22501")); });
	Bind(TabButtons[0], 22200, [this]() { OnTabSelected(0); });   // 66817 `a5 = 0`
	Bind(TabButtons[1], 22200, [this]() { OnTabSelected(1); });   // 66817 `a5 = 1`
	for (int32 R = 0; R < kMaxLots; ++R)
	{
		for (int32 K = 0; K < 4; ++K)
		{
			const int32 IdNum = 20 * R + (K < 2 ? 22302 + K : 22305 + (K - 2));
			Bind(SellRows[R].Btn[K], IdNum, [this, R, K]() { OnRowButton(R, K + 1); });
		}
	}

	// ── Подписи и начальные значения — хвост `CSellWindow_InitControls` ──
	SetTxt(TxtNameTitle, Loc(L::kNameTitle));      // 22010 = 105000
	SetTxt(TxtNameSave, Loc(L::kNameSave));        // 22012 = 105001 (`Collection_AddString`)
	static const int32 kHdrLoc[5] = { L::kHdrSell, L::kHdrBag, L::kHdrPrice, L::kHdrQty, L::kHdrTotal };
	for (int32 i = 0; i < 5; ++i) { SetTxt(Hdrs[i], Loc(kHdrLoc[i])); }   // 22201/22021/22210/22211/22212
	SetTxt(TabCaptions[0], Loc(L::kTab0));         // `CListBox_SetItemString(22200, 0, 105007)`
	SetTxt(TabCaptions[1], Loc(L::kTab1));         // `CListBox_SetItemString(22200, 1, 105008)`
	SetTxt(TxtSumCap, Loc(L::kSumCap));            // 22510 = 105009
	SetTxt(TxtGoldCap, Loc(L::kGoldCap));          // 22512 = 105010
	SetTxt(TxtOk, Loc(L::kOk));                    // 22500 = 105011
	SetTxt(TxtCancel, Loc(L::kCancel));            // 22501 = 105012
	// 22011 = `sprintf(107061 «Магазин '%s'», имя)`, лимит 50 (`ctrlEditSetTextLength`).
	// ★ [ПОРТ] 08.10.2026: лимит в БАЙТАХ провода (поле 6302 +455 — 50 б без нуля).
	const FString DefName = FAsdaLoginData::TruncateToWireLimit(Loc(L::kDefName).Replace(TEXT("%s"), *SelfName()), kNameMaxChars, 50);
	SetEdit(EdName, DefName);
	NameCache = DefName;
	// Заглушки ассета («9999,657,356,000», «10,200,000,000» — Р-3 волны 0) перетираются первой же перерисовкой.
	for (FAsdaStallSellRowUi& R : SellRows) { SetTxt(R.Total, FString()); SetTxt(R.IconText, FString()); SetEdit(R.Price, FString()); SetEdit(R.Qty, FString()); }
	SetTxt(TxtSum, TEXT("0"));

	// ── Итог разбора: 128 привязок эталона (22199 — колесо без узла WBP, обслуживается кодом) ──
	int32 Found = SellWidget ? 1 : 0;
	auto Has = [&Found](const UObject* O) { if (O) { ++Found; } };
	Has(BtnSellClose); Has(TxtNameTitle); Has(EdName); Has(BtnNameSave);
	for (UTextBlock* T : Hdrs) { Has(T); }
	for (const FAsdaStallSellRowUi& R : SellRows)
	{
		// 22301…22308 +20N: цена, ▲, ▼, кол-во, ▲, ▼, итог, иконка (контрол 22308 = план + надпись — один счёт).
		Has(R.Price); for (UButton* B : R.Btn) { Has(B); } Has(R.Qty); Has(R.Total);
		if (R.Icon && R.IconText) { ++Found; }
	}
	if (TabButtons[0] && TabButtons[1] && TabPlanes[0] && TabPlanes[1]) { ++Found; }   // 22200 — один RadioButton
	for (int32 i = 0; i < kGridCells; ++i) { if (GridImg[i] && GridTxt[i]) { ++Found; } }
	Has(TxtSumCap); Has(TxtSum); Has(TxtGoldCap); Has(TxtGold); Has(BtnOk); Has(BtnCancel);
	Found22000 = Found;
	bSellReady = (EdName != nullptr && BtnOk != nullptr && GridImg[0] != nullptr && SellRows[0].Price != nullptr);
	UE_LOG(LogAsdaSelfStore, Log, TEXT("[лавка] окно 22000 разобрано: привязок %d из 127 (+ колесо 22199 кодом), готово=%d"),
		Found22000, bSellReady ? 1 : 0);
}

// ═════════════════════════════ ОКНО 22000: ОТКРЫТИЕ / ЗАКРЫТИЕ ════════════════════════════════
void UAsdaSelfStoreSubsystem::OpenSellWindow()
{
	// `sellWindow_Open@0xDCF6C0` (shop_trade.c:15512-15535).
	if (!EnsureSellWidget()) { JournalAdd(TEXT("22000 не открыто: WBP не разобран")); return; }
	if (bSellOpen)
	{
		// Окно уже открыто ⇒ `return antiCheat_SendPositionReport(this)` (тумблер через сервер).
		SendClose6305(TEXT("повторное открытие окна 22000 (тумблер sellWindow_Open)"));
		return;
	}
	UGameInstance* GI = GetGameInstance();
	// `sowelWindowClose(g_pInventoryWnd, 1)` — МИСНЕЙМ: закрыть сумку.
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr) { if (Inv->IsWindowOpen()) { Inv->CloseWindow(); } }
	bSellOpen = true;   // `+0x300 = 1`
	if (UAsdaTradeStateSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTradeStateSubsystem>() : nullptr) { TS->SetSellWindowOpen(true, TEXT("sellWindow_Open (6301 код 1)")); }
	// `if (!g_bSellWindowActive) sub_92C2E5(list)` — список очищается, если это не «Изменить» (переоткрытие хранит выставленное).
	if (!bReopen) { SellList.Reset(); }
	bReopen = false;   // `g_bSellWindowActive = 0`
	PendingItem = FIntPoint(-1, -1);
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (WM)
	{
		WM->SetWindowVisible(kSellId, true);
		WM->BringWindowToFront(kSellId);   // `CClass_MapInsert(g_pWindowMgr, root)`
	}
	ScrollPos = 0;     // `uiCtrl_SetChildScrollbarPos(22199, 0)`
	SourceTab = 0;     // `CControl_SetSelectedIndex(22200, 0)`, `+0x2F0 = 0`
	RefreshTabs();
	RefreshGrid();     // `CWarehouseDlg_RefreshItemGrid`
	RefreshSellList(); // `CSellWindow_RefreshItemList`
	bCanSendOpen = true;   // `+0x388 = 1`
	Mouse.Reset();
	JournalAdd(FString::Printf(TEXT("22000 открыто: строк продажи %d (переоткрытие сохраняет список)"), SellList.Num()));
}

void UAsdaSelfStoreSubsystem::HideSellWindow(const TCHAR* Why)
{
	// `sellWndHide@0xDD0DC0`: скрыть корень, `+0x300 = 0`.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kSellId, false);
	}
	const bool bWas = bSellOpen;
	bSellOpen = false;
	if (UAsdaTradeStateSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTradeStateSubsystem>() : nullptr)
	{
		TS->SetSellWindowOpen(false, Why);
	}
	if (TipKind == 0 || TipKind == 1) { TickTip(-1, -1); }
	if (bWas) { JournalAdd(FString::Printf(TEXT("22000 скрыто (%s)"), Why)); }
}

// ═════════════════════════════ ОКНО 22000: СЕТКА СУМКИ ═════════════════════════════════════════
const FAsdaItemInstance* UAsdaSelfStoreSubsystem::CellItem(int32 Container, int32 Cell) const
{
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	return (Inv && Container >= 0 && Cell >= 0) ? Inv->GetCell(Container, Cell) : nullptr;
}

void UAsdaSelfStoreSubsystem::RefreshGrid()
{
	// Вкладка 0 — `CWarehouseDlg_RefreshItemGrid@0xDD1AF0` (МИСНЕЙМ; item_inventory.c:54654): `g_InventoryArray2`, 60 ячеек,
	//   страница 6 × позиция колеса. Вкладка 1 — `CInventoryDlg_RefreshItemGrid@0xDD25A0` (:54866): `g_InventoryBag`, 30,
	//   без страниц, гейт уровня 24 у `categoryFlag == 3` и у спец-предметов.
	if (!bSellReady) { return; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const int32 Level = AsdaSelf::GetLevel(GI);
	// 1) Очистка 30 ячеек: пустая иконка, пустая надпись, ссылка NULL, альфа 0xFF, цвет надписи −1.
	for (int32 i = 0; i < kGridCells; ++i)
	{
		GridRef[i] = FIntPoint(-1, -1);
		SetTxt(GridTxt[i], FString());
		SetTxtColor(GridTxt[i], kWhite);
	}
	// 2) Кандидаты (`sub_92EF8B` в список `+0x2DC`/`+0x2E8`) — с фильтром эталона.
	const int32 Cont = (SourceTab == 0) ? 2 : 1;
	const int32 NSlots = (SourceTab == 0) ? UAsdaInventorySubsystem::kSpendSlots : UAsdaInventorySubsystem::kShellSlots;
	TArray<int32> Cand;
	for (int32 j = 0; j < NSlots && IT; ++j)
	{
		const FAsdaItemInstance* It = CellItem(Cont, j);
		if (!It || It->ItemCode <= 0) { continue; }   // `CItemManager_Get*Record(0)` → NULL ⇒ ни одна ветка
		const uint8 B50 = It->Raw.IsValidIndex(50) ? It->Raw[50] : 0;   // байт +50 записи слота («не продаётся»)
		if (const FAsdaItemRecord* Rec = IT->GetItem(It->ItemCode))
		{
			if (SourceTab == 1 && Rec->CategoryFlag == 3 && Level < 24) { continue; }
			if ((B50 == 3 && Rec->ItemSubType == 32) || B50 != 0 || Rec->TooltipFlag200 == 2) { continue; }
			Cand.Add(j);
		}
		FAsdaSpecialItemlistRow Sp;
		if (IT->GetSpecialItem(It->ItemCode, Sp))
		{
			if ((SourceTab == 1 && Level < 24) || B50 != 0 || Sp.Flag124 == 2) { continue; }
			Cand.Add(j);
		}
		FAsdaSowellRow Sw;
		if (IT->IsSowelCode(It->ItemCode) && IT->GetSowell(It->ItemCode, Sw) && UAsdaItemTableSubsystem::SowelRawByte(Sw, 0x42) == 0)
		{
			Cand.Add(j);   // `sowelRecord && !gap_41[1]` (+0x42 — «вещь не передаётся»)
		}
	}
	// 3) Раскладка по ячейкам.
	//   ★ [ПОРТ] 29.10.2026 (инвентарь ×2): эталон листает только вкладку 0 (на вкладке 1 сумка 30 = вся сетка); при сумке
	//   60 вкладка 1 листается тем же колесом 22199 по рядам (`AsdaSellScrollMax`). При позиции 0 — ровно эталон.
	const int32 PageStart = kGridCols * FMath::Clamp(ScrollPos, 0, AsdaSellScrollMax(SourceTab));
	int32 CellIdx = 0, Skip = 0;
	for (int32 j : Cand)
	{
		const FAsdaItemInstance* It = CellItem(Cont, j);
		if (!It) { continue; }
		if (SourceTab == 0)
		{
			if (It->ItemCode == UAsdaInventorySubsystem::kGoldItemCode) { continue; }   // `*pItemEntry != 20551`
		}
		if (Skip < PageStart) { ++Skip; continue; }   // ★ 29.10.2026: и вкладка 1 (у эталона там PageStart = 0)
		if (CellIdx >= kGridCells) { break; }
		const int32 Start = CellIdx;
		GridRef[Start] = FIntPoint(Cont, j);
		auto Label = [this, It](int32 C, bool bCountColor)
		{
			if (!GridTxt.IsValidIndex(C)) { return; }
			if (It->Amount > 1)
			{
				SetTxt(GridTxt[C], FString::Printf(TEXT("%d"), It->Amount));
				if (bCountColor) { SetTxtColor(GridTxt[C], kWhite); }
			}
			if (It->GemGrade != 0)
			{
				SetTxtColor(GridTxt[C], kYellow);
				SetTxt(GridTxt[C], FString::Printf(TEXT("+%d%%"), (int32)(int16)It->EnchantPercent));
			}
		};
		if (IT->GetItem(It->ItemCode)) { Label(CellIdx, true); ++CellIdx; }
		FAsdaSpecialItemlistRow Sp;
		if (IT->GetSpecialItem(It->ItemCode, Sp) && CellIdx < kGridCells) { Label(CellIdx, SourceTab == 1); ++CellIdx; }
		FAsdaSowellRow Sw;
		if (IT->IsSowelCode(It->ItemCode) && IT->GetSowell(It->ItemCode, Sw) && CellIdx < kGridCells) { ++CellIdx; }
		if (CellIdx >= kGridCells) { /* разложено 30 — дальше эталон выходит после проверки выставленного */ }
	}
	// 4) Иконки (провайдер — `GridRef`) и затемнение выставленного (`CControl_SetColor4(…, 0x7D)` + надпись 0x7DFFFFFF).
	if (GridIcons) { GridIcons->Refresh(); }
	for (int32 i = 0; i < kGridCells; ++i)
	{
		SetImgAlpha(GridImg[i], 1.f);
		const FAsdaItemInstance* It = GridRef[i].X >= 0 ? CellItem(GridRef[i].X, GridRef[i].Y) : nullptr;
		if (It && IsListed(GridRef[i].X, GridRef[i].Y, It->ItemCode))
		{
			SetImgAlpha(GridImg[i], kDimAlpha);
			SetTxtColor(GridTxt[i], kDimText);
		}
	}
	// 5) Золото 22513 — `honorPoints_resetDisplayIfZero(g_pGameClient)`.
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	SetTxt(TxtGold, AsdaUi::FormatThousands(Inv ? Inv->GetGold() : 0));
}

void UAsdaSelfStoreSubsystem::RefreshTabs()
{
	// Вид радио 22200 — общая точка вкладок порта (как у окна 76000).
	if (TabPlanes.Num() == 2 && TabButtons.Num() == 2 && TabCaptions.Num() == 2)
	{
		AsdaUi::ApplyTabState(TabPlanes, TabCaptions, TabButtons, SourceTab, TabIdlePlanes, TabIdleStyles, TabIdleCaptions);
	}
}

void UAsdaSelfStoreSubsystem::OnTabSelected(int32 Tab)
{
	// 66817 по 22200 (ui_core.c:64974-64988): `+0x2F0 = a5`; 0 — сетка расходников, колесо видно, радио 0; 1 — сумка, колесо скрыто.
	if (!bSellOpen) { return; }
	SourceTab = FMath::Clamp(Tab, 0, 1);
	ScrollPos = FMath::Clamp(ScrollPos, 0, AsdaSellScrollMax(SourceTab));   // ★ 29.10.2026 (инвентарь ×2): предел новой вкладки
	RefreshTabs();
	RefreshGrid();
	JournalAdd(FString::Printf(TEXT("вкладка %d (%s)"), SourceTab, SourceTab == 0 ? TEXT("расходники, контейнер 2") : TEXT("снаряжение, контейнер 1")));
}

void UAsdaSelfStoreSubsystem::OnWheel(int32 Notches)
{
	// 0x17 по 22199 (ui_core.c:65040-65046): `+0x2EC = позиция`; перерисовка только на вкладке 0. «От себя» (+1) — вверх.
	//   ★ [ПОРТ] 29.10.2026 (инвентарь ×2): и на вкладке 1 при сумке 60 (`AsdaSellScrollMax`).
	if (!bSellOpen || AsdaSellScrollMax(SourceTab) == 0) { return; }
	const int32 NewPos = FMath::Clamp(ScrollPos - Notches, 0, AsdaSellScrollMax(SourceTab));
	if (NewPos == ScrollPos) { return; }
	ScrollPos = NewPos;
	RefreshGrid();
	JournalAdd(FString::Printf(TEXT("колесо 22199: позиция %d"), ScrollPos));
}

// ═════════════════════════════ ОКНО 22000: СПИСОК ПРОДАЖИ ══════════════════════════════════════
bool UAsdaSelfStoreSubsystem::IsListed(int32 Container, int32 Cell, int32 ItemId) const
{
	// Сравнение эталона: `code == code && (i16)+5 == (i16)+5 && (u8)+4 == (u8)+4` — по живым записям обеих ячеек.
	const FAsdaItemInstance* New = CellItem(Container, Cell);
	for (const FSellEntry& E : SellList)
	{
		const FAsdaItemInstance* Old = CellItem(E.Container, E.Cell);
		if (!Old || !New) { continue; }
		if (Old->ItemCode == ItemId && Old->SlotIndex == New->SlotIndex && Old->ContainerType == New->ContainerType) { return true; }
	}
	return false;
}

int32 UAsdaSelfStoreSubsystem::BoosterAvgValue(int32 ItemId) const
{
	// `booster_calcAvgValue@0xDD0000` (shop_trade.c:15767): 35 позиций записи бустера (`+4+8i`); item_list → `subCategory`
	//   (+0xA0), самоцвет → `field_30` (+0x30), спец-предмет → `value_108` (+0x6C); среднее; сумма или счётчик 0 ⇒ 0.
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(ItemId) : nullptr;
	if (!Rec) { return 0; }
	const FAsdaBoosterListRow* B = IT->FindBoosterRow(Rec->DurationOrEnchant);
	if (!B) { return 0; }
	const int32 Codes[35] = {
		B->Slot00ItemCode, B->Slot01ItemCode, B->Slot02ItemCode, B->Slot03ItemCode, B->Slot04ItemCode,
		B->Slot05ItemCode, B->Slot06ItemCode, B->Slot07ItemCode, B->Slot08ItemCode, B->Slot09ItemCode,
		B->Slot10ItemCode, B->Slot11ItemCode, B->Slot12ItemCode, B->Slot13ItemCode, B->Slot14ItemCode,
		B->Slot15ItemCode, B->Slot16ItemCode, B->Slot17ItemCode, B->Slot18ItemCode, B->Slot19ItemCode,
		B->Slot20ItemCode, B->Slot21ItemCode, B->Slot22ItemCode, B->Slot23ItemCode, B->Slot24ItemCode,
		B->Slot25ItemCode, B->Slot26ItemCode, B->Slot27ItemCode, B->Slot28ItemCode, B->Slot29ItemCode,
		B->Slot30ItemCode, B->Slot31ItemCode, B->Slot32ItemCode, B->Slot33ItemCode, B->Slot34ItemCode };
	int64 Sum = 0; int32 Count = 0;
	for (int32 Code : Codes)
	{
		if (const FAsdaItemRecord* R = IT->GetItem(Code)) { Sum += R->SubCategory; ++Count; continue; }
		FAsdaSowellRow Sw;
		if (IT->IsSowelCode(Code) && IT->GetSowell(Code, Sw)) { Sum += Sw.SellPrice; ++Count; continue; }
		FAsdaSpecialItemlistRow Sp;
		if (IT->GetSpecialItem(Code, Sp)) { Sum += Sp.Value108; ++Count; }
	}
	if (Sum == 0 || Count == 0) { return 0; }
	return (int32)(Sum / Count);
}

void UAsdaSelfStoreSubsystem::AddItemToSell(int32 Container, int32 Cell, int32 Quantity)
{
	// `CSellWindow_addItemToSell@0xDCFC60` (shop_trade.c:15669-15760).
	const FAsdaItemInstance* It = CellItem(Container, Cell);
	if (!It || It->ItemCode <= 0) { return; }
	UGameInstance* GI = GetGameInstance();
	if (SellList.Num() >= kMaxLots)
	{
		// 11-я строка: `CChat_AddTokenizedLines(107010, −256, 10000)` + `CControl_SetTooltipText(g_pChatWnd, 107010, −256, 10000)`.
		AsdaNotice::PlankAndChatLoc(GI, EAsdaMsgCategory::Trade, L::kListFull, kYellow, 10000.f, 22000, /*bChatFirst*/ true);
		JournalAdd(TEXT("11-я строка: 107010 (чат + плашка)"));
		return;
	}
	if (IsListed(Container, Cell, It->ItemCode)) { return; }   // дубликат — молча
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return; }
	int32 ItemCount = 0, UnitPrice = 0, BasePrice = 0;
	if (const FAsdaItemRecord* Rec = IT->GetItem(It->ItemCode))
	{
		ItemCount = It->Amount;
		UnitPrice = Rec->Price;
		if (Rec->ItemSubType == 66 || Rec->ItemSubType == 200 || Rec->ItemSubType == 201) { UnitPrice = BoosterAvgValue(It->ItemCode); }
		if (Rec->CategoryFlag == 3) { UnitPrice = (int32)(uint32)(uint64)((double)(uint32)Rec->Price * 100.0); }
		BasePrice = Rec->Price;
	}
	FAsdaSpecialItemlistRow Sp;
	if (IT->GetSpecialItem(It->ItemCode, Sp))
	{
		ItemCount = It->Amount;
		UnitPrice = (int32)(uint32)(uint64)((double)Sp.Price * 100.0);
		BasePrice = Sp.Price;
	}
	FAsdaSowellRow Sw;
	if (IT->IsSowelCode(It->ItemCode) && IT->GetSowell(It->ItemCode, Sw))
	{
		ItemCount = It->Amount;
		UnitPrice = Sw.BuyPrice;   // `field_34`
		BasePrice = Sw.BuyPrice;
	}
	// [ПОРТ] P43: цена за единицу не выше 1 000 000 000 (сервер отвергает дороже — «Wrong Price» → 107040).
	UnitPrice = FMath::Min(UnitPrice, kPriceCap);
	FSellEntry E;
	E.Container = Container;
	E.Cell = Cell;
	E.BasePrice = BasePrice;
	E.UnitPrice = UnitPrice;
	E.Qty = (Quantity != 0) ? Quantity : It->Amount;
	if (E.Qty == 0) { E.Qty = 1; }
	E.Total = (int64)E.UnitPrice * E.Qty;
	E.MaxQty = ItemCount;
	SellList.Add(E);
	JournalAdd(FString::Printf(TEXT("выставлено: код %d (к=%d ячейка %d) цена %d кол-во %d макс %d"),
		It->ItemCode, Container, Cell, E.UnitPrice, E.Qty, E.MaxQty));
	RefreshSellList();
	RefreshGrid();
}

void UAsdaSelfStoreSubsystem::RemoveFromList(int32 Container, int32 Cell)
{
	// `cSellWindow_removeItemFromList@0xDCFB00`: найти по «код + слот + контейнер», удалить, перерисовать список и сетку.
	const FAsdaItemInstance* It = CellItem(Container, Cell);
	for (int32 i = 0; i < SellList.Num(); ++i)
	{
		const FAsdaItemInstance* Old = CellItem(SellList[i].Container, SellList[i].Cell);
		const bool bSame = (SellList[i].Container == Container && SellList[i].Cell == Cell)
			|| (Old && It && Old->ItemCode == It->ItemCode && Old->SlotIndex == It->SlotIndex && Old->ContainerType == It->ContainerType);
		if (bSame) { SellList.RemoveAt(i); break; }
	}
	RefreshSellList();
	RefreshGrid();
}

void UAsdaSelfStoreSubsystem::RefreshSellList()
{
	// `CSellWindow_RefreshItemList@0xDD01A0` (shop_trade.c:15441-15685).
	if (!bSellReady) { return; }
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const bool bFix = CVarFixK5K6.GetValueOnGameThread() != 0;
	bNameGuard = true;
	// 1) Очистка 10 строк: иконка, надпись, поля кол-ва/цены, итог; `gap_28[40N] = gap_28[40N+4] = 0`.
	for (int32 R = 0; R < kMaxLots; ++R)
	{
		RowEntry[R] = -1;
		SetTxt(SellRows[R].IconText, FString());
		SetEdit(SellRows[R].Qty, FString());
		SetEdit(SellRows[R].Price, FString());
		SetTxt(SellRows[R].Total, FString());
	}
	// 2) Выбросить записи опустевших ячеек (`entryItemId && !*entryDup`).
	SellList.RemoveAll([this](const FSellEntry& E) { const FAsdaItemInstance* It = CellItem(E.Container, E.Cell); return !It || It->ItemCode == 0; });
	// 3) Раскладка.
	int64 Sum = 0;
	int32 Row = 0;
	for (int32 i = 0; i < SellList.Num() && IT; ++i)
	{
		FSellEntry& E = SellList[i];
		const FAsdaItemInstance* It = CellItem(E.Container, E.Cell);
		if (!It) { continue; }
		const int32 R = Row;
		if (R >= kMaxLots) { break; }
		RowEntry[R] = i;
		if (It->Amount < E.Qty)
		{
			E.Qty = It->Amount;
			if (E.Qty <= 0) { E.Qty = 1; }
		}
		UTextBlock* IconTxt = SellRows[R].IconText;
		const FAsdaItemRecord* Rec = IT->GetItem(It->ItemCode);
		auto Label = [IconTxt, It, &E](bool bCountColor)
		{
			if (E.Qty > 1)
			{
				if (bCountColor) { SetTxtColor(IconTxt, kWhite); }
				SetTxt(IconTxt, FString::Printf(TEXT("%d"), E.Qty));
			}
			if (It->GemGrade != 0)   // `*(BYTE*)(slot + 24)` → «+%d%%» жёлтым
			{
				SetTxtColor(IconTxt, kYellow);
				SetTxt(IconTxt, FString::Printf(TEXT("+%d%%"), (int32)(int16)It->EnchantPercent));
			}
		};
		if (Rec)
		{
			if (Rec->FlagWord176) { E.Qty = It->Amount; }   // нештучный — всегда весь запас
			Label(true);
			++Row;
		}
		FAsdaSpecialItemlistRow Sp;
		const bool bSp = IT->GetSpecialItem(It->ItemCode, Sp);
		if (bSp) { Label(true); ++Row; }
		FAsdaSowellRow Sw;
		if (IT->IsSowelCode(It->ItemCode) && IT->GetSowell(It->ItemCode, Sw)) { ++Row; }
		SetEdit(SellRows[R].Price, AsdaUi::FormatThousands(E.UnitPrice));
		SetEdit(SellRows[R].Qty, AsdaUi::FormatThousands(E.Qty));
		E.Total = (int64)E.UnitPrice * E.Qty;
		if (Rec && (Rec->FlagWord176 || Rec->CategoryFlag == 3)) { E.Total = E.UnitPrice; }
		if (bSp) { E.Total = E.UnitPrice; }
		if (!bFix) { E.Total = (int64)(int32)(uint32)(uint64)E.Total; }   // оригинал: итог в int32
		SetTxt(SellRows[R].Total, Thousands64(E.Total));
		Sum += E.Total;
		if (!bFix)
		{
			// К-6: отрицательный итог (переполнение i32) → 6305 и обрыв; К-5: сумма > 2,1 млрд → итог строки 0 и обрыв.
			if (E.Total < 0) { SendClose6305(TEXT("К-6: отрицательный итог строки (как оригинал)")); break; }
			if ((double)Sum > 2100000000.0) { E.Total = 0; break; }
		}
		if (Row >= kMaxLots) { break; }
	}
	SetTxt(TxtSum, Thousands64(Sum));   // 22511
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	SetTxt(TxtGold, AsdaUi::FormatThousands(Inv ? Inv->GetGold() : 0));   // 22513
	if (SellIcons) { SellIcons->Refresh(); }
	// Кэш полей — чтобы опрос в Tick не принял собственную перерисовку за ввод игрока.
	for (int32 R = 0; R < kMaxLots; ++R)
	{
		EditCache[R]            = SellRows[R].Price ? SellRows[R].Price->GetText().ToString() : FString();
		EditCache[R + kMaxLots] = SellRows[R].Qty ? SellRows[R].Qty->GetText().ToString() : FString();
	}
	bNameGuard = false;
}

// ═════════════════════════════ ОКНО 22000: ДЕЙСТВИЯ ИГРОКА ════════════════════════════════════
void UAsdaSelfStoreSubsystem::OnGridCellClicked(int32 Cell, bool bRightButton)
{
	// `sellWindowOnSlotClick@0xDCF860` (0xB → a3 = 0, 0xE → a3 = 1).
	if (!bSellOpen || !GridRef.IsValidIndex(Cell)) { return; }
	const FIntPoint Ref = GridRef[Cell];
	const FAsdaItemInstance* It = Ref.X >= 0 ? CellItem(Ref.X, Ref.Y) : nullptr;
	if (!It || It->ItemCode <= 0) { return; }
	if (IsListed(Ref.X, Ref.Y, It->ItemCode)) { return; }   // уже выставлен — молча
	PendingItem = FIntPoint(-1, -1);   // `m_pPendingItem = 0`
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(It->ItemCode) : nullptr;
	if (!Rec || Rec->FlagWord176 || !Rec->TradableFlag || It->Amount <= 1 || bRightButton)
	{
		AddItemToSell(Ref.X, Ref.Y, 0);   // весь стек
		return;
	}
	// Стопка > 1 торгуемого штучного — нумпад режим 7 (`uiOpenContextPopupWithHandlers(g_pNumPadDialog, стек, 7, 0)`),
	//   OK → `dlgApplyQuantityOkHandler` → `sub_DCF800` = добавить с количеством. OK эталона проверяет только `> 0`,
	//   общая точка `ShowNumPad` — 1..стек. ★ Ревизия Л1 (МЕЛК-1): «К-13» — НЕ дефект: поле нумпада режется до максимума после
	//   ЛЮБОГО изменения (`uiClampQuantityToMax@0xDA6410`, ui_core.c:62117), так что поведение совпадает с оригиналом.
	PendingItem = Ref;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	TWeakObjectPtr<UAsdaSelfStoreSubsystem> WeakThis(this);
	WM->ShowNumPad(It->Amount, [WeakThis](bool bOk, int32 Qty)
	{
		UAsdaSelfStoreSubsystem* S = WeakThis.Get();
		if (!S) { return; }
		const FIntPoint P = S->PendingItem;
		S->JournalAdd(FString::Printf(TEXT("нумпад 7: %s, кол-во %d"), bOk ? TEXT("OK") : TEXT("отмена"), Qty));
		if (bOk && Qty > 0 && P.X >= 0 && S->bSellOpen) { S->AddItemToSell(P.X, P.Y, Qty); }
	});
	JournalAdd(FString::Printf(TEXT("нумпад режим 7: код %d, максимум %d"), It->ItemCode, It->Amount));
}

void UAsdaSelfStoreSubsystem::OnSellIconClicked(int32 Row)
{
	// 0xB по 22308+20N → `cSellWindow_onRemoveButtonClick` → снять с продажи.
	const int32 E = RowEntry.IsValidIndex(Row) ? RowEntry[Row] : -1;
	if (!SellList.IsValidIndex(E)) { return; }
	JournalAdd(FString::Printf(TEXT("снято со строки %d"), Row));
	RemoveFromList(SellList[E].Container, SellList[E].Cell);
}

void UAsdaSelfStoreSubsystem::OnPriceFieldClicked(int32 Row)
{
	// 0xB по 22301+20N → `sellWnd_ClearSlotQtyByButton@0xDD15C0` (МИСНЕЙМ): цена строки := 0. [ОРИГИНАЛ]
	const int32 E = RowEntry.IsValidIndex(Row) ? RowEntry[Row] : -1;
	if (!SellList.IsValidIndex(E)) { return; }
	SellList[E].UnitPrice = 0;
	RefreshSellList();
	JournalAdd(FString::Printf(TEXT("клик по цене строки %d — цена 0 (оригинал)"), Row));
}

void UAsdaSelfStoreSubsystem::OnRowButton(int32 Row, int32 Kind)
{
	// `sellWnd_OnPriceButton@0xDD0E30` виды 1…4: шаг цены 5 % от БАЗЫ (`5 × (база/100)`, минимум 1), кол-во ±1.
	const int32 EIdx = RowEntry.IsValidIndex(Row) ? RowEntry[Row] : -1;
	if (!SellList.IsValidIndex(EIdx)) { return; }
	FSellEntry& E = SellList[EIdx];
	int32 Step = 5 * (E.BasePrice / 100);
	if (Step <= 0) { Step = 1; }
	if (Kind == 1) { E.UnitPrice = (int32)FMath::Min<int64>((int64)E.UnitPrice + Step, kPriceCap); }   // [ПОРТ] потолок P43
	if (Kind == 2) { E.UnitPrice -= Step; }
	if (Kind == 3) { ++E.Qty; }
	if (Kind == 4) { --E.Qty; }
	// Общий хвост: цена ≤ 0 → 0; кол-во < 1 → 1; спец-предмет — весь стек; не больше [5]; [5] == 0 ⇒ 1.
	const FAsdaItemInstance* It = CellItem(E.Container, E.Cell);
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	FAsdaSpecialItemlistRow Sp;
	if (E.UnitPrice <= 0) { E.UnitPrice = 0; }
	if (E.Qty < 1) { E.Qty = 1; }
	if (It && IT && IT->GetSpecialItem(It->ItemCode, Sp)) { E.Qty = It->Amount; }
	if (E.Qty > E.MaxQty) { E.Qty = E.MaxQty; }
	if (E.MaxQty == 0) { E.Qty = 1; }
	RefreshSellList();
}

void UAsdaSelfStoreSubsystem::OnRowEditText(int32 Row, bool bPrice)
{
	// `sellWnd_OnPriceButton` виды 5 (цена из поля 22301) и 6 (кол-во из поля 22304, не больше стопки): `atoi` без запятых.
	const int32 EIdx = RowEntry.IsValidIndex(Row) ? RowEntry[Row] : -1;
	if (!SellList.IsValidIndex(EIdx)) { return; }
	FSellEntry& E = SellList[EIdx];
	const FAsdaItemInstance* It = CellItem(E.Container, E.Cell);
	if (bPrice)
	{
		const FString Txt = SellRows[Row].Price ? SellRows[Row].Price->GetText().ToString() : FString();
		const int64 V = AtoiNoCommas(Txt);
		E.UnitPrice = (int32)FMath::Clamp<int64>(V, (int64)MIN_int32, (int64)kPriceCap);   // [ПОРТ] потолок ввода P43 — 1 млрд
	}
	else
	{
		const FString Txt = SellRows[Row].Qty ? SellRows[Row].Qty->GetText().ToString() : FString();
		int64 V = AtoiNoCommas(Txt);
		if (It && V > It->Amount) { V = It->Amount; }
		E.Qty = (int32)FMath::Clamp<int64>(V, (int64)MIN_int32, (int64)MAX_int32);
	}
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	FAsdaSpecialItemlistRow Sp;
	if (E.UnitPrice <= 0) { E.UnitPrice = 0; }
	if (E.Qty < 1) { E.Qty = 1; }
	if (It && IT && IT->GetSpecialItem(It->ItemCode, Sp)) { E.Qty = It->Amount; }
	if (E.Qty > E.MaxQty) { E.Qty = E.MaxQty; }
	if (E.MaxQty == 0) { E.Qty = 1; }
	RefreshSellList();
}

void UAsdaSelfStoreSubsystem::OnNameSaveClicked()
{
	// 66561 по 22012 (ui_core.c:65003-65016): текст поля (≤ 128); пусто — ничего; мат → msgbox 9005 + очистка поля;
	//   иначе `+0x308 = текст` и заголовок витрины `sub_93A138(CShopDlg, текст)`.
	if (!bSellOpen || !EdName) { return; }
	const FString Txt = EdName->GetText().ToString().Left(127);
	if (Txt.IsEmpty()) { return; }
	const UAsdaChatWindowSubsystem* Chat = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr;
	if (Chat && Chat->IsCursed(Txt))
	{
		MsgBoxLoc(L::kCurse);
		SetEdit(EdName, FString());
		NameCache.Reset();
		JournalAdd(TEXT("22012: мат в названии — 9005, поле очищено"));
		return;
	}
	StoreTitle = Txt;
	if (bShopOpen) { RefreshShowcaseList(); }
	JournalAdd(FString::Printf(TEXT("22012: название «%s»"), *StoreTitle));
}

void UAsdaSelfStoreSubsystem::OnNameFieldClicked()
{
	// 0xB по 22011: `CControl_SetText_12F9850(22011, "")` — поле очищается. [ОРИГИНАЛ]
	SetEdit(EdName, FString());
	NameCache.Reset();
}

void UAsdaSelfStoreSubsystem::OnOkClicked()
{
	// 66561 по 22500 (ui_core.c:65018-65026): пустой список → msgbox 107085; иначе перерисовка и `storeOpenSendRequest`.
	if (!bSellOpen) { return; }
	if (SellList.Num() == 0)
	{
		MsgBoxLoc(L::kNothing);
		JournalAdd(TEXT("22500: список пуст — 107085"));
		return;
	}
	RefreshSellList();
	StoreOpenSendRequest();
}

void UAsdaSelfStoreSubsystem::StoreOpenSendRequest()
{
	// `storeOpenSendRequest@0xDD3AA0` (ui_core.c:65091-65291).
	UGameInstance* GI = GetGameInstance();
	const UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
	if (Dlg && Dlg->IsDialogOpen()) { JournalAdd(TEXT("6302 не отправлен: открыт разговор с NPC (g_bInventoryLocked) — молча")); return; }
	if (!bCanSendOpen) { JournalAdd(TEXT("6302 не отправлен: одноразовый флаг +0x388 = 0 (ждём 6303)")); return; }
	const bool bFixK16 = CVarFixK16.GetValueOnGameThread() != 0;
	if (!bFixK16) { bCanSendOpen = false; }   // оригинал: `shopDlg[904] = 0` ДО проверки названия
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	// Лоты: код/контейнер/слот — из живой записи; кол-во — [3], если [5] (размер стопки) ≠ 0, иначе 0; цена [2].
	TArray<int32> Ids, Slots, Qtys, Prices; TArray<uint8> Conts;
	for (const FSellEntry& E : SellList)
	{
		const FAsdaItemInstance* It = CellItem(E.Container, E.Cell);
		if (!It) { continue; }
		Ids.Add(It->ItemCode);
		Conts.Add(It->ContainerType);
		Slots.Add(It->SlotIndex);
		Qtys.Add(E.MaxQty ? E.Qty : 0);
		Prices.Add(E.UnitPrice);
	}
	// Название: текст 22011 (≤ 128) → `strFilterCharsKeepAfterNameChar(…, "<>")`. [ПОРТ] Строка UTF-16: второго байта
	//   двухбайтовой буквы нет, исключение «после буквы» недостижимо — фильтр убирает все '<' и '>' (как у питомцев).
	FString Name = EdName ? EdName->GetText().ToString().Left(127) : StoreTitle;
	Name.ReplaceInline(TEXT("<"), TEXT(""));
	Name.ReplaceInline(TEXT(">"), TEXT(""));
	if (Name.IsEmpty())
	{
		MsgBoxLoc(L::kNoName);
		JournalAdd(TEXT("6302 не отправлен: пустое название — 107062"));
		return;
	}
	const UAsdaChatWindowSubsystem* Chat = GI->GetSubsystem<UAsdaChatWindowSubsystem>();
	if (Chat && Chat->IsCursed(Name))
	{
		MsgBoxLoc(L::kCurse);
		SetEdit(EdName, FString());
		NameCache.Reset();
		JournalAdd(TEXT("6302 не отправлен: мат в названии — 9005, поле очищено"));
		return;
	}
	StoreTitle = Name;   // `sprintf(this + 776, …)` и `sub_93A138(CShopDlg, …)`
	// ★ Ревизия Л1 (МЕЛК-3), [ПОРТ] при ключе К-5/К-6 = 1: сервер `CharSelfStoreOpen` (GameServer.c:100196-100214) считает
	//   для стековых лотов `кол-во × цена` в int32; переполнение (P43 G1 обнуляет его) ⇒ «Wrong Price» ⇒ 6303 код 0 = 107040.
	//   У оригинала такой лот до сервера не доходил (К-6: 6305 на подготовке). Порт лот с `кол-во × цена > 2^31−1` не шлёт и
	//   показывает тот же 107040, что дал бы сервер (своего текста у оригинала для этого случая нет — не выдумываем).
	if (CVarFixK5K6.GetValueOnGameThread() != 0)
	{
		for (int32 n = 0; n < Ids.Num(); ++n)
		{
			if ((int64)Qtys[n] * (int64)FMath::Max(Prices[n], 1) > (int64)MAX_int32)
			{
				MsgBoxLoc(L::kOpen0);
				JournalAdd(FString::Printf(TEXT("6302 не отправлен: лот %d кол-во %d × цена %d > 2^31−1 — 107040 ([ПОРТ], К-5/К-6)"), n, Qtys[n], Prices[n]));
				return;
			}
		}
	}
	const bool bSent = Net->SendSelfStoreOpen6302(AsdaSelf::GetCharId(Net), Ids, Conts, Slots, Qtys, Prices, Name);
	if (bSent) { bCanSendOpen = false; }
	JournalAdd(FString::Printf(TEXT("6302 %s: лотов %d, название «%s»"), bSent ? TEXT("отправлен") : TEXT("НЕ отправлен"), Ids.Num(), *Name));
}

// ═════════════════════════════ ВХОДЫ, 6300, БИЛЕТ, 6305 ═══════════════════════════════════════
bool UAsdaSelfStoreSubsystem::DispatchPrivateShop(const TCHAR* Source)
{
	// `UI_DispatchTargetContextMenu` кат. 2 case 7 (ui_core.c:32293-32296): `if (!sub_934E86(g_pSellWindow)) netSendPlayerInfo6300`.
	if (bSellOpen)
	{
		JournalAdd(FString::Printf(TEXT("%s: окно подготовки уже открыто — молча"), Source));
		return false;
	}
	return SendReady6300(Source);
}

bool UAsdaSelfStoreSubsystem::SendReady6300(const TCHAR* Source)
{
	// `netSendPlayerInfo6300@0xDD45C0` (network.c:26721-26837).
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return false; }
	// Молчаливый гейт: элитный магазин (`helper_getField2075(g_pShopWnd)`), окно ремесла / диалог количества ремесла.
	//   ★ B §2.1 зовёт первое условие «NPC-магазином»; порт (ремесло, гейт 4; диалог NPC 30035) доказал, что поле 2075
	//   `g_pShopWnd` — ЭЛИТНЫЙ магазин 6000 (`UAsdaPremiumShopSubsystem`).
	if (const UAsdaPremiumShopSubsystem* PS = GI->GetSubsystem<UAsdaPremiumShopSubsystem>())
	{
		if (PS->IsOpen()) { JournalAdd(FString::Printf(TEXT("%s: 6300 молча — открыт элитный магазин 6000"), Source)); return false; }
	}
	if (UAsdaCraftSubsystem::IsAnyCraftWindowOpen(GI)) { JournalAdd(FString::Printf(TEXT("%s: 6300 молча — открыто окно ремесла"), Source)); return false; }
	// ★★★ ПАЧКА 20, жалоба 10 — [ИСПРАВЛЕНИЕ]: пока открыт рынок 13000, личная лавка не готовится (молча, как гейты выше).
	//   Ключ `asda.Exchange.ExclusiveWindows` 0 — как в оригинале (гейта нет).
	if (UAsdaExchangeSubsystem::IsMarketWindowOpen(GI)) { JournalAdd(FString::Printf(TEXT("%s: 6300 молча — открыт рынок 13000 (пачка 20)"), Source)); return false; }
	if (AsdaSelf::GetLevel(GI) < kMinLevel)
	{
		MsgBoxLoc(L::kLowLevel);   // 107080 — msgbox
		JournalAdd(FString::Printf(TEXT("%s: уровень %d < 10 — 107080"), Source, AsdaSelf::GetLevel(GI)));
		return false;
	}
	UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
	if (!Net) { return false; }
	// `g_CachedLocalPlayerSerial = userIdx (charId)`, `g_CachedSelectedUserIdx = Target_GetSelectedUserIdx(LocalPlayer)` —
	//   поля `CShopDlg+0x274/+0x278` («хозяин» = я): от них потом уйдёт 6305.
	HostCharId = AsdaSelf::GetCharId(Net);
	HostUserNum = AsdaSelf::GetUserIdxOr(GI, 0);
	const bool bSent = Net->SendSelfStoreReady6300(HostCharId, bPremium, SelfName(), Ticket45);
	JournalAdd(FString::Printf(TEXT("%s: 6300 %s (charId %d userNum %d премиум %d)"), Source,
		bSent ? TEXT("отправлен") : TEXT("НЕ отправлен"), HostCharId, HostUserNum, bPremium ? 1 : 0));
	return bSent;
}

void UAsdaSelfStoreSubsystem::OnTicketUsed(const uint8* Rec57)
{
	// `netRecvItemBuffApply` вложенный case 64 (network.c:36570-36577): запись билета → `+0x38A` (код, контейнер, слот поверх
	//   сброшенной записи 45 б), `+0x389 = 1`; `if (g_bSellWindowActive == 1) 6305`; `netSendPlayerInfo6300`.
	if (!Rec57) { return; }
	Ticket45 = MakeResetLot45();
	AsdaBytes::WrI32(Ticket45.GetData() + 0, AsdaBytes::RdI32(Rec57 + 0));
	Ticket45[4] = Rec57[4];
	AsdaBytes::WrI16(Ticket45.GetData() + 5, AsdaBytes::RdI16(Rec57 + 5));
	bPremium = true;
	JournalAdd(FString::Printf(TEXT("билет премиум-лавки: код %d к=%d слот %d"), AsdaBytes::RdI32(Rec57), (int32)Rec57[4], AsdaBytes::RdI16(Rec57 + 5)));
	if (bReopen) { SendClose6305(TEXT("билет при переоткрытии (g_bSellWindowActive)")); }
	SendReady6300(TEXT("билет премиум-лавки (5451 подтип 65)"));
}

void UAsdaSelfStoreSubsystem::SendClose6305(const TCHAR* Why)
{
	// `antiCheat_SendPositionReport@0xDD4DA0` (МИСНЕЙМ): @0 charId хозяина, @4 userNum хозяина (кэш 6300/6308).
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	const bool bSent = Net->SendSelfStoreClose6305(HostCharId, HostUserNum);
	JournalAdd(FString::Printf(TEXT("6305 %s (%s): хозяин %d/%d"), bSent ? TEXT("отправлен") : TEXT("НЕ отправлен"), Why, HostCharId, HostUserNum));
}

// ═════════════════════════════ ПРИЁМ ══════════════════════════════════════════════════════════
void UAsdaSelfStoreSubsystem::OnPacket(int32 Op, const TArray<uint8>& Body)
{
	// Колбэк сети — только копим (закон `no-reentrant-net-callbacks`): разбор шлёт 6305 и правит инвентарь.
	FPending P; P.Op = Op; P.Body = Body;
	PendingPackets.Push(MoveTemp(P));
}

void UAsdaSelfStoreSubsystem::PumpPending()
{
	// ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива.
	TArray<FPending> Batch;
	if (!PendingPackets.Drain(Batch)) { return; }
	for (const FPending& P : Batch)
	{
		switch (P.Op)
		{
		case 6301: HandleReadyResult6301(P.Body); break;
		case 6303: HandleOpenResult6303(P.Body);  break;
		case 6306: HandleCloseResult6306(P.Body); break;
		case 6308: HandleVisitOpen6308(P.Body); break;
		case 6310: HandleBuyResult6310(P.Body); break;
		case 6311: HandleSold6311(P.Body); break;
		case 6312: HandleShowcaseUpdate6312(P.Body); break;
		case 6315: HandleGuests6315(P.Body); break;
		case 6314: HandleChat6314(P.Body); break;   // волна Л4
		default: break;
		}
	}
}

void UAsdaSelfStoreSubsystem::HandleReadyResult6301(const TArray<uint8>& Body)
{
	// `itemEnhanceOnResult@0xDD4F80` (МИСНЕЙМ = ответ 6300; item_inventory.c:55077-55132). Коды сервера — C2 §3.1.
	if (Body.Num() < 1) { return; }
	const int32 Code = Body[0];
	JournalAdd(FString::Printf(TEXT("6301 код %d"), Code));
	// ★ Ревизия Л1 (СРЕДН-1 б): переоткрытие не состоялось (6300 отвергнут) — снять пометку; если лавка уже закрыта
	//   (витрины хозяина нет — 6306/1 прошёл) и действует исправление К-3, снять и премиум: у оригинала его к этому моменту
	//   уже обнулил 6306 (`+0x389 = 0`), иначе следующее обычное открытие ушло бы премиумом со старым билетом.
	if (Code != 1 && bReopen)
	{
		bReopen = false;
		if (!IsOwnerShowcaseOpen() && CVarFixK3.GetValueOnGameThread() != 0 && bPremium)
		{
			bPremium = false;
			JournalAdd(TEXT("6301 ≠ 1 при переоткрытии: переоткрытие снято, премиум снят (К-3)"));
		}
		else
		{
			JournalAdd(TEXT("6301 ≠ 1 при переоткрытии: переоткрытие снято"));
		}
	}
	switch (Code)
	{
	case 0: MsgBoxLoc(L::kReady0); break;   // 107021 «Неудача»
	case 1: OpenSellWindow(); break;        // `sellWindow_Open`
	case 2: MsgBoxLoc(L::kReady2); break;   // 107022 «Ты в своей лавке» (storeStep ≠ 0)
	case 3: MsgBoxLoc(L::kReady3); break;   // 107023 «Ты в бою» (кулдаун боя)
	case 4: MsgBoxLoc(L::kReady4); break;   // 107024 «Ты труп» (сервер не шлёт)
	case 5: MsgBoxLoc(L::kReady5); break;   // 112500 (уровень < 10 по серверу)
	case 6: MsgBoxLoc(L::kReady6); break;   // 112501 (билет неверен)
	case 7: MsgBoxLoc(L::kReady7); break;   // 112502 (карта тип 4/6)
	default:
		// `sprintf(Buffer, "%d", код)` — msgbox с числом.
		AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::Trade, FString::Printf(TEXT("%d"), Code));
		break;
	}
}

void UAsdaSelfStoreSubsystem::HandleOpenResult6303(const TArray<uint8>& Body)
{
	// `pktHandleShopWindowOpen@0xDD3310` (shop_trade.c:16259-16368): `+0x388 = 1` всегда; [0] код, [1] число, лоты 45 б с +2.
	if (Body.Num() < 452) { return; }
	bCanSendOpen = true;
	const int32 Code = Body[0];
	JournalAdd(FString::Printf(TEXT("6303 код %d лотов %d"), Code, (int32)Body[1]));
	switch (Code)
	{
	case 0: MsgBoxLoc(L::kOpen0); return;   // 107040 «ошибка»
	case 1:
	{
		OwnerName = SelfName();   // `sub_92E761(CShopDlg, своё имя)` → `+0x1F1`
		Lots.Reset();             // `sub_920C0B` — витрина пуста
		const int32 N = FMath::Min<int32>(Body[1], kMaxLots);   // [ПОРТ] эталон читает до `count` (> 10 — за буфер 452 б)
		for (int32 i = 0; i < N; ++i)
		{
			const uint8* P = Body.GetData() + 2 + 45 * i;
			FLot Lot;
			Lot.ItemId = AsdaBytes::RdI32(P + 0);
			Lot.Price = AsdaBytes::RdI32(P + 13);
			Lot.Stock = AsdaBytes::RdI32(P + 7);
			if (Lot.Stock == 0) { Lot.Stock = 1; }   // `if (!qty) qty = 1`
			Lot.Container = P[4];
			Lot.Slot = AsdaBytes::RdI16(P + 5);
			Lot.SrvIdx = AsdaBytes::RdI16(P + 11);   // [ПОРТ] К-10 (волна Л2): индекс лота у сервера — для сверки 6311
			Lot.RawQty = AsdaBytes::RdI32(P + 7);
			// Заточка — из СВОЕГО инвентаря по контейнеру+слоту (контейнер 1: слот ≤ 29, контейнер 2: слот < 60).
			//   ★ [ПОРТ] 29.10.2026 (инвентарь ×2): сумка 60 ⇒ граница контейнера 1 — доступная ёмкость сумки.
			const bool bBag = (Lot.Container == 1 && Lot.Slot >= 0 && Lot.Slot < UAsdaInventorySubsystem::GetShellCapacity());
			const bool bSpend = (Lot.Container == 2 && Lot.Slot >= 0 && Lot.Slot < 60);
			if (bBag || bSpend)
			{
				if (const FAsdaItemInstance* It = CellItem(Lot.Container, Lot.Slot))
				{
					Lot.EnchantFlag = It->GemGrade;
					Lot.EnchantPct = (int32)(int16)It->EnchantPercent;
				}
			}
			if (Lots.Num() < kMaxLots) { Lots.Add(Lot); }   // `CShopDlg_AddBuyItem`: только если < 10
		}
		OpenShowcase();   // `shopDlgOpenSellWindow` (МИСНЕЙМ = открыть витрину)
		return;
	}
	case 2: case 3: case 4: case 5: case 6: case 7: case 8:
		MsgBoxLoc(L::kOpen2 + (Code - 2));   // 107042…107048
		return;
	default:
		JournalAdd(FString::Printf(TEXT("6303 код %d — у эталона ветки нет (молча)"), Code));
		return;
	}
}

void UAsdaSelfStoreSubsystem::HandleCloseResult6306(const TArray<uint8>& Body)
{
	// `sellWindowOnSellResult@0xDD5230` (shop_trade.c:16387-16460): `+0x389 = 0` ВСЕГДА; [0] код; при 1 — записи 57 б с +1.
	if (Body.Num() < 571) { return; }
	const bool bKeepPremium = CVarFixK3.GetValueOnGameThread() != 0 && bReopen;   // ★ К-3: «Изменить» премиум-лавки
	if (!bKeepPremium) { bPremium = false; }
	const int32 Code = Body[0];
	JournalAdd(FString::Printf(TEXT("6306 код %d (премиум %s)"), Code, bKeepPremium ? TEXT("сохранён — К-3") : TEXT("сброшен")));
	UGameInstance* GI = GetGameInstance();
	switch (Code)
	{
	case 0:
		MsgBoxLoc(L::kClose0);   // 107030 «Ошибка»
		return;
	case 1:
	{
		// `if (g_bSellWndOpen) sellWndHide; if (g_bShopDlgOpen) shopDlgShow` — глобалы-МИСНЕЙМЫ «инициализировано».
		if (bSellReady) { HideSellWindow(TEXT("6306 код 1")); }
		if (bShopReady) { HideShowcase(TEXT("6306 код 1")); }
		// Записи: `код > 0 && кол-во == 0` → очистить слот; `код > 0` → положить запись в свой слот (контейнер 2 — `memcpy`,
		//   контейнер 1 — `Inventory_SetBagSlot`). Итог — запись лежит в слоте: общая точка `ApplyExternalRecord`.
		//   ★ К-1: эталон — `i < 6` из 10 (сервер шлёт 10); исправление — все 10.
		const int32 NRec = (CVarFixK1.GetValueOnGameThread() != 0) ? 10 : 6;
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		int32 Applied = 0;
		for (int32 i = 0; i < NRec && Inv; ++i)
		{
			const uint8* Rec = Body.GetData() + 1 + 57 * i;
			if (AsdaBytes::RdI32(Rec) <= 0) { continue; }
			TArray<uint8> R57; R57.Append(Rec, 57);
			if (Inv->ApplyExternalRecord(R57)) { ++Applied; }
		}
		JournalAdd(FString::Printf(TEXT("6306 код 1: окна скрыты, записей применено %d (из %d просмотренных)"), Applied, NRec));
		return;
	}
	case 2:
		// ★ [ИСПРАВЛЕНИЕ] `asda.Trade.StoreStrayGuard` (ревизия Л2, СРЕДН-2): сервер шлёт 6306/2 по userNum гостя из guest[] хозяина;
		//   застрявший слот (аварийный выход `DisConnectUser` до S:111416) мог достаться другому игроку — эталон показал бы ему
		//   107031 без всякой лавки. Показываем, только если открыта витрина посетителя или окно подготовки.
		if (CVarStrayGuard.GetValueOnGameThread() != 0 && !IsVisitorShowcaseOpen() && !bSellOpen)
		{
			JournalAdd(TEXT("6306 код 2 без витрины посетителя и подготовки — молча (StoreStrayGuard)"));
			return;
		}
		MsgBoxLoc(L::kClose2);   // 107031 «Хозяин лавки закрыл ее»
		if (bSellReady) { HideSellWindow(TEXT("6306 код 2")); }
		if (bShopReady) { HideShowcase(TEXT("6306 код 2")); }
		// [ПОРТ] Волна Л2: у гостя незаконченный ввод количества (нумпад 8) к закрытой лавке не относится.
		PendingQtyLot = -1;
		return;
	default:
		// Код 3 («идёт покупка») у эталона ветки не имеет — молча (К-7): текста для него в таблицах нет, не выдумываем.
		JournalAdd(FString::Printf(TEXT("6306 код %d — у эталона ветки нет (К-7, молча)"), Code));
		return;
	}
}
