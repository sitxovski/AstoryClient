// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окна высокой переплавки `Smelting3_Win` (JUI 78200). Весь разбор эталона, адреса и флаги —
//   в шапке `AsdaHighRefineSubsystem.h` и в отчёте `Reports/gear_upgrade/wave5-7.md`.
//   Структурный дамп ассета (каждый контрол) — `Reports/gear_upgrade/_smelting3_wbp_tree.txt`
//   (зонд `Tools/_probe_itemupgrade_widgets.py`, переменная `ASDA_PROBE_ASSETS`).
#include "UI/AsdaHighRefineSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока

#include "Login/AsdaButtonRelay.h"

#include "Blueprint/WidgetTree.h"
#include "AsdaHighRefineSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaHighRefineSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaHighRefine);   // ★ R3-c 12.11.2026: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

// ★ Д7-3. Сетка — 14 ячеек на страницу, у эталона `uiControlSetScrollRange(78226, 1, 2)`, но сама полоса
//   `Smelting3_list_1` (78226, `WheelWin_Nomal`) в `.JUI` `Visible = "0"`, код её не включает, а в
//   импортированном ассете узла нет вовсе — сообщение смены страницы 0x17 не приходит, предметы
//   снаряжения сумки дальше 14-го выбрать нельзя. Исправление: колесо над окном листает страницы тем же
//   путём, что событие 0x17 (`sub_CF3E80` + `uiResetTradeWindow`). Под тем же ключом — две латентные
//   ошибки, которые у оригинала «спали» только потому, что страница 2 недостижима:
//     • отправка 6500 берёт предмет по индексу ячейки СТРАНИЦЫ (`vector[this[93]]`, без `+14*страница`)
//       — на второй странице ушёл бы предмет с первой;
//     • тултип иконки результата стоит за гейтом `[93] + 14*[92] <= 13` — на второй странице его нет.
//   ★ Волна 12 (25.09.2026, разрешение владельца «Полосу прокрутки добавь. Возьми её из ассетов игры»):
//   зона 78226 и полоса скина `Nomal_Scr` теперь ЕСТЬ в ассете (шаг `Tools/asda_ui_smelting3_scroll.py`),
//   код ведёт бегунок и ввод (`OnListScroll`, тот же путь 0x17, что колесо). Ключ 0 прячет и полосу.
ASDA_DEV_CVAR(int32, CVarWheelPages,
	TEXT("asda.HighRefine.WheelPages"), 1,
	TEXT("Дефект оригинала Д7-3: 1 = колесо над окном 78200 листает страницы сетки (предметы после 14-го), "
	     "отправка и тултип результата учитывают страницу; 0 = как оригинал — только первая страница."),
	ECVF_Default);

// ★ Д7-6: закон «не защёлкивать состояние заранее» — молчание сервера не должно давать вечный тупик.
//   Значение — как у заточки (`asda.Upgrade.ReplyTimeoutSec`, серверный замок P33 15 с + запас).
static ASDA_DEV_CVAR(float, CVarReplyTimeoutSec,
	TEXT("asda.HighRefine.ReplyTimeoutSec"), 20.f,
	TEXT("Секунд ожидания ответа 6501, после которых замок «6500 в полёте» снимается. 0 = без таймаута."),
	ECVF_Default);

namespace {
	const TCHAR* kWindowWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Smelting3_Win.WBP_Smelting3_Win_C");

}

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════
void UAsdaHighRefineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	UGameInstance* GI = GetGameInstance();
	// ★★★ ESC ОКНО 78200 НЕ ЗАКРЫВАЕТ — КАК У ОРИГИНАЛА (поправка волны 9; прежняя роспись `RegisterEscCloser`
	//   реестром отклонялась, а комментарий врал). Ветка `case 0x15: if (a2 == 78200) craftWnd_Close(this)`
	//   `uiTooltipWndProc` (ui_core.c:46146-46148) есть, но событие 0x15 по ESC приходит только окнам с признаком
	//   CControl+0x228 (`helper_setByteField552(окно, 1)`), а `CHighRefineUI_InitControls` (ui_core.c:45998-46130)
	//   его НЕ ставит (0 вызовов) — по ESC 0x15 окну 78200 не приходит. Закрыть окно можно X (78299) и
	//   «Отменой» (78292). В `uiCloseAllWindows` окна 78200 тоже НЕТ (ui_core.c:54349…) — «закрыть все» не вешаем.
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь.
		HResult = Net->OnHighRefineResult.AddLambda([this](const TArray<uint8>& B) { PendingResults.Push(B); });
	}
	UE_LOG(LogAsdaHighRefine, Log, TEXT("[переплавка] подсистема окна 78200 поднята"));
}

void UAsdaHighRefineSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnHighRefineResult.Remove(HResult);
	}
	DisarmCapture6500(TEXT("Deinitialize"));   // ★ волна 14 (МЕЛК-3): перехватчик самотеста не переживает подсистему
	ResetWidget();
	Super::Deinitialize();
}

void UAsdaHighRefineSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Эталон при перезагрузке интерфейсов (`LoadAllInterfaces2` case 13 → `sub_CF3AD0`) зовёт `Init` заново:
	//   окно скрыто, все поля в нуле. Замок Д7-6 снимается здесь — ответ старого сокета уже не придёт.
	if (bOpen)
	{
		bOpen = false;
		if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			WM->SetWindowVisible(kWindowId, false);
		}
	}
	ReleaseInFlight(AsdaSessionResetReasonName(Reason));
	bBusy = bDone = bInProgress = bMatsOk = false;
	Page = 0; SelCell = -1; RecipeKey = -1;
	LastReplyTime = -1.0;
	ProgressStart = -1.0;
	AttemptStartTime = -1.0;
	ProgressValue = 0.f;
	List.Reset();
	for (int32& C : MatCodes) { C = 0; }
	ResultCode = 0;
	PendingResults.Clear();
	TipKind = TipIdx = -1;
	Mouse.Reset();
	if (bControlsReady)
	{
		ShowPlane(FxE1, false);
		ShowPlane(FxE2, false);
		SetProgressVisible(false);
		SetProgressValue(0.f);
		ResetMoneyTexture();
	}
}

void UAsdaHighRefineSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.HighRefine");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	DrainPendingPackets();
	EnforceSinglePageWhenKeyOff();   // ★ волна 13 (СРЕДН-1): ключ Д7-3 = 0 ⇒ одна страница, как у оригинала
	const double Now = FPlatformTime::Seconds();
	// ★ Волна 14 (МЕЛК-3): прерванный самотест (смена карты, quit между шагами) не оставляет перехватчик навсегда.
	if (bCapture6500Armed && (Now - Capture6500ArmedAt) > kCapture6500TimeoutSec)
	{
		UE_LOG(LogAsdaHighRefine, Warning, TEXT("[переплавка] перехват 6500 висел %.1f с без пакета — снят по таймауту"),
			Now - Capture6500ArmedAt);
		DisarmCapture6500(TEXT("таймаут"));
	}
	// ★ Д7-6: таймаут замка «6500 в полёте» — работает и при закрытом окне.
	if (ReplyLatch.IsExpired(CVarReplyTimeoutSec.GetValueOnGameThread())) { OnReplyTimeout(); }   // ★ S2 (В2-A дедупа №2, 11.10.2026): та же формула
	if (!bControlsReady) { return; }
	TickProgress();
	if (!bOpen) { return; }
	Mouse.Update();
	// ★ Волна 14 (МЕЛК-5 ревизии волны 13): латч «полоса съедена» снимается отпусканием ЛКМ ДО всех ранних выходов
	//   (модальный гейт, перекрытие окна, ключ Д7-3 = 0, полоса не собрана) — иначе отпускание там терялось, и первое
	//   «занято» следующего нажатия не попадало в журнал.
	if (!Mouse.bLmb) { bBusyScrollLogged = false; }
	// ★ Модальный гейт (П8-9): под модальным нотисом эталон не отдаёт мышь окнам ниже.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		TickTooltip(-1, -1);
		Mouse.Reset();
		ListScroll.bThumbGrab = false;   // волна 12: под модальным нотисом захват бегунка снимается
		return;
	}
	TickMouse();
}

// ═════════════════════════════ ОКНО ═══════════════════════════════════════════════════════════
bool UAsdaHighRefineSubsystem::EnsureWidget()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWindowWbp, kWindowZ).Ensure(GetGameInstance(), Widget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupControls();
			RegisterZones();
			// ★★★ Волна 9 (С-2 ревизии волны 8): окна 20000 и 78200 НЕЗАВИСИМЫ, как у оригинала. `craftWnd_Open`
			//   (item_enhance.c:1437-1455) делает только `CClass_MapInsert(g_pWindowMgr, окно)` — обычный подъём в
			//   очередь окон; `uiShowChildWindow` (владелец) НЕ зовётся; сообщения 78200 идут своей процедуре
			//   (группа 782), 20000 — `CGameWindow_OnCommand` (группа 200). Прежний `SetWindowOwner(78200 → 20000)`
			//   снят: клик по 20000 поднимает его НАД 78200 (подъём на взаимодействие — `TickDrag`).
			(void)WM;
		},
		[]() { UE_LOG(LogAsdaHighRefine, Error, TEXT("[переплавка] WBP_Smelting3_Win не загрузился (%s)"), kWindowWbp); });
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaHighRefineSubsystem::ResetWidget()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kWindowId);
		WM->UnregisterWheelHandler(kWindowId);
		WM->CloseWindow(kWindowId);
	}
	Widget = nullptr;
	bControlsReady = false;
	bOpen = false;
	TxtTitle = TxtTop = TxtOptTitle = TxtGuide = TxtMatTitle = nullptr;
	TxtExpLabel = TxtExp = TxtUseGoldLbl = TxtUseGold = TxtHaveGoldLbl = TxtHaveGold = nullptr;
	TxtBtnOk = TxtBtnCancel = ResultLabel = nullptr;
	GridIcons.Reset(); GridLabels.Reset();
	MatIcons.Reset(); MatIconLabels.Reset(); MatNames.Reset(); MatCounts.Reset();
	CheckFrame = nullptr; ResultIcon = nullptr;
	FxE1 = FxE2 = nullptr;
	BtnOk = BtnCancel = BtnX = nullptr;
	BarBg = BarFill = nullptr;
	DragHandle = nullptr;
	ListZone1 = ListZone2 = nullptr;
	ListScroll = FAsdaScrollBarNomal();   // указатели на планы снесённого виджета не держим
	GridIconGrid = MatIconGrid = ResultIconGrid = nullptr;
	Relays.Reset();
	Mouse.Reset();
	// ★★★ Волна 11 (МЕЛК-1 ревизии волны 10): пересоздание виджета БЕЗ сброса сессии во время полосы оставляло
	//   +4 «идёт» = 1 и живой `ProgressStart` без замка «6500 в полёте» — таймаута Д7-6 у такой полосы нет, и общая
	//   блокировка `AsdaUi::CheckRecentActivity` держала инвентарь/окна его процедуры до переоткрытия 78200.
	//   Полоса принадлежала снесённому виджету (`TickProgress` без `bControlsReady` не идёт) — попытка сорвана,
	//   поэтому «идёт» снимаем и полосу гасим: 6500 по ней не уйдёт ни сейчас, ни при следующем открытии.
	//   Если 6500 УЖЕ ушёл (замок стоит) — не трогаем: состояние снимет ответ 6501 или таймаут Д7-6.
	//   [ПОРТ] у оригинала виджет не пересоздаётся (окно живёт с загрузки интерфейсов до `Init`).
	if (!ReplyLatch.IsArmed() && (bInProgress || ProgressStart >= 0.0))
	{
		UE_LOG(LogAsdaHighRefine, Log, TEXT("[переплавка] виджет пересоздан во время полосы — «идёт» снято, 6500 по ней не уйдёт"));
		bInProgress = false;
		ProgressStart = -1.0;
		AttemptStartTime = -1.0;
		ProgressValue = 0.f;
		JournalAdd(TEXT("reset widget: «идёт» снято"));
	}
}

void UAsdaHighRefineSubsystem::RegisterZones()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Рамка окна — девять планов `Smelting3_Win_0_0…8`.
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kWindowId, this, Widget, bOpen, TEXT("Smelting3_Win_0_%d"), 9);
	WM->RegisterWheelHandler(kWindowId, FAsdaUiWheelHandler::CreateUObject(this, &UAsdaHighRefineSubsystem::OnWheel));
}

void UAsdaHighRefineSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }

	GridIcons.SetNum(kGridCells); GridLabels.SetNum(kGridCells);
	MatIcons.SetNum(kMats); MatIconLabels.SetNum(kMats); MatNames.SetNum(kMats); MatCounts.SetNum(kMats);
	TArray<FName> GridNames; GridNames.SetNum(kGridCells);
	TArray<FName> MatIconNames; MatIconNames.SetNum(kMats);
	FName ResultName;

	// Планы полосы 78226 из ассета (их берёт общая полоса `FAsdaScrollBarNomal::Adopt`).
	UImage* ScrTrack = nullptr;
	UImage* ScrUp = nullptr;
	UImage* ScrDown = nullptr;
	UImage* ScrThumb = nullptr;
	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("Smelting3_OKbtn")) { BtnOk = Btn; }       // 78291
			if (N == TEXT("Smelting3_XXbtn")) { BtnCancel = Btn; }   // 78292
			if (N == TEXT("Smelting3_Xbtn"))  { BtnX = Btn; }        // 78299
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == TEXT("Smelting3_movetext"))   { TxtTitle = Tb; continue; }        // 78201
			if (N == TEXT("Smelting3_Top_text1"))  { TxtTop = Tb; continue; }          // 78202
			if (N == TEXT("Smelting3_T1_1_Text"))  { TxtOptTitle = Tb; continue; }     // 78210
			if (N == TEXT("Smelting3_T2_1"))       { TxtGuide = Tb; continue; }        // 78230
			if (N == TEXT("Smelting3_T3_1"))       { TxtMatTitle = Tb; continue; }     // 78231
			if (N == TEXT("Smelting3_T4_1"))       { TxtExpLabel = Tb; continue; }     // 78281
			if (N == TEXT("Smelting3_T4_2"))       { TxtExp = Tb; continue; }          // 78282
			if (N == TEXT("Smelting3_T4_3"))       { TxtUseGoldLbl = Tb; continue; }   // 78284
			if (N == TEXT("Smelting3_T4_4"))       { TxtUseGold = Tb; continue; }      // 78285
			if (N == TEXT("Smelting3_T4_5"))       { TxtHaveGoldLbl = Tb; continue; }  // 78286
			if (N == TEXT("Smelting3_T4_6"))       { TxtHaveGold = Tb; continue; }     // 78287
			if (N == TEXT("Smelting3_DMARK_1"))    { ResultLabel = Tb; continue; }     // 78271 (текст)
			if (N == TEXT("Smelting3_OKbtn_Text")) { TxtBtnOk = Tb; continue; }
			if (N == TEXT("Smelting3_XXbtn_Text")) { TxtBtnCancel = Tb; continue; }
			for (int32 C = 0; C < kGridCells; ++C)
			{
				if (N == FString::Printf(TEXT("Smelting3_M1_%d"), C + 1)) { GridLabels[C] = Tb; }   // 78211+C
			}
			for (int32 M = 0; M < kMats; ++M)
			{
				if (N == FString::Printf(TEXT("Smelting3_MARK2_%d"), M + 1)) { MatIconLabels[M] = Tb; }   // 78241+10M
				if (N == FString::Printf(TEXT("Smelting3_T3_%d"), 2 + 2 * M)) { MatNames[M] = Tb; }       // 78242+10M
				if (N == FString::Printf(TEXT("Smelting3_T3_%d"), 3 + 2 * M)) { MatCounts[M] = Tb; }      // 78243+10M
			}
			continue;
		}
		if (N == TEXT("Smelting3_movetext_5_DRAG")) { DragHandle = W; continue; }   // ручка окна (общая система окон)
		// ★ Волна 12: колёсные окна и полоса 78226 — узлы шага `Tools/asda_ui_smelting3_scroll.py` (имена из .JUI `ID`).
		if (N == TEXT("Smelting3_list_1")) { ListZone1 = W; continue; }   // 78226
		if (N == TEXT("Smelting3_list_2")) { ListZone2 = W; continue; }   // 78265
		if (N == TEXT("Smelting3_list_1_Scr_Track")) { ScrTrack = Cast<UImage>(W); continue; }
		if (N == TEXT("Smelting3_list_1_Scr_Up"))    { ScrUp    = Cast<UImage>(W); continue; }
		if (N == TEXT("Smelting3_list_1_Scr_Down"))  { ScrDown  = Cast<UImage>(W); continue; }
		if (N == TEXT("Smelting3_list_1_Scr_Thumb")) { ScrThumb = Cast<UImage>(W); continue; }
		// Планы `<idc>_<juiIdx>_<plane>`.
		FString Idc; int32 JuiIdx = 0, Plane = 0;
		if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, Plane)) { continue; }
		if (Idc == TEXT("Smelting3_M1_Check") && Plane == 0) { CheckFrame = W; CheckBase = SlotPos(W); continue; }   // 78225
		if (Idc == TEXT("Smelting3_DMARK_1_E1")) { FxE1 = Cast<UAsdaAnimatedPlane>(W); continue; }   // 78272
		if (Idc == TEXT("Smelting3_DMARK_1_E2")) { FxE2 = Cast<UAsdaAnimatedPlane>(W); continue; }   // 78273
		UImage* Img = Cast<UImage>(W);
		if (!Img) { continue; }
		if (Idc == TEXT("Smelting3_bar_G"))                  // 78280: план 0 — фон, план 1 — заливка
		{
			if (Plane == 0) { BarBg = Img; }
			if (Plane == 1) { BarFill = Img; }
			continue;
		}
		if (Idc == TEXT("Smelting3_DMARK_1") && Plane == 0) { ResultIcon = Img; ResultName = FName(*N); continue; }   // 78271
		for (int32 C = 0; C < kGridCells; ++C)
		{
			if (Plane == 0 && Idc == FString::Printf(TEXT("Smelting3_M1_%d"), C + 1)) { GridIcons[C] = Img; GridNames[C] = FName(*N); }
		}
		for (int32 M = 0; M < kMats; ++M)
		{
			if (Plane == 0 && Idc == FString::Printf(TEXT("Smelting3_MARK2_%d"), M + 1)) { MatIcons[M] = Img; MatIconNames[M] = FName(*N); }
		}
	}

	// ── ★ Волна 12: полоса 78226 — ПЛАНЫ АССЕТА (не создаём кодом). Место и высота — тоже из ассета: левый-верх
	//    стрелки ↑ и низ стрелки ↓ (шаг пост-импорта ставит их по `uiControl_attachScrollbar` у правого края
	//    зоны 78226); код дальше двигает только бегунок по странице (`LayoutListScroll`).
	ListScroll = FAsdaScrollBarNomal();
	ListScrollOrigin = FVector2D::ZeroVector;
	ListScrollH = 0.f;
	if (ListScroll.Adopt(ScrTrack, ScrUp, ScrDown, ScrThumb))
	{
		const FMargin U = AsdaUi::SlotOf(ScrUp)->GetOffsets();
		const FMargin D = AsdaUi::SlotOf(ScrDown)->GetOffsets();
		ListScrollOrigin = FVector2D(U.Left, U.Top);
		ListScrollH = (float)(D.Top + D.Bottom - U.Top);
	}
	else
	{
		UE_LOG(LogAsdaHighRefine, Warning, TEXT("[переплавка] полосы 78226 в ассете нет (узлы Smelting3_list_1_Scr_* — шаг "
			"Tools/asda_ui_smelting3_scroll.py); страницы листает только колесо"));
	}

	// ── Полоса 78280: «максимум анимации» эталона = ширина ПЛАНА 0 (`uiProgressBar_AnimStart`, флаг 4) ──
	if (UCanvasPanelSlot* S = BarBg ? AsdaUi::SlotOf(BarBg) : nullptr) { BarFullW = (float)S->GetOffsets().Right; }
	if (UCanvasPanelSlot* S = BarFill ? AsdaUi::SlotOf(BarFill) : nullptr) { BarBaseLeft = (float)S->GetOffsets().Left; }

	// ── Иконки — через ОБЩУЮ точку. Клик ловим сами по живой геометрии (`TickMouse`). ──
	auto MakeGrid = [this](const TArray<FName>& Names, float Px, TFunction<int32(int32)>&& Code) -> UAsdaIconCellGrid*
	{
		UAsdaIconCellGrid* G = NewObject<UAsdaIconCellGrid>(this);
		G->SetCellSizePx(Px);
		G->SetClickOverlayEnabled(false);
		G->bClearEmptyCells = true;   // эталон затирает иконку пустой текстурой
		G->SetupNamed(Widget, Names, /*bMale*/true, MoveTemp(Code), [](int32) {});
		return G;
	};
	// Сетка — СНИМОК кода на момент построения списка (эталон рисует иконки в `uiInventoryBagPanelRefresh`
	//   и больше их не трогает — ответ 6501 сетку не перерисовывает).
	GridIconGrid = MakeGrid(GridNames, 32.f, [this](int32 C)
	{
		const int32 Abs = C + kGridCells * Page;
		return List.IsValidIndex(Abs) ? List[Abs].IconCode : 0;
	});
	MatIconGrid = MakeGrid(MatIconNames, 32.f, [this](int32 M) { return (M >= 0 && M < kMats) ? MatCodes[M] : 0; });
	if (!ResultName.IsNone())
	{
		TArray<FName> One; One.Add(ResultName);
		ResultIconGrid = MakeGrid(One, 64.f, [this](int32) { return ResultCode; });   // Size «64 64» у DMARK_1
	}

	// ── Кнопки: ветка msgType 66561 `uiTooltipWndProc` → `craftWnd_OnButtonMsg`. Звука клика у окна НЕТ. ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void()>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, [F = MoveTemp(Fn)](int32) { F(); }, Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnOk,     78291, [this]() { OnOkClicked(); });
	Bind(BtnCancel, 78292, [this]() { OnCancelClicked(); });
	Bind(BtnX,      78299, [this]() { OnCloseClicked(); });

	// ── Эффекты 78272/78273: событие 0x18 «анимация доиграла» → `craftWndOnButtonCommand` ──
	if (FxE1) { FxE1->OnAnimFinished.AddUniqueDynamic(this, &UAsdaHighRefineSubsystem::HandleE1Finished); }
	if (FxE2) { FxE2->OnAnimFinished.AddUniqueDynamic(this, &UAsdaHighRefineSubsystem::HandleE2Finished); }

	// ── Статичные подписи `CHighRefineUI_InitControls` (ui_core.c:46013-46125) ──
	auto SetText = [](UTextBlock* Tb, const FString& S) { if (Tb) { Tb->SetText(FText::FromString(S)); } };
	SetText(TxtTitle,      Loc(AsdaHighRefineLoc::kTitle));
	SetText(TxtTop,        Loc(AsdaHighRefineLoc::kTopText));
	SetText(TxtOptTitle,   Loc(AsdaHighRefineLoc::kOptionTitle));
	for (int32 M = 0; M < kMats; ++M)
	{
		SetText(MatIconLabels.IsValidIndex(M) ? MatIconLabels[M].Get() : nullptr, FString());
		SetText(MatNames.IsValidIndex(M) ? MatNames[M].Get() : nullptr, FString());
		SetText(MatCounts.IsValidIndex(M) ? MatCounts[M].Get() : nullptr, FString());
	}
	SetText(TxtGuide,      Loc(AsdaHighRefineLoc::kGuideInit));
	// ★ `CControl_SetField924(78230, 4)` — выравнивание текста гайда = 0x4 (центр по вертикали, по горизонтали
	//   влево); в `.JUI` у контрола 5 (0x1 | 0x4). Это поведение КОДА оригинала, не правка разметки.
	//   Высота контрола из `.JUI` — 30 (`Size = "260 30"`).
	if (TxtGuide) { AsdaUi::ApplyJuiTextAlign(TxtGuide, /*AlignMask*/0x4, /*JuiH*/30.f); }
	SetText(TxtMatTitle,   Loc(AsdaHighRefineLoc::kMatTitle));
	SetText(TxtExpLabel,   Loc(AsdaHighRefineLoc::kExpLabel));
	SetText(TxtExp,        TEXT("0"));
	SetText(TxtUseGoldLbl, Loc(AsdaHighRefineLoc::kUseGold));
	SetText(TxtUseGold,    TEXT("0"));
	SetText(TxtHaveGoldLbl,Loc(AsdaHighRefineLoc::kHaveGold));
	SetText(TxtHaveGold,   TEXT("0"));
	SetText(TxtBtnOk,      Loc(AsdaHighRefineLoc::kBtnOk));
	SetText(TxtBtnCancel,  Loc(AsdaHighRefineLoc::kBtnCancel));
	// `CProgressBar_SetValue(78280, 0)` + `SetRange(0, 100.0f)`.
	SetProgressValue(0.f);
	ShowPlane(FxE1, false);
	ShowPlane(FxE2, false);
	if (CheckFrame) { CheckFrame->SetVisibility(ESlateVisibility::Collapsed); }

	bControlsReady = (GridIcons[0] != nullptr && BtnOk != nullptr && TxtGuide != nullptr);
	UE_LOG(LogAsdaHighRefine, Log, TEXT("%s"), *DumpControls());
	if (!bControlsReady)
	{
		UE_LOG(LogAsdaHighRefine, Error, TEXT("[переплавка] ключевые контролы не разобраны: сетка=%s ОК=%s гайд=%s"),
			GridIcons[0] ? TEXT("есть") : TEXT("НЕТ"), BtnOk ? TEXT("есть") : TEXT("НЕТ"), TxtGuide ? TEXT("есть") : TEXT("НЕТ"));
	}
}
