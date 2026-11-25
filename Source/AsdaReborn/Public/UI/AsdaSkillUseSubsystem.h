// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillUseSubsystem.h — БОЕВОЙ ДИСПЕТЧЕР ПРИМЕНЕНИЯ НАВЫКОВ.
//
// ПОЛНЫЙ РЕВЕРС ПЕРЕД КОДОМ: `Reports/skill_use_system.md` (весь поток), плюс
//   `Reports/hotbar_inventory_drag.md` §14 (почему нажатие по навыку было тихой пустышкой).
//
// ЧТО ЭТО
//   Порт трёх эталонных функций и всего, что между ними:
//     `combatHandleSkillUse@0xE4B960`      — боевой навык  (`skill_info.sif +0x2A == 2`)
//     `skillUseOnTargetDispatch@0xE4E9F0`  — навык по цели (`+0x2A == 1`)
//     `skillCastActivate@0xE4D900`         — навык СОУЛМЕЙТА (пакет 5231)
//   плюс отложенное действие 102 (`charObject_SetSkillAction102@0xA28150` → следующий тик
//   `charUpdateActionStateTick` case 0x66), отправка **5256**, приём **5257 / 5270 / 5271**,
//   таблица 35 кодов отказа → нотисы, локальный предикт кулдауна и полоса применения.
//
// ★★★ ГЛАВНАЯ ПОПРАВКА К ПРЕЖНЕЙ ДОКУМЕНТАЦИИ (отчёт §2.2, §10)
//   `skill_info.sif +0x2A` (в порту — `FAsdaSkillDef::Kind`) — это НЕ «активный/пассивный»,
//   а **ДИСПЕТЧЕР ПУТИ ПРИМЕНЕНИЯ**:
//       1 → `skillUseOnTargetDispatch`   2 → `combatHandleSkillUse`   0 → навык не применяется.
//   Активность/пассивность даёт `+0x2C` (`SubType`/`iSkillMotionType`): 3 = пассив, 5 = тогл.
//   Раньше мост хотбара считал вид цели как `TargetSlotType == 6 ? 1 : 2` — это ДРУГОЕ ПОЛЕ (+0x34),
//   отсюда и «нажимаю навык — ничего не происходит» на половине навыков.
//
// ★★★ ПОРЯДОК ГЕЙТОВ 1:1 (`skill_buff.c:31546`+). Отказ = нотис в чат, пакет НЕ уходит:
//   1 удочка → 110707 | 2 авто-бур → 111204 | 3 верхом → 2038 | 4 нет записи / подтип 3|4 → МОЛЧА
//   5 кулдаун → 2203 | 6 не хватает ОМ → 1018 | 7 тот же навык уже кастуется (подтип ≠ 5) → 2203
//   8 оружие не подходит по маске (+0x83) → 1020 | 12 состояние ∈ {14,27,13,15,25,6,7,8,9} → МОЛЧА
//   ⚠ Проверка ОМ (`skillCheckLevelRequirement@0xE57DE0`) в эталоне названа «проверкой уровня» —
//     это ОШИБКА ИМЕНИ: сравнивается ТЕКУЩАЯ ОМ с `learned[5]` (стоимость ОМ текущего уровня).
//
// ★★★ ДАЛЬНОСТЬ (отчёт §3.1): вне дальности навык ДЕГРАДИРУЕТ В ОБЫЧНУЮ АТАКУ (подход 4006),
//   а не отменяется. Направление FPU-сравнения в декомпиле неоднозначно (`__SETP__`) — честная
//   дыра №4 отчёта; поведение вынесено в ключ `bOutOfRangeFallsBackToAttack`.
//
// ★★★ ПОЛОСА ПРИМЕНЕНИЯ ведётся отдельной подсистемой `UAsdaCastBarSubsystem`; здесь только
//   события «зажечь / погасить» — ровно там, где эталон пишет `effectSlots[100].field_4`.
//
// ЗАКОНЫ: no-reentrant-net-callbacks (сетевые колбэки только копят, работа в Tick),
//   charid-vs-usernum (в 5256 уходит i16 renderId/userIdx, НЕ charId), no-simplifications-stubs
//   (чего нет — честная запись в лог, а не тишина), code_dedup_audit_2 (нотисы — через
//   `UAsdaMessageSubsystem`, окна — через `UAsdaWindowSubsystem`, бары — через общий ClipFill).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaTickGate.h"   // ★ R4, доля a: шаблон класса не тикает (общая точка)
#include "AsdaSkillUseSubsystem.generated.h"

class UAsdaCastBarSubsystem;
class UAsdaNetSubsystem;
class UAsdaSkillDatabase;
class UAsdaSkillWindowSubsystem;
class UDataTable;
class AAsdaPlayerCharacter;
class AAsdaPlayerController;

// Разрешение «код навыка → клип каста → тайминги» (отчёт §1.7).
USTRUCT()
struct FAsdaSkillCastTiming
{
	GENERATED_BODY()

	UPROPERTY() FString ClipName;          // имя клипа из `combo time.bin` (без расширения)
	UPROPERTY() float   Combo0Ms = 0.f;    // кадр ПЕРВОГО удара, мс — МАКСИМУМ полосы применения
	UPROPERTY() float   FullMs = 0.f;      // полная длительность фазы 0, мс
	UPROPERTY() bool    bResolved = false; // false = источника нет (см. честную дыру №1 отчёта)
};

// Отложенное действие персонажа (эталон `char + 52121`). Порт нужных кодов.
UENUM()
enum class EAsdaCharAction : uint8
{
	None       = 0   UMETA(DisplayName = "100 — нет действия"),
	UseSkill   = 102 UMETA(DisplayName = "102 — применить навык"),
	SoulGuard  = 105 UMETA(DisplayName = "105 — навык Хранителя души"),
};

UCLASS()
class ASDAREBORN_API UAsdaSkillUseSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()
	// ★ 27.09.2026: самотест `asda.Move.SelfTest4007` (AsdaMove4007SelfTest.cpp) проверяет гейт бита 2 в `CheckGates`
	//   и в подходе ради навыка (`TickSkillApproach`) — нужен доступ к закрытым полям подхода и гейтам.
	friend struct FAsdaMove4007SelfTest;

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Окно, его защёлка «запрос в полёте» и полученные от сервера данные
	// принадлежат СЕССИИ и переживали разрыв связи вместе с GameInstance.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaSkillUseSubsystem, STATGROUP_Tickables); }
	// ★ R4, доля a (14.11.2026): шаблон класса (CDO) не тикает — `AsdaTickGate.h`, ключ `asda.Perf.NoCdoTick`; рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── ТОЧКИ ВХОДА ──────────────────────────────────────────────────────────────────────────────

	// Порт `combatHandleSkillUse@0xE4B960` — боевой навык (`Kind == 2`). SlotParam = индекс записи
	//   книги навыков, который эталон передаёт четвёртым аргументом (используется только в логе).
	UFUNCTION(BlueprintCallable, Category = "Asda|Навыки") bool UseCombatSkill(int32 SkillCode, int32 SlotParam = -1);

	// Порт `skillUseOnTargetDispatch@0xE4E9F0` — навык по выбранной цели (`Kind == 1`).
	UFUNCTION(BlueprintCallable, Category = "Asda|Навыки") bool UseTargetSkill(int32 SkillCode);

	// Порт `skillCastActivate@0xE4D900` → `netSkill_SendSoulmateUse@0xA07540` — навык СОУЛМЕЙТА (5231).
	UFUNCTION(BlueprintCallable, Category = "Asda|Навыки") bool CastSoulmateSkill(int32 SoulmateSkillId);

	// Порт `skillDispatchUseByBaseId@0xE57CB0` — применение из ОКНА НАВЫКОВ (клик по узлу дерева,
	//   `skillWndOnUICommand` case 0xD). Сам ветвится по `Kind`, как и хотбар.
	UFUNCTION(BlueprintCallable, Category = "Asda|Навыки") bool UseFromSkillWindow(int32 SkillCode);

	// Единая «дверь»: ветвление по `Kind` (+0x2A). Ею пользуются и хотбар, и окно навыков.
	UFUNCTION(BlueprintCallable, Category = "Asda|Навыки") bool DispatchByKind(int32 SkillCode, int32 SlotParam = -1);

	// ★★★ СКИЛЛ ХРАНИТЕЛЯ ДУШИ (1228..1254) — ТА ЖЕ «ДВЕРЬ».
	//   Эталонный `hudQuickSlot_TryUseSkill@0xEF4600` (ui_core.c:92688-92712) после СВОИХ гейтов
	//   (ряд доступен / тип карты ∉ {13,14,15,24,25,27} / нет эффекта 547) зовёт РОВНО тот же
	//   диспетчер, что и обычные навыки:
	//       if (skillinfo_GetByteFieldAt42(code) == 2) combatHandleSkillUse(code, 0);
	//       if (skillinfo_GetByteFieldAt42(code) == 1) skillUseOnTargetDispatch(code);
	//   Отдельного отправителя у Хранителя НЕТ: опкод 6158 выбирается внутри общих отправителей по
	//   `skillCode < 1201`. Поэтому подсистема Хранителя обязана ходить сюда, а не собирать заявку сама.
	UFUNCTION(BlueprintCallable, Category = "Asda|Навыки") bool UseGuardianSkill(int32 SkillCode);

	// Снять «активный навык» по ответу, который приходит НЕ опкодами 5257/5270/5271 (у Хранителя
	//   это 6159/6160/6161). Без этого гейт 7 держал бы ряд до истечения сторожа 2000 мс.
	void ClearActiveSkill(int32 SkillCode, const TCHAR* Why);

	// ★★★ ОТВЕТ СЕРВЕРА НА КАСТ ХРАНИТЕЛЯ (6159 успех / 6160 «долгий каст») = ПОЛНЫЙ АНАЛОГ
	//   5257 / 5270 и обязан делать РОВНО ТО ЖЕ: завести слот показа, поставить состояние
	//   действия (24 / 14), зажечь полосу применения и запустить показ (клип + эффекты).
	//   ДОКАЗАТЕЛЬСТВО, что каст хранителя проходит через ТЕ ЖЕ поля персонажа:
	//   реакция компаньона (`charVisibilityStateRefresh` → ветка атак,
	//   `soul_guardian_companion_re.md` §7.2) читает у ВЛАДЕЛЬЦА состояние **13/14/24/27** И
	//   `activeSlot != −1`, и только тогда берёт `SkillCoinTable[activeId] +16` → мотион 5/10/11.
	//   Значит на 6159 эталон ставит владельцу состояние 24 с ЗАВЕДЁННЫМ слотом — а полоса
	//   применения (`uiUpdateActiveSkillSlot@0xEDC480`) видна ровно при этих двух условиях.
	//   bIdle = ответ 6160 (состояние 14), иначе 6159 (состояние 24).
	// ★★★ КАСТЕР — ЛЮБОЙ (`CasterUserIdx`): 6159/6160 идут ТЕМ ЖЕ секторным броадкастом, что и
	//   5257/5270 (`SendPacket_ClippingSectorInAllUser@0x41E4F0`), и разбирает их у эталона ОДНА И
	//   ТА ЖЕ пара функций. Показ ставится любому найденному персонажу; полоса применения,
	//   `ActiveSkill`/`CharState` и сторож выхода — только своему (это локальный UI).
	void OnGuardianCastAccepted(int32 CasterUserIdx, int32 SkillCode, bool bIdle);

	// ★★★ ОТКАЗ/ПРЕРЫВАНИЕ КАСТА ХРАНИТЕЛЯ (6161) — гашение показа у ЛЮБОГО кастера.
	//   6161 и 5271 у эталона разбирает ОДНА функция `recvOtherCharSkillUse@0xF4D7D0`, и обнуление
	//   слота показа (`effectSlots[100].field_4 = -1`, network.c:44634) стоит СНАРУЖИ гейта «свой
	//   игрок». Публичная точка нужна подсистеме Хранителя: у неё свой канал 6161.
	void CancelSkillVisualFor(int32 CasterUserIdx, const TCHAR* Why);

	// Есть ли соулмейт (эталон `Hotbar_SlotActivate` case 4: `gap_878[47] == −1 || gap_8B4[6] != 1`
	//   ⇒ нотис 8162). В порту — соулмейт-стейт СВОЕГО персонажа из 5220/6152.
	UFUNCTION(BlueprintCallable, Category = "Asda|Навыки") bool HasSoulmate() const;

	// ★★★ ЧЕТВЁРТЫЙ КАНАЛ ЭФФЕКТОВ (навыки СОУЛМЕЙТА). Ремап `sub_ACEFC0@0xACEFC0`:
	//   `a1 < 30 → a1`; `a1 >= 100 → 0`; иначе `a1 + 1256` (0, если ≥ 0x54C).
	//   Обработчики кодов 1287…1300 регистрирует `charFxInitSoulSkillVtbl@0xACEE20` в ТЕ ЖЕ массивы
	//   k0/k5 таблицы диспатча, что и основной сайт (`skill_buff.c:17254-17255`).
	UFUNCTION(BlueprintCallable, Category = "Asda|Навыки")
	static int32 RemapSoulmateSkillId(int32 SoulmateSkillId);

	// Слот Type=4 хотбара хранит код навыка; эталон подбирает «лучший доступный уровень».
	//   В `SoulmateSkill.ssi` уровни — ОТДЕЛЬНЫЕ записи (Love-Love Power I/II/III и т.п.), номера
	//   уровня внутри записи нет ⇒ подменять нечего, возвращаем вход. См. отчёт §6.6(b).
	UFUNCTION(BlueprintCallable, Category = "Asda|Навыки") int32 FindBestSlotForLevel(int32 SkillCode) const;

	// ── Тайминги полосы применения ───────────────────────────────────────────────────────────────

	// Разрешить «навык → клип → COMBO0». Порядок источников описан в .cpp.
	FAsdaSkillCastTiming ResolveTiming(int32 SkillCode) const;
	// Ручная привязка навыка к клипу (консоль `asda.Skill.SetClip`) — закрывает честную дыру №1
	//   отчёта до тех пор, пока таблица хардкод-загрузчиков не снята с бинаря.
	void SetSkillClip(int32 SkillCode, const FString& ClipName);

	// ── Дампы ────────────────────────────────────────────────────────────────────────────────────
	FString DumpState() const;
	FString DumpSkill(int32 SkillCode) const;

	// ── Эмуляция сервера (проверка без сервера) ──────────────────────────────────────────────────
	void SimulateCastResult5257(int32 Result, int32 SkillCode);
	void SimulateCastIdle5270(int32 SkillCode);
	void SimulateCastEnd5271(int32 Result, int32 SkillCode);

	// ── Ключи поведения ──────────────────────────────────────────────────────────────────────────

	// ★★★ УДАЛЕНО 2026-08-11: `DispatchThrottleMs` / `LastDispatchS`.
	//   Эталонный анти-спам применения навыка (`timeGetTime() − g_dwLastSkillDispatchTime > 0xC8`)
	//   существует РОВНО В ОДНОМ месте — `Hotbar_SlotActivate` case 3 (`ui_core.c:67441`;
	//   в бинаре обе ссылки на VA `0x2C30500` лежат внутри этой функции). В порту он живёт там же —
	//   `UAsdaHotbarSubsystem::SkillThrottleMs`. Второй, «общий» таймер здесь был НЕэталонным и
	//   молча съедал нажатия из окна навыков, ряда соулмейта и ряда Хранителя.

	// Вне дальности эталон уходит в обычную атаку (подход 4006), а не отказывает.
	//   Направление FPU-сравнения в декомпиле неоднозначно (дыра №4 отчёта) — ключ на случай,
	//   если живая проверка покажет обратное.
	UPROPERTY(EditAnywhere, Category = "Asda|Навыки") bool bOutOfRangeFallsBackToAttack = true;

	// ★★★ КУЛДАУН ВЗВОДИТСЯ ПО ОТВЕТУ 5271 (result == 1), А НЕ ПРИ ОТПРАВКЕ.
	//   Эталон `recvOtherCharSkillUse@0xF4D7D0` (network.c:44622-44624): только у локального игрока
	//   и только при коде 1 зовётся `sub_9216EC` → `skillStartGroupCooldown@0xE57860`.
	//   Прежний предикт «на отправке» рисовал перезарядку даже когда сервер молчал, и именно он
	//   создавал ложное впечатление «навык применился» (живой тест 2026-08-09).
	//   ★ 25.10.2026 (ревизия 1.1): `ArrivalSeq` — номер прихода 5271; взвод пропускается, если после него пришли 5465/5274.
	void StartCooldown(int32 SkillCode, uint64 ArrivalSeq = 0);

	// ★★★ Д1 (исправлено 2026-08-11). ИМЯ `Target_GetSelectedUserIdx@0x9254E5` — **МИСНЕЙМ**, и
	//   прежний комментарий здесь («= поле выбранной цели персонажа») был НЕВЕРЕН и уже один раз
	//   ломал систему. Это thunk на `Char_GetHoveredUserIdx@0x966490` (`entity_char.c:110-114`):
	//       __int16 __thiscall Char_GetUserNum(_WORD *this) { return *(this + 95094); }  // байт +190188
	//   Поле `+190188` — **СОБСТВЕННЫЙ `userNum` ПЕРЕДАННОГО персонажа**, а не «выбранная цель»:
	//   по нему идёт поиск в хеш-таблице `CharManager_FindByUserIdx@0x9D1240` (`entity_char.c:6270-6283`,
	//   `*(u16*)(entity + 190188) == a2`). Значит `Target_GetSelectedUserIdx(локальныйИгрок)` = МОЙ
	//   номер (так и работает само-каст), а `Target_GetSelectedUserIdx(pTargetChar)` = номер ЦЕЛИ,
	//   потому что передан объект цели.
	//   Функция НИЖЕ — не порт того геттера: это выбор цели в порту (`AAsdaPlayerController::TargetActor`),
	//   СЕССИОННЫЙ индекс выбранной цели — renderId моба ЛИБО userIdx игрока (закон charid-vs-usernum). Публично, потому что тем же значением заполняют
	//   hit-запись ВСЕ отправители применения навыка, включая 6158 Хранителя души
	//   (`skillCastSelfSendPacket@0x9FE210`, skill_buff.c:2222-2223).
	//   OutType: 1 = цель ПЕРСОНАЖ, 0 = МОБ. false = цели нет (OutIdx = −1).
	bool GetSelectedTarget(int32& OutType, int32& OutIdx, FVector2D& OutGamePos, float& OutDistU) const;

	// ★★★ «ПОДБЕЖАТЬ И ПРИМЕНИТЬ» (эталон `Player_UpdateAutoActionState@0xA249A0`, case 0x66,
	//   entity_char.c:24845-24878). Состояние 102 живёт, пока цель вне дальности; каждый тик:
	//     distSq = Math_DistanceSquared3D(цель, я);  rangeSq = effectSlots[слот].soundId
	//                                                        = ((бонус + rec[+0x35]) · 100)²
	//     вне дальности → `Combat_SendNormalAttack_thunk(...)` = ПОДХОД (4006 с renderId цели);
	//     в дальности   → стоп (`Net_SendActionPacket` = 4008, если движемся) + заявка 5256.
	//   Отмена: цель потеряна/сменилась/умерла, новый приказ движения, смерть, таймаут.
	void CancelSkillApproach(const TCHAR* Why);
	bool HasSkillApproach() const { return ApproachSkill >= 0; }
	// ★ 15.10.2026 (ритм атаки): состояние действия своего персонажа (25 — заявка ушла, 24 — показ, 14 — накопление;
	//   0 — свободен). Боевой тик не возобновляет замах, пока навык занят (эталон: тик атаки идёт только в состоянии 5).
	int32 GetSelfCharState() const { return CharState; }
	// ★ 15.10.2026 (ревизия НИЗК-2): активный навык (−1 — нет). Состояние 25/24/14 без активного навыка (соулмейт 5231) не
	//   держит боевой тик.
	int32 GetActiveSkill() const { return ActiveSkill; }
	// ★ R5-fix8 (14.11.2026): у кастера в очереди сети стоит успешный 5257 либо 5270, ещё не разобранный тиком (показ навыка не
	//   запущен). OutSkill — код этого навыка. Читает `UAsdaSkillFxSubsystem::ShelveHitForQueuedCast`.
	bool HasQueuedCast(int32 CasterUserIdx, int32& OutSkill) const;
	// ★ 31.10.2026 (баг 7): сколько раз предохранитель `TickLatchGuard` снял вечную защёлку (самотест `asda.Skill.SelfTestLatch`).
	int32 GetLatchGuardFired() const { return LatchGuardFired; }
	// ★ 31.10.2026 (баг 7, ревизия п.5): сколько Warning «серия молчаливых отказов» записано (самотест `asda.Skill.SelfTestLatch`).
	int32 GetSilentRefuseWarns() const { return SilentRefuseWarns; }
	// ★ 18.10.2026 (ревизия Ф4, мелочь 8): эталонный флаг PvP-режима `buffFxSlots[160]` в диспетчере навыков (читает самотест PvP:
	//   штатный отбой 4206 обязан его снять).
	bool IsSelfPvpModeFlag() const { return bSelfPvpMode; }
	// ★ Только самотест ритма атаки (`asda.Combat.SelfTestRhythm`): выставить состояние действия и активный навык так, как
	//   их ставят заявка (25) и ответ сервера (24) — без сервера. State 0 / Skill −1 — сброс.
	void DebugSetCastState(int32 State, int32 Skill) { CharState = State; ActiveSkill = Skill; ActiveSkillSentS = 0.0; }
	// ★ 06.11.2026 только самотест (вне Shipping): снять гейт 8 «оружие по маске» — перебрать навыки всех классов одним персонажем.
	bool bDebugSkipWeaponGate = false;
	// Гейт 2 «надет авто-бур» = запись надетого оружия ItemType 10 / ItemSubType 2
	//   (эталон `playerHasEquippedItemType10Sub2@0xFE9A40`; закрыт 2026-08-10).
	// ★ Волна В2 ремесла (25.09.2026): ПУБЛИЧНО — тот же предикат читает гейт окна ремесла
	//   `tileInteractGateAndOpen@0xFDD1A0` (ui_core.c:100527, «лопата в руке» → молча). Второй копии не заводим.
	bool IsAutoShovelEquipped() const;

private:
	// ── Гейты ────────────────────────────────────────────────────────────────────────────────────
	// Общий блок гейтов эталона. Возвращает true = можно применять.
	bool CheckGates(int32 SkillCode, const struct FAsdaSkillDef& D, bool bTargetPath);
	bool IsFishingRodEquipped() const;
	// (Гейт 2 «надет авто-бур» `IsAutoShovelEquipped` — перенесён в публичную секцию, волна В2 ремесла.)
	bool IsWeaponAllowed(const struct FAsdaSkillDef& D, int32& OutWeaponType) const;
	int32 GetMpCost(int32 SkillCode, const struct FAsdaSkillDef& D) const;
	int32 GetCurrentMp() const;

	// ── Отложенное действие 102 и отправка ───────────────────────────────────────────────────────
	void QueueSkillAction102(int32 SkillCode);
	void FlushPendingAction();
	bool SendUse(int32 SkillCode, const struct FAsdaSkillDef& D);

	// ★ Дальность применения в игровых единицах: `(.sif +0x35) · 100` (эталон
	//   `skillCalcAbilityBonus@0xA870B0`, skill_buff.c:19225: `(бонус + rec[53]) · 100`, результат —
	//   уже КВАДРАТ, сравнивается с квадратом расстояния). Бонус способностей (пассивы 718/960,
	//   эффекты 107/242/244) в порту не заведён — берём голое поле записи.
	float SkillRangeUnits(const struct FAsdaSkillDef& D) const;
	// Начать подход к цели ради навыка (состояние 102 держится, каст — по прибытии).
	void BeginSkillApproach(int32 SkillCode, float RangeU, int32 TType, int32 TIdx);
	// Тик подхода: пересчёт дальности, продление маршрута, стоп + каст по прибытии.
	void TickSkillApproach();

	// Стоп ПЕРЕД заявкой (эталон: `if (getFlag_47551(me)) Net_SendActionPacket(...)` — 4008).
	//   Без него серверный флаг движения `CUser+51` остаётся взведённым и `RecvChar_SkillUse`
	//   (GameServer.c:45973) отбивает навык кодом 24 «Сначала нужно остановиться».
	void StopBeforeCast(int32 SkillCode);

	// Ветка `castType == 0`: само-каст (`resourceType` 0/1) либо площадь вокруг себя (2/3).
	//   Цель НЕ читается — это структура эталона, а не упрощение (см. комментарий у реализации).
	bool SendUseSelfOrArea(int32 SkillCode, const struct FAsdaSkillDef& D, const FVector2D& SelfGamePos);

	// ★★★ ПЛОЩАДНОЙ СБОР ЦЕЛЕЙ — общая часть всех пяти площадных отправителей эталона
	//   (`skillCastAreaSendPacket@0xA00570`, `Skill_SendAreaSkillTargetsCast@0xA01140`,
	//    `Skill_SendAreaSkillCast@0xA01DA0`, `skillCastAreaTargetsAndSend@0xA12800`).
	//   Все они строят ОДИН И ТОТ ЖЕ бокс: тайл ЦЕНТРА ± `.sif +0x37` (`AoeRadius`, В ТАЙЛАХ),
	//   углы переводятся обратно в мир (`CMap_GetTileAt` отдаёт ЦЕНТР ячейки) и расширяются на ±50
	//   игровых единиц. Отсюда полу-размер `R·100 + 50` вокруг ЦЕНТРА ТАЙЛА центра области
	//   (`index·100 + 50`, `world_map.c:12567`), а не вокруг его НАЧАЛА и не вокруг точной позиции.
	//   `CenterGamePos` — позиция центра области: своя (площадь вокруг себя) либо ЦЕЛИ.
	//   Возвращает полу-размер бокса и координаты его центра в игровых единицах.
	void AreaBoxFromTile(const FVector2D& CenterGamePos, int32 RadiusTiles,
		FVector2D& OutCenter, float& OutHalf) const;

	// ★★ Радиус площади в ТАЙЛАХ. Обычно `.sif +0x37` применяемого навыка; но если у персонажа висит
	//   эффект 132 и код навыка ≤ 1200, `skillCastAreaSendPacket@0xA00570` (skill_buff.c:2893-2900)
	//   берёт радиус ИЗ ЗАПИСИ НАВЫКА 132 (`CSkillMgr_GetSkillRecord(0x84)`).
	//   ★★ `bEffect132Substitution` — включать ТОЛЬКО в ветке «площадь вокруг цели-моба»: греп `0x84u`
	//   по всему эталону даёт единственное «навыковое» вхождение — `skill_buff.c:2897`, внутри
	//   `skillCastAreaSendPacket`. Три других площадных отправителя подмены НЕ делают.
	int32 AreaRadiusTiles(int32 SkillCode, const struct FAsdaSkillDef& D,
		bool bEffect132Substitution = false) const;

	// ★★ Эффект 132 у СВОЕГО персонажа — второй переключатель площадной ветки
	//   (`CList_FindByTypeSub(&char->gap_96A[128], 132, 0)`; combat_pvp.c:7194/7210/7302/7429/7464/7569).
	bool HasAreaEffect132() const;

	// ★★★ `skillCastAreaTargetsAndSend@0xA12800` — СМЕШАННАЯ площадь (сущности + персонажи).
	//   `TargetFlags` эталона: бит0 = цель-СУЩНОСТЬ, бит3 (8) = цель-ПЕРСОНАЖ (это `char[1].gap_0[2]`,
	//   та же маска, что тик каста кладёт в `+104193`). Записи ПЕРСОНАЖЕЙ добавляются ТОЛЬКО в
	//   PvP-бою (`buffFxSlots[160] == 1`, у нас `bSelfPvpMode`).
	bool SendUseAreaMixed(int32 SkillCode, const struct FAsdaSkillDef& D, int32 TargetFlags,
		int32 TargetIdx, int32 TargetKindSlot);

	// ── Сеть (колбэки только копят — закон no-reentrant-net-callbacks) ───────────────────────────
	void OnNetCastResult(int32 Result, int32 CasterUserIdx, int32 SkillCode, int32 Level, int32 HitCount);
	void OnNetCastIdle(int32 CasterUserIdx, int32 SkillCode, int32 Level, int32 HitCount);
	void OnNetCastState(int32 Result, int32 UserIdx, int32 SkillId, int32 Flag, bool bGuardian);
	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (баг 7 «навыки перестают работать»): 5232 — ответ сервера на навык соулмейта 5231
	//   (эталон `net_handleSoulmateSkillState`, network.c:45393: отказ → `CChar_SetState(кастер, 0)`, успех → 24). Снимает
	//   состояние 25, которое ставит `CastSoulmateSkill`; разбор — следующим тиком (закон no-reentrant-net-callbacks).
	void OnNetSoulSkillState(const TArray<uint8>& Body);
	void OnNetSoulmateState(int32 CharId, int32 UserIdx, int32 SoulIdx, int32 PartnerUserIdx);
	void OnNetSoulmatePeer(int32 CharId, int32 SoulIdx, int32 PartnerCharId);

	// Отложенные события сети, разбираемые в Tick.
	struct FPendingNetEvent
	{
		int32 Opcode = 0;     // 5257 / 5270 / 5271
		int32 Result = 0;
		int32 UserIdx = 0;
		int32 SkillCode = 0;
		int32 Level = 0;
		uint64 ArrivalSeq = 0; // ★ 25.10.2026 (ревизия 1.1): номер прихода (книга `NextCooldownArrivalSeq`; 0 — неизвестен)
	};
	TArray<FPendingNetEvent> NetQueue;
	void ApplyNetEvent(const FPendingNetEvent& E);

	// ★★★ ВИЗУАЛЬНЫЙ СЛОЙ. Запуск — на успешном 5257 (эталон грузит клип ровно там,
	//   `Recv_SkillCast@0xF4E370`), гашение — только на ОТКАЗЕ (`recvOtherCharSkillUse@0xF4D7D0`
	//   обнуляет слот показа лишь при `result != 1`). Сам показ ведёт `UAsdaSkillFxSubsystem`.
	// ★★★ КАСТЕР — ЛЮБОЙ. Эталон ищет его `CharManager_FindByUserIdx(g_CharManager, пакет+1)` и
	//   ставит показ найденному персонажу независимо от того, локальный он или сосед; признак
	//   локального (`CClass_GetField47565`) закрывает ТОЛЬКО окно навыков и текст отказа.
	//   Раньше обе функции звали `GetLocalChar()`, и чужие касты были невидимы (дыра отчёта §28.8).
	void StartSkillVisual(int32 CasterUserIdx, int32 SkillCode);
	void StopSkillVisual(int32 CasterUserIdx, const TCHAR* Why);

	void PostSkillNotice(int32 LocId, const FString& Fallback = FString()) const;   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без фолбэка → «[#id]»
	void PostFailNotice(int32 ResultCode, int32 SkillCode) const;

	const struct FAsdaSkillDef* GetDef(int32 SkillCode) const;
	AAsdaPlayerCharacter* GetLocalChar() const;
	// ★ Персонаж по сессионному индексу — свой павн либо сосед из реестра мир-подсистемы
	//   (`ActorByUserIdx`, порт `CharManager_FindByUserIdx@…`). Единственный поиск, своего не дублируем.
	AAsdaPlayerCharacter* CharByUserIdx(int32 UserIdx) const;
	AAsdaPlayerController* GetLocalPC() const;
	int32 SelfUserIdx() const;

	// ★ Аудит №3 (A3/Д2): тайминги по ИМЕНИ КЛИПА из единого индекса `combo_time`
	//   (`AAsdaPlayerCharacter::FindComboTimeRow`). Пустое `ClipName` в ответе = строки нет.
	FAsdaSkillCastTiming TimingFromComboRow(const FString& ClipName) const;

	// ── Состояние ────────────────────────────────────────────────────────────────────────────────

	// Отложенное действие персонажа (эталон `char + 52121` = 102, `+43135` = pending-слот).
	EAsdaCharAction PendingAction = EAsdaCharAction::None;
	int32 PendingSkill = -1;

	// ── ★ ПОДХОД РАДИ НАВЫКА (состояние 102 «вне дальности», см. CancelSkillApproach) ────────────
	int32  ApproachSkill      = -1;    // код навыка, ради которого бежим (−1 = не бежим)
	int32  ApproachTargetType = -1;    // 0 = моб (renderId), 1 = персонаж (userNum)
	int32  ApproachTargetIdx  = -1;    // сессионный индекс цели (закон charid-vs-usernum)
	double ApproachNextS      = 0.0;   // троттл переотправки маршрута (0.4 с — как у боевого подхода)
	double ApproachDeadlineS  = 0.0;   // страховка от вечного бега
	// Останавливаемся НЕ в упор, а внутри зоны действия — доля радиуса (эталонный боевой подход
	//   тоже целится на границу дальности, см. AttackTick: Stop = target − dir·StopDist).
	static constexpr float kApproachRangeFrac = 0.85f;
	static constexpr double kApproachTimeoutS = 20.0;

	// Активный навык (эталон `pChar[43134]`): −1 = не применяется.
	int32 ActiveSkill = -1;
	// ★ Время отправки заявки, сек (FPlatformTime::Seconds). Нужно СТОРОЖУ ниже.
	double ActiveSkillSentS = 0.0;
	// ★★ СТОРОЖ «ответа нет». Эталон в состоянии 14 при `slot.field_56C == 0` отсчитывает
	//   `effectSlots[111].field_598` от **2000.0 мс** и по исчерпании сам уходит в состояние 13/27,
	//   сбрасывая активный слот (`charUpdateActionStateTick` case 0xE — **`entity_char.c:23164-23188`**,
	//   `self[47539] = 1157234688` = 0x44FA0000 = 2000.0f; прежняя ссылка на `:24095` была битой,
	//   там уже другая функция — отправка обычной атаки; отчёт §5.4).
	//   ⚠ ДВЕ ОГОВОРКИ, чтобы величину не приняли за секундомер: (1) эталон вычитает
	//   `a4 * pChar[320]`, т.е. отсчёт идёт С МНОЖИТЕЛЕМ СКОРОСТИ АНИМАЦИИ и реальная длительность
	//   ≠ ровно 2000 мс; (2) эталон по исчерпании переводит персонажа в состояние 13/27, а порт
	//   переиспользует ту же величину под свою задачу — сброс активного навыка при молчании сервера.
	//   Без этого сторожа порт залипал: `ActiveSkill` ставится на отправке, а сбросить
	//   его мог только ответ сервера — которого не было, и КАЖДОЕ следующее нажатие давало
	//   «Навык уже применяется» (нотис 2203) вместо новой заявки. Живой лог 2026-08-09 21:26 —
	//   5 подряд таких нотисов. Длительность взята из эталона, не выдумана.
	static constexpr double kNoAnswerGuardS = 2.0;
	// Состояние действия персонажа (эталон `effectSlots[112].field_8`): 14 = накопление, 24 = каст.
	int32 CharState = 0;
	// ★★★ КОНЕЦ ПОКАЗА (состояние 24 → 0). Живой лог 2026-08-10 12:17-12:18: у мгновенных атакующих
	//   навыков сервер шлёт **5271 (result 1) РАНЬШЕ 5257** — в одном кадре, в этом порядке
	//   (например 707: 5271 @48:253, 5257 @48:254). Обработчик 5271 сбрасывал ActiveSkill/CharState,
	//   а СЛЕДУЮЩИЙ ЗА НИМ 5257 снова ставил ActiveSkill и состояние 24 — и снять их было уже НЕКОМУ:
	//   второй 5271 не приходит. Гейт 7 («навык уже применяется») запирал навык до следующего каста
	//   ЧЕГО-ТО ДРУГОГО — в логе 10 секунд подряд нотисов 2203. Это и есть «атакующие навыки очень
	//   неотзывчивы». Эталон здесь полагается на ТИК состояния 24: клип доиграл (`obj[13] >= obj[11]`)
	//   → `CChar_SetState(pChar, 0)`, слот показа снят. Порт воспроизводит тот же выход: на успешном
	//   5257 запоминается срок конца клипа (полная длительность фазы из ResolveTiming; если данных
	//   нет — немедленно), и Tick по его истечении (и когда визуальный слой уже погас) возвращает
	//   состояние 0 и снимает активный навык.
	double CastShowEndS = 0.0;

	// ★★★ 31.10.2026 [ИСПРАВЛЕНИЕ] ПРЕДОХРАНИТЕЛЬ ЗАЩЁЛОК (баг 7 игрока Player55 «после активного использования навыки перестают
	//   работать вообще, помогает только перезапуск»). У эталона состояние действия — ОДНО поле персонажа
	//   (`effectSlots[112].field_8`), его перезаписывает любой ход и любая атака, а состояние 14 само уходит по отсчёту
	//   `charUpdateActionStateTick` case 0xE (entity_char.c:23164: 14 → 13/27), а 13/27 выводит в 24 или 0
	//   `char_updateChargeState@entity_char.c:20058–20165` (вариант для 27 — `char_updateChargeStateVariant`, :19956). У порта `CharState` — тень диспетчера навыков, её снимают
	//   ТОЛЬКО пакеты навыков; путь, где пакет-снятие не пришёл или не дошёл до диспетчера (отказ 6159/6161 Хранителя,
	//   5231 без разбора 5232), оставлял блокирующее состояние навсегда — гейт 12 молча резал все навыки, автоатака шла.
	//   `TickLatchGuard` снимает ЛЮБОЕ блокирующее состояние, которое не охраняет сторож «ответа нет» (kNoAnswerGuardS),
	//   по сроку: 14 при активном навыке — время каста `.sif +0x2D` + kNoAnswerGuardS, прочее — kNoAnswerGuardS; и
	//   ожидание визуала в 24 сверх kShowOverrunCapS. Ключ `asda.Skill.LatchGuard` (0 — прежнее поведение).
	void TickLatchGuard();
	double LatchSinceS = 0.0;        // момент входа в текущее блокирующее состояние без сторожа (0 — не отсчитываем)
	int32 LatchState = 0;            // какое состояние отсчитываем (смена состояния — новый отсчёт)
	int32 LatchSkill = -1;           // при каком активном навыке начат отсчёт (смена навыка — новый отсчёт)
	double ShowOverrunSinceS = 0.0;  // с какого момента состояние 24 ждёт погасания визуала после срока клипа (0 — не ждёт)
	// Предел ожидания визуала в 24 сверх срока клипа, сек: самый длинный клип показа в `DT_SkillClips` короче 4 с.
	static constexpr double kShowOverrunCapS = 4.0;
	int32 LatchGuardFired = 0;       // сколько раз предохранитель снял защёлку (DumpState, самотест)
	// ★ 31.10.2026 (баг 7, ревизия п.3): навык, чей показ последним запущен СВОЕМУ персонажу (−1 — нет). Отказ 5257 на ДРУГОЙ
	//   навык (поздний отказ новой заявки) не гасит показ текущего каста.
	int32 SelfShownSkill = -1;
	// ★ 31.10.2026 (баг 7, ревизия п.2): навык соулмейта, чья заявка 5231 ушла и ждёт 5232 (−1 — нет).
	int32 SoulmatePendingSkill = -1;
	// ★ 31.10.2026 (баг 7, ревизия п.5): серия МОЛЧАЛИВЫХ отказов (гейт 12 / бит хода) — одно Warning в журнал, если больше
	//   kSilentRefuseWarnCount нажатий подряд за kSilentRefuseWindowS (журнал игрока за пределом 4 МБ пишет только Warning).
	void NoteSilentRefuse(int32 SkillCode, const TCHAR* Gate);
	int32 SilentRefuseRun = 0;          // нажатий в текущей серии
	double SilentRefuseFirstS = 0.0;    // начало серии
	bool bSilentRefuseWarned = false;   // Warning по серии уже записан
	static constexpr int32 kSilentRefuseWarnCount = 3;
	static constexpr double kSilentRefuseWindowS = 5.0;
	int32 SilentRefuseWarns = 0;        // сколько Warning записано (самотест)

	// Свой соулмейт: индекс из 5220/6152 (> 0 = пара есть).
	int32 SelfSoulIdx = -1;

	// ★★★ PVP-РЕЖИМ = эталонный флаг `char[1].buffFxSlots[160]` (хвост §13.11 ЗАКРЫТ 2026-08-10).
	//   Писатель найден: опкод **4206** (`netTradeRequestHandler@0xF4A190`, network.c:44129, ветка
	//   type==1 — «дуэль/битва началась», нотисы 108114/108131) ставит 1; гасят конец дуэли
	//   (combat_pvp.c:6531/6558/6598/6622), «отбой» той же ветки и смена карты (game_main.c:3904/3931).
	//   Диспетчер навыков читает флаг ДВАЖДЫ: (а) гейт combat_pvp.c:7263 — групповой бафф (slot 2)
	//   вне PvP площадь НЕ собирает (само-каст); (б) выбор отправителя площади
	//   `skillCastAreaTargetsAndSend@0xA12800` (собирает и ПЕРСОНАЖЕЙ) против
	//   `Skill_SendAreaSkillTargetsCast@0xA01140` (только сущности). Порт слушает OnPvpMode (4206).
	bool bSelfPvpMode = false;
	void OnNetPvpMode(int32 UserIdx, bool bOn, int32 SubType);

	// ★ Аудит №3 (A3/Д2): своего кэша `combo time.bin` больше НЕТ. Таблица грузится один раз
	//   через `UAsdaItemTableSubsystem`, индекс «имя клипа → строка» один на порт (см. `TimingFromComboRow`).

	// Ручные привязки «навык → клип» (консоль). Пусто по умолчанию: таблица хардкод-загрузчиков
	//   `CharSkillEffect_InitDispatchTable@0xA89510` в срезе не снята — честная дыра №1 отчёта.
	TMap<int32, FString> SkillClipOverride;

	// Счётчики для дампа.
	int32 Sent5256 = 0, Sent5231 = 0, Recv5257 = 0, Recv5270 = 0, Recv5271 = 0, Refused = 0;
	int32 LastRefuseCode = -1, LastRefuseSkill = -1;
	// Последний отправленный навык — им подписывается отказ, пришедший с skillId = −1
	//   (сервер отверг заявку до заполнения поля; см. ApplyNetEvent).
	int32 LastSentSkill = -1;
};
