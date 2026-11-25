// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaFishWindowSubsystem — реализация окна рыбалки 71300 и мини-игры (волна В2, 08.10.2026).
//   Обоснование каждого числа — шапка `UI/AsdaFishWindowSubsystem.h` и `Reports/owner_1008/fishing_parts/ui_fishwin.md`.
//   `g:N` — строка `RE/ida/export/AsdaStory_game.c`.
#include "UI/AsdaFishWindowSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton

#include "UI/AsdaJuiWindowHandle.h"        // каркас окон JUI (Ensure)
#include "UI/AsdaWindowSubsystem.h"        // реестр окон, ESC-закрыватель, зоны
#include "UI/AsdaInventorySubsystem.h"     // закрыть инвентарь (`sowelWindowClose`), иконка улова
#include "UI/AsdaItemTooltipSubsystem.h"   // общая плашка подсказки (`tutorialPopupLayoutSmall`)
#include "UI/AsdaTipPanel.h"               // EAsdaTipOwner::FishWin, FAsdaTipMetrics
#include "UI/AsdaNotice.h"                 // LocRaw
#include "Items/AsdaItemTableSubsystem.h"  // имя/запись пойманного предмета
#include "Resources/AsdaSoundSubsystem.h"  // `s_e_fishing_bto_00` / `btx_00`
#include "Login/AsdaButtonRelay.h"         // реле кнопки 71328

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFishWin, Log, All);

// ═════════════════════ КОНСТАНТЫ ОКНА ═════════════════════
static const FName  kFishWindowId(TEXT("FISH_WIN"));   // корень 71300 в `.JUI` / реестре окон / таблице ESC
static const TCHAR* kFishWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Fishing/WBP_FISH_WIN.WBP_FISH_WIN_C");   // WBP импортёра

// Loc-идентификаторы (сняты с вызовов `UI_FormatLocalizedNotice` в обработчиках эталона).
namespace FishWinLoc
{
	constexpr int32 kTipTitle   = 111096;   // «Рыболовство» — заголовок подсказок (событие 9)
	constexpr int32 kTipCancel  = 111064;   // 71328 X: «Рыбалка будет прекращена. Наживка использована во время ловли.»
	constexpr int32 kTipPause   = 111065;   // 71329: «Рыбалка прекращена.»
	constexpr int32 kTipAuto    = 111066;   // 71349: «Рыбалка автоматически начнется заново.»
	constexpr int32 kCantPause  = 111061;   // «Сейчас нельзя прекращать ловлю.» (плашка 221)
	constexpr int32 kPaused     = 111062;   // «Ловля остановлена» (плашка 221)
	constexpr int32 kResumed    = 111063;   // «Продолжить рыбалку» (плашка 221)
	constexpr int32 kCatchSize  = 111070;   // «Добыча: [%s] %d см!» (71401)
	constexpr int32 kCatchName  = 111078;   // «Добыча: [%s]» (71401, размера нет)
}

// IDNum кнопок с подсказками (ключи `OnHoverControl`).
static constexpr int32 kIdCancel = 71328;   // FISH_Xbtn
static constexpr int32 kIdPause  = 71329;   // FISHI_PLAY_STOP_btn
static constexpr int32 kIdAuto   = 71349;   // FISHI_AR_Check

// ★ Р8 [ПОРТ] — галка автоповтора. Обоснование — шапка .h и `fishing_reverse.md` §8.
static ASDA_DEV_CVAR(int32, CVarFishLegacyAutoCheck,
	TEXT("asda.Fish.LegacyAutoCheck"), 0,
	TEXT("Р8. Галка автоповтора 71349 окна рыбалки: 0 (по умолчанию) = [ПОРТ] рисует ФАКТИЧЕСКОЕ состояние автоповтора ")
	TEXT("(отмечена = рыбалка начнётся заново); 1 = как оригинал — показ окна галку не трогает (JUI: снята), клик ставит ")
	TEXT("«отмечена = автоповтор ВЫКЛЮЧЕН» (визуальная инверсия, uiControlEventDispatch@0xFC9110)."),
	ECVF_Default);

// ★ Ревизия В2 КРИТ-1 [ИСПРАВЛЕНИЕ] (волна В4, 08.10.2026). Дефект оригинала: FSM 0x80 (g:608620-608627) гайд НЕ сбрасывает,
//   а `Input_FishingKeyFilter` (g:605056-605057) проверяет только `*(this+70) == 4` — после отмены/смерти в состоянии ввода
//   стрелок у эталона съедаются WM_KEYDOWN (ход, окна), хоткеи же идут по WM_KEYUP и живут. У порта команды — по фронту
//   отпускания состояния `UPlayerInput`, поэтому тот же дефект глушил ВСЮ клавиатуру до следующей рыбалки.
//   0 (по умолчанию) = FSM 128 (`PanelHideAndClear`) гасит гайд, а фильтр мини-игры работает только пока идёт рыбалка;
//   1 = как оригинал (гайд остаётся в 4, фильтр ест нажатия и при fishState 0).
static ASDA_DEV_CVAR(int32, CVarFishLegacyStaleGuide,
	TEXT("asda.Fish.LegacyStaleGuide"), 0,
	TEXT("Ревизия В2 КРИТ-1: 0 (по умолчанию) = [ИСПРАВЛЕНИЕ] FSM 128 сбрасывает гайд мини-игры, фильтр стрелок только при ")
	TEXT("идущей рыбалке; 1 = как оригинал (гайд после отмены остаётся в состоянии ввода и ест нажатия клавиш)."),
	ECVF_Default);

// ═════════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaFishWindowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaFishingSubsystem::StaticClass());
	Cells.SetNum(kCells);
	if (UAsdaFishingSubsystem* F = Fish())
	{
		// Контракт `IAsdaFishMiniGame` (ревизия В1 МЕЛК-7): владелец — эта подсистема, автомат держит слабую ссылку.
		F->SetMiniGame(this, this);
		bRegistered = true;
	}
	UE_LOG(LogAsdaFishWin, Log, TEXT("[окно рыбалки] подсистема поднята, мини-игра %s"), bRegistered ? TEXT("зарегистрирована") : TEXT("НЕ зарегистрирована"));
}

void UAsdaFishWindowSubsystem::Deinitialize()
{
	if (UAsdaFishingSubsystem* F = Fish())
	{
		if (F->GetRegisteredMiniGameOwner() == this) { F->SetMiniGame(nullptr); }
	}
	bRegistered = false;
	ResetWidget();
	Super::Deinitialize();
}

// Окно и гайд — состояние сессии. Эталон при разрыве закрывает клиент; у порта — сброс (автомат сбрасывается сам и
//   зовёт `OnFishSessionReset`; здесь — то же на случай другого порядка сброса подсистем).
void UAsdaFishWindowSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	OnFishSessionReset(AsdaIsFullSessionReset(Reason));
}

void UAsdaFishWindowSubsystem::OnFishSessionReset(bool /*bFull*/)
{
	Guide = FAsdaFishKeyGuide();
	LastTimerDigitX[0] = LastTimerDigitX[1] = LastTimerDigitX[2] = -1;   // ревизия В2 МЕЛК-7
	HideTip();
	HoverId = 0;
	Mouse.Reset();
	if (bControlsReady)
	{
		PanelSetVisible(false);
		AsdaUi::SetShown(PopupGrp, false);
	}
	if (bOpen)
	{
		bOpen = false;
		if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			WM->SetWindowVisible(kFishWindowId, false);
		}
	}
	bPanelShown = false;
}

UAsdaFishingSubsystem* UAsdaFishWindowSubsystem::Fish() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaFishingSubsystem>() : nullptr;
}

uint32 UAsdaFishWindowSubsystem::NowMs() const
{
	const UAsdaFishingSubsystem* F = Fish();
	return F ? F->GetNowMs() : (uint32)(FPlatformTime::Seconds() * 1000.0);   // `timeGetTime()`
}

// Покадровый опрос мыши: события 9/0xA (наведение/уход) и 0xB (нажатие ЛКМ) по ЖИВОЙ геометрии.
void UAsdaFishWindowSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.FishWindow");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	if (!bOpen || !bControlsReady)
	{
		if (HoverId != 0) { HoverId = 0; HideTip(); }
		Mouse.Reset();
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	Mouse.Update();
	// Модальный гейт (`AsdaUi::IsModalDispatchBlocked`): под диалогом 80 эталон мышь окну не отдаёт.
	if (AsdaUi::IsModalDispatchBlocked(GI) || (WM && WM->IsPointerBlockedByTopmost(kFishWindowId)))
	{
		if (HoverId != 0) { HoverId = 0; HideTip(); }
		Mouse.Reset();
		return;
	}
	int32 Over = 0;
	if (AsdaUi::IsShown(BtnX) && AsdaUi::IsCursorOverWidget(BtnX)) { Over = kIdCancel; }
	else if (AsdaUi::IsShown(ChkPause) && AsdaUi::IsCursorOverWidget(ChkPause)) { Over = kIdPause; }
	else if (AsdaUi::IsShown(ChkAuto) && AsdaUi::IsCursorOverWidget(ChkAuto)) { Over = kIdAuto; }
	if (Over != HoverId)
	{
		HoverId = Over;
		OnHoverControl(Over);
	}
	// Событие 0xB (нажатие ЛКМ, `game.c:986977`) — у галок; клик X 71328 — событие 66561 (реле кнопки).
	if (Mouse.bLmbDown)
	{
		if (Over == kIdPause) { OnPausePressed(); }
		else if (Over == kIdAuto) { OnAutoRepeatPressed(); }
	}
}

// ═════════════════════════════════ ВИДЖЕТЫ ═════════════════════════════════════════════════════════

bool UAsdaFishWindowSubsystem::EnsureWidget()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kFishWindowId, kFishWbp, kZ).Ensure(GetGameInstance(), Widget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupControls();
			RegisterZones();
			// `helper_setByteField552(FISH_WIN, 1)` — окно отвечает на ESC событием 0x15 (диалог 80). FISH_WIN есть
			//   в эталонной таблице `kEscCloseWindows` (`fishingWindowInitControls@0xFC83E0`).
			WM.RegisterEscCloser(kFishWindowId, FSimpleDelegate::CreateWeakLambda(this, [this]() { OnCancelPressed(); }));
		},
		[]()
		{
			UE_LOG(LogAsdaFishWin, Error, TEXT("[окно рыбалки] WBP_FISH_WIN не загрузился (%s)"), kFishWbp);
		});
	if (R == EAsdaJuiEnsure::Created) { bOpen = false; }
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaFishWindowSubsystem::ResetWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		for (int32 i = 0; i < ZoneNodes.Num(); ++i) { WM->UnregisterUiZone(FName(*FString::Printf(TEXT("FISH_WIN_Z%d"), i))); }
		WM->UnregisterEscCloser(kFishWindowId);
		WM->CloseWindow(kFishWindowId);
	}
	HideTip();
	Widget = nullptr;
	HeaderGrp = nullptr; RoundBar = FAsdaFishBarUi(); RoundText = nullptr;
	PanelGrp = nullptr; PanelPlane = nullptr; PanelBbPlane = nullptr;
	Cells.Reset(); Cells.SetNum(kCells);
	TmplO = TmplX = nullptr;
	ImageGrp = nullptr; FishTypeImg = nullptr; BarGrp = nullptr; GaugeBar = FAsdaFishBarUi(); FishAni = nullptr;
	PopupGrp = nullptr; PopupIcon = nullptr; PopupText = FAsdaFmtText();
	TimerGrp = nullptr; TimerBar = FAsdaFishBarUi(); DigitsGrp = nullptr; Digit1 = Digit2 = Digit3 = nullptr;
	LastTimerDigitX[0] = LastTimerDigitX[1] = LastTimerDigitX[2] = -1;   // ревизия В2 МЕЛК-7: кэш кадров цифр
	BtnX = nullptr; ChkPause = nullptr; ChkAuto = nullptr;
	ZoneNodes.Reset(); Relays.Reset();
	bControlsReady = false;
	bOpen = false;
	bPanelShown = false;
	HoverId = 0;
	Mouse.Reset();
}

// Снять полосу-прогрессбар по планам ES0/ES1: левый край и ширина — по ПОДЛОЖКЕ (ширина контрола, закон полос порта).
static void AsdaFishBindBar(FAsdaFishBarUi& B)
{
	const UCanvasPanelSlot* S = B.Back ? AsdaUi::SlotOf(B.Back) : (B.Fill ? AsdaUi::SlotOf(B.Fill) : nullptr);
	if (!S) { return; }
	B.BaseLeft = S->GetOffsets().Left;
	B.FullW = S->GetOffsets().Right;   // POINT-анкер импортёра: Right = ширина
}

// ★ ПОЛНЫЙ СТРУКТУРНЫЙ РАЗБОР (`fishingWindowInitControls@0xFC83E0`, g:605579-605628). Имена — импортёра:
//   группа `{idc}_grp`, план `{idc}_{juiIdx}_{plane}`, интерактив/Text — точное idc; клоны O/X — шаг пост-импорта.
void UAsdaFishWindowSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }
	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	UWidget* DigitsParentHint = nullptr;   // родитель группы цифр = 71348
	UWidget* GaugeParentHint = nullptr;    // родитель плана шкалы = 71304
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("FISH_Xbtn")) { BtnX = Btn; continue; }
			if (N.StartsWith(TEXT("FISH_btn_")) && N.Len() == 11)
			{
				const int32 Idx = FCString::Atoi(*N.Mid(9)) - 1;   // FISH_btn_01 … FISH_btn_15
				if (Cells.IsValidIndex(Idx)) { Cells[Idx].Btn = Btn; }
			}
			continue;
		}
		if (UCheckBox* Chk = Cast<UCheckBox>(W))
		{
			if (N == TEXT("FISHI_PLAY_STOP_btn")) { ChkPause = Chk; }
			else if (N == TEXT("FISHI_AR_Check")) { ChkAuto = Chk; }
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == TEXT("FISH_BAR_1_TEXT")) { RoundText = Tb; }
			else if (N == TEXT("FISH_icon_Messagebox_text")) { UAsdaRichText::CaptureFmt(PopupText, Tb); }
			continue;
		}
		if (N == TEXT("Gfont_fishing_grp"))        { HeaderGrp = W; continue; }
		if (N == TEXT("FISH_btn_00_grp"))          { PanelGrp = W; continue; }
		if (N == TEXT("FISH_image_1_grp"))         { ImageGrp = W; continue; }
		if (N == TEXT("FISH_icon_Messagebox_grp")) { PopupGrp = W; continue; }
		if (N == TEXT("FISH_count_1_grp"))         { DigitsGrp = W; DigitsParentHint = W->GetParent(); continue; }
		// Клоны O/X ячеек (шаг `asda_ui_fish_ox_clones`): `FISH_SMALL_{O,X}_CLONEnn`.
		if (N.StartsWith(TEXT("FISH_SMALL_O_CLONE")) || N.StartsWith(TEXT("FISH_SMALL_X_CLONE")))
		{
			const int32 Idx = FCString::Atoi(*N.Mid(18)) - 1;
			UImage* Img = Cast<UImage>(W);
			if (Img && Cells.IsValidIndex(Idx)) { (N[11] == TEXT('O') ? Cells[Idx].O : Cells[Idx].X) = Img; }
			continue;
		}
		FString Idc; int32 JuiIdx = 0, Plane = 0;
		if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, Plane)) { continue; }
		UImage* Img = Cast<UImage>(W);
		if (!Img) { continue; }
		if (Idc == TEXT("FISH_btn_00") && Plane == 0)  { PanelPlane = Img; continue; }
		if (Idc == TEXT("FISH_btn_bb") && Plane == 0)  { PanelBbPlane = Img; continue; }
		if (Idc == TEXT("SMALL_O")) { TmplO = Img; continue; }
		if (Idc == TEXT("SMALL_X")) { TmplX = Img; continue; }
		if (Idc == TEXT("FISH_image_M") && Plane == 0) { FishTypeImg = Img; continue; }
		if (Idc == TEXT("FISH_bar_1"))        { (Plane == 0 ? RoundBar.Back : RoundBar.Fill) = Img; continue; }
		if (Idc == TEXT("FISH_count_1_bar"))  { (Plane == 0 ? TimerBar.Back : TimerBar.Fill) = Img; continue; }
		if (Idc == TEXT("FISH_bar_2"))        { (Plane == 0 ? GaugeBar.Back : GaugeBar.Fill) = Img; GaugeParentHint = W->GetParent(); continue; }
		if (Idc == TEXT("FISH_ANI"))          { FishAni = Img; continue; }
		if (Idc == TEXT("FISH_icon_Messagebox_Mark") && Plane == 0) { PopupIcon = Img; continue; }
		if (Idc == TEXT("FISH_count_1") && Plane == 0) { Digit1 = Img; continue; }
		if (Idc == TEXT("FISH_count_2") && Plane == 0) { Digit2 = Img; continue; }
		if (Idc == TEXT("FISH_count_3") && Plane == 0) { Digit3 = Img; continue; }
		if (Idc.StartsWith(TEXT("FISH_FFimgae_m")) && Plane == 0)
		{
			const int32 Idx = FCString::Atoi(*Idc.Mid(14)) - 1;   // FISH_FFimgae_m1 … m15
			if (Cells.IsValidIndex(Idx)) { Cells[Idx].Icon = Img; }
		}
	}
	TimerGrp = DigitsParentHint;   // 71348 — родитель группы цифр 71361 (`.JUI`: 71361 — ребёнок 71348)
	BarGrp = GaugeParentHint;      // 71304 — родитель плана 71305
	AsdaFishBindBar(RoundBar);
	AsdaFishBindBar(TimerBar);
	AsdaFishBindBar(GaugeBar);

	// Геометрия-база для SetPos/SetSize эталона (числа `.JUI`: Origin 71310 = −316, Origin 71326 = 5).
	if (const UCanvasPanelSlot* S = PanelGrp ? AsdaUi::SlotOf(PanelGrp) : nullptr) { PanelRootLeft = S->GetOffsets().Left - (float)kPanelJuiX; }
	if (const UCanvasPanelSlot* S = FishAni ? AsdaUi::SlotOf(FishAni) : nullptr)
	{
		AniBaseLeft = S->GetOffsets().Left - (float)kAniJuiX;
		AniExtentW = S->GetOffsets().Right;
	}
	if (GaugeBar.FullW > 0.f) { GaugeExtentW = GaugeBar.FullW; }

	int32 CellsOk = 0;
	for (const FAsdaFishCellUi& C : Cells) { CellsOk += (C.Btn && C.Icon && C.O && C.X) ? 1 : 0; }
	bControlsReady = CellsOk == kCells && PanelGrp && PanelPlane && PanelBbPlane && RoundBar.Fill && RoundText &&
		GaugeBar.Fill && TimerBar.Fill && FishAni && PopupGrp && PopupIcon && PopupText.Node && TimerGrp && DigitsGrp &&
		Digit1 && Digit2 && Digit3 && BtnX && ChkPause && ChkAuto && ImageGrp && FishTypeImg && BarGrp;

	// ── Шаги init эталона ──
	AsdaUi::SetShown(TmplO, false);                            // `CControl_SetVisible(71356, 0)` — шаблон O
	AsdaUi::SetShown(TmplX, false);                            // `CControl_SetVisible(71355, 0)` — шаблон X
	for (FAsdaFishCellUi& C : Cells) { AsdaUi::SetShown(C.O, false); AsdaUi::SetShown(C.X, false); }   // клоны рождаются скрытыми
	if (RoundText) { RoundText->SetText(FText::GetEmpty()); }   // `CControl_SetText(71346, "")` (стиль 926 — в ассете)

	// Кнопка X: событие 66561 → диалог 80.
	if (BtnX)
	{
		AsdaUi::BindButton(this, BtnX, kIdCancel, [this](int32) { OnCancelPressed(); }, Relays);   // ★ U20: реле клика — общая точка
	}
	// Галки: `set_o261(+609) = 1` — сами не переключаются; состояние ставит только код (`CControl_SetVisibleState`).
	if (ChkPause) { ChkPause->OnCheckStateChanged.AddDynamic(this, &UAsdaFishWindowSubsystem::OnPauseCheckChanged); }
	if (ChkAuto) { ChkAuto->OnCheckStateChanged.AddDynamic(this, &UAsdaFishWindowSubsystem::OnAutoCheckChanged); }

	UE_LOG(LogAsdaFishWin, Log,
		TEXT("[окно рыбалки] разбор 71300: ячеек %d/15 (кнопка+кадр+O+X), панель %s/%s/%s, раунды %s/%s, шкала %s (ширина %.0f), ")
		TEXT("рыбка %s (база %.0f, ширина %.0f), таймер %s/%s, цифры %s/%s/%s, всплывашка %s/%s/%s, X %s, пауза %s, автоповтор %s, ")
		TEXT("картинка %s/%s, шаблоны O/X %s/%s → %s"),
		CellsOk, PanelGrp ? TEXT("да") : TEXT("НЕТ"), PanelPlane ? TEXT("да") : TEXT("НЕТ"), PanelBbPlane ? TEXT("да") : TEXT("НЕТ"),
		RoundBar.Fill ? TEXT("да") : TEXT("НЕТ"), RoundText ? TEXT("да") : TEXT("НЕТ"), GaugeBar.Fill ? TEXT("да") : TEXT("НЕТ"), GaugeExtentW,
		FishAni ? TEXT("да") : TEXT("НЕТ"), AniBaseLeft, AniExtentW, TimerGrp ? TEXT("да") : TEXT("НЕТ"), TimerBar.Fill ? TEXT("да") : TEXT("НЕТ"),
		Digit1 ? TEXT("да") : TEXT("НЕТ"), Digit2 ? TEXT("да") : TEXT("НЕТ"), Digit3 ? TEXT("да") : TEXT("НЕТ"),
		PopupGrp ? TEXT("да") : TEXT("НЕТ"), PopupIcon ? TEXT("да") : TEXT("НЕТ"), PopupText.Node ? TEXT("да") : TEXT("НЕТ"),
		BtnX ? TEXT("да") : TEXT("НЕТ"), ChkPause ? TEXT("да") : TEXT("НЕТ"), ChkAuto ? TEXT("да") : TEXT("НЕТ"),
		ImageGrp ? TEXT("да") : TEXT("НЕТ"), FishTypeImg ? TEXT("да") : TEXT("НЕТ"), TmplO ? TEXT("да") : TEXT("НЕТ"), TmplX ? TEXT("да") : TEXT("НЕТ"),
		bControlsReady ? TEXT("ГОТОВО") : TEXT("НЕПОЛНО (клоны O/X: Tools/asda_ui_fish_ox_clones.py)"));
}

// Зоны интерфейса: каждая видимая группа окна — отдельная зона (объединение раздуло бы зону на полэкрана).
void UAsdaFishWindowSubsystem::RegisterZones()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM || !Widget || !Widget->WidgetTree) { return; }
	ZoneNodes.Reset();
	ZoneNodes.Add(HeaderGrp); ZoneNodes.Add(BarGrp); ZoneNodes.Add(PanelGrp); ZoneNodes.Add(ImageGrp); ZoneNodes.Add(PopupGrp);
	ZoneNodes.Add(BtnX ? BtnX->GetParent() : nullptr);        // 71327 рамка X
	ZoneNodes.Add(ChkPause ? ChkPause->GetParent() : nullptr); // рамка паузы
	ZoneNodes.Add(ChkAuto && ChkAuto->GetParent() ? ChkAuto->GetParent()->GetParent() : nullptr);   // рамка автоповтора
	for (int32 i = 0; i < ZoneNodes.Num(); ++i)
	{
		TWeakObjectPtr<UWidget> Node = ZoneNodes[i].Get();
		WM->RegisterUiZone(FName(*FString::Printf(TEXT("FISH_WIN_Z%d"), i)), FAsdaUiZoneRect::CreateWeakLambda(this,
			[this, Node](FVector2D& Mn, FVector2D& Mx)
			{
				const UWidget* W = Node.Get();
				return bOpen && W && AsdaUi::IsShown(W) && AsdaUi::WidgetAbsRect(W, Mn, Mx);
			}));
	}
}

// ★ U3 (дедуп №2, В2-A, 11.10.2026): свои `SetVis`/`IsVis` (копия `AsdaUi::ShownVisibilityFor`, урезанная до Button/CheckBox,
//   и дословная копия `AsdaUi::IsShown`) удалены — окно зовёт общие `AsdaUi::SetShown`/`AsdaUi::IsShown`. Узлы, к которым
//   правило применяется: группы-канвасы `FISH_*_grp` (PanelGrp/ImageGrp/PopupGrp/DigitsGrp, TimerGrp — их родитель),
//   картинки `SMALL_O/SMALL_X` (шаблоны и клоны), кнопки ячеек `FISH_btn_NN` (UButton); `IsShown` ещё и у BtnX (UButton),
//   ChkPause/ChkAuto (UCheckBox). Ползунков, прокруток и полей ввода среди них нет ⇒ видимость каждого узла прежняя;
//   различие только при ключе `asda.UI.InteractiveHitTest 0` — он теперь действует и здесь (как во всех окнах).

void UAsdaFishWindowSubsystem::BarSet(FAsdaFishBarUi& B, float Min, float Max, float Value)
{
	B.Min = Min; B.Max = Max; B.Value = Value;
	const float Range = Max - Min;
	const float Frac = (Range > 0.f) ? (Value - Min) / Range : 0.f;
	FAsdaGaugeFill::Clip(B.Fill, B.BaseLeft, B.FullW, Frac);
}

void UAsdaFishWindowSubsystem::DrawRect(UImage* Img, const TCHAR* Tex, int32 X, int32 Y, int32 W, int32 H)
{
	if (!Img) { return; }
	if (!AsdaUi::ApplyJuiIconRect(Img, Tex, X, Y, W, H))
	{
		UE_LOG(LogAsdaFishWin, Warning, TEXT("[окно рыбалки] текстура %s не найдена (DrawTextureRect %d,%d %d×%d)"), Tex, X, Y, W, H);
	}
}

void UAsdaFishWindowSubsystem::HideTip()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::FishWin);   // `CControl_KillFocus(&g_ItemTooltip)` — гасит только владелец
	}
}

// ═════════════════════════════════ ФУНКЦИИ ОКНА ЭТАЛОНА ═══════════════════════════════════════════

// `FishingWindow_Show@0xFC8810` (g:605631) + FSM 110 `SetVisible(71307, 1)` (g:608521).
void UAsdaFishWindowSubsystem::FishWinShow()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaFishingSubsystem* F = Fish();
	if (!EnsureWidget() || !F)
	{
		UE_LOG(LogAsdaFishWin, Error, TEXT("[окно рыбалки] FishingWindow_Show: окно не готово — рыбалка идёт без окна"));
		return;
	}
	// `sowelWindowClose(g_pInventoryWnd, 1)` — МИСНЕЙМ: закрыть инвентарь.
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr) { Inv->CloseWindow(); }
	// `SetVisible(71300, 1)` — общий механизм показа (поднимает окно поверх остальных).
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr) { WM->SetWindowVisible(kFishWindowId, true); }
	bOpen = true;                                    // byte +341
	F->SetPaused(false);                             // `g_bUiToggle71329 = 0`
	AsdaUi::SetShown(TimerGrp, false);                         // [77] 71348
	AsdaUi::SetShown(PopupGrp, false);                         // [74] 71400
	ApplyChecks();                                   // `SetVisibleState(71329, пауза)`; Р8 — 71349
	if (const FAsdaFishDataRow* Rec = F->FindFishData(F->GetSelectedFishCode()))
	{
		const TCHAR* Tex = TEXT("Ne_fish_small");   // тип 0, 4 и прочие (в т.ч. −1)
		switch (Rec->FishType)
		{
		case 1: Tex = TEXT("Ne_fish_nomal"); break;
		case 2: Tex = TEXT("Ne_fish_big"); break;
		case 3: Tex = TEXT("Ne_fish_special"); break;
		default: break;
		}
		DrawRect(FishTypeImg, Tex, 0, 0, 128, 128);   // [70] 71301
	}
	// записи нет — эталон выходит, картинка не меняется [ОРИГИНАЛ]
	AsdaUi::SetShown(ImageGrp, true);                          // FSM 110: 71307 FISH_image_1
	UE_LOG(LogAsdaFishWin, Log, TEXT("[окно рыбалки] показано (FishCode %d)"), F->GetSelectedFishCode());
}

// `uiFishingSlotInit@0xFC6E90`: totalPages = Rounds, curPage = pageBase = 0, state = 1; 71303 0…Rounds, значение 0.
void UAsdaFishWindowSubsystem::GuideStart(int32 Rounds)
{
	EnsureWidget();
	Guide.TotalPages = Rounds;
	Guide.CurPage = 0;
	Guide.State = 1;
	BarSet(RoundBar, 0.f, (float)Rounds, RoundBar.Value);   // `uiProgressBarSetRangeF(0, a2)`
	SlotSetValue(0);                                         // `uiFishingSlotSetValue(0)`
	UE_LOG(LogAsdaFishWin, Log, TEXT("[окно рыбалки] гайд: раундов %d"), Rounds);
}

// `CTutorialKeyGuide_Update@0xFC7460` (g:605225) — все ветки, числа сняты дизасмом (0xBB8 / 0x7D0).
int32 UAsdaFishWindowSubsystem::GuideTick()
{
	UAsdaFishingSubsystem* F = Fish();
	int32 Ret = 0;
	switch (Guide.State)
	{
	case 1:
		Guide.TShowStart = NowMs();
		Guide.State = 2;
		break;
	case 2:
		if (F && F->IsPaused()) { Guide.TShowStart = NowMs(); }        // пауза держит таймер
		if (NowMs() - Guide.TShowStart > (uint32)kShowMs) { Guide.State = 3; }
		break;
	case 3:
	{
		if (AsdaUi::IsShown(ImageGrp)) { AsdaUi::SetShown(ImageGrp, false); }               // `*(71307 + 292)` → скрыть
		BuildKeys();
		PanelSetVisible(true);
		SlotSetValue(Guide.CurPage + 1);
		if (RoundText) { RoundText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), Guide.CurPage + 1, Guide.TotalPages))); }
		const FAsdaFishDataRow* Rec = F ? F->FindFishData(F->GetSelectedFishCode()) : nullptr;
		Guide.RoundLimitMs = Rec ? Rec->RoundMs : UAsdaFishingSubsystem::kNoFishDataRoundMs;   // `sub_9219A8` / 30000
		Guide.TFadeStart = NowMs();
		Guide.State = 4;
		break;
	}
	case 4:
	{
		// `areAllEntriesFlagged`: count ≤ 0 → ложь.
		bool bAll = Guide.Recs.Num() > 0;
		for (const FAsdaFishKeyRec& R : Guide.Recs) { if (!R.bAnswered) { bAll = false; break; } }
		if (bAll) { Guide.State = 6; }
		UpdateTimer((int32)(NowMs() - Guide.TFadeStart));
		// Сравнение БЕЗЗНАКОВОЕ (`DWORD > DWORD`): лимит −1 — никогда.
		if (NowMs() - Guide.TFadeStart > (uint32)Guide.RoundLimitMs)
		{
			Guide.State = 6;
			for (FAsdaFishKeyRec& R : Guide.Recs)
			{
				if (!R.bAnswered) { R.bAnswered = true; R.bCorrect = false; }
			}
			PanelRefresh();
		}
		break;
	}
	case 5:
	{
		++Guide.CurPage;
		PanelSetVisible(false);
		Ret = 5;
		int32 Correct = 0;   // `countEntriesByte16Equal1`
		for (const FAsdaFishKeyRec& R : Guide.Recs) { Correct += R.bCorrect ? 1 : 0; }
		// `netSendMaskedPacket_op6169(шкала, всего = count, верно)` (порядок аргументов — дизасм 0xFC7743…0xFC7758).
		if (F) { F->SendPhaseResult(Guide.Recs.Num(), Correct); }
		Guide.State = (Guide.CurPage < Guide.TotalPages) ? 1 : 8;   // pageBase всегда 0
		UE_LOG(LogAsdaFishWin, Log, TEXT("[окно рыбалки] раунд %d/%d: всего %d, верно %d → 6169"), Guide.CurPage, Guide.TotalPages, Guide.Recs.Num(), Correct);
		break;
	}
	case 6:
		Guide.TWaitStart = NowMs();
		Guide.State = 7;
		break;
	case 7:
		if (NowMs() - Guide.TWaitStart > (uint32)kWaitMs) { Guide.State = 5; }
		break;
	case 8:
		Ret = 8;
		Guide.State = 0;
		break;
	default:
		break;
	}
	return Ret;
}

// `Input_BuildKeySchemeString@0xFC7120` + `inputBuildRandomKeyString@0xFC7030`.
void UAsdaFishWindowSubsystem::BuildKeys()
{
	Guide.Recs.Reset();   // `sub_FC6DD0`: count = 0, все записи {vk −1, 0, 0}
	UAsdaFishingSubsystem* F = Fish();
	if (!F) { return; }
	// Стрелок = `rangeTableLookupValue(Fish_num, зона+16)` (умение выбранного спота).
	int32 N = F->GetKeysPerRound();
	const FAsdaFishDataRow* Rec = F->FindFishData(F->GetSelectedFishCode());
	if (!Rec || Rec->FishType < 0 || Rec->FishType > 4) { return; }   // default: стрелок нет [ОРИГИНАЛ]
	// ★ Буфер эталона `keyBuf[5]` = 20 б и 20 записей: N > 20 писал бы за конец. Кламп (данные: везде 4).
	N = FMath::Clamp(N, 0, FAsdaFishKeyGuide::kCapacity);
	// Алфавит — всегда 4 стрелки `& % ( '` = VK 38/37/40/39, `rand() % 4` (строка-параметр — только проверка длины).
	static const int32 kAlphabet[4] = { 38, 37, 40, 39 };
	for (int32 i = 0; i < N; ++i)
	{
		FAsdaFishKeyRec R;
		R.Vk = kAlphabet[FMath::Rand() % 4];
		Guide.Recs.Add(R);
	}
}

// `uiFishingPanel_setVisible@0xFC8A20`.
void UAsdaFishWindowSubsystem::PanelSetVisible(bool bVisible)
{
	AsdaUi::SetShown(PanelGrp, bVisible);       // [4] 71310
	bPanelShown = bVisible;           // byte +340
	AsdaUi::SetShown(DigitsGrp, bVisible);      // [79] 71361
	AsdaUi::SetShown(TimerGrp, bVisible);       // [77] 71348
	if (bVisible) { PanelRefresh(); }
}

// `uiFishingPanel_Refresh@0xFC8C50` (g:605746).
void UAsdaFishWindowSubsystem::PanelRefresh()
{
	if (!bControlsReady) { return; }
	for (FAsdaFishCellUi& C : Cells)
	{
		AsdaUi::SetShown(C.Btn, false); AsdaUi::SetShown(C.O, false); AsdaUi::SetShown(C.X, false);
		AsdaUi::ClearImageBrush(C.Icon);   // пустая текстура `GetSubObjectAtFixedOff(g_pGameClient)`
	}
	const int32 N = Guide.Recs.Num();
	// ★ Ячеек на окне 15, записей до 20 — эталон при N > 15 писал бы в чужие поля окна; рисуем первые 15.
	for (int32 j = 0; j < FMath::Min(N, kCells); ++j)
	{
		const FAsdaFishKeyRec& R = Guide.Recs[j];
		AsdaUi::SetShown(Cells[j].Btn, true);
		if (R.bAnswered)
		{
			AsdaUi::SetShown(Cells[j].O, R.bCorrect);
			AsdaUi::SetShown(Cells[j].X, !R.bCorrect);
		}
		int32 Cell = 0;   // '&' 38 ↑ → 0, '%' 37 ← → 1, '(' 40 ↓ → 2, ''' 39 → → 3 (X/цифры не генерируются)
		switch (R.Vk)
		{
		case 37: Cell = 1; break;
		case 38: Cell = 0; break;
		case 39: Cell = 3; break;
		case 40: Cell = 2; break;
		case '0': Cell = 12; break;
		case '1': Cell = 5; break; case '2': Cell = 6; break; case '3': Cell = 7; break; case '4': Cell = 8; break;
		case '5': Cell = 9; break; case '6': Cell = 10; break; case '7': Cell = 11; break;
		case 'X': Cell = 4; break;
		default: break;
		}
		DrawRect(Cells[j].Icon, TEXT("fish_gfont"), 32 * Cell, 0, 32, 32);
	}
	// Центрирование: `SetField74_75(71310, 100)`, затем x = 100 − (u64)((n−1)·61·0.5) (усечение `__ftol2`).
	const int32 X = 100 - (int32)(int64)((double)(N - 1) * 61.0 * 0.5);
	if (UCanvasPanelSlot* S = AsdaUi::SlotOf(PanelGrp))
	{
		FMargin O = S->GetOffsets();
		O.Left = PanelRootLeft + (float)X;
		S->SetOffsets(O);
	}
	// SetSize (vtbl+0x14 `uiPlaneOffsetChildRects`: ширина СОБСТВЕННЫХ планов; дети, в т.ч. 71309, не меняются).
	if (UCanvasPanelSlot* S = AsdaUi::SlotOf(PanelBbPlane))
	{
		FMargin O = S->GetOffsets(); O.Right = (float)(int32)(int64)((double)N * 58.700001 + 12.0); S->SetOffsets(O);
	}
	if (UCanvasPanelSlot* S = AsdaUi::SlotOf(PanelPlane))
	{
		FMargin O = S->GetOffsets(); O.Right = (float)(int32)(int64)((double)N * 58.700001 + 20.0); S->SetOffsets(O);
	}
}

// `uiFishingPanelClearSlot@0xFC89C0` (g:605673).
void UAsdaFishWindowSubsystem::PanelClearSlot()
{
	SlotSetValue(0);                                       // 71303 = 0, 71346 = ""
	BarSet(GaugeBar, GaugeBar.Min, GaugeBar.Max, 0.f);     // `sub_93D51D(0)` — 71305 = 0 (рыбка не двигается)
	if (bOpen)
	{
		if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			WM->SetWindowVisible(kFishWindowId, false);    // `SetVisible(71300, 0)`
		}
	}
	bOpen = false;                                         // byte +341
	HideTip();
	HoverId = 0;
}

// `uiFishingSlotSetValue@0xFC8C00`.
void UAsdaFishWindowSubsystem::SlotSetValue(int32 V)
{
	if (V == 0 && RoundText) { RoundText->SetText(FText::GetEmpty()); }
	BarSet(RoundBar, RoundBar.Min, RoundBar.Max, (float)V);
}

// `fishingUpdateTimerBar@0xFC9520` (g:605946).
void UAsdaFishWindowSubsystem::UpdateTimer(int32 ElapsedMs)
{
	const int32 Limit = Guide.RoundLimitMs;
	// ★ Р9 (без ключа): остаток < 0 эталон приводит к u64 — мусор в цифрах; кламп к 0.
	const int32 Remain = FMath::Max(0, Limit - ElapsedMs);
	BarSet(TimerBar, 0.f, (float)Limit, (float)Remain);
	const int32 Sec = FMath::Min(Remain / 1000, 99);
	const int32 Cs = FMath::Min((Remain % 1000) / 10, 99);
	const TCHAR* Tex = TEXT("fish_gfont2");   // цифра d → x = 16·d, клетка 16×22
	// Кадры трёх цифр: секунды «0d» / две цифры; сотые — одна → «0», две → первая.
	const int32 X1 = (Sec < 10) ? 0 : 16 * (Sec / 10);
	const int32 X2 = (Sec < 10) ? 16 * Sec : 16 * (Sec % 10);
	const int32 X3 = (Cs < 10) ? 0 : 16 * (Cs / 10);
	// ★ Ревизия В2 МЕЛК-7 (волна В4): эталон перерисовывает каждый кадр; порт меняет кисть только при смене кадра цифры
	//   (новая кисть = инвалидация Slate). Результат на экране тот же.
	if (X1 != LastTimerDigitX[0]) { DrawRect(Digit1, Tex, X1, 0, 16, 22); LastTimerDigitX[0] = X1; }
	if (X2 != LastTimerDigitX[1]) { DrawRect(Digit2, Tex, X2, 0, 16, 22); LastTimerDigitX[1] = X2; }
	if (X3 != LastTimerDigitX[2]) { DrawRect(Digit3, Tex, X3, 0, 16, 22); LastTimerDigitX[2] = X3; }
}

// `gauge_SetValue@0xFC81D0` (g:605542): рыбка 71326 x = (barW / max · value − aniW) + 8; 71305 0…max = value.
void UAsdaFishWindowSubsystem::OnGaugeChanged(float Value, float Max)
{
	if (!EnsureWidget()) { return; }
	const double V3 = (Max > 0.f) ? (double)GaugeExtentW / (double)Max * (double)Value : 0.0;
	const int32 X = (int32)(int64)(V3 - (double)AniExtentW) + 8;
	if (UCanvasPanelSlot* S = AsdaUi::SlotOf(FishAni))
	{
		FMargin O = S->GetOffsets();
		O.Left = AniBaseLeft + (float)X;   // `SetField74_75(71326, x, −1)` — Y не меняется
		S->SetOffsets(O);
	}
	BarSet(GaugeBar, 0.f, (float)(int32)Max, (float)(int32)Value);   // `setProgressBarRange(0, (u64)max)` + `SetValue((u64)value)`
}

// `uiDrawPickedItemIcon@0xFC98C0` (g:606018).
void UAsdaFishWindowSubsystem::ShowCatchPopup(int32 CaughtItemId)
{
	if (!EnsureWidget()) { return; }
	UGameInstance* GI = GetGameInstance();
	AsdaUi::SetShown(PopupGrp, true);
	AsdaUi::ClearImageBrush(PopupIcon);
	UAsdaRichText::SetFmt(PopupText, FString());
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(CaughtItemId) : nullptr;
	if (!Rec) { return; }
	if (const UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>())
	{
		Inv->ApplyIconToImage(PopupIcon, CaughtItemId, 64.f);   // img-опция предмета → `DrawTextureRect(71404, …)`
	}
	const UAsdaFishingSubsystem* F = Fish();
	const int32 SizeCm = F ? F->GetCaughtSizeCm() : 0;      // `word_352034D`
	const FString Name = IT->GetItemName(CaughtItemId);    // `ItemData_FindByCode(itemCode)`
	const FString Text = SizeCm
		? AsdaUi::FormatPrintfArgs(AsdaNotice::LocRaw(GI, FishWinLoc::kCatchSize), { Name, FString::FromInt(SizeCm) })
		: AsdaUi::FormatPrintfArgs(AsdaNotice::LocRaw(GI, FishWinLoc::kCatchName), { Name });
	UAsdaRichText::SetFmt(PopupText, Text);
	UE_LOG(LogAsdaFishWin, Log, TEXT("[окно рыбалки] всплывашка улова: %s"), *Text);
}

bool UAsdaFishWindowSubsystem::IsCatchPopupShown() const
{
	return bControlsReady && AsdaUi::IsShown(PopupGrp);   // `*(dword_3520C1C + 292)`
}

void UAsdaFishWindowSubsystem::HideCatchPopup()
{
	AsdaUi::SetShown(PopupGrp, false);   // `sub_FC9B70`
}

void UAsdaFishWindowSubsystem::PanelHideAndClear()
{
	// ★ Ревизия В2 КРИТ-1 [ИСПРАВЛЕНИЕ] (`asda.Fish.LegacyStaleGuide 0`): конец рыбалки гасит гайд (эталон — нет, см. ключ).
	if (CVarFishLegacyStaleGuide.GetValueOnGameThread() == 0) { Guide.State = 0; }
	if (!bControlsReady) { bOpen = false; bPanelShown = false; return; }
	PanelSetVisible(false);
	PanelClearSlot();
}

// ★ Ревизия В2 КРИТ-1: состояние ввода стрелок — только пока идёт рыбалка (ключ 1 — как оригинал, без проверки).
bool UAsdaFishWindowSubsystem::IsGuideInputState() const
{
	if (Guide.State != 4) { return false; }
	if (CVarFishLegacyStaleGuide.GetValueOnGameThread() != 0) { return true; }
	return UAsdaFishingSubsystem::IsFishingNow(GetGameInstance());   // ★ В3 S7 (N14): одна точка
}

// ═════════════════════════════════ ВВОД ═══════════════════════════════════════════════════════════

void UAsdaFishWindowSubsystem::OnGuideKeyDown(int32 Vk)
{
	if (Guide.State != 4) { return; }
	FAsdaFishKeyRec* R = nullptr;   // `slotArrayFindFirstFree`: первая с answered == 0
	for (FAsdaFishKeyRec& E : Guide.Recs) { if (!E.bAnswered) { R = &E; break; } }
	if (!R) { return; }
	R->bAnswered = true;
	if (Vk == 104) { Vk = 38; }   // VK_NUMPAD8 → ↑
	if (Vk == 100) { Vk = 37; }   // VK_NUMPAD4 → ←
	if (Vk == 98)  { Vk = 40; }   // VK_NUMPAD2 → ↓
	if (Vk == 102) { Vk = 39; }   // VK_NUMPAD6 → →
	R->bCorrect = (Vk == R->Vk);
	UGameInstance* GI = GetGameInstance();
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound2D(FName(R->bCorrect ? TEXT("s_e_fishing_bto_00") : TEXT("s_e_fishing_btx_00")));
	}
	PanelRefresh();
	UE_LOG(LogAsdaFishWin, Log, TEXT("[окно рыбалки] ответ VK %d: %s"), Vk, R->bCorrect ? TEXT("верно") : TEXT("неверно"));
}

// ═════════════════════════════════ КНОПКИ ═════════════════════════════════════════════════════════

void UAsdaFishWindowSubsystem::OnCancelPressed()
{
	if (UAsdaFishingSubsystem* F = Fish()) { F->RequestCancelDialog80(); }   // `CItem_BuildTooltipText(…, 1, 80, "", 1)`
}

// Событие 0xB по 71329 (g:605911-605930). Нотисы — `CControl_SetTooltipText(g_pChatWnd, …, −11514, 10000)` = плашка 221.
void UAsdaFishWindowSubsystem::OnPausePressed()
{
	UAsdaFishingSubsystem* F = Fish();
	if (!F) { return; }
	if (F->IsPaused())
	{
		F->SetPaused(false);
		F->NoticePlankLoc(FishWinLoc::kResumed);
	}
	else if (F->GetFishState() == 115 && Guide.State == 2)   // `dword_351FBA0` = гайд +0x118
	{
		F->NoticePlankLoc(FishWinLoc::kPaused);
		F->SetPaused(true);
	}
	else
	{
		F->NoticePlankLoc(FishWinLoc::kCantPause);
	}
	ApplyChecks();   // `SetVisibleState(71329, пауза)`
}

// Событие 0xB по 71349: `AR = !AR; SetVisibleState(71349, AR == 0)` — Р8.
void UAsdaFishWindowSubsystem::OnAutoRepeatPressed()
{
	UAsdaFishingSubsystem* F = Fish();
	if (!F) { return; }
	F->SetAutoRepeat(!F->GetAutoRepeat());
	bLegacyAutoVisual = !F->GetAutoRepeat();   // эталонное состояние галки (видно при ключе 1)
	ApplyChecks();
	UE_LOG(LogAsdaFishWin, Log, TEXT("[окно рыбалки] автоповтор %s"), F->GetAutoRepeat() ? TEXT("ВКЛ") : TEXT("ВЫКЛ"));
}

void UAsdaFishWindowSubsystem::ApplyChecks()
{
	const UAsdaFishingSubsystem* F = Fish();
	if (!F) { return; }
	if (ChkPause) { ChkPause->SetIsChecked(F->IsPaused()); }
	if (ChkAuto)
	{
		const bool bLegacy = CVarFishLegacyAutoCheck.GetValueOnGameThread() != 0;
		ChkAuto->SetIsChecked(bLegacy ? bLegacyAutoVisual : F->GetAutoRepeat());   // ★ Р8 [ПОРТ]
	}
}

void UAsdaFishWindowSubsystem::OnPauseCheckChanged(bool /*bChecked*/)
{
	ApplyChecks();   // галка сама не переключается (+609 = 1)
}

void UAsdaFishWindowSubsystem::OnAutoCheckChanged(bool /*bChecked*/)
{
	ApplyChecks();
}

// Событие 9 — `tutorialPopupLayoutSmall(&g_ItemTooltip, 111096, тело, −1, −1)` + `Tooltip_LayoutNearCursor`; 0xA — скрыть.
void UAsdaFishWindowSubsystem::OnHoverControl(int32 IdNum)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Tip) { return; }
	int32 Body = 0;
	if (IdNum == kIdCancel) { Body = FishWinLoc::kTipCancel; }
	if (IdNum == kIdPause)  { Body = FishWinLoc::kTipPause; }
	if (IdNum == kIdAuto)   { Body = FishWinLoc::kTipAuto; }
	if (Body == 0) { Tip->HideTooltipFor(EAsdaTipOwner::FishWin); return; }
	FAsdaTipMetrics M;
	M.WidthAdd = 27.f;   // «маленькая» всплывашка эталона (`tutorialPopupLayoutSmall`)
	M.ExtraLines = 0;
	Tip->ShowTextTip(EAsdaTipOwner::FishWin, AsdaNotice::LocRaw(GI, FishWinLoc::kTipTitle), AsdaNotice::LocRaw(GI, Body), M);
}

// ═════════════════════════════════ ДИАГНОСТИКА ═══════════════════════════════════════════════════

FString UAsdaFishWindowSubsystem::DumpControls() const
{
	FString S = FString::Printf(TEXT("=== окно рыбалки 71300: разобранные контролы (готово=%d, открыто=%d) ===\n"), bControlsReady ? 1 : 0, bOpen ? 1 : 0);
	auto Line = [&S](const FString& Id, const UWidget* W)
	{
		FVector2D Mn = FVector2D::ZeroVector, Mx = FVector2D::ZeroVector;
		const bool bGeo = W && AsdaUi::WidgetAbsRect(W, Mn, Mx);
		S += FString::Printf(TEXT("  %-40s %s вид=%d rect=%s\n"), *Id, W ? *W->GetName() : TEXT("НЕТ"), AsdaUi::IsShown(W) ? 1 : 0,
			bGeo ? *FString::Printf(TEXT("(%.1f,%.1f)-(%.1f,%.1f)"), Mn.X, Mn.Y, Mx.X, Mx.Y) : TEXT("нет"));
	};
	Line(TEXT("71300 FISH_WIN (корень)"), Widget);
	Line(TEXT("71302 Gfont_fishing"), HeaderGrp);
	Line(TEXT("71303 FISH_bar_1 подложка"), RoundBar.Back);
	Line(TEXT("71303 FISH_bar_1 заливка"), RoundBar.Fill);
	Line(TEXT("71346 FISH_BAR_1_TEXT"), RoundText);
	Line(TEXT("71304 полоса (группа)"), BarGrp);
	Line(TEXT("71305 FISH_bar_2 подложка"), GaugeBar.Back);
	Line(TEXT("71305 FISH_bar_2 заливка"), GaugeBar.Fill);
	Line(TEXT("71326 FISH_ANI"), FishAni);
	Line(TEXT("71307 FISH_image_1"), ImageGrp);
	Line(TEXT("71301 FISH_image_M"), FishTypeImg);
	Line(TEXT("71310 FISH_btn_00 (группа)"), PanelGrp);
	Line(TEXT("71310 FISH_btn_00 план"), PanelPlane);
	Line(TEXT("71308 FISH_btn_bb план"), PanelBbPlane);
	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		Line(FString::Printf(TEXT("%d FISH_btn_%02d"), 71311 + i, i + 1), Cells[i].Btn);
		Line(FString::Printf(TEXT("%d FISH_FFimgae_m%d"), 71331 + i, i + 1), Cells[i].Icon);
		Line(FString::Printf(TEXT("клон O ячейки %d"), i + 1), Cells[i].O);
		Line(FString::Printf(TEXT("клон X ячейки %d"), i + 1), Cells[i].X);
	}
	Line(TEXT("71356 SMALL_O шаблон"), TmplO);
	Line(TEXT("71355 SMALL_X шаблон"), TmplX);
	Line(TEXT("71348 таймер (группа)"), TimerGrp);
	Line(TEXT("71347 FISH_count_1_bar заливка"), TimerBar.Fill);
	Line(TEXT("71361 цифры (группа)"), DigitsGrp);
	Line(TEXT("71361 FISH_count_1"), Digit1);
	Line(TEXT("71362 FISH_count_2"), Digit2);
	Line(TEXT("71363 FISH_count_3"), Digit3);
	Line(TEXT("71400 всплывашка (группа)"), PopupGrp);
	Line(TEXT("71404 иконка улова"), PopupIcon);
	Line(TEXT("71401 текст улова"), PopupText.Node);
	Line(TEXT("71328 FISH_Xbtn"), BtnX);
	Line(TEXT("71329 FISHI_PLAY_STOP_btn"), ChkPause);
	Line(TEXT("71349 FISHI_AR_Check"), ChkAuto);
	return S;
}

FString UAsdaFishWindowSubsystem::DumpState() const
{
	FString Keys;
	for (const FAsdaFishKeyRec& R : Guide.Recs)
	{
		Keys += FString::Printf(TEXT(" %d%s"), R.Vk, R.bAnswered ? (R.bCorrect ? TEXT("+") : TEXT("-")) : TEXT("?"));
	}
	const UAsdaFishingSubsystem* F = Fish();
	return FString::Printf(TEXT("=== окно рыбалки: открыто %d, панель %d, гайд state %d, раунд %d/%d, лимит %d мс, стрелки [%s ], ")
		TEXT("пауза %d, автоповтор %d, зарегистрировано %d ==="),
		bOpen ? 1 : 0, bPanelShown ? 1 : 0, Guide.State, Guide.CurPage, Guide.TotalPages, Guide.RoundLimitMs, *Keys,
		F && F->IsPaused() ? 1 : 0, F && F->GetAutoRepeat() ? 1 : 0, (F && F->GetRegisteredMiniGame() == this) ? 1 : 0);
}

// ═════════════════════════════════ КОНСОЛЬ ═══════════════════════════════════════════════════════

// Подсистема окна мира W (nullptr — нет).
static UAsdaFishWindowSubsystem* FishWinOf(UWorld* W)
{
	return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaFishWindowSubsystem>() : nullptr;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GFishWinDump(
	TEXT("asda.Fish.WinDump"),
	TEXT("Окно рыбалки 71300: КАЖДЫЙ разобранный контрол с живой геометрией (GetCachedGeometry) + состояние гайда"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaFishWindowSubsystem* S = FishWinOf(W)) { UE_LOG(LogAsdaFishWin, Log, TEXT("\n%s\n%s"), *S->DumpControls(), *S->DumpState()); }
	}));
#endif // ASDA_DEV_CONSOLE
