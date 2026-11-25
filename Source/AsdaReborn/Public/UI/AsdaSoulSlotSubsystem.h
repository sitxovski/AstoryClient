// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaSoulSlotSubsystem — СЛОТЫ ДРУЖБЫ: запуск и завершение 30 слотов на персонаже. Визуальный слой состояний типа 1.
//
// ЭТАЛОН. На каждом персонаже 30 слотов: состояние `+56552` (dword), обработчик `+56582`, два хэндла эффекта `+113224` и `+113254` (word).
//   Заполняет `skillEffectInitAnimEventTable@0xAD34A0`: всем состояние 3 и пустой обработчик `sub_AD3D00`, первые 16 прошиты.
//   Запуск — `invSlotActivateAndDispatch@0xAD3630`: если состояние не 2, ставит 1 и зовёт обработчик. Его вызывают 5229
//   (`netRecvBuffStateApply@0xF50380`), снапшот 6154 (`entity_char.c:6046`) и `charReapplyActiveSkillEffects@0xA2BA10`.
//   Завершение — `CCharFx_SoulRite_EndSlot@0xAD3750`; его вызывают 5230, 5306 (все 30) и истечение записи (`charBuffUpdateExpired`, кроме слота 0).
//
//   Слот  | обработчик                                | что показывает
//   0     | SkillEffect_OnAnimEventSpawnSoulRecall    | дух (`AAsdaSoulBaseActor`), вспышка `soulritec-a_03`, S_c_SMSoulRecal02
//   1     | SkillEffect_OnAnimEventSpawnAttBombA      | `attbomh-a_02` на костях 8_End и _8_End, S_c_OddIndexSM001
//   2     | SkillEffect_OnAnimEventSpawnAttBomb       | `attbomh-a_04` на тех же костях, `attbomg-a_03`, S_c_OddIndexSM002b
//   3     | SkillEffect_OnAnimEventSpawnEvasion       | `evasiong-a_02`, постоянные `evasionc-a_03/_04`, S_c_OddIndexSM003a/b
//   4     | sub_AD4960                                | ничего, только состояние 2
//   5     | SkillEffect_OnAnimEventSpawnGuard         | `skigurdg-a_02`, `skigurdu-a_03` на кости 4, S_c_OddIndexSM005
//   6     | SkillEffect_OnAnimEventSpawnPercentDamage | `perdmgg-a_02`, `perdmgu-a_03` на кости 4, S_c_OddIndexSM006b
//   7     | CCharFx_RedMgu_Cast                       | `redmgu-a_01`, S_c_OddIndexSM007a
//   8     | CCharFx_SkigurdC_Cast                     | `skigurdc-a_04`, S_c_OddIndexSM008
//   9     | CharSkill_SoulRecallCast                  | нотис 8128 в плашку своему, `soulritec-a_01`, S_c_SMSoulRecal01
//   10    | charFxSpawnDamchgg                        | `damchgg-a_03`
//   11…15 | skillEffectStopRecallSound*               | S_c_SMSoulRecal02
//   16…29 | sub_AD3D00                                | ничего, состояние остаётся 1
//
// ОТКЛОНЕНИЕ. Обработчики 0 и 11…15 зовут `CEffectMgr_StopByOwner` со значением СОСТОЯНИЯ слота (1…3) вместо хэндла: эталон ищет эффект с
//   таким номером. Вызов не перенесён.
// КАРТА. У эталона объект своего персонажа переживает смену карты: состояния слотов остаются, дух создаётся заново
//   (`network.c:40567`), а загрузчик зовёт обработчики всех записей (`CCharacter_StopAllActiveBuffActions@0xA2C480`,
//   `game_main.c:3762`). Разовые эффекты повторяются, постоянные появляются заново, обработчики 0 и 11…15 молчат: состояние уже 2.
// ЧУЖИЕ. Запуск и конец идут для любого персонажа. Запись в список эталон на 5229 делает только своему, порт — всем (`AddType1`).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"
#include "AsdaSoulSlotSubsystem.generated.h"

class AAsdaEffectActor;
class AAsdaSoulBaseActor;
class USkeletalMeshComponent;

// Состояние слотов одного персонажа.
struct FAsdaSoulSlotChar
{
	int32 State[30];                            // 1 — запущен, 2 — действует, 3 — свободен (эталон `+56552`)
	TWeakObjectPtr<AAsdaEffectActor> Fx0[30];   // постоянный эффект (эталон `+113224`)
	TWeakObjectPtr<AAsdaEffectActor> Fx1[30];   // постоянный эффект (эталон `+113254`)
	TSet<int32> Pending;                        // слоты, ждущие появления актора носителя
	double PendingSinceS = 0.0;                 // с какого времени ждут
	TWeakObjectPtr<AAsdaSoulBaseActor> Soul;    // дух
	FAsdaSoulSlotChar() { for (int32& S : State) { S = 3; } }
};

UCLASS()
class ASDAREBORN_API UAsdaSoulSlotSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// Сброс сессии и смена карты.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;
	// Разбор ожидающих слотов и повтора после карты.
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaSoulSlotSubsystem, STATGROUP_Tickables); }
	// ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// `invSlotActivateAndDispatch@0xAD3630`.
	void ActivateSlot(int32 UserIdx, int32 Slot);
	// `CCharFx_SoulRite_EndSlot@0xAD3750`, визуальная часть. Нотис 8155 и снятие записи делает `UAsdaEffectsSubsystem`.
	void EndSlot(int32 UserIdx, int32 Slot);
	// `CCharFx_SoulRite_EndAllSlots@0xAD3CB0`.
	void EndAllSlots(int32 UserIdx);
	// Персонаж ушёл из мира (`entity_char.c:7076`): дух убирается молча, состояния уходят с объектом.
	void ForgetChar(int32 UserIdx);
	// Своя 4003 на новой карте: перенос состояний на новый номер. Зовётся после переноса списка эффектов.
	void OnSelfSpawnCarry(int32 NewSelf);

	// Состояние слота (3, если персонаж неизвестен).
	int32 GetSlotState(int32 UserIdx, int32 Slot) const;
	// Дух персонажа.
	AAsdaSoulBaseActor* FindSoul(int32 UserIdx) const;

	// Самотест: журнал показанного («fx:<база>», «snd:<имя>», «soul+», «soul-», «notice:<id>»).
	const TArray<FString>& DebugLog() const { return ShowLog; }
	// Самотест: очистить журнал.
	void DebugClearLog() { ShowLog.Reset(); }
	// Самотест: жив ли постоянный эффект слота (Which 0 или 1).
	bool DebugPersistentFxAlive(int32 UserIdx, int32 Slot, int32 Which) const;

private:
	// Вызвать обработчик слота. false — носителя ещё нет в мире.
	bool Dispatch(int32 UserIdx, FAsdaSoulSlotChar& C, int32 Slot);
	// Эффект на носителе: Attach — код режима эталона, Bone — кость или NAME_None, bPersistent — без срока.
	AAsdaEffectActor* SpawnFx(AActor* Host, USkeletalMeshComponent* Skel, const TCHAR* Jeb, int32 Attach, FName Bone, bool bPersistent);
	// Звук в точке носителя.
	void PlaySnd(AActor* Host, const TCHAR* Name);
	// Создать духа.
	void SpawnSoul(int32 UserIdx, FAsdaSoulSlotChar& C, AActor* Host, bool bBirthFlash);
	// Убрать духа сразу.
	void DestroySoul(FAsdaSoulSlotChar& C);
	// Погасить постоянные эффекты слота.
	static void StopSlotFx(FAsdaSoulSlotChar& C, int32 Slot);
	// Повтор обработчиков своих записей после смены карты.
	void ReplayAfterMap(int32 Self, FAsdaSoulSlotChar& C);
	// Запись в журнал самотеста; в Shipping пусто.
	void Note(const FString& What);
	// Номер своего персонажа.
	int32 SelfIdx() const;

	TMap<int32, FAsdaSoulSlotChar> Chars;   // номер персонажа → слоты
	int32 CarriedSelfIdx = INDEX_NONE;      // свой номер на прошлой карте
	bool bReplayPending = false;            // ждём своего актора на новой карте
	TArray<FString> ShowLog;                // журнал самотеста
};

// Включён ли запуск слотов (`asda.Soul.SlotRun`).
ASDAREBORN_API bool AsdaSoulSlotRunOn();
