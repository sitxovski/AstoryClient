// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ПРЯМОЙ ОБМЕН МЕЖДУ ИГРОКАМИ — КАРКАС `CExchange` (волна О1 торговли, 29.09.2026). [ПОРТ]
//
// ЭТАЛОН. `CExchange` — статический объект 0x2BF9630 (он же `g_pExchangeWnd`, `CExchangeWnd`, `CInventoryWnd_13E7D10`
//   в разных телах IDA). Разбор полей и автомата — `Reports/trade/A_exchange_client.md` §1, §5; план —
//   `Reports/trade/trade_re_report.md` §5 (волна О1). Сервер — `C1_exchange_server.md`, `GameServer.c`.
//   ⚠ НЕ ПУТАТЬ: `UAsdaExchangeSubsystem` порта — АУКЦИОН 13000 (`CItemMediation`), к этой системе отношения не имеет.
//
// ЧТО ЗДЕСЬ (О1).
//   • Модель полей `CExchange` 1:1 (роль +0x04, «закрыл я» +0x08, заявка +0x14, «ответ-принятие ушёл» +0x18, 30-с шкала
//     входящей +0x1C, таймер своей заявки +0x38, userNum сторон +0x3C/+0x3E/+0x46/+0x48, windowIndex +0x40, тип +0x44,
//     цель меню +0x4C6/+0x4C8/+0x2700/+0x2708, имена +0x281C/+0x2830/+0x2844). Признак «окно 2008 открыто» (+0x2C) —
//     общая точка `UAsdaTradeStateSubsystem` (волна 0); эта подсистема — его ЕДИНСТВЕННЫЙ писатель (Т0-1).
//   • Отправка 5282 (`CExchange_SendRequest` тип 0 / `CExchange_SendAcceptRequest` тип 1 — МИСНЕЙМ), таймаут 33 с → 5286(−2)
//     (`CExchange_SendCancelTimeout`), 5284/1 (`CExchange_SendWindowReady`), 5284/0 (`CExchange_SendWindowCancel`),
//     5284/3 (`CExchange_SendRequestAck`, авто-«занят»), 5286(windowIndex) (`CExchange_SendClose`).
//   • Приём 5283 (`Recv_PartyMember` a2=7 → `netTradeSwap_OnResult`), 5285 (`Recv_ExchangeRequestResult`),
//     5287 (`Recv_ExchangeClosed`, гейт `+0x08` в диспетчере network.c:1458).
//   • Значок хотбара 4/5 + звук 215 + чат 5058/5059; окно-вопрос TWOBUT 1520 вид 5/6 (5001/5002, кнопки 5003/5004) со
//     шкалой 30 с от ПРИХОДА 5283 (`uiProgressTickBuffAction`); гейты «Принять» (0x1389/0x138A), меню 2004/2005
//     (7 гейтов, shop_trade.c:6616-6717) и открытия (`uiWindow_OpenWithGuards`); вход эмоциями 202/203 и клавишами 57/58.
//   • Сброс: сброс сессии (любая причина), «закрыть всё при уходе» (зов друга), 5287, отмена 2012/2050 (`CloseByCancelButton`).
//
// ЧТО ЗДЕСЬ (О2, 29.09.2026; итог — `Reports/trade/wave_O2.md`; тела — `Private/UI/AsdaExchangeP2PWindow.cpp`).
//   • Окно `TRAN_WIN` 2008 (`WBP_TRAN_WIN`, разбор `CExchange_BuildUI@0xC77720`): список МОИХ кандидатов 2013…2036
//     (4×6, колесо 2051 — прямоугольник `.JUI`, узла нет), мой лоток 2037…2041, лоток партнёра 2042…2046, рамка 2047,
//     «ОК» моей готовности 2048 / индикатор партнёра 2049 (в ассете HitTestInvisible — обработчика нет), итог 2011,
//     «Отмена» 2012 / крестик 2050 (+ шестерни 2058/2059), подписи 2056/2057/2061.
//   • Модель списков и лотков 1:1: список-источник типа 0 (+0xBFC, 60×57) / типа 1 (+0x515, 31×57), мой лоток (+0x2A8, 5×57),
//     копии лотка (+0x168, 5×64: запись + индекс в списке), лоток партнёра (+0x3C5, 5×51), выбор/перенос/прокрутка
//     (+0x26B4…+0x26DC), накопитель 5289 (+0x26E4).
//   • События JUI окна (`exchangeWnd_OnMessage@0xC6F400`): 0xB выбор, 5 начало переноса (гейты `itemEquipValidate`,
//     `notifyHandleOpcodeRange7DD`), 6 кадр переноса, 7 бросок (`exchangeWnd_HandleItemSlotClick`), 0xD двойной ЛКМ
//     (`exchangeWndOnAddItem`), 8/9/0xA подсказка, 23 колесо. ★ 0xD — ДВОЙНОЙ ЛЕВЫЙ, а не ПКМ, как в A §6.2.
//   • Нумпад режим 3 (`uiExchangeConfirmEnteredQty` / `uiNumPadCloseRefreshExchange`) — общая точка `ShowNumPad`.
//   • Отправка 5288 (`Exchange_SendAddItem`…`SendAddItem4`) и 5290 (`Exchange_SendRemoveItem`…`SendRemoveItem4`), приём 5289
//     (`Recv_Exchange_SlotUpdate`) — через делегат `OnPlayerMenuResult`, разбор в `Tick`. «Душа»: 5288/5290 — в таблице гейта.
//   • Сброс окна: отмена 2012/2050, 5287, «закрыть всё при уходе», гейт открытия, сброс сессии, [ПОРТ] смерть (ключ
//     `asda.Trade.P2PCloseOnDeath`). ESC и `uiCloseAllWindows` окно обмена у эталона НЕ закрывают (таблицы ESC и закрывателей
//     его не содержат) — порт так же.
//   • Временная мера О1 (`asda.Trade.P2PNoWindowCancel`, реестр О1-1) снята: окно есть.
//
// ЧТО ЗДЕСЬ (О3, 30.09.2026; итог — `Reports/trade/wave_O3.md`; тела — `Private/UI/AsdaExchangeP2PWindow.cpp`).
//   • Готовность 2048 (`exchangeOnLockToggleMsg@0xC78940`): +0x4C4 = 1, галка, запор флажка (латч +609, `sub_9350A2`),
//     5291 (`Exchange_SendLockToggle@0xC81990`: счётчик +0x26E0++, 2011 при 2, +0x0C = 0, 5 записей лотка), при счётчике 2 и
//     +0x28 == 0 — 5046 + 5069. Снять готовность из UI нельзя (ветка lock = 0 у эталона недостижима — A §5.4).
//   • Приём 5292 (`Recv_ExchangeLockState@0xC85000`): [4] = 0 → 5071, [4] = 2 → 5073 (оба −11514; сервер шлёт отказ
//     ОТПРАВИТЕЛЮ фиксации — готовность и счётчик эталон не трогает, текст велит закрыть окно); иначе lock = 1 — лоток партнёра
//     (+0x3C5), счётчик++, галка 2049 (ставит КОД: в ассете 2049 HitTestInvisible, обработчика клика у эталона нет), 2011 при 2,
//     5046 + 5069; lock = 0 — счётчик−−, лоток партнёра пуст, 2049 снята, 2011 скрыт, +0x28 = 0.
//   • Итог 2011 (`exchangeUiOnCommand` 0x7DB → LABEL_72, shop_trade.c:6452): счётчик 2 и +0x28 == 0 → +0x28 = 1, 5293
//     (`Exchange_SendComplete@0xC82A80`). Повторно — только после сброса +0x28 (5292 lock = 0, итог 5294/5295 — О4).
//   • «Душа»: 5291/5293 — в `kSoulGatedOpcodes`.
//   • Сервер P44 (выложен 30.09): при «в полёте» 5288 → 5289 код 0 (5019 + 5290 + откат — ветка О2), 5290 — молча, 5291 → 5292
//     [4] = 0 (5071, ветка выше), повторный 5293 — молча. Клиент показывает их обработчиками эталона; своих веток нет.
//
// ЧТО ЗДЕСЬ (О4, 30.09.2026; итог — `Reports/trade/wave_O4.md`; тела — `Private/UI/AsdaExchangeP2PComplete.cpp`).
//   • Приём 5295 (`Recv_Exchange_Complete@0xC85300`, тип 0, 70 б) и 5294 (`Recv_Exchange_CompleteFull@0xC86150`, тип 1, 275 б) —
//     через делегат `OnPlayerMenuResult`, разбор в `Tick`. В начале +0x28 = 0 при ЛЮБОМ коде. Коды: 0 → 5025 + `uiInventoryShowFullNotice`
//     (окно закрыто, 9571 красным: плашка + чат + ONEBUT 1500); 1 → 5026, 2 → 1012 (−21248), 3 → 5028, 4 → 5029 — окно и готовность
//     остаются, 2011 снова шлёт 5293; 5 → применение + вес + закрытие + 5030; прочие — ничего.
//   • Применение 5 — через ОБЩУЮ точку записей инвентаря (`UAsdaInventorySubsystem::ApplyExternalRecord` = `quickSlot_ApplyItemUpdate`
//     / `OnRecv_ItemUpdate`; `ClearCell` = `OnRecv_RemoveSlot_byPtr`) в одном пакете ячеек (`BeginCellBatch`/`EndCellBatch` — хотбар
//     `CClass_BuildItemRecord` пересобирается один раз). Золото — `SetGoldDisplay` (`uiSetHonorPointsDisplay`), ячейка денег —
//     `SetGoldCellAmount` (`invGetFreeSlotByMarker`), отложенное золото аукциона 9913 (`sub_92282B`/`sub_92104D`) — `UAsdaExchangeSubsystem`.
//     Вес — `UAsdaNetSubsystem::SetSelfWeight` (`HUD_UpdateMiniWeightBar`).
//
// ИСПРАВЛЕНИЯ ПО РЕВИЗИИ О3 (О4): Д21 (ниже); Д18 — ответ 5289 сверяется с эхом запроса (invType/invSlot), а не строго FIFO; откат
//   неотправленного/отказанного 5291 снимает только собственный инкремент счётчика готовности.
//
// ДЕФЕКТЫ ОРИГИНАЛА, ИСПРАВЛЕННЫЕ ЗДЕСЬ (A §12; ключ `asda.Trade.P2PFixDefects`, 1 — исправлять, 0 — как эталон):
//   Д1  «закрыть всё при уходе» шлёт 5284/0 вместо 5286 — порт шлёт 5286(windowIndex).
//   Д2  отказ гейта открытия после 5285/1 не освобождает окно сервера — порт шлёт 5286(windowIndex) перед сбросом.
//   Д3  гейты «Принять» типа 1 перепутаны (бустер → 10307, склад → 13104) — порт берёт строки типа 0.
//   Д5/Д6 30-с шкала: по таймауту эталон закрывает ТЕКУЩЕЕ окно-сообщение (даже чужое), снимает значок типа 7 вместо
//      4/5 и, если вопрос не открывали, не отвечает серверу — висящий значок принимает протухшую заявку. Порт: отвечает
//      5284/0, снимает СВОЮ запись очереди и значок 4/5.
//   Д11 вход 202/203/N/B не пишет `+0x2708` — таймаут шлёт 5286(−2) с чужим/−1 userNum. Порт пишет цель.
//   Д12 5285/1 закрывает окно-сообщение вида 5/6 целиком — чужой вопрос теряется без ответа. Порт снимает только свою запись.
//   Д15 (новый, 29.09) авто-«занят» `SendRequestAck` сбрасывает +0x14/+0x18/+0x1C, хотя они описывают ПЕРВУЮ заявку, а
//      не отклонённую вторую: у висящей заявки останавливается шкала. Порт эти поля не трогает.
//   Д16 (новый, О2) откат лотка после ошибки 5289 (`exchangeSlot_Reset@0xC7AC00`) пишет запись в список по ТЕКУЩЕМУ выбору
//      +0x26D0, а не по индексу-источнику из копии лотка (+0x168 +60, им пользуется снятие `airPlane_CommitFoundResource`):
//      клик по другому кандидату между 5288 и 5289 переписывал чужую запись списка (при −1 — память перед массивом).
//      Порт берёт индекс из копии лотка.
//   Д17 (новый, О2) перерисовка страницы списка (`CItemListWin_RefreshItemPage`) целиком пропускается при выбранном
//      кандидате с индексом ≥ 24 (`if (+0x26D0 < 24)` охраняет весь блок, а не только сброс заливки): колесо на 3-й…7-й
//      странице с выбранной вещью оставляло старую страницу. Порт охраняет только сброс заливки.
//   Д18 (новый, О3; ревизия О2 СРЕДН-1) ответ 5289 применяется к ячейке ТЕКУЩЕГО выбора (+0x26DC/+0x26D8), а не к той, на
//      которую был запрос: две 5288 быстрее пинга — ответ на первую пишет/откатывает вторую ячейку, 5290 снимает у сервера
//      чужую. Сервер отвечает по TCP по порядку ⇒ порт держит очередь ожидающих ответа 5288 {ячейка, вид, индекс-источник,
//      кол-во, докладка} и берёт 5289 из её головы.
//   Д20 (новый, О3; ревизия О2 МЕЛК-2, только отображение) тип 1: отказ 5289 на ДОКЛАДКУ золота — 5290 вида 4 стирает у
//      сервера всю ячейку, а `exchangeSlot_Reset` возвращает в список копию ДО докладки: ранее принятая часть пропадает с
//      экрана до переоткрытия окна. Порт ставит в список фактический запас золота (тот же приём, что у снятия
//      `cInven_ApplySelectedSlotItemCode`).
//   Д21 (новый, О4; ревизия О3 СРЕДН-1, реестр О3-1) отказ фиксации 5292 [4] = 0/2 эталон только печатает (5071/5073), готовность,
//      латч и счётчик остаются; сервер фиксацию не взводил, партнёр 5292 не получил — у отказанного при готовности партнёра
//      появлялись 2011 и 5046/5069, итог не проводился ни стоком, ни P44 (тупик до «Отмены»). Порт откатывает готовность (как
//      неотправленный 5291), если 5293 ещё не ушёл; после 5293 — как эталон (подтверждение уже на сервере).
//   [ПОРТ] (не дефект эталона) одновременная смерть обеих сторон: у порта каждая сторона закрывает окно путём «Отмены», и два
//      5286 по одному окну у стока освобождали бы его дважды. Сторона-цель (+0x04 = 0) ждёт 3 с (5287 от партнёра закроет
//      окно без её пакета) и только потом шлёт свою «Отмену».
//   Не исправлено (истины нет в данных): Д4 — гейт бустера при открытии печатает 97101 (как эталон); Д9 — пустое
//      присваивание при докладке в 5289; Д10 — 5017/5018 инвертированы; Д7 — 5013 недостижим; Д14 — индекс == числу записей
//      проходит гейт `sub_C7A8A0` (дальше безвреден: запись пуста).
//
// ЗАКОНЫ. Приём копится в очередь и разбирается в `Tick` (no-reentrant-net-callbacks: закрытие окон и показ модалок из
//   сетевого колбэка запрещены). userNum (i16) ≠ charId (i32). Протокол не меняется: тела 1:1 с эталоном.
//
// Отладка (не Shipping): `asda.Trade.P2P` — дамп модели; самотесты `asda.Trade.SelfTestExchangeO1` (`AsdaExchangeP2PSelfTest.cpp`),
//   `…O2` (`AsdaExchangeP2PSelfTestO2.cpp`), `…O3` (`AsdaExchangeP2PSelfTestO3.cpp`), `…O4` (`AsdaExchangeP2PSelfTestO4.cpp`).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс модели вместе с сессией
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "AsdaExchangeP2PSubsystem.generated.h"

class UUserWidget;        // корень WBP_TRAN_WIN
class UWidget;            // шестерни, рамка, группа кнопки итога
class UImage;             // планы ячеек 2013…2046
class UTextBlock;         // надписи ячеек и подписи
class UButton;            // 2011 / 2012 / 2050
class UCheckBox;          // 2048 / 2049
class UAsdaButtonRelay;   // реле кнопок (держит делегаты)
struct FAsdaItemInstance; // запись 57 б (подсказка по записи лотка/списка)

UCLASS()
class ASDAREBORN_API UAsdaExchangeP2PSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// ── UGameInstanceSubsystem ──
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;   // зависимости и начальная модель
	virtual void Deinitialize() override;                                    // отписка от сети и хотбара

	// ── IAsdaSessionResettable ──
	// [ПОРТ] Эталон при разрыве закрывает клиент, при выходе к выбору персонажа пишет только +0x2C=0 (`gameLeaveToCharSelect`).
	//   Порт сбрасывает модель МОЛЧА при ЛЮБОЙ причине, включая смену карты: соединение пересоздаётся, сервер сам снимает
	//   обмен (`DisConnectUser` S:111411, `CharRegenIndunBeforePortalMove` S:115920). Значки 4/5 и свои записи 1520 снимаются,
	//   окно 2008 прячется без пакетов (О2), перенос и подсказка гасятся.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// ── FTickableGameObject ──
	virtual void Tick(float DeltaTime) override;   // разбор очереди приёма, таймеры 33 с / 30 с, мышь окна 2008, [ПОРТ] смерть
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaExchangeP2PSubsystem, STATGROUP_Tickables); }   // статистика тикера
	virtual bool IsTickable() const override { return !IsTemplate(); }   // тикает только живой экземпляр
	virtual bool IsTickableInEditor() const override { return false; }   // в редакторе без мира не тикает

	// ═══ ВХОДЫ ═══
	// Меню по игроку 2004 (тип 0) / 2005 (тип 1): `exchangeUiOnCommand` case 0x7D4/0x7D5 (shop_trade.c:6616-6717) —
	//   7 гейтов (5043/5033/5032/13016/13104/10307/крафт молча) → при отказе `invItemListDialogInit` + меню скрыть + нотис;
	//   окно обмена открыто → 5042 и меню ОСТАЁТСЯ; иначе `SendRequest` + меню скрыть. Возврат: надо ли скрыть меню.
	bool OnMenuCommand(bool bPersonal, int32 TargetUserNum, int32 TargetUserIdx, const FString& TargetName);
	// `UI_DispatchTargetContextMenu(2, 2/3)` (ui_core.c:32166-32198): эмоции 202/203 окна действий, хотбар, клавиши 57/58.
	//   Цель — выбранный ПЕРСОНАЖ (`!g_nInputControlState`), не я. Гейтов меню и «окно открыто» эталон тут НЕ проверяет.
	//   Возврат: заявка ушла.
	bool SendRequestFromSelectedTarget(bool bPersonal);
	// `set_o9C2(g_pExchangeWnd, userNum)` (game_main.c:9424): при ПКМ-отпускании по игроку, если таймер заявки не идёт,
	//   запомнить userNum для таймаута 5286(−2) (+0x2708). Зовёт меню игрока.
	void NoteMenuTargetForTimeout(int32 TargetUserNum);
	// Кнопки «Отмена» 2012 / крестик 2050 окна 2008 (`exchangeUiOnCommand` 0x7DC / 2050, shop_trade.c:6536 LABEL_76):
	//   `CExchange_SendClose` → `invItemListDialogInit` → `uiInventoryDialogReset` → 5041.
	void CloseByCancelButton(const TCHAR* Why);
	// [ОРИГИНАЛ] хвост `uiCloseAllWindowsOnLeave@0xD62790` (ui_core.c:54726-54730): при открытом окне — отказ + сброс окна.
	//   Зовёт `UAsdaTradeStateSubsystem::OnCloseAllWindowsOnLeave`. Возврат: было ли окно открыто.
	bool OnCloseAllWindowsOnLeave(const TCHAR* Why);

	// ═══ ЧТЕНИЯ ═══
	// `CExchange+0x14`: заявка в полёте ИЛИ есть входящая (гейт 5054). ≠ «окно открыто» (+0x2C, общая точка торговли).
	bool IsRequestActive() const { return bRequestActive != 0; }
	// `CExchange+0x38`: идёт 33-с таймер моей заявки.
	bool IsRequestTimerActive() const { return bTimerActive != 0; }
	// `CExchange+0x1C` (МИСНЕЙМ `g_bItemUpgradeWndOpen`): идёт 30-с шкала входящей заявки.
	bool IsIncomingGaugeActive() const { return bIncomingGauge != 0; }
	int32 GetWindowIndex() const { return WindowIndex; }   // `CExchange+0x40` — индекс окна сервера из 5285
	int32 GetExchangeType() const { return ExchangeType; } // `CExchange+0x44` — 0 обычные / 1 личные вещи
	int32 GetRole() const { return Role; }                 // `CExchange+0x04` — 1 инициатор/простой, 0 цель
	FString DebugString() const;                           // сводка модели одной строкой (лог / `asda.Trade.P2P`)

	// ★ След нотисов подсистемы (самотесты): счётчик, loc-id и слот последнего.
	int32 GetNoticeSeq() const { return NoticeSeq; }             // сколько нотисов напечатано
	int32 GetLastNoticeLoc() const { return LastNoticeLoc; }     // loc-id последнего
	const FString& GetLastNoticeSlot() const { return LastNoticeSlot; }   // слот [ss1] последнего

	// ═══ ОКНО 2008 (волна О2): константы эталона ═══
	static constexpr int32 kRec          = 57;   // запись предмета (`CSlot_Init`, 0x39)
	static constexpr int32 kCopyRec      = 64;   // копия лотка +0x168: запись 57 + индекс в списке-источнике @60
	static constexpr int32 kPartnerRec   = 51;   // запись лотка партнёра +0x3C5 (из 5292)
	static constexpr int32 kTray         = 5;    // ячеек лотка (`hasFreeQuickSlot`: < 5 занятых)
	static constexpr int32 kListCells    = 24;   // ячеек списка на экране (2013…2036, 4 ряда × 6)
	static constexpr int32 kCols         = 6;    // ячеек в ряду (`a2 % 0x7DD + 6 * +0x26B4`)
	static constexpr int32 kList0Max     = 60;   // список типа 0 (+0xBFC, `memset 0xD5C`)
	// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): эталон 31 = 30 ячеек сумки + золото (+0x515, `memset 0x6E7` = 31 × 57);
	//   сумка 60 ⇒ 61 (= `UAsdaInventorySubsystem::kShellSlots + 1`, static_assert в .cpp).
	static constexpr int32 kList1Max     = 61;
	static constexpr int32 kCtrlList     = 2013; // TRAN_TRAY01 — первый кандидат
	static constexpr int32 kCtrlTray     = 2037; // TRAN_TRAY25 — первая ячейка моего лотка
	static constexpr int32 kCtrlPartner  = 2042; // TRAN_TRAY30 — первая ячейка лотка партнёра
	static constexpr int32 kCells        = 34;   // 24 + 5 + 5 ячеек окна (индекс = IDNum − 2013)
	static constexpr int32 kWinZ         = 730;  // z окна (как окна лавки; наверх поднимает показ)

	// Окно 2008 показано (зеркало видимости корня; признак +0x2C — в `UAsdaTradeStateSubsystem`).
	bool IsWindowShown() const { return bWinShown; }

	// ═══ ОКНО 2008: СОБЫТИЯ JUI (`exchangeWnd_OnMessage@0xC6F400`) — их зовёт опрос мыши `Tick`, и они же — путь самотеста ═══
	// 0xB (нажатие ЛКМ) по контролу `IdNum` (2013…2041): `itemSlotCmdDispatch@0xC6F700` → +0x10 «ячейка взята».
	void OnCellPressed(int32 IdNum);
	// 5 (начало переноса), `IdNum` — источник: гейты `itemEquipValidate` и `notifyHandleOpcodeRange7DD`. true — перенос начат.
	bool OnDragBegin(int32 IdNum);
	// 7 (бросок): `exchangeWnd_HandleItemSlotClick@0xC72520`; `TargetIdNum` — контрол под курсором (−1 — мимо окна).
	void OnDrop(int32 TargetIdNum);
	// 0xD (двойной ЛКМ) по контролу: `exchangeWndOnAddItem@0xC71A30` (кандидат → лоток / лоток → снять).
	void OnCellDoubleClick(int32 IdNum);
	// 23 (колесо 2051): `itemListWinHandleMsg2051@0xC707F0`. `Notches` > 0 — «от себя» (вверх).
	void OnWheel(int32 Notches);
	// 0x10601/0x10602 по 2048: `exchangeOnLockToggleMsg@0xC78940` (О3) — готовность, 5291, 5046 + 5069. Запертый флажок
	//   (латч +609 после первой готовности) событий не шлёт: галка возвращается к модели.
	void OnMyLockChecked(bool bChecked);
	// 0x10401 по 2011 → `exchangeUiOnCommand` 0x7DB → LABEL_72 (shop_trade.c:6452-6458): итог 5293 (О3).
	void OnOkClicked();
	// Живой хит-тест: IdNum ячейки (2013…2046) под точкой в абсолютных координатах Slate; −1 — нет.
	int32 HitTestCellAt(const FVector2D& AbsPoint) const;

	// ═══ ОКНО 2008: ЧТЕНИЯ ДЛЯ САМОТЕСТА И ДАМПОВ ═══
	UUserWidget* DebugWindowWidget() const { return WinWidget; }             // корень WBP_TRAN_WIN (геометрия путём игрока)
	int32 GetFoundControls() const { return FoundControls; }                  // сколько привязок эталона разобрано
	FString DebugCellText(int32 IdNum) const;                                // надпись ячейки 2013…2046
	float DebugCellAlpha(int32 IdNum) const;                                 // прозрачность плана ячейки (заливка 5 — перенос)
	bool DebugCellHasIcon(int32 IdNum) const;                                // у плана ячейки есть текстура
	UWidget* DebugCellWidget(int32 IdNum) const;                             // план ячейки (геометрия)
	UWidget* DebugNamedWidget(const TCHAR* Name) const;                      // узел окна по имени (геометрия подписей/кнопок)
	FString DebugWidgetText(const TCHAR* Name) const;                        // текст TextBlock по имени узла
	bool DebugIsShown(const TCHAR* Name) const;                              // узел окна видим по цепочке
	bool DebugIsChecked(int32 IdNum) const;                                  // галка 2048 / 2049
	FString DebugDumpModel() const;                                          // списки, лоток, выбор — одной строкой

#if !UE_BUILD_SHIPPING
	// Самотесты: взвести/снять «заявку в полёте» (+0x14) без сети (контрольная пара ПД-2 волны 0).
	void DebugSetRequestActive(bool bOn) { bRequestActive = bOn ? 1 : 0; }
	// Самотест волны 0: разобрать очередь приёма сейчас (тот же `Tick`-путь, без ожидания кадра).
	void DebugDrainNow() { DrainPending(); }
#endif

private:
	friend struct FAsdaExchangeP2PSelfTest;     // самотест О1 читает и сдвигает поля модели (таймеры) — только отладка
	friend struct FAsdaExchangeP2PSelfTestO2;   // самотест О2 читает списки/лоток и подкладывает лоток партнёра — только отладка
	friend struct FAsdaExchangeP2PSelfTestO3;   // самотест О3 читает готовность/счётчик/очередь 5289 — только отладка
	friend struct FAsdaExchangeP2PSelfTestO4;   // самотест О4 читает лоток/списки/+0x28 при итоге 5294/5295 — только отладка
	friend struct FAsdaDedupW1ASelfTest;        // ★ В1 дедупа №2: побайтная сверка `PartnerRecordTo57` с прежней сборкой

	// ── приём ──
	void OnMenuResult(int32 Opcode, const TArray<uint8>& Body);   // подписка на `UAsdaNetSubsystem::OnPlayerMenuResult`: только копим
	void DrainPending();                                          // разбор очереди приёма (из `Tick`)
	void Recv5283(const TArray<uint8>& Body);                     // `Recv_PartyMember` case 4/5 (network.c:27343-27377)
	bool CheckSlotItemMatch(const TArray<uint8>& Body);           // `exchangeCheckSlotItemMatch@0xC86CB0`: «эхо своей заявки»
	bool TradeSwapOnResult(const TArray<uint8>& Body);            // `netTradeSwap_OnResult@0xC837C0` (network.c:14057-14259)
	void Recv5285(const TArray<uint8>& Body);                     // `Recv_ExchangeRequestResult@0xC84160` (network.c:14263-14368)
	void Recv5287(const TArray<uint8>& Body);                     // `Recv_ExchangeClosed@0xC86BA0` (network.c:15077-15099)
	void Recv5289(const TArray<uint8>& Body);                     // `Recv_Exchange_SlotUpdate@0xC84820` (network.c:14378-14510), О2
	void Recv5292(const TArray<uint8>& Body);                     // `Recv_ExchangeLockState@0xC85000` (network.c:14533-14583), О3
	void Recv5295(const TArray<uint8>& Body);                     // `Recv_Exchange_Complete@0xC85300` (network.c:14587-14864), тип 0, О4
	void Recv5294(const TArray<uint8>& Body);                     // `Recv_Exchange_CompleteFull@0xC86150` (network.c:14878-15060), тип 1, О4

	// ── итог обмена (О4, `AsdaExchangeP2PComplete.cpp`) ──
	bool CompleteRefusal(int32 Code);               // общие ветки 0…4 обоих приёмников: нотис (+ закрытие при 0); true — ветка отработала
	void ShowFullNotice();                          // `uiInventoryShowFullNotice@0xC86EF0` (ui_core.c:35513): сброс окна + 9571 красным ×3
	void ApplyItemRec(const uint8* Rec57, const TCHAR* Why);   // `quickSlot_ApplyItemUpdate@0xD61610` → общая точка `ApplyExternalRecord`
	void RemoveInvSlot(const uint8* Rec57, const TCHAR* Why);  // `OnRecv_RemoveSlot_byPtr@0xD0CE30` (контейнеры 1/2) → общая `ClearCell`
	void FinishComplete(int32 Weight, const TCHAR* Why);       // хвост ветки 5: вес, `invItemListDialogInit`, `uiInventoryDialogReset`, 5030

	// ── отправители (тела 1:1; конверт меню `SendMenuPacket`) ──
	bool SendRequest(bool bPersonal, const TCHAR* Source);   // `CExchange_SendRequest` / `CExchange_SendAcceptRequest`
	void SendCancelTimeout();                                // `CExchange_SendCancelTimeout@0xC7B5F0` (33 с → 5286(−2))
	bool SendWindowReady();                                  // `CExchange_SendWindowReady@0xC7CF60` (5284/1)
	bool SendWindowCancel();                                 // `CExchange_SendWindowCancel@0xC7D5F0` (5284/0)
	bool SendRequestAck(const TArray<uint8>& Body);          // `CExchange_SendRequestAck@0xC7DC40` (5284/3)
	bool SendClose();                                        // `CExchange_SendClose@0xC830A0` (5286(windowIndex) + сброс полей)

	// ── окна и сбросы эталона ──
	void InvItemListDialogInit(const TCHAR* Why);   // `invItemListDialogInit@0xC78AB0` (item_inventory.c:23583) — сброс модели
	void InventoryDialogReset(const TCHAR* Why);    // `uiInventoryDialogReset@0xC75BE0` (ui_core.c:35228) — +0x2C=0, сброс, золото
	void DialogClearAndHide();                      // `uiDialog_ClearAndHide@0xC75850` — скрыть меню 2000
	void OpenWithGuards();                          // `uiWindow_OpenWithGuards@0xC758A0` (ui_core.c:35155-35215)
	void CloseQuestionBox(int32 Type, const TCHAR* Why);   // `CGameUI_CloseWindow(g_pItemTooltipUI, 5/6)` из 5285 (звук 5)
	void ShowQuestionBox(int32 NoticeType);         // `uiNoticeButtonDispatch` case 4/5 (ui_core.c:69410-69417) → TWOBUT 1520
	void OnQuestionAnswer(int32 Type, bool bYes);   // «Принять» 0x1389/0x138A (ui_core.c:57205-57308) / «Отказать» 1524
	void TickIncomingGauge();                       // `uiProgressTickBuffAction@0xD78870` (ui_core.c:58242-58280)
	void OnHotbarNoticeClicked(int32 NoticeType);   // подписка на `UAsdaHotbarSubsystem::OnNoticeIconClicked` (типы 4/5)

	// ── окно 2008: каркас и разбор (О2, `AsdaExchangeP2PWindow.cpp`) ──
	bool EnsureWindow();                            // каркас `FAsdaJuiWindowHandle::Ensure` + `CExchange_BuildUI@0xC77720`
	void ResetWindowWidget();                       // снести виджет, зону и ссылки на контролы
	void SetupWindowControls();                     // разбор дерева WBP 2008 по именам `.JUI` + подписи BuildUI
	void ShowWindow();                              // ветка «открыть» `uiWindow_OpenWithGuards` (ui_core.c:35205-35214)
	void HideWindow(const TCHAR* Why);              // скрыть корень (`CControl_SetVisible(pSlotCtrl1, 0)`), погасить перенос/подсказку
	void TickWindowMouse();                         // опрос мыши окна: наведение 8/9/0xA, 0xB, 5/6/7, 0xD, шестерня 2050
	void TickTooltip(int32 IdNum);                  // `equipSlotShowTooltip@0xC70DB0` (8) / `CControl_KillFocus` (0xA); −1 — спрятать
	void TickGearHover(bool bOverClose);            // 8/0xA по 2050: шестерня 2058 ↔ анимированная 2059 (shop_trade.c:6358-6363)
	void CancelDragVisual();                        // снять призрак переноса (без изменения модели)
	void RegisterWindowHooks();                     // колесо 2051 у оконной подсистемы (Initialize)
	void UnregisterWindowHooks();                   // снять колесо (Deinitialize)
	static void PlaneShow(UWidget* W);              // `CControl_Show`: перезапустить анимацию плана и показать
	static void PlaneHide(UWidget* W);              // `CControl_CloseHide`: остановить анимацию и спрятать

	// ── окно 2008: перерисовки эталона ──
	void SlotIconsReset();                          // `uiSlotIconsReset@0xC77570` — 34 ячейки пусто, галки 2048/2049 сняты
	void BuildSourceList(int32 Type);               // `invListFilterConsumablesRefresh@0xC78E20` (+ фильтры типа 0/1 и колесо)
	void FilterType0();                             // `invFilterSowelsToList@0xC78E90` (МИСНЕЙМ) — контейнер 2 → +0xBFC
	void FilterType1();                             // `invFilterConsumablesToList@0xC79110` (МИСНЕЙМ) — контейнер 1 + золото → +0x515
	void ResetSelectionAndRefresh();                // `CItemListWin_ResetSelectionAndRefresh@0xC7B5A0`
	void RefreshItemPage(int32 Page);               // `CItemListWin_RefreshItemPage@0xC79600` (item_inventory.c:23774)
	void RefreshTrayIcons();                        // `CQuest_RefreshItemIconGrid@0xC766E0` (quest.c:4915) — мой лоток
	void RefreshPartnerTray(bool bShow);            // `CraftSlots_RefreshIcons@0xC73F30` (item_enhance.c:190) — лоток партнёра, 2049, 2011
	void RefreshSlotText();                         // `uiInventoryRefreshSlotText@0xC75D70` — возврат ячейки после отмены переноса
	void MoveCursorToSel();                         // `uiItemSlotMoveCursorToSel@0xC75D00` — рамка 2047 на выбранного кандидата
	void RefreshSlotFillText();                     // `exchangeWnd_RefreshSlotFillText@0xC72440` (6) — ячейка-источник «у курсора»
	void SetCellFill(int32 IdNum, int32 Mode);      // `CPlane_SetFillMode(ячейка, 0/5)`: 0 — обычный план, 5 — рисуется у курсора
	void DrawCell(int32 IdNum, int32 Code, bool bClearIfEmpty);   // `CControl_DrawTextureRect` иконки предмета (0 — пустая текстура)
	void SetCellText(int32 IdNum, const FString& Text);          // `CControl_SetText` надписи ячейки
	void SetCellTextColor(int32 IdNum, const FColor& Color);     // `CControl_SetField923` цвет надписи ячейки
	void SetOkButtonShown(bool bShown);             // `CControl_SetVisible(2011)` — итог виден только при счётчике 2
	void SetCheckState(int32 IdNum, bool bChecked); // `CControl_SetVisibleState(2048/2049)` — галка

	// ── окно 2008: выбор, перенос, добавление, снятие (порядок и ветки — по телам эталона) ──
	bool SelectListCell(int32 Cell);                // `itemSlotSelectByIndex@0xC70860`
	bool ResolveTrayCell(int32 Cell);               // `itemSlotResolveIconRecord@0xC70B40`
	bool EquipValidate(int32 IdNum);                // `itemEquipValidate@0xC7A8E0` (только перенос кандидата)
	int32 DragGate(int32 IdNum);                    // `notifyHandleOpcodeRange7DD@0xC722D0`: 2 — перенос начат, 1 — нет
	bool ListIndexValid(int32 Index) const;         // `sub_C7A8A0` = `a2 <= +0x26C8` (Д14: равенство проходит)
	bool HasFreeTraySlot() const;                   // `hasFreeQuickSlot@0xC72D60`
	bool ItemRecordExists(int32 Code, int32 B50) const;   // `isItemRecordExists@0xC86D70`
	bool IsSlotItemTypeState(int32 Index) const;    // `isSlotItemTypeState@0xC86E00` (subType 32 и +50 == 3)
	bool SelectedMatchesSlot() const;               // `cItemListWinSelectedMatchesSlot@0xC7B300` (тип 0: тот же стак в лотке)
	bool SelectedIsGoldInTray() const;              // `cItemListWinSelectedIsSpecialItem@0xC7B1C0` (МИСНЕЙМ: золото уже в лотке)
	bool SelectedIsAllGold() const;                 // `sub_C7B410` (тип 0) / встроенная проверка типа 1: в лоток ВСЁ золото
	void AllocSlotRecord();                         // `allocSlotRecord@0xC739A0` (тип 1, вся запись)
	void PlaneRecallStoredCard();                   // `plane_RecallStoredCard@0xC73790` (тип 0, докладка 1 шт.)
	void SlotSwapIntoFreeSlot();                    // `slotSwapIntoFreeSlot@0xC73180` (тип 0, самоцвет целиком)
	void PlaneMoveCardToEmptySlot();                // `planeMoveCardToEmptySlot@0xC73320` (тип 0, `flagWord176`)
	void PlaneSlotActivateFree();                   // `planeSlotActivateFree@0xC73010` (тип 0, 1 шт.)
	void CraftMatchSlotByItem();                    // `craftMatchSlotByItem@0xC73CE0` (тип 1, докладка золота)
	void CraftSlotsAssignFreeSlot();                // `craftSlotsAssignFreeSlot@0xC73B10` (тип 1, золото/кол-во с нумпада)
	void PlaneStoreCardIntoSlot();                  // `plane_StoreCardIntoSlot@0xC734C0` (тип 0, докладка с нумпада)
	void SlotActivateFreeWithData();                // `slotActivateFreeWithData@0xC72DE0` (тип 0, кол-во с нумпада)
	void CommitFoundResource();                     // `airPlane_CommitFoundResource@0xC75190` (тип 0, возврат снятого в список)
	void ApplySelectedSlotItemCode();               // `cInven_ApplySelectedSlotItemCode@0xC75400` (тип 1, возврат снятого)
	void ExchangeSlotReset();                       // `exchangeSlot_Reset@0xC7AC00` (откат после ошибки 5289)
	void AddSelectedItem();                         // `exchange_AddSelectedItem@0xC7AFE0` (ОК нумпада режима 3)
	void OpenNumPad(int32 MaxQty);                  // `uiOpenContextPopupWithHandlers(g_pNumPadDialog, max, 3, 0)`
	void RollbackUnsentAdd(int32 Kind, const TCHAR* Why);   // [ПОРТ] 5288 вида Kind не ушёл (душа/нет связи): локальный откат

	// ── окно 2008: отправители 5288/5290 (тела 1:1; конверт меню `SendMenuPacket`) ──
	bool SendAddItem(int32 Kind);                   // Kind 1 `Exchange_SendAddItem` · 2 `…2` · 3 `…3` · 4 `…4`
	bool SendRemoveItem(int32 Kind);                // Kind 1 `Exchange_SendRemoveItem` · 2 `…2` · 3 `…3` · 4 `…4`
	bool SendRemoveByMode();                        // `exchangeSendRemoveItemByMode@0xC86EA0` — 3 (тип 0) / 4 (тип 1)
	void RemoveTrayCell();                          // снятие ячейки +0x26C4 (0xD / бросок на список): 5290 вида 1/2, возврат, обнуление

	// ── окно 2008: готовность и итог (О3) ──
	bool SendLockToggle();                          // `Exchange_SendLockToggle@0xC81990` — счётчик/2011/+0x0C и 5291 (5 записей лотка)
	bool SendComplete();                            // `Exchange_SendComplete@0xC82A80` — 5293(windowIndex)
	void RollbackUnsentLock(const TCHAR* Why);      // [ПОРТ] 5291 не ушёл (душа/нет связи): готовность не взводится
	void RollbackRefusedLock(int32 Act);            // [ПОРТ, Д21] отказ фиксации 5292 [4]=0/2 до 5293: готовность назад (ключ 1)
	bool PickedIsLowOrAbsent() const;               // фильтр типа 1: `g_nPickedUserIdx` не найден или ≤ 23 ур. (item_inventory.c:23745)
	TArray<uint8> BuildPartnerRec57(int32 Slot) const;   // запись 57 б для подсказки лотка партнёра (`equipSlotShowTooltip`)
	// ★ U17 (В1 дедупа №2): запись 57 б из 45-байтовой записи партнёра (`equipSlotShowTooltip`) — для побайтного самотеста.
	static TArray<uint8> PartnerRecordTo57(const uint8* PartnerRec45, bool bGems);

	// ── записи моделей (57/64/51 б) ──
	uint8* ListRec(int32 Type, int32 Index);        // запись списка-источника (+0xBFC / +0x515); nullptr — вне границ
	const uint8* ListRec(int32 Type, int32 Index) const;   // то же, чтение
	uint8* TrayRec(int32 Slot);                     // запись моего лотка +0x2A8
	const uint8* TrayRec(int32 Slot) const;         // то же, чтение
	uint8* CopyRec(int32 Slot);                     // копия лотка +0x168 (57 + индекс @60)
	const uint8* PartnerRecAt(int32 Slot) const;    // запись лотка партнёра +0x3C5 (51 б)
	int32 SelListIndexForReset() const;             // Д16: индекс списка для отката — из копии лотка (ключ 1) / +0x26D0 (ключ 0)
	bool CloseOnDeath() const;                      // ключ `asda.Trade.P2PCloseOnDeath`
	static int32 TrayCtrl(int32 Slot) { return (Slot >= 0 && Slot < kTray) ? kCtrlTray + Slot : -1; }          // IDNum ячейки лотка; −1 — нет
	static int32 ListCtrl(int32 Cell) { return (Cell >= 0 && Cell < kListCells) ? kCtrlList + Cell : -1; }    // IDNum ячейки списка; −1 — нет

	// ── гейты (порядок и loc — по обработчику эталона; 0 = пропуск, −1 = отказ молча, >0 = loc отказа) ──
	int32 MenuGateLoc() const;                           // меню 2004/2005 (shop_trade.c:6617-6629)
	int32 AcceptGateLoc(int32 Type, FColor& OutColor) const;   // «Принять» 0x1389/0x138A (ui_core.c:57205-57306)
	int32 OpenGateLoc() const;                           // `uiWindow_OpenWithGuards` (ui_core.c:35163-35202)

	// ── общее ──
	void Notice(int32 LocId, FColor Color, const FString& Slot = FString()) const;   // чат `CChat_AddTokenizedLines(txt, цвет, 10000)`
	double NowReal() const { return FPlatformTime::Seconds(); }   // эталонные часы `CObject_GetField2(g_dwDisplayOptionFlags)` (мс)
	bool FixDefects() const;                                      // ключ `asda.Trade.P2PFixDefects`
	void EnsureBindings();                                        // ленивые подписки на сеть и хотбар (поднимаются позже)
	int32 MyUserNum() const;                                      // `CClass_GetField142C(&g_InventoryBag)->effectSlots[111].field_5B8`
	bool IsPartnerDead() const;                                   // [ПОРТ] актор партнёра (сторона 5285 ≠ я) в позе «мёртв» — О3

	// ═══ МОДЕЛЬ ПОЛЕЙ `CExchange` (смещения эталона; A §1.1) ═══
	int32 Role = 1;               // +0x04 — 1 инициатор/простой, 0 я цель входящей
	int32 bClosedByMe = 0;        // +0x08 — «закрытие отправлено мной»: 5287 игнорируется
	int32 bLockOff = 1;           // +0x0C — «моя готовность снята» (`Exchange_SendLockToggle`: 0 при взводе / 1 при снятии; сбросы — 1)
	int32 bCellTaken = 0;         // +0x10 — ячейка взята (итог 0xB: `itemSlotCmdDispatch`)
	int32 bRequestActive = 0;     // +0x14 — заявка в полёте / входящая есть (гейт 5054)
	int32 bAcceptSent = 0;        // +0x18 — «5284/1 уже отправлен»
	int32 bIncomingGauge = 0;     // +0x1C — идёт 30-с шкала входящей (МИСНЕЙМ `g_bItemUpgradeWndOpen`)
	int32 bDragging = 0;          // +0x24 — идёт перетаскивание (`notifyHandleOpcodeRange7DD` → 1)
	int32 bCompleteSent = 0;      // +0x28 — «5293 уже отправлен» (О3)
	int32 bStackAdd = 0;          // +0x30 — 5288 ушёл докладкой (`CExchange_SendAddItem2`)
	int32 bTimerActive = 0;       // +0x38 — идёт 33-с таймер моей заявки (`g_bExchangeTimerActive`)
	int32 MyNum = -1;             // +0x3C — МОЙ userNum (i16), пишется при заявке и приёме 5283
	int32 InitiatorNum = -1;      // +0x3E — userNum инициатора входящей (уходит в 5284)
	int32 WindowIndex = -1;       // +0x40 — индекс окна сервера (i32) из 5285
	int32 ExchangeType = -1;      // +0x44 — тип 0/1 (МИСНЕЙМ `craftMode`)
	int32 PartnerNum1 = -1;       // +0x46 — userNum стороны 1 (из 5283/5285)
	int32 PartnerNum2 = -1;       // +0x48 — userNum стороны 2
	int32 bMyLock = 0;            // +0x4C4 — моя готовность взведена (О3)
	int32 MenuTargetNum = -1;     // +0x4C6 — userNum цели меню (уходит в 5282)
	FString MenuTargetName;       // +0x4C8 — имя цели меню (слот [ss1] 20090/20091)
	int32 MenuTargetIdx = -1;     // +0x2700 — userIdx цели меню (гейт «не сам»; `invItemListDialogInit` не трогает)
	double RequestStartReal = 0.0;   // +0x2704 — старт 33-с таймера (с)
	int32 TimeoutTargetNum = -1;  // +0x2708 — userNum для 5286(−2)
	FString InitiatorName;        // +0x281C — имя инициатора входящей (из 5283)
	FString SideName1;            // +0x2830 — имя стороны 1 (из 5285)
	FString SideName2;            // +0x2844 — имя стороны 2 (из 5285)
	int32 ReadyCounter = 0;       // +0x26E0 — счётчик готовности 0..2 (О3)
	bool bMyLockLatch = false;    // латч +609 флажка 2048 (`sub_9350A2` после готовности; `uiSlotIconsReset` снимает) — событий нет
	bool bLockCounted = false;    // [ПОРТ] мой взвод увеличил счётчик +0x26E0 (ревизия О3 МЕЛК-3: откат снимает только его)

	// ── окно 2008: модель списков и лотков (О2; смещения эталона, `invItemListDialogInit` чистит всё) ──
	TArray<uint8> TrayCopy;       // +0x168 — 5 × 64: копия записи, положенной в лоток, + @60 i32 индекс в списке-источнике
	TArray<uint8> MyTray;         // +0x2A8 — мой лоток, 5 × 57 (`aLocalItemTemplate`)
	TArray<uint8> PartnerTray;    // +0x3C5 — лоток партнёра, 5 × 51 (заполняет 5292 — О3)
	TArray<uint8> List1;          // +0x515 — список-источник типа 1, 31 × 57 (`aSelectedSlotItem`)
	TArray<uint8> List0;          // +0xBFC — список-источник типа 0, 60 × 57 (МИСНЕЙМ «partnerItems»)
	int32 ScrollRow = 0;          // +0x26B4 — позиция колеса 2051 (ряд × 6 = первый индекс страницы)
	int32 CachedPage = -2;        // +0x26B8 — нарисованная страница (−1/−2 — перерисовать)
	int32 SelListCell = -1;       // +0x26BC — выбранная ячейка списка на экране (0…23)
	int32 SelTrayCell = -1;       // +0x26C4 — выбранная ячейка моего лотка (снятие 5290)
	int32 ListCount = -1;         // +0x26C8 — число записей списка-источника
	int32 SelCode = -1;           // +0x26CC — код выбранного предмета
	int32 SelSlotIndex = -1;      // +0x26D0 — индекс выбранного кандидата в списке-источнике
	int32 SelSource = -1;         // +0x26D4 — откуда перенос: 0 список, 1 мой лоток (МИСНЕЙМ `bLocked`)
	int32 SelectedQty = 0;        // +0x26D8 — количество (нумпад / 1 / вся стопка)
	int32 ActiveSlotIndex = -1;   // +0x26DC — ячейка лотка, куда кладём (уходит в 5288 @4)
	TArray<int32> SlotAccum;      // +0x26E4 — 5 × i32: подтверждённое сервером кол-во по ячейкам (5289, тип 0)

	// ── [ПОРТ, Д18] очередь 5288, ждущих ответа 5289 (ключ `asda.Trade.P2PFixDefects`; у эталона её нет) ──
	struct FPendingAdd
	{
		int32 Cell = -1;      // ячейка лотка (+0x26DC на момент отправки)
		int32 Kind = 0;       // вид отправителя 1…4 (`Exchange_SendAddItem`…`SendAddItem4`)
		int32 SrcIdx = -1;    // индекс-источник в списке (копия лотка +60 на момент отправки)
		int32 Qty = 0;        // +0x26D8 на момент отправки (докладка шлёт именно его)
		bool bStack = false;  // +0x30 — 5288 ушёл докладкой вида 2 (ветка накопителя 5289)
		int32 InvType = -1;   // контейнер записи лотка (5288 @5) — эхо 5289 @1 (ревизия О3 МЕЛК-1)
		int32 InvSlot = -1;   // ячейка контейнера (5288 @6) — эхо 5289 @2
	};
	TArray<FPendingAdd> AddQueue;   // голова — самый старый запрос (TCP сохраняет порядок ответов)
	int32 ReplySrcIdx = -1;         // индекс-источник записи, чей 5289 сейчас разбирается (−1 — брать по эталону)
	int32 ReplyKind = 0;            // вид 5288 этой записи (0 — неизвестен: ключ 0 / очередь пуста)

	// ── вне `CExchange` ──
	FString HotbarNoticeName;            // `CHotbar+1260` (`storeStringFieldByType(g_pQuickSlotBar, 7, имя)`) — текст вопроса 1520
	double IncomingGaugeStartReal = -1.0;   // `CMsgBoxMgr+0x3344` (`gap_3300[68]`): старт 30-с шкалы; −1 — не идёт

	// ── окно 2008: виджеты (привязки `CExchange_BuildUI`) ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> WinWidget = nullptr;          // корень WBP_TRAN_WIN (2008, +0x274C)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> CellImg;                   // 2013…2046 планы `TRAN_TRAYNN_<k>_0`
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> CellTxt;               // 2013…2046 надписи `TRAN_TRAYNN`
	UPROPERTY(Transient) TObjectPtr<UButton> BtnTopClose = nullptr;            // 2050 `TRAN_TOPCLOSE`
	UPROPERTY(Transient) TObjectPtr<UButton> BtnOk = nullptr;                  // 2011 `TRAN_OKBUT` (итог, О3)
	UPROPERTY(Transient) TObjectPtr<UWidget> OkGroup = nullptr;                // `TRAN_OKBUT_grp` — 2011 с подписью и значком (дети .JUI)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtOk = nullptr;               // подпись 2011 (6805)
	UPROPERTY(Transient) TObjectPtr<UButton> BtnCancel = nullptr;              // 2012 `TRAN_CANCELBUT`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCancel = nullptr;           // подпись 2012 (2289)
	UPROPERTY(Transient) TObjectPtr<UCheckBox> ChkMine = nullptr;              // 2048 `TRAN_BUT01` — моя готовность
	UPROPERTY(Transient) TObjectPtr<UCheckBox> ChkPartner = nullptr;           // 2049 `TRAN_BUT02` — индикатор партнёра (HitTestInvisible)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtChkMine = nullptr;          // подпись 2048 (13244)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtChkPartner = nullptr;       // подпись 2049 (13244)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTab01 = nullptr;            // 2056 `Tran_SlotWin01_Tab` (13245)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTab02 = nullptr;            // 2057 `Tran_SlotWin02_Tab` (13246)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGuide = nullptr;            // 2061 `Tran_GuideTxt` (2379)
	UPROPERTY(Transient) TObjectPtr<UWidget> Gear = nullptr;                   // 2058 `Tran_Gaer` (статичная шестерня)
	UPROPERTY(Transient) TObjectPtr<UWidget> AniGear = nullptr;                // 2059 `Tran_AniGaer` (анимированная)
	UPROPERTY(Transient) TObjectPtr<UWidget> TraySel = nullptr;                // 2047 `TRAN_TRAYSEL` — рамка выбранного кандидата
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> WinRelays;       // реле кнопок/галки окна (держат делегаты)
	UPROPERTY(Transient) FAsdaDragGhost DragGhost;                             // призрак переноса (общая точка)

	// ── окно 2008: состояние опроса мыши ──
	bool bWinReady = false;               // WBP разобран, привязки есть
	bool bWinShown = false;               // корень показан (зона окна читает по ссылке)
	int32 FoundControls = 0;              // привязок эталона найдено при разборе
	FAsdaMouseFrame Mouse;                // латч нажатий мыши окна
	FAsdaDragArm DragArm;                 // взвод переноса (порог — общая точка порта)
	int32 DragSrcIdNum = -1;              // контрол-источник идущего переноса
	int32 LastClickIdNum = -1;            // контрол предыдущего НАЖАТИЯ ЛКМ (0xD — второе нажатие по нему же, ревизия О2 МЕЛК-6)
	double LastClickTime = 0.0;           // время этого нажатия (с); окно двойного — `GetDoubleClickTime` Windows
	int32 TipIdNum = -1;                  // над каким контролом показана подсказка (`tooltipShown`)
	bool bGearHover = false;              // курсор над 2050 (шестерня анимирована)
	bool bDeathCloseDone = false;         // [ПОРТ] смерть при открытом окне уже обработана (раз на смерть)
	bool bDragFailed = false;             // порог переноса пройден, но гейт 5 отказал — отпускание не клик
	double DeathGraceStart = -1.0;        // [ПОРТ] обе стороны мертвы, я цель (+0x04 = 0): старт отсрочки своей «Отмены» (с)
#if !UE_BUILD_SHIPPING
	bool bDebugFailEnsure = false;        // самотест: притвориться, что WBP 2008 не загрузился (ревизия О2 МЕЛК-4)
#endif

	// ── приём: очередь пакетов (копит колбэк сети, разбирает `Tick`) ──
	struct FPendingPkt { int32 Opcode = 0; TArray<uint8> Body; };   // опкод и тело без шапки
	TAsdaNetQueue<FPendingPkt> Pending;    // пакеты 5283/5285/5287/5289/5292/5294/5295 в порядке прихода
	FDelegateHandle HMenuResult;           // подписка на `OnPlayerMenuResult`
	FDelegateHandle HNoticeClick;          // подписка на `OnNoticeIconClicked`
	bool bNetBound = false;                // подписка на сеть поставлена
	bool bHotbarBound = false;             // подписка на хотбар поставлена

	// ── след нотисов (пишет константный `Notice`) ──
	mutable int32 NoticeSeq = 0;           // счётчик напечатанных нотисов
	mutable int32 LastNoticeLoc = 0;       // loc-id последнего
	mutable FString LastNoticeSlot;        // слот [ss1] последнего
};
