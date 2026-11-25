// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaDonationSubsystem — ★★★ «ПОЖЕРТВОВАНИЕ» (кнопка диалога NPC **30051** «Donation», NPC 163), волна 2-Б (06.10.2026).
//   Полный реверс — `Reports/owner_1006/wave2B_hunt_donation.md` §3.
//
// ПОТОК ЭТАЛОНА (1:1):
//   • `game_main.c:10217` `case 30051`: `uiOpenContextPopupWithHandlers(g_pNumPadDialog, CountItemById(60073), 6, 0)` —
//     НУМПАД РЕЖИМА 6 ПО ЦЕНТРУ ЭКРАНА; разговор остаётся открытым.
//   • ОК → `uiQtyDialog_sendUseItem60073@0xDA65D0`: `> 0` ⇒ `netSendGuarded_op6132_useItem60073(qty)`; отмена `sub_DA6640` —
//     только закрыть.
//   • Отправитель `netSendGuarded_op6132_useItem60073@0xD9F680`: запись = `invFindItemById(60073)` — ПОСЛЕДНЯЯ стопка
//     (цикл без `break`); общий счёт < qty ⇒ НЕ шлёт.
//   • Уход от NPC (`UI_resetAllWindowsAndLeave_5045`) закрывает открытый нумпад БЕЗ колбэка — `CloseNumPadOnNpcLeave`.
//   • Приём 6133 `onRecvRefineSlotUpdate@0xD9F8B0`: только код 1 — кол-во ≠ 0 ⇒ ячейке с этим слотом ставится кол-во
//     (`SlotTable_SetFieldById@0x99A560`), 0 ⇒ ячейка удаляется (`OnRecv_RemoveSlot`); перерисовка сумки; вес (в сети).
//     НОТИСОВ У ЭТАЛОНА НЕТ ни в одной ветке; сервер на «кол-во > стопки» не отвечает вовсе.
//
// [ПОРТ] Д3 (ключ `asda.Npc.DonationStackMax`, 1 — исправление): максимум нумпада у эталона — СУММА всех стопок 60073,
//   а уходит ПОСЛЕДНЯЯ стопка; сервер сверяет кол-во со стопкой и при превышении МОЛЧА ничего не делает (только лог
//   «iStackCount<=sItem.iAmount»). У игрока с двумя стопками часть ввода «пропадала» без ответа. Порт ограничивает
//   максимум размером той стопки, которая будет отправлена. 0 — как оригинал (сумма).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"      // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaNetQueue.h"   // ★ U7: TAsdaNetQueue — очередь пакетов из сетевого колбэка
#include "AsdaDonationSubsystem.generated.h"

UCLASS()
class ASDAREBORN_API UAsdaDonationSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// Сброс сессии: очередь 6133 и признак «наш нумпад открыт» (нумпад закрывает его владелец — подсистема окон).
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaDonationSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	static constexpr int32 kItemCode = 60073;   // «[Event] May Flower» — подтип 149 (сервер: `+27 == 149`)

	// Кнопка 30051: нумпад режима 6 по центру. true — нумпад открыт.
	bool OpenNumPad();
	// ОК нумпада режима 6 (`uiQtyDialog_sendUseItem60073`): `> 0` ⇒ отправка 6132. true — пакет ушёл.
	bool OnNumPadAccepted(int32 Qty);
	// Уход от NPC (`UI_resetAllWindowsAndLeave_5045`): закрыть НАШ нумпад без действия. true — закрыли.
	bool CloseNumPadOnNpcLeave();
	// Открыт ли наш нумпад (режим 6).
	bool IsNumPadOwned() const { return bNumPadOwned; }
	// Максимум нумпада по правилу эталона / [ПОРТ] Д3 (для самотеста и лога).
	int32 ComputeMax() const;
	// Разобрать очередь 6133 немедленно (самотест; в игре — Tick).
	void PumpPending() { DrainPending(); }

private:
	// Последняя стопка 60073 (порт `invFindItemById@0x999E50`: цикл без `break`). false — стопки нет.
	bool FindLastStack(int32& OutContainer, int32& OutCell, int32& OutUnits) const;
	void DrainPending();
	void OnResult6133(const TArray<uint8>& Body);   // `onRecvRefineSlotUpdate@0xD9F8B0`

	bool bNumPadOwned = false;                // наш нумпад режима 6 открыт
	TAsdaNetQueue<TArray<uint8>> PendingResults;     // очередь 6133 (закон `no-reentrant-net-callbacks`)
	FDelegateHandle HResult;                  // подписка на 6133
};
