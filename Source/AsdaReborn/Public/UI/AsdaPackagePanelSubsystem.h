// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaPackagePanelSubsystem — ПАНЕЛЬ «СОСТАВ ПАКЕТА» (JUI IDNum **26500** `Package_Contents_Win`).
//   Полный реверс с адресами и дампом каждого контрола — `Reports/item_use_panels.md`.
//   Ни одно число здесь не выдумано: у каждого есть ссылка на `RE/etalon/*.c`,
//   `RE/ida/export/AsdaStory_ida.c` или `Data/Decoded/jui/gamemain_jui.ini`.
//
// ═══════════════════ КЛАССИФИКАЦИЯ (закон `classify-before-building`) ═══════════════════════════
//   Это **НОВАЯ СИСТЕМА**, а не доделка инвентаря: собственное верхнеуровневое окно .JUI (26500 —
//   корень, не потомок 26000), собственный жизненный цикл (открытие/заполнение/закрытие),
//   собственная геометрия (окно РАСТЁТ по ширине и центрируется), собственная таблица ячеек
//   (5 блоков × 9 ячеек), собственные тултипы и собственная отложенная отправка.
//   Поэтому — отдельная подсистема по образцу `UAsdaShowOffSubsystem`.
//
// ═══════════════════ ЧТО ЭТО ЗА ПАНЕЛЬ ═════════════════════════════════════════════════════════
//   Окно `Package_Contents_Win` (26500) — превью содержимого коробки/пакета и подтверждение вскрытия.
//   В оригинале двойной клик по коробке (`itemSubType ∈ {200, 201}`, 5 045 кодов) **НЕ шлёт 6096**,
//   а открывает эту панель: `item_inventory.c:37873-37878` →
//   `stampWnd_PopulateItemSlots(g_pPackageStampWindow, code, /*stampMode*/ 1, /*bShowTooltip*/ 0)`.
//   Пакет 6096 уходит только по кнопке 26502.
//
//   ★★★ ДВА РЕЖИМА (`stampMode`, `item_enhance.c:3339-3349`):
//     0 = ПРЕВЬЮ  — подпись кнопки 26502 = нотис **99300** «Confirm», клик просто закрывает панель;
//     1 = ВСКРЫТИЕ — подпись = нотис **99305** «Open Package», клик проверяет место и шлёт 6096.
//
// ═══════════════════ РАСКЛАДКА: 5 БЛОКОВ × 9 ЯЧЕЕК ════════════════════════════════════════════
//   Класс эталона держит массив `aSowelRows` @0x934 — **5 записей по 148 байт** (имя поля ВРЁТ:
//   к самоцветам отношения не имеет). Привязка — `UIPackageStampWnd_Init@0xDAEA20`
//   (`ui_core.c:63051-63070` + `AsdaStory_ida.c:616145-616164`).
//   ⚠ ЛОВУШКА МЕТОДА (закон `false-negatives-method`): в срезе `RE/etalon/ui_core.c` привязки
//     ячеек 1…3 **НЕТ** — она есть только в полном дампе `AsdaStory_ida.c`. Вывод «ячеек 1…3 не
//     существует» был бы ложным «нет».
//
//   Блок r (0…4), база `aSowelRows + 148*r`, шаг ячейки 16 б:
//     +0                     ctrl рамки блока                 `Package_Bag{r}`        = 26510 + 20r
//     ВЕРХНИЕ 4 ячейки k=0…3:  +4+16k код · +8+16k IDNum · +12+16k подпись · +16+16k иконка
//        k=0  иконка `Package_BagIcon{r}`    = 26511 + 20r · подпись `Package_BagName{r}`    = 26512 + 20r
//        k=1  иконка `Package_ItemIcon{r}_0` = 26701 + 10r · подпись `Package_ItemName{r}_0` = 26702 + 10r
//        k=2  иконка `Package_ItemIcon{r}_1` = 26703 + 10r · подпись `Package_ItemName{r}_1` = 26704 + 10r
//        k=3  иконка `Package_ItemIcon{r}_2` = 26705 + 10r · подпись `Package_ItemName{r}_2` = 26706 + 10r
//     НИЖНИЕ 5 ячеек c=0…4:   +68+16c код · +72+16c IDNum · +76+16c подпись · +80+16c иконка
//        иконка `Package_BagItemIcon{r}_{c}` = 26513 + 20r + 2c · подпись `…Name…` = 26514 + 20r + 2c
//   Сходимость шага: +68 + 16*4 + 16 = 148 ✓.
//
//   ★ Ячейка **k=0 занята ИСКЛЮЧИТЕЛЬНО вложенными коробками**, обычные предметы идут в k=1…3
//     (`item_enhance.c:3371-3425`). Вложенность ровно **2 уровня**, третьего нет.
//
// ═══════════════════ ГЕОМЕТРИЯ: ОКНО РАСТЁТ ═══════════════════════════════════════════════════
//   `item_enhance.c:3571-3598`. Блоки лежат В РЯД по X с шагом 278 (.JUI: 4, 282, 560, 838, 1116),
//   а окно в .JUI всего 284 px — то есть видно ровно один блок. Эталон подгоняет ширину под число
//   занятых блоков и заново центрирует окно на экране:
//     одноразовый замер (защёлка `g_bGridCellWidthMeasured`):
//         g_nGridCellWidth = ширина 26500 = **284**   (шаг колонки)
//         g_nGridBaseX     = ширина 26505 = **178**   (базовая ширина титул-бара)
//     ширина 26505 = 178 + 284*blocks · ширина 26501 и 26500 = 284*(blocks+1)
//     26507 (подложка кнопки) центрируется: x = (284*(blocks+1) − ширина26507) / 2
//     окно: x = экранW/2 − ширина/2 · y = экранH/2 − высота/2
//   ⚠ Это ЕДИНСТВЕННОЕ место, где .JUI-раскладка заведомо неполна — позиции блоков 1…4 в данных
//     лежат за краем окна намеренно, их «включает» именно этот пересчёт.
//
// ═══════════════════ КНОПКИ ═══════════════════════════════════════════════════════════════════
//   **26506** `Package_ClsBtn` → `uiPanelCloseAndKillFocus@0xDAC900` (`ui_core.c:62735`). Пакетов нет.
//   **26502** `Package_UnpackBtn` → `uiCashShopButtonDispatch@0xDACAB0` (`ui_core.c:62796-62818`):
//       stampMode == 0                       → просто закрыть панель;
//       stampMode == 1 и выбор не пуст:
//           места хватает (`itemHasNoEmptySowelSlots@0xDAAAB0` — имя ВРЁТ, true = «хватает»)
//               → взвести защёлку отправки (`gap_C64[84] = 1, [85] = 1, [88] = 0`);
//           места НЕ хватает
//               → нотис **99306** с двумя числами В ОБЩЕЙ ПЛАШКЕ ПОДСКАЗКИ
//                 (`CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)`) — ★ НЕ в чат! —
//                 и панель ЗАКРЫВАЕТСЯ.
//   **Отправка отложена на следующий кадр** — `itemPackAgeToggleSendAction@0xDAB6E0`
//   (`item_inventory.c:53940-53968`), вызов из главного цикла `game_main.c:1480`. Защёлка
//   ОДНОРАЗОВАЯ, перед отправкой выбор перепроверяется (`itemSubType ∈ {200,201}` + строка
//   `packagetable` на месте). Это и есть эталонный аналог правила `no-reentrant-net-callbacks`.
//
// ═══════════════════ ЗАКРЫТИЕ ═════════════════════════════════════════════════════════════════
//   `uiPanelCloseAndKillFocus@0xDAC900` (`ui_core.c:62660-62683`). Точки вызова:
//     • кнопка 26506; • кнопка 26502 при stampMode 0; • кнопка 26502 при отказе по месту;
//     • **ESC** (msg 21), если панель видима (`item_enhance.c:3758-3761`);
//     • ★ **автоматически первым делом при приходе 6097** (`network.c:24677`).
//   ⚠ Закрытие НИЧЕГО не обнуляет — ни режим, ни выбор, ни коды ячеек. Очистка делается только
//     следующим заполнением (`item_enhance.c:3314-3338`). Воспроизводим 1:1.
//
// ═══════════════════ ПОДСКАЗКА ПО НАВЕДЕНИЮ ═══════════════════════════════════════════════════
//   `stampWnd_ShowItemTooltip@0xDB29B0` (`item_enhance.c:3681-3742`), маршрут — msg 9 (наведение) /
//   msg 10 (уход) в `stampCraftWnd_msgProc@0xDB2C80`. По IDNum контрола ищется код в тех же полях
//   `+8+16k` / `+72+16c` и строится ОБЩАЯ плашка предмета. Владелец плашки — `Package`
//   (закон `ui-shared-tip-ownership`: гасит только владелец).
//
// ═══════════════════ ЧЕГО В ДАННЫХ НЕТ (не выдумываем, закон `no-invented-ui`) ═════════════════
//   • **Ни прокрутки, ни страниц.** Кнопки пейджинга 26453/26454/26455/26466 принадлежат окну
//     МАГАЗИНА `PackageShop_Win` (26000), а не 265xx; скин-контролов скроллбара в 26500 нет.
//     Вместо прокрутки — рост ширины (см. выше).
//   • **Ни анимации, ни таймера.** `stampCraftWnd_msgProc` обрабатывает только 9/10/11/21/66561;
//     обращений ко времени в заполнении нет. Единственный покадровый вызов — защёлка отправки.
//   • Контрол **26503** `Package_Contents_AType` — мёртвый: 0 ссылок в полном IDA-дампе. Оставляем
//     как есть (он есть в .JUI), логики не придумываем.
//   • Кнопка-подложка **26507** обработчика клика не имеет — только центрируется.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaPackagePanelSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UCanvasPanel;
class UAsdaButtonRelay;

// ═══ Одна ЯЧЕЙКА панели = четвёрка полей блока (`code / IDNum / подпись / иконка`) ══════════════
USTRUCT()
struct FAsdaPackageCellUi
{
	GENERATED_BODY()

	// Эталонное поле `+4+16k` (верхние) / `+68+16c` (нижние). −1 = ячейка пуста.
	UPROPERTY(Transient) int32 ItemCode = -1;
	// Эталонное поле `+8+16k` / `+72+16c` — IDNum контрола-иконки: по нему `stampWnd_ShowItemTooltip`
	//   опознаёт, над какой ячейкой курсор. Храним для дампа и для сообщений диагностики.
	UPROPERTY(Transient) int32 IdNum = -1;
	// Эталонное поле `+16+16k` / `+80+16c` — контрол-иконка (он же цель хит-теста).
	UPROPERTY(Transient) TObjectPtr<UImage> Icon = nullptr;
	// Число на иконке: эталон при `количество > 1` пишет «%d» ПРЯМО НА ИКОНКУ (`item_enhance.c:3445-3449`).
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Count = nullptr;
	// Эталонное поле `+12+16k` / `+76+16c` — подпись под/рядом с иконкой.
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Name = nullptr;
};

// ═══ Один БЛОК = эталонная запись `aSowelRows[148*r]` ═══════════════════════════════════════════
USTRUCT()
struct FAsdaPackageBlockUi
{
	GENERATED_BODY()

	// Эталонное поле `+0` — рамка блока `Package_Bag{r}` (её видимость и есть «блок показан»).
	UPROPERTY(Transient) TObjectPtr<UWidget> Frame = nullptr;
	// Верхние 4 ячейки (k = 0…3). k=0 — только под вложенную коробку.
	UPROPERTY(Transient) TArray<FAsdaPackageCellUi> Big;
	// Нижние 5 ячеек (c = 0…4) — содержимое вложенной коробки.
	UPROPERTY(Transient) TArray<FAsdaPackageCellUi> Small;
};

UCLASS()
class ASDAREBORN_API UAsdaPackagePanelSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaPackagePanelSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ═══════════════════ КОНСТАНТЫ ИЗ ДАННЫХ .JUI / ЭТАЛОНА ═══════════════════
	static constexpr int32 kWinIdNum   = 26500;   // Package_Contents_Win
	static constexpr int32 kBlocks     = 5;       // aSowelRows: 5 записей по 148 б
	static constexpr int32 kBigCells   = 4;       // верхние ячейки k = 0…3
	static constexpr int32 kSmallCells = 5;       // нижние ячейки c = 0…4
	static constexpr int32 kPkgSlots   = 5;       // кодов в строке packagetable: [1..5], количества [6..10]

	// Геометрия из .JUI (замер эталона — одноразовая защёлка `g_bGridCellWidthMeasured`).
	static constexpr float kGridCellWidth = 284.f;   // ширина окна 26500 = шаг колонки
	static constexpr float kGridBaseX     = 178.f;   // ширина титул-бара 26505
	static constexpr float kWinHeight     = 320.f;   // высота окна 26500
	static constexpr float kBtnBaseY      = -36.f;   // Origin.y подложки кнопки 26507

	// Z: панель обязана лежать ВЫШЕ окна инвентаря (у него 720), из которого её открывают.
	static constexpr int32 kPanelZ = 760;

	// ═══════════════════ ★★★ ОТКРЫТИЕ — порт `stampWnd_PopulateItemSlots@0xDACF10` ═══════════════
	//   `StampMode`: 0 = превью (кнопка 26502 = «Confirm», только закрывает), 1 = вскрытие
	//   (кнопка = «Open Package», шлёт 6096). `SrcContainer`/`SrcSlot` — выбранная ячейка инвентаря
	//   (эталонный `gap_34`); нужны только для режима 1.
	//   `bShowTooltip` — эталонный 4-й аргумент: панель уезжает к краю и рядом встаёт hover-подсказка
	//   (используется только точкой входа «наведение на слот экипировки», `ui_core.c:10421`).
	//   Возвращает false, если предмет не коробка или строки `packagetable` нет.
	UFUNCTION(BlueprintCallable, Category="Asda|Пакеты")
	bool Populate(int32 ItemCode, int32 StampMode, int32 SrcContainer, int32 SrcSlot, bool bShowTooltip = false);

	// ═══════════════════ ЗАКРЫТИЕ — порт `uiPanelCloseAndKillFocus@0xDAC900` ═════════════════════
	//   ⚠ Как в эталоне: НЕ обнуляет ни режим, ни выбор, ни коды ячеек.
	UFUNCTION(BlueprintCallable, Category="Asda|Пакеты")
	void ClosePanel();

	UFUNCTION(BlueprintPure, Category="Asda|Пакеты")
	bool IsPanelOpen() const { return bPanelVisible; }
	UFUNCTION(BlueprintPure, Category="Asda|Пакеты")
	int32 GetStampMode() const { return StampMode; }
	// ★ R4-b хвосты-2 (15.11.2026): только чтение — снимок открытой панели самотестом (возврат тем же `Populate`).
	int32 GetSelItemCode() const { return SelItemCode; }   // код коробки, по которой открыли
	int32 GetSelContainer() const { return SelContainer; } // контейнер ячейки-источника (−1 — нет)
	int32 GetSelSlot() const { return SelSlot; }           // ячейка-источник (−1 — нет)

	// ═══════════════════ КНОПКИ ═══════════════════
	// 26502 — «Confirm» / «Open Package».
	UFUNCTION(BlueprintCallable, Category="Asda|Пакеты")
	void OnUnpackButton();
	// 26506 — крестик.
	UFUNCTION(BlueprintCallable, Category="Asda|Пакеты")
	void OnCloseButton();

	// ★ Приход 6097: эталон ПЕРВЫМ ДЕЛОМ закрывает панель (`network.c:24677`). Зовётся из
	//   `UAsdaInventorySubsystem::ApplyPackageResult` до всякой работы со слотами.
	void NotifyPackageResult();

	// ═══════════ ★ 31.10.2026: СТЫКОВКА С ОКНОМ «ВИД» 15600 — порт `positionHoverTooltipWnd@0xDB3900` ═══════════
	//   Панель уезжает к ПРАВОМУ краю экрана так, чтобы справа от неё поместилось окно «Вид», по вертикали — в центр:
	//       x = экранW − (ширина15600 + ширина26500),  y = экранH/2 − высота26500/2;
	//   окно «Вид» встаёт вплотную справа: (x + ширина26500, y + высота26500 − 343). Зовут оба входа эталона:
	//   щелчок по ячейке бустера (`invItemUseOrBooster@0xDB36D0`) и заполнение с 4-м аргументом = 1.
	void DockBesidePrevWindow();
	// Иконка ячейки (блок r, верхняя `bBig`/нижняя, индекс) — точка симклика самотеста по живой геометрии.
	UWidget* DebugCellIcon(int32 Block, bool bBig, int32 Idx) const;
	// Код предмета той же ячейки (−1 = пуста) — самотест выбирает ячейку бустера.
	int32 DebugCellCode(int32 Block, bool bBig, int32 Idx) const;
	// Корень окна панели (WBP) — самотест иконок состава проверяет видимость цепочки родителей до него.
	const UWidget* DebugRootWidget() const;

	// ═══════════════════ ДИАГНОСТИКА (закон `full-structural-analysis-always`) ═══════════════════
	FString DumpControls() const;   // каждый разобранный контрол панели
	FString DumpModel() const;      // текущее содержимое ячеек

private:
	// ── Жизненный цикл виджетов ──
	bool EnsurePanel();
	void SetupControls();
	// ★★★ `EnsureNameLabel` УДАЛЁН 2026-08-27: 19 подписей БОЛЬШИХ ячеек
	//   (`Package_BagName{1..4}_Text`, `Package_ItemName{r}_{k}_Text`) теперь лежат в самом
	//   `WBP_Package_Contents_Win` — их кладёт идемпотентный шаг
	//   `Tools/asda_ui_geometry_bake.py` (блок G5) из `asda_ui_post_import.STEPS`.
	//   Подробный разбор — в .cpp на месте снятого метода.
	void ClearAllCells();                       // порт `item_enhance.c:3314-3338`
	void ApplyGeometry(int32 UsedBlocks);       // порт `item_enhance.c:3571-3598`
	void RegisterZones();
	void UnregisterZones();
	bool GetPanelRect(FVector2D& OutMin, FVector2D& OutMax) const;

	// ── Заполнение ──
	// Разложить одну ячейку: код → иконка + подпись + число. Возвращает false, если код не нашёлся
	//   ни в одной из трёх таблиц (эталон такую позицию просто пропускает — ячейка остаётся скрытой).
	bool FillCell(FAsdaPackageCellUi& Cell, int32 ItemCode, int32 Amount);
	void HideCell(FAsdaPackageCellUi& Cell);

	// ── Подсказка по наведению (порт `stampWnd_ShowItemTooltip@0xDB29B0`) ──
	void TickCellTooltip();
	// ── ★ 31.10.2026: щелчок ЛКМ по ячейке — событие 0xB `stampCraftWnd_msgProc@0xDB2C80` → `invItemUseOrBooster@0xDB36D0` ──
	void TickCellClick();

	// ── Отложенная отправка (порт `itemPackAgeToggleSendAction@0xDAB6E0`) ──
	void TickDeferredSend();

	// ── Виджеты ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> PanelWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> RootCanvas = nullptr;
	// 26501 `Package_Contents_Move` — эталонное поле `pSowelItemNameLabel`: имя ВРЁТ, это МЕТКА
	//   ИМЕНИ КОРОБКИ (и заодно ручка перетаскивания, EVS 6→0x20003).
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BoxNameLabel = nullptr;
	UPROPERTY(Transient) TObjectPtr<UWidget>    TitleBar     = nullptr;   // 26505 (ширина растёт)

	/** ★★★ Авторская привязка крестика 26506 к ПРАВОМУ краю родителя.
	 *  `gamemain_jui.ini`: `Package_ClsBtn` имеет `Origin = "-22 6"`. Отрицательный X в данных .JUI
	 *  означает отсчёт от правого края родителя: x = ширина родителя − 22. Импортёр эту привязку
	 *  не переносит — он один раз пересчитал её в абсолютную координату при авторской ширине окна
	 *  (284) и «заморозил», поэтому у широких наборов крестик уезжал в середину.
	 *  Число взято ИЗ ДАННЫХ, не подобрано. */
	static constexpr float kCloseOriginRight = 22.f;

	/** Правый край, относительно которого стоит крестик 26506, В СИСТЕМЕ КООРДИНАТ ЕГО РОДИТЕЛЯ.
	 *  Крестик может лежать как в корневом канвасе, так и внутри группы шапки — координаты у этих
	 *  случаев разные, и смешивать их нельзя (иначе крестик уезжает на ширину группы). */
	float CloseParentRightEdge(float WindowWidth) const;
	UPROPERTY(Transient) TObjectPtr<UWidget>    BtnBase      = nullptr;   // 26507 (центрируется)
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnUnpack    = nullptr;   // 26502
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BtnUnpackCap = nullptr;   // подпись 26502
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose     = nullptr;   // 26506
	UPROPERTY(Transient) TArray<FAsdaPackageBlockUi> Blocks;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	// ── Состояние (эталонные поля класса) ──
	int32 StampMode    = 0;    // `stampMode`
	int32 SelItemCode  = 0;    // код коробки, по которой открыли
	int32 SelContainer = -1;   // `*(BYTE*)(gap_34 + 4)`
	int32 SelSlot      = -1;   // `*(WORD*)(gap_34 + 5)`
	int32 UsedBlocks   = 0;    // эталонный `slotColumn` — индекс ПОСЛЕДНЕГО занятого блока
	bool  bPanelVisible = false;
	// ★ 31.10.2026: опрос мыши для щелчка по ячейкам (фронт нажатия ЛКМ = эталонное событие 0xB).
	FAsdaMouseFrame Mouse;
	// ★ 31.10.2026: кадр последнего заполнения (`GFrameCounter`) — нажатие, которое открыло панель (щелчок по набору в окне
	//   «Вид»), в том же кадре щелчком по её ячейке не считается.
	uint64 PopulateFrame = 0;
	bool  bReady        = false;

	// ★ Защёлка отложенной отправки: эталонные `gap_C64[84]` (взведено) и `[85]` (разрешение).
	//   Одноразовая: кнопка ставит обе, тик стреляет и разоружает.
	bool bSendArmed  = false;
	bool bSendAllow  = false;

	// Нехватка ячеек, посчитанная перед отправкой (эталонные `nNoticeArg1` @0xCCC — общий инвентарь,
	//   `nNoticeArg2` @0xCD0 — сумка). Подставляются в нотис 99306 как [sd1] / [sd2].
	int32 ShortSpend = 0;
	int32 ShortBag   = 0;

	int32 CellHovered = -1;   // сплющенный индекс ячейки под курсором (владение подсказкой)

	// Счётчики для консольного дампа.
	int32 CntPopulate = 0;
	int32 CntSent     = 0;
};
