// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaNpcShopSubsystem — МАГАЗИН NPC: окно `NPCSHOP_WIN` (JUI IDNum 1300), покупка и продажа.
//   Полный реверс потока с адресами — `Reports/npc_shop_port.md`.
//   Полный структурный разбор контролов — `Docs/UI/Trade/NPCSHOP_WIN.md`.
//
// ═══════════════════════════ ОТКУДА ОКНО ОТКРЫВАЕТСЯ ══════════════════════════════════════════
//   `CGameUI_HandleControlCommand@0xD87590` (game_main.c:9944-10068), коды кнопок диалога NPC:
//     30001 → `NPCShop_OpenWindow(g_pNpcShopWnd,      5, npcId)` + `shopCraftWnd_reset(сторона продажи)`
//     30002 → `npcShop_Close(сторона покупки)` + `npcShopOpenWindow(g_pNpcShopWnd_91D0, 6,  npcId)`
//     30003 → `npcShop_Close(...)`             + `NPCShop_OpenWindow(g_pNpcShopWnd,      7,  npcId)`
//     30004 → `npcShop_Close(...)`             + `npcShopOpenWindow(g_pNpcShopWnd_91D0, 8,  npcId)`
//     30026 → `npcShop_Close(...)`             + `npcShopOpenWindow(g_pNpcShopWnd_91D0, 10, npcId)`
//   `shopType` решает ВСЁ: какой заголовок показан, какая кнопка (BUYBUT/SELLBUT) видна и откуда
//   берётся список:
//     5  «Разное»,  ПОКУПКА  — список из `js<npcId>.spd`
//     7  «Самоцветы», ПОКУПКА — тот же файл (тип влияет только на иконку заголовка)
//     6  «Разное»,   ПРОДАЖА — список = контейнер 2 (расходники), кроме золота 20551
//     8  «Самоцветы», ПРОДАЖА — тот же контейнер 2
//     10 «Оружейный», ПРОДАЖА — список = контейнер 1 (снаряжение), записи с `ContainerType == 1`
//
// ★★★ В ОРИГИНАЛЕ ЭТО ДВА ОБЪЕКТА, А НЕ ОДИН. `g_pNpcShopWnd` — класс `CShopNpc` (сторона покупки,
//   vtable `off_1414BF8`), `g_pNpcShopWnd_91D0` — `CShopSell` (сторона продажи, vtable `0x1415A10`).
//   Оба строят свои указатели на ОДНИ И ТЕ ЖЕ контролы окна 1300 (`CShopSell_BuildUI@0xE2C050` и
//   ctor покупки зовут один и тот же `NPCShop_InitControls@0xE233F0`) и потому не могут быть видны
//   одновременно — переключение вкладок в диспетчере и закрывает одну сторону перед открытием
//   другой. В порту это ОДНА подсистема с полем `ShopType`: состояние всё равно сбрасывается при
//   каждом переключении, а второй копии окна не возникает.
//   Слоты vtable сняты ИЗ БИНАРЯ (jmp-переходники разрезолвлены), а не угаданы:
//     покупка  +4 OpenWindow 0xE25C00 · +8 Close 0xE26570 · +12 «нумпад OK» 0xE28A60 ·
//              +16 0xE2A050 · +20 «крестик» 0xE2A1A0 · +24 **BUYBUT → Send_CharItemBuy_1 0xE2A240** ·
//              +28 ПУСТО 0xE231B0 · +32 0xE26770 · +36 ПУСТО · +40 тултип 0xE285B0 ·
//              +44 «в корзину» 0xE27800 · +48 «из корзины» 0xE28330
//     продажа  +4 OpenWindow 0xE2C090 · +8 reset 0xE2C860 · +12 «нумпад OK» 0xE30AA0 ·
//              +16 0xE31500 · +20 «крестик» 0xE32730 · +24 ПУСТО 0xE23190 ·
//              +28 **SELLBUT → Send_CharItemSell 0xE32800** · +32 0xE2CAD0 · +36 клик 0xE33060 ·
//              +40 тултип 0xE307F0 · +44 0xE2EF50 · +48 0xE30330
//   Отсюда и правило кнопок: `uiNPCShop_DispatchHotkeyCmd@0xE24890` шлёт 1311 в +24, 1312 в +28 —
//   то есть В РЕЖИМЕ ПОКУПКИ работает только BUYBUT, в режиме продажи — только SELLBUT.
//
// ═══════════════════════════ ДАННЫЕ АССОРТИМЕНТА ══════════════════════════════════════════════
//   `NPCShop_LoadBuyList@0xE28700`: `./data/npc/Shop/js<npcId>.spd` = `[u32 0][u32 count][коды u32]`.
//   В порту — `DT_NpcShopList` (`FAsdaNpcShopRow`, ключ строки = npcId), импортёр
//   `Tools/asda_import_npc_shop.py`. Пятое поле записи витрины («потолок стопки») загрузчик берёт
//   не из файла, а из справочника: `ItemRecord->flagWord176` (≤ 1 ⇒ 1), у самоцветов 0.
//   ⚠ `Data\NPC\Shop\PriceTable.BIN` магазин НЕ читает: единственный её загрузчик
//   `CItemManager_LoadPriceTableBin@0x9C4AC0` берёт запись из `Data\character\Bin.bbz`, а цены
//   магазина приходят из `item_list` / `sowell_list` (см. `PriceOf`).
//
// ═══════════════════════════ ЦЕНЫ ═════════════════════════════════════════════════════════════
//   ПОКУПКА (`npcShop_UpdateTradeTaxDisplay@0xE29760`, `shopWnd_calcMaxBuyableQty@0xE2AEC0`,
//     `npcShop_BuyItemIntoSlot@0xE27800`): `ItemRecord->price` (+0xA4); самоцвет — `sowell_list+0x34`.
//   ПРОДАЖА (`shopComputePriceAndUpdateUI@0xE31FB0`): `ItemRecord->subCategory` (+0xA0 — ИМЯ ВРЁТ,
//     это ЦЕНА ПРОДАЖИ); самоцвет — `sowell_list+0x30`; запись второй таблицы — `special_itemlist+0x6C`.
//   Умножение на количество — только если `quantity > 0 && !flagWord176 && tradableFlag == 1`.
//   ★ У ПРОДАЖИ есть множитель баффа «ставка цены» (`g_pActiveBuffEntry`, тип 38): цена растёт на
//     `+N%`; у покупки такого множителя нет. Пока система активных баффов не отдаёт этот тип,
//     множитель = 1 (записано в `PORT_PENDING_ON_SYSTEMS.md`).
//   НАЛОГ/СКИДКА войны Unicon (`uniconWar_ApplyOptionPercent@0xBCEEC0`) — только на картах 3/0/7.
//
// ═══════════════════════════ КОРЗИНА И НУМПАД ═════════════════════════════════════════════════
//   Корзина = 5 ячеек `NPCSHOP_BTRAY00…04` (1360…1364). Клик по товару витрины кладёт его в
//   первую свободную ячейку; если предмет СТЕКУЕТСЯ (`flagWord176 != 0`), тредабельный и не
//   самоцвет и `itemSubType != 32` — сперва открывается ОКНО КОЛИЧЕСТВА (эталонный режим **1**,
//   `uiOpenContextPopupWithHandlers(g_pNumPadDialog, max, 1, 0)`), и уже его OK кладёт N штук.
//   Максимум для покупки считает `shopWnd_calcMaxBuyableQty` (сколько влезает в оставшиеся деньги
//   с учётом налога), для продажи — размер стопки в инвентаре.
//   Клик по занятой ячейке корзины возвращает предмет обратно (`npcShopClearTradeSlot@0xE28330`
//   для покупки, `shopCraftWnd_onSlotClick@0xE2CAD0` для продажи).
//
// ═══════════════════════════ ПАКЕТЫ ═══════════════════════════════════════════════════════════
//   Покупка: **5020** (`OP_CHAR_ITEM_BUY`, 7×6 б) → **5021** (406 б). Продажа: **5018**
//   (`OP_CHAR_ITEM_SELL`, 5×11 б) → **5019** (62 б). Имена опкодов в перечислении ПЕРЕПУТАНЫ
//   (`OP_SHOP_SELL_RESULT_0` = ответ на покупку) — доказано серверными `CharItemBuy@0x50F170`
//   и `CharItemSell@0x50D240`. Сборка тел — `UAsdaNetSubsystem::SendCharItemBuy5020/SendCharItemSell5018`.
//   ★ Пока ответ не пришёл, стоит флаг `bPending` (эталонное поле окна +512) и повторный запрос
//   не уходит.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaScrollBarNomal.h"   // полоса прокрутки скина `Nomal_Scr` (контрол 1365 в WBP не приехал)
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "UI/AsdaShopDblClick.h"     // FAsdaShopDblClick — товар витрины в корзину двойным щелчком (10.10.2026)
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaHeldTables.h"                // FAsdaHeldTables — удержание DT_NpcShopList на сессию (15.09.2026, dt_gc_hold)
#include "AsdaNpcShopSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UCanvasPanel;
class UAsdaAnimatedPlane;
class UAsdaButtonRelay;
class UAsdaIconCellGrid;

// ═══ ОДНА ЗАПИСЬ СПИСКА/КОРЗИНЫ — эталонные 20 байт вектора товаров ═══
//   Раскладка снята по всем читателям (`NPCShop_LoadBuyList`, `shopBuildSellableItemList@0xE31DB0`,
//   `shopBuildBuybackItemList@0xE31B50`, `Send_CharItemSell@0xE32800`, `ItemSlot_UpdateIcon@0xE23FC0`):
//     +0  itemCode
//     +4  «второй индекс» — у продажи это `запись+5` (слот контейнера), у витрины 0
//     +8  индекс в массиве контейнера (у витрины 0)
//     +12 контейнер (`запись+4`; у витрины 0)
//     +16 количество: у витрины — ПОТОЛОК СТОПКИ, у продажи и в корзине — сколько штук
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaShopEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Asda|Shop") int32 Code = 0;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Shop") int32 SlotIndex = 0;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Shop") int32 ArrayIndex = 0;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Shop") int32 Container = 0;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Shop") int32 Amount = 0;
	// ★ ОТКУДА В СПИСКЕ ВЗЯТА ПОЗИЦИЯ КОРЗИНЫ — индекс в векторе товаров.
	//   В эталоне для этого ПЕРЕИСПОЛЬЗУЕТСЯ третье поле: `shopBuySlotMoveItem@0xE30AA0` кладёт в
	//   `корзина[+272]` значение `F+388` (индекс в векторе), а `shopCraftWnd_onSlotClick@0xE2CAD0`
	//   по нему же возвращает предмет обратно (`Stream_Insert(вектор, корзина[+272])`).
	//   Мы держим ОТДЕЛЬНОЕ поле, потому что `ArrayIndex` (индекс в КОНТЕЙНЕРЕ) нужен нам живым:
	//   по нему исправление Д2 читает поля `+7`/`+14` из правильного контейнера при сборке 5018.
	UPROPERTY(BlueprintReadOnly, Category="Asda|Shop") int32 ListIndex = -1;

	bool IsEmpty() const { return Code <= 0; }
	void Clear() { Code = 0; SlotIndex = 0; ArrayIndex = 0; Container = 0; Amount = 0; ListIndex = -1; }
};

// Виджеты одной ячейки витрины/корзины: план-иконка `{idc}_{juiIdx}_0` + TextBlock `{idc}` (счётчик).
USTRUCT()
struct FAsdaShopCellUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UImage>     Icon = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CountText = nullptr;
	UPROPERTY(Transient) FVector2D IconRootPos = FVector2D::ZeroVector;
};

UCLASS()
class ASDAREBORN_API UAsdaNpcShopSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaNpcShopSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа окна (все — из `.JUI` и `NPCShop_InitControls`, не выдуманы) ──
	static constexpr int32 kGridCells = 36;   // NPCSHOP_TRAY00…35 = idnum 1320…1355
	static constexpr int32 kGridCols  = 6;    // страница = 6 ячеек (эталон делит Size на 6)
	static constexpr int32 kGridRows  = 6;
	static constexpr int32 kCartCells = 5;    // NPCSHOP_BTRAY00…04 = idnum 1360…1364
	// ★ U6 (В1 дедупа №2): одна 11-байтовая ссылка на предмет продажи 5018 (`Send_CharItemSell@0xE32800`) по смещению Off:
	//   +0 i16 слот, +2 i16 второй индекс, +4 u8 контейнер, +5 i32 количество, +9 i16 вес стопки. Самотест сверяет побайтно.
	static void WriteSellRef11(class FAsdaPacketWriter& W, int32 Off, int32 SlotIndex, int32 Second, int32 Container, int32 Amount, int32 StackW);
	static constexpr int32 kCellPx    = 32;   // Size = "32 32" у ячеек
	static constexpr int32 kCellStep  = 33;   // шаг между ячейками
	// z-order: выше инвентаря (720) — окно магазина в оригинале поднимается поверх него.
	static constexpr int32 kShopZ = 730;

	// ═══════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ═══════════════════
	// Единственная точка входа. ShopType ∈ {5, 6, 7, 8, 10}; NpcId = `pThis[436]` окна разговора.
	UFUNCTION(BlueprintCallable, Category="Asda|Shop")
	void OpenShop(int32 ShopType, int32 NpcId);

	// Эталон `npcShop_Close@0xE26570` / `shopCraftWnd_reset@0xE2C860` — обе сбрасывают состояние
	//   и прячут окно; разницы для порта нет, потому что состояние у нас одно.
	UFUNCTION(BlueprintCallable, Category="Asda|Shop")
	void CloseShop();

	UFUNCTION(BlueprintPure, Category="Asda|Shop")
	bool IsOpen() const { return bOpen; }

	UFUNCTION(BlueprintPure, Category="Asda|Shop")
	int32 GetShopType() const { return ShopType; }

	// ── 10.10.2026: доступ самотеста `asda.Shop.SelfTestDblClick` (только чтение; клик — настоящим событием Slate) ──
	int32 DebugCartFilled() const;                         // сколько ячеек корзины занято
	void DebugPutCart(int32 ItemCode, int32 Amount);       // ★ 06.11.2026: положить товар в ячейку 0 корзины (самотест звуков)
	// ★ 06.11.2026: звук сделки по предмету — общая точка покупки у NPC (окно 1300 и магазин снаряжения 6800), им же пользуется самотест.
	void PlayDealSound(int32 ItemCode) const { PlayTradeSound(ItemCode); }
	void DebugConfirm() { OnConfirmClicked(); }            // ★ 06.11.2026: кнопка подтверждения покупки/продажи (самотест звуков)
	int32 DebugSelGridCell() const { return SelGridCell; } // выбранная ячейка витрины (F+384, −1 = нет)
	int32 DebugSelCartCell() const { return SelCartCell; } // выбранная ячейка корзины (F+392, −1 = нет)
	int32 DebugItemCode(int32 AbsIndex) const;             // код товара витрины по абсолютному индексу (0 = нет)
	UWidget* DebugGridIcon(int32 Cell) const;              // иконка ячейки витрины — точка симклика
	UWidget* DebugCartIcon(int32 Cell) const;              // иконка ячейки корзины — точка симклика

	// Есть ли у этого NPC ассортимент (строка в `DT_NpcShopList`). Нужно и диалогу, и диагностике.
	UFUNCTION(BlueprintPure, Category="Asda|Shop")
	bool HasShopList(int32 NpcId) const;

	// ★ УДЕРЖАНИЕ `DT_NpcShopList` (15.09.2026, отчёт dt_gc_hold): ассортимент читается через слабый `AsdaDt::Cached`
	//   при КАЖДОМ открытии магазина — в -game GC выгружал таблицу между открытиями. Держим сильно, лениво в
	//   `OpenShop`. Возврат — 0/1. Публичный — для стенда.
	int32 HoldGameTables();
	const FAsdaHeldTables& GetHeldTables() const { return HeldTables; }

	// ═══════════════════ ДЕЙСТВИЯ (в эталоне — обработчики контролов) ═══════════════════
	// Клик по ячейке ВИТРИНЫ (`npcShop_onItemSlotClick@0xE33060` для продажи,
	//   `npcShop_BuyItemIntoSlot@0xE27800` для покупки).
	void OnGridCellClicked(int32 Cell);
	// 10.10.2026: одиночный щелчок по витрине — только выбор ячейки (курсор 1314), как эталонный 0xB `npcShopMapClickToItem@0xE24970`.
	void OnGridCellSelected(int32 Cell);
	// Клик по ячейке КОРЗИНЫ — вернуть предмет назад.
	void OnCartCellClicked(int32 Cell);
	// 10.10.2026: одиночный щелчок по корзине — только выбор (курсор 1315), как эталонный 0xB; вернуть — двойным (0xD).
	void OnCartCellSelected(int32 Cell);
	// ★ 10.10.2026 (решение владельца): ОДИНОЧНЫЙ ПКМ по ячейке в режиме ПРОДАЖИ — эталонное событие 0xF окна 1300
	//   (`uiHandleWindowCommand@0xE24DE0`, ui_core.c:71366) → vtbl+36 = `npcShop_onItemSlotClick@0xE33060`: ячейка витрины
	//   (предмет сумки) — в корзину продажи, ячейка корзины — вернуть. У покупки слот +36 ПУСТ — ПКМ ничего не делает.
	//   Ключ `asda.Shop.DblClickToCart` эту ветку НЕ трогает (одиночный ПКМ — как оригинал). bCart — ячейка корзины.
	void OnSellCellRightClick(int32 Cell, bool bCart);
	// BUYBUT 1311 / SELLBUT 1312.
	void OnConfirmClicked();

	// Прокрутка витрины В СТРАНИЦАХ (эталон: страница = 6 ячеек = ряд).
	UFUNCTION(BlueprintCallable, Category="Asda|Shop")
	void SetPage(int32 NewPage);
	UFUNCTION(BlueprintCallable, Category="Asda|Shop")
	void ScrollBy(int32 DeltaPages);
	UFUNCTION(BlueprintPure, Category="Asda|Shop")
	int32 GetMaxPage() const;

	// ★★★ МНОЖИТЕЛЬ ЦЕНЫ ПРОДАЖИ ОТ АКТИВНОГО БАФФА ТИПА 38 — эталон `shopComputePriceAndUpdateUI`
	//   (shop_trade.c:21449-21453):
	//       if (g_pActiveBuffEntry && *(u8*)g_pActiveBuffEntry && *(i16*)(g_pActiveBuffEntry+88) == 38)
	//           unitPrice = unitPrice * (значение/100.0) + 0.5 + unitPrice;
	//   ⚠⚠⚠ В ЭТОЙ СБОРКЕ ВЕТКА НЕДОСТИЖИМА. Байт-скан ВСЕГО `.text` по всем формам записи в
	//   `g_pActiveBuffEntry` (0x2C2F23C): `A3 …`, `89 0D/15/1D/35/3D …`, `C7 05 …` — НИ ОДНОЙ
	//   инструкции. Все 14 ссылок на адрес — только `cmp dword ptr [g],0` (`83 3D`) и `mov eax,[g]`
	//   (`A1`), то есть ТОЛЬКО ЧТЕНИЯ: указатель всегда NULL. Проверено и по данным — поля `u16@88`
	//   со значением 38 нет ни в `special_itemlist` (558 записей, шаг 316: там только 33/37/42/56/72),
	//   ни в `PlusPackageTable` (6 записей, шаг 140).
	//   ⇒ ФОРМУЛУ воспроизводим точно, а ИСТОЧНИК держим одной явной точкой: −1 = баффа нет
	//   (поведение оригинала во всех случаях). Появится канал — достаточно вызвать сеттер.
	void SetSellPriceBuffPercent(int32 Percent) { SellPriceBuffPercent = Percent; }
	int32 GetSellPriceBuffPercent() const { return SellPriceBuffPercent; }

	// ★★ [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] ПОТОЛОК ЦЕНЫ ПРОДАЖИ ТОРГОВЦУ — правило СЕРВЕРА, которого у
	//   клиента не было. `CharItemSell@GameServer.c:103330` после множителя баффа 38 (:103655) для
	//   КАЖДОЙ позиции режет цену единицы сверху: предмет (`sub_532A90`) — по цене покупки
	//   `item_table +41`, самоцвет (`sub_532AE0`) — по `+19` своей таблицы; только если та > 1:
	//       cap = buy − 25·buy/100 (целочисленно);   unit = min(unit, cap)        (:103662-103684)
	//   Спец-предметы второй таблицы (`sub_532B10`) потолка не имеют. Без правила окно продажи и
	//   подсказка показывали больше, чем сервер реально начисляет (32 120 предметов,
	//   `Reports/tables_final_sync_list.md` §1.2). `BuyPrice` порта после синхронизации
	//   таблиц = серверному `+41` (`finalsync_apply.md`). Откат: `asda.Shop.ServerSellCap 0`.
	static int32 ServerSellUnitCap(int32 SellUnit, int32 BuyPrice);

	// ═══════════════════ ДИАГНОСТИКА ═══════════════════
	FString DumpControls() const;   // КАЖДЫЙ разобранный контрол (закон full-structural-analysis)
	FString DumpState() const;      // список, корзина, суммы, режим

private:
	void PlayWindowSound(int32 SoundId) const;   // ★ 06.11.2026: звук окна 5/6 (ключ `asda.Shop.WindowSound`)
	// ── Жизненный цикл окна ──
	// Сильная ссылка на таблицу ассортимента (см. `HoldGameTables`).
	UPROPERTY(Transient)
	FAsdaHeldTables HeldTables;

	bool EnsureWidget();
	void ResetWidget();
	void SetupControls();
	void RegisterZone();

	// ── Наполнение списка ──
	void LoadBuyList();             // `NPCShop_LoadBuyList@0xE28700`
	void BuildSellListSpendBag();   // `shopBuildBuybackItemList@0xE31B50` — контейнер 2 без золота
	void BuildSellListShellBag();   // `shopBuildSellableItemList@0xE31DB0` — контейнер 1
	void RecalcPages();             // `Size/6` + остаток, скрытие полосы при ≤ 6 страниц

	// ── Рендер ──
	void ApplyTitles();             // `npcShop_ShowWindow@0xE26280` / `shopCraftWnd_openAndSelectTab@0xE2C4F0`
	void RefreshList();             // `NPCShop_RenderList@0xE23DE0`
	void RefreshCart();
	void RefreshMoneyTexts();       // TEXT00 (сумма сделки) и TEXT01 (деньги игрока)
	void RefreshTaxTexts();         // 1380…1383 — блок налога/итога
	void UpdateCellVisual(int32 Cell, bool bCart);
	// Курсор 1314/1315 на ячейку: общая точка `AsdaUi::PlaceFrameOver` (дедуп №2, В3-S10), по ЖИВЫМ слотам.
	void PlaceCursorOnCell(UAsdaAnimatedPlane* Cur, const UWidget* Icon);
	void HideCursors();

	// ── Цена ──
	// Цена ОДНОЙ позиции: `bBuy` ⇒ покупка (`ItemRecord->price`), иначе продажа (`+0xA0`).
	int32 PriceOf(int32 ItemCode, int32 Quantity, bool bBuy) const;
	// `uniconWar_ApplyOptionPercent@0xBCEEC0`: процент войны за города; `bAdd` = эталонный `a2`.
	int32 ApplyWarPercent(int32 Value, bool bAdd) const;
	// Множитель баффа «ставка цены» (тип 38) — только для ПРОДАЖИ, см. SetSellPriceBuffPercent.
	int32 ApplySellPriceBuff(int32 UnitPrice, bool bBuy) const;
	// Слот города текущей карты: 0 Альпия (карта 3), 1 Силарис (0), 2 Пламения (7); −1 — не военная.
	int32 WarSlot() const;
	// Владеет ли городом СВОЯ фракция. `OutPercent` — ставка города, `bOutNeutral` — «город не занят
	//   либо у игрока нет фракции» (эталон в этом случае гасит блок налога).
	bool IsOwnFactionCity(int32 Slot, int32& OutPercent, bool& bOutNeutral, bool* bOutNoFaction = nullptr) const;


	// `shopWnd_calcMaxBuyableQty@0xE2AEC0` — сколько штук ещё можно купить на оставшиеся деньги.
	int32 CalcMaxBuyableQty(int32 ItemCode) const;
	// Пересчитать бегущие суммы после изменения корзины (`npcShop_UpdateTradeTaxDisplay` /
	//   `shopComputePriceAndUpdateUI`): `bBuy` — режим, `bAdd` — прибавляем или вычитаем.
	void AddToRunningTotal(int32 ItemCode, int32 Quantity, bool bAdd);

	// ── Корзина ──
	int32 FindCartSlotWithSame(int32 Code, int32 Key) const;   // `slotFindMatchingPair@0xE32680`
	// Укладка предмета витрины ПРОДАЖИ (абсолютный индекс) в корзину — тело `npcShop_onItemSlotClick@0xE33060` для ячеек
	//   1320…1355 (докладка в пару / нотис 2312, свободная ячейка, `slotKind`). Общая точка для ЛКМ (двойной) и ПКМ (одиночный).
	void SellIntoCart(int32 Abs);
	// `shopSelectTabById@0xE29FC0` — поиск ячейки корзины ТОЛЬКО ПО КОДУ (без второго ключа).
	//   Эталон зовёт его в хвосте `npcShop_BuyItemIntoSlot`, когда все пять ячеек заняты.
	int32 FindCartSlotWithCode(int32 Code) const;

	/** ★ Звук сделки — НАША добавка (в оригинале магазин озвучивает только открытие/закрытие окна).
	 *  Берёт родной звук падения предмета через общую точку `PlayItemDropSound`.
	 *  Откат: `asda.Shop.TradeSound 0`. */
	void PlayTradeSound(int32 ItemCode) const;
	/** Код предмета последней подтверждённой сделки — для звука (корзина к моменту ответа уже пуста). */
	int32 LastTradeItemCode = 0;
	int32 FindFreeCartSlot() const;                             // `slotFindFirstFree5@0xE24630`
	// Положить в корзину `Quantity` штук позиции списка `ListIdx` (у продажи количество СНИМАЕТСЯ
	//   с записи списка — `shopBuySlotMoveItem@0xE30AA0`).
	void  PutIntoCart(int32 CartSlot, int32 ListIdx, int32 Quantity);
	void  TakeFromCart(int32 CartSlot);

	// ── Сеть ──
	void SendBuy();     // `Send_CharItemBuy_1@0xE2A240`
	void SendSell();    // `Send_CharItemSell@0xE32800`
	void OnBuyResult(const TArray<uint8>& Body406);
	void OnSellResult(const TArray<uint8>& Body62);
	void DrainPendingPackets();

	// ── Нотисы (таблица «событие → loc → приёмник» в отчёте) ──
	void NoticeChat(int32 LocId) const;     // `CChat_AddTokenizedLines(...)` — системный чат
	void NoticeMsgBox(int32 LocId) const;   // `CItem_BuildTooltipText(..., 0, −1, текст, 1)` = ONEBUT_MSGBOX
	FString Loc(int32 LocId) const;

	// ── Полоса прокрутки ──
	void BuildScrollBar();
	void LayoutScrollBar();
	void TickScrollBar(bool bLmb, bool bLmbDownEdge);
	bool IsScrollBarShown() const;

	// ── Подсказки ──
	void TickTooltip();

	// ── ★★★ ПЕРЕТАСКИВАНИЕ ПРИЗРАКОМ (эталон: сообщение 0xB «нажали» ставит режим и слот
	//   (`npcShopMapClickToItem@0xE24970`), сообщение 0xD «отпустили» зовёт `dispatchByState44or48
	//   @0xE24AE0` → vtbl+44 (из витрины в корзину) либо vtbl+48 (из корзины обратно)).
	//   Призрак иконки в оригинале рисует сам движок (D3D-слой, `CPlane_SetFillMode(…, 5)` —
	//   именно его ставит `uiControlApplySelectedHighlight@0xE24CA0` схваченной ячейке).
	//   В порту призрак — ОБЩАЯ точка `FAsdaDragGhost`, второго механизма не заводим.
	void TickDrag();
	void BeginDrag(int32 Cell, bool bFromCart);
	void DropDrag();

	// ═══════════════════ СОСТОЯНИЕ ═══════════════════
	// Виджет окна и его разобранные контролы.
	UPROPERTY(Transient) TObjectPtr<UUserWidget> ShopWidget = nullptr;
	UPROPERTY(Transient) TArray<FAsdaShopCellUi> GridUi;
	UPROPERTY(Transient) TArray<FAsdaShopCellUi> CartUi;
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose = nullptr;   // 1302
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnExit  = nullptr;   // 1313
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnBuy   = nullptr;   // 1311
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnSell  = nullptr;   // 1312
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CapBuy   = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CapSell  = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CapExit  = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtList00 = nullptr;  // 1305 (2281/2284)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtList02 = nullptr;  // 1306 (2282/2285)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtList04 = nullptr;  // 1307 (111914/2286)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtList06 = nullptr;  // 1308 (12006)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTotal  = nullptr;  // 1309 сумма сделки
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtMoney  = nullptr;  // 1310 деньги игрока
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTaxPct = nullptr;  // 1380
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTaxVal = nullptr;  // 1381
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtNetLbl = nullptr;  // 1382 (105006)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtNetVal = nullptr;  // 1383
	// Группы заголовков — видимость переключает `ApplyTitles`.
	UPROPERTY(Transient) TObjectPtr<UWidget> GrpSowelTitle  = nullptr;  // 1301
	UPROPERTY(Transient) TObjectPtr<UWidget> GrpMiscTitle   = nullptr;  // 1303
	UPROPERTY(Transient) TObjectPtr<UWidget> GrpWeaponTitle = nullptr;  // 1370
	// Шестерёнки кнопки закрытия (1371 статичная ↔ 1372 анимированная).
	UPROPERTY(Transient) TObjectPtr<UImage> Gear = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> AniGear = nullptr;
	// Анимированные рамки выбранной ячейки (1314 витрина, 1315 корзина).
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> GridSel = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> CartSel = nullptr;
	// ★ Дедуп №2, В3-S10: кэш базы родителя курсора (`GridSelParentRoot/CartSelParentRoot`, снимался один раз при привязке)
	//   убран — база берётся по живым слотам в `AsdaUi::PlaceFrameOver` (см. `PlaceCursorOnCell`).

	// Клик-оверлеи (общая точка `UAsdaIconCellGrid` — она же ставит иконки).
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridClicks = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> CartClicks = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	// Полоса прокрутки витрины (контрол 1365 импортёром не перенесён — строим из данных скина).
	UPROPERTY(Transient) FAsdaScrollBarNomal ScrollBar;

	// ── Модель ──
	int32 ShopType = 0;            // F+372
	int32 NpcId    = -1;           // F+476
	bool  bOpen    = false;        // F+508
	bool  bPending = false;        // F+512 — ждём ответ сервера
	bool  bControlsReady = false;

	TArray<FAsdaShopEntry> Items;                 // F+256 (вектор товаров)
	FAsdaShopEntry Cart[kCartCells];              // F+272…F+371
	int32 Page = 0;                               // F+376
	int32 TotalPages = 0;                         // F+416
	int32 SelGridCell = -1;                       // F+384
	int32 SelAbsIndex = -1;                       // F+388
	int32 SelCartCell = -1;                       // F+392
	int32 SelMode = 0;                            // F+420: 0 = витрина, 1 = корзина
	int32 RunningTotal = 0;                       // F+428 — сумма сделки БЕЗ налога
	int32 TaxDelta = 0;                           // F+432 — налог/скидка со знаком
	// Процент баффа «ставка цены» (тип 38). −1 = баффа нет; см. SetSellPriceBuffPercent.
	int32 SellPriceBuffPercent = -1;

	// Очередь пришедших пакетов (закон `no-reentrant-net-callbacks`).
	TAsdaNetQueue<TArray<uint8>> PendingBuy;
	TAsdaNetQueue<TArray<uint8>> PendingSell;

	// ★ Строки чата 20065 «Ты покупаешь X за Y золота». Эталон собирает их В МОМЕНТ ОТПРАВКИ 5020
	//   (`Send_CharItemBuy_1`, пять std::string в самом объекте окна), а ПЕЧАТАЕТ только по
	//   успешному ответу 5021 (`Recv_ShopSell_Result`, network.c:28238-28246). Повторяем 1:1.
	TArray<FString> PendingBuyLines;

	FAsdaMouseFrame Mouse;
	// Перетаскивание: взвод порога и общий призрак иконки.
	FAsdaDragArm  DragArm;
	// 10.10.2026: распознаватель двойного щелчка (эталонный 0xD): ключ A — абсолютный индекс товара либо ячейка корзины, B — 0 витрина / 1 корзина.
	FAsdaShopDblClick GridDbl;
	// 10.10.2026: ячейка под нажатием ПКМ (−1 — нет): эталонный 0xF — щелчок ПКМ, нажатие и отпускание над одной ячейкой.
	int32 RmbCell = -1;
	bool bRmbCart = false;   // ячейка нажатия ПКМ — в корзине (иначе витрина)
	UPROPERTY(Transient) FAsdaDragGhost DragGhost;
	int32 DragCell = -1;          // схваченная ячейка
	bool  bDragFromCart = false;  // откуда схвачена (эталонный режим F+420)
	int32 TipCell = -1;            // ячейка, по которой сейчас показана подсказка (−1 = нет)
	bool  bTipFromCart = false;

	FDelegateHandle HBuyResult;
	FDelegateHandle HSellResult;
	FDelegateHandle HCityOwner;
};
