// Copyright (c) 2026 @sitxovski (Discord)
// AsdaExchangeSubsystem_Register.cpp — ★ R2 10.11.2026 (разрез AsdaExchangeSubsystem.cpp, дословный перенос): регистрация лота: фильтр, сетка продажи, строки, цена, классификация, пакет 9901.
//   Общее для частей — в AsdaExchangeSubsystemInternal.h; шапка и реверс — в AsdaExchangeSubsystem.cpp.
#include "UI/AsdaExchangeSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: окно обмена 2008 открыто — гейт режимов и отложенное золото 9913

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaPacketWriter.h"   // ★ U6 (В1 дедупа №2): записи 9901/9905/9909 через общий писатель
#include "Items/AsdaItemTableSubsystem.h"
// GetCurrentNpcId — NPC-аукционщик в шапке 99xx (+10)   ★ R3: комментарий снятого включения "Npc/AsdaNpcDialogSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaJuiComboBox.h"
// имя предмета и подстановка аргументов нотиса   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaItemTooltipSubsystem.h"   // общая точка подсказки предмета
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaCommunityFriendSubsystem.h"   // ★ общая точка AsdaCommunityUi::ShownVisibilityFor
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaWarehouseSubsystem.h"   // ★ пачка 20, жалоба 10: склад 4000 открыт — рынок не открывается
#include "World/AsdaWorldSubsystem.h"      // GetSelfCharName — имя игрока в записи 9901

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableText.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "Login/AsdaLoginData.h"   // [ПОРТ] 08.10.2026: общий кодек провода (гибрид RU+EN+AR)
#include "AsdaExchangeSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaExchangeSubsystem*.cpp

// ★ Откат ужесточения фильтра сетки выставления до эталонного (см. `PassesRegisterFilter`):
//   1 (по умолчанию) — как в эталоне, по категории предмета; 0 — прежний мягкий фильтр порта.
static ASDA_DEV_CVAR(int32, CVarExchangeStrictCategory,
	TEXT("asda.Exchange.StrictCategory"), 1,
	TEXT("Аукцион: 1 — фильтровать сетку выставления по категории предмета, как invFilterUsableItems."),
	ECVF_Default);

// [ОРИГИНАЛ] Жалоба 6: сетка выставления показывает ОСТАТОК ячейки за вычетом уже перенесённого в строки лота
//   (эталон держит копию контейнера — `uiMoveSlotToList@0xD4AA10`), повторный щелчок по тому же предмету стекуется в ту же
//   строку (`findItemSlotByKey@0xD4B020`). Без резерва вещь с количеством 1 добавлялась в строки сколько угодно раз.
static ASDA_DEV_CVAR(int32, CVarExchangeRegReserve,
	TEXT("asda.Exchange.RegReserve"), 1,
	TEXT("Аукцион: 1 — перенесённое в строки лота вычитается из сетки выставления; 0 — сетка читает сумку напрямую, как до 09.11."),
	ECVF_Default);

namespace {
	// ★ ИСПРАВЛЕНО: 12677 и 12699 — ОТКАЗЫ ДО ОТПРАВКИ 9901, а не «отправка пошла»
	//   (`mediationSendRegister@0xD4F0F0`: `errorFlag` → 12677 + `return`; цена == 0 → 12699 + `return`).
	constexpr int32 kLocRegBadData    = 12677;   // битые данные строки лота
	constexpr int32 kLocRegBadPrice   = 12699;   // цена не задана (ноль)
}

bool UAsdaExchangeSubsystem::PassesRegisterFilter(const FAsdaItemInstance& It, int32 Container) const
{
	if (It.IsEmpty()) { return false; }
	// Золото (20551) не выставляется ни в одном режиме.
	if (It.ItemCode == 20551) { return false; }
	// `запись[50] == 0` — общий гейт обоих режимов.
	if (It.Raw.IsValidIndex(50) && It.Raw[50] != 0) { return false; }

	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return false; }

	// ★ Порог «заряженных» предметов (`categoryFlag == 3`) — уровень 24. Условие стоит в ОБЕИХ
	//   ветках `invFilterUsableItems@0xD45520` и продублировано подписью JUI 13126.
	if (const FAsdaItemRecord* Rec = IT->GetItem(It.ItemCode))
	{
		if (Rec->CategoryFlag == 3 && SelfLevel() < 24) { return false; }
	}

	// ★★★ КАТЕГОРИЯ ПРЕДМЕТА — ГЛАВНЫЙ ОТБОР ЭТАЛОНА (`invFilterUsableItems@0xD45520`,
	//   `item_inventory.c:47346`), которого в порте не было вовсе:
	//     режим 1 (контейнер 2): ветка `ItemRecord` требует `itemGetCategoryById == 1`,
	//                            ветка самоцвета — `== 2`;
	//     режим 2 (контейнер 1): ветка `ItemRecord` требует `== 3` (или `itemType == 11 || 13`,
	//                            `LABEL_34`), ветка `Table2Record` — `== 4`.
	//   Категория 0 = «код неизвестен таблице» — эталон такую запись не находит вовсе, пропускаем.
	//   Откат: `asda.Exchange.StrictCategory 0`.
	if (CVarExchangeStrictCategory.GetValueOnGameThread() != 0)
	{
		const int32 Cat = IT->GetItemCategory(It.ItemCode);
		if (Cat != 0)
		{
			const bool bSpend = (Container == (int32)EAsdaItemContainer::SpendBag);
			const bool bOk = bSpend ? (Cat == 1 || Cat == 2) : (Cat == 3 || Cat == 4);
			if (!bOk) { return false; }
		}
	}
	return true;
}

void UAsdaExchangeSubsystem::RebuildSellGrid()
{
	// Порт `invFilterUsableItems@0xD45520`: сетка — УПЛОТНЁННАЯ копия годных ячеек контейнера.
	SellCells.Reset();
	const int32 Container = SourceContainerForMode(Mode);
	if (Container == 0) { if (GridSell) { GridSell->Refresh(); } return; }

	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { if (GridSell) { GridSell->Refresh(); } return; }

	const int32 Size = Inv->GetContainerSize(Container);
	for (int32 C = 0; C < Size && SellCells.Num() < kSellCells; ++C)
	{
		const FAsdaItemInstance* It = Inv->GetCell(Container, C);
		if (It && PassesRegisterFilter(*It, Container)) { SellCells.Add(C); }
	}
	UE_LOG(LogAsdaExchange, Log,
		TEXT("аукцион: сетка выставления — контейнер %d, ячеек в контейнере %d, годных %d"),
		Container, Size, SellCells.Num());
	if (GridSell) { GridSell->Refresh(); }
}

int32 UAsdaExchangeSubsystem::SellCellCode(int32 GridIndex) const
{
	if (!SellCells.IsValidIndex(GridIndex)) { return 0; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return 0; }
	const int32 Container = SourceContainerForMode(Mode);
	// ★ Пачка 20, жалоба 6: ячейка, целиком перенесённая в строки лота, в сетке ПУСТА (эталон очищает копию —
	//   `uiMoveSlotToList@0xD4AA10`), поэтому повторно её не выбрать.
	if (CVarExchangeRegReserve.GetValueOnGameThread() != 0 && AvailableInCell(Container, SellCells[GridIndex]) <= 0) { return 0; }
	return Inv->GetCellItemCode(Container, SellCells[GridIndex]);
}

// ★★★ ПАЧКА 20, жалоба 6 — РЕЗЕРВ СЕТКИ ВЫСТАВЛЕНИЯ. Эталон: сетка — КОПИЯ контейнера (`invFilterUsableItems@0xD45520`), перенос в
//   строку лота вычитает из копии (`uiMoveSlotToList@0xD4AA10`: весь стек / `flagWord176` ⇒ ячейка копии очищается, иначе
//   её количество −= перенесённое), возврат строки прибавляет обратно (`invSlotSwapRecordByMode@0xD4BEB0`). Порт держит не
//   копию, а живую сумку, поэтому остаток считается так: количество ячейки − сумма строк лота с той же ячейкой-источником.
int32 UAsdaExchangeSubsystem::ReservedInRegRows(int32 Container, int32 Cell) const
{
	int32 Sum = 0;
	for (const FAsdaExchangeRegRow& R : RegRows)
	{
		if (!R.IsEmpty() && R.SrcContainer == Container && R.SrcCell == Cell) { Sum += FMath::Max(1, R.Item.Amount); }
	}
	return Sum;
}

int32 UAsdaExchangeSubsystem::AvailableInCell(int32 Container, int32 Cell) const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const FAsdaItemInstance* It = Inv ? Inv->GetCell(Container, Cell) : nullptr;
	if (!It || It->IsEmpty()) { return 0; }
	const int32 Reserved = ReservedInRegRows(Container, Cell);
	if (Reserved <= 0) { return FMath::Max(1, It->Amount); }
	// Непакуемая вещь (количество 0/1) и `flagWord176` переносятся целиком — любая строка с этой ячейкой её исчерпывает.
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(It->ItemCode) : nullptr;
	if (It->Amount <= 1 || (Rec && Rec->FlagWord176 != 0) || Container != (int32)EAsdaItemContainer::SpendBag) { return 0; }
	return FMath::Max(0, It->Amount - Reserved);
}

// ★★★ ПАЧКА 20, жалоба 5 — БОЛЬШИЕ ГАЛОЧКИ «ВЫБРАТЬ ВСЁ».
//   Эталон одинаков у трёх панелей: взведение при НЕПУСТОМ списке — отметить все строки с известным предметом и показать
//   галочку взведённой (`invShowEquipSlotsByItems` / `cBoosterShop_ValidateAndRefreshSlots` / `equipSlotsSetVisibleByItem`);
//   при ПУСТОМ списке галочка тут же гаснет; снятие — снять все строки. Снятие одной строки гасит «всё» (`DropAllCheck`).
void UAsdaExchangeSubsystem::OnAllCheck(int32 Panel, bool bOn)
{
	bool bAny = false;
	if (Panel == 1)
	{
		for (FAsdaExchangeRegRow& R : RegRows) { if (!R.IsEmpty()) { R.bChecked = bOn; bAny = true; } }
		bRegAll = bOn && bAny;
		RefreshRegRows();
	}
	else if (Panel == 2)
	{
		for (FAsdaExchangeMyLot& L : MyLots) { if (!L.IsEmpty()) { L.bChecked = bOn; bAny = true; } }
		bMyAll = bOn && bAny;
		RefreshMyLots();
	}
	else if (Panel == 3)
	{
		for (FAsdaExchangeListing& L : Listings) { if (!L.IsEmpty()) { L.bChecked = bOn; bAny = true; } }
		bLotAll = bOn && bAny;
		RefreshListings();
	}
	UCheckBox* Cb = (Panel == 1) ? ChkRegAll.Get() : (Panel == 2) ? ChkMyAll.Get() : ChkLotAll.Get();
	const bool bState = (Panel == 1) ? bRegAll : (Panel == 2) ? bMyAll : bLotAll;
	if (Cb && Cb->IsChecked() != bState) { Cb->SetIsChecked(bState); }
	UE_LOG(LogAsdaExchange, Log, TEXT("аукцион: «выбрать всё» панели %d → %s (строк в списке: %s)"), Panel,
		bState ? TEXT("взведена") : TEXT("снята"), bAny ? TEXT("есть") : TEXT("нет"));
}

void UAsdaExchangeSubsystem::DropAllCheck(int32 Panel)
{
	bool& Flag = (Panel == 1) ? bRegAll : (Panel == 2) ? bMyAll : bLotAll;
	UCheckBox* Cb = (Panel == 1) ? ChkRegAll.Get() : (Panel == 2) ? ChkMyAll.Get() : ChkLotAll.Get();
	Flag = false;
	if (Cb && Cb->IsChecked()) { Cb->SetIsChecked(false); }
}

// ★★★ УРОВЕНЬ ДЛЯ ГЕЙТА «24» — ТЕМ ЖЕ ПУТЁМ, ЧТО HUD И МЕНЮ СМЕРТИ.
//   ПРИЧИНА ПРАВКИ (жалоба владельца 2026-08-28 п. 1 «модалки нет вообще»): рантайм-поле
//   `Net->SelfLevel` сидируется блоком 4005-self (@+40) и обновляется 4036; если 4005-self с
//   уровнем в сессии не пришёл, поле так и остаётся НУЛЁМ — и гейт, обязанный игнорировать ноль
//   («уровень неизвестен», закон `helper_initInvalidIds`), не срабатывает НИКОГДА. Ровно этот
//   отказ уже ловили живым тестом 2026-08-03 в меню смерти: персонаж 15 уровня получил одну
//   опцию вместо двух, потому что читали только `Net->SelfLevel`, а HUD в той же сессии писал
//   `level=15` из ФОЛБЭКА на запись char-select (0x3FF rec+26).
//   ⇒ Тот же фолбэк здесь: сперва рантайм-поле, при нуле — запись выбранного персонажа.
//   Возврат 0 по-прежнему означает «уровень неизвестен» и гейт не взводит.
int32 UAsdaExchangeSubsystem::SelfLevel() const
{
	// ★ Волна 1 (14.09.2026, В9): общая точка `AsdaSelf::GetLevel` — рантайм-поле, иначе запись ВЫБРАННОГО персонажа (как было).
	return AsdaSelf::GetLevel(GetGameInstance());
}

void UAsdaExchangeSubsystem::OnSellCellClicked(int32 GridIndex)
{
	if (!SellCells.IsValidIndex(GridIndex)) { return; }
	const int32 Container = SourceContainerForMode(Mode);
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return; }
	const FAsdaItemInstance* It = Inv->GetCell(Container, SellCells[GridIndex]);
	if (!It || It->IsEmpty()) { return; }

	// ★★★ НУМПАД, РЕЖИМ 5 — ПОЛНЫЙ ГЕЙТ `itemSlotWnd_OnRightClickSlot@0xD4C8E0`
	//   (`item_inventory.c:48823-48877`), а не только «стек > 1»:
	//     режим 1, категория 2 (самоцвет) ⇒ весь стек без вопроса;
	//     режим 1, категория 1: `flagWord176 != 0` ⇒ ВЕСЬ стек; `tradableFlag == 0` ⇒ весь стек;
	//       `tradableFlag == 1`: стек ≤ 0 ⇒ ничего, стек == 1 ⇒ весь, стек > 1 ⇒ **НУМПАД**;
	//       прочие значения `tradableFlag` ⇒ клик игнорируется;
	//     ★ режим 2 (сумка): нумпад НЕ ОТКРЫВАЕТСЯ НИКОГДА — обе ветки идут «весь стек».
	//   Сверено с `Docs/Port/systems/NUMPAD_FLOW.md` §2 (режим 5) и §3 (площадки 48860 / 48990).
	// ★ Пачка 20, жалоба 6: «стек» — ОСТАТОК ячейки за вычетом уже перенесённого в строки лота (эталон берёт количество из
	//   КОПИИ контейнера, `+2069` её ячейки, которую уменьшает `uiMoveSlotToList`). Остатка нет — щелчок ничего не делает.
	const bool bReserve = CVarExchangeRegReserve.GetValueOnGameThread() != 0;
	const int32 Avail = bReserve ? AvailableInCell(Container, SellCells[GridIndex]) : FMath::Max(0, It->Amount);
	if (bReserve && Avail <= 0)
	{
		UE_LOG(LogAsdaExchange, Log, TEXT("аукцион: ячейка %d/%d уже целиком в строках лота — щелчок пропущен (пачка 20, жалоба 6)"),
			Container, SellCells[GridIndex]);
		return;
	}
	const int32 Stack = (bReserve && It->Amount > 1) ? Avail : FMath::Max(0, It->Amount);
	const UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
	const int32 Category = IT ? IT->GetItemCategory(It->ItemCode) : 0;
	bool bAsk = false;
	int32 TakeAll = FMath::Max(1, Stack);

	if (Mode == EAsdaExchangeMode::RegisterSpend && Category == 1)
	{
		const FAsdaItemRecord* Rec = IT ? IT->GetItem(It->ItemCode) : nullptr;
		if (!Rec) { return; }
		if (Rec->FlagWord176 != 0)        { TakeAll = FMath::Max(1, Stack); }
		else if (Rec->TradableFlag == 0)  { TakeAll = FMath::Max(1, Stack); }
		else if (Rec->TradableFlag == 1)
		{
			if (Stack <= 0) { return; }
			if (Stack == 1) { TakeAll = 1; }
			else            { bAsk = true; }
		}
		else { return; }                  // прочие значения флага эталон не обрабатывает вовсе
	}

	if (!bAsk)
	{
		AddToRegRow(*It, Container, SellCells[GridIndex], TakeAll);
		return;
	}

	UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
	if (!WM) { AddToRegRow(*It, Container, SellCells[GridIndex], Stack); return; }

	PendingNumPadGrid = GridIndex;
	const int32 Cell = SellCells[GridIndex];
	const FAsdaItemInstance Copy = *It;
	WM->ShowNumPad(Stack, [this, Copy, Container, Cell, Stack](bool bAccepted, int32 Qty)
	{
		PendingNumPadGrid = -1;
		// ★ Проверка диапазона живёт в OK-колбэке: вне 1…max операция НЕ выполняется,
		//   окно всё равно закрывается (`NUMPAD_FLOW.md` §1). ★ Пачка 20: потолок — ОСТАТОК ячейки (`Stack`), а не весь стек.
		if (!bAccepted || Qty < 1 || Qty > FMath::Max(1, Stack)) { return; }
		AddToRegRow(Copy, Container, Cell, Qty);
	});
}

void UAsdaExchangeSubsystem::AddToRegRow(const FAsdaItemInstance& Src, int32 Container,
	int32 Cell, int32 Amount)
{
	// ★★★ ПАЧКА 20, жалоба 6 — [ОРИГИНАЛ] `itemGridAddOrStackSlot@0xD4AE40` + `findItemSlotByKey@0xD4B020`: в режиме 1 строка с
	//   тем же ключом (код + контейнер + ячейка) уже есть ⇒ количество ДОБАВЛЯЕТСЯ в неё, новая строка не заводится. Режим 2
	//   переносит вещь целиком, повторно она в сетке не видна (`AvailableInCell`).
	if (CVarExchangeRegReserve.GetValueOnGameThread() != 0)
	{
		if (Container == (int32)EAsdaItemContainer::SpendBag)
		{
			for (FAsdaExchangeRegRow& Row : RegRows)
			{
				if (!Row.IsEmpty() && Row.SrcContainer == Container && Row.SrcCell == Cell && Row.Item.ItemCode == Src.ItemCode)
				{
					Row.Item.Amount += Amount;
					RefreshRegRows();
					RefreshRegTotals();
					if (GridSell) { GridSell->Refresh(); }
					return;
				}
			}
		}
		if (AvailableInCell(Container, Cell) <= 0) { return; }
	}
	for (int32 R = 0; R < RegRows.Num(); ++R)
	{
		if (!RegRows[R].IsEmpty()) { continue; }
		RegRows[R].SrcContainer = Container;
		RegRows[R].SrcCell      = Cell;
		RegRows[R].Item         = Src;
		RegRows[R].Item.Amount  = Amount;
		RegRows[R].Price        = 0;
		RegRows[R].DueDays      = 1;
		RegRows[R].bChecked     = false;
		RefreshRegRows();
		RefreshRegTotals();
		if (GridSell) { GridSell->Refresh(); }   // ★ пачка 20: ячейка-источник гаснет/уменьшается в сетке
		return;
	}
	// Все 5 строк заняты — эталон просто ничего не делает.
}

void UAsdaExchangeSubsystem::DropCheckedRegRows()
{
	// `itemDropSelectedFromGrid@0xD52A60`: убрать отмеченные, затем УПЛОТНИТЬ список
	//   (`craftCompactSlotArrays`) и перерисовать.
	bool bAny = false;
	for (FAsdaExchangeRegRow& R : RegRows) { if (R.bChecked && !R.IsEmpty()) { bAny = true; break; } }
	if (!bAny) { return; }

	TArray<FAsdaExchangeRegRow> Kept;
	for (const FAsdaExchangeRegRow& R : RegRows)
	{
		if (!R.IsEmpty() && !R.bChecked) { Kept.Add(R); }
	}
	RegRows.Reset(); RegRows.SetNum(kRegRows);
	for (int32 I = 0; I < Kept.Num() && I < kRegRows; ++I) { RegRows[I] = Kept[I]; }
	RefreshRegRows();
	RefreshRegTotals();
	if (GridSell) { GridSell->Refresh(); }   // ★ пачка 20: убранные строки возвращают остаток в сетку (`invSlotSwapRecordByMode`)
	DropAllCheck(1);
}

void UAsdaExchangeSubsystem::RefreshRegRows()
{
	// ★ Пачка 20, жалоба 5: снятая строка (щелчком по иконке или галочке) гасит «выбрать всё» (`uiItemSlots_OnHover`).
	if (bRegAll) { for (const FAsdaExchangeRegRow& R : RegRows) { if (!R.IsEmpty() && !R.bChecked) { DropAllCheck(1); break; } } }
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;

	for (int32 R = 0; R < kRegRows; ++R)
	{
		const FAsdaExchangeRegRow& Row = RegRows[R];
		// Цена: читаем поле ввода той же общей точкой, что и живой набор текста.
		//   ★ Пустая строка ⇒ поле гасится; иначе цена = ровно то, что в поле (ноль — тоже цена,
		//   его ловит гейт 12699 при отправке). Прежнее `if (Typed > 0)` не давало стереть цену.
		if (RegPriceEdits.IsValidIndex(R) && RegPriceEdits[R])
		{
			if (Row.IsEmpty()) { RegPriceEdits[R]->SetText(FText::GetEmpty()); RegRows[R].Price = 0; }
			else               { ApplyPriceFromEdit(R, /*bNormalizeText*/ false); }
		}
		// `EXCH_ITEMPRICE0N` — КОМИССИЯ строки (1 % от цены).
		if (RegFeeTexts.IsValidIndex(R) && RegFeeTexts[R])
		{
			RegFeeTexts[R]->SetText(Row.IsEmpty()
				? FText::GetEmpty()
				: FText::FromString(AsdaUi::FormatThousands((int32)ListingFee(RegRows[R].Price))));
		}
		if (RegChecks.IsValidIndex(R) && RegChecks[R])
		{
			RegChecks[R]->SetIsChecked(Row.bChecked);
			RegChecks[R]->SetVisibility(Row.IsEmpty()
				? ESlateVisibility::Hidden : ESlateVisibility::Visible);
		}
		if (RegDueCombos.IsValidIndex(R) && RegDueCombos[R])
		{
			RegDueCombos[R]->SetOpen(false);
		}
		(void)IT;
	}
	if (GridReg) { GridReg->Refresh(); }
}

void UAsdaExchangeSubsystem::RefreshRegTotals()
{
	if (TxtTotalFee)
	{
		TxtTotalFee->SetText(FText::FromString(AsdaUi::FormatThousands((int32)GetTotalFee())));
	}
	if (TxtMoney)
	{
		UGameInstance* GI = GetGameInstance();
		const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		TxtMoney->SetText(FText::FromString(AsdaUi::FormatThousands(Inv ? Inv->GetGold() : 0)));
	}
}

// ═══════════════════════ цена строки лота (порт `consignRegWnd_OnAmountEdit@0xD4E330`) ════════
void UAsdaExchangeSubsystem::ApplyPriceFromEdit(int32 Row, bool bNormalizeText)
{
	if (!RegRows.IsValidIndex(Row) || !RegPriceEdits.IsValidIndex(Row)) { return; }
	UEditableText* Ed = RegPriceEdits[Row];
	if (!Ed) { return; }
	// ★ Гейт эталона: пустая строка лота — поле не обрабатывается вовсе (`isValidItem`).
	if (RegRows[Row].IsEmpty()) { return; }

	// `amount = atoi(GetEditText(...))` — берём только цифры: `Atoi64` на «12 345» дал бы 12.
	const FString Src = Ed->GetText().ToString();
	FString Digits;
	Digits.Reserve(Src.Len());
	for (const TCHAR C : Src) { if (C >= TEXT('0') && C <= TEXT('9')) { Digits.AppendChar(C); } }

	int64 Amount = Digits.IsEmpty() ? 0 : FCString::Atoi64(*Digits);
	if (Amount > kPriceMax) { Amount = kPriceMax; }   // потолок 100 000 000 (`item_inventory.c:49085`)
	if (Amount < 0)         { Amount = 0; }
	RegRows[Row].Price = Amount;

	// Ветка `a3 == 67587` (Enter/потеря фокуса): эталон переписывает текст поля нормализованным
	//   значением. При наборе текста этого делать нельзя — каретка прыгала бы на каждый символ.
	if (bNormalizeText)
	{
		const FString Want = FString::Printf(TEXT("%lld"), Amount);
		if (Src != Want) { Ed->SetText(FText::FromString(Want)); }
	}

	// `ItemListWnd_RefreshRows` — колонка комиссии строки и итог по всем строкам.
	if (RegFeeTexts.IsValidIndex(Row) && RegFeeTexts[Row])
	{
		RegFeeTexts[Row]->SetText(FText::FromString(AsdaUi::FormatThousands((int32)ListingFee(Amount))));
	}
}

void UAsdaExchangeSubsystem::OnPriceTextChanged(const FText& Text)
{
	(void)Text;
	for (int32 R = 0; R < RegPriceEdits.Num(); ++R) { ApplyPriceFromEdit(R, /*bNormalizeText*/ false); }
	RefreshRegTotals();
}

void UAsdaExchangeSubsystem::OnPriceTextCommitted(const FText& Text, ETextCommit::Type Method)
{
	(void)Text; (void)Method;
	for (int32 R = 0; R < RegPriceEdits.Num(); ++R) { ApplyPriceFromEdit(R, /*bNormalizeText*/ true); }
	RefreshRegTotals();
}

int64 UAsdaExchangeSubsystem::GetTotalFee() const
{
	int64 Sum = 0;
	for (const FAsdaExchangeRegRow& R : RegRows)
	{
		if (!R.IsEmpty()) { Sum += ListingFee(R.Price); }
	}
	return Sum;
}

int32 UAsdaExchangeSubsystem::DurationCodeForDays(int32 Days)
{
	// Коды срока сервера: 24 / 72 / 120 / 168 часов. Дефолт эталона — 24 (`ui_core.c:51713-51737`).
	switch (Days)
	{
		case 3: return 72;
		case 5: return 120;
		case 7: return 168;   // 0xA8 — четвёртый срок, в UI оригинала отсутствовал (дефект D8)
		default: return 24;
	}
}

int32 UAsdaExchangeSubsystem::SaleTaxPercentForDurationCode(int32 DurationCode)
{
	switch (DurationCode)
	{
		case 72:  return 3;
		case 120: return 5;
		case 168: return 7;
		default:  return 0;   // 24 ч — налога нет
	}
}

namespace {
// ═══════ ★★★ КЛАССИФИКАЦИЯ ПРЕДМЕТА ДЛЯ ЗАПИСИ 9901 — ПОЛЯ ФИЛЬТРОВ ПОИСКА ════════════════════
//
// ЗАЧЕМ. Поля **+50 / +51 / +53 / +55** записи 9901 — это ТИП / ПОДТИП / РАЗРЯД УРОВНЯ /
//   ИНДЕКС КАТЕГОРИИ лота; по ним работают три списка критериев 13410/13411/13412 в режимах
//   поиска 4 и 5. Порт слал туда нули, из-за чего выставленный лот уходил в категорию «0/0/0/0»:
//   в общем списке он есть, а поиском по типу/классу/уровню не находится
//   (аудит 2026-09-02, находка 2 в редакции ревизии `08_shops_ревизия.md` §2).
// ЧТО НЕ ТРОГАЕМ И ПОЧЕМУ. Поля **+52** (`usageCategory`), **+54** (`classRestriction`),
//   **+56** (`itemExtraAttr`), **+82** (комиссия), **+23** (количество), **+86** (золото),
//   **+90…97** сервер заполняет САМ из своих таблиц и затирает то, что прислал клиент
//   (`Recv_TransAction_RegistItemReq@0x517AB0`, `GameServer.c:106946`, `:106963`, `:106977`,
//   `:107021`, `:107036`, `:107044`, `:107052`). Наш ноль там безвреден, менять его незачем.
// ИСТОЧНИК. Всё ниже — построчный порт `mediationSendRegister@0xD4F0F0`
//   (`shop_trade.c:14223-14375`) и девяти справочных функций, на которые он опирается.
//   Ничего не додумано: где эталон возвращает −1, возвращаем −1.
// `itemClassifyCodeCategory@0xD43620` (`item_inventory.c:46864`): к какому КОДОВОМУ ПРОСТРАНСТВУ
//   принадлежит код — 0 самоцвет (`sowell_list`), 1 обычный предмет (`item_list`),
//   2 спец-предмет (`special_itemlist`), −1 ни одно. Порядок проверок эталонный.
int32 AsdaExch_ClassifyCodeSpace(const UAsdaItemTableSubsystem* IT, int32 Code)
{
	if (!IT) { return -1; }
	if (IT->IsSowelCode(Code))        { return 0; }
	if (IT->GetItem(Code) != nullptr) { return 1; }
	if (IT->IsSpecialItemCode(Code))  { return 2; }
	return -1;
}

// `item_getLevelTier@0xD53030` (`item_inventory.c:49279`): требуемый уровень → разряд 1…10
//   (десятками). Источник уровня: `item_list.requiredLevel`, иначе `special_itemlist` +0x48
//   (`ItemLevel`), иначе `sowell_list` +0x41 (`RequiredLevel`). Уровень 0 ⇒ разряд 1;
//   код неизвестен всем трём таблицам либо уровень вне 0…100 ⇒ **−1** (эталонный `errorFlag`).
int32 AsdaExch_LevelTier(const UAsdaItemTableSubsystem* IT, int32 Code)
{
	if (!IT) { return -1; }
	const FAsdaItemRecord* Rec = IT->GetItem(Code);
	FAsdaSowellRow Sw;
	FAsdaSpecialItemlistRow Sp;
	const bool bSowel = IT->GetSowell(Code, Sw);
	const bool bSpec  = IT->GetSpecialItem(Code, Sp);
	if (!Rec && !bSowel && !bSpec) { return -1; }

	int32 Level = 0;
	if (Rec)         { Level = Rec->RequiredLevel; }
	else if (bSpec)  { Level = Sp.ItemLevel; }
	else if (bSowel) { Level = Sw.RequiredLevel; }

	if (Level >= 1 && Level <= 100) { return (Level - 1) / 10 + 1; }
	if (Level == 0)                 { return 1; }
	return -1;
}

// `itemGetSubCategoryIndex@0xD53250` (`item_inventory.c:49384`): только для `itemType == 9`
//   (аксессуары) — подтип записи → индекс категории. Порядок пар эталонный, не арифметический.
int32 AsdaExch_SubCategoryIndex(const UAsdaItemTableSubsystem* IT, int32 Code)
{
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(Code) : nullptr;
	if (!Rec || Rec->ItemType != 9) { return -1; }
	switch (Rec->ItemSubType)
	{
		case 0:  return 0;
		case 1:  return 1;
		case 2:  return 2;
		case 3:  return 3;
		case 5:  return 7;
		case 8:  return 8;
		case 9:  return 4;
		case 10: return 5;
		case 11: return 6;
		default: return -1;
	}
}

// `itemGetCategoryIndexBySubtype@0xD53380` (`item_inventory.c:49431`): подтипы 0x3C…0x44
//   (60…68) → 1…9, остальное −1.
int32 AsdaExch_CategoryIndexBySubtype(const UAsdaItemTableSubsystem* IT, int32 Code)
{
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(Code) : nullptr;
	if (!Rec) { return -1; }
	const int32 S = Rec->ItemSubType;
	return (S >= 60 && S <= 68) ? (S - 59) : -1;
}

// `Sowel_GetCategoryIndex@0xD534B0` (`item_inventory.c:49478`): тип бонуса самоцвета
//   (`sowell_list` +0x24) → индекс категории 1…14. У типа 0 решает маска опций (+0x44).
int32 AsdaExch_SowelCategoryIndex(const UAsdaItemTableSubsystem* IT, int32 Code)
{
	FAsdaSowellRow Sw;
	if (!IT || !IT->GetSowell(Code, Sw)) { return -1; }
	switch (Sw.BonusStatType)
	{
		case 0:
			if (Sw.OptionMask == 15)   { return 1; }
			if (Sw.OptionMask == 3840) { return 2; }
			return -1;
		case 1:  return 3;
		case 2:  return 4;
		case 3:  return 5;
		case 4: case 5: case 6: case 7:
		case 26: case 27: case 28: case 29:
			return 14;
		case 8:  return 6;
		case 9:  return 7;
		case 10: return 8;
		case 11: return 9;
		case 12: return 10;
		case 13: return 11;
		case 14: case 15: case 16: case 17: case 18: case 19:
			return 12;
		case 20: case 21: case 22: case 23: case 24: case 25:
			return 13;
		default: return -1;
	}
}

// `itemTypeToCategoryIndex@0xD53680` (`item_inventory.c:49548`): `usageCategory` → индекс 0…9.
int32 AsdaExch_UsageToCategoryIndex(const UAsdaItemTableSubsystem* IT, int32 Code)
{
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(Code) : nullptr;
	if (!Rec) { return -1; }
	switch (Rec->UsageCategory)
	{
		case 0:  return 0;
		case 1:  return 1;
		case 2:  return 2;
		case 3:  return 3;
		case 12: return 4;
		case 13: return 5;
		case 14: return 6;
		case 23: return 7;
		case 24: return 8;
		case 25: return 9;
		default: return -1;
	}
}

// `item_ClassifyEquipKind@0xD53870` (`item_inventory.c:49598`): `itemType` → «род экипировки»
//   0 (тип 9, аксессуар) / 1 (тип 8, шлем) / 2 (типы 0…4, броня-оружие) /
//   3 (типы 11 или 13 с `categoryFlag == 3` — «заряженные»), иначе −1.
int32 AsdaExch_ClassifyEquipKind(const UAsdaItemTableSubsystem* IT, int32 Code)
{
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(Code) : nullptr;
	if (!Rec) { return -1; }
	const int32 T = Rec->ItemType;
	if (T == 8) { return 1; }
	if (T == 9) { return 0; }
	if (T > 4)
	{
		return ((T == 11 || T == 13) && Rec->CategoryFlag == 3) ? 3 : -1;
	}
	return 2;
}

// `itemTypeToCategory@0xD443C0` (`item_inventory.c:46999`) с эталонным `a2 = 0`: вариант предмета
//   (`item_list` +0x90) → категория. Аргумент эталон берёт как `unsigned __int8` — берём так же.
int32 AsdaExch_VariantToCategory(int32 Variant)
{
	const uint32 V = (uint32)(uint8)Variant;
	if (V < 12)             { return 0; }
	if (V < 24)             { return 1; }
	if (V == 24)            { return 2; }
	if (V >= 25 && V < 53)  { return 3; }
	if (V >= 53 && V <= 63) { return 4; }
	return 0;                          // хвост эталона: возвращается инициализатор `v3 = 0`
}

// `itemTypeToCategory_44F0@0xD444F0` (`item_inventory.c:47062`): подтип предмета → категория,
//   значение по умолчанию 5. Аргумент — `unsigned __int8`.
int32 AsdaExch_SubTypeToCategory(int32 SubType)
{
	const uint32 S = (uint32)(uint8)SubType;
	if (S < 6)                            { return 1; }
	if (S < 14 || (S >= 44 && S <= 53))   { return 2; }
	if (S == 14)                          { return 3; }
	if (S >= 15 && S < 17)                { return 4; }
	if (S >= 70 && S <= 89)               { return 4; }
	if (S == 17)                          { return 5; }
	if (S >= 105 && S <= 109)             { return 3; }
	return 5;
}

// Результат классификации одной строки лота — `FAsdaExchRegClass` (объявлен в заголовке: его принимает сборщик записи 9901).
// Ветвление 1:1 с `mediationSendRegister`: `TableSel` — эталонное поле `regModeTableSel`
//   (режим окна: 1 — выставление из инвентаря, 2 — из сумки).
FAsdaExchRegClass AsdaExch_ClassifyForRegister(const UAsdaItemTableSubsystem* IT, int32 Code,
	int32 TableSel)
{
	FAsdaExchRegClass Out;
	if (!IT) { Out.bError = true; return Out; }

	if (TableSel == 1)
	{
		const int32 Space = AsdaExch_ClassifyCodeSpace(IT, Code);
		if (Space == 1)
		{
			const FAsdaItemRecord* Rec = IT->GetItem(Code);
			if (!Rec) { return Out; }
			Out.ItemType = Rec->ItemType;
			// Типы 5, 6 и 7 идут по одной и той же ветке (`LABEL_12` эталона).
			if (Rec->ItemType >= 5 && Rec->ItemType <= 7)
			{
				Out.SubType   = 0;
				Out.LevelTier = AsdaExch_LevelTier(IT, Code);
				if (Out.LevelTier < 0) { Out.bError = true; }
				Out.CategoryIndex = AsdaExch_VariantToCategory(Rec->ItemVariant);
				Out.bValid = true;
			}
			else if (Rec->ItemType == 11)
			{
				const int32 BySub = AsdaExch_CategoryIndexBySubtype(IT, Code);
				if (BySub < 0)
				{
					Out.SubType       = AsdaExch_SubTypeToCategory(Rec->ItemSubType);
					Out.CategoryIndex = 0;
					Out.LevelTier     = 0;
				}
				else
				{
					// ★ Не опечатка: разряд уровня эталон подменяет индексом по подтипу.
					Out.SubType       = 3;
					Out.CategoryIndex = 0;
					Out.LevelTier     = BySub;
				}
				Out.bValid = true;
			}
			// Прочие типы: эталон `hasItem` не ставит ⇒ поля остаются нулями.
		}
		else if (Space == 0)
		{
			FAsdaSowellRow Sw;
			if (IT->GetSowell(Code, Sw))
			{
				Out.ItemType      = 11;
				Out.SubType       = 6;
				Out.CategoryIndex = AsdaExch_SowelCategoryIndex(IT, Code);
				if (Out.CategoryIndex < 0) { Out.bError = true; }
				Out.LevelTier = AsdaExch_LevelTier(IT, Code);
				if (Out.LevelTier < 0) { Out.bError = true; }
				Out.bValid = true;
			}
		}
	}
	else if (TableSel == 2)
	{
		const FAsdaItemRecord* Rec = IT->GetItem(Code);
		FAsdaSpecialItemlistRow Sp;
		const bool bSpec = IT->GetSpecialItem(Code, Sp);
		if (Rec)
		{
			const int32 Kind = AsdaExch_ClassifyEquipKind(IT, Code);
			if (Kind < 0) { Out.bError = true; }
			switch (Kind)
			{
				case 0:
					Out.ItemType      = Rec->ItemType;
					Out.SubType       = 0;
					Out.CategoryIndex = AsdaExch_SubCategoryIndex(IT, Code);
					if (Out.CategoryIndex < 0) { Out.bError = true; }
					Out.LevelTier = AsdaExch_LevelTier(IT, Code);
					if (Out.LevelTier < 0) { Out.bError = true; }
					Out.bValid = true;
					break;
				case 1:
					Out.ItemType      = Rec->ItemType;
					Out.SubType       = 0;
					Out.CategoryIndex = 0;
					Out.LevelTier     = AsdaExch_LevelTier(IT, Code);
					if (Out.LevelTier < 0) { Out.bError = true; }
					Out.bValid = true;
					break;
				case 2:
					Out.ItemType      = Rec->ItemType;
					Out.SubType       = 0;
					Out.CategoryIndex = 0;
					// ★ В этой ветке эталон кладёт `itemTypeToCategoryIndex` в поле **+52**,
					//   которое сервер переписывает сам, поэтому значение нам нужно ТОЛЬКО ради
					//   эталонного `errorFlag` — иначе гейт 12677 отличался бы от оригинала.
					if (AsdaExch_UsageToCategoryIndex(IT, Code) < 0) { Out.bError = true; }
					Out.LevelTier = AsdaExch_LevelTier(IT, Code);
					if (Out.LevelTier < 0) { Out.bError = true; }
					Out.bValid = true;
					break;
				case 3:
					Out.ItemType      = 12;
					Out.SubType       = 0;
					Out.CategoryIndex = 0;
					Out.LevelTier     = AsdaExch_LevelTier(IT, Code);
					if (Out.LevelTier < 0) { Out.bError = true; }
					Out.bValid = true;
					break;
				default:
					break;
			}
		}
		else if (bSpec)
		{
			// ★ Ветка спец-предмета: разряд уровня эталон считает по коду ЗАПИСИ таблицы
			//   (`Table2Record->code`) и `errorFlag` здесь НЕ ставит.
			Out.ItemType      = 12;
			Out.SubType       = 0;
			Out.CategoryIndex = 0;
			Out.LevelTier     = AsdaExch_LevelTier(IT, Sp.Code);
			Out.bValid = true;
		}
	}
	return Out;
}
}

// namespace
void UAsdaExchangeSubsystem::SendRegister()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }

	// ★★★ ЗАПИСЬ 98 БАЙТ (0x62) — РАСКЛАДКА ПЕРЕСНЯТА ПО ЭТАЛОНУ `mediationSendRegister@0xD4F0F0`
	//   (`shop_trade.c:14403-14437`). Прежняя раскладка порта была взята из §7.6 отчёта, где
	//   смещения ЗАПИСИ ОКНА перепутаны со смещениями ПАКЕТА (цена стояла на +24, код срока
	//   дублировался на +75). Пересчёт по коду (`itemEntry` — `_DWORD[25]`, побайтовые записи):
	//     itemEntry[0]                 = rec+11 → **+0  i32 itemCode**
	//     LOBYTE(itemEntry[1])         = rec+15 → +4  u8  контейнер
	//     *(WORD*)(&itemEntry[1]+1)    = rec+16 → +5  i16 слот
	//     HIBYTE(itemEntry[1])         = rec+24 → +7  u8  ПРОЧНОСТЬ
	//     LOBYTE(itemEntry[2])         = rec+35 → +8  u8  уровень заточки
	//     BYTE1/BYTE2(itemEntry[2])    = rec+37/36 → +9 / +10
	//     *(WORD*)(&itemEntry[2]+3)    = rec+38 → +11 i16 процент заточки
	//     *(DWORD*)(&itemEntry[4]+3)   = rec+20 → **+19 i32 количество**
	//     itemEntry[i+7]+1, i<5        = rec+40+4i → +29/+33/+37/+41/+45 — ПЯТЬ ОПЦИЙ (пары заточки)
	//     *(DWORD*)(&itemEntry[14]+1)  = rec+71 → **+57 i32 ЦЕНА**
	//     BYTE1(itemEntry[15])         = rec+75 → **+61 u8 КОД СРОКА** (24 / 72 / 120 / 168)
	//     &itemEntry[15]+2, 20 б       = имя игрока → **+62…+81**
	//   Что rec+71 — именно ЦЕНА, доказано вторым методом: поле ввода пишет её в `wnd2[20*row+330]`
	//   (`consignRegWnd_OnAmountEdit@0xD4E330`, `item_inventory.c:49080`), а адресная арифметика
	//   даёт 330×4 − (315×4 − 11) = **71**, где dword 315 — код предмета той же строки.
	//   ★ ЗАКРЫТО 2026-09-02 (аудит, находка 2 в редакции ревизии):
	//     • **+9  u8** = rec+37 = байт **+26** записи предмета — кладём из `Item.Raw`;
	//     • **+10 u8** = rec+36 = байт **+25** записи предмета — кладём из `Item.Raw`;
	//     • **+50/+51/+53/+55** — тип / подтип / разряд уровня / индекс категории из справочника
	//       (`AsdaExch_ClassifyForRegister` выше). Это ПОЛЯ ФИЛЬТРОВ ПОИСКА: без них лот
	//       уходил в категорию «0/0/0/0» и не находился вкладками 13410/13411/13412.
	//   ⚠ ОСТАЮТСЯ НУЛЯМИ НАМЕРЕННО — их заполняет и затирает САМ СЕРВЕР
	//     (`Recv_TransAction_RegistItemReq@0x517AB0`): **+23** количество (`GameServer.c:107021`),
	//     **+27** вес, **+52/+54/+56** (`:106977`, `:107021`, `:107036`, `:107044`),
	//     **+82** комиссия (`:106946`, считается `sub_521920`, 1 %), **+90…97** (`:106963`).
	//     Поле **+86** — золото игрока (`getFieldIfPositive`), сервер в проверке комиссии его не
	//     использует (`:107052` берёт СВОЁ значение), поэтому ноль там безвреден.
	UGameInstance* GI2 = GetGameInstance();
	const UAsdaWorldSubsystem* WS = GI2 ? GI2->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const FString SelfName = WS ? WS->GetSelfCharName() : FString();
	// Справочник нужен и гейту 12677, и классификации — берём один раз на всю отправку.
	const UAsdaItemTableSubsystem* ITab = GI2 ? GI2->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	// `regModeTableSel` эталона — номер режима окна (1 — из инвентаря, 2 — из сумки).
	const int32 TableSel = (int32)Mode;

	TArray<TArray<uint8>> Rows;
	for (const FAsdaExchangeRegRow& R : RegRows)
	{
		if (R.IsEmpty()) { continue; }
		// ★ Первый отказ эталона (`errorFlag`, `shop_trade.c:14381-14387`): по коду предмета нет
		//   записи ни в одной таблице ⇒ нотис 12677 и отправка отменяется целиком.
		if (ITab && ITab->GetItemCategory(R.Item.ItemCode) == 0)
		{
			NoticeChat(kLocRegBadData);
			return;
		}
		// ★★★ КЛАССИФИКАЦИЯ ДЛЯ ПОЛЕЙ ФИЛЬТРОВ (+50/+51/+53/+55). Здесь же — ОСТАЛЬНЫЕ ветки
		//   эталонного `errorFlag`: справочник вернул −1 (уровень вне 0…100, неизвестный подтип
		//   аксессуара, неизвестный тип бонуса самоцвета, не-экипировка в режиме 2). Эталон в
		//   этом случае выводит ТОТ ЖЕ нотис 12677 и отменяет ВСЮ отправку
		//   (`shop_trade.c:14377-14387`), а не шлёт −1 в байтовое поле.
		const FAsdaExchRegClass Cls = AsdaExch_ClassifyForRegister(ITab, R.Item.ItemCode, TableSel);
		if (Cls.bError)
		{
			UE_LOG(LogAsdaExchange, Warning,
				TEXT("аукцион 9901: предмет %d не классифицируется справочником (режим %d) — "
				     "нотис 12677, отправка отменена (эталонный errorFlag)"),
				R.Item.ItemCode, TableSel);
			NoticeChat(kLocRegBadData);
			return;
		}
		// ★★★ ГЕЙТ ЭТАЛОНА: ЦЕНА 0 ⇒ нотис **12699** и ОТПРАВКА ОТМЕНЯЕТСЯ ЦЕЛИКОМ
		//   (`shop_trade.c:14424-14431`: `if (!rec+71) { нотис; return; }`).
		//   ⚠ ИСПРАВЛЕН МИСНЕЙМ §7.5: 12677 и 12699 — это НЕ «отправка пошла», а ДВА ОТКАЗА
		//   ДО отправки (12677 — битые данные строки, `errorFlag`, `shop_trade.c:14381-14387`).
		if (R.Price <= 0)
		{
			NoticeChat(kLocRegBadPrice);
			return;
		}
		if (!Cls.bValid)
		{
			// Эталон такую строку вообще НЕ отправляет (`hasItem` остаётся 0), но своих чисел
			//   для полей фильтров у него нет — значит и у нас их взять неоткуда. Оставляем нули
			//   и пишем в лог, чтобы случай был виден на живом прогоне.
			UE_LOG(LogAsdaExchange, Warning,
				TEXT("аукцион 9901: предмет %d не попал ни в одну ветку классификации (режим %d) — "
				     "поля фильтров +50/+51/+53/+55 уходят нулями"), R.Item.ItemCode, TableSel);
		}
		Rows.Add(BuildRegisterRow9901(R, Cls, SelfName));   // ★ U6: запись — сборщиком ниже
	}
	if (Rows.Num() == 0) { return; }

	Net->SendExchangeRegister9901(Rows);
}

// ★ U6 (В1 дедупа №2): запись через `FAsdaPacketWriter` (байты те же, что у прежнего ручного буфера). Раскладка — шапка `SendRegister` выше.
TArray<uint8> UAsdaExchangeSubsystem::BuildRegisterRow9901(const FAsdaExchangeRegRow& R, const FAsdaExchRegClass& Cls, const FString& SelfName)
{
	FAsdaPacketWriter W(98);
	W.I32(0, R.Item.ItemCode);
	W.U8(4, R.SrcContainer);
	W.I16(5, R.SrcCell);
	W.U8(7, R.Item.Durability);
	W.U8(8, R.Item.GemGrade);
	// ★★★ +9 и +10 — ДВА БАЙТА ЗАПИСИ ПРЕДМЕТА, которые порт терял (аудит 2026-09-02).
	//   Эталон: `BYTE1(itemEntry[2]) = aRegSlotRecs[80*slot + 37]` → **+9**,
	//           `BYTE2(itemEntry[2]) = aRegSlotRecs[80*slot + 36]` → **+10**
	//   (`shop_trade.c:14405-14407`). Строка выставления — 80 байт, запись предмета лежит
	//   в ней с +11, значит rec+37 = байт **+26** записи, rec+36 = байт **+25**
	//   (та же арифметика, по которой rec+24 = «+13 прочность», а rec+35 = «+24 заточка»).
	//   Именованных зеркал у этих двух байтов в `FAsdaItemInstance` нет, смысл их в эталоне
	//   не выведен — переносим как есть, из сырой записи, ничего не домысливая.
	if (R.Item.Raw.IsValidIndex(26)) { W.U8(9,  R.Item.Raw[26]); }
	if (R.Item.Raw.IsValidIndex(25)) { W.U8(10, R.Item.Raw[25]); }
	W.I16(11, R.Item.EnchantPercent);
	W.I32(19, R.Item.Amount);
	for (int32 S = 0; S < 5; ++S)
	{
		const int32 Code = R.Item.EnchantCode.IsValidIndex(S)  ? R.Item.EnchantCode[S]  : 0;
		const int32 Val  = R.Item.EnchantValue.IsValidIndex(S) ? R.Item.EnchantValue[S] : 0;
		W.I16(29 + S * 4,     Code);
		W.I16(29 + S * 4 + 2, Val);
	}
	// ★★★ ПОЛЯ ФИЛЬТРОВ ПОИСКА. Эталон (`shop_trade.c:14416-14421`):
	//     BYTE2(itemEntry[12]) = itemType      → **+50**
	//     HIBYTE(itemEntry[12]) = subType      → **+51**
	//     BYTE1(itemEntry[13]) = LevelTier     → **+53**
	//     HIBYTE(itemEntry[13]) = CategoryIndex→ **+55**
	//   Соседние +52/+54/+56 эталон тоже заполняет, но их сервер переписывает своими
	//   (см. шапку `SendRegister`) — туда не пишем. Не классифицирована ⇒ нули (лог — у вызывающего).
	if (Cls.bValid)
	{
		W.U8(50, Cls.ItemType);
		W.U8(51, Cls.SubType);
		W.U8(53, Cls.LevelTier);
		W.U8(55, Cls.CategoryIndex);
	}
	W.I32(57, (int32)FMath::Clamp<int64>(R.Price, 0, MAX_int32));
	W.U8(61, DurationCodeForDays(R.DueDays));
	// ★ [ПОРТ] 08.10.2026: было `FTCHARToUTF8` — кириллица уходила UTF-8 (2 б/букву, обрезка посреди буквы на 20), а
	//   приём (+52 лота) читал кодовой страницей ОС — пара была несимметрична. Теперь общий кодек провода
	//   (`EncodeCp1251` = гибрид; русский — cp1251 байт-в-байт, как у всех остальных имён).
	W.Cp1251(62, SelfName, 20);   // имя 20 байт, хвост нулями (тело обнулено)
	return W.ToArray();
}
