// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaFishAnimSubsystem — РЫБАЛКА: ПОКАЗ ДЕЙСТВИЙ, УДОЧКИ И РЫБЫ СВОЕГО ПЕРСОНАЖА И ЧУЖИХ (волна В5, 08.10.2026).
//   Реверс: `Reports/owner_1008/fishing_parts/world_anim.md` §2-§3, `client_net.md` §3; отчёт `fishing_impl_W5.md`.
//
//   Связывает автомат рыбалки (`UAsdaFishingSubsystem`, волна В1) с актором персонажа (`AAsdaPlayerCharacter`):
//     • своё действие (`charEmoteSabzilAction@0xFC5590` → `OnSelfFishAction`) → клип персонажа и клип удочки;
//     • свои поля `CCharacter[1].gap_A42` [15036]/[15066]/[15068]/[15076] каждый кадр → персонажу (он по ним вставляет
//       и снимает удочку/рыбу, как `LocalPlayer_UpdateRender@0x9D4170`, game.c:55024-55056);
//     • запись 6173 чужого (`charApplyStateFromList@0xFCAC60` → `OnPeerFishState`) → поля и действие чужого персонажа.
//   4025 (stateId 122…129 → действие 60…67) разбирает `UAsdaWorldSubsystem::OnCharAction` — прямо персонажу.
//   Сам показ (клипы, удочка, рыба, эффекты, гейты) живёт в персонаже — у эталона это поля и тики `CCharacter`.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaFishAnimSubsystem.generated.h"

struct FAsdaFishPeerState;

UCLASS()
class ASDAREBORN_API UAsdaFishAnimSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// Подписаться на автомат рыбалки (зависимость инициализируется раньше).
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Снять подписки.
	virtual void Deinitialize() override;
	// Каждый кадр: свои поля рыбалки → свой персонаж.
	virtual void Tick(float DeltaTime) override;
	// Стат тикера.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaFishAnimSubsystem, STATGROUP_Tickables); }
	// В редакторе не тикаем.
	// ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	// На паузе не тикаем.
	virtual bool IsTickableWhenPaused() const override { return false; }

	// Перенести свои поля рыбалки персонажу немедленно (самотест зовёт после шага автомата).
	void PushSelfFields();

private:
	// Своё действие сменилось (`charEmoteSabzilAction`): 60…67 — клип, 0 — состояние 0.
	void OnSelfAction(int32 Action);
	// Применена запись 6173 к персонажу (`charApplyStateFromList`).
	void OnPeerState(AActor* Char, const FAsdaFishPeerState& St);
	// Подписки поставлены.
	bool bBound = false;
	// Подписаться (Initialize; повтор безопасен).
	void Bind();
	// Хэндлы делегатов.
	FDelegateHandle HSelf, HPeer;
	// Персонаж, которому в прошлый кадр отдавались свои поля (смена павна — снять у прежнего признак «свой»).
	TWeakObjectPtr<class AAsdaPlayerCharacter> LastSelf;
};
