// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaEffectsSubsystem — МОДЕЛЬ ДАННЫХ активных эффектов персонажа (баффы, дебаффы, постоянные и
//   временные усилители) + ивент-бонусы «часа удачи». ЭТАП 2 окна MINISTATE_WIN.
//   Рисование (28 иконок, спрайтовые таймеры, тултипы, иконки EXP/Drop) — в UAsdaMiniStateSubsystem;
//   здесь только данные, тик и сетевой поток, ровно как в оригинале (список живёт в CCharacter,
//   а CHUD его лишь читает).
//
// ═══════════════════ ЭТАЛОН (сборка IDB; полный реверс — Reports/ministate_win_re_B.md) ══════
//
// КОНТЕЙНЕР. `CCharacter + 0x9F8 (2552)` — НЕ массив, а STL-список узлов по 40 байт
//   (`stlListAppendNode40_a`). У мобов такой же список лежит на `+0x4890 (18576)` — прежняя дока
//   путала эти два контейнера. Ограничения на длину списка нет; ограничение — 28 слотов иконок
//   в HUD (`if (iconSlot < 28)`), лишнее просто не показывается.
//
// ЗАПИСЬ 40 б (подтверждено всеми тремя конструкторами и всеми читателями):
//   +2  i16 idFlag      копия pkt+2; **>= 1201 → таймер НЕ рисуется** (это баффы «хранителя»)
//   +5  u8  stack       стек/уровень; текстом на иконке при 2..30
//   +6  u8  kind        ★ **4 = ПОСТОЯННЫЙ** (buffListTickDecrement@0xF36930 не уменьшает остаток)
//   +7  i16 remainSec   остаток в секундах; < 0 → таймер не рисуется
//   +9  u8  byte9       копия pkt+9; у type1/type2 жёстко 99
//   +12 u32 lastTickMs  timeGetTime() последнего декремента
//   +17 u8  iconStack   кладётся в statusIconStackCount слота HUD; в конструкторах = 0
//   +18 i16 id          skillId (type 0) / соулмейт-слот (type 1) / SpecialItemRecord.subType (type 2)
//   +20 u8  type        0 = скилл, 1 = соулмейт, 2 = предмет   ← КЛАСС ЭФФЕКТА
//   +21 u8  shortFlag   1, если remainSec < 60
//   +22 u8  targetStack стек на плашке ЦЕЛИ
//   +24/+28 u32 fx0/fx1 хэндлы CEffectMgr (−1 = нет)
//   +32 u32 itemCode    код предмета (только type 2)
//
// КОНСТРУКТОРЫ (по одному на класс):
//   type 0 — `buffList_AddOrUpdate@0xF35810` (через `cChar_restartSkillEffectByType@0xA2AD60`);
//   type 1 — `charStatusAddType1@0xF35B90` (жёстко kind=0, byte9=99, stack=0);
//   type 2 — `charStatusAddType2@0xF35CC0` (жёстко kind=0, byte9=99, stack=0; **duration == 0 →
//            остаток НЕ переписывается** ⇒ бессрочный усилитель задаётся duration = 0).
//   Поиск — `CList_FindByTypeSub@0xF36D90`: совпадение по ПАРЕ (id, type).
//
// ТИК. `buffListTickDecrement@0xF36930` — каждый кадр для ЛОКАЛЬНОГО игрока (entity_char.c:7695):
//   `if (kind != 4 && now - lastTickMs >= 1000) { remainSec -= (now-lastTickMs)/1000;
//                                                 lastTickMs = now - (now-lastTickMs)%1000; }`
// ИСТЕЧЕНИЕ. `charBuffUpdateExpired@0xA2BCF0` — раз в 1000 мс (entity_char.c:7681): всё с
//   `remainSec < 0` снимается (type 0 — кроме `skillSubType == 5`; type 1 — если id != 0;
//   type 2 — только id 52/53/265/266), затем `HUD_PlayerBuffTray_Update`.
// ОЧИСТКА. `buffListClearAllStopFx@0xF36700` — удаляет ВСЁ, **кроме type == 2**: предметные
//   усилители переживают общий сброс (это и есть «постоянные усилители» с точки зрения игрока).
//
// ИВЕНТ-БОНУСЫ («час удачи»): опкод 4301 (16 б) + КЛИЕНТСКОЕ РАСПИСАНИЕ из текстового файла
//   `System\Texture\act_secession3.TEX` (`SecessionSchedule_LoadFromTex@0xE81800`,
//   `fscanf("%4d %2d %2d %2d %2d %f")` = год, месяц, день, часНачала, часКонца, бонус).
//   Отрисовка — `gameUi_updateScheduledClockDisplay@0xE819D0`, раз в 60 000 мс.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Net/AsdaNetEffectTypes.h"   // ★ R1: FAsdaEffectMsg / FAsdaEventBonusPkt — типы без класса сети
#include "AsdaEffectActor.h"        // EAsdaFxAttachMode — режим привязки эффекта (attach-7 / attach-10)
#include "UI/AsdaBuffFxRow.h"
#include "Engine/DataTable.h"   // FTableRowBase — FAsdaEventScheduleRow = строка DT_EventSchedule
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaHeldTables.h"     // ★ дедуп №2 В2-C (F3): FAsdaHeldTables — удержание DT_FxLoop на игровую сессию
#include "AsdaTickGate.h"   // ★ R4, доля a: шаблон класса не тикает (общая точка)
#include "AsdaEffectsSubsystem.generated.h"

class AAsdaEffectActor;
class USkeletalMeshComponent;

// Одна запись списка эффектов = 40-байтный узел эталона (имена полей = смещения оригинала).
USTRUCT(BlueprintType)
struct FAsdaEffectEntry
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Effects") int32 IdFlag = 0;      // +2
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Effects") int32 Stack = 0;       // +5
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Effects") int32 KindByte = 0;    // +6 (4 = постоянный)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Effects") int32 RemainSec = 0;   // +7
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Effects") int32 Byte9 = 0;       // +9
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Effects") int32 IconStack = 0;   // +17
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Effects") int32 Id = -1;         // +18
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Effects") int32 Type = 0;        // +20
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Effects") int32 ShortFlag = 0;   // +21
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Effects") int32 TargetStack = 0; // +22
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Effects") int32 ItemCode = 0;    // +32
	// +12 эталона: timeGetTime() последнего декремента. У нас — миллисекунды платформы (то же поведение).
	UPROPERTY() int64 LastTickMs = 0;
	// ★★★ ТРЕТИЙ КАНАЛ ВИЗУАЛА НАВЫКОВ — ЭФФЕКТ СОСТОЯНИЯ (`skill_fx_anim.md` §16/§17).
	//   Эталонные поля записи `+24`/`+28` — хэндлы `CEffectMgr` (−1 = нет). Обработчик состояния
	//   (`*((_DWORD*)pChar + 52304 + buffId)` / `*((_DWORD*)pMob + 1844 + buffId)`) кладёт их сюда,
	//   а `buffStopEffectsBySkillId@0xF83930` / `cCharRemoveSkillBuff@0xA2AC20` /
	//   `cChar_restartSkillEffectByType@0xA2AD60` гасят через `CEffectMgr_StopByOwner`.
	//   У нас вместо хэндла — сам эффект-актор (слабая ссылка: он может умереть сам по endMode).
	UPROPERTY() TWeakObjectPtr<AActor> Fx0;   // +24
	UPROPERTY() TWeakObjectPtr<AActor> Fx1;   // +28
	// Программа показа уже отработала (спавн — РОВНО ОДИН раз на наложение, как в эталоне).
	//   ⚠ ОТСТУПЛЕНИЕ ПОРТА (не эталон): сетевой актор носителя может ещё не существовать в момент
	//   прихода пакета (5272 приходит и до, и после спавна соседа) — тогда спавн откладывается и
	//   повторяется тиком, пока флаг не встанет. У эталона такой гонки нет: сущности уже в списке.
	UPROPERTY() bool bFxSpawned = false;
	// Кто наложил (userIdx). Нужен записям программы с носителем `caster`
	//   (`Effect_SpawnCounterOnChar@0xF695B0`, `Effect_SpawnDridg5OnChar@0xF6F280`).
	UPROPERTY() int32 CasterUserIdx = -1;
	// Постоянный (не тикает) — эталонное условие `kind != 4` в buffListTickDecrement.
	bool IsPermanent() const { return KindByte == 4; }
};

// Список эффектов одного персонажа. Порядок массива = порядок наложения (в оригинале — порядок
//   обхода STL-списка), именно он определяет порядок иконок в HUD.
USTRUCT()
struct FAsdaEffectList
{
	GENERATED_BODY()
	UPROPERTY() TArray<FAsdaEffectEntry> Items;
};

// Строка расписания ивент-бонусов (act_secession3.TEX; 6 полей на строку).
USTRUCT(BlueprintType)
struct FAsdaEventScheduleRow
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Effects") int32 Year = 0;       // rec+0  (клиент НЕ сверяет)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Effects") int32 Month = 0;      // rec+4  == hud+2833
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Effects") int32 Day = 0;        // rec+8  == hud+2834
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Effects") int32 StartHour = 0;  // rec+12 <= hud+2835
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Effects") int32 EndHour = 0;    // rec+16 >  hud+2835
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Effects") float Bonus = 0.f;    // rec+20; множитель = Bonus + 1.0
};

// Метаданные скилла, нужные системе эффектов (читаются из DT_skill_info, поле DataHex = сырая
//   273-байтная запись .sif 1:1).
USTRUCT()
struct FAsdaSkillEffectMeta
{
	GENERATED_BODY()
	UPROPERTY() int32 SkillSubType = 0;      // .sif +44 (0x2C) — charBuffUpdateExpired: == 5 не снимать по истечении
	UPROPERTY() int32 Buff0DamageType = 0;   // .sif +70 (0x46) — 5307 (не-10): сверяется с байтом режима
	UPROPERTY() int32 Buff0SubType = 0;      // .sif +71 (0x47) — 0/1 бафф, >= 2 дебафф (цвет таймера и стека)
	// ★ 26.10.2026 (bowfx) [ОРИГИНАЛ]: поля цепочки состояний навыка — `buff_ApplySkillBuff`/`skillBuffAdd` по слотам 0..2
	//   (`skillApplyChainedSubEffectsAtPos@0xA33900`, `Skill_ApplyEffectResult@0xA2D780`), сверка грейда при повторном наложении.
	int32 BuffStateId[3] = { -1, -1, -1 };      // (без UPROPERTY: C-массив с инициализатором — не для UHT)   // .sif +0x48 / +0x59 / +0x6A (i16) — id СОСТОЯНИЯ слота 0/1/2
	int32 BuffDurationMs[3] = { 0, 0, 0 };    // .sif +0x4E / +0x5F / +0x70 (i32) — длительность слота (0 = слота нет)
	UPROPERTY() int32 GradeNum = 0;                       // .sif +0xBE (u8) — номер грейда (`skillGradeNum`, правило «слабее не перебивает»)
	UPROPERTY() bool  bValid = false;
};

// ★★★ 26.10.2026 (bowfx) [ОРИГИНАЛ] 6079 0x17BF — «УРОН ПО ВРЕМЕНИ НАЛОЖЕН» (`pktHandlerSkillCast@0xF4EFA0`, skill_buff.c:38423).
//   Сервер: NpcServer `sub_4261B0`/`sub_434E80` → GameServer `sub_474010@0x474010` (GameServer.c:47255) рассылает 21 б сектору
//   моба (вид 0) или игрока (вид 1). Тело (21 б): @0 u8 ok(1) · @1 i16 userIdx кастера · @3 u8 вид цели (0 моб / 1 игрок) ·
//   @4 i16 renderId/userIdx цели · @6 i16 навык · @8 i16 состояние (0..349) · @10 u8 уровень · @11 i32 длительность, мс ·
//   @15 i16 шаг тика, мс · @17 i32 урон одного тика. Живой журнал 25.10 (Player50, навык 705): `01 01 00 00 44 00 C1 02 46 00 05
//   98 3A 00 00 B8 0B A1 01 00 00` = кастер 1, моб rid 68, навык 705, яд 70, ур. 5, 15000 мс, шаг 3000 мс, тик 417.
struct FAsdaDotSlotPkt
{
	int32 Ok = 0;              // @0  u8  1 = наложено (иное — эталон молчит)
	int32 CasterIdx = -1;      // @1  i16 userIdx кастера
	int32 TargetKind = 0;      // @3  u8  0 = моб (renderId), иначе игрок (userIdx)
	int32 TargetIdx = -1;      // @4  i16 цель
	int32 SkillId = -1;        // @6  i16 навык-источник
	int32 StateId = -1;        // @8  i16 id состояния (индекс слота урона по времени)
	int32 Level = 0;           // @10 u8  уровень навыка (эталон не читает)
	int32 DurationMs = 0;      // @11 i32 длительность, мс
	int32 IntervalMs = 0;      // @15 i16 шаг тика, мс
	int32 TickDamage = 0;      // @17 i32 урон одного тика
};

// ★★★ Слот урона по времени — 28-байтный блок эталона (моб `ent + 8776 + 28·state`, игрок `char + 226704 + 28·state`):
//   +0 f32 длительность, +4 f32 шаг, +8 i32 ОСТАТОК урона (= длит/шаг·тик), +12 i32 тик, +16 i16 счётчик (0), +20 f32 накопитель,
//   +24 i16 активен. Заполняют `skillInitDotSlot@0xA40D20` / `skillInitDotDamageSlot@0xA40E00`, тикают
//   `Skill_Buff_TickAll@0xF83A00` (моб) / `charDot_TickAll@0xA2BFD0` (игрок).
struct FAsdaDotSlot
{
	float DurationMs = 0.f;    // +0
	float IntervalMs = 0.f;    // +4
	int32 Remaining = 0;       // +8  остаток урона (−тик на каждом шаге; <= 0 — слот гаснет)
	int32 TickDamage = 0;      // +12
	float AccumMs = 0.f;       // +20 накопитель времени
	int32 SkillId = -1;        // навык-источник (журнал)
	int32 CasterUserIdx = -1;  // кто наложил (`ent[4669]` / поле жертвы) — гейт цифры «моя / режим показа 1»
};

// Событие одного тика слота (подписчик — мир: HP, цифра, чат, заявка 8226). bStopSlot — подписчик велит погасить слот.
struct FAsdaDotTickEvent
{
	bool  bMob = true;         // моб (ключ — renderId) / игрок (ключ — userIdx)
	int32 Key = -1;            // renderId / userIdx носителя
	int32 StateId = -1;        // состояние слота
	int32 SkillId = -1;        // навык-источник
	int32 Damage = 0;          // урон тика
	int32 CasterUserIdx = -1;  // кастер
	int32 Remaining = 0;       // остаток урона ПОСЛЕ вычета тика
	bool  bStopSlot = false;   // выход: погасить слот (носителя нет / моб умер / заявка 8226 ушла)
};

UCLASS()
class ASDAREBORN_API UAsdaEffectsSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Находка №3 отчёта 17_effects: bFxSpawned не сбрасывал НИКТО. Подсистема живёт
	// на GameInstance, а эффект-акторы гибнут вместе с уровнем — после травела записи
	// оставались с bFxSpawned==true и SpawnBuffFx выходил первой же строкой.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaEffectsSubsystem, STATGROUP_Tickables); }
	// ★ R4, доля a (14.11.2026): шаблон класса (CDO) не тикает — `AsdaTickGate.h`, ключ `asda.Perf.NoCdoTick`; рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Список эффектов ───────────────────────────────────────────────────────────────────────
	// Список персонажа (пустой, если о нём ничего не приходило).
	const TArray<FAsdaEffectEntry>& GetList(int32 UserIdx) const;
	// Список ЛОКАЛЬНОГО игрока (его и рисует мини-статус).
	const TArray<FAsdaEffectEntry>& GetSelfList() const { return GetList(SelfUserIdx()); }
	int32 SelfUserIdx() const;

	// ★★★ 2026-09-02 (К-1): объявление `HasControlDebuff` УДАЛЕНО — это был второй порт той же
	//   эталонной функции, что и `HasControlState` ниже, но со сверкой ОДНОГО id без type.
	//   Обе точки проверки «игрок обездвижен» теперь зовут `HasControlState`. Разбор — у определения.

	// Поиск по ПАРЕ (id, type) — эталон CList_FindByTypeSub@0xF36D90.
	FAsdaEffectEntry* FindByTypeSub(int32 UserIdx, int32 Id, int32 Type);
	const FAsdaEffectEntry* FindByTypeSub(int32 UserIdx, int32 Id, int32 Type) const;

	// ★★★ «ПЕРСОНАЖ ПОД КОНТРОЛЕМ» — порт `charHasAnyEquippedItemType@0xA38E40` (П11, 2026-08-11).
	//   ⚠ ИМЯ ФУНКЦИИ В IDA — МИСНЕЙМ, к экипировке она отношения не имеет. Тело (entity_char.c:26480)
	//   целиком состоит из шести обращений к СПИСКУ СОСТОЯНИЙ персонажа:
	//       CList_FindByTypeSub(this + 638, {0,1,3,14,8,4}, 0) — вернуть 1 на первом же найденном.
	//   `this + 638` — тот же список, что везде пишется как `&pChar->gap_96A[128]`
	//   (сравнить `netRemoveSkillBuffState@0xF50440` и `CCharacter_TakeDamage2@0xA2C350`, который по
	//   состояниям **0** и **1** ОТМЕНЯЕТ получение урона). `CList_FindByTypeSub@0xF36D90` сверяет
	//   `node+18` (id) с 2-м аргументом и `node+20` (type) с 3-м ⇒ ищутся записи с **Type == 0**.
	//   Названия состояний из `skill_buff_fx.tsv`: 0 «Оледенение», 1 «Окаменение» — то есть это набор
	//   ОБЕЗДВИЖИВАЮЩИХ состояний, а вовсе не «надет предмет типа N».
	//   Прежняя причина отсрочки П11 («в порту нет источника про надетые предметы») снята: источник —
	//   этот самый список, он у нас уже полный и приходит теми же пакетами, что у оригинала.
	bool HasControlState(int32 UserIdx) const;

	// Конструкторы 1:1 с эталоном.
	void AddOrUpdateSkill(int32 UserIdx, int32 SkillId, int32 IdFlag, int32 Stack, int32 KindByte, int32 RemainSec,
	                      int32 Byte9, int32 CasterUserIdx = -1);
	void AddType1(int32 UserIdx, int32 Slot, int32 DurationSec);                       // charStatusAddType1
	void AddType2(int32 UserIdx, int32 SubTypeId, int32 DurationSec, int32 ItemCode);  // charStatusAddType2
	// ★★★ РАЗОВЫЙ ЭФФЕКТ ПРИМЕНЕНИЯ ПРЕДМЕТА НА ПЕРСОНАЖЕ (волна 1 «применение предметов»).
	//   Эталон — `switch (ItemRecord->linkCode)` в `OnRecv_UseItemResult@0xD0B1E0`
	//   (item_inventory.c:39095-39157): каждая ветка зовёт
	//   `Effect_SpawnByName(g_pEffectMgr, "<имя>.jeb", 1, x, y, z, 0, targetChar + 2, 0, 5, 0, 1.0f)`,
	//   где x/y/z = `Class_GetVec3_86(targetChar)` (кэш мировой позиции `CCharacter +0x158`), а
	//   `targetChar + 2` = `CCharacter::posVec` (+0x8) — то есть эффект ПРИВЯЗАН к позиции персонажа
	//   и едет за ним. Хэндл эталон не сохраняет ⇒ гасить нечем и не надо, эффект догорает сам.
	//   ⚠ ИМЯ ЭФФЕКТА В ДАННЫХ ОТСУТСТВУЕТ: скан всех 58 878 записей `item_list` даёт 0 вхождений
	//   `.jeb`/`.wav`/`eff_` — в оригинале визуал ЗАШИТ В КОДЕ по виду (`linkCode`). Поэтому имя
	//   приходит сюда параметром из таблицы-в-коде, снятой с эталона (закон `never-invent-only-original`).
	//   `SoundId` < 0 ⇒ звука нет (ветка `linkCode == 5` его действительно не играет).
	//   Возвращает nullptr, если персонажа `UserIdx` нет в мире или BP эффекта не собран.
	//   ★ `SoundName` — для веток, где эталон играет звук ПО ИМЕНИ ФАЙЛА, а не по числовому id
	//   (Камень души: `Audio_Play3DSound(..., "S_c_SmExpGift.wav", 0)`, `item_inventory.c:39058`).
	//   Задавать одновременно `SoundId >= 0` и непустое имя нельзя — берётся имя.
	AActor* SpawnItemUseFx(int32 UserIdx, const FString& JebBase, int32 SoundId,
	                       const FString& SoundName = FString());

	// ★★★ ВОЛНА 2. ЭФФЕКТ НА КОСТИ-ПУСТЫШКЕ `M_DUMMY` (муж.) / `G_DUMMY` (жен.) — общая точка
	//   для двух каналов, которые в эталоне спавнят эффект ОДИНАКОВО:
	//     • гача — `Recv_BoosterShopResult_0@0xCFF9F0` (network.c:17070) и его отложенный близнец
	//       `OnRecv_ItemEnchantResult@0xD20B30` (shop_trade.c:12417), а также чужой
	//       `OnRecv_OtherPlayerBoost@0xF4C700` (network.c:44525) — привязка **10**;
	//     • премиум-баффы — семь классов `CharEffect_SpawnPMBuff_00..04` / `CharBuff_SpawnPMbuff05/06`
	//       (`charBuff_ApplyPMBuffFromPacket@0xF26110`) — привязка **7**.
	//   ★ ИСПРАВЛЕНО: привязки РАЗНЫЕ, поэтому режим передаётся параметром, а не зашит.
	//   Раньше на оба канала жёстко ставился `BoneFull` (attach-10), и премиум-баффы вращались
	//   вместе с костью. Номера сняты с последнего-но-одного аргумента `Effect_SpawnByName`:
	//   гача — 10 (`network.c:44557`, `:44576`), PM-баффы — 7 (`effects_particles.c:56979`).
	//   attach-10 = `EAsdaFxAttachMode::BoneFull`, attach-7 = `EAsdaFxAttachMode::BonePos`.
	//   `SoundName` пуст ⇒ звука нет (у семи классов PM-баффов звука действительно НЕТ —
	//   проверено вторым методом: в их телах ни одного `Audio_Play*` / `.wav`).
	//   ★ `BoneOverride` — конкретное имя кости вместо пустышки `M_DUMMY`/`G_DUMMY`. Нужно эмоции
	//     «ухаживание»: эталон вешает четыре цветка на кость `_8` (ui_core.c:32498-32545), а не на
	//     пустышку. `NAME_None` = прежнее поведение (пустышка по полу).
	AActor* SpawnDummyBoneFx(int32 UserIdx, const FString& JebBase, const FString& SoundName,
	                         EAsdaFxAttachMode BoneMode = EAsdaFxAttachMode::BoneFull,
	                         FName BoneOverride = NAME_None);

	// ★★★ ЭФФЕКТ ЗВАНИЯ НА ПЕРСОНАЖЕ — attach-6 (ПОЛНАЯ МИРОВАЯ МАТРИЦА персонажа, без кости).
	//   Два случая, оба спавнятся ОДИНАКОВО и оба — ВНЕ гейта «свой игрок» (эффект видно и у соседей):
	//     • «найдено»/секрет открыт (6085), `netHandler_OnGuildTitleResult@0xE9A850` (network.c:31468):
	//         Effect_SpawnByName(g_pEffectMgr, "ex_mark_003.jeb", 1, x,y,z, colorArg, char->posVec, 0, 6, 0, 1.0f)
	//     • «получено» (6065), `uiTitle_OnJobChangeResult@0xE9A9F0` (ui_core.c:82321): то же с "0018.jeb".
	//   ★ Предпоследний аргумент = 6 ⇒ обработчик `effectNodeFollowPlayerBone@0xF58850` (5-й в таблице
	//     `CEffectMgr_Construct`, индекс = режим − 1). Имя IDA ВРЁТ: кости там нет — хендлер копирует
	//     `pByUserIdx->worldMatrix` целиком и подменяет только элемент [13] (высоту) на
	//     `Class_GetVec3_86(char).y`. Это ровно `EAsdaFxAttachMode::CharWorld`; поправки +15,
	//     как у attach-5, здесь НЕТ.
	//   ⚠ ЗВУКА НЕТ НИ У ОДНОГО ИЗ ДВУХ — доказано двумя методами: (1) во всём классе `CUITitle`
	//     (0xE962A0…0xE9CC80) нет ни одного `Audio_Play*`/`.wav`; (2) в самих `.jeb`/`.jet` нет ни
	//     поля звука, ни строки `.wav`. Закон `never-invent-only-original`: похожий не подставляем.
	//   Возвращает nullptr, если персонажа `UserIdx` нет в мире или BP эффекта не собран.
	AActor* SpawnTitleFx(int32 UserIdx, const FString& JebBase);

	// ★★★ РЕКВИЗИТ ЭМОЦИЙ (29.10.2026, баг «у жонглирования нет мячей»). ЕДИНАЯ точка для своего игрока (эталон
	//   `Emote_Execute`, ui_core.c:32458-32560 — сразу после `CharFx_AutoSabzil_State`) и соседа (`Recv_CharSpawnFx`,
	//   network.c:39731-39814 — сразу после `CChar_SetState`). У эталона реквизит есть ровно у двух эмоций:
	//     • 45 жонглирование — `M_Ball.jeb`, attach-6 (CharWorld: мировая матрица персонажа; индекс кости `M`/`G`
	//       эталон считает и НЕ передаёт — в вызов идёт 0, режиму 6 кость не нужна);
	//     • 44 ухаживание — `M_flower01` + `M_flower02` (муж) или `F_flower01` + `F_flower02` (жен), attach-10 на кость `_8`.
	//   Хэндлы эталон кладёт в `char[1].gap_A42[15164]` (мяч) и `[15168]/[15172]` (цветы), а покадровый
	//   `CCharacter_UpdateHeldItemEffect` (entity_char.c:28310-28327) гасит их, как только состояние ушло с 45 / 44 —
	//   у порта это `TickEmoteProps`. Прочие эмоции — no-op. Ключ `asda.Emote.Props` (0 — прежний порт: реквизит только
	//   у соседа, мяч на `M_DUMMY`, четыре цветка, без снятия). Возвращает число заспавненных эффектов.
	int32 SpawnEmoteProps(int32 UserIdx, int32 EmoteState);
	// Ключ `asda.Emote.Props` (аксессор для других .cpp — имя ключа в рабочий код не вписывать).
	static bool IsEmotePropsEtalon();

	// ★★★ ЭФФЕКТ УДАЧИ ГАЧИ. Имена сняты дословно с эталона (обе ветки, свой и чужой):
	//   редкий → `eff_lucky_a_01.jeb` + `s_item_boostrare.wav`, обычный → `eff_lucky_a_00.jeb`
	//   + `s_item_boostnormal.wav`.
	AActor* SpawnBoostFx(int32 UserIdx, bool bRare);

	// ★★★ ВИЗУАЛ ПРЕМИУМ-БАФФА ПО `special_itemlist.SubType` — `switch` из
	//   `charBuff_ApplyPMBuffFromPacket@0xF26110`: 1→PMbuff_a_03, 2→_02, 3→_04, 6→_01, 7→_00,
	//   0x10→_05, 0x11→_06; прочие подтипы визуала не имеют (`default` — только иконка в HUD).
	//   Возвращает nullptr, если у подтипа своего эффекта нет.
	AActor* SpawnPmBuffFx(int32 UserIdx, int32 SubType);

	// ★★★ ВИЗУАЛ ПРИМЕНЕНИЯ ПРЕМИУМ-ПРЕДМЕТА СВОИМ ИГРОКОМ — канал 5451.
	//   ⚠ ЭТО ДРУГОЙ ДИСПЕТЧЕР, НЕ `SpawnPmBuffFx`. В оригинале их ровно два и они разведены по
	//   опкодам (`network.c:1434-1437`):
	//     • опкод **5451** → `netRecvItemBuffApplyAndClear@0xF34AF0` → `netRecvItemBuffApply@0xF23C60`
	//       — СВОЙ игрок, `switch (Table2Record->subType − 1)` на **32 кейса** (`network.c:36236-36592`);
	//     • «чужой» пакет → `recvItemApplyPMBuff@0xF34B60` → `charBuff_ApplyPMBuffFromPacket@0xF26110`
	//       — всего **7 кейсов** (это и есть `SpawnPmBuffFx`).
	//   Порт до 13.08 показывал визуал ТОЛЬКО по 7-кейсовой таблице, поэтому у премиум-предметов
	//   подтипов 19/25/41…52 (восстановление ЖИЗНЕЙ/МАНЫ/всего, разрушение брони, сброс навыков)
	//   эффекта не было вовсе — дефект живого прогона «эффекта применения премиум зелий не вижу».
	//   Возвращает число заспавненных акторов (0 — у подтипа визуала нет).
	int32 SpawnPmItemUseFx(int32 UserIdx, int32 SubType);

	// Удалить запись по (id, type) — эталон buffListRemoveBySkillState@0xF36640.
	bool RemoveBySkillState(int32 UserIdx, int32 Id, int32 Type);
	// Полная очистка КРОМЕ type 2 — эталон buffListClearAllStopFx@0xF36700.
	void ClearAllStopFx(int32 UserIdx);
	// ★ 06.11.2026 Завершение слота Дружбы 0 у своего персонажа (нотис 8155) — часть `CCharFx_SoulRite_EndSlot@0xAD3750`. Звать до снятия записи.
	void EndSoulRecallSlot(int32 UserIdx);
	// Самотест: сколько раз за сессию вышел нотис 8155.
	int32 DebugSoulRecallEndNotices() const { return SoulRecallEndNotices; }
	// Выборочная очистка по 5307 — эталон netClearCharacterBuffsPacket@0xF4F5E0.
	void ClearByMode(int32 UserIdx, int32 Mode);
	// Тик остатков — эталон buffListTickDecrement@0xF36930 (каждый кадр, локальный игрок).
	void TickDecrement(int32 UserIdx);
	// Снятие истёкших раз в секунду — эталон charBuffUpdateExpired@0xA2BCF0.
	void UpdateExpired(int32 UserIdx);

	// ══ ★★★ СОСТОЯНИЯ МОБОВ (вторая таблица обработчиков) ═════════════════════════════════════
	// Эталон держит у сущности ТОТ ЖЕ 40-байтный список (моб + 0x4890) и дёргает СВОЮ таблицу
	//   `CCharEffect_InitSkillEffectTable@0xF601D0` (`*((_DWORD*)pMob + 1844 + buffId)`).
	//   Вход — `skillBuffAdd@0xF836E0` (из `Skill_ApplyEffectResult@0xA2D780` по `.sif buff0/1/2` и
	//   из `Recv_MobSpawn@0xF305E0`, где у моба приезжают 28 уже висящих состояний),
	//   тик остатков — `Skill_Buff_TickAll@0xF83A00`.
	// Ключ — renderId сущности (у мобов нет userIdx — [[charid-vs-usernum]]).
	void AddOrUpdateMobSkill(int32 RenderId, int32 StateId, int32 Stack, int32 DurationSec, int32 CasterUserIdx);
	bool RemoveMobBySkillState(int32 RenderId, int32 StateId);
	// ★★★ 26.10.2026 (bowfx) [ОРИГИНАЛ] `buff_ApplySkillBuff@0xF83480` (bSetStack=false) / `skillBuffAdd@0xF836E0` (bSetStack=true):
	//   отсев id >= 350 и 177; уже висит то же состояние от навыка БОЛЬШЕГО грейда (`skillGradeNum`) — отказ (false);
	//   иначе запись (источник = SourceSkill в +2) и перезапуск визуала. Стек (+22) пишет только skillBuffAdd. Ключ — renderId.
	bool ApplyMobSkillState(int32 RenderId, int32 StateId, int32 SourceSkill, int32 Stack, bool bSetStack, int32 CasterUserIdx);
	// ★★★ 26.10.2026 (bowfx) [ОРИГИНАЛ] ЦЕПОЧКА СОСТОЯНИЙ НАВЫКА НА МОБЕ: состояния слотов buff0/1/2 записи навыка.
	//   bRequireDuration=true — `Skill_ApplyEffectResult@0xA2D780` (свой удар 5257/5270, бит записи 0x02): слот берётся только при
	//   ненулевой длительности, стек — байты записи @8/@9/@10 (`Stacks3`, может быть nullptr) → skillBuffAdd.
	//   bRequireDuration=false — `skillApplyChainedSubEffectsAtPos@0xA33900` (4102, вид 0, кастер известен): все три слота →
	//   buff_ApplySkillBuff. bOnlySlot0 — ветка 4102 «кастер не найден» (network.c:45272: только `SkillRecord[36]` = buff0).
	//   Возврат — сколько состояний наложено.
	// ★ 04.11.2026 (status_effect_expiry) CastRecvAtS >= 0 — the hit-frame call (`Skill_ApplyEffectResult`): a slot whose state the
	//   server already ended after the cast result arrived is NOT applied (see IsMobStateEndedSince).
	int32 ApplyMobSkillChain(int32 RenderId, int32 SkillId, int32 CasterUserIdx, const uint8* Stacks3, bool bRequireDuration, bool bOnlySlot0,
	                         double CastRecvAtS = -1.0);
	// ★★★ 04.11.2026 (status_effect_expiry) [ИСПРАВЛЕНИЕ ДЕФЕКТА] Server state list of a mob from 4017 (28 ids @8). The original
	//   removes a mob state ONLY by 4017 (`Skill_Buff_RefreshStackDepth@0xF84090`), and NpcServer sends 4017 only on a CHANGE (state
	//   end sets mob+30120 → `Frame_Monster` queues 8003 → GameServer `sub_425D40` → 4017). The client adds hit-frame states later
	//   (`Skill_ApplyEffectResult@0xA2D780`, frame of the projectile/strike), so a state the server ended BEFORE that frame was re-added
	//   and never removed again (owner 04.11: stun icon + glow on StampBerserker forever). We remember the list and the moment each
	//   state left it. Key — renderId. Rollback `asda.Buff.MobStaleHitGate 0`.
	void NoteMobServerStateList(int32 RenderId, const TArray<int32>& BuffIds);
	// true — the latest 4017 does NOT list StateId and the server dropped it at/after SinceS (the hit's cast result moment).
	bool IsMobStateEndedSince(int32 RenderId, int32 StateId, double SinceS) const;
	// ★★★ 26.10.2026 (bowfx) [ОРИГИНАЛ] приём 6079 (разбор — `FAsdaDotSlotPkt`): ветки `pktHandlerSkillCast@0xF4EFA0` 1:1 —
	//   кастер известен: моб → buff_ApplySkillBuff + слот урона; игрок → слот урона. Кастер неизвестен, но навык есть в таблице:
	//   моб → ТОЛЬКО состояние (слота нет), игрок → слот. Ключ отката `asda.Dot.Slots 0` — 6079 игнорируется (прежний порт).
	void OnDotSlotPacket(const FAsdaDotSlotPkt& P);
	// Слот урона по времени (nullptr — нет). Для самотестов и дампа.
	const FAsdaDotSlot* FindDotSlot(bool bMob, int32 Key, int32 StateId) const;
	// Число активных слотов носителя.
	int32 GetDotSlotCount(bool bMob, int32 Key) const;
	// Погасить слот(ы) носителя: StateId < 0 — все.
	void ClearDotSlots(bool bMob, int32 Key, int32 StateId = -1);
	// Тик слота: подписчик (мир) применяет урон тика. Зовётся из `Tick` (накопитель в мс, как у эталона).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaDotTick, FAsdaDotTickEvent& /*E*/);
	FOnAsdaDotTick OnDotTick;
	// ★★★ 26.10.2026 (bowfx) [ОРИГИНАЛ] флаги сущности, которые эталон пересчитывает каждый кадр (`charUpdateActionState23@0xF844F0`,
	//   entity_char.c:56783-56784): `+6986` = `cclassHasAnyBuffType@0xF8E610` — состояния 3, 14, 8, 2, 9 (оглушение, абсолютный стан, сон,
	//   ЛОВУШКА, замаскированная ловушка): позиция по пути НЕ сдвигается (effect_UpdateProjectileTick / charUpdateMoveTick — гейт
	//   `!6987 && !6986`); `+6987` = `cclassHasBuffType0Or1@0xF8E5A0` — 0 оледенение / 1 окаменение: ещё и время анимации стоит
	//   (`effectiveDelta = 0`). Ключ отката `asda.Mob.ControlFreeze 0`.
	bool HasMobMoveLock(int32 RenderId) const;
	bool HasMobAnimFreeze(int32 RenderId) const;
	// ★ R4, доля a (14.11.2026): оба признака за ОДИН поиск списка состояний моба — те же наборы id и тот же ключ отката, что у
	//   `HasMobAnimFreeze` (0/1) и `HasMobMoveLock` (3/14/8/2/9). `bOutMoveLock` — признак запрета хода сам по себе (без заморозки).
	//   Нужен кадровому тику моба (`AsdaMobControlFlags` в `AsdaMobActor.cpp`).
	void MobControlFlags(int32 RenderId, bool& bOutAnimFreeze, bool& bOutMoveLock) const;
	// Моб умер / ушёл из AoI — эталон гасит эффекты вместе с сущностью (`Skill_Buff_TickAll`: при
	//   `state == 10` (смерть) все хэндлы `StopByOwner`).
	void ClearMobEffects(int32 RenderId);
	// ★★★ Игрок вышел / ушёл из AoI (4011) — эталон убивает сам объект персонажа
	//   (`Recv_LogOutPacket@0xF3AE00` → `CharManager_RemoveNode`), и список его состояний вместе с
	//   хэндлами эффектов (`rec+24`/`rec+28`) умирает с ним. Снимает ВСЁ, включая `type == 2`.
	void ClearCharEffects(int32 UserIdx);
	// Единая точка делегата 4011: чистит и сущность (renderId), и игрока (userIdx) по одному ключу.
	void OnEntityDespawn(int32 Key);
	// ★ 22.10.2026 (ревизия локального старта, ВАЖНО-1): пришла своя 4003 — перенести список своих состояний с номера прошлой карты
	//   на новый. При смене карты `SelfUserNum` не сбрасывается, новый приходит только с 4003 (живой журнал 18.10: LoadMap кадр 378,
	//   4003 кадр 379) — поэтому перенос ждёт именно её, а не «номер известен» в тике.
	//   ★ 25.10.2026: та же 4003 после входа в мир (1021) восстанавливает действующие состояния из снимка 1021 — см. ниже.
	void OnSelfSpawnCarry();
	// ★★★ 25.10.2026 [ОРИГИНАЛ] ВОССТАНОВЛЕНИЕ СВОИХ СОСТОЯНИЙ ПРИ ВХОДЕ В МИР (баг игроков: зелья-баффы на час пропадали после
	//   перезапуска игры). Сервер хранит остаток в БД (`CharSkillItem` — 30 функц. предметов, `CharStatus` — 28 состояний
	//   навыков) и отдаёт его ЛОГИН-сервером в теле 1021 (LoginManager `sub_40AD30`: `UPCharStatus` → +32, 28 × 12 б;
	//   `UPCharItemStatus` → +368, 30 × 10 б). Эталон кладёт блоки в локального игрока (`game_main.c:2813`, +1288) и тут же зовёт
	//   `char_restoreSkillEffectsAndMounts@0xA2C670` (`entity_char.c:25977`). Порт эти байты не читал вовсе. Разбирает снимок
	//   `UAsdaNetSubsystem::EnterWorldSnapshot`, свой номер — `SelfIdx`. Возврат — сколько состояний наложено.
	int32 ApplyEnterWorldStates(int32 SelfIdx, const TArray<uint8>& Snapshot1021);
	// Ждёт ли восстановление первой своей 4003 после 1021 (для самотеста и диагностики).
	bool IsEnterWorldRestorePending() const { return bEnterWorldRestorePending; }
	const TArray<FAsdaEffectEntry>& GetMobList(int32 RenderId) const;

	// ══ ★★★ ВИЗУАЛ СОСТОЯНИЙ (третий канал, `skill_fx_anim.md` §16/§17) ═══════════════════════
	// Строка таблицы состояния (DT_BuffFx). nullptr — состояния нет в таблице.
	const FAsdaBuffFxRow* GetBuffFxRow(int32 StateId) const;
	// Разбор программы показа («jeb|attach|bone|slot|loop|variant|owner;…»). Публично — чтобы
	//   консольная проверка печатала ровно то, что исполнит рантайм.
	static void ParseBuffFxProgram(const FString& Cell, TArray<FAsdaBuffFxSpawn>& Out);
	// Состояние канала для `asda.Buff.FxDump` (что висит, чем держится, где живой актор).
	FString DumpBuffFx() const;

	// ── Справочники ───────────────────────────────────────────────────────────────────────────
	// Метаданные скилла из DT_skill_info (кэшируются при первом обращении).
	const FAsdaSkillEffectMeta& GetSkillMeta(int32 SkillId) const;
	// Запись stateinfotip (иконка/имя/описание скиллового эффекта): Name +4, Icon +36, Cell +68, Text +72.
	bool GetStateInfo(int32 Id, FString& OutName, FString& OutIcon, int32& OutCell, FString& OutText) const;
	// То же для соулмейт-эффекта (soulstateinfotip).
	bool GetSoulStateInfo(int32 Id, FString& OutName, FString& OutIcon, int32& OutCell, FString& OutText) const;
	// Запись special_itemlist («table2») по коду предмета: subType, флаг 116, иконка баффа (@197/@229).
	bool GetTable2(int32 ItemCode, int32& OutSubType, int32& OutFlag116, FString& OutBuffIcon, int32& OutBuffCell) const;
	// Тултип предмета (specialitem_tooltiplist): имя + описание.
	bool GetItemTooltipText(int32 ItemCode, FString& OutName, FString& OutText) const;

	// ── Ивент-бонусы («час удачи») ────────────────────────────────────────────────────────────
	// Применить пакет 4301 — эталон uiSetScheduledClock@0xE82DB0.
	void ApplyEventBonusPacket(const FAsdaEventBonusPkt& P);
	// Пересчёт «часов» и выбор иконок — эталон gameUi_updateScheduledClockDisplay@0xE819D0.
	//   Mode: −1 = обычный тик (раз в 60 с), 1 = показать по таймеру окна 0x67, 9 = скрыть.
	void UpdateScheduledClock(int32 Mode);
	// Индексы иконок для мини-статуса: −1 = не показывать.
	void GetEventIconIndices(bool& bOutContainer, int32& OutExpIdx, int32& OutDropIdx) const;
	// ★ 6039/6041 — GM включил/выключил ивент конкретного ВИДА (0 = дроп, 1 = опыт) с множителем.
	void ApplyEventSet(bool bOk, int32 Type, float Multiplier, bool bOn);
	// ★ ревизия 49 С-2: итог значка из двух источников (событие 6039 важнее часа 4301) → ExpMultiplier/DropMultiplier/b*EventOn.
	void RecombineEventBonus();

	// ── Отмена баффа кликом (6142) ────────────────────────────────────────────────────────────
	// Kind: 0 = скилловый (id = skillId), 1 = предметный (id = itemCode). Соулмейт снять нельзя.
	bool RequestCancel(int32 SlotTypeFlag, int32 SlotId);

	// Многострочный дамп модели (для asda.Buff.Dump).
	FString DumpEffects() const;

	// Список изменился (userIdx) → мини-статус перерисовывает иконки.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaEffectsChanged, int32 /*userIdx*/, bool /*bWithTimers*/);
	FOnAsdaEffectsChanged OnEffectsChanged;
	// Изменилось состояние ивент-бонуса → мини-статус перерисовывает EXP_Buff/Drop_Buff.
	DECLARE_MULTICAST_DELEGATE(FOnAsdaEventBonusChanged);
	FOnAsdaEventBonusChanged OnEventBonusChanged;

	// ── Состояние ивент-бонуса (публично для дампа/рендера) ───────────────────────────────────
	// ★★★ ДВА НЕЗАВИСИМЫХ СЛОТА МНОЖИТЕЛЕЙ. У сервера их ровно два и они РАЗНЫЕ по природе:
	//   ОПЫТ (`Exp_*` в Event.ini + таблица GameServerEvent.txt) и ДРОП (`Item_*` — шанс выпадения
	//   предметов, вместе с ним больше падает и золота; поэтому на значке нарисованы монеты).
	//   Заполняются двумя путями: 4301 (по своему байту вида) и 6039/6041 (по своему Type).
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   float ExpMultiplier = 1.f;   // ×N опыта
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   float DropMultiplier = 1.f;  // ×N дропа
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   bool  bExpEventOn = false;
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   bool  bDropEventOn = false;
	// ★ 21.11.2026 (ревизия 49 С-2, P128): четыре поля выше — ИТОГ для значка; источники хранятся РАЗДЕЛЬНО и сводятся
	//   RecombineEventBonus(): событие Event.dat (6039/6041 — сервер P128 шлёт уже «событие + час») важнее часа (4301 + часы клиента).
	//   Прежде одно поле на оба канала: часы клиента раз в 60 с перетирали множитель события, 6041 гасил значок часа.
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   bool  bSetExpOn = false;    // 6039/6041 вид 1
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   float SetExpMult = 1.f;
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   bool  bSetDropOn = false;   // 6039/6041 вид 0
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   float SetDropMult = 1.f;
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   bool  bHourExpOn = false;   // 4301 вид 1/3 + часы клиента
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   float HourExpMult = 1.f;
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   bool  bHourDropOn = false;  // 4301 вид 2/3 + часы клиента
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   float HourDropMult = 1.f;
	// ★ Решение юзера 2026-08-07: при множителе ×1 значок НЕ показывать (ни опыта, ни дропа).
	//   Заодно снимает расхождение оригинала «арт ×0.5 при множителе ×1».
	UPROPERTY(EditAnywhere, Category="Asda|Effects|Ивент")      bool bHideEventIconAtX1 = true;

	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   int32 EventType = 0;       // hud[704]: 1 EXP, 2 Drop, 3 оба
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   int32 EventRemainMin = 0;  // hud[705]
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   bool  bEventActive = false;// hud[706]
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   float EventRate = 1.f;     // hud[707] = бонус + 1.0
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   int32 ClockMonth = 0;      // hud+2833
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   int32 ClockDay = 0;        // hud+2834
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   int32 ClockHour = 0;       // hud+2835
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   int32 ClockMinute = 0;     // hud+2836
	// Поля пакета 4301, которые КЛИЕНТ-ЭТАЛОН не читает (лежат в CHUD+2832 «про запас»): секунда
	//   сервера и координаты сработавшей строки расписания. Держим для дампа/диагностики.
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   int32 PktSecond = 0;       // +5
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   int32 PktSchedYear = 0;    // +6  i16
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   int32 PktSchedMonth = 0;   // +8
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   int32 PktSchedDay = 0;     // +9
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   int32 PktSchedStartHour = 0; // +10
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   int32 PktEndHour = 0;      // +11
	UPROPERTY(VisibleAnywhere, Category="Asda|Effects|Ивент")   float PktBonus = 0.f;      // +12 f32 (бонус, НЕ множитель)
	// Расписание из act_secession3 (редактируется в редакторе; пусто → грузится из файла при старте).
	UPROPERTY(EditAnywhere, Category="Asda|Effects|Ивент")      TArray<FAsdaEventScheduleRow> EventSchedule;
	// Таблица расписания (эталон читал файл `System\Texture\act_secession3.TEX` с диска; в порту
	//   это DataTable, а исходный файл лежит в `RawAssets/Data/act_secession3.txt`).
	UPROPERTY(EditAnywhere, Category="Asda|Effects|Ивент")      FString EventScheduleTable = TEXT("/Game/AsdaAssets/Data/DT_EventSchedule.DT_EventSchedule");
	// Период перерисовки «часов» — эталон 60 000 мс (game_main.c:1428).
	UPROPERTY(EditAnywhere, Category="Asda|Effects|Ивент")      float EventClockPeriodSec = 60.f;
	// ★★★ ОТСТУПЛЕНИЕ, УТВЕРЖДЁННОЕ ЮЗЕРОМ 2026-08-07 (закон fix-original-defects) — НЕ «возвращать
	//   к эталону». Оригинал показывает иконки ТОЛЬКО если нашёл строку локального расписания с
	//   совпадающими месяцем/днём/часом. Файл `act_secession3` в дистрибутиве заполнен датами
	//   ИЮЛЯ–АВГУСТА 2011 ⇒ сегодня совпадений нет НИКОГДА и значок не появляется, даже когда сервер
	//   прислал активный бонус пакетом 4301 (у сервера СВОЯ копия расписания —
	//   `.\Event\GameServerEvent.txt`, тот же формат из 6 колонок; клиентский файл — просто его копия,
	//   которую забыли обновлять). Считаем это дефектом ДАННЫХ: если совпадения нет, но пакет говорит
	//   «событие активно», берём множитель ИЗ ПАКЕТА. false = молчать, как оригинал.
	UPROPERTY(EditAnywhere, Category="Asda|Effects|Ивент")      bool bEventFallbackToPacketRate = true;

	// ── Параметры модели ──────────────────────────────────────────────────────────────────────
	// Период проверки истёкших эффектов — эталон 1000 мс (entity_char.c:7681).
	UPROPERTY(EditAnywhere, Category="Asda|Effects")            float ExpireCheckPeriodSec = 1.f;
	// ★ Гейт «отмена баффа кликом разрешена» = `hud->gap_22C[0]` эталона. Кто его выставляет —
	//   в срезе ссылок НЕТ (закон dead-code-markings-suspect: пишем «ссылок в срезе нет», а не «мёртвый»),
	//   поэтому вынесен в параметр и по умолчанию включён.
	UPROPERTY(EditAnywhere, Category="Asda|Effects")            bool bBuffCancelAllowed = true;
	// ★ ОТСТУПЛЕНИЕ: в снапшоте 6154 секция соулмейт-эффектов у оригинала берёт слот = ИНДЕКС записи
	//   в массиве (`charStatusAddType1(list, j, …)`), хотя настоящий слот лежит в поле, которое
	//   оригинал только проверяет на `>= 0`. Берём настоящий слот. 1 = как в оригинале.
	UPROPERTY(EditAnywhere, Category="Asda|Effects")            bool bLegacySoulSlotFromIndex = false;
	int32 SoulRecallEndNotices = 0;   // ★ 06.11.2026: счётчик нотисов 8155 для самотеста

	// ── Параметры канала визуала состояний ────────────────────────────────────────────────────
	// Выключатель канала целиком (живая проверка «до/после» без пересборки).
	UPROPERTY(EditAnywhere, Category="Asda|Effects|Визуал состояний") bool bBuffFxEnabled = true;
	// ⚠ ОТСТУПЛЕНИЕ ПОРТА: сколько секунд ждать появления актора-носителя, прежде чем перестать
	//   пытаться спавнить эффект состояния (у эталона сущность уже существует — см. bFxSpawned).
	UPROPERTY(EditAnywhere, Category="Asda|Effects|Визуал состояний") float BuffFxHostWaitSec = 8.f;
	// Поправка высоты эталонных привязок 5 и 7 (`CEffect_SetPos(..., y + 15.0)`), игровые единицы.
	UPROPERTY(EditAnywhere, Category="Asda|Effects|Визуал состояний") float BuffFxTrackRiseZ = AAsdaEffectActor::kEtalonTrackRiseZ;   // ★ В1-C F12: 15
	// ★★ Дедуп №2, В2-C (F3, 11.10.2026): удержание таблицы ЦИКЛА 1:1 `DT_FxLoop` на игровую сессию — вместо `AddToRoot`
	//   эффект-актора (разбор у `AsdaFxLoopTable`, AsdaEffectActor.cpp). Зовёт эффект при чтении таблицы; первый вызов
	//   группы `FxLoop` берёт объект через `AsdaDt::Cached`, повторный ничего не грузит. Возврат — 0/1 (таблица удержана).
	int32 HoldFxLoopTable();
	// Набор удержанных таблиц (стенд `asda.DtHold.GcCheck`: сверка «заявлено ⊆ удержано»).
	const FAsdaHeldTables& GetHeldTables() const { return HeldTables; }

private:
	// ★ Дедуп №2, В2-C (F3): сильные ссылки на таблицы эффектов на время сессии (группа `FxLoop` — DT_FxLoop).
	UPROPERTY(Transient)
	FAsdaHeldTables HeldTables;
	void OnEffectMsg(const FAsdaEffectMsg& Msg);
	// ★ 5473 — чужой эффект удачи (гача). Порт `OnRecv_OtherPlayerBoost@0xF4C700`.
	void OnOtherPlayerBoost(int32 UserNum, bool bRare);
	void OnEventBonusPkt(const FAsdaEventBonusPkt& P);
	void LoadEventSchedule();
	void EnsureSkillMetaLoaded() const;
	void EnsureBuffFxLoaded() const;
	FAsdaEffectList& ListFor(int32 UserIdx);
	FAsdaEffectList& MobListFor(int32 RenderId);

	// ── Визуал состояний: спавн/гашение ───────────────────────────────────────────────────────
	// Носитель состояния: актор игрока по userIdx либо актор моба по renderId (+ его скелет).
	AActor* ResolveHost(int32 Key, bool bMob, USkeletalMeshComponent*& OutSkel) const;
	// Отыграть программу показа состояния. false — носителя ещё нет (повторим тиком).
	bool SpawnBuffFx(FAsdaEffectEntry& E, int32 Key, bool bMob);
	// Погасить сохранённые эффекты записи — эталон `CEffectMgr_StopByOwner(rec+24 / rec+28)`.
	static void StopBuffFx(FAsdaEffectEntry& E);
	// Повторные попытки спавна для записей, чей носитель ещё не появился.
	void RetryPendingBuffFx();
	// ★ R5-fix6 (13.11.2026) [ОРИГИНАЛ]: эффект мгновенного состояния из пакета HP 4058 / MP 4059 (тип 4 — по id состояния у игрока
	//   пакета, тип 6 — на своём персонаже). Разбор и ключ `asda.Fx.VitalsStateFx` — у определения (AsdaEffectsSubsystem_Fx.cpp).
	void OnVitalsStateFx(int32 PktUserIdx, uint8 UpdateType, int32 StateId, bool bMp);
	// ★ 29.10.2026: покадровое снятие реквизита эмоций — эталон `CCharacter_UpdateHeldItemEffect` (см. `SpawnEmoteProps`).
	void TickEmoteProps();
	// Живой реквизит эмоции: носитель, состояние, при уходе с которого реквизит гаснет, и его эффект-акторы.
	struct FAsdaEmotePropSet
	{
		TWeakObjectPtr<AActor> Host;                 // персонаж-носитель
		int32 State = -1;                            // 45 (мяч) / 44 (цветы)
		TArray<TWeakObjectPtr<AActor>> Fx;           // заспавненные эффекты (эталон: 1 хэндл мяча / 2 хэндла цветов)
	};
	// Реквизит, который ещё может погаснуть досрочно. Слабые ссылки: актор эффекта сам умирает по `SetLifeSpan`.
	TArray<FAsdaEmotePropSet> EmoteProps;
	// Тик остатков + снятие истёкших у ВСЕХ мобов (эталон Skill_Buff_TickAll@0xF83A00).
	void TickMobEffects();
	// ★ 26.10.2026 (bowfx): покадровый тик слотов урона по времени (`Skill_Buff_TickAll` / `charDot_TickAll`), шаг в мс.
	void TickDotSlots(float DeltaMs);
	// Слоты урона по времени: носитель → (состояние → слот). Моб — по renderId, игрок — по userIdx.
	TMap<int32, TMap<int32, FAsdaDotSlot>> MobDotSlots;
	TMap<int32, TMap<int32, FAsdaDotSlot>> CharDotSlots;

	UPROPERTY() TMap<int32, FAsdaEffectList> Lists;
	// ★ 22.10.2026 (ревизия стана, п. 10): номер своего персонажа на прошлой карте. При смене карты чужие списки снимаются, свой —
	//   переносится на новый номер, как только он станет известен (4003), см. `Tick`. INDEX_NONE — переносить нечего.
	int32 CarriedSelfIdx = INDEX_NONE;
	// ★ 25.10.2026: «пришёл 1021 — состояния из его снимка наложить на первой своей 4003». Взводится полным сбросом по причине
	//   `EnteringWorld` (рассылает обработчик 1021, снимок к этому моменту уже записан), снимается этой 4003 и любым другим
	//   полным сбросом (разрыв до 4003 — снимок больше не наш). Портал/смена канала 1021 не приносят — восстановления нет.
	bool bEnterWorldRestorePending = false;
	// Состояния сущностей (мобов), ключ = renderId. Эталон — тот же список на `моб + 0x4890`.
	UPROPERTY() TMap<int32, FAsdaEffectList> MobLists;
	// ★ 04.11.2026 (status_effect_expiry): latest server state list per mob (4017) and moment each state left it.
	TMap<int32, TSet<int32>> MobServerStates;     // renderId → ids of the latest 4017
	TMap<int64, double> MobStateEndedAtS;          // (renderId << 32 | stateId) → FPlatformTime::Seconds of the drop
	void ForgetMobServerStates(int32 RenderId);    // RenderId < 0 — all

	// Кэш DT_BuffFx: id состояния → строка (копия, чтобы не держать ассет и не искать линейно).
	mutable TMap<int32, FAsdaBuffFxRow> BuffFx;
	mutable bool bBuffFxLoaded = false;
	// Момент наложения (сек мира) — гейт BuffFxHostWaitSec для отложенного спавна.
	TMap<int64, double> BuffFxWaitSince;

	// Кэш метаданных скиллов (mutable — заполняется лениво из const-геттера).
	mutable TMap<int32, FAsdaSkillEffectMeta> SkillMeta;
	mutable bool bSkillMetaLoaded = false;
	mutable FAsdaSkillEffectMeta EmptyMeta;

	// ★ Аудит №3 (A3): ЧЕТЫРЕ своих кэша справочников (`StateInfoCache`, `SoulStateCache`,
	//   `Table2Cache`, `ItemTipCache`) вместе с их флагами загрузки УБРАНЫ. Каждый был ВТОРОЙ копией
	//   строк таблицы, которую `UAsdaItemTableSubsystem` уже держит (stateinfotip, soulstateinfotip,
	//   special_itemlist, specialitem_tooltiplist — все четыре в манифесте item-БД): правка данных
	//   разъезжалась по копиям, а `special_itemlist` вдобавок искался ЛИНЕЙНО мимо ключевого индекса
	//   менеджера. Резолв — типизированными геттерами менеджера (вызовов не более 28 в секунду:
	//   `RefreshBuffIcons` перебирает 28 слотов и работает по изменению списка / раз в секунду).

	double NextExpireCheckS = 0.0;
	double NextClockS = 0.0;
	// Флаг «в расписании нашлась активная строка» — эталонный v13.
	bool bScheduleHit = false;

	static const TArray<FAsdaEffectEntry> EmptyList;
};
