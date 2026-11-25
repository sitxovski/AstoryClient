// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaArrowAmmoSubsystem — РАСХОД СТРЕЛ СЛОТА 10 НАДЕТОГО (пакет 5030). 31.10.2026, баг 10 владельца: в надетой ячейке
//   боеприпасов число не менялось при стрельбе. ПРИЧИНА: порт пакет 5030 не принимал вовсе (код был только в `NetOpcodes.h`),
//   поэтому ни уменьшения числа, ни подтяжки следующей пачки из сумки, ни остановки атаки при пустом колчане не было.
//
// ═════════════════ ★★★ ЧТО ЭТО В ОРИГИНАЛЕ ═════════════════════════════════════════════════════════════════════════
//   Сервер `sub_4D7FF0@0x4D7FF0` (GameServer.c:85464) после выстрела шлёт 5030 (18 б) — разметка тела (после заголовка):
//     @0  u8  код: 1 — выстрел учтён, 2 — стрел больше нет;
//     @1  i32 новое количество слота 10 (код 1; 0 ⇒ пачка кончилась и сервер переложил следующую);
//     @5  i16 текущий вес (u16 на проводе с P97);
//     @7  i16 ячейка РАСХОДНИКОВ (контейнер 2), откуда сервер взял новую пачку (код 1, количество 0);
//     @9  i16 0, @11 u8 2 (контейнер), @12 i32 количество новой пачки, @16 i16 — справочно, клиент их не читает.
//   Клиент `recvItemResultNotice@0xD61300` (network.c:21645), диспетчер network.c:1893 (`case 0x13A6`):
//     код 1 → `itemDropUpdateWeightBar@0xD61440` (item_inventory.c:50126):
//       • количество > 0 — запись слота 10 +9 = количество; `uiStatLabelUpdateCount@0xC6BF80` (ui_core.c:34101) пишет его
//         в контрол слота 10, если там предмет и слот не тащат (у порта надетое не тащится);
//       • количество ≤ 0 — хотбар, вес (@5), `slotItemDrop(3, 10)` (чат 20135 «[предмет] вынут», цвет −11514),
//         `inventoryApplySlotItem(3, 10, запись контейнера 2 из @7)` (чат 20134 «помещён», цвет −11514; +50 = 1 при
//         `tooltipFlag200 == 1`; `itemUI_ApplyEquipWithColor` при `flagWord176 == 0`), `slotItemDrop(2, @7)` (ячейка пуста);
//     код 2 → хотбар, `itemDropBuildLocalPlayerRecord@0xD61520` = `slotItemDrop(3, 10)` (чат 20135),
//       `CharFx_AutoSabzil_State(LocalPlayer, …, 0, 0, 0)` — действие персонажа в 0 (атака встаёт),
//       чат 2021 цветом −21248 (#FFAD00);
//     прочие коды — ничего.
//   Нумпада у пути нет.
//
// ═════════════════ ПОРТ ═════════════════════════════════════════════════════════════════════════════════════════════
//   Диспетчер `UAsdaNetSubsystem` (case 5030) только кладёт тело сюда (закон no-reentrant-net-callbacks) и ставит вес;
//   применение — в Tick штатными точками инвентаря (`ApplyExternalRecord`: модель + хотбар + перерисовка надписей).
//   «Действие в 0» — общая точка `AsdaAttackExit::StopSelfActionToIdle` (`CCharacter_SetAction` case 0: PvP — `StopPvpAttack`,
//   обычная атака — как клик по земле, стойка снята, слот показа навыка погашен, покой; было действие 5 ⇒ 4024/400).
//   Перед разбором 5030 прокручивается очередь пакетов инвентаря — порядок прихода как у эталона (ревизия 01.11).
//   [ПОРТ] Если в ячейке @7 пусто (рассинхрон модели), слот 10 только очищается — пустую запись не надеваем.
//   Откат — ключ `asda.Inven.Arrow5030 0` (пакет игнорируется, как до правки).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaNetQueue.h"       // TAsdaNetQueue — очередь тел 5030 (закон no-reentrant-net-callbacks)
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс очереди при разрыве/смене карты
#include "AsdaTickGate.h"   // ★ R4, доля a: шаблон класса не тикает (общая точка)
#include "AsdaArrowAmmoSubsystem.generated.h"

UCLASS()
class ASDAREBORN_API UAsdaArrowAmmoSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// Длина тела 5030 (эталон: `memcpy(v6, Src + 9, 0x12)`).
	static constexpr int32 kBodySize = 18;
	// Слот боеприпасов контейнера 3.
	static constexpr int32 kArrowSlot = 10;

	// Сброс очереди (соединение пересоздано — старые ответы не применяем).
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// Разбор очереди 5030.
	virtual void Tick(float DeltaTime) override;
	// Стат тикающего объекта.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaArrowAmmoSubsystem, STATGROUP_Tickables); }
	// ★ R4, доля a (14.11.2026): шаблон класса (CDO) не тикает — `AsdaTickGate.h`, ключ `asda.Perf.NoCdoTick`; рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	// В редакторе не тикаем.
	virtual bool IsTickableInEditor() const override { return false; }

	// Положить тело 5030 (из диспетчера сети; только очередь). false — ключ выключен, тело не принято.
	bool Push5030(const TArray<uint8>& Body18);
	// Применить накопленное сейчас (самотест: тот же разбор, что в Tick).
	void PumpPending();

	// Чат-нотисы (loc-id), выданные этой подсистемой по порядку (для самотеста `asda.SelfTest.EquipCellText`).
	const TArray<int32>& GetShownNotices() const { return ShownNotices; }
	// Забыть их (начало шага самотеста).
	void ClearShownNotices() { ShownNotices.Reset(); }
	// Сколько раз пакет «стрел нет» снимал атаку (для самотеста).
	int32 GetAttackStopCount() const { return AttackStops; }

private:
	// Разбор одного тела (все ветки шапки).
	void Apply(const TArray<uint8>& B);
	// `slotItemDrop(3, 10)`: чат 20135 с именем и очистка слота (штатной точкой с перерисовкой).
	void DropArrowSlot();
	// `inventoryApplySlotItem(3, 10, запись)`: чат 20134, предупреждение о гнёздах, запись в слот 10.
	void PutArrowSlot(TArray<uint8> Rec57);
	// `CharFx_AutoSabzil_State(…, 0)`: авто-атака снята, персонаж в покой.
	void StopOwnAction();
	// Чат-нотис с цветом обработчика (журнал `ShownNotices`).
	void ChatLoc(int32 LocId, const FString& Slot1, FColor Color);

	TAsdaNetQueue<TArray<uint8>> Pending;   // тела 5030 до разбора в Tick
	TArray<int32> ShownNotices;             // выданные чат-нотисы (последние 32)
	int32 AttackStops = 0;                  // снятий атаки кодом 2
};
