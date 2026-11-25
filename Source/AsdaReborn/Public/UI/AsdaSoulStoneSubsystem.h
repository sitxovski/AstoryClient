// Copyright (c) 2026 @sitxovski (Discord)
// КАМЕНЬ ДУШИ (`itemSubType == 32`): привязка к слоту Дружбы 0 и банка опыта. Блок окна сумки эталона `+636…+655`.
//
// ПОТОК ЭТАЛОНА (06.11.2026, item_inventory.c, network.c, security.c, сервер `GameServer.c:59610-59870`, ветка 32 `CharSpendItemUse`):
//   1. Друг вне игры, сервер запускает слот 0 → `invSlotActivateAndDispatch@0xAD3630` ставит +637 и +640, своему — окно 8139.
//   2. Двойной щелчок по камню → `inventoryTryEquipSelectedItem@0xD22B90`:
//        активированного камня нет → `inventoryCheckGuardItemState@0xD25A30` (нотисы 8152, 8153, 8154) → окно 8157 → 5028;
//        активированный есть → `net_sendEquipItemRequest@0xD22D50` → 5419, нотис 8150.
//   3. 5029 успех → `invIsBagSlotActive@0xD25C20` → байт записи +50 = 3, значок `bind_soulstone`, эффект 10009, окно 9125.
//   4. Сервер копит опыт и шлёт 5425 → проценты в окне 9125.
//   5. Друг вошёл, 5419 → Другу 5421 (окно 20160 + 8131) → 5422 → просителю 5420: код 3 → окно 20161 → 5423 и 5028 (камень тратится);
//      коды 0, 1, 2, 4, 5, 6 → нотисы 8132, 8133, 8134, 8135, 8136, 8140. Другу приходит 5424, нотис 20162.
//   Без этой ветки 5028 по активированному камню списывает его на сервере, и накопленный опыт пропадает.
//
// Ключи: `asda.Soul.StoneBind` (1 — как эталон, 0 — прежний порт: камень сразу уходит в 5028, 5420…5425 не разбираются);
//   `asda.Soul.StoneSelfSlotOnly` (1 — флаги ставит только свой слот 0, 0 — слот любого персонажа, как оригинал).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AsdaSessionReset.h"
#include "AsdaSoulStoneSubsystem.generated.h"

class UTextBlock;
class UUserWidget;
class UWidget;

// `asda.Soul.StoneBind` для других файлов.
ASDAREBORN_API bool AsdaSoulStoneOn();

// Камень души: состояние окна сумки эталона и окно 9125.
UCLASS()
class ASDAREBORN_API UAsdaSoulStoneSubsystem : public UGameInstanceSubsystem, public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// Начальные значения блока.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Снять таймер показа окна.
	virtual void Deinitialize() override;
	// Смена карты: окно 9125 ушло с уровнем. Полный сброс: блок полей.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// Часть `invSlotActivateAndDispatch@0xAD3630`: `set_o27D`, `set_o280`.
	void OnSlot0Started(bool bSelf);
	// `uiDlg_ClearControlGroup@0xE6A770`: окно 9125 гаснет, текст стирается.
	void ClearGhost();
	// `sub_E65450`: панель Друга показана, окно 9125 гаснет.
	void HideGhostWindow();

	// `inventoryTryEquipSelectedItem@0xD22B90`: двойной щелчок по камню.
	void TryUseStone(int32 Container, int32 Cell);
	// `invIsBagSlotActive@0xD25C20`: можно ли принять активацию из 5029.
	bool IsBagSlotActive() const;
	// `sub_D25CF0@0xD25CF0`: сброс блока.
	void ResetBlock();
	// Хвост `inventorySlotSelectAndCopy@0xD25B70`: номер ячейки, эффект 10009, окно 9125.
	void OnStoneActivated(int32 Cell);
	// `inventoryFindEquippedType32Storage@0xD25ED0`: конец загрузки карты.
	void OnWorldReady();

	// Ответ на окно 8157: `item_sendUseSelectedSecure@0xD23C40`.
	void OnActivateConfirm(bool bYes);
	// Ответ на окно 20160 + 8131: `netSendItemMoveSecure@0xD23540`.
	void OnPartnerAskConfirm(bool bYes);
	// ОК в окне 20161: `antiCheat_SendIntegrityChallenge@0xD24470`.
	void OnSendConfirmOk();

	// 5420 `netRecvTradeStatusResult@0xD25110`: 14 байт.
	void OnRecvStatus5420(const uint8* B, int32 Len);
	// 5421 `cTrade_OnTargetInfoPacket@0xD24EC0`: 13 байт.
	void OnRecvPartnerAsk5421(const uint8* B, int32 Len);
	// 5424 `Recv_ExpGiftBuff@0xD254C0`: i64 подаренный опыт.
	void OnRecvExpGift5424(const uint8* B, int32 Len);
	// 5425 `recvCopyPos22Handler@0xD25840`: 22 байта.
	void OnRecvBar5425(const uint8* B, int32 Len);

	// Самотест: журнал показанного («chat:<id>», «box:<id>», «ask:<id>», «send:<опкод>», «ghost+», «ghost-», «fx»).
	const TArray<FString>& DebugLog() const { return ShowLog; }
	// Самотест: очистить журнал.
	void DebugClearLog() { ShowLog.Reset(); }
	// Самотест: флаг +637.
	bool DebugBound() const { return bBound; }
	// Самотест: флаг +640.
	bool DebugFlag640() const { return bFlag640; }
	// Самотест: ячейка +647.
	int32 DebugCell() const { return StoneCell; }
	// Самотест: проситель +648.
	int32 DebugRequester() const { return Requester; }
	// Самотест: флаг +636.
	bool DebugSendArmed() const { return bSendArmed; }
	// Самотест: текст процентов.
	const FString& DebugPercentText() const { return PercentText; }
	// Самотест: тело последнего 5419 или 5422.
	const TArray<uint8>& DebugLastBody() const { return LastBody; }
	// Самотест: ответ на окно 8157 ждёт нажатия для этой ячейки.
	int32 DebugPendingCell() const { return PendingCell; }
	// Самотест: привязаны ли узлы окна 9125.
	bool DebugGhostBound() const { return GhostTxt != nullptr && GhostName != nullptr && GhostTitle != nullptr; }
	// Самотест: показано ли окно 9125.
	bool DebugGhostVisible() const;
	// Самотест: последний текст 20162.
	const FString& DebugGiftText() const { return GiftText; }

private:
	// Поднять окно 9125 `WBP_SOULMATE_GHOST_WIN`.
	void EnsureGhost();
	// `uiProgressShowZero@0xE6A680`: показать окно 9125, если панель Друга скрыта.
	void ShowGhostZero();
	// Показать окно 9125 с текущими текстами; мир не готов — повторить через секунду.
	void ApplyGhost();
	// Записать проценты в окно 9125 (`uiCtrl_setPercentLabel@0xE6A7D0`).
	void SetGhostPercent(const FString& Text);
	// `inventoryCheckGuardItemState@0xD25A30`.
	bool CheckGuard();
	// `sub_D22C80@0xD22C80`: сброс просьбы и шкалы, привязка и ячейка остаются.
	void ResetRequest();
	// `net_sendEquipItemRequest@0xD22D50`: 5419.
	bool SendJarRequest();
	// `netSendItemMoveSecure@0xD23540`.
	bool SendPartnerAnswer(uint8 Mode);
	// `invBag_FindEquippedRideItemSlot@0xD263D0`: найти активированный камень, поставить +637 и +647.
	bool FindActivatedStone();
	// Индекс активированного камня в расходниках, −1 — нет (`inventoryHasEquippedItemOfType32@0xD25980`).
	int32 FindActivatedCell() const;
	// Номер пары, −1 — неизвестен.
	int32 BondIndex() const;
	// Друг в игре (`gap_8B4[6]`).
	bool PartnerOnline() const;
	// Состояние своего слота 0.
	int32 SelfSlot0State() const;
	// Нотис в чат золотым цветом эталона.
	void ChatNotice(int32 LocId);
	// Однокнопочное окно.
	void BoxNotice(int32 LocId, const FString& Text = FString());
	// Запись в журнал самотеста.
	void Note(const FString& What);

	bool bSendArmed = false;      // +636: сервер подтвердил передачу, ОК шлёт 5423
	bool bBound = false;          // +637: камень привязан к слоту 0 (`gap_9EC[8]`)
	bool bAsked = false;          // +639: Другу показан вопрос 5421; эталон только пишет и стирает
	bool bFlag640 = false;        // +640: ставится при старте слота 0; читателей в срезе нет
	int32 StoneCell = -1;         // +647: ячейка камня
	int32 Requester = -1;         // +648: userNum просителя из 5421
	uint8 ReqPos[11] = {};        // +649: 11 байт из 5421
	int32 PendingCell = -1;       // ячейка камня, по которой открыто окно 8157 (эталон +197)
	int32 PendingCode = 0;        // код предмета в этой ячейке
	FString PercentText;          // текст процентов окна 9125
	FString GiftText;             // последний текст 20162
	TArray<uint8> LastBody;       // тело последнего 5419 или 5422
	TArray<FString> ShowLog;      // журнал самотеста

	UPROPERTY(Transient) TObjectPtr<UUserWidget> GhostWnd = nullptr;   // окно 9125
	UPROPERTY(Transient) TObjectPtr<UTextBlock> GhostTxt = nullptr;    // 9127 проценты
	UPROPERTY(Transient) TObjectPtr<UTextBlock> GhostName = nullptr;   // 9134 имя Друга
	UPROPERTY(Transient) TObjectPtr<UTextBlock> GhostTitle = nullptr;  // 9135 заголовок, loc 8172
	UPROPERTY(Transient) TObjectPtr<UWidget> GhostIcon = nullptr;      // 9126 значок
	bool bGhostBound = false;     // окно поднято
	bool bGhostWanted = false;    // окно 9125 должно быть на экране
	FTimerHandle GhostRetry;      // повтор показа, пока идёт загрузка карты
};
