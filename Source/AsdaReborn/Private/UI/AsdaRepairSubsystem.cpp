// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окна ремонта — весь реверс с адресами в шапке UI/AsdaRepairSubsystem.h
//   и в отчёте `Reports/repair_and_warshop_port.md` §1.
#include "UI/AsdaRepairSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaUiHelpers.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/GameInstance.h"
#include "Login/AsdaButtonRelay.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaRepair, Log, All);
// ★ U21 (В1 дедупа №2): имя константы — по контейнеру; значение обязано совпадать с размером контейнера инвентаря.
static_assert(UAsdaRepairSubsystem::kShellCells == UAsdaInventorySubsystem::kShellSlots, "размер контейнера разошёлся с инвентарём");

// ── Константы окна ──
static const FName  kRepairWindowId(TEXT("INVEN_Repair_Win"));
static const TCHAR* kRepairWbp =
	TEXT("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_INVEN_Repair_Win.WBP_INVEN_Repair_Win_C");

// ★★★ СООТВЕТСТВИЕ «слот надетого → имя ячейки». В `.JUI` подписи `Jmark_N` ПЕРЕМЕШАНЫ
//   относительно `IDNum`, а эталон привязывается ТОЛЬКО к `IDNum` (`i + 57311`, i = слот).
//   Таблица снята из `gamemain_jui.ini` (окно 57300) и продублирована в
//   `Docs/UI/Craft/INVEN_Repair_Win.md`: 57311→Jmark_2, 57312→Jmark_6, 57313→Jmark_8,
//   57314→Jmark_7, 57315→Jmark_5, 57316→Jmark_1, 57317→Jmark_4, 57318→Jmark_3,
//   57319→Jmark_10, 57320→Jmark_9, 57321→Jmark_11.
static const int32 kJmarkLabelBySlot[UAsdaRepairSubsystem::kEquipCells] =
	{ 2, 6, 8, 7, 5, 1, 4, 3, 10, 9, 11 };

// ── Loc-идентификаторы. ВСЕ сняты с эталонных вызовов `UI_FormatLocalizedNotice`. ──
namespace RepairLoc
{
	constexpr int32 kNothingToFix   = 110326;   // «Нечего чинить.»                     → msgbox
	constexpr int32 kCostLabel      = 110306;   // «Стоимость» (57327)
	constexpr int32 kGoldLabel      = 13241;    // «Золото:»   (57325)
	constexpr int32 kWarBaseLabel   = 111922;   // «Цена ремонта (по умолчанию)» (79401)
	constexpr int32 kWarTotalLabel  = 111923;   // «Цена ремонта»                (79405)
	constexpr int32 kDiscountFmt    = 111915;   // «Скидка (%d%%)»
	constexpr int32 kMarkupFmt      = 111916;   // «Наценка (%d%%)»
	constexpr int32 kConfirmOne     = 110312;   // «Починить предмет '[%s]' за %d золота?»
	constexpr int32 kConfirmEquip   = 110313;   // «Отремонтировать все снаряжение за %d золота?»
	constexpr int32 kConfirmAll     = 110314;   // «…снаряжение и вещи в рюкзаке за %d золота?»
	constexpr int32 kBtnOk          = 108510;   // подпись «OK»     двухкнопочного окна кода 73
	constexpr int32 kBtnCancel      = 108511;   // подпись «Отмена» того же окна
	constexpr int32 kErrFmt         = 110315;   // «Ошибка окна снаряжения: %d…»
	constexpr int32 kDone           = 110316;   // «Ремонт снаряжения завершен.»
	constexpr int32 kNoGold         = 2050;     // «Недостаточно золота.»
	constexpr int32 kTipEquipBody   = 110317;   // подсказка кнопки 57305 (тело)
	constexpr int32 kTipAllBody     = 110318;   // подсказка кнопки 57306 (тело)
	constexpr int32 kTipEquipTitle  = 110319;   // «Ремонт снаряжения»
	constexpr int32 kTipAllTitle    = 110320;   // «Общий ремонт»
}

using AsdaBytes::RdI16;
using AsdaBytes::RdI32;

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaRepairSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★★★ РОСПИСЬ «КАК ЗАКРЫТЬ СЕБЯ ПО ESC» — порт события 21 (`CControl_OnEvent21@0x12DCC30`).
	//   Окно `INVEN_Repair_Win` (IDNum 57300) стоит в эталонной таблице `kEscCloseWindows`: признак
	//   `CControl+0x228` ему ставит `uiEquipUpgradePanelInit@0xED9310`. Проход
	//   `UAsdaWindowSubsystem::CloseTopmostOnEsc` закрывает верхнее окно ИМЕННО ЭТИМ вызовом,
	//   а не снятием виджета: снятие оставило бы подсистеме состояние «открыто», и окно
	//   больше не открылось бы. Закрыватель идемпотентен — повторный вызов ничего не делает.
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	if (UAsdaWindowSubsystem* EscWM = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		EscWM->RegisterEscCloser(kRepairWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaRepairSubsystem::CloseWindow));
	}
	EquipUi.SetNum(kEquipCells);
	BagUi.SetNum(kBagGridCells);   // ★ 29.10.2026 (инвентарь ×2): видимые ячейки сетки — 30
	Equip.SetNum(kEquipCells);
	Bag.SetNum(kShellCells);       // ★ 29.10.2026: уплотнённый список — вся сумка (до 60)
	// ★★★ [ПОРТ] 29.10.2026 (инвентарь ×2): КОЛЕСО НАД СЕТКОЙ СУМКИ — прокрутка по рядам (`BagScroll`, шапка
	//   `AsdaBagListScroll.h`). У эталона сетка 30 без страниц: при 60 ячейках сумки вещи сверх 30-й были бы недостижимы.
	//   Колесо съедается, только когда курсор над ячейками сетки сумки (над надетым и мимо окна — дальше по z).
	if (UAsdaWindowSubsystem* WheelWM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WheelWM->RegisterWheelHandler(kRepairWindowId, FAsdaUiWheelHandler::CreateWeakLambda(this, [this](int32 Notches) -> bool
		{
			if (!bOpen || !bControlsReady) { return false; }
			bool bOver = false;
			for (const FAsdaRepairCellUi& U : BagUi) { if (U.Icon && AsdaUi::IsCursorOverWidget(U.Icon)) { bOver = true; break; } }
			if (!bOver) { return false; }
			if (BagScroll.Wheel(Notches))
			{
				TipCell = -1;               // плашка/рамка прежней позиции — к другой вещи
				HideSelectionFrame();
				RebuildAll();
			}
			return true;
		}));
	}

	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь.
		HRepairResult = Net->OnRepairResult.AddLambda([this](const TArray<uint8>& B) { PendingResults.Push(B); });
		// Смена владельца города меняет ставку — блок 79400 перерисовывается на месте.
		HCityOwner = Net->OnCityOwnerInfo.AddLambda([this]()
		{
			if (bOpen && bControlsReady) { RefreshPriceTexts(); }
		});
	}
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		// Прочность/состав контейнеров могли поменяться извне (надел вещь, выбросил, пришёл 5000) —
		//   эталон в этих случаях зовёт `uiEquipPanelRefreshAll` из обработчиков инвентаря.
		HInvCell = Inv->OnCellChanged.AddLambda([this](int32 /*Container*/, int32 /*Cell*/)
		{
			// Из колбэка только МЕТИМ: один ответ 6572 двигает до 41 ячейки подряд, и перестроение
			//   на каждой было бы 41 полным проходом по обоим контейнерам.
			bDirty = true;
		});
	}
	UE_LOG(LogAsdaRepair, Log, TEXT("[ремонт] подсистема поднята"));
}

void UAsdaRepairSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnRepairResult.Remove(HRepairResult);
		Net->OnCityOwnerInfo.Remove(HCityOwner);
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
void UAsdaRepairSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	CloseWindow();
	bPending = false;                 // ждали 6572
	bDirty = false;
	bConfirmOpen = false;
	TotalEquip = 0; TotalAll = 0; HoverCost = 0; SelMode = 0;
	SelOne = FAsdaRepairEntry();
	bTipBag = false; TipButton = -1;
	PendingResults.Clear();
	Mouse.Reset();
	BagScroll.Reset();                // ★ 29.10.2026 (инвентарь ×2)
	RepairTailCont.Reset(); RepairTailSlots.Reset(); RepairChunksSent = 0;   // хвост «починить всё» — данные сессии
	(void)Reason;
}

void UAsdaRepairSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.Repair");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	DrainPendingPackets();
	if (!bOpen || !bControlsReady) { return; }
	if (bDirty) { bDirty = false; RebuildAll(); }
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		if (TipCell >= 0 || TipButton >= 0)   // висящую плашку ячейки/кнопки ГАСИМ, а не просто не показываем
		{
			TipCell = -1; bTipBag = false; TipButton = -1;
			HideSelectionFrame();
			if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
			{
				Tip->HideTooltipFor(EAsdaTipOwner::Repair);
			}
		}
		Mouse.Reset();
		return;
	}
	TickCells();
	TickButtonTips();
}

// ═════════════════════════════ ОКНО ═══════════════════════════════════════════════════════════

bool UAsdaRepairSubsystem::EnsureWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kRepairWindowId, kRepairWbp, kRepairZ).Ensure(GetGameInstance(), Widget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem&)
		{
			SetupControls();
			RegisterZone();
		},
		[]()
		{
			UE_LOG(LogAsdaRepair, Error, TEXT("[ремонт] WBP_INVEN_Repair_Win не загрузился (%s)"), kRepairWbp);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaRepairSubsystem::ResetWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kRepairWindowId);
		WM->CloseWindow(kRepairWindowId);
	}
	Widget = nullptr;
	GridEquip = nullptr; GridBag = nullptr;
	Relays.Reset();
	for (FAsdaRepairCellUi& U : EquipUi) { U = FAsdaRepairCellUi(); }
	for (FAsdaRepairCellUi& U : BagUi)   { U = FAsdaRepairCellUi(); }
	BtnClose = BtnRepairEquip = BtnRepairAll = nullptr;
	TxtGoldLabel = TxtGold = TxtCostLabel = TxtCostValue = nullptr;
	TxtWarBaseLbl = TxtWarBase = TxtWarPct = TxtWarDelta = TxtWarTotLbl = TxtWarTotal = nullptr;
	WarGroup = nullptr; SelFrame = nullptr;
	bControlsReady = false;
	TipCell = -1; TipButton = -1;
	Mouse.Reset();
}

// Подпись по прямоугольнику `Ref` в том же родителе: строка ассета той же геометрии
//   (`INVEN_Repair_Win_TXT00/01` для 57327/57328), а если её нет — новый `TextBlock` (см. шапку .h).
UTextBlock* UAsdaRepairSubsystem::MakeLabelLike(UWidget* Ref, const TCHAR* NewName, const UTextBlock* FontDonor)
{
	if (!Ref || !Widget || !Widget->WidgetTree) { return nullptr; }
	UPanelWidget* Parent = Ref->GetParent();
	if (!Parent) { return nullptr; }
	// ★ 10.10.2026 (волна «наезды текста», `Reports/owner_1010/overlaps.md`): у контролов 57327/57328 в ассете УЖЕ
	//   есть свои строки — импортёр назвал их по якорю `INVEN_Repair_Win_TXT00/01` (IDNum без ID), в той же точке, что и
	//   хит-зона. Новая строка поверх них давала на экране «Стоимость починки» + «Стоимость» + «0» + «999,999~» друг на
	//   друге. Берём СУЩЕСТВУЮЩУЮ строку того же родителя с той же геометрией; новую строим, только если её нет.
	//   Поиск — общая точка `AsdaUi::FindOwnLabelLike` (тот же приём у первой карточки магазина снаряжения).
	if (UTextBlock* Own = AsdaUi::FindOwnLabelLike(Ref))
	{
		Own->SetVisibility(ESlateVisibility::SelfHitTestInvisible);   // подписи мышь не ловят
		return Own;
	}
	UTextBlock* Tb = Widget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(NewName));
	if (!Tb) { return nullptr; }
	Tb->SetAutoWrapText(false);
	// Закон `ui-button-hover-click-rule`: подписи мышь НЕ ловят.
	Tb->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (FontDonor) { Tb->SetFont(FontDonor->GetFont()); Tb->SetColorAndOpacity(FontDonor->GetColorAndOpacity()); }
	Parent->AddChild(Tb);
	// Геометрия — ровно та, что импортёр вычислил для контрола (её несёт его хит-зона).
	if (const UCanvasPanelSlot* From = AsdaUi::SlotOf(Ref))
	{
		if (UCanvasPanelSlot* To = AsdaUi::SlotOf(Tb)) { AsdaUi::CopyCanvasGeometry(From, To); }
	}
	return Tb;
}

// Разбор ВСЕХ контролов окна (закон `full-structural-analysis-always`).
void UAsdaRepairSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }

	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);

	TArray<FName> EquipIconNames; EquipIconNames.SetNum(kEquipCells);
	TArray<FName> BagIconNames;   BagIconNames.SetNum(kBagGridCells);
	// Хит-зоны безымянных `NoImage` 60×14 — это и есть 57327/57328 (см. шапку .h).
	//   Порядок в `.JUI` = порядок индексов в имени, поэтому сортируем по индексу.
	TArray<TPair<int32, UWidget*>> CostZones;

	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();

		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("INVEN_Repair_X"))    { BtnClose       = Btn; continue; }
			if (N == TEXT("INVEN_Repair_BTN1")) { BtnRepairEquip = Btn; continue; }
			if (N == TEXT("INVEN_Repair_BTN2")) { BtnRepairAll   = Btn; continue; }
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == TEXT("INVEN_Repair_T1"))       { TxtGoldLabel  = Tb; continue; }
			if (N == TEXT("INVEN_Repair_T2"))       { TxtGold       = Tb; continue; }
			if (N == TEXT("INVEN_Repair_new1_t1"))  { TxtWarBaseLbl = Tb; continue; }
			if (N == TEXT("INVEN_Repair_new1_t2"))  { TxtWarBase    = Tb; continue; }
			if (N == TEXT("INVEN_Repair_new1_t3"))  { TxtWarPct     = Tb; continue; }
			if (N == TEXT("INVEN_Repair_new1_t4"))  { TxtWarDelta   = Tb; continue; }
			if (N == TEXT("INVEN_Repair_new1_t5"))  { TxtWarTotLbl  = Tb; continue; }
			if (N == TEXT("INVEN_Repair_new1_t6"))  { TxtWarTotal   = Tb; continue; }
			// Подпись «+N%» ячейки: импортёр зовёт `TextBlock` ровно как idc контрола.
			bool bTaken = false;
			for (int32 S = 0; S < kEquipCells && !bTaken; ++S)
			{
				if (N == FString::Printf(TEXT("INVEN_Repair_Jmark_%d"), kJmarkLabelBySlot[S]))
				{
					EquipUi[S].Label = Tb; bTaken = true;
				}
			}
			for (int32 C = 0; C < kBagGridCells && !bTaken; ++C)
			{
				if (N == FString::Printf(TEXT("INVEN_Repair_Imark_%d"), C + 1)) { BagUi[C].Label = Tb; bTaken = true; }
			}
			if (bTaken) { Tb->SetText(FText::GetEmpty()); }
			continue;
		}
		// Группа блока «цена/скидка/итог» — её показывает эталон при открытии окна
		//   (`CControl_SetVisible(v7[132] = 79400, 1)`), в `.JUI` она объявлена скрытой.
		if (N == TEXT("INVEN_Repair_new1_grp")) { WarGroup = W; continue; }

		// Планы и хит-зоны.
		FString Idc; int32 JuiIdx = 0, PlaneIdx = 0;
		if (AsdaUi::ParsePlaneName(N, Idc, JuiIdx, PlaneIdx))
		{
			if (Idc == TEXT("INVEN_Repair_Imark_Click")) { SelFrame = W; continue; }
			if (UImage* Img = Cast<UImage>(W))
			{
				bool bTaken = false;
				for (int32 S = 0; S < kEquipCells && !bTaken; ++S)
				{
					if (Idc == FString::Printf(TEXT("INVEN_Repair_Jmark_%d"), kJmarkLabelBySlot[S]))
					{
						EquipUi[S].Icon = Img; EquipIconNames[S] = FName(*N); bTaken = true;
					}
				}
				for (int32 C = 0; C < kBagGridCells && !bTaken; ++C)
				{
					if (Idc == FString::Printf(TEXT("INVEN_Repair_Imark_%d"), C + 1))
					{
						BagUi[C].Icon = Img; BagIconNames[C] = FName(*N); bTaken = true;
					}
				}
			}
			continue;
		}
		// Хит-зона безымянного контрола: `{skin}_{juiIdx}_HIT`.
		if (N.StartsWith(TEXT("NoImage_")) && N.EndsWith(TEXT("_HIT")))
		{
			FVector2D Pos, Size;
			if (AsdaUi::SlotRect(W, Pos, Size) && FMath::IsNearlyEqual(Size.X, 60.f, 2.f) && FMath::IsNearlyEqual(Size.Y, 14.f, 2.f))
			{
				FString Mid = N.Mid(8);                       // после «NoImage_»
				Mid.RemoveFromEnd(TEXT("_HIT"));
				CostZones.Add(TPair<int32, UWidget*>(FCString::Atoi(*Mid), W));
			}
		}
	}

	// ── 57327 «Стоимость» и 57328 (значение) — строки ассета по геометрии их хит-зон (нет строки — создаём) ──
	CostZones.Sort([](const TPair<int32, UWidget*>& A, const TPair<int32, UWidget*>& B) { return A.Key < B.Key; });
	if (CostZones.Num() >= 2)
	{
		TxtCostLabel = MakeLabelLike(CostZones[0].Value, TEXT("INVEN_Repair_CostLabel_57327"), TxtGold);
		TxtCostValue = MakeLabelLike(CostZones[1].Value, TEXT("INVEN_Repair_CostValue_57328"), TxtGold);
	}
	else
	{
		UE_LOG(LogAsdaRepair, Warning,
			TEXT("[ремонт] хит-зон 60x14 найдено %d (ждали 2) — подписи 57327/57328 не построены"),
			CostZones.Num());
	}

	// ── Сетки иконок через ОБЩУЮ точку (закон `port-shared-points`) ──
	//   Клик ловим САМИ по живой геометрии (`TickCells`), кнопки-оверлеи не строим.
	GridEquip = NewObject<UAsdaIconCellGrid>(this);
	GridEquip->SetCellSizePx((float)kCellPx);
	GridEquip->SetClickOverlayEnabled(false);
	// Эталон в начале КАЖДОГО прохода затирает иконку пустой текстурой 32×32 — иначе на
	//   уплотнённой сетке остаются картинки прошлого состава.
	GridEquip->bClearEmptyCells = true;
	GridEquip->SetupNamed(Widget, EquipIconNames, /*bMale*/true,
		[this](int32 Cell) -> int32 { return Equip.IsValidIndex(Cell) ? Equip[Cell].ItemCode : 0; },
		[this](int32 Cell) { OnCellClicked(Cell, /*bBag*/false); });

	GridBag = NewObject<UAsdaIconCellGrid>(this);
	GridBag->SetCellSizePx((float)kCellPx);
	GridBag->SetClickOverlayEnabled(false);
	GridBag->bClearEmptyCells = true;
	GridBag->SetupNamed(Widget, BagIconNames, /*bMale*/true,
		// ★ 29.10.2026 (инвентарь ×2): видимая ячейка → позиция списка `Bag` через окно прокрутки.
		[this](int32 Cell) -> int32 { const int32 L = BagScroll.ToList(Cell); return Bag.IsValidIndex(L) ? Bag[L].ItemCode : 0; },
		[this](int32 Cell) { OnCellClicked(BagScroll.ToList(Cell), /*bBag*/true); });

	// ── Кнопки ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnClose,       57369, [this](int32) { CloseWindow(); });
	Bind(BtnRepairEquip, 57305, [this](int32) { OnRepairEquipClicked(); });
	Bind(BtnRepairAll,   57306, [this](int32) { OnRepairAllClicked(); });

	// ── Статичные подписи (эталон ставит их в `uiEquipUpgradePanelInit`) ──
	if (TxtCostLabel)  { const FString S = Loc(RepairLoc::kCostLabel);     if (!S.IsEmpty()) { TxtCostLabel->SetText(FText::FromString(S)); } }
	if (TxtGoldLabel)  { const FString S = Loc(RepairLoc::kGoldLabel);     if (!S.IsEmpty()) { TxtGoldLabel->SetText(FText::FromString(S)); } }
	if (TxtWarBaseLbl) { const FString S = Loc(RepairLoc::kWarBaseLabel);  if (!S.IsEmpty()) { TxtWarBaseLbl->SetText(FText::FromString(S)); } }
	if (TxtWarTotLbl)  { const FString S = Loc(RepairLoc::kWarTotalLabel); if (!S.IsEmpty()) { TxtWarTotLbl->SetText(FText::FromString(S)); } }
	if (TxtCostValue)  { TxtCostValue->SetText(FText::GetEmpty()); }
	HideSelectionFrame();

	bControlsReady = (EquipUi[0].Icon != nullptr && BagUi[0].Icon != nullptr
		&& BtnRepairEquip != nullptr && BtnRepairAll != nullptr);
	UE_LOG(LogAsdaRepair, Log,
		TEXT("[ремонт] окно 57300 разобрано: надетое %d икон, сумка %d икон, кнопки 57305/57306/57369 = %s/%s/%s, ")
		TEXT("блок войны %s, рамка 57362 %s, подписи 57327/57328 %s"),
		kEquipCells, kBagGridCells,
		BtnRepairEquip ? TEXT("да") : TEXT("НЕТ"), BtnRepairAll ? TEXT("да") : TEXT("НЕТ"),
		BtnClose ? TEXT("да") : TEXT("НЕТ"),
		WarGroup ? TEXT("да") : TEXT("НЕТ"), SelFrame ? TEXT("да") : TEXT("НЕТ"),
		(TxtCostLabel && TxtCostValue) ? TEXT("построены") : TEXT("НЕТ"));
}

void UAsdaRepairSubsystem::RegisterZone()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Корпус окна — девять планов рамки (закон «rect только по узлам, которые окно и образуют»).
	// ★ Волна 3: зона по планам рамки — общая точка `FAsdaJuiWindowHandle::RegisterFrameZone` (`!bOpen || !Widget` → нет зоны).
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kRepairWindowId, this, Widget, bOpen, TEXT("INVEN_Repair_Win_0_%d"), 9);
}

// ═════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ════════════════════════════════════════════

void UAsdaRepairSubsystem::OpenWindow()
{
	if (!EnsureWidget()) { return; }
	// `uiShowUniconWarWindow@0xEDA280`: повторный показ при уже открытом окне НИЧЕГО не делает
	//   (`if (v7[556] != 1)`).
	if (bOpen) { return; }

	bOpen = true;
	bPending = false;
	SelMode = 0;
	SelOne.Clear();
	HoverCost = 0;
	TipCell = -1; TipButton = -1;
	BagScroll.Reset();   // ★ 29.10.2026 (инвентарь ×2): окно открывается с первого ряда списка

	RebuildAll();
	// Эталон при показе включает блок 79400 и обнуляет три его значения.
	if (WarGroup) { WarGroup->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	if (TxtWarBase)  { TxtWarBase->SetText(FText::FromString(TEXT("0"))); }
	if (TxtWarDelta) { TxtWarDelta->SetText(FText::FromString(TEXT("0"))); }
	if (TxtWarTotal) { TxtWarTotal->SetText(FText::FromString(TEXT("0"))); }
	RefreshPriceTexts();
	HideSelectionFrame();

	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kRepairWindowId, true);
	}
	UE_LOG(LogAsdaRepair, Log, TEXT("[ремонт] окно открыто: надетое %d зол., всё %d зол."), TotalEquip, TotalAll);
}

void UAsdaRepairSubsystem::CloseWindow()
{
	if (!bOpen) { return; }
	bOpen = false;
	TipCell = -1; TipButton = -1;
	bConfirmOpen = false;
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::Repair);
	}
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kRepairWindowId, false);
	}
	UE_LOG(LogAsdaRepair, Log, TEXT("[ремонт] окно закрыто"));
}

// ═════════════════════════════ ЦЕНА ═══════════════════════════════════════════════════════════

bool UAsdaRepairSubsystem::IsRepairable(int32 ItemCode) const
{
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* R = IT ? IT->GetItem(ItemCode) : nullptr;
	if (!R) { return false; }
	// 1:1 гейт эталона (одинаков у `uiEquipDurabilityPanelRefresh` и `uiRefreshItemIconList`).
	if (R->ItemSubType == 10 && (R->ItemVariant == 1 || R->ItemVariant == 2)) { return false; }
	return (R->ItemType <= 4) || (R->ItemType >= 8 && R->ItemType <= 9);
}

int32 UAsdaRepairSubsystem::CostOfRecord(const FAsdaItemInstance& It) const
{
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* R = IT ? IT->GetItem(It.ItemCode) : nullptr;
	if (!R || R->DurabilityMax <= 0) { return 0; }   // эталон: `if (ItemRecord->field68)`
	const double MaxDur = (double)R->DurabilityMax;
	const double CurDur = (double)It.Durability;
	// (maxDur − curDur)/maxDur × price × (заточка·0.02 + 0.1)
	const double Cost = (MaxDur - CurDur) / MaxDur * (double)R->Price * ((double)It.GemGrade * 0.02 + 0.1);
	return (Cost <= 0.0) ? 0 : (int32)Cost;
}

int32 UAsdaRepairSubsystem::ApplyWarPercent(int32 Value) const
{
	// 1:1 `uniconWar_ApplyOptionPercent@0xBCEEC0` с эталонным `a2 = 0`:
	//   своя фракция ⇒ СКИДКА (Value − доля), чужая ⇒ НАЦЕНКА (Value + доля),
	//   город не занят либо фракции у игрока нет ⇒ доля 0.
	const int32 Slot = AsdaUi::WarCitySlot(GetGameInstance());
	if (Slot < 0) { return Value; }
	int32 Percent = 0; bool bNeutral = true; bool bNoFaction = true;
	const bool bOwn = AsdaUi::WarCityOwnedBySelf(GetGameInstance(), Slot, Percent, bNeutral, &bNoFaction);
	// ★ ИСПРАВЛЕНИЕ денежного дефекта (`fix_helpers_dedup.md`): доля обнуляется ТОЛЬКО при
	//   отсутствии фракции у игрока (`bNoFaction`), а НЕ на ничьём городе. Оригинал (эталон
	//   `uniconWar_ApplyOptionPercent@0xBCEEC0`) на ничьём городе наценку ПРИМЕНЯЕТ. Прежний
	//   `Percent <= 0 || bNeutral` терял наценку за ремонт на ничьём городе.
	if (Percent <= 0 || bNoFaction) { return Value; }
	const int32 Delta = (int32)((double)Value / 100.0 * (double)Percent);
	return bOwn ? (Value - Delta) : (Value + Delta);
}

// ═════════════════════════════ НАПОЛНЕНИЕ И РЕНДЕР ════════════════════════════════════════════

void UAsdaRepairSubsystem::RebuildAll()
{
	RebuildEquip();
	RebuildBag();
	RefreshGoldText();
	if (GridEquip) { GridEquip->Refresh(); }
	if (GridBag)   { GridBag->Refresh(); }
}

// `uiEquipDurabilityPanelRefresh@0xED9830`: индекс ЯЧЕЙКИ = индекс СЛОТА (уплотнения НЕТ).
void UAsdaRepairSubsystem::RebuildEquip()
{
	for (FAsdaRepairEntry& E : Equip) { E.Clear(); }
	TotalEquip = 0;
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (Inv)
	{
		for (int32 S = 0; S < kEquipCells; ++S)
		{
			const FAsdaItemInstance* It = Inv->GetCell(3, S);
			if (!It || It->IsEmpty() || !IsRepairable(It->ItemCode)) { continue; }
			FAsdaRepairEntry& E = Equip[S];
			E.ItemCode  = It->ItemCode;
			E.Container = (It->ContainerType > 0) ? (int32)It->ContainerType : 3;
			E.Slot      = (It->SlotIndex >= 0) ? It->SlotIndex : S;
			E.CellIndex = S;
			E.Cost      = CostOfRecord(*It);
			TotalEquip += E.Cost;
			// «+N%» под иконкой — эталон печатает поле записи +27 (`Util_Identity16(slot+27) > 0`).
			if (EquipUi.IsValidIndex(S) && EquipUi[S].Label)
			{
				EquipUi[S].Label->SetText(It->EnchantPercent > 0
					? FText::FromString(FString::Printf(TEXT("+%d%%"), It->EnchantPercent))
					: FText::GetEmpty());
			}
		}
	}
	// Ячейки без предмета: подпись гасим (эталон затирает её в начале прохода).
	for (int32 S = 0; S < kEquipCells; ++S)
	{
		if (Equip[S].IsEmpty() && EquipUi.IsValidIndex(S) && EquipUi[S].Label)
		{
			EquipUi[S].Label->SetText(FText::GetEmpty());
		}
	}
}

// `uiRefreshItemIconList@0xED9D50`: ячейки заполняются ПОДРЯД (`visibleCount`), сумма «всего»
//   ИНИЦИАЛИЗИРУЕТСЯ суммой надетого.
void UAsdaRepairSubsystem::RebuildBag()
{
	for (FAsdaRepairEntry& E : Bag) { E.Clear(); }
	TotalAll = TotalEquip;
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	int32 Count = 0;
	if (Inv)
	{
		// Контейнер 1 — сумка СНАРЯЖЕНИЯ (`g_InventoryBag`; эталон 30 ячеек, ★ 29.10.2026 порт — вся сумка, до 60).
		//   Сумма «всего» и список — по ВСЕЙ сумке, сетка показывает окно из 30 позиций (`BagScroll`).
		const int32 Cells = FMath::Min(Inv->GetContainerSize(1), kShellCells);
		for (int32 C = 0; C < Cells && Count < kShellCells; ++C)
		{
			const FAsdaItemInstance* It = Inv->GetCell(1, C);
			if (!It || It->IsEmpty() || !IsRepairable(It->ItemCode)) { continue; }
			FAsdaRepairEntry& E = Bag[Count];
			E.ItemCode  = It->ItemCode;
			E.Container = (It->ContainerType > 0) ? (int32)It->ContainerType : 1;
			E.Slot      = (It->SlotIndex >= 0) ? It->SlotIndex : C;
			E.CellIndex = C;
			E.Cost      = CostOfRecord(*It);
			TotalAll   += E.Cost;
			++Count;
		}
	}
	BagScroll.SetTotal(Count);   // ряд клампится, если список укоротился
	// Подписи «+N%» — только видимым ячейкам (позиция списка = окно прокрутки + ячейка).
	for (int32 V = 0; V < kBagGridCells; ++V)
	{
		if (!BagUi.IsValidIndex(V) || !BagUi[V].Label) { continue; }
		const int32 L = BagScroll.ToList(V);
		const FAsdaItemInstance* It = (Inv && L < Count) ? Inv->GetCell(1, Bag[L].CellIndex) : nullptr;
		BagUi[V].Label->SetText((It && It->EnchantPercent > 0)
			? FText::FromString(FString::Printf(TEXT("+%d%%"), It->EnchantPercent))
			: FText::GetEmpty());
	}
}

void UAsdaRepairSubsystem::RefreshGoldText()
{
	if (!TxtGold) { return; }
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	TxtGold->SetText(FText::FromString(AsdaUi::FormatThousands(Inv ? Inv->GetGold() : 0)));
}

// `shopSellPriceTooltipUpdate@0xEDB960` — печать 57328 и всего блока 79400…79406.
void UAsdaRepairSubsystem::RefreshPriceTexts()
{
	const FString PriceText = AsdaUi::FormatThousands(HoverCost);
	if (TxtCostValue) { TxtCostValue->SetText(FText::FromString(PriceText)); }
	if (WarGroup)     { WarGroup->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	if (TxtWarBase)   { TxtWarBase->SetText(FText::FromString(PriceText)); }

	const int32 Slot = AsdaUi::WarCitySlot(GetGameInstance());
	if (Slot < 0)
	{
		// Эталон: невоенная карта — оба поля «-», ИТОГ не трогается вовсе.
		if (TxtWarPct)   { TxtWarPct->SetText(FText::FromString(TEXT("-"))); }
		if (TxtWarDelta) { TxtWarDelta->SetText(FText::FromString(TEXT("-"))); }
		return;
	}
	int32 Percent = 0; bool bNeutral = true;
	const bool bOwn = AsdaUi::WarCityOwnedBySelf(GetGameInstance(), Slot, Percent, bNeutral);
	const int32 Delta = bNeutral ? 0 : (int32)((double)HoverCost / 100.0 * (double)Percent);

	if (bOwn)
	{
		// СКИДКА: сумма печатается со знаком «−» (формат "-%s" по 0x13E1F48), итог = цена − скидка.
		if (TxtWarDelta)
		{
			TxtWarDelta->SetText(FText::FromString(Delta
				? FString::Printf(TEXT("-%s"), *AsdaUi::FormatThousands(Delta))
				: AsdaUi::FormatThousands(0)));
		}
		if (TxtWarTotal) { TxtWarTotal->SetText(FText::FromString(AsdaUi::FormatThousands(HoverCost - Delta))); }
		if (TxtWarPct)
		{
			TxtWarPct->SetText(FText::FromString(
				AsdaUi::FormatPrintfArgs(Loc(RepairLoc::kDiscountFmt), { FString::FromInt(Percent) })));
		}
	}
	else
	{
		// НАЦЕНКА: формат "+%s" (0x13E1F44), итог = цена + наценка.
		if (TxtWarDelta) { TxtWarDelta->SetText(FText::FromString(FString::Printf(TEXT("+%s"), *AsdaUi::FormatThousands(Delta)))); }
		if (TxtWarTotal) { TxtWarTotal->SetText(FText::FromString(AsdaUi::FormatThousands(HoverCost + Delta))); }
		if (TxtWarPct)
		{
			TxtWarPct->SetText(FText::FromString(
				AsdaUi::FormatPrintfArgs(Loc(RepairLoc::kMarkupFmt), { FString::FromInt(Percent) })));
		}
		// Эталон гасит оба поля, если город не занят либо у игрока нет фракции.
		if (bNeutral)
		{
			if (TxtWarPct)   { TxtWarPct->SetText(FText::FromString(TEXT("-"))); }
			if (TxtWarDelta) { TxtWarDelta->SetText(FText::FromString(TEXT("-"))); }
		}
	}
}

void UAsdaRepairSubsystem::HideSelectionFrame()
{
	if (SelFrame) { SelFrame->SetVisibility(ESlateVisibility::Collapsed); }
}

// Эталон переносит рамку 57362 к ячейке под курсором (`CControl_SetField74_75` + `SetVisible`).
void UAsdaRepairSubsystem::MoveSelectionFrame(const UWidget* OverIcon)
{
	if (!SelFrame || !OverIcon) { HideSelectionFrame(); return; }
	// ★ Дедуп №2, В2-B (U19): общая точка рамки выбора (позиция в координатах РОДИТЕЛЯ рамки). Без перезапуска кадров —
	//   решение волны 1 по эталону Ремонта (O8).
	AsdaUi::PlaceFrameOver(SelFrame, OverIcon, /*bRestartAnim*/false);
}

// ═════════════════════════════ ПОКАДРОВЫЕ ОПРОСЫ ══════════════════════════════════════════════

void UAsdaRepairSubsystem::TickCells()
{
	// Клик и наведение ловим ПО ЖИВОЙ ГЕОМЕТРИИ (`AsdaUi::IsCursorOverWidget`), а не по
	//   вычисленным зонам: ячейки в `.JUI` лежат внутри групп, и посчитанные зоны уезжают.
	int32 OverCell = -1; bool bOverBag = false; const UWidget* OverIcon = nullptr;
	for (int32 S = 0; S < kEquipCells && OverCell < 0; ++S)
	{
		if (EquipUi[S].Icon && !Equip[S].IsEmpty() && AsdaUi::IsCursorOverWidget(EquipUi[S].Icon))
		{
			OverCell = S; bOverBag = false; OverIcon = EquipUi[S].Icon;
		}
	}
	// ★ 29.10.2026 (инвентарь ×2): `OverCell` у сумки — позиция СПИСКА `Bag` (видимая ячейка + окно прокрутки).
	for (int32 C = 0; C < kBagGridCells && OverCell < 0; ++C)
	{
		const int32 L = BagScroll.ToList(C);
		if (BagUi[C].Icon && Bag.IsValidIndex(L) && !Bag[L].IsEmpty() && AsdaUi::IsCursorOverWidget(BagUi[C].Icon))
		{
			OverCell = L; bOverBag = true; OverIcon = BagUi[C].Icon;
		}
	}

	UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;

	if (OverCell < 0)
	{
		if (TipCell >= 0)
		{
			TipCell = -1;
			HideSelectionFrame();
			if (Tip) { Tip->HideTooltipFor(EAsdaTipOwner::Repair); }
		}
		return;
	}

	const FAsdaRepairEntry& E = bOverBag ? Bag[OverCell] : Equip[OverCell];
	if (TipCell != OverCell || bTipBag != bOverBag)
	{
		TipCell = OverCell; bTipBag = bOverBag;
		// Эталон: цена под курсором едет в `filler[568]` и печатается в 57328 и в блок 79400.
		HoverCost = E.Cost;
		RefreshPriceTexts();
		MoveSelectionFrame(OverIcon);
		if (Tip && Inv)
		{
			if (const FAsdaItemInstance* It = Inv->GetCell(E.Container, E.CellIndex))
			{
				// `slotSource`: 3 — надетое, 1 — сумка снаряжения (эталон различает пайплайн).
				Tip->ShowItemTooltip(*It, /*bSuppressCompare*/ !bOverBag, EAsdaTipOwner::Repair, bOverBag ? 1 : 3);
			}
		}
	}

	if (Mouse.bLmbDown)
	{
		OnCellClicked(OverCell, bOverBag);
	}
}

// `uiShowTutorialPopupByCode@0xEDC2E0`: наведение на 57305/57306 показывает подсказку-плашку.
void UAsdaRepairSubsystem::TickButtonTips()
{
	UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Tip) { return; }
	int32 Over = -1;
	if (BtnRepairEquip && AsdaUi::IsCursorOverWidget(BtnRepairEquip)) { Over = 57305; }
	else if (BtnRepairAll && AsdaUi::IsCursorOverWidget(BtnRepairAll)) { Over = 57306; }
	if (Over == TipButton) { return; }
	TipButton = Over;
	if (Over < 0)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::Repair);
		return;
	}
	FAsdaTipMetrics M; M.FontPx = 8; M.MaxW = 218.f;
	const int32 TitleLoc = (Over == 57305) ? RepairLoc::kTipEquipTitle : RepairLoc::kTipAllTitle;
	const int32 BodyLoc  = (Over == 57305) ? RepairLoc::kTipEquipBody  : RepairLoc::kTipAllBody;
	Tip->ShowTextTip(EAsdaTipOwner::Repair, Loc(TitleLoc), Loc(BodyLoc), M);
}

// ═════════════════════════════ ДЕЙСТВИЯ ═══════════════════════════════════════════════════════

void UAsdaRepairSubsystem::OnCellClicked(int32 Cell, bool bBag)
{
	const TArray<FAsdaRepairEntry>& Src = bBag ? Bag : Equip;
	if (!Src.IsValidIndex(Cell)) { return; }
	const FAsdaRepairEntry& E = Src[Cell];
	// Эталон: клик срабатывает ТОЛЬКО если у ячейки ненулевая цена (`if (*(this + 3*i + 6) != 0.0)`).
	if (E.IsEmpty() || E.Cost == 0) { return; }
	AskConfirm(kModeOne, &E);
}

void UAsdaRepairSubsystem::OnRepairEquipClicked()
{
	// `shopSellWndProc` 57305: нулевая сумма надетого ⇒ нотис 110326 и выход.
	if (TotalEquip == 0) { NoticeMsgBox(RepairLoc::kNothingToFix); return; }
	RebuildAll();                       // эталон обновляет обе сетки ПЕРЕД подтверждением
	AskConfirm(kModeEquip, nullptr);
}

void UAsdaRepairSubsystem::OnRepairAllClicked()
{
	if (TotalAll == 0) { NoticeMsgBox(RepairLoc::kNothingToFix); return; }
	RebuildAll();
	AskConfirm(kModeAll, nullptr);
}

// `itemSlotSelect@0xEDA720` + ветка кода 73 в `CItem_BuildTooltipText@0xD6E790`:
//   двухкнопочное окно TWOBUT_MSGBOX 1520 с подписями 108510/108511 и текстом
//   110312 (один предмет) / 110313 (надетое) / 110314 (всё). Цена в тексте — УЖЕ со скидкой
//   или наценкой города (`uniconWar_ApplyOptionPercent(..., 0)`).
void UAsdaRepairSubsystem::AskConfirm(int32 Mode, const FAsdaRepairEntry* One)
{
	// Эталон: пока ждём ответ сервера, новое подтверждение не открывается (`if (this[580] != 1)`).
	//   ★ И пока открыто само окно подтверждения — тоже: в оригинале msgbox 1520 МОДАЛЕН и
	//   перехватывает ввод, у нас клик по ячейке ловится покадрово и мог бы открыть второе.
	if (bPending || bConfirmOpen) { return; }
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }

	SelMode = Mode;
	SelOne.Clear();
	FString Text;
	if (Mode == kModeOne && One)
	{
		SelOne = *One;
		// Эталон берёт цену НЕ из записи, а из `filler[568]` — «цена предмета под курсором».
		//   Клик по ячейке всегда следует за наведением, поэтому это цена той же ячейки;
		//   чтобы не зависеть от порядка событий, берём цену самой позиции.
		const int32 Cost = ApplyWarPercent(One->Cost);
		const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		// Имя предмета — ОБЩАЯ точка `GetItemName` (эталон: `ItemData_FindByCode(…, itemCode)`).
		FString Name = IT ? IT->GetItemName(One->ItemCode) : FString();
		if (Name.IsEmpty()) { Name = FString::FromInt(One->ItemCode); }
		Text = AsdaUi::FormatPrintfArgs(Loc(RepairLoc::kConfirmOne), { Name, FString::FromInt(Cost) });
	}
	else if (Mode == kModeEquip)
	{
		Text = AsdaUi::FormatPrintfArgs(Loc(RepairLoc::kConfirmEquip), { FString::FromInt(ApplyWarPercent(TotalEquip)) });
	}
	else
	{
		Text = AsdaUi::FormatPrintfArgs(Loc(RepairLoc::kConfirmAll), { FString::FromInt(ApplyWarPercent(TotalAll)) });
	}
	if (Text.IsEmpty()) { return; }

	bConfirmOpen = true;
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Text, [this](bool bYes)
	{
		bConfirmOpen = false;
		// Эталон: «OK» кода 73 → `net_SendSlotBatchReq(g_pShopSellWnd)`; «Отмена» — ничего.
		if (bYes) { SendRepair(); }
		else { SelMode = 0; SelOne.Clear(); }
	}, /*GaugeGateSec*/ 0.f, RepairLoc::kBtnOk, RepairLoc::kBtnCancel, /*очередь: эталон (1, 73) item_inventory.c:65052*/73, 1);
}

// `net_SendSlotBatchReq@0xEDA7A0` — сборка тела 6571 по текущему режиму.
void UAsdaRepairSubsystem::SendRepair()
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || bPending) { return; }

	TArray<uint8> Containers;
	TArray<int32> Slots;
	auto Push = [&Containers, &Slots](const FAsdaRepairEntry& E)
	{
		if (E.IsEmpty()) { return; }
		Containers.Add((uint8)E.Container);
		Slots.Add(E.Slot);
	};

	if (SelMode == kModeOne)
	{
		Push(SelOne);
	}
	else if (SelMode == kModeEquip)
	{
		for (const FAsdaRepairEntry& E : Equip) { Push(E); }
	}
	else if (SelMode == kModeAll)
	{
		// ★ 1:1 эталон: сперва ОДИННАДЦАТЬ НАДЕТЫХ, затем ячейки сумки (эталон 30, ★ 29.10.2026 — до 60).
		for (const FAsdaRepairEntry& E : Equip) { Push(E); }
		for (const FAsdaRepairEntry& E : Bag)   { Push(E); }
	}
	else
	{
		return;
	}
	if (Containers.Num() == 0) { NoticeMsgBox(RepairLoc::kNothingToFix); return; }

	// ★★★ [ПОРТ] 29.10.2026 (инвентарь ×2): ТЕЛО 6571 ФИКСИРОВАНО НА 41 ЗАПИСЬ (11 надетых + 30 сумки эталона), а при
	//   сумке 60 «починить всё» даёт до 71. Протокол не меняем: первые 41 уходят сейчас, остаток — ВТОРЫМ пакетом после
	//   ответа 6572 на первый (`OnRepairResult`, код 1). Прежний `SendRepair6571` молча резал хвост (`Min3(…, 41)`).
	RepairTailCont.Reset(); RepairTailSlots.Reset(); RepairChunksSent = 0;
	if (Containers.Num() > kRepairBatch)
	{
		RepairTailCont.Append(Containers.GetData() + kRepairBatch, Containers.Num() - kRepairBatch);
		RepairTailSlots.Append(Slots.GetData() + kRepairBatch, Slots.Num() - kRepairBatch);
		Containers.SetNum(kRepairBatch);
		Slots.SetNum(kRepairBatch);
	}
	if (Net->SendRepair6571(Containers, Slots))
	{
		bPending = true;   // эталонное `*(self + 580) = 1`
		++RepairChunksSent;
		UE_LOG(LogAsdaRepair, Log, TEXT("[ремонт] отправлен 6571: режим %d, записей %d (хвост на второй пакет: %d)"),
			SelMode, Containers.Num(), RepairTailCont.Num());
	}
	else
	{
		RepairTailCont.Reset(); RepairTailSlots.Reset();   // первый не ушёл — хвост не шлём
	}
}

// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): следующий пакет «починить всё» из хвоста (≤ 41 записи). Зовётся из `OnRepairResult`
//   (тик подсистемы, не колбэк сети — закон `no-reentrant-net-callbacks`). true — пакет ушёл и ждём следующий 6572.
static bool AsdaRepairSendTailChunk(UAsdaNetSubsystem* Net, TArray<uint8>& TailCont, TArray<int32>& TailSlots, int32 Batch)
{
	if (!Net || TailCont.Num() == 0 || TailCont.Num() != TailSlots.Num()) { TailCont.Reset(); TailSlots.Reset(); return false; }
	const int32 N = FMath::Min(Batch, TailCont.Num());
	TArray<uint8> C; C.Append(TailCont.GetData(), N);
	TArray<int32> S; S.Append(TailSlots.GetData(), N);
	TailCont.RemoveAt(0, N);
	TailSlots.RemoveAt(0, N);
	if (!Net->SendRepair6571(C, S)) { TailCont.Reset(); TailSlots.Reset(); return false; }
	return true;
}

void UAsdaRepairSubsystem::DrainPendingPackets()
{
	// ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива. Прежний цикл `while … [0]; RemoveAt(0)` давал тот же порядок: во время разбора новые тела в очередь
	//   не попадают (колбэк сети приходит из `UAsdaNetSubsystem::Tick`, не отсюда).
	if (TArray<TArray<uint8>> Local; PendingResults.Drain(Local))
	{
		for (const TArray<uint8>& B : Local) { OnRepairResult(B); }
	}
}

// `netHandleStateArrayPacket@0xEDB2B0` — приём 6572.
void UAsdaRepairSubsystem::OnRepairResult(const TArray<uint8>& Body)
{
	// Эталон снимает флаг ВСЕГДА, ещё до разбора кода (`*(this + 580) = 0`).
	bPending = false;
	if (Body.Num() < 170) { return; }
	const int32 Code = RdI16(Body.GetData());
	switch (Code)
	{
	case 0:
	case 10:
	{
		// «Ошибка окна снаряжения: %d. Обратись в службу поддержки.»
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U2): было `ShowGameMessage(S, 2288, …)` с литералом кнопки и «нет оконной
		//   подсистемы ⇒ молча» — стало общая политика `AsdaNotice::MsgBox` (окна нет ⇒ лента ItemRefused).
		const FString S = AsdaUi::FormatPrintfArgs(Loc(RepairLoc::kErrFmt), { FString::FromInt(Code) });
		AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::ItemRefused, S, AsdaLoc::kBtnOk);
		UE_LOG(LogAsdaRepair, Warning, TEXT("[ремонт] 6572 отказ, код %d"), Code);
		break;
	}
	case 1:
	{
		int32 Applied = 0;
		if (UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
		{
			Applied = Inv->ApplyEquipRepairResult6572(Body);
		}
		// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): есть хвост «починить всё» (> 41 записи) — шлём следующий пакет 6571 и ждём
		//   его 6572; нотис «готово» и сброс режима — после последнего.
		if (RepairTailCont.Num() > 0)
		{
			UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			if (AsdaRepairSendTailChunk(Net, RepairTailCont, RepairTailSlots, kRepairBatch))
			{
				bPending = true;
				++RepairChunksSent;
				UE_LOG(LogAsdaRepair, Log, TEXT("[ремонт] 6572 успех (слотов %d) — отправлен следующий 6571 (пакет %d, осталось %d)"),
					Applied, RepairChunksSent, RepairTailCont.Num());
				RebuildAll();
				return;   // режим «всё» держим до последнего ответа
			}
		}
		NoticeMsgBox(RepairLoc::kDone);
		RebuildAll();                    // эталон: `uiEquipPanelRefreshAll(v10)`
		HoverCost = 0;
		RefreshPriceTexts();
		UE_LOG(LogAsdaRepair, Log, TEXT("[ремонт] 6572 успех: слотов %d, деньги %d"), Applied, RdI32(Body.GetData() + 166));
		break;
	}
	case 11:
		NoticeMsgBox(RepairLoc::kNoGold);
		break;
	default:
		UE_LOG(LogAsdaRepair, Warning, TEXT("[ремонт] 6572 код %d — текста под него в эталоне нет"), Code);
		break;
	}
	RepairTailCont.Reset(); RepairTailSlots.Reset();   // ★ 29.10.2026: отказ/конец — хвост «починить всё» больше не шлём
	SelMode = 0;
	SelOne.Clear();
}

// ═════════════════════════════ НОТИСЫ ═════════════════════════════════════════════════════════

FString UAsdaRepairSubsystem::Loc(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

void UAsdaRepairSubsystem::NoticeMsgBox(int32 LocId) const
{
	// Эталон: `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)` = ONEBUT_MSGBOX 1500.
	// ★ Волна 1 (14.09.2026, В2): единый маршрут `AsdaNotice::MsgBoxLoc` — окно 1500, без оконной подсистемы —
	//   строка в ленту той же категорией. Категория/подпись кнопки — прежние.
	AsdaNotice::MsgBoxLoc(GetGameInstance(), EAsdaMsgCategory::ItemRefused, LocId, 2288);
}

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════

FString UAsdaRepairSubsystem::DumpControls() const
{
	FString S = TEXT("=== окно ремонта 57300: разобранные контролы ===\n");
	auto Line = [&S](const TCHAR* Id, const UObject* W) { S += FString::Printf(TEXT("  %-34s %s\n"), Id, W ? TEXT("есть") : TEXT("НЕТ")); };
	Line(TEXT("57369 INVEN_Repair_X"),        BtnClose);
	Line(TEXT("57305 INVEN_Repair_BTN1"),     BtnRepairEquip);
	Line(TEXT("57306 INVEN_Repair_BTN2"),     BtnRepairAll);
	Line(TEXT("57325 INVEN_Repair_T1"),       TxtGoldLabel);
	Line(TEXT("57326 INVEN_Repair_T2"),       TxtGold);
	Line(TEXT("57327 строка ассета TXT00"),   TxtCostLabel);
	Line(TEXT("57328 строка ассета TXT01"),   TxtCostValue);
	Line(TEXT("79400 INVEN_Repair_new1_grp"), WarGroup);
	Line(TEXT("79401 new1_t1"),               TxtWarBaseLbl);
	Line(TEXT("79402 new1_t2"),               TxtWarBase);
	Line(TEXT("79403 new1_t3"),               TxtWarPct);
	Line(TEXT("79404 new1_t4"),               TxtWarDelta);
	Line(TEXT("79405 new1_t5"),               TxtWarTotLbl);
	Line(TEXT("79406 new1_t6"),               TxtWarTotal);
	Line(TEXT("57362 Imark_Click"),           SelFrame);
	for (int32 i = 0; i < kEquipCells; ++i)
	{
		S += FString::Printf(TEXT("  %d Jmark_%-2d икона %s подпись %s\n"), 57311 + i, kJmarkLabelBySlot[i],
			EquipUi[i].Icon ? TEXT("есть") : TEXT("НЕТ"), EquipUi[i].Label ? TEXT("есть") : TEXT("НЕТ"));
	}
	for (int32 i = 0; i < kBagGridCells; ++i)
	{
		S += FString::Printf(TEXT("  %d Imark_%-2d икона %s подпись %s\n"), 57331 + i, i + 1,
			BagUi[i].Icon ? TEXT("есть") : TEXT("НЕТ"), BagUi[i].Label ? TEXT("есть") : TEXT("НЕТ"));
	}
	return S;
}

FString UAsdaRepairSubsystem::DumpState() const
{
	FString S = FString::Printf(
		TEXT("=== окно ремонта: состояние ===\n  открыто=%s ждём ответ=%s режим=%d\n")
		TEXT("  сумма надетого=%d  сумма всего=%d  цена под курсором=%d\n"),
		bOpen ? TEXT("да") : TEXT("нет"), bPending ? TEXT("да") : TEXT("нет"), SelMode,
		TotalEquip, TotalAll, HoverCost);
	S += FString::Printf(TEXT("  сумка: позиций %d, ряд прокрутки %d/%d; хвост «починить всё» %d записей, пакетов 6571 отправлено %d\n"),
		BagScroll.Total, BagScroll.Row, BagScroll.MaxRow(), RepairTailCont.Num(), RepairChunksSent);
	const int32 Slot = AsdaUi::WarCitySlot(GetGameInstance());
	int32 Pct = 0; bool bN = true;
	const bool bOwn = AsdaUi::WarCityOwnedBySelf(GetGameInstance(), Slot, Pct, bN);
	S += FString::Printf(TEXT("  город: слот=%d своя=%s нейтрален=%s ставка=%d%%\n"),
		Slot, bOwn ? TEXT("да") : TEXT("нет"), bN ? TEXT("да") : TEXT("нет"), Pct);
	for (int32 i = 0; i < kEquipCells; ++i)
	{
		if (Equip[i].IsEmpty()) { continue; }
		S += FString::Printf(TEXT("  надетое[%2d] код=%d контейнер=%d слот=%d цена=%d\n"),
			i, Equip[i].ItemCode, Equip[i].Container, Equip[i].Slot, Equip[i].Cost);
	}
	for (int32 i = 0; i < kShellCells; ++i)
	{
		if (Bag[i].IsEmpty()) { continue; }
		S += FString::Printf(TEXT("  сумка[%2d]   код=%d контейнер=%d слот=%d цена=%d\n"),
			i, Bag[i].ItemCode, Bag[i].Container, Bag[i].Slot, Bag[i].Cost);
	}
	return S;
}

// ═══════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ (проверка без NPC) ════════════════════════════
namespace
{
	UAsdaRepairSubsystem* RepairSub(UWorld* W)
	{
		return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaRepairSubsystem>() : nullptr;
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GRepairOpen(
	TEXT("asda.Repair.Open"),
	TEXT("asda.Repair.Open — открыть окно ремонта 57300 так же, как код диалога NPC 30069"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaRepairSubsystem* S = RepairSub(W);
		if (!S) { UE_LOG(LogAsdaRepair, Warning, TEXT("подсистема недоступна")); return; }
		S->OpenWindow();
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GRepairClose(
	TEXT("asda.Repair.Close"),
	TEXT("asda.Repair.Close — закрыть окно ремонта (эталонный uiEquipUpgradePanelHide)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaRepairSubsystem* S = RepairSub(W)) { S->CloseWindow(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GRepairDump(
	TEXT("asda.Repair.Dump"),
	TEXT("asda.Repair.Dump — дамп КАЖДОГО контрола окна 57300 + обе сетки, суммы и блок войны"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaRepairSubsystem* S = RepairSub(W);
		if (!S) { UE_LOG(LogAsdaRepair, Warning, TEXT("подсистема недоступна")); return; }
		UE_LOG(LogAsdaRepair, Log, TEXT("\n%s\n%s"), *S->DumpControls(), *S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GRepairClick(
	TEXT("asda.Repair.Click"),
	TEXT("asda.Repair.Click <ячейка> [bag] — прогнать клик ТЕМ ЖЕ путём, что и мышь "
	     "(в headless-PIE мышью не кликнуть, а второй путь обработки заводить нельзя)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaRepairSubsystem* S = RepairSub(W);
		if (!S) { UE_LOG(LogAsdaRepair, Warning, TEXT("подсистема недоступна")); return; }
		const int32 Cell = A.Num() > 0 ? FCString::Atoi(*A[0]) : 0;
		const bool bBag  = A.Num() > 1 && A[1].Equals(TEXT("bag"), ESearchCase::IgnoreCase);
		S->OnCellClicked(Cell, bBag);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GRepairAll(
	TEXT("asda.Repair.Button"),
	TEXT("asda.Repair.Button <equip|all> — нажать 57305 (ремонт надетого) либо 57306 (общий ремонт)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaRepairSubsystem* S = RepairSub(W);
		if (!S) { UE_LOG(LogAsdaRepair, Warning, TEXT("подсистема недоступна")); return; }
		if (A.Num() > 0 && A[0].Equals(TEXT("all"), ESearchCase::IgnoreCase)) { S->OnRepairAllClicked(); }
		else { S->OnRepairEquipClicked(); }
	}));
#endif // ASDA_DEV_CONSOLE
