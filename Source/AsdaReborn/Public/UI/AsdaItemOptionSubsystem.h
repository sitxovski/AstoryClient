// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaItemOptionSubsystem — ОКНО «ИЗМЕНЕНИЕ ПАРАМЕТРОВ ПРЕДМЕТА»: `ItemOptionValue_WIN`
//   (JUI IDNum **50300**, заголовок — нотис 109600 «Изменение параметров предмета»).
//   Открывается кнопкой диалога NPC **30060** (у кузнеца `136npc`).
//   Полный реверс потока с адресами — `Reports/npc_exchange_params_30060.md`.
//
// ═══════════════════ ★★★ ЧТО ЭТО ЗА ОКНО НА САМОМ ДЕЛЕ (миснеймы сняты) ══════════════════════
//   Прежний заход записал 30060 как «окно апгрейда предмета» — ЭТО НЕВЕРНО. Ветка 30060
//   `CGameUI_HandleControlCommand` зовёт `itemUpgradeWindow_open@0xB635C0`, а та показывает
//   контрол **50300** `ItemOptionValue_WIN`, чей заголовок — нотис **109600 «Изменение параметров
//   предмета»**, кнопка — 109602 «Изменить», а расходник — 109922 «Свитков обмена параметрами».
//   Ни заточки, ни повышения уровня предмета здесь нет: окно ПЕРЕБРАСЫВАЕТ значения двух
//   параметров вещи в пределах разрешённых ей диапазонов.
//   Имена эталона `CItemUpgradeWindow` / `itemUpgrade*` / `Net_OnItemUpgradeResult` — АВТОРСКИЕ
//   ЯРЛЫКИ, они врут (закон `false-negatives-method`). Настоящий апгрейд/рефайн — это ДРУГИЕ
//   коды (30046/30047) и ДРУГОЙ объект (`g_pUpgradeWindow`, `refineWnd_Open`).
//   ⚠ ПОПРАВКА 2026-08-27: код **30073** из этого перечня УБРАН — он к апгрейду не относится
//   ВООБЩЕ. `case 30073` зовёт `uiUpgradeWndOpen@0xECBED0` (тоже миснейм), а та открывает окно
//   `Constellation_S_WIN` 74500 — ВЫБОР ЗНАКА ЗОДИАКА (`Reports/npc_button_30073.md`).
//   Ещё два миснейма, снятых здесь же:
//     • `sumInventoryItemsType67@0xB63500` — не «сумма предметов», а ПОИСК СВИТКА обмена
//       параметрами: `special_itemlist.SubType == 67` (7 кодов: 364/365/366/367/393/458/546).
//     • `UI_OnInventoryRemove@0x91EFBE` — переходник на `Inventory_SetBagSlot@0xD61650`, который
//       ЗАПИСЫВАЕТ запись в ячейку, а не удаляет её.
//     • Контрол **50336** (`ItemOptionValue_2_T1`) в документации значился «декоративным» — на
//       самом деле это ПОДПИСЬ ИМЕНИ выбранного предмета (`ItemSlotTooltip_Refresh@0xB629A0`
//       ставит в него `ItemData_FindByCode(itemCode)`).
//
// ═══════════════════ ЧТО ДЕЛАЕТ ОКНО ═══════════════════════════════════════════════════════════
//   Слева — сетка из 30 ячеек (`invGridRefreshGeneral@0xB62570`): предметы СУМКИ СНАРЯЖЕНИЯ
//   (контейнер 1, база `g_InventoryBag` 0x2901950), у которых есть гнёзда параметров.
//   Гейт ячейки 1:1: `ItemSubType != 66 && != 200 && != 201`, И (`запись+29 >= 0` ИЛИ
//   `запись+33 >= 0`), И код предмета ВНЕ диапазона 56051…56080. Заполнение — ПОДРЯД
//   (уплотнение есть), максимум 30.
//   Клик по ячейке выбирает предмет; его иконка идёт в 50302, имя — в 50336.
//   Справа — ДВЕ панели (гнездо параметра 1 и 2). В каждой: текущее значение (50341/50361),
//   заголовок 109607 «Изменяемые параметры» (50342/50362) и список ВОЗМОЖНЫХ значений — до
//   четырёх строк (50343/45/47/49 и 50363/65/67/69) с полосой прокрутки (50351/50371).
//   Список берётся из `itemoptionrange` по ГРУППЕ предмета: `u16 @0x68` шаблона для гнезда 0,
//   `u16 @0x74` — для гнезда 1 (`FAsdaItemRecord::OptionGroup0/1`).
//   Внизу — счётчик свитков (50380, нотис 109922) и две предупреждающие строки 109923/109924.
//
// ═══════════════════ КНОПКИ (1:1 `itemUpgradeWndProc@0xB639B0`) ════════════════════════════════
//   50390 «Изменить» (109602) → `itemUpgradeConfirmReady@0xB637F0`: гейты, затем ПОКАЗ анимации
//     50393 (`Ani_SoulSton`, 37 кадров, один прогон 3000 мс).
//   50393 — не кнопка, а АНИМАЦИЯ: её СОБЫТИЕ «прогон завершён» (msg 0x18) прячет её и
//     отправляет пакет 6553. То есть запрос уходит ПОСЛЕ ролика, а не по клику.
//   50391 «Отмена» (109603), 50392 «×» и msg 21 (Esc) → `sub_93548F` = спрятать окно.
//   Ячейки 50304…50333 — msg 0xB (клик) и msg 8/9/0xA (тултип предмета).
//   Полосы 50351/50371 — msg 0x17: новая позиция уходит в `filler[316]`/`filler[352]`.
//
// ═══════════════════ ПАКЕТЫ ════════════════════════════════════════════════════════════════════
//   **6553** (40 б = 8 секрет + 16 MD5 + 16 тело) → **6554** (тело 119 б).
//   Раскладки — у объявлений в `AsdaNetSubsystem.h`. Порядок пары в теле ЖЁСТКИЙ:
//   сперва СВИТОК, потом СНАРЯЖЕНИЕ (сервер `Recv_UserItemOptionTrans@0x536460` проверяет их
//   разными предикатами: `sub_532B10` = код 1…N для свитка, `sub_532A90` = код ≥ 20551 для вещи).
//   Пока ответ не пришёл, стоит флаг `bPending` (эталонное поле `filler[480]`).
//
// ═══════════════════ ЗВУКИ И ЭФФЕКТЫ ═══════════════════════════════════════════════════════════
//   СВОИХ звуков у окна НЕТ: во всех его функциях (`itemUpgradeWindow_open`,
//   `itemUpgradeWindowInitControls`, `itemUpgradeWndProc`, `itemUpgradeConfirmReady`,
//   `itemSendUpgradeRequest`, `Net_OnItemUpgradeResult`, `CItemTooltip_BuildOptionLines`,
//   `invGridRefreshGeneral`, `ItemSlotTooltip_Refresh`) нет ни одного вызова звуковой подсистемы.
//   Единственный звук пути — «error.wav» модального окна `ONEBUT_MSGBOX`, его ставит общая точка
//   `UAsdaWindowSubsystem::ShowGameMessage`.
//   Единственный эффект — кадровая анимация 50393 (`Ani_SoulSton`, атлас `ne_soulston`);
//   в порту это `UAsdaAnimatedPlane`, импортёр её уже завёл.
//   Курсор выбранной ячейки 50334 — `Slot_AniSelect`, тоже `UAsdaAnimatedPlane`.
//
// ═══════════════════ ОТСТУПЛЕНИЯ И ДЕФЕКТЫ ОРИГИНАЛА ══════════════════════════════════════════
//   1. Нотис **10971** («в стопке свитков ноль») в таблице текстов ОТСУТСТВУЕТ — во всём
//      клиентском `msg*.dat` нет ни одной строки диапазона 10900…10999. Оригинал показывает
//      ПУСТОЕ модальное окно. Чиним по закону `fix-original-defects`: подставляем 109710
//      «Свитка нет.»; откат — `asda.ItemOption.FixLoc10971 0`.
//   2. Многострочный нотис успеха эталон печатает С ТЕГАМИ `<FC=…>` (старое значение
//      0xFF00AEEF, новое 0xFFF26522). Наш `ONEBUT_MSGBOX` рисует ПРОСТЫМ `UTextBlock`, поэтому
//      теги СРЕЗАЮТСЯ (`FAsdaMarkup::StripTags`) — иначе игрок увидел бы разметку. Точка
//      занесена в реестр отложенного.
//   3. Прямоугольник колёсных окон 50351/50371 импортёр НЕ материализовал: единственный план
//      скина `WheelWin_Nomal` помечен `PlaneAlwaysInvisible = "1"`. Полосу прокрутки строим по
//      ЖИВОЙ геометрии четырёх строк списка и авторским числам `.JUI`
//      (полоса: X = правый край строки + 1, Y = верх первой строки, H = 80).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "UI/AsdaBagListScroll.h"    // ★ 29.10.2026 (инвентарь ×2): FAsdaBagListScroll — окно 30 позиций над списком до 60
#include "UI/AsdaScrollBarNomal.h"   // FAsdaScrollBarNomal — общая полоса `Nomal_Scr`
#include "Items/AsdaItemDbRows.h"    // FAsdaItemoptionrangeRow
#include "UI/AsdaMarkup.h"           // FAsdaFmtText — строка с тегами <FC=…> (общая точка разметки)
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaItemOptionSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UCanvasPanel;
class UAsdaIconCellGrid;
class UAsdaAnimatedPlane;
class UAsdaButtonRelay;

// Виджеты одной ячейки сетки: план-иконка + одноимённый `TextBlock` («+N%»).
USTRUCT()
struct FAsdaItemOptionCellUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UImage>     Icon  = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Label = nullptr;
	// Авторский цвет подписи из `.JUI` (блок `EXS TextColor`, закон `ui-text-color-exs`).
	//   Возвращаем его, когда заточки у предмета нет: эталон в этой ветке цвет не трогает вовсе.
	FSlateColor BaseColor = FSlateColor(FLinearColor::White);
};

// Одна позиция сетки — ссылка на предмет сумки снаряжения.
USTRUCT()
struct FAsdaItemOptionEntry
{
	GENERATED_BODY()
	UPROPERTY(Transient) int32 ItemCode  = 0;    // 0 ⇒ ячейка пуста
	UPROPERTY(Transient) int32 Container = 0;    // поле записи +4
	UPROPERTY(Transient) int32 Slot      = -1;   // поле записи +5
	UPROPERTY(Transient) int32 CellIndex = -1;   // индекс ячейки в контейнере (для тултипа/записи)

	bool IsEmpty() const { return ItemCode <= 0; }
	void Clear() { ItemCode = 0; Container = 0; Slot = -1; CellIndex = -1; }
};

// Виджеты одной панели параметров (j = 0 и 1).
USTRUCT()
struct FAsdaItemOptionPanelUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Tab     = nullptr;   // 50340 / 50360 — подпись вкладки
	// ★ 25.09.2026: текущее значение и строки списка несут теги `<FC=%08x>…</F>` (эталон печатает их
	//   `sprintf` в `CItemTooltip_BuildOptionLines@0xB62C70`, а цвет разбирает рендер текста
	//   `textParseColorTag@0x12C8700`). Поэтому это `FAsdaFmtText` — строка общей точки разметки
	//   (`UAsdaRichText::SetFmt`), а не голый `UTextBlock`, который показывал теги буквами.
	UPROPERTY(Transient) FAsdaFmtText Current;                       // 50341 / 50361 — текущее значение
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Header  = nullptr;   // 50342 / 50362 — «Изменяемые параметры»
	UPROPERTY(Transient) FAsdaFmtText Row0;                          // 50343 / 50363
	UPROPERTY(Transient) FAsdaFmtText Row1;                          // 50345 / 50365
	UPROPERTY(Transient) FAsdaFmtText Row2;                          // 50347 / 50367
	UPROPERTY(Transient) FAsdaFmtText Row3;                          // 50349 / 50369
	UPROPERTY(Transient) TObjectPtr<UWidget>    RowPlane0 = nullptr; // план строки 1 — из него берём геометрию полосы
	UPROPERTY(Transient) TObjectPtr<UWidget>    RowPlane3 = nullptr; // план строки 4

	// Строка списка i (0…3) или nullptr.
	FAsdaFmtText* Row(int32 i)
	{
		switch (i) { case 0: return &Row0; case 1: return &Row1; case 2: return &Row2; case 3: return &Row3; default: return nullptr; }
	}
};

UCLASS()
class ASDAREBORN_API UAsdaItemOptionSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Окно, его защёлка «запрос в полёте» и полученные от сервера данные
	// принадлежат СЕССИИ и переживали разрыв связи вместе с GameInstance.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaItemOptionSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа окна. Все из `.JUI` 50300 и `itemUpgradeWindowInitControls@0xB60BC0` ──
	static constexpr int32 kCells    = 30;   // 50304…50333 — ячеек СЕТКИ окна (контейнер 1, `g_InventoryBag`)
	// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): предел перебора сумки и длина уплотнённого списка `Cells` — вся сумка
	//   (= `UAsdaInventorySubsystem::kShellSlots`, static_assert в .cpp); сетка показывает окно из 30 (`BagScroll`).
	static constexpr int32 kListMax  = 60;
	static constexpr int32 kPanels   = 2;    // цикл `for (j = 0; j < 2; ++j)`
	static constexpr int32 kListRows = 4;    // `uiControlSetScrollRange(listbox, 4, N)` — 4 видимых строки
	static constexpr int32 kCellPx   = 32;   // `Size = "32 32"` у ячеек
	static constexpr int32 kWinZ     = 733;  // выше инвентаря (720), магазина (730), ремонта (731), витрины (732)

	// Гейт сетки (`invGridRefreshGeneral`): коды 56051…56080 исключены целиком.
	static constexpr int32 kExcludeCodeLo = 56051;
	static constexpr int32 kExcludeCodeHi = 56080;
	// Порог «значение хранится сотыми долями» (`cmp eax, 0x12C` в трёх местах эталона).
	static constexpr int32 kPercentOptionIdFirst = 300;
	// `special_itemlist.SubType` свитка обмена параметрами (`cmp dword ptr [eax+0x50], 0x43`).
	static constexpr int32 kScrollSubType = 67;

	// ═══ ОТКРЫТИЕ / ЗАКРЫТИЕ ═══
	// Эталон `itemUpgradeWindow_open@0xB635C0` (показ) и `sub_93548F` → `sub_B636D0` (скрыть).
	UFUNCTION(BlueprintCallable, Category="Asda|ItemOption")
	void OpenWindow();

	UFUNCTION(BlueprintCallable, Category="Asda|ItemOption")
	void CloseWindow();

	UFUNCTION(BlueprintPure, Category="Asda|ItemOption")
	bool IsOpen() const { return bOpen; }

	// ═══ ДЕЙСТВИЯ (в эталоне — обработчики контролов) ═══
	void OnCellClicked(int32 Cell);   // msg 0xB, id 50304+i → `itemUpgradeOnSlotMsg@0xB63770`
	void OnOkClicked();               // 50390 → `itemUpgradeConfirmReady@0xB637F0`
	void OnCancelClicked();           // 50391 / 50392 / msg 21 → спрятать окно
	// msg 0x18 от 50393 («прогон анимации завершён») → `itemSendUpgradeRequest@0xB611E0`.
	//   UFUNCTION обязателен: `UAsdaAnimatedPlane::OnAnimFinished` — динамический делегат.
	UFUNCTION()
	void OnConfirmAnimFinished();

	// ═══ ДИАГНОСТИКА ═══
	FString DumpControls() const;   // КАЖДЫЙ разобранный контрол (закон full-structural-analysis)
	FString DumpState() const;      // сетка, выбор, свитки, обе панели, флаги
	UWidget* DebugCellIcon(int32 Cell) const;           // самотест: живая иконка видимой ячейки сетки (nullptr — нет)
	int32 DebugSelectedCode() const;                    // самотест: код выбранного предмета (0 — выбора нет)
	int32 DebugRangeRows(int32 Panel) const;            // самотест: строк диапазонов панели 0/1
	FString DebugRangeText(int32 Panel, int32 Row) const;   // самотест: строка диапазона так, как её рисует окно

private:
	// ── Жизненный цикл окна ──
	bool EnsureWidget();
	void ResetWidget();
	void SetupControls();
	void RegisterZone();

	// ── Модель ──
	void RebuildCells();                       // `invGridRefreshGeneral@0xB62570`
	void RefreshScrollCount();                 // `sumInventoryItemsType67@0xB63500` + текст 50380
	void RefreshSelectedItem();                // `ItemSlotTooltip_Refresh@0xB629A0`
	void RefreshOptionPanels();                // `CItemTooltip_BuildOptionLines@0xB62C70`
	void RefreshAll();                         // `invWeightTextRefresh@0xB624C0`
	void SelectEntry(int32 Cell);              // `invWeightOnSlotMsgRefresh@0xB63700`

	// ── Покадровые опросы ──
	void TickCells();
	void TickScrollBars();
	void LayoutScrollBars();

	// ── Сеть ──
	void SendRequest();                                       // `itemSendUpgradeRequest@0xB611E0`
	void DrainPendingPackets();
	void OnTransResult(const TArray<uint8>& Body119);         // `Net_OnItemUpgradeResult@0xB61990`

	// ── Нотисы ──
	FString Loc(int32 LocId) const;
	void NoticeMsgBox(int32 LocId) const;                     // `CItem_BuildTooltipText(…,0,−1,текст,1)`
	void NoticeMsgBoxText(const FString& Text) const;

	// ── Мелочи ──
	const FAsdaItemOptionEntry* Selected() const;
	FString FormatOptionCurrent(int32 OptionId, int32 RawValue) const;
	FString FormatOptionRange(const FAsdaItemoptionrangeRow& R) const;
	int32   MaxScrollRow(int32 Panel) const;
	void    SetScrollRow(int32 Panel, int32 Row);
	void    HideSelectionFrame();
	void    MoveSelectionFrame(const UWidget* OverIcon);

	// ── Виджеты ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> Grid = nullptr;
	// ★ Слот выбранного предмета 50302 рисуется ТОЙ ЖЕ общей точкой, что и сетка — «сеткой из одной
	//   ячейки». Второго механизма рисования иконок в порту нет и заводить его нельзя
	//   (закон `port-shared-points`); эталон здесь тоже зовёт ту же `CControl_DrawTextureRect`.
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridTarget = nullptr;
	UPROPERTY(Transient) TArray<FAsdaItemOptionCellUi> CellUi;
	UPROPERTY(Transient) TArray<FAsdaItemOptionPanelUi> PanelUi;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTitle      = nullptr;   // 50301
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtItemName   = nullptr;   // 50336
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTabItem    = nullptr;   // 50303 (подпись вкладки)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtScrollCnt  = nullptr;   // 50380
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtWarn1      = nullptr;   // 50381
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtWarn2      = nullptr;   // 50382
	UPROPERTY(Transient) TObjectPtr<UImage>     IconTarget    = nullptr;   // 50302 (план иконки)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> IconTargetLbl = nullptr;   // 50302 («+N%»)
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnOk         = nullptr;   // 50390
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnCancel     = nullptr;   // 50391
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtBtnOk      = nullptr;   // подпись 50390
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtBtnCancel  = nullptr;   // подпись 50391
	// 50392 — крестик заголовка. У контрола в `.JUI` пустой `ID`; узел заведён В АССЕТЕ шагом
	//   `Tools/asda_ui_missing_interactive.py` под именем общего правила `{ЯКОРЬ}_BTN{NN}`.
	//   Раньше здесь лежала `CloseZone` (просто план-картинка) и клик по ней ловил `TickCloseButton`.
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose      = nullptr;   // 50392
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> AniConfirm = nullptr;   // 50393
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> SelFrame   = nullptr;   // 50334
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	// ── Полосы прокрутки списков (50351 / 50371) ──
	UPROPERTY(Transient) TArray<FAsdaScrollBarNomal> Bars;
	UPROPERTY(Transient) TArray<TObjectPtr<UCanvasPanel>> BarHosts;

	// ── Состояние ──
	TArray<FAsdaItemOptionEntry> Cells;            // уплотнённый список (эталонные `this[2i+9]`; ★ 29.10: до 60, `kListMax`)
	int32 SelCell = -1;                            // индекс в `Cells` (НЕ видимая ячейка); −1 ⇒ ничего (`filler[368]` == 0)
	FAsdaBagListScroll BagScroll;                  // ★ 29.10.2026: окно сетки 30 над `Cells` (ряд прокрутки колесом)
	FAsdaItemOptionEntry ScrollRef;                // свиток (`filler[372]`)
	int32 ScrollTotal = 0;                         // сумма количеств свитков (второй выход `sumInventoryItemsType67`)
	TArray<FAsdaItemoptionrangeRow> Rows0;         // список возможных значений гнезда 0
	TArray<FAsdaItemoptionrangeRow> Rows1;         // гнезда 1
	TArray<int32> ScrollRow;                       // `filler[316]` / `filler[352]`
	bool bOpen = false;
	bool bControlsReady = false;
	bool bPending  = false;                        // `filler[480]` — ждём 6554
	bool bConfirm  = false;                        // `filler[481]` — играет ролик 50393
	bool bDirty    = false;
	int32 TipCell  = -1;                           // ячейка, для которой показана плашка (−2 = слот 50302)
	FAsdaMouseFrame Mouse;

	// ── Подписки ──
	FDelegateHandle HTransResult;
	FDelegateHandle HInvCell;
	TAsdaNetQueue<TArray<uint8>> PendingResults;          // закон `no-reentrant-net-callbacks`
};
