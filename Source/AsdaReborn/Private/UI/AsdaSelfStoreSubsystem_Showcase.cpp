// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSelfStoreSubsystem_Showcase.cpp — ★ R3-c 12.11.2026 (разрез AsdaSelfStoreSubsystem.cpp, дословный перенос): витрина покупателя: виджет, контролы, список, корзина, покупка, пакеты 6308–6315, журнал и чат лавки.
//   Общее для частей — в AsdaSelfStoreSubsystem_Private.h; шапка и реверс — в AsdaSelfStoreSubsystem.cpp.
#include "UI/AsdaSelfStoreSubsystem.h"
#include "AsdaWeightWire.h"   // ★ 29.10.2026 (вес ×2): AsdaWeightFromWire — вес персонажа по проводу u16

#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
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
#include "Components/ScrollBox.h"               // журнал 23402 (волна Л2)
#include "Components/ScrollBoxSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Login/AsdaLoginData.h"                // DecodeCp1251 — название/имя из 6308
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"              // `DefaultAsdaTrade.ini` — коды правила «цена лота» сервера
#include "AsdaSelfStoreSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSelfStoreSubsystem*.cpp

namespace {
	const TCHAR* kShopWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_StreetStall_Win.WBP_StreetStall_Win_C");
	const FName  kShopChatZone(TEXT("StreetStall_Win_Chat"));   // вторая зона 23000: блок чата 23400 лежит ниже рамки окна

	ASDA_DEV_CVAR(int32, CVarFixK4, TEXT("asda.Trade.StoreFixK4"), 1,
		TEXT("Лавка, дефект К-4: 1 = «Изменить» премиум-лавки повторно применяет БИЛЕТ (5450 по его ячейке, [ПОРТ] при переложенном "
		     "билете — по ячейке сумки с тем же кодом); 0 = НЕ оригинал: оригинал повторял бы кэш последнего пакета 5450, а кэша "
		     "пакета в порту нет — при 0 кнопка только взводит переоткрытие и пишет лог (5450 не уходит)."),
		ECVF_Default);

	// ── ключи волны Л2 (посетитель) ──
	ASDA_DEV_CVAR(int32, CVarFixK2, TEXT("asda.Trade.StoreFixK2"), 1,
		TEXT("Лавка, дефект К-2: 1 = 6312 убирает из корзины позиции по индексам ВСЕХ записей пакета; 0 = как оригинал — только по "
		     "индексу ПЕРВОЙ записи (`SHIWORD(tradeSlots[5])` не зависит от цикла, shop_trade.c:18506)."),
		ECVF_Default);
	ASDA_DEV_CVAR(int32, CVarFixK8, TEXT("asda.Trade.StoreFixK8"), 1,
		TEXT("Лавка, дефект К-8: 1 = 6311 у продавца уменьшает стопку в сумке на ПРОДАННОЕ кол-во (распродана — слот пуст); 0 = как "
		     "оригинал — при распродаже лота слот сумки чистится целиком, частичная продажа не видна до закрытия лавки."),
		ECVF_Default);
	ASDA_DEV_CVAR(int32, CVarFixK10, TEXT("asda.Trade.StoreFixK10"), 1,
		TEXT("Лавка, дефект К-10: 1 = индекс лота в 6309 и сверке 6311/6312 — позиция лота у сервера (порядок 6308 / поле +11 записи "
		     "6303); 0 = как оригинал — номер строки на экране (+36), который не растёт у предмета без записи в таблицах."),
		ECVF_Default);
	ASDA_DEV_CVAR(int32, CVarBuyGuard, TEXT("asda.Trade.StoreBuyGuard"), 1,
		TEXT("[ПОРТ] Лавка, покупка: 1 = 6309 не уходит, если стоимость позиции корзины ПО ПРАВИЛУ СЕРВЕРА (кол-во × цена только у "
		     "предмета item_list со штучным кол-вом, кроме «заточка 4 + тип 10»; у остальных — цена лота) или сумма корзины больше "
		     "2^31−1 (сервер P43 обнулил бы произведение и ответил 6310 код 0) — msgbox 107050, тот же текст, что дал бы сервер; "
		     "0 = как оригинал."),
		ECVF_Default);

	// Подстановка аргументов в loc-строку с printf-метками (`sprintf(buf, loc, …)` эталона): каждая `%s`/`%d` по порядку
	//   заменяется очередным аргументом (строки 107082/107087/107088 — только эти две метки).
	FString SubstPrintf(const FString& Fmt, std::initializer_list<FString> Args)
	{
		FString Out;
		const FString* It = Args.begin();
		for (int32 i = 0; i < Fmt.Len(); ++i)
		{
			if (Fmt[i] == TEXT('%') && i + 1 < Fmt.Len() && (Fmt[i + 1] == TEXT('s') || Fmt[i + 1] == TEXT('d')) && It != Args.end())
			{
				Out += *It++;
				++i;
				continue;
			}
			Out.AppendChar(Fmt[i]);
		}
		return Out;
	}
}

// ═════════════════════════════ ВИТРИНА 23000 (РЕЖИМ ХОЗЯИНА) ══════════════════════════════════
bool UAsdaSelfStoreSubsystem::EnsureShopWidget()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kShopId, kShopWbp, kWinZ).Ensure(GetGameInstance(), ShopWidget, bShopReady,
		[this](UAsdaWindowSubsystem&) { ResetShopWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupShopControls();
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kShopId, this, ShopWidget, bShopOpen, TEXT("StreetStall_Win_0_%d"), 9);
			// Блок чата 23400 (438×121) лежит ПОД рамкой окна (0,427): своя зона, иначе клик по нему уходил бы в мир.
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kShopChatZone, this, ShopWidget, bShopOpen, TEXT("StreetStall_ChatBox_138_%d"), 9);
		},
		[]() { UE_LOG(LogAsdaSelfStore, Error, TEXT("[лавка] WBP_StreetStall_Win не загрузился (%s)"), kShopWbp); });
	return FAsdaJuiWindowHandle::Succeeded(R) && bShopReady;
}

void UAsdaSelfStoreSubsystem::ResetShopWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kShopId);
		WM->UnregisterUiZone(kShopChatZone);
		if (ShopWidget) { WM->CloseWindow(kShopId); }
	}
	ShopWidget = nullptr;
	bShopReady = false;
	if (bShopOpen)
	{
		bShopOpen = false;
		bSpecialShop = false;
		if (UAsdaTradeStateSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTradeStateSubsystem>() : nullptr)
		{
			TS->SetShopDlgOpen(false, TEXT("сброс виджета 23000"));
		}
	}
	BtnShopX = BtnMain = BtnShopCancel = nullptr;
	TxtShopTitle = TxtCartLbl = TxtTotalCap = TxtTotal = TxtMoneyCap = TxtMoney = TxtMain = TxtShopCancel = TxtChatTitle = nullptr;
	SelRect = ListHdr = CartSel = nullptr;
	ShopCols.Reset(); LotRows.Reset(); CartImg.Reset(); CartTxt.Reset();
	LotIcons = nullptr;
	Found23000 = 0;
	BtnTrash = BtnLogHome = nullptr;   // волна Л2
	LogList = nullptr;
	LogFontSrc = nullptr;
	EdChat = nullptr;                  // волна Л4
	ChatRelays.Reset();
	FoundVisitor = 0;
}

void UAsdaSelfStoreSubsystem::SetupShopControls()
{
	bShopReady = false;
	if (!ShopWidget || !ShopWidget->WidgetTree) { return; }
	ShopCols.SetNum(5); LotRows.SetNum(kMaxLots); CartImg.SetNum(10); CartTxt.SetNum(10);
	TArray<FName> LotNames; LotNames.SetNum(kMaxLots);

	// `CShopDlg_InitControls@0xDD6610` (shop_trade.c:16659-16758).
	TArray<UWidget*> All;
	ShopWidget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* B = Cast<UButton>(W))
		{
			if (N == TEXT("Stall_ClsBtn0")) { BtnShopX = B; continue; }        // 23005
			if (N == TEXT("Stall_EditBtn")) { BtnMain = B; continue; }         // 23305
			if (N == TEXT("Stall_ClsBtn1")) { BtnShopCancel = B; continue; }   // 23306
			if (N == TEXT("Stall_Pur_TrashBtn")) { BtnTrash = B; continue; }   // 23243 урна (`btnClose`/`lblBuyCap` — оба 23243)
			if (N == TEXT("Stall_Chat_HomeBtn")) { BtnLogHome = B; continue; } // 23404 «в конец» журнала
			continue;
		}
		if (UEditableText* E = Cast<UEditableText>(W))
		{
			if (N == TEXT("Stall_Chat_Input")) { EdChat = E; }                 // 23403 (волна Л4; `self->editMemo`, shop_trade.c:16730)
			continue;
		}
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			if (N == TEXT("StreetStall_Title_Text"))   { TxtShopTitle = T; continue; }   // 23006
			if (N == TEXT("Stall_SubTitle_Text"))      { ShopCols[0] = T; continue; }    // 23101
			if (N == TEXT("Stall_SubTxt0"))            { ShopCols[1] = T; continue; }    // 23102
			if (N == TEXT("Stall_SubTxt1"))            { ShopCols[2] = T; continue; }    // 23103
			if (N == TEXT("Stall_SubTxt2"))            { ShopCols[3] = T; continue; }    // 23104
			if (N == TEXT("Stall_SubTxt3"))            { ShopCols[4] = T; continue; }    // 23105
			if (N == TEXT("Stall_PurItem_List_Text"))  { TxtCartLbl = T; continue; }     // 23242
			if (N == TEXT("Stall_Txt0"))               { TxtTotalCap = T; continue; }    // 23301
			if (N == TEXT("Stall_Price0"))             { TxtTotal = T; continue; }       // 23302
			if (N == TEXT("Stall_Txt1"))               { TxtMoneyCap = T; continue; }    // 23303
			if (N == TEXT("Stall_Price1"))             { TxtMoney = T; continue; }       // 23304
			if (N == TEXT("Stall_EditBtn_Text"))       { TxtMain = T; continue; }        // подпись 23305
			if (N == TEXT("Stall_ClsBtn1_Text"))       { TxtShopCancel = T; continue; }  // подпись 23306
			if (N == TEXT("Stall_ChatBox_Title"))      { TxtChatTitle = T; continue; }   // 23401
			for (int32 R = 0; R < kMaxLots; ++R)
			{
				if (N == FString::Printf(TEXT("Stall_SellIcon%02d"), R))   { LotRows[R].IconText = T; }   // 23121+10R
				if (N == FString::Printf(TEXT("Stall_SellIQnty%02d"), R))  { LotRows[R].Price = T; }      // 23122+10R
				if (N == FString::Printf(TEXT("Stall_SellIPrice%02d"), R)) { LotRows[R].Total = T; }      // 23123+10R
			}
			for (int32 k = 0; k < 10; ++k)
			{
				if (N == FString::Printf(TEXT("Stall_PurItem%02d"), k)) { CartTxt[k] = T; }             // 23250+k
			}
			continue;
		}
		if (N == TEXT("Stall_PurItemBase_grp")) { ListHdr = W; continue; }   // 23240 — группа корзины целиком
		FString Idc; int32 JuiIdx = 0, Plane = 0;
		if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, Plane) || Plane != 0) { continue; }
		if (Idc == TEXT("Stall_SellSel")) { SelRect = W; continue; }   // 23230
		if (Idc == TEXT("Stall_PurSel"))  { CartSel = W; continue; }   // 23241
		for (int32 R = 0; R < kMaxLots; ++R)
		{
			if (Idc == FString::Printf(TEXT("Stall_SellIcon%02d"), R)) { LotRows[R].Icon = Cast<UImage>(W); LotNames[R] = FName(*N); }
		}
		for (int32 k = 0; k < 10; ++k)
		{
			if (Idc == FString::Printf(TEXT("Stall_PurItem%02d"), k)) { CartImg[k] = Cast<UImage>(W); }
		}
	}

	const UAsdaNetSubsystem* NetG = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	LotIcons = NewObject<UAsdaIconCellGrid>(this);
	LotIcons->SetCellSizePx(32.f);
	LotIcons->SetClickOverlayEnabled(false);
	LotIcons->bClearEmptyCells = true;
	LotIcons->SetupNamed(ShopWidget, LotNames, /*bMale*/ !NetG || NetG->ResolveSelfGender() != 2, [this](int32 R) -> int32
	{
		for (const FLot& Lot : Lots) { if (Lot.Row == R) { return FMath::Max(0, Lot.ItemId); } }
		return 0;
	}, [](int32) {});

	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void()>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, [F = MoveTemp(Fn)](int32) { F(); }, Relays);   // ★ U20: реле клика — общая точка
	};
	// `CShopDlg_OnCommand@0xDD8B70` ветка 66561: 23005 и 23306 → 6305 (и хозяин, и гость); 23305 — «Изменить» / покупка.
	Bind(BtnShopX,      23005, [this]() { SendClose6305(TEXT("крестик 23005")); });
	Bind(BtnShopCancel, 23306, [this]() { SendClose6305(TEXT("«Отмена» 23306")); });
	Bind(BtnMain,       23305, [this]() { OnShowcaseMainButton(); });
	Bind(BtnTrash,      23243, [this]() { OnTrashClicked(); });      // волна Л2: `shopCartRemoveItem(pSelectedSlotItem)`
	Bind(BtnLogHome,    23404, [this]() { OnLogHomeClicked(); });    // волна Л2: полоса журнала в конец
	LogFontSrc = TxtChatTitle;   // 23401 — тот же текстовый скин блока чата, что у строк журнала
	EnsureLogList();
	// ★ Волна Л4: поле ввода чата 23403 (`.JUI` `EditBox`, `LimitText = "60"`). [ОРИГИНАЛ] Win32-поле эталона режет набор по
	//   EM_LIMITTEXT 60 байт (cp1251: символ = байт) и шлёт окну 67587 по Enter → `CShopDlg_OnCommand` (shop_trade.c:17541).
	//   Порт: жёсткий стоп набора на 60 (как диалог ввода 1580, `UAsdaTextLimiter`) и подтверждение ТОЛЬКО по Enter (потеря фокуса и
	//   Esc — не отправка). Поле лежит целиком под ручкой `StreetStall_ChatBox_138_DRAG` (реестр П8-12) — клик по нему фокусирует
	//   поле и окно не тащит (проверка — самотест Л4 путём игрока).
	ChatRelays.Reset();
	if (EdChat)
	{
		UAsdaTextLimiter* Lim = NewObject<UAsdaTextLimiter>(this);
		Lim->Callback = [this](const FText& NewText)
		{
			if (!EdChat) { return; }
			const FString Str = NewText.ToString();
			// ★ [ПОРТ] 08.10.2026: стоп набора по БАЙТАМ провода (EM_LIMITTEXT 60 — байты; поле 6313 +26 — 79 б).
			const FString Cut = FAsdaLoginData::TruncateToWireLimit(Str, kChatInputLimit, 79);
			if (Cut != Str) { EdChat->SetText(FText::FromString(Cut)); }
		};
		EdChat->OnTextChanged.AddDynamic(Lim, &UAsdaTextLimiter::OnTextChanged);
		UAsdaTextLimiter* Com = NewObject<UAsdaTextLimiter>(this);
		Com->CommitCallback = [this](const FText&, ETextCommit::Type Method)
		{
			if (Method == ETextCommit::OnEnter) { OnChatInputEnter(); }   // 67587 — только Enter
		};
		EdChat->OnTextCommitted.AddDynamic(Com, &UAsdaTextLimiter::OnTextCommitted);
		AsdaUi::AnchorRelay(ChatRelays, Lim, EdChat);   // ★ 31.10.2026: ограничитель — тот же якорь с прочисткой
		AsdaUi::AnchorRelay(ChatRelays, Com, EdChat);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}

	// Подписи init: 23006 пусто; 23101 107000; 23102/23104 107001; 23103/23105 107002; 23242 107003; 23301 107004;
	//   23303 107005; 23305 107006; 23306 107007; 23401 пусто (К-15: 107008 не используется). Корзина 23256…23259 скрыта в ассете.
	SetTxt(TxtShopTitle, FString());
	static const int32 kColLoc[5] = { L::kColList, L::kColUnit, L::kColTotal, L::kColUnit, L::kColTotal };
	for (int32 i = 0; i < 5; ++i) { SetTxt(ShopCols[i], Loc(kColLoc[i])); }
	SetTxt(TxtCartLbl, Loc(L::kCartLbl));
	SetTxt(TxtTotalCap, Loc(L::kBuyTotal));
	SetTxt(TxtMoneyCap, Loc(L::kMoney));
	SetTxt(TxtMain, Loc(L::kBtnOk));
	SetTxt(TxtShopCancel, Loc(L::kBtnCancel));
	SetTxt(TxtChatTitle, FString());

	// Итог разбора: привязки L1 (корень, 23005/23006, 23101…23105, 30 полей товаров, 23230, 23240…23242, 23250…23259,
	//   23301…23306, 23401). ★ Волна Л2 добавила 23243 (урна), 23404 («в конец»), 23402 (журнал, строится кодом) — счёт
	//   `FoundVisitor`; поле ввода 23403 и отправка 6313 — волна Л4.
	int32 Found = ShopWidget ? 1 : 0;
	auto Has = [&Found](const UObject* O) { if (O) { ++Found; } };
	Has(BtnShopX); Has(TxtShopTitle);
	for (UTextBlock* T : ShopCols) { Has(T); }
	for (const FAsdaStallLotRowUi& R : LotRows) { if (R.Icon && R.IconText) { ++Found; } Has(R.Price); Has(R.Total); }
	Has(SelRect); Has(ListHdr); Has(CartSel); Has(TxtCartLbl);
	for (int32 k = 0; k < 10; ++k) { if (CartImg[k] && CartTxt[k]) { ++Found; } }
	Has(TxtTotalCap); Has(TxtTotal); Has(TxtMoneyCap); Has(TxtMoney); Has(BtnMain); Has(BtnShopCancel); Has(TxtChatTitle);
	Found23000 = Found;
	FoundVisitor = (BtnTrash ? 1 : 0) + (BtnLogHome ? 1 : 0) + (LogList ? 1 : 0);
	bShopReady = (TxtShopTitle != nullptr && BtnMain != nullptr && LotRows[0].Icon != nullptr && TxtTotal != nullptr);
	UE_LOG(LogAsdaSelfStore, Log, TEXT("[лавка] окно 23000 разобрано: привязок %d из 59 (режим хозяина) + %d из 3 (посетитель: 23243/23404/23402) + поле чата 23403=%d, готово=%d"),
		Found23000, FoundVisitor, EdChat ? 1 : 0, bShopReady ? 1 : 0);
}

void UAsdaSelfStoreSubsystem::OpenShowcase()
{
	// `shopDlgOpenSellWindow@0xDD7020` (shop_trade.c:16838-16873).
	if (!EnsureShopWidget()) { JournalAdd(TEXT("23000 не открыто: WBP не разобран")); return; }
	if (bShopOpen) { HideShowcase(TEXT("повторное открытие (тумблер shopDlgOpenSellWindow)")); return; }
	UGameInstance* GI = GetGameInstance();
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr) { if (Inv->IsWindowOpen()) { Inv->CloseWindow(); } }
	if (bSellOpen) { HideSellWindow(TEXT("открытие витрины (shopDlgOpenSellWindow)")); }
	bShopOpen = true;   // `+0x168 = 1`
	if (UAsdaTradeStateSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTradeStateSubsystem>() : nullptr) { TS->SetShopDlgOpen(true, TEXT("shopDlgOpenSellWindow (6303 код 1)")); }
	// `clearFieldAndReturn(g_pGameClient)`, `thunk_clearFlag35C4B94()`, курсор «nomal» — общие флаги клиента/курсора, к окну
	//   лавки отношения не имеют (в порту курсор ставит менеджер окон).
	// ★ Волна Л2: `listClearNodes(cart)`, `bAutoScrollLog = 1`, `containerClearAndFree(listLog)` (журнал 23402 пуст),
	//   `sub_92BBBA(this, 0)` = `set_oA4` — число гостей 0. Лоты новые ⇒ [ПОРТ] прежний выбор корзины (`pSelectedSlotItem`, у
	//   эталона — висячий указатель на освобождённый лот) снят.
	Cart.Reset();
	SelLot = -1;
	PendingQtyLot = -1;
	LotDbl.Reset();   // [ПОРТ] ревизия 10.10 МЕЛК-2: лоты новые (может быть другая лавка) — пара щелчков прежней витрины не живёт
	bLogAutoScroll = true;
	if (LogList) { LogList->ClearChildren(); }
	GuestCount = 0;
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kShopId, true);
		WM->BringWindowToFront(kShopId);
	}
	// `shopBuyDlgSetBuyModeTexts`: 23240 и 23301 видны, 23305 = 107006, 23301 = 107004, `bSpecialShop = 0`.
	AsdaUi::SetShownDecor(ListHdr, true);
	if (TxtTotalCap) { TxtTotalCap->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	SetTxt(TxtMain, Loc(L::kBtnOk));
	SetTxt(TxtTotalCap, Loc(L::kBuyTotal));
	bSpecialShop = false;
	// `if (LocalPlayer && pNpcChar == LocalPlayer->userIdx) shopSellDlgResetTexts`: 23240 скрыт, 23301 = 107009,
	//   23305 = 107060 «Изменить», `bSpecialShop = 1`.
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Net && HostCharId == AsdaSelf::GetCharId(Net))
	{
		AsdaUi::SetShownDecor(ListHdr, false);
		SetTxt(TxtTotalCap, Loc(L::kSellTotal));
		SetTxt(TxtMain, Loc(L::kEdit));
		bSpecialShop = true;
	}
	RefreshShowcaseTotals();
	RefreshShowcaseList();
	Mouse.Reset();
	JournalAdd(FString::Printf(TEXT("23000 открыто: хозяин=%d лотов %d название «%s»"), bSpecialShop ? 1 : 0, Lots.Num(), *StoreTitle));
	if (bSpecialShop) { SetPose(107, TEXT("витрина хозяина открыта (shopDlgOpenSellWindow)")); }
}

void UAsdaSelfStoreSubsystem::HideShowcase(const TCHAR* Why)
{
	// `shopDlgShow@0xDD7580` (МИСНЕЙМ = скрыть): корень скрыт, `+0x168 = 0`; хозяину — поза 109; `bSpecialShop = 0`.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kShopId, false);
	}
	const bool bWas = bShopOpen;
	bShopOpen = false;
	LotDbl.Reset();   // [ПОРТ] ревизия 10.10 МЕЛК-2: витрина закрыта — пара щелчков по лоту рвётся
	if (UAsdaTradeStateSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTradeStateSubsystem>() : nullptr)
	{
		TS->SetShopDlgOpen(false, Why);
	}
	if (TipKind == 2 || TipKind == 3) { TickTip(-1, -1); }
	const bool bOwner = bSpecialShop;
	bSpecialShop = false;
	if (bWas) { JournalAdd(FString::Printf(TEXT("23000 скрыто (%s)"), Why)); }
	if (bOwner) { SetPose(109, TEXT("витрина хозяина скрыта (shopDlgShow)")); }
}

void UAsdaSelfStoreSubsystem::RefreshShowcaseTotals()
{
	// `CShopSellDlg_RefreshTotalPrice@0xDD7600` (shop_trade.c:16924-17115): очистка 10 ячеек корзины (пустая текстура, текст,
	//   ссылка), 23241 скрыт; корзина по порядку: ячейка k = позиция, рамка 23241 — на ячейке выбранного (`pSelectedSlotItem`),
	//   иконка, кол-во > 1 белым, «+N%» жёлтым; сумма корзины → 23302 жёлтым при золоте ≥ сумме, иначе красным + msgbox 107056
	//   (на КАЖДОЙ перерисовке — К-14; повтор глушит очередь окон-сообщений по паре (0, −1), как `objList_ContainsPair`);
	//   золото 23304. У хозяина корзина пуста ⇒ 0 жёлтым. ★ Эталон пишет `cartItem+36 = номер ячейки корзины`, но следом
	//   всегда идёт `CShopDlg_RefreshItemList`, возвращающий +36 = строку витрины, — в порту поле строки не трогаем.
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UAsdaNetSubsystem* NetG = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bMale = !NetG || NetG->ResolveSelfGender() != 2;   // `g_ItemImgOptionVariant`
	for (int32 k = 0; k < 10; ++k)
	{
		SetTxt(CartTxt.IsValidIndex(k) ? CartTxt[k].Get() : nullptr, FString());
		if (CartImg.IsValidIndex(k) && CartImg[k]) { UAsdaIconCellGrid::ApplyItemIcon(this, CartImg[k], 0, bMale, 32.f, 32.f, /*bClearIfEmpty*/ true); }
	}
	CartCellLot.Init(-1, 10);   // `*(&field_C8_pad + 2 * i) = 0`
	AsdaUi::SetShownDecor(CartSel, false);
	const bool bWide = CVarFixK5K6.GetValueOnGameThread() != 0;   // [ПОРТ] К-5/К-6: сумма в int64 (0 — int32 как оригинал)
	int64 Total = 0;
	int32 Slot = 0;
	for (const int32 LIdx : Cart)
	{
		if (!Lots.IsValidIndex(LIdx) || Slot >= 10) { continue; }
		const FLot& Lot = Lots[LIdx];
		const int32 Cell = Slot;
		CartCellLot[Cell] = LIdx;   // `*(&field_C8_pad + 2 * slotIndex) = pItemSlot`
		UImage* Img = CartImg.IsValidIndex(Cell) ? CartImg[Cell].Get() : nullptr;
		UTextBlock* Txt = CartTxt.IsValidIndex(Cell) ? CartTxt[Cell].Get() : nullptr;
		if (SelLot == LIdx && Img && CartSel)
		{
			// `Class_GetVec2_74(ячейка)` → `CControl_SetField74_75(23241, x, y)` + показать: рамка и ячейки — дети одной группы
			//   (`Stall_PurItemBase_grp_1`), позиция слота ячейки = позиция рамки.
			UCanvasPanelSlot* From = AsdaUi::SlotOf(Img);
			UCanvasPanelSlot* To = AsdaUi::SlotOf(CartSel);
			if (From && To) { To->SetPosition(From->GetPosition()); }
			AsdaUi::SetShownDecor(CartSel, true);
		}
		auto Label = [Txt, &Lot](bool bCountColor)
		{
			if (Lot.CartQty > 1)
			{
				SetTxt(Txt, FString::Printf(TEXT("%d"), Lot.CartQty));
				if (bCountColor) { SetTxtColor(Txt, kWhite); }
			}
			if (Lot.EnchantFlag)
			{
				SetTxtColor(Txt, kYellow);
				SetTxt(Txt, FString::Printf(TEXT("+%d%%"), Lot.EnchantPct));
			}
		};
		const FAsdaItemRecord* Rec = IT ? IT->GetItem(Lot.ItemId) : nullptr;
		FAsdaSpecialItemlistRow Sp;
		const bool bSp = IT && IT->GetSpecialItem(Lot.ItemId, Sp);
		FAsdaSowellRow Sw;
		const bool bSw = IT && IT->IsSowelCode(Lot.ItemId) && IT->GetSowell(Lot.ItemId, Sw);
		if (Img && (Rec || bSp || bSw)) { UAsdaIconCellGrid::ApplyItemIcon(this, Img, Lot.ItemId, bMale, 32.f, 32.f, /*bClearIfEmpty*/ true); }
		if (Rec) { Label(true); ++Slot; }
		if (bSp) { Label(false); ++Slot; }
		if (bSw) { ++Slot; }
		// Цена позиции: `flagWord176` / `categoryFlag == 3` — цена за лот; прочие — кол-во × цена; спец-предмет — цена за лот.
		int64 Price = (int64)Lot.CartQty * Lot.Price;
		if (Rec && (Rec->FlagWord176 || Rec->CategoryFlag == 3)) { Price = Lot.Price; }
		if (bSp) { Price = Lot.Price; }
		if (!bWide) { Price = (int64)(int32)(uint32)(uint64)Price; }
		Total += Price;
		if (!bWide) { Total = (int64)(int32)(uint32)(uint64)Total; }
	}
	SetTxt(TxtTotal, Thousands64(Total));
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const int64 Gold = Inv ? Inv->GetGold() : 0;   // `getFieldIfPositive(g_pGameClient)`
	if (Gold >= Total)
	{
		SetTxtColor(TxtTotal, kYellow);   // −256
	}
	else
	{
		SetTxtColor(TxtTotal, FColor(0xFF, 0x00, 0x00, 0xFF));   // −65536
		MsgBoxLoc(L::kNoGold);                                  // 107056 «Не хватает золота»
	}
	SetTxt(TxtMoney, AsdaUi::FormatThousands(Inv ? Inv->GetGold() : 0));
}

void UAsdaSelfStoreSubsystem::RefreshShowcaseList()
{
	// `CShopDlg_RefreshItemList@0xDD7F80` (shop_trade.c:17119-17339).
	if (!bShopReady) { return; }
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	SetTxt(TxtShopTitle, StoreTitle);   // 23006 = `szNpcName` (+0x171, МИСНЕЙМ) = название лавки
	for (int32 R = 0; R < kMaxLots; ++R)
	{
		SetTxt(LotRows[R].IconText, FString());
		SetTxt(LotRows[R].Total, FString());
		SetTxt(LotRows[R].Price, FString());
		SetImgAlpha(LotRows[R].Icon, 1.f);
		SetTxtColor(LotRows[R].IconText, kWhite);
	}
	AsdaUi::SetShownDecor(SelRect, false);
	// ★ [ПОРТ] Ревизия 10.10.2026, МЕЛК-2: список лотов перерисован (6308/6303 — новые лоты, 6311/6312 — распроданное
	//   уходит, строки сдвигаются, 22012 — название). Ключ пары двойного щелчка — индекс в `Lots` по строке; после
	//   перерисовки под той же строкой может оказаться ДРУГОЙ лот, поэтому начатая пара рвётся. Перерисовка после
	//   укладки в корзину (`CartAddItem`/`CartRemoveItem`) пару не теряет — она к тому моменту уже сработала.
	LotDbl.Reset();
	int64 Total = 0;
	int32 Row = 0;
	for (int32 LotIdx = 0; LotIdx < Lots.Num(); ++LotIdx)
	{
		FLot& Lot = Lots[LotIdx];
		if (Row >= kMaxLots) { break; }
		Lot.Row = Row;
		const int32 R = Row;
		UTextBlock* IconTxt = LotRows[R].IconText;
		auto Label = [IconTxt, &Lot](bool bCountColor)
		{
			if (Lot.Stock > 1)
			{
				SetTxt(IconTxt, FString::Printf(TEXT("%d"), Lot.Stock));
				if (bCountColor) { SetTxtColor(IconTxt, kWhite); }
			}
			if (Lot.EnchantFlag)
			{
				SetTxtColor(IconTxt, kYellow);
				SetTxt(IconTxt, FString::Printf(TEXT("+%d%%"), Lot.EnchantPct));
			}
		};
		const FAsdaItemRecord* Rec = IT ? IT->GetItem(Lot.ItemId) : nullptr;
		if (Rec) { Label(true); ++Row; }
		FAsdaSpecialItemlistRow Sp;
		const bool bSp = IT && IT->GetSpecialItem(Lot.ItemId, Sp);
		if (bSp) { Label(false); ++Row; }
		FAsdaSowellRow Sw;
		if (IT && IT->IsSowelCode(Lot.ItemId) && IT->GetSowell(Lot.ItemId, Sw)) { ++Row; }
		if (Lot.Stock == -1) { SetImgAlpha(LotRows[R].Icon, kDimAlpha); SetTxtColor(IconTxt, kDimText); }   // распродан
		// Волна Л2: лот в корзине (`cartItem == pItem`) — тоже приглушён (альфа 0x7D, надпись 0x7DFFFFFF).
		if (Cart.Contains(LotIdx)) { SetImgAlpha(LotRows[R].Icon, kDimAlpha); SetTxtColor(IconTxt, kDimText); }
		// Итог строки — дословно эталон: у `categoryFlag == 3` присваивание перекрывается веткой `else` (мёртвое).
		// ★ Ревизия Л1 (МЕЛК-2): у эталона итог строки и сумма хозяина — int32 (цена до 1 млрд × остаток ≥ 3 даёт отрицательные
		//   числа). [ПОРТ] исправление К-5/К-6 — int64 под тем же ключом `asda.Trade.StoreFixK5K6`; 0 — усечение до int32 как оригинал.
		const bool bWide = CVarFixK5K6.GetValueOnGameThread() != 0;
		const int64 Qty = (Lot.Stock == -1) ? 0 : Lot.Stock;
		int64 Line = Qty * Lot.Price;
		if (Rec)
		{
			if (Rec->FlagWord176) { Line = (Lot.Stock == -1) ? 0 : Lot.Price; }
			else { Line = Qty * Lot.Price; }
		}
		if (bSp) { Line = Lot.Price; }
		if (!bWide) { Line = (int64)(int32)(uint32)(uint64)Line; }
		Total += Line;
		if (!bWide) { Total = (int64)(int32)(uint32)(uint64)Total; }
		if (bSpecialShop) { SetTxt(TxtTotal, Thousands64(Total)); }   // хозяину 23302 = сумма витрины (нарастающе)
		SetTxt(LotRows[R].Price, AsdaUi::FormatThousands(Lot.Price));
		SetTxt(LotRows[R].Total, Thousands64(Line));
	}
	if (LotIcons) { LotIcons->Refresh(); }
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	SetTxt(TxtMoney, AsdaUi::FormatThousands(Inv ? Inv->GetGold() : 0));
}

void UAsdaSelfStoreSubsystem::OnShowcaseMainButton()
{
	// 66561 по 23305 (shop_trade.c:17406-17531). Режим хозяина — «Изменить».
	if (!bShopOpen) { return; }
	if (!bSpecialShop)
	{
		// ★ Волна Л2 — режим покупателя (shop_trade.c:17525-17541): корзина пуста → msgbox 107084; `bPendingBuy == 1` → msgbox-вопрос
		//   тип 62 (`CItem_BuildTooltipText(…, 1, 62, …)`: 6888 «Купить выбранный предмет?», кнопки 12519 «OK» / 2424 «Отмена»),
		//   «OK» → `OnSendItemUpgrade` case 0x3E → `shopDlg_SendBuyReq`; иначе (6309 ушёл, 6310 ещё нет) → msgbox 107081.
		if (Cart.Num() == 0)
		{
			MsgBoxLoc(L::kBuyEmpty);
			JournalAdd(TEXT("23305 (гость): корзина пуста — 107084"));
			return;
		}
		if (!bPendingBuy)
		{
			MsgBoxLoc(L::kNoAnswer);
			JournalAdd(TEXT("23305 (гость): ответа на 6309 нет — 107081"));
			return;
		}
		UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!WM) { return; }
		TWeakObjectPtr<UAsdaSelfStoreSubsystem> WeakThis(this);
		AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Loc(L::kBuyAsk), [WeakThis](bool bYes)
		{
			UAsdaSelfStoreSubsystem* S = WeakThis.Get();
			if (!S) { return; }
			S->JournalAdd(FString::Printf(TEXT("msgbox 62: %s"), bYes ? TEXT("OK") : TEXT("Отмена")));
			if (bYes) { S->OnBuyConfirmed(); }
		}, 0.f, L::kBuyAskOk, L::kBuyAskCancel, /*EtalonNoticeId*/ 62, /*EtalonKind*/ 1);
		JournalAdd(FString::Printf(TEXT("23305 (гость): msgbox 62 (корзина %d)"), Cart.Num()));
		return;
	}
	if (bPremium)
	{
		// Премиум: эталон повторно шлёт КЭШ последнего пакета применения предмета (`CSellWindow+0x3B7`, 281 б) → ответ 5451
		//   case 64 → 6305 (переоткрытие) + 6300 с флагом премиума. ★ К-4: кэш перезаписывает ЛЮБОЕ применение предмета, так что
		//   уходит не билет. Исправление — применить сам билет (его ячейка из `+0x38A`) той же общей точкой, что клик игрока.
		UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		const int32 TCode = AsdaBytes::RdI32(Ticket45.GetData());
		const int32 TCont = Ticket45[4];
		const int32 TSlot = AsdaBytes::RdI16(Ticket45.GetData() + 5);
		const bool bFixK4 = CVarFixK4.GetValueOnGameThread() != 0;
		const FAsdaItemInstance* It = CellItem(TCont, TSlot);
		int32 UseCell = (It && It->ItemCode == TCode && TCont == 1) ? TSlot : -1;
		// [ПОРТ] Билет переложен (гейтов сумки у открытой лавки ещё нет — Л1-5): ищем ячейку сумки с тем же кодом.
		//   Канал 5450 работает только с сумкой (контейнер 1) — `UsePremiumItem`.
		if (bFixK4 && UseCell < 0 && Inv && TCode > 0)
		{
			for (int32 c = 0; c < UAsdaInventorySubsystem::kShellSlots && UseCell < 0; ++c)
			{
				const FAsdaItemInstance* B = Inv->GetCell(1, c);
				if (B && B->ItemCode == TCode) { UseCell = c; }
			}
		}
		bool bSent = false;
		if (bFixK4 && Inv && UseCell >= 0) { bSent = Inv->UsePremiumItem(1, UseCell); }
		// ★ Ревизия Л1 (СРЕДН-1 а): переоткрытие взводится ТОЛЬКО при реальной отправке 5450 (иначе К-3 «залипал» премиум,
		//   и следующее обычное открытие уходило с премиумом 1 и старым билетом → сервер 6 = 112501 на каждую попытку).
		//   При ключе К-4 = 0 — ветка без кэша (не оригинал, см. ключ): взвод как у эталона (`self->bBuySent = 1`).
		if (bSent || !bFixK4) { bReopen = true; }
		JournalAdd(FString::Printf(TEXT("23305 «Изменить» (премиум): билет код %d к=%d слот %d (ячейка сумки %d) — 5450 %s, переоткрытие %d%s"),
			TCode, TCont, TSlot, UseCell, bSent ? TEXT("отправлен") : TEXT("НЕ отправлен"), bReopen ? 1 : 0,
			bFixK4 ? (UseCell >= 0 ? TEXT("") : TEXT(" (билета в сумке нет)")) : TEXT(" (ключ К-4 = 0: кэша пакета в порту нет)")));
		return;
	}
	// Обычная лавка: 6305 + 6300 подряд, `+0x28C = 1` (список выставленного сохранится при переоткрытии).
	SendClose6305(TEXT("«Изменить» 23305"));
	SendReady6300(TEXT("«Изменить» 23305"));
	bReopen = true;
}

// ═════════════════════════════ ПОСЕТИТЕЛЬ (ВОЛНА Л2) ═══════════════════════════════════════════
//   Разбор — `Reports/trade/B_selfstore_client.md` §3.2, эталон — shop_trade.c:17353-18520; сервер — C2 §3.4-3.7.
bool UAsdaSelfStoreSubsystem::RequestVisit(int32 InHostUserNum, const TCHAR* Source)
{
	// `tradeRequestTargetByServerIdx@0xDE0F80` (МИСНЕЙМ = клик по вывеске, shop_trade.c:18644-18680): `byte_2C2FC0C != 1` (я не хозяин
	//   открытой витрины), `CharManager_FindByUserIdx(вывеска+460)` найден, `!get_o168` (витрина закрыта), `!get_m_bDlgVisible`
	//   (подготовка закрыта), `timeGetTime() − g_dwLastTradeReqTime > 1000` ⇒ метка времени, `netSendTradeTargetRequest6307`.
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return false; }
	if (bSpecialShop) { JournalAdd(FString::Printf(TEXT("%s: 6307 молча — я хозяин открытой лавки"), Source)); return false; }
	const UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	const int32 PeerCharId = WS ? WS->GetNetPlayerCharId(InHostUserNum) : 0;
	if (PeerCharId == 0) { JournalAdd(FString::Printf(TEXT("%s: 6307 молча — хозяина %d нет в мире"), Source, InHostUserNum)); return false; }
	if (bShopOpen || bSellOpen) { JournalAdd(FString::Printf(TEXT("%s: 6307 молча — витрина или подготовка открыты"), Source)); return false; }
	// ★ Пачка 20, жалоба 10 — [ИСПРАВЛЕНИЕ]: чужая витрина при открытом рынке 13000 не открывается.
	if (UAsdaExchangeSubsystem::IsMarketWindowOpen(GetGameInstance())) { JournalAdd(FString::Printf(TEXT("%s: 6307 молча — открыт рынок 13000 (пачка 20)"), Source)); return false; }
	const double Now = FPlatformTime::Seconds();
	if ((Now - LastVisitReqAt) * 1000.0 <= 1000.0) { JournalAdd(FString::Printf(TEXT("%s: 6307 молча — троттл 1000 мс"), Source)); return false; }
	LastVisitReqAt = Now;
	// `netSendTradeTargetRequest6307` (network.c:26875): `!g_bInventoryLocked` (разговор с NPC) ⇒ иначе молча; @0 свой charId,
	//   @4 charId хозяина, @8 userNum хозяина.
	const UAsdaNpcDialogSubsystem* Dlg = GI->GetSubsystem<UAsdaNpcDialogSubsystem>();
	if (Dlg && Dlg->IsDialogOpen()) { JournalAdd(FString::Printf(TEXT("%s: 6307 молча — открыт разговор с NPC (g_bInventoryLocked)"), Source)); return false; }
	UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
	if (!Net) { return false; }
	const bool bSent = Net->SendSelfStoreInfo6307(AsdaSelf::GetCharId(Net), PeerCharId, InHostUserNum);
	JournalAdd(FString::Printf(TEXT("%s: 6307 %s (хозяин charId %d userNum %d)"), Source, bSent ? TEXT("отправлен") : TEXT("НЕ отправлен"), PeerCharId, InHostUserNum));
	return bSent;
}

int32 UAsdaSelfStoreSubsystem::LotAtRow(int32 Row) const
{
	// `slots_array[Row]` — последний лот, нарисованный в строке (у предмета без таблицы строка не растёт — его перекрывает следующий).
	if (Row < 0) { return -1; }
	int32 Found = -1;
	for (int32 i = 0; i < Lots.Num(); ++i) { if (Lots[i].Row == Row) { Found = i; } }
	return Found;
}

int32 UAsdaSelfStoreSubsystem::LotIndexForWire(const FLot& Lot) const
{
	// Эталон: индекс лота 6309 и ключ сверки 6311/6312 — `CShopItem+36` = строка на экране. ★ К-10: при предмете без записи в
	//   таблицах строка не растёт и индексы следующих лотов расходятся с сервером — исправление берёт позицию лота у сервера.
	if (CVarFixK10.GetValueOnGameThread() != 0 && Lot.SrvIdx >= 0) { return Lot.SrvIdx; }
	return Lot.Row;
}

void UAsdaSelfStoreSubsystem::OnLotClicked(int32 Row, bool bRightButton)
{
	// `shopDlg_AddTabItemToCart@0xDD9770` (shop_trade.c:17598-17681): только НЕ в режиме хозяина; лот строки; распродан (−1) ⇒
	//   молча; уже в корзине (код + строка) ⇒ молча; штучный торгуемый со стопкой > 1 и не ПКМ ⇒ нумпад режим 8 (максимум = остаток);
	//   иначе кол-во к покупке = остаток и `shopCart_addItem`.
	if (!bShopOpen || bSpecialShop) { return; }
	const int32 LIdx = LotAtRow(Row);
	if (!Lots.IsValidIndex(LIdx)) { return; }
	FLot& Lot = Lots[LIdx];
	// Эталон проверяет остаток узла списка с ПОРЯДКОВЫМ номером = строке (при К-10 = 0 — как у него, иначе остаток самого лота).
	const bool bFixK10 = CVarFixK10.GetValueOnGameThread() != 0;
	const int32 StockCheck = bFixK10 ? Lot.Stock : (Lots.IsValidIndex(Row) ? Lots[Row].Stock : 0);
	if (StockCheck == -1) { JournalAdd(FString::Printf(TEXT("лот строки %d распродан — молча"), Row)); return; }
	for (const int32 C : Cart)
	{
		// `*slotItem == *cartEntry && slotItem[9] == cartEntry[9]` (код + строка). Вторая ветка эталона `cartEntry[1] == −1` читает
		//   НЕИНИЦИАЛИЗИРОВАННОЕ поле +4 лота (`sub_DD3A60` его не пишет, 6308 тоже) — [ОРИГИНАЛ] мусор стека; порт считает его ≠ −1.
		if (Lots.IsValidIndex(C) && Lots[C].ItemId == Lot.ItemId && Lots[C].Row == Lot.Row) { JournalAdd(TEXT("лот уже в корзине — молча")); return; }
	}
	PendingQtyLot = -1;   // `pPendingQtyItem = 0`
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(Lot.ItemId) : nullptr;
	if (!Rec || Rec->FlagWord176 || !Rec->TradableFlag || Lot.Stock <= 1 || bRightButton)
	{
		Lot.CartQty = Lot.Stock;   // `slotItem[4] = slotItem[3]`
		CartAddItem(LIdx);
		return;
	}
	// Нумпад режим 8 (`uiOpenContextPopupWithHandlers(g_pNumPadDialog, остаток, 8, 0)`): «OK» → `shopApplyQuantityFromEdit` →
	//   `> 0` → `sub_DD9700` (кол-во к покупке := введённое, `shopCart_addItem`). Поле нумпада режется до максимума
	//   (`uiClampQuantityToMax`) — общая точка `ShowNumPad` проверяет 1..остаток (NUMPAD_FLOW.md, «К-13 — не дефект»).
	PendingQtyLot = LIdx;
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	TWeakObjectPtr<UAsdaSelfStoreSubsystem> WeakThis(this);
	WM->ShowNumPad(Lot.Stock, [WeakThis](bool bOk, int32 Qty)
	{
		UAsdaSelfStoreSubsystem* S = WeakThis.Get();
		if (!S) { return; }
		const int32 P = S->PendingQtyLot;
		S->JournalAdd(FString::Printf(TEXT("нумпад 8: %s, кол-во %d"), bOk ? TEXT("OK") : TEXT("отмена"), Qty));
		// [ПОРТ] витрина гостя закрылась, пока был открыт нумпад, — позиция к закрытой лавке не относится.
		if (bOk && Qty > 0 && S->IsVisitorShowcaseOpen() && S->Lots.IsValidIndex(P))
		{
			S->Lots[P].CartQty = Qty;
			S->CartAddItem(P);
		}
	});
	JournalAdd(FString::Printf(TEXT("нумпад режим 8: код %d, максимум %d"), Lot.ItemId, Lot.Stock));
}

void UAsdaSelfStoreSubsystem::CartAddItem(int32 LotIdx)
{
	// `shopCart_addItem@0xDD9A50` (shop_trade.c:17685-17720): < 6 — дубль (код + строка) молча, иначе в конец + перерисовка
	//   суммы и витрины; 7-я — `CChat_AddTokenizedLines(107086, −256, 10000)` затем `CControl_SetTooltipText(g_pChatWnd, 107086, −256,
	//   10000)` (строка чата, затем плашка).
	if (!Lots.IsValidIndex(LotIdx)) { return; }
	if (Cart.Num() >= kCartMax)
	{
		AsdaNotice::PlankAndChatLoc(GetGameInstance(), EAsdaMsgCategory::Trade, L::kCartFull, kYellow, 10000.f, 23000, /*bChatFirst*/ true);
		JournalAdd(TEXT("7-я позиция корзины: 107086 (чат + плашка)"));
		return;
	}
	for (const int32 C : Cart) { if (Lots.IsValidIndex(C) && Lots[C].ItemId == Lots[LotIdx].ItemId && Lots[C].Row == Lots[LotIdx].Row) { return; } }
	Cart.Add(LotIdx);
	JournalAdd(FString::Printf(TEXT("в корзину: код %d строка %d кол-во %d (позиций %d)"), Lots[LotIdx].ItemId, Lots[LotIdx].Row, Lots[LotIdx].CartQty, Cart.Num()));
	RefreshShowcaseTotals();
	RefreshShowcaseList();
}

void UAsdaSelfStoreSubsystem::CartRemoveItem(int32 LotIdx)
{
	// `shopCartRemoveItem@0xDD9BC0`: `a2 == NULL` ⇒ ничего; первая позиция с тем же лотом (код и поле +40 — у эталона это
	//   один и тот же объект) удаляется; перерисовка суммы и витрины — ВСЕГДА при непустом аргументе.
	if (!Lots.IsValidIndex(LotIdx)) { return; }
	const int32 At = Cart.Find(LotIdx);
	if (At != INDEX_NONE)
	{
		Cart.RemoveAt(At);
		JournalAdd(FString::Printf(TEXT("из корзины: код %d (позиций %d)"), Lots[LotIdx].ItemId, Cart.Num()));
	}
	RefreshShowcaseTotals();
	RefreshShowcaseList();
}

void UAsdaSelfStoreSubsystem::OnCartCellClicked(int32 Cell)
{
	// 0xB по 23250+k: `itemSlotWndShowSlotCtrl@0xDD9DA0` — рамка 23241 скрыта, `pSelectedSlotItem = field_C8_pad[k]`; есть лот ⇒
	//   рамка на ячейку и показ; затем `shopDlgOnCartRemoveButton@0xDD9D30` — `shopCartRemoveItem(field_C8_pad[k])`: клик по
	//   ячейке корзины СНИМАЕТ позицию (перерисовка прячет рамку — выбранного в корзине больше нет). [ОРИГИНАЛ]
	if (!bShopOpen || Cell < 0 || Cell > 9) { return; }
	AsdaUi::SetShownDecor(CartSel, false);
	const int32 LIdx = CartCellLot.IsValidIndex(Cell) ? CartCellLot[Cell] : -1;
	SelLot = LIdx;
	if (Lots.IsValidIndex(LIdx) && CartImg.IsValidIndex(Cell) && CartImg[Cell] && CartSel)
	{
		UCanvasPanelSlot* From = AsdaUi::SlotOf(CartImg[Cell]);
		UCanvasPanelSlot* To = AsdaUi::SlotOf(CartSel);
		if (From && To) { To->SetPosition(From->GetPosition()); }
		AsdaUi::SetShownDecor(CartSel, true);
	}
	JournalAdd(FString::Printf(TEXT("клик по ячейке корзины %d: лот %d"), Cell, LIdx));
	if (Lots.IsValidIndex(LIdx)) { CartRemoveItem(LIdx); }
}

void UAsdaSelfStoreSubsystem::OnTrashClicked()
{
	// 66561 по 23243: `shopCartRemoveItem(self, pSelectedSlotItem)` — выбранный кликом лот (если он снова в корзине).
	JournalAdd(FString::Printf(TEXT("урна 23243: выбран лот %d"), SelLot));
	if (Lots.IsValidIndex(SelLot)) { CartRemoveItem(SelLot); }
}

void UAsdaSelfStoreSubsystem::OnLogHomeClicked()
{
	// 66561 по 23404: `uiScrollbarSetPos(полоса журнала, её максимум)` — журнал в конец.
	if (LogList) { LogList->ScrollToEnd(); }
	JournalAdd(TEXT("23404: журнал в конец"));
}

void UAsdaSelfStoreSubsystem::OnBuyConfirmed()
{
	// «OK» msgbox 62 → `OnSendItemUpgrade` case 0x3E (МИСНЕЙМ общего обработчика msgbox, G:380146) → `shopDlg_SendBuyReq`.
	SendBuyReq();
}

bool UAsdaSelfStoreSubsystem::SendBuyReq()
{
	// `shopDlg_SendBuyReq@0xDDB5F0` (shop_trade.c:17963-18145): только при `bPendingBuy`; позиции корзины по порядку: код,
	//   кол-во = `+24 ? +16 : +24` (у нештучного 0), индекс = `+36`, цена `+8`; `bPendingBuy = 0`.
	if (!bPendingBuy) { JournalAdd(TEXT("6309 не отправлен: bPendingBuy = 0")); return false; }
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return false; }
	TArray<int32> Ids, Qtys, Idx, Prices;
	int64 Sum = 0;
	for (const int32 C : Cart)
	{
		if (!Lots.IsValidIndex(C)) { continue; }
		const FLot& Lot = Lots[C];
		const int32 WireQty = Lot.RawQty != 0 ? Lot.CartQty : Lot.RawQty;
		Ids.Add(Lot.ItemId);
		Qtys.Add(WireQty);
		Idx.Add(LotIndexForWire(Lot));
		Prices.Add(Lot.Price);
		// [ПОРТ] `asda.Trade.StoreBuyGuard`: сервер (P43 G2/G3) обнуляет переполненное произведение/сумму и отвечает «Wrong Price»
		//   (6310 код 0 = 107050). Порт такой пакет не шлёт и показывает тот же текст. Потолок самой цены (1 млрд, P43) задаёт продавец.
		//   ★ Ревизия Л2 (МЕЛК-2): стоимость позиции — ПО ПРАВИЛУ СЕРВЕРА (`ServerLinePrice`), а не «всегда кол-во × цена»: иначе
		//   ложный блок покупки, которую сервер примет (он берёт цену лота у большинства предметов).
		const int64 Line = ServerLinePrice(Lot, WireQty);
		Sum += Line;
		if (CVarBuyGuard.GetValueOnGameThread() != 0 && (Line > (int64)MAX_int32 || Sum > (int64)MAX_int32))
		{
			MsgBoxLoc(L::kBuy0);
			JournalAdd(FString::Printf(TEXT("6309 не отправлен: код %d кол-во %d, стоимость позиции по правилу сервера %lld (сумма %lld) > 2^31−1 — 107050 ([ПОРТ])"),
				Lot.ItemId, WireQty, Line, Sum));
			return false;
		}
	}
	const bool bSent = Net->SendSelfStoreBuy6309(AsdaSelf::GetCharId(Net), HostCharId, HostUserNum, Ids, Qtys, Idx, Prices);
	// [ПОРТ] Эталон гасит `bPendingBuy` ДО отправки; порт — только если пакет реально ушёл (закон «не защёлкивать заранее»):
	//   иначе «OK» до переоткрытия витрины отвечал бы 107081 на запрос, которого сервер не видел.
	if (bSent) { bPendingBuy = false; }
	JournalAdd(FString::Printf(TEXT("6309 %s: позиций %d, хозяин %d/%d"), bSent ? TEXT("отправлен") : TEXT("НЕ отправлен"), Ids.Num(), HostCharId, HostUserNum));
	return bSent;
}

int64 UAsdaSelfStoreSubsystem::ServerLinePrice(const FLot& Lot, int32 WireQty) const
{
	// [ПРОТОКОЛ] Правило СЕРВЕРА `CharSelfStoreBuyItem` (GameServer.c:100863-100886), стоимость позиции v80:
	//   • кол-во 0 (нештучный) ИЛИ «плита» лота 1 (сумка) → цена лота;
	//   • иначе код 7001…(`Cellob_Table`, `sub_532AE0`) → цена лота;
	//   • иначе код `item_list` (20551…, `sub_532A90`): запись `Item_Table.txt` +26 == 10 И +7 == 4 → цена лота, прочие — кол-во × цена;
	//   • иначе v80 = 0 → «Wrong Price» (6310 код 0) при любой сумме — не переполнение, защите не подлежит (цена лота).
	//   Столбцы сервера совпадают с клиентскими полями `item_list` (сверено по живому `Item_Table.txt` 29.08, 58 878 строк):
	//   +7 (столбец 3) = `EnchantOrSocket` (+0x4C, совпало 57 729 из 58 878), +26 (столбец 16) = `ItemType` (+0x80, 57 320);
	//   правило «4 и 10» даёт 35 кодов у сервера и 31 у клиента — 4 «Inferno Fishing Log» (69682…69685) у клиента тип 11.
	//   ★ Волна Л4/Л5 (ревизия Л3, МЕЛК-1): прежний вывод «для защиты это лишь пропуск» был НЕВЕРЕН — у этих 4 кодов клиент
	//   считал кол-во × цена, а сервер берёт цену лота: оценка порта ВЫШЕ серверной, возможен ложный 107050 (стопка ≥ 22 при цене
	//   1e8). Защита — [ПОРТ] и имеет право только ЗАНИЖАТЬ оценку, поэтому набор кодов правила берётся из ДАННЫХ сервера
	//   (`Config/DefaultAsdaTrade.ini`, выгрузка живой `Item_Table.txt`; без зашивки кодов в код). Вариант «EnchantOrSocket == 4 без
	//   ItemType» отвергнут: у клиента 4 стоит у 11 186 кодов типа 11, которые сервер умножает, — защита потеряла бы смысл.
	//   69681 — у клиента и у сервера 4/11 (сервер умножает), в правило не входит.
	//   ★ Потолок сервера — 100 000 000 за единицу (6302 `CharSelfStoreOpen` отвергает цену > 1e8, S:100221; 6309 — строку
	//   `v80 > 0x5F5E100` кодом 0 «Wrong Price», S:100891; P43 — собран, НЕ установлен — поднимает оба до 1e9, C2 §10).
	//   ★ Ревизия Л4/Л5 (МЕЛК-1): v80 — цена СТРОКИ (кол-во × цена, S:100886), поэтому строку > 1e8 сервер и так отвергает кодом 0.
	//   Порог защиты — ЗАВОРОТ i32 произведения и суммы корзины: завёрнутая строка может стать «законной» (43 × 1e8 → 5 032 704 —
	//   сервер её ПРИМЕТ), от этого защита и нужна.
	//   «Плиты» лота посетитель не знает (6308 её не несёт): штучный предмет `item_list` лежит в контейнере 2, спец-предметы 1…558 —
	//   в сумке (плита 1). Итог: оценка никогда не больше серверной у предметов, которые сервер примет, — ложного блока нет.
	if (WireQty == 0) { return (int64)Lot.Price; }
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(Lot.ItemId) : nullptr;
	if (!Rec) { return (int64)Lot.Price; }
	bool bFromData = false;
	if (IsServerLotPriceCode(Lot.ItemId, bFromData)) { return (int64)Lot.Price; }
	return (int64)WireQty * (int64)Lot.Price;
}

bool UAsdaSelfStoreSubsystem::IsServerLotPriceCode(int32 ItemId, bool& bOutFromData) const
{
	// Ленивая загрузка набора кодов из `Config/DefaultAsdaTrade.ini` (иерархия Base → Default → платформа, как у любого ini UE).
	if (!bSrvLotPriceLoaded)
	{
		bSrvLotPriceLoaded = true;
		FConfigFile Ini;
		FConfigCacheIni::LoadLocalIniFile(Ini, TEXT("AsdaTrade"), /*bIsBaseIniName*/ true);
		TArray<FString> Codes;
		Ini.GetArray(TEXT("SelfStore.ServerLotPriceRule"), TEXT("Code"), Codes);
		for (const FString& C : Codes) { if (C.IsNumeric()) { SrvLotPriceCodes.Add(FCString::Atoi(*C)); } }
		UE_LOG(LogAsdaSelfStore, Log, TEXT("[лавка] правило сервера «цена лота» (DefaultAsdaTrade.ini): %d кодов%s"), SrvLotPriceCodes.Num(),
			SrvLotPriceCodes.Num() ? TEXT("") : TEXT(" — секции нет, правило по клиентской item_list (4 и 10)"));
	}
	if (SrvLotPriceCodes.Num() > 0)
	{
		bOutFromData = true;
		return SrvLotPriceCodes.Contains(ItemId);
	}
	bOutFromData = false;
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(ItemId) : nullptr;
	return Rec && Rec->EnchantOrSocket == 4 && Rec->ItemType == 10;
}

void UAsdaSelfStoreSubsystem::HandleVisitOpen6308(const TArray<uint8>& Body)
{
	// `shopHandleOpenShopPacket@0xDDA580` (shop_trade.c:17793-17864). Витрина открыта (`get_o168`) ⇒ пакет игнорируется целиком.
	if (Body.Num() < 628) { return; }
	const int32 Code = Body[0];
	JournalAdd(FString::Printf(TEXT("6308 код %d лотов %d"), Code, (int32)Body[7]));
	if (bShopOpen) { JournalAdd(TEXT("6308: витрина уже открыта — пакет игнорируется")); return; }
	switch (Code)
	{
	case 0: MsgBoxLoc(L::kVisit0); return;   // 107070 «Это не частный магазин» (в т. ч. «Same User Error» длиной 4)
	case 1:
	{
		bPendingBuy = true;
		StoreTitle = FAsdaLoginData::DecodeCp1251(Body.GetData() + 8, 50);    // `sub_93A138` → `+0x171` название → 23006
		OwnerName = FAsdaLoginData::DecodeCp1251(Body.GetData() + 58, 20);    // `sub_92E761` → `+0x1F1` (не выводится, К-15)
		HostCharId = AsdaBytes::RdI32(Body.GetData() + 1);                     // `pNpcChar` (МИСНЕЙМ) = `+0x274`
		HostUserNum = AsdaBytes::RdI16(Body.GetData() + 5);                    // `selItemType` (МИСНЕЙМ) = `+0x278`
		Lots.Reset();                                                          // `sub_920C0B` — витрина пуста
		// [ПОРТ] эталон читает `count` лотов (u8) — лот 10+ лёг бы за буфер 628 б; `CShopDlg_AddBuyItem` всё равно берёт < 10.
		const int32 N = FMath::Min<int32>(Body[7], kMaxLots);
		for (int32 i = 0; i < N; ++i)
		{
			const uint8* P = Body.GetData() + 78 + 55 * i;
			FLot Lot;
			Lot.ItemId = AsdaBytes::RdI32(P + 0);
			Lot.Price = AsdaBytes::RdI32(P + 45);
			Lot.RawQty = AsdaBytes::RdI32(P + 4);               // v9[6] — до замены
			Lot.Stock = Lot.RawQty ? Lot.RawQty : 1;            // `if (!qty) qty = 1; v9[3] = qty`
			for (int32 k = 0; k < 5; ++k) { Lot.Ench5[k] = AsdaBytes::RdI32(P + 24 + 4 * k); }
			for (int32 k = 0; k < 4; ++k) { Lot.Sowel[k] = (int16)AsdaBytes::RdI16(P + 11 + 2 * k); }
			Lot.EnchantFlag = P[19];
			Lot.Ench73 = P[20];
			Lot.Ench74 = P[21];
			Lot.EnchantPct = (int32)(int16)AsdaBytes::RdI16(P + 22);
			Lot.StackWeight = (int16)AsdaBytes::RdI16(P + 9);
			Lot.Durability = P[8];
			Lot.SrvIdx = i;                                     // [ПОРТ] К-10: позиция лота у сервера = порядок в 6308
			if (Lots.Num() < kMaxLots) { Lots.Add(Lot); }       // `CShopDlg_AddBuyItem`: только если < 10
		}
		OpenShowcase();   // `shopDlgOpenSellWindow` (МИСНЕЙМ = открыть витрину): хозяин ≠ я ⇒ режим покупателя
		return;
	}
	case 2: MsgBoxLoc(L::kVisit2); return;   // 107072 «Превышена вместимость» (5 гостей)
	default:
		JournalAdd(FString::Printf(TEXT("6308 код %d — у эталона ветки нет (молча)"), Code));
		return;
	}
}

void UAsdaSelfStoreSubsystem::HandleBuyResult6310(const TArray<uint8>& Body)
{
	// `shopSellDlg_OnSellResult@0xDDBF40` (МИСНЕЙМ = ответ покупателю, shop_trade.c:18147-18255): `bPendingBuy = 1` ВСЕГДА; [0] код.
	if (Body.Num() < 350) { return; }
	bPendingBuy = true;
	const int32 Code = Body[0];
	JournalAdd(FString::Printf(TEXT("6310 код %d"), Code));
	UGameInstance* GI = GetGameInstance();
	switch (Code)
	{
	case 1:
	{
		// Вес @1 (`HUD_UpdateMiniWeightBar`), золото @3 (`uiSetHonorPointsDisplay` — МИСНЕЙМ, ставит золото), 6 записей 57 б с @8:
		//   код > 0 ⇒ запись в свой слот (контейнер 2 — `memcpy`, 1 — `Inventory_SetBagSlot`) — общая точка `ApplyExternalRecord`;
		//   `CInventoryUI_ResetAndRefresh`; корзина очищается; перерисовка суммы (витрину эталон здесь НЕ перерисовывает — это
		//   сделает следующий 6312).
		if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr) { Net->SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI16(Body.GetData() + 1)), -1, TEXT("6310")); }   // ★ 29.10.2026: вес u16
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		if (Inv) { Inv->SetGoldDisplay(AsdaBytes::RdI32(Body.GetData() + 3)); }
		int32 Applied = 0;
		for (int32 i = 0; i < 6 && Inv; ++i)
		{
			const uint8* Rec = Body.GetData() + 8 + 57 * i;
			if (AsdaBytes::RdI32(Rec) <= 0) { continue; }
			TArray<uint8> R57; R57.Append(Rec, 57);
			if (Inv->ApplyExternalRecord(R57)) { ++Applied; }
		}
		Cart.Reset();
		RefreshShowcaseTotals();
		JournalAdd(FString::Printf(TEXT("6310 код 1: вес %d золото %d, записей применено %d, корзина очищена"),
			AsdaBytes::RdI16(Body.GetData() + 1), AsdaBytes::RdI32(Body.GetData() + 3), Applied));
		return;
	}
	case 0: MsgBoxLoc(L::kBuy0); return;                               // 107050 «ошибка» (в т. ч. сбой БД — и хозяину)
	case 2: case 3: case 4: case 5: case 6: case 7: case 8:
		// 107052 недоступен / 107053 закрыл окно / 107054 тяжело / 107055 нет ячеек / 107056 не хватает золота (★ и новый отказ
		//   P43 G4 «золото продавца переполнится» — тот же код 6, своего текста у эталона нет) / 107057 нет в наличии / 107058 купили.
		MsgBoxLoc(L::kBuy0 + Code);
		return;
	case 9: MsgBoxLoc(L::kBuy9); return;                               // 107048 «…не младше 25 уровня»
	default:
		JournalAdd(FString::Printf(TEXT("6310 код %d — у эталона ветки нет (молча)"), Code));
		return;
	}
}

void UAsdaSelfStoreSubsystem::ApplySoldLots(const uint8* Lots45, int32 BuyerCharId)
{
	// Общая часть `shopDlg_onBuyResultPacket` / `shopDlg_onConfirmBuyPacket`: для каждой из 6 записей (с +11 тела) и каждого лота
	//   витрины с тем же кодом и индексом (+36) — строка журнала 107082 жёлтым (если покупатель виден: `CharManager_FindByCharId`),
	//   затем остаток лота := остаток из записи (−1 — распродан).
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	FString Buyer;
	const bool bBuyer = CharNameById(BuyerCharId, Buyer);
	const bool bWide = CVarFixK5K6.GetValueOnGameThread() != 0;
	for (int32 i = 0; i < 6; ++i)
	{
		const uint8* P = Lots45 + 45 * i;
		const int32 Code = AsdaBytes::RdI32(P + 0);
		const int32 Idx = AsdaBytes::RdI16(P + 11);
		const int32 Rem = AsdaBytes::RdI32(P + 7);
		for (FLot& Lot : Lots)
		{
			if (Lot.ItemId != Code || LotIndexForWire(Lot) != Idx) { continue; }
			if (bBuyer)
			{
				int64 Sold = (int64)Lot.Stock - (Rem == -1 ? 0 : Rem);   // `priceFlag = buyEntry[3] − refundQty`
				const FAsdaItemRecord* Rec = IT ? IT->GetItem(Lot.ItemId) : nullptr;
				FAsdaSpecialItemlistRow Sp;
				if (Rec && (Rec->FlagWord176 || Rec->CategoryFlag == 3)) { Sold = 1; }
				if (IT && IT->GetSpecialItem(Lot.ItemId, Sp)) { Sold = 1; }
				int64 Total = Sold * Lot.Price;
				if (!bWide) { Total = (int64)(int32)(uint32)(uint64)Total; }
				const FString Line = SubstPrintf(Loc(L::kSoldLog), { Buyer, ItemNameOf(Lot.ItemId),
					FString::Printf(TEXT("%lld"), Sold), FString::Printf(TEXT("%lld"), Total) });
				AddLogLine(FString(), Line, kYellow);
			}
			Lot.Stock = Rem;   // `buyEntry[3] = остаток`
		}
	}
}

void UAsdaSelfStoreSubsystem::HandleSold6311(const TArray<uint8>& Body)
{
	// `shopDlg_onBuyResultPacket@0xDDC480` (МИСНЕЙМ = продавцу «у тебя купили», shop_trade.c:18273-18380): вес @0 i16, золото @2,
	//   покупатель @7, записи 45 б с @11 (@0 код, @4 контейнер, @5 слот, @7 остаток −1 = распродан, @11 индекс лота).
	if (Body.Num() < 281) { return; }
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr) { Net->SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI16(Body.GetData())), -1, TEXT("6311")); }   // ★ 29.10.2026: вес u16
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (Inv) { Inv->SetGoldDisplay(AsdaBytes::RdI32(Body.GetData() + 2)); }
	const int32 Buyer = AsdaBytes::RdI32(Body.GetData() + 7);
	const bool bFixK8 = CVarFixK8.GetValueOnGameThread() != 0;
	int32 Cleared = 0, Reduced = 0;
	for (int32 i = 0; i < 6 && Inv; ++i)
	{
		const uint8* P = Body.GetData() + 11 + 45 * i;
		const int32 Code = AsdaBytes::RdI32(P);
		const int32 Cont = P[4];
		const int32 SlotN = AsdaBytes::RdI16(P + 5);
		const int32 Rem = AsdaBytes::RdI32(P + 7);
		const int32 Idx = AsdaBytes::RdI16(P + 11);
		if (!bFixK8)
		{
			// [ОРИГИНАЛ] `memset(g_InventoryArray2 / g_InventoryBag + 57 × слот, 0, 57)` — только при остатке −1, слот целиком, по
			//   контейнеру/слоту ИЗ ЗАПИСИ пакета.
			if (Cont != 1 && Cont != 2) { continue; }
			if (Rem == -1) { Inv->ClearCell(Cont, SlotN); ++Cleared; }
			continue;
		}
		// ★ К-8: стопка в сумке уменьшается на ПРОДАННОЕ (прежний остаток лота − новый); ноль и меньше — слот пуст. У оригинала
		//   частичная продажа не видна, а распродажа части стопки стирала стопку целиком до 6306 (там — К-1, 6 из 10 записей).
		//   ★ Ревизия Л2 (СРЕДН-1): сервер (`CharSelfStoreBuyItem`, RecvDB `sub_506A00`, S:101438-101447) в ветке «остаток > 0» пишет
		//   в запись ТОЛЬКО код, индекс (+11) и остаток (+7) — контейнер/слот остаются от `sub_508F20` (+4 = 0, +5 = −1); настоящие
		//   контейнер/слот он кладёт лишь при распродаже (S:101449-101452) и у снаряжения (S:101374-101376). Поэтому адрес ячейки
		//   сумки берётся из ЛОТА ХОЗЯИНА (+28/+32 `CShopItem`, из 6303), а запись пакета — только если в ней контейнер 1/2.
		int32 Sold = 0;
		int32 LotCont = -1, LotSlot = -1;
		for (const FLot& Lot : Lots)
		{
			if (Lot.ItemId != Code || LotIndexForWire(Lot) != Idx) { continue; }
			Sold = FMath::Max(0, Lot.Stock) - (Rem == -1 ? 0 : Rem);
			LotCont = Lot.Container;
			LotSlot = Lot.Slot;
		}
		const bool bRecAddr = (Cont == 1 || Cont == 2);
		const int32 UseCont = bRecAddr ? Cont : LotCont;
		const int32 UseSlot = bRecAddr ? SlotN : LotSlot;
		if (UseCont != 1 && UseCont != 2) { continue; }
		const FAsdaItemInstance* It = Inv->GetCell(UseCont, UseSlot);
		if (!It || It->ItemCode != Code) { continue; }
		if (Sold <= 0) { if (Rem == -1) { Inv->ClearCell(UseCont, UseSlot); ++Cleared; } continue; }
		const int32 Left = It->Amount - Sold;
		if (Left <= 0) { Inv->ClearCell(UseCont, UseSlot); ++Cleared; continue; }
		TArray<uint8> R57 = It->Raw;
		if (R57.Num() >= 57)
		{
			AsdaBytes::WrI32(R57.GetData() + 9, Left);
			Inv->ApplyExternalRecord(R57);
			++Reduced;
		}
	}
	if (Lots.Num() > 0) { ApplySoldLots(Body.GetData() + 11, Buyer); }
	RefreshShowcaseList();
	JournalAdd(FString::Printf(TEXT("6311: покупатель %d, слотов очищено %d, уменьшено %d (К-8 %d)"), Buyer, Cleared, Reduced, bFixK8 ? 1 : 0));
}

void UAsdaSelfStoreSubsystem::HandleShowcaseUpdate6312(const TArray<uint8>& Body)
{
	// `shopDlg_onConfirmBuyPacket@0xDDCAA0` (МИСНЕЙМ = гостям «витрина изменилась», shop_trade.c:18384-18520): @0 charId хозяина
	//   (иначе пакет игнорируется), @7 покупатель, записи 45 б с @11. Витрина не пуста ⇒ журнал + остатки; корзина не пуста ⇒
	//   убрать позиции проданных лотов; перерисовка суммы и витрины.
	if (Body.Num() < 281) { return; }
	// ★ [ИСПРАВЛЕНИЕ] `asda.Trade.StoreStrayGuard` (ревизия Л2, СРЕДН-2): только при открытой витрине посетителя — сверка хозяина
	//   ниже не спасает игрока, унаследовавшего userNum застрявшего гостя и не заходившего к этому хозяину (HostCharId от 6308 прошлой
	//   лавки или 0).
	if (CVarStrayGuard.GetValueOnGameThread() != 0 && !IsVisitorShowcaseOpen()) { JournalAdd(TEXT("6312 без витрины посетителя — молча (StoreStrayGuard)")); return; }
	if (HostCharId != AsdaBytes::RdI32(Body.GetData())) { JournalAdd(TEXT("6312: чужая лавка — пакет игнорируется")); return; }
	const int32 Buyer = AsdaBytes::RdI32(Body.GetData() + 7);
	if (Lots.Num() > 0) { ApplySoldLots(Body.GetData() + 11, Buyer); }
	if (Cart.Num() > 0)
	{
		// ★ К-2: эталон сравнивает `+36` КАЖДОЙ позиции корзины с индексом лота ТОЛЬКО ПЕРВОЙ записи (`SHIWORD(tradeSlots[5])`
		//   от цикла не зависит) — позиции, проданные второй…шестой записью, в корзине остаются. Исправление — по всем записям.
		const bool bFixK2 = CVarFixK2.GetValueOnGameThread() != 0;
		const int32 NRec = bFixK2 ? 6 : 1;
		int32 Removed = 0;
		for (int32 r = 0; r < NRec; ++r)
		{
			const uint8* P = Body.GetData() + 11 + 45 * r;
			if (bFixK2 && AsdaBytes::RdI32(P) <= 0) { continue; }   // пустая запись (`slotStructReset`: код 0, индекс −1)
			const int32 Idx = AsdaBytes::RdI16(P + 11);
			for (int32 c = Cart.Num() - 1; c >= 0; --c)
			{
				const int32 L = Cart[c];
				if (!Lots.IsValidIndex(L)) { continue; }
				const int32 Key = bFixK2 ? LotIndexForWire(Lots[L]) : Lots[L].Row;   // оригинал: `cartEntry+36`
				if (Key == Idx) { Cart.RemoveAt(c); ++Removed; }
			}
		}
		JournalAdd(FString::Printf(TEXT("6312: из корзины убрано %d (К-2 %d)"), Removed, bFixK2 ? 1 : 0));
	}
	RefreshShowcaseTotals();
	RefreshShowcaseList();
}

void UAsdaSelfStoreSubsystem::HandleGuests6315(const TArray<uint8>& Body)
{
	// `shopDlgAppendTradeResultLog@0xDDB3F0` (МИСНЕЙМ, shop_trade.c:17905-17932): @0 u8 число гостей (`set_oA4`), @1 charId; персонаж
	//   виден ⇒ «прежнее ≤ новое» — 107087 «Входит %s'», иначе 107088 «%s' выходит», жёлтым в журнал. Хозяину и гостям.
	if (Body.Num() < 5) { return; }
	// ★ [ИСПРАВЛЕНИЕ] `asda.Trade.StoreStrayGuard` (ревизия Л2, СРЕДН-2): только при открытой витрине (своей или чужой).
	if (CVarStrayGuard.GetValueOnGameThread() != 0 && !bShopOpen) { JournalAdd(TEXT("6315 без открытой витрины — молча (StoreStrayGuard)")); return; }
	const int32 Prev = GuestCount;
	GuestCount = Body[0];
	const int32 Who = AsdaBytes::RdI32(Body.GetData() + 1);
	FString Name;
	if (CharNameById(Who, Name))
	{
		const FString Line = SubstPrintf(Loc(Prev <= GuestCount ? L::kGuestIn : L::kGuestOut), { Name });
		AddLogLine(FString(), Line, kYellow);
	}
	JournalAdd(FString::Printf(TEXT("6315: гостей %d → %d, charId %d (%s)"), Prev, GuestCount, Who, Name.IsEmpty() ? TEXT("не виден — молча") : *Name));
}

void UAsdaSelfStoreSubsystem::EnsureLogList()
{
	// 23402 `Stall_Chat_History` (ListBox `Inven_List`, `.JUI`: (19,25) 412×71 от блока 23400) — узла в WBP НЕТ: список строится
	//   кодом (решение владельца, `wave0.md` §1.3). Эталон: `helper_setByte295`, `sub_937235(…, 0x400000)` (тень), `sub_935C9B(…, 900)`
	//   (≤ 900 строк), `uiListSetScrollOffset(…, 2)` (шаг строки +2).
	//   ★ Родитель — группа подложки `Stall_ChatBoxBG_grp` (#139, (7,25) 424×71, та же полоса, что список): порядок обхода `.JUI`
	//   23401 (#141) → 23402 (#142) → 23403 (#143) → 23404 (#144) ставит список ПОД поле ввода и кнопку «в конец» (кнопка (418,452)
	//   лежит на правом крае списка x 19…431 и должна ловить клик сама). Добавление в конец группы блока чата положило бы список
	//   поверх кнопки (живой `SConstraintCanvas` получает новый слот последним), а подложка — предок раньше по порядку, и список
	//   в ней рисуется над её планами, но под всеми узлами #141…#145. Размеры/позиции ассета не меняются.
	if (LogList || !ShopWidget || !ShopWidget->WidgetTree) { return; }
	UCanvasPanel* Host = Cast<UCanvasPanel>(AsdaUi::FindNodeByName(ShopWidget, TEXT("Stall_ChatBoxBG_grp"), nullptr));
	if (!Host) { UE_LOG(LogAsdaSelfStore, Warning, TEXT("[лавка] группа подложки чата Stall_ChatBoxBG_grp не найдена — журнал 23402 не создан")); return; }
	LogList = ShopWidget->WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("Stall_Chat_History"));
	if (!LogList) { return; }
	LogList->SetOrientation(Orient_Vertical);
	LogList->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
	LogList->SetScrollBarVisibility(ESlateVisibility::Collapsed);
	LogList->SetVisibility(ESlateVisibility::Visible);
	if (UCanvasPanelSlot* CS = Host->AddChildToCanvas(LogList))
	{
		CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		CS->SetAlignment(FVector2D(0.f, 0.f));
		CS->SetPosition(FVector2D(19.f - 7.f, 0.f));   // (19,25) от блока 23400 минус начало подложки (7,25)
		CS->SetSize(FVector2D(412.f, 71.f));
	}
}

void UAsdaSelfStoreSubsystem::AddLogLine(const FString& Who, const FString& Text, const FColor& Color)
{
	// `shopDlg_AddLogLineEllipsis@0xDD6D00`: «Кто : текст» (имя пусто — только текст); шире 406 px — посимвольно, начиная с
	//   символа, на котором ширина > 390 px, символы заменяются '.', до трёх; `uiChatLogAddLine(listLog, строка, цвет)`;
	//   `bAutoScrollLog == 1` и полоса у низа ⇒ флаг снят и полоса в конец; полоса была у низа ⇒ в конец.
	EnsureLogList();
	const FString Buffer = Who.IsEmpty() ? Text : FString::Printf(TEXT("%s : %s"), *Who, *Text);
	JournalAdd(FString::Printf(TEXT("журнал 23402: «%s»"), *Buffer));
	if (!LogList || !ShopWidget || !ShopWidget->WidgetTree) { return; }
	const FSlateFontInfo Font = LogFontSrc ? LogFontSrc->GetFont() : FSlateFontInfo();
	FString Line;
	if (AsdaUi::MeasureTextWidth(Buffer, Font, 0.f) <= 406.f)
	{
		Line = Buffer;
	}
	else
	{
		int32 Dots = 0;
		for (int32 i = 0; i < Buffer.Len(); ++i)
		{
			Line.AppendChar(Buffer[i]);
			if (AsdaUi::MeasureTextWidth(Line, Font, 0.f) > 390.f)
			{
				++Dots;
				Line[Line.Len() - 1] = TEXT('.');
				if (Dots == 3) { break; }
			}
		}
	}
	const bool bAtEnd = (LogList->GetScrollOffsetOfEnd() - LogList->GetScrollOffset()) <= 1.f;
	UTextBlock* Tb = ShopWidget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	if (!Tb) { return; }
	Tb->SetFont(Font);
	AsdaUi::SetTextOutline(Tb, 0);   // стиль 926 у списка — только тень 0x400000, обводки нет
	Tb->SetText(FText::FromString(Line));
	Tb->SetColorAndOpacity(FSlateColor(FLinearColor::FromSRGBColor(Color)));
	Tb->SetShadowOffset(FVector2D(1.f, 1.f));                         // 0x400000 — тень (как общая точка чата)
	Tb->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.5f));
	Tb->SetVisibility(ESlateVisibility::HitTestInvisible);
	LogList->AddChild(Tb);
	if (UScrollBoxSlot* SS = Cast<UScrollBoxSlot>(Tb->Slot)) { SS->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f)); }   // `uiListSetScrollOffset(…, 2)`
	while (LogList->GetChildrenCount() > 900) { LogList->RemoveChildAt(0); }                                   // `sub_935C9B(…, 900)`
	if (bLogAutoScroll || bAtEnd)
	{
		bLogAutoScroll = false;
		LogList->ScrollToEnd();
	}
}

// ═════════════════════════════ ЧАТ ЛАВКИ 23403 / 6313 / 6314 (волна Л4) ═════════════════════════
void UAsdaSelfStoreSubsystem::OnChatInputEnter()
{
	// `CShopDlg_OnCommand@0xDD8B70`, ветка `notifyCode == 67587 && ctrlId == 23403` (shop_trade.c:17541-17561):
	//   `sprintf(memoText, "%s", GetEditText(editMemo))`; пусто — ничего. Мат (`Chat_MatchBannedWord(g_pChatWnd, текст, 0)`) →
	//   `wsprintfA(memoText, loc 9005)`, `shopDlg_AddLogLineEllipsis(self, "", memoText, −27136)` (журнал без имени, 0xFFFF9600) и
	//   `SetText(editMemo, "")`; иначе `netSendTwoStringRequest6313(self, имя своего персонажа, текст)` и `SetText(editMemo, "")`.
	//   Звука нет (B §5). [ПОРТ] Окно событий не шлёт, пока не показано: без витрины Enter не делает ничего.
	if (!bShopOpen || !EdChat) { return; }
	const FString Txt = EdChat->GetText().ToString().Left(kChatInputLimit);
	if (Txt.IsEmpty()) { return; }
	const UAsdaChatWindowSubsystem* Chat = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr;
	if (bDebugCurseAll || (Chat && Chat->IsCursed(Txt)))
	{
		AddLogLine(FString(), Loc(L::kCurse), FColor(0xFF, 0x96, 0x00, 0xFF));   // −27136 = 0xFFFF9600
		EdChat->SetText(FText::GetEmpty());
		JournalAdd(TEXT("23403: мат — 9005 в журнал 23402 (#FF9600), поле очищено, 6313 не отправлен"));
		return;
	}
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bSent = Net && Net->SendSelfStoreChat6313(HostCharId, HostUserNum, SelfName(), Txt);
	EdChat->SetText(FText::GetEmpty());
	JournalAdd(FString::Printf(TEXT("23403: 6313 %s — хозяин %d/%d, «%s»"), bSent ? TEXT("отправлен") : TEXT("НЕ отправлен"), HostCharId, HostUserNum, *Txt));
}

bool UAsdaSelfStoreSubsystem::IsChatInputFocused() const
{
	// Эталон: пока Win32-поле 23403 в фокусе, клавиши съедает поле (менеджер окон), а `uiCanOpenWindowGate` (гейт h) считает клавиши
	//   хода «не нажатыми». UE сам этого не делает: буква, не обработанная `SEditableText::OnKeyDown`, всплывает до вьюпорта и
	//   `UPlayerInput` — поэтому признак нужен читателям (контроллер: горячие клавиши и гейт окон h).
	return bShopOpen && EdChat && EdChat->HasKeyboardFocus();
}

void UAsdaSelfStoreSubsystem::HandleChat6314(const TArray<uint8>& Body)
{
	// `shopDlg_RecvLogLineColored@0xDDB1F0` (shop_trade.c:17868-17890): `memcpy(v3, Src + 9, 224)`; номер v3[0] → цвет строки
	//   `shopDlg_AddLogLineEllipsis(self, имя (v3+4), текст (v3+24), цвет)`: 0 хозяин −256 (0xFFFFFF00), 1 −1 (белый), 2 −5450893
	//   (0xFFACD373), 3 −16728077 (0xFF00BFF3), 4 −11111239 (0xFF5674B9), 5 −618922 (0xFFF68E56); прочие номера — строки нет.
	//   Проверки «витрина открыта» у эталона нет (строка ляжет в журнал и скрытого окна; открытие витрины журнал очищает).
	if (Body.Num() < 104) { return; }
	static const FColor kChatColor[6] = {
		FColor(0xFF, 0xFF, 0x00, 0xFF), FColor(0xFF, 0xFF, 0xFF, 0xFF), FColor(0xAC, 0xD3, 0x73, 0xFF),
		FColor(0x00, 0xBF, 0xF3, 0xFF), FColor(0x56, 0x74, 0xB9, 0xFF), FColor(0xF6, 0x8E, 0x56, 0xFF) };
	const int32 Num = AsdaBytes::RdI32(Body.GetData());
	const FString Who = FAsdaLoginData::DecodeCp1251(Body.GetData() + 4, 20);
	const FString Txt = FAsdaLoginData::DecodeCp1251(Body.GetData() + 24, 80);
	if (Num < 0 || Num > 5)
	{
		JournalAdd(FString::Printf(TEXT("6314: номер %d вне 0..5 — строки нет (эталон)"), Num));
		return;
	}
	AddLogLine(Who, Txt, kChatColor[Num]);
	JournalAdd(FString::Printf(TEXT("6314: номер %d «%s : %s» цвет %s"), Num, *Who, *Txt, *kChatColor[Num].ToHex()));
}
