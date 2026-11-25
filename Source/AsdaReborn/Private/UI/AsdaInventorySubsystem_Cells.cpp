// Copyright (c) 2026 @sitxovski (Discord)
// AsdaInventorySubsystem_Cells.cpp — ★ R3-c 12.11.2026 (разрез AsdaInventorySubsystem.cpp, дословный перенос): вкладки/прокрутка, контейнеры и ячейки, пачки ячеек, кэш быстрых слотов, материалы, авторасстановка.
//   Общее для частей — в AsdaInventorySubsystem_Private.h; шапка и реверс — в AsdaInventorySubsystem.cpp.
#include "UI/AsdaInventorySubsystem.h"

// ★ Волна 1 «применение предметов»: эффект/звук по `linkCode` и состояния 265/266 из 5029 —
//   через подсистему эффектов (своих копий спавна и поиска актора не заводим, [[port-shared-points]]).
// ★ Только ради ОДНОЙ статической таблицы цветов заточки (`itemGetGradeColor@0xD29940`) — второй
//   копии палитры не заводим (закон «аудит дублей»). Сам файл тултипа отсюда не правится.
#include "Engine/DataTable.h"              // сырые поля шаблона предмета (DataHex) для гейтов панели

// ★ Явно, а не «через unity»: файл зовёт `GI->GetSubsystem<>()` больше сотни раз. Без этого include
//   одиночная сборка файла (`Build.bat … -SingleFile=…`) не проходит — а она единственный способ
//   проверить компиляцию, пока в редакторе активен Live Coding и полная сборка запрещена.
#include "AsdaInventorySubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaInventorySubsystem*.cpp

// ★★★ Д9: ВТОРИЧНЫЙ КЛЮЧ ВНУТРИ РАЗРЯДА — НАША ДОРАБОТКА ПОВЕРХ ОРИГИНАЛА.
//   ─── что снято с ЖИВОГО ДАМПА Asda Global (2026-08-13, `Reports/inventory_autosort_global_dump.md`,
//       `inventoryAutoArrange@0x0078D840`, VA Global) ───
//     for (m = 0; order[m] != -1 && cursor < 60; ++m)          // разряды по таблице порядка
//       for (n = 0; n < slotCount; ++n)                        // ← перебор СВЕРХУ, каждый разряд
//         if (matches(order[m], buf[n]) && n >= cursor) { swap(buf[cursor], buf[n]); ... ++cursor; }
//   Сравнения кандидатов между собой в теле НЕТ ВООБЩЕ — берётся первое совпадение. Порядок внутри
//   разряда задаётся ПОРЯДКОМ РАБОЧЕГО БУФЕРА на момент прохода этого разряда, а он уже переставлен
//   обменами предыдущих разрядов, то есть это даже не «исходный порядок слотов». Ровно то же самое
//   у нашего эталона (`inventoryAutoArrange@0xD35340`): порядка внутри разряда нет НИ У КОГО.
//   ─── что делаем МЫ ───
//   МЕХАНИКА остаётся 1:1 с Global (первое-подходящее → ОБМЕН в локальной копии 57-байтовых записей,
//   таблица порядка, предикаты, разряд 99 по трём таблицам, оба отсева отправителя). Меняется
//   ТОЛЬКО ВЫБОР КАНДИДАТА внутри разряда: не «первый с `n >= курсора`», а «НАИМЕНЬШИЙ ПО КОДУ
//   ПРЕДМЕТА среди подходящих с `n >= курсора`» (при равных кодах — меньший индекс, он найден раньше).
//   Зачем: без ключа самый массовый разряд 99 не упорядочивается вовсе, и юзер видит
//   «рецепт, смазка, рецепт». То, что так же ведёт себя сам Global, этого не оправдывает —
//   это ровно случай закона `fix-original-defects`, решение юзера от 2026-08-13.
//   Ничего не выдумано: коды в `item_list` идут блоками по смыслу (20913..20962 — сырьё,
//   71669..71711 и 72785..72800 — рецепты/инструкции), поэтому ключ складывает одинаковое к одинаковому.
//   Откат на точное поведение Global — ключом ниже (юзер сравнивает живьём).
static ASDA_DEV_CVAR(int32, CVarLegacyArrangeOrder,
	TEXT("asda.Inven.LegacyArrangeOrder"),
	0,
	TEXT("Порядок ВНУТРИ разряда автосортировки: 0 = Д9, вторичный ключ по коду предмета (наша "
	     "доработка: иначе разряд 99 не упорядочивается вовсе); 1 = один в один Asda Global и эталон "
	     "(inventoryAutoArrange@0x0078D840, VA Global) — первое подходящее с индексом >= курсора, "
	     "вторичного ключа нет. Механика прохода (обмен в локальной копии) в обоих режимах одна."),
	ECVF_Default);

void UAsdaInventorySubsystem::SetTab(int32 TabIndex)
{
	TabIndex = FMath::Clamp(TabIndex, 0, 1);
	if (CurrentTab == TabIndex) { return; }
	CurrentTab = TabIndex;
	SelCell = -1;
	// Эталон при смене вкладки зовёт `uiInventoryDetail_Reset` (item_inventory.c:41556) — панель гаснет.
	if (Tooltip.ClearInfoPanel) { Tooltip.ClearInfoPanel(); }
	SowelNotifySelect(GetGameInstance(), 0, -1);   // ★ ветка самоцветов гаснет вместе с инфо-панелью
	// ★★★ ЭТАЛОН `inventoryItemDetailSetVisible@0xD173A0` (item_inventory.c:41540) — смена вкладки
	//   САМА управляет правой панелью, и это ровно то, что просил юзер («открывается само»):
	//     вкладка 1 → `uiInventoryDetail_Reset` + `ItemTooltip_PopulateInfoPanel(this,1,-1)`
	//                 + `uiPanelExpand` (панель раскрывается);
	//     вкладка 0 → панель гасится, ширина 228, назад показывается колёсное окно 494.
	//   Память ручной свёртки (Д8) на смене вкладки сбрасывается — как и в эталоне, где её нет вовсе.
	bPanelCollapsedByUser = false;
	// ★★★ ОТСТУПЛЕНИЕ ПО ТРЕБОВАНИЮ ЮЗЕРА (2026-08-09): «если не выбран предмет — панель скрыта».
	//   Эталон здесь раскрывает её ПУСТОЙ (`uiPanelExpand` + `PopulateInfoPanel(this,1,-1)`, где
	//   слот −1 только гасит поля) — то есть переключение на «Снаряжение» само открывало пустое окно.
	//   Раскрытие теперь делает только выбор предмета (`AutoExpandOnSelect`) и ручная стрелка 408.
	//   Вернуть эталон: `asda.ItemTip.LegacyInfoPanelNoSelection 1` (там же перехват со стороны
	//   подсистемы подсказки — он останется страховкой, даже если эта строка снова станет эталонной).
	SetPanelExpanded(false, /*bFromUser=*/false);
	ScrollRow[CurrentTab] = FMath::Clamp(ScrollRow[CurrentTab], 0, GetMaxScrollRow());
	RefreshTabVisual();
	RefreshGrid();
	UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] вкладка %d (%s), контейнер %d, ячеек %d"),
		CurrentTab, CurrentTab == 1 ? TEXT("Снаряжение") : TEXT("Расходники"),
		GetGridContainer(), GetContainerSize(GetGridContainer()));
}

void UAsdaInventorySubsystem::SetScrollRow(int32 Row)
{
	const int32 Clamped = FMath::Clamp(Row, 0, GetMaxScrollRow());
	if (ScrollRow[CurrentTab] == Clamped) { return; }
	ScrollRow[CurrentTab] = Clamped;
	SelCell = -1;
	RefreshGrid();
}

void UAsdaInventorySubsystem::ScrollBy(int32 DeltaRows)
{
	SetScrollRow(ScrollRow[CurrentTab] + DeltaRows);
}

// ══════════════════════════════ МОДЕЛЬ ═════════════════════════════════════════════════════════
TArray<FAsdaItemInstance>* UAsdaInventorySubsystem::Container(int32 ContainerId)
{
	switch (ContainerId)
	{
	case 1: return &Shell;
	case 2: return &Spend;
	case 3: return &Equip;
	case 5: return &Store;
	// 4 = слоты КВЕСТОВ (12 × 44 б) — ★ это НЕ предметы, здесь их не моделируем (отчёт B §1.2).
	default: return nullptr;
	}
}

const TArray<FAsdaItemInstance>* UAsdaInventorySubsystem::Container(int32 ContainerId) const
{
	return const_cast<UAsdaInventorySubsystem*>(this)->Container(ContainerId);
}

int32 UAsdaInventorySubsystem::CountItemById(int32 ItemCode) const
{
	// Порт `CInventory_CountItemById@0x9998E0` — разбор у объявления. Размеры 30/60 — как у эталона
	//   (контейнеры 1/2 порта ровно такие: `kShellSlots`/`kSpendSlots`).
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(ItemCode) : nullptr;
	const bool bEquip = IT && IT->IsWeaponOrEquipClass(ItemCode);
	const int32 Cont = bEquip ? 1 : 2;
	const int32 N = bEquip ? kShellSlots : kSpendSlots;
	const bool bTradable = Rec && Rec->TradableFlag != 0;
	int32 Total = 0;
	for (int32 I = 0; I < N; ++I)
	{
		const FAsdaItemInstance* C = GetCell(Cont, I);
		if (!C || C->ItemCode != ItemCode) { continue; }
		Total += bTradable ? C->Amount : 1;
	}
	return Total;
}

void UAsdaInventorySubsystem::ForEachItemStack(int32 ItemCode, TFunctionRef<void(int32, int32, int32)> Fn) const
{
	// Тот же выбор контейнера, размера и «стек = количество», что у `CountItemById` выше (`CInventory_CountItemById`).
	if (ItemCode < 0) { return; }
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(ItemCode) : nullptr;
	const bool bEquip = IT && IT->IsWeaponOrEquipClass(ItemCode);
	const int32 Cont = bEquip ? 1 : 2;
	const int32 N = bEquip ? kShellSlots : kSpendSlots;
	const bool bTradable = Rec && Rec->TradableFlag != 0;
	for (int32 I = 0; I < N; ++I)
	{
		const FAsdaItemInstance* C = GetCell(Cont, I);
		if (!C || C->ItemCode != ItemCode) { continue; }
		Fn(Cont, I, bTradable ? C->Amount : 1);
	}
}

int32 UAsdaInventorySubsystem::GetContainerSize(int32 ContainerId) const
{
	if (ContainerId == 3) { return GetEquipSlotCount(); }   // 11 либо 20 (аватар-задел)
	const TArray<FAsdaItemInstance>* C = Container(ContainerId);
	// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): у сумки ДОСТУПНАЯ ёмкость (60 либо 30 по `asda.Inven.ShellX2`); массив — всегда 60.
	if (C && ContainerId == 1) { return FMath::Min(C->Num(), GetShellCapacity()); }
	return C ? C->Num() : 0;
}

// ★★★ «Контейнер получен целиком» — см. развёрнутое обоснование у объявления в заголовке.
//   Контейнеры, которых мы не моделируем (0, 4 и всё прочее), считаем ПОЛУЧЕННЫМИ: для них
//   «пусто» — окончательный ответ, и потребитель должен вести себя как эталон, а не ждать вечно.
bool UAsdaInventorySubsystem::IsContainerReceived(int32 ContainerId) const
{
	if (ContainerId != 1 && ContainerId != 2 && ContainerId != 3 && ContainerId != 5) { return true; }
	return (ContainerReceivedMask & (1 << ContainerId)) != 0;
}

const FAsdaItemInstance* UAsdaInventorySubsystem::GetCell(int32 ContainerId, int32 CellIndex) const
{
	const TArray<FAsdaItemInstance>* C = Container(ContainerId);
	if (!C || !C->IsValidIndex(CellIndex)) { return nullptr; }
	return &(*C)[CellIndex];
}

FAsdaItemInstance* UAsdaInventorySubsystem::GetCellMutable(int32 ContainerId, int32 CellIndex)
{
	TArray<FAsdaItemInstance>* C = Container(ContainerId);
	if (!C || !C->IsValidIndex(CellIndex)) { return nullptr; }
	return &(*C)[CellIndex];
}

int32 UAsdaInventorySubsystem::GetCellItemCode(int32 ContainerId, int32 CellIndex) const
{
	const FAsdaItemInstance* I = GetCell(ContainerId, CellIndex);
	return I ? I->ItemCode : 0;
}

int32 UAsdaInventorySubsystem::GetCellAmount(int32 ContainerId, int32 CellIndex) const
{
	const FAsdaItemInstance* I = GetCell(ContainerId, CellIndex);
	return I ? I->Amount : 0;
}

int32 UAsdaInventorySubsystem::GetGold() const
{
	// ★ Эталон держит сумму в ОТДЕЛЬНОМ поле (`uiSetHonorPointsDisplay`), которое обновляют
	//   пакеты 5003 (gold-слот), 5007 @12, 5013 (запись+9) и батчи 4048. Запись контейнера 2
	//   с кодом 20551 — лишь начальный источник (сервер её пересылает не при каждом изменении).
	if (GoldDisplay >= 0) { return GoldDisplay; }
	for (const FAsdaItemInstance& I : Spend)
	{
		if (I.ItemCode == kGoldItemCode) { return I.Amount; }
	}
	return 0;
}

int32 UAsdaInventorySubsystem::FindFreeSlot(int32 ContainerId) const
{
	const TArray<FAsdaItemInstance>* C = Container(ContainerId);
	if (!C) { return -1; }
	const int32 N = FMath::Min(C->Num(), GetContainerSize(ContainerId));
	for (int32 i = 0; i < N; ++i) { if ((*C)[i].IsEmpty()) { return i; } }
	return -1;
}

void UAsdaInventorySubsystem::PutRecord(const uint8* Rec57, int32 ForcedContainer)
{
	if (!Rec57) { return; }
	const int32 Code = RdI32(Rec57 + 0);
	int32 Cont = (ForcedContainer > 0) ? ForcedContainer : (int32)Rec57[4];
	int32 Slot = RdI16(Rec57 + 5);
	TArray<FAsdaItemInstance>* C = Container(Cont);
	if (!C)
	{
		UE_LOG(LogAsdaInven, Warning, TEXT("[инвентарь] запись code=%d: контейнер %d не моделируется — пропуск"), Code, Cont);
		return;
	}
	// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): предел — ДОСТУПНАЯ ёмкость (`GetContainerSize`): при `asda.Inven.ShellX2 0`
	//   сумка 30, запись в 30..59 не принимается (как прежде — вне контейнера).
	if (!C->IsValidIndex(Slot) || Slot >= GetContainerSize(Cont))
	{
		UE_LOG(LogAsdaInven, Warning, TEXT("[инвентарь] запись code=%d: слот %d вне контейнера %d (%d ячеек) — пропуск"),
			Code, Slot, Cont, GetContainerSize(Cont));
		return;
	}
	(*C)[Slot].SetRaw(Rec57, FAsdaItemInstance::kRecordSize);
	// ★ Батч 4048 приходит с полем +4 контейнера, но у части записей оно может быть не выставлено —
	//   принудительно приводим к тому контейнеру, в который сервер этот батч и отдал.
	if (ForcedContainer > 0) { (*C)[Slot].SetContainerType((uint8)ForcedContainer); }
	// ★ Эталон `recv_InventoryFullRefresh` (network.c:17303): запись 20551 сразу двигает поле денег.
	if (Code == kGoldItemCode) { SetGoldDisplay((*C)[Slot].Amount); }
	NotifyCell(Cont, Slot);
}

void UAsdaInventorySubsystem::ClearCell(int32 ContainerId, int32 CellIndex)
{
	TArray<FAsdaItemInstance>* C = Container(ContainerId);
	if (!C || !C->IsValidIndex(CellIndex)) { return; }
	(*C)[CellIndex].Clear();
	(*C)[CellIndex].SetContainerType((uint8)ContainerId);
	(*C)[CellIndex].SetSlotIndex(CellIndex);
	NotifyCell(ContainerId, CellIndex);
}

// ★★★ Пакетное изменение ячеек — обоснование у объявления в `.h`.
void UAsdaInventorySubsystem::BeginCellBatch()
{
	bSuppressHotbarRefresh = true;
}

void UAsdaInventorySubsystem::EndCellBatch()
{
	if (!bSuppressHotbarRefresh) { return; }   // `End` без `Begin` — ничего не делаем
	bSuppressHotbarRefresh = false;
	RefreshHotbarCache();                      // ровно ОДНА пересборка на весь батч
}

void UAsdaInventorySubsystem::NotifyCell(int32 ContainerId, int32 CellIndex)
{
	// ★★★ СБРОС ВЫДЕЛЕНИЯ ОПУСТЕВШЕЙ ЯЧЕЙКИ. Единая точка: и `PutRecord`, и `ClearCell` проходят
	//   ЗДЕСЬ, поэтому правило накрывает ВСЕ пути опустошения разом — применение (5029/5451/5469/
	//   6097), перенос (5001), продажу и покупку (5003/4999), выброс и уничтожение (5007),
	//   обмен и надевание (5003 контейнер 3), подбор с заменой. Разбор — раздел 2 отчёта.
	ClearSelectionIfCellEmptied(ContainerId, CellIndex);
	OnCellChanged.Broadcast(ContainerId, CellIndex);
	// ★ Хотбар хранит предмет как «контейнер + ячейка» и обязан перечитать кэш при любом изменении
	//   (эталон зовёт `hotbarRefreshSlotsByItemRef@0xDF0840` ИЗ инвентаря, а не по опросу).
	//   ⚠ На приёме батча ячеек десятки — там ребилд подавляется и делается ОДИН раз в конце.
	if (bSuppressHotbarRefresh) { return; }
	RefreshHotbarCache();
}

// ═════════ ★★★ СБРОС ВЫДЕЛЕНИЯ ПРИ ОПУСТОШЕНИИ ЯЧЕЙКИ (жалоба владельца 18.08.2026) ═══════════
// ЭТАЛОН. Правило «в пустой ячейке выделения не бывает» записано в `CInventoryUI_OnSlotClick`
//   (`item_inventory.c:36266-36361`) двумя одинаковыми ветками:
//     • контейнер 1 («Снаряжение», `g_InventoryBag`), ячейка пуста:
//         m_nSelBagSlot = -1; m_nSelBagCol = -1;
//         uiHideControlIfVisible(this);          // погасить курсор лотка самоцветов
//         uiWindowResetAllFields(this);          // ОЧИСТИТЬ правую инфо-панель
//         itemGrid_RedrawDefaultSlots(this);     // перерисовать гнёзда самоцветов пустыми
//     • контейнер 2 («Расходники», `g_InventoryArray2`), ячейка пуста:
//         m_nSelEquipSlot = -1; m_nSelEquipCol = -1;
//         cControl_HideSubIfFlagClear(this);     // → SetVisible(m_pItemTraySel, 0) — курсор выбора
//   Третье доказательство (второй метод, закон `false-negatives-method`): при смене НАДЕТОГО
//   эталон гасит тот же курсор безусловно — `Recv_ItemEquipStat@0xD0F350` (`network.c:17341`).
//   И после КАЖДОГО изменения слота контейнера 2 эталон снова зовёт `cControl_HideSubIfFlagClear`
//   (`OnRecv_UseItemResult` 39085/39167, `OnRecv_ItemUpdate` case 2, `OnRecv_RemoveSlot` case 2).
// ⚠ ЧЕГО НЕ ВОСПРОИЗВОДИМ И ПОЧЕМУ. Гейт эталонного `cControl_HideSubIfFlagClear` —
//   `if (!gap_1B0[84])`, то есть «правая панель СВЁРНУТА». В оригинале панель раскрывается самой
//   вкладкой, поэтому гейт означал «мы на Расходниках». В порту раскрытие панели — осознанное
//   отступление по требованию владельца (панель открывает ТОЛЬКО выбор предмета,
//   `AutoExpandOnSelect`), и буквальный перенос гейта прятал бы курсор на «Расходниках» ВСЕГДА.
//   Поэтому переносим смысл, а не букву: гасим выделение ровно тогда, когда выбранная ячейка
//   ОПУСТЕЛА. Отступление зафиксировано в `Reports/inventory_use_fix.md`, раздел 2.
void UAsdaInventorySubsystem::ClearSelectionIfCellEmptied(int32 ContainerId, int32 CellIndex)
{
	if (CellIndex < 0) { return; }
	// Интересуют только те две ячейки, что сейчас выделены.
	const bool bIsSelGrid  = (ContainerId == GetGridContainer()) && (CellIndex == SelCell);
	const bool bIsSelEquip = (ContainerId == 3)                  && (CellIndex == SelEquipSlot);
	if (!bIsSelGrid && !bIsSelEquip) { return; }

	const FAsdaItemInstance* It = GetCell(ContainerId, CellIndex);
	if (It && !It->IsEmpty()) { return; }   // содержимое сменилось, но ячейка НЕ пуста — эталон выделение держит

	if (bIsSelGrid)  { SelCell = -1; }
	if (bIsSelEquip) { SelEquipSlot = -1; }

	// Эталон здесь же гасит подсказку, чистит правую инфо-панель и перерисовывает гнёзда самоцветов.
	HideCellTooltip();
	if (Tooltip.ClearInfoPanel) { Tooltip.ClearInfoPanel(); }
	SowelNotifySelect(GetGameInstance(), 0, -1);
	if (bWindowOpen)
	{
		if (bIsSelGrid)  { RefreshGrid(); }    // сам курсор 409 гаснет внутри (SelCell == -1 ⇒ bVisible = false)
		if (bIsSelEquip) { RefreshEquip(); }
	}
	UE_LOG(LogAsdaInven, Log,
		TEXT("[инвентарь] выделение снято: ячейка %d:%d опустела (эталон CInventoryUI_OnSlotClick, ветка пустой ячейки)"),
		ContainerId, CellIndex);
}

void UAsdaInventorySubsystem::RefreshHotbarCache()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>())
		{
			Hb->RebuildSlots();
			Hb->RefreshPage();
		}
	}
}

// ══════════════════════════════ АВТОСОРТИРОВКА (Д1) ════════════════════════════════════════════
//   ПОЛНЫЙ ПОТОК ЭТАЛОНА — ДВЕ функции, а не одна (реверс 2026-08-12, отчёт
//   `Reports/inventory_autosort_fix.md`):
//
//   1) `inventoryAutoArrange@0xD35340` — СТРОИТ ПЛАН (пакетов не шлёт):
//     • аргумент (`cmdId − 83691`) в теле НЕ ИСПОЛЬЗУЕТСЯ: контейнер берётся из ПОЛЯ ВКЛАДКИ
//       `this[129]` (= `this+0x204`) — вкладка 0 ⇒ контейнер 2 (60), вкладка 1 ⇒ контейнер 1 (30);
//     • троттл `timeGetTime() - this[0x1AC0] >= 0xEA60` (60 000 мс), иначе нотис 112406 с числом 60;
//     • перебор идёт по ПОЛНОМУ РАЗМЕРУ КОНТЕЙНЕРА (`v15 = 60`, для вкладки 1 → `30`), а НЕ по числу
//       занятых ячеек: пустые ячейки просто не проходят предикат (`itemCode` там 0);
//     • работает по ЛОКАЛЬНОЙ КОПИИ 57-байтовых записей (`v18[57*60]`), меняя её местами по ходу, —
//       ни `g_InventoryBag`, ни `g_InventoryArray2` не трогаются, применит сервер;
//     • порядок категорий — жёсткий массив: 0, 1, 15..23, 2, 3, 4..14, 99;
//     • алгоритм — селекшн-сорт с курсором записи `v19`: первая подходящая запись с индексом ≥ курсора
//       МЕНЯЕТСЯ МЕСТАМИ с ячейкой курсора, пара (откуда, куда) кладётся в список, курсор +1;
//     • пары копятся в `this[0x1158 + 8k]` (откуда) / `this[0x115C + 8k]` (куда), их число — в
//       `this[0x1AB8]`, курсор исполнения `this[0x1ABC]` = −1, флаг «сортировка идёт» `this+0x1154` = 1.
//
//   2) `inventoryAutoArrangeStep@0xD35E60` — ИСПОЛНЯЕТ ПЛАН ПО ОДНОЙ ПАРЕ ЗА ТИК:
//         if (!this[0x1154]) return;                    // сортировка не запущена
//         if (this[0x1AC4] == 1) return;                // ждём ответ на предыдущий 5000
//         if (++this[0x1ABC] >= this[0x1AB8]) {         // пары кончились
//             this[0x1154] = 0; this[0x1ABC] = 0;
//             CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, notice(112116), 1); return; }
//         if (pair.from == pair.to) return;             // тождественная пара — просто пропуск
//         netSendInventoryMoveRequest(this, this[0x1AC8]/*вкладка*/, pair.from, pair.to);
//     ⚠ Отдельного пакета «применить сортировку» НЕТ: идут обычные 5000 (`OP_CHAR_ITEM_MOVE`,
//       сервер `CharItemMove@0x4CE3E0` → БД → `sub_4D12F0` шлёт 5001 с subKind 1 = ОБМЕН местами,
//       2 = переезд в пустую), по одному, с ожиданием ответа.
//     ⚠⚠ ОБРАБОТКИ ОТКАЗА У ЭТАЛОНА НЕТ ВООБЩЕ: флаг ожидания снимается на ЛЮБОЙ 5001, курсор едет
//       дальше. Ни прерывания всей сортировки, ни повтора пары — воспроизводим это 1:1
//       (см. `ApplyMoveResult`).
//   ★ В эталонной сборке шаговый драйвер НЕДОСТИЖИМ: на 0xD35E60 нет ни одного xref, единственный
//     переход — «висячий» thunk `jmp loc_D35E60 @ 0x92FA0D`, на который тоже никто не ссылается
//     (проверено xref'ами IDA + сплошным сканом rel32/dword по всем секциям). Поэтому кнопка
//     автосортировки в оригинале НИЧЕГО НЕ ДЕЛАЛА — Д1, чиним по закону `fix-original-defects`.
// ★★★ ЧЕМ ОТЛИЧАЕТСЯ Asda Global — снято с ЖИВОГО ДАМПА КЛИЕНТА (2026-08-13, отчёт
//   `Reports/inventory_autosort_global_dump.md`; образ `RE/dumps/global/global_image_t12.bin`,
//   ImageBase 0x400000, все адреса помечены «VA Global»; база IDA
//   `RE/ida/global_work/dump/GlobalDump.exe.i64`). ⚠ Прежний отчёт `inventory_autosort_global.md`
//   снимался с `NEW/WorldOfDreams/system/Asda2.exe` (ДРУГАЯ ИГРА) и признан недостоверным.
//     • Функции: `inventoryAutoArrange@0x0078D840`, `inventoryAutoArrangeStep@0x0078E200`,
//       `netSendInventoryMoveRequest@0x0078E310`; окно инвентаря — ГЛОБАЛЬНЫЙ объект
//       по адресу 0x01608E20 (vtbl 0x01072B9C, ctor 0x0076EA20).
//     • Алгоритм, таблица порядка (0,1,15..23,2,3,4..14,99,−1), троттл 60 000 мс
//       (`GetTickCount() − this+0x1754 >= 0xEA60`), нотисы 112116/112406, запись слота 57 байт,
//       смещения полей записи предмета (+0x60 linkCode, +0x80 itemType, +0x84 itemSubType,
//       +0xB0 word flagWord176) — ВСЁ совпадает с нашим эталоном ОДИН В ОДИН.
//     • Вкладка берётся из `this+0x260`: 0 → массив `dword_1303A8C` (60 записей), 1 →
//       `dword_1302D30` (перебор ограничен 30). Поле плана: `this+0x1750` курсор, `this+0x174C`
//       число пар, пары в `this+0xDEC`/`this+0xDF0` (до 300), флаг прохода — байт `this+0xDE8`.
//     • Шаговый драйвер в Global ВЫЗЫВАЕТСЯ: кадровый цикл 0x00455A90 делает
//       `mov ecx, 0x1608E20; call 0x0041F442` @0x0045624C (тханк → 0x0078E200). Именно этого
//       вызова не хватало эталонной сборке (там 0xD35E60 без единого xref).
//     • ⚠⚠ ДЕФЕКТ САМОГО GLOBAL: байт ожидания ответа `this+0x1758` отправитель ставит в 1
//       (@0x0078E9DE), а СНИМАЕТ ЕГО ТОЛЬКО КОНСТРУКТОР окна (@0x0076EBC8). Обработчик 5001
//       (диспетчер пакетов 0x00460120, ветка `if (opcode == 5001)` → 0x007C4C30/0x007C50C0)
//       его не трогает. Проверено ДВУМЯ независимыми методами: (1) сплошной байтовый скан всех
//       секций образа на disp32 `58 17 00 00`, (2) полный обход декодированных инструкций .text
//       через IDA — оба дают РОВНО ТРИ обращения: 0x0076EBC8 (=0), 0x0078E220 (чтение),
//       0x0078E9DE (=1). ⇒ живой Global успевает отправить РОВНО ОДНУ пару 5000 за запуск
//       клиента, после чего проход висит навсегда и нотис 112116 не показывается.
//       У нас флаг снимается по 5001 (`ApplyMoveResult`) — чиним по закону `fix-original-defects`.
//     • Пакет тот же 5000 (в Global 50 байт полезной нагрузки), по одной паре за кадр;
//       отдельного «применить сортировку» нет. Отправитель ещё до пакета отбрасывает пару, если
//       запись уже стоит на месте (`rec+5 == to`) либо если в ЦЕЛЕВОЙ ячейке лежит запись с
//       байтом `+50 == 3` и `itemSubType == 32` (@0x0078E39x) — воспроизведено в `TickAutoArrange`.
//     • Пунктов меню по-прежнему ТРИ, но сортировок ДВЕ: 83691 → arrange(0), 83692 → arrange(1)
//       (аргумент телом не читается ⇒ порядок один и тот же), 83693 = «Close». Подписи ставит
//       `CInvenNewWnd_InitControls@0x0076F400` циклом `Loc(112400 + i)`.
// ★★★ КАТЕГОРИЯ 7 АВТОСОРТИРОВКИ — «предмет является СЫРЬЁМ какого-нибудь рецепта крафта».
//   Цепочка эталона (её раньше не дочитали и категория стояла заглушкой `return false`):
//     `inventoryAutoArrange@0xD35340` → `sub_924ED2@0x924ED2` (thunk) → `sub_D36790@0xD36790`
//       → `sub_929865@0x929865` (thunk) → `sub_FE1040@0xFE1040`:
//         for (i = 0; i < this[6]; ++i)                  // this = g_pItemMainWnd (окно крафта)
//           for (j = 0; j < 6; ++j)
//             if (*(int *)(this[8] + (i << 7) + 4*j + 76) == itemCode) return 1;
//         return 0;
//   Таблица — `./Data/NPC/making_recipe.BIN`, читает `tableLoad128ByteRecords@0xFDDE40` из
//   `CItemMainWnd_LoadDataAndInit@0xFDD630`: заголовок 20 б (`fread(buf, 0x14, 1)`), число записей —
//   dword по смещению +4 заголовка (в нашем файле 249), запись 128 б; поле +72 = код РЕЗУЛЬТАТА,
//   поля +76..+99 = ШЕСТЬ кодов ингредиентов (незанятые = −1). 249×128 + 20 = 31 892 = размер файла.
//
//   ★ ДАННЫЕ ЖИВУТ В ТАБЛИЦЕ, А НЕ В КОДЕ (правка 2026-09-02, аудит А2/Р2). Файл заведён в ОБЩИЙ
//   реестр форматов `@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\ItemDB\decode_item_db.py` (запись `making_recipe.bin`, ключ `loose`
//   — файл лежит россыпью, не в `Bin.bbz`), строка `FAsdaMakingRecipeRow` и путь ассета
//   `/Game/AsdaAssets/Data/ItemDB/DT_making_recipe` сгенерированы `Tools/gen_itemdb_structs.py`,
//   таблицу грузит `UAsdaItemTableSubsystem` вместе со всей item-БД. Список сырья = множество
//   Mat0..Mat5 всех записей — ровно то, что перебирает `sub_FE1040`. Прежде здесь лежал массив
//   из 134 констант; при переходе на таблицу множество СОВПАЛО 1:1 (134/134, расхождений ноль
//   в обе стороны — сверка `Reports/audit_2026-09-02/fix_craft_table.md`).
//   Обновление данных: `python Tools\decode_item_db.py --only making_recipe.bin`, затем переимпорт
//   `Tools\_import_one_itemdb_table.py` с `ASDA_TABLES=making_recipe`.
//   ★ Проверено на живом логе 2026-08-12 14:27: коды 20924/20929/20961 («Улучшенный камень магии C»,
//   «Магический камень», «Кусок железа») — ингредиенты; без этой категории они попадали в общий
//   разряд 99 и НИКУДА НЕ ПЕРЕЕЗЖАЛИ. Всего 127 кодов переходят из 99 в 7, ещё 7 — из 8/9/13.
const TSet<int32>& UAsdaInventorySubsystem::GetCraftMaterialCodes() const
{
	if (bCraftMaterialsBuilt) { return CraftMaterialCodes; }
	// Флаг ставим ДО разбора: если таблицы нет, повторный перебор строк на каждый предмет
	//   автосортировки ничего не даст, а лог засорит.
	bCraftMaterialsBuilt = true;

	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UDataTable* Dt = IT ? IT->GetDataTable(TEXT("making_recipe")) : nullptr;
	if (!Dt)
	{
		// Громко: без таблицы категория 7 пуста и сырьё поедет в общий разряд 99 — ровно та
		//   поломка, ради которой категорию и дочитывали. Молчать здесь нельзя.
		UE_LOG(LogAsdaInven, Warning,
			TEXT("[инвентарь] таблица рецептов DT_making_recipe не загружена — сырьё крафта не распознаётся"));
		return CraftMaterialCodes;
	}
	const TMap<FName, uint8*>& Rows = Dt->GetRowMap();
	CraftMaterialCodes.Reserve(Rows.Num() * 2);
	for (const TPair<FName, uint8*>& It : Rows)
	{
		const FAsdaMakingRecipeRow* Row = reinterpret_cast<const FAsdaMakingRecipeRow*>(It.Value);
		if (!Row) { continue; }
		// Те же ШЕСТЬ полей, что перебирает эталон (+76…+99). Пустой слот в файле = −1.
		const int32 Mats[6] = { Row->Mat0, Row->Mat1, Row->Mat2, Row->Mat3, Row->Mat4, Row->Mat5 };
		for (const int32 Code : Mats)
		{
			if (Code > 0) { CraftMaterialCodes.Add(Code); }
		}
	}
	UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] сырьё крафта: %d кодов из %d рецептов DT_making_recipe"),
		CraftMaterialCodes.Num(), Rows.Num());
	return CraftMaterialCodes;
}

bool UAsdaInventorySubsystem::IsCraftMaterialCode(int32 ItemCode) const
{
	// Эталон сканирует записи линейно; множество даёт тот же ответ и не зависит от порядка записей.
	return ItemCode > 0 && GetCraftMaterialCodes().Contains(ItemCode);
}

bool UAsdaInventorySubsystem::AutoArrangeMatches(int32 Category, int32 ItemCode, int32 SlotIdx, int32 WriteCursor) const
{
	if (ItemCode <= 0) { return false; }
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return false; }
	const FAsdaItemRecord* R = IT->GetItem(ItemCode);
	FAsdaSowellRow SowelRow;
	const bool bSowel = IT->GetSowell(ItemCode, SowelRow);

	// ★ Каждая строка ниже сверена с ЖИВЫМ ДАМПОМ Asda Global, `inventoryAutoArrange@0x0078D840`
	//   (VA Global): предикаты и константы совпали ОДИН В ОДИН, включая литерал 20551 у категории 0
	//   (`cmp dword ptr [ecx], 5047h` @0x0078DC75).
	switch (Category)
	{
	case 0:  return ItemCode == kGoldItemCode;                     // деньги — всегда первыми (20551)
	case 1:  return R && R->ItemType == 0;                         // шлем
	case 15: case 16: case 17: case 18: case 19:
	case 20: case 21: case 22: case 23:
		return R && R->ItemType == (Category - 14);                // itemType 1..9
	case 2:  return bSowel;                                        // самоцвет
	case 3:  return R && R->FlagWord176 != 0;
	case 4:  return R && (R->LinkCode == 7   || R->LinkCode == 200);
	case 5:  return R && (R->LinkCode == 8   || R->LinkCode == 201);
	case 6:  return R && R->ItemSubType == 66;
	case 7:  return IsCraftMaterialCode(ItemCode);                  // сырьё рецептов крафта
	case 8:  return R && R->ItemSubType == 60;
	case 9:  return R && R->ItemSubType == 68;
	case 10: return R && R->ItemSubType == 110;
	case 11: return R && R->ItemSubType == 105;
	case 12: return R && R->ItemSubType == 149;
	case 13: return R && R->ItemSubType == 202;
	case 14: return R && R->LinkCode == 70;
	case 99:
		// ★ Global (`inventoryAutoArrange@0x0078D840`, VA Global):
		//     v13 == 99 && (v8 || v9 || v11) && n >= v23
		//   где v8 = запись `item_list` (248 б, поиск 0x004EA4A0), v9 = запись `sowell_list.BIN`
		//   (176 б, 0x004EA8F0), v11 = запись `Special_ItemList.BIN` (300 б, 0x004EA9D0) —
		//   имена файлов подтверждены загрузчиками 0x004E94E0 / 0x004E9750.
		//   ⇒ «всё прочее, ЧТО ВООБЩЕ ЕСТЬ хоть в одной из трёх таблиц». Код, которого нет нигде,
		//   НЕ переезжает и остаётся на своём месте — прежняя проверка «ячейка занята» это теряла.
		return (R != nullptr || bSowel || IT->IsSpecialItemCode(ItemCode)) && SlotIdx >= WriteCursor;
	default: return false;
	}
}

void UAsdaInventorySubsystem::AutoArrange(int32 Mode)
{
	if (CVarLegacyNoAutoArrange.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] автосортировка отключена ключом asda.Inven.LegacyNoAutoArrange"));
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - LastAutoArrangeS < (double)AutoArrangeCooldownS)
	{
		// Эталон: нотис 112406 с подставленным числом секунд (в оригинале в него подставляется 60).
		Notice(112406);   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): строка 112406 заведена в DT_GameText (ведомость), RU-фолбэк убран
		return;
	}
	if (bArrangeRunning || ArrangeQueue.Num() > 0)
	{
		UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] автосортировка уже идёт (%d обменов в очереди)"), ArrangeQueue.Num());
		return;
	}
	// Контейнер берётся из ВКЛАДКИ. Аргумент `Mode` (= cmdId − 80411, в Global cmdId − 83691) не
	//   используется ни эталоном, ни Asda Global — доказано двумя методами по обоим бинарям
	//   (см. шапку блока). Оставлен в сигнатуре, чтобы связь «пункт меню → вызов» была видна.
	//   ★ `N` — ПОЛНЫЙ размер контейнера (30 либо 60), ровно как `v15` эталона: перебираем ВСЕ слоты
	//   вкладки, включая пустые (они отсеются предикатом по `ItemCode <= 0`), а не только занятые.
	const int32 Cont = GetGridContainer();
	const TArray<FAsdaItemInstance>* C = Container(Cont);
	if (!C) { return; }
	// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): доступный размер (сумка 60, при `asda.Inven.ShellX2 0` — 30: в 30..59 не кладём).
	const int32 N = FMath::Min(C->Num(), GetContainerSize(Cont));

	// Локальная копия кодов: сортируем «на бумаге», в модель ничего не пишем — применит сервер.
	TArray<int32> Codes;
	Codes.SetNum(N);
	for (int32 i = 0; i < N; ++i) { Codes[i] = (*C)[i].ItemCode; }

	// Таблица порядка — ОДНА на оба сортирующих пункта меню (снята с дизассемблера эталона,
	//   `[ebp+var_110C]`…, терминатор −1 не нужен: у нас длина массива известна). Совпадает с
	//   таблицей Asda Global (`inventoryAutoArrange@0x00D52B20`, `v14[0..25]`), а скан ВСЕГО
	//   Global-бинаря на паттерн «…,99,−1» даёт ровно одно совпадение — эту таблицу.
	static const int32 kOrder[] = { 0, 1, 15, 16, 17, 18, 19, 20, 21, 22, 23,
	                                2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 99 };
	// Д9: 0 = наш вторичный ключ (код предмета по возрастанию), 1 = точное поведение Global/эталона.
	const bool bLegacyOrder = (CVarLegacyArrangeOrder.GetValueOnGameThread() != 0);
	// Куда какой предмет уехал — только для диагностического лога (код + категория на позицию).
	TArray<int32> PlanCode; PlanCode.Init(0, N);
	TArray<int32> PlanCat;  PlanCat.Init(-1, N);
	ArrangeQueue.Reset();
	ArrangeContainer = Cont;
	int32 Cursor = 0;
	for (int32 Oi = 0; Oi < UE_ARRAY_COUNT(kOrder) && Cursor < N; ++Oi)
	{
		const int32 Cat = kOrder[Oi];
		// ★ МЕХАНИКА 1:1 с `inventoryAutoArrange@0x0078D840` (VA Global, живой дамп): выбранная
		//   ячейка МЕНЯЕТСЯ МЕСТАМИ с ячейкой курсора в локальной копии, курсор +1. Обмен (а не
		//   сдвиг!) обязателен: он переставляет рабочий буфер, и именно переставленный буфер задаёт
		//   порядок внутри СЛЕДУЮЩИХ разрядов. Перебор с `Cursor`, а не с нуля, — то же самое:
		//   ячейки левее курсора уже разложены и предикат их всё равно отбрасывает (`n >= v23`).
		// ★ ВЫБОР КАНДИДАТА — единственное, что мы меняем (Д9, наша доработка):
		//     bLegacyOrder = 1 → первое подходящее, как в Global и эталоне (вторичного ключа нет);
		//     bLegacyOrder = 0 → НАИМЕНЬШИЙ ПО КОДУ среди подходящих (при равных — меньший индекс,
		//       он найден раньше), иначе разряд 99 не упорядочивается вовсе.
		for (;;)
		{
			int32 Pick = INDEX_NONE;
			for (int32 n = Cursor; n < N; ++n)
			{
				if (!AutoArrangeMatches(Cat, Codes[n], n, Cursor)) { continue; }
				if (Pick == INDEX_NONE) { Pick = n; if (bLegacyOrder) { break; } continue; }
				if (Codes[n] < Codes[Pick]) { Pick = n; }
			}
			if (Pick == INDEX_NONE) { break; }
			PlanCode[Cursor] = Codes[Pick];
			PlanCat[Cursor]  = Cat;
			if (Pick != Cursor)
			{
				FArrangePair P;
				P.From = Pick;
				P.To = Cursor;
				ArrangeQueue.Add(P);
				Swap(Codes[Pick], Codes[Cursor]);
			}
			++Cursor;
			if (Cursor >= N) { break; }
		}
	}
	LastAutoArrangeS = Now;
	ArrangeRefused = 0;
	// Флаг эталона `this+0x1154`: проход начат. Снимет его шаговый драйвер, когда курсор дойдёт до
	//   конца списка пар (там же — завершающий нотис 112116).
	bArrangeRunning = true;
	// Диагностика: сколько ячеек вкладки вообще занято — чтобы в логе было видно, что перебор шёл по
	//   ПОЛНОМУ размеру контейнера, а курсор дошёл до последнего занятого слота.
	int32 Used = 0;
	for (int32 i = 0; i < N; ++i) { if ((*C)[i].ItemCode > 0) { ++Used; } }
	UE_LOG(LogAsdaInven, Log,
		TEXT("[инвентарь] автосортировка (пункт меню %d = id %d) вкладки %d (контейнер %d): "
		     "слотов %d, занято %d, разложено %d, %d обменов в очередь, порядок внутри разряда: %s "
		     "(механика и 25 разрядов — 1:1 с Asda Global; Д1 — в эталонной сборке шаговый драйвер "
		     "был недостижим)"),
		Mode, 80411 + Mode, CurrentTab, Cont, N, Used, Cursor, ArrangeQueue.Num(),
		bLegacyOrder ? TEXT("как в Asda Global (вторичного ключа нет)")
		             : TEXT("Д9 — наш вторичный ключ по коду предмета"));
	// ★ Построчная раскладка плана: без неё причину «нелогичного» порядка живьём не поймать.
	//   Пишем ровно то, чем оперировал алгоритм: позиция ← код, категория, имя из DT_item_list.
	{
		UGameInstance* GI = GetGameInstance();
		UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		for (int32 i = 0; i < Cursor; ++i)
		{
			const FAsdaItemRecord* R = IT ? IT->GetItem(PlanCode[i]) : nullptr;
			UE_LOG(LogAsdaInven, Log,
				TEXT("[инвентарь]   план[%02d] код=%-6d кат=%-2d type=%-3d sub=%-4d link=%-4d fw=%-4d %s"),
				i, PlanCode[i], PlanCat[i],
				R ? R->ItemType : -1, R ? R->ItemSubType : -1, R ? R->LinkCode : -1,
				R ? R->FlagWord176 : -1,
				R ? *R->ItemName : TEXT("<нет записи в item_list>"));
		}
	}
	if (ArrangeQueue.Num() == 0)
	{
		// Список пар пуст — эталонный драйвер на первом же тике закрыл бы проход нотисом 112116.
		bArrangeRunning = false;
		Notice(112116);   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): строка 112116 заведена в DT_GameText (ведомость)
	}
}

// Шаговый драйвер: точный аналог `inventoryAutoArrangeStep@0xD35E60` (см. шапку блока).
//   Зовётся раз в тик из `Tick`; `bActionPending` — наш аналог `this+0x1AC4`.
void UAsdaInventorySubsystem::TickAutoArrange()
{
	if (!bArrangeRunning) { return; }
	if (bActionPending) { return; }           // ждём 5001 на предыдущую пару
	if (ArrangeQueue.Num() == 0)
	{
		// Эталон: курсор дошёл до `this[0x1AB8]` → флаг снят, курсор обнулён, выдан нотис 112116
		//   (`UI_FormatLocalizedNotice(g_pLocNoticeCtx, 0x1B5F4, 0, 1)` → `CItem_BuildTooltipText`).
		bArrangeRunning = false;
		if (ArrangeRefused > 0)
		{
			UE_LOG(LogAsdaInven, Warning,
				TEXT("[инвентарь] автосортировка завершена, но сервер отклонил %d пар(ы) — "
				     "часть слотов могла остаться не на месте"), ArrangeRefused);
		}
		Notice(112116);   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): строка 112116 заведена в DT_GameText (ведомость)
		return;
	}
	const FArrangePair Pair = ArrangeQueue[0];
	// Эталон: `if (pair.from == pair.to) { /* пропуск */ }` — тождественные пары он в список кладёт,
	//   мы их туда просто не кладём; проверка оставлена как страховка от порчи плана.
	if (Pair.From == Pair.To)
	{
		ArrangeQueue.RemoveAt(0);
		return;
	}
	const FAsdaItemInstance* Src = GetCell(ArrangeContainer, Pair.From);
	if (!Src || Src->IsEmpty())
	{
		// Ячейка уже уехала предыдущим обменом — пара неактуальна. У эталона такой проверки нет
		//   (`netSendInventoryMoveRequest@0xD35FB0` собрал бы пакет из пустой записи и получил отказ),
		//   мы пустой пакет не шлём — закон `fix-original-defects`.
		ArrangeQueue.RemoveAt(0);
		return;
	}
	// ★ ДВА ОТСЕВА САМОГО ОТПРАВИТЕЛЯ Global (`netSendInventoryMoveRequest@0x0078E310`, VA Global),
	//   оба ДО сборки пакета — иначе мы шлём заведомо отказные 5000:
	//     1) `if (*(int16 *)(rec + 5) == to) return 0;` — запись уже стоит в целевой ячейке;
	//     2) `if (targetRec[+50] == 3 && itemInfo(target)->itemSubType == 32) return 0;` — в целевой
	//        ячейке лежит запись, которую двигать нельзя (@0x0078E37x…0x0078E3A0).
	//   ⚠ Дефект оригинала: проверку (2) Global делает по массиву ВКЛАДКИ 0 даже когда сортирует
	//   вкладку 1 (`dword_1303A8C` захардкожен). Мы смотрим фактический контейнер прохода —
	//   закон `fix-original-defects`.
	if (Src->SlotIndex == Pair.To)
	{
		ArrangeQueue.RemoveAt(0);
		return;
	}
	if (const FAsdaItemInstance* Dst = GetCell(ArrangeContainer, Pair.To))
	{
		if (!Dst->IsEmpty() && Dst->Raw.IsValidIndex(50) && Dst->Raw[50] == 3)
		{
			UGameInstance* GI = GetGameInstance();
			UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
			const FAsdaItemRecord* DstRec = IT ? IT->GetItem(Dst->ItemCode) : nullptr;
			if (DstRec && DstRec->ItemSubType == 32)
			{
				UE_LOG(LogAsdaInven, Log,
					TEXT("[инвентарь] автосортировка: пара %d → %d пропущена — в целевой ячейке "
					     "код %d (запись+50 == 3, itemSubType 32), Global такие не двигает"),
					Pair.From, Pair.To, Dst->ItemCode);
				ArrangeQueue.RemoveAt(0);
				return;
			}
		}
	}
	// Пара снимается с очереди В ЛЮБОМ случае: при удачной отправке — потому что ждём 5001, при
	//   неудачной (нет сети/сессии) — чтобы проход не завис навсегда, перепосылая одну и ту же пару.
	if (!SendMovePacket(*Src, Pair.To, ArrangeContainer, TEXT("автосортировка")))
	{
		++ArrangeRefused;
		UE_LOG(LogAsdaInven, Warning,
			TEXT("[инвентарь] автосортировка: пакет 5000 (%d → %d) не ушёл — пара пропущена"),
			Pair.From, Pair.To);
	}
	ArrangeQueue.RemoveAt(0);
}
