// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окна «Доклад об охоте» 56450 (волна 2-Б, 06.10.2026) — весь реверс с адресами в шапке
//   `UI/AsdaHuntReportSubsystem.h` и в отчёте `Reports/owner_1006/wave2B_hunt_donation.md` §2.
#include "UI/AsdaHuntReportSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "UI/AsdaJuiWindowHandle.h"        // каркас окон JUI (Ensure / RegisterFrameZone)
#include "UI/AsdaNotice.h"                 // фасад нотисов: Loc / MsgBox (ONEBUT_MSGBOX 1500)
#include "UI/AsdaWindowSubsystem.h"        // реестр окон, нумпад 1410 (`ShowNumPad`)
#include "UI/AsdaMessageSubsystem.h"       // EAsdaMsgCategory — канал нотиса
#include "UI/AsdaInventorySubsystem.h"     // контейнер 2, иконки, применение записи 6151
#include "UI/AsdaItemTooltipSubsystem.h"   // тултип ячейки (наведение, событие 8)
#include "UI/AsdaTipPanel.h"               // EAsdaTipOwner::HuntReport
#include "Items/AsdaItemTableSubsystem.h"  // item_list (LinkCode/ItemType), pricetable (dw05), title_list (Name)
#include "Items/AsdaItemDbRows.h"          // FAsdaPriceRow / FAsdaTitleRow
#include "AsdaNetSubsystem.h"              // 6150 / 6151
#include "AsdaBytes.h"                     // RdI16 / RdI32

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Login/AsdaButtonRelay.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaHuntReport, Log, All);
// ★ U21 (В1 дедупа №2): имя константы — по контейнеру; значение обязано совпадать с размером контейнера инвентаря.
static_assert(UAsdaHuntReportSubsystem::kSpendCells == UAsdaInventorySubsystem::kSpendSlots, "размер контейнера разошёлся с инвентарём");

// ── Константы окна ──
static const FName  kHuntWindowId(TEXT("Hunt_report_Win"));   // имя корня в `.JUI`/реестре окон (пробел импортёр заменил на «_»)
static const TCHAR* kHuntWbp =
	TEXT("/Game/AsdaAssets/UI/Player_UI/Character/WBP_Hunt_report_Win.WBP_Hunt_report_Win_C");

// ── Loc-идентификаторы. ВСЕ сняты с эталонных вызовов `UI_FormatLocalizedNotice` (по обработчику). ──
namespace HuntLoc
{
	constexpr int32 kCaption   = 110210;   // 56494 «доклад о результате охоты на монстров» (`uiSowelWindow_InitControls`)
	constexpr int32 kBtnOk     = 110211;   // 56493 «Отнести знаки»
	constexpr int32 kBtnNo     = 110212;   // 56492 «отмена»
	constexpr int32 kTitleFmt  = 110214;   // 56452 «звание '%s' » (`itemGrid_RefreshSlots`)
	constexpr int32 kDone1     = 110213;   // 6151 код 1: «Поздравляем. Задание '%s' выполнено.» (имя предмета)
	constexpr int32 kDone2     = 110215;   // 6151 код 2: «…'%s' выполнено, и ты получаешь звание '%s'…» (предмет, звание)
	constexpr int32 kErrFmt    = 110404;   // 6151 коды 0,3–8,10–12: «Ошибка задания на убийство монстра %d…»
	constexpr int32 kHaveTitle = 110405;   // 6151 код 9: «Звание уже получено»
}

// ★ [ПОРТ] Д1 — перерисовка окна при изменении сумки извне (обоснование — шапка .h). 0 — как оригинал.
static ASDA_DEV_CVAR(int32, CVarHuntLiveRefresh,
	TEXT("asda.HuntReport.LiveRefresh"), 1,
	TEXT("1 — [ПОРТ] окно 56450 перерисовывается при любом изменении сумки (исправление протухших ячеек оригинала); ")
	TEXT("0 — как оригинал: только при показе, клике, ОК нумпада и ответе 6151."),
	ECVF_Default);

using AsdaBytes::RdI16;
using AsdaBytes::RdI32;

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaHuntReportSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	CellUi.SetNum(kCells);
	for (int32 i = 0; i < kCells; ++i) { CellBag[i] = -1; }

	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь.
		HResult = Net->OnHuntReportResult.AddLambda([this](const TArray<uint8>& B) { PendingResults.Push(B); });
	}
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		// [ПОРТ] Д1: из колбэка только МЕТИМ — перерисует Tick (один пакет двигает много ячеек подряд).
		HInvCell = Inv->OnCellChanged.AddLambda([this](int32 /*Container*/, int32 /*Cell*/) { bDirty = true; });
	}
	UE_LOG(LogAsdaHuntReport, Log, TEXT("[доклад об охоте] подсистема поднята"));
}

void UAsdaHuntReportSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr) { Net->OnHuntReportResult.Remove(HResult); }
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr) { Inv->OnCellChanged.Remove(HInvCell); }
	ResetWidget();
	Super::Deinitialize();
}

// Сброс сессии. Эталонный `sub_ECF8F0` (сброс сцены) гасит только «открыто» и «привязано»; выбор знака — указатель в
//   живую сумку — переживает смену карты. [ПОРТ] при полном сбросе (разрыв/выбор персонажа/вход) выбор и копия отправленной
//   записи тоже чистятся: следующий вход может быть другим персонажем (оригинал при разрыве просто закрывается).
void UAsdaHuntReportSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	CloseWindow();
	PendingResults.Clear();
	bDirty = false;
	Mouse.Reset();
	TipCell = -1;
	if (AsdaIsFullSessionReset(Reason))
	{
		SelBagCell = -1; PendingBagCell = -1; SelQty = 0; SentItemCode = 0;
	}
}

void UAsdaHuntReportSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.HuntReport");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	DrainPendingPackets();
	if (!bOpen || !bControlsReady) { return; }
	if (bDirty)
	{
		bDirty = false;
		if (CVarHuntLiveRefresh.GetValueOnGameThread() != 0) { Refresh(); }
	}
	Mouse.Update();
	// ★ Модальный гейт (закон из `AsdaUi::IsModalDispatchBlocked`): под нумпадом/нотисом эталон мышь окну не отдаёт.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		if (TipCell >= 0)
		{
			TipCell = -1;
			if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
			{
				Tip->HideTooltipFor(EAsdaTipOwner::HuntReport);
			}
		}
		Mouse.Reset();
		return;
	}
	TickCells();
}

// ═════════════════════════════ ОКНО ═══════════════════════════════════════════════════════════

bool UAsdaHuntReportSubsystem::EnsureWidget()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kHuntWindowId, kHuntWbp, kZ).Ensure(GetGameInstance(), Widget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem&)
		{
			SetupControls();
			RegisterZone();
		},
		[]()
		{
			UE_LOG(LogAsdaHuntReport, Error, TEXT("[доклад об охоте] WBP_Hunt_report_Win не загрузился (%s)"), kHuntWbp);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaHuntReportSubsystem::ResetWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kHuntWindowId);
		WM->CloseWindow(kHuntWindowId);
	}
	Widget = nullptr;
	for (FAsdaHuntCellUi& U : CellUi) { U = FAsdaHuntCellUi(); }
	ResultIcon = nullptr; ResultQty = nullptr; ResultText = nullptr; Caption = nullptr; SelectFrame = nullptr;
	BtnOk = BtnNo = BtnClose = nullptr; BtnOkText = BtnNoText = nullptr;
	Relays.Reset();
	bControlsReady = false;
	bOpen = false;
	TipCell = -1;
	Mouse.Reset();
}

// Разбор ВСЕХ контролов окна (закон `full-structural-analysis-always`), 1:1 с `uiSowelWindow_InitControls@0xECF680`.
void UAsdaHuntReportSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }

	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("Hunt_report_Ok")) { BtnOk = Btn; }
			else if (N == TEXT("Hunt_report_No")) { BtnNo = Btn; }
			else if (N == TEXT("Hunt_report_X")) { BtnClose = Btn; }
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == TEXT("Hunt_report_Ok_Text"))        { BtnOkText = Tb; continue; }
			if (N == TEXT("Hunt_report_No_Text"))        { BtnNoText = Tb; continue; }
			if (N == TEXT("Hunt_report_Text_Move"))      { Caption = Tb; continue; }
			if (N == TEXT("Hunt_report_Text1"))          { ResultText = Tb; continue; }
			if (N == TEXT("Hunt_report_Mark_0"))         { ResultQty = Tb; continue; }
			// Число стопки ячейки: импортёр зовёт `TextBlock` ровно как idc контрола (`Hunt_report_Mark_N`).
			for (int32 i = 0; i < kCells; ++i)
			{
				if (N == FString::Printf(TEXT("Hunt_report_Mark_%d"), i + 1)) { CellUi[i].Label = Tb; break; }
			}
			continue;
		}
		FString Idc; int32 JuiIdx = 0, PlaneIdx = 0;
		if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, PlaneIdx)) { continue; }
		if (Idc == TEXT("Hunt_report_Select")) { SelectFrame = W; continue; }   // 56491 `Slot_AniSelect` (анимированный план)
		UImage* Img = Cast<UImage>(W);
		if (!Img) { continue; }
		if (Idc == TEXT("Hunt_report_Mark_0")) { ResultIcon = Img; continue; }
		for (int32 i = 0; i < kCells; ++i)
		{
			if (Idc == FString::Printf(TEXT("Hunt_report_Mark_%d"), i + 1)) { CellUi[i].Icon = Img; break; }
		}
	}

	// ── Кнопки (диспетчер `uiTooltipWndMsgDispatch@0xED0EE0`, событие 66561) ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnOk,    56493, [this](int32) { OnSendClicked(); });   // `net_sendBlobSyncOpcode6150`
	Bind(BtnNo,    56492, [this](int32) { CloseWindow(); });     // `uiSowelWindowToggle`
	Bind(BtnClose, 56499, [this](int32) { CloseWindow(); });     // `uiSowelWindowToggle`

	// ── Статичные подписи (эталон ставит их в `uiSowelWindow_InitControls`) ──
	if (Caption)   { Caption->SetText(FText::FromString(Loc(HuntLoc::kCaption))); }      // `CControl_SetText(56494, 110210)`
	if (BtnOkText) { BtnOkText->SetText(FText::FromString(Loc(HuntLoc::kBtnOk))); }      // `CWidget_SetTextAll4(56493, 110211)`
	if (BtnNoText) { BtnNoText->SetText(FText::FromString(Loc(HuntLoc::kBtnNo))); }      // `CWidget_SetTextAll4(56492, 110212)`
	HideSelectFrame();

	int32 Icons = 0, Labels = 0;
	for (const FAsdaHuntCellUi& U : CellUi) { Icons += U.Icon ? 1 : 0; Labels += U.Label ? 1 : 0; }
	bControlsReady = (Icons == kCells && ResultIcon != nullptr && BtnOk != nullptr);
	UE_LOG(LogAsdaHuntReport, Log,
		TEXT("[доклад об охоте] окно 56450 разобрано: ячейки %d/%d икон, %d подписей; 56451 %s/%s; 56452 %s; 56491 %s; ")
		TEXT("56493/56492/56499 %s/%s/%s; 56494 %s → %s"),
		Icons, kCells, Labels, ResultIcon ? TEXT("иконка") : TEXT("НЕТ"), ResultQty ? TEXT("число") : TEXT("НЕТ"),
		ResultText ? TEXT("да") : TEXT("НЕТ"), SelectFrame ? TEXT("да") : TEXT("НЕТ"),
		BtnOk ? TEXT("да") : TEXT("НЕТ"), BtnNo ? TEXT("да") : TEXT("НЕТ"), BtnClose ? TEXT("да") : TEXT("НЕТ"),
		Caption ? TEXT("да") : TEXT("НЕТ"), bControlsReady ? TEXT("ГОТОВО") : TEXT("НЕ ГОТОВО"));
}

void UAsdaHuntReportSubsystem::RegisterZone()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Корпус окна — девять планов рамки `Black_BottomRound_White2Border` (`Hunt_report_Win_0_0…8`).
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kHuntWindowId, this, Widget, bOpen, TEXT("Hunt_report_Win_0_%d"), 9);
}

// ═════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ════════════════════════════════════════════

void UAsdaHuntReportSubsystem::OpenWindow()
{
	if (!EnsureWidget()) { return; }
	// `uiSowelWindow_Show@0xECFFF0`: `if (filler[272] != 1) { SetVisible(1); filler[272] = 1; MapInsert; Refresh; }`.
	if (bOpen) { return; }
	bOpen = true;
	TipCell = -1;
	Mouse.Reset();
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kHuntWindowId, true);
	}
	Refresh();
	UE_LOG(LogAsdaHuntReport, Log, TEXT("[доклад об охоте] окно открыто: выбран знак в ячейке %d, кол-во %d"), SelBagCell, SelQty);
}

void UAsdaHuntReportSubsystem::CloseWindow()
{
	if (!bOpen) { return; }
	// `uiSowelWindowToggle@0xED0070`: `SetVisible(0); filler[272] = 0;` — выбор (`filler[276]`) НЕ трогается.
	bOpen = false;
	TipCell = -1;
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::HuntReport);
	}
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kHuntWindowId, false);
	}
	UE_LOG(LogAsdaHuntReport, Log, TEXT("[доклад об охоте] окно закрыто"));
}

// ═════════════════════════════ РЕНДЕР ═════════════════════════════════════════════════════════

bool UAsdaHuntReportSubsystem::TitleNameOf(int32 ItemCode, FString& OutName) const
{
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return false; }
	// `CItemManager_GetTable2Record_0@0x9C39F0` = pricetable (13 dword, шаг 52), поле [5] = `dw05` = id звания;
	//   `dataRecLookupById@0x9C48A0` = title_list (240 б), `+16` = имя.
	FAsdaPriceRow P;
	if (!IT->GetPrice(ItemCode, P)) { return false; }
	FAsdaTitleRow T;
	if (!IT->GetTitle(P.dw05, T)) { return false; }
	OutName = T.Name;
	return true;
}

// `itemGrid_RefreshSlots@0xECF930` (item_inventory.c:64696-64830), 1:1 по шагам.
void UAsdaHuntReportSubsystem::Refresh()
{
	if (!bControlsReady) { return; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;

	// 1) 30 ячеек: пустая текстура 32×32, текст пуст, указатель ячейки = 0.
	for (int32 i = 0; i < kCells; ++i)
	{
		CellBag[i] = -1;
		if (Inv && CellUi[i].Icon) { Inv->ClearIcon(CellUi[i].Icon); }
		if (CellUi[i].Label) { CellUi[i].Label->SetText(FText::GetEmpty()); }
	}
	// 2) Слот 56451 пуст, рамка 56491 скрыта, текст 56452 пуст.
	if (Inv && ResultIcon) { Inv->ClearIcon(ResultIcon); }
	if (ResultQty) { ResultQty->SetText(FText::GetEmpty()); }
	HideSelectFrame();
	if (ResultText) { ResultText->SetText(FText::GetEmpty()); }
	if (!Inv || !IT) { return; }

	// Выбранный знак — запись, на которую смотрит `filler[276]`; эталон сравнивает её СЛОТ (+5) со слотом записи сумки.
	const FAsdaItemInstance* Sel = (SelBagCell >= 0) ? Inv->GetCell(kBagContainer, SelBagCell) : nullptr;

	// 3) Обход 60 записей `g_InventoryArray2`: знаки (`linkCode == 208`) подряд в ячейки окна.
	int32 Grid = 0;
	const int32 BagCells = FMath::Min(Inv->GetContainerSize(kBagContainer), kSpendCells);
	for (int32 j = 0; j < BagCells; ++j)
	{
		const FAsdaItemInstance* It = Inv->GetCell(kBagContainer, j);
		if (!It || It->IsEmpty()) { continue; }
		const FAsdaItemRecord* R = IT->GetItem(It->ItemCode);   // `CItemManager_GetItemRecord`
		if (!R || R->LinkCode != kLinkCodeMark) { continue; }
		// ★ [ПОРТ] Д2: эталон проверяет `gridSlotIndex >= 30` только в ветке выбранного знака — при > 30 знаках писал бы за
		//   конец массива ячеек. Порт ограничивает заполнение 30 ячейками всегда (видимых ячеек ровно 30, как и у эталона).
		if (Grid >= kCells) { break; }
		CellBag[Grid] = j;
		Inv->ApplyIconToImage(CellUi[Grid].Icon, It->ItemCode, 32.f);
		if (It->Amount > 0 && CellUi[Grid].Label)   // `if (*(int*)(запись + 9) > 0) sprintf("%d")`
		{
			CellUi[Grid].Label->SetText(FText::FromString(FString::FromInt(It->Amount)));
		}
		const UWidget* CellIcon = CellUi[Grid].Icon;
		++Grid;
		if (Sel && Sel->SlotIndex == It->SlotIndex)
		{
			// Число выбранного — в слот 56451 (`if (filler[344] > 0)`), ДО поиска звания.
			if (SelQty > 0 && ResultQty) { ResultQty->SetText(FText::FromString(FString::FromInt(SelQty))); }
			// Звание — по коду ВЫБРАННОЙ записи (`**(DWORD**)&filler[276]`), иконка — по записи сумки (`ItemRecord->imgIndex`).
			FString TitleName;
			if (TitleNameOf(Sel->ItemCode, TitleName))
			{
				Inv->ApplyIconToImage(ResultIcon, It->ItemCode, 64.f);
				if (ResultText)
				{
					ResultText->SetText(FText::FromString(AsdaUi::FormatPrintfArgs(Loc(HuntLoc::kTitleFmt), { TitleName })));
				}
				MoveSelectFrame(CellIcon);   // `CControl_SetField74_75(56491, ячейка + (10,7))` = точно на ячейку
			}
		}
	}
}

void UAsdaHuntReportSubsystem::HideSelectFrame()
{
	if (SelectFrame) { SelectFrame->SetVisibility(ESlateVisibility::Collapsed); }
}

// Эталон ставит рамку 56491 в координаты ячейки (`Class_GetVec2_74(ячейка)` + (10,7) — смещение контейнера ячеек
//   `NoImage_icon` в общем родителе), то есть РОВНО на ячейку. Переводим абсолютную позицию ячейки в локальную родителя рамки.
void UAsdaHuntReportSubsystem::MoveSelectFrame(const UWidget* OverIcon)
{
	if (!SelectFrame || !OverIcon) { HideSelectFrame(); return; }
	// ★ Дедуп №2, В2-B (U19): общая точка рамки выбора (тот же расчёт, без перезапуска кадров — как было).
	AsdaUi::PlaceFrameOver(SelectFrame, OverIcon, /*bRestartAnim*/false);
}

// ═════════════════════════════ ДЕЙСТВИЯ ═══════════════════════════════════════════════════════

// `itemGrid_OnSlotContextMenu@0xED00D0`: ячейка с предметом → запомнить и открыть нумпад режима 9 (у курсора),
//   максимум = стопка (+9). Перерисовка — всегда, даже по пустой ячейке.
void UAsdaHuntReportSubsystem::OnCellClicked(int32 Cell)
{
	if (Cell >= 0 && Cell < kCells && CellBag[Cell] >= 0)
	{
		UGameInstance* GI = GetGameInstance();
		const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		const FAsdaItemInstance* It = Inv ? Inv->GetCell(kBagContainer, CellBag[Cell]) : nullptr;
		if (It && WM)
		{
			PendingBagCell = CellBag[Cell];   // `filler[280] = filler[8*i+24]`
			TWeakObjectPtr<UAsdaHuntReportSubsystem> WeakThis(this);
			// Режим 9: OK → `uiUpgradeQtyDlgOk` (`> 0` ⇒ `itemGridSetDataAndRefresh`), отмена → `sub_DA6820` (только закрыть).
			//   Проверку `1..максимум` делает общая точка `ShowNumPad` (поле и так режется `uiClampQuantityToMax`).
			WM->ShowNumPad(It->Amount, [WeakThis](bool bOk, int32 Qty)
			{
				if (bOk && Qty > 0 && WeakThis.IsValid()) { WeakThis->OnNumPadAccepted(Qty); }
			}, /*bCenterOnScreen*/ false);
			UE_LOG(LogAsdaHuntReport, Log, TEXT("[доклад об охоте] клик по ячейке %d (сумка %d, код %d, стопка %d) → нумпад режим 9"),
				Cell, CellBag[Cell], It->ItemCode, It->Amount);
		}
	}
	Refresh();
}

// `itemGridSetDataAndRefresh@0xED0E90`: `filler[344] = кол-во; filler[276] = filler[280]; refresh`.
void UAsdaHuntReportSubsystem::OnNumPadAccepted(int32 Qty)
{
	SelQty = Qty;
	SelBagCell = PendingBagCell;
	UE_LOG(LogAsdaHuntReport, Log, TEXT("[доклад об охоте] выбран знак: ячейка сумки %d, кол-во %d"), SelBagCell, SelQty);
	Refresh();
}

// `net_sendBlobSyncOpcode6150@0xED0170`: только при выбранном знаке (`if (this[69])`); копия записи — в `filler[284]`.
//   Защёлки «ждём ответ» у эталона НЕТ (повторное нажатие шлёт снова) — порт её не заводит.
void UAsdaHuntReportSubsystem::OnSendClicked()
{
	if (SelBagCell < 0) { return; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const FAsdaItemInstance* It = Inv ? Inv->GetCell(kBagContainer, SelBagCell) : nullptr;
	if (!It || !Net) { return; }
	// Поля берутся из САМОЙ записи (её +4/+5), как у эталона — даже если запись с тех пор опустела (сервер ответит кодом).
	const uint8 Cont = (It->Raw.Num() > 6) ? It->Raw[4] : (uint8)It->ContainerType;
	const int32 Slot = (It->Raw.Num() > 6) ? (int32)RdI16(It->Raw.GetData() + 5) : It->SlotIndex;
	SentItemCode = It->ItemCode;   // `qmemcpy(self + 71, запись, 57)` — нужен приёму 6151 (имя предмета и звание)
	Net->SendTitleItemCheck6150(Cont, Slot, SelQty);
}

// ═════════════════════════════ ПРИЁМ 6151 ═════════════════════════════════════════════════════

void UAsdaHuntReportSubsystem::DrainPendingPackets()
{
	// ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива. Прежний цикл `while … [0]; RemoveAt(0)` давал тот же порядок:
	//   во время разбора новые тела в очередь не попадают (колбэк сети приходит из `UAsdaNetSubsystem::Tick`, не отсюда).
	TArray<TArray<uint8>> Local;
	if (!PendingResults.Drain(Local)) { return; }
	for (const TArray<uint8>& B : Local) { OnResult6151(B); }
}

// `inventoryUI_onItemMovePacket@0xED0820` (item_inventory.c:64850-64967) — все ветки.
void UAsdaHuntReportSubsystem::OnResult6151(const TArray<uint8>& Body)
{
	if (Body.Num() < 66) { return; }
	UGameInstance* GI = GetGameInstance();
	const int32 Code = (int32)Body[61];   // `BYTE1(itemSlot[15])`
	switch (Code)
	{
	case 0: case 3: case 4: case 5: case 6: case 7: case 8: case 10: case 11: case 12:
		// `sprintf(110404, код)` → ONEBUT_MSGBOX (`CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, …, 1)`).
		MsgBoxText(AsdaUi::FormatPrintfArgs(Loc(HuntLoc::kErrFmt), { FString::FromInt(Code) }), EAsdaMsgCategory::ItemRefused);
		UE_LOG(LogAsdaHuntReport, Warning, TEXT("[доклад об охоте] 6151 отказ, код %d"), Code);
		break;
	case 1:
	case 2:
	{
		// Вес поставлен в `UAsdaNetSubsystem` (первая строка ветки эталона). Запись: кол-во 0 ⇒ ячейка очищается,
		//   > 0 ⇒ кладётся целиком (`memcpy` в `g_InventoryArray2` — контейнер 2; ветка контейнера 1 недостижима:
		//   сервер принимает только контейнер 2, код 3). Отрицательное кол-во эталон не трогает.
		TArray<uint8> Rec; Rec.Append(Body.GetData() + 4, FAsdaItemInstance::kRecordSize);
		const int32 Amount = RdI32(Body.GetData() + 13);   // запись +9
		if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
		{
			if (Amount == 0)     { Inv->ApplyExternalRecord(Rec, UAsdaInventorySubsystem::EExternalRecordMode::CountOrClear); }
			else if (Amount > 0) { Inv->ApplyExternalRecord(Rec, UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
		}
		// По копии ОТПРАВЛЕННОЙ записи: предмет → pricetable → звание; всё найдено ⇒ нотис и сброс выбора.
		const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		FString TitleName;
		if (IT && IT->GetItem(SentItemCode) && TitleNameOf(SentItemCode, TitleName))
		{
			const FString ItemName = IT->GetItemName(SentItemCode);   // `ItemData_FindByCode(itemCode)` — общая точка имени
			// Порядок аргументов 110215 снят с asm (0xED0B01…0xED0B3A): сначала имя предмета, затем звание.
			const FString Text = (Code == 1)
				? AsdaUi::FormatPrintfArgs(Loc(HuntLoc::kDone1), { ItemName })
				: AsdaUi::FormatPrintfArgs(Loc(HuntLoc::kDone2), { ItemName, TitleName });
			MsgBoxText(Text, EAsdaMsgCategory::Progress);
			SelBagCell = -1;   // `*(DWORD*)&filler[276] = 0`
		}
		UE_LOG(LogAsdaHuntReport, Log, TEXT("[доклад об охоте] 6151 код %d: предмет %d, новое кол-во %d, звание «%s»"),
			Code, SentItemCode, Amount, *TitleName);
		break;
	}
	case 9:
		MsgBoxText(Loc(HuntLoc::kHaveTitle), EAsdaMsgCategory::ItemRefused);   // «Звание уже получено»
		break;
	default:
		UE_LOG(LogAsdaHuntReport, Warning, TEXT("[доклад об охоте] 6151 код %d — у эталона ветки нет (молча)"), Code);
		break;
	}
	Refresh();   // `itemGrid_RefreshSlots(pWnd)` — всегда, в конце обработчика
}

// ═════════════════════════════ ПОКАДРОВЫЙ ОПРОС ═══════════════════════════════════════════════

void UAsdaHuntReportSubsystem::TickCells()
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;

	// Под курсором — ячейка 0…29 (по ЖИВОЙ геометрии, закон `jui-cell-chain-nesting`) либо слот 56451 (= 30).
	int32 Over = -1;
	for (int32 i = 0; i < kCells && Over < 0; ++i)
	{
		if (CellUi[i].Icon && AsdaUi::IsCursorOverWidget(CellUi[i].Icon)) { Over = i; }
	}
	if (Over < 0 && ResultIcon && AsdaUi::IsCursorOverWidget(ResultIcon)) { Over = kCells; }

	// Наведение (событие 8) — тултип предмета; уход (0xA) — `CControl_KillFocus(&g_ItemTooltip)`.
	const FAsdaItemInstance* TipIt = nullptr;
	if (Inv && Over >= 0 && Over < kCells && CellBag[Over] >= 0) { TipIt = Inv->GetCell(kBagContainer, CellBag[Over]); }
	if (Inv && Over == kCells && SelBagCell >= 0)                { TipIt = Inv->GetCell(kBagContainer, SelBagCell); }
	const int32 TipNow = (TipIt && !TipIt->IsEmpty()) ? Over : -1;
	if (TipNow != TipCell)
	{
		TipCell = TipNow;
		if (Tip)
		{
			if (TipNow < 0) { Tip->HideTooltipFor(EAsdaTipOwner::HuntReport); }
			else
			{
				// `itemGridSlotShowTooltip@0xED0CC0`: `item_list` и (тип ≤ 4 / 8…9 / категория 3) → ПОЛНЫЙ билдер
				//   (`ItemTooltip_Build`), иначе ПРОСТОЙ (`CControl_SetState(…,0,код,кол-во,1,−1,0)`); самоцвет и запись второй
				//   таблицы — их карточки (у `ShowItemTooltip` при отсутствии записи `item_list`). Пайплайн выбирает `SourceKind`:
				//   1 — полный, 2 — простой с количеством (+9). ⚠ Привязку простой билдер этого окна получает −1; у знаков
				//   (тип 11) общая точка даёт тот же −1.
				const FAsdaItemRecord* R = IT ? IT->GetItem(TipIt->ItemCode) : nullptr;
				const bool bFull = !R || R->ItemType <= 4 || (R->ItemType >= 8 && R->ItemType <= 9) || R->CategoryFlag == 3;
				Tip->ShowItemTooltip(*TipIt, /*bSuppressCompare*/ false, EAsdaTipOwner::HuntReport, bFull ? 1 : 2);
			}
		}
	}

	// ЛКМ (событие 0xB) по ячейке 56461…56490 — `itemGrid_OnSlotContextMenu` (по пустой — только перерисовка).
	if (Mouse.bLmbDown && Over >= 0 && Over < kCells) { OnCellClicked(Over); }
}

// ═════════════════════════════ НОТИСЫ ═════════════════════════════════════════════════════════

FString UAsdaHuntReportSubsystem::Loc(int32 LocId) const
{
	return AsdaNotice::Loc(GetGameInstance(), LocId);   // общая точка: теги срезаны, маркеры как в таблице
}

void UAsdaHuntReportSubsystem::MsgBoxText(const FString& Text, EAsdaMsgCategory Cat) const
{
	// Эталон: `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)` = ONEBUT_MSGBOX 1500, подпись «ОК» 2288.
	AsdaNotice::MsgBox(GetGameInstance(), Cat, Text);
}

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════

int32 UAsdaHuntReportSubsystem::DebugCellCode(int32 Cell) const
{
	if (Cell < 0 || Cell >= kCells || CellBag[Cell] < 0) { return 0; }
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const FAsdaItemInstance* It = Inv ? Inv->GetCell(kBagContainer, CellBag[Cell]) : nullptr;
	return It ? It->ItemCode : 0;
}

FString UAsdaHuntReportSubsystem::DebugCellText(int32 Cell) const
{
	return (Cell >= 0 && Cell < kCells && CellUi[Cell].Label) ? CellUi[Cell].Label->GetText().ToString() : FString();
}

FString UAsdaHuntReportSubsystem::DebugResultText() const { return ResultText ? ResultText->GetText().ToString() : FString(); }
FString UAsdaHuntReportSubsystem::DebugResultQtyText() const { return ResultQty ? ResultQty->GetText().ToString() : FString(); }
bool UAsdaHuntReportSubsystem::DebugSelectFrameShown() const
{
	return SelectFrame && SelectFrame->GetVisibility() != ESlateVisibility::Collapsed && SelectFrame->GetVisibility() != ESlateVisibility::Hidden;
}

FString UAsdaHuntReportSubsystem::DumpControls() const
{
	FString S = TEXT("=== окно «Доклад об охоте» 56450: разобранные контролы ===\n");
	auto Line = [&S](const TCHAR* Id, const UWidget* W)
	{
		FVector2D Mn = FVector2D::ZeroVector, Mx = FVector2D::ZeroVector;   // живой прямоугольник (GetCachedGeometry)
		const bool bGeo = W && AsdaUi::WidgetAbsRect(W, Mn, Mx);
		const FString Geo = bGeo ? FString::Printf(TEXT("rect=(%.0f,%.0f)-(%.0f,%.0f)"), Mn.X, Mn.Y, Mx.X, Mx.Y) : FString(TEXT("rect=нет"));
		S += FString::Printf(TEXT("  %-34s %s %s\n"), Id, W ? TEXT("есть") : TEXT("НЕТ"), *Geo);
	};
	Line(TEXT("56494 Hunt_report_Text_Move"), Caption);
	Line(TEXT("56451 Hunt_report_Mark_0 икона"), ResultIcon);
	Line(TEXT("56451 Hunt_report_Mark_0 число"), ResultQty);
	Line(TEXT("56452 Hunt_report_Text1"), ResultText);
	Line(TEXT("56491 Hunt_report_Select"), SelectFrame);
	Line(TEXT("56493 Hunt_report_Ok"), BtnOk);
	Line(TEXT("56493 Hunt_report_Ok_Text"), BtnOkText);
	Line(TEXT("56492 Hunt_report_No"), BtnNo);
	Line(TEXT("56492 Hunt_report_No_Text"), BtnNoText);
	Line(TEXT("56499 Hunt_report_X"), BtnClose);
	for (int32 i = 0; i < kCells; ++i)
	{
		Line(*FString::Printf(TEXT("%d Hunt_report_Mark_%d икона"), kFirstCellId + i, i + 1), CellUi[i].Icon);
		Line(*FString::Printf(TEXT("%d Hunt_report_Mark_%d число"), kFirstCellId + i, i + 1), CellUi[i].Label);
	}
	return S;
}

FString UAsdaHuntReportSubsystem::DumpState() const
{
	FString S = FString::Printf(TEXT("=== окно 56450: состояние ===\n  открыто=%s привязано=%s выбран(сумка)=%d кол-во=%d ждёт ОК(сумка)=%d отправлен код=%d\n")
		TEXT("  56451 число=«%s» 56452=«%s» рамка=%s\n"),
		bOpen ? TEXT("да") : TEXT("нет"), bControlsReady ? TEXT("да") : TEXT("нет"), SelBagCell, SelQty, PendingBagCell, SentItemCode,
		*DebugResultQtyText(), *DebugResultText(), DebugSelectFrameShown() ? TEXT("видна") : TEXT("скрыта"));
	for (int32 i = 0; i < kCells; ++i)
	{
		if (CellBag[i] < 0) { continue; }
		S += FString::Printf(TEXT("  ячейка %2d (%d) ← сумка %2d код=%d число=«%s»\n"), i, kFirstCellId + i, CellBag[i], DebugCellCode(i), *DebugCellText(i));
	}
	return S;
}

// ═══════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ (проверка без NPC) ════════════════════════════
namespace
{
	// Подсистема окна мира W (nullptr — нет).
	UAsdaHuntReportSubsystem* HuntSub(UWorld* W)
	{
		return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaHuntReportSubsystem>() : nullptr;
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GHuntOpen(
	TEXT("asda.HuntReport.Open"),
	TEXT("asda.HuntReport.Open — открыть окно 56450 так же, как код диалога NPC 30068"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaHuntReportSubsystem* S = HuntSub(W)) { S->OpenWindow(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GHuntClose(
	TEXT("asda.HuntReport.Close"),
	TEXT("asda.HuntReport.Close — закрыть окно 56450 (эталонный uiSowelWindowToggle)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaHuntReportSubsystem* S = HuntSub(W)) { S->CloseWindow(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GHuntDump(
	TEXT("asda.HuntReport.Dump"),
	TEXT("asda.HuntReport.Dump — дамп КАЖДОГО контрола окна 56450 (живая геометрия) + сетка и выбор"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaHuntReportSubsystem* S = HuntSub(W)) { UE_LOG(LogAsdaHuntReport, Log, TEXT("\n%s\n%s"), *S->DumpControls(), *S->DumpState()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GHuntClick(
	TEXT("asda.HuntReport.Click"),
	TEXT("asda.HuntReport.Click <ячейка 0..29> — ЛКМ по ячейке тем же путём, что мышь (нумпад режима 9)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (UAsdaHuntReportSubsystem* S = HuntSub(W)) { S->OnCellClicked(A.Num() > 0 ? FCString::Atoi(*A[0]) : 0); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GHuntSend(
	TEXT("asda.HuntReport.Send"),
	TEXT("asda.HuntReport.Send — нажать 56493 «Отнести знаки» (6150 по выбранному знаку)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaHuntReportSubsystem* S = HuntSub(W)) { S->OnSendClicked(); }
	}));
#endif // ASDA_DEV_CONSOLE
