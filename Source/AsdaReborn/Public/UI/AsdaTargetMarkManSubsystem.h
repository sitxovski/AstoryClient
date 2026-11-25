// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ `TARGETMARK_MAN` (JUI 1620) — СВОЯ ПЛАШКА НАД ГОЛОВОЙ ОРИГИНАЛА: показ/скрытие.
//   Волна 2-В (06.10.2026), отчёт `Reports/owner_1006/wave2C_duel_mark_banner.md` §2.
//
//   ЧТО ЭТО В ЭТАЛОНЕ (вопрос координатора «метка над целью при выборе игрока» — посылка НЕВЕРНА):
//     • `uiCharSelectInitControls@0x9915B0` (AsdaStory_game.c:29405-29548) кладёт 1620 в `g_InventoryBag+0x148C`
//       (`gap_1430[92]`), пишет в 1621 СВОЁ имя, в 1622 СВОЙ клан, в 1627 «звание ник», цвет имени
//       `uiSetItemSlotColorByGrade` и делает `SetVisible(1620, 1)`. Корень — окно 0×0 в ФИКСИРОВАННОЙ точке
//       экрана (Origin 526,259, Align «5»): камера держит своего персонажа в центре, плашка висит над головой.
//       Клон 1620 (`gap_1430[180]`) рисует ИМЕНА NPC (`NPC_DrawNameLabel@0x9951B0`); игроков рисует 1660 (MAN1).
//     • `hudShowTargetNameplate@0x994A60` — `SetVisible(1620, 1)` + `partyHideMemberControls`. Вызов ОДИН:
//       вход своего персонажа на карту/воскрешение (`GameNet_OnLocalPlayerMapEntry@0xF43390` — 4056,
//       `char_onLocalPlayerMapEntry@0xF49090` — 6025), под `g_bWorldActive == 1`.
//     • `hudHideTargetNameplate@0x994A20` — `SetVisible(1620, 0)`. Вызов ОДИН: смерть своего персонажа
//       (`CCharacter_SetAction`, entity_char.c:16907).
//   В ПОРТУ своя плашка — `UAsdaNameplateComponent` на своём павне (решение владельца 30.07: «всё, что на других, —
//   и на себе», раскладка MAN1); имя/клан на ней обновляются по 4105/4109 (`AsdaWorldSubsystem.cpp:6276-6335`).
//   Второе окно 1620 НЕ поднимаем — это была бы ДВОЙНАЯ плашка.
//   ★ Ревизия 2-В С-2: по закону владельца своя плашка ведёт себя КАК ЧУЖИЕ, а чужие плашки порта при смерти
//   не гаснут (ветки смерти в `UAsdaNameplateComponent` нет) ⇒ ключ `asda.Plate.SelfHideOnDeath` по умолчанию 0.
//   При ключе 1 — точный эталон: скрыть ТОЛЬКО на переходе в смерть «на глазах» (4053 → `CCharacter_SetAction` 6;
//   вход уже мёртвым через 4003/6001 плашку НЕ прячет), показать на КАЖДОМ входе своего персонажа по 4056 (4055 показа в эталоне нет;
//   6025 — ГМ-«поднять», отправитель-сирота, порт его не принимает) независимо от «мёртв»; состояние держится между павнами, как окно 1620.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"
#include "AsdaTickGate.h"   // ★ R4, доля a: шаблон класса не тикает (общая точка)
#include "AsdaTargetMarkManSubsystem.generated.h"

class UAsdaNameplateComponent;

UCLASS()
class ASDAREBORN_API UAsdaTargetMarkManSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;   // подписка на смерть и вход своего персонажа
	virtual void Deinitialize() override;                                      // отписка
	// ★ Ревизия 2-В (повторная, М-2): полный сброс сессии = `uiCharSelectInitControls` эталона (`SetVisible(1620, 1)`) — защёлка «скрыта» снимается.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;
	virtual void Tick(float DeltaTime) override;   // свести гейт своей плашки к защёлке эталона (при ключе 1)
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaTargetMarkManSubsystem, STATGROUP_Tickables); }
	// ★ R4, доля a (14.11.2026): шаблон класса (CDO) не тикает — `AsdaTickGate.h`, ключ `asda.Perf.NoCdoTick`; рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	/** Своя плашка сейчас скрыта этим правилом (`hudHideTargetNameplate`)? */
	bool IsSelfPlateHiddenByDeath() const { return bHiddenByUs; }
	/** Защёлка окна 1620 (true = скрыто) — для самотеста. */
	bool DebugHideLatch() const { return bHideLatch; }
	/** Плашка своего павна (nullptr — павна нет). */
	UAsdaNameplateComponent* FindSelfPlate() const;

private:
	void OnCharDeath(int32 UserIdx, int32 KillerIdx);       // 4053/вход мёртвым → `hudHideTargetNameplate` (только «на глазах»)
	void OnMapEntryFx(int32 UserIdx, bool bDeadArrival);   // ТОЛЬКО 4056 (`OnMapEntry4056`) → `hudShowTargetNameplate`
	FDelegateHandle DeathHandle;     // подписка на `OnCharDeath`
	FDelegateHandle EntryHandle;     // подписка на `OnMapEntryFx`
	bool bHideLatch  = false;        // видимость окна 1620 эталона: true = скрыто (`hudHide…`), false = показано
	bool bHiddenByUs = false;        // мы перевели гейт `bHideForLocalPlayer` в «скрыть»
	TWeakObjectPtr<UAsdaNameplateComponent> LastPlate;   // плашка, которой правили (новый павн — новая плашка)
};
