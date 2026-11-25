// Copyright (c) 2026 @sitxovski (Discord)
// Реализация склада. Весь реверс с адресами — в шапке `UI/AsdaWarehouseSubsystem.h`
//   и в отчётах `Reports/warehouse_client_ui.md` (+ приложение `…_jui.md`) и
//   `Reports/warehouse_net_server.md`.
#include "UI/AsdaWarehouseSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока

#include "UI/AsdaAnimatedPlane.h"

#include "Components/PanelWidget.h"
#include "Login/AsdaButtonRelay.h"
#include "AsdaWarehouseSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaWarehouseSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaWare);   // ★ R3-c 12.11.2026: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

static const TCHAR* kWareWbp =
	TEXT("/Game/AsdaAssets/UI/Player_UI/Inventory/WBP_WARE_WIN.WBP_WARE_WIN_C");

namespace WareLoc {
// ── Loc-идентификаторы. ВСЕ сняты с эталонных вызовов `UI_FormatLocalizedNotice` (§10 отчёта). ──
	// Подписи контролов.
	constexpr int32 kFeeLabel     = 12013;    // 4044 «Keeping Fee»
	constexpr int32 kGoldLabel    = 12006;    // 4046 «Gold»
	constexpr int32 kTopLabel     = 12924;    // 4001 «warehouse»
	constexpr int32 kExtraLabel   = 12137;    // 4190 «Additional Warehouse»
	constexpr int32 kBtnKeep      = 13242;    // 4002 «Keep»
	constexpr int32 kBtnTake      = 13243;    // 4049 «Get»

	constexpr int32 kBagTitle1_4  = 12139;    // 12139…12142 — подписи сумок 1…4
	constexpr int32 kBagTitle5_8  = 32001;    // 32001…32004 — подписи сумок 5…8 (см. D18)
}

// ★ ЦВЕТ ТЕКСТА НОТИСОВ СКЛАДА. Эталон передаёт −11514 = 0xFFFFD306 (жёлтый) и в чат
//   (`CChat_AddTokenizedLines`), и в плашку (`CControl_SetTooltipText` — миснейм, это
//   `WARMESSAGE_WIN` 221). TTL плашки — эталонные 10000 мс.
static constexpr uint32 kNoticeArgb = 0xFFFFD306u;

// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА D15. `WAREKEEP_TXT01` (4038) носит скин КНОПКИ `TabBtn_ImgW108`,
//   но ветки обработки не имеет — выглядит нажимаемым и ничего не делает. Мы снимаем с него приём
//   мыши. 1 = оставить как в эталоне (ловит мышь и съедает клики по области под ним).
static ASDA_DEV_CVAR(int32, CVarWareLegacyHeaderHit,
	TEXT("asda.Warehouse.LegacyHeaderHitTest"), 0,
	TEXT("1 = эталонный дефект D15: надпись 4038 ловит мышь как кнопка; 0 = мышь проходит насквозь."),
	ECVF_Default);

// ★ [ИСПРАВЛЕНИЕ] 05.11.2026 (баг игроков «склад не работает, если открыта личная лавка»). Сервер `CharSelfStoreReady` (6300)
//   ставит игроку признак «участвует в лавке» (`CUser+0x146`, у гостя — `CharSelfStoreInfo`), а гейт `sub_52BFF0@0x52BFF0`
//   (GameServer.c, причина 4) отвергает с ним «положить» 5008 (`CharItemKeeping` → 5009 код 0, лог «CharItemKeeping(1)») и
//   перенос 5000 (`CharItemMove` → 5001 код 18). Клиент-эталон окна склада и лавки открывать вместе не мешает и выводит на
//   отказ 2240 «Эту вещь нельзя держать в хранилище» — игрок думает, что сломан склад или предмет (живой dev 24.10.2026:
//   Player21, 29 отказов подряд при открытой подготовке лавки). 1 = при открытой своей подготовке 22000/витрине 23000
//   (или чужой витрине) «Хранить» и перенос между ячейками склада пакет не шлют, а дают эталонный нотис лавки 107022
//   «Ты в своей лавке» в чат (общая точка `UAsdaTradeStateSubsystem::ChatStoreGate`, как у прочих окон-гейтов эталона);
//   корзина остаётся. «Забрать» 5010 сервер этим гейтом не проверяет — не трогаем. 0 = как в оригинале.
ASDA_DEV_CVAR(int32, CVarWareSelfStoreGate,
	TEXT("asda.Warehouse.SelfStoreGate"), 1,
	TEXT("1 = при открытой лавке (подготовка/витрина) «Хранить» 4002 и перенос между ячейками склада не шлют 5008/5000, "
	     "а пишут 107022 в чат (исправление 05.11.2026); 0 = как в оригинале: пакет уходит, сервер отвечает отказом 2240."),
	ECVF_Default);

// ═════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════════
void UAsdaWarehouseSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	GridUi.SetNum(kGridCells);
	BasketUi.SetNum(kBasketCells);
	BagCellUi.SetNum(kBags * kWareBagCells);
	Bags.SetNum(kBags);
	Store.SetNum(kStoreSlots);
	Basket.SetNum(kBasketCells);
	InvList.SetNum(UAsdaInventorySubsystem::kSpendSlots);
	for (int32 k = 0; k < kBags; ++k) { SlotAssign[k] = -1; }

	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка ТОЛЬКО кладём тело в очередь.
		HWareResult = Net->OnWarehouseResult.AddLambda(
			[this](bool bKeeping, uint8 Result, const TArray<uint8>& Body)
			{
				FWarePacket P; P.bKeeping = bKeeping; P.Result = Result; P.Body = Body;
				PendingResults.Push(MoveTemp(P));
			});
		// 5456 — вместимость склада выросла. Нотисы (12833/12834/32010) печатает
		//   `UAsdaInventorySubsystem::ApplyBagExpand` — второй копии не заводим. Наше дело —
		//   ровно то, что делает эталон в `inventoryChangeBagPage`: пометить вкладку `новая/30 − 1`
		//   загруженной, перерисовать окно в ТЕКУЩЕМ режиме и сбросить прокрутку.
		// ★ Пакет-маркер конца выдачи страницы склада (4052 `warehouseDrawCurrentTab@0xE89D50`) —
		//   ИМЕННО ЗДЕСЬ эталон ставит `aTabLoaded[вкладка] = 1` и показывает окно вкладки.
		//   Контейнер 5 = склад. Из колбэка только МЕТИМ (закон `no-reentrant-net-callbacks`):
		//   сами записи в этот момент ещё лежат в очереди подсистемы инвентаря.
		HBatchEnd = Net->OnInventoryBatchEnd.AddLambda([this](int32 Container)
		{
			if (Container == 5) { bTabArrived = true; }
		});
		HBagExpand = Net->OnBagExpand.AddLambda(
			[this](int32 NewMax, bool bAccepted, int32 /*Loc*/, bool /*bChat*/)
			{
				if (!bAccepted) { return; }
				const int32 Tab = NewMax / kWareBagCells - 1;
				if (Tab >= 0 && Tab < kStoreTabs) { bTabLoaded[Tab] = true; }
				// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА D12: эталон зовёт `uiInventoryBagOpen(self, nMode)`
				//   даже при `nMode == −1` (склад закрыт) и выполняет «мусорную» ветку — чистит
				//   корзину и обновляет тексты скрытого окна. Мы просто ничего не делаем.
				//   Ключ отката общий с D11 не нужен: наблюдаемого поведения ветка не даёт.
				if (Mode == kModeClosed) { return; }
				ScrollRow = 0;
				bDirty = true;
			});
	}
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		// Батчи 4051/4052 (контейнер 5) и любые правки контейнеров 1/2 приходят сюда.
		//   Из колбэка только МЕТИМ: один батч двигает до 30 ячеек подряд.
		HInvCell = Inv->OnCellChanged.AddLambda([this](int32 Container, int32 /*Cell*/)
		{
			if (Container == 5 || Container == 2 || Container == 1 || Container == -1) { bDirty = true; }
		});
	}
	UE_LOG(LogAsdaWare, Log, TEXT("[склад] подсистема поднята"));
}

void UAsdaWarehouseSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnWarehouseResult.Remove(HWareResult);
		Net->OnBagExpand.Remove(HBagExpand);
		Net->OnInventoryBatchEnd.Remove(HBatchEnd);
	}
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		Inv->OnCellChanged.Remove(HInvCell);
	}
	ResetWidget();
	Super::Deinitialize();
}

// ★ [ИСПРАВЛЕНИЕ] 25.10.2026: «эта причина начинает новую сессию и вещи прошлой уходят» — определено рядом с ключом
//   в `AsdaInventorySubsystem.cpp` (общая точка инвентаря, панели и склада).
bool AsdaSessionResetClearsItems(EAsdaSessionResetReason Reason);

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaWarehouseSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	ResetWidget();
	// `bRequestSent` снимается только ответом 5455 — после разрыва склад больше не открылся бы.
	bRequestSent  = false;
	bRecallActive = false;
	bTabArrived = false;
	bDirty = false;
	bConfirmOpen = false;
	PendingTab = -1;
	Category = kCatSpend;
	ScrollRow = 0;
	LastBasketCtrl = -1;
	NumPadSrcCtrl = -1; NumPadDstCtrl = -1;
	DragCtrl = -1; TipCtrl = -1;
	CellDbl.Reset();
	bGearHover = false;
	PendingResults.Clear();
	Mouse.Reset();
	DragArm.Disarm();
	if (AsdaIsFullSessionReset(Reason))
	{
		UE_LOG(LogAsdaWare, Log, TEXT("[склад] сброс сессии (%s)"), AsdaSessionResetReasonName(Reason));
	}
	// ★ [ИСПРАВЛЕНИЕ] 25.10.2026 — перезаход другой учёткой (`Reports/inventory_relog_leak/REPORT.md`).
	//   `bTabLoaded[]` (эталонный `aTabLoaded[9]`, +0x578) переживал выход на логин: склад НОВОЙ учётки считал вкладки
	//   «уже загруженными», не слал 4050 и показывал копию склада ПРОШЛОЙ (`Store`, она же контейнер 5 инвентаря).
	//   Эталон этого не знает: поля класса рождаются нулевыми с процессом, а при выходе клиент закрывается.
	//   Вместе с флагами — локальные копии (склад/сумка/корзина), признаки окон-сумок и их позиции, режим окна
	//   (окно уже снято `ResetWidget` выше — режим «открыто» без окна гейтил бы повторное открытие).
	//   Причины и ключ — общие с инвентарём (`AsdaSessionResetClearsItems`, ключ `asda.Session.RelogClearItems`).
	if (AsdaSessionResetClearsItems(Reason))
	{
		Mode = kModeClosed;
		for (int32 t = 0; t < kStoreTabs; ++t) { bTabLoaded[t] = false; }
		for (int32 k = 0; k < kBags; ++k) { bBagOpen[k] = false; SlotAssign[k] = -1; }
		// Длины фиксированные (`SetNum` в `Initialize`) — чистим значения.
		for (FAsdaItemInstance& I : Store)   { I.Clear(); }
		for (FAsdaItemInstance& I : InvList) { I.Clear(); }
		for (FAsdaItemInstance& I : Basket)  { I.Clear(); }
		UE_LOG(LogAsdaWare, Log, TEXT("[склад] сброс сессии (%s): вкладки «не загружены», копии склада/корзины сняты"),
			AsdaSessionResetReasonName(Reason));
	}
}

void UAsdaWarehouseSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.Warehouse");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	DrainPendingPackets();
	ApplyTabArrival();
	if (Mode == kModeClosed || !bControlsReady) { return; }
	if (bDirty) { bDirty = false; SyncMirrors(); RefreshAll(); }
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал (у склада это перенос вещи между сумкой и хранилищем).
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		// Летящий призрак снимаем БЕЗ действия (ветка «отпустили мимо»): иначе он замер бы поверх
		//   нотиса, а отпускание после закрытия засчиталось бы как перенос.
		if (DragGhost.IsActive()) { DragGhost.Cancel(); DragCtrl = -1; }
		DragArm.Disarm();
		// Полосе прокрутки отдаём кадр «кнопка отпущена» — так снимается латч захвата бегунка
		//   вместе с картинкой нажатой стрелки (`FAsdaScrollBarNomal::Tick`).
		{
			int32 Dummy = 0;
			if (ScrollBar.IsBuilt()) { ScrollBar.Tick(false, false, GetMaxScrollRow(), Dummy); }
		}
		if (TipCtrl >= 0)   // висящую плашку предмета ГАСИМ, а не просто «не показываем»
		{
			TipCtrl = -1;
			if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
			{
				Tip->HideTooltipFor(EAsdaTipOwner::Warehouse);
			}
		}
		// Сумки едут за окном склада не мышью, а положением окна — это не ввод, оставляем живым.
		TickBagFollow();
		Mouse.Reset();
		return;
	}
	LayoutScrollBar();
	{
		int32 Value = 0;
		switch (ScrollBar.Tick(Mouse.bLmb, Mouse.bLmbDown, GetMaxScrollRow(), Value))
		{
		case EAsdaScrollAction::ScrollBy: SetScrollRow(ScrollRow + Value); break;
		case EAsdaScrollAction::SetRow:   SetScrollRow(Value);             break;
		default: break;
		}
	}
	TickGearHover();
	TickBagFollow();
	TickDrag();
	TickCells();
}

// ★ Окна-сумки ЕДУТ ЗА ОКНОМ СКЛАДА (требование владельца 2026-08-27).
//   В `.JUI` они дети окна 4000 и двигались с ним сами собой; у нас они лежат в одном канвасе
//   с окном, и их позиция — абсолютная, поэтому связь надо держать руками: следим за фактическим
//   левым верхним углом окна и, когда он сдвинулся, переставляем ВСЕ ОТКРЫТЫЕ сумки на ту же
//   дельту. Позиция каждой считается как «угол окна + смещение её слота» — та же формула, что и
//   при открытии (`AssignFreeSlotPos`), так что расхождений между путями нет.
void UAsdaWarehouseSubsystem::TickBagFollow()
{
	const FVector2D Origin = GetWareWinOrigin();
	if (Origin.Equals(LastWareWinOrigin, 0.5f)) { return; }   // окно не двигали
	LastWareWinOrigin = Origin;

	int32 Moved = 0;
	for (int32 p = 0; p < kBags; ++p)
	{
		const int32 Tab = SlotAssign[p];
		if (Tab < 1 || Tab > kBags) { continue; }             // позиция свободна
		UWidget* Root = Bags[Tab - 1].Root.Get();
		if (!Root || Root->GetVisibility() == ESlateVisibility::Collapsed) { continue; }
		AsdaUi::SetSlotRect(Root, Origin + Bags[p].DesignPos, kWareBagLayout[p].Size);
		++Moved;
	}
	if (Moved > 0)
	{
		UE_LOG(LogAsdaWare, Verbose,
			TEXT("[склад] окно переехало в (%.0f,%.0f) — сдвинуто открытых сумок: %d"),
			Origin.X, Origin.Y, Moved);
	}
}

// ═════════════════════════ ОКНО 4000: СБОРКА ══════════════════════════════════════════════════
bool UAsdaWarehouseSubsystem::EnsureWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWareWindowId, kWareWbp, kWareZ).Ensure(GetGameInstance(), Widget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			// ★ Приоритетная полоса z [8800…8899]: склад обязан быть выше окна разговора с NPC и портрета.
			WM.SetWindowPriority(kWareWindowId, true);
			SetupControls();
			RegisterZones();
		},
		[]()
		{
			UE_LOG(LogAsdaWare, Error, TEXT("[склад] WBP_WARE_WIN не загрузился (%s)"), kWareWbp);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaWarehouseSubsystem::ResetWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kWareWindowId);
		WM->UnregisterUiZone(kRecallWindowId);
		WM->CloseWindow(kWareWindowId);
		WM->CloseWindow(kRecallWindowId);
	}
	Widget = nullptr;
	RecallWidget = nullptr;
	GridIcons = nullptr; BasketIcons = nullptr;
	Relays.Reset();
	RecallRelays.Reset();
	for (FAsdaWareCellUi& U : GridUi)    { U = FAsdaWareCellUi(); }
	for (FAsdaWareCellUi& U : BasketUi)  { U = FAsdaWareCellUi(); }
	for (FAsdaWareCellUi& U : BagCellUi) { U = FAsdaWareCellUi(); }
	for (FAsdaWareBagUi& B : Bags)       { B = FAsdaWareBagUi(); }
	BtnClose = BtnKeep = BtnTake = BtnCat0 = BtnCat1 = nullptr;
	TxtCat = TxtCap = TxtFeeLbl = TxtFee = TxtGoldLbl = TxtGold = nullptr;
	TxtHeader = TxtTop = TxtExtra = CapKeep = CapTake = nullptr;
	Gear = nullptr; AniGear = nullptr; GridSel = nullptr;
	TabBlockRoot = nullptr;
	TabBlockParts.Reset();
	CatPlanes.Reset();
	AlwaysHidden.Reset();
	ScrollBar = FAsdaScrollBarNomal();
	RcBtnTake = RcBtnKeep = RcBtnNo = RcBtnX = nullptr;
	RcTitle = RcCapTake = RcCapKeep = RcCapNo = nullptr;
	bControlsReady = false;
	DragArm.Disarm();
	DragGhost = FAsdaDragGhost();
	DragCtrl = -1; TipCtrl = -1;
	Mouse.Reset();
}

// ★★★ ПОЛНЫЙ СТРУКТУРНЫЙ РАЗБОР (закон `full-structural-analysis-always`).
//   Имена узлов сняты живым дампом WBP (`Tools/asda_ui_dump_window.py`, 996 узлов), а не угаданы:
//   интерактив носит ТОЧНОЕ имя idc, планы — `{idc}_{juiIdx}_{plane}` (закон
//   `jui-importer-widget-naming`), подпись ячейки — `TextBlock` с именем idc.
void UAsdaWarehouseSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }

	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);

	TArray<FName> GridIconNames;   GridIconNames.SetNum(kGridCells);
	TArray<FName> BasketIconNames; BasketIconNames.SetNum(kBasketCells);
	TArray<TArray<FName>> BagIconNames; BagIconNames.SetNum(kBags);
	for (TArray<FName>& A : BagIconNames) { A.SetNum(kWareBagCells); }

	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();

		// ── Кнопки ──
		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("WARE_CLOBUT"))         { BtnClose = Btn; continue; }
			if (N == TEXT("WAREKEEP_BUT"))        { BtnKeep  = Btn; continue; }
			if (N == TEXT("WARETAKE_BUT"))        { BtnTake  = Btn; continue; }
			if (N == TEXT("WAREKEEP_2BUT_OPT0"))  { BtnCat0  = Btn; continue; }
			if (N == TEXT("WAREKEEP_2BUT_OPT1"))  { BtnCat1  = Btn; continue; }
			for (int32 k = 0; k < kBags; ++k)
			{
				if (N == FString::Printf(TEXT("WAREBAG%d_CLOSE"), k + 1)) { Bags[k].Close = Btn; break; }
			}
			continue;
		}
		// ── Чек-кнопки вкладок 4004…4007 / 4301…4304 (импортёр делает из `CheckButton` `UCheckBox`) ──
		if (UCheckBox* Cb = Cast<UCheckBox>(W))
		{
			for (int32 k = 0; k < kBags; ++k)
			{
				const FString Want = (k < 4)
					? FString::Printf(TEXT("WAREUI_BAG%02d"), k)
					: FString::Printf(TEXT("WAREUI_BAG%02d_2"), k - 4);
				if (N == Want) { Bags[k].Btn = Cb; break; }
			}
			continue;
		}
		// ── Подписи ──
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == TEXT("WAREKEEP_TXT00"))      { TxtCap     = Tb; continue; }
			if (N == TEXT("WAREKEEP_TXT01_Text")) { TxtHeader  = Tb; continue; }
			if (N == TEXT("WAREKEEP_TXT02"))      { TxtFeeLbl  = Tb; continue; }
			if (N == TEXT("WAREKEEP_TXT03"))      { TxtFee     = Tb; continue; }
			if (N == TEXT("WAREKEEP_TXT04"))      { TxtGoldLbl = Tb; continue; }
			if (N == TEXT("WAREKEEP_TXT05"))      { TxtGold    = Tb; continue; }
			if (N == TEXT("WAREKEEP_TXT06"))      { TxtExtra   = Tb; continue; }
			if (N == TEXT("WAREKEEP_TOP_Text"))   { TxtTop     = Tb; continue; }
			if (N == TEXT("WAREKEEP_2BUT_Text"))  { TxtCat     = Tb; continue; }
			// ★ Вторая подпись вкладки теперь ЖИВЁТ В АССЕТЕ (её кладёт шаг
			//   `Tools/asda_ui_tabbar_captions.py`), а не создаётся кодом.
			if (N == TEXT("WAREKEEP_2BUT_Text2")) { TxtCat2 = Tb; continue; }
			if (N == TEXT("WAREKEEP_BUT_Text"))   { CapKeep    = Tb; continue; }
			if (N == TEXT("WARETAKE_BUT_Text"))   { CapTake    = Tb; continue; }
			// ★ D14: заготовки подписи режима — «вечно скрытые» (см. CVarWareModeTitle).
			if (N == TEXT("Ware_Title_Output") || N == TEXT("Ware_Title_Input"))
			{
				AlwaysHidden.AddUnique(W); continue;
			}
			bool bTaken = false;
			for (int32 c = 0; c < kGridCells && !bTaken; ++c)
			{
				if (N == FString::Printf(TEXT("WAREKEEP_TRAY%02d"), c)) { GridUi[c].Label = Tb; bTaken = true; }
			}
			for (int32 c = 0; c < kBasketCells && !bTaken; ++c)
			{
				if (N == FString::Printf(TEXT("WAREKEEP_TRAY%02d"), 30 + c)) { BasketUi[c].Label = Tb; bTaken = true; }
			}
			for (int32 k = 0; k < kBags && !bTaken; ++k)
			{
				if (N == FString::Printf(TEXT("WAREBAG%d_TXT00"), k + 1)) { Bags[k].Title = Tb; bTaken = true; break; }
				for (int32 c = 0; c < kWareBagCells; ++c)
				{
					if (N == FString::Printf(TEXT("WAREBAG%d_TRAY%02d"), k + 1, c))
					{
						BagCellUi[k * kWareBagCells + c].Label = Tb; bTaken = true; break;
					}
				}
			}
			if (bTaken) { Tb->SetText(FText::GetEmpty()); }
			continue;
		}
		// ── Группы-контейнеры ──
		if (N == TEXT("WAREKEEP_TOP_grp")) { TabBlockRoot = W; continue; }
		// ★ Планы-рамки переключателя категорий (`WAREKEEP_2BUT_<n>_0/_1`) — их рисует скин
		//   `TabBar_W108_2Btn`. Без них в режиме «хранить» видны голые надписи без вкладок
		//   (жалоба владельца 2026-08-27, скриншот). Показываем их вместе с переключателем.
		if (N.StartsWith(TEXT("WAREKEEP_2BUT_")) && Cast<UImage>(W)) { CatPlanes.AddUnique(W); continue; }
		// Корень окна 4000 — нужен, чтобы понять, расплющил ли импортёр иерархию окон-сумок.
		if (N == TEXT("WARE_WIN_grp")) { WareWinRoot = W; continue; }
		// ★ Левый-верхний угол рамки окна: от него считаются позиции окон-сумок.
		if (N == TEXT("WARE_WIN_0_0")) { WinFrameTL = W; continue; }
		// ★ 10.10.2026: правый-верхний угол рамки — по нему видна фактическая ширина окна (без `continue`: тот же узел
		//   нужен разбору частей фона ниже).
		if (N == TEXT("WARE_WIN_0_2")) { WinFrameTR = W; }
		// ★ Растянутые части фона окна (`WARE_WIN_0_3…0_5`) — им укорачиваем высоту при схлопывании.
		if (N.StartsWith(TEXT("WARE_WIN_0_")))
		{
			const FString Tail = N.RightChop(11);
			if (Tail == TEXT("3") || Tail == TEXT("4") || Tail == TEXT("5")) { WinBodyParts.AddUnique(W); }
			continue;
		}
		// ★ Коричневая рамка вокруг блока вкладок — источник «чёрной дыры» (см. D19).
		if (N == TEXT("Ware_BrownBox_grp")) { BrownBoxGrp = W; continue; }
		for (int32 k = 0; k < kBags; ++k)
		{
			if (N == FString::Printf(TEXT("WAREBAG%d_WIN_grp"), k + 1)) { Bags[k].Root = W; break; }
		}

		// ── Планы (иконки, рамки, шестерёнки) ──
		FString Idc; int32 JuiIdx = 0, PlaneIdx = 0;
		if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, PlaneIdx)) { continue; }
		if (UAsdaAnimatedPlane* Anim = Cast<UAsdaAnimatedPlane>(W))
		{
			if (Idc == TEXT("Ware_AniGear")) { AniGear = Anim; AlwaysHidden.AddUnique(W); continue; }
			if (Idc == TEXT("WARE_SEL00"))   { GridSel = Anim; continue; }
			for (int32 k = 0; k < kBags; ++k)
			{
				if (Idc == FString::Printf(TEXT("WAREBAG%d_SEL"), k + 1)) { Bags[k].Sel = Anim; break; }
			}
			continue;
		}
		UImage* Img = Cast<UImage>(W);
		if (!Img) { continue; }
		if (Idc == TEXT("Ware_Gear")) { Gear = Img; continue; }
		// ★ D14: хит-зоны скрытых заготовок заголовка — туда же, в «вечно скрытые».
		if (Idc == TEXT("Ware_Title_Output") || Idc == TEXT("Ware_Title_Input"))
		{
			AlwaysHidden.AddUnique(W); continue;
		}
		bool bTaken = false;
		for (int32 c = 0; c < kGridCells && !bTaken; ++c)
		{
			if (Idc == FString::Printf(TEXT("WAREKEEP_TRAY%02d"), c))
			{
				GridUi[c].Icon = Img; GridIconNames[c] = FName(*N); bTaken = true;
			}
		}
		for (int32 c = 0; c < kBasketCells && !bTaken; ++c)
		{
			if (Idc == FString::Printf(TEXT("WAREKEEP_TRAY%02d"), 30 + c))
			{
				BasketUi[c].Icon = Img; BasketIconNames[c] = FName(*N); bTaken = true;
			}
		}
		for (int32 k = 0; k < kBags && !bTaken; ++k)
		{
			for (int32 c = 0; c < kWareBagCells; ++c)
			{
				if (Idc == FString::Printf(TEXT("WAREBAG%d_TRAY%02d"), k + 1, c))
				{
					BagCellUi[k * kWareBagCells + c].Icon = Img;
					BagIconNames[k][c] = FName(*N);
					bTaken = true; break;
				}
			}
		}
	}

	// ★★★ СУМКИ 5…8: ЯЧЕЙКИ БЕЗ ИМЁН (правка 2026-08-27 по жалобе владельца «не могу переместить
	//   вещи в сумку 5, 6»).
	//   ПРИЧИНА (снята по данным + живому дампу): у сумок 1…4 каждая ячейка в `.JUI` имеет idc
	//   (`WAREBAG1_TRAY00`…), а у сумок 5…8 ячейки объявлены с ПУСТЫМ idc — `ID = ""`, при живом
	//   `IDNum` 75411…75440 / 75511… и т.д. Импортёр именует виджеты по idc, поэтому в ассете они
	//   стали безымянными `NoImage_icon_<n>_0`, поиск по `WAREBAG5_TRAY00` их не находил, сетка
	//   оставалась пустой — и положить в эти сумки было НЕЧЕМ и НЕКУДА.
	//   ЛЕЧЕНИЕ: если именованных ячеек у сумки не нашлось, берём Image-детей её группы
	//   `WAREBAG{n}_TRAYIcon_grp` ПО ПОРЯДКУ — в `.JUI` они идут ровно как idnum 75411…75440.
	for (int32 k = 0; k < kBags; ++k)
	{
		const bool bHasNamed = BagIconNames[k].ContainsByPredicate(
			[](const FName& N) { return !N.IsNone(); });
		if (bHasNamed) { continue; }

		UWidget* IconGrp = nullptr;
		const FString GrpName = FString::Printf(TEXT("WAREBAG%d_TRAYIcon_grp"), k + 1);
		for (UWidget* W : All) { if (W && W->GetName() == GrpName) { IconGrp = W; break; } }
		UPanelWidget* P = Cast<UPanelWidget>(IconGrp);
		if (!P) { continue; }

		int32 c = 0;
		for (int32 i = 0; i < P->GetChildrenCount() && c < kWareBagCells; ++i)
		{
			UImage* Img = Cast<UImage>(P->GetChildAt(i));
			// `_HIT`-подложки группы — не ячейки; ячейки носят скин `NoImage_icon`.
			if (!Img || !Img->GetName().StartsWith(TEXT("NoImage_icon"))) { continue; }
			BagCellUi[k * kWareBagCells + c].Icon = Img;
			BagIconNames[k][c] = FName(*Img->GetName());
			++c;
		}
		UE_LOG(LogAsdaWare, Log,
			TEXT("[склад] сумка %d: ячейки БЕЗ idc в данных — привязано по порядку %d из %d"),
			k + 1, c, kWareBagCells);
	}

	// ── ПОДДЕРЕВЬЯ по префиксу имени (★★★ закон `jui-uneven-node-layout`: гасить/показывать
	//    надо ВСЁ поддерево, а не один узел; иерархия JUI местами расплющена). ──
	auto CollectSubtree = [&All](UWidget* Root, TArray<TObjectPtr<UWidget>>& Out)
	{
		if (!Root) { return; }
		Out.AddUnique(Root);
		TArray<UWidget*> Stack; Stack.Add(Root);
		while (Stack.Num() > 0)
		{
			UWidget* Cur = Stack.Pop();
			if (UPanelWidget* P = Cast<UPanelWidget>(Cur))
			{
				const int32 Cnt = P->GetChildrenCount();
				for (int32 i = 0; i < Cnt; ++i)
				{
					if (UWidget* Ch = P->GetChildAt(i)) { Out.AddUnique(Ch); Stack.Add(Ch); }
				}
			}
		}
	};
	CollectSubtree(TabBlockRoot, TabBlockParts);
	// ★★★ ПРАВКА 2026-08-27 (жалоба владельца: в режиме «ХРАНИТЬ» вверху окна оставалась пустая
	//   чёрная область на месте блока «Дополнительное хранилище»).
	//   ПРИЧИНА: в `.JUI` подпись 4190, обе подложки (`WareUi_BagBase`, `WareUi_BagBase_2` 4195) и
	//   обе четвёрки чек-кнопок — ДЕТИ `WAREKEEP_TOP` (4001), но ИМПОРТЁР РАСПЛЮЩИЛ ИЕРАРХИЮ
	//   (закон `jui-flattened-hierarchy-geometry`), и в ассете они лежат РЯДОМ с 4001, а не внутри.
	//   Обход поддерева их поэтому не находил, и гасился только сам ярлык 4001.
	//   ЛЕЧЕНИЕ: добираем узлы блока ПО ИМЕНАМ из данных — независимо от того, сохранил импортёр
	//   вложенность или нет. Список закрыт (не префикс `WAREKEEP_`), чтобы не зацепить сетку и
	//   счётчик, которые в режиме «хранить» обязаны остаться видимыми.
	{
		static const TCHAR* kTabBlockIdc[] = {
			TEXT("WAREKEEP_TOP"),        // 4001 — сам ярлык-контейнер
			TEXT("WAREKEEP_TXT06"),      // 4190 — подпись «Дополнительное хранилище»
			TEXT("WareUi_BagBase"),      // подложка первого ряда кнопок-сумок
			TEXT("WareUi_BagBase_2"),    // 4195 — подложка второго ряда (в данных Visible=1 принудительно)
		};
		int32 Added = 0;
		for (UWidget* W : All)
		{
			if (!W) { continue; }
			const FString N = W->GetName();
			bool bHit = false;
			for (const TCHAR* Idc : kTabBlockIdc)
			{
				// Узел может называться и `Idc`, и `Idc_grp`, и `Idc_<n>_<n>` (план) — сверяем начало.
				if (N.StartsWith(Idc)) { bHit = true; break; }
			}
			// Чек-кнопки вкладок: 4004…4007 (`WAREUI_BAG00…03`) и 4301…4304 (`…_2`).
			if (!bHit && N.StartsWith(TEXT("WAREUI_BAG"))) { bHit = true; }
			if (bHit && !TabBlockParts.Contains(W)) { TabBlockParts.AddUnique(W); ++Added; }
		}
		UE_LOG(LogAsdaWare, Log,
			TEXT("[склад] блок вкладок: %d узлов из поддерева + %d добрано по именам (иерархия расплющена)"),
			TabBlockParts.Num() - Added, Added);
	}
	for (int32 k = 0; k < kBags; ++k)
	{
		CollectSubtree(Bags[k].Root, Bags[k].Parts);
		// SEL-рамка сумки в `.JUI` объявлена `Visible = 0` и включается только на перетаскивании —
		//   гасим её ПОСЛЕ общего показа поддерева, иначе она всплывает вместе с окном.
		if (Bags[k].Sel) { Bags[k].AlwaysHidden.AddUnique(Bags[k].Sel.Get()); }
		// ★★★ Дизайн-позиция и РАЗМЕР окна-сумки — ИЗ ДАННЫХ `.JUI`, а не из ассета
		//   (см. `kWareBagLayout` в заголовке: импортёр расплющил иерархию, и окна вставали
		//   не на свои места, налезая друг на друга).
		if (Bags[k].Root)
		{
			// `Origin` из `.JUI` задан ОТНОСИТЕЛЬНО окна 4000 — храним его как есть, а экранную
			//   позицию считаем в момент показа (окно можно перетащить, см. `PlaceBagWindow`).
			Bags[k].DesignPos = kWareBagLayout[k].Origin;
			// Размер выставляем сразу: он от позиции окна не зависит.
			AsdaUi::SetSlotRect(Bags[k].Root.Get(),
				GetWareWinOrigin() + kWareBagLayout[k].Origin, kWareBagLayout[k].Size);
		}
	}

	// ── Сетки иконок через ОБЩУЮ точку (закон `port-shared-points`) ──
	//   ★ Клик-оверлеи ОТКЛЮЧЕНЫ: ячейки окна 4000 заданы ЦЕПОЧКОЙ ВЛОЖЕННОСТИ
	//   (`WAREKEEP_TRAYIcon_grp` → 30 икон; корзина — в `WareKeep_TrayBase1_grp`), и вычисленные
	//   зоны съезжают на чужие панели. Клик и перетаскивание ловим по ЖИВОЙ ГЕОМЕТРИИ
	//   (`AsdaUi::IsCursorOverWidget`) — закон `jui-cell-chain-nesting`.
	GridIcons = NewObject<UAsdaIconCellGrid>(this);
	GridIcons->SetCellSizePx((float)kCellPx);
	GridIcons->SetClickOverlayEnabled(false);
	GridIcons->bClearEmptyCells = true;
	GridIcons->SetupNamed(Widget, GridIconNames, /*bMale*/true,
		[this](int32 Cell) -> int32
		{
			const FAsdaItemInstance* It = SlotPtrByCtrl(WareIds::kTrayBase[0] + Cell);
			return It ? It->ItemCode : 0;
		},
		[](int32) {});

	BasketIcons = NewObject<UAsdaIconCellGrid>(this);
	BasketIcons->SetCellSizePx((float)kCellPx);
	BasketIcons->SetClickOverlayEnabled(false);
	BasketIcons->bClearEmptyCells = true;
	BasketIcons->SetupNamed(Widget, BasketIconNames, /*bMale*/true,
		[this](int32 Cell) -> int32 { return Basket.IsValidIndex(Cell) ? Basket[Cell].ItemCode : 0; },
		[](int32) {});

	for (int32 k = 0; k < kBags; ++k)
	{
		UAsdaIconCellGrid* G = NewObject<UAsdaIconCellGrid>(this);
		G->SetCellSizePx((float)kCellPx);
		G->SetClickOverlayEnabled(false);
		G->bClearEmptyCells = true;
		const int32 Tab = k + 1;
		G->SetupNamed(Widget, BagIconNames[k], /*bMale*/true,
			[this, Tab](int32 Cell) -> int32
			{
				const int32 Abs = Tab * kWareBagCells + Cell;
				return Store.IsValidIndex(Abs) ? Store[Abs].ItemCode : 0;
			},
			[](int32) {});
		Bags[k].Grid = G;
	}

	// ── Кнопки. Диспетчер эталона — `uiStore_handleEvent@0xE87790`. ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnClose, 4050, [this](int32) { CloseWindow(); });                 // событие 66561
	Bind(BtnKeep,  4002, [this](int32) { OnKeepClicked(); });
	Bind(BtnTake,  4049, [this](int32) { OnTakeClicked(); });
	Bind(BtnCat0,  4188, [this](int32) { OnCategoryChanged(kCatSpend); });  // событие 66817
	Bind(BtnCat1,  4188, [this](int32) { OnCategoryChanged(kCatShell); });
	for (int32 k = 0; k < kBags; ++k)
	{
		const int32 Tab = k + 1;
		// Крестик окна-сумки → `storageUIClearTab(this, 1..8)` (событие 66561).
		Bind(Bags[k].Close, WareIds::kBagWindow[k], [this, Tab](int32) { OnBagButtonToggled(Tab, false); });
		// Чек-кнопка вкладки: 67073 «включена» / 67074 «выключена».
		if (UCheckBox* Cb = Bags[k].Btn.Get())
		{
			UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
			R->Index = WareIds::kBagButton[k];
			R->CheckCallback = [this, Tab](int32, bool bOn) { OnBagButtonToggled(Tab, bOn); };
			Cb->OnCheckStateChanged.AddDynamic(R, &UAsdaButtonRelay::OnCheckChanged);
			AsdaUi::AnchorRelay(Relays, R, Cb);
		}
	}

	// ── Статичные подписи (эталон ставит их в `CWarehouseUI_Build`/`Warehouse_InitControls`) ──
	auto SetLoc = [this](UTextBlock* Tb, int32 LocId)
	{
		if (!Tb) { return; }
		const FString S = Loc(LocId);
		if (!S.IsEmpty()) { Tb->SetText(FText::FromString(S)); }
	};
	SetLoc(TxtFeeLbl,  WareLoc::kFeeLabel);
	SetLoc(TxtGoldLbl, WareLoc::kGoldLabel);
	SetLoc(TxtTop,     WareLoc::kTopLabel);
	SetLoc(TxtExtra,   WareLoc::kExtraLabel);
	SetLoc(CapKeep,    WareLoc::kBtnKeep);
	SetLoc(CapTake,    WareLoc::kBtnTake);
	for (int32 k = 0; k < kBags; ++k)
	{
		// ★ D18/§5.1: наивный расчёт `12139 + k` для k ≥ 4 даёт МУСОР («Required Assembly Skill
		//   Level», «System», «Haha», «Suddenly»), поэтому эталон для сумок 5…8 перезаписывает
		//   подпись нотисом `31997 + k` = 32001…32004. Мы сразу берём правильный нотис — двойной
		//   записи (сам дефект D18) не делаем.
		SetLoc(Bags[k].Title.Get(), (k < 4) ? (WareLoc::kBagTitle1_4 + k) : (WareLoc::kBagTitle5_8 + (k - 4)));
	}

	// ── Полоса прокрутки 4189 (её в WBP нет — см. комментарий у kListWidth) ──
	if (UCanvasPanel* Root = Cast<UCanvasPanel>(Widget->WidgetTree->RootWidget))
	{
		ScrollBar.Build(Widget, Root, TEXT("WAREKEEP_LIST"), /*ZTrack*/ 20);
	}

	// ── ОБВОДКА ТЕКСТА. Закон `ui-text-outline-readability`: у ТЁМНОГО текста контур 1 px из скина
	//    съедает букву. Тёмный здесь ровно один — счётчик «занято/всего» 4003 (`EXS TextColor`
	//    = FF323232) и подпись переключателя 4188 (FF373737 по данным WBP). С них обводку снимаем,
	//    остальным оставляем 1 px.
	{
		// ★★★ ОБВОДКА СНЯТА С ОКНА ЦЕЛИКОМ (правка 2026-08-27, реверс поля 926).
		//   ЧТО БЫЛО: здесь стоял `ApplyTextOutlineTree(Widget, 1, …)` — обводка ставилась ВСЕМУ
		//   окну, а точечными исключениями снималась у тех подписей, на которые жаловался владелец.
		//   Это лечило симптом: по данным оригинала окну 4000 обводка НЕ ПОЛОЖЕНА НИ ОДНОМУ
		//   контролу — все 275 его текстов имеют ТЕНЬ (`0x400000`), а не обводку (`0x800000`)
		//   (`Tools/data/text_style926.tsv`, отчёт `Reports/text_style926_table.md`).
		//   Ошибка шла от импортёра: он ставит обводку по скиновому `TextOutLine`, а это поле 925
		//   (флаги ПЕРЕНОСА строк), обводку же даёт поле 926.
		//   КАК СТАЛО: обводка приведена к данным оригинала В САМИХ АССЕТАХ шагом пост-импорта
		//   `Tools/asda_ui_text_style926.py` (снята у 4613 текстов, включена у 30), поэтому
		//   рантайм-простановка здесь не нужна и ВРЕДНА — она перезаписала бы исправленный ассет.
	}

	// ★ D15: снять с надписи 4038 приём мыши (у неё скин КНОПКИ, но ветки обработки нет).
	if (CVarWareLegacyHeaderHit.GetValueOnGameThread() == 0 && TxtHeader)
	{
		TxtHeader->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	// Шестерёнка: статичная видна, анимированная погашена (эталон при инициализации).
	if (Gear)    { Gear->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	if (AniGear) { AniGear->SetVisibility(ESlateVisibility::Collapsed); }
	HideSelFrames();

	int32 IconsFound = 0;
	for (const FAsdaWareCellUi& U : GridUi)    { if (U.Icon) { ++IconsFound; } }
	int32 BagIconsFound = 0;
	for (const FAsdaWareCellUi& U : BagCellUi) { if (U.Icon) { ++BagIconsFound; } }
	bControlsReady = (IconsFound == kGridCells) && (BasketUi[0].Icon != nullptr)
		&& (BtnKeep != nullptr) && (BtnTake != nullptr);

	UE_LOG(LogAsdaWare, Log,
		TEXT("[склад] окно 4000 разобрано: сетка %d/%d икон, корзина %d, сумок %d (икон %d/%d), ")
		TEXT("кнопки 4002/4049/4050 = %s/%s/%s, категории 4188 = %s/%s, блок вкладок 4001 = %s (узлов %d), ")
		TEXT("полоса 4189 = %s → %s"),
		IconsFound, kGridCells, BasketUi[0].Icon ? kBasketCells : 0, kBags, BagIconsFound, kBags * kWareBagCells,
		BtnKeep ? TEXT("да") : TEXT("НЕТ"), BtnTake ? TEXT("да") : TEXT("НЕТ"), BtnClose ? TEXT("да") : TEXT("НЕТ"),
		BtnCat0 ? TEXT("да") : TEXT("НЕТ"), BtnCat1 ? TEXT("да") : TEXT("НЕТ"),
		TabBlockRoot ? TEXT("да") : TEXT("НЕТ"), TabBlockParts.Num(),
		ScrollBar.IsBuilt() ? TEXT("построена") : TEXT("НЕТ"),
		bControlsReady ? TEXT("ГОТОВО") : TEXT("НЕПОЛНО"));
}

void UAsdaWarehouseSubsystem::RegisterZones()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	WM->RegisterUiZone(kWareWindowId, FAsdaUiZoneRect::CreateWeakLambda(this,
		[this](FVector2D& Mn, FVector2D& Mx) -> bool
		{
			// Корпус окна — девять планов рамки (закон «rect только по узлам, которые окно и образуют»:
			//   корень — канвас во весь вьюпорт, от него зона глушила бы орбит-камеру всюду).
			if (Mode == kModeClosed || !Widget) { return false; }
			if (!AsdaUi::FramePlanesAbsRect(Widget, TEXT("WARE_WIN_0_%d"), 9, Mn, Mx)) { return false; }
			// ★ Открытые окна-сумки — ОТДЕЛЬНЫЕ прямоугольники ВНЕ корпуса 4000; без них клик по
			//   сумке уходил бы в мир (окно-сумка живёт в том же виджете, но правее и ниже).
			for (int32 k = 0; k < kBags; ++k)
			{
				if (!bBagOpen[k] || !Bags[k].Root) { continue; }
				FVector2D BMn, BMx;
				if (AsdaUi::WidgetAbsRect(Bags[k].Root, BMn, BMx))
				{
					Mn = FVector2D(FMath::Min(Mn.X, BMn.X), FMath::Min(Mn.Y, BMn.Y));
					Mx = FVector2D(FMath::Max(Mx.X, BMx.X), FMath::Max(Mx.Y, BMx.Y));
				}
			}
			return true;
		}));
	// Колесо над СПИСКОМ 4189 листает список инвентаря (эталонное событие 23 у контрола 4189).
	// ★ 10.10.2026 (whwheel): раньше обработчик возвращал true без проверки курсора — пока склад открыт в режиме
	//   «Хранить», колесо съедалось в ЛЮБОЙ точке экрана и зум камеры над миром не работал. Эталон:
	//   слот 7 менеджера окон `uiControlDispatchHitTestToChildren@0x12AF9E0` → `uiControl_dispatchEvent20a` →
	//   `CControl_OnEvent20a@0x12DC910` — событие 20 только контролу, чей план под курсором (vtable+39
	//   `uiPlanePixelHitTest`); у `CWheelWindow` (4189) слот 37 `sub_12F3480` двигает полосу, полоса шлёт 69633…
	//   → событие 23 (`sub_12F3540`). Над остальной частью окна список не листается, над миром — зум
	//   (game_main.c:5864, `!CControl_HitTest` → `Camera_MouseWheelZoom`).
	WM->RegisterWheelHandler(kWareWindowId, FAsdaUiWheelHandler::CreateWeakLambda(this,
		[this](int32 Notches) -> bool
		{
			if (Mode != kModeKeep || !Widget) { return false; }
			FVector2D Mn, Mx;
			if (!GetKeepListAbsRect(Mn, Mx) || !FSlateApplication::IsInitialized()) { return false; }
			const FVector2D Cur = FSlateApplication::Get().GetCursorPos();   // курсор в абсолютных координатах Slate
			if (Cur.X < Mn.X || Cur.X > Mx.X || Cur.Y < Mn.Y || Cur.Y > Mx.Y) { return false; }
			SetScrollRow(ScrollRow - Notches);
			return true;
		}));
}
