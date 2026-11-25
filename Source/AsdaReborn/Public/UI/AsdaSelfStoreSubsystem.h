// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ UAsdaSelfStoreSubsystem — ЛИЧНАЯ ЛАВКА (StreetStall), СТОРОНА ПРОДАВЦА. Волна Л1 торговли (29.09.2026).
//
// ЧТО ЭТО. Порт двух классов эталона в части продавца:
//   • `CSellWindow` (объект `g_pSellWindow` = 0x2C2F3D0) — окно ПОДГОТОВКИ лавки `StreetStall_Open_Win` 22000:
//     сетка сумки (30 ячеек, вкладки «Расходники»/«Снаряжение»), 10 строк продажи (цена/кол-во/итог, ▲▼),
//     название лавки, сумма, золото, «OK» → 6302, «Отмена»/крестик/ESC → 6305.
//   • `CShopDlg` (объект 0x2C2F990, глобал-МИСНЕЙМ `g_bSomeFeatureEnabled`) — витрина `StreetStall_Win` 23000 в РЕЖИМЕ
//     ХОЗЯИНА (`bSpecialShop = 1`): название, 10 лотов из 6303, сумма, золото, «Изменить» 23305, закрытие 23005/23306/ESC.
//   • ★ Волна Л2 (30.09.2026, `Reports/trade/wave_L2.md`): `CShopDlg` в режиме ПОСЕТИТЕЛЯ (`bSpecialShop = 0`) — вход
//     6307 → 6308, корзина ≤ 6 (нумпад режим 8, урна 23243, клик по ячейке корзины снимает позицию), «OK» 23305 → msgbox 62 →
//     6309 → 6310; продажи 6311 (хозяину) / 6312 (гостям), гости 6315, журнал витрины 23402 (строится кодом — решение
//     владельца, ассетного узла нет), выход 6305 → 6306. Вывески 6304 и клик по вывеске — волна Л3 (точка входа здесь —
//     `RequestVisit`), чат 6313/6314 и поле 23403 — волна Л4.
//   • ★ Волны Л4/Л5 (30.09.2026, `Reports/trade/wave_L45.md`): чат лавки — поле 23403 (Enter → 6313 телом 106 б, [ИСПРАВЛЕНИЕ]
//     К-0; мат 9005 → журнал #FF9600), приём 6314 (журнал 23402, 6 цветов по номеру отправителя); гейты лавки у чужих окон — через
//     общую точку `UAsdaTradeStateSubsystem` (`NoticeOwnShop107022`), см. отчёт.
//   • ★ Волна Л3 (30.09.2026, `Reports/trade/wave_L3.md`): вывески/прилавки `CShopMgr` и очередь 6304 живут в
//     `UAsdaStallSignSubsystem` (`Public/UI/AsdaStallSignSubsystem.h`); клик по вывеске зовёт `RequestVisit`, правило «свой и
//     состояние 6 → 6305 раз в 4 с» и реклама премиум-лавки 5086 — в рендере СВОЕЙ вывески (там же, где у эталона). Исправления
//     ревизии Л2: К-8 (адрес ячейки сумки — из лота хозяина), `StoreBuyGuard` по правилу сервера, `StoreStrayGuard`.
//
// ПОТОК (разбор — `Reports/trade/B_selfstore_client.md` §2.1, §3.1; сервер — `C2_selfstore_server.md` §3):
//   вход (эмоция 207 / хоткей 65 / доп-меню 2 / билет премиум-лавки) → гейты → **6300** → **6301** код 1 →
//   `sellWindow_Open` (22000) → выставление (клик ЛКМ: нумпад режим 7 у стопки; ПКМ: вся стопка) → «OK» → **6302** →
//   **6303** код 1 → `shopDlgOpenSellWindow` (22000 прячется, витрина хозяина, поза 107) … → **6305** → **6306** код 1 →
//   окна скрыты, поза 109, записи 57 б — в инвентарь.
//
// ОБЩИЕ ТОЧКИ (новых сущностей транспорта нет, D_port_inventory §3): пакеты — `UAsdaNetSubsystem::SendSelfStore*` (конверт
//   `SendSecured`) и делегат `OnSelfStorePacket`; признаки «лавка открыта» — `UAsdaTradeStateSubsystem` (22000/23000);
//   нотисы — `AsdaNotice`; нумпад — `UAsdaWindowSubsystem::ShowNumPad`; поза — `AAsdaPlayerController::SendStallStatePose`;
//   иконки — `UAsdaIconCellGrid`; подсказки — `UAsdaItemTooltipSubsystem` (владелец `EAsdaTipOwner::SelfStore`);
//   записи инвентаря — `UAsdaInventorySubsystem::ApplyExternalRecord`; сброс — `IAsdaSessionResettable`.
//
// СОБЫТИЯ JUI (доказательство — `AsdaPetCageSubsystem.h`): 0xB — нажатие ЛКМ, 0xE (14) — нажатие ПКМ (★ НЕ «двойной
//   клик», как в B §1.1), 8/9/0xA — наведение вошло/держится/ушло, 0x15 — ESC, 0x17 — колесо, 66561 — клик кнопки,
//   66817 — радио, 67587 — Enter в поле, 67591 — ЛЮБАЯ другая клавиша в поле (ui_core.c:116253, т.е. на каждый символ).
//
// ИСПРАВЛЕННЫЕ ДЕФЕКТЫ ОРИГИНАЛА (закон `fix-original-defects`, ключи `asda.Trade.Store*`):
//   К-1 (6306 ресинхронизирует 6 из 10 записей), К-3 (премиум теряется после «Изменить»), К-4 («Изменить» премиум-лавки
//   повторяет ПОСЛЕДНИЙ использованный предмет, а не билет), К-5/К-6 (сумма > 2,1 млрд обрывает перерисовку / переполнение
//   i32 шлёт 6305; [ПОРТ] лот с кол-во × цена > 2^31−1 не отправляется — 107040), К-16 (новое: одноразовый флаг 6302
//   гаснет ДО проверки названия — после 107062/9005 «OK» мёртв до переоткрытия).
//   «К-13» (нумпад 7 без верхней границы) — НЕ дефект: поле режется до максимума (`uiClampQuantityToMax`), ревизия Л1 МЕЛК-1.
//   [ПОРТ] Клиентский потолок цены за единицу — 1 000 000 000 (серверный патч P43).
//   ★ Волна Л2: К-2 (6312 выкидывает из корзины позиции только по индексу ПЕРВОЙ записи пакета), К-8 (6311 у продавца
//   чистит слот сумки целиком при распродаже лота, частичную продажу не видно), К-10 (индекс лота в 6309 = номер строки
//   на экране; предмет без записи в таблицах сдвигает индексы) — ключи `asda.Trade.StoreFixK2/K8/K10`; [ПОРТ] 6309 со
//   строкой, где кол-во × цена переполняет i32, не уходит (`asda.Trade.StoreBuyGuard`); [ПОРТ] смерть посетителя → 6305
//   (`asda.Trade.StoreVisitorLeaveOnDeath`).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "UI/AsdaShopDblClick.h"        // FAsdaShopDblClick — лот в корзину гостя двойным щелчком (10.10.2026)
#include "AsdaSessionReset.h"          // IAsdaSessionResettable
#include "Styling/SlateTypes.h"        // FButtonStyle — кэш стилей вкладок общей точки `ApplyTabState`
#include "AsdaSelfStoreSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UEditableText;
class UAsdaIconCellGrid;
class UAsdaButtonRelay;
class UScrollBox;          // журнал 23402 витрины (волна Л2)
struct FAsdaItemInstance;

// Строка продажи окна 22000 (`CSellWindow+0x28+40N` / `+0x34+40N`).
USTRUCT()
struct FAsdaStallSellRowUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UImage>        Icon = nullptr;       // 22308+20N `Stall_Open_SellIconNN_<k>_0` (иконка)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    IconText = nullptr;   // 22308+20N `Stall_Open_SellIconNN` (кол-во / «+N%»)
	UPROPERTY(Transient) TObjectPtr<UEditableText> Price = nullptr;      // 22301+20N `Stall_Open_SellPriceNN`
	UPROPERTY(Transient) TObjectPtr<UEditableText> Qty = nullptr;        // 22304+20N `Stall_Open_SellAmountNN`
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    Total = nullptr;      // 22307+20N `Stall_Open_SellTPirceNN` (итог строки)
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> Btn;                // 22302/22303/22305/22306 +20N — ▲▼ цены, ▲▼ кол-ва
};

// Товар витрины хозяина 23000 (`CShopDlg+0xA4+16N`).
USTRUCT()
struct FAsdaStallLotRowUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UImage>     Icon = nullptr;          // 23121+10N `Stall_SellIconNN_<k>_0`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> IconText = nullptr;      // 23121+10N `Stall_SellIconNN` (остаток / «+N%»)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Price = nullptr;         // 23122+10N `Stall_SellIQntyNN` (★ цена за штуку; имя ассета врёт)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Total = nullptr;         // 23123+10N `Stall_SellIPriceNN` (итог строки)
};

UCLASS()
class ASDAREBORN_API UAsdaSelfStoreSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// Сброс сессии: окна прячутся без пакетов (сервер при разрыве сам закрывает лавку — `DisConnectUser` S:111416-111420),
	//   признаки/списки гаснут; запись билета и премиум — только при полном сбросе (другой персонаж).
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaSelfStoreSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	static constexpr int32 kMaxLots      = 10;           // `sub_92E185(list) < 0xA` — строк продажи / лотов витрины
	static constexpr int32 kGridCells    = 30;           // 22100…22129
	static constexpr int32 kGridCols     = 6;            // страница сетки «Расходников» = 6 × позиция колеса
	static constexpr int32 kScrollMax    = 5;            // `uiControlSetScrollRange(22199, 4, 9)` ⇒ 9 − 4 = 5
	static constexpr int32 kMinLevel     = 10;           // `playerLevel >= 10`, иначе 107080
	static constexpr int32 kPriceCap     = 1000000000;   // [ПОРТ] P43: потолок цены за единицу (сервер открытия)
	static constexpr int32 kNameMaxChars = 50;           // `ctrlEditSetTextLength(22011, 50)`
	static constexpr int32 kWinZ         = 730;          // z окон лавки (ниже модальных; наверх поднимает показ)

	// ═══ ВХОДЫ ═══
	// `UI_DispatchTargetContextMenu` кат. 2 пункт 7 (эмоция 207 / хоткей 65 / доп-меню cmd 2): окно подготовки открыто ⇒ молча,
	//   иначе `SendReady6300`. Гейт «верхом» (2040) — у вызывающего (`PlayEmotion`). true — 6300 ушёл.
	bool DispatchPrivateShop(const TCHAR* Source);
	// `netSendPlayerInfo6300@0xDD45C0`: элитный магазин / окно ремесла / диалог количества ремесла ⇒ молча; уровень < 10 ⇒
	//   msgbox 107080; иначе 6300 (кэш charId/userNum хозяина `CShopDlg+0x274/+0x278` — своими). true — пакет ушёл.
	bool SendReady6300(const TCHAR* Source);
	// Билет премиум-лавки применён (5451 подтип 65 → `netRecvItemBuffApply` case 64). `Rec57` — запись предмета из тела 5451.
	void OnTicketUsed(const uint8* Rec57);

	// ═══ СОСТОЯНИЕ (для гейтов/самотеста) ═══
	bool IsSellWindowOpen() const { return bSellOpen; }          // `CSellWindow+0x300`
	bool IsOwnerShowcaseOpen() const { return bShopOpen && bSpecialShop; }   // витрина 23000 в режиме хозяина
	bool IsShowcaseOpen() const { return bShopOpen; }            // `CShopDlg+0x168`
	bool IsPremium() const { return bPremium; }                  // `CSellWindow+0x389`
	bool IsReopenPending() const { return bReopen; }             // `CShopDlg+0x28C` («Изменить» / переоткрытие)
	int32 GetSellCount() const { return SellList.Num(); }        // строк продажи
	int32 GetSourceTab() const { return SourceTab; }             // 0 «Расходники» (контейнер 2) / 1 «Снаряжение» (контейнер 1)
	int32 GetScrollPos() const { return ScrollPos; }             // позиция колеса 22199 (0..5)
	int32 GetLotCount() const { return Lots.Num(); }             // лотов витрины
	FString GetStoreTitle() const { return StoreTitle; }         // `CSellWindow+0x308` / `CShopDlg+0x171`

	// ═══ ДЕЙСТВИЯ ОКНА 22000 (публичные — их же зовёт самотест путём игрока) ═══
	void OnGridCellClicked(int32 Cell, bool bRightButton);   // 0xB / 0xE по 22100+Cell (`sellWindowOnSlotClick`)
	void OnSellIconClicked(int32 Row);                        // 0xB по 22308+20N (`cSellWindow_onRemoveButtonClick`)
	void OnPriceFieldClicked(int32 Row);                      // 0xB по 22301+20N (`sellWnd_ClearSlotQtyByButton` — обнуляет ЦЕНУ)
	void OnRowButton(int32 Row, int32 Kind);                  // 66561 ▲▼: Kind 1 цена▲ / 2 цена▼ / 3 кол-во▲ / 4 кол-во▼
	void OnRowEditText(int32 Row, bool bPrice);               // 67587/67591 по 22301/22304 (`sellWnd_OnPriceButton` виды 5/6)
	void OnTabSelected(int32 Tab);                            // 66817 по 22200
	void OnWheel(int32 Notches);                              // 0x17 по 22199 (только вкладка 0)
	void OnNameSaveClicked();                                 // 66561 по 22012 «Изменить» (название)
	void OnNameFieldClicked();                                // 0xB по 22011 — очистка поля
	void OnOkClicked();                                       // 66561 по 22500
	void SendClose6305(const TCHAR* Why);                     // 22004 / 22501 / 0x15 / 23005 / 23306 / смерть (`antiCheat_SendPositionReport`)
	void OnShowcaseMainButton();                              // 66561 по 23305: хозяин — «Изменить», посетитель — покупка (msgbox 62)

	// ═══ ПОСЕТИТЕЛЬ (волна Л2) ═══
	static constexpr int32 kCartMax = 6;                      // `sub_923CDA(cart) < 6`, иначе 107086
	// Клик по вывеске лавки (`tradeRequestTargetByServerIdx@0xDE0F80` — МИСНЕЙМ) → `netSendTradeTargetRequest6307`: хозяин
	//   (userNum из вывески) найден в мире, я не в режиме хозяина, витрина закрыта, подготовка закрыта, прошло > 1000 мс;
	//   у отправителя — разговор с NPC (`g_bInventoryLocked`) ⇒ молча. Носитель клика — вывески волны Л3. true — 6307 ушёл.
	bool RequestVisit(int32 InHostUserNum, const TCHAR* Source);
	bool IsVisitorShowcaseOpen() const { return bShopOpen && !bSpecialShop; }   // витрина 23000 в режиме посетителя
	int32 GetCartCount() const { return Cart.Num(); }             // позиций корзины (`CShopDlg+0x15C`)
	int32 GetGuestCount() const { return GuestCount; }           // `CShopDlg+0x290` (из 6315, эталон не выводит — К-15)
	bool IsBuyPending() const { return bPendingBuy; }            // `CShopDlg+0x27D` («можно просить подтверждение покупки»)
	int32 GetHostCharId() const { return HostCharId; }           // `+0x274`
	int32 GetHostUserNum() const { return HostUserNum; }         // `+0x278`
	void OnLotClicked(int32 Row, bool bRightButton);             // 0xB / 0xE по 23121+10N (`shopDlg_AddTabItemToCart`)
	void OnCartCellClicked(int32 Cell);                          // 0xB по 23250+k (`itemSlotWndShowSlotCtrl` + `shopDlgOnCartRemoveButton`)
	void OnTrashClicked();                                       // 66561 по 23243 (`shopCartRemoveItem(pSelectedSlotItem)`)
	void OnLogHomeClicked();                                     // 66561 по 23404 — журнал 23402 в конец
	void OnBuyConfirmed();                                       // «OK» msgbox 62 (`OnSendItemUpgrade` case 0x3E) → `shopDlg_SendBuyReq`

	// ═══ ЧАТ ЛАВКИ (волна Л4) ═══
	static constexpr int32 kChatInputLimit = 60;                 // `.JUI` 23403 `LimitText = "60"` (EM_LIMITTEXT поля, байты cp1251)
	// 67587 (Enter) по 23403 (`CShopDlg_OnCommand`, shop_trade.c:17541-17561): пусто — ничего; мат (`Chat_MatchBannedWord`) → 9005 в
	//   журнал 23402 (без имени, 0xFFFF9600) + очистка поля; иначе 6313 (имя своего персонажа, хозяин из `+0x274/+0x278`) + очистка.
	void OnChatInputEnter();
	// Поле ввода 23403 держит клавиатурный фокус (гейт окон h `uiCanOpenWindowGate` и глушение горячих клавиш — ввод съедает поле).
	bool IsChatInputFocused() const;
	UEditableText* DebugChatInput() const { return EdChat; }    // поле 23403 (путь игрока самотеста)

	// ═══ ПРИЁМ (из очереди в Tick) ═══
	// Разобрать накопленные пакеты 6301/6303/6306 (Tick; самотест зовёт сам после штатной подачи `DebugFeedPacket`).
	void PumpPending();

	// ═══ ДИАГНОСТИКА ═══
	FString DumpState() const;                                // одна строка состояния
	FString DumpControls() const;                             // разбор контролов обоих окон (сколько найдено)
	int32 GetFoundControls22000() const { return Found22000; }
	int32 GetFoundControls23000() const { return Found23000; }
	int32 GetFoundControlsVisitor() const { return FoundVisitor; }   // волна Л2: 23243 / 23404 / 23402 (из 3)
	FString DebugRowText(int32 Row, int32 Col) const;         // текст поля строки (0 цена / 1 кол-во / 2 итог / 3 надпись иконки)
	FString DebugSumText() const;                             // 22511
	FString DebugGridText(int32 Cell) const;                  // надпись ячейки сетки
	float   DebugGridAlpha(int32 Cell) const;                 // прозрачность иконки ячейки (0x7D/255 — выставлена)
	FString DebugShowcaseText(int32 IdNum) const;             // тексты витрины по IDNum (23006/23301/23302/23305/23122+10N/23123+10N)
	bool    DebugListHdrShown() const;                        // 23240 показан (у хозяина — нет)
	UUserWidget* DebugSellWidget() const { return SellWidget; }   // корень 22000 (геометрия путём игрока)
	UUserWidget* DebugShopWidget() const { return ShopWidget; }   // корень 23000
	const TArray<FString>& GetJournal() const { return Journal; }
	void ClearJournal() { Journal.Reset(); }
	// Самотест: пересчитать кэш «кто хозяин» без 6300 (`CShopDlg+0x274/+0x278`).
	void DebugSetHost(int32 CharId, int32 UserNum) { HostCharId = CharId; HostUserNum = UserNum; }
	// ── диагностика посетителя (волна Л2) ──
	FString DebugCartText(int32 Cell) const;                  // надпись ячейки корзины 23250+k
	int32   DebugCartCode(int32 Cell) const;                  // код предмета в ячейке корзины (−1 — пусто)
	int32   DebugCartQty(int32 Cell) const;                   // кол-во к покупке у позиции корзины
	bool    DebugCartSelShown() const;                        // рамка 23241 видна
	float   DebugLotAlpha(int32 Row) const;                   // прозрачность иконки лота (0x7D/255 — в корзине/распродан)
	int32   DebugLotStock(int32 Row) const;                   // остаток лота строки (−1 — распродан)
	FLinearColor DebugTotalColor() const;                     // цвет суммы 23302
	TArray<FString> DebugLogLines() const;                    // строки журнала 23402
	bool    DebugTrashShown() const;                          // урна 23243 видна

private:
	// Строка списка продажи (`std::list` записей 24 б: [0] слот, [1] база, [2] цена/шт, [3] кол-во, [4] итог, [5] макс).
	//   [ПОРТ] Вместо указателя на слот — адрес ячейки инвентаря (контейнер + номер); данные читаются живыми.
	struct FSellEntry
	{
		int32 Container = -1;   // запись+4 слота (1 сумка / 2 расходники)
		int32 Cell = -1;        // номер ячейки контейнера
		int32 BasePrice = 0;    // [1] — база шага ▲▼ (5 % от неё)
		int32 UnitPrice = 0;    // [2] — цена за единицу
		int32 Qty = 0;          // [3] — количество к продаже
		int64 Total = 0;        // [4] — итог строки ([ПОРТ] int64: К-5/К-6)
		int32 MaxQty = 0;       // [5] — размер стопки на момент выставления (0 ⇒ нештучный: в 6302 кол-во 0)
	};
	// Лот витрины (`CShopItem` 88 б, `sub_924946` → `sub_DD3A60`). Хозяину — из 6303, посетителю — из 6308 (`CShopDlg_AddBuyItem`).
	struct FLot
	{
		int32 ItemId = 0;       // +0
		int32 Price = 0;        // +8 цена за единицу
		int32 Stock = 0;        // +12 остаток (−1 — распродан; 0 из пакета ⇒ 1)
		int32 CartQty = 0;      // +16 кол-во к покупке (корзина держит УКАЗАТЕЛИ на лоты — поле живёт в самом лоте)
		int32 RawQty = 0;       // +24 кол-во из пакета ДО замены «0 ⇒ 1» (0 — нештучный: в 6309 уходит 0)
		int32 Container = 0;    // +28
		int32 Slot = -1;        // +32
		int32 Row = -1;         // +36 номер строки на экране (он же индекс лота 6309 у оригинала)
		int32 SrvIdx = -1;      // [ПОРТ] К-10: индекс лота на сервере (позиция в 6308 / поле +11 записи 6303)
		int16 Sowel[4] = { 0, 0, 0, 0 };            // +44 4×i16 (6308 +11)
		int32 Ench5[5] = { 0, 0, 0, 0, 0 };         // +52 5×i32 (6308 +24)
		uint8 EnchantFlag = 0;  // +72 (6308 +19; у хозяина — байт +24 записи своего инвентаря)
		uint8 Ench73 = 0;       // +73 (6308 +20)
		uint8 Ench74 = 0;       // +74 (6308 +21)
		int32 EnchantPct = 0;   // +76 i16 (6308 +22; у хозяина — +27 записи своего инвентаря)
		int16 StackWeight = 0;  // +80 i16 (6308 +9)
		uint8 Durability = 0;   // +84 u8 (6308 +8)
	};

	// ── окно 22000 ──
	bool EnsureSellWidget();                 // каркас `FAsdaJuiWindowHandle::Ensure` + разбор контролов (`CSellWindow_InitControls`)
	void ResetSellWidget();                  // снести виджет и зону
	void SetupSellControls();                // разбор дерева WBP 22000 по именам `.JUI`, подписи init
	void OpenSellWindow();                   // `sellWindow_Open@0xDCF6C0`
	void HideSellWindow(const TCHAR* Why);   // `sellWndHide@0xDD0DC0`
	void RefreshGrid();                      // `CWarehouseDlg_RefreshItemGrid` (вкладка 0) / `CInventoryDlg_RefreshItemGrid` (1)
	void RefreshSellList();                  // `CSellWindow_RefreshItemList@0xDD01A0`
	void RefreshTabs();                      // вид радио 22200
	void AddItemToSell(int32 Container, int32 Cell, int32 Quantity);   // `CSellWindow_addItemToSell@0xDCFC60`
	void RemoveFromList(int32 Container, int32 Cell);                  // `cSellWindow_removeItemFromList@0xDCFB00`
	bool IsListed(int32 Container, int32 Cell, int32 ItemId) const;    // сравнение «код + слот + контейнер» эталона
	void StoreOpenSendRequest();             // `storeOpenSendRequest@0xDD3AA0` → 6302
	void TickSellMouse();                    // наведение/ЛКМ/ПКМ окна 22000
	void TickTip(int32 Kind, int32 Idx);     // подсказка: Kind 0 сетка / 1 иконка строки / 2 лот витрины; −1 — спрятать
	int32 BoosterAvgValue(int32 ItemId) const;   // `booster_calcAvgValue@0xDD0000`

	// ── окно 23000 (режим хозяина) ──
	bool EnsureShopWidget();                 // каркас + разбор (`CShopDlg_InitControls`)
	void ResetShopWidget();
	void SetupShopControls();
	void OpenShowcase();                     // `shopDlgOpenSellWindow@0xDD7020`
	void HideShowcase(const TCHAR* Why);     // `shopDlgShow@0xDD7580` (МИСНЕЙМ = скрыть; хозяину — поза 109)
	void RefreshShowcaseTotals();            // `CShopSellDlg_RefreshTotalPrice@0xDD7600` (корзина пуста у хозяина)
	void RefreshShowcaseList();              // `CShopDlg_RefreshItemList@0xDD7F80`
	void TickShopMouse();                    // наведение по лотам 23121+10N

	// ── пакеты ──
	void OnPacket(int32 Op, const TArray<uint8>& Body);   // колбэк сети: только копит
	void HandleReadyResult6301(const TArray<uint8>& Body);     // `itemEnhanceOnResult@0xDD4F80`
	void HandleOpenResult6303(const TArray<uint8>& Body);      // `pktHandleShopWindowOpen@0xDD3310`
	void HandleCloseResult6306(const TArray<uint8>& Body);     // `sellWindowOnSellResult@0xDD5230`
	void HandleVisitOpen6308(const TArray<uint8>& Body);       // `shopHandleOpenShopPacket@0xDDA580`
	void HandleBuyResult6310(const TArray<uint8>& Body);       // `shopSellDlg_OnSellResult@0xDDBF40` (МИСНЕЙМ = ответ 6309)
	void HandleSold6311(const TArray<uint8>& Body);            // `shopDlg_onBuyResultPacket@0xDDC480` (МИСНЕЙМ = продавцу)
	void HandleShowcaseUpdate6312(const TArray<uint8>& Body);  // `shopDlg_onConfirmBuyPacket@0xDDCAA0` (МИСНЕЙМ = гостям)
	void HandleGuests6315(const TArray<uint8>& Body);          // `shopDlgAppendTradeResultLog@0xDDB3F0` (МИСНЕЙМ = гости)
	void HandleChat6314(const TArray<uint8>& Body);            // `shopDlg_RecvLogLineColored@0xDDB1F0` (волна Л4)

	// ── посетитель: корзина, покупка, журнал ──
	void CartAddItem(int32 LotIdx);                 // `shopCart_addItem@0xDD9A50`
	void CartRemoveItem(int32 LotIdx);              // `shopCartRemoveItem@0xDD9BC0`
	bool SendBuyReq();                              // `shopDlg_SendBuyReq@0xDDB5F0` → 6309; true — пакет ушёл
	// [ПОРТ] Стоимость позиции корзины так, как её посчитает СЕРВЕР (`CharSelfStoreBuyItem`, GameServer.c:100863-100886) — для
	//   `asda.Trade.StoreBuyGuard`. `WireQty` — кол-во, уходящее в 6309 (0 у нештучного). Правило и сверка столбцов — в .cpp.
	int64 ServerLinePrice(const FLot& Lot, int32 WireQty) const;
	// ★ Волна Л4/Л5 (ревизия Л3, МЕЛК-1): код входит в правило сервера «цена лота» (`Item_Table.txt` СЕРВЕРА +7 == 4 И +26 == 10)?
	//   Источник — ДАННЫЕ `Config/DefaultAsdaTrade.ini` [SelfStore.ServerLotPriceRule] (выгрузка из живой таблицы сервера
	//   `Tools/asda_export_store_lotprice_codes.py`); секции нет — клиентская `item_list` (4 и 10). `bOutFromData` — ответ из данных.
	bool IsServerLotPriceCode(int32 ItemId, bool& bOutFromData) const;
	int32 LotAtRow(int32 Row) const;                // индекс лота, показанного в строке (`slots_array[Row]`); −1 — нет
	int32 LotIndexForWire(const FLot& Lot) const;   // индекс лота для 6309/сверки 6311/6312 (К-10: `SrvIdx`, иначе `Row`)
	// Применить к витрине записи 6311/6312 (45 б с +11): остатки и строки журнала 107082 (`BuyerCharId` виден ⇒ строка).
	void ApplySoldLots(const uint8* Lots, int32 BuyerCharId);
	void EnsureLogList();                           // журнал 23402 (узла в WBP нет — список строится кодом, решение владельца)
	void AddLogLine(const FString& Who, const FString& Text, const FColor& Color);   // `shopDlg_AddLogLineEllipsis@0xDD6D00`
	bool CharNameById(int32 CharId, FString& OutName) const;   // `CharManager_FindByCharId` + `thunk_getFieldAt1056` (имя)
	FString ItemNameOf(int32 ItemId) const;         // `ItemData_FindByCode` / `itemGetTooltipById` / `sowelTableFindRecordByCode`
	void ShowLotTooltip(const FLot& Lot);           // `itemSlotWnd_showSlotTooltip@0xDD9E80` по полям лота (синтетическая запись)
	static TArray<uint8> BuildLotTipRecord(const FLot& Lot, bool bFull);   // ★ U17: та самая синтетическая запись 57 б (самотест побайтно)

	// ── общее ──
	bool SetPose(int32 Code, const TCHAR* Why);   // `CharAction_SendStatePose` через общую точку контроллера
	void MsgBoxLoc(int32 LocId);                  // `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, loc)` — ONEBUT 1500
	FString Loc(int32 Id) const;                  // loc-строка (теги срезаны)
	void JournalAdd(const FString& Line);         // журнал самотеста + лог
	const FAsdaItemInstance* CellItem(int32 Container, int32 Cell) const;   // живая запись ячейки инвентаря
	FString SelfName() const;                     // имя своего персонажа (`thunk_getFieldAt1056(LocalPlayer)`)

	// ── виджеты 22000 ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget>   SellWidget = nullptr;     // корень WBP_StreetStall_Open_Win
	UPROPERTY(Transient) TObjectPtr<UButton>       BtnSellClose = nullptr;   // 22004 `Stall_Open_ClsBtn`
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtNameTitle = nullptr;   // 22010
	UPROPERTY(Transient) TObjectPtr<UEditableText> EdName = nullptr;         // 22011 (МИСНЕЙМ IDA `m_pGoldEditTxt`)
	UPROPERTY(Transient) TObjectPtr<UButton>       BtnNameSave = nullptr;    // 22012 (МИСНЕЙМ `m_pCategoryCombo`)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtNameSave = nullptr;    // подпись 22012
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> Hdrs;                // 22201, 22021, 22210, 22211, 22212
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     GridImg;             // 22100…22129 планы-иконки
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> GridTxt;             // 22100…22129 надписи
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     TabPlanes;           // 22200 два плана
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    TabButtons;          // 22200 `_OPT0/_OPT1`
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TabCaptions;         // `_Text` / `_Text2`
	TArray<FSlateBrush>  TabIdlePlanes;                                      // кэш общей точки `ApplyTabState`
	TArray<FButtonStyle> TabIdleStyles;                                      // кэш стилей кнопок вкладок
	TArray<FLinearColor> TabIdleCaptions;                                    // кэш цветов подписей вкладок
	UPROPERTY(Transient) TArray<FAsdaStallSellRowUi>    SellRows;            // 10 строк продажи
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtSumCap = nullptr;      // 22510
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtSum = nullptr;         // 22511
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtGoldCap = nullptr;     // 22512
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtGold = nullptr;        // 22513
	UPROPERTY(Transient) TObjectPtr<UButton>       BtnOk = nullptr;          // 22500
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtOk = nullptr;          // подпись 22500
	UPROPERTY(Transient) TObjectPtr<UButton>       BtnCancel = nullptr;      // 22501
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtCancel = nullptr;      // подпись 22501
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridIcons = nullptr;  // рендер иконок сетки (общая точка)
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> SellIcons = nullptr;  // рендер иконок строк продажи

	// ── виджеты 23000 ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget>   ShopWidget = nullptr;     // корень WBP_StreetStall_Win
	UPROPERTY(Transient) TObjectPtr<UButton>       BtnShopX = nullptr;       // 23005 `Stall_ClsBtn0`
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtShopTitle = nullptr;   // 23006 `StreetStall_Title_Text` (МИСНЕЙМ `lblNpcName`)
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ShopCols;            // 23101…23105
	UPROPERTY(Transient) TArray<FAsdaStallLotRowUi>     LotRows;             // 10 товаров витрины
	UPROPERTY(Transient) TObjectPtr<UWidget>       SelRect = nullptr;        // 23230 рамка выбора витрины
	UPROPERTY(Transient) TObjectPtr<UWidget>       ListHdr = nullptr;        // 23240 группа корзины (`Stall_PurItemBase_grp`)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtCartLbl = nullptr;     // 23242
	UPROPERTY(Transient) TObjectPtr<UWidget>       CartSel = nullptr;        // 23241 рамка выбора корзины
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     CartImg;             // 23250…23259 иконки корзины (очистка)
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> CartTxt;             // 23250…23259 надписи корзины
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtTotalCap = nullptr;    // 23301
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtTotal = nullptr;       // 23302
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtMoneyCap = nullptr;    // 23303
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtMoney = nullptr;       // 23304
	UPROPERTY(Transient) TObjectPtr<UButton>       BtnMain = nullptr;        // 23305 «OK» / «Изменить»
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtMain = nullptr;        // подпись 23305
	UPROPERTY(Transient) TObjectPtr<UButton>       BtnShopCancel = nullptr;  // 23306 «Отмена»
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtShopCancel = nullptr;  // подпись 23306
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    TxtChatTitle = nullptr;   // 23401 (эталон ставит пусто)
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> LotIcons = nullptr;   // рендер иконок лотов витрины
	// ── виджеты 23000 режима посетителя (волна Л2) ──
	UPROPERTY(Transient) TObjectPtr<UButton>       BtnTrash = nullptr;       // 23243 `Stall_Pur_TrashBtn` (урна)
	UPROPERTY(Transient) TObjectPtr<UButton>       BtnLogHome = nullptr;     // 23404 `Stall_Chat_HomeBtn` («в конец» журнала)
	UPROPERTY(Transient) TObjectPtr<UScrollBox>       LogList = nullptr;     // 23402 `Stall_Chat_History` (строится кодом)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    LogFontSrc = nullptr;     // образец шрифта строк журнала (23401 — тот же скин текста)
	UPROPERTY(Transient) TObjectPtr<UEditableText> EdChat = nullptr;         // 23403 `Stall_Chat_Input` (волна Л4; `CShopDlg::editMemo`)
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> ChatRelays;             // реле поля 23403 (лимит 60 и Enter) — якоря от GC
	bool bDebugCurseAll = false;                                             // самотест Л4: мат-фильтр считает любой текст матом

	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;        // реле кнопок (держат делегаты)

	// ── состояние `CSellWindow` ──
	bool  bSellReady = false;        // WBP 22000 разобран (`+0x4B8`, МИСНЕЙМ-глобал `g_bSellWndOpen` — «инициализировано»)
	bool  bSellOpen = false;         // `+0x300` m_bDlgVisible
	bool  bCanSendOpen = false;      // `+0x388` одноразовый «можно слать 6302»
	bool  bPremium = false;          // `+0x389` (глобал `byte_2C2F759`)
	TArray<uint8> Ticket45;          // `+0x38A` запись билета 45 б (`slotStructReset` + код/контейнер/слот из 5451)
	int32 SourceTab = 0;             // `+0x2F0` вкладка источника
	int32 ScrollPos = 0;             // `+0x2EC` позиция колеса 22199
	TArray<FSellEntry> SellList;     // `+0x2C8` список выставленного (≤ 10)
	TArray<FIntPoint>  GridRef;      // 30 × (контейнер, ячейка) — `gap_184[8*i+52]`; (−1, −1) — пусто
	TArray<int32>      RowEntry;     // 10 × индекс в `SellList` (−1 — строка пуста) — `gap_28[40N]`
	FIntPoint PendingItem = FIntPoint(-1, -1);   // `m_pPendingItem` — предмет под нумпадом режима 7
	FString StoreTitle;              // `+0x308` название лавки (оно же `CShopDlg+0x171`)
	bool  bNameGuard = false;        // защита от повторного входа при перезаписи полей строк (SetText при опросе полей)
	// [ПОРТ] Опрос полей ввода вместо событий 67591/67587: эталон зовёт `sellWnd_OnPriceButton` на КАЖДУЮ клавишу в поле
	//   (ui_core.c:116253 → 64991); здесь — на каждое изменение текста, замеченное в Tick. 0..9 — цена строки, 10..19 — кол-во.
	TArray<FString> EditCache;
	FString NameCache;               // последний текст поля 22011 (обрезка до 50 символов — `ctrlEditSetTextLength`)
	int32 Found22000 = 0;            // узлов 22000 разобрано

	// ── состояние `CShopDlg` (режим хозяина) ──
	bool  bShopReady = false;        // WBP 23000 разобран (`bInitialized`, МИСНЕЙМ-глобал `g_bShopDlgOpen`)
	bool  bShopOpen = false;         // `+0x168`
	bool  bSpecialShop = false;      // `+0x27C` режим хозяина
	bool  bReopen = false;           // `+0x28C` «переоткрытие» (МИСНЕЙМ-глобал `g_bSellWindowActive`, поле IDA `bBuySent`)
	int32 HostCharId = 0;            // `+0x274` charId хозяина (МИСНЕЙМ `g_CachedLocalPlayerSerial`)
	int32 HostUserNum = 0;           // `+0x278` userNum хозяина (МИСНЕЙМ `g_CachedSelectedUserIdx`)
	FString OwnerName;               // `+0x1F1` имя хозяина (эталон не выводит — К-15)
	TArray<FLot> Lots;               // `+0x150` витрина (≤ 10)
	int32 Found23000 = 0;            // узлов 23000 разобрано
	int32 FoundVisitor = 0;          // волна Л2: привязок режима посетителя (урна 23243, «в конец» 23404, журнал 23402)

	// ── состояние `CShopDlg` (режим посетителя, волна Л2) ──
	TArray<int32> Cart;              // `+0x15C` корзина: индексы лотов `Lots` (у эталона — указатели на `CShopItem`), порядок добавления
	TArray<int32> CartCellLot;       // `field_C8_pad[k]` — лот, нарисованный в ячейке корзины 23250+k (−1 — пусто; пишет перерисовка корзины)
	int32 SelLot = -1;               // `+0x294` `pSelectedSlotItem` — лот, выбранный кликом по ячейке корзины (рамка 23241)
	int32 PendingQtyLot = -1;        // `pPendingQtyItem` — лот под нумпадом режима 8
	bool  bPendingBuy = true;        // `+0x27D`: 1 — «OK» спрашивает msgbox 62; 0 — 6309 ушёл, ждём 6310 (107081)
	int32 GuestCount = 0;            // `+0x290` (`set_oA4`/`get_oA4`) — число гостей из 6315
	double LastVisitReqAt = -10.0;   // `g_dwLastTradeReqTime` — троттл 6307 (1000 мс)
	bool  bLogAutoScroll = true;     // `bAutoScrollLog` — первая строка после открытия прокручивает журнал в конец
	double VisitorDeathAt = 0.0;     // [ПОРТ] троттл «посетитель мёртв → 6305» (4 с)
	mutable TSet<int32> SrvLotPriceCodes;    // коды правила «цена лота» сервера из `DefaultAsdaTrade.ini` (ленивая загрузка)
	mutable bool bSrvLotPriceLoaded = false; // данные правила уже читались (пусто после чтения — секции нет)

	// ── мышь/подсказки ──
	FAsdaMouseFrame Mouse;           // латч нажатий мыши окон лавки
	// [ПОРТ] 10.10.2026: двойной щелчок по лоту витрины гостя (ключ — индекс лота + кнопка: 0 ЛКМ, 1 ПКМ).
	FAsdaShopDblClick LotDbl;
	int32 TipKind = -1;              // показанная подсказка: вид
	int32 TipIdx = -1;               // показанная подсказка: индекс

	// ── приём ──
	struct FPending { int32 Op = 0; TArray<uint8> Body; };   // пакет в очереди
	TAsdaNetQueue<FPending> PendingPackets;  // очередь колбэка сети (разбор в Tick)
	FDelegateHandle HNet;            // подписка на `UAsdaNetSubsystem::OnSelfStorePacket`

	TArray<FString> Journal;         // журнал самотеста
	friend struct FAsdaSelfStoreL1SelfTest;
	friend struct FAsdaSelfStoreL2SelfTest;   // самотест посетителя (волна Л2)
	friend struct FAsdaSelfStoreL3SelfTest;   // самотест вывесок (волна Л3)
	friend struct FAsdaSelfStoreL4SelfTest;   // самотест чата лавки (волна Л4)
	friend struct FAsdaSelfStoreL5SelfTest;   // самотест гейтов лавки (волна Л5)
	friend struct FAsdaDedupW1ASelfTest;      // ★ В1 дедупа №2: побайтная сверка `BuildLotTipRecord` с прежней сборкой
};
