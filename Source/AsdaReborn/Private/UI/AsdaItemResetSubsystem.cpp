// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окна `Item_Initializ_Win` (JUI 20200) — сброс/восстановление улучшений предмета.
//   Весь разбор эталона, адреса и раскладки пакетов — в шапке `AsdaItemResetSubsystem.h`
//   и в отчёте `Reports/npc_reset_upgrade_30046.md`.

#include "UI/AsdaItemResetSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaDevConsole.h"   // ★ 25.10.2026 (ревизия волны, М-4): ASDA_DEV_CVAR — в Shipping ключ становится константой
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaDataTableCache.h"
#include "Items/AsdaItemDbRows.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaWindowSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaItemReset, Log, All);
// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): список кандидатов — вся сумка.
static_assert(UAsdaItemResetSubsystem::kListMax == UAsdaInventorySubsystem::kShellSlots, "длина списка разошлась с ёмкостью сумки");

namespace
{
	// Окно и его виджет. Ассет уже импортирован заходом JUI.
	const FName  kWindowId(TEXT("Item_Initializ_Win"));
	const TCHAR* kWindowWbp =
		TEXT("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Item_Initializ_Win.WBP_Item_Initializ_Win_C");

	// Таблицы цен и множителей (обе уже есть в проекте).
	const TCHAR* kPriceTablePath = TEXT("/Game/AsdaAssets/Data/ItemDB/DT_pricetable.DT_pricetable");
	const TCHAR* kGradeRatePath  = TEXT("/Game/AsdaAssets/Data/ItemDB/DT_graderate.DT_graderate");

	// Подтипы свитков в `DT_special_itemlist` (сервер проверяет ровно эти числа).
	constexpr int32 kScrollWeapon = 210;
	constexpr int32 kScrollArmor  = 211;

	// Фильтр списка кандидатов (`uiInventoryCraftListRefresh@0xD2F360`): в список НЕ попадают
	//   предметы с `ItemRecord->itemSubType` из этой тройки. Никаких других условий эталон
	//   на этом шаге не ставит — годность проверяется уже при добавлении в очередь целей.
	FORCEINLINE bool PassesListFilter(int32 ItemSubType)
	{
		return ItemSubType != 66 && ItemSubType != 200 && ItemSubType != 201;
	}

	// Байт записи предмета по смещению: полей +25/+26 в `FAsdaItemInstance` нет, читаем из `Raw`.
	FORCEINLINE int32 RecByte(const FAsdaItemInstance& It, int32 Off)
	{
		return It.Raw.IsValidIndex(Off) ? (int32)It.Raw[Off] : 0;
	}

	// ── Раскладка тела 6507 (154 б), ветка успеха — сервер `recvDBShellItemUpgradeReset@0x501A80` (GameServer.c:99531) ──
	constexpr int32 kOffWeight   = 1;    // i16 — новый вес игрока (`sub_43FA00`)
	constexpr int32 kOffGold     = 3;    // i32 — новое золото (`sub_4D7690`)
	constexpr int32 kOffTarget   = 7;    // 57 б — запись цели после сброса (+24 уровень, +25/+26 попытки, +27 процент)
	constexpr int32 kOffMats     = 64;   // 10 × 9 б — записи материалов (свитки режима 1)
	constexpr int32 kMatStride   = 9;    // u8 контейнер · i16 слот · i32 количество · i16 вес стека
	constexpr int32 kMatCount    = 10;   // столько же, сколько слотов материалов в запросе 6506
}

// ★★★ [ИСПРАВЛЕНИЕ] 25.10.2026 — применять ответ 6507 к модели инвентаря (разбор — у `ApplyResultToInventory` в .h).
//   1 — как эталон `uiInventoryOnRecvSlotUpdate@0xD31430` case 1: золото, вес, запись цели, записи материалов;
//   0 — прежний порт: только нотис, предмет остаётся улучшенным до перезахода.
//   ★ Ревизия волны (М-4): объявлен через `ASDA_DEV_CVAR`, как остальные ключи волны (в Shipping — константа 1, имени в exe нет).
static ASDA_DEV_CVAR(int32, CVarItemResetApplyResult,
	TEXT("asda.ItemReset.ApplyResult"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 25.10.2026: ответ 6507 (сброс/восстановление улучшений) кладёт в инвентарь новую запись предмета, "
	     "свитки, золото и вес, как оригинал. 0 — прежнее поведение порта (только нотис, предмет улучшен до перезахода)."),
	ECVF_Default);

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaItemResetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★★★ РОСПИСЬ «КАК ЗАКРЫТЬ СЕБЯ ПО ESC» — порт события 21 (`CControl_OnEvent21@0x12DCC30`).
	//   Окно `Item_Initializ_Win` (IDNum 20200) стоит в эталонной таблице `kEscCloseWindows`: признак
	//   `CControl+0x228` ему ставит `CItemInitializeWin_InitControls@0xD2DCA0`. Проход
	//   `UAsdaWindowSubsystem::CloseTopmostOnEsc` закрывает верхнее окно ИМЕННО ЭТИМ вызовом,
	//   а не снятием виджета: снятие оставило бы подсистеме состояние «открыто», и окно
	//   больше не открылось бы. Закрыватель идемпотентен — повторный вызов ничего не делает.
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	if (UAsdaWindowSubsystem* EscWM = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		EscWM->RegisterEscCloser(kWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaItemResetSubsystem::CloseWindow));
	}
	ListIcons.SetNum(kListCells);
	ListLabels.SetNum(kListCells);
	TargetIcons.SetNum(kTargetCells);
	TargetLabels.SetNum(kTargetCells);
	DisableGroups.SetNum(kListCells);
	List.SetNum(kListMax);   // ★ 29.10.2026 (инвентарь ×2): кандидаты — вся сумка (до 60); сетка — окно из 30 (`BagScroll`)
	// ★★★ [ПОРТ] 29.10.2026 (инвентарь ×2): КОЛЕСО НАД СЕТКОЙ КАНДИДАТОВ — прокрутка по рядам (`BagScroll`,
	//   шапка `AsdaBagListScroll.h`). Сетка эталона 20240…20269 — 30 без страниц.
	if (UAsdaWindowSubsystem* WheelWM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WheelWM->RegisterWheelHandler(kWindowId, FAsdaUiWheelHandler::CreateWeakLambda(this, [this](int32 Notches) -> bool
		{
			if (!bOpen || !bControlsReady) { return false; }
			bool bOver = false;
			for (const TObjectPtr<UImage>& I : ListIcons) { if (I && AsdaUi::IsCursorOverWidget(I)) { bOver = true; break; } }
			if (!bOver) { return false; }
			if (BagScroll.Wheel(Notches))
			{
				TipListCell = -1;        // плашка прежней позиции — к другой вещи
				CellDbl.Reset();         // двойной щелчок не склеивается через прокрутку
				RebuildList();
			}
			return true;
		}));
	}

	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь.
		HResetResult = Net->OnItemResetResult.AddLambda(
			[this](const TArray<uint8>& B) { PendingResults.Push(B); });
	}
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		// Один ответ 6507 двигает до 11 ячеек — перестраиваем один раз в Tick, а не на каждой.
		HInvCell = Inv->OnCellChanged.AddLambda(
			[this](int32 /*Container*/, int32 /*Cell*/) { bDirty = true; });
	}
	UE_LOG(LogAsdaItemReset, Log, TEXT("[сброс улучшений] подсистема поднята"));
}

void UAsdaItemResetSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnItemResetResult.Remove(HResetResult);
	}
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		Inv->OnCellChanged.Remove(HInvCell);
	}
	ResetWidget();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaItemResetSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	CloseWindow();
	bDirty = false;
	bConfirmOpen = false;
	bArmed = false;
	bReady = true;
	bSelectedValid = false;
	BagScroll.Reset();   // ★ 29.10.2026 (инвентарь ×2)
	TotalGold = 0; NeedWeapon = 0; NeedArmor = 0; HaveWeapon = 0; HaveArmor = 0;
	TipTargetCell = -1;
	CellDbl.Reset();
	PendingResults.Clear();
	Mouse.Reset();
	(void)Reason;
}

void UAsdaItemResetSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.ItemReset");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	DrainPendingPackets();
	if (!bOpen || !bControlsReady) { return; }
	// ★ Насос отправки стоит в эталоне в ГЛАВНОМ ТИКЕ (`game_main.c:1479`), то есть работает
	//   независимо от ввода — очередь целей уходит по одному пакету на каждый пришедший ответ.
	PumpSendQueue();
	if (bDirty) { bDirty = false; RebuildList(); RebuildTargets(); }
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		if (TipListCell >= 0 || TipTargetCell >= 0)   // висящую плашку ГАСИМ, а не просто «не показываем»
		{
			TipListCell = -1; TipTargetCell = -1;
			if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
			{
				Tip->HideTooltipFor(EAsdaTipOwner::ItemReset);
			}
		}
		// Накопитель двойного щелчка — тоже защёлка: без сброса клик ДО нотиса и клик ПОСЛЕ его
		//   закрытия слиплись бы в двойной, а он здесь отправляет предмет на сброс улучшений.
		CellDbl.Reset();
		Mouse.Reset();
		return;
	}
	TickCells();
}

// ═════════════════════════════ ОКНО ═══════════════════════════════════════════════════════════

bool UAsdaItemResetSubsystem::EnsureWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWindowWbp, kResetZ).Ensure(GetGameInstance(), Widget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem&)
		{
			SetupControls();
			RegisterZone();
		},
		[]()
		{
			UE_LOG(LogAsdaItemReset, Error,
				TEXT("[сброс улучшений] WBP_Item_Initializ_Win не загрузился (%s)"), kWindowWbp);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaItemResetSubsystem::ResetWidget()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kWindowId);
		WM->CloseWindow(kWindowId);
	}
	Widget = nullptr;
	bControlsReady = false;
	ListIcons.Reset();   ListIcons.SetNum(kListCells);
	ListLabels.Reset();  ListLabels.SetNum(kListCells);
	TargetIcons.Reset(); TargetIcons.SetNum(kTargetCells);
	TargetLabels.Reset();TargetLabels.SetNum(kTargetCells);
	DisableGroups.Reset(); DisableGroups.SetNum(kListCells);
	GroupFullReset.Reset();
	GroupRestore.Reset();
	Relays.Reset();
	GridList = nullptr; GridTarget = nullptr; GridSel = nullptr;
	Mouse.Reset();
}

void UAsdaItemResetSubsystem::RegisterZone()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Рамка окна — девять планов `Item_Initializ_Win_0_0…8` (скин `Black_BottomRound_White2Border`,
	//   ResizeWindow из девяти кусков).
	// ★ Волна 3: зона по планам рамки — общая точка `FAsdaJuiWindowHandle::RegisterFrameZone` (`!bOpen || !Widget` → нет зоны).
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kWindowId, this, Widget, bOpen, TEXT("Item_Initializ_Win_0_%d"), 9);
}

void UAsdaItemResetSubsystem::CollectByIdc(const FString& Idc, TArray<TObjectPtr<UWidget>>& Out) const
{
	// ★ R5-b 12.11.2026: тело — общая точка `AsdaUi::CollectByIdc` (дословно то же; разбор имён и комментарии — там).
	AsdaUi::CollectByIdc(Widget, Idc, Out);
}

void UAsdaItemResetSubsystem::SetGroupVisible(const TArray<TObjectPtr<UWidget>>& Group, bool bVisible)
{
	// Декоративным планам — `HitTestInvisible`: `SelfHitTestInvisible` НЕ мешает ДЕТЯМ ловить мышь,
	//   а маски/подложки не должны съедать клики по ячейкам под ними.
	for (const TObjectPtr<UWidget>& W : Group)
	{
		if (W)
		{
			AsdaUi::SetShownNoHit(W, bVisible);
		}
	}
}

void UAsdaItemResetSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }

	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);

	TArray<FName> ListIconNames;   ListIconNames.SetNum(kListCells);
	TArray<FName> TargetIconNames; TargetIconNames.SetNum(kTargetCells);
	FName SelIconName;

	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();

		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("Item_Initializ_CloseBtn")) { BtnClose  = Btn; continue; }
			if (N == TEXT("Item_Initializ_Btn"))      { BtnOk     = Btn; continue; }
			if (N == TEXT("Item_Initializ_Cancel"))   { BtnCancel = Btn; continue; }
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == TEXT("Item_Initializ_Title"))        { TxtTitle    = Tb; continue; }
			if (N == TEXT("Item_Initializ_SelItemName"))  { TxtSelName  = Tb; continue; }
			if (N == TEXT("Item_Initializ_Grade"))        { TxtGradeLbl = Tb; continue; }
			if (N == TEXT("Item_Initializ_GradeBefore"))  { TxtGradeBef = Tb; continue; }
			if (N == TEXT("Item_Initializ_GradeAfter"))   { TxtGradeAft = Tb; continue; }
			if (N == TEXT("Item_Initializ_Power"))        { TxtPowerLbl = Tb; continue; }
			if (N == TEXT("Item_Initializ_PowerBefore"))  { TxtPowerBef = Tb; continue; }
			if (N == TEXT("Item_Initializ_PowerAfter"))   { TxtPowerAft = Tb; continue; }
			if (N == TEXT("Item_Initializ_Chance"))       { TxtChanceLbl= Tb; continue; }
			if (N == TEXT("Item_Initializ_ChanceBefore")) { TxtChanceBef= Tb; continue; }
			if (N == TEXT("Item_Initializ_ChanceAfter"))  { TxtChanceAft= Tb; continue; }
			if (N == TEXT("Item_Initializ_GuideTxt"))     { TxtGuide    = Tb; continue; }
			if (N == TEXT("Item_Initializ_TargetTitle_Text")) { TxtTargetTitle = Tb; continue; }
			if (N == TEXT("Item_Initializ_IniFeeTxt"))    { TxtFeeLbl   = Tb; continue; }
			if (N == TEXT("Item_Initializ_IniFee"))       { TxtFee      = Tb; continue; }
			if (N == TEXT("Item_Initializ_GoldTxt"))      { TxtGoldLbl  = Tb; continue; }
			if (N == TEXT("Item_Initializ_Gold"))         { TxtGold     = Tb; continue; }
			if (N == TEXT("Item_Initializ_IniFeeTxt2"))   { TxtNeedWpnLbl = Tb; continue; }
			if (N == TEXT("Item_Initializ_IniFee2"))      { TxtNeedWpn    = Tb; continue; }
			if (N == TEXT("Item_Initializ_ScrollTxt2"))   { TxtHaveWpnLbl = Tb; continue; }
			if (N == TEXT("Item_Initializ_Scroll2"))      { TxtHaveWpn    = Tb; continue; }
			if (N == TEXT("Item_Initializ_IniFeeTxt3"))   { TxtNeedArmLbl = Tb; continue; }
			if (N == TEXT("Item_Initializ_IniFee3"))      { TxtNeedArm    = Tb; continue; }
			if (N == TEXT("Item_Initializ_ScrollTxt3"))   { TxtHaveArmLbl = Tb; continue; }
			if (N == TEXT("Item_Initializ_Scroll3"))      { TxtHaveArm    = Tb; continue; }
			// Подписи ячеек: импортёр зовёт `TextBlock` ровно как idc контрола.
			bool bTaken = false;
			for (int32 C = 0; C < kListCells && !bTaken; ++C)
			{
				if (N == FString::Printf(TEXT("Item_Initializ_Item%02d"), C)) { ListLabels[C] = Tb; bTaken = true; }
			}
			for (int32 C = 0; C < kTargetCells && !bTaken; ++C)
			{
				if (N == FString::Printf(TEXT("Item_Initializ_Target%02d"), C)) { TargetLabels[C] = Tb; bTaken = true; }
			}
			if (bTaken) { Tb->SetText(FText::GetEmpty()); }
			continue;
		}

		// Планы: `<idc>_<juiIdx>_<plane>`.
		FString Idc; int32 JuiIdx = 0, Plane = 0;
		if (AsdaUi::ParsePlaneName(N, Idc, JuiIdx, Plane))
		{
			if (Idc == TEXT("Item_Initializ_ItemSel"))   { ListSelFrame   = W; continue; }
			if (Idc == TEXT("Item_Initializ_TargetSel")) { TargetSelFrame = W; continue; }
			if (UImage* Img = Cast<UImage>(W))
			{
				if (Idc == TEXT("Item_Initializ_SelItem")) { ImgSelItem = Img; SelIconName = FName(*N); continue; }
				bool bTaken = false;
				for (int32 C = 0; C < kListCells && !bTaken; ++C)
				{
					if (Idc == FString::Printf(TEXT("Item_Initializ_Item%02d"), C))
					{
						ListIcons[C] = Img; ListIconNames[C] = FName(*N); bTaken = true;
					}
				}
				for (int32 C = 0; C < kTargetCells && !bTaken; ++C)
				{
					if (Idc == FString::Printf(TEXT("Item_Initializ_Target%02d"), C))
					{
						TargetIcons[C] = Img; TargetIconNames[C] = FName(*N); bTaken = true;
					}
				}
			}
		}
	}

	// ── Маски 20280…20309 и блоки-режимы 20390/20391 — целыми поддеревьями ──
	for (int32 C = 0; C < kListCells; ++C)
	{
		CollectByIdc(FString::Printf(TEXT("Item_Initializ_Disable%02d"), C), DisableGroups[C]);
	}
	CollectByIdc(TEXT("All_Reset"),      GroupFullReset);
	CollectByIdc(TEXT("OneTree_Reset"),  GroupRestore);

	// ── Сетки иконок через ОБЩУЮ точку. Клик ловим САМИ по живой геометрии (`TickCells`),
	//    поэтому кнопки-оверлеи не строим — иначе они съедят клики по ячейкам. ──
	GridList = NewObject<UAsdaIconCellGrid>(this);
	GridList->SetCellSizePx((float)kCellPx);
	GridList->SetClickOverlayEnabled(false);
	GridList->bClearEmptyCells = true;   // эталон затирает иконку пустой текстурой 32×32
	GridList->SetupNamed(Widget, ListIconNames, /*bMale*/true,
		// ★ 29.10.2026 (инвентарь ×2): видимая ячейка → позиция списка `List` через окно прокрутки.
		[this](int32 Cell) -> int32 { const int32 L = BagScroll.ToList(Cell); return List.IsValidIndex(L) ? List[L].ItemCode : 0; },
		[this](int32 Cell) { OnListCellClicked(BagScroll.ToList(Cell), /*bDouble*/false); });

	GridTarget = NewObject<UAsdaIconCellGrid>(this);
	GridTarget->SetCellSizePx((float)kCellPx);
	GridTarget->SetClickOverlayEnabled(false);
	GridTarget->bClearEmptyCells = true;
	GridTarget->SetupNamed(Widget, TargetIconNames, /*bMale*/true,
		[this](int32 Cell) -> int32 { return Targets.IsValidIndex(Cell) ? Targets[Cell].ItemCode : 0; },
		[this](int32 Cell) { OnTargetCellClicked(Cell, /*bDouble*/false); });

	// Иконка выбранного предмета 20203 — одна ячейка.
	if (!SelIconName.IsNone())
	{
		GridSel = NewObject<UAsdaIconCellGrid>(this);
		GridSel->SetCellSizePx(64.f);   // Size = "64 64" у `Item_Initializ_SelItem`
		GridSel->SetClickOverlayEnabled(false);
		GridSel->bClearEmptyCells = true;
		TArray<FName> One; One.Add(SelIconName);
		GridSel->SetupNamed(Widget, One, /*bMale*/true,
			[this](int32) -> int32 { return bSelectedValid ? Selected.ItemCode : 0; },
			[](int32) {});
	}

	// ── Кнопки ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	// Эталон `clanWindowOnButtonEvent@0xD30480`: 20201 и 20370 — ОДИН И ТОТ ЖЕ `sub_93C0A5`.
	// ★ Волна 9 заточки: кнопки окна — процедура инвентаря (группы 202/203), ветка 66561 играет звук id 5
	//   (ui_core.c:47649) ДО `clanWindowOnButtonEvent` (:47686) — общая точка `AsdaUi::PlayInvenButtonSfx`.
	Bind(BtnClose,  20201, [this](int32) { AsdaUi::PlayInvenButtonSfx(GetGameInstance()); OnCancelClicked(); });
	Bind(BtnCancel, 20370, [this](int32) { AsdaUi::PlayInvenButtonSfx(GetGameInstance()); OnCancelClicked(); });
	Bind(BtnOk,     20350, [this](int32) { AsdaUi::PlayInvenButtonSfx(GetGameInstance()); OnOkClicked(); });

	// ── Статичные подписи (`CItemInitializeWin_InitControls@0xD2DCA0`) ──
	auto SetLoc = [this](UTextBlock* Tb, int32 LocId)
	{
		if (!Tb) { return; }
		const FString S = Loc(LocId);
		if (!S.IsEmpty()) { Tb->SetText(FText::FromString(S)); }
	};
	SetLoc(TxtGradeLbl,   AsdaItemResetLoc::kLabelGrade);
	SetLoc(TxtPowerLbl,   AsdaItemResetLoc::kLabelPower);
	SetLoc(TxtChanceLbl,  AsdaItemResetLoc::kLabelChance);
	SetLoc(TxtTargetTitle,AsdaItemResetLoc::kLabelTargetTitle);
	SetLoc(TxtFeeLbl,     AsdaItemResetLoc::kLabelFee);
	SetLoc(TxtGoldLbl,    AsdaItemResetLoc::kLabelGold);
	SetLoc(TxtNeedWpnLbl, AsdaItemResetLoc::kNeedWeaponScroll);
	SetLoc(TxtNeedArmLbl, AsdaItemResetLoc::kNeedArmorScroll);
	SetLoc(TxtHaveWpnLbl, AsdaItemResetLoc::kHaveWeaponScroll);
	SetLoc(TxtHaveArmLbl, AsdaItemResetLoc::kHaveArmorScroll);

	// Рамки-курсоры в `.JUI` объявлены скрытыми — такими и оставляем (эталон их не трогает).
	if (ListSelFrame)   { ListSelFrame->SetVisibility(ESlateVisibility::Collapsed); }
	if (TargetSelFrame) { TargetSelFrame->SetVisibility(ESlateVisibility::Collapsed); }

	bControlsReady = (ListIcons[0] != nullptr && TargetIcons[0] != nullptr && BtnOk != nullptr);
	if (!bControlsReady)
	{
		UE_LOG(LogAsdaItemReset, Error,
			TEXT("[сброс улучшений] контролы не разобраны: иконка списка=%s цель=%s кнопка OK=%s"),
			ListIcons[0] ? TEXT("есть") : TEXT("НЕТ"),
			TargetIcons[0] ? TEXT("есть") : TEXT("НЕТ"),
			BtnOk ? TEXT("есть") : TEXT("НЕТ"));
	}
}

// ═════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ════════════════════════════════════════════

void UAsdaItemResetSubsystem::OpenWindow(int32 InMode)
{
	// Эталон: `this->gap_A74[1537] = a2 != 0` — любое ненулевое значение это режим 1.
	Mode = (InMode != 0) ? kModeRestore : kModeFullReset;

	if (!EnsureWidget()) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }

	// Эталон при открытии обнуляет выбранный предмет (`*(this+1520) = 0`) и чистит очередь целей
	//   (`sub_93B5A1(this+1508)`), но список кандидатов пересобирает целиком.
	Targets.Reset();
	Selected.Clear();
	bSelectedValid = false;
	bArmed = false;
	bReady = true;                 // `this->gap_A74[1581] = 1`
	bConfirmOpen = false;

	// Заголовок 20202 и подсказка 20230 — по режиму.
	if (TxtTitle)
	{
		const FString S = Loc(Mode == kModeRestore ? AsdaItemResetLoc::kTitleRestore
		                                           : AsdaItemResetLoc::kTitleFullReset);
		if (!S.IsEmpty()) { TxtTitle->SetText(FText::FromString(S)); }
	}
	// Эталон чистит имя выбранного предмета пустой строкой (`CControl_SetText(this[1012], &Src)`).
	if (TxtSelName) { TxtSelName->SetText(FText::GetEmpty()); }
	SetGuide(Mode == kModeRestore ? AsdaItemResetLoc::kGuideOpenRestore
	                              : AsdaItemResetLoc::kGuideOpenFull);
	ApplyModeVisibility();

	bOpen = true;
	Mouse.Reset();                 // иначе удержанная с прошлого кадра кнопка съест первый клик
	TipListCell = -1; TipTargetCell = -1;
	CellDbl.Reset();
	BagScroll.Reset();   // ★ 29.10.2026 (инвентарь ×2): с первого ряда списка

	RebuildList();
	RebuildTargets();
	UpdateInfoPanel();

	WM->SetWindowVisible(kWindowId, true);
	UE_LOG(LogAsdaItemReset, Log, TEXT("[сброс улучшений] окно 20200 открыто, режим=%d (%s)"),
		Mode, Mode == kModeRestore ? TEXT("восстановление попыток") : TEXT("полный сброс"));
}

void UAsdaItemResetSubsystem::CloseWindow()
{
	if (!bOpen) { return; }
	bOpen = false;
	// Незавершённую очередь бросаем: эталон при закрытии окна её не досылает.
	Targets.Reset();
	bArmed = false;
	bReady = true;
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::ItemReset);
	}
	if (UAsdaWindowSubsystem* WM = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWindowId, false);
	}
	Mouse.Reset();
}

void UAsdaItemResetSubsystem::ApplyModeVisibility()
{
	// `refineWnd_Open`: режим 0 → показать `All_Reset` (блок золота), скрыть `OneTree_Reset`;
	//   режим 1 — наоборот. Видимость в JUI НАСЛЕДУЕТСЯ, поэтому гасим контейнер целиком.
	SetGroupVisible(GroupFullReset, Mode == kModeFullReset);
	SetGroupVisible(GroupRestore,   Mode == kModeRestore);
}

void UAsdaItemResetSubsystem::SetGuide(int32 LocId)
{
	if (!TxtGuide) { return; }
	const FString S = Loc(LocId);
	if (!S.IsEmpty()) { TxtGuide->SetText(FText::FromString(S)); }
}

// ═════════════════════════════ НАПОЛНЕНИЕ ═════════════════════════════════════════════════════

void UAsdaItemResetSubsystem::RebuildList()
{
	for (FAsdaItemResetEntry& E : List) { E.Clear(); }
	HaveWeapon = 0;
	HaveArmor  = 0;

	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem*  Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>()  : nullptr;
	const UAsdaItemTableSubsystem*  IT  = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>()  : nullptr;
	if (Inv && IT)
	{
		// ★ 29.10.2026 (инвентарь ×2): эталон 30 ячеек сумки — у нас вся доступная сумка (до 60).
		const int32 Cells = FMath::Min(Inv->GetContainerSize(kBagContainer), kListMax);
		// ── Проход 1: сколько свитков в наличии (эталон считает ДО построения списка) ──
		for (int32 C = 0; C < Cells; ++C)
		{
			const FAsdaItemInstance* It = Inv->GetCell(kBagContainer, C);
			if (!It || It->IsEmpty()) { continue; }
			const int32 Sub = ScrollSubTypeOf(It->ItemCode);
			if (Sub == kScrollWeapon) { HaveWeapon += It->Amount; }
			else if (Sub == kScrollArmor) { HaveArmor += It->Amount; }
		}
		// ── Проход 2: кандидаты ──
		int32 Visible = 0;   // ★ 29.10.2026: длина списка (вся сумка); видимое окно — `BagScroll`
		for (int32 C = 0; C < Cells && Visible < kListMax; ++C)
		{
			const FAsdaItemInstance* It = Inv->GetCell(kBagContainer, C);
			if (!It || It->IsEmpty()) { continue; }
			const FAsdaItemRecord* Rec = IT->GetItem(It->ItemCode);
			if (!Rec || !PassesListFilter(Rec->ItemSubType)) { continue; }

			FAsdaItemResetEntry& E = List[Visible];
			E.ItemCode  = It->ItemCode;
			E.Container = (It->ContainerType > 0) ? (int32)It->ContainerType : kBagContainer;
			E.Slot      = (It->SlotIndex >= 0) ? It->SlotIndex : C;
			E.CellIndex = C;
			E.Level     = (int32)It->GemGrade;      // запись +24
			E.UsedTries = RecByte(*It, 25);         // запись +25
			E.MaxTries  = RecByte(*It, 26);         // запись +26
			++Visible;
		}
		BagScroll.SetTotal(Visible);   // ряд клампится, если список укоротился
		// Подписи «остаток/максимум попыток» и маски — видимым ячейкам (позиция списка = окно прокрутки + ячейка).
		for (int32 C = 0; C < kListCells; ++C)
		{
			const int32 L = BagScroll.ToList(C);
			static const FAsdaItemResetEntry kNone;
			const FAsdaItemResetEntry& E = List.IsValidIndex(L) ? List[L] : kNone;
			if (ListLabels.IsValidIndex(C) && ListLabels[C])
			{
				ListLabels[C]->SetText(E.IsEmpty()
					? FText::GetEmpty()
					: FText::FromString(FString::Printf(TEXT("%d/%d"),
						FMath::Max(0, E.MaxTries - E.UsedTries), E.MaxTries)));
			}
			// ★ Эталон 1:1: маска показана у ВСЕХ тридцати ячеек и СНИМАЕТСЯ только у тех,
			//   что уже лежат в очереди целей (`if (node+4 == target+4) SetVisible(mask, 0)`).
			bool bInTargets = false;
			if (!E.IsEmpty())
			{
				for (const FAsdaItemResetEntry& T : Targets)
				{
					if (T.SameAs(E)) { bInTargets = true; break; }
				}
			}
			if (DisableGroups.IsValidIndex(C)) { SetGroupVisible(DisableGroups[C], !bInTargets); }
		}
	}
	if (GridList) { GridList->Refresh(); }
	UpdateTotals();
}

void UAsdaItemResetSubsystem::RebuildTargets()
{
	for (int32 C = 0; C < kTargetCells; ++C)
	{
		const bool bHas = Targets.IsValidIndex(C);
		if (TargetLabels.IsValidIndex(C) && TargetLabels[C])
		{
			TargetLabels[C]->SetText(bHas
				? FText::FromString(FString::Printf(TEXT("%d/%d"),
					FMath::Max(0, Targets[C].MaxTries - Targets[C].UsedTries), Targets[C].MaxTries))
				: FText::GetEmpty());
		}
	}
	if (GridTarget) { GridTarget->Refresh(); }
	UpdateTotals();
}

void UAsdaItemResetSubsystem::UpdateTotals()
{
	TotalGold  = 0;
	NeedWeapon = 0;
	NeedArmor  = 0;

	const UAsdaItemTableSubsystem* IT = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	for (const FAsdaItemResetEntry& T : Targets)
	{
		if (T.IsEmpty()) { continue; }
		TotalGold += GoldCostOf(T.ItemCode);
		const FAsdaItemRecord* Rec = IT ? IT->GetItem(T.ItemCode) : nullptr;
		const int32 Scrolls = ScrollCostOf(T.ItemCode);
		// Эталон: `itemType == 9` (оружие) → корзина ОРУЖИЯ, всё остальное → корзина БРОНИ.
		if (Rec && Rec->ItemType == 9) { NeedWeapon += Scrolls; } else { NeedArmor += Scrolls; }
	}

	if (TxtFee)     { TxtFee->SetText(FText::FromString(AsdaUi::FormatThousands(TotalGold))); }
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U10e): было `FText::AsNumber` (разделитель разрядов по культуре: EN «1,234», RU «1 234») — стало `FString::FromInt`, как `sprintf("%d")` эталона (`craftWnd_updateMaterialTotals@0xD2F920`, item_enhance.c:1902-1909).
	if (TxtNeedWpn) { TxtNeedWpn->SetText(FText::FromString(FString::FromInt(NeedWeapon))); }
	if (TxtNeedArm) { TxtNeedArm->SetText(FText::FromString(FString::FromInt(NeedArmor))); }
	if (TxtHaveWpn) { TxtHaveWpn->SetText(FText::FromString(FString::FromInt(HaveWeapon))); }
	if (TxtHaveArm) { TxtHaveArm->SetText(FText::FromString(FString::FromInt(HaveArmor))); }
	if (TxtGold)
	{
		const UAsdaInventorySubsystem* Inv = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		TxtGold->SetText(FText::FromString(AsdaUi::FormatThousands(Inv ? Inv->GetGold() : 0)));
	}
}

void UAsdaItemResetSubsystem::UpdateInfoPanel()
{
	// `ItemInfoWin_Update@0xD31B60`. Нет выбранного предмета ⇒ все шесть полей пустые.
	if (!bSelectedValid || Selected.IsEmpty())
	{
		for (UTextBlock* Tb : { TxtGradeBef.Get(), TxtGradeAft.Get(), TxtPowerBef.Get(),
		                        TxtPowerAft.Get(), TxtChanceBef.Get(), TxtChanceAft.Get() })
		{
			if (Tb) { Tb->SetText(FText::GetEmpty()); }
		}
		if (GridSel) { GridSel->Refresh(); }
		return;
	}

	const int32 Lvl = Selected.Level;
	const int32 Use = Selected.UsedTries;
	const int32 Max = Selected.MaxTries;

	// 20211 «уровень до» = +24; 20212 «после» = режим 1 ? +24 : 0.
	if (TxtGradeBef) { TxtGradeBef->SetText(FText::FromString(FString::Printf(TEXT("+%d"), Lvl))); }
	if (TxtGradeAft)
	{
		TxtGradeAft->SetText(FText::FromString(
			FString::Printf(TEXT("+%d"), Mode == kModeRestore ? Lvl : 0)));
	}
	// 20214 «улучшений до» = +25; 20215 «после» — у эталона ВСЕГДА 0
	//   (`v38 = 0; if (byte26 <= 0) v38 = byte26;` — байт беззнаковый, ветка недостижима).
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U10e): было `FText::AsNumber` (разделитель разрядов по культуре: EN «1,234», RU «1 234») — стало `FString::FromInt`, как `sprintf("%d")` эталона (`ItemInfoWin_Update@0xD31B60`, item_inventory.c:45273/45279).
	if (TxtPowerBef) { TxtPowerBef->SetText(FText::FromString(FString::FromInt(Use))); }
	if (TxtPowerAft) { TxtPowerAft->SetText(FText::FromString(FString::FromInt(0))); }
	// 20217 «попыток до» = (max − used)/max; 20218 «после» = режим 1 ? (max − уровень)/max : max/max.
	if (TxtChanceBef)
	{
		TxtChanceBef->SetText(FText::FromString(
			FString::Printf(TEXT("%d/%d"), FMath::Max(0, Max - Use), Max)));
	}
	if (TxtChanceAft)
	{
		const int32 After = (Mode == kModeRestore) ? (Max - Lvl) : Max;
		TxtChanceAft->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), After, Max)));
	}
	if (TxtSelName)
	{
		const UAsdaItemTableSubsystem* IT = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		TxtSelName->SetText(FText::FromString(IT ? IT->GetItemName(Selected.ItemCode) : FString()));
	}
	if (GridSel) { GridSel->Refresh(); }
}

// ═════════════════════════════ ЦЕНА И МАТЕРИАЛЫ ═══════════════════════════════════════════════

int32 UAsdaItemResetSubsystem::GoldCostOf(int32 ItemCode) const
{
	const UDataTable* Price = AsdaDt::Cached(kPriceTablePath);
	const UDataTable* Grade = AsdaDt::Cached(kGradeRatePath);
	if (!Price) { return 0; }

	int32 Base = 0;
	for (const TPair<FName, uint8*>& It : Price->GetRowMap())
	{
		const FAsdaPriceRow* R = reinterpret_cast<const FAsdaPriceRow*>(It.Value);
		if (R && R->ItemCode == ItemCode) { Base = R->Price; break; }
	}
	if (Base == 0) { return 0; }

	const UAsdaItemTableSubsystem* IT = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(ItemCode) : nullptr;
	if (!Rec || !Grade) { return Base; }

	int32 Rate = 0;
	for (const TPair<FName, uint8*>& It : Grade->GetRowMap())
	{
		const FAsdaGraderateRow* R = reinterpret_cast<const FAsdaGraderateRow*>(It.Value);
		if (R && R->GradeIndex == Rec->ItemType) { Rate = R->RatePercent; break; }
	}
	// Эталон: множитель применяется ТОЛЬКО если он не ноль, иначе цена = база.
	//   (Сервер проверки на ноль не делает — расхождение описано в шапке .h.)
	if (Rate == 0) { return Base; }
	return (int32)((double)Base * ((double)Rate / 10.0));
}

int32 UAsdaItemResetSubsystem::ScrollCostOf(int32 ItemCode) const
{
	const UDataTable* Price = AsdaDt::Cached(kPriceTablePath);
	if (!Price) { return 0; }
	for (const TPair<FName, uint8*>& It : Price->GetRowMap())
	{
		const FAsdaPriceRow* R = reinterpret_cast<const FAsdaPriceRow*>(It.Value);
		// `Table2Record_0[2]` = третий i32 записи = колонка `dw02`.
		if (R && R->ItemCode == ItemCode) { return R->dw02; }
	}
	return 0;
}

int32 UAsdaItemResetSubsystem::ScrollSubTypeOf(int32 ItemCode) const
{
	const UAsdaItemTableSubsystem* IT = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT || !IT->IsSpecialItemCode(ItemCode)) { return 0; }
	FAsdaSpecialItemlistRow Row;
	if (!IT->GetSpecialItem(ItemCode, Row)) { return 0; }
	return Row.SubType;
}

void UAsdaItemResetSubsystem::CollectScrollSlots(bool bWeapon, int32 NeedCount, TArray<int32>& OutSlots) const
{
	// `inventoryCollectItemsByType@0xD30530`: идём по сумке, берём слоты свитков нужного вида,
	//   пока не наберём требуемое количество; повторов не допускаем. Максимум 10 слотов.
	OutSlots.Reset();
	const UAsdaInventorySubsystem* Inv = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return; }
	const int32 Want = bWeapon ? kScrollWeapon : kScrollArmor;
	int32 Got = 0;
	const int32 Cells = FMath::Min(Inv->GetContainerSize(kBagContainer), kListMax);   // ★ 29.10.2026 (инвентарь ×2): вся сумка
	for (int32 C = 0; C < Cells && OutSlots.Num() < 10; ++C)
	{
		const FAsdaItemInstance* It = Inv->GetCell(kBagContainer, C);
		if (!It || It->IsEmpty() || ScrollSubTypeOf(It->ItemCode) != Want) { continue; }
		const int32 Slot = (It->SlotIndex >= 0) ? It->SlotIndex : C;
		if (OutSlots.Contains(Slot)) { continue; }
		OutSlots.Add(Slot);
		Got += FMath::Max(1, It->Amount);
		if (Got >= NeedCount) { break; }
	}
}

// ═════════════════════════════ ДЕЙСТВИЯ ═══════════════════════════════════════════════════════

void UAsdaItemResetSubsystem::OnListCellClicked(int32 Cell, bool bDouble)
{
	if (!List.IsValidIndex(Cell) || List[Cell].IsEmpty()) { return; }
	const FAsdaItemResetEntry& E = List[Cell];

	if (!bDouble)
	{
		// `uiCraftListSelectItem@0xD2F0B0` — ОДИНОЧНЫЙ щелчок: подсветка + подсказка + инфо-панель.
		Selected = E;
		bSelectedValid = true;
		if (E.Level == E.MaxTries && E.MaxTries != 0 && Mode == kModeRestore)
		{
			SetGuide(AsdaItemResetLoc::kGuideAtMax);
		}
		else if (E.MaxTries != 0)
		{
			if (E.MaxTries - E.UsedTries == E.MaxTries) { SetGuide(AsdaItemResetLoc::kGuideNothingLost); }
			else { SetGuide(Mode == kModeRestore ? AsdaItemResetLoc::kGuideReadyFull
			                                     : AsdaItemResetLoc::kGuideReadyPart); }
		}
		else { SetGuide(AsdaItemResetLoc::kGuideNotUpgrade); }
		UpdateInfoPanel();
		return;
	}

	// `uiTradeItemSelectHandler@0xD2ED00` — ДВОЙНОЙ щелчок: добавить предмет в очередь целей.
	if (Targets.Num() >= kTargetCells) { SetGuide(AsdaItemResetLoc::kGuideListFull); return; }
	for (const FAsdaItemResetEntry& T : Targets)
	{
		// Дубликат — эталон молча ничего не делает (даже подсказку не меняет).
		if (T.SameAs(E)) { return; }
	}
	if (E.Level == E.MaxTries && E.MaxTries != 0 && Mode == kModeRestore)
	{
		SetGuide(AsdaItemResetLoc::kGuideAtMax);           // не добавляем
	}
	else if (E.MaxTries != 0)
	{
		if (E.MaxTries - E.UsedTries == E.MaxTries)
		{
			SetGuide(AsdaItemResetLoc::kGuideNothingLost);  // не добавляем
		}
		else
		{
			Targets.Add(E);
			SetGuide(Mode == kModeRestore ? AsdaItemResetLoc::kGuideAddedRestore
			                              : AsdaItemResetLoc::kGuideAddedFull);
		}
	}
	else { SetGuide(AsdaItemResetLoc::kGuideNotUpgrade); }  // не добавляем

	RebuildList();     // маски зависят от состава очереди
	RebuildTargets();
}

void UAsdaItemResetSubsystem::OnTargetCellClicked(int32 Cell, bool bDouble)
{
	if (!Targets.IsValidIndex(Cell)) { return; }
	if (!bDouble)
	{
		// `uiItemInfoHandleOpcode4F6A@0xD2EBF0` — одиночный щелчок: показать в инфо-панели.
		Selected = Targets[Cell];
		bSelectedValid = true;
		UpdateInfoPanel();
		return;
	}
	// `uiRefineMaterialSelectHandler@0xD2EBF0-` — двойной щелчок УБИРАЕТ цель из очереди.
	Targets.RemoveAt(Cell);
	RebuildList();
	RebuildTargets();
}

void UAsdaItemResetSubsystem::OnCancelClicked()
{
	CloseWindow();
}

void UAsdaItemResetSubsystem::OnOkClicked()
{
	// `clanWindowOnButtonEvent@0xD30480`, ветка `a2 == 20350`.
	if (Targets.Num() == 0)
	{
		NoticeBoth(AsdaItemResetLoc::kNoticeNoTarget);   // 98508
		return;
	}
	AskConfirm();
}

void UAsdaItemResetSubsystem::AskConfirm()
{
	// Модалка класса 54 (`case 0x36` в `CItem_BuildTooltipText@0xD6E790`): тело — нотис 50200,
	//   кнопки — 50201 / 50202. Ответ «Да» ставит байт +4256 (`set_o10A0@0xD30A70`).
	if (bConfirmOpen || bArmed) { return; }
	UAsdaWindowSubsystem* WM = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const FString Text = Loc(AsdaItemResetLoc::kConfirmText);
	if (!WM || Text.IsEmpty()) { return; }

	bConfirmOpen = true;
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Text, [this](bool bYes)
	{
		bConfirmOpen = false;
		if (bYes)
		{
			bArmed = true;   // насос заберёт очередь в ближайшем Tick
			bReady = true;
		}
	}, /*GaugeGateSec*/ 0.f, AsdaItemResetLoc::kConfirmOk, AsdaItemResetLoc::kConfirmCancel, /*очередь: эталон (1, 54) social.c:5226*/54, 1);
}

// ═════════════════════════════ НАСОС ОТПРАВКИ ═════════════════════════════════════════════════

void UAsdaItemResetSubsystem::PumpSendQueue()
{
	// `craftWnd_processCraftRequest@0xD30810` — покадрово, РОВНО ОДИН пакет за проход,
	//   и следующий уходит только после ответа 6507 (он снова взводит `bReady`).
	if (!bArmed || !bReady) { return; }
	bReady = false;

	if (Targets.Num() == 0)
	{
		// Очередь исчерпана — эталон снимает флаг и перерисовывает окно.
		bArmed = false;
		bReady = true;
		RebuildList();
		RebuildTargets();
		return;
	}

	const FAsdaItemResetEntry Head = Targets[0];
	Targets.RemoveAt(0);

	UAsdaNetSubsystem* Net = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { bArmed = false; bReady = true; return; }

	TArray<int32> MatSlots;
	if (Mode == kModeRestore)
	{
		// Эталон: вид свитка выбирается по ВИДУ ЦЕЛИ (`itemType == 9` → оружейные),
		//   а нужное количество — из `DT_pricetable.dw02` этой же цели.
		const UAsdaItemTableSubsystem* IT = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		const FAsdaItemRecord* Rec = IT ? IT->GetItem(Head.ItemCode) : nullptr;
		const bool bWeapon = (Rec && Rec->ItemType == 9);
		CollectScrollSlots(bWeapon, ScrollCostOf(Head.ItemCode), MatSlots);
	}
	// В режиме 0 материалы не собираются вовсе: сервер весь их блок пропускает
	//   (`if (v19 == 1) { … }`), а слоты уходят как −1 (см. `SendItemReset6506`).

	Net->SendItemReset6506((uint8)Mode, Head.Slot, MatSlots);
}

void UAsdaItemResetSubsystem::DrainPendingPackets()
{
	// ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива.
	TArray<TArray<uint8>> Local;
	if (!PendingResults.Drain(Local)) { return; }
	for (const TArray<uint8>& B : Local) { OnResetResult(B); }
}

void UAsdaItemResetSubsystem::OnResetResult(const TArray<uint8>& Body)
{
	if (Body.Num() < 154) { return; }
	const int32 Code = (int32)Body[0];
	// Эталон первым делом взводит байт +4257 — «можно слать следующий из очереди».
	bReady = true;

	UE_LOG(LogAsdaItemReset, Log,
		TEXT("[сброс улучшений] 6507 код=%d вес=%d золото=%d (в очереди осталось %d)"),
		Code, AsdaBytes::RdI16(&Body[1]), AsdaBytes::RdI32(&Body[3]), Targets.Num());

	// ★★★ [ИСПРАВЛЕНИЕ] 25.10.2026: эталон в `case 1` СНАЧАЛА кладёт данные (золото, вес, запись цели, материалы)
	//   и только потом показывает нотис 50057. Порт данные выбрасывал — сброс «удавался», а предмет оставался +N.
	if (Code == 1 && CVarItemResetApplyResult.GetValueOnGameThread() != 0)
	{
		ApplyResultToInventory(Body);
	}

	// Нотис — ТОЛЬКО по коду; код 0 у эталона нотиса НЕ имеет (`default: break;`).
	switch (Code)
	{
	case 1: NoticeBoth(AsdaItemResetLoc::kResultOk);        break;
	case 2: NoticeBoth(AsdaItemResetLoc::kResultNotOwn);    break;   // сервер этого не шлёт
	case 3: NoticeBoth(AsdaItemResetLoc::kResultBadLevel);  break;   // сервер этого не шлёт
	case 4: NoticeBoth(AsdaItemResetLoc::kResultNoGold);    break;
	case 5: NoticeBoth(AsdaItemResetLoc::kResultNoScroll);  break;
	case 6: NoticeBoth(AsdaItemResetLoc::kResultCantReset); break;
	default: break;
	}

	// Состав сумки после успеха меняет ИНВЕНТАРЬ (он владелец записей и слушает 5000/5002);
	//   окно, как и эталон, БЕЗУСЛОВНО перерисовывает себя на любой код ответа.
	if (bOpen && bControlsReady)
	{
		// Подсвеченный предмет мог измениться — перечитываем его из инвентаря.
		if (bSelectedValid)
		{
			const UAsdaInventorySubsystem* Inv = GetGameInstance()
				? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
			const FAsdaItemInstance* It = Inv ? Inv->GetCell(Selected.Container, Selected.CellIndex) : nullptr;
			if (It && !It->IsEmpty() && It->ItemCode == Selected.ItemCode)
			{
				Selected.Level     = (int32)It->GemGrade;
				Selected.UsedTries = RecByte(*It, 25);
				Selected.MaxTries  = RecByte(*It, 26);
			}
			else { bSelectedValid = false; Selected.Clear(); }
		}
		UpdateInfoPanel();
		RebuildList();
		RebuildTargets();
	}
}

int32 UAsdaItemResetSubsystem::ApplyResultToInventory(const TArray<uint8>& Body)
{
	// Эталон `uiInventoryOnRecvSlotUpdate@0xD31430`, ветка `case 1` (ui_core.c:49773-49839). Зовётся из Tick
	//   (через `DrainPendingPackets`), не из сетевого колбэка — закон `no-reentrant-net-callbacks`.
	if (Body.Num() < kOffMats + kMatStride * kMatCount) { return 0; }
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return 0; }
	const uint8* B = Body.GetData();

	// ── 1. Золото и вес (`uiSetHonorPointsDisplay(@3)`, `HUD_UpdateMiniWeightBar(*(WORD*)@1)`) ──
	const int32 NewGold   = AsdaBytes::RdI32(B + kOffGold);
	const int32 NewWeight = (int32)AsdaBytes::RdU16(B + kOffWeight);   // эталон читает WORD (беззнаковое)
	Inv->SetGoldDisplay(NewGold);
	if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
	{
		Net->SetSelfWeight(NewWeight, -1, TEXT("6507"));
	}

	int32 Changed = 0;
	Inv->BeginCellBatch();   // эталон перерисовывает один раз в конце — до 11 ячеек за ответ

	// ── 2. Запись ЦЕЛИ @7 ──
	//   Эталон: `if (код записи >= 0) { memset(g_InventoryBag + 57*слот, 0, 57); Inventory_SetBagSlot(запись); }`,
	//   а `Inventory_SetBagSlot@0xD61650` кладёт запись только при коде > 0. Контейнер эталон не смотрит — всегда сумка:
	//   сервер возвращает запись из того контейнера, что прислал клиент, а эталон (и порт) шлёт 1.
	//   [ПОРТ] Запись с контейнером 3 (надетое) кладётся в экипировку — защита на случай, если цель когда-либо
	//   уйдёт не из сумки; в сумку эталона такая запись легла бы поверх чужого слота.
	{
		const uint8* Rec = B + kOffTarget;
		const int32 Code = AsdaBytes::RdI32(Rec);
		const int32 Cont = (Rec[4] == 3) ? 3 : kBagContainer;
		const int32 Slot = AsdaBytes::RdI16(Rec + 5);
		if (Code >= 0 && Slot >= 0 && Slot < Inv->GetContainerSize(Cont))
		{
			TArray<uint8> R57;
			R57.Append(Rec, FAsdaItemInstance::kRecordSize);
			R57[4] = (uint8)Cont;
			if (Cont == kBagContainer) { Inv->ClearCell(Cont, Slot); }
			if (Code > 0) { Inv->ApplyExternalRecord(R57, UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
			++Changed;
			UE_LOG(LogAsdaItemReset, Log,
				TEXT("[сброс улучшений] 6507: запись цели → контейнер %d слот %d код=%d ступень=%d попытки=%d/%d процент=%d"),
				Cont, Slot, Code, (int32)Rec[24], (int32)Rec[25], (int32)Rec[26], AsdaBytes::RdI16(Rec + 27));
		}
		else
		{
			UE_LOG(LogAsdaItemReset, Warning,
				TEXT("[сброс улучшений] 6507: запись цели код=%d контейнер %d слот %d вне модели — не применена"),
				Code, (int32)Rec[4], Slot);
		}
	}

	// ── 3. Записи МАТЕРИАЛОВ @64+9i ──
	//   Эталон: `if (слот > -1) { если количество ≠ 0 — ячейка[+9] = количество, ячейка[+14] = вес стека;
	//   иначе memset ячейки; затем Inventory_SetBagSlot(ячейка) }`. Пустые записи сервер шлёт со слотом −1
	//   (режим 0 материалов не берёт вовсе). Контейнер эталон не смотрит (сумка); [ПОРТ] контейнер 2 уважаем.
	for (int32 I = 0; I < kMatCount; ++I)
	{
		const uint8* M = B + kOffMats + kMatStride * I;
		const int32 Slot = AsdaBytes::RdI16(M + 1);
		if (Slot <= -1) { continue; }
		const int32 Cont   = (M[0] == 2) ? 2 : kBagContainer;
		const int32 Count  = AsdaBytes::RdI32(M + 3);
		const int32 Weight = AsdaBytes::RdI16(M + 7);
		if (Slot >= Inv->GetContainerSize(Cont))
		{
			UE_LOG(LogAsdaItemReset, Warning, TEXT("[сброс улучшений] 6507: материал %d — слот %d вне контейнера %d"), I, Slot, Cont);
			continue;
		}
		const FAsdaItemInstance* Cell = Inv->GetCell(Cont, Slot);
		if (Count != 0 && Cell && !Cell->IsEmpty() && Cell->Raw.Num() >= FAsdaItemInstance::kRecordSize)
		{
			TArray<uint8> R57 = Cell->Raw;
			AsdaBytes::WrI32(R57.GetData() + 9, Count);
			AsdaBytes::WrI16(R57.GetData() + 14, Weight);
			Inv->ApplyExternalRecord(R57, UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
		}
		else if (Count == 0)
		{
			Inv->ClearCell(Cont, Slot);
		}
		++Changed;
		UE_LOG(LogAsdaItemReset, Log, TEXT("[сброс улучшений] 6507: материал %d → контейнер %d слот %d количество=%d вес=%d"),
			I, Cont, Slot, Count, Weight);
	}
	Inv->EndCellBatch();

	UE_LOG(LogAsdaItemReset, Log, TEXT("[сброс улучшений] 6507 применён: ячеек %d, золото=%d, вес=%d"), Changed, NewGold, NewWeight);
	return Changed;
}

// ═════════════════════════════ ПОКАДРОВЫЕ ОПРОСЫ ══════════════════════════════════════════════

void UAsdaItemResetSubsystem::TickCells()
{
	// ★ Ревизия R-6 окна 20000 (25.09.2026), та же дыра здесь: закон шапки `AsdaWindowSubsystem.h` —
	//   опросчик мыши ПЕРВОЙ строкой спрашивает, не лежит ли под курсором чужой всплывающий слой
	//   (список каналов чата, справка «?», контекстные меню). Иначе клик по слою ушёл бы в ячейку под ним.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		if (WM->IsPointerBlockedByTopmost(kWindowId))
		{
			if (TipListCell >= 0 || TipTargetCell >= 0)
			{
				TipListCell = -1; TipTargetCell = -1;
				if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>())
				{
					Tip->HideTooltipFor(EAsdaTipOwner::ItemReset);
				}
			}
			Mouse.Reset();
			return;
		}
	}
	// Клик и наведение — ТОЛЬКО по живой геометрии (`AsdaUi::IsCursorOverWidget`):
	//   ячейки лежат внутри групп, и посчитанные зоны уезжают.
	int32 OverList = -1, OverTarget = -1;
	// ★ 29.10.2026 (инвентарь ×2): `OverList` — позиция СПИСКА `List` (видимая ячейка + окно прокрутки).
	for (int32 C = 0; C < kListCells && OverList < 0; ++C)
	{
		const int32 L = BagScroll.ToList(C);
		if (ListIcons.IsValidIndex(C) && ListIcons[C] && List.IsValidIndex(L) && !List[L].IsEmpty()
			&& AsdaUi::IsCursorOverWidget(ListIcons[C]))
		{
			OverList = L;
		}
	}
	for (int32 C = 0; C < kTargetCells && OverList < 0 && OverTarget < 0; ++C)
	{
		if (TargetIcons.IsValidIndex(C) && TargetIcons[C] && Targets.IsValidIndex(C)
			&& AsdaUi::IsCursorOverWidget(TargetIcons[C]))
		{
			OverTarget = C;
		}
	}

	UAsdaItemTooltipSubsystem* Tip = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	const UAsdaInventorySubsystem* Inv = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;

	if (OverList < 0 && OverTarget < 0)
	{
		if (TipListCell >= 0 || TipTargetCell >= 0)
		{
			TipListCell = -1; TipTargetCell = -1;
			if (Tip) { Tip->HideTooltipFor(EAsdaTipOwner::ItemReset); }
		}
		return;
	}

	// Тултип предмета под курсором (`uiInventoryShowItemTooltip@0xD30AA0`).
	const bool bOnTarget = (OverTarget >= 0);
	const int32 Cell = bOnTarget ? OverTarget : OverList;
	if ((bOnTarget ? TipTargetCell : TipListCell) != Cell || (bOnTarget ? TipListCell : TipTargetCell) >= 0)
	{
		TipListCell   = bOnTarget ? -1 : Cell;
		TipTargetCell = bOnTarget ? Cell : -1;
		const FAsdaItemResetEntry& E = bOnTarget ? Targets[Cell] : List[Cell];
		if (Tip && Inv)
		{
			if (const FAsdaItemInstance* It = Inv->GetCell(E.Container, E.CellIndex))
			{
				Tip->ShowItemTooltip(*It, /*bSuppressCompare*/false, EAsdaTipOwner::ItemReset, /*SourceKind*/1);
			}
		}
	}

	if (!Mouse.bLmbDown) { return; }
	// ★★★ Волна 9 заточки (С-1): окно 20200 — процедура инвентаря (`CGameWindow_OnCommand`, группы 202/203);
	//   общая блокировка `uiCheckRecentActivity` глушит у неё и одиночный (0xB, `uiCraftListSelectItem` /
	//   `uiItemInfoHandleOpcode4F6A`, ui_core.c:47932), и двойной щелчок (0xD, `uiTradeItemSelectHandler` /
	//   `uiRefineMaterialSelectHandler`, :47991). Тултип выше — не глушится. Серию двойного клика обрываем.
	if (AsdaUi::CheckRecentActivity(GetGameInstance())) { CellDbl.Reset(); return; }

	// ★ Оригинал различает одиночный (сообщение 0xB) и двойной (0xD) щелчок по ОДНИМ И ТЕМ ЖЕ
	//   ячейкам, а `FAsdaMouseFrame` фронта двойного клика не даёт — считаем его сами.
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U1): общий `FAsdaDblClick` — порог Windows `GetDoubleClickTime`, реальное время (было — `kDoubleClickSec` 0,35 с;
	//   прежний порог — `LegacySec`, ключ `asda.UI.DblClickLegacy 1`). Ключ узла: A = ячейка, B = «цель/список». Третий клик подряд
	//   снова считается первым (сброс пары внутри `Click`).
	const bool bDouble = CellDbl.Click(Cell, bOnTarget ? 1 : 0);

	// ★ ВАЖНО: двойной щелчок в оригинале приходит В ДОПОЛНЕНИЕ к одиночному, а не вместо него
	//   (закон `target-ring-from-twin`: `IE_DoubleClick` НЕ заменяет `IE_Pressed`), поэтому
	//   одиночную ветку мы уже отработали на первом клике и здесь зовём только нужную.
	if (bOnTarget) { OnTargetCellClicked(Cell, bDouble); }
	else           { OnListCellClicked(Cell, bDouble); }
}

// ★ U1 (В2-A дедупа №2): копия распознавателя с прежним порогом окна — для самотеста (описание — у объявления).
FAsdaDblClick UAsdaItemResetSubsystem::DebugDblClickProbe() const
{
	return CellDbl;
}

// ═════════════════════════════ НОТИСЫ ═════════════════════════════════════════════════════════

FString UAsdaItemResetSubsystem::Loc(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

void UAsdaItemResetSubsystem::NoticeBoth(int32 LocId) const
{
	// Эталон на каждый результат 6507 шлёт нотис В ДВА КАНАЛА СРАЗУ:
	//   модалка `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)` (= ONEBUT_MSGBOX)
	//   И строка в чат `CChat_AddTokenizedLines(g_pChatWnd, текст, −11514, 10000, 0)`.
	// ★ Волна 1 (14.09.2026, В2): оба канала — через фасад `AsdaNotice`. `MsgBoxLoc` при пустой строке или без
	//   оконной подсистемы сам кладёт в ленту `PostLoc` (маркеры подставлены) — вторую строку тогда не шлём.
	//   Итог 1:1 с прежним кодом: окно + строка; без окна/строки — одна строка `PostLoc`.
	UGameInstance* GI = GetGameInstance();
	if (AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::ItemRefused, LocId, 2288))
	{
		AsdaNotice::Chat(GI, EAsdaMsgCategory::ItemRefused, LocId);
	}
}

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════

FString UAsdaItemResetSubsystem::DumpControls() const
{
	FString S = TEXT("=== окно сброса улучшений 20200: разобранные контролы ===\n");
	auto Line = [&S](const TCHAR* Id, const UObject* W)
	{ S += FString::Printf(TEXT("  %-34s %s\n"), Id, W ? TEXT("есть") : TEXT("НЕТ")); };
	Line(TEXT("20201 CloseBtn"),      BtnClose);
	Line(TEXT("20202 Title"),         TxtTitle);
	Line(TEXT("20203 SelItem"),       ImgSelItem);
	Line(TEXT("20204 SelItemName"),   TxtSelName);
	Line(TEXT("20210 Grade"),         TxtGradeLbl);
	Line(TEXT("20211 GradeBefore"),   TxtGradeBef);
	Line(TEXT("20212 GradeAfter"),    TxtGradeAft);
	Line(TEXT("20213 Power"),         TxtPowerLbl);
	Line(TEXT("20214 PowerBefore"),   TxtPowerBef);
	Line(TEXT("20215 PowerAfter"),    TxtPowerAft);
	Line(TEXT("20216 Chance"),        TxtChanceLbl);
	Line(TEXT("20217 ChanceBefore"),  TxtChanceBef);
	Line(TEXT("20218 ChanceAfter"),   TxtChanceAft);
	Line(TEXT("20220 ItemSel"),       ListSelFrame);
	Line(TEXT("20230 GuideTxt"),      TxtGuide);
	Line(TEXT("20320 TargetTitle"),   TxtTargetTitle);
	Line(TEXT("20349 TargetSel"),     TargetSelFrame);
	Line(TEXT("20350 Btn (OK)"),      BtnOk);
	Line(TEXT("20360 IniFeeTxt"),     TxtFeeLbl);
	Line(TEXT("20361 IniFee"),        TxtFee);
	Line(TEXT("20362 GoldTxt"),       TxtGoldLbl);
	Line(TEXT("20363 Gold"),          TxtGold);
	Line(TEXT("20364 IniFeeTxt2"),    TxtNeedWpnLbl);
	Line(TEXT("20365 IniFee2"),       TxtNeedWpn);
	Line(TEXT("20366 ScrollTxt2"),    TxtHaveWpnLbl);
	Line(TEXT("20367 Scroll2"),       TxtHaveWpn);
	Line(TEXT("20370 Cancel"),        BtnCancel);
	Line(TEXT("20371 IniFeeTxt3"),    TxtNeedArmLbl);
	Line(TEXT("20372 IniFee3"),       TxtNeedArm);
	Line(TEXT("20373 ScrollTxt3"),    TxtHaveArmLbl);
	Line(TEXT("20374 Scroll3"),       TxtHaveArm);
	int32 Icons = 0, Labels = 0, Masks = 0;
	for (int32 C = 0; C < kListCells; ++C)
	{
		if (ListIcons.IsValidIndex(C)  && ListIcons[C])  { ++Icons; }
		if (ListLabels.IsValidIndex(C) && ListLabels[C]) { ++Labels; }
		if (DisableGroups.IsValidIndex(C)) { Masks += DisableGroups[C].Num() > 0 ? 1 : 0; }
	}
	S += FString::Printf(TEXT("  20240…20269 иконки=%d/30 подписи=%d/30\n"), Icons, Labels);
	S += FString::Printf(TEXT("  20280…20309 маски=%d/30\n"), Masks);
	int32 Tg = 0;
	for (int32 C = 0; C < kTargetCells; ++C) { if (TargetIcons.IsValidIndex(C) && TargetIcons[C]) { ++Tg; } }
	S += FString::Printf(TEXT("  20330…20334 иконки целей=%d/5\n"), Tg);
	S += FString::Printf(TEXT("  20390 All_Reset узлов=%d · 20391 OneTree_Reset узлов=%d\n"),
		GroupFullReset.Num(), GroupRestore.Num());
	return S;
}

FString UAsdaItemResetSubsystem::DumpState() const
{
	FString S = FString::Printf(
		TEXT("=== окно сброса улучшений: состояние ===\n")
		TEXT("  открыто=%d режим=%d (%s) контролы=%d насос: armed=%d ready=%d очередь=%d\n")
		TEXT("  золото за очередь=%d · свитки нужно оружие=%d броня=%d · в наличии оружие=%d броня=%d\n"),
		bOpen ? 1 : 0, Mode,
		Mode == kModeRestore ? TEXT("восстановление попыток") : TEXT("полный сброс"),
		bControlsReady ? 1 : 0, bArmed ? 1 : 0, bReady ? 1 : 0, Targets.Num(),
		TotalGold, NeedWeapon, NeedArmor, HaveWeapon, HaveArmor);
	S += TEXT("  --- список кандидатов ---\n");
	for (int32 C = 0; C < kListMax; ++C)
	{
		if (List.IsValidIndex(C) && !List[C].IsEmpty())
		{
			S += FString::Printf(TEXT("   [%2d] код=%d ячейка=%d слот=%d ур=%d исп=%d макс=%d\n"),
				C, List[C].ItemCode, List[C].CellIndex, List[C].Slot,
				List[C].Level, List[C].UsedTries, List[C].MaxTries);
		}
	}
	S += TEXT("  --- очередь целей ---\n");
	for (int32 C = 0; C < Targets.Num(); ++C)
	{
		S += FString::Printf(TEXT("   [%d] код=%d ячейка=%d слот=%d ур=%d исп=%d макс=%d\n"),
			C, Targets[C].ItemCode, Targets[C].CellIndex, Targets[C].Slot,
			Targets[C].Level, Targets[C].UsedTries, Targets[C].MaxTries);
	}
	return S;
}

FString UAsdaItemResetSubsystem::DebugInfoPanel() const
{
	// Описание — у объявления. Читаем ровно то, что видит игрок в шести полях «до → после».
	if (!bOpen || !bControlsReady) { return FString(); }
	auto T = [](const UTextBlock* Tb) { return Tb ? Tb->GetText().ToString() : FString(TEXT("—")); };
	return FString::Printf(TEXT("ур=%s→%s исп=%s→%s поп=%s→%s"),
		*T(TxtGradeBef), *T(TxtGradeAft), *T(TxtPowerBef), *T(TxtPowerAft), *T(TxtChanceBef), *T(TxtChanceAft));
}
