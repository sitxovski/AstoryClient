// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaFishWindowSubsystem — ОКНО РЫБАЛКИ FISH_WIN (JUI IDNum 71300) И МИНИ-ИГРА «СТРЕЛОЧКИ» (волна В2, 08.10.2026).
//   Реверс (источник истины): `Reports/owner_1008/fishing_parts/ui_fishwin.md` (§1 поля, §2 окно, §3 ввод, §4 гайд),
//   сводка `Reports/owner_1008/fishing_reverse.md` §2-§3, §5, §8; поправки В1 — `fishing_impl_W1.md` §3.
//   Отчёт волны: `Reports/owner_1008/fishing_impl_W2.md`.
//
// ═════════════════════ ЧТО ЭТО В ЭТАЛОНЕ ═══════════════════════════════════════════════════════════════════════
//   • Окно — объект `g_pFishingWindow` (0x3520AF4), его поля ([0]…[84]) заполняет `fishingWindowInitControls@0xFC83E0`
//     (g:605579). Функции: `FishingWindow_Show@0xFC8810`, `uiFishingPanelClearSlot@0xFC89C0`,
//     `uiFishingPanel_setVisible@0xFC8A20`, `uiFishingPanel_Refresh@0xFC8C50`, `fishingUpdateTimerBar@0xFC9520`,
//     `uiDrawPickedItemIcon@0xFC98C0`, `sub_FC9B70` (спрятать 71400), шкала `gauge_SetValue@0xFC81D0`,
//     события `uiControlEventDispatch@0xFC9110` (g:605840).
//   • Мини-игра — «ключ-гайд» `CTutorialKeyGuide` по адресу 0x351FA88 (`g_pFishingUICtx`, МИСНЕЙМ): старт
//     `uiFishingSlotInit@0xFC6E90` (МИСНЕЙМ), тик `CTutorialKeyGuide_Update@0xFC7460` (g:605225), генерация
//     `Input_BuildKeySchemeString@0xFC7120` + `inputBuildRandomKeyString@0xFC7030`, ввод `Input_FishingKeyFilter@0xFC6F10`.
//   • Автомат рыбалки (В1, `UAsdaFishingSubsystem`) зовёт окно ровно в тех состояниях, что эталон, через интерфейс
//     `IAsdaFishMiniGame`; эта подсистема его реализует и регистрирует себя (`SetMiniGame`), эмулятор В1 при этом не работает.
//
// ═════════════════════ ВВОД ═══════════════════════════════════════════════════════════════════════════════════
//   Эталон: `MainWndProc` WM_KEYDOWN первым делом зовёт фильтр гайда (state 4 — ЛЮБАЯ клавиша = ответ, автоповтор не
//   отсекается, нумпад 8/4/2/6 = стрелки), и при fishState ≠ 0 клавиша не идёт ни в окна, ни в хоткеи; WM_KEYUP — тоже,
//   кроме PrintScreen/Esc/Enter. В порту это фильтр `AAsdaPlayerController::InputKey` (до `UPlayerInput`), он зовёт
//   `OnGuideKeyDown` этой подсистемы.
//
// ═════════════════════ РАЗМЕТКА ═══════════════════════════════════════════════════════════════════════════════
//   `WBP_FISH_WIN` собран импортёром (174 узла, полный дамп — отчёт В2 §1). Клоны O/X ячеек (`CControl_InstantiateChild`
//   эталона) положены В АССЕТ шагом пост-импорта `Tools/asda_ui_fish_ox_clones.py` — кодом-геометрией окно не правится.
//   Код делает ТОЛЬКО то, что делает код эталона: показ/скрытие контролов, кадры атласов (`DrawTextureRect`),
//   центрирование панели стрелок и ширины её рамок (`uiFishingPanel_Refresh`: SetPos/SetSize), позиция рыбки
//   (`gauge_SetValue`: SetPos), заливка полос-прогрессбаров.
//
// ═════════════════════ ОТСТУПЛЕНИЯ [ПОРТ] (ключи) ═════════════════════════════════════════════════════════════
//   Р8 `asda.Fish.LegacyAutoCheck` 0 — галка 71349 рисует ФАКТИЧЕСКОЕ состояние автоповтора (1 — инверсия оригинала);
//   Р9 — таймер раунда клампится к 0 (без ключа); раунд с > 20 стрелками (буфер 20 б эталона) клампится к 20 (без ключа).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "UI/AsdaMarkup.h"                 // FAsdaFmtText — строка с тегами <FC=…> (текст улова 71401)
#include "AsdaSessionReset.h"              // IAsdaSessionResettable — сброс состояния сессии
#include "World/AsdaFishingSubsystem.h"    // IAsdaFishMiniGame — контракт с автоматом В1
#include "AsdaFishWindowSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UCheckBox;
class UTextBlock;
class UAsdaButtonRelay;

// Запись ключ-гайда (12 б в эталоне, `+0x08 + 12·i`): индекс, VK требуемой клавиши, «верно», «отвечено».
struct FAsdaFishKeyRec
{
	int32 Vk = -1;            // +4 VK требуемой клавиши (−1 — пусто): 38 ↑, 37 ←, 40 ↓, 39 →
	bool bCorrect = false;    // +8 byte «верно»
	bool bAnswered = false;   // +9 byte «отвечено»
};

// Ключ-гайд `CTutorialKeyGuide` (0x351FA88) — поля, которые читает/пишет мини-игра.
struct FAsdaFishKeyGuide
{
	static constexpr int32 kCapacity = 20;   // +0x04 ёмкость = 20 (буфер `keyBuf[5]` = 20 б)
	TArray<FAsdaFishKeyRec> Recs;            // +0x08 записи; Num() = +0x00 count (стрелок в раунде)
	int32 TotalPages = 0;                    // +0xFC число раундов
	int32 CurPage = 0;                       // +0x100 текущий раунд (pageBase +0x104 всегда 0)
	uint32 TFadeStart = 0;                   // +0x110 начало ввода (state 3 → 4)
	uint32 TShowStart = 0;                   // +0x114 начало паузы (state 1/2)
	uint32 TWaitStart = 0;                   // +0x11C начало показа результата (state 6/7)
	int32 State = 0;                         // +0x118 состояние 0…8
	int32 RoundLimitMs = 0;                  // `g_InventoryUI`+0 (МИСНЕЙМ) — лимит раунда, мс (ставит state 3)
};

// Виджеты одной ячейки панели стрелок (i = 0…14): подложка BLUE_BIG_BTN, кадр стрелки, клоны O/X.
USTRUCT()
struct FAsdaFishCellUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UButton> Btn = nullptr;   // [5+4i] 71311+i `FISH_btn_NN` (BLUE_BIG_BTN 56×56)
	UPROPERTY(Transient) TObjectPtr<UImage>  Icon = nullptr;  // [6+4i] 71331+i `FISH_FFimgae_mN_<n>_0` (32×32)
	UPROPERTY(Transient) TObjectPtr<UImage>  O = nullptr;     // [7+4i] клон 71356 SMALL_O (`FISH_SMALL_O_CLONENN`)
	UPROPERTY(Transient) TObjectPtr<UImage>  X = nullptr;     // [8+4i] клон 71355 SMALL_X (`FISH_SMALL_X_CLONENN`)
};

// Полоса-прогрессбар JUI (ProgressType 5): подложка ES0 и заливка ES1 (ширина = доля × ширина подложки).
USTRUCT()
struct FAsdaFishBarUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UImage> Back = nullptr;   // план ES0 (`<idc>_<n>_0`)
	UPROPERTY(Transient) TObjectPtr<UImage> Fill = nullptr;   // план ES1 (`<idc>_<n>_1`)
	float BaseLeft = 0.f;                                     // левый край (по подложке)
	float FullW = 0.f;                                        // полная ширина (по подложке)
	float Min = 0.f;                                          // `CProgressBar_SetRange` минимум
	float Max = 1.f;                                          // `CProgressBar_SetRange` максимум
	float Value = 0.f;                                        // `CProgressBar_SetValue`
};

UCLASS()
class ASDAREBORN_API UAsdaFishWindowSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable, public IAsdaFishMiniGame
{
	GENERATED_BODY()

	friend struct FAsdaFishWinSelfTest;   // самотест `asda.Fish.WinSelfTest` (часы, гайд, виджеты)
	friend struct FAsdaFishZoneSelfTest;  // ★ 08.10.2026: самотест зон и гейтов В4 `asda.Fish.ZoneSelfTest` (состояние автомата/гайда)

public:
	// Поднять подсистему и зарегистрировать себя мини-игрой автомата рыбалки.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Снять регистрацию, сбросить виджеты.
	virtual void Deinitialize() override;
	// IAsdaSessionResettable: окно и гайд принадлежат сессии (автомат В1 сбрасывается сам, `OnFishSessionReset`).
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;
	// Покадровый опрос мыши (наведение/нажатие 71329/71349, подсказки). Мини-игру ведёт автомат (FSM 115 → GuideTick).
	virtual void Tick(float DeltaTime) override;
	// Стат тикера.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaFishWindowSubsystem, STATGROUP_Tickables); }
	// В редакторе не тикаем.
	// ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	// На паузе не тикаем.
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ═════════════════════ IAsdaFishMiniGame (зовёт автомат В1) ═════════════════════
	// FSM 110: `FishingWindow_Show@0xFC8810` + `SetVisible(71307, 1)` (g:608519-608521).
	virtual void FishWinShow() override;
	// FSM 114: `uiFishingSlotInit@0xFC6E90` — старт гайда на Rounds раундов.
	virtual void GuideStart(int32 Rounds) override;
	// FSM 115: `CTutorialKeyGuide_Update@0xFC7460` → 0 / 5 / 8.
	virtual int32 GuideTick() override;
	// FSM 120: видна ли всплывашка 71400.
	virtual bool IsCatchPopupShown() const override;
	// FSM 120: `uiDrawPickedItemIcon@0xFC98C0`.
	virtual void ShowCatchPopup(int32 CaughtItemId) override;
	// FSM 120 → 121: `sub_FC9B70` — спрятать 71400.
	virtual void HideCatchPopup() override;
	// FSM 128: `uiFishingPanel_setVisible(0)` + `uiFishingPanelClearSlot@0xFC89C0`.
	virtual void PanelHideAndClear() override;
	// `gauge_SetValue@0xFC81D0`: рыбка 71326 и полоса 71305.
	virtual void OnGaugeChanged(float Value, float Max) override;
	// Сброс сессии автомата (`UAsdaFishingSubsystem::ResetSessionState`): гайд в 0, окно скрыто.
	virtual void OnFishSessionReset(bool bFull) override;

	// ═════════════════════ ВВОД (фильтр `AAsdaPlayerController::InputKey`) ═════════════════════
	// Гайд в состоянии 4 (окно ввода стрелок) — `Input_FishingKeyFilter`: `*(this+70) == 4`.
	//   ★ Ревизия В2 КРИТ-1 [ИСПРАВЛЕНИЕ] (`asda.Fish.LegacyStaleGuide`): и только пока идёт рыбалка (fishState ≠ 0).
	bool IsGuideInputState() const;
	// Сырое состояние гайда (самотест: «после отмены гайд погашен»).
	int32 GetGuideState() const { return Guide.State; }
	// `Input_FishingKeyFilter@0xFC6F10` ветка WM_KEYDOWN: первая неотвеченная запись ← ответ (нумпад 104/100/98/102 →
	//   38/37/40/39), «верно» — `bto_00`, иначе `btx_00`, перерисовка панели. Нет свободной записи — ничего.
	void OnGuideKeyDown(int32 Vk);

	// ═════════════════════ КНОПКИ (события окна, `uiControlEventDispatch@0xFC9110`) ═════════════════════
	// 71328 X (66561) / ESC (0x15) → диалог 80 «Прекратить рыбалку?».
	void OnCancelPressed();
	// 71329 пауза (0xB): снять / поставить (fs 115 и гайд 2) / «нельзя»; галка = пауза.
	void OnPausePressed();
	// 71349 автоповтор (0xB): переключить; галка — Р8.
	void OnAutoRepeatPressed();
	// Наведение (9) на 71328/71329/71349 — подсказка 111096 + 111064/111065/111066; 0 — уход (0xA).
	void OnHoverControl(int32 IdNum);

	// ═════════════════════ ДИАГНОСТИКА ═════════════════════
	// Дамп КАЖДОГО разобранного контрола с живой геометрией (`GetCachedGeometry`).
	FString DumpControls() const;
	// Состояние гайда/окна одной простынёй.
	FString DumpState() const;
	// Окно показано (`g_pFishingWindow` byte +341).
	bool IsOpen() const { return bOpen; }
	// Панель стрелок видна (byte +340).
	bool IsPanelShown() const { return bPanelShown; }

	// ═════════════════════ ЧИСЛА ЭТАЛОНА ═════════════════════
	static constexpr int32 kCells = 15;             // ячеек 71311…71325 / 71331…71345
	static constexpr int32 kShowMs = 3000;          // гайд state 2: пауза перед раундом (0xFC74F8 `cmp eax,0xbb8`)
	static constexpr int32 kWaitMs = 2000;          // гайд state 7: показ результата (0xFC76FF `cmp eax,0x7d0`)
	static constexpr int32 kPanelJuiX = -316;       // Origin 71310 по X в `.JUI` (относительно FISH_WIN)
	static constexpr int32 kAniJuiX = 5;            // Origin 71326 по X в `.JUI` (относительно 71304)
	static constexpr int32 kZ = 731;                // z-порядок окна (полоса обычных окон; подъём при показе — общий)

private:
	// ── окно ──
	// Окно есть (кэш годен или только что создано) и контролы разобраны.
	bool EnsureWidget();
	// Снести кэш виджетов.
	void ResetWidget();
	// Полный структурный разбор (`fishingWindowInitControls`): поиск всех контролов по именам импортёра.
	void SetupControls();
	// Зоны интерфейса окна (видимые группы — клик по ним не уходит в мир).
	void RegisterZones();
	// ★ U3 (дедуп №2, В2-A, 11.10.2026): свои `SetVis`/`IsVis` убраны — видимость узлов окна через `AsdaUi::SetShown`/`IsShown`.

	// ── функции окна эталона ──
	// `uiFishingPanel_setVisible@0xFC8A20`.
	void PanelSetVisible(bool bVisible);
	// `uiFishingPanel_Refresh@0xFC8C50`.
	void PanelRefresh();
	// `uiFishingPanelClearSlot@0xFC89C0`.
	void PanelClearSlot();
	// `fishingUpdateTimerBar@0xFC9520` (Р9: остаток < 0 → 0).
	void UpdateTimer(int32 ElapsedMs);
	// `uiFishingSlotSetValue@0xFC8C00`: 0 → текст 71346 ""; значение полосы 71303.
	void SlotSetValue(int32 V);
	// `Input_BuildKeySchemeString@0xFC7120`: сброс записей + стрелки по Fish_num зоны и типу рыбы.
	void BuildKeys();
	// Галки (`CControl_SetVisibleState`): 71329 = пауза, 71349 — по Р8.
	void ApplyChecks();
	// Полоса: диапазон/значение и заливка (ширина = доля).
	static void BarSet(FAsdaFishBarUi& B, float Min, float Max, float Value);
	// Кадр атласа `DrawTextureRect(ctrl, tex, x, y, w, h)`.
	static void DrawRect(UImage* Img, const TCHAR* Tex, int32 X, int32 Y, int32 W, int32 H);
	// Время автомата (`timeGetTime()` с подменой часов самотеста).
	uint32 NowMs() const;
	// Автомат рыбалки (В1).
	UAsdaFishingSubsystem* Fish() const;
	// Подсказку снять (если она наша).
	void HideTip();

	// ── обработчики UMG ──
	UFUNCTION() void OnPauseCheckChanged(bool bChecked);   // галка сама не переключается (+609 = 1): вернуть код
	UFUNCTION() void OnAutoCheckChanged(bool bChecked);    // то же для 71349

	// ═════════════════════ ВИДЖЕТЫ (поля `g_pFishingWindow`) ═════════════════════
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;          // [0] 71300 FISH_WIN (корень)
	UPROPERTY(Transient) TObjectPtr<UWidget> HeaderGrp = nullptr;           // [1] 71302 Gfont_fishing (группа)
	UPROPERTY(Transient) FAsdaFishBarUi RoundBar;                           // [2] 71303 FISH_bar_1 (раунды)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RoundText = nullptr;        // [3] 71346 FISH_BAR_1_TEXT «%d/%d»
	UPROPERTY(Transient) TObjectPtr<UWidget> PanelGrp = nullptr;            // [4]/[68] 71310 FISH_btn_00 (группа панели)
	UPROPERTY(Transient) TObjectPtr<UImage> PanelPlane = nullptr;           // 71310 собственный план (ширина n·58.7+20)
	UPROPERTY(Transient) TObjectPtr<UImage> PanelBbPlane = nullptr;         // [67] 71308 FISH_btn_bb план (n·58.7+12)
	UPROPERTY(Transient) TArray<FAsdaFishCellUi> Cells;                     // 15 ячеек
	UPROPERTY(Transient) TObjectPtr<UImage> TmplO = nullptr;                // шаблон 71356 SMALL_O (скрыт в init)
	UPROPERTY(Transient) TObjectPtr<UImage> TmplX = nullptr;                // шаблон 71355 SMALL_X (скрыт в init)
	UPROPERTY(Transient) TObjectPtr<UWidget> ImageGrp = nullptr;            // [69] 71307 FISH_image_1 (группа)
	UPROPERTY(Transient) TObjectPtr<UImage> FishTypeImg = nullptr;          // [70] 71301 FISH_image_M (128×128)
	UPROPERTY(Transient) TObjectPtr<UWidget> BarGrp = nullptr;              // [71] 71304 нижняя полоса (группа)
	UPROPERTY(Transient) FAsdaFishBarUi GaugeBar;                           // [72] 71305 FISH_bar_2 (шкала 0…500)
	UPROPERTY(Transient) TObjectPtr<UImage> FishAni = nullptr;              // [73] 71326 FISH_ANI (рыбка)
	UPROPERTY(Transient) TObjectPtr<UWidget> PopupGrp = nullptr;            // [74] 71400 всплывашка улова (группа)
	UPROPERTY(Transient) TObjectPtr<UImage> PopupIcon = nullptr;            // [75] 71404 иконка улова 64×64
	UPROPERTY(Transient) FAsdaFmtText PopupText;                            // [76] 71401 текст улова (теги <FC>)
	UPROPERTY(Transient) TObjectPtr<UWidget> TimerGrp = nullptr;            // [77] 71348 узел таймера (группа)
	UPROPERTY(Transient) FAsdaFishBarUi TimerBar;                           // [78] 71347 FISH_count_1_bar
	UPROPERTY(Transient) TObjectPtr<UWidget> DigitsGrp = nullptr;           // [79] 71361 FISH_count_1 (группа цифр)
	UPROPERTY(Transient) TObjectPtr<UImage> Digit1 = nullptr;               // [79] 71361 десятки секунд (16×22)
	UPROPERTY(Transient) TObjectPtr<UImage> Digit2 = nullptr;               // [80] 71362 единицы секунд
	UPROPERTY(Transient) TObjectPtr<UImage> Digit3 = nullptr;               // [81] 71363 десятые доли
	UPROPERTY(Transient) TObjectPtr<UButton> BtnX = nullptr;                // [82] 71328 FISH_Xbtn
	UPROPERTY(Transient) TObjectPtr<UCheckBox> ChkPause = nullptr;          // [83] 71329 FISHI_PLAY_STOP_btn
	UPROPERTY(Transient) TObjectPtr<UCheckBox> ChkAuto = nullptr;           // [84] 71349 FISHI_AR_Check
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> ZoneNodes;             // группы-зоны окна (клик не в мир)
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;       // реле кнопок

	// ── геометрия, снятая при разборе (база для SetPos/SetSize эталона) ──
	float PanelRootLeft = 0.f;      // левый край FISH_WIN в канве группы панели (= L группы − Origin 71310)
	float AniBaseLeft = 0.f;        // левый край 71304 в канве рыбки (= L рыбки − Origin 71326)
	float GaugeExtentW = 786.f;     // `extent(71305).w` (ширина подложки шкалы)
	float AniExtentW = 56.f;        // `extent(71326).w`

	// ═════════════════════ МОДЕЛЬ ═════════════════════
	FAsdaFishKeyGuide Guide;        // ключ-гайд 0x351FA88
	bool bOpen = false;             // byte +341 «окно открыто»
	bool bPanelShown = false;       // byte +340 «панель стрелок видна»
	bool bControlsReady = false;    // контролы разобраны
	bool bLegacyAutoVisual = false; // Р8 при ключе 1: последнее эталонное состояние галки 71349 (JUI-умолчание — снята)
	int32 HoverId = 0;              // контрол под курсором с подсказкой (0 — нет)
	FAsdaMouseFrame Mouse;          // покадровый опрос мыши
	bool bRegistered = false;       // зарегистрирован в автомате как мини-игра
	// ★ Ревизия В2 МЕЛК-7 (волна В4): последние кадры (x в атласе `fish_gfont2`) трёх цифр таймера 71361…71363; −1 — не рисовано.
	int32 LastTimerDigitX[3] = { -1, -1, -1 };
};
