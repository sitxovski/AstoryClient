// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaWarehouseSubsystem — СКЛАД: два окна JUI, `WARE_WIN` (IDNum **4000**) и `Warehouse_Win`
//   (IDNum **5500**, «свиток возврата склада»).
//   Полный реверс клиентского UI со ссылками `файл:строка`/`@0x…` — `Reports/warehouse_client_ui.md`
//   (18 разделов) и приложение с дампом всех 715 узлов — `warehouse_client_ui_jui.md`.
//   Сетевой разбор — `Reports/warehouse_net_server.md`. Нумпад — `Docs/Port/systems/NUMPAD_FLOW.md`.
//
// ═══════════════════ ЧТО ЭТО ЗА ОКНА ══════════════════════════════════════════════════════════
//   **4000 WARE_WIN** — само окно склада: сетка 6×5 = 30 ячеек (4008…4037), КОРЗИНА ПЕРЕНОСА
//   из 5 ячеек (4039…4043), кнопки «Хранить» 4002 / «Забрать» 4049, крестик 4050, счётчик
//   «занято/всего» 4003, комиссия 4045, деньги игрока 4047, переключатель категорий 4188,
//   список-прокрутка 4189 и ВОСЕМЬ отдельных плавающих окон-сумок (4052/4086/4120/4154/
//   75400/75500/75600/75700), каждое со своими 30 ячейками, крестиком, подписью и SEL-рамкой.
//   **5500 Warehouse_Win** — маленькое всплывающее окно СВИТКА ВОЗВРАТА СКЛАДА: заголовок 5501
//   (он же ручка перетаскивания), «Забрать» 5502, «Хранить» 5503, «Отмена» 5504 и крестик 5509.
//   ⚠ У NPC-кладовщика окно 5500 НЕ показывается: там окно 4000 открывают команды диалога
//   **30011** («Хранить», режим 1) и **30012** («Забрать», режим 0).
//
// ═══════════════════ ★★★ ИСПРАВЛЕННЫЕ МИСНЕЙМЫ (§14 отчёта) ═══════════════════════════════════
//   • `CStore_SendWithdraw` (тунк @0x91F1AD) → `Send_CharItemKeeping@0xE8C3A0`, пакет **5008**
//     = ПОЛОЖИТЬ (кнопка 4002 «Keep»). Имя врёт ровно наоборот.
//   • `CStore_SendDeposit` (тунк @0x9200B7) → `Send_CharItemRetake@0xE8BAF0`, пакет **5010**
//     = ЗАБРАТЬ (кнопка 4049 «Get»).
//   • `warehouseSlot_OnRightClick@0xE8CCE0` — ПРАВОГО КЛИКА У СКЛАДА НЕТ ВООБЩЕ. Это «спросить
//     количество для корзины», зовётся из двойного щелчка (0xD/0xF) и из броска в корзину (7).
//     Правильное имя — `warehouseAskQuantityForBasket`.
//   • `quickslotPlaceItem@0xE8B4B0` / `quickslotContextPlaceItem@0xE8CC90` — кладут в КОРЗИНУ
//     ПЕРЕНОСА СКЛАДА (`this+22032`), к панели быстрого доступа отношения не имеют.
//   • `selSlotIndex` (+0x564) — это РЕЖИМ ОКНА (−1 закрыто, 0 «забрать», 1 «хранить»).
//   • `pKeeperMoneyText` (4045) — КОМИССИЯ, `pKeeperMoney2Text` (4047) — ДЕНЬГИ ИГРОКА.
//   • `inventoryChangeBagPage@0xE8E440` — меняет ВМЕСТИМОСТЬ СКЛАДА, а не страницу инвентаря.
//   • `netSendBuyRequest5000@0xE884A0` — не покупка, а `OP_CHAR_ITEM_MOVE`.
//
// ═══════════════════ ДВА РЕЖИМА ОКНА 4000 (§4.4, `uiInventoryBagOpen@0xE8CD60`) ════════════════
//   Режим **0 «ЗАБРАТЬ»** (надпись 4038 = нотис 2248): сетка 30 ячеек показывает СКЛАД (вкладка 0),
//     виден блок вкладок 4001 со всем поддеревом и кнопка 4049; скрыты 4188, 4002, 4189;
//     комиссия 4045 пересчитывается.
//   Режим **1 «ХРАНИТЬ»** (надпись 4038 = нотис 2249): сетка показывает ВАШ ИНВЕНТАРЬ
//     (уплотнённую копию), видны 4189 (прокрутка), 4188 (категории) и кнопка 4002;
//     блок вкладок 4001 гасится ЦЕЛИКОМ, все 8 окон-сумок принудительно закрываются,
//     4188 сбрасывается на пункт 0.
//   ★ `WAREKEEP_TOP` (4001) — КОНТЕЙНЕР всего блока вкладок: подпись 4190, подложки
//     `WareUi_BagBase`/`WareUi_BagBase_2` (4195) и обе четвёрки чек-кнопок 4004…4007/4301…4304 —
//     ЕГО ДЕТИ (иерархия JUI расплющена). Поэтому гасим/показываем ПОДДЕРЕВО по узлу-группе
//     `WAREKEEP_TOP_grp`, а «вечно скрытые» авторские заглушки держим отдельным списком
//     `AlwaysHidden` и гасим ПОСЛЕ общего показа (закон `jui-uneven-node-layout`).
//
// ═══════════════════ КОРЗИНА ПЕРЕНОСА ═════════════════════════════════════════════════════════
//   Прямого перетаскивания «инвентарь ↔ склад» НЕТ (§7). Обмен идёт ТОЛЬКО через корзину из
//   5 ячеек (4039…4043) и кнопку «Keep»/«Get». Запись корзины — ПОЛНАЯ 57-байтовая копия записи
//   источника, у которой поле +9 (количество) заменено на «сколько переносим»
//   (`quickslotPlaceItem`: `memcpy(корзина, источник, 0x39)`, затем `корзина+22041 = a4`,
//   а `22041 − 22032 = 9`). Источник опознаётся полями самой записи: +4 контейнер, +5 слот.
//   Пакет **5000** (`DstType = 5`) работает ТОЛЬКО МЕЖДУ ЯЧЕЙКАМИ СКЛАДА.
//
// ═══════════════════ ★ НУМПАД (окно 1410), РЕЖИМ 4 — РОВНО ОДНО МЕСТО ═════════════════════════
//   `warehouseAskQuantityForBasket@0xE8CCE0`: `uiOpenContextPopupWithHandlers(g_pNumPadDialog,
//   количество, /*режим*/4, 0)`. ОК → `warehouseBasket_ConfirmQty@0xE8CC90`, отмена → тот же
//   вызов с −1, который НИЧЕГО не делает. Гейт открытия (`inventory_OnItemSlotMoveOrSplit`
//   @0xE8B240, item_inventory.c:58668-58707): предмет не «привязанный камень души»
//   (`itemSubType == 32 && запись[50] == 3` — иначе нотис 2247 в чат), класс 0/3, `tradableFlag != 0`,
//   `flagWord176 == 0`, количество > 1. Самоцвет (класс 1) и спец-предмет (класс 2) вопроса не задают.
//   В порте роль «режима» играет колбэк `UAsdaWindowSubsystem::ShowNumPad`.
//
// ═══════════════════ СЕТЬ (§9) ════════════════════════════════════════════════════════════════
//   4050 запрос страницы (`SendStoreHousePageReq4050`, PageNo = вкладка + 1, 1…9) →
//     4051 записи + 4052 конец; содержимое приезжает как КОНТЕЙНЕР 5 в `UAsdaInventorySubsystem`.
//   5008 положить → 5009 (коды 0,2,3,4,5,6,7 — отказы); 5010 забрать → 5011 (0,2,3,4,5).
//   5000 перенос между ячейками склада → 5001. 5456 расширение вместимости.
//
// ═══════════════════ ЗВУКОВ И ЭФФЕКТОВ У ОКНА НЕТ ═════════════════════════════════════════════
//   §11 отчёта: скан всех 78 функций класса `0xE85650…0xE8EB10` регуляркой `Sound|PlaySfx|…`
//   даёт 0 совпадений, второй метод — скан всего UI-фреймворка. Единственная анимация —
//   подмена `Ware_Gear` (4191) на `Ware_AniGear` (4192) при наведении на крестик 4050.
//   Единственные звуки в потоке — СВОИ звуки нумпада (открытие 5, закрытие 6).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaInventorySubsystem.h"   // FAsdaItemInstance — записи склада/корзины (57 байт)
#include "UI/AsdaScrollBarNomal.h"       // полоса прокрутки 4189 (в WBP её нет, строим из скина)
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "Layout/Margin.h"         // FMargin
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "UI/AsdaShopDblClick.h"   // ★ U1 (В2-A дедупа №2): FAsdaDblClick — двойной щелчок по порогу Windows
#include "AsdaWarehouseSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UCheckBox;
class UTextBlock;
class UAsdaAnimatedPlane;
class UAsdaButtonRelay;
class UAsdaIconCellGrid;

// Виджеты ОДНОЙ ячейки: план-иконка `{idc}_{juiIdx}_0` и одноимённый `TextBlock` (счётчик/«+N%»).
USTRUCT()
struct FAsdaWareCellUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UImage>     Icon  = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Label = nullptr;
};

// Виджеты ОДНОГО плавающего окна-сумки (вкладки 1…8).
USTRUCT()
struct FAsdaWareBagUi
{
	GENERATED_BODY()
	// Корневая группа окна-сумки `WAREBAGn_WIN_grp` — ЕЁ поддерево и показываем/гасим.
	UPROPERTY(Transient) TObjectPtr<UWidget>            Root  = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton>            Close = nullptr;   // WAREBAGn_CLOSE
	UPROPERTY(Transient) TObjectPtr<UTextBlock>         Title = nullptr;   // WAREBAGn_TXT00
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> Sel   = nullptr;   // WAREBAGn_SEL
	UPROPERTY(Transient) TObjectPtr<UCheckBox>          Btn   = nullptr;   // 4004…4007 / 4301…4304
	// Все узлы поддерева окна-сумки (по префиксу имени) — закон `jui-uneven-node-layout`.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    Parts;
	// Узлы, которые не должны показываться НИКОГДА (SEL-рамка): гасим ПОСЛЕ общего показа.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    AlwaysHidden;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid>  Grid  = nullptr;
	// Дизайн-позиция окна из `.JUI` (она же эталонный элемент `aTabSlotPos`).
	FVector2D DesignPos = FVector2D::ZeroVector;
};

// ★★★ РАСКЛАДКА ОКОН-СУМОК БЕРЁТСЯ ИЗ ДАННЫХ `.JUI`, А НЕ ИЗ АССЕТА (правка 2026-08-27).
//   ЧТО БЫЛО: позиция снималась со слота импортированного `WBP_WARE_WIN`
//   (`DesignPos = CS->GetOffsets()`), а размер не задавался вовсе. Импортёр в этом окне
//   РАСПЛЮЩИЛ ИЕРАРХИЮ (закон `jui-flattened-hierarchy-geometry`): окна-сумки в `.JUI` — ДЕТИ
//   окна 4000, их `Origin` задан ОТНОСИТЕЛЬНО родителя, а в ассете они оказались в общем канвасе
//   с теми же числами. В итоге восемь окон вставали не на свои места, налезали друг на друга и
//   на само окно склада, а их размер брался произвольным из ассета.
//   КАК СТАЛО: и позиция, и размер прописываются из `gamemain_jui.ini` (сверено байт в байт):
//   все восемь окон 203×167, сетка 3 колонки × 3 ряда, `Origin` — относительно окна 4000 (39,56).
//   Если импортёр всё же сохранил вложенность, позиция берётся как есть; если расплющил —
//   к ней прибавляется позиция окна 4000. Выбор делается в рантайме по фактическому родителю.
struct FAsdaWareBagLayout
{
	int32     IdNum;      // idnum окна-сумки в `.JUI`
	FVector2D Origin;     // `Origin` из `.JUI` — ОТНОСИТЕЛЬНО окна 4000
	FVector2D Size;       // `Size`   из `.JUI`
};
// `gamemain_jui.ini`: WAREBAG1..8_WIN. Порядок — номер сумки 1…8.
static const FAsdaWareBagLayout kWareBagLayout[8] = {
	{ 4052,  FVector2D(236.f,  24.f), FVector2D(203.f, 167.f) },
	{ 4086,  FVector2D(236.f, 217.f), FVector2D(203.f, 167.f) },
	{ 4120,  FVector2D(237.f, 410.f), FVector2D(203.f, 167.f) },
	{ 4154,  FVector2D(442.f,  24.f), FVector2D(203.f, 167.f) },
	{ 75400, FVector2D(442.f, 217.f), FVector2D(203.f, 167.f) },
	{ 75500, FVector2D(443.f, 410.f), FVector2D(203.f, 167.f) },
	{ 75600, FVector2D(647.f,  23.f), FVector2D(203.f, 167.f) },
	{ 75700, FVector2D(648.f, 216.f), FVector2D(203.f, 167.f) },
};
// `WARE_WIN` (4000) `Origin = "39 56"` — нужен, когда иерархия расплющена.
static const FVector2D kWareWinOrigin(39.f, 56.f);
// ★ 10.10.2026: дизайн-ширина окна 4000 по рамке (`WARE_WIN_0_0` x 39 … `WARE_WIN_0_2` x 247 + 20 = 267).
static constexpr float kWareWinDesignWidth = 228.f;

UCLASS()
class ASDAREBORN_API UAsdaWarehouseSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaWarehouseSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа окна. ВСЕ из `.JUI` и из тел `CWarehouseUI_Build`/`Warehouse_InitControls`. ──
	static constexpr int32 kGridCells   = 30;   // 4008…4037 — сетка вкладки 0 / страница инвентаря
	static constexpr int32 kGridCols    = 6;    // «страница» прокрутки = 1 ряд = 6 ячеек
	static constexpr int32 kGridRows    = 5;
	static constexpr int32 kBasketCells = 5;    // 4039…4043 — КОРЗИНА ПЕРЕНОСА (эталонная «вкладка 9»)
	static constexpr int32 kBags        = 8;    // 8 плавающих окон-сумок = вкладки 1…8
	static constexpr int32 kWareBagCells = 30;   // ячеек в каждой сумке СКЛАДА (не инвентаря; ★ U21 (В1 дедупа №2): было `kBagCells`)
	static constexpr int32 kStoreTabs   = 9;    // вкладок хранения (0 + 8), 9×30 = 270 ячеек
	static constexpr int32 kStoreSlots  = kStoreTabs * kWareBagCells;   // 270 — потолок склада
	static constexpr int32 kCellPx      = 32;   // Size = "32 32" у всех ячеек
	// ★ Комиссия за ОДНУ занятую ячейку корзины (`CStore_UpdateDepositFeeText@0xE8BA10`:
	//   `for (i<5) if (корзина[i].код > 0) v5 += 30;`).
	static constexpr int32 kFeePerItem  = 30;
	// Категории переключателя 4188: 0 = «Обычные/Самоцветы» (контейнер 2, 60), 1 = «Снаряжение/
	//   Премиум» (контейнер 1, 30) — `uiInitSlotGridPanel@0xE8D680`.
	static constexpr int32 kCatSpend    = 0;
	static constexpr int32 kCatShell    = 1;
	// Режимы окна (эталонное поле +0x564).
	static constexpr int32 kModeClosed  = -1;
	static constexpr int32 kModeTake    = 0;    // «Забрать» (кнопка NPC 30012, кнопка 5502)
	static constexpr int32 kModeKeep    = 1;    // «Хранить» (кнопка NPC 30011, кнопка 5503)
	// ★ Z-ПОРЯДОК. Склад обязан лежать ВЫШЕ окна разговора с NPC и его портрета, поэтому оба окна
	//   переводятся в ПРИОРИТЕТНУЮ ПОЛОСУ [8800…8899] (`UAsdaWindowSubsystem::SetWindowPriority`).
	//   Запрошенный z внутри полосы назначает менеджер; здесь — только стартовые значения.
	static constexpr int32 kWareZ   = 8802;
	static constexpr int32 kRecallZ = 8803;   // окно свитка — поверх окна склада

	// ═══════════════════ ПУБЛИЧНЫЙ API ═══════════════════
	/** Открыть окно 4000 в режиме `Mode` (0 «Забрать» — код диалога NPC 30012 и кнопка 5502,
	 *  1 «Хранить» — код 30011 и кнопка 5503). Эталон `warehouseUI_BuildAndOpenBag@0xE89AC0`:
	 *  строит UI при надобности и, если запрошенный режим ≠ текущему, зовёт `uiInventoryBagOpen`. */
	UFUNCTION(BlueprintCallable, Category="Asda|Warehouse")
	void OpenWindow(int32 Mode);

	/** Крестик 4050 = `storageUIClearTab(this, 0)`: скрыть 4000 и все 8 окон-сумок, снять чек-кнопки,
	 *  ВЕРНУТЬ содержимое корзины владельцам, `nMode = −1`. Пакет НЕ шлётся. */
	UFUNCTION(BlueprintCallable, Category="Asda|Warehouse")
	void CloseWindow();

	/** Окно склада 4000 сейчас открыто (эталонное `nMode != −1`). Гейт для чужих окон. */
	UFUNCTION(BlueprintPure, Category="Asda|Warehouse")
	bool IsOpen() const { return Mode != kModeClosed; }

	/** Открыть маленькое окно 5500 «свиток возврата склада» (эталон `uiWindowOpen_EA20@0xE8EA20`):
	 *  показать 5500, поднять его в менеджере окон, ЗАКРЫТЬ окно персонажа и окно инвентаря,
	 *  поднять признак `isOpen` (+0x5734) — с этого момента работают все блокировки §12. */
	UFUNCTION(BlueprintCallable, Category="Asda|Warehouse")
	void OpenRecallPanel();

	/** `uiHidePanelAndChild@0xE8EAB0`: скрыть 5500 И 4000, снять `isOpen`. */
	UFUNCTION(BlueprintCallable, Category="Asda|Warehouse")
	void CloseRecallPanel();

	/** ★★★ ПРИЗНАК «УДАЛЁННЫЙ СКЛАД АКТИВЕН» (+0x5734). Его читают 30 мест в 25 функциях (§12):
	 *  движение, атака, клик по земле, телепорт по карте, приглашение в группу, обмен, хотбар,
	 *  операции инвентаря, диалог NPC, окно персонажа, журнал заданий.
	 *  ⚠ Ставится ТОЛЬКО свитком: у NPC-кладовщика склад ничего не блокирует. */
	UFUNCTION(BlueprintPure, Category="Asda|Warehouse")
	bool IsRecallActive() const { return bRecallActive; }

	/** ★ 05.11.2026 [ИСПРАВЛЕНИЕ] Гейт чужих окон «склад» = эталонное `CObject_GetField5581(g_pWarehouseWnd)` — флаг СВИТКА (+0x5734),
	 *  а не «окно 4000 открыто». Ключ `asda.Warehouse.GateRecallFlag`: 1 — `IsRecallActive()`, 0 — прежнее `IsOpen()` порта.
	 *  Читатели: гейты обмена (меню и «Принять») и ремесла. */
	bool IsGateField5581() const;

	/** Нотисы блокировок §12 — чтобы каждое чужое окно не таскало номера строк у себя.
	 *  10305 движение · 10306 разговор с NPC · 10307 обмен · 10308 хотбар · 10309 инвентарь. */
	UFUNCTION(BlueprintCallable, Category="Asda|Warehouse")
	void NoticeRecallBlocked(int32 LocId) const;

	/** «Уход от NPC» — эталон `storageUIResetAll@0xE88BC0` в хвосте `UI_resetAllWindowsAndLeave_5045`:
	 *  скрыть 4000 и все вкладки, вернуть корзину, снять подсветку, `nMode = −1`, убрать тултип.
	 *  От `CloseWindow` отличается только тем, что зовётся снаружи (пакетов не шлёт ни та, ни эта). */
	UFUNCTION(BlueprintCallable, Category="Asda|Warehouse")
	void ResetAllOnNpcLeave();

	// ═══ ДЕЙСТВИЯ (в эталоне — ветки `uiStore_handleEvent@0xE87790`) ═══
	/** Событие 0xD/0xF — ДВОЙНОЙ ЩЕЛЧОК по ячейке. `CtrlId` — idnum контрола (4008…, 4039…, 4055… и т.д.),
	 *  `DstCtrlId` = −1 (приёмник подбирается сам). Эталон `inventory_OnItemSlotMoveOrSplit@0xE8B240`. */
	void OnCellDoubleClick(int32 CtrlId);
	/** Кнопка 4002 «Хранить» → пакет 5008 (`Send_CharItemKeeping@0xE8C3A0`). */
	void OnKeepClicked();
	/** Кнопка 4049 «Забрать» → локальная проверка «деньги ≥ комиссии» (иначе нотис 2245),
	 *  затем пакет 5010 (`Send_CharItemRetake@0xE8BAF0`). */
	void OnTakeClicked();
	/** Чек-кнопка вкладки: `bOn` = эталонное событие 67073, иначе 67074. `Tab` = 1…8. */
	void OnBagButtonToggled(int32 Tab, bool bOn);
	/** Переключатель категорий 4188 (событие 66817) → `uiInitSlotGridPanel(this, индекс)`. */
	void OnCategoryChanged(int32 Category);
	/** Прокрутка списка инвентаря (событие 23) → `uiInvSetPage`. Аргумент — НОМЕР РЯДА. */
	void SetScrollRow(int32 Row);
	int32 GetMaxScrollRow() const;
	// ★ 10.10.2026 (modal_lock3, самотест колеса `asda.UI.SelfTestModalLock`): текущий ряд прокрутки списка (`ScrollRow`).
	int32 GetScrollRow() const { return ScrollRow; }
	// ★ 10.10.2026 (whwheel): абсолютный прямоугольник области колёсного окна 4189 `WAREKEEP_LIST` (198×165 от левого
	//   верха ячейки 0, координаты Slate). Колесо листает список ТОЛЬКО над ним — эталон `CWheelWindow` (vtable
	//   `off_1507990`, слот 37 `sub_12F3480`) получает событие 20 лишь при попадании курсора в свой план. false — окна нет.
	bool GetKeepListAbsRect(FVector2D& OutMin, FVector2D& OutMax) const;

	// ═══ ДИАГНОСТИКА ═══
	FString DumpControls() const;   // КАЖДЫЙ разобранный контрол (закон full-structural-analysis)
	FString DumpState() const;      // режим, вкладки, сетка, корзина, комиссия, деньги

private:
	// ── Жизненный цикл окон ──
	bool EnsureWidget();            // 4000
	bool EnsureRecallWidget();      // 5500
	void ResetWidget();
	void SetupControls();
	void SetupRecallControls();
	void RegisterZones();

	// ── Раскладка/видимость ──
	// `uiInventoryBagOpen@0xE8CD60` — вся разница режимов 0/1 (§4.4).
	void ApplyMode(int32 NewMode);
	// `uiWnd_SetModeVisibility@0xE8EB90`: режим 2 — скрыть 4188/4002/4189, показать 4001/4049;
	//   режим 1 — показать 4189/4188/4002, скрыть 4001/4049.
	void ApplyModeVisibility(int32 EtalonVisMode);
	// Показать/погасить ПОДДЕРЕВО блока вкладок `WAREKEEP_TOP` (4001) со всеми детьми.
	void ShowTabBlock(bool bVisible);
	// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА D19: схлопнуть пустую полосу на месте блока вкладок
	//   в режиме «хранить» (эталон её оставляет — чёрный провал под заголовком).
	void ApplyTabAreaCollapse(bool bCollapse);
	// Фактическая экранная позиция левого-верхнего угла окна 4000 (для раскладки окон-сумок).
	FVector2D GetWareWinOrigin() const;
	// ★ Держит открытые окна-сумки привязанными к окну склада: если его перетащили — переставляет
	//   их на ту же дельту (в `.JUI` они были ДЕТЬМИ окна и ездили сами).
	void TickBagFollow();
	// ★ Подписи ДВУХ вкладок переключателя категорий 4188: в ассете подпись одна на обе кнопки,
	//   поэтому вторую создаём сами (иначе категория «Снаряжение» игроку не видна).
	void SetupCategoryCaptions();
	void RefreshCategoryCaptions();
	// Показать/погасить ПОДДЕРЕВО окна-сумки `Tab` (1…8).
	void ShowBagWindow(int32 Tab, bool bVisible);
	// Сколько кнопок-сумок видно: `uiInventoryBagOpen` ui_core.c:78840-78842 — `вместимость/30`,
	//   кнопки `k = 1 … v6−1`.
	void ApplyBagButtonVisibility();
	// `uiAssignControlToFreeSlot@0xE8D180` — занять первую свободную из 8 позиций `aTabSlotPos`.
	int32 AssignFreeSlotPos(int32 Tab);
	void ReleaseSlotPos(int32 Tab);
	// `warehouseDrawCurrentTab@0xE89D50` — приём 4052 «страница склада выдана целиком»:
	//   снять ожидание, пометить вкладку загруженной, показать её окно (для вкладок 1…8).
	void ApplyTabArrival();

	// ── Данные ──
	// Пересобрать ЛОКАЛЬНЫЕ копии (эталонный массив `aSlots`): склад 270 из контейнера 5,
	//   уплотнённый список инвентаря текущей категории, затем вычесть то, что лежит в корзине.
	void SyncMirrors();
	// `inventory_CompactSlots@0xE8D7F0` — уплотнить контейнер без дыр; ЗОЛОТО 20551 выбрасывается.
	void RebuildInvList();
	// Запись, стоящая за ячейкой-контролом `CtrlId` (эталон `warehouseGetSlotPtr@0xE891A0`).
	FAsdaItemInstance* SlotPtrByCtrl(int32 CtrlId);
	const FAsdaItemInstance* SlotPtrByCtrl(int32 CtrlId) const;
	// `itemCodeToGroupIndexA@0xE896C0`: idnum контрола → номер вкладки 0…8, 9 = корзина, −1 = не наш.
	static int32 GroupOfCtrl(int32 CtrlId);
	// `itemCodeCategorySlotIndex@0xE89850`: idnum → номер ячейки внутри группы.
	static int32 CellOfCtrl(int32 CtrlId);
	// Обратно: группа+ячейка → idnum контрола.
	static int32 CtrlOfGroupCell(int32 Group, int32 Cell);
	// Запись-ИСТОЧНИК записи корзины `B` (по её полям +4 контейнер и +5 слот).
	FAsdaItemInstance* ResolveBasketSource(const FAsdaItemInstance& B);
	// `itemListIsItemNotPresent@0xE8CBF0` — предмета ЕЩЁ НЕТ в корзине (сверка по коду И полю +5).
	bool IsNotInBasket(const FAsdaItemInstance& It) const;

	// ── Рендер ──
	void RefreshAll();
	void RefreshGrid();        // сетка 30 ячеек (склад в режиме 0, инвентарь в режиме 1)
	void RefreshBasket();      // 5 ячеек корзины
	void RefreshBag(int32 Tab);
	void RefreshStatusTexts(); // 4003 «%3d/%3d» + 4047 деньги (`uiUpdateStatusTexts@0xE8D090`)
	void RefreshFeeText();     // 4045 = 30 × занятых ячеек корзины, «Free» на своей карте
	void RefreshSourceDim();   // `uiSkillBarRefreshHighlight@0xE8DEF0` — затемнить ячейки-источники
	void LayoutScrollBar();
	void HideSelFrames();
	void MoveSelFrame(int32 Group, const UWidget* OverIcon);
	// Подпись поверх иконки (`uiSetItemCountText@0xE8A1F0`, §6 отчёта).
	FString CellText(const FAsdaItemInstance& It, uint32& OutColorArgb) const;

	// ── Корзина ──
	// `quickslotPlaceItem@0xE8B4B0` (правильное имя `warehouseBasket_PutItem`): положить `Qty` штук
	//   записи ячейки `SrcCtrlId` в ячейку корзины `DstCtrlId` (−1 = первая свободная).
	void BasketPut(int32 SrcCtrlId, int32 DstCtrlId, int32 Qty);
	// `storageUI_PlaceItemInSlot@0xE8B780`: вернуть содержимое ячейки корзины владельцу.
	void BasketReturn(int32 BasketCtrlId);
	// Вернуть ВСЮ корзину (крестик 4050, смена режима, `uiWarehouseResetTab(this, 9)`).
	void BasketReturnAll();
	int32 BasketOccupied() const;
	// `inventory_OnItemSlotMoveOrSplit@0xE8B240` — общий вход двойного щелчка и броска в корзину.
	void MoveOrSplit(int32 SrcCtrlId, int32 DstCtrlId);
	// ★ Гейт нумпада (§8.1): true ⇒ спросить количество, false ⇒ переносить весь стек.
	bool NeedsQuantityDialog(const FAsdaItemInstance& It, int32& OutQty) const;
	// `itemCodeClassify@0xE899B0` — 0/3 обычный предмет, 1 самоцвет, 2 спец-предмет второй таблицы.
	int32 EtalonClassify(int32 Code) const;
	// `itemIsNotStackableType32Slot3` — НЕ привязанный камень души (иначе нотис 2247 в чат).
	bool IsNotBoundSoulStone(const FAsdaItemInstance& It) const;

	// ── Ввод ──
	void TickCells();      // наведение, тултип, клик, двойной щелчок
	void TickDrag();       // §7 — весь конвейер перетаскивания
	void TickGearHover();  // 4050: наведение подменяет Ware_Gear на Ware_AniGear
	void BeginDrag(int32 CtrlId);
	void DropDrag();
	// Ячейка под курсором (по ЖИВОЙ ГЕОМЕТРИИ, закон `jui-cell-chain-nesting`). −1 = нет.
	int32 HitTestCell(bool bRequireItem, const UWidget** OutIcon) const;

	// ── Сеть ──
	void RequestTab(int32 Tab);   // `uiBagSendActionPacket@0xE89B30` → пакет 4050
	void SendKeep();              // 5008
	void SendTake();              // 5010
	void DrainPendingPackets();
	void OnWarehouseResult(bool bKeeping, uint8 Result, const TArray<uint8>& Body);

	// ── Нотисы (§10; каналы НЕ выдуманы — взяты из тел обработчиков) ──
	FString Loc(int32 LocId) const;
	void NoticeChat(int32 LocId) const;     // `CChat_AddTokenizedLines(g_pChatWnd, txt, −11514, 10000, 0)`
	void NoticePlank(int32 LocId) const;    // `CControl_SetTooltipText` = WARMESSAGE_WIN 221 (миснейм)
	void NoticeMsgBox(int32 LocId) const;   // `CItem_BuildTooltipText(…, 0, −1, txt, 1)` = ONEBUT_MSGBOX
	// Своя ли фракция владеет городом текущей карты (`isLocalCharOwnFactionOnMap@0xBCF030`).
	bool IsOwnFactionMap() const;

	// ═══════════════════ СОСТОЯНИЕ ═══════════════════
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget       = nullptr;   // 4000
	UPROPERTY(Transient) TObjectPtr<UUserWidget> RecallWidget = nullptr;   // 5500

	UPROPERTY(Transient) TArray<FAsdaWareCellUi> GridUi;      // 30
	UPROPERTY(Transient) TArray<FAsdaWareCellUi> BasketUi;    // 5
	UPROPERTY(Transient) TArray<FAsdaWareCellUi> BagCellUi;   // 8 × 30 = 240 (плоско)
	UPROPERTY(Transient) TArray<FAsdaWareBagUi>  Bags;        // 8

	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose  = nullptr;   // 4050 WARE_CLOBUT
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnKeep   = nullptr;   // 4002 WAREKEEP_BUT
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnTake   = nullptr;   // 4049 WARETAKE_BUT
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnCat0   = nullptr;   // 4188 WAREKEEP_2BUT_OPT0
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnCat1   = nullptr;   // 4188 WAREKEEP_2BUT_OPT1
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCat    = nullptr;   // подпись 4188 (1204/1205)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCap    = nullptr;   // 4003 «занято/всего»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtFeeLbl = nullptr;   // 4044 (12013)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtFee    = nullptr;   // 4045 КОМИССИЯ
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGoldLbl= nullptr;   // 4046 (12006)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGold   = nullptr;   // 4047 ДЕНЬГИ ИГРОКА
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtHeader = nullptr;   // 4038 (2248/2249)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTop    = nullptr;   // 4001 (12924)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtExtra  = nullptr;   // 4190 (12137)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CapKeep   = nullptr;   // подпись 4002 (13242)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CapTake   = nullptr;   // подпись 4049 (13243)
	UPROPERTY(Transient) TObjectPtr<UImage>            Gear    = nullptr;   // 4191
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> AniGear = nullptr;  // 4192
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> GridSel = nullptr;  // 4051 WARE_SEL00
	// Блок вкладок: корневая группа `WAREKEEP_TOP_grp` + всё её поддерево + «вечно скрытые».
	UPROPERTY(Transient) TObjectPtr<UWidget>          TabBlockRoot = nullptr;
	// Корень группы окна 4000 (`WARE_WIN_grp`) — по нему определяем, лежат ли окна-сумки
	//   в том же канвасе (расплющенная иерархия) и надо ли смещать их `Origin`.
	UPROPERTY(Transient) TObjectPtr<UWidget>          WareWinRoot  = nullptr;
	// ★ Левый-верхний угол рамки окна 4000 (`WARE_WIN_0_0`) — ФАКТИЧЕСКАЯ позиция окна на экране.
	//   От неё считаются позиции окон-сумок: окно можно перетащить, и жёсткие дизайн-координаты
	//   тогда разъезжаются с ним (жалоба владельца 2026-08-27, скриншот).
	UPROPERTY(Transient) TObjectPtr<UWidget>          WinFrameTL   = nullptr;
	// ★ 10.10.2026: правый-верхний угол рамки (`WARE_WIN_0_2`) — фактическая ширина окна для позиций окон-сумок.
	UPROPERTY(Transient) TObjectPtr<UWidget>          WinFrameTR   = nullptr;
	// Части фона окна, которым в режиме «хранить» укорачивается высота (`WARE_WIN_0_3…0_5`).
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>  WinBodyParts;
	// Коричневая рамка ВОКРУГ блока вкладок (`Ware_BrownBox_grp`): полоса сверху + полоса снизу,
	//   между ними 69 px пустоты. Именно она давала «чёрную дыру» при скрытом блоке вкладок.
	UPROPERTY(Transient) TObjectPtr<UWidget>          BrownBoxGrp  = nullptr;
	// Вторая подпись вкладки категорий («Снаряжение») — создаётся кодом, в данных её нет.
	UPROPERTY(Transient) TObjectPtr<UTextBlock>       TxtCat2      = nullptr;
	// ★ Планы-РАМКИ переключателя категорий (`WAREKEEP_2BUT_345_0/_345_1`): в `.JUI` скин
	//   `TabBar_W108_2Btn` рисует ДВЕ вкладки, и без них видны только надписи без рамок.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>  CatPlanes;
	// Базовые цвета НЕАКТИВНЫХ вкладок. Их снимает ИЗ АССЕТА общая точка
	//   `AsdaUi::ApplyTabCaptionState` (цвет кладёт шаг `Tools/asda_ui_tabbar_captions.py`);
	//   своей покраски вкладок склад больше НЕ держит — закон `port-shared-points`.
	TArray<FLinearColor> CatIdleColors;
	// Снимок исходных offsets узлов корневого канваса — для возврата при переключении режима.
	TMap<TObjectPtr<UWidget>, FMargin>                BaseOffsets;
	// Сдвинуто ли сейчас содержимое вверх (режим «хранить»).
	bool bTabAreaCollapsed = false;
	// Позиция окна 4000 на прошлом тике — по ней ловим перетаскивание (см. `TickBagFollow`).
	FVector2D LastWareWinOrigin = FVector2D(-1.f, -1.f);
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>  TabBlockParts;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>  AlwaysHidden;
	// Прокрутка 4189: импортёр её не создал (у скина `WheelWin_Nomal` полоса рождается в рантайме).
	UPROPERTY(Transient) FAsdaScrollBarNomal ScrollBar;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridIcons   = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> BasketIcons = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	// Окно 5500.
	UPROPERTY(Transient) TObjectPtr<UButton>    RcBtnTake  = nullptr;   // 5502 (10303)
	UPROPERTY(Transient) TObjectPtr<UButton>    RcBtnKeep  = nullptr;   // 5503 (10304)
	UPROPERTY(Transient) TObjectPtr<UButton>    RcBtnNo    = nullptr;   // 5504 (5035)
	UPROPERTY(Transient) TObjectPtr<UButton>    RcBtnX     = nullptr;   // 5509
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RcTitle    = nullptr;   // 5501 (10301)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RcCapTake  = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RcCapKeep  = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RcCapNo    = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> RecallRelays;

	// ── Модель (эталонные поля класса `CWarehouseWnd`, §2 отчёта) ──
	// Локальная копия склада, 270 записей (эталонный `aSlots[0…269]`, +0x5E8).
	UPROPERTY(Transient) TArray<FAsdaItemInstance> Store;
	// Уплотнённая копия инвентаря текущей категории (эталонные `aSlots+15390` / `aSlots+18810`).
	UPROPERTY(Transient) TArray<FAsdaItemInstance> InvList;
	// Корзина переноса, 5 записей (эталонный `aSlots+20520`).
	UPROPERTY(Transient) TArray<FAsdaItemInstance> Basket;

	int32 Mode          = kModeClosed;   // +0x564 (миснейм `selSlotIndex`)
	int32 Category      = kCatSpend;     // +0x56C — выбранный пункт 4188
	int32 ScrollRow     = 0;             // +0x568 — прокрутка списка В РЯДАХ по 6
	int32 PendingTab    = -1;            // +0x560 — вкладка, которую грузит сервер
	bool  bRequestSent  = false;         // +0x5738 `bRequestPending`
	bool  bRecallActive = false;         // +0x5734 `isOpen` — «удалённый склад активен»
	bool  bControlsReady = false;        // +0x570
	bool  bTabLoaded[kStoreTabs] = {};   // +0x578 `aTabLoaded[9]`
	bool  bBagOpen[kBags]  = {};         // окно-сумка показано
	int32 SlotAssign[kBags];             // +0x5E0 `aSlotTabAssign[8]` (−1 = позиция свободна)
	// Ячейка корзины, тронутая последней (+0x554) — приёмник по умолчанию, когда корзина полна.
	int32 LastBasketCtrl = -1;
	// Контекст нумпада (+0x558/+0x55C).
	int32 NumPadSrcCtrl = -1;
	int32 NumPadDstCtrl = -1;
	// Модель изменилась извне (батч 4051/4052, ответ 5001) — пересобрать в `Tick`.
	bool  bDirty = false;
	// Пришёл маркер конца выдачи страницы склада (пакет 4052) — разобрать в `Tick`.
	bool  bTabArrived = false;
	// Модальное подтверждение 10302 открыто — второго не открываем.
	bool  bConfirmOpen = false;

	// ── Ввод ──
	FAsdaMouseFrame Mouse;
	FAsdaDragArm    DragArm;
	UPROPERTY(Transient) FAsdaDragGhost DragGhost;
	int32 DragCtrl = -1;      // idnum схваченной ячейки
	int32 TipCtrl  = -1;      // ячейка, по которой показан тултип
	// Двойной щелчок по ячейке (ключ — IDNum контрола). ★ U1 (В2-A): общий `FAsdaDblClick`, порог Windows (было 0,35 с;
	//   прежний — `LegacySec`, ключ `asda.UI.DblClickLegacy 1`).
	FAsdaDblClick CellDbl{ -1, -1, -1.0, 0.35 };
public:
	// ★ U1 (В2-A дедупа №2): КОПИЯ распознавателя двойного щелчка с прежним порогом окна (`LegacySec`) — для таблицы
	//   порогов самотеста `asda.Shop.SelfTestDblClick` (раздел U1). Своё состояние пары окна не трогает.
	FAsdaDblClick DebugDblClickProbe() const;
private:
	bool   bGearHover = false;

	// Очередь пришедших пакетов (закон `no-reentrant-net-callbacks`).
	struct FWarePacket { bool bKeeping = false; uint8 Result = 0; TArray<uint8> Body; };
	TAsdaNetQueue<FWarePacket> PendingResults;

	FDelegateHandle HWareResult;
	FDelegateHandle HInvCell;
	FDelegateHandle HBagExpand;
	FDelegateHandle HBatchEnd;
};
