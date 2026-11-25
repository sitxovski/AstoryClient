// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaMobSkillSubsystem — НАВЫКИ МОБОВ (пакеты 8011 / 8012 / 8013). [ОРИГИНАЛ] перенос автомата клиента-эталона.
//
// ЭТАЛОН (полный разбор — `Reports/mob_skills/FLOW.md`):
//   8011 `netApplyCharActionState@0xF33F00` — начало каста: запись навыка в очередь моба, состояние 17;
//   8012 `NetHandler_CharActionUpdate@0xF33FC0` — результат: список целей (до 20 × 29 б), переход 17/16/15 по очереди;
//   8013 `Entity_ApplyHpRestore@0xF34500` — лечение моба (HP + эффект зелья).
//   Автомат — три тика сущности-моба: 17 пре-каст `charActionTick_State23@0xF7E870`, 16 каст `charState_UpdateCast@0xF7ECD0`,
//   15 атака `charExecuteAttackAction@0xF7F1F0`; снаряд — `charSkill_InitHitDamageFx@0xA50BE0` / `CharSkill_UpdateHitDamageFx@0xA510B0`.
//   Таблицы: эффекты навыка — DT_MonsterSkillEffect (Monster_SkillEffect.BIN), msif +43 / mst +6 / маска слотов — АВТОГЕН
//   `AsdaMobSkillTables.inl` (`Tools/gen_mob_skill_tables.py`).
//   Клип навыка: слот 18/19/20/21 → `anim_11/12/13/14` (`Effect_BuildJbbSuffixByType@0xF717C0`), только если слот загружен
//   (`mobEffect_LoadArchiveStep@0xF81B90`, флаги Mob list +0x9C/+0xAC) — `Reports/mob_skills/anim_slots.md`.
//
// Ключи: `asda.MobSkill` (1 — автомат навыков мобов; 0 — пакеты только в журнал, как порт до 30.10),
//   `asda.MobSkill.HitFxFix`, `asda.MobSkill.ChatFix`, `asda.MobSkill.ObserverChatFix` — исправления дефектов оригинала (см. .cpp).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaMobSkillSubsystem.generated.h"

class AAsdaMobActor;
class AAsdaProjectileActor;
class UDataTable;

UCLASS()
class ASDAREBORN_API UAsdaMobSkillSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// ── Приём (зовёт диспетчер `UAsdaNetSubsystem::HandlePacket`; Body — тело без 9-байтного заголовка пакета) ──
	void OnNet8011(const uint8* Body, int32 Len);   // начало каста навыка мобом
	void OnNet8012(const uint8* Body, int32 Len);   // результат навыка моба (цели, урон, флаги)
	void OnNet8013(const uint8* Body, int32 Len);   // лечение моба

	// ── Справка/самотест ──
	// Состояние автомата моба (15/16/17; 0 — не в навыке или моба нет).
	int32 DebugState(int32 Rid) const;
	// Длина очереди записей навыка у моба (эталон std::list ent+0x4928).
	int32 DebugQueueLen(int32 Rid) const;
	// Число летящих снарядов навыка у моба (эталон 3 слота ent+286·4).
	int32 DebugFlights(int32 Rid) const;
	// Журнал событий автомата для самотеста: «вид|rid|подробность» (кольцо 256 строк).
	const TArray<FString>& DebugEvents() const { return Events; }
	void DebugClearEvents() { Events.Reset(); }
	// Маска загруженных слотов навыка у типа моба (бит0 — слот 18 … бит3 — слот 21).
	static int32 SlotMaskForType(int32 MobType);
	// Клип файла для слота навыка (18→11, 19→12, 20→13, 21→14; иначе −1).
	static int32 ClipForSlot(int32 Slot);

	// ── USubsystem / FTickableGameObject / IAsdaSessionResettable ──
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaMobSkillSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickableWhenPaused() const override { return false; }
	virtual ETickableTickType GetTickableTickType() const override { return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Always; }
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

private:
	// Рантайм-запись навыка = клон записи Monster_SkillEffect (эталон объект 0x19C, `skillCloneEntryByIndex@0xF76900`).
	struct FEntry
	{
		int32 Skill = 0;              // +392 id навыка (= индекс записи MSE)
		FString Jeb[5];               // +0+32k .jeb фазы k (0 пре-каст, 1 каст, 2 атака, 3 полёт, 4 удар); стирается после спавна
		FString Bone[5];              // +160+32k кость (читается только Bone4)
		uint8 Flag[5] = {0, 0, 0, 0, 0};   // +352+k флаг фазы; у k=4 — режим точки удара снаряда 1/2/3
		int32 Attach[5] = {0, 0, 0, 0, 0}; // +360+4k режим привязки эффекта (0/8/14/15)
		float DelayA = 500.f;         // +380 мс — пуск снаряда (t_anim ≥ A)
		float DelayB = 1000.f;        // +384 мс — удар (t_anim ≥ B)
		float TimeoutMs = 5000.f;     // +388 мс — таймаут фаз 0/1 = mst[тип]+6 + 5000
		int32 TargetType = 0;         // +396 вид цели из пакета (1 — игрок, 0 — моб)
		int32 TargetIdx = -1;         // +400 цель из пакета
		int32 Marker = 0;             // +404 1 — идёт пре-каст, 2 — идёт каст
		bool bProjectile = false;     // +408 у навыка есть снаряд (Flag[3] != 0)
	};
	// Снаряд навыка (эталон слот 469 dword, `charSkill_InitHitDamageFx`).
	struct FFlight
	{
		TWeakObjectPtr<AAsdaProjectileActor> Proj;
		int32 Id = 0;                 // сквозной номер (прилёт находит запись по нему)
		int32 Skill = 0;              // +1796
		int32 MobRid = -1;            // +1792 renderId моба-кастера
		int32 TargetUserIdx = -1;     // +1780 userIdx цели
		int32 Damage = 0;             // +1784 урон первой цели
		int32 Flags = 0;              // +1868 флаги первой цели
		int32 HitMode = 0;            // +1872 режим точки удара (1 центр, 2 ноги, 3 верх)
		FString HitJeb;               // +8 .jeb удара по прилёте
	};
	// Рантайм моба (поля сущности эталона, которые трогает автомат).
	struct FMobRt
	{
		TArray<FEntry> Queue;         // ent+0x4928 — очередь записей (голова = [0])
		int32 State = 0;              // ent[120] ∈ {15,16,17}; 0 — автомат не ведёт
		int32 ClipSerial = -1;        // счётчик клипов актёра на момент нашего клипа (смена = моба перевели)
		int32 ClipSlot = -1;          // текущий слот клипа навыка (−1 — клип ещё не ставили в этом состоянии)
		float ClipLenMs = 0.f;        // длина клипа (0 — слот пуст: «клип кончился сразу»)
		float AnimMs = 0.f;           // ent[113] — время клипа/фазы
		float PhaseMs = 0.f;          // ent[1768] — таймер таймаута фаз 17/16
		TWeakObjectPtr<AActor> TrackedFx;   // ent[4672] — эффект фазы, гасится при смене состояния
		int32 TargetCount = 0;        // ent[1693]
		int32 TargetIdx[20];          // word 3389+i
		int32 TargetDmg[20];          // dword 1705+i
		int32 TargetFlags[20];        // word 3450+i
		TArray<FFlight> Flights;      // до 3 одновременно
		FMobRt() { FMemory::Memzero(TargetIdx); FMemory::Memzero(TargetDmg); FMemory::Memzero(TargetFlags); }
	};

	// Клон записи навыка (false — навыка/записи нет: эталон `gameSpawnTimedSlotEntry` вернул 0).
	bool CloneEntry(int32 Skill, int32 MobType, FEntry& Out) const;
	// `gameSpawnTimedSlotEntry@0xF8C100`: клон → в хвост очереди, цель/вид цели, таймаут.
	bool SpawnEntry(AAsdaMobActor* M, FMobRt& R, int32 Skill, int32 TargetType, int32 TargetIdx);
	// `CCharacter_SetActionState(mob, 15/16/17/6)` — общая часть: гашение эффекта фазы, сброс времени клипа.
	void SetState(AAsdaMobActor* M, FMobRt& R, int32 State);
	// Тики состояний.
	void TickPrecast(AAsdaMobActor* M, FMobRt& R, float DtMs);   // 17
	void TickCast(AAsdaMobActor* M, FMobRt& R, float DtMs);      // 16
	void TickAttack(AAsdaMobActor* M, FMobRt& R, float DtMs);    // 15
	// Клип слота навыка (один раз на состояние); true — клип конец (или слот пуст).
	bool DriveClip(AAsdaMobActor* M, FMobRt& R, int32 Slot, bool bLoop);
	// Эффект фазы 0/1/2 на мобе (матрица моба, привязка по режиму).
	AActor* SpawnMobFx(AAsdaMobActor* M, const FString& Jeb, int32 Attach, int32 Skill, const TCHAR* Phase);
	// Эффект удара (фаза 4) на игроке-цели.
	AActor* SpawnHitFx(AActor* Target, const FEntry& E, int32 Rid);
	// Пуск снаряда (фаза 3).
	void LaunchFlight(AAsdaMobActor* M, FMobRt& R, FEntry& E);
	// Прилёт снаряда (`CharSkill_UpdateHitDamageFx`, ветка `+4 == 1`).
	void OnFlightArrived(int32 Rid, int32 FlightId);
	// Цифры и строки чата по списку целей (ветка «площадь», entity_char.c:56160–56290).
	void ShowAreaTexts(AAsdaMobActor* M, FMobRt& R, const FEntry& E);
	// Строка чата по нотису (`[ss1]` — имя моба, `[sd1]` — урон).
	void ChatNotice(int32 NoticeId, const FString& MobName, int32 Damage, bool bWithNumber, const FColor& Col) const;
	// Пресет «пробуждение Кайи» (навык 1, `uiControlSpawnSelectEffect@0xF79660`).
	void PlayKaiyaRevive(int32 Rid);
	// Живой игрок по userIdx (эталон `CharManager_FindByUserIdx` + `(sub_93B218 & 1) == 0`).
	AActor* FindLivePlayer(int32 UserIdx) const;
	bool IsSelf(int32 UserIdx) const;
	void Event(const TCHAR* Kind, int32 Rid, const FString& Detail);
	const UDataTable* EffectTable() const;   // DT_MonsterSkillEffect (общий кэш `AsdaDt::Cached`)
	AAsdaMobActor* FindMob(int32 Rid) const;

	TMap<int32, FMobRt> Mobs;                                // по renderId моба
	int32 NextFlightId = 1;                                  // сквозной номер снаряда (связь прилёта с записью)
	TArray<FString> Events;                                  // журнал для самотеста
};
