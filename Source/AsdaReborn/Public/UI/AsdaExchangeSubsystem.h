// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaExchangeSubsystem — АУКЦИОН: окно JUI `EXCHANGE_WIN` (IDNum **13000**), класс эталона
//   `CItemMediation`. Полный реверс со ссылками `файл:строка`/`@0x…` — `Reports/npc_exchange_win_13000.md`.
//
// ═══════════════════ ЧТО ЭТО ЗА ОКНО ══════════════════════════════════════════════════════════
//   Игрок выставляет вещи на продажу, другие игроки их ищут и покупают. Окно ОДНО (13000), внутри
//   ТРИ панели-ребёнка, из которых одновременно видна ровно одна:
//     • `EXCH_REGISTRATION` **13005** — выставление лота: сетка 60 ячеек инвентаря
//       (`EXCHANGE_SELL0…SELL60`, IDNum 13064…13123) + 5 строк лота (цена, срок, комиссия);
//     • `EXCH_MYITEM`      **13200** — мои лоты: 8 строк (иконка, имя, цена, остаток времени);
//     • `EXCHANGER_WIN`    **13400** — поиск и покупка: 8 строк выдачи, фильтры и пагинация.
//   `uiHideThreeControls@0xD435D0` (`ui_core.c:50679`) гасит все три перед каждым переключением.
//
// ═══════════════════ ★★★ ПЯТЬ КОДОВ ДИАЛОГА NPC — ЭТО РЕЖИМЫ, А НЕ ВКЛАДКИ ════════════════════
//   `game_main.c:10009-10076`, все пять — `itemMediationOpenTab(g_pItemShopWnd, N)` с гейтом
//   `sub_922402(...) != N`. `sub_922402@0x922402` — тунк на `get_o4C8@0xD88960`
//   (`AsdaStory_ida.c:599924`): `return *(this + 1224)` = байт `+0x4C8` = ТЕКУЩИЙ РЕЖИМ ОКНА.
//   Свитч `itemMediationOpenTab@0xD42E60` (`item_inventory.c:46772-46786`):
//     **30018** → 1 → выставить лот из ОСНОВНОГО инвентаря (60 слотов `g_InventoryArray2`);
//     **30017** → 2 → выставить лот из СУМКИ (30 слотов `g_InventoryBag`, спец-предметы);
//     **30019** → 3 → ★ НЕ ВКЛАДКА, а ДЕЙСТВИЕ: сразу шлёт запрос «мои лоты» (опкод **9907**);
//                     окно при этом НЕ показывается — гейт `if (a2 != 3)` (`item_inventory.c:46788`);
//     **30021** → 4 → поиск/покупка, набор категорий A;
//     **30028** → 5 → поиск/покупка, набор категорий B.
//
// ═══════════════════ ★★★ ИСПРАВЛЕННЫЕ МИСНЕЙМЫ ═══════════════════════════════════════════════
//   • **M1** `uiSatisfyWindowSwitchTab@0xD53D10` (`ui_core.c:51962-51979`) + `structs_ru.h`
//     (`pExchangerWin` = `+0x2CC` = dword 179, `pRadioBtn1` = 180, `pRadioBtn2` = 181):
//     режим **4** показывает `EXCHANGER_RADIOBTN**2**` (13409, `TabBar_W72_7Btn`, СЕМЬ кнопок),
//     режим **5** показывает `EXCHANGER_RADIOBTN**1**` (13408, `TabBar_W72_8Btn`, ВОСЕМЬ).
//     Нумерация имён контролов ПРОТИВОПОЛОЖНА нумерации режимов.
//   • **M2** `Docs/Port/systems/NUMPAD_FLOW.md`, режим нумпада 5 подписан «Магазин предметов: применить
//     слот». На деле это ЭТО окно — аукцион: `g_pItemShopWnd` и есть объект `CItemMediation`.
//   • **M3** `itemMediationSendRegisterRequest@0xD3C7D0` (опкод **9909**) НИЧЕГО НЕ РЕГИСТРИРУЕТ —
//     это СНЯТЬ/ЗАБРАТЬ отмеченные лоты (единственный вызов — кнопка `EXCHMY_DEL` 13251,
//     `itemMediationMsgRouter@0xD3D7C0`, `item_inventory.c:46467`).
//   • **M4** `NetOpcodes.h`: 9902/9904/9913/9914 подписаны `npcShop_DispatchResult`, 9915 —
//     «~npc Shop Dispatch Result». К магазину NPC отношения не имеют: `npcShop_DispatchResult@0xD43270`
//     (`npc_mob_pet.c:511`) — ДИСПЕТЧЕР РЕЗУЛЬТАТОВ АУКЦИОНА.
//   • **M5** `boosterShopWnd_OnItemListPacket@0xD3A220` (`shop_trade.c:12845`) — не бустер-магазин,
//     а приём СПИСКА ЛОТОВ выдачи поиска (ветка `a3 == 3` диспетчера).
//   • ★ Номера контейнеров ЗЕРКАЛЬНЫ номерам режимов: режим **1** читает контейнер **2**
//     (`SpendBag`, 60 ячеек), режим **2** — контейнер **1** (`ShellBag`, 30 ячеек).
//     Доказано `invFilterUsableItems@0xD45520` (`item_inventory.c:47346`) и гейтами индекса
//     в `itemSlotWnd_OnRightClickSlot@0xD4C8E0` (`< 0x3C` против `< 0x1E`).
//   • ★ Нумерация ЗАКРЫТИЯ не совпадает с нумерацией открытия:
//     `itemMediationCancelByType@0xD43100` (`item_inventory.c:46818`) — `1` закрывает выставление,
//     `2` закрывает «мои лоты», `3` закрывает поиск.
//
// ═══════════════════ ГЕЙТЫ ОТКРЫТИЯ (`item_inventory.c:46760-46807`) ══════════════════════════
//   1. `sub_93248D(g_pInventoryWnd)` → `get_o84_7520@0xC27520` (dword `+0x210` окна инвентаря)
//      ≠ 0 ⇒ МОЛЧАЛИВЫЙ отказ. 2. `g_bPartyInviteFlag` ⇒ МОЛЧАЛИВЫЙ отказ.
//   3. ★ Уже активен ДРУГОЙ режим (и это не режим 3) ⇒ нотис **12555** ОДНОВРЕМЕННО в чат
//      и во всплывающую подсказку.
//   ★ При входе в режим 4/5 эталон САМ имитирует клик 13424 (`EXCHANGER_RESCAN`) —
//   `uiItemGridOnCommand(this, 13424, 0,0,0)` (`item_inventory.c:46797`) ⇒ сразу уходит поиск 9903
//   со сброшенной в 0 страницей.
//
// ═══════════════════ КОМИССИЯ (из подписей JUI `EXCH_ITEM_Msg01/02`, 13300/13301) ═════════════
//   `거래 성사되면 판매금액 []%의 수수료가 있습니다` — комиссия N % от суммы продажи;
//   `5일은 5%, 3일은 3%, 1일은 수수료 공짜` — **срок 5 дней → 5 %, 3 дня → 3 %, 1 день → 0 %**.
//
// ═══════════════════ ★ НУМПАД (окно 1410), РЕЖИМ 5 — РОВНО ДВЕ ПЛОЩАДКИ ══════════════════════
//   `itemSlotWnd_OnRightClickSlot@0xD4C8E0` (`item_inventory.c:48860` и `:48990`), гейт по IDNum
//   `a2 >= 0x3308 && a2 <= 0x3343` = ячейки 13064…13123 панели выставления:
//   `uiOpenContextPopupWithHandlers(g_pNumPadDialog, <размер стека>, /*режим*/5, 0)`.
//   Правила: `flagWord176 != 0` ⇒ ВЕСЬ стек без вопроса; `tradableFlag == 0` ⇒ отказ;
//   `tradableFlag == 1` и стек `== 1` ⇒ весь стек; стек `> 1` ⇒ НУМПАД.
//   ★ На ЦЕНУ нумпад НЕ вызывается: цена — поле `EXCH_EDIT_PRICE0N` (`Black_EditBox`,
//   `consignRegWnd_OnAmountEdit@0xD4E330`) и стрелки 13020/13021.
//   В порте роль «режима» играет колбэк `UAsdaWindowSubsystem::ShowNumPad`.
//
// ═══════════════════ СЕТЬ ═════════════════════════════════════════════════════════════════════
//   C→S: **9915** открыть сессию (`mediationSendOpen@0xD44370`); **9901** выставить
//   (`mediationSendRegister@0xD4F0F0`, `EXCH_OK` 13061); **9903** поиск/страница
//   (`mediationSendSearch@0xD55BB0`, `EXCHANGER_RESCAN` 13424 и все 4 кнопки пагинации);
//   **9905** купить (`mediationSendBuy@0xD56990`); **9907** запросить мои лоты
//   (`itemMediation_sendConfirm@0xD3BED0`); **9909** снять лоты (`itemMediationSendRegisterRequest`).
//   Шапка payload — 15 байт: `+5 i32 userIdx` (★ userIdx, а НЕ charId), `+9 i32 sessionId`, `+13 = 0`.
//   Запись покупки — 45 б (`0x2D`): `+0 serial`, `+4 itemCode`, `+8 field1`, `+12 amount`,
//   `+13 durability`, `+24…+43` пять гнёзд самоцветов. Пустые поля инициализируются **−1**, НЕ нулём
//   (`sub_D3C720@0xD3C720`; закон `helper_initInvalidIds@0x973D30`).
//   S→C: диспетчер `npcShop_DispatchResult@0xD43270` по `a3`: 1/2 результат выставления,
//   3 список лотов выдачи, 4/5 результат по слотам, 6 результат покупки/продажи, 7 рассинхрон.
//
// ═══════════════════ ★ ПАГИНАЦИЯ — ТЕРМИНАТОРА НЕТ ═══════════════════════════════════════════
//   `uiTabNavigate` (`ui_core.c:53273-53320`), поля `+2318` текущая / `+2319` последняя страница.
//   «вперёд» на последней странице запроса НЕ шлёт (`if (+2318 == +2319) v3 = 0`), «назад» на
//   нулевой — тоже. Общее число страниц ПРИСЫЛАЕТ СЕРВЕР. Отдельного пакета-терминатора
//   «страница кончилась» (как опкод 4052 у склада) у аукциона НЕТ.
//
// ═══════════════════ ЗВУКОВ У ОКНА НЕТ ════════════════════════════════════════════════════════
//   Скан всех функций диапазона класса `0xD39000…0xD5E000` регуляркой
//   `PlaySound|Sound_Play|CSound|PlaySfx|SoundMgr|Sound_|_Sound` даёт 0 совпадений; второй метод —
//   автоматическая таблица нотисов по тому же диапазону. Единственные звуки в потоке — СВОИ звуки
//   нумпада (открытие 5, закрытие 6). Единственная анимация — `Exchange_Gaer` (13001) ↔
//   `Exchange_AniGaer` (13002).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaInventorySubsystem.h"   // FAsdaItemInstance — запись предмета 57 байт
#include "UI/AsdaScrollBarNomal.h"       // ★ ОБЩАЯ ТОЧКА полосы прокрутки скина `Nomal_Scr`
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaExchangeSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UCheckBox;
class UTextBlock;
class UEditableText;
class UAsdaButtonRelay;
class UAsdaIconCellGrid;
class UAsdaJuiComboBox;

// Режим окна = байт `+0x4C8` эталона (`get_o4C8@0xD88960`). Значения — ровно те, что передают
//   пять кодов диалога NPC.
UENUM(BlueprintType)
enum class EAsdaExchangeMode : uint8
{
	None            = 0 UMETA(DisplayName = "0 — закрыто"),
	RegisterSpend   = 1 UMETA(DisplayName = "1 — выставить из инвентаря (код 30018, контейнер 2)"),
	RegisterShell   = 2 UMETA(DisplayName = "2 — выставить из сумки (код 30017, контейнер 1)"),
	MyLots          = 3 UMETA(DisplayName = "3 — запросить мои лоты (код 30019, пакет 9907)"),
	BrowseA         = 4 UMETA(DisplayName = "4 — поиск, набор категорий A (код 30021)"),
	BrowseB         = 5 UMETA(DisplayName = "5 — поиск, набор категорий B (код 30028)"),
};

// Одна из пяти строк выставляемого лота (`EXCH_ITEMBASE01…05`).
USTRUCT()
struct FAsdaExchangeRegRow
{
	GENERATED_BODY()

	// Откуда взят предмет: контейнер `EAsdaItemContainer` и номер ячейки.
	UPROPERTY() int32 SrcContainer = 0;
	UPROPERTY() int32 SrcCell      = -1;
	// Копия записи предмета; `Amount` уже урезан до выбранного нумпадом количества.
	UPROPERTY() FAsdaItemInstance Item;
	// Цена за лот — поле `EXCH_EDIT_PRICE0N`.
	UPROPERTY() int64 Price = 0;
	// Срок в днях — комбобокс `EXCH_DUE0N`. Допустимые значения — 1, 3, 5 (см. подпись 13301).
	UPROPERTY() int32 DueDays = 1;
	// Галочка `EXCH_ITEMCHK0N` — по ней работает `EXCH_SELDEL` 13058.
	UPROPERTY() bool bChecked = false;

	bool IsEmpty() const { return Item.IsEmpty(); }
	void Clear() { SrcContainer = 0; SrcCell = -1; Item.Clear(); Price = 0; DueDays = 1; bChecked = false; }
};

// Одна строка «моих лотов» (`EXCHMY_ITEM01…08`).
USTRUCT()
struct FAsdaExchangeMyLot
{
	GENERATED_BODY()

	UPROPERTY() int32 Serial    = -1;   // серийный номер лота на сервере
	UPROPERTY() int32 ItemCode  = 0;
	UPROPERTY() int32 Amount    = 0;
	UPROPERTY() int64 Price     = 0;    // `EXCHMY_PRICE0N`
	// ★★★ ОСТАТОК ТОРГОВ — В МИЛЛИСЕКУНДАХ, а не в секундах (миснейм порта снят 2026-08-27).
	//   `tagBoosterShop_FormatSlotRemainTime@0xD3E620` (`shop_trade.c:13519-13520`):
	//   `hours = a3 / 3600000; remainMs = a3 % 3600000; minutes = remainMs / 60000`.
	//   Источник — i32 по смещению **+54** записи 9908 (`item_inventory.c:46053-46054`:
	//   четыре i16 записи +54/+56/+58/+60 кладутся в `storageWnd[19*n+1814]`, а формат берёт
	//   ИМЕННО этот dword — `shop_trade.c:13395`). Раньше порт его вообще не читал: колонка
	//   `EXCHMY_TIME0N` всегда показывала «0 ч 00 мин».
	UPROPERTY() int32 RemainMs = 0;    // `EXCHMY_TIME0N`
	UPROPERTY() bool  bChecked  = false;// `EXCHMY_CHK0N`
	UPROPERTY() FAsdaItemInstance Item;
	// ★★★ ДОБАВЛЕНО 2026-09-02. Байты +25 и +26 ЗАПИСИ ПРЕДМЕТА строки «моих лотов». Смысл в
	//   эталоне не выведен (у +24 это заточка, у +27 — процент заточки, а эти два — «другая
	//   пара»), поэтому имена нейтральные, по смещению, как у `Field46`.
	//   Приходят в 9908 байтами **+16** и **+15** соответственно (`item_inventory.c:46048-46049`:
	//   `BYTE1(storageWnd[1816]) = itemRecords[4]+62*i` = байт +16 → запись +25,
	//    `BYTE2(storageWnd[1816]) = itemRecords[3]+62*i+3` = байт +15 → запись +26)
	//   и обязаны уехать обратно в запись 9909 байтами **+15** и **+14**
	//   (`item_inventory.c:46344-46345`). Без них забор лота уходил с нулями.
	UPROPERTY() int32 ItemRec25 = 0;
	UPROPERTY() int32 ItemRec26 = 0;
	// ★★★ ДОБАВЛЕНО 2026-09-02. Пять пар заточки/зачарования (i16 код + i16 значение, 5×i32) —
	//   байты `+25…+44` записи 9908 (`item_inventory.c:46051-46052`, ложатся в запись предмета
	//   с +29). Уходят в байты `+24…+43` записи 9909 (`item_inventory.c:46347-46348`).
	//   Хранятся сырыми dword'ами, как их читает и пишет эталон, — ничего не пересобираем.
	UPROPERTY() TArray<int32> EnchantOpts;
	// ★★★ ПОЛЕ **+46** ЗАПИСИ 9908 — ПРИЗНАК, ПО КОТОРОМУ АУКЦИОН БЛОКИРУЕТСЯ.
	//   Эталон кладёт его в `storageWnd[19*n + 1826]` (`item_inventory.c:46056`:
	//   `*(_DWORD *)((char *)&itemRecords[11] + 62*i + 2)` = 44 + 2 = байт **+46** записи),
	//   а читает ровно один пересчёт — `playerInv_CountSowelEquipped@0xD3F1D0`
	//   (`entity_char.c:45742-45766`): лот считается «висящим», если код предмета нашёлся хотя бы
	//   в одной из трёх таблиц И это поле РАВНО НУЛЮ. Смысл самого нуля в эталоне не выведен,
	//   поэтому имя поля нейтральное — храним значение как есть, ничего не домысливая.
	UPROPERTY() int32 Field46 = 0;

	bool IsEmpty() const { return Serial < 0 && ItemCode <= 0; }
};

// Одна строка выдачи поиска (`EXCHANGER_ITEM01…08`).
USTRUCT()
struct FAsdaExchangeListing
{
	GENERATED_BODY()

	UPROPERTY() int32   Serial   = -1;
	UPROPERTY() int32   ItemCode = 0;
	UPROPERTY() int32   Amount   = 0;
	UPROPERTY() int64   Price    = 0;   // `EXCHANGER_ITEMPRI0N`
	UPROPERTY() FString Seller;         // `EXCHANGER_ITEMSEL0N`
	UPROPERTY() bool    bChecked = false;
	// ★ Запись предмета лота. Из пакета 9904 заполняются ЧЕТЫРЕ поля, и все четыре уходят
	//   в запись покупки 9905: `ItemCode` (+5 → 9905 +4), `Amount` (+9 → 9905 +8),
	//   `Durability` (+13 → 9905 +12), `GemGrade` (+14 → 9905 +13). Раскладку и доказательства
	//   см. в шапке `SendBuyChecked`.
	UPROPERTY() FAsdaItemInstance Item;
	// ★★★ ИСПРАВЛЕН МИСНЕЙМ (2026-09-02): поле называлось `Sowels` («гнёзда самоцветов»), и имя
	//   ВРАЛО. Это байты `+25…+44` записи 9904, а приёмник эталона
	//   `netOnItemSlotListResult@0xD5BAB0` (`network.c:21575-21576`) кладёт их в
	//   `запись предмета +29` шагом 4 — то есть это ПЯТЬ ПАР ЗАТОЧКИ/ЗАЧАРОВАНИЯ
	//   (i16 код + i16 значение), см. `FAsdaItemInstance::EnchantCode/EnchantValue`.
	//   Настоящие ЧЕТЫРЕ гнезда самоцветов (`запись предмета +16/+18/+20/+22`) приходят другими
	//   байтами — `+72…+79` записи 9904 (`network.c:21577-21578`), и ни эталон, ни порт их
	//   в запись покупки 9905 не кладут.
	//   Значения ОТПРАВЛЯЮТСЯ как есть в байты `+24…+43` записи 9905 (эталон
	//   `shop_trade.c:15085-15086`), поэтому содержимое поля менять нельзя — только имя.
	UPROPERTY() TArray<int32> EnchantOpts;

	bool IsEmpty() const { return Serial < 0 && ItemCode <= 0; }
};

// Результат классификации одной строки лота — эталонные локальные переменные `mediationSendRegister@0xD4F0F0`.
//   ★ U6 (В1 дедупа №2): перенесена из .cpp — её принимает `UAsdaExchangeSubsystem::BuildRegisterRow9901`.
struct FAsdaExchRegClass
{
	bool  bValid = false;    // `hasItem`   — ветка классификации сработала, запись отправляется
	bool  bError = false;    // `errorFlag` — справочник вернул −1 ⇒ эталон отменяет ВСЮ отправку
	int32 ItemType      = 0; // → байт +50 записи 9901
	int32 SubType       = 0; // → байт +51
	int32 LevelTier     = 0; // → байт +53
	int32 CategoryIndex = 0; // → байт +55
};

UCLASS()
class ASDAREBORN_API UAsdaExchangeSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()
	friend struct FAsdaTradeGatesSelfTest;   // ★ 29.09.2026: самотест `asda.Trade.SelfTestGates` зовёт `PassesOpenGates`
	friend struct FAsdaInvRelogSelfTest;     // ★ 25.10.2026: самотест перезахода `asda.SelfTest.InvRelog` — строки выставления/лоты/выдача
	friend struct FAsdaMarketSelfTest;       // ★ 09.11.2026 (пачка 20, рынок): самотест `asda.Exchange.MarketSelfTest` — десять жалоб владельца

public:
	// ★★★ ПАЧКА 20 (09.11.2026), жалоба 10: «пока открыт рынок, открываются склад и личная лавка, приходят заявки обмена».
	//   [ИСПРАВЛЕНИЕ] Общая точка для ЧУЖИХ гейтов (склад 4000, лавка 22000/23000, обмен 2008): окно аукциона 13000 на экране.
	//   В оригинале таких гейтов нет (склад/лавка/обмен про окно 13000 не знают) — владелец назвал это дефектом.
	//   Ключ `asda.Exchange.ExclusiveWindows` (1 по умолчанию; 0 — как раньше: всегда false). Контекст — мир/GI/подсистема.
	static bool IsMarketWindowOpen(const UObject* Ctx);
	// ★ Пачка 20: признак ключа «окна взаимоисключают рынок» — для обратного гейта (рынок не открывается поверх склада/лавки).
	static bool ExclusiveWindowsOn();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Окно, его защёлка «запрос в полёте» и полученные от сервера данные
	// принадлежат СЕССИИ и переживали разрыв связи вместе с GameInstance.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaExchangeSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return !IsTemplate(); }

	// ★ ЕДИНАЯ ТОЧКА ВХОДА для пяти кодов диалога NPC. Повторяет `game_main.c:10009-10076`
	//   ВМЕСТЕ с гейтом `sub_922402(...) != Mode` — вызывать можно безусловно.
	//   Возвращает true, если режим был переключён (эталонный `itemMediationOpenTab` вернул 1).
	//     30018 → OpenMode(1)   30017 → OpenMode(2)   30019 → OpenMode(3)
	//     30021 → OpenMode(4)   30028 → OpenMode(5)
	UFUNCTION(BlueprintCallable, Category = "Asda|Exchange")
	bool OpenMode(int32 Mode);

	// Закрытие по эталонной нумерации `itemMediationCancelByType@0xD43100`:
	//   1 — панель выставления, 2 — «мои лоты», 3 — панель поиска.
	UFUNCTION(BlueprintCallable, Category = "Asda|Exchange")
	bool CancelByType(int32 CancelType);

	// Полное закрытие окна (крестик 13004 через `objClassifyType123`).
	UFUNCTION(BlueprintCallable, Category = "Asda|Exchange")
	void CloseAll();

	UFUNCTION(BlueprintPure, Category = "Asda|Exchange")
	EAsdaExchangeMode GetMode() const { return Mode; }

	UFUNCTION(BlueprintPure, Category = "Asda|Exchange")
	bool IsOpen() const { return bOpen; }

	// ★ ДВА РАЗНЫХ СПИСАНИЯ, не путать (§9.3 отчёта):
	//   1) КОМИССИЯ ЗА ВЫСТАВЛЕНИЕ — ровно 1 % от цены, списывается сразу
	//      (`sub_521920@0x521920`, `GameServer.c:109820`: `fee = (int)(price * 0.01f)`).
	//      Это и есть `중개료` контролов 13014 / 13055 / 13056.
	UFUNCTION(BlueprintPure, Category = "Asda|Exchange")
	static int64 ListingFee(int64 Price) { return (int64)((double)Price * 0.01); }

	//   2) НАЛОГ С ПРОДАЖИ — по КОДУ СРОКА (`recvRelayServerTransActionItemBuy@0x5211B0`):
	//      0x18 = 24 ч (1 день) → 0 %, 0x48 = 72 ч (3 дня) → 3 %, 0x78 = 120 ч (5 дней) → 5 %,
	//      0xA8 = 168 ч (7 дней) → 7 %. ★ Четвёртый срок в UI оригинала отсутствует (дефект D8).
	UFUNCTION(BlueprintPure, Category = "Asda|Exchange")
	static int32 SaleTaxPercentForDurationCode(int32 DurationCode);

	// Дни → код срока (24 / 72 / 120 / 168 часов). Всё прочее приводится к 24 ч, как дефолт эталона.
	UFUNCTION(BlueprintPure, Category = "Asda|Exchange")
	static int32 DurationCodeForDays(int32 Days);

	// ★ U6 (В1 дедупа №2): ЗАПИСИ ПАКЕТОВ БИРЖИ — сборка отдельно от отправки (раскладки и эталоны — у определений;
	//   самотест `asda.Dedup.W1ASelfTest` сверяет их побайтно с прежней ручной сборкой).
	// Запись выставления 9901 (98 б) из строки окна, её классификации и имени своего персонажа.
	static TArray<uint8> BuildRegisterRow9901(const FAsdaExchangeRegRow& R, const FAsdaExchRegClass& Cls, const FString& SelfName);
	// Запись забора 9909 (45 б) из строки «моих лотов».
	static TArray<uint8> BuildDeleteRow9909(const FAsdaExchangeMyLot& L);
	// Запись покупки 9905 (45 б) из строки выдачи поиска.
	static TArray<uint8> BuildBuyRow9905(const FAsdaExchangeListing& L);

	// Итоговая комиссия ЗА ВЫСТАВЛЕНИЕ по всем непустым строкам (`EXCH_TOTALFEE` 13056).
	UFUNCTION(BlueprintPure, Category = "Asda|Exchange")
	int64 GetTotalFee() const;

	// ★ 29.09.2026 (волна 0 торговли): [ОРИГИНАЛ] `uiHandleItemSaleResult@0xD436C0` (ui_core.c:50738-50746) — золото из
	//   9913 «твой лот купили» при ОТКРЫТОМ окне обмена 2008 (`CExchange+0x2C`) НЕ применяется, а откладывается
	//   (`sub_92104D(g_pItemShopWnd, золото)`); вывод — при закрытии обмена `uiInventoryDialogReset` (ui_core.c:35240-35248:
	//   `if (sub_92282B(...) > 0) { uiSetHonorPointsDisplay(...); sub_92104D(..., 0); }`) и по итогу обмена (network.c:14700-14730).
	//   Зовёт будущий владелец окна 2008 (волны О2/О4 плана торговли). Возврат: было ли что выводить.
	bool FlushDeferredSaleGold(const TCHAR* Why);
	// Отложенное золото продажи (0 = нет) — для самотеста и логов.
	int32 GetDeferredSaleGold() const { return DeferredSaleGold; }
	// ★ 30.09.2026 (волна О4 торговли): `sub_92104D(g_pItemShopWnd, 0)` БЕЗ вывода — итог обмена 5294/5295 сам решает, какую сумму
	//   показать (network.c:14700-14730, 14942-14954), и лишь обнуляет отложенное. Возврат: было ли что обнулять.
	bool ResetDeferredSaleGold(const TCHAR* Why);

private:
	// ── жизненный цикл окна ───────────────────────────────────────────────────────────────────
	void EnsureWidget();          // ленивое создание/поиск WBP, аналог `CItemMediation_BuildUI`
	void SetupControls();         // связывание по idc (аналог 84 `FindControlByID`)
	void RegisterZone();
	void ResetBindings();
	void ShowPanel(EAsdaExchangeMode NewMode);   // аналог `uiHideThreeControls` + показ нужной
	void CollectByIdc(const FString& Idc, TArray<TObjectPtr<UWidget>>& Out) const;
	// ★ Набор категорий (`RadioButton`): контрол + его кнопки `_OPT<i>` + подписи + планы ОДНОГО
	//   объявления. `EXCHANGER_RADIOBTN2` в данных объявлен ДВАЖДЫ — лишний набор планов отсекается
	//   по числу кнопок.
	void CollectRadioSet(const FString& Idc, int32 ExpectedButtons, TArray<TObjectPtr<UWidget>>& Out) const;
	// ★ Подписи вкладок набора: тексты из нотисов + подсветка активной (общая точка
	//   `AsdaUi::ApplyTabCaptionState` с кнопками — цвета берутся из КАДРОВ СКИНА).
	void RefreshCategoryCaptions();
	// ★★★ Статические подписи ТРЁХ панелей — из ТАБЛИЦЫ СТРОК (12500…12519, 2289, 5035, 12722),
	//   ровно как их ставит оригинал в `invForgeDialogInit@0xD450B0` / `uiInitDialogControls` /
	//   `shop_trade.c:12809`. Корейский `EXS TextContent` в `.JUI` — заготовка дизайнера.
	void InitStaticCaptions();
	// ★ Остаток торгов «ч + мин» из нотисов 12535/12536/12697/12698 (вход — МИЛЛИСЕКУНДЫ).
	FString FormatRemainTime(int32 RemainMs) const;

public:
	// ★★★ ЖИВАЯ ДИАГНОСТИКА (консоль). Мышью в headless-PIE не кликнуть, а офлайн-дамп ассета
	//   показывает только разметку — реально ли связались контролы, какая панель показана и где
	//   лежат клик-зоны, видно только в запущенном мире (закон `false-negatives-method`).
	//     `asda.Exchange.Open <1..5>` — открыть режим;
	//     `asda.Exchange.Widgets`     — дамп живого состояния окна.
	void DumpLive() const;
	FString DumpNodeGeom(const TCHAR* Name) const;
	// ★ Экранная геометрия вкладок — проверка ПУТЁМ ИГРОКА (`-game`), а не PIE.
	void DumpTabsOnScreen() const;
	// Живые проверки: `asda.Exchange.Cat <4|5|3> <индекс>` и `asda.Exchange.FakeLot <код> [цена]`.
	void DebugPickCategory(int32 Set, int32 Index);
	void DebugFeedFakeLot(int32 ItemCode, int32 Price);
	// ★ Уровень, каким его видит гейт «24» — для строки самотеста (0 = «неизвестен»).
	int32 DebugSelfLevel() const { return SelfLevel(); }

private:
	static void SetGroupVisible(const TArray<TObjectPtr<UWidget>>& Group, bool bVisible);

	// ── гейты открытия ────────────────────────────────────────────────────────────────────────
	bool PassesOpenGates(int32 NewMode) const;
	// ★★★ ЕДИНСТВЕННАЯ ТОЧКА ГЕЙТА УРОВНЯ «24» для режимов 1, 2 и 3 (см. `.cpp`). Возвращает
	//   true, когда уровень ЗАВЕДОМО известен и меньше 24: окно открывать нельзя, показывается
	//   только модалка 97101. Модалку НЕ показывает — это делает вызывающий, одним местом.
	bool LevelGateBlocks(int32 NewMode) const;

	// ★★★ ПЕРЕСЧЁТ ФЛАГА «АУКЦИОН ЗАБЛОКИРОВАН» — 1:1 связка эталона
	//   `playerInv_CountSowelEquipped@0xD3F1D0` (`entity_char.c:45742`) +
	//   `itemMediation_onConfirmEvent@0xD3A0A0` (`item_inventory.c:45896-45908`):
	//     `if (playerInv_CountSowelEquipped(this)) *(this+305) = 1; else *(this+305) = 0;`
	//   (305·4 = 0x4C4 = `CItemMediation::curCategory` — тот самый флаг).
	//   ПРИСВАИВАЕТ, а не «поднимает»: оригинал умеет флаг и СНИМАТЬ. Зовётся ровно там же, где
	//   эталон, — на входе в режим 3 и на его закрытии (`itemMediationOpenTab(…,3)` и
	//   `itemMediationCancelByType(…,2)`).
	void RecomputeAuctionBlocked();

	// ── панель выставления ────────────────────────────────────────────────────────────────────
	int32 SourceContainerForMode(EAsdaExchangeMode M) const;  // ★ 1→2, 2→1 (зеркально)
	int32 SellCellCode(int32 GridIndex) const;                // код предмета ячейки сетки
	int32 SelfLevel() const;                                  // уровень игрока (гейт «24»)
	void  RebuildSellGrid();                                  // аналог `invFilterUsableItems@0xD45520`
	bool  PassesRegisterFilter(const FAsdaItemInstance& It, int32 Container) const;
	void  OnSellCellClicked(int32 GridIndex);                 // включает нумпад режима 5
	void  AddToRegRow(const FAsdaItemInstance& Src, int32 Container, int32 Cell, int32 Amount);
	void  DropCheckedRegRows();                               // `EXCH_SELDEL` 13058
	void  RefreshRegRows();
	void  RefreshRegTotals();
	// ★★★ ЦЕНА ИЗ ПОЛЯ ВВОДА — порт `consignRegWnd_OnAmountEdit@0xD4E330`
	//   (`item_inventory.c:49040-49105`). Эталон на КАЖДОЕ событие поля: `atoi` текста, потолок
	//   100 000 000, запись в `wnd2[20*row + 330]` (это и есть `rec+71` → цена пакета 9901 на +57)
	//   и `ItemListWnd_RefreshRows` — пересчёт колонки комиссии и итога.
	//   ⚠ ДО 2026-08-28 порт читал поле ТОЛЬКО внутри `RefreshRegRows`, а её никто не звал при
	//   наборе текста: комиссия не пересчитывалась, а `SendRegister` видел цену 0 и печатал
	//   нотис 12699 «указана цена '0'» (жалоба владельца №6).
	void  ApplyPriceFromEdit(int32 Row, bool bNormalizeText);
	// Нотисы С АРГУМЕНТАМИ (порт заполнения слотов `g_pLocNoticeCtx` перед вызовом форматтера).
	void  NoticeChatFmt(int32 LocId, const TArray<FString>& StrArgs, const TArray<int32>& NumArgs) const;
	void  NoticeChatBFmt(int32 LocId, const TArray<FString>& StrArgs) const;
	FString ItemNameByCode(int32 ItemCode) const;
	class UAsdaItemTooltipSubsystem* TooltipSys() const;
	// Приёмники событий полей цены. Движковые делегаты `UEditableText` не сообщают отправителя,
	//   поэтому обходим все пять строк — это дешевле, чем заводить по виджету-обёртке на строку.
	UFUNCTION() void OnPriceTextChanged(const FText& Text);
	UFUNCTION() void OnPriceTextCommitted(const FText& Text, ETextCommit::Type Method);
	void  SendRegister();                                     // `EXCH_OK` 13061 → 9901
	// Потолок цены эталона (`item_inventory.c:49085`: `if (amount >= 100000000.0) amount = 1e8`).
	static constexpr int64 kPriceMax = 100000000;

	// ── панель «мои лоты» ─────────────────────────────────────────────────────────────────────
	void RequestMyLots();                                     // режим 3 → 9907
	void RefreshMyLots();
	void SendTakeCheckedLots();                               // `EXCHMY_DEL` 13251 → 9909

	// ── панель поиска ─────────────────────────────────────────────────────────────────────────
	void SendSearch();                                        // 9903 с текущей страницей и фильтрами
	void RefreshListings();
	void RefreshPager();
	void PageNavigate(int32 Dir, int32 Jump);                 // повтор `uiTabNavigate`
	void SendBuyChecked();                                    // `EXCHANGER_BUY` 13423 → 9905

	// ── ★★★ P1: ТРИ СПИСКА КРИТЕРИЕВ 13410 / 13411 / 13412 ────────────────────────────────────
	//   Полный порт четырёх эталонных функций (адреса — у реализации):
	//     `uiItemDlg_fillAttributeLists@0xD59C80`   — ЧЕМ наполнены три списка при данной категории;
	//     `render_SetTexCoordSignFlags@0xD5C560`    — ★ миснейм: это ВЫБОР ПО УМОЛЧАНИЮ (0 либо −1);
	//     `charAppearanceSetPartIndex@0xD5AF20`     — ★ миснейм: запись выбора пункта с клампом;
	//     `mediationSendSearch@0xD55BB0`            — что выбор значит для 8 байт критериев 9903.
	void FillAttributeLists(int32 InMode, int32 Cat);
	void ApplyDefaultFilterSelection(int32 InMode, int32 Cat);
	void OnCategoryPicked(int32 InMode, int32 Cat);           // `uiItemDlgOnSelChange@0xD59B20`
	void OnFilterPicked(int32 Which, int32 Idx);              // Which = 0/1/2 → 13410/13411/13412
	void ResetSearchSlots(int32 InMode, bool bResetFilters);  // `satisfyWindowResetSlots@0xD5ADA0`
	void BuildSearchCriteria(uint8 Out[8]) const;             // 8 байт критериев пакета 9903
	// `charGetEquipSlotCode@0xD5B8F0`: индекс пункта комбобокса 13410 → КОД СЛОТА ЭКИПИРОВКИ.
	//   Только режим 5; вне таблицы — −1, и тогда пакет 9903 НЕ УХОДИТ ВООБЩЕ (гейт эталона).
	static int32 EquipSlotCodeOf(int32 Sel);
	// `charGetVehicleStateCode@0xD5BA30` (он же тунк `sub_92DA0A`): 13411 → значение criteria[4].
	int32 VehicleStateCode() const;
	// `uiItemDlgSelChangeNeedsRefresh@0xD5CB10`: надо ли перезаполнять списки при смене категории.
	static bool SelChangeNeedsRefresh(int32 InMode, int32 Prev, int32 Now);

	// ── ★★★ P3: ТРИ ПОЛОСЫ ПРОКРУТКИ (общая точка `FAsdaScrollBarNomal`) ──────────────────────
	void BuildScrollBars();
	void LayoutScrollBars();
	void TickScrollBars();
	int32 GetSellMaxScrollRow() const;
	int32 GetMyMaxScrollRow() const;
	int32 GetLotMaxScrollRow() const;

	// Гнёзда самоцветов строк выдачи (P2).
	void RefreshSowelIcons();

	// ── приём ─────────────────────────────────────────────────────────────────────────────────
	void HandleDispatch(int32 Kind, int32 Sub, const TArray<uint8>& Body);
	void HandleRegisterResult(const TArray<uint8>& Body);
	void HandleListingPacket(const TArray<uint8>& Body, int32 Sub);
	void HandleTradeResult(const TArray<uint8>& Body, int32 Sub);
	// ★★★ ПАЧКА 20 (09.11.2026): приёмники разведены по `a4` эталонного диспетчера, а не по длине тела (разбор — у определений).
	void HandleMyLotsPacket(const TArray<uint8>& Body);       // 9908 (Sub 0) — `inventory_ParseItemListPacket` + показ панели 13200
	void HandleTakeResult(const TArray<uint8>& Body);         // 9910 (Sub 1) — `exchangeParseItemPacket@0xD3A3E0`
	void HandleSearchPage(const TArray<uint8>& Body);         // 9904 (Sub 0) — `netOnItemSlotListResult@0xD5BAB0`
	void HandleBuyResult(const TArray<uint8>& Body);          // 9906 (Sub 1) — `netParseItemAcquireResult@0xD57620`
	// ★★★ ПАЧКА 20, жалобы 4/7/9: запись лота → ячейка сумки (`invSlotUpdateOrRemove@0xD43CB0`). Раскладка записи 57 б —
	//   `CSlot`; у каждого приёмника своё смещение полей (у определения). `Kind`: 0 — выставление (очистить/остаток),
	//   1 — забор, 2 — покупка режима 4 (стек суммируется), 3 — покупка режима 5 (кладётся в слот).
	void ApplyLotToInventory(int32 Kind, const TArray<uint8>& Rec57, int32 RemainAmount);

	// ── ★★★ ПАЧКА 20, жалоба 5: три большие галочки «выбрать всё» (в порте не были привязаны вовсе) ──
	//   Panel: 1 — строки выставления (`EXCH_ALLCHK` 13478, `uiItemSlots_OnHover@0xD4EBC0`),
	//          2 — «мои лоты» (`EXCHMY_ALLCHK` 13479, `uiHandleBoosterMsg@0xD3D8C0`),
	//          3 — выдача поиска (`EXCHANGER_ALLCHK` 13482, `uiSowelSlotCommandHandler@0xD55430`).
	void OnAllCheck(int32 Panel, bool bOn);
	// Снять большую галочку панели (эталон: снятие строки при взведённой «всё» гасит её).
	void DropAllCheck(int32 Panel);

	// ── ★★★ ПАЧКА 20, жалоба 6: резерв сетки выставления (эталон держит КОПИЮ контейнера и вычитает перенесённое —
	//   `uiMoveSlotToList@0xD4AA10`, обратно — `invSlotSwapRecordByMode@0xD4BEB0`) ──
	int32 ReservedInRegRows(int32 Container, int32 Cell) const;   // сколько единиц ячейки уже лежит в строках лота
	int32 AvailableInCell(int32 Container, int32 Cell) const;     // остаток ячейки для сетки (≤ 0 — ячейка пуста)

	// ── нотисы ────────────────────────────────────────────────────────────────────────────────
	void NoticeChat(int32 LocId) const;      // `CChat_AddTokenizedLines`, буфер A
	void NoticeChatB(int32 LocId) const;     // то же, но буфер B — только 9913
	void NoticeTip(int32 LocId) const;       // `CControl_SetTooltipText`
	void NoticeChatAndTip(int32 LocId) const;// нотис 12555 идёт в ОБА канала
	void NoticeModal(int32 LocId) const;     // ★ `CItem_BuildTooltipText` = МОДАЛЬНОЕ ОК (M6)

private:
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;

	EAsdaExchangeMode Mode  = EAsdaExchangeMode::None;
	bool bOpen              = false;
	bool bControlsReady     = false;

	// ★ Флаг «аукцион для тебя заблокирован» — эталонное поле `curCategory` **+0x4C4**
	//   (`structs_ru.h:2115`). Пока он взведён, `PassesOpenGates` пускает ТОЛЬКО режим 3.
	//   У флага РОВНО ТРИ писателя, все три воспроизведены:
	//     • конструктор `playerInventoryState_ctor@0xD3F310` (`entity_char.c:45876`) — 0
	//       (у нас это инициализатор поля);
	//     • приём 9914 `Recv_InventorySlotMismatchNotice@0xD43B70` (`network.c:20479`) — 1;
	//     • пересчёт `itemMediation_onConfirmEvent@0xD3A0A0` (`item_inventory.c:45904-45907`) —
	//       И 1, И **0** ⇒ `RecomputeAuctionBlocked()`.
	//   ⚠ ПРИЁМ 9908 ФЛАГ НЕ ТРОГАЕТ. Он только сохраняет поле +46 каждой записи
	//   (`FAsdaExchangeMyLot::Field46`), по которому потом считает пересчёт.
	bool bAuctionBlocked = false;

	// ★ 29.09.2026: золото 9913, отложенное при открытом окне обмена 2008 (`sub_92104D(g_pItemShopWnd, v)` эталона). 0 = нет.
	int32 DeferredSaleGold = 0;

	// Опкод последнего принятого аукционного пакета — для логов и живых прогонов.
	int32 LastOpcode = 0;

	// Пагинация: эталонные поля `+2318` (текущая) и `+2319` (последняя).
	int32 PageCur  = 0;
	int32 PageLast = 0;

	// ── ★★★ СОСТОЯНИЕ ФИЛЬТРОВ ПОИСКА. Имена и НАЧАЛЬНЫЕ ЗНАЧЕНИЯ — из конструктора эталона
	//   `playerInventoryState_ctor@0xD3F310` (миснейм M13, `entity_char.c:45922-45930`):
	//   `v7[2075] = 0; v7[2076] = 0; v7[2077] = -1; v7[2078] = -1; v7[2079..2081] = -1`.
	//   ★ ЛОВУШКА ХВОСТОВ: «не выбрано» здесь именно −1, ноль — ВАЛИДНЫЙ пункт (закон
	//   `helper_initInvalidIds@0x973D30`), поэтому нигде не заменять −1 на 0.
	int32 SearchCritSel1 = 0;        // +0x206C (dword 2075) — категория РЕЖИМА 5 (RADIOBTN1, 8 кнопок)
	int32 SearchCritSel2 = 0;        // +0x2070 (dword 2076) — категория РЕЖИМА 4 (RADIOBTN2, 7 кнопок)
	int32 PrevCritSel1   = -1;       // dword 2077 — прошлая категория режима 5
	int32 PrevCritSel2   = -1;       // dword 2078 — прошлая категория режима 4
	int32 SearchParamGrade    = -1;  // +0x207C (2079) — выбор комбобокса **13410**
	int32 SearchParamMinLevel = -1;  // +0x2080 (2080) — выбор комбобокса **13411**
	int32 SearchParamAlt      = -1;  // +0x2084 (2081) — выбор комбобокса **13412**

	// Верхний ряд списков, показанный в 8 строках (у «моих лотов» сервер шлёт до 10 записей на
	//   8 строк — ради этого прокрутка и существует).
	int32 ScrollRowSell = 0;
	int32 ScrollRowMy   = 0;
	int32 ScrollRowLot  = 0;

	// Данные трёх панелей.
	UPROPERTY() TArray<FAsdaExchangeRegRow>  RegRows;    // 5
	UPROPERTY() TArray<FAsdaExchangeMyLot>   MyLots;     // 8
	UPROPERTY() TArray<FAsdaExchangeListing> Listings;   // 8

	// Отображение сетки выставления: `SellCells[i]` = номер ячейки контейнера-источника.
	TArray<int32> SellCells;

	// Ячейка, для которой открыт нумпад (режим 5).
	int32 PendingNumPadGrid = -1;

	// ── виджеты ───────────────────────────────────────────────────────────────────────────────
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> GrpRegistration;  // поддерево EXCH_REGISTRATION
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> GrpMyItem;        // поддерево EXCH_MYITEM
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> GrpExchanger;     // поддерево EXCHANGER_WIN
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> GrpRadio1;        // EXCHANGER_RADIOBTN1 (13408)
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> GrpRadio2;        // EXCHANGER_RADIOBTN2 (13409)
	// Узлы, которые не показываются НИКОГДА (рамки SEL, недоделанный фильтр самоцветов 13501/13503,
	//   не связанный в эталоне `EXCHANGER_RADIOBTN3` 13500) — гасим ПОСЛЕ общего показа.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> AlwaysHidden;

	UPROPERTY(Transient) TObjectPtr<UButton> BtnTitleDel   = nullptr;  // 13004
	UPROPERTY(Transient) TObjectPtr<UButton> BtnSelDel     = nullptr;  // 13058
	UPROPERTY(Transient) TObjectPtr<UButton> BtnRegOk      = nullptr;  // 13061
	UPROPERTY(Transient) TObjectPtr<UButton> BtnRegCancel  = nullptr;  // 13060
	UPROPERTY(Transient) TObjectPtr<UButton> BtnMyDel      = nullptr;  // 13251
	UPROPERTY(Transient) TObjectPtr<UButton> BtnMyClose    = nullptr;  // 13250
	UPROPERTY(Transient) TObjectPtr<UButton> BtnBuy        = nullptr;  // 13423
	UPROPERTY(Transient) TObjectPtr<UButton> BtnExchCancel = nullptr;  // 13422
	UPROPERTY(Transient) TObjectPtr<UButton> BtnRescan     = nullptr;  // 13424
	UPROPERTY(Transient) TObjectPtr<UButton> BtnPagePrev   = nullptr;  // 13418/13419
	UPROPERTY(Transient) TObjectPtr<UButton> BtnPageNext   = nullptr;  // 13416/13417
	UPROPERTY(Transient) TObjectPtr<UButton> BtnPageFirst  = nullptr;  // 13483/13484
	UPROPERTY(Transient) TObjectPtr<UButton> BtnPageLast   = nullptr;  // 13485/13486

	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtSubTitle  = nullptr; // 13007
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtMoney     = nullptr; // 13009
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTotalFee  = nullptr; // 13056
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtMyAmount  = nullptr; // 13481
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtMyRegGold = nullptr; // 13203
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtBuyPrice  = nullptr; // 13472
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtExchGold  = nullptr; // 13475
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPage1     = nullptr; // 13414
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPage2     = nullptr; // 13415

	// Сетка выставления: 60 иконок 13064…13123 и их подписи.
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     SellIcons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SellLabels;
	UPROPERTY(Transient) TObjectPtr<UWidget>            SellSelFrame = nullptr; // 13477

	// Пять строк лота.
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     RegIcons;    // 13016…13048
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> RegFeeTexts; // 13018…13050
	UPROPERTY(Transient) TArray<TObjectPtr<UCheckBox>>  RegChecks;   // 13022…13054
	UPROPERTY(Transient) TArray<TObjectPtr<UEditableText>>    RegPriceEdits; // 13019…13051
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaJuiComboBox>> RegDueCombos;  // 13017…13049
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    RegArrUp;    // 13020…13052
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    RegArrDn;    // 13021…13053

	// Восемь строк «моих лотов».
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     MyIcons;   // 13205…13240
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MyNames;   // 13206…13241
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MyPrices;  // 13207…13242
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MyTimes;   // 13208…13243
	UPROPERTY(Transient) TArray<TObjectPtr<UCheckBox>>  MyChecks;  // 13209…13244

	// Восемь строк выдачи.
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     LotIcons;  // 13429…13464
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> LotNames;  // 13430…13465
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> LotPrices; // 13431…13466
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> LotSellers;// 13432…13467
	UPROPERTY(Transient) TArray<TObjectPtr<UCheckBox>>  LotChecks; // 13433…13468

	// ★ Пачка 20, жалоба 5: большие галочки «выбрать всё» (`BigBox_Check`) и их эталонные флаги
	//   (`aRegSlotRecs[7]`, `this+1809`, `gap_2068`).
	UPROPERTY(Transient) TObjectPtr<UCheckBox> ChkRegAll = nullptr;   // 13478 `EXCH_ALLCHK`
	UPROPERTY(Transient) TObjectPtr<UCheckBox> ChkMyAll  = nullptr;   // 13479 `EXCHMY_ALLCHK`
	UPROPERTY(Transient) TObjectPtr<UCheckBox> ChkLotAll = nullptr;   // 13482 `EXCHANGER_ALLCHK`
	bool bRegAll = false;   // взведена «всё» панели выставления
	bool bMyAll  = false;   // взведена «всё» «моих лотов»
	bool bLotAll = false;   // взведена «всё» выдачи
	// Пять иконок гнёзд самоцветов на каждую из 8 строк (13511…13585). У строк 2…8 у этих узлов
	//   в JUI ПУСТОЙ `ID` — адресуем строго по IDNum.
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     LotSowelIcons;  // 8 × 5

	// Фильтры поиска 13410/13411/13412. ★ Статические подписи JUI (`종류` вид, `직업` класс,
	//   `레벨` уровень) — только ЗАГОТОВКА: эталон переписывает их при каждом заполнении списков
	//   (`uiItemDlg_fillAttributeLists` пишет в +299/+300/+301 = 13487/13488/13489).
	UPROPERTY(Transient) TObjectPtr<UAsdaJuiComboBox> CmbKind  = nullptr;   // 13410
	UPROPERTY(Transient) TObjectPtr<UAsdaJuiComboBox> CmbClass = nullptr;   // 13411
	UPROPERTY(Transient) TObjectPtr<UAsdaJuiComboBox> CmbLevel = nullptr;   // 13412
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtFilter1 = nullptr;       // 13487 `EXCHANGER_TXT1`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtFilter2 = nullptr;       // 13488 `EXCHANGER_TXT2`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtFilter3 = nullptr;       // 13489 `EXCHANGER_TXT3`
	// Поддеревья комбобоксов — их гасит хвост `uiItemDlg_fillAttributeLists`
	//   (`if (listNRowCount <= 0) CControl_SetVisible(список, 0)`).
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> GrpFilter1;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> GrpFilter2;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> GrpFilter3;

	// ★ Наборы КАТЕГОРИЙ — это `RadioButton` из данных, импортёр делает по кнопке на пункт
	//   (`{idc}_OPT{i}`): 13408 → 8 кнопок (режим 5), 13409 → 7 (режим 4). Их выбор и есть
	//   `searchCritSel1` / `searchCritSel2`.
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> CatBtns1;   // EXCHANGER_RADIOBTN1_OPT0…7
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> CatBtns2;   // EXCHANGER_RADIOBTN2_OPT0…6

	// ★ Переключатель категорий «моих лотов» 13260 — тоже `RadioButton` (`TabBar_W109_2Btn`),
	//   а НЕ комбобокс: `gamemain_jui.ini` даёт `Name = "TabBar_W109_2Btn"`, подписи двух пунктов
	//   ставит `CItemMediation_BuildUI` нотисами 98200 / 98201 (`shop_trade.c:13924-13931`).
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    MyCatBtns;    // EXCHMY_CateSelBtn_OPT0/OPT1
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MyCatTexts;   // EXCHMY_CateSelBtn_Text/_Text2
	int32 MyCategory = 0;

	// ── Подписи вкладок: сами `TextBlock` и КЭШ базовых цветов (снимается ровно один раз) ──
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> CatTexts1;   // EXCHANGER_RADIOBTN1_Text…_Text8
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> CatTexts2;   // EXCHANGER_RADIOBTN2_Text…_Text7
	TArray<FLinearColor> CatIdle1;
	TArray<FLinearColor> CatIdle2;
	TArray<FLinearColor> MyCatIdle;

	// ★★★ ПЛАШКИ ВКЛАДОК И ИХ ИСХОДНЫЕ КИСТИ (кадр 0 скина). Активная вкладка получает кисть
	//   кадра 2 из `FButtonStyle::Pressed` своей кнопки — там её оставил импортёр
	//   (`UAsdaUmgBuilder::AddOptionButton`). Разбор — в `SetupControls` и в
	//   `AsdaUi::ApplyTabPlaneState`.
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> CatPlanes1;   // EXCHANGER_RADIOBTN1_<idx>_0…7
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> CatPlanes2;   // EXCHANGER_RADIOBTN2_<idx>_0…6
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> MyCatPlanes;  // EXCHMY_CateSelBtn_<idx>_0/_1
	TArray<FSlateBrush> CatPlaneIdle1;
	TArray<FSlateBrush> CatPlaneIdle2;
	TArray<FSlateBrush> MyCatPlaneIdle;
	// ★ Исходные стили кнопок-вкладок: у АКТИВНОЙ кнопка не рисует ничего (иначе её Hovered/Pressed
	//   перекрывают нашу плашку и получается «сначала чёрная полоса, потом обводка»).
	TArray<FButtonStyle> CatBtnIdle1;
	TArray<FButtonStyle> CatBtnIdle2;
	TArray<FButtonStyle> MyCatBtnIdle;

	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	// Сетки иконок: общая точка проекта — рисует иконки и ловит клик по ЖИВОЙ геометрии.
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridSell = nullptr;  // 60 ячеек 13064…13123
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridReg  = nullptr;  // 5 строк лота
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridMy   = nullptr;  // 8 «моих лотов»
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridLots = nullptr;  // 8 строк выдачи
	// ★ P2: 8×5 гнёзд самоцветов строк выдачи. Та же общая точка — иконку по коду рисует она.
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridSowel = nullptr;

	// ★★★ P3: ТРИ ПОЛОСЫ ПРОКРУТКИ — ОДНА ОБЩАЯ ТОЧКА `FAsdaScrollBarNomal`, СВОЕЙ КОПИИ НЕТ
	//   (закон `port-shared-points`; её же использует склад). Носители полос — три «колёсных
	//   окна» со скином `WheelWin_Nomal`, у которого `ScrollBarFileNameVert = "Nomal_Scr"`:
	//     `EXCHANGE_SCROLL` **13125** (сетка выставления, 198×329 от (16,174));
	//     `EXCHMY_SCROLL`   **13249** («мои лоты», 563×320 от (16,222));
	//     `EXCHANGER_SCROLL`**13470** (выдача поиска, 758×320 от (16,223)).
	//   ★ В самом WBP этих узлов НЕТ: у всех трёх `NoSelect = "1"`, а планы скина помечены
	//     `PlaneAlwaysInvisible = "1"` — импортёр не делает ни картинки, ни хит-зоны. Поэтому
	//     положение считается от ЖИВОЙ геометрии первой ячейки списка плюс дельта из данных.
	UPROPERTY(Transient) FAsdaScrollBarNomal ScrollSell;
	UPROPERTY(Transient) FAsdaScrollBarNomal ScrollMy;
	UPROPERTY(Transient) FAsdaScrollBarNomal ScrollLot;

	// Рамка выбора и наведение — только по ЖИВОЙ геометрии.
	FAsdaMouseFrame Mouse;

	FDelegateHandle HDispatch;
};
