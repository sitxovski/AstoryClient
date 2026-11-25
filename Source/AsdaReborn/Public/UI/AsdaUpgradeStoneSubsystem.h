// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaUpgradeStoneSubsystem — ПЕРЕНОС УЛУЧШЕНИЯ КАМНЕМ («Оружие — камень улучшения» 401, «Доспех — камень
//   улучшения» 363 и прочие коды special_itemlist SubType 204 / 206). 31.10.2026, баг 4 от игроков: двойной щелчок по
//   камню и щелчок по оружию ничего не делали. ПРИЧИНА: в порту не было ни взвода камня, ни пакетов 6551/6552 —
//   двойной щелчок уходил в общее использование спец-предмета (5450), а сервер такой предмет так не применяет.
//
// ═════════════════ ★★★ ЧТО ЭТО В ОРИГИНАЛЕ (имена IDA врут — «colorMix» и «antiCheat» к делу не относятся) ═══════
//   Объект `g_pItemUpgradeWin` (класс окна 50500 `Strength_Part_Win` «Разделение улучшения»). Поля:
//     +428 (`this+107`) — указатель на запись СНАРЯЖЕНИЯ-цели   → `GearCell`;
//     +432 (`this+108`) — указатель на запись КАМНЯ              → `StoneCell`;
//     +436 — «взведён камень» (`get_o1B4@0xB65500`, переходник `sub_931AFB`) → `bPick`;
//     +437 — «6551 в полёте» (ставит отправка, снимает 6552)        → `bPending`.
//   Камни лежат в СУМКЕ снаряжения (контейнер 1, `g_InventoryBag`), поиск — по `special_itemlist`
//   (`CItemManager_GetTable2Record`): 204 — для оружия (`itemType == 9`), 206 — для доспехов и украшений
//   (`itemType <= 8`). Пустые камни 203/205 («Оружие — пустой камень улучшения» 400 и т. п.) — материал окна
//   разделения 50500, двойным щелчком не взводятся (у них обычное использование).
//
// ═════════════════ ПОТОК (все ветки эталона) ══════════════════════════════════════════════════════════════════
//   1. Двойной щелчок по ячейке СУМКИ (`CInventory_OnSlotUse@0xD07460`, ветка `m_nSelKind == 1`, item_inventory.c:
//      38087-38136), по порядку: гейт `sub_93723F` («ответ на прошлое действие ещё не пришёл») ⇒ молча; 104; 107;
//      удочка (подтверждение снасти) и снятие её взвода (`sub_9241EE`); затем:
//        • `special_itemlist.subType ∈ {204, 206}` → `colorMix_BeginPick@0xB65530`: запомнить камень, курсор 26
//          (P0.ani), `+436 = 1`. Возврат 0 — предмет НЕ используется;
//        • иначе, если камень взведён и у предмета есть запись `item_list`: подтипы 66/200/201 — молча (взвод
//          ОСТАЁТСЯ); прочее → `colorMix_OnItemPicked@0xB65670`: курсор 0, `+436 = 0`, и если пара допустима
//          (`itemIsValidColorMixPair@0xB65590`: 206 + `itemType <= 8` или 204 + `itemType == 9`) — цель запомнена,
//          TWOBUT (1, 69): текст 110322 с именем цели (`inventoryFormatItemNameToBuf@0xB65120`), кнопки 2288/2289
//          (item_inventory.c:51438-51451). Недопустимая пара — молча, взвод снят. Предмет НЕ надевается;
//        • иначе взвод снимается (`wnd_ResetCursorAndDrag@0xB65720` = `sub_938F54`), и щелчок идёт дальше обычным путём.
//      Взвод снимается ещё при открытии сумки (`uiOpenInventoryWindow@0xCFF300`, ui_core.c:46606).
//   2. «ОК» вопроса 69 (ui_core.c:57097) → `netSendAntiCheatChallengeResponse@0xB65D30` (network.c:8815): `+437`
//      занят ⇒ молча; цели нет ⇒ ONEBUT 108515; камня нет ⇒ ONEBUT 108516; иначе 6551 и `+437 = 1`. «Отмена» — ничего.
//   3. 6552 (`inventory_parseItemUpdatePacket@0xB65780`): `+437 = 0`; код 0 → 108530; 1 и 5 → вес, обе записи
//      (камень и вещь — `ApplyItemOptionTransRecords`), затем ONEBUT 110324 «[имя] [+N%]» (1) или 110323 (5), и в
//      обоих случаях следом ONEBUT 108531 «Перенос улучшения завершён»; 2 → 108532; 3 → 108533; 4 → 108534;
//      99 → 108535; иное — молча. Звуков и эффектов, кроме общих звуков окон-сообщений, у пути нет.
//   Сервер (`Recv_UserUpgradeBless@0x5358C0`, GameServer.c:118125): камень/вещь сверяются по коду (иначе 2/3), у камня
//   уровень +24 == 0 ⇒ 4; удача 60 % (`sub_5329A0(0,100) <= 60`) — вещи переходят уровень +24…+28 камня (и пересчёт
//   опции при уровне > 5); БД (`recvDBUpgradeBless@0x535FB0`) удаляет камень и отвечает 1 или 5.
//   Нумпада у пути нет (ни одного вызова в функциях выше).
//
// ═════════════════ ОТСТУПЛЕНИЯ ПОРТА ═══════════════════════════════════════════════════════════════════════════
//   Курсор взвода (ревизия 31.10, Б1/Б2): эталон ставит P0 взводом, а над миром курсор перебивает селектор
//     наведения (`sub_923BA4`); над СУМКОЙ P0 возвращает `invBagCursorColorMixCheck@0xB68890`. Ближе всего к этому —
//     захват курсора (`bUiCursorOverride`) ТОЛЬКО пока мышь над окном сумки (`INVEN_WIN`): над миром работают курсоры
//     боя/разговора, над сумкой — P0. Курсор отпускается лишь тем, кто его захватил (`bCursorOwned`) — чужие режимы
//     (самоцветы, миникарта) не затираются.
//   [ПОРТ] Взвод снимается и закрытием сумки: целью может быть только ячейка сумки, а эталон снимает взвод при
//     следующем открытии сумки (ui_core.c:46606) — для игрока это то же самое, но курсор не висит при закрытой сумке.
//   [ИСПРАВЛЕНИЕ] «6551 в полёте» без ответа снимается через 10 с (`asda.UpgradeStone.PendingTimeoutSec`, 0 — вечно, как эталон).
//   [ПОРТ] Взвод и «в полёте» снимаются сбросом сессии (разрыв, смена карты): соединение пересоздано, ответа не будет.
//   Откат — ключ `asda.UpgradeStone.Enable 0` (двойной щелчок по камню — прежнее общее использование).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaNetQueue.h"       // TAsdaNetQueue — очередь тел 6552 (закон no-reentrant-net-callbacks)
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaUpgradeStoneSubsystem.generated.h"

UCLASS()
class ASDAREBORN_API UAsdaUpgradeStoneSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// Подписка на 6552.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Отписка.
	virtual void Deinitialize() override;
	// Сброс взвода и «в полёте» (см. шапку, [ПОРТ]).
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// Разбор очереди 6552.
	virtual void Tick(float DeltaTime) override;
	// Стат тикающего объекта.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaUpgradeStoneSubsystem, STATGROUP_Tickables); }
	// В редакторе не тикаем.
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }

	// Камень переноса улучшения: `special_itemlist.SubType` 204 (оружие) или 206 (доспех).
	static bool IsTransferStoneSubType(int32 SubType) { return SubType == 204 || SubType == 206; }

	// ★ Развилка двойного щелчка по ячейке СУМКИ (контейнер 1) — шаг 1 шапки. true — щелчок поглощён (дальше
	//   обычный путь не идёт), bOutResult — что вернуть вызывающему. `bActionPending` — гейт `sub_93723F` сумки.
	bool OnBagDoubleClick(int32 CellIndex, bool bActionPending, bool& bOutResult);

	// Снять взвод (`wnd_ResetCursorAndDrag@0xB65720`): курсор 0, камня нет. Why — подпись журнала.
	void CancelPick(const TCHAR* Why);

	// «ОК» вопроса 69 — шаг 2 шапки (публично для самотеста и диагностики).
	void SendRequest();

	// Взведён ли камень (`+436`).
	bool IsPickActive() const { return bPick; }
	// Ждём ли 6552 (`+437`).
	bool IsPending() const { return bPending; }
	// Ячейка сумки камня (−1 — нет).
	int32 GetStoneCell() const { return StoneCell; }
	// Ячейка сумки цели (−1 — нет).
	int32 GetGearCell() const { return GearCell; }
	// Допустимость пары «камень — вещь» по кодам (`itemIsValidColorMixPair@0xB65590`).
	bool IsValidPair(int32 StoneCode, int32 GearCode) const;
	// Текст вопроса 110322 для цели (то, что увидит игрок; для самотеста).
	FString BuildConfirmText(int32 GearCode) const;
	// Состояние строкой (консоль `asda.UpgradeStone.Dump`, самотест).
	FString DumpState() const;
	// Тексты окон-сообщений, отданных этой подсистемой по порядку (для самотеста `asda.SelfTest.UpgradeStone`).
	const TArray<FString>& GetShownNotices() const { return ShownNotices; }
	// Забыть их (начало шага самотеста).
	void ClearShownNotices() { ShownNotices.Reset(); }

private:
	// Камень взвести (`colorMix_BeginPick@0xB65530`).
	void BeginPick(int32 CellIndex);
	// Выбрана цель (`colorMix_OnItemPicked@0xB65670`).
	void OnTargetPicked(int32 CellIndex);
	// Ответ 6552.
	void OnResult(const TArray<uint8>& Body);
	// Окно-сообщение ONEBUT 1500 с готовым текстом.
	void MsgBox(const FString& Text) const;
	// Курсор взвода: true — P0 и захват курсора; false — отпустить, ТОЛЬКО если захватывали мы (Б2).
	void SetPickCursor(bool bOn);
	// Покадрово: захват P0, пока мышь над сумкой; снятие взвода при закрытой сумке (Б1).
	void TickPickCursor();
	// Подстановка `%s` / `%d` / `%%` по порядку (эталонный `sprintf` над loc-строкой).
	static FString FormatC(const FString& Fmt, const FString& S, int32 D);

	bool  bPick     = false;   // `+436` — камень взведён
	bool  bPending  = false;   // `+437` — 6551 в полёте
	double PendingSince = 0.0; // когда ушёл 6551 (таймаут «в полёте», [ИСПРАВЛЕНИЕ])
	bool  bCursorOwned = false;   // курсор P0 и `bUiCursorOverride` поставлены этой подсистемой
	int32 StoneCell = -1;      // `+432` — ячейка сумки камня (эталон хранит указатель на ту же ячейку)
	int32 GearCell  = -1;      // `+428` — ячейка сумки цели
	TAsdaNetQueue<TArray<uint8>> PendingResults;   // тела 6552 до разбора в Tick
	FDelegateHandle HResult;                       // подписка на 6552
	mutable TArray<FString> ShownNotices;          // тексты ONEBUT по порядку (последние 16)
};
