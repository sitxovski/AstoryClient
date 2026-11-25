// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaBoosterShopSubsystem — ★★★ МАГАЗИН БУСТЕРОВ, окно `BOOSTER_SHOP_WIN` (JUI IDNum **15300**,
//   334 узла) + панель покупки `BOOSTER_PUR_WIN` (**15515**) + подложка сообщения
//   `SHELLITEM_MsgBase` (**460**).
//   Реверс — `Reports/premium_mall_shops.md`; порт и решения — `Reports/premium_mall_port.md`.
//   Класс оригинала — `CBoosterShop`, исходник `…\game source\interface boostershop.cpp`,
//   Init — `BoosterShop_InitWindow@0xB7E4A0` (`AsdaStory_ida.c:420291-420690`).
//
// ═══════════════ ★★★ ЖИВОСТЬ ВХОДА: ЧТО ДОКАЗАНО ПЕРЕД ПОРТОМ ══════════════════════════════════
//   В ЭТАЛОНЕ у окна 15300 ровно ДВА входа, и ОБА в текущей сборке недостижимы:
//     1. Сегмент 1 «Бустер» переключателя `PremiumMall_Menu` 30002 (`boosterShopOnMessage@0xB876F0`,
//        `case 30002`, `dialogPage == 1`, `ida.c:423296-423308`). Он приходит СОБЫТИЕМ **0x10501
//        (66817)**, а это событие во всём бинаре поднимает РОВНО ОДНА функция —
//        `uiList_OnItemClickSelect@0x12F6440` (grep «66817» по 1.28 млн строк: два вызова
//        `CControl_RaiseEvent`, оба в ней). И она НЕ поднимает его для индексов, лежащих в
//        контейнере контрола по смещению **+0x268** (dword 154):
//           if (!empty(this+0x268)) for (v : this+0x268) if (v == clickedIndex) return 1;   // МОЛЧА
//        ⇒ контейнер = СПИСОК НЕНАЖИМАЕМЫХ ПУНКТОВ. А `BoosterShop_InitWindow` кладёт в него
//        индексы **1** и **3** (`sub_931C4A(pPremiumMenuBtn, 1)` и `(…, 3)`, `ida.c:420670-420671`;
//        `sub_931C4A`→`sub_B807F0`: `containerInsertOrGrow(this + 154, &a2)`).
//        ★ ВТОРОЙ МЕТОД (не декомпил): дизассемблирование `0x12F6440` прямо из
//        `RE/ida/work/AsdaStory.exe` — `add ecx,0x268` / обход итератора / `cmp ecx,[ebp+8]` /
//        `mov al,1; ret` ДО `push 0x10501`. Ранний возврат подтверждён байтами.
//        ★ ПОЛОЖИТЕЛЬНЫЙ КОНТРОЛЬ: индексов 0 и 2 в контейнере НЕТ — и они работают (сегмент 0
//        открывает окно 6000, сегмент 2 — парикмахерскую).
//     2. Код кнопки NPC-диалога **30034** (`ida.c:599673-599675`). Кода 30034 нет ни в одном из
//        187 файлов диалогов пяти сборок (`Reports/warehouse_entrypoints_npcs.md:336-343`).
//   Других входов нет: `j_uiStorageWindow_Open` зовут ровно эти два места; скан
//   распакованного exe на immediate `15300` даёт ТРИ попадания — `BoosterShop_InitWindow` (поиск
//   контрола), `uiItemComposeDialog_OnMessage@0xB87610` (обработчик СВОЕГО же окна) и одно в
//   .rdata-мусоре библиотеки. Гипотеза «магазин жив как вкладка внутри другого окна» (как вышло
//   с витриной наборов 58300 внутри окна 6000) ОПРОВЕРГНУТА: 15300 — самостоятельный корень
//   `.JUI`, чужой код его не показывает.
//   ⇒ ВЫВОД: в оригинале магазин бустеров недостижим. Порт сделан по решению владельца, вход —
//   НОВЫЙ: кнопка NPC 146 «Робот "Пре-Пре 19"» (данные кнопки и ветку 30034 заводит владелец),
//   она зовёт публичный `OpenWindow()`. Сегмент 1 премиум-меню оставлен ЧЕСТНО НЕДОСТУПНЫМ, как
//   в оригинале; включить его можно ключом `asda.PremiumMall.EnableBoosterSegment 1`.
//
// ═══════════════ УСТРОЙСТВО ОКНА (все числа из `gamemain_jui.ini` и эталона) ═══════════════════
//   15300 корень 671×760 · 15301 крестик · 15599 `Booster_Shop_Part2` — подложка сетки, ★ высота
//        ПЕРЕСЧИТЫВАЕТСЯ кодом: `98 * (cellsPerPage / 3) + 48` (`ida.c:421847-421852`) ·
//   15303/15304 чекбокс «Искать для своего пола» — `Visible=0` и включения в коде НЕТ (Д-3),
//        а сам фильтр по полу НЕ РЕАЛИЗОВАН (Д-4: пол читается и не используется) ·
//   15545 `BOOSTER_SHOP_MENU` — 7 категорий: 0 Все (12775) · 1 Популярные (13240) · 2 Воин (6915) ·
//        3 Лучник (6916) · 4 Маг (6917) · 5 Доспех (8434) · 6 Инструмент (15150).
//        ★ В `.JUI` `Visible=0`, `SetVisible` в коде нет ⇒ 7 вкладок игроку недоступны (Д-2) ·
//   СЕТКА: 21 ячейка, ID = `15306 + 9*i + (i>=2 ? 1 : 0)`; состав ячейки:
//        +0 коробка 15306 · +1 «Подарок» 15307 (loc 6821; `Visible=0`, включения нет — Д-1) ·
//        +2 имя 15308 · +3 цена 15309 · +4 иконка 15310 (64×64) · +5 «Купить» 15311 (loc 6944) ·
//        +7 NEW 15313 (бейдж записи **2**) · +8 HOT 15314 (бейдж **1**) ·
//        клон плашки скидки 15560 + контейнер цифр 15570 + 9 цифр 15561…15569 ·
//   ПАГИНАЦИЯ: 15498/15499 счётчики · 15501 `NEXTP` +1 · 15503 `BACP` −1 ·
//        ★ ИМЕНА ВРУТ (Д-5): **15505 `FIRSTP` ведёт на ПОСЛЕДНЮЮ страницу, 15507 `ENDP` — на ПЕРВУЮ** ·
//   ДЕНЬГИ: 15510 «Сейчас GXP» (loc 12904) + 15511 значение + 15512 монета ·
//        15513 «Пополнить» (внешний браузер) · 15514 «Проверить баланс» (опкод 5466) ·
//        40004/40005 «Деньги в подарок» — эталон гасит в Init (Д-11) ·
//   ПАНЕЛЬ ПОКУПКИ 15515: 15538 имя · 15536 описание · 15537 вес (loc 2295) · 15533 иконка ·
//        15518/15519/15520 имя получателя + поле + «ОК» (только режим подарка) ·
//        15543 «Уровень:» (8019) / «Уровень персонажа» (13232) · 15544 КОМБО-1 = **usageCategory
//        вариантов** (подписи loc 12556+cat), а вовсе не «профессия персонажа» ·
//        15541 «Профессия:» (8020) / «Профессия персонажа» (13231) · 15542 КОМБО-2 = УРОВЕНЬ ·
//        15539 «Количество (шт.)» (13233) · 15540 КОМБО-3 = **количество 1…7** ·
//        15521/15522 «Цена» (13234) = кол-во × цена · 15524/15525 «Золото:» (13235) — ★ на деле
//        это КЭШ/GXP (`inventoryGold_FormatToBuf@0xECB950` читает `CCharacter+0x44C`) ·
//        15528/15529 «В хранилище» (6822) → байт 2 тела 5070; ★ группа `Visible=0` (Д-15) ·
//        15527 «OK» (6857) → ОБЩАЯ МОДАЛКА 1559, режим **F** · 15530 «Отмена» (2289) ·
//        15547 «Вид» (13237) → окно предпросмотра 15600 (уже портировано) ·
//        15590/15591 гайд-строки (loc 6960/6961) — эталон гасит сразу после заполнения (Д-10) ·
//   460/461 `SHELLITEM_MsgBase` — подложка «строки сообщения»: эталон кладёт в 461 loc **60000**
//        и ГАСИТ подложку (`SetField166(…,0)`, `SetFlag668(…,0)`); `SetVisible(…,1)` в бинаре
//        НЕТ вовсе (скан exe на `461`: 31 попадание, кодовое одно — внутри Init).
//        ⇒ как механизм сообщений подложка МЕРТВА; воспроизводим 1:1 (скрыта).
//
// ═══════════════ ★★★ СКОЛЬКО ЯЧЕЙОК НА СТРАНИЦЕ — НЕ КОНСТАНТА ════════════════════════════════
//   `uiStorageWindow_Open@0xB83B60` (`ida.c:421813-421818`):
//        self[666] = 15;
//        if (CConfig_GetHeight() <= 0x400) self[666] = 15;
//        if (CConfig_GetHeight() >= 0x400) self[666] = 21;
//   ⇒ высота экрана ≥ 1024 → **21** ячейки (3×7), иначе → **15** (3×5).
//   ★ При РОВНО 1024 срабатывают ОБЕ ветки и побеждает вторая (Д-12) — итог 21; порт пишет это
//   условие один раз и честно (`>= 1024 ? 21 : 15`), результат тот же.
//
// ═══════════════ ДАННЫЕ ВИТРИНЫ ═══════════════════════════════════════════════════════════════
//   `Bpremium.BIN` (magic "BII") — основной ассортимент, `Fpremium.BIN` ("FII") — вкладка
//   «Популярные». Импортёр — `Tools/asda_import_booster_shop.py`, таблица `DT_BoosterShopList`.
//   ⚠ `DT_booster_list` — ЭТО ДРУГОЕ (барабан 10050); отчёт §8.1 в этом месте ошибается.
//
// ═══════════════ СЕТЬ ═════════════════════════════════════════════════════════════════════════
//   Живой путь покупки ОДИН: **5070 → 5071** (общие точки `UAsdaNetSubsystem`, паддинг `a6=1`).
//   ★ 01.11.2026 P101_CashGift: подарок 15520 → **5072**, OK 1559 в режиме 2 → **5074** — общий владелец состояния
//     `UAsdaCashGiftSubsystem` (`Reports/cash_gift/re/client.md` §1.3). Пока на сервере P101 не выложен,
//     ключ `asda.CashGift.Enable 0`: кнопки подарка скрыты и ничего не уходит (как прежде).
//   ⚠ 5469 — это НЕ бустер-шоп, а `BOOSTING_WIN` 10050 (`UAsdaBoosterPanelSubsystem`).
//
// ═══════════════ НУМПАД 1410 ══════════════════════════════════════════════════════════════════
//   ★ НЕ УЧАСТВУЕТ. Сверено с `Docs/Port/systems/NUMPAD_FLOW.md` (закон `check-numpad-on-new-system`):
//   ни один из 13 режимов за магазином бустеров не закреплён; ни одна из 26 площадок
//   `uiOpenContextPopupWithHandlers@0xDA55A0` не попадает в диапазон `boostershop.cpp`
//   (0xB7E100…0xB88C00). Количество даёт КОМБОБОКС **15540** (пункты «1»…«7» зашиты в Init,
//   `ida.c:420480-420488`; значение через `atoi` подписи — `uiGetSelectedItemAsInt@0xB86570`).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Engine/DataTable.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "Layout/Margin.h"         // FMargin
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "UI/AsdaPremiumBuyQueue.h"   // FAsdaPremiumBuyQueue — «количество > 1» уходит поштучно (25.10.2026)
#include "AsdaBoosterShopSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UCheckBox;
class UTextBlock;
class UEditableText;
class UAsdaIconCellGrid;
class UAsdaButtonRelay;
class UAsdaJuiComboBox;

// ═══ ОДНА ЗАПИСЬ АССОРТИМЕНТА (`Bpremium.BIN` / `Fpremium.BIN`, шаг 80 б) ═══
//   Разбор формата и доказательства — в шапке `Tools/asda_import_booster_shop.py`.
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaBoosterShopRow : public FTableRowBase
{
	GENERATED_BODY()

	// "B" = `Bpremium.BIN` (весь ассортимент), "F" = `Fpremium.BIN` (вкладка «Популярные»).
	UPROPERTY(BlueprintReadOnly, Category="Asda|BoosterShop") FString Source;
	// Порядок В ВИТРИНЕ (у B — уже ПОСЛЕ эталонной сортировки по бейджу). Порядок значим:
	//   по нему считаются номер страницы и место в ней.
	UPROPERTY(BlueprintReadOnly, Category="Asda|BoosterShop") int32 Order = 0;
	/*+0x00*/ UPROPERTY(BlueprintReadOnly, Category="Asda|BoosterShop") int32 ItemCode = 0;
	// Имя из файла витрины. ★ Кодом отрисовки НЕ ЧИТАЕТСЯ (имя берётся из `item_list`), СПРАВКА.
	/*+0x04*/ UPROPERTY(BlueprintReadOnly, Category="Asda|BoosterShop") FString RawName;
	/*+0x44*/ UPROPERTY(BlueprintReadOnly, Category="Asda|BoosterShop") int32 ItemType = 0;
	/*+0x48*/ UPROPERTY(BlueprintReadOnly, Category="Asda|BoosterShop") int32 DiscountPct = 0;
	/*+0x4C*/ UPROPERTY(BlueprintReadOnly, Category="Asda|BoosterShop") int32 Badge = 0;
	// Сырая запись 80 б целиком (1:1, hex) — поля, которых код отрисовки не читает.
	UPROPERTY(BlueprintReadOnly, Category="Asda|BoosterShop") FString DataHex;
};

// ═══ [ПОРТ] 05.11.2026 ОДНА ВКЛАДКА ПОЛОСЫ 15545 (`DT_BoosterShopTabs`, редактор витрины `Tools/BoosterShopEditor`) ═══
//   В оригинале порядок и набор семи вкладок ЗАШИТЫ в коде (`BoosterShop_InitWindow`, подписи `ida.c:420398-420411`, фильтр
//   `itemGridRefreshPage` по индексу кнопки). Задача владельца 05.11: витрину и её категории правит инструмент, а не код ⇒
//   кнопка `Slot` показывает вкладку с эталонным смыслом `Filter`. Источник — ведомость `Tools/data/booster_shop_tabs.csv`
//   (импортёр `Tools/asda_import_booster_shop.py`). Строк нет / ключ `asda.PremiumMall.BoosterTabsFromTable 0` — как эталон.
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaBoosterShopTabRow : public FTableRowBase
{
	GENERATED_BODY()

	// Позиция кнопки на полосе 15545 слева направо (0…6): кнопки `BOOSTER_SHOP_MENU_OPT<Slot>`.
	UPROPERTY(BlueprintReadOnly, Category="Asda|BoosterShop") int32 Slot = 0;
	// Эталонный смысл вкладки: 0 Все · 1 Популярные (Fpremium) · 2 Воин · 3 Лучник · 4 Маг · 5 Доспех · 6 Инструмент.
	UPROPERTY(BlueprintReadOnly, Category="Asda|BoosterShop") int32 Filter = 0;
};

// ═══ ВИДЖЕТЫ ОДНОЙ ЯЧЕЙКИ СЕТКИ `BOOSTER_ITEM{nn}` ═══
USTRUCT()
struct FAsdaBoosterCellUi
{
	GENERATED_BODY()
	// ВСЁ поддерево ячейки по ПРЕФИКСУ имени (закон `jui-uneven-node-layout`): раскладка ячеек
	//   в `.JUI` неоднородна, точечные имена покрывают не все планы.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Parts;
	// Узлы, которые показываются ТОЛЬКО по условию (бейджи, плашка скидки, кнопка подарка).
	//   Гасятся ПОСЛЕ общего показа поддерева.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Conditional;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText  = nullptr;   // 15308 `_TITLE{nn}_Text`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PriceText = nullptr;   // 15309 `_PRICE{nn}`
	UPROPERTY(Transient) TObjectPtr<UImage>     ItemIcon  = nullptr;   // 15310 `_ICON{nn}_<n>_0`
	UPROPERTY(Transient) TObjectPtr<UButton>    BuyButton = nullptr;   // 15311 `_PUR{nn}`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BuyCaption= nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton>    GiftButton= nullptr;   // 15307 `_PRESENT{nn}`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> GiftCaption = nullptr;
	UPROPERTY(Transient) TObjectPtr<UWidget>    BadgeNew  = nullptr;   // 15313 — бейдж записи 2
	UPROPERTY(Transient) TObjectPtr<UWidget>    BadgeHot  = nullptr;   // 15314 — бейдж записи 1
	UPROPERTY(Transient) TObjectPtr<UWidget>    Stamp     = nullptr;   // клон 15560 «скидка»
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Digits;           // клоны 15561…15569
	UPROPERTY(Transient) int32 ItemCode = 0;
};

// ═══ ВАРИАНТ ПРЕДМЕТА ДЛЯ ПАНЕЛИ ПОКУПКИ (эталонный контейнер `this+690`, тройка) ═══
USTRUCT()
struct FAsdaBoosterVariant
{
	GENERATED_BODY()
	UPROPERTY(Transient) int32 ItemCode      = 0;
	UPROPERTY(Transient) int32 UsageCategory = 0;   // комбо-1 (15544)
	UPROPERTY(Transient) int32 RequiredLevel = 0;   // комбо-2 (15542)
};

UCLASS()
class ASDAREBORN_API UAsdaBoosterShopSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaBoosterShopSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа окна (все из `.JUI` и эталона) ──
	static constexpr int32 kWinIdNum   = 15300;
	static constexpr int32 kCells      = 21;   // максимум ячеек в сетке (3 × 7)
	static constexpr int32 kCellsTall  = 21;   // высота экрана ≥ 1024
	static constexpr int32 kCellsShort = 15;   // высота экрана  < 1024
	static constexpr int32 kCats       = 7;    // 15545
	static constexpr int32 kDigits     = 9;    // 15561…15569
	static constexpr int32 kMaxQty     = 7;    // комбо 15540: «1»…«7»
	// Окно лежит над инвентарём (720) и магазином снаряжения (732); панель покупки — его часть.
	static constexpr int32 kShopZ = 734;

	// ═══ ★ ПУБЛИЧНЫЙ ВХОД. Его зовёт кнопка NPC (код диалога 30034 у NPC 146). ═══
	//   Порт `uiStorageWindow_Open@0xB83B60`: ленивая загрузка → сброс → число ячеек по высоте
	//   экрана → пересчёт высоты подложки 15599 → обновление витрины → запрос баланса 5466.
	UFUNCTION(BlueprintCallable, Category="Asda|МагазинБустеров")
	void OpenWindow();

	// Порт `uiStorageWindow_Close` + `uiCloseItemComposeDialog@0xB83DB0` (крестик 15301).
	UFUNCTION(BlueprintCallable, Category="Asda|МагазинБустеров")
	void CloseWindow();

	// ★ 10.10.2026 [ОРИГИНАЛ] Роспись ESC окна 15300 (`RegisterEscCloser`; признак `CControl+0x228` ставит
	//   `BoosterShop_InitWindow@0xB7E4A0`, shop_trade.c:176). Эталон `boosterShopOnMessage@0xB876F0`, событие 0x15
	//   (shop_trade.c:1703-1706) + `uiItemComposeDialog_OnMessage@0xB87610`: открыто окно «Вид» 15600 → закрыть только его;
	//   иначе открыта панель покупки 15515 → закрыть только её; иначе закрыть магазин целиком.
	void EscClose();

	UFUNCTION(BlueprintPure, Category="Asda|МагазинБустеров")
	bool IsOpen() const { return bOpen; }

	// Панель покупки 15515 показана (`self[613]` ≠ 0 — режим 1 покупка / 2 подарок). ★ 10.10.2026 (barber_minors): для
	//   самотеста `asda.UI.SelfTestEscOnTop` — слой, который ESC снимает вторым (после окна «Вид» 15600).
	bool IsPurPanelShown() const { return PurMode != 0; }

	// ═══ ДЕЙСТВИЯ (ровно те же входы, что мышь) ═══
	void SetCategory(int32 Cat);           // 15545: Cat = эталонный смысл вкладки (фильтр 0…6)
	// [ПОРТ] 05.11.2026: клик по КНОПКЕ полосы 15545 номер `Slot` (вход мыши) → вкладка с фильтром из `DT_BoosterShopTabs`.
	void OnTabSlotClicked(int32 Slot);
	void SetPage(int32 NewPage1Based);     // 15501/15503/15505/15507
	// Клик по ИКОНКЕ карточки (15310 + 9i) — рамка выбора + окно предпросмотра 15600 «Вид».
	void OnCellIconClicked(int32 Cell);
	// Поставить окно «Вид» 15600 слева от магазина на уровне строки ячейки `Cell` (Д-23).
	void PlacePreviewBesideRow(int32 Cell);
	void OnCellBuyClicked(int32 Cell);     // 15311 + 9i — панель покупки, режим 1
	void OnCellGiftClicked(int32 Cell);    // 15307 + 9i — панель покупки, режим 2 (подарок)
	void OnCellClicked(int32 Cell);        // клик по иконке — подсветка выбора 15496
	void OnPurOkClicked();                 // 15527 → общая модалка 1559, режим F
	void OnPurCancelClicked();             // 15530
	void OnPurPreviewClicked();            // 15547 → окно 15600 (общая точка)
	void OnPurConfirmClicked();            // 15520 — проверка имени получателя 5072 (P101, `UAsdaCashGiftSubsystem`)
	void OnChargeClicked();                // 15513
	void OnBalanceClicked();               // 15514 → 5466
	void SetQuantity(int32 Qty);           // комбо 15540
	void SetVariantJob(int32 Idx);         // комбо 15544
	void SetVariantLevel(int32 Idx);       // комбо 15542
	void SetToWarehouse(bool bOn);         // чекбокс 15528

	// ★ ДИАГНОСТИКА БЕЗ СЕРВЕРА: подставить баланс GXP, который приносит 5467.
	void DebugSetGxp(int32 Value);
	// ── 25.10.2026: доступ самотеста `asda.PremiumMall.CartSelfTest` ([ИСПРАВЛЕНИЕ] «количество > 1» поштучно) ──
	// Перехват 5070: тело (171 б) кладётся в `DebugSent` вместо сокета, защёлка ставится как после настоящей отправки.
	void DebugSetCapture(bool bOn) { bDebugCapture = bOn; DebugSent.Reset(); }
	const TArray<TArray<uint8>>& DebugSentBodies() const { return DebugSent; }   // перехваченные тела 5070 по порядку
	// То, что делает «ОК» модалки 1559 после подтверждения: купить `Qty` штук кода `Code` (окно должно быть открыто).
	void DebugConfirmBuy(int32 Code, int32 Qty) { PurItemCode = Code; PurQty = Qty; SendBuy(); }
	void DebugFeedBuyResult(const TArray<uint8>& Body407) { OnBuyResult(Body407); }   // приём 5071 тем же обработчиком
	bool DebugIsPending() const { return bPending; }        // защёлка «запрос в полёте»
	bool DebugBuyRunning() const { return BuyRun.bRun; }    // идёт поштучная покупка
	void DebugSetOpenFlag(bool bOn) { bOpen = bOn; }        // признак «окно открыто» без окна (вызывающий вернёт false)
	// ── 01.11.2026 P101_CashGift: доступ самотеста `asda.SelfTest.CashGift` (клики — те же входы, что мышь) ──
	void DebugSetGiftName(const FString& Name);   // ввести имя получателя в поле 15519 (как набор с клавиатуры)
	FString DebugGiftGeometry() const;            // экранная геометрия кнопки «Подарок» ячейки 0, поля 15519 и кнопки 15520
	int32 DebugPurMode() const { return PurMode; }   // режим панели покупки (2 = подарок)
	// ── 05.11.2026: доступ самотеста `asda.PremiumMall.BoosterLayoutSelfTest` (витрина из данных редактора) ──
	// Число видимых вкладок полосы 15545 и фильтр кнопки `Slot` (−1 — кнопка скрыта).
	int32 DebugTabCount() const { return TabFilters.Num(); }
	int32 DebugTabFilter(int32 Slot) const { return TabFilters.IsValidIndex(Slot) ? TabFilters[Slot] : -1; }
	// Живое состояние кнопки `Slot` полосы: подпись, видимость кнопки/подписи/сегмента, живая геометрия кнопки (px).
	bool DebugTabState(int32 Slot, FString& OutCaption, bool& bOutShown, FVector2D& OutSize) const;
	int32 DebugCategory() const { return Cat; }      // текущий фильтр
	int32 DebugPage() const { return Page; }         // текущая страница (1-based)
	int32 DebugMaxPage() const { return MaxPage; }   // всего страниц
	int32 DebugCellsPerPage() const { return CellsPerPage(); }   // 21 либо 15 по высоте экрана
	// Карточка `Cell` текущей страницы: код, видимые имя и цена, видна ли (как её видит игрок).
	bool DebugCell(int32 Cell, int32& OutCode, FString& OutName, FString& OutPrice, bool& bOutShown) const;

	// ═══ ДИАГНОСТИКА (закон `full-structural-analysis-always`) ═══
	FString DumpControls() const;
	FString DumpState() const;
	FString SelfTest() const;

private:
	// ── Данные ──
	bool  LoadAssortment();                // `BoosterShop_LoadBpremium/Fpremium`
	const TArray<FAsdaBoosterShopRow>& ActiveSource() const;   // вкладка 1 → Fpremium, иначе Bpremium
	void  BuildPage();                     // `itemGridRefreshDispatch@0xB80FA0` + оба Refresh
	int32 CellsPerPage() const;            // ★ 21 либо 15 — см. шапку
	bool  PassesCategory(const FAsdaBoosterShopRow& Row) const;   // фильтр `itemGridRefreshPage`
	// [ПОРТ] 05.11.2026: раскладка полосы 15545 из `DT_BoosterShopTabs` (нет строк / ключ 0 — эталонные 0…6).
	void  LoadTabs();
	// Номер кнопки, на которой стоит фильтр `Filter` (−1 — вкладка снята с витрины).
	int32 TabSlotOfFilter(int32 Filter) const;
	// Подписи, видимость кнопок/сегментов и торцевая плашка полосы по `TabFilters`.
	void  ApplyTabLayout();

	// ── Жизненный цикл виджета ──
	bool EnsureWidget();
	void SetupControls();
	void SetupCells();
	void RegisterZone();
	void ApplyPanelHeight();               // `98 * (cells / 3) + 48` у 15599 + рамка 9-slice
	// Сколько карточек даст вкладка `Which` при текущих данных (для самотеста «путём игрока»).
	int32 CountForCategory(int32 Which) const;

	// ── Рендер ──
	void PopulateCell(int32 Cell, const FAsdaBoosterShopRow& Row);
	void ShowCell(int32 Cell, bool bVisible);
	void RefreshStatTexts();               // `uiRefreshStatTexts@0xB80E20`: 15498/15499/15511
	void RefreshTabState();

	// ── Панель покупки 15515 ──
	void PopulatePurPanel(int32 ItemCode, int32 DialogMode, bool bShow);  // `uiPopulateItemComposeDialog@0xB81E70`
	void RebuildLevelCombo();              // `uiList_OnCategorySelectFilterSub@0xB81B90`
	void CollectSelections();              // `formCollectInputAndSelections@0xB862E0`
	void RefreshPurPrice();                // 15522 = кол-во × цена; 15525 = GXP

	// ── Сеть ──
	void SendBuy();
	// [ИСПРАВЛЕНИЕ] 25.10.2026 «количество > 1» поштучно (`asda.PremiumMall.CartOnePerPacket`, шапка `AsdaPremiumBuyQueue.h`).
	bool SendOneBuy(const TArray<int32>& Codes);   // один пакет 5070 с этими кодами; true = ушёл, защёлка взведена
	void SendNextBuyItem();                        // следующая штука (из Tick, не из колбэка 5071)
	void StopBuyRun(const TCHAR* Why);             // прервать запуск (неотправленное не покупается)
	void OnBuyResult(const TArray<uint8>& Body407);
	void OnBalance(uint8 Status, int32 Balance);

	// ── Подсказка ──
	void TickTooltip(int32 OverCell);

	// ── Нотисы ──
	FString Loc(int32 Id) const;
	void NoticeChat(int32 LocId) const;
	void NoticeTip(int32 LocId) const;

	// ═══ СОСТОЯНИЕ ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TArray<FAsdaBoosterCellUi> Cells;
	UPROPERTY(Transient) TObjectPtr<UWidget>    Part2      = nullptr;   // 15599 подложка сетки
	// Девять планов рамки подложки 15599 (`Booster_Shop_Part2_11_0…8`): их высоту и положение
	//   двигает `ApplyPanelHeight` — сама группа рамку не тянет (иерархия .JUI расплющена).
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> PanelPlanes;
	float PanelHeightApplied = -1.f;    // последняя применённая высота подложки (px)
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose   = nullptr;   // 15301
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnNext    = nullptr;   // 15501 стр. +1
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnBack    = nullptr;   // 15503 стр. −1
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnLast    = nullptr;   // ★ 15505 `FIRSTP` — В КОНЕЦ
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnFirst   = nullptr;   // ★ 15507 `ENDP`   — В НАЧАЛО
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPage    = nullptr;   // 15498
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPages   = nullptr;   // 15499
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCashLbl = nullptr;   // 15510 (loc 12904)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCash    = nullptr;   // 15511
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnCharge  = nullptr;   // 15513
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnBalance = nullptr;   // 15514
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    CatButtons;     // 15545 OPT0…OPT6
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> CatLabels;
	UPROPERTY(Transient) TObjectPtr<UWidget>    CatPanel   = nullptr;   // корень 15545
	// [ПОРТ] 05.11.2026: сегменты полосы `BOOSTER_SHOP_MENU_332_<i>` (фон кнопки i) и торцевая плашка `…_RBg_333_0`.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> CatSegments;
	UPROPERTY(Transient) TObjectPtr<UWidget>    CatEndCap  = nullptr;
	FVector2D CatEndCapBase = FVector2D::ZeroVector;   // положение плашки в ассете (L,T) — при 7 вкладках не меняется
	float CatSlotStep = 0.f;                           // шаг кнопок полосы (OPT1.L − OPT0.L из ассета)
	// Фильтр каждой ВИДИМОЙ кнопки слева направо (`DT_BoosterShopTabs`); кнопки за концом массива скрыты.
	TArray<int32> TabFilters;
	bool bTabsLoaded = false;   // таблица вкладок прочитана (один раз за жизнь подсистемы, как ассортимент)
	// ★ 15496 `BOOSTER_ITEM_SEL` — рамка выбора. В ассете это НЕ группа, а ВОСЕМЬ отдельных
	//   картинок `BOOSTER_ITEM_SEL_<n>_0…_7` прямо в канвасе подложки (скин `Box_White3Stroke` —
	//   рамка 3×3 без центра). Двигаем их пачкой, сохранив взаимные смещения.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> SelFrame;
	TArray<FVector2D> SelFrameDelta;   // смещение каждой части относительно первой
	// Узлы, которые эталон ГАСИТ в Init: 40004/40005 «Деньги в подарок», чекбокс пола 15303/15304.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> HiddenByCode;

	// ── Панель покупки 15515 ──
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> PurTree;           // всё поддерево панели
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PurItemName = nullptr;  // 15538
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PurNarr    = nullptr;   // 15536
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PurWeight  = nullptr;   // 15537
	UPROPERTY(Transient) TObjectPtr<UImage>     PurIcon    = nullptr;   // 15533
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PurTxt1    = nullptr;   // 15518 (подарок)
	UPROPERTY(Transient) TObjectPtr<UEditableText> PurName = nullptr;   // 15519
	UPROPERTY(Transient) TObjectPtr<UWidget>    PurNameBase= nullptr;   // подложка поля
	UPROPERTY(Transient) TObjectPtr<UButton>    PurConfirm = nullptr;   // 15520
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PurTxt4    = nullptr;   // 15543 «Уровень:»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PurTxt5    = nullptr;   // 15541 «Профессия:»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PurTxt6    = nullptr;   // 15539 «Количество (шт.)»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PurPrice   = nullptr;   // 15522
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PurCash    = nullptr;   // 15525
	UPROPERTY(Transient) TObjectPtr<UCheckBox>  PurWareChk = nullptr;   // 15528
	UPROPERTY(Transient) TObjectPtr<UWidget>    PurWareGrp = nullptr;   // группа чекбокса (Д-15)
	UPROPERTY(Transient) TObjectPtr<UButton>    PurOk      = nullptr;   // 15527
	UPROPERTY(Transient) TObjectPtr<UButton>    PurCancel  = nullptr;   // 15530
	UPROPERTY(Transient) TObjectPtr<UButton>    PurPreview = nullptr;   // 15547
	// Д-19: значок «минус» (скин `WkBtn_IconMinus`) на кнопке покупки 15527 — гасим по ключу.
	UPROPERTY(Transient) TObjectPtr<UWidget>    PurOkIcon = nullptr;
	// Д-21: подпись кнопки подарка 15520 — гаснет вместе с самой кнопкой (иначе висит «ОК» без кнопки).
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PurConfirmCap = nullptr;
	// Д-20: исходные смещения рядов панели покупки для `AsdaUi::ShiftSlotYFromBase`.
	TMap<TWeakObjectPtr<UWidget>, FMargin> RowShiftBase;
	float RowStepY = 0.f;   // шаг рядов панели покупки (из ассета, меряется один раз)
	int32 PrevPlaceCellPending = -1;   // отложенная постановка окна «Вид» (ждём живую геометрию)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PurGuide0  = nullptr;   // 15590 (loc 6960)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PurGuide1  = nullptr;   // 15591 (loc 6961)
	UPROPERTY(Transient) TObjectPtr<UAsdaJuiComboBox> ComboJob   = nullptr;  // 15544
	UPROPERTY(Transient) TObjectPtr<UAsdaJuiComboBox> ComboLevel = nullptr;  // 15542
	UPROPERTY(Transient) TObjectPtr<UAsdaJuiComboBox> ComboQty   = nullptr;  // 15540
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> IconGrid  = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;
	// ★ 02.11.2026 P101: ограничитель LimitText 20 поля 15519 (GC-якорь).
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> GiftLimiters;

	// ── Модель ──
	UPROPERTY(Transient) TArray<FAsdaBoosterShopRow> RowsB;   // Bpremium (уже отсортирован)
	UPROPERTY(Transient) TArray<FAsdaBoosterShopRow> RowsF;   // Fpremium («Популярные»)
	UPROPERTY(Transient) TArray<FAsdaBoosterVariant> Variants;
	TArray<FLinearColor> CatIdleColors;
	TArray<int32> JobList;      // усл. значения комбо-1 в порядке показа
	TArray<int32> LevelList;    // усл. значения комбо-2 в порядке показа

	FAsdaMouseFrame Mouse;      // ОДИН опрос мыши на окно (закон общей точки)

	bool  bOpen = false;
	bool  bReady = false;
	bool  bLoaded = false;
	bool  bPending = false;     // ждём 5071 → нотис 1026
	double PendingSince = 0.0;  // когда ушёл 5070 (Д-18: снятие зависшей защёлки по таймауту)
	int32 Cat  = 0;             // 15545, эталонное поле `self[62]`
	int32 Page = 1;             // ★ 1-based, как `self[46]`
	int32 MaxPage = 1;          // `self[47]`
	int32 CashGxp = 0;          // `CCharacter+0x44C`
	int32 SelCell = -1;         // подсветка рамкой 15496
	int32 TipCell = -1;
	int32 PurMode = 0;          // 0 скрыта · 1 обычная покупка · 2 подарок (`self[613]`)
	int32 PurItemCode = 0;      // `self[611]` — код ВЫБРАННОГО ВАРИАНТА
	int32 PurBaseCode = 0;      // `self[612]` — код, по которому строился список вариантов
	int32 PurPriceOne = 0;      // `self[614]`
	int32 PurQty = 1;           // `self[609]`
	int32 PurJob = 0;           // `self[607]`
	int32 PurLevel = 0;         // `self[608]`
	bool  bPurToWarehouse = false;  // `self[610]` (инверсия байта 2 тела 5070)
	int32 BuiltForCells = 0;
	TArray<int32> PendingBuyCodes;
	FAsdaPremiumBuyQueue BuyRun;             // [ИСПРАВЛЕНИЕ] 25.10.2026: очередь поштучной покупки N штук
	bool bBuyRunToInventory = true;          // доставка запуска (снимок чекбокса 15528 на момент «ОК»)
	bool bDebugCapture = false;              // самотест: 5070 не уходит в сокет, тело пишется в DebugSent
	TArray<TArray<uint8>> DebugSent;         // самотест: перехваченные тела 5070
	FDelegateHandle HBuyResult;
	FDelegateHandle HBalance;
	int32 CntBuy = 0;
	int32 CntResult = 0;
};
