// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaCashGiftSubsystem — ★★★ P101_CashGift (01.11.2026): ПОДАРОК ИЗ КЭШ-МАГАЗИНА — общее состояние и сеть.
//   Проект — `Reports/cash_gift/PROJECT_P101.md` §5/§8 (утверждён владельцем), реверс клиента — `Reports/cash_gift/re/client.md`.
//
// ═══════════════ ЧТО ЭТО В ОРИГИНАЛЕ ══════════════════════════════════════════════════════════════════════════════
//   Состояние подарка живёт в объекте окна 6000 `g_pShopWnd` (0x2BE5730, `CShellItemStore`) и общее для ОБОИХ окон:
//   окно бустеров 15300 читает подтверждённое имя `byte_2BE811E` (= g_pShopWnd+10734) и маршрут `dword_2BE7C2E…`.
//     +9468 u8 режим витрины (=1)          +9470…+9483 маршрут получателя из 5073 (Channel, Portal, Gsidx, Threadidx, Job,
//     +10734 char[20] имя получателя         Userindex i32 @+9476, Charidx @+9480, Usernum i16 @+9482)
//     [2078] (+8312) ждём 5073   [2079] (+8316) ждём 5075   [2072] (+8288) «подарок отправлен»   +9508 копия 5078 (421 б)
//   Порт держит его ЗДЕСЬ, а окна (`UAsdaPremiumShopSubsystem`, `UAsdaBoosterShopSubsystem`) зовут отправителей и слушают события.
//
// ═══════════════ СЕТЬ (конверт вида A «с секретом», `SendSecured(bPad = true)`, как 5070) ══════════════════════════
//   C→S 5072 (20 б имени) · C→S 5074 (187 б, раскладка `BuildPresentBody`)
//   S→C 5073 (34 б) `netHandleCraftResultPacket@0xC337E0` · 5075 (6 б) `bankGoldResultPacket_Handle@0xC34F80`
//       5077 (61 б) `tradeOnItemBindResult@0xC346B0` · 5078 (421 б) `tradeOnConfirmPacket@0xC34950` + `Recv_PartyMember` case 3
//   Приём разбирается из Tick (закон `no-reentrant-net-callbacks`): записи в сумку — общая точка
//   `UAsdaInventorySubsystem::ApplyExternalRecord` (её контракт — звать не из сетевого колбэка).
//
// ═══════════════ НОТИСЫ (`re/client.md` §4; каналы — только `AsdaNotice`, своих нет) ═══════════════════════════════
//   5073: 0→6876 · 1→20070 [ss1] · 2→20040 [ss1] · 3→6879 · 4→6880 · 5→12946 (ONEBUT 1500)
//   5075: 0→6891 · 1→20071 [ss1] · 2→6892 · 3→6893 · 4→1216 · 5→6895 · 6→6896 (ONEBUT) · 5077 (только при [2072]): 0→6891 · 1→20071 · 2→6894
//   5078: на каждую вещь чат 20072 [ss1]=отправитель [ss2]=предмет (item_list 0xFF96FFD4 / special 0xFFFFD306, 10 с);
//         итог — значок 3 `mes_present`, звук 215, чат 5060 (0xFF78FF3C); клик по значку 3 → ONEBUT 6886 + сброс.
//   Гейты отправки 6000: 12919 (окно обмена открыто, `uiCanInteractWithTarget`), 12946 (себе), 12939, 6778 (мало GXP).
//
// ═══════════════ КЛЮЧИ ════════════════════════════════════════════════════════════════════════════════════════════
//   `asda.CashGift.Enable` (0) — ОБЩИЙ ВЫКЛЮЧАТЕЛЬ: пока на сервере нет P101, 5072/5074 не уходят и кнопки подарка скрыты.
//   [ИСПРАВЛЕНИЕ] дефекты оригинала (`re/client.md` §5), 1 = исправлено, 0 = как оригинал:
//     `asda.CashGift.FixK1` модалка 1559 из 15300 — в режиме подарка («Кому» 20196 + 12922), а не «купить?» 12923;
//     `asda.CashGift.FixK2` 5078 принимается и во время своей отправки ([2072]): иначе вещи невидимы до перезахода;
//     `asda.CashGift.FixK3` «себе» чистит ТО поле, в которое вводили (6345, если открыта панель 6333), а не всегда 6311;
//     `asda.CashGift.FixK5` 15300 проверяет «себе» (12946), «в полёте» ([2078]/[2079]), кэш (6778) и ставит [2072]/[2079].
//   Д-К6 (`re/client.md` §5: «сброс не чистит имя») — НЕ ДЕФЕКТ: `uiResetWindowState` → `uiDialogCloseReset` →
//     `uiPanelClearTextAndState` чистит +10734 (ui_core.c:27644 → 28876 → 29526); ключа нет.
//   Д-К4 (перевод loc 20070: код 1 = успех, а текст «не можешь») — исправление ТЕКСТА в DT_GameText/DT_Loc_GameText_en
//     (ведомости `Tools/data/gametext_overrides.json`, `Tools/data/loc/en/GameText.json`); откат — прежняя строка из `was`.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaCashGiftSubsystem.generated.h"

// Общий выключатель подарка `asda.CashGift.Enable` (аксессор — имя ключа в рабочем коде других файлов не пишется).
ASDAREBORN_API bool AsdaCashGiftOn();
// Ключ отката исправления Д-К<N> (N = 1, 2, 3, 5): true — исправление включено.
ASDAREBORN_API bool AsdaCashGiftFixOn(int32 N);
// ★ ОБЩИЙ ГЕЙТ отправителей окна 6000 (5070/5072/5074): порт `uiCanInteractWithTarget@0xC43310` (ui_core.c:28972-28982) ЦЕЛИКОМ.
//   false (нельзя), если: открыт магазин NPC 6800 (`this+10327` = `CShopWnd::m_bOpen`, ставит `shopWin_Open` shop_trade.c:3852,
//   снимает `shopWin_Close`); открыто окно обмена 2008 (`g_bPartyInviteFlag` = `CExchange+0x2C`, МИСНЕЙМ — PORT_PENDING №37 ①
//   опровергнут, стр. 3900); персонаж занят (`objStateAllowsRender(LocalPlayer)` = `UAsdaSystemOptionSubsystem::IsLocalPlayerBusy`);
//   открыт рюкзак (`sub_93248D(g_pInventoryWnd)` = `get_o84_7520` +0x210 — пара нотиса 5032 «Открыт рюкзак»). Чата не шлёт.
ASDAREBORN_API bool AsdaShopCanInteract(const UObject* Ctx);

// Кто отправляет (эталонные отправители различаются гейтами и источником имени).
enum class EAsdaGiftSource : uint8
{
	Shop6000,      // окно 6000: `netSendSecureChallengeMd5` / `net_sendItemActionRequest`
	Booster15300,  // окно 15300: `BoosterShop_SendBuy` / `BoosterShop_SendBuyPresent`
};

UCLASS()
class ASDAREBORN_API UAsdaCashGiftSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable: маршрут, имя, флаги ожидания и очередь приёма принадлежат СЕССИИ ──
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// Разбор очереди приёма и поздние подписки (FTickableGameObject).
	virtual void Tick(float DeltaTime) override;
	// Статистика тика.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaCashGiftSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// Длины тел (с шапкой конверта 24 б провод даёт 44 и 211).
	static constexpr int32 kNameBytes = 20;     // char[20] имени
	static constexpr int32 kPresentBody = 187;  // тело 5074
	static constexpr int32 kRouteBytes = 13;    // байты 5073 +1…+13 (маршрут получателя)
	static constexpr int32 kMaxPresentItems = 7;// позиций в теле 5074 (+1…+7 / +8…+35)

	// ═══ ОТПРАВКА 5072 «проверить имя» ═══
	//   Shop6000: порт `netSendSecureChallengeMd5@0xC42BC0` — гейт 12919, [2078] ≠ 0 → молча, пустое имя → молча; после — [2078] = 1.
	//   Booster15300: порт `BoosterShop_SendBuy@0xB841E0` — только пустое имя → молча ([ИСПРАВЛЕНИЕ] Д-К5: и гейты 6000 + «себе»).
	//   Ключ `asda.CashGift.Enable 0` — ничего не уходит (true не возвращается). true — пакет ушёл.
	bool SendCheckName(EAsdaGiftSource Src, const FString& Name);

	// ═══ ОТПРАВКА 5074 «подарить» ═══
	//   Shop6000: порт `net_sendItemActionRequest@0xC33C60` (одна позиция `ItemCode`, таблица по записи предмета; гейты 12919,
	//     [2079], пустое имя, нет записи, кэш `CashGxp` < цены → чат 6778; после — [2072] = 1, [2079] = 1).
	//   Booster15300: порт `BoosterShop_SendBuyPresent@0xB84840` — имя поля 15519 `FieldName` обязано совпасть с подтверждённым 5073,
	//     иначе ONEBUT 12939; `Qty` (≤ 7) одинаковых позиций `ItemCode`. ([ИСПРАВЛЕНИЕ] Д-К5: гейты 6000 и флаги.)
	//   true — пакет ушёл.
	//   ★ 06.11.2026: WireCode > 0 — код позиции в теле 5074, другой, чем ItemCode (коробка, которую выдаёт покупка карточки); гейты и цена — по ItemCode.
	bool SendPresent(EAsdaGiftSource Src, int32 ItemCode, int32 Qty, int32 CashGxp, const FString& FieldName = FString(), int32 WireCode = 0);

	// Собрать тело 5074 (187 б) по эталонному конструктору `sub_B852A0@0xB852A0` + заполнению отправителя.
	//   Позиции k < Qty: таблица (+1+k: 1 item_list / 0 special) и код (+8+4k); маршрут — сохранённый из 5073.
	void BuildPresentBody(int32 ItemCode, bool bTable1, int32 Qty, uint8 Out[kPresentBody]) const;

	// Имя совпадает с именем своего персонажа (`strncmp(name, CCharacter->name, 20)`)?
	bool IsSelfName(const FString& Name) const;
	// Свой персонаж известен (`CClass_GetField142C(&g_InventoryBag)` ≠ null — без него `uiConfirmPasswordMatch` молча возвращает 0).
	bool IsSelfKnown() const;

	// Порт `uiResetWindowState@0xC34570`: маршрут сброшен, [2072] = 0, модалка 1559 закрыта, панели 6296/6333 закрыты
	//   (через событие `OnWindowReset` — `uiDialogCloseReset`), поля и имя очищены (`uiPanelClearTextAndState`), копия 5078 = 0.
	void ResetWindowState();
	// Порт `uiPanelClearTextAndState@0xC46BA0`: очистить поля 6311/6345 (событием) и имя +10734.
	void ClearNameFieldsAndName();
	// Окно 6000 открыло панель 6296/6333 (`ItemTooltip_PopulateWindow@0xC40A10`, item_inventory.c:21250-21258): имя +10734 = 0,
	//   [2078] = 0, [2072] = 0, +9469 = 1.
	void OnShopPanelOpened();
	// Окно 6000: `uiDialogCloseReset@0xC42600` (отмена 6322/6356, ESC, успех 5071): [2078] = 0, [2072] = 0, поля и имя пустые.
	void OnShopDialogCloseReset();

	// ── Состояние для окон ──
	const FString& GetConfirmedName() const { return ConfirmedName; }   // +10734 (пусто — не подтверждено)
	bool IsWaitingCheck() const { return bWaitCheck; }                   // [2078]
	bool IsWaitingPresent() const { return bWaitPresent; }               // [2079]
	// ★ [ИСПРАВЛЕНИЕ] Д-К5 / ревизия С-2: снять флаги «в полёте», которые поставил 15300 (у эталона 15300 флагов нет, тайм-аута
	//   нет и у 6000; молчание сервера не должно запирать окно дольше жизни панели — закон no-state-latch-before-shown):
	//   показ/сброс панели 15515 снимает [2078]; закрытие окна 15300 (`bWindowClosed`) — ещё и [2079]/[2072].
	void OnBoosterPanelReset(bool bWindowClosed);
	bool IsPresentSent() const { return bPresentSent; }                  // [2072]

	// ── События для окон ──
	// Очистить поля имени окна 6000: `bBoth` — оба (6311 и 6345, `uiPanelClearTextAndState`), иначе только `FieldIdc` (6311/6345).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaGiftClearFields, bool /*bBoth*/, int32 /*FieldIdc*/);
	FOnAsdaGiftClearFields OnClearNameFields;
	// `uiResetWindowState` → окно 6000 закрывает панели 6296/6333 (`uiDialogCloseReset`).
	DECLARE_MULTICAST_DELEGATE(FOnAsdaGiftWindowReset);
	FOnAsdaGiftWindowReset OnWindowReset;

	// ═══ ДИАГНОСТИКА / САМОТЕСТЫ ═══
	// Разобрать очередь приёма СЕЙЧАС (то, что делает Tick) — самотест подаёт пакет через `DebugFeedPacket` и ждёт итог сразу.
	int32 DebugPump();
	// Подставить подтверждённое имя/маршрут (как после 5073 код 1) — проверка 5074 без приёма.
	void DebugSetConfirmed(const FString& Name, const uint8 Route13[kRouteBytes]);
	// Байты маршрута (для побайтной сверки самотестом).
	void DebugGetRoute(uint8 Out[kRouteBytes]) const { FMemory::Memcpy(Out, Route, kRouteBytes); }
	// Сбросить флаги ожидания (самотест между шагами).
	void DebugClearFlags() { bWaitCheck = false; bWaitPresent = false; bPresentSent = false; bCheckByBooster = bPresentByBooster = false; }
	// Взвести [2072] «подарок отправлен» (проверка Д-К2 без отправки).
	void DebugSetSent(bool bOn) { bPresentSent = bOn; }
	// Последний нотис-окно/чат (loc id) — самотест сверяет выбор строки по коду.
	int32 DebugLastLoc() const { return LastLocId; }
	// Текст последнего нотиса (с подставленными [ssN]) — самотест сверяет имя в 20071.
	const FString& DebugLastText() const { return LastText; }
	// Сколько записей 5078 последнего подарка положено в сумку.
	int32 DebugLastPlaced() const { return LastPlaced; }
	FString DumpState() const;

private:
	void OnNetPacket(int32 Opcode, const TArray<uint8>& Body);   // в очередь (без реакции в колбэке)
	void Handle5073(const TArray<uint8>& B);
	void Handle5075(const TArray<uint8>& B);
	void Handle5077(const TArray<uint8>& B);
	void Handle5078(const TArray<uint8>& B);
	void OnNoticeIcon(int32 Type);                                // значок 3 → `uiShowItemBuyTooltipNotice@0xC35330`
	bool CanInteract(bool bChat) const;                           // порт `uiCanInteractWithTarget@0xC43310` + чат 12919
	void MsgBoxLoc(int32 LocId, const FString& Slot1 = FString());// ONEBUT 1500 (строка с [ss1])
	void ChatLoc(int32 LocId, FColor Color, const FString& S1 = FString(), const FString& S2 = FString());
	void BindLate();                                              // подписки на сеть и хотбар (подсистемы поднимаются в любом порядке)

	// ── Состояние (`g_pShopWnd`) ──
	uint8 ShowcaseMode = 1;                 // +9468 (эталон держит 1: `cShopDialogResetState`, `uiResetWindowState`)
	uint8 Route[kRouteBytes] = {};          // +9470…+9482 ← тело 5073 +1…+13
	bool  bRoute = false;                   // маршрут сохранён кодом 1
	FString ConfirmedName;                  // +10734 (cp1251 → строка)
	bool  bWaitCheck = false;               // [2078]
	bool  bWaitPresent = false;             // [2079]
	bool  bPresentSent = false;             // [2072]
	TArray<uint8> LastGift;                 // +9508 копия тела 5078 (421 б)
	TArray<TPair<int32, TArray<uint8>>> Queue;   // принятые, но ещё не разобранные пакеты
	int32 LastLocId = 0;                    // последний показанный loc (диагностика)
	FString LastText;                       // его текст (диагностика)
	bool  bCheckByBooster = false;          // [2078] поставлен окном 15300 (Д-К5)
	bool  bPresentByBooster = false;        // [2079]/[2072] поставлены окном 15300 (Д-К5)
	int32 LastPlaced = 0;                   // сколько записей 5078 положено
	FDelegateHandle HNet;                   // подписка на `OnCashGiftPacket`
	FDelegateHandle HIcon;                  // подписка на клик значка хотбара
	bool bBound = false;                    // подписки стоят
};
