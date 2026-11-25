// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окна «Изменение параметров предмета» (JUI 50300) — весь реверс с адресами в шапке
//   UI/AsdaItemOptionSubsystem.h и в отчёте `Reports/npc_exchange_params_30060.md`.
#include "UI/AsdaItemOptionSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaAnimatedPlane.h"
#include "UI/AsdaMarkup.h"
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
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Login/AsdaButtonRelay.h"
#include "Text/AsdaLang.h"   // [ПОРТ] 09.10.2026 (мультиязычность): формат параметра на языке сессии

DEFINE_LOG_CATEGORY_STATIC(LogAsdaItemOpt, Log, All);
// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): список кандидатов — вся сумка.
static_assert(UAsdaItemOptionSubsystem::kListMax == UAsdaInventorySubsystem::kShellSlots, "длина списка разошлась с ёмкостью сумки");

// ── Константы окна ──
static const FName  kOptWindowId(TEXT("ItemOptionValue_WIN"));
static const TCHAR* kOptWbp =
	TEXT("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_ItemOptionValue_WIN.WBP_ItemOptionValue_WIN_C");

// Loc-идентификаторы. ВСЕ сняты с эталонных вызовов `UI_FormatLocalizedNotice`
//   в `itemUpgradeWindowInitControls@0xB60BC0`, `itemUpgradeConfirmReady@0xB637F0`,
//   `itemSendUpgradeRequest@0xB611E0`, `Net_OnItemUpgradeResult@0xB61990`,
//   `CItemTooltip_BuildOptionLines@0xB62C70`, `invWeightTextRefresh@0xB624C0`.
namespace ItemOptLoc
{
	constexpr int32 kTitle        = 109600;   // «Изменение параметров предмета»  (50301)
	constexpr int32 kTabItem      = 109601;   // «Изменяемый предмет»             (50303)
	constexpr int32 kBtnOk        = 109602;   // «Изменить»                       (50390)
	constexpr int32 kBtnCancel    = 109603;   // «Отмена»                         (50391)
	constexpr int32 kListHeader   = 109607;   // «Изменяемые параметры»           (50342 / 50362)
	constexpr int32 kSuccess      = 109609;   // «Параметры предмета изменились.»
	constexpr int32 kTabOpt0      = 109920;   // «Текущий параметр 1»             (50340)
	// 50360 берёт 109920 + 1 = 109921 «Текущий параметр 2» — эталон складывает индекс цикла.
	constexpr int32 kWarn1        = 109923;   // предупреждение о максимуме       (50381)
	constexpr int32 kWarn2        = 109924;   // «нужен свиток…»                  (50382)
	constexpr int32 kScrollCntFmt = 109922;   // «Свитков обмена параметрами: %d шт.» (50380)
	constexpr int32 kOptLabel     = 12612;    // «Настройки» — префикс строки нотиса успеха

	// Отказы. `case 0` приёмника и первая ветка отправителя используют ОДИН И ТОТ ЖЕ 109700.
	constexpr int32 kErrParam     = 109700;   // «Ошибка параметра»
	constexpr int32 kErrScrollDat = 109702;   // «Ошибка данных свитка.»
	constexpr int32 kErrGearDat   = 109703;   // «Ошибка данных снаряжения.»
	constexpr int32 kErrDb        = 109704;   // «Ошибка БД»
	constexpr int32 kErrProc      = 109705;   // «Ошибка вызова процедуры»
	constexpr int32 kNoScroll     = 109710;   // «Свитка нет.»
	constexpr int32 kNoItem       = 109711;   // «Нет выбранного предмета снаряжения.»
	// ★★★ ДЕФЕКТ ОРИГИНАЛА: строки 10971 в таблице текстов НЕТ (весь диапазон 10900…10999 пуст).
	constexpr int32 kScrollEmptyBroken = 10971;
}

// ★★★ ЦВЕТА, снятые с бинаря (не подобраны). Дизасм `CItemTooltip_BuildOptionLines@0xB62C70`:
//   B62E65 mov [ebp-0x218], 0xff01df0b — ТЕКУЩЕЕ значение параметра (50341/50361);
//   B6311E mov [ebp-0x448], 0xff90e1a8 — строка списка возможных значений.
//   `Net_OnItemUpgradeResult@0xB61990`: −16732433 = 0xFF00AEEF (старое значение в нотисе успеха),
//   −891614 = 0xFFF26522 (новое значение).
static constexpr uint32 kColCurrent = 0xFF01DF0B;
static constexpr uint32 kColRange   = 0xFF90E1A8;
static constexpr uint32 kColOld     = 0xFF00AEEF;
static constexpr uint32 kColNew     = 0xFFF26522;
// `CControl_SetField923(-3584)` = 0xFFFFF200 — цвет надписи «+N%» в ячейке.
static constexpr uint32 kColEnchantLegacy = 0xFFFFF200;

// ★ ДЕФЕКТ ОРИГИНАЛА (закон `fix-original-defects`): нотиса 10971 в таблице нет — оригинал
//   показывает ПУСТОЕ модальное окно. Подставляем 109710 «Свитка нет.» (это ровно тот же случай:
//   стопка свитков пуста). Откат — 0.
static ASDA_DEV_CVAR(int32, CVarFixLoc10971,
	TEXT("asda.ItemOption.FixLoc10971"),
	1,
	TEXT("Окно 50300: 1 = вместо отсутствующего нотиса 10971 показывать 109710 «Свитка нет.» "
	     "(починка дефекта оригинала); 0 = как в оригинале — пустое модальное окно."),
	ECVF_Default);

// ★ Цвет «+N%» в ячейке. Эталон — фиксированный 0xFFFFF200; инвентарь порта по просьбе владельца
//   красит по уровню заточки той же таблицей, что заголовок тултипа. Держим ту же настройку,
//   чтобы обе сетки выглядели одинаково.
static ASDA_DEV_CVAR(int32, CVarEnchantGradeColor,
	TEXT("asda.ItemOption.EnchantTextGradeColor"),
	1,
	TEXT("Окно 50300, цвет «+N%» в ячейке: 1 = по уровню заточки (как в инвентаре порта); "
	     "0 = как эталон — фиксированный 0xFFFFF200 (SetField923(-3584))."),
	ECVF_Default);

using AsdaBytes::RdI16;
using AsdaBytes::RdI32;

// Один индекс списка возможных значений → его формат. Линейный поиск по `itemoptiontable`
//   (эталон `CItemManager_FindRecordById@0xB62200` — тоже линейный, ключевого индекса у таблицы нет).
static FString FindOptionFmt(const UAsdaItemTableSubsystem* IT, int32 OptionId)
{
	if (!IT || OptionId <= 0) { return FString(); }
	const UDataTable* Dt = IT->GetDataTable(TEXT("itemoptiontable"));
	if (!Dt) { return FString(); }
	for (const TPair<FName, uint8*>& P : Dt->GetRowMap())
	{
		const FAsdaItemoptiontableRow* R = reinterpret_cast<const FAsdaItemoptiontableRow*>(P.Value);
		// ★ [ПОРТ] 09.10.2026 (мультиязычность): оверлей `itemoptiontable`, ключ `<_row>.FmtString` (имя строки — ключ карты).
		if ((int32)R->OptionId == OptionId) { return FAsdaLang::ResolveRow(Dt, P.Key, TEXT("FmtString"), R->FmtString); }
	}
	return FString();
}

// «<FC=AARRGGBB>текст</F>» — ровно то, что печатает эталон через sprintf.
static FString Colored(uint32 Argb, const FString& S)
{
	return FString::Printf(TEXT("<FC=%08x>%s</F>"), Argb, *S);
}

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaItemOptionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★★★ РОСПИСЬ «КАК ЗАКРЫТЬ СЕБЯ ПО ESC» — порт события 21 (`CControl_OnEvent21@0x12DCC30`).
	//   Окно `ItemOptionValue_WIN` (IDNum 50300) стоит в эталонной таблице `kEscCloseWindows`: признак
	//   `CControl+0x228` ему ставит `itemUpgradeWindowInitControls@0xB60BC0`. Проход
	//   `UAsdaWindowSubsystem::CloseTopmostOnEsc` закрывает верхнее окно ИМЕННО ЭТИМ вызовом,
	//   а не снятием виджета: снятие оставило бы подсистеме состояние «открыто», и окно
	//   больше не открылось бы. Закрыватель идемпотентен — повторный вызов ничего не делает.
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	if (UAsdaWindowSubsystem* EscWM = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		EscWM->RegisterEscCloser(kOptWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaItemOptionSubsystem::CloseWindow));
	}
	CellUi.SetNum(kCells);     // видимые ячейки сетки (30)
	Cells.SetNum(kListMax);    // ★ 29.10.2026 (инвентарь ×2): уплотнённый список — вся сумка (до 60)
	// ★★★ [ПОРТ] 29.10.2026 (инвентарь ×2): КОЛЕСО НАД СЕТКОЙ — прокрутка по рядам (`BagScroll`, шапка `AsdaBagListScroll.h`).
	//   Сетка эталона 30 без страниц: при 60 ячейках сумки подходящие вещи сверх 30-й были бы недостижимы.
	if (UAsdaWindowSubsystem* WheelWM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WheelWM->RegisterWheelHandler(kOptWindowId, FAsdaUiWheelHandler::CreateWeakLambda(this, [this](int32 Notches) -> bool
		{
			if (!bOpen || !bControlsReady) { return false; }
			bool bOver = false;
			for (const FAsdaItemOptionCellUi& U : CellUi) { if (U.Icon && AsdaUi::IsCursorOverWidget(U.Icon)) { bOver = true; break; } }
			if (!bOver) { return false; }
			if (BagScroll.Wheel(Notches))
			{
				TipCell = -1;            // плашка/рамка прежней позиции — к другой вещи
				HideSelectionFrame();
				RebuildCells();          // выбор держится ЗА ПРЕДМЕТ (позиция списка), прокрутка его не снимает
			}
			return true;
		}));
	}
	PanelUi.SetNum(kPanels);
	Bars.SetNum(kPanels);
	BarHosts.SetNum(kPanels);
	ScrollRow.Init(0, kPanels);

	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь.
		HTransResult = Net->OnItemOptionTransResult.AddLambda(
			[this](const TArray<uint8>& B) { PendingResults.Push(B); });
	}
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		// Состав сумки мог поменяться извне (надел вещь, выбросил, пришёл 5000) — эталон в этих
		//   случаях перерисовывает окно из обработчиков инвентаря. Из колбэка только МЕТИМ.
		HInvCell = Inv->OnCellChanged.AddLambda([this](int32, int32) { bDirty = true; });
	}
	UE_LOG(LogAsdaItemOpt, Log, TEXT("[смена параметров] подсистема поднята"));
}

void UAsdaItemOptionSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnItemOptionTransResult.Remove(HTransResult);
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
void UAsdaItemOptionSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	CloseWindow();
	bPending = false;                 // ждали 6554
	bConfirm = false;
	bDirty = false;
	ScrollRow.Reset();
	PendingResults.Clear();
	Mouse.Reset();
	BagScroll.Reset();                // ★ 29.10.2026 (инвентарь ×2)
	(void)Reason;
}

void UAsdaItemOptionSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.ItemOption");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	DrainPendingPackets();
	if (!bOpen || !bControlsReady) { return; }
	if (bDirty) { bDirty = false; RefreshAll(); }
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		if (TipCell != -1)   // уже висящую плашку и рамку выбора ГАСИМ, а не просто «не показываем»
		{
			TipCell = -1;
			HideSelectionFrame();
			if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
			{
				Tip->HideTooltipFor(EAsdaTipOwner::ItemOption);
			}
		}
		// Полоса прокрутки держит СВОЙ латч захвата бегунка (`FAsdaScrollBarNomal::bThumbGrab`),
		//   и кадром «кнопка отпущена» он снимается вместе с картинкой нажатой стрелки — иначе
		//   схваченный бегунок поехал бы дальше, как только нотис закроется.
		for (int32 j = 0; j < kPanels; ++j)
		{
			int32 Dummy = 0;
			if (Bars.IsValidIndex(j) && Bars[j].IsBuilt()) { Bars[j].Tick(false, false, MaxScrollRow(j), Dummy); }
		}
		Mouse.Reset();
		return;
	}
	TickCells();
	LayoutScrollBars();
	TickScrollBars();
}

// ═════════════════════════════ ОКНО ═══════════════════════════════════════════════════════════

bool UAsdaItemOptionSubsystem::EnsureWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kOptWindowId, kOptWbp, kWinZ).Ensure(GetGameInstance(), Widget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem&)
		{
			SetupControls();
			RegisterZone();
		},
		[]()
		{
			UE_LOG(LogAsdaItemOpt, Error, TEXT("[смена параметров] WBP_ItemOptionValue_WIN не загрузился (%s)"), kOptWbp);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaItemOptionSubsystem::ResetWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kOptWindowId);
		WM->CloseWindow(kOptWindowId);
	}
	if (AniConfirm) { AniConfirm->OnAnimFinished.RemoveAll(this); }
	Widget = nullptr;
	Grid = nullptr;
	GridTarget = nullptr;
	Relays.Reset();
	for (FAsdaItemOptionCellUi& U : CellUi)  { U = FAsdaItemOptionCellUi(); }
	for (FAsdaItemOptionPanelUi& P : PanelUi) { P = FAsdaItemOptionPanelUi(); }
	for (FAsdaScrollBarNomal& B : Bars) { B = FAsdaScrollBarNomal(); }
	for (TObjectPtr<UCanvasPanel>& H : BarHosts) { H = nullptr; }
	TxtTitle = TxtItemName = TxtTabItem = TxtScrollCnt = TxtWarn1 = TxtWarn2 = nullptr;
	IconTargetLbl = TxtBtnOk = TxtBtnCancel = nullptr;
	IconTarget = nullptr;
	BtnOk = BtnCancel = BtnClose = nullptr;
	AniConfirm = nullptr; SelFrame = nullptr;
	bControlsReady = false;
	TipCell = -1;
	Mouse.Reset();
}

// Разбор ВСЕХ контролов окна (закон `full-structural-analysis-always`).
//   Имена импортёра: интерактив и Text — ТОЧНОЕ имя `idc`, планы — `{idc}_{juiIdx}_{plane}`
//   (закон `jui-importer-widget-naming`), поэтому планы ищем через `AsdaUi::ParsePlaneName`.
void UAsdaItemOptionSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }

	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);

	TArray<FName> CellIconNames; CellIconNames.SetNum(kCells);
	FName TargetIconName;   // имя плана 50302 — для «сетки из одной ячейки»

	// Имена контролов панелей j = 0 (50340…50351) и j = 1 (50360…50371). В `.JUI` они называются
	//   `ItemOptionValue_2_*` и `ItemOptionValue_3_*` — цифра панели, а НЕ номер гнезда.
	const TCHAR* PanelTag[kPanels] = { TEXT("2"), TEXT("3") };

	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();

		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("ItemOptionValue_OK_B")) { BtnOk     = Btn; continue; }
			if (N == TEXT("ItemOptionValue_NO_B")) { BtnCancel = Btn; continue; }
			// 50392 — крестик заголовка. У контрола в `.JUI` ПУСТОЙ `ID`, поэтому имя даёт общее
			//   правило `asda_jui_parse.anchored_names`: `{ЯКОРЬ}_BTN{NN}`, якорь — `ID` ближайшего
			//   предка (`ItemOptionValue_Top_Text`). Сам узел живёт В АССЕТЕ (шаг
			//   `Tools/asda_ui_missing_interactive.py`), здесь только привязка обработчика.
			if (N == TEXT("ItemOptionValue_Top_Text_BTN00")) { BtnClose = Btn; continue; }
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == TEXT("ItemOptionValue_Top_Text"))     { TxtTitle      = Tb; continue; }   // 50301
			if (N == TEXT("ItemOptionValue_2_T1"))         { TxtItemName   = Tb; continue; }   // 50336 (имя предмета!)
			if (N == TEXT("ItemOptionValue_1_b1_Text"))    { TxtTabItem    = Tb; continue; }   // 50303
			if (N == TEXT("ItemOptionValue_Op_Text"))      { TxtScrollCnt  = Tb; continue; }   // 50380
			if (N == TEXT("ItemOptionValue_Waning_Text"))  { TxtWarn1      = Tb; continue; }   // 50381
			if (N == TEXT("ItemOptionValue_Waning_Text2")) { TxtWarn2      = Tb; continue; }   // 50382
			if (N == TEXT("ItemOptionValue_OK_B_Text"))    { TxtBtnOk      = Tb; continue; }
			if (N == TEXT("ItemOptionValue_NO_B_Text"))    { TxtBtnCancel  = Tb; continue; }
			if (N == TEXT("ItemOptionValue_Mark_1"))       { IconTargetLbl = Tb; continue; }   // «+N%» слота 50302

			bool bTaken = false;
			for (int32 j = 0; j < kPanels && !bTaken; ++j)
			{
				if (N == FString::Printf(TEXT("ItemOptionValue_%s_T1_Text"), PanelTag[j])) { PanelUi[j].Tab     = Tb; bTaken = true; break; }
				if (N == FString::Printf(TEXT("ItemOptionValue_%s_T2"),      PanelTag[j])) { UAsdaRichText::CaptureFmt(PanelUi[j].Current, Tb); bTaken = true; break; }
				if (N == FString::Printf(TEXT("ItemOptionValue_%s_T3"),      PanelTag[j])) { PanelUi[j].Header  = Tb; bTaken = true; break; }
				for (int32 r = 0; r < kListRows && !bTaken; ++r)
				{
					// Строки списка — 50343/50345/50347/50349, то есть b1…b4 (шаг idnum 2).
					if (N == FString::Printf(TEXT("ItemOptionValue_%s_b%d_Text"), PanelTag[j], r + 1))
					{
						// Стиль JUI снимаем ДО первой подмены на rich (общая точка `UAsdaRichText::CaptureFmt`).
						UAsdaRichText::CaptureFmt(*PanelUi[j].Row(r), Tb);
						bTaken = true;
					}
				}
			}
			if (!bTaken)
			{
				// Подписи ячеек носят ТОЧНОЕ имя idc (`ItemOptionValue_1_Box_Mark_01`…`_30`).
				for (int32 c = 0; c < kCells; ++c)
				{
					if (N == FString::Printf(TEXT("ItemOptionValue_1_Box_Mark_%02d"), c + 1))
					{
						CellUi[c].Label = Tb;
						CellUi[c].BaseColor = Tb->GetColorAndOpacity();
						Tb->SetText(FText::GetEmpty());
						bTaken = true; break;
					}
				}
			}
			continue;
		}

		// Планы и хит-зоны.
		FString Idc; int32 JuiIdx = 0, PlaneIdx = 0;
		if (AsdaUi::ParsePlaneName(N, Idc, JuiIdx, PlaneIdx))
		{
			if (Idc == TEXT("ItemOptionValue_1_Box_Mark_Click"))
			{
				SelFrame = Cast<UAsdaAnimatedPlane>(W);   // 50334, скин `Slot_AniSelect`
				continue;
			}
			if (Idc == TEXT("ItemOptionValue_Mark_ANI"))
			{
				if (UAsdaAnimatedPlane* Ap = Cast<UAsdaAnimatedPlane>(W)) { AniConfirm = Ap; }
				continue;
			}
			// 50392 `CloseBtn_WinSm` — план-картинку крестика больше не запоминаем: с 2026-08-27 её
			//   гасит шаг `asda_ui_missing_interactive`, а рисует и ловит клик узел
			//   `ItemOptionValue_Top_Text_BTN00` (ветка `UButton` выше).
			if (UImage* Img = Cast<UImage>(W))
			{
				if (Idc == TEXT("ItemOptionValue_Mark_1")) { IconTarget = Img; TargetIconName = FName(*N); continue; }   // 50302
				bool bTaken = false;
				for (int32 c = 0; c < kCells && !bTaken; ++c)
				{
					if (Idc == FString::Printf(TEXT("ItemOptionValue_1_Box_Mark_%02d"), c + 1))
					{
						CellUi[c].Icon = Img; CellIconNames[c] = FName(*N); bTaken = true;
					}
				}
				if (bTaken) { continue; }
			}
			// Планы строк списка — из них берём геометрию полосы прокрутки (см. шапку .h, п. 3).
			for (int32 j = 0; j < kPanels; ++j)
			{
				if (Idc == FString::Printf(TEXT("ItemOptionValue_%s_b1"), PanelTag[j]) && !PanelUi[j].RowPlane0) { PanelUi[j].RowPlane0 = W; }
				if (Idc == FString::Printf(TEXT("ItemOptionValue_%s_b4"), PanelTag[j]) && !PanelUi[j].RowPlane3) { PanelUi[j].RowPlane3 = W; }
			}
			continue;
		}
	}

	// ── Сетка иконок через ОБЩУЮ точку (закон `port-shared-points`) ──
	//   Клик ловим САМИ по живой геометрии (`TickCells`), кнопки-оверлеи не строим
	//   (закон `jui-cell-chain-nesting`).
	Grid = NewObject<UAsdaIconCellGrid>(this);
	Grid->SetCellSizePx((float)kCellPx);
	Grid->SetClickOverlayEnabled(false);
	// Эталон в начале КАЖДОГО прохода затирает иконку пустой текстурой 32×32
	//   (`invGridRefreshGeneral`, первый цикл по 30 ячейкам) — иначе на уплотнённой сетке
	//   остаются картинки прошлого состава.
	Grid->bClearEmptyCells = true;
	Grid->SetupNamed(Widget, CellIconNames, /*bMale*/true,
		// ★ 29.10.2026 (инвентарь ×2): видимая ячейка → позиция списка `Cells` через окно прокрутки.
		[this](int32 Cell) -> int32 { const int32 L = BagScroll.ToList(Cell); return Cells.IsValidIndex(L) ? Cells[L].ItemCode : 0; },
		[this](int32 Cell) { OnCellClicked(BagScroll.ToList(Cell)); });

	// ── Слот выбранного предмета 50302: та же общая точка, «сетка из одной ячейки» 64×64
	//   (`Size = "64 64"` у контрола `ItemOptionValue_Mark_1`). Клик по нему эталон не обрабатывает —
	//   у 50302 есть только тултип (msg 8), поэтому оверлей выключен и колбэк пустой.
	if (!TargetIconName.IsNone())
	{
		GridTarget = NewObject<UAsdaIconCellGrid>(this);
		GridTarget->SetCellSizePx(64.f);
		GridTarget->SetClickOverlayEnabled(false);
		GridTarget->bClearEmptyCells = true;
		TArray<FName> OneCell; OneCell.Add(TargetIconName);
		GridTarget->SetupNamed(Widget, OneCell, /*bMale*/true,
			[this](int32) -> int32 { const FAsdaItemOptionEntry* S = Selected(); return S ? S->ItemCode : 0; },
			[](int32) {});
	}

	// ── Кнопки ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnOk,     50390, [this](int32) { OnOkClicked(); });
	Bind(BtnCancel, 50391, [this](int32) { OnCancelClicked(); });
	// ★★★ 2026-08-27. БЫЛО: узла у 50392 в ассете не было вовсе, и порт ловил клик покадровым
	//   опросом живой геометрии ПЛАНА `CloseBtn_WinSm_*_0` (поле `CloseZone` + `TickCloseButton`).
	//   ПОЧЕМУ УБРАНО: контрол СТАТИЧЕН — существование, класс (`PushButton`) и геометрия
	//   (13x13 в шапке неподвижного окна) целиком заданы `gamemain_jui.ini`, рантайм на них не
	//   влияет. Опрос вдобавок не давал состояний наведения/нажатия (закон
	//   `ui-button-hover-click-rule`), хотя скин `CloseBtn_WinSm` несёт три кадра.
	//   ГДЕ ЖИВЁТ ТЕПЕРЬ: узел `ItemOptionValue_Top_Text_BTN00` в
	//   `Content/AsdaAssets/UI/Player_UI/Craft/WBP_ItemOptionValue_WIN` (шаг
	//   `Tools/asda_ui_missing_interactive.py`, сторож — в `asda_ui_post_import.HANDMADE`).
	Bind(BtnClose,  50392, [this](int32) { OnCancelClicked(); });

	// ── Анимация подтверждения 50393 ──
	//   Эталон играет её ОДИН раз и по событию «прогон завершён» (msg 0x18) шлёт пакет.
	//   Поэтому режим строго one-shot, а не луп (`Ani_SoulSton`: 37 кадров, прогон 3000 мс).
	if (AniConfirm)
	{
		AniConfirm->bLoop = false;
		AniConfirm->MaxLoops = 1;
		AniConfirm->OnAnimFinished.AddDynamic(this, &UAsdaItemOptionSubsystem::OnConfirmAnimFinished);
		AniConfirm->StopAnim();
		AniConfirm->SetVisibility(ESlateVisibility::Collapsed);   // open(): SetVisible(0) + CloseHide
	}
	HideSelectionFrame();

	// ── Статичные подписи (эталон ставит их в `itemUpgradeWindowInitControls`) ──
	auto SetLoc = [this](UTextBlock* Tb, int32 LocId)
	{
		if (!Tb) { return; }
		const FString S = Loc(LocId);
		if (!S.IsEmpty()) { Tb->SetText(FText::FromString(S)); }
	};
	SetLoc(TxtTitle,     ItemOptLoc::kTitle);
	SetLoc(TxtTabItem,   ItemOptLoc::kTabItem);
	SetLoc(TxtBtnOk,     ItemOptLoc::kBtnOk);
	SetLoc(TxtBtnCancel, ItemOptLoc::kBtnCancel);
	SetLoc(TxtWarn1,     ItemOptLoc::kWarn1);
	SetLoc(TxtWarn2,     ItemOptLoc::kWarn2);
	for (int32 j = 0; j < kPanels; ++j)
	{
		SetLoc(PanelUi[j].Tab,    ItemOptLoc::kTabOpt0 + j);   // эталон: `loc(j + 109920)`
		SetLoc(PanelUi[j].Header, ItemOptLoc::kListHeader);
	}
	// Обе предупреждающие строки многострочные (в тексте есть «\n»), поэтому перенос им нужен;
	//   остальным — нет (закон `ui-autowrap-canvas-gotcha`: AutoWrap в Canvas ломает раскладку).
	if (TxtWarn1) { TxtWarn1->SetAutoWrapText(true); }
	if (TxtWarn2) { TxtWarn2->SetAutoWrapText(true); }

	// ── Полосы прокрутки списков (50351 / 50371) ──
	for (int32 j = 0; j < kPanels; ++j)
	{
		UWidget* Ref = PanelUi[j].RowPlane0;
		UCanvasPanel* Host = Ref ? Cast<UCanvasPanel>(Ref->GetParent()) : nullptr;
		if (!Host)
		{
			UE_LOG(LogAsdaItemOpt, Warning,
				TEXT("[смена параметров] панель %d: плана строки b1 нет — полосу %d не строим"),
				j, 50351 + 20 * j);
			continue;
		}
		BarHosts[j] = Host;
		Bars[j].Build(Widget, Host, *FString::Printf(TEXT("ItemOptionValue_%s_ListBox_Scr"), PanelTag[j]), /*ZTrack*/ 20);
	}

	bControlsReady = (CellUi[0].Icon != nullptr && BtnOk != nullptr && BtnCancel != nullptr);
	int32 IconCount = 0;
	for (const FAsdaItemOptionCellUi& U : CellUi) { if (U.Icon) { ++IconCount; } }
	UE_LOG(LogAsdaItemOpt, Log,
		TEXT("[смена параметров] окно 50300 разобрано: ячеек с иконой %d/%d, 50302 %s, 50336 %s, ")
		TEXT("50390/50391 %s/%s, 50392 %s, 50393 %s, 50334 %s, панели %s/%s, полосы %s/%s"),
		IconCount, (int32)kCells,
		IconTarget ? TEXT("да") : TEXT("НЕТ"), TxtItemName ? TEXT("да") : TEXT("НЕТ"),
		BtnOk ? TEXT("да") : TEXT("НЕТ"), BtnCancel ? TEXT("да") : TEXT("НЕТ"),
		BtnClose ? TEXT("да") : TEXT("НЕТ"), AniConfirm ? TEXT("да") : TEXT("НЕТ"),
		SelFrame ? TEXT("да") : TEXT("НЕТ"),
		PanelUi[0].Current.Node ? TEXT("да") : TEXT("НЕТ"), PanelUi[1].Current.Node ? TEXT("да") : TEXT("НЕТ"),
		Bars[0].IsBuilt() ? TEXT("да") : TEXT("НЕТ"), Bars[1].IsBuilt() ? TEXT("да") : TEXT("НЕТ"));
}

void UAsdaItemOptionSubsystem::RegisterZone()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Корпус окна — девять планов рамки (закон «rect только по узлам, которые окно и образуют»).
	// ★ Волна 3: зона по планам рамки — общая точка `FAsdaJuiWindowHandle::RegisterFrameZone` (`!bOpen || !Widget` → нет зоны).
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kOptWindowId, this, Widget, bOpen, TEXT("ItemOptionValue_WIN_0_%d"), 9);
}

// ═════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ════════════════════════════════════════════

// `itemUpgradeWindow_open@0xB635C0` — целиком:
//   1) если контролы не разобраны — разобрать (`filler[482]`);
//   2) `sumInventoryItemsType67` → указатель на свиток (`filler[372]`); при отсутствии свитка
//      эталон обнуляет `filler[385]` и подставляет в `filler[372]` адрес СОБСТВЕННОГО поля
//      `filler[376]` — то есть «пустую запись», а не nullptr (у нас это `ScrollRef.Clear()`);
//   3) сбрасывает флаги `filler[480]`/`filler[481]` и выбор `filler[368]`;
//   4) `invWeightTextRefresh` — пересчитать всё;
//   5) показать корень 50300, ЗАКРЫТЬ ИНВЕНТАРЬ (`sowelWindowClose(g_pInventoryWnd, 1)`),
//      спрятать анимацию 50393 и поставить окно в фокус (`CClass_MapInsert(g_pWindowMgr, …)`).
void UAsdaItemOptionSubsystem::OpenWindow()
{
	if (!EnsureWidget()) { return; }

	bOpen = true;
	bPending = false;
	bConfirm = false;
	SelCell = -1;
	TipCell = -1;
	ScrollRow.Init(0, kPanels);
	BagScroll.Reset();   // ★ 29.10.2026 (инвентарь ×2): с первого ряда списка

	RefreshAll();

	if (AniConfirm)
	{
		AniConfirm->StopAnim();
		AniConfirm->SetVisibility(ESlateVisibility::Collapsed);
	}
	HideSelectionFrame();

	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kOptWindowId, true);
	}
	// ★ 1:1: открытие окна ЗАКРЫВАЕТ инвентарь (`sowelWindowClose@0xCFF760` — это метод закрытия
	//   окна инвентаря, имя-ярлык врёт; аргумент 1 = «с полным сбросом»).
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		if (Inv->IsWindowOpen()) { Inv->CloseWindow(); }
	}
	int32 Filled = 0;
	for (const FAsdaItemOptionEntry& E : Cells) { if (!E.IsEmpty()) { ++Filled; } }
	UE_LOG(LogAsdaItemOpt, Log, TEXT("[смена параметров] окно открыто: предметов %d, свитков %d"),
		Filled, ScrollTotal);
}

// `sub_93548F` → `sub_B636D0@0xB636D0`: `CControl_SetVisible(корень, 0)`. Больше НИЧЕГО —
//   ни выбор, ни флаги эталон здесь не трогает (их сбрасывает открытие).
void UAsdaItemOptionSubsystem::CloseWindow()
{
	if (!bOpen) { return; }
	bOpen = false;
	TipCell = -1;
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::ItemOption);
	}
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kOptWindowId, false);
	}
	UE_LOG(LogAsdaItemOpt, Log, TEXT("[смена параметров] окно закрыто"));
}

// ═════════════════════════════ МОДЕЛЬ ═════════════════════════════════════════════════════════

const FAsdaItemOptionEntry* UAsdaItemOptionSubsystem::Selected() const
{
	return Cells.IsValidIndex(SelCell) && !Cells[SelCell].IsEmpty() ? &Cells[SelCell] : nullptr;
}

// `invGridRefreshGeneral@0xB62570` — сетка из 30 ячеек, контейнер 1 (`g_InventoryBag` 0x2901950).
//   Гейт (дизасм B62640…B62690, все три условия подряд):
//     itemSubType != 0x42 && != 0xC8 && != 0xC9
//     && (i16 запись+0x1D >= 0 || i16 запись+0x21 >= 0)     ← ИМЕННО >= 0, не > 0
//     && (itemCode < 0xDAF3 || itemCode > 56080)
//   Заполнение ПОДРЯД (`visibleSlotIdx`), максимум 30 ячеек.
void UAsdaItemOptionSubsystem::RebuildCells()
{
	const FAsdaItemOptionEntry Prev = Selected() ? *Selected() : FAsdaItemOptionEntry();
	for (FAsdaItemOptionEntry& E : Cells) { E.Clear(); }

	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	int32 Visible = 0;   // ★ 29.10.2026 (инвентарь ×2): здесь — длина уплотнённого списка (вся сумка), подписи — вторым проходом
	if (Inv && IT)
	{
		const int32 Total = FMath::Min(Inv->GetContainerSize(1), kListMax);
		for (int32 C = 0; C < Total && Visible < kListMax; ++C)
		{
			const FAsdaItemInstance* It = Inv->GetCell(1, C);
			if (!It || It->IsEmpty() || It->Raw.Num() < FAsdaItemInstance::kRecordSize) { continue; }
			const FAsdaItemRecord* R = IT->GetItem(It->ItemCode);
			if (!R) { continue; }
			if (R->ItemSubType == 66 || R->ItemSubType == 200 || R->ItemSubType == 201) { continue; }
			const int32 Opt0 = AsdaBytes::RdI16(It->Raw.GetData() + 29);
			const int32 Opt1 = AsdaBytes::RdI16(It->Raw.GetData() + 33);
			if (Opt0 < 0 && Opt1 < 0) { continue; }
			if (R->ItemCode >= kExcludeCodeLo && R->ItemCode <= kExcludeCodeHi) { continue; }

			FAsdaItemOptionEntry& E = Cells[Visible];
			E.ItemCode  = It->ItemCode;
			E.Container = (It->ContainerType > 0) ? (int32)It->ContainerType : 1;
			E.Slot      = (It->SlotIndex >= 0) ? It->SlotIndex : C;
			E.CellIndex = C;
			++Visible;
		}
	}
	BagScroll.SetTotal(Visible);   // ряд клампится, если список укоротился
	// «+N%» под иконкой — только видимым ячейкам: гейт `запись+24`, значение `i16 запись+27`.
	for (int32 V = 0; V < kCells; ++V)
	{
		if (!CellUi.IsValidIndex(V) || !CellUi[V].Label) { continue; }
		const int32 L = BagScroll.ToList(V);
		const FAsdaItemInstance* It = (Inv && L < Visible) ? Inv->GetCell(1, Cells[L].CellIndex) : nullptr;
		if (It && It->GemGrade != 0)
		{
			CellUi[V].Label->SetText(FText::FromString(FString::Printf(TEXT("+%d%%"), It->EnchantPercent)));
			const uint32 Argb = (CVarEnchantGradeColor.GetValueOnGameThread() != 0)
				? UAsdaItemTooltipSubsystem::EnhanceColorArgb((int32)It->GemGrade)
				: kColEnchantLegacy;
			// Закон `ui-srgb-gamma-rule`: цвет из данных — в sRGB, UMG ждёт linear.
			CellUi[V].Label->SetColorAndOpacity(
				FSlateColor(AsdaUi::SrgbHexToLinear(FString::Printf(TEXT("%08X"), Argb))));
		}
		else
		{
			CellUi[V].Label->SetText(FText::GetEmpty());
			CellUi[V].Label->SetColorAndOpacity(CellUi[V].BaseColor);
		}
	}

	// Выбранный предмет мог уехать в другую ячейку (уплотнение) — держимся ЗА ПРЕДМЕТ, а не за
	//   номер ячейки. Эталону это не нужно: он хранит УКАЗАТЕЛЬ на запись, а она живёт по своему
	//   слоту контейнера и от перерисовки сетки не двигается.
	if (!Prev.IsEmpty())
	{
		SelCell = -1;
		for (int32 i = 0; i < kListMax; ++i)
		{
			if (Cells[i].Container == Prev.Container && Cells[i].CellIndex == Prev.CellIndex && !Cells[i].IsEmpty())
			{
				SelCell = i; break;
			}
		}
	}
	if (Grid) { Grid->Refresh(); }
}

// `sumInventoryItemsType67@0xB63500`: те же 30 ячеек контейнера 1; предмет берётся из
//   `special_itemlist` (`CItemManager_GetTable2Record`), гейт `SubType == 67` (0x43).
//   Возвращает ПОСЛЕДНЮЮ подошедшую запись и СУММУ количеств (`запись+9`) по всем подошедшим.
//   Текст 50380 — нотис 109922 с этой суммой (`invWeightTextRefresh@0xB624C0`).
void UAsdaItemOptionSubsystem::RefreshScrollCount()
{
	ScrollRef.Clear();
	ScrollTotal = 0;
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (Inv && IT)
	{
		const int32 Total = FMath::Min(Inv->GetContainerSize(1), kListMax);   // ★ 29.10.2026 (инвентарь ×2): вся сумка
		for (int32 C = 0; C < Total; ++C)
		{
			const FAsdaItemInstance* It = Inv->GetCell(1, C);
			if (!It || It->IsEmpty()) { continue; }
			FAsdaSpecialItemlistRow Sp;
			if (!IT->GetSpecialItem(It->ItemCode, Sp)) { continue; }
			if (Sp.SubType != kScrollSubType) { continue; }
			ScrollRef.ItemCode  = It->ItemCode;
			ScrollRef.Container = (It->ContainerType > 0) ? (int32)It->ContainerType : 1;
			ScrollRef.Slot      = (It->SlotIndex >= 0) ? It->SlotIndex : C;
			ScrollRef.CellIndex = C;
			ScrollTotal += It->Amount;
		}
	}
	if (TxtScrollCnt)
	{
		const FString Fmt = Loc(ItemOptLoc::kScrollCntFmt);
		TxtScrollCnt->SetText(FText::FromString(
			Fmt.IsEmpty() ? FString::FromInt(ScrollTotal)
			              : AsdaUi::FormatPrintfArgs(Fmt, { FString::FromInt(ScrollTotal) })));
	}
}

// `ItemSlotTooltip_Refresh@0xB629A0`: иконка 50302 + ИМЯ предмета в 50336; при отсутствии выбора
//   и то и другое очищается.
void UAsdaItemOptionSubsystem::RefreshSelectedItem()
{
	const FAsdaItemOptionEntry* Sel = Selected();
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;

	if (!Sel || !IT)
	{
		// Эталон при пустом выборе рисует в 50302 ПУСТУЮ текстуру 32×32 и стирает обе подписи —
		//   контрол при этом остаётся видимым (`ItemSlotTooltip_Refresh`, ветка else).
		if (TxtItemName)   { TxtItemName->SetText(FText::GetEmpty()); }
		if (IconTargetLbl) { IconTargetLbl->SetText(FText::GetEmpty()); }
		if (GridTarget)    { GridTarget->Refresh(); }
		return;
	}
	if (TxtItemName)
	{
		FString Name = IT->GetItemName(Sel->ItemCode);
		if (Name.IsEmpty()) { Name = FString::FromInt(Sel->ItemCode); }
		TxtItemName->SetText(FText::FromString(Name));
	}
	if (IconTarget) { IconTarget->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	if (GridTarget) { GridTarget->Refresh(); }   // иконку рисует ОБЩАЯ точка (закон `port-shared-points`)
	if (IconTargetLbl)
	{
		const FAsdaItemInstance* It = Inv ? Inv->GetCell(Sel->Container, Sel->CellIndex) : nullptr;
		if (It && It->GemGrade != 0)
		{
			IconTargetLbl->SetText(FText::FromString(FString::Printf(TEXT("+%d%%"), It->EnchantPercent)));
			const uint32 Argb = (CVarEnchantGradeColor.GetValueOnGameThread() != 0)
				? UAsdaItemTooltipSubsystem::EnhanceColorArgb((int32)It->GemGrade)
				: kColEnchantLegacy;
			IconTargetLbl->SetColorAndOpacity(
				FSlateColor(AsdaUi::SrgbHexToLinear(FString::Printf(TEXT("%08X"), Argb))));
		}
		else
		{
			IconTargetLbl->SetText(FText::GetEmpty());
		}
	}
}

// Текущее значение параметра: `sprintf(fmt, id < 300 ? value : value/100)`.
FString UAsdaItemOptionSubsystem::FormatOptionCurrent(int32 OptionId, int32 RawValue) const
{
	const UAsdaItemTooltipSubsystem* Tip = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	// Общая точка: та же формула живёт в тултипе предмета (закон `port-shared-points`).
	return Tip ? Tip->FormatOptionValue(OptionId, RawValue) : FString();
}

// Строка списка возможных значений (`CItemTooltip_BuildOptionLines`, внутренний цикл):
//   значения делятся на 100 при `OptionType >= 300`; если формат кончается на «%» — хвост
//   печатается как «~+N%%», иначе «~+N».
//   ★ ДЕФЕКТ ОРИГИНАЛА, воспроизводим 1:1: при `MaxValue == 0` эталон перепечатывает строку
//     формата СЫРЫМ `MinValue` — БЕЗ деления на 100 (`sprintf(buf, fmt, optValue1)`), из-за чего
//     процентные параметры с нулевым верхом показываются в сотых долях.
FString UAsdaItemOptionSubsystem::FormatOptionRange(const FAsdaItemoptionrangeRow& R) const
{
	const UAsdaItemTableSubsystem* IT = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FString Fmt = FindOptionFmt(IT, R.OptionType);
	if (Fmt.IsEmpty()) { return FString(); }

	if (R.MaxValue == 0)
	{
		return AsdaUi::FormatPrintfArgs(Fmt, { FString::FromInt(R.MinValue) });
	}
	const bool bPercent = (R.OptionType >= kPercentOptionIdFirst);
	const int32 Lo = bPercent ? (int32)((double)R.MinValue / 100.0) : R.MinValue;
	const int32 Hi = bPercent ? (int32)((double)R.MaxValue / 100.0) : R.MaxValue;
	const FString Head = AsdaUi::FormatPrintfArgs(Fmt, { FString::FromInt(Lo) });
	// Эталон смотрит ПОСЛЕДНИЙ символ строки формата: «%» ⇒ хвост тоже с процентом.
	const bool bFmtEndsPercent = Fmt.EndsWith(TEXT("%"));
	return bFmtEndsPercent
		? FString::Printf(TEXT("%s~+%d%%"), *Head, Hi)
		: FString::Printf(TEXT("%s~+%d"),  *Head, Hi);
}

// `CItemTooltip_BuildOptionLines@0xB62C70` — обе панели целиком.
void UAsdaItemOptionSubsystem::RefreshOptionPanels()
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;

	// Первый цикл эталона: сброс ОБЕИХ панелей независимо от того, выбран ли предмет.
	for (int32 j = 0; j < kPanels; ++j)
	{
		UAsdaRichText::SetFmt(PanelUi[j].Current, TEXT("NONE"));
		if (PanelUi[j].Header)  { const FString S = Loc(ItemOptLoc::kListHeader); if (!S.IsEmpty()) { PanelUi[j].Header->SetText(FText::FromString(S)); } }
		for (int32 r = 0; r < kListRows; ++r)
		{
			if (FAsdaFmtText* Rw = PanelUi[j].Row(r)) { UAsdaRichText::SetFmt(*Rw, FString()); }
		}
	}
	Rows0.Reset();
	Rows1.Reset();

	const FAsdaItemOptionEntry* Sel = Selected();
	if (!Sel || !IT || !Inv) { return; }
	const FAsdaItemInstance* It = Inv->GetCell(Sel->Container, Sel->CellIndex);
	const FAsdaItemRecord*   R  = IT->GetItem(Sel->ItemCode);
	if (!It || It->IsEmpty() || !R || It->Raw.Num() < FAsdaItemInstance::kRecordSize) { return; }

	for (int32 j = 0; j < kPanels; ++j)
	{
		const int32 OptId  = AsdaBytes::RdI16(It->Raw.GetData() + 29 + 4 * j);
		const int32 OptVal = AsdaBytes::RdI16(It->Raw.GetData() + 31 + 4 * j);
		// ★ Эталон обрабатывает панель ТОЛЬКО при `запись+29+4j > 0` — гнездо без параметра
		//   остаётся с «NONE» и пустым списком.
		if (OptId <= 0) { continue; }

		{
			// Строка `<FC=ff01df0b>…</F>` — цвет разбирает общая точка разметки (как рендер эталона).
			const FString Cur = FormatOptionCurrent(OptId, OptVal);
			UAsdaRichText::SetFmt(PanelUi[j].Current, Cur.IsEmpty() ? FString(TEXT("NONE")) : Colored(kColCurrent, Cur));
		}

		// Группа возможных значений: гнездо 0 — `u16 @0x68` шаблона, гнездо 1 — `u16 @0x74`.
		const int32 Group = (j == 0) ? R->OptionGroup0 : R->OptionGroup1;
		TArray<FAsdaItemoptionrangeRow>& Dst = (j == 0) ? Rows0 : Rows1;
		IT->GetItemOptionRangesByGroup(Group, Dst);

		const int32 Start = FMath::Clamp(ScrollRow.IsValidIndex(j) ? ScrollRow[j] : 0, 0, FMath::Max(0, Dst.Num() - 1));
		for (int32 r = 0; r < kListRows; ++r)
		{
			FAsdaFmtText* Rw = PanelUi[j].Row(r);
			if (!Rw) { continue; }
			const int32 Idx = Start + r;
			if (!Dst.IsValidIndex(Idx)) { UAsdaRichText::SetFmt(*Rw, FString()); continue; }
			const FString S = FormatOptionRange(Dst[Idx]);
			UAsdaRichText::SetFmt(*Rw, S.IsEmpty() ? FString() : Colored(kColRange, S));
		}
	}
}

// `invWeightTextRefresh@0xB624C0`: пересчитать свитки, перерисовать сетку, слот и обе панели.
void UAsdaItemOptionSubsystem::RefreshAll()
{
	RefreshScrollCount();
	RebuildCells();
	RefreshSelectedItem();
	RefreshOptionPanels();
}

// `invWeightOnSlotMsgRefresh@0xB63700`: при НЕнулевой записи сбросить ОБЕ полосы в 0 и запомнить
//   выбор; затем — полный `invWeightTextRefresh`. Нулевая запись (клик по пустой ячейке) выбор
//   НЕ меняет… но `itemUpgradeOnSlotMsg@0xB63770` перед этим ОБНУЛЯЕТ `filler[368]`, поэтому
//   итог клика по пустой ячейке — снятие выбора.
void UAsdaItemOptionSubsystem::SelectEntry(int32 Cell)
{
	SelCell = -1;                       // `*(this + 92) = 0`
	if (Cells.IsValidIndex(Cell) && !Cells[Cell].IsEmpty())
	{
		ScrollRow.Init(0, kPanels);     // `uiCtrl_SetChildScrollbarPos(обе полосы, 0)`
		SelCell = Cell;
	}
	RefreshAll();
}

// ═════════════════════════════ ПОКАДРОВЫЕ ОПРОСЫ ══════════════════════════════════════════════

void UAsdaItemOptionSubsystem::TickCells()
{
	// Клик и наведение ловим ПО ЖИВОЙ ГЕОМЕТРИИ (`AsdaUi::IsCursorOverWidget`), а не по
	//   вычисленным зонам: ячейки в `.JUI` заданы цепочкой вложенности (закон `jui-cell-chain-nesting`).
	// ★ 29.10.2026 (инвентарь ×2): `OverCell` — позиция СПИСКА `Cells` (видимая ячейка + окно прокрутки).
	int32 OverCell = -1; const UWidget* OverIcon = nullptr;
	for (int32 C = 0; C < kCells && OverCell < 0; ++C)
	{
		if (CellUi[C].Icon && AsdaUi::IsCursorOverWidget(CellUi[C].Icon))
		{
			OverCell = FMath::Min(BagScroll.ToList(C), kListMax - 1); OverIcon = CellUi[C].Icon;
		}
	}
	const bool bOverTargetSlot = (OverCell < 0) && IconTarget && AsdaUi::IsCursorOverWidget(IconTarget);

	UGameInstance* GI = GetGameInstance();
	UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;

	// ── Тултип (msg 8 → `equipSlotTooltipByCtrlId@0xB63B70`, msg 0xA → KillFocus) ──
	const int32 WantTip = bOverTargetSlot ? -2 : ((OverCell >= 0 && !Cells[OverCell].IsEmpty()) ? OverCell : -1);
	if (WantTip != TipCell)
	{
		TipCell = WantTip;
		if (TipCell == -1)
		{
			HideSelectionFrame();
			if (Tip) { Tip->HideTooltipFor(EAsdaTipOwner::ItemOption); }
		}
		else
		{
			const FAsdaItemOptionEntry* E = (TipCell == -2) ? Selected() : &Cells[TipCell];
			if (E && !E->IsEmpty() && Tip && Inv)
			{
				if (const FAsdaItemInstance* It = Inv->GetCell(E->Container, E->CellIndex))
				{
					// `slotSource` = 1 («Снаряжение», `g_InventoryBag`) — эталон сюда и смотрит.
					Tip->ShowItemTooltip(*It, /*bSuppressCompare*/ false, EAsdaTipOwner::ItemOption, /*SourceKind*/ 1);
				}
			}
			if (TipCell >= 0) { MoveSelectionFrame(OverIcon); }
		}
	}

	// ── Клик (msg 0xB) ──
	if (Mouse.bLmbDown && OverCell >= 0)
	{
		OnCellClicked(OverCell);
	}
}

int32 UAsdaItemOptionSubsystem::MaxScrollRow(int32 Panel) const
{
	const TArray<FAsdaItemoptionrangeRow>& Src = (Panel == 0) ? Rows0 : Rows1;
	return FMath::Max(0, Src.Num() - kListRows);
}

void UAsdaItemOptionSubsystem::SetScrollRow(int32 Panel, int32 Row)
{
	if (!ScrollRow.IsValidIndex(Panel)) { return; }
	const int32 Clamped = FMath::Clamp(Row, 0, MaxScrollRow(Panel));
	if (Clamped == ScrollRow[Panel]) { return; }
	ScrollRow[Panel] = Clamped;
	RefreshOptionPanels();
}

// Прямоугольник полосы восстановлен по авторским числам `.JUI` (см. шапку .h, п. 3):
//   строка списка 292×19, колёсное окно 300×80 и начинается на 4 px левее строки ⇒
//   полоса (11 px) стоит по правому краю строки + 1, от верха первой строки, высотой 80.
void UAsdaItemOptionSubsystem::LayoutScrollBars()
{
	for (int32 j = 0; j < kPanels; ++j)
	{
		if (!Bars[j].IsBuilt()) { continue; }
		const UWidget* Ref = PanelUi[j].RowPlane0;
		FVector2D Pos, Size;
		if (!Ref || !AsdaUi::SlotRect(Ref, Pos, Size)) { Bars[j].Hide(); continue; }
		const int32 MaxRow = MaxScrollRow(j);
		const float BarX = Pos.X + Size.X + 1.f;
		const float BarY = Pos.Y;
		const float BarH = 80.f;                        // `Size = "300 80"` колёсного окна
		// ★ Волна 15: целые аргументы эталона `uiControlSetScrollRange(список 50351/50371, 4, N)`
		//   (`CItemTooltip_BuildOptionLines@0xB62C70`, item_inventory.c:4328 и :4354): a2 = 4 видимых строки
		//   (`kListRows`), a3 = N — число записей списка гнезда (`sub_93923D` → `sub_B63E30` = поле +8 списка,
		//   его размер; наполняет `objFilterRefreshByType`). Гнездо без параметра эталон ставит (4, 4) —
		//   у нас пустой список, a3 = 0: в обоих случаях диапазон ≤ 0 и бегунок скрыт. `MaxRow` = a3 − a2,
		//   как `+0x264` эталона (`MaxScrollRow`). Длина бегунка — float-арифметикой эталона (`EtalonThumbLen`).
		const TArray<FAsdaItemoptionrangeRow>& Src = (j == 0) ? Rows0 : Rows1;
		Bars[j].Layout(FVector2D(BarX, BarY), BarH, /*Scale*/ 1.f, kListRows, Src.Num(), MaxRow, ScrollRow[j]);
	}
}

void UAsdaItemOptionSubsystem::TickScrollBars()
{
	for (int32 j = 0; j < kPanels; ++j)
	{
		if (!Bars[j].IsBuilt()) { continue; }
		int32 Value = 0;
		switch (Bars[j].Tick(Mouse.bLmb, Mouse.bLmbDown, MaxScrollRow(j), Value))
		{
		case EAsdaScrollAction::ScrollBy: SetScrollRow(j, ScrollRow[j] + Value); break;
		case EAsdaScrollAction::SetRow:   SetScrollRow(j, Value);                break;
		default: break;
		}
	}
}

void UAsdaItemOptionSubsystem::HideSelectionFrame()
{
	if (SelFrame) { SelFrame->SetVisibility(ESlateVisibility::Collapsed); }
}

// Эталон переносит курсор 50334 к ячейке ВЫБРАННОГО предмета (`invGridRefreshGeneral`, ветка
//   `invSlotPtr == v24[92]`): `Class_GetVec2_74(ячейка) → CControl_SetField74_75(курсор) + SetVisible(1)`.
void UAsdaItemOptionSubsystem::MoveSelectionFrame(const UWidget* OverIcon)
{
	if (!SelFrame || !OverIcon) { HideSelectionFrame(); return; }
	// ★ Дедуп №2, В2-B (U19): общая точка рамки выбора (позиция в координатах РОДИТЕЛЯ курсора — ячейки и курсор лежат в
	//   разных группах, закон `jui-flattened-hierarchy-geometry`); перезапуск кадров — как было у этого окна.
	AsdaUi::PlaceFrameOver(SelFrame, OverIcon, /*bRestartAnim*/true);
}

// ═════════════════════════════ ДЕЙСТВИЯ ═══════════════════════════════════════════════════════

// `itemUpgradeOnSlotMsg@0xB63770`: пока играет ролик подтверждения (`filler[481] == 1`) клики
//   по ячейкам ИГНОРИРУЮТСЯ.
void UAsdaItemOptionSubsystem::OnCellClicked(int32 Cell)
{
	if (bConfirm) { return; }
	SelectEntry(Cell);
	UE_LOG(LogAsdaItemOpt, Log, TEXT("[смена параметров] выбрана ячейка %d: код=%d контейнер=%d слот=%d"),
		Cell, Cells.IsValidIndex(Cell) ? Cells[Cell].ItemCode : 0,
		Cells.IsValidIndex(Cell) ? Cells[Cell].Container : 0,
		Cells.IsValidIndex(Cell) ? Cells[Cell].Slot : -1);
}

// `itemUpgradeConfirmReady@0xB637F0` — гейты кнопки 50390 «Изменить», 1:1:
//   • ждём ответ (`filler[480]`) ⇒ не делаем НИЧЕГО;
//   • нет выбранного предмета ⇒ нотис 109711 (окно остаётся открытым);
//   • нет свитка ⇒ ЗАКРЫТЬ окно и нотис 109710;
//   • у свитка ноль в стопке ⇒ пересчитать свитки и обновить 50380; если после пересчёта свитка
//     всё ещё нет — закрыть окно и нотис 109710;
//   • иначе — поднять флаг `filler[481]` и ПОКАЗАТЬ анимацию 50393.
void UAsdaItemOptionSubsystem::OnOkClicked()
{
	if (bPending) { return; }
	if (!Selected())
	{
		NoticeMsgBox(ItemOptLoc::kNoItem);
		return;
	}
	if (ScrollRef.IsEmpty())
	{
		CloseWindow();                       // эталон: `sub_93548F(v9)` ДО показа нотиса
		NoticeMsgBox(ItemOptLoc::kNoScroll);
		return;
	}
	if (ScrollTotal == 0)
	{
		RefreshScrollCount();                // эталон пересчитывает и перепечатывает 50380
		if (ScrollRef.IsEmpty())
		{
			CloseWindow();
			NoticeMsgBox(ItemOptLoc::kNoScroll);
			return;
		}
	}
	bConfirm = true;
	if (AniConfirm)
	{
		AniConfirm->SetVisibility(ESlateVisibility::HitTestInvisible);
		AniConfirm->RestartAnim();           // `CControl_SetVisible(1)` + `CControl_Show`
		UE_LOG(LogAsdaItemOpt, Log, TEXT("[смена параметров] 50390: играем ролик 50393 (3000 мс), затем шлём 6553"));
	}
	else
	{
		// Плана анимации нет (переимпорт мог его не создать) — иначе запрос не ушёл бы НИКОГДА.
		UE_LOG(LogAsdaItemOpt, Warning, TEXT("[смена параметров] плана 50393 нет — шлём 6553 без ролика"));
		OnConfirmAnimFinished();
	}
}

// 50391 «Отмена», 50392 «×» и msg 21 — все три ведут в `sub_93548F` (спрятать окно).
void UAsdaItemOptionSubsystem::OnCancelClicked()
{
	CloseWindow();
}

// msg 0x18 от 50393: `SetVisible(0)` + `CloseHide`, затем `itemSendUpgradeRequest`.
void UAsdaItemOptionSubsystem::OnConfirmAnimFinished()
{
	if (AniConfirm)
	{
		AniConfirm->SetVisibility(ESlateVisibility::Collapsed);
		AniConfirm->StopAnim();
	}
	SendRequest();
}

// `itemSendUpgradeRequest@0xB611E0` — гейты ОТПРАВКИ (они ДРУГИЕ, чем у кнопки 50390):
//   `filler[480]` занят ⇒ молча выйти; нет предмета ⇒ 109700; нет свитка ⇒ 109710;
//   ноль в стопке свитков ⇒ 10971 (строки в таблице НЕТ — см. шапку .h).
void UAsdaItemOptionSubsystem::SendRequest()
{
	if (bPending) { return; }
	const FAsdaItemOptionEntry* Sel = Selected();
	if (!Sel)
	{
		NoticeMsgBox(ItemOptLoc::kErrParam);
		return;
	}
	if (ScrollRef.IsEmpty())
	{
		NoticeMsgBox(ItemOptLoc::kNoScroll);
		return;
	}
	if (ScrollTotal == 0)
	{
		// ДЕФЕКТ ОРИГИНАЛА: 10971 в таблице текстов нет. Откат — `asda.ItemOption.FixLoc10971 0`.
		NoticeMsgBox(CVarFixLoc10971.GetValueOnGameThread() != 0
			? ItemOptLoc::kNoScroll : ItemOptLoc::kScrollEmptyBroken);
		return;
	}
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	if (Net->SendItemOptionTrans6553(ScrollRef.ItemCode, ScrollRef.Container, ScrollRef.Slot,
	                                 Sel->ItemCode,      Sel->Container,      Sel->Slot))
	{
		bPending = true;   // эталонное `self[480] = 1`
	}
}

// ═════════════════════════════ ПРИЁМ 6554 ═════════════════════════════════════════════════════

void UAsdaItemOptionSubsystem::DrainPendingPackets()
{
	// ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива. Прежний цикл `while … [0]; RemoveAt(0)` давал тот же порядок: во время разбора новые тела в очередь
	//   не попадают (колбэк сети приходит из `UAsdaNetSubsystem::Tick`, не отсюда).
	if (TArray<TArray<uint8>> Local; PendingResults.Drain(Local))
	{
		for (const TArray<uint8>& B : Local) { OnTransResult(B); }
	}
}

// `Net_OnItemUpgradeResult@0xB61990`. ПОРЯДОК ВАЖЕН: сперва строится текст нотиса (он читает
//   СТАРЫЕ параметры из ещё не обновлённой записи), и только ПОТОМ записи кладутся в контейнеры.
void UAsdaItemOptionSubsystem::OnTransResult(const TArray<uint8>& Body)
{
	// Эталон снимает ОБА флага в самом начале, до разбора кода.
	bPending = false;
	bConfirm = false;
	if (Body.Num() < 119) { return; }
	const int32 Code = (int32)(int8)Body[0];

	switch (Code)
	{
	case 0:  NoticeMsgBox(ItemOptLoc::kErrParam);     UE_LOG(LogAsdaItemOpt, Warning, TEXT("[смена параметров] 6554 отказ 0")); return;
	case 2:  NoticeMsgBox(ItemOptLoc::kErrScrollDat); UE_LOG(LogAsdaItemOpt, Warning, TEXT("[смена параметров] 6554 отказ 2 (свиток)")); return;
	case 3:  NoticeMsgBox(ItemOptLoc::kErrGearDat);   UE_LOG(LogAsdaItemOpt, Warning, TEXT("[смена параметров] 6554 отказ 3 (снаряжение)")); return;
	case 98: NoticeMsgBox(ItemOptLoc::kErrDb);        UE_LOG(LogAsdaItemOpt, Warning, TEXT("[смена параметров] 6554 отказ 98 (БД)")); return;
	case 99: NoticeMsgBox(ItemOptLoc::kErrProc);      UE_LOG(LogAsdaItemOpt, Warning, TEXT("[смена параметров] 6554 отказ 99 (процедура)")); return;
	case 1:  break;
	default:
		// ★ 1:1: у эталона `default: return;` — текста под другие коды в нём нет. Сервер, однако,
		//   умеет прислать код 4 (`Recv_DBItemOptionTrans(6)`), и оригинал на него МОЛЧИТ.
		UE_LOG(LogAsdaItemOpt, Warning,
			TEXT("[смена параметров] 6554 код %d — текста под него в эталоне нет (оригинал молчит)"), Code);
		return;
	}

	// ── Успех. Нотис строится ДО применения записей (старые значения ещё в модели) ──
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const FAsdaItemOptionEntry* Sel = Selected();
	const FAsdaItemInstance* Was = (Sel && Inv) ? Inv->GetCell(Sel->Container, Sel->CellIndex) : nullptr;

	FString Text = Loc(ItemOptLoc::kSuccess);
	const FString OptLabel = Loc(ItemOptLoc::kOptLabel);
	for (int32 k = 0; k < kPanels; ++k)
	{
		// A — СТАРОЕ значение из записи выбранного предмета (`filler[368] + 29 + 4k` / `+31 + 4k`).
		FString A(TEXT("NONE"));
		if (Was && Was->Raw.Num() >= FAsdaItemInstance::kRecordSize)
		{
			const int32 Id  = AsdaBytes::RdI16(Was->Raw.GetData() + 29 + 4 * k);
			const int32 Val = AsdaBytes::RdI16(Was->Raw.GetData() + 31 + 4 * k);
			const FString S = FormatOptionCurrent(Id, Val);
			if (!S.IsEmpty()) { A = Colored(kColOld, S); }
		}
		// B — НОВОЕ значение из записи снаряжения пакета (смещения 87 + 4k и 89 + 4k = 58 + 29/31).
		FString B(TEXT("NONE"));
		{
			const int32 Id  = RdI16(Body.GetData() + 87 + 4 * k);
			const int32 Val = RdI16(Body.GetData() + 89 + 4 * k);
			const FString S = FormatOptionCurrent(Id, Val);
			if (!S.IsEmpty()) { B = Colored(kColNew, S); }
		}
		Text += FString::Printf(TEXT("\n %s %d:%s -> %s"), *OptLabel, k + 1, *A, *B);
	}
	NoticeMsgBoxText(Text);

	// ── Теперь применяем обе записи (общая точка модели инвентаря) ──
	int32 Applied = 0;
	if (UAsdaInventorySubsystem* InvM = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		Applied = InvM->ApplyItemOptionTransRecords(Body);
	}
	RefreshAll();
	UE_LOG(LogAsdaItemOpt, Log, TEXT("[смена параметров] 6554 успех: применено записей %d, новый вес %d"),
		Applied, RdI32(Body.GetData() + 115));
}

// ═════════════════════════════ НОТИСЫ ═════════════════════════════════════════════════════════

FString UAsdaItemOptionSubsystem::Loc(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

void UAsdaItemOptionSubsystem::NoticeMsgBox(int32 LocId) const
{
	NoticeMsgBoxText(Loc(LocId));
}

// Эталон: `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)` = ONEBUT_MSGBOX 1500.
//   ★ ОТСТУПЛЕНИЕ (см. шапку .h, п. 2): теги `<FC=…>` СРЕЗАЕМ — общий `ONEBUT_MSGBOX` порта
//     рисует простым `UTextBlock`, и разметка была бы видна игроку.
void UAsdaItemOptionSubsystem::NoticeMsgBoxText(const FString& Text) const
{
	if (Text.IsEmpty()) { return; }
	// ★ Волна 1 (14.09.2026, В2): единый маршрут `AsdaNotice::MsgBox` — окно 1500, без оконной подсистемы —
	//   строка в ленту той же категорией. Категория/подпись кнопки — прежние.
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::ItemRefused, FAsdaMarkup::StripTags(Text), 2288);
}

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════

UWidget* UAsdaItemOptionSubsystem::DebugCellIcon(int32 Cell) const
{
	return CellUi.IsValidIndex(Cell) ? CellUi[Cell].Icon : nullptr;
}

int32 UAsdaItemOptionSubsystem::DebugSelectedCode() const
{
	const FAsdaItemOptionEntry* E = Selected();
	return E ? E->ItemCode : 0;
}

int32 UAsdaItemOptionSubsystem::DebugRangeRows(int32 Panel) const
{
	return (Panel == 0) ? Rows0.Num() : Rows1.Num();
}

FString UAsdaItemOptionSubsystem::DebugRangeText(int32 Panel, int32 Row) const
{
	const TArray<FAsdaItemoptionrangeRow>& Src = (Panel == 0) ? Rows0 : Rows1;
	return Src.IsValidIndex(Row) ? FormatOptionRange(Src[Row]) : FString();
}

FString UAsdaItemOptionSubsystem::DumpControls() const
{
	FString S = TEXT("=== окно смены параметров 50300: разобранные контролы ===\n");
	auto Line = [&S](const TCHAR* Id, const UObject* W) { S += FString::Printf(TEXT("  %-42s %s\n"), Id, W ? TEXT("есть") : TEXT("НЕТ")); };
	Line(TEXT("50301 ItemOptionValue_Top_Text (заголовок)"), TxtTitle);
	Line(TEXT("50302 ItemOptionValue_Mark_1 (иконка)"),      IconTarget);
	Line(TEXT("50302 ItemOptionValue_Mark_1 (+N%)"),         IconTargetLbl);
	Line(TEXT("50303 ItemOptionValue_1_b1_Text (вкладка)"),  TxtTabItem);
	Line(TEXT("50336 ItemOptionValue_2_T1 (ИМЯ предмета)"),  TxtItemName);
	Line(TEXT("50334 Box_Mark_Click (курсор)"),              SelFrame);
	Line(TEXT("50380 ItemOptionValue_Op_Text (свитки)"),     TxtScrollCnt);
	Line(TEXT("50381 ItemOptionValue_Waning_Text"),          TxtWarn1);
	Line(TEXT("50382 ItemOptionValue_Waning_Text2"),         TxtWarn2);
	Line(TEXT("50390 ItemOptionValue_OK_B (Изменить)"),      BtnOk);
	Line(TEXT("50391 ItemOptionValue_NO_B (Отмена)"),        BtnCancel);
	Line(TEXT("50392 ItemOptionValue_Top_Text_BTN00 (крестик)"), BtnClose);
	Line(TEXT("50393 ItemOptionValue_Mark_ANI (ролик)"),     AniConfirm);
	for (int32 j = 0; j < kPanels; ++j)
	{
		S += FString::Printf(TEXT("  панель %d (idnum %d…%d): вкладка %s, текущее %s, заголовок %s, строки %s%s%s%s, полоса %s\n"),
			j, 50340 + 20 * j, 50351 + 20 * j,
			PanelUi[j].Tab ? TEXT("да") : TEXT("НЕТ"),
			PanelUi[j].Current.Node ? TEXT("да") : TEXT("НЕТ"),
			PanelUi[j].Header ? TEXT("да") : TEXT("НЕТ"),
			PanelUi[j].Row0.Node ? TEXT("1") : TEXT("-"), PanelUi[j].Row1.Node ? TEXT("2") : TEXT("-"),
			PanelUi[j].Row2.Node ? TEXT("3") : TEXT("-"), PanelUi[j].Row3.Node ? TEXT("4") : TEXT("-"),
			Bars[j].IsBuilt() ? TEXT("да") : TEXT("НЕТ"));
	}
	for (int32 i = 0; i < kCells; ++i)
	{
		S += FString::Printf(TEXT("  %d Box_Mark_%02d икона %s подпись %s\n"), 50304 + i, i + 1,
			CellUi[i].Icon ? TEXT("есть") : TEXT("НЕТ"), CellUi[i].Label ? TEXT("есть") : TEXT("НЕТ"));
	}
	return S;
}

FString UAsdaItemOptionSubsystem::DumpState() const
{
	FString S = FString::Printf(
		TEXT("=== окно смены параметров: состояние ===\n")
		TEXT("  открыто=%s ждём 6554=%s ролик=%s выбор=%d\n")
		TEXT("  свиток: код=%d контейнер=%d слот=%d, всего в стопках=%d\n")
		TEXT("  прокрутка панелей: %d / %d (максимум %d / %d)\n"),
		bOpen ? TEXT("да") : TEXT("нет"), bPending ? TEXT("да") : TEXT("нет"),
		bConfirm ? TEXT("да") : TEXT("нет"), SelCell,
		ScrollRef.ItemCode, ScrollRef.Container, ScrollRef.Slot, ScrollTotal,
		ScrollRow.IsValidIndex(0) ? ScrollRow[0] : 0, ScrollRow.IsValidIndex(1) ? ScrollRow[1] : 0,
		MaxScrollRow(0), MaxScrollRow(1));
	S += FString::Printf(TEXT("  сетка: позиций %d, ряд прокрутки %d/%d\n"), BagScroll.Total, BagScroll.Row, BagScroll.MaxRow());
	for (int32 i = 0; i < kListMax; ++i)
	{
		if (Cells[i].IsEmpty()) { continue; }
		S += FString::Printf(TEXT("  ячейка[%2d] код=%d контейнер=%d слот=%d%s\n"),
			i, Cells[i].ItemCode, Cells[i].Container, Cells[i].Slot, i == SelCell ? TEXT("  ← выбрана") : TEXT(""));
	}
	for (int32 j = 0; j < kPanels; ++j)
	{
		const TArray<FAsdaItemoptionrangeRow>& R = (j == 0) ? Rows0 : Rows1;
		S += FString::Printf(TEXT("  панель %d: возможных значений %d\n"), j, R.Num());
		// ★ 25.09.2026: что ЛЕЖИТ в видимых узлах (имя/класс живого узла + сырая строка с тегами) —
		//   проверка «пишем в тот узел, который видно» (дефект строки 50343, `optwin_fix.md`).
		auto Node = [&S](const TCHAR* What, const FAsdaFmtText& F)
		{
			S += FString::Printf(TEXT("    %s: узел %s (%s) «%s»\n"), What,
				F.Node ? *F.Node->GetName() : TEXT("НЕТ"), F.Node ? *F.Node->GetClass()->GetName() : TEXT("-"), *F.LastRaw);
		};
		Node(TEXT("текущее"), PanelUi[j].Current);
		Node(TEXT("строка 1"), PanelUi[j].Row0);
		Node(TEXT("строка 2"), PanelUi[j].Row1);
		Node(TEXT("строка 3"), PanelUi[j].Row2);
		Node(TEXT("строка 4"), PanelUi[j].Row3);
		for (int32 r = 0; r < R.Num() && r < 12; ++r)
		{
			S += FString::Printf(TEXT("    группа=%d тип=%d диапазон %d…%d → «%s»\n"),
				R[r].GroupId, R[r].OptionType, R[r].MinValue, R[r].MaxValue, *FormatOptionRange(R[r]));
		}
	}
	return S;
}

// ═══════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ (проверка без NPC) ════════════════════════════
namespace
{
	UAsdaItemOptionSubsystem* OptSub(UWorld* W)
	{
		return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaItemOptionSubsystem>() : nullptr;
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GOptOpen(
	TEXT("asda.ItemOption.Open"),
	TEXT("asda.ItemOption.Open — открыть окно 50300 так же, как код диалога NPC 30060"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaItemOptionSubsystem* S = OptSub(W);
		if (!S) { UE_LOG(LogAsdaItemOpt, Warning, TEXT("подсистема недоступна")); return; }
		S->OpenWindow();
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GOptClose(
	TEXT("asda.ItemOption.Close"),
	TEXT("asda.ItemOption.Close — закрыть окно 50300"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaItemOptionSubsystem* S = OptSub(W)) { S->CloseWindow(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GOptDump(
	TEXT("asda.ItemOption.Dump"),
	TEXT("asda.ItemOption.Dump — дамп КАЖДОГО контрола окна 50300 + сетка, свитки и обе панели"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaItemOptionSubsystem* S = OptSub(W);
		if (!S) { UE_LOG(LogAsdaItemOpt, Warning, TEXT("подсистема недоступна")); return; }
		UE_LOG(LogAsdaItemOpt, Log, TEXT("\n%s\n%s"), *S->DumpControls(), *S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GOptClick(
	TEXT("asda.ItemOption.Click"),
	TEXT("asda.ItemOption.Click <ячейка 0…29> — прогнать клик ТЕМ ЖЕ путём, что и мышь "
	     "(в headless-PIE мышью не кликнуть, а второй путь обработки заводить нельзя)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaItemOptionSubsystem* S = OptSub(W);
		if (!S) { UE_LOG(LogAsdaItemOpt, Warning, TEXT("подсистема недоступна")); return; }
		S->OnCellClicked(A.Num() > 0 ? FCString::Atoi(*A[0]) : 0);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GOptOk(
	TEXT("asda.ItemOption.Ok"),
	TEXT("asda.ItemOption.Ok — нажать 50390 «Изменить» (дальше играет ролик 50393 и уходит 6553)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaItemOptionSubsystem* S = OptSub(W)) { S->OnOkClicked(); }
	}));
#endif // ASDA_DEV_CONSOLE
