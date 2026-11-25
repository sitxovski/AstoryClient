// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWarehouseSubsystem_Grid.cpp — ★ R3-c 12.11.2026 (разрез AsdaWarehouseSubsystem.cpp, дословный перенос): сетка и корзина: прокрутка, ячейки, зеркала, обновления, рамки выбора, перенос/делёж, перетаскивание.
//   Общее для частей — в AsdaWarehouseSubsystem_Private.h; шапка и реверс — в AsdaWarehouseSubsystem.cpp.
#include "UI/AsdaWarehouseSubsystem.h"

#include "UI/AsdaAnimatedPlane.h"
#include "Items/AsdaItemTableSubsystem.h"

#include "AsdaWarehouseSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaWarehouseSubsystem*.cpp

// Эталонный `uiControlSetScrollRange(list, 5, ceil(N/6))` — видимых рядов ровно 5 (`uiInitSlotGridPanel`).
static constexpr int32 kVisibleRows = 5;

namespace WareLoc {
	constexpr int32 kFeeFree      = 111924;   // 4045 «Free» на карте своей фракции

	// Отказы.
	constexpr int32 kBoundItem    = 2247;     // «Эту вещь нельзя положить на склад» → СИСЧАТ
}

// ═════════════════════════ КЛЮЧИ ОТКАТА (дефекты §15) ═════════════════════════════════════════
// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА D13. `uiWarehouseResetTab@0xE859D0` при сбросе ЛЮБОЙ вкладки
//   (a2 ≠ 0 и ≠ 9) снимает подсветку `CPlane_SetFillMode` у ЯЧЕЕК ВКЛАДКИ 0 (`aKeeperItemIcon`),
//   какую бы вкладку ни сбрасывали (ui_core.c:78261-78262). 1 = вернуть эталонное поведение.
static ASDA_DEV_CVAR(int32, CVarWareLegacyResetGrid,
	TEXT("asda.Warehouse.LegacyResetTabGrid"), 0,
	TEXT("1 = эталонный дефект D13: сброс любой вкладки снимает подсветку с сетки вкладки 0; 0 = со своей."),
	ECVF_Default);

// ★ Тот же вопрос, что у инвентаря: гейт «`tradableFlag == 0` ⇒ счётчик не рисуем» из
//   `uiSetItemCountText@0xE8A1F0`. Поле +0x98, которым порт считает `tradableFlag`, выставлено лишь
//   у 1177 записей из 58878, поэтому по умолчанию гейт ВЫКЛЮЧЕН (иначе счётчики пропадут у ~90 %
//   расходников). Ключ включает эталонное поведение для сверки.
static ASDA_DEV_CVAR(int32, CVarWareTradableCountGate,
	TEXT("asda.Warehouse.EtalonTradableCountGate"), 0,
	TEXT("1 = эталонный гейт счётчика по tradableFlag (uiSetItemCountText); 0 = счётчик рисуется всегда."),
	ECVF_Default);

// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): размер контейнера списка «хранить» по категории — расходники 60, сумка —
//   ДОСТУПНАЯ ёмкость (60, либо 30 при `asda.Inven.ShellX2 0` — тогда, как у эталона, прокрутки у категории 1 нет).
static int32 WareKeepListSize(const UGameInstance* GI, bool bShell)
{
	if (!bShell) { return UAsdaInventorySubsystem::kSpendSlots; }
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	return Inv ? Inv->GetContainerSize(1) : UAsdaInventorySubsystem::kShellSlotsLegacy;
}

int32 UAsdaWarehouseSubsystem::GetMaxScrollRow() const
{
	if (Mode != kModeKeep) { return 0; }
	// Эталон: `uiControlSetScrollRange(list, 5, ceil(N/6))`, N — размер КОНТЕЙНЕРА (60), не числа
	//   занятых ячеек. Максимальный ряд = всего рядов − видимых.
	//   ★ [ПОРТ] 29.10.2026 (инвентарь ×2): эталон у категории «снаряжение» (сумка 30) возвращал 0; при сумке 60 видно
	//   30 из 60 — диапазон считается по размеру контейнера категории.
	const int32 Total = FMath::DivideAndRoundUp(WareKeepListSize(GetGameInstance(), Category == kCatShell), kGridCols);
	return FMath::Max(0, Total - kVisibleRows);
}

void UAsdaWarehouseSubsystem::SetScrollRow(int32 Row)
{
	const int32 Clamped = FMath::Clamp(Row, 0, GetMaxScrollRow());
	if (Clamped == ScrollRow) { return; }
	ScrollRow = Clamped;
	RefreshGrid();
	RefreshSourceDim();
}

// ═════════════════════════ МОДЕЛЬ ═════════════════════════════════════════════════════════════
int32 UAsdaWarehouseSubsystem::GroupOfCtrl(int32 CtrlId)
{
	// `itemCodeToGroupIndexA@0xE896C0` — 1:1, включая корзину (9).
	for (int32 t = 0; t < kStoreTabs; ++t)
	{
		const int32 Base = WareIds::kTrayBase[t];
		if (CtrlId >= Base && CtrlId <= Base + kWareBagCells - 1) { return t; }
	}
	if (CtrlId >= WareIds::kBasketBase && CtrlId <= WareIds::kBasketBase + kBasketCells - 1) { return 9; }
	return -1;
}

int32 UAsdaWarehouseSubsystem::CellOfCtrl(int32 CtrlId)
{
	// `itemCodeCategorySlotIndex@0xE89850` — `idnum − база группы`.
	const int32 G = GroupOfCtrl(CtrlId);
	if (G < 0) { return -1; }
	if (G == 9) { return CtrlId - WareIds::kBasketBase; }
	return CtrlId - WareIds::kTrayBase[G];
}

int32 UAsdaWarehouseSubsystem::CtrlOfGroupCell(int32 Group, int32 Cell)
{
	if (Group == 9) { return (Cell >= 0 && Cell < kBasketCells) ? WareIds::kBasketBase + Cell : -1; }
	if (Group < 0 || Group >= kStoreTabs || Cell < 0 || Cell >= kWareBagCells) { return -1; }
	return WareIds::kTrayBase[Group] + Cell;
}

// `warehouseGetSlotPtr@0xE891A0`: в режиме «хранить» сетка вкладки 0 показывает УПЛОТНЁННЫЙ
//   ИНВЕНТАРЬ со сдвигом `6 · nScrollRow`; во всех прочих случаях — массив склада.
FAsdaItemInstance* UAsdaWarehouseSubsystem::SlotPtrByCtrl(int32 CtrlId)
{
	const int32 G = GroupOfCtrl(CtrlId);
	const int32 C = CellOfCtrl(CtrlId);
	if (G < 0 || C < 0) { return nullptr; }
	if (G == 9) { return Basket.IsValidIndex(C) ? &Basket[C] : nullptr; }
	if (Mode == kModeKeep && G == 0)
	{
		const int32 Idx = C + kGridCols * ScrollRow;
		return InvList.IsValidIndex(Idx) ? &InvList[Idx] : nullptr;
	}
	const int32 Abs = G * kWareBagCells + C;
	return Store.IsValidIndex(Abs) ? &Store[Abs] : nullptr;
}

const FAsdaItemInstance* UAsdaWarehouseSubsystem::SlotPtrByCtrl(int32 CtrlId) const
{
	return const_cast<UAsdaWarehouseSubsystem*>(this)->SlotPtrByCtrl(CtrlId);
}

// `inventoryResolveSlotPtr@0xE89410` в применении к записи КОРЗИНЫ: источник опознаётся полями
//   самой записи (+4 контейнер, +5 слот).
FAsdaItemInstance* UAsdaWarehouseSubsystem::ResolveBasketSource(const FAsdaItemInstance& B)
{
	if (B.IsEmpty()) { return nullptr; }
	if (B.ContainerType == 5)
	{
		return Store.IsValidIndex(B.SlotIndex) ? &Store[B.SlotIndex] : nullptr;
	}
	// Контейнеры 1/2 — ищем запись в УПЛОТНЁННОМ списке по её настоящему слоту
	//   (эталонные `inventoryGetNthOccupiedBagSlot` / `invSlotToVisibleItemAddr` делают то же самое).
	for (FAsdaItemInstance& It : InvList)
	{
		if (!It.IsEmpty() && It.ContainerType == B.ContainerType && It.SlotIndex == B.SlotIndex) { return &It; }
	}
	return nullptr;
}

// `itemListIsItemNotPresent@0xE8CBF0`: сверка по КОДУ и полю +5 (слот).
bool UAsdaWarehouseSubsystem::IsNotInBasket(const FAsdaItemInstance& It) const
{
	if (It.IsEmpty()) { return false; }
	for (const FAsdaItemInstance& B : Basket)
	{
		if (B.ItemCode == It.ItemCode && B.SlotIndex == It.SlotIndex) { return false; }
	}
	return true;
}

void UAsdaWarehouseSubsystem::SyncMirrors()
{
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	// ── Склад: локальная копия контейнера 5 (эталонный `aSlots[0…269]`) ──
	for (int32 i = 0; i < kStoreSlots; ++i)
	{
		const FAsdaItemInstance* Src = Inv ? Inv->GetCell(5, i) : nullptr;
		if (Src) { Store[i] = *Src; }
		else     { Store[i].Clear(); }
	}
	RebuildInvList();
	// ── Снять с источников то, что уже лежит в корзине (эталон делает это в `quickslotPlaceItem`
	//    сразу при укладке; у нас копии пересобираются заново, значит вычитание надо повторить). ──
	for (const FAsdaItemInstance& B : Basket)
	{
		if (B.IsEmpty()) { continue; }
		if (FAsdaItemInstance* Src = ResolveBasketSource(B))
		{
			Src->SetAmount(FMath::Max(0, Src->Amount - B.Amount));
		}
	}
}

// `inventory_CompactSlots@0xE8D7F0` (item_inventory.c:58931): уплотнить контейнер без дыр,
//   ★ ЗОЛОТО 20551 из категории 0 ВЫБРАСЫВАЕТСЯ.
void UAsdaWarehouseSubsystem::RebuildInvList()
{
	const int32 Cont = (Category == kCatShell) ? 1 : 2;
	const int32 Size = (Category == kCatShell)
		? UAsdaInventorySubsystem::kShellSlots
		: UAsdaInventorySubsystem::kSpendSlots;
	InvList.SetNum(Size);
	for (FAsdaItemInstance& It : InvList) { It.Clear(); }
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return; }
	int32 Out = 0;
	for (int32 c = 0; c < Size && Out < Size; ++c)
	{
		const FAsdaItemInstance* It = Inv->GetCell(Cont, c);
		if (!It || It->IsEmpty()) { continue; }
		if (It->ItemCode == UAsdaInventorySubsystem::kGoldItemCode) { continue; }   // ★ золото не переносится
		InvList[Out++] = *It;
	}
}

// ═════════════════════════ РЕНДЕР ═════════════════════════════════════════════════════════════
void UAsdaWarehouseSubsystem::RefreshAll()
{
	RefreshGrid();
	RefreshBasket();
	for (int32 k = 0; k < kBags; ++k) { if (bBagOpen[k]) { RefreshBag(k + 1); } }
	RefreshStatusTexts();
	RefreshFeeText();
	RefreshSourceDim();
	ApplyBagButtonVisibility();
}

void UAsdaWarehouseSubsystem::RefreshGrid()
{
	if (GridIcons) { GridIcons->Refresh(); }
	for (int32 c = 0; c < kGridCells; ++c)
	{
		if (!GridUi.IsValidIndex(c) || !GridUi[c].Label) { continue; }
		const FAsdaItemInstance* It = SlotPtrByCtrl(WareIds::kTrayBase[0] + c);
		uint32 Argb = 0;
		const FString S = (It && !It->IsEmpty()) ? CellText(*It, Argb) : FString();
		GridUi[c].Label->SetText(FText::FromString(S));
		if (Argb != 0) { GridUi[c].Label->SetColorAndOpacity(AsdaUi::SrgbU32ToLinear(Argb)); }
	}
}

void UAsdaWarehouseSubsystem::RefreshBasket()
{
	if (BasketIcons) { BasketIcons->Refresh(); }
	for (int32 c = 0; c < kBasketCells; ++c)
	{
		if (!BasketUi.IsValidIndex(c) || !BasketUi[c].Label) { continue; }
		uint32 Argb = 0;
		const FString S = Basket[c].IsEmpty() ? FString() : CellText(Basket[c], Argb);
		BasketUi[c].Label->SetText(FText::FromString(S));
		if (Argb != 0) { BasketUi[c].Label->SetColorAndOpacity(AsdaUi::SrgbU32ToLinear(Argb)); }
	}
}

void UAsdaWarehouseSubsystem::RefreshBag(int32 Tab)
{
	if (Tab < 1 || Tab > kBags) { return; }
	FAsdaWareBagUi& B = Bags[Tab - 1];
	if (B.Grid) { B.Grid->Refresh(); }
	for (int32 c = 0; c < kWareBagCells; ++c)
	{
		const int32 Ui = (Tab - 1) * kWareBagCells + c;
		if (!BagCellUi.IsValidIndex(Ui) || !BagCellUi[Ui].Label) { continue; }
		const int32 Abs = Tab * kWareBagCells + c;
		uint32 Argb = 0;
		const FString S = (Store.IsValidIndex(Abs) && !Store[Abs].IsEmpty()) ? CellText(Store[Abs], Argb) : FString();
		BagCellUi[Ui].Label->SetText(FText::FromString(S));
		if (Argb != 0) { BagCellUi[Ui].Label->SetColorAndOpacity(AsdaUi::SrgbU32ToLinear(Argb)); }
	}
}

// `uiUpdateStatusTexts@0xE8D090` (ui_core.c:78854-78862): `wsprintfA("%3d/%3d", занято, всего)`
//   в 4003 и деньги игрока в 4047.
void UAsdaWarehouseSubsystem::RefreshStatusTexts()
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (TxtCap)
	{
		const int32 Total = (Net && Net->SelfStoreSlotsMax > 0) ? Net->SelfStoreSlotsMax : 0;
		// Занятость: сервер шлёт её в блоке 1017/5456 (`CCharacter+0x458`). Пока не приходила —
		//   считаем по локальной копии, чтобы поле не врало нулём.
		int32 Used = (Net && Net->SelfStoreSlotsUsed >= 0) ? Net->SelfStoreSlotsUsed : -1;
		if (Used < 0)
		{
			Used = 0;
			for (const FAsdaItemInstance& It : Store) { if (!It.IsEmpty()) { ++Used; } }
		}
		TxtCap->SetText(FText::FromString(FString::Printf(TEXT("%3d/%3d"), Used, Total)));
	}
	if (TxtGold)
	{
		const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		TxtGold->SetText(FText::FromString(AsdaUi::FormatThousands(Inv ? Inv->GetGold() : 0)));
	}
}

// `CStore_UpdateDepositFeeText@0xE8BA10` (item_inventory.c:58868-58890):
//   `for (i<5) if (корзина[i].код > 0) v5 += 30;` → 4045; на карте своей фракции — нотис 111924 «Free».
void UAsdaWarehouseSubsystem::RefreshFeeText()
{
	if (!TxtFee) { return; }
	const int32 Fee = kFeePerItem * BasketOccupied();
	TxtFee->SetText(FText::FromString(FString::FromInt(Fee)));
	if (IsOwnFactionMap())
	{
		const FString S = Loc(WareLoc::kFeeFree);
		if (!S.IsEmpty()) { TxtFee->SetText(FText::FromString(S)); }
	}
}

// `uiSkillBarRefreshHighlight@0xE8DEF0` (миснейм, §14): затемнить `CControl_SetColor4(cell, 0x66)`
//   ячейки-ИСТОЧНИКИ предметов, лежащих в корзине; остальным вернуть 0xFF.
void UAsdaWarehouseSubsystem::RefreshSourceDim()
{
	const FLinearColor Normal(1.f, 1.f, 1.f, 1.f);
	const FLinearColor Dim(1.f, 1.f, 1.f, 0x66 / 255.f);
	auto Apply = [&](UImage* Icon, const FAsdaItemInstance* It)
	{
		if (!Icon) { return; }
		const bool bDim = It && !It->IsEmpty() && !IsNotInBasket(*It);
		Icon->SetColorAndOpacity(bDim ? Dim : Normal);
	};
	for (int32 c = 0; c < kGridCells; ++c)
	{
		Apply(GridUi[c].Icon.Get(), SlotPtrByCtrl(WareIds::kTrayBase[0] + c));
	}
	// ★ D13: подсветку снимаем/ставим у ВСЕХ ОТКРЫТЫХ вкладок, а не всегда у вкладки 0.
	//   Эталонное поведение возвращает `asda.Warehouse.LegacyResetTabGrid 1`.
	if (CVarWareLegacyResetGrid.GetValueOnGameThread() == 0)
	{
		for (int32 k = 0; k < kBags; ++k)
		{
			if (!bBagOpen[k]) { continue; }
			for (int32 c = 0; c < kWareBagCells; ++c)
			{
				const int32 Abs = (k + 1) * kWareBagCells + c;
				Apply(BagCellUi[k * kWareBagCells + c].Icon.Get(), Store.IsValidIndex(Abs) ? &Store[Abs] : nullptr);
			}
		}
	}
}

void UAsdaWarehouseSubsystem::LayoutScrollBar()
{
	if (!ScrollBar.IsBuilt()) { return; }
	if (Mode != kModeKeep || GetMaxScrollRow() == 0) { ScrollBar.Hide(); return; }   // ★ 29.10.2026: и сумка 60 (категория 1)
	if (!GridUi.IsValidIndex(0) || !GridUi[0].Icon) { ScrollBar.Hide(); return; }
	// Полоса стоит у ПРАВОГО края области списка 4189 (её левый-верх = левый-верх первой ячейки).
	const FVector2D Origin = AsdaUi::CanvasRootPos(GridUi[0].Icon.Get()) + FVector2D(kListWidth, 0.f);
	const int32 MaxRow = GetMaxScrollRow();
	const int32 TotalRows = FMath::DivideAndRoundUp(WareKeepListSize(GetGameInstance(), Category == kCatShell), kGridCols);   // ★ 29.10.2026
	// ★ Волна 14 (МЕЛК-1): целые аргументы эталона `uiControlSetScrollRange(list, 5, ceil(N/6))`.
	ScrollBar.Layout(Origin, kListHeight, /*Scale*/1.f, kVisibleRows, TotalRows, MaxRow, ScrollRow);
}

void UAsdaWarehouseSubsystem::HideSelFrames()
{
	if (GridSel) { GridSel->SetVisibility(ESlateVisibility::Collapsed); }
	for (FAsdaWareBagUi& B : Bags) { if (B.Sel) { B.Sel->SetVisibility(ESlateVisibility::Collapsed); } }
}

// `wndUpdateSlotControlVisibility@0xE8A3E0` (событие 0xC): двигать рамку выделения —
//   4051 для вкладки 0, `WAREBAGn_SEL` для 1…8; для корзины (группа 9) НИЧЕГО.
void UAsdaWarehouseSubsystem::MoveSelFrame(int32 Group, const UWidget* OverIcon)
{
	HideSelFrames();
	if (!OverIcon || Group < 0 || Group > 8) { return; }
	UWidget* Sel = (Group == 0) ? Cast<UWidget>(GridSel.Get()) : Cast<UWidget>(Bags[Group - 1].Sel.Get());
	if (!Sel) { return; }
	// ★ Дедуп №2, В2-B (U19): общая точка рамки выбора (ячейки и рамка в РАЗНЫХ группах — позиция в координатах родителя
	//   рамки). Эталон `wndUpdateSlotControlVisibility` — только `SetField74_75` + `SetVisible(1)`, кадры не перезапускает.
	AsdaUi::PlaceFrameOver(Sel, OverIcon, /*bRestartAnim*/false);
}

// `uiSetItemCountText@0xE8A1F0` (ui_core.c:78628), §6 отчёта.
FString UAsdaWarehouseSubsystem::CellText(const FAsdaItemInstance& It, uint32& OutColorArgb) const
{
	OutColorArgb = 0;
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	FString S;
	const int32 Cls = EtalonClassify(It.ItemCode);
	if (Cls == 2)
	{
		// Спец-предмет второй таблицы: количество показывается, только если «число применений» > 1.
		//   В порте это поле не разобрано (записи `special_itemlist` подключены не полностью),
		//   поэтому берём то же правило по количеству записи — оно совпадает для всех живых данных.
		if (It.Amount > 1) { S = FString::FromInt(It.Amount); }
	}
	else if (Cls == 1)
	{
		// Самоцвет — количества у него нет (эталон принудительно обнуляет его в `MoveOrSplit`).
	}
	else
	{
		const FAsdaItemRecord* Rec = IT ? IT->GetItem(It.ItemCode) : nullptr;
		if (Rec && Rec->ItemSubType == 32)
		{
			// Камень души — количество НЕ показывать.
		}
		else if (Rec)
		{
			const bool bTradableOk = (CVarWareTradableCountGate.GetValueOnGameThread() == 0) || (Rec->TradableFlag != 0);
			if (bTradableOk && It.ItemCode != UAsdaInventorySubsystem::kGoldItemCode)
			{
				if (It.Amount > 0) { S = FString::FromInt(It.Amount); }
			}
			else if (Rec->FlagWord176 != 0)
			{
				S = FString::FromInt(Rec->FlagWord176);
			}
		}
	}
	// ★ Если поле записи +24 не ноль, подпись ПЕРЕЗАПИСЫВАЕТСЯ на «+N%» цветом −256 (жёлтый).
	if (It.GemGrade != 0)
	{
		S = FString::Printf(TEXT("+%d%%"), It.EnchantPercent);
		OutColorArgb = 0xFFFFFF00u;
	}
	return S;
}

// ═════════════════════════ КОРЗИНА ════════════════════════════════════════════════════════════
int32 UAsdaWarehouseSubsystem::BasketOccupied() const
{
	int32 N = 0;
	for (const FAsdaItemInstance& B : Basket) { if (!B.IsEmpty()) { ++N; } }
	return N;
}

int32 UAsdaWarehouseSubsystem::EtalonClassify(int32 Code) const
{
	// `itemCodeClassify@0xE899B0` (item_inventory.c:58176): 1 самоцвет, 2 спец-предмет,
	//   3 надеваемое (itemType ≤ 4 либо 8…9), 0 всё прочее.
	//   ⚠ Нумерация НЕ совпадает с `UAsdaItemTableSubsystem::GetItemCategory` (там 3 надеваемое,
	//   1 прочее, 2 самоцвет, 4 спец) — поэтому считаем здесь, а не переиспользуем.
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return 0; }
	if (IT->IsSowelCode(Code)) { return 1; }
	if (const FAsdaItemRecord* Rec = IT->GetItem(Code))
	{
		const int32 T = Rec->ItemType;
		return (T <= 4 || (T >= 8 && T <= 9)) ? 3 : 0;
	}
	if (IT->IsSpecialItemCode(Code)) { return 2; }
	return 0;
}

// `itemIsNotStackableType32Slot3`: «привязанный камень души» = `itemSubType == 32 && запись[50] == 3`.
bool UAsdaWarehouseSubsystem::IsNotBoundSoulStone(const FAsdaItemInstance& It) const
{
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(It.ItemCode) : nullptr;
	if (!Rec || Rec->ItemSubType != 32) { return true; }
	const uint8 Byte50 = It.Raw.IsValidIndex(50) ? It.Raw[50] : 0;
	return Byte50 != 3;
}

// Гейт нумпада, §8.1 (`inventory_OnItemSlotMoveOrSplit`, item_inventory.c:58668-58707).
bool UAsdaWarehouseSubsystem::NeedsQuantityDialog(const FAsdaItemInstance& It, int32& OutQty) const
{
	OutQty = It.Amount;
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(It.ItemCode) : nullptr;
	// Камень души — вопроса нет (эталон: `v4 = 1; v7 = 0;`).
	if (Rec && Rec->ItemSubType == 32) { return false; }
	switch (EtalonClassify(It.ItemCode))
	{
	case 0:
	case 3:
		if (!Rec) { return true; }                       // эталон: `if (pItemRecord)` — иначе `v7` остаётся 1
		if (Rec->TradableFlag == 0) { return false; }
		if (Rec->FlagWord176 != 0 || OutQty == 1) { return false; }
		return true;
	case 1:
		OutQty = 0;                                      // эталон принудительно обнуляет количество
		return false;
	case 2:
		return false;
	default:
		return true;
	}
}

// `inventory_OnItemSlotMoveOrSplit@0xE8B240` — общий вход двойного щелчка и броска в корзину.
void UAsdaWarehouseSubsystem::MoveOrSplit(int32 SrcCtrlId, int32 DstCtrlId)
{
	const int32 G = GroupOfCtrl(SrcCtrlId);
	if (G == -1) { return; }
	if (G == 9) { BasketReturn(SrcCtrlId); return; }     // клик по КОРЗИНЕ = вернуть предмет

	const FAsdaItemInstance* Src = SlotPtrByCtrl(SrcCtrlId);
	if (!Src || Src->IsEmpty()) { return; }
	if (!IsNotInBasket(*Src)) { return; }                // предмет уже в корзине
	if (!IsNotBoundSoulStone(*Src))
	{
		NoticeChat(WareLoc::kBoundItem);                 // нотис 2247 в СИСТЕМНЫЙ ЧАТ
		return;
	}
	int32 Qty = 0;
	if (!NeedsQuantityDialog(*Src, Qty))
	{
		BasketPut(SrcCtrlId, DstCtrlId, Qty);
		return;
	}
	// ★ НУМПАД, эталонный режим 4 (`warehouseAskQuantityForBasket@0xE8CCE0`).
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { BasketPut(SrcCtrlId, DstCtrlId, Qty); return; }
	NumPadSrcCtrl = SrcCtrlId;
	NumPadDstCtrl = DstCtrlId;
	WM->ShowNumPad(Qty, [this](bool bOk, int32 Amount)
	{
		// ОК-колбэк режима 4 → `warehouseBasket_ConfirmQty@0xE8CC90`.
		// ★ ОТМЕНА (эталонное −1) НЕ ДЕЛАЕТ НИЧЕГО: `if (a2 != -1) …` (дефект D5 — на этой ветке
		//   возвращается неинициализированное значение, наблюдаемого эффекта нет).
		const int32 Src2 = NumPadSrcCtrl, Dst2 = NumPadDstCtrl;
		NumPadSrcCtrl = -1; NumPadDstCtrl = -1;
		if (!bOk || Amount <= 0) { return; }
		BasketPut(Src2, Dst2, Amount);
	}, /*bCenterOnScreen*/ false);
}

// `quickslotPlaceItem@0xE8B4B0` (правильное имя `warehouseBasket_PutItem`), item_inventory.c:58775.
void UAsdaWarehouseSubsystem::BasketPut(int32 SrcCtrlId, int32 DstCtrlId, int32 Qty)
{
	FAsdaItemInstance* Src = SlotPtrByCtrl(SrcCtrlId);
	if (!Src || Src->IsEmpty() || Qty <= 0) { return; }
	if (!IsNotInBasket(*Src)) { return; }

	// Куда класть: явная ячейка приёмника, иначе первая свободная; если свободных нет —
	//   последняя тронутая ячейка корзины, а при её отсутствии — эталонная ячейка 4.
	int32 Dst = -1;
	if (DstCtrlId != -1 && GroupOfCtrl(DstCtrlId) == 9) { Dst = CellOfCtrl(DstCtrlId); }
	if (Dst < 0)
	{
		int32 i = 0;
		for (; i < kBasketCells && !Basket[i].IsEmpty(); ++i) {}
		if (i == kBasketCells)
		{
			Dst = (LastBasketCtrl > -1) ? CellOfCtrl(LastBasketCtrl) : 4;
		}
		else { Dst = i; }
	}
	if (!Basket.IsValidIndex(Dst)) { return; }

	// Снять количество с источника.
	Src->SetAmount(FMath::Max(0, Src->Amount - Qty));
	// Целевая ячейка корзины занята — ВЕРНУТЬ её содержимое владельцу.
	if (!Basket[Dst].IsEmpty())
	{
		if (FAsdaItemInstance* Owner = ResolveBasketSource(Basket[Dst]))
		{
			Owner->SetAmount(Owner->Amount + Basket[Dst].Amount);
		}
		Basket[Dst].Clear();
	}
	// Скопировать 57-байтовую запись и положить в поле +9 «сколько переносим»
	//   (эталон: `memcpy(корзина, источник, 0x39)`, затем `корзина + 22041 = a4`, 22041 − 22032 = 9).
	Basket[Dst] = *Src;
	Basket[Dst].SetAmount(Qty);
	LastBasketCtrl = WareIds::kBasketBase + Dst;

	RefreshGrid();
	RefreshBasket();
	for (int32 k = 0; k < kBags; ++k) { if (bBagOpen[k]) { RefreshBag(k + 1); } }
	RefreshSourceDim();
	// Эталон пересчитывает комиссию ТОЛЬКО в режиме 0 (`if (!*(this + 345))`).
	if (Mode == kModeTake) { RefreshFeeText(); }
	UE_LOG(LogAsdaWare, Verbose, TEXT("[склад] в корзину[%d]: код %d × %d (из контрола %d)"),
		Dst, Basket[Dst].ItemCode, Qty, SrcCtrlId);
}

// `storageUI_PlaceItemInSlot@0xE8B780` (item_inventory.c:58799): вернуть предмет из корзины.
void UAsdaWarehouseSubsystem::BasketReturn(int32 BasketCtrlId)
{
	const int32 C = CellOfCtrl(BasketCtrlId);
	if (!Basket.IsValidIndex(C) || Basket[C].IsEmpty()) { return; }
	if (FAsdaItemInstance* Owner = ResolveBasketSource(Basket[C]))
	{
		Owner->SetAmount(Owner->Amount + Basket[C].Amount);
	}
	Basket[C].Clear();
	if (LastBasketCtrl == BasketCtrlId) { LastBasketCtrl = -1; }
	RefreshGrid();
	RefreshBasket();
	for (int32 k = 0; k < kBags; ++k) { if (bBagOpen[k]) { RefreshBag(k + 1); } }
	RefreshSourceDim();
	if (Mode == kModeTake) { RefreshFeeText(); }
}

// `uiWarehouseResetTab(this, 9)` — вернуть ВСЮ корзину владельцам.
void UAsdaWarehouseSubsystem::BasketReturnAll()
{
	for (int32 c = 0; c < kBasketCells; ++c)
	{
		if (Basket[c].IsEmpty()) { continue; }
		if (FAsdaItemInstance* Owner = ResolveBasketSource(Basket[c]))
		{
			Owner->SetAmount(Owner->Amount + Basket[c].Amount);
		}
		Basket[c].Clear();
	}
	LastBasketCtrl = -1;
	if (bControlsReady)
	{
		RefreshGrid(); RefreshBasket(); RefreshSourceDim();
		if (Mode == kModeTake) { RefreshFeeText(); }
	}
}

// ═════════════════════════ ВВОД ═══════════════════════════════════════════════════════════════
// Ячейка под курсором — по ЖИВОЙ ГЕОМЕТРИИ (закон `jui-cell-chain-nesting`), а не по слоту.
int32 UAsdaWarehouseSubsystem::HitTestCell(bool bRequireItem, const UWidget** OutIcon) const
{
	if (OutIcon) { *OutIcon = nullptr; }
	// Корзина — первой: её ячейки лежат отдельной группой и не перекрываются сеткой.
	for (int32 c = 0; c < kBasketCells; ++c)
	{
		if (!BasketUi[c].Icon || !AsdaUi::IsCursorOverWidget(BasketUi[c].Icon)) { continue; }
		if (bRequireItem && Basket[c].IsEmpty()) { return -1; }
		if (OutIcon) { *OutIcon = BasketUi[c].Icon.Get(); }
		return WareIds::kBasketBase + c;
	}
	for (int32 k = 0; k < kBags; ++k)
	{
		if (!bBagOpen[k]) { continue; }
		for (int32 c = 0; c < kWareBagCells; ++c)
		{
			const int32 Ui = k * kWareBagCells + c;
			if (!BagCellUi[Ui].Icon || !AsdaUi::IsCursorOverWidget(BagCellUi[Ui].Icon)) { continue; }
			const int32 Abs = (k + 1) * kWareBagCells + c;
			if (bRequireItem && (!Store.IsValidIndex(Abs) || Store[Abs].IsEmpty())) { return -1; }
			if (OutIcon) { *OutIcon = BagCellUi[Ui].Icon.Get(); }
			return WareIds::kTrayBase[k + 1] + c;
		}
	}
	for (int32 c = 0; c < kGridCells; ++c)
	{
		if (!GridUi[c].Icon || !AsdaUi::IsCursorOverWidget(GridUi[c].Icon)) { continue; }
		const int32 Ctrl = WareIds::kTrayBase[0] + c;
		if (bRequireItem)
		{
			const FAsdaItemInstance* It = SlotPtrByCtrl(Ctrl);
			if (!It || It->IsEmpty()) { return -1; }
		}
		if (OutIcon) { *OutIcon = GridUi[c].Icon.Get(); }
		return Ctrl;
	}
	return -1;
}

// §7 — весь конвейер перетаскивания (события 0xB → 5 → 6 → 0xC → 7).
void UAsdaWarehouseSubsystem::TickDrag()
{
	if (!bControlsReady || !Widget) { return; }

	// ── Нажатие: взводим захват над НЕПУСТОЙ ячейкой (эталонное событие 0xB + возврат 2 у 5) ──
	if (Mouse.bLmbDown && !DragGhost.IsActive() && !DragArm.IsArmed())
	{
		const UWidget* Icon = nullptr;
		const int32 Ctrl = HitTestCell(/*bRequireItem*/true, &Icon);
		if (Ctrl < 0) { return; }
		const FAsdaItemInstance* It = SlotPtrByCtrl(Ctrl);
		if (!It || It->IsEmpty()) { return; }
		// `warehouseValidateSlotForAction@0xE87FF0`: привязанный камень души тащить нельзя —
		//   нотис 2247 в системный чат.
		if (!IsNotBoundSoulStone(*It)) { NoticeChat(WareLoc::kBoundItem); return; }
		if (GroupOfCtrl(Ctrl) == 9) { LastBasketCtrl = Ctrl; }
		DragArm.Arm(/*KeyA*/Ctrl, /*KeyB*/-1, Mouse.Cursor);
		return;
	}

	// ── Порог пройден — поднимаем призрак ──
	if (DragArm.IsArmed() && Mouse.bLmb && DragArm.ShouldStart(Mouse.Cursor))
	{
		DragArm.MarkStarted();
		BeginDrag(DragArm.KeyA);
	}

	// ── Кадр перетаскивания: призрак за курсором + рамка выделения на ячейке под ним ──
	if (DragGhost.IsActive())
	{
		DragGhost.Tick(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
		const UWidget* Icon = nullptr;
		const int32 Over = HitTestCell(/*bRequireItem*/false, &Icon);
		MoveSelFrame(Over >= 0 ? GroupOfCtrl(Over) : -1, Icon);
	}

	// ── Отпускание ──
	if (Mouse.bLmbUp)
	{
		if (DragGhost.IsActive()) { DropDrag(); }
		else if (DragArm.IsArmed())
		{
			// Порог не пройден ⇒ это КЛИК. Двойной щелчок ловим по времени: эталон различает
			//   события 0xB (нажатие) и 0xD/0xF (двойной), у нас источник один — мышь.
			const int32 Ctrl = DragArm.KeyA;
			DragArm.Disarm();
			// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U1): общий `FAsdaDblClick` — порог Windows `GetDoubleClickTime`, реальное время (было — литерал 0,35 с;
			//   прежний порог — `LegacySec`, ключ `asda.UI.DblClickLegacy 1`).
			if (CellDbl.Click(Ctrl)) { OnCellDoubleClick(Ctrl); }
			return;
		}
		DragArm.Disarm();
	}
}

// ★ U1 (В2-A дедупа №2): копия распознавателя с прежним порогом окна — для самотеста (описание — у объявления).
FAsdaDblClick UAsdaWarehouseSubsystem::DebugDblClickProbe() const
{
	return CellDbl;
}

void UAsdaWarehouseSubsystem::BeginDrag(int32 CtrlId)
{
	DragCtrl = CtrlId;
	// Иконку берём у того же контрола (по группе/ячейке), чтобы браш призрака совпал с ячейкой.
	const int32 G = GroupOfCtrl(CtrlId), C = CellOfCtrl(CtrlId);
	UImage* Src = nullptr;
	if (G == 9 && BasketUi.IsValidIndex(C))    { Src = BasketUi[C].Icon.Get(); }
	else if (G == 0 && GridUi.IsValidIndex(C)) { Src = GridUi[C].Icon.Get(); }
	else if (G >= 1 && G <= kBags && BagCellUi.IsValidIndex((G - 1) * kWareBagCells + C))
	{
		Src = BagCellUi[(G - 1) * kWareBagCells + C].Icon.Get();
	}
	if (!Src || !Widget || !Widget->WidgetTree) { return; }
	if (UCanvasPanel* Root = Cast<UCanvasPanel>(Widget->WidgetTree->RootWidget))
	{
		DragGhost.Begin(Widget, Root, Src->GetBrush(), FVector2D((float)kCellPx, (float)kCellPx),
			/*Opacity*/ 0.75f, FVector2D(-16.f, -16.f), TEXT("WARE_DragGhost"),
			GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
	}
	// Эталон на событии 6 переводит ячейку-источник в `FillMode 5` и стирает подпись стопки.
	Src->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.35f));
	if (G == 9 && BasketUi.IsValidIndex(C) && BasketUi[C].Label)   { BasketUi[C].Label->SetText(FText::GetEmpty()); }
	else if (G == 0 && GridUi.IsValidIndex(C) && GridUi[C].Label)  { GridUi[C].Label->SetText(FText::GetEmpty()); }
	// Подсказку на время перетаскивания гасим (эталон: `CControl_KillFocus(&g_ItemTooltip)`).
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::Warehouse);
	}
	TipCtrl = -1;
}

// `invHandleSlotDrop@0xE88220` (item_inventory.c:57825) — таблица §7.
void UAsdaWarehouseSubsystem::DropDrag()
{
	DragGhost.Cancel();
	HideSelFrames();
	const int32 SrcCtrl = DragCtrl;
	DragCtrl = -1;
	DragArm.Disarm();
	if (SrcCtrl < 0) { RefreshGrid(); RefreshBasket(); RefreshSourceDim(); return; }

	const UWidget* Icon = nullptr;
	const int32 DstCtrl = HitTestCell(/*bRequireItem*/false, &Icon);
	const int32 DstGroup = (DstCtrl >= 0) ? GroupOfCtrl(DstCtrl) : -1;

	auto PutBack = [this]()
	{
		// «Вернуть иконку на место»: снять FillMode и перерисовать счётчик.
		RefreshGrid(); RefreshBasket();
		for (int32 k = 0; k < kBags; ++k) { if (bBagOpen[k]) { RefreshBag(k + 1); } }
		RefreshSourceDim();
	};

	// Мимо любого контрола / не ячейка склада / режим «хранить» и цель НЕ корзина — вернуть.
	if (DstCtrl < 0 || DstGroup == -1 || (Mode == kModeKeep && DstGroup != 9)) { PutBack(); return; }

	if (DstGroup == 9)
	{
		MoveOrSplit(SrcCtrl, DstCtrl);                 // корзина → §8
		PutBack();
		return;
	}

	// Цель — ячейка склада (группы 0…8): пакет 5000, если режим ≠ 0, либо цель пуста,
	//   либо предмет цели не лежит в корзине.
	const FAsdaItemInstance* SrcIt = SlotPtrByCtrl(SrcCtrl);
	const FAsdaItemInstance* DstIt = SlotPtrByCtrl(DstCtrl);
	if (!SrcIt || SrcIt->IsEmpty()) { PutBack(); return; }
	const bool bAllowed = (Mode != kModeTake) || !DstIt || DstIt->IsEmpty() || IsNotInBasket(*DstIt);
	if (!bAllowed) { PutBack(); return; }

	// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] `asda.Warehouse.SelfStoreGate`: при открытой лавке сервер отвергнет 5000 (5001 код 18) — не шлём.
	if (WareBlockedBySelfStore(this, TEXT("склад: перенос между ячейками (5000) при открытой лавке"))) { PutBack(); return; }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Net && SrcIt->Raw.Num() >= FAsdaItemInstance::kRecordSize)
	{
		// `netSendBuyRequest5000@0xE884A0` (network.c:30887): если целевая ячейка ПУСТА, слот
		//   назначения считается как `ячейка + 30 · вкладка`; иначе берётся поле +5 записи цели.
		int32 DstSlot;
		if (!DstIt || DstIt->IsEmpty())
		{
			DstSlot = CellOfCtrl(DstCtrl) + ((DstGroup > 0 && DstGroup <= 8) ? kWareBagCells * DstGroup : 0);
		}
		else
		{
			DstSlot = DstIt->SlotIndex;
		}
		Net->SendItemMove5000(SrcIt->Raw, DstSlot, /*DstType — СКЛАД*/5);
		UE_LOG(LogAsdaWare, Log, TEXT("[склад] 5000: код %d, слот-источник %d → слот %d (тип 5)"),
			SrcIt->ItemCode, SrcIt->SlotIndex, DstSlot);
	}
	PutBack();
}

// Наведение, тултип и одиночный клик по ячейкам.
void UAsdaWarehouseSubsystem::TickCells()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Tip) { return; }
	// Гейт эталона (ui_core.c:78506): пока идёт перетаскивание, тултип НЕ показываем.
	if (DragGhost.IsActive())
	{
		if (TipCtrl >= 0) { Tip->HideTooltipFor(EAsdaTipOwner::Warehouse); TipCtrl = -1; }
		return;
	}
	const UWidget* Icon = nullptr;
	const int32 Ctrl = HitTestCell(/*bRequireItem*/true, &Icon);
	if (Ctrl < 0)
	{
		if (TipCtrl >= 0) { Tip->HideTooltipFor(EAsdaTipOwner::Warehouse); TipCtrl = -1; }
		return;
	}
	if (Ctrl == TipCtrl) { return; }
	TipCtrl = Ctrl;
	const FAsdaItemInstance* It = SlotPtrByCtrl(Ctrl);
	if (!It || It->IsEmpty()) { return; }
	// `warehouseSlotSetTooltipOrState@0xE89060` (событие 8): полный тултип по 57-байтовой записи.
	//   `SourceKind` = откуда предмет: 1 сумка снаряжения, 2 расходники, 3 надетое; склад
	//   показывается по тем же правилам, что вкладка «Снаряжение» (сравнения нет).
	const int32 SourceKind = (It->ContainerType == 2) ? 2 : 1;
	Tip->ShowItemTooltip(*It, /*bSuppressCompare*/false, EAsdaTipOwner::Warehouse, SourceKind);
}

// Наведение на крестик 4050: спрятать `Ware_Gear`, показать и запустить `Ware_AniGear`
//   (ui_core.c:78509-78514 / 78521-78526). Единственная анимация окна.
void UAsdaWarehouseSubsystem::TickGearHover()
{
	if (AsdaCloseGear::IsCommonOn()) { bGearHover = false; return; }   // ★ 08.11.2026: ведёт общая точка UAsdaCloseGearSubsystem
	const bool bOver = BtnClose && AsdaUi::IsCursorOverWidget(BtnClose);
	if (bOver == bGearHover) { return; }
	bGearHover = bOver;
	AsdaUi::SetShownDecor(Gear, !bOver);
	AsdaUi::SetShownNoHit(AniGear, bOver);
}

void UAsdaWarehouseSubsystem::OnCellDoubleClick(int32 CtrlId)
{
	// События 0xD и 0xF: `if (!g_pUIManager[21]) inventory_OnItemSlotMoveOrSplit(this, a3, −1)`.
	if (bRequestSent) { return; }
	MoveOrSplit(CtrlId, -1);
}
