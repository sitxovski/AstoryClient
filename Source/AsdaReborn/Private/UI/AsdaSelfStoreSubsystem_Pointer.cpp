// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSelfStoreSubsystem_Pointer.cpp — ★ R3-c 12.11.2026 (разрез AsdaSelfStoreSubsystem.cpp, дословный перенос): подсказка лота, мышь окон продавца и витрины, поза, тексты; отладочные чтения, дампы, дев-команды.
//   Общее для частей — в AsdaSelfStoreSubsystem_Private.h; шапка и реверс — в AsdaSelfStoreSubsystem.cpp.
#include "UI/AsdaSelfStoreSubsystem.h"

#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "UI/AsdaExchangeSubsystem.h"         // ★ пачка 20, жалоба 10: рынок 13000 открыт — лавка не открывается
#include "UI/AsdaJuiWindowHandle.h"         // каркас окна JUI (Ensure + зона рамки)
#include "UI/AsdaWindowSubsystem.h"         // окна, ESC, колесо, нумпад
#include "UI/AsdaNotice.h"                  // нотисы: msgbox 1500, плашка+чат
#include "UI/AsdaItemTooltipSubsystem.h"    // подсказки предметов
#include "UI/AsdaTipPanel.h"                // EAsdaTipOwner::SelfStore
#include "UI/AsdaInventorySubsystem.h"      // ячейки, золото, закрытие сумки, записи 57 б
#include "UI/AsdaPremiumShopSubsystem.h"    // гейт 6300: элитный магазин (`helper_getField2075(g_pShopWnd)`)
#include "UI/AsdaChatWindowSubsystem.h"     // мат-фильтр `Chat_MatchBannedWord` (общая точка `IsCursed`)
#include "Npc/AsdaNpcDialogSubsystem.h"     // `g_bInventoryLocked` = открыт разговор с NPC (гейт 6302)
#include "Items/AsdaItemTableSubsystem.h"   // item_list / special_itemlist / sowell_list / booster_list
#include "Items/AsdaItemDbRows.h"
#include "World/AsdaPlayerController.h"     // поза 107/109 (`SendStallStatePose`), «мёртв»
#include "World/AsdaWorldSubsystem.h"       // имя своего персонажа
#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"                       // charId / userNum / уровень своего персонажа
#include "AsdaBytes.h"

#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"               // журнал 23402 (волна Л2)
#include "Login/AsdaLoginData.h"                // DecodeCp1251 — название/имя из 6308
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "UI/AsdaItemRecordWriter.h"   // ★ U17 (В1 дедупа №2): синтетическая запись предмета 57 б
#include "AsdaSelfStoreSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSelfStoreSubsystem*.cpp

bool UAsdaSelfStoreSubsystem::CharNameById(int32 CharId, FString& OutName) const
{
	// `CharManager_FindByCharId` (видимые персонажи, включая себя) + `thunk_getFieldAt1056` (имя). Нет в мире ⇒ false (строки нет).
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (CharId == 0) { return false; }
	if (Net && CharId == AsdaSelf::GetCharId(Net)) { OutName = SelfName(); return !OutName.IsEmpty(); }
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const int32 UserNum = WS ? WS->GetUserNumByCharId(CharId) : -1;
	if (UserNum < 0) { return false; }
	int32 ClassId = 0, Level = 0, ClanId = 0;
	return WS->GetNetPlayerInfo(UserNum, OutName, ClassId, Level, ClanId);
}

FString UAsdaSelfStoreSubsystem::ItemNameOf(int32 ItemId) const
{
	// Имя предмета для 107082: item_list (`ItemData_FindByCode`), самоцвет (`sowelTableFindRecordByCode`), спец-предмет
	//   (`itemGetTooltipById(…)->name`) — общая точка имени таблиц.
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	return IT ? IT->GetItemName(ItemId) : FString();
}

// ★ U17 (В1 дедупа №2): запись через `FAsdaItemRecordWriter` (байты те же, что у прежней ручной сборки). Прежнее тело `ShowLotTooltip` построчно (+5 = −1; +7 — прежний `WrI32(+7, −1)`,
//   чьи байты +9…+10 тут же перезаписывало количество, — то есть i16 −1 в +7).
TArray<uint8> UAsdaSelfStoreSubsystem::BuildLotTipRecord(const FLot& Lot, bool bFull)
{
	FAsdaItemRecordWriter W;
	W.Slot(-1).Second(-1);
	W.Code(Lot.ItemId);
	W.Amount(bFull ? Lot.Stock : FMath::Max(0, Lot.Stock));
	W.Durability(Lot.Durability);
	W.StackWeight(Lot.StackWeight);
	for (int32 k = 0; k < 4; ++k) { W.Sowel(k, Lot.Sowel[k]); }
	W.GemGrade(Lot.EnchantFlag);
	W.Byte25(Lot.Ench73);
	W.Byte26(Lot.Ench74);
	W.EnchantPercent((int16)Lot.EnchantPct);
	for (int32 k = 0; k < 5; ++k) { W.EnchantDword(k, Lot.Ench5[k]); }
	return W.ToArray();
}

void UAsdaSelfStoreSubsystem::ShowLotTooltip(const FLot& Lot)
{
	// `itemSlotWnd_showSlotTooltip@0xDD9E80` (item_inventory.c:55166-55256): синтетическая запись 57 б (`CSlot_Init`) из полей лота:
	//   @0 код, @9 остаток (+12), @13 прочность (+84), @14 i16 (+80), @16 4×i16 гнёзда (+44), @24/@25/@26 байты заточки (+72..+74),
	//   @27 i16 «+%» (+76), @29 5×i32 (+52). item_list: тип ≤ 4 / 8…9 / `categoryFlag == 3` ⇒ полный `ItemTooltip_Build`, иначе
	//   простой `CControl_SetState(0, код, остаток (−1 ⇒ 0))`; самоцвет — `CControl_SetState(1, …)`; спец-предмет — полный.
	UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!Tip || !IT || Lot.ItemId <= 0) { if (Tip) { Tip->HideTooltipFor(EAsdaTipOwner::SelfStore); } return; }
	bool bFull = false;
	if (const FAsdaItemRecord* Rec = IT->GetItem(Lot.ItemId))
	{
		bFull = (Rec->ItemType <= 4) || (Rec->ItemType >= 8 && Rec->ItemType <= 9) || Rec->CategoryFlag == 3;
	}
	FAsdaSpecialItemlistRow Sp;
	if (IT->GetSpecialItem(Lot.ItemId, Sp)) { bFull = true; }
	const TArray<uint8> R = BuildLotTipRecord(Lot, bFull);   // ★ U17: сборка — отдельной функцией (ниже)
	FAsdaItemInstance It;
	It.SetRaw(R.GetData(), R.Num());
	// Полный билдер — источник 1 (как запись сумки), простой — источник 2 (`CControl_SetState` = билдер «Расходников»); самоцвет
	//   выбирает свой билдер сам (так же у окна обмена О2).
	Tip->ShowItemTooltip(It, /*bSuppressCompare*/ false, EAsdaTipOwner::SelfStore, bFull ? 1 : 2);
}

// ── диагностика посетителя ──
FString UAsdaSelfStoreSubsystem::DebugCartText(int32 Cell) const
{
	return (CartTxt.IsValidIndex(Cell) && CartTxt[Cell]) ? CartTxt[Cell]->GetText().ToString() : FString();
}

int32 UAsdaSelfStoreSubsystem::DebugCartCode(int32 Cell) const
{
	const int32 L = CartCellLot.IsValidIndex(Cell) ? CartCellLot[Cell] : -1;
	return Lots.IsValidIndex(L) ? Lots[L].ItemId : -1;
}

int32 UAsdaSelfStoreSubsystem::DebugCartQty(int32 Cell) const
{
	const int32 L = CartCellLot.IsValidIndex(Cell) ? CartCellLot[Cell] : -1;
	return Lots.IsValidIndex(L) ? Lots[L].CartQty : -1;
}

bool UAsdaSelfStoreSubsystem::DebugCartSelShown() const
{
	return CartSel && CartSel->GetVisibility() != ESlateVisibility::Collapsed && CartSel->GetVisibility() != ESlateVisibility::Hidden;
}

float UAsdaSelfStoreSubsystem::DebugLotAlpha(int32 Row) const
{
	return (LotRows.IsValidIndex(Row) && LotRows[Row].Icon) ? LotRows[Row].Icon->GetColorAndOpacity().A : -1.f;
}

int32 UAsdaSelfStoreSubsystem::DebugLotStock(int32 Row) const
{
	const int32 L = LotAtRow(Row);
	return Lots.IsValidIndex(L) ? Lots[L].Stock : -999;
}

FLinearColor UAsdaSelfStoreSubsystem::DebugTotalColor() const
{
	return TxtTotal ? TxtTotal->GetColorAndOpacity().GetSpecifiedColor() : FLinearColor::Transparent;
}

TArray<FString> UAsdaSelfStoreSubsystem::DebugLogLines() const
{
	TArray<FString> Out;
	if (!LogList) { return Out; }
	for (int32 i = 0; i < LogList->GetChildrenCount(); ++i)
	{
		if (const UTextBlock* T = Cast<UTextBlock>(LogList->GetChildAt(i))) { Out.Add(T->GetText().ToString()); }
	}
	return Out;
}

bool UAsdaSelfStoreSubsystem::DebugTrashShown() const
{
	// Урна лежит в группе 23240 (`Stall_PurItemBase_grp`): видна, если видны и она, и группа (у хозяина группа скрыта).
	return BtnTrash && BtnTrash->IsVisible() && DebugListHdrShown();
}

// ═════════════════════════════ МЫШЬ И ПОДСКАЗКИ ════════════════════════════════════════════════
void UAsdaSelfStoreSubsystem::TickSellMouse()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	// Поля ввода: опрос изменений (эталон — 67591 на каждую клавишу / 67587 Enter → `sellWnd_OnPriceButton`).
	if (!bNameGuard)
	{
		for (int32 R = 0; R < kMaxLots; ++R)
		{
			if (!RowEntry.IsValidIndex(R) || RowEntry[R] < 0) { continue; }
			const FString P = SellRows[R].Price ? SellRows[R].Price->GetText().ToString() : FString();
			if (P != EditCache[R]) { EditCache[R] = P; OnRowEditText(R, /*bPrice*/ true); continue; }
			const FString Q = SellRows[R].Qty ? SellRows[R].Qty->GetText().ToString() : FString();
			if (Q != EditCache[R + kMaxLots]) { EditCache[R + kMaxLots] = Q; OnRowEditText(R, /*bPrice*/ false); }
		}
		if (EdName)
		{
			const FString Nm = EdName->GetText().ToString();
			if (Nm != NameCache)
			{
				// `ctrlEditSetTextLength(22011, 50)` — не длиннее 50 символов.
				// ★ [ПОРТ] 08.10.2026: по БАЙТАМ провода (поле 6302 — 50 б без нуля).
				const FString Cut = FAsdaLoginData::TruncateToWireLimit(Nm, kNameMaxChars, 50);
				if (Cut != Nm) { SetEdit(EdName, Cut); }
				NameCache = Cut;
			}
		}
	}
	if (WM && (WM->IsPointerBlockedByTopmost(kSellId) || WM->IsCursorOverHigherWindow(kSellId)))
	{
		if (TipKind == 0 || TipKind == 1) { TickTip(-1, -1); }
		return;
	}
	int32 OverCell = -1, OverIcon = -1, OverPrice = -1;
	for (int32 i = 0; i < kGridCells && OverCell < 0; ++i) { if (GridImg[i] && AsdaUi::IsCursorOverWidget(GridImg[i])) { OverCell = i; } }
	for (int32 R = 0; R < kMaxLots && OverCell < 0 && OverIcon < 0; ++R) { if (SellRows[R].Icon && AsdaUi::IsCursorOverWidget(SellRows[R].Icon)) { OverIcon = R; } }
	for (int32 R = 0; R < kMaxLots && OverPrice < 0; ++R) { if (SellRows[R].Price && AsdaUi::IsCursorOverWidget(SellRows[R].Price)) { OverPrice = R; } }
	// 8/9/0xA — подсказка ячейки сетки / иконки строки (`shopWndBuildSlotTooltip`).
	if (OverCell >= 0 && GridRef[OverCell].X >= 0) { TickTip(0, OverCell); }
	else if (OverIcon >= 0 && RowEntry[OverIcon] >= 0) { TickTip(1, OverIcon); }
	else if (TipKind == 0 || TipKind == 1) { TickTip(-1, -1); }
	// 0xB — нажатие ЛКМ (порядок веток эталона: ячейка, иконка строки, поле цены, поле названия).
	if (Mouse.bLmbDown)
	{
		if (OverCell >= 0) { OnGridCellClicked(OverCell, /*bRight*/ false); }
		else if (OverIcon >= 0) { OnSellIconClicked(OverIcon); }
		else if (OverPrice >= 0) { OnPriceFieldClicked(OverPrice); }
		else if (EdName && AsdaUi::IsCursorOverWidget(EdName)) { OnNameFieldClicked(); }
	}
	// 0xE — нажатие ПКМ по ячейке: весь стек.
	if (Mouse.bRmbDown && OverCell >= 0) { OnGridCellClicked(OverCell, /*bRight*/ true); }
}

void UAsdaSelfStoreSubsystem::TickShopMouse()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (WM && (WM->IsPointerBlockedByTopmost(kShopId) || WM->IsCursorOverHigherWindow(kShopId)))
	{
		if (TipKind == 2 || TipKind == 3) { TickTip(-1, -1); }
		// [ПОРТ] ревизия 10.10 МЕЛК-1: нажатие по окну выше лавки — тоже «мимо лота», пара двойного рвётся.
		if (Mouse.bLmbDown || Mouse.bRmbDown) { LotDbl.Reset(); }
		return;
	}
	int32 Over = -1;
	for (int32 R = 0; R < kMaxLots && Over < 0; ++R) { if (LotRows[R].Icon && AsdaUi::IsCursorOverWidget(LotRows[R].Icon)) { Over = R; } }
	const bool bHasLot = LotAtRow(Over) >= 0;
	// Волна Л2: ячейки корзины 23250…23255 (видимы 6; `CClass_SetField926(…, 0x400000)` — ловят клик).
	int32 OverCart = -1;
	for (int32 k = 0; k < kCartMax && Over < 0 && OverCart < 0; ++k) { if (CartImg.IsValidIndex(k) && CartImg[k] && AsdaUi::IsCursorOverWidget(CartImg[k])) { OverCart = k; } }
	const bool bHasCart = CartCellLot.IsValidIndex(OverCart) && Lots.IsValidIndex(CartCellLot[OverCart]);
	// 8 → `itemSlotWnd_showSlotTooltip` (лот 23121+10N или ячейка корзины 23250+k); 0xA — спрятать.
	if (Over >= 0 && bHasLot) { TickTip(2, Over); }
	else if (OverCart >= 0 && bHasCart) { TickTip(3, OverCart); }
	else if (TipKind == 2 || TipKind == 3) { TickTip(-1, -1); }
	// 0xB (нажатие ЛКМ) — `CShopDlg_OnCommand` shop_trade.c:17581-17585: `itemSlotWndShowSlotCtrl` (ячейка корзины → выбор),
	//   `shopDlg_AddTabItemToCart(…, 0)` (лот → в корзину; у хозяина молча), `shopDlgOnCartRemoveButton` (ячейка корзины → снять).
	// ★ [ПОРТ] ТРЕБОВАНИЕ ВЛАДЕЛЬЦА 10.10.2026: лот в корзину — ДВОЙНЫМ щелчком (эталон кладёт по одиночному 0xB/0xE).
	//   Первый щелчок по лоту ничего не кладёт (подсказка — по наведению, как была), второй по ТОМУ ЖЕ лоту той же
	//   кнопкой в пределах `GetDoubleClickTime` — `OnLotClicked` ровно один раз. ЛКМ — с нумпадом, ПКМ — весь остаток
	//   (ветки эталона сохранены). Клик по ячейке корзины (снять позицию) — одиночный, как в эталоне.
	//   Откат: `asda.Shop.DblClickToCart 0`.
	if (Mouse.bLmbDown)
	{
		if (Over >= 0) { if (LotDbl.Click(LotAtRow(Over), 0)) { OnLotClicked(Over, /*bRight*/ false); } }
		else
		{
			LotDbl.Reset();   // нажатие мимо лотов рвёт пару щелчков
			if (OverCart >= 0) { OnCartCellClicked(OverCart); }
		}
	}
	// 0xE (нажатие ПКМ) по лоту — `shopDlg_AddTabItemToCart(…, 1)`: весь остаток без нумпада.
	if (Mouse.bRmbDown && Over >= 0) { if (LotDbl.Click(LotAtRow(Over), 1)) { OnLotClicked(Over, /*bRight*/ true); } }
	// ★ [ПОРТ] Ревизия 10.10.2026, МЕЛК-1: нажатие ПКМ мимо лотов рвёт пару так же, как ЛКМ (прежде у ПКМ сброса не было:
	//   «ПКМ по лоту → ПКМ мимо → ПКМ по лоту» клало весь остаток).
	if (Mouse.bRmbDown && Over < 0) { LotDbl.Reset(); }
}

void UAsdaSelfStoreSubsystem::TickTip(int32 Kind, int32 Idx)
{
	// ★ Дедуп №2, В1-B (U18, 11.10.2026): шапка трекера (ключ «что показано», гашение при уходе курсора) — общая точка
	//   `UAsdaItemTooltipSubsystem::BeginHoverTip`; тело показа (эталонные билдеры окна) — ниже, как было.
	UAsdaItemTooltipSubsystem* Tip = UAsdaItemTooltipSubsystem::BeginHoverTip(GetGameInstance(), EAsdaTipOwner::SelfStore, TipKind, TipIdx, Kind, Idx);
	if (!Tip) { return; }
	// ★ Волна Л2: лот ЧУЖОЙ витрины (гость) и ячейка корзины — `itemSlotWnd_showSlotTooltip` по полям самого лота.
	if (Kind == 3 || (Kind == 2 && !bSpecialShop))
	{
		const int32 LIdx = (Kind == 3) ? (CartCellLot.IsValidIndex(Idx) ? CartCellLot[Idx] : -1) : LotAtRow(Idx);
		if (Lots.IsValidIndex(LIdx)) { ShowLotTooltip(Lots[LIdx]); }
		else { Tip->HideTooltipFor(EAsdaTipOwner::SelfStore); }
		return;
	}
	// Источник: ячейка сумки (сетка / строка продажи) или лот витрины (у хозяина — свой же предмет по контейнеру+слоту).
	int32 Cont = -1, Cell = -1, Code = 0;
	if (Kind == 0 && GridRef.IsValidIndex(Idx)) { Cont = GridRef[Idx].X; Cell = GridRef[Idx].Y; }
	if (Kind == 1 && RowEntry.IsValidIndex(Idx) && SellList.IsValidIndex(RowEntry[Idx])) { Cont = SellList[RowEntry[Idx]].Container; Cell = SellList[RowEntry[Idx]].Cell; }
	if (Kind == 2) { for (const FLot& Lot : Lots) { if (Lot.Row == Idx) { Cont = Lot.Container; Cell = Lot.Slot; Code = Lot.ItemId; } } }
	const FAsdaItemInstance* It = CellItem(Cont, Cell);
	if (It && Code == 0) { Code = It->ItemCode; }
	if (Code <= 0) { Tip->HideTooltipFor(EAsdaTipOwner::SelfStore); return; }
	const bool bRecordMatches = It && It->ItemCode == Code;
	// `shopWndBuildSlotTooltip@0xDD3100` / `itemSlotWnd_showSlotTooltip@0xDD9E80`: item_list — тип ≤ 4, 8…9 или
	//   `categoryFlag == 3` ⇒ полный `ItemTooltip_Build` по записи, иначе простой по коду; самоцвет — простой по коду;
	//   спец-предмет — полный по записи.
	const UAsdaItemTableSubsystem* IT = GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>();
	bool bFull = false;
	if (const FAsdaItemRecord* Rec = IT ? IT->GetItem(Code) : nullptr)
	{
		bFull = (Rec->ItemType <= 4) || (Rec->ItemType >= 8 && Rec->ItemType <= 9) || Rec->CategoryFlag == 3;
	}
	FAsdaSpecialItemlistRow Sp;
	if (IT && IT->GetSpecialItem(Code, Sp)) { bFull = true; }
	if (bFull && bRecordMatches) { Tip->ShowItemTooltip(*It, /*bSuppressCompare*/ false, EAsdaTipOwner::SelfStore, It->ContainerType == 1 ? 1 : 2); }
	else { Tip->ShowItemTooltipByCode(Code, EAsdaTipOwner::SelfStore); }
}

// ═════════════════════════════ ОБЩЕЕ ═══════════════════════════════════════════════════════════
bool UAsdaSelfStoreSubsystem::SetPose(int32 Code, const TCHAR* Why)
{
	UWorld* W = GetWorld();
	AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	const bool bOk = PC && PC->SendStallStatePose(Code, Why);
	JournalAdd(FString::Printf(TEXT("поза %d: %s (%s)"), Code, bOk ? TEXT("поставлена, 4024 отправлен") : TEXT("не поставлена"), Why));
	return bOk;
}

void UAsdaSelfStoreSubsystem::MsgBoxLoc(int32 LocId)
{
	AsdaNotice::MsgBoxLoc(GetGameInstance(), EAsdaMsgCategory::Trade, LocId);
	JournalAdd(FString::Printf(TEXT("msgbox %d"), LocId));
}

FString UAsdaSelfStoreSubsystem::Loc(int32 Id) const
{
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

FString UAsdaSelfStoreSubsystem::SelfName() const
{
	const UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	return WS ? WS->GetSelfCharName() : FString();
}

void UAsdaSelfStoreSubsystem::JournalAdd(const FString& Line)
{
	Journal.Add(Line);
	if (Journal.Num() > 400) { Journal.RemoveAt(0, Journal.Num() - 400); }
	UE_LOG(LogAsdaSelfStore, Log, TEXT("[лавка] %s"), *Line);
}

FString UAsdaSelfStoreSubsystem::DumpState() const
{
	return FString::Printf(TEXT("22000 открыто=%d 23000 открыто=%d хозяин=%d премиум=%d переоткрытие=%d флаг6302=%d вкладка=%d колесо=%d "
		"строк=%d лотов=%d хозяин %d/%d название «%s» билет код %d | гость: корзина=%d выбран=%d ждём6310=%d гостей=%d журнал=%d"),
		bSellOpen ? 1 : 0, bShopOpen ? 1 : 0, bSpecialShop ? 1 : 0, bPremium ? 1 : 0, bReopen ? 1 : 0, bCanSendOpen ? 1 : 0,
		SourceTab, ScrollPos, SellList.Num(), Lots.Num(), HostCharId, HostUserNum, *StoreTitle,
		Ticket45.Num() >= 4 ? AsdaBytes::RdI32(Ticket45.GetData()) : 0,
		Cart.Num(), SelLot, bPendingBuy ? 0 : 1, GuestCount, LogList ? LogList->GetChildrenCount() : -1);
}

FString UAsdaSelfStoreSubsystem::DumpControls() const
{
	return FString::Printf(TEXT("22000: разобрано %d из 127 (+колесо кодом), готово=%d; 23000: разобрано %d из 59 (хозяин), готово=%d"),
		Found22000, bSellReady ? 1 : 0, Found23000, bShopReady ? 1 : 0);
}

FString UAsdaSelfStoreSubsystem::DebugRowText(int32 Row, int32 Col) const
{
	if (!SellRows.IsValidIndex(Row)) { return FString(); }
	const FAsdaStallSellRowUi& R = SellRows[Row];
	switch (Col)
	{
	case 0: return R.Price ? R.Price->GetText().ToString() : FString();
	case 1: return R.Qty ? R.Qty->GetText().ToString() : FString();
	case 2: return R.Total ? R.Total->GetText().ToString() : FString();
	default: return R.IconText ? R.IconText->GetText().ToString() : FString();
	}
}

FString UAsdaSelfStoreSubsystem::DebugSumText() const { return TxtSum ? TxtSum->GetText().ToString() : FString(); }

FString UAsdaSelfStoreSubsystem::DebugGridText(int32 Cell) const
{
	return (GridTxt.IsValidIndex(Cell) && GridTxt[Cell]) ? GridTxt[Cell]->GetText().ToString() : FString();
}

float UAsdaSelfStoreSubsystem::DebugGridAlpha(int32 Cell) const
{
	return (GridImg.IsValidIndex(Cell) && GridImg[Cell]) ? GridImg[Cell]->GetColorAndOpacity().A : -1.f;
}

FString UAsdaSelfStoreSubsystem::DebugShowcaseText(int32 IdNum) const
{
	auto T = [](const UTextBlock* Tb) { return Tb ? Tb->GetText().ToString() : FString(); };
	if (IdNum == 23006) { return T(TxtShopTitle); }
	if (IdNum == 23301) { return T(TxtTotalCap); }
	if (IdNum == 23302) { return T(TxtTotal); }
	if (IdNum == 23304) { return T(TxtMoney); }
	if (IdNum == 23305) { return T(TxtMain); }
	for (int32 R = 0; R < kMaxLots; ++R)
	{
		if (IdNum == 23122 + 10 * R) { return T(LotRows[R].Price); }
		if (IdNum == 23123 + 10 * R) { return T(LotRows[R].Total); }
		if (IdNum == 23121 + 10 * R) { return T(LotRows[R].IconText); }
	}
	return FString();
}

bool UAsdaSelfStoreSubsystem::DebugListHdrShown() const
{
	return ListHdr && ListHdr->GetVisibility() != ESlateVisibility::Collapsed && ListHdr->GetVisibility() != ESlateVisibility::Hidden;
}

#if !UE_BUILD_SHIPPING
namespace {
	// `asda.Trade.Store` — печать состояния лавки (только чтение).
	FAutoConsoleCommandWithWorldAndArgs GAsdaTradeStoreDump(
		TEXT("asda.Trade.Store"),
		TEXT("asda.Trade.Store — состояние личной лавки продавца: окна 22000/23000, премиум, списки, кэш хозяина, разбор контролов."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
		{
			const UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			const UAsdaSelfStoreSubsystem* S = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
			if (!S) { UE_LOG(LogAsdaSelfStore, Log, TEXT("[лавка] подсистемы нет")); return; }
			UE_LOG(LogAsdaSelfStore, Log, TEXT("[лавка] %s"), *S->DumpState());
			UE_LOG(LogAsdaSelfStore, Log, TEXT("[лавка] %s"), *S->DumpControls());
		}));

	// `asda.Trade.StoreVisit <userNum>` — войти в лавку соседа тем же путём, что клик по его вывеске (вывески — волна Л3).
	FAutoConsoleCommandWithWorldAndArgs GAsdaTradeStoreVisit(
		TEXT("asda.Trade.StoreVisit"),
		TEXT("asda.Trade.StoreVisit <userNum> — клик по вывеске лавки соседа (гейты `tradeRequestTargetByServerIdx` + 6307). До волны Л3 — единственный вход посетителя."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			UAsdaSelfStoreSubsystem* S = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
			if (!S || Args.Num() < 1) { UE_LOG(LogAsdaSelfStore, Log, TEXT("[лавка] asda.Trade.StoreVisit <userNum>")); return; }
			S->RequestVisit(FCString::Atoi(*Args[0]), TEXT("консоль asda.Trade.StoreVisit"));
		}));
}
#endif
