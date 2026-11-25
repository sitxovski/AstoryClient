// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaRepairSubsystem — ОКНО РЕМОНТА СНАРЯЖЕНИЯ: `INVEN_Repair_Win` (JUI IDNum **57300**).
//   Открывается кнопкой диалога NPC **30069** («Ремонт вещи»).
//   Полный реверс потока с адресами — `Reports/repair_and_warshop_port.md` §1.
//   Полный структурный разбор контролов — `Docs/UI/Craft/INVEN_Repair_Win.md`.
//
// ═══════════════════ ★★★ ИСПРАВЛЕННЫЕ МИСНЕЙМЫ (важно для чтения эталона) ════════════════════
//   • `uiShowUniconWarWindow@0xEDA280` — это НЕ окно войны, а ПОКАЗ ОКНА РЕМОНТА;
//     глобал `g_pShopSellWnd` — объект этого окна (0x2FFB110), а не магазина.
//   • `uiRepairWindowToggle@0xBC2CE0` с полями `repairPanel`/`repairHonorText` — НЕ ремонт:
//     его поле-редактор читает `netSendPasswordPacket@0xBC8A10` и шлёт `OP_CLAN_CREATE_REQ`,
//     то есть это окно СОЗДАНИЯ КЛАНА. Ложная зацепка снята.
//   • `honorPoints_resetDisplayIfZero@0xBC2EF0` / `uiSetHonorPointsDisplay@0xB75D40` — это ЗОЛОТО
//     (`g_pGameClient+770234` число, `+3080940` строка), а не очки чести. Подпись поля 57325 —
//     нотис 13241 «Золото:», подтверждения 110312/110313/110314 говорят «за %d золота»,
//     отказ ответа — 2050 «Недостаточно золота.».
//   ⇒ **РЕМОНТ В ЭТОЙ ИГРЕ СТОИТ ТОЛЬКО ЗОЛОТА.** Очки чести/«Монеты славы» к нему отношения не имеют.
//
// ═══════════════════ ЧТО ДЕЛАЕТ ОКНО ═══════════════════════════════════════════════════════════
//   Слева сетка из 11 НАДЕТЫХ предметов (контейнер 3, ячейки 57311…57321 — эталон кладёт предмет
//   слота j в ячейку j, БЕЗ уплотнения), справа сетка из 30 предметов СУМКИ СНАРЯЖЕНИЯ
//   (контейнер 1, ячейки 57331…57360 — здесь уплотнение ЕСТЬ: подходящие предметы кладутся
//   подряд с нулевой ячейки). Наведение показывает тултип предмета, стоимость его ремонта и
//   полный блок «цена / скидка-наценка города / итог». Клик по ячейке чинит ОДИН предмет,
//   кнопка 57305 — всё надетое, кнопка 57306 — надетое И сумку.
//
// ═══════════════════ ФОРМУЛА ЦЕНЫ (1:1, `uiEquipDurabilityPanelRefresh@0xED9830`) ══════════════
//       цена = (maxDur − curDur) / maxDur × price × (0.02 × заточка + 0.1)
//   maxDur = `ItemRecord->field68` (+0x44, у нас `FAsdaItemRecord::DurabilityMax`),
//   curDur = поле записи **+13**, price = `ItemRecord->price` (+0xA4), заточка = поле записи **+24**.
//   `maxDur == 0` ⇒ цена 0 (предмет не ремонтируется).
//   Гейт вида предмета одинаков у обеих сеток:
//       !(subType==10 && variant==1) && !(subType==10 && variant==2)
//       && (itemType <= 4 || itemType == 8 || itemType == 9)
//
// ═══════════════════ СУММЫ ════════════════════════════════════════════════════════════════════
//   `filler[572]` — сумма ремонта НАДЕТОГО; `filler[576]` — сумма ремонта ВСЕГО, и эталон
//   ИНИЦИАЛИЗИРУЕТ её значением `filler[572]` (`uiRefreshItemIconList`: `*(this+144) = *(this+143)`),
//   то есть «всё» = надетое + сумка. Это НЕ дефект: кнопка 57306 подписана нотисом 110320
//   «Общий ремонт», а пакет режима 3 действительно перечисляет 11 надетых и 30 ячеек сумки.
//
// ═══════════════════ ПАКЕТЫ ═══════════════════════════════════════════════════════════════════
//   **6571** (147 б, тело 123) `net_SendSlotBatchReq@0xEDA7A0` → **6572** (тело 170)
//   `netHandleStateArrayPacket@0xEDB2B0`. Раскладки — у объявлений в `AsdaNetSubsystem.h`.
//   Пока ответ не пришёл, стоит флаг `bPending` (эталонное поле `filler[580]`), и ни новое
//   подтверждение, ни повторный запрос не уходят.
//
// ═══════════════════ ОТСТУПЛЕНИЯ ══════════════════════════════════════════════════════════════
//   Контролы 57327 (подпись «Стоимость») и 57328 (значение) в `.JUI` объявлены БЕЗ строкового
//   `ID`. Импортёр создал их хит-зоны `NoImage_*_HIT` и, поскольку в `.JUI` у них есть образец
//   текста, строки `INVEN_Repair_Win_TXT00/01` (имя по якорю окна) в той же точке. Код находит
//   хит-зоны по размеру 60×14 и берёт СТРОКИ АССЕТА той же геометрии (`AsdaUi::FindOwnLabelLike`,
//   10.10.2026); рантайм-строка строится, только если строки ассета нет.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "UI/AsdaBagListScroll.h"   // ★ 29.10.2026 (инвентарь ×2): FAsdaBagListScroll — окно 30 позиций над списком до 60
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaRepairSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UAsdaIconCellGrid;
class UAsdaButtonRelay;

// Виджеты одной ячейки сетки: план-иконка `{idc}_{juiIdx}_0` + одноимённый `TextBlock` («+N%»).
USTRUCT()
struct FAsdaRepairCellUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UImage>     Icon  = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Label = nullptr;
};

// Одна позиция сетки: что за предмет и сколько стоит его ремонт.
USTRUCT()
struct FAsdaRepairEntry
{
	GENERATED_BODY()
	UPROPERTY(Transient) int32 ItemCode  = 0;    // 0 ⇒ ячейка пуста
	UPROPERTY(Transient) int32 Container = 0;    // поле записи +4
	UPROPERTY(Transient) int32 Slot      = -1;   // поле записи +5
	UPROPERTY(Transient) int32 CellIndex = -1;   // индекс ячейки в контейнере (для тултипа)
	UPROPERTY(Transient) int32 Cost      = 0;    // цена ремонта БЕЗ скидки/наценки города

	bool IsEmpty() const { return ItemCode <= 0; }
	void Clear() { ItemCode = 0; Container = 0; Slot = -1; CellIndex = -1; Cost = 0; }
};

UCLASS()
class ASDAREBORN_API UAsdaRepairSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaRepairSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа окна (все из `.JUI` и `uiEquipUpgradePanelInit`, ни одно не выдумано) ──
	static constexpr int32 kEquipCells = 11;   // 57311…57321, контейнер 3
	// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): «сетка окна» и «предел сумки» разделены. Сетка 57331…57360 — по-прежнему 30
	//   ячеек 6×5 (`.JUI`), а перебор сумки и уплотнённый список `Bag` — вся сумка (`kShellSlots` = 60); хвост списка
	//   листается колесом по рядам (`BagScroll`).
	static constexpr int32 kBagGridCells = 30; // 57331…57360 — ячеек сетки сумки в окне
	static constexpr int32 kShellCells = 60;   // предел перебора сумки = `UAsdaInventorySubsystem::kShellSlots` (static_assert в .cpp; ★ U21: было `kBagCells`)
	static constexpr int32 kRepairBatch = 41;  // записей в ОДНОМ 6571 (тело фиксировано: 41 × u8 контейнер + 41 × i16 слот)
	static constexpr int32 kCellPx     = 32;   // Size = "32 32" у обеих сеток
	static constexpr int32 kRepairZ    = 731;  // выше инвентаря (720) — как магазин (730)

	// Режимы эталона (`itemSlotSelect@0xEDA720`, поле `filler[564]`).
	static constexpr int32 kModeOne   = 1;   // один выбранный предмет
	static constexpr int32 kModeEquip = 2;   // всё надетое           (кнопка 57305)
	static constexpr int32 kModeAll   = 3;   // надетое и сумка       (кнопка 57306)

	// ═══ ОТКРЫТИЕ / ЗАКРЫТИЕ ═══
	// Эталон `uiShowUniconWarWindow@0xEDA280` (показ) и `uiEquipUpgradePanelHide@0xEDA590`.
	UFUNCTION(BlueprintCallable, Category="Asda|Repair")
	void OpenWindow();

	UFUNCTION(BlueprintCallable, Category="Asda|Repair")
	void CloseWindow();

	UFUNCTION(BlueprintPure, Category="Asda|Repair")
	bool IsOpen() const { return bOpen; }

	// ═══ ДЕЙСТВИЯ (в эталоне — обработчики контролов) ═══
	void OnCellClicked(int32 Cell, bool bBag);   // `itemHandleSlotClickMsg@0xEDA600`
	void OnRepairEquipClicked();                 // 57305 → `itemSlotSelect(…, 0, 2)`
	void OnRepairAllClicked();                   // 57306 → `itemSlotSelect(…, 0, 3)`

	// ═══ ДИАГНОСТИКА ═══
	FString DumpControls() const;   // КАЖДЫЙ разобранный контрол (закон full-structural-analysis)
	FString DumpState() const;      // обе сетки, суммы, режим, блок войны

private:
	// ── Жизненный цикл окна ──
	bool EnsureWidget();
	void ResetWidget();
	void SetupControls();
	void RegisterZone();
	// Подпись контрола по прямоугольнику его хит-зоны (57327/57328, у которых в `.JUI` нет строкового `ID` —
	//   см. шапку файла): строка ассета той же геометрии, а если её нет — новый `TextBlock`.
	UTextBlock* MakeLabelLike(UWidget* Ref, const TCHAR* NewName, const UTextBlock* FontDonor);

	// ── Наполнение и рендер ──
	void RebuildAll();          // `uiEquipPanelRefreshAll@0xED97C0`
	void RebuildEquip();        // `uiEquipDurabilityPanelRefresh@0xED9830`
	void RebuildBag();          // `uiRefreshItemIconList@0xED9D50`
	void RefreshGoldText();     // 57326
	void RefreshPriceTexts();   // 57328 + весь блок 79400…79406 по текущей `HoverCost`
	void HideSelectionFrame();
	void MoveSelectionFrame(const UWidget* OverIcon);

	// ── Цена ──
	// 1:1 формула §«ФОРМУЛА ЦЕНЫ». Возвращает 0, если предмет не ремонтируется.
	int32 CostOfRecord(const struct FAsdaItemInstance& It) const;
	// Проходит ли предмет гейт вида (тот же у обеих сеток).
	bool  IsRepairable(int32 ItemCode) const;
	// `uniconWar_ApplyOptionPercent@0xBCEEC0` с эталонным `a2 = 0` (скидка своим, наценка чужим).
	int32 ApplyWarPercent(int32 Value) const;

	// ── Подтверждение и сеть ──
	// `itemSlotSelect@0xEDA720` + ветка кода 73 в `CItem_BuildTooltipText@0xD6E790`.
	void AskConfirm(int32 Mode, const FAsdaRepairEntry* One);
	void SendRepair();                              // `net_SendSlotBatchReq@0xEDA7A0`
	void OnRepairResult(const TArray<uint8>& Body); // `netHandleStateArrayPacket@0xEDB2B0`
	void DrainPendingPackets();

	// ── Покадровые опросы ──
	void TickCells();        // наведение/клик по обеим сеткам (по ЖИВОЙ геометрии)
	void TickButtonTips();   // `uiShowTutorialPopupByCode@0xEDC2E0` для 57305/57306

	// ── Нотисы ──
	FString Loc(int32 LocId) const;
	void NoticeMsgBox(int32 LocId) const;           // ONEBUT_MSGBOX 1500

	// ═══ СОСТОЯНИЕ ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TArray<FAsdaRepairCellUi> EquipUi;   // 11
	UPROPERTY(Transient) TArray<FAsdaRepairCellUi> BagUi;     // 30 (видимые ячейки сетки `kBagGridCells`)
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose      = nullptr;   // 57369
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnRepairEquip= nullptr;   // 57305
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnRepairAll  = nullptr;   // 57306
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGoldLabel  = nullptr;   // 57325 (13241)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGold       = nullptr;   // 57326
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCostLabel  = nullptr;   // 57327 (110306) — строка ассета TXT00
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCostValue  = nullptr;   // 57328          — строка ассета TXT01
	UPROPERTY(Transient) TObjectPtr<UWidget>    WarGroup      = nullptr;   // 79400 (группа блока)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtWarBaseLbl = nullptr;   // 79401 (111922)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtWarBase    = nullptr;   // 79402
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtWarPct     = nullptr;   // 79403 (111915/111916)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtWarDelta   = nullptr;   // 79404
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtWarTotLbl  = nullptr;   // 79405 (111923)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtWarTotal   = nullptr;   // 79406
	UPROPERTY(Transient) TObjectPtr<UWidget>    SelFrame      = nullptr;   // 57362 (Slot_AniSelect)
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridEquip = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridBag   = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	// ── Модель ──
	bool bOpen = false;             // `filler[556]`
	bool bControlsReady = false;    // `filler[581]`
	bool bPending = false;          // `filler[580]` — ждём 6572
	// Инвентарь изменился — окно надо пересобрать (метит колбэк, применяет Tick).
	bool bDirty = false;
	// Окно подтверждения (TWOBUT_MSGBOX 1520) сейчас открыто — второго не открываем.
	bool bConfirmOpen = false;
	TArray<FAsdaRepairEntry> Equip; // 11 (индекс = слот надетого, БЕЗ уплотнения)
	TArray<FAsdaRepairEntry> Bag;   // до 60 (уплотнено, как у эталона; ★ 29.10: вся сумка, видимое окно — `BagScroll`)
	// ★ [ПОРТ] 29.10.2026: окно сетки сумки над `Bag` (ряд прокрутки колесом; при ≤ 30 вещах ряд всегда 0).
	FAsdaBagListScroll BagScroll;
	// ★ [ПОРТ] 29.10.2026: хвост «починить всё» сверх 41 записи (11 надетых + до 60 сумки = до 71) — уходит ВТОРЫМ
	//   пакетом 6571 после ответа 6572 на первый (протокол не меняется: тело 6571 фиксировано на 41 запись).
	TArray<uint8> RepairTailCont;   // контейнеры оставшихся записей
	TArray<int32> RepairTailSlots;  // слоты оставшихся записей
	int32 RepairChunksSent = 0;     // сколько пакетов 6571 ушло в текущем «починить всё» (диагностика/самотест)
	int32 TotalEquip = 0;           // `filler[572]`
	int32 TotalAll   = 0;           // `filler[576]` = надетое + сумка
	int32 HoverCost  = 0;           // `filler[568]` — цена предмета под курсором
	int32 SelMode    = 0;           // `filler[564]`
	FAsdaRepairEntry SelOne;        // `filler[560]` — выбранная одиночная позиция
	int32 TipCell = -1;             // ячейка, по которой показан тултип (−1 = нет)
	bool  bTipBag = false;
	int32 TipButton = -1;           // 57305/57306 — по какой кнопке показана подсказка

	FAsdaMouseFrame Mouse;
	// Очередь пришедших пакетов (закон `no-reentrant-net-callbacks`).
	TAsdaNetQueue<TArray<uint8>> PendingResults;
	FDelegateHandle HRepairResult;
	FDelegateHandle HCityOwner;
	FDelegateHandle HInvCell;

	friend struct FAsdaInvX2SelfTest;   // ★ 29.10.2026: самотест «инвентарь ×2» — ремонт > 41 записи двумя пакетами, прокрутка сетки
};
