// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaWorldSubsystem — мир-система входа (ФАЗЫ B4/B6, WORLD_ENTRY_CHAIN.md): спавн локального игрока по
//   4003 (позиция тайлами; мир = тайл*100+50, Y = трейс террейна), камера, финализация 4054(0)→4055.
//   Эталон: позиция ставится на стадии 11 (CMap_GetTileAt(g_nSpawnSelfFieldA/B)→player_setPositionAndTarget
//   +cameraUpdate), финал — CharRegen(0) → 4055 (сервер помечает «в мире», CUser+0x141=1).
// B5 (окружение: adoption NPC 4067, мобы 4013, игроки 4065) — следующий шаг, батчи копятся в NetSubsystem.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"             // FTSTicker — пер-кадровое доведение компаньонов-хранителей
#include "Subsystems/GameInstanceSubsystem.h"
#include "Characters/AsdaWornParts.h"   // FAsdaWornParts (внешность чужого игрока в реестре)   ★ R3: FAsdaWornParts — узкий заголовок вместо AsdaCharAssembly.h
#include "Engine/DataTable.h"
#include "Net/AsdaNetWorldTypes.h"     // ★ R1: FAsdaLootDrop, FAsdaSkillHitRec, FAsdaVitals … — типы пакетов без класса сети
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaHeldTables.h"                // FAsdaHeldTables — удержание таблиц спауна на сессию (15.09.2026, dt_gc_hold)
#include "AsdaWorldSubsystem.generated.h"

class UAsdaNetSubsystem;   // ★ R1: только указатель в объявлениях; полный класс — в .cpp

struct FAsdaDotTickEvent;   // ★ bowfx: событие тика урона по времени (UI/AsdaEffectsSubsystem.h)

// Строка таблицы имён сущностей (мобы: ключ=индекс строки Mob list; NPC: ключ=NpcId). Импортируется из
//   локализованных данных игры (Mob list.BIN @+100, npc_list.BIN @+4) в UE-DataTable — не парсим .bin в рантайме.
USTRUCT(BlueprintType)
struct FAsdaEntityNameRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda") FString Name;    // читаемое ЛОКАЛИЗОВАННОЕ имя (RU)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda") FString Model;   // имя модели (гард маппинга мобов)
};

// Строка DT_PlusPackage — база .jeb крыльев по WingId (эталон CPlusPackage_LoadTable@0xEE3440).
//   Источник: Data/Character/PlusPackageTable.BIN (магия "PPT", header 20 (count@4), запись 140 б:
//   id i32@0, noticeId муж i32@4, noticeId жен i32@8, jeb-база муж char[64]@12, жен char[64]@76).
//   Импортёр: Tools/asda_build_plus_package_dt.py. Ключ строки = Id.
// ★ Рантайм читает ТОЛЬКО этот ассет: .BIN в Content/ кукером в пак не пакуется.
USTRUCT(BlueprintType)
struct FAsdaPlusPackageRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Wings") int32   Id = 0;         // дубль RowName
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Wings") int32   NoticeIdMale = 0;   // id нотиса (муж)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Wings") int32   NoticeIdFemale = 0; // id нотиса (жен)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Wings") FString JebMale;        // база .jeb крыла (муж)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Wings") FString JebFemale;      // база .jeb крыла (жен)
};

// ══ ★★★ ВИД ЦЕЛИ ПАРЫ «(вид, индекс)» — ЯВНЫЙ, НЕ СЫРОЙ БАЙТ ПАКЕТА ═════════════════════════════
//   ★ Аудит №3 (B6). У эталона поиск цели всегда двухветочный, и обе ветки берут ОДИН И ТОТ ЖЕ
//   int-индекс, но читают его по-разному:
//     · сущность (моб) — `Entity_FindByRenderId@0xF772D0` (entity_char.c:54053), индекс = renderId;
//     · персонаж       — `CharManager_FindByUserIdx@0x9D1240` (entity_char.c:6272), индекс = userNum.
//   ⚠ ЗАКОН [[charid-vs-usernum]]: charId (i32) ≠ userNum (i16) ≠ renderId (i16). Перепутанный
//   идентификатор не даёт ошибки — цель просто «не находится», а сервер игнорирует заявку молча.
//   ⚠⚠⚠ СЫРОЙ БАЙТ `kind` В РАЗНЫХ КАНАЛАХ ИНВЕРТИРОВАН (см. `AsdaTargetKindFromHitRec` ниже),
//   поэтому единая точка `UAsdaWorldSubsystem::ActorByTarget` принимает ТОЛЬКО этот enum, а перевод
//   сырого значения в него делается на месте разбора КОНКРЕТНОГО канала, со ссылкой на эталон.
enum class EAsdaTargetKind : uint8
{
	Entity,      // сущность/моб: индекс = renderId (Entity_FindByRenderId@0xF772D0)
	Character,   // персонаж:     индекс = userNum  (CharManager_FindByUserIdx@0x9D1240)
};

// ★ Аудит №3 (B6): перевод СЫРОГО БАЙТА `rec[0]` 29-БАЙТНОЙ ЗАПИСИ ЦЕЛИ (пакеты 5257/5270/4102) в вид.
//   Доказательство — `skillParseQuickSlotEntry@0xA869F0` (skill_buff.c:19042-19056): при `rec[0] == 1`
//   индекс `rec[1..2]` уходит в массив ПЕРСОНАЖЕЙ (pChar+172592 = слово+86296), при любом другом
//   значении — в массив СУЩНОСТЕЙ (pChar+172552 = слово+86276). Кто эти массивы потом читает, видно в
//   `CSkillCast_ApplyAoEHitEffects@0xA75EF0`: слово+86276 → `Entity_FindByRenderId` (skill_buff.c:16536),
//   слово+86296 → `CharManager_FindByUserIdx` (skill_buff.c:16831). ⇒ 1 = ПЕРСОНАЖ, 0 = СУЩНОСТЬ.
//   ⚠⚠⚠ ЭТО ОБРАТНО глобали `g_nInputControlState` (@0x02905598), где **1 = СУЩНОСТЬ**
//   (`Entity_FindByRenderId(g_nTargetUserIdx)`, skill_buff.c:31652, entity_char.c:30232) и
//   **0 = ПЕРСОНАЖ** (`CharManager_FindByUserIdx`, skill_buff.c:31789, entity_char.c:30217).
//   Именно из-за этой инверсии сырой байт запрещено передавать в `ActorByTarget` напрямую.
FORCEINLINE EAsdaTargetKind AsdaTargetKindFromHitRec(int32 RawKind)
{
	return (RawKind == 1) ? EAsdaTargetKind::Character : EAsdaTargetKind::Entity;
}

// ★ Имена эффекта и звука ПОЯВЛЕНИЯ персонажа (вход в мир / воскрешение / прибытие соседа) — ОДНА точка
//   для UAsdaWorldSubsystem::PlayEntryFx и предзагрузки «первого раза» менеджера загрузки
//   (UAsdaLoadManagerSubsystem::StartFirstUsePreload, задача load_hitch 11.09.2026).
inline constexpr const TCHAR* kAsdaEntryFxJeb = TEXT("eff_0_char_99_cmchar01-a_00");
inline constexpr const TCHAR* kAsdaEntrySound = TEXT("S_c_entry");

// ★ Группы таблиц спауна, которые мир удерживает сильно на игровую сессию (15.09.2026, dt_gc_hold; см. `HoldSpawnTables`).
enum class EAsdaSpawnTables : uint8
{
	Mob,           // DT_MobStats, DT_MobTextures, DT_MobNameColors — спаун моба (тип, текстуры, цвет имени)
	Npc,           // DT_NpcQuestNpc — высота плашки NPC
	Clan,          // DT_Clan_Sign, DT_Clan_BorderImage — эмблема клана на плашке
	Pet,           // DT_PetModels — спаун питомца
	Mount,         // DT_RideEffect — шлейф/звук транспорта
	MountIndex,    // ★ N12 (дедуп №2, В3): DT_special_itemlist — индекс транспорта `LookupMount`
	SoulGuardian,  // DT_SkillFx — «h»-эффект удара хранителя
};

UCLASS()
class ASDAREBORN_API UAsdaWorldSubsystem : public UGameInstanceSubsystem,
	public IAsdaSessionResettable
{
	GENERATED_BODY()
	friend class UAsdaViewRangeSubsystem;   // ★ 06.11.2026: только чтение реестров для графического скрытия

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Реестры мира и зеркала своего персонажа (клан, золото, имя, экипировка) жили
	// через выход в выбор персонажа: реестры чистились только по 4003.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// Локальный игрок в мире (спавнится по 4003; сборка частей/экипа — расширение B4).
	UPROPERTY(BlueprintReadOnly, Category="AsdaWorld")
	TObjectPtr<AActor> LocalPlayerActor;

	// ★ ТЕСТОВЫЙ КРЮК (консоль `Asda.World.FakeSelfSpawn`), а не игровая механика: кладёт
	//   тайл-позицию в `UAsdaNetSubsystem::SelfTileX/Z` и дёргает ТУ ЖЕ точку `OnSelfSpawn`,
	//   которую в игре дёргает приход пакета 4003. Нужен живым проверкам: в headless-PIE сервера
	//   нет, павна нет, и всё, что опирается на персонажа (движение, межкартовый автопуть,
	//   расстояние трекера), давало бы ложное «не работает» (закон `false-negatives-method`).
	//   ★ Третий аргумент — ИМЯ своего персонажа. В игре оно приезжает вместе со списком
	//   персонажей и садится в `SelfCharName` внутри `TrySpawnLocalPlayer`; в headless-PIE списка
	//   нет, имя остаётся пустым — и всё, что от него зависит (ключ ini у списка отслеживаемых
	//   заданий), не проверялось бы вовсе. Пишем в ТО ЖЕ поле, второго склада имени не заводим.
	void DebugSpawnSelfAt(int32 TileX, int32 TileY, const FString& CharName = FString());

private:
	// 4003 получен: если целевой уровень уже загружен — спавн; иначе ждём PostLoadMap.
	void OnSelfSpawn();
	// 4055 получен: игрок «в мире» — снять загрузочный экран.
	void OnWorldReady();
	// ── ★ ЭФФЕКТ ПОЯВЛЕНИЯ/ВОСКРЕШЕНИЯ (spawn_revive_effect.md) ──
	// Эталон: eff_0_char_99_cmchar01-a_00.jeb + S_c_entry.wav. Триггеры: (1) первый кадр мира после
	// загрузки — CRenderer_RenderWorldFrame@0x9773B0 (порт: OnSelfSpawn после RevealWorld);
	// (2) 4056 «прибытие» — GameNet_OnLocalPlayerMapEntry@0xF43390: self-ветка И ветка ЧУЖОГО игрока
	// (сервер шлёт 4056 всем в секторе при ЛЮБОМ воскрешении: город/на месте/кампвар — CharRegenVillage/
	// CharRegenVillageMove/CharRegenSelfPosition@GameServer). Звук: self = 3D в своей позиции
	// (Audio_Play3DSound), чужой = 2D (CSoundFlag_PlaySound2D) — как в эталоне.
	void OnMapEntryFx(int32 UserIdx, bool bDeadArrival);
	void PlayEntryFx(AActor* Ch, bool bSelf);
	// Дедуп self: при ПЕРВОМ входе эффект играет по 4003-спавну, а 4056 приходит следом (в эталоне оба
	// триггера срабатывают в одном кадре и сливаются; в порте их разносит на секунды) — второй столб не нужен.
	double LastSelfEntryFxS = -1000.0;
	// Уровень загрузился (страховка порядка 4003 ↔ загрузка уровня).
	void OnPostLoadMap(UWorld* World);
	// Спавн локального игрока на тайл-позицию из NetSubsystem + камера + отправка 4054(0).
	void TrySpawnLocalPlayer();
	// Показать мир: снять загрузочный экран + закрыть login-UI + игровой ввод (по 4003 спавну; идемпотентно).
	// ★ 22.10.2026: bReopenTravelKept — после `CloseAll` вернуть окна, которые эталон держит при смене карты
	//   (`UAsdaWindowSubsystem::ReopenTravelKeptWindows`). Только у показа по 4003 (вход на карту); запасной показ по
	//   4055/4056 (4003 не пришёл) запомненное не тратит — его вернёт следующий 4003.
	void RevealWorld(bool bReopenTravelKept = true);




	// ── ★ B5 ADOPTION: связывание сетевых сущностей с запечёнными акторами ──
	// Батчи 4067 (NPC, записи 108б: [0]=renderId i16, [2]=type u16 1-based, [8]=tileX, [10]=tileZ —
	//   эталон Recv_DisconnectPopup@0xF934B0 → npcMgr_spawnWorldEntityFromTable(a1[1]-1, a1[0], a1[4], a1[5]))
	//   и 4013 (мобы, записи 173б: [0]=renderId, [2]=type, [9]=HP i32, [19]/[21]=текущие тайлы i16,
	//   [27]=spawnEnabled i32, [167]=facing° i16). Сматченный актор получает NetRenderId; моб телепортируется
	//   на текущий тайл; call-моб показывается. Несматченные — лог (динамический спавн — позже).
	void ProcessEnvBatches();
	// 4011 (эталон Recv_LogOutPacket@0xF3AE00 → CharManager_FindByUserIdx = игроцкие бакеты):
	//   удаляет ТОЛЬКО ИГРОКОВ. Мобов/NPC 4011 в оригинале НЕ трогает — их жизненный цикл клиент-сайд:
	//   смерть = 4015 newHp<=0 (OnCombatHit), уход = AoI-таймаут (TickAoiDespawn).
	void OnEntityDespawn(int32 RenderId);
	// 4015 (эталон Recv_CombatHit@0xF310E0): HP цели из @+25 (безусловно); kind 1=стоп, 2=движение моба
	//   cur→dst, 3=удар (урон по игроку @21 + цифры, позиц-коррекция ≥3 тайлов); newHp<=0 → NetDeath
	//   (у эталона смерть ставит покадровый `entityListUpdateFade@0xF78660` по `i[284] <= 0`).
	//   ★ Придерживающий гейт по кадру удара здесь ВЫКЛЮЧЕН по умолчанию (§28) — см.
	//   IsMobDeathHeldByPendingHit и `asda.Skill.HoldMobDeath`.
	void OnCombatHit(const struct FAsdaCombatHit& H);
	// ★★★ Тело ветки `case 3` (удар моба по игроку) — вынесено отдельным методом, потому что эталон
	//   исполняет ЕГО ЖЕ в ветке «сущности не было» после спауна на лету (game.c:548243-548303 —
	//   дословный повтор game.c:547748-547821). Возврат в двух местах к одному коду = 1:1.
	void ApplyCombatHitKind3(class AAsdaMobActor* M, const struct FAsdaCombatHit& H, bool bAllowReflect);
	// ★★★ Ветка B4 эталона: kind=3 пришёл, когда моб в хит-флинче/«отложенном действии»
	//   (`Entity_GetActionState ∈ {13,14,15,16,17}`, game.c:547635) — замах НЕ запускается,
	//   показывается только результат удара по игроку + снап ≥3 тайлов. Возвращает true, если
	//   ветка сработала и `case 3` дальше идти не должен.
	bool ApplyCombatHitFlinched(class AAsdaMobActor* M, const struct FAsdaCombatHit& H);
	// ★★★ ОТРАЖЕНИЕ УРОНА (@31/@33) — порт `cclassApplyReflectSkillDamage@0xF8A970`. Зовётся с тем же
	//   таймингом, что и цифра урона по игроку (у эталона это делает покадровый тик сразу после
	//   показа урона, game.c:576977-576981).
	void ApplyReflectSkillDamage(class AAsdaMobActor* M, int32 ReflectSkillIdRaw, int32 ReflectDamage,
	                             int32 AttackerUserIdx);
	// 4017 (эталон Recv_CombatHitAux@0xF33B60): flags=0 → СМЕРТЬ моба (основной канал!); flags&4 →
	//   снап на тайл + HP; прочее → HP-апдейт + СИНХРОНИЗАЦИЯ СПИСКА СОСТОЯНИЙ.
	//   MobType (@0) — для ветки flags&4 при отсутствующей сущности: эталон СОЗДАЁТ моба
	//   (mobTableFindIndexById → mobListSpawnAndLink), т.е. 4017 — третий канал спауна.
	void OnCombatHitAux(const struct FAsdaCombatHitAux& A);
	// ★★★ Порт `Skill_Buff_RefreshStackDepth@0xF84090` (game.c:579309): 28 id + 28 глубин стека из
	//   4017 — авторитетный СПИСОК состояний сущности. Совпавшим записям обновляется глубина стека
	//   (`rec+22`), НЕ совпавшие снимаются (`buffStopEffectsBySkillId@0xF83930`). Новых записей
	//   эталон здесь НЕ заводит — обход идёт по списку сущности, а не по массиву пакета.
	void RefreshMobBuffStacks(int32 Rid, const TArray<int32>& BuffIds, const TArray<int32>& BuffLevels);
	// 4027 case 1 (эталон Recv_CombatAnimState@0xF3EEB0): атака игрока по мобу — очередь ударов @13 →
	//   цифры над целью (наш бой: стили 1/2/5/6; чужой: 9/0xA/0xD/0xE). Раздача цифр — по таймеру
	//   (эталон вяжет их к кадрам клипа атаки; интервал — приближение до реверса weapon-клипов).
	void OnCombatAnim(uint8 Case, int32 AttackerUserIdx, int32 TargetRid, const TArray<int32>& DmgQueue, int32 TargetHp);
	// ★★★ 15.10.2026 (волна mobpos_swing): живая клетка цели из 4027 case 1 (`UAsdaNetSubsystem::OnCombatAnimTargetCell`).
	void OnCombatAnimTargetCell(int32 AttackerUserIdx, int32 TargetRid, FIntPoint LiveCell);
	// ★★★ 15.10.2026 (волна mobpos_swing): СВЕРКА ВИДИМОЙ ПОЗИЦИИ МОБА С ЖИВОЙ КЛЕТКОЙ NpcServer (4027 @5/@7, 4017 @96/@98).
	//   Расхождение ≥ 3 клеток по любой оси (эталонный порог `Recv_CombatHit` kind=3, network.c:37729-37747): «призрак» (стоит
	//   у клиента или живая клетка = начало его траектории) ставится на клетку сервера и останавливается; законно идущий —
	//   догоняет нормировкой скорости без снапа. Возврат — была ли поправка. Ключ `asda.Mob.LiveCellSnap`.
	//   [ПОРТ] исправление: эталон эти поля не читает, и при «немом» мобе сервера (см. отчёт §1) видимая позиция расходилась.
	bool CorrectMobToServerCell(class AAsdaMobActor* M, FIntPoint LiveCell, const TCHAR* Src);
	// ★★★ 4102 (эталон netApplySkillHitResultPacket@0xF4F8A0) — ЕДИНСТВЕННЫЙ канал урона от НАВЫКА.
	//   kind 0 (моб): HP −= урон локально (`CClass_SubtractStatClamped`), цифра урона стилем 1
	//   (`FloatingText_Spawn`), реакция цели на удар; `hpAfter <= 0 && != -1` → пометка трупа.
	//   kind 1 (игрок): `CCharacter_TakeDamage2(char, урон, 3, attacker)` = HP −= урон + цифра.
	//   Раскладка записи и доказательства — в шапке делегата `OnSkillHit` (AsdaNetSubsystem.h).
	// ★★★ ЗДЕСЬ ЖЕ СТЫК С ВИЗУАЛОМ: запись НЕ применяется по приходу пакета, если у навыка идёт
	//   показ — она паркуется в `UAsdaSkillFxSubsystem::DeferSkillHit` и возвращается делегатом
	//   `OnSkillHitMoment` В МОМЕНТ УДАРА (порог фазы `COMBO[фаза] <= прогресс` либо прилёт
	//   носителя). Так цифра и вспышка совпадают во времени, как в эталоне.
	void OnSkillHit(const FAsdaSkillHitRec& R);
	// Момент удара наступил (порог фазы / прилёт снаряда) — применить запись 4102.
	void OnSkillHitMoment(const FAsdaSkillHitRec& R, bool bShowMissText);
	// Обе ветки эталона (kind 0 — сущность, kind 1 — персонаж). `bShowMissText` — рисовать «MISS»
	//   при нулевом уроне (эталон `CharFx_ShowSkillDamageText@0xA7FC00` / `Combat_ShowDamageFeedback
	//   @0xA80390`: `if (урон) {цифра} else {«MISS»}`).
	// ★ 17.10.2026 (Ф1): `bLedgerArmed` — по записи взведена запись ленты попаданий (припаркована показом навыка или полкой
	//   5257): применение её закрывает. false — запись 4102 применяется сразу, в ленте её нет.
	void ApplySkillHit(const FAsdaSkillHitRec& R, bool bShowMissText, bool bLedgerArmed = true);
	// ★★★ 26.10.2026 (bowfx) [ОРИГИНАЛ] ТИК УРОНА ПО ВРЕМЕНИ (подписка на `UAsdaEffectsSubsystem::OnDotTick`).
	//   Моб — `Skill_Buff_TickAll@0xF83A00` (skill_buff.c:39065): HP полосы −= тик (`CClass_SubtractStatClamped(…, 1)` — и под льдом),
	//   цифра стилем 7 и строка 20013 (имя моба, урон) — если кастер я или режим показа 1; остаток > 0 при мёртвом по серверу мобе —
	//   гашение эффектов и заявка 8226 (`netSendSoulmateSkillReq@0xA07260` — МИСНЕЙМ: это «моб умер от моего яда», NpcServer
	//   `Process_ClientMonsterDead@0x45AD10`). Игрок — `charDot_TickAll@0xA2BFD0` → `CCharacter_TakeDamage(…, 2)`: под 0/1 урона нет,
	//   иначе HP −= тик и `combatFloatingDamageText(вид 0)` — 7/15/9 + 20013/20241.
	void OnDotTick(FAsdaDotTickEvent& E);
	// ★ 26.10.2026 (bowfx): применить к мобу цепочку состояний СВОЕГО удара 5257/5270 (`Skill_ApplyEffectResult@0xA2D780`, бит 0x02).
	void ApplyOwnHitSkillStates(class AAsdaMobActor* M, const FAsdaSkillHitRec& R);
	// ★★★ Стиль floating-цифры урона НАВЫКА по флагам записи (байт `+7` 29-байтной записи 5257/5270).
	//   Реверс — `CharFx_ShowSkillDamageText@0xA7FC00` (цель-моб) и `Combat_ShowDamageFeedback@0xA80390`
	//   (цель-игрок): &4 крит → 4 (0xC, если бьют МЕНЯ), &8 → 8, &0x20 → 5 «IMMUNE»,
	//   иначе → 3 (0xB, если бьют меня); урон 0 → 5 «MISS». Полная таблица — в .cpp.
	//   ★ ТРИ признака, а не два (П21/П26): эталон проверяет «АТАКУЮЩИЙ Я» ПЕРВЫМ, затем «жертва Я»,
	//     затем наблюдателя. Наблюдателю положен 0xB (не 3), само-удар уходит в ветку атакующего.
	static int32 SkillDamageTextStyle(int32 HitFlags, int32 Damage,
		bool bAttackerIsLocalPlayer, bool bVictimIsLocalPlayer);
	// ★★★ Стиль floating-цифры канала **4102** — ДРУГАЯ цепочка эталона (`CCharacter_TakeDamage2(a4=3)`
	//   → `combatFloatingDamageText(damageType = 0)`): 7 бью я · 15 бьют меня · 9 наблюдатель.
	//   По `hitFlags` эталон здесь не ветвится — поля флагов в 17-байтной записи нет. Доказательство
	//   построчно — в .cpp.
	static int32 Damage4102TextStyle(bool bAttackerIsLocalPlayer, bool bVictimIsLocalPlayer);
	// ★★★ РАЗВОРОТ КАСТЕРА К ЦЕЛИ НАВЫКА — 1:1 эталон (5257/5270). См. реализацию в .cpp.
	void OnSkillCastFaceTarget(int32 CasterUserIdx, int32 SkillCode,
		const TArray<FAsdaSkillCastRec>& Recs);
	// Обновить HP-полосу плашки моба (если есть).
	void UpdateMobHpBar(class AAsdaMobActor* M);
	// Применить HP моба из любого источника (4013/4019/4017): полоса + цифра урона по дельте,
	//   если очередь уронов не пришла. bAllowNumber=false — источник уже показал цифры (4029/4027).
	// ★ 17.10.2026 (Ф1, ревизия Ф0 п.3): `bSourceOwn` — источник дельты ИЗВЕСТЕН и это наш замах (свой 4029 без ударов, свой
	//   отложенный снимок): своя цифра без проверки авторства по ленте (при быстрой смене цели она подавлялась).
	void ApplyMobHp(class AAsdaMobActor* M, int32 NewHp, bool bAllowNumber = true, bool bSourceOwn = false);
	// ★ 17.10.2026 (ревизия правки боя, п.8): то же применение БЕЗ фильтра устаревшего снимка — для значения, которое этот
	//   фильтр уже прошло на приёме (придержанное окном «наш замах в полёте»). Повторный вызов фильтра пишет
	//   `LastServerHpSnapshot` и меняет его ответ, поэтому второй раз его не зовём.
	void ApplyMobHpUnfiltered(class AAsdaMobActor* M, int32 NewHp, bool bAllowNumber, bool bSourceOwn = false);
	// ★ HP цели из боевого пакета (4029 @+4 / 4027 @+9) — авторитетный, синхронизирует полосу с сервером.
	void OnTargetHp(int32 TargetRid, int32 Hp);
	// ★★★ ЕДИНЫЙ ГЕЙТ «обнуление HP и смерть этого моба ждут кадра удара навыка»: по цели уже принята,
	//   но ещё не выпущена смертельная запись каста (5257/5270/6159/6160, `HpAfter <= 0`). Прикрывает
	//   ВСЕ каналы обнуления — 4015, 4017 flags=0 и боевой HP цели.
	//   ⚠ ВЫКЛЮЧЕН ПО УМОЛЧАНИЮ (отчёт §28): гейт питается ПОКАЗОМ навыка, а показ в порту есть только
	//   у локального игрока ⇒ у кастера и у наблюдателя один и тот же моб падал в разные моменты
	//   (до ~1.4 с врозь). Эталон рассинхрона не имеет: и записи урона, и показ идут секторным
	//   броадкастом и работают у ЛЮБОГО кастера. Включить обратно (только для локальных опытов) —
	//   `asda.Skill.HoldMobDeath 1`.
	bool IsMobDeathHeldByPendingHit(int32 Rid) const;

	// ★★★ ЭТАЛОННАЯ РЕАКЦИЯ НА СМЕРТЕЛЬНУЮ ЗАПИСЬ КАСТА — метка «обречён» (`sub_92C3AD(ent, 32)`,
	//   бит 0x20), а НЕ задержка смерти. Разбор ветки `Recv_SkillCast@0xF4E370` — в .cpp.
	void MarkDoomedByCastRecs(int32 CasterUserIdx, int32 SkillCode,
		const TArray<FAsdaSkillCastRec>& Recs);

	// ═══ ★★★ «СНИМОК HP ОТ СЕРВЕРА ОТСТАЁТ» — ПРОГНОЗ ПО ЗАПИСИ КАСТА ═══════════════════════════
	//   Доказано по бинарю боевого процесса (отчёт `skill_use_system.md` §27, `NpcServer.exe`):
	//     • `CMonster::OnDamaged@0x4423C0` при `mon+34368 == 1` (моб ЕЩЁ НЕ В БОЮ, «первый удар»)
	//       урон в HP НЕ пишет — он копится в накопителе `mon+34376`, а живое поле HP `mon+30853`
	//       остаётся прежним;
	//     • `CMonster::ProcessState@0x448350` сливает накопитель в HP только когда
	//       `now - mon+34372 >= 2000` — ЖЁСТКО ВШИТЫЕ 2000 мс;
	//     • ответ каста (`CSkillManager::SoulGuarderSkillStart@0x414E10` → `sub_414690`) кладёт в
	//       запись поле `+11 = HP(30853) − накопитель(34376) − этот урон`, т.е. HpAfter — это
	//       ПРОГНОЗ сервера на будущее, а не текущее HP;
	//     • пакеты 4015/4017 несут именно ЖИВОЕ поле `mon+30853` (см. `AlarmMonsterStateToGameServer
	//       @0x45A580` и `CMonsterManager::Recv_MonsterUpdateAction@0x426B00` в GameServer).
	//   ⇒ В окне до 2000 мс сервер САМ шлёт снимок HP ВЫШЕ собственного прогноза. Живой замер
	//   2026-08-11 09:20: ответ 6159 в :46.597 дал `HpAfter=0` по rid=328, а `4017 flags=0x1 hp=108`
	//   пришёл в :46.715 — полоса «оживала» до полной сразу после цифры урона.
	//   Правило порта: пока прогноз свеж, снимок HP, который ВЫШЕ прогноза, отбрасывается.
	//   Откат к дословному эталону — `asda.Skill.LegacyDeath 1`.
	//   ★ Фильтр РАССИНХРОНА НЕ ДАЁТ: полоса HP — локальный показ у каждого клиента свой, а питается
	//   фильтр только секторным броадкастом записей (5257/5270/6159/6160), одинаковым у всех (§28.3).
	struct FAsdaMobHpForecast
	{
		int32  Hp     = -1;    // прогноз HP из записи каста (поле `+11`)
		double UntilS = 0.0;   // до какого времени прогноз старше серверного снимка
	};
	// rid моба → прогноз. Чистится по истечении срока, при респавне (4017 flags&4) и при смерти.
	TMap<int32, FAsdaMobHpForecast> MobHpForecast;
	// Срок жизни прогноза: 2000 мс вшитого окна накопителя NpcServer + запас на сеть/тик.
	// ★ 17.10.2026 (синхронизация боя, Ф1, ревизия проекта п.5) [ПОРТ]: срок прогноза НЕ КОРОЧЕ удержания ленты попаданий
	//   (`kImpactHoldMaxS` = 4.5 с в .cpp). При 2.5 с устаревший верхний снимок (живое поле HP моба до слива накопителя),
	//   пришедший после 2.5 с удержания, проходил фильтр как «реген» — полоса прыгала вверх.
	static constexpr double kMobHpForecastHoldS = 4.5;
	static constexpr double kMobHpForecastHoldLegacyS = 2.5;   // прежний срок — ключ отката `asda.Combat.HitFrameAllChannels 0`
	// Действующий срок прогноза: 4.5 с, при ключе отката ленты — 2.5 с (как до 17.10).
	double MobHpForecastHoldS() const;
	// Запомнить прогноз HP из записей каста (5257/5270/6159/6160) — подписан на те же делегаты.
	void NoteServerHpForecast(int32 CasterUserIdx, int32 SkillCode,
		const TArray<FAsdaSkillCastRec>& Recs);
	// true — серверный снимок HP по этому мобу УСТАРЕЛ (выше свежего прогноза) и применять его нельзя.
	bool IsServerHpSnapshotStale(int32 Rid, int32 SnapHp) const;

	// ★ «Наш замах по этой цели ещё не долетел». Единая точка правила «серверное здоровье не
	//   показываем раньше визуального удара» — читают и боевой канал, и канал движения 4015.
	//   ★ Ф1: при `asda.Combat.HitFrameAllChannels 1` — есть ли в ленте попаданий моба запись СВОЕГО атакующего;
	//   при ключе 0 — прежнее одиночное окно (`PendingImpactRid`/`PendingImpactUntilS`).
	bool IsOurSwingInFlightAgainst(int32 Rid) const;
	// ★ Ф1: эти два поля живут ТОЛЬКО для ключа отката `asda.Combat.HitFrameAllChannels 0` (окно как до 17.10: HP из 4015
	//   при ответе с HP > 0). При ключе 1 их место заняла лента попаданий `ImpactLedgers`.
	int32  PendingImpactRid    = -1;    // цель нашего текущего замаха (ключ 0)
	double PendingImpactUntilS = 0.0;   // до какого момента ждём попадания (ключ 0)

	// ═══ ★★★ 17.10.2026 ЛЕНТА ПОПАДАНИЙ МОБА — ЕДИНОЕ УДЕРЖАНИЕ HP И СМЕРТИ ДЛЯ ЛЮБОГО АТАКУЮЩЕГО (синхронизация боя, Ф1) ═══
	//   [ПОРТ] Проект `Reports/combat_sync_2026-10-17/03_DESIGN.md` §3, полный разбор — у `ArmImpact` в .cpp.
	//   Окно «наш замах в полёте» (правка 17.10, только свои замахи, один rid) — частный случай ленты: та же запись, тот же код.
	// Вид записи ленты: обычный удар ближнего боя, снаряд дальнобойного, навык (5257/5270/6159), прок/отражение 4102.
	enum class EAsdaImpactKind : uint8 { Attack, Projectile, Skill, Proc4102 };
	// Одна запись ленты — ожидающее ВИДИМОЕ попадание одного атакующего по этому мобу (записи одного атакующего и вида сливаются).
	struct FAsdaImpactEntry
	{
		int32  Attacker = -1;                          // userIdx атакующего (свой — тоже userIdx; в правилах ленты не различается)
		EAsdaImpactKind Kind = EAsdaImpactKind::Attack;// вид попадания
		double ArmedS = 0.0;                           // мир-время первого взвода записи
		double AtS = 0.0;                              // момент видимого попадания на ЭТОМ экране (мир-время)
		double CommittedAtS = 0.0;                     // момент, назначенный ответом сервера (0 — ответа ещё не было)
		bool   bLethal = false;                        // ответ нёс HP ≤ 0 (4029 @4 ≤ 0, 5257 +11 ≤ 0, 4102 «убит»)
		bool   bOutcomeUnknown = true;                 // исход не известен: заявка без ответа или ответ без видимого урона
		bool   bCommittedOutcomeUnknown = true;        // исход по последнему ОТВЕТУ (прерванная заявка возвращает к нему)
		int32  PendingDamage = 0;                      // известный урон записи, ещё не показанный попаданием
		int32  PendingRecs = 0;                        // (навыки) записей урона, ещё не выпущенных показом навыка
	};
	// Отложенный до показа смерти моба показ: добыча (`OnLootSpawn`), свой опыт (4038 → `OnSelfExp`).
	struct FAsdaHeldPresentation
	{
		FString Why;                 // что держим (для журнала)
		TFunction<void()> Release;   // выпуск (вызвать один раз)
		bool bReleaseOnReset = false;// ★ Ф2 (ревизия Ф1 п.10): выпустить и при сбросе лент (опыт — настоящий, его нельзя терять)
	};
	// Лента попаданий одного моба (ключ — rid).
	struct FMobImpactLedger
	{
		TArray<FAsdaImpactEntry> Pending;              // ожидающие видимые попадания
		int32  ServerHp = -1;                          // последний серверный снимок HP, прошедший фильтр прогноза (−1 — не было)
		bool   bServerHpNumber = false;                // канал снимка показал бы цифру по дельте (4017/4013/4019 — да, 4015 — нет)
		int32  ShownHp = -1;                           // что на полосе после последней операции ленты (журнал/самотест)
		bool   bDeathPending = false;                  // серверная смерть получена и ждёт смертельной записи
		FString DeathChan;                             // канал, принёсший отложенную смерть (журнал)
		double DeathArrivedS = 0.0;                    // мир-время прихода отложенной смерти (допуск 0.5 с для «смерти без записи»)
		TArray<FAsdaHeldPresentation> HeldLoot;        // добыча/опыт/нотисы, ждущие показа смерти
		double HoldSinceS = 0.0;                       // начало удержания (абсолютный потолок kImpactHoldMaxS; 0 — не держим)
		// ★ 10.11.2026 (job4-vfx): надбавка к потолкам взвода и удержания — задержка отложенной записи навыка (911: 9 волн до 1.2 с после
		//   порога фазы, 907: 0.4 с), чтобы HP/смерть не показались раньше последней волны. Сбрасывается вместе с записями ленты.
		double HoldExtraS = 0.0;
		double LastQueueHitS = -1000.0;                // мир-время последней цифры из очереди по мобу (глушит цифру «по дельте»)
		TMap<int32, double> LastAttackS;               // атакующий → мир-время его последней записи (авторство дельты HP)
		FTimerHandle Timer;                            // таймер ближайшего события ленты
	};
	TMap<int32, FMobImpactLedger> ImpactLedgers;       // rid моба → лента
	// Лента включена для этого атакующего: `HitFrameAllChannels 1` и (свой или `ObserverSync 1`) — ключи отката.
	bool IsImpactLedgerOn(bool bSelfAttacker) const;
	// Взвести/продлить запись ленты (полный разбор в .cpp). bCommitted — момент назначен ответом сервера; KnownDamage —
	//   известный урон ответа (сумма показываемых ударов / дельта снимка); bOutcomeKnown — ответ показал урон или смерть.
	void ArmImpact(int32 Rid, int32 Attacker, EAsdaImpactKind Kind, float DelayS, bool bCommitted, bool bLethal,
		int32 KnownDamage, bool bOutcomeKnown, const TCHAR* Why);
	// Попадание показано (цифра/снимок): вычесть урон из записи этого атакующего (вида удара).
	void NoteImpactShown(int32 Rid, int32 Attacker, int32 Damage);
	// ★ 17.10.2026 (ревизия Ф2, ВАЖНО 4): момент закрытия записи ленты — снаряд с непоказанным известным уроном ждёт показа
	//   (оценка попадания + запас), прочие — оценка попадания.
	static double LedgerEntryCloseS(const FAsdaImpactEntry& E);
	// Серверный снимок из самого ответа (4029 @4, 5257 +11) при записях ленты: запомнить и сверить «только вниз» (без подъёма).
	void NoteLedgerSnapshot(int32 Rid, int32 Hp, bool bAllowNumber);
	// Навык: выпущена одна запись урона (`ApplySkillHit`) — вычесть её из записи ленты; последняя — попадание наступило.
	//   true — запись ленты была.
	bool ConsumeSkillImpact(int32 CasterIdx, int32 Rid, bool bFrom4102, int32 Damage);
	// ★ Ф1 (ревизия Ф0 п.6): на мобе тикает СВОЁ состояние (яд/DoT) и нет чужих — дельта 4017 без записей ленты — наша.
	bool IsOwnDotOnMob(int32 Rid) const;
	// Есть ли по мобу ожидающие записи (любой атакующий).
	bool HasPendingImpacts(int32 Rid) const;
	// Смерть отложена лентой (до смертельной записи).
	bool IsLedgerDeathPending(int32 Rid) const;
	// Момент, к которому отложенная смерть должна наступить (правила §3.2; потолок удержания).
	double LedgerDeathDueS(const FMobImpactLedger& L, double Now) const;
	// Сверка полосы «только вниз»: полоса не выше серверного снимка + ещё не показанного известного урона записей.
	void LedgerReconcileHp(int32 Rid, const TCHAR* Why);
	// Серверный снимок HP при записях ленты: придержан (покрыт записями) / опущен «только вниз» / подъём через фильтр.
	//   false — записей по мобу нет (или ключ 0): вызывающий применяет как раньше. Смысл параметров — как у прежнего окна.
	//   ★ 18.10.2026 bRegenChan — канал регена/спауна (4013/4019/4017): при `asda.Combat.LedgerRaiseBareOnly 1` только он поднимает
	//   полосу при записях ленты, и голым значением (без прибавки непоказанного); 4015, 5257/4102 полосу при записях не поднимают.
	bool LedgerHoldServerHp(class AAsdaMobActor* M, int32 Hp, const TCHAR* Chan, bool bStaleChecked, bool bAllowNumber, bool bRegenChan = false);
	// Поставленный, но ещё не показанный удар по мобу (свой и соседа; цифра на кадре удара, у дальнобойного — на прилёте снаряда).
	struct FAsdaPlannedHit
	{
		int32 Attacker = -1;     // userIdx атакующего
		int32 Damage = 0;        // урон удара (сентинелы промаха — 0)
		FTimerHandle H;          // таймер кадра удара (выстрела) соседа; недействителен — удар/выстрел уже на кадре или таймер свой
		int32 OwnTimerId = 0;    // свой замах: номер таймера в `OwnHitTimers` (перестановка `RetimeOwnHits` меняет сам таймер); 0 — не свой
		float FlightS = 0.f;     // полёт снаряда после кадра выстрела (ближний — 0)
		double AtS = 0.0;        // оценка момента показа (мир-время) — для снаряда, уже вылетевшего
		uint32 Seq = 0;          // ★ ревизия 18.10: порядковый номер постановки (порядок ударов одного атакующего = порядок показа, FIFO)
		bool bGenGuard = false;  // ★ ревизия 18.10: удар соседа сверяет поколение серии (прерванная серия — удар не будет показан)
		uint32 PeerGen = 0;      // поколение серии соседа на постановке (`PeerSeriesGen`)
	};
	TMap<int32, TArray<FAsdaPlannedHit>> PlannedHits;   // rid моба → поставленные непоказанные удары
	uint32 NextPlannedHitSeq = 1;                        // следующий порядковый номер поставленного удара (0 — «не поставлен»)
	// Запомнить поставленный удар по мобу Rid (DelayS — до показа от «сейчас», с полётом; H — таймер соседа, OwnTimerId — свой;
	//   Seq — номер, выданный до постановки таймера; bGenGuard/PeerGen — сверка поколения серии соседа).
	void NotePlannedHit(int32 Rid, int32 Attacker, int32 Damage, FTimerHandle H, int32 OwnTimerId, float FlightS, float DelayS,
		uint32 Seq, bool bGenGuard, uint32 PeerGen);
	// Действующий таймер кадра поставленного удара (свой — по номеру в `OwnHitTimers`); nullptr — таймер отработал/снят.
	const FTimerHandle* PlannedHitTimer(const FAsdaPlannedHit& X) const;
	// Удар ещё будет показан: таймер ждёт или снаряд летит, серия соседа не прервана. OutS — до показа (с).
	bool IsPlannedHitPending(const FAsdaPlannedHit& X, float& OutS) const;
	// Через сколько (с) ляжет смертельный удар атакующего Attacker по мобу Rid: накопленный урон поставленных ударов (всех атакующих,
	//   по времени показа) ≥ полосы BarHp — на ударе Attacker, где это случилось; не хватило — на последнем ударе Attacker. false —
	//   у Attacker поставленных ударов по мобу нет. OutHits — сколько его ударов учтено (журнал); OutSeq — номер смертельного удара
	//   (0 — полоса уже 0). Удары прерванной серии соседа не учитываются (ревизия 18.10, п.3).
	bool LethalPlannedHitDelayS(int32 Rid, int32 Attacker, int32 BarHp, float& OutDelayS, int32& OutHits, uint32& OutSeq);
	// ★★★ Ревизия 18.10 (п.1/п.2): метка смертельного удара по мобу. Смерть — ПОСЛЕ ПОКАЗА смертельного удара (+0.05 с, `OnPlannedHitShown`),
	//   таймер — запасной: перевзводится, пока смертельный удар ещё впереди (перестановка `RetimeOwnHits`), но не дальше потолка.
	//   Гашение по трупу — по ПОРЯДКУ ударов (у того же атакующего — номер после смертельного), у прочих — позже смерти.
	struct FAsdaLethalMark
	{
		int32 Attacker = -1;     // атакующий смертельного ответа
		uint32 Seq = 0;          // номер смертельного удара (0 — удара нет: смерть по сроку, гасятся все удары атакующего по трупу)
		double PlanS = 0.0;      // плановый момент смертельного удара (мир-время) — повторный смертельный ответ берёт меньший
		double CeilS = 0.0;      // потолок запасного срока (мир-время)
		FTimerHandle Backup;     // запасной таймер смерти
	};
	TMap<int32, FAsdaLethalMark> LethalMarks;   // rid моба → метка смертельного удара (живёт до регена/респауна/снятия ленты)
	// Поставить метку и срок смерти по смертельному ответу: DeathDelayS — до смерти (с, удар + 0.05). Повторный смертельный ответ при
	//   живом мобе срок не отодвигает (меньший из двух). Возвращает задержку для смертельной записи ленты (с).
	float ArmLethalDeath(class AAsdaMobActor* M, int32 Attacker, uint32 Seq, float DeathDelayS, const TCHAR* Why);
	// Запасной таймер смерти: смертельный удар ещё впереди — перевзвести; иначе смерть (ApplyLedgerDeath).
	void OnLethalBackup(int32 Rid);
	// Поставленный удар показан (цифра на кадре удара / прилёт): снять из учёта; смертельный — смерть через 0.05 с.
	void OnPlannedHitShown(int32 Rid, uint32 Seq);
	// Удар атакующего Attacker с номером Seq по мобу M — позже смертельного удара этой смерти (гашение по трупу, запрет выстрела).
	bool IsHitAfterLethal(const class AAsdaMobActor* M, int32 Attacker, uint32 Seq) const;
	// Забыть метку смертельного удара по мобу (реген/респаун: смерть прошлой жизни недействительна).
	void ForgetLethalMark(int32 Rid);
	// Забыть всё по мобу (метка и поставленные удары) — снятие ленты, переиспользование rid.
	void ForgetLethalState(int32 Rid);
	// ═══ ★★★ 18.10.2026 (живой бой 20:50, ключ `asda.Combat.HpFloorFromServer`) «ПОЛ» ПОЛОСЫ HP МОБА — разбор у объявления ключа ═══
	struct FAsdaMobHpFloor
	{
		int32 Floor = -1;               // пол полосы: последнее HP из 4029 (HP − накопитель; любой атакующий) или ниже него из других каналов; −1 — неизвестен (= 0)
		double LastCombatS = -1000.0;   // мир-время последнего удара/ответа по мобу (бой — моложе `kMobCombatQuietS`)
		int32 DeferredHp = -1;          // подъём полосы, отложенный до выхода моба из боя (последний серверный снимок выше полосы; −1 — нет)
		FTimerHandle QuietTimer;        // проверка выхода моба из боя (применение отложенного подъёма)
		// ★ 05.11.2026 (bug-mob-hp-zero, ключ `asda.Combat.LiftLargeGap`): показанный на этом экране урон по мобу за последние
		//   `kRecentShownS` — (мир-время показа, урон). Сервер отстаёт от показа не больше чем на эту сумму; разрыв больше — полоса неверна.
		TArray<TPair<double, int32>> RecentShown;
		double HealS = -1000.0;         // ★ 05.11.2026 (ключ `asda.Combat.HealEpoch`): мир-время прихода последнего лечения 8013 (−1000 — не было)
		int32 HealGain = 0;             // ★ 05.11.2026: на сколько лечение 8013 подняло полосу (HP после − полоса до; 0 — не поднимало)
	};
	TMap<int32, FAsdaMobHpFloor> MobHpFloors;   // rid моба → пол полосы и состояние боя
	bool bApplyingDeferredRaise = false;       // идёт применение отложенного подъёма (гейт подъёма его не держит)
	// Ключ `asda.Combat.HpFloorFromServer` включён.
	static bool IsHpFloorOn();
	// Серверное HP моба из канала Chan: 4029 (bFrom4029) — пол = Hp; прочие каналы (4027 сырое, 4015 @25, 4017, 4013/4019) — пол
	//   только опускают (урон других источников); снимок ниже отложенного подъёма его отменяет.
	void NoteMobHpFloor(class AAsdaMobActor* M, int32 Hp, bool bFrom4029, const TCHAR* Chan);
	// Пол полосы моба Rid (0 — неизвестен).
	int32 MobHpFloorValue(int32 Rid) const;
	// Удар/ответ по мобу Rid: моб в бою ещё `kMobCombatQuietS`.
	void NoteMobCombat(int32 Rid);
	// Моб в бою: есть непоказанные удары (поставленные/лента) или последний удар/ответ моложе `kMobCombatQuietS`.
	bool IsMobInCombat(int32 Rid) const;
	// Подъём полосы до NewHp из канала Chan при моб в бою — отложить до выхода из боя (true — отложен, вызывающий НЕ применяет).
	bool HoldMobHpRaise(class AAsdaMobActor* M, int32 NewHp, const TCHAR* Chan);
	// Таймер проверки выхода из боя: вышел — применить отложенный подъём (плавно — сглаживание полосы), иначе перевзвести.
	void OnMobQuietCheck(int32 Rid);
	// Пол опустился ниже полосы, а непоказанных ударов не хватает это объяснить — полоса сразу к «пол + непоказанный урон».
	void ApplyHpFloorNow(class AAsdaMobActor* M, const TCHAR* Why);
	// ★ 05.11.2026: запись навыка по мобу M с HpAfter и уроном Damage посчитана сервером ДО лечения 8013 (её урон уже внутри
	//   значения лечения): окно `kImpactHoldMaxS` после лечения и «HP до удара» записи ниже полосы больше чем на половину прибавки.
	bool IsPreHealRecord(const class AAsdaMobActor* M, int32 HpAfter, int32 Damage) const;
	// ★ 05.11.2026 (ключ `asda.Combat.LiftLargeGap`): урон Dmg показан по мобу Rid на этом экране — в учёт «сервер отстаёт от показа».
	void NoteMobHpShown(int32 Rid, int32 Dmg);
	// ★ 05.11.2026: сумма урона, показанного по мобу Rid за последние `kRecentShownS` (на столько сервер может отставать от полосы).
	int32 RecentShownDamage(int32 Rid) const;
	// ★ 05.11.2026: ключ `asda.Combat.ForecastNoExtend` включён (прогноз HP продлевается только более низким значением).
	static bool IsForecastNoExtendOn();
	// Число ещё не сработавших таймеров кадров удара своего замаха текущего поколения по мобу Rid (FIFO-план, `asda.Combat.OwnHitsFifo`).
	int32 OwnPendingHitFrames(const class AAsdaPlayerCharacter* Ch, int32 Rid) const;
	// Серверная смерть при записях ленты: отложена до смертельной записи (или до ближайшего попадания ≤ 0.5 с). false — сразу.
	bool LedgerDeferDeath(class AAsdaMobActor* M, const TCHAR* Chan);
	// Реген/респаун (4017 flags&4, 4013 HP>0) при отложенной смерти — смерть отменяется, HP пакета сразу. true — была и отменена.
	//   ★ Ф2 (ревизия Ф1 п.7): bSureRevive — канал сам по себе означает реген (4017 flags&4): признак «смертельная» с записей
	//   снимается и заявки без ответа снимаются и без отложенной смерти.
	bool CancelDeferredDeathOnRevive(class AAsdaMobActor* M, int32 Hp, const TCHAR* Chan, bool bSureRevive = false);
	// ★ Ф2 (ревизия Ф1 п.1а): пересчитать удержание ленты — держим только необъяснимое записями (полоса выше «снимок + непоказанный
	//   известный урон»), отложенную смерть и удержанный показ.
	void LedgerUpdateHold(int32 Rid);
	// Таймер ленты: закрыть наступившие записи, применить смерть/полосу, выпустить удержанное, перевзвести.
	void OnLedgerTimer(int32 Rid);
	// Обработать ленту сейчас (то же, что таймер) — после прерывания или выпуска записей.
	void ProcessLedger(int32 Rid, const TCHAR* Why);
	// Перевзвести таймер ленты на её ближайшее событие (попадание, срок смерти, потолок удержания).
	void RearmLedgerTimer(int32 Rid);
	// Применить всё удержанное немедленно и очистить записи (потолок удержания, ключ переключён).
	void FlushLedger(int32 Rid, const TCHAR* Why);
	// Смерть моба показана (любой путь): выпустить удержанную добычу/опыт ленты.
	void ReleaseLedgerHeld(int32 Rid, const TCHAR* Why);
	// Смерть по ленте: снятие цели, `NetDeath`, выпуск удержанного.
	void ApplyLedgerDeath(class AAsdaMobActor* M, const TCHAR* Why);
	// Прерывание записей атакующего (отказ, смена цели, смерть, навык, уход из видимости): запись с назначенным ответом
	//   сокращается до него, без ответа — снимается. OnlyRid ≥ 0 — только по этому мобу; ExceptRid ≥ 0 — кроме этого моба.
	void CutImpactEntries(int32 Attacker, const TCHAR* Why, int32 OnlyRid = -1, int32 ExceptRid = -1);
	// Удалить ленту моба (новая жизнь rid / моб пропал / сброс): таймер гасится, удержанное выпускается.
	void DropLedger(int32 Rid);
	// Сброс всех лент (смена карты, сброс сессии): таймеры гасятся; удержанная добыча мёртвого мира не выпускается, удержанный
	//   показ опыта — выпускается (★ Ф2, ревизия Ф1 п.10: опыт настоящий, его мир — персонаж, а не карта).
	void ResetImpactLedgers();
	// Применить удержанное всех лент сразу (ключ `HitFrameAllChannels` переключён в 0 посреди удержания).
	void FlushAllLedgers(const TCHAR* Why);
	// Непоказанный известный урон записей ДРУГИХ атакующих по мобу; bUnknown — у кого-то из них исход неизвестен.
	int64 LedgerOtherCover(int32 Rid, int32 Attacker, bool& bUnknown) const;
public:
	// ★ Ф1: навык — взвести запись ленты при парковке попадания 5257/4102 (зовёт `UAsdaSkillFxSubsystem` и `OnSkillHit`).
	void ArmSkillImpact(int32 CasterIdx, int32 Rid, bool bFrom4102, int32 Damage, int32 HpAfter, float RecordDelayS = 0.f);   // ★ 10.11: RecordDelayS — задержка отложенной записи (надбавка к окну)
	// ★ Ф1: показ (добыча, опыт, нотис) по мобу держится до показа его смерти, если смерть/попадание по нему ещё ждёт ленту.
	//   true — удержан (выпуск — `Release`, вместе со смертью или по потолку удержания); false — показывать сразу.
	//   bReleaseOnReset — выпустить и при сбросе лент (смена карты/сессии): опыт, в отличие от добычи мёртвого мира, не теряется.
	bool HoldUntilMobDeathShown(int32 Rid, TFunction<void()> Release, const TCHAR* Why, bool bReleaseOnReset = false);
	// ★ Ф1 (самотест): наблюдение ленты — число записей, отложена ли смерть, удержано показов.
	int32 GetLedgerPendingNum(int32 Rid) const { const FMobImpactLedger* L = ImpactLedgers.Find(Rid); return L ? L->Pending.Num() : 0; }
	bool  GetLedgerDeathPending(int32 Rid) const { return IsLedgerDeathPending(Rid); }
	int32 GetLedgerHeldNum(int32 Rid) const { const FMobImpactLedger* L = ImpactLedgers.Find(Rid); return L ? L->HeldLoot.Num() : 0; }
	// ★ 05.11.2026 (bug-mob-hp-zero): лечение моба 8013 до Hp (полоса до лечения — WasHp) — новая «эпоха» HP: прогноз, последний
	//   снимок, пол и отложенный подъём прошлой эпохи сбрасываются, пол = Hp (ключ `asda.Combat.HealEpoch`).
	void OnMobHealed(class AAsdaMobActor* M, int32 WasHp, int32 Hp);
private:
	// Отказ атаки сервером (4029 sub!=0 / 4027 case!=1): эталон сбрасывает атакующего в idle.
	void OnCombatRefused(int32 CasterUserIdx, uint8 Code, uint16 SrcOpcode);
	// 4205: сервер сбросил боевое состояние → выход из боевой стойки.
	void OnCombatStanceReset(int32 UserIdx);
	// ★★★ 4025 — ДЕЙСТВИЕ/ЭМОЦИЯ ЛЮБОГО ПЕРСОНАЖА (эталон `Recv_CharSpawnFx@0xF3AF00`, network.c:39532).
	//   Разбор `actionType` 1:1 по эталону: 0/400 → покой, 1/2 → движение, 100…106 → состояния 33…39,
	//   107/108/109 → сесть/сидит/встать, 112…117 → состояния 41…46 (с эффектами у 115/116),
	//   111 → синхронизация показа шлема, 300/301 → смерть/воскрешение, 121…128 → рыбалка.
	//   Пакет приходит И на СВОЕГО персонажа (сервер шлёт в сектор всем) — эталон различий не делает.
	void OnCharAction(int32 UserIdx, int32 ActionType, float GameX, float GameZ);
	// ★ 10.10.2026 (копание): 5430 — ИТОГ КОПКИ любого персонажа сектора (`network.c:1530-1592`). Нет актора — эталон молчит
	//   целиком (и нотиса нет). Свой — нотис **9003** «Раскопка проведена удачно» / **9004** «Раскопка не удалась» в системный
	//   чат (`CChat_AddTokenizedLines(…, −11514, 10000, 0)`: цвет 0xFFFFD306, 10 с); всем — эффект и звук итога у персонажа
	//   (`AAsdaPlayerCharacter::OnMiningResult5430`).
	void OnMiningResult(int32 Result, int32 UserIdx);

	// ★★★ GM-НЕВИДИМОСТЬ (опкоды 6005/6006, рассылка 4025 состояние 300/301, вход в зону 4103).
	//   Полный разбор — `Reports/gm_commands_hide_2026-09-09.md`.
	//   Оригинал прячет персонажа ОДНИМ битом флагов `char + 0x40C` (`sub_9210D4`/`sub_9305E8`),
	//   не трогая ни коллизию, ни сетевой реестр; ближайший аналог — `SetActorHiddenInGame`.
	void SetCharHiddenByGm(int32 UserIdx, bool bHidden);
	// Ответ 6006 про СЕБЯ (`bAccepted` = сервер принял команду, `bHidden` = состояние после неё).
	void OnGmHideResult(bool bAccepted, bool bHidden);
	// Кто сейчас скрыт GM-невидимостью (сессионные userIdx, включая свой). Нужен, чтобы состояние
	//   пережило пере-спаун актора: при возврате в зону 4103 придёт заново, но между пакетами
	//   единственный источник правды — этот набор.
	bool IsCharHiddenByGm(int32 UserIdx) const { return GmHiddenUserIdx.Contains(UserIdx); }

	// ═══ ★ СМЕРТЬ ИГРОКА — ПРЕЗЕНТАЦИЯ (эталон Recv_CharDeath_Apply@0xF42E50 + CChar_SetState case 6) ═══
	//   4053: [i16 userIdx][i16 killerIdx]. Своя смерть → состояние 6: клип гибели (моушен-слот 7 =
	//   ch1_{g}_cremmy_previewanim_18 «death01», ВСЕГДА из набора _00, от оружия не зависит), звук 3D
	//   63 (муж) / 64 (жен), сброс цели/маршрута/атаки/подбора, ссаживание с транспорта, обесцвечивание
	//   сцены. Клип играется ОДИН РАЗ и замирает последним кадром — «уже мёртв» = эта поза; посмертного
	//   фейда у ИГРОКА нет (в отличие от мобов): тело лежит, пока игрок не выберет воскрешение.
	void OnPlayerCharDeath(int32 UserIdx, int32 KillerIdx);
	// 4055/4056: сервер подтвердил воскрешение → вернуть цвет и управление, поза покоя.
	void OnPlayerCharRevived();
	// ★ Перенос по команде сервера: MapId < 0 — телепорт в пределах карты, иначе смена карты.
	//   Источник координат — «город привязки» из CUser (сервер, переменная), не константа клиента.
	void OnSelfWarp(int32 MapId, int32 TileX, int32 TileZ);
	// ★ HP/MP из пакета воскрешения (лог; само значение уже легло в единый источник NetSubsystem).
	void OnSelfVitals(int32 Hp, int32 Mp);
	// ★★★ ЕДИНЫЙ ИСТОЧНИК ИСТИНЫ: витальные значения игрока изменились → плашка (своя и чужая).
	void OnVitalsChanged(int32 UserIdx, const FAsdaVitals& V);
	// ★ Всплывающая цифра дельты HP/MP + бафф-FX (эталон netHandleHpMpUpdate case 1/4/6, стиль 0x12).
	void OnVitalsFx(int32 UserIdx, int32 Delta, uint8 UpdateType, int32 SkillId);
	// ★ 6539: HP/MP по charId (ответ на 6538). Разрешаем charId→userIdx по реестру и кладём в источник.
	void OnCharStateUpdate(int32 CharId, int32 MaxHp, int32 CurHp, int32 MaxMp, int32 CurMp);
public:

	// ══════ ★★★ ФЛАГИ И ПЛОЩАДКИ ВОЙНЫ ЗА ГОРОДА (карты 3 Альпия / 0 Силарис / 7 Пламения) ══════
	// ЭТАЛОН — ПОДМЕНА ТЕКСТУРЫ ПРИ ОТРИСОВКЕ, а не замена ассета. Три перехватчика:
	//   `uniconWarApplyTex0@0x9C8F60` (render_core.c:2975): меш рисуется текстурой **unicon.TEX**
	//       ⇒ вместо неё ставится `g_UniconWarTex0Array[состояние карты]` — ФЛАГ;
	//   `uniconWarApplyTex1@0x9C90C0` (render_core.c:3001): текстура **unicon03.TEX**
	//       ⇒ `g_UniconWarTex1Array[состояние]` — ПЛОЩАДКА (пол) под флагом;
	//   `UI_LoadClanIcon@0x9C9250` (ui_core.c:3963): текстура **unicon_clan.TEX**
	//       ⇒ эмблема клана-победителя этой карты (`g_pClanIconTexMap3/0/7`), а если её нет —
	//         `g_pDefaultClanIconTex`.
	// «Состояние карты» = фракция-хозяин ИМЕННО ЭТОГО города из пакета 6743:
	//   карта 3 → слот 0, карта 0 → слот 1 (`g_nUniconWarTexStateMap0` = 0x290883C = 0x29087E1 + 91),
	//   карта 7 → слот 2. Значения 0 Свет / 1 Тьма / 2 Хаос / 3 не занят.
	// НАБОРЫ ТЕКСТУР (`CMap_LoadUniconWarTextures@0x9C88E0`, world_map.c:666), порядок = код фракции:
	//   флаги:     uniconLightFlag · uniconDarkFlag · uniconChaosFlag · uniconNoConqureFlag
	//   площадки:  uniconLightFloor · uniconDarkFloor · uniconChaosFloor · uniconNotConqureFloor
	//   плюс отдельная `uniconNoConqureFlag2` (гнездо 3560) — в оба массива НЕ входит.
	// В порту меши города уже носят импортированные `UNICON` / `UNICON03` / `UNICON_CLAN`
	//   (по одной копии на карту, `Textures/Maps/<карта>/Texture/`), поэтому подменяем не ассет,
	//   а параметр `Tex` динамического материала тех слотов, где стоит нужная текстура.
	// ★★★ ЭМБЛЕМА КЛАНА-ПОБЕДИТЕЛЯ (слот `UNICON_CLAN`) — РАЗОБРАНА ПОЛНОСТЬЮ.
	//   Прошлое «писателя `g_pClanIconTexMap3/0/7` нет» было ЛОЖНЫМ «нет» (закон
	//   `false-negatives-method`): писатель адресуется не по имени глобала, а КАК ПОЛЕ ОБЪЕКТА.
	//   `uniconWar_UpdateFactionStateUI@0x9C8B90` (обработчик 6743) в конце цикла по трём городам
	//   делает:  `if (byte_2908813[91*i] > 0) CMiniMap_BakeIconToRenderTarget(&v12[232*i + 965], …)`
	//   — и шаг 232 dword ТОЧНО совпадает с расстоянием между глобалами:
	//   `g_pClanIconTexMap3` 0x2908B14 → `Map0` 0x2908EB4 → `Map7` 0x2909254, разность 0x3A0 = 232·4.
	//   То есть эмблема ПЕЧЁТСЯ В RENDER TARGET прямо из пакета 6743, второго канала нет.
	//   Аргументы бейка (по адресам, база записи 0x29087E1): SignIdx +75, BorderIdx +51,
	//   BgColor +67, SignColor +83, BorderColor +59 — это РОВНО 40 байт `flag40` (+51…+90),
	//   тот же формат, что в 6542/4135. Собираем их общей точкой `UAsdaClanFlag::GetOrBake`
	//   (второго сборщика не заводим, закон `port-shared-points`).
	//   Флага нет / клана нет ⇒ слот остаётся с родной текстурой — эталонная ветка
	//   `g_pDefaultClanIconTex` в `UI_LoadClanIcon@0x9C9250`.
	UFUNCTION(BlueprintCallable, Category="Asda|War")
	void ApplyCityWarTextures();

	// Слот города текущей карты: 0 Альпия (карта 3), 1 Силарис (карта 0), 2 Пламения (карта 7);
	//   −1 — карта не военная. Та же лесенка, что в `uniconWar_ApplyOptionPercent@0xBCEEC0`.
	UFUNCTION(BlueprintPure, Category="Asda|War")
	int32 GetCityWarSlotForCurrentMap() const;
	// Применить витальные значения к плашке актора игрока (общая точка ВСЕХ каналов HP).
	//   Публично: контроллер зовёт при выборе игрока целью (показ его полосы HP).
	void ApplyVitalsToNameplate(int32 UserIdx);
private:
	// Время последней цифры из очереди — чтобы фолбэк по дельте HP (4017) не дублировал её.
	double LastQueueHitS = 0.0;
	// ★ 17.10.2026 (Ф1): время последней цифры очереди ПО МОБУ (Ф0, ревизия проекта п.14) перенесено в ленту попаданий —
	//   `FMobImpactLedger::LastQueueHitS` (проект §3.7). Общее `LastQueueHitS` выше — только ключ отката `ObserverSync 0`.
	// ★ 17.10.2026 (Ф1, ревизия Ф0 п.1/п.8) [ОРИГИНАЛ]: признак серии СОСЕДА — зеркало эталонного `caster[1].field11C`
	//   (network.c:42050–42081): 8 — серия открыта 4027 case 1 (network.c:40916), 0x40 — серия идёт (принятый 4029 case 0),
	//   0 — вне серии. В серии (8/0x40) 4029 ДОПИСЫВАЕТ очередь без всякого таймаута; вне серии — сбрасывает и наполняет заново.
	//   Снимается отказами 4029 1–7 (и кодом 2 при живой цели — network.c:42204–42216: дописал и `field11C = 0`), смертью
	//   или пропажей цели, смертью и уходом соседа. Прежний признак «сосед в замахе» брался из анимации и у медленного оружия
	//   (сервер молча отбрасывает ранний 4028 — пропуск одного 4029) гас посреди серии и сбрасывал очередь.
	TMap<int32, uint8> PeerSeriesFlags;
	// ★ 17.10.2026 (Ф0/Ф1) [ПОРТ]: цель серии соседа (userIdx → rid; эталон — `caster->targetRenderId`, network.c:42061/42069).
	//   Смена цели — новая серия (сброс очереди, проект §4): эталон в серии дописывает при ЛЮБОЙ цели, и хвост, уже применённый
	//   сервером к прежнему мобу, выводился бы цифрами на новом (дефект оригинала). Смена цели прерывает и записи ленты попаданий
	//   соседа по прежнему мобу (`CutImpactEntries`).
	TMap<int32, int32> PeerSeriesTargetRid;
	// ★ 17.10.2026 (Ф1, ревизия Ф0 п.10) [ОРИГИНАЛ]: поколение серии соседа (userIdx → счётчик). Отказ, переводящий соседа в покой,
	//   увеличивает его — уже поставленные таймеры цифр/выстрелов прохода с прежним поколением ничего не показывают (эталон:
	//   покой `CChar_SetState(…, 0)` прерывает показ прохода, entity_char.c:19301–19305).
	TMap<int32, uint32> PeerSeriesGen;
	// ★ 17.10.2026 (Ф1, ревизия Ф0 п.2) [ОРИГИНАЛ]: отказ 4029 код 2 при живой цели — очередь пакета ДОПИСЫВАЕТСЯ к очереди
	//   соседа (network.c:42204–42216); флаг на время такого прохода через `OnCombatAnim`.
	bool bPeerCode2Append = false;
	// ★ БУФЕР УРОНОВ ПО ЦЕЛИ (эталон addEntriesBounded4 + playerCharObject_Cleanup pop-front, макс 4):
	//   сервер присылает до 4 значений РАЗОМ, а применяет по 2 за замах; следующий пакет приходит с
	//   ПУСТОЙ очередью, но HP падает — это расходуется остаток буфера. Без буфера такие пакеты
	//   выглядели как «цифры и импакт пропали, HP не уменьшается» (юзер 2026-07-29).
	// ★★★ ОЧЕРЕДЬ РЕЗУЛЬТАТОВ УДАРОВ — КЛЮЧ ЭТО АТАКУЮЩИЙ, А НЕ ЦЕЛЬ (правка 2026-08-25).
	//   Эталон держит её на объекте АТАКУЮЩЕГО: все четыре вызова `addEntriesBounded4` в
	//   `Recv_CastState` (`network.c:42045/42050/42059/42071`) получают `caster`, и снимается она
	//   тоже с него — `playerArrayPopFront(attacker)` (`entity_char.c:23503`).
	//   Порт держал её на ЦЕЛИ, и от этого шли сразу три беды:
	//     • буферы копились по каждому монстру, которого игрок когда-либо бил, и переживали его
	//       смерть и повторное появление под тем же номером — в бой приходили чужие значения
	//       («вижу промах, а здоровье упало»: показывалась метка промаха из прошлой жизни);
	//     • урон СОСЕДА по общей цели попадал в нашу очередь и показывался как наш;
	//     • очередь никогда не обнулялась входом в бой (см. `ClearOwnDamageQueue`).
	//   Ключ — userIdx атакующего: у своего игрока очередь ровно одна на весь бой, как в оригинале.
	TMap<int32, TArray<int32>> DmgBuffers;   // userIdx атакующего → очередь результатов ударов
	// ★ Последний СЕРВЕРНЫЙ снимок здоровья по монстру. Нужен фильтру устаревших снимков, чтобы
	//   отличать ПАДЕНИЕ здоровья (яд и прочий урон по времени — принимаем) от подъёма (отбрасываем).
	//   `mutable`: фильтр — метод только для чтения, но обязан помнить, что видел в прошлый раз.
	mutable TMap<int32, int32> LastServerHpSnapshot;
	// ★ Сброс своей очереди при заявке на удар — 1:1 эталон: `netSabzil_SendActionRaw`
	//   (`network.c:5219-5223`) сразу после отправки 4026 обнуляет счётчики очереди у ЛОКАЛЬНОГО
	//   игрока (`field118 = 1; field11C = 0`). Без этого хвост прошлого боя уезжает в новый.
	void ClearOwnDamageQueue(const TCHAR* Why);
public:
	// Публично: зовёт контроллер в момент заявки на удар (вход в бой).
	void ClearOwnDamageQueuePublic(const TCHAR* Why) { ClearOwnDamageQueue(Why); }
	// ★ 17.10.2026: наш удар (4026/4028) по мобу только что ушёл — открыть окно «замах в полёте» до оценки момента
	//   попадания из замаха (кадр удара + полёт снаряда). Ответ сервера (4029/4027) окно уточнит. Разбор — в .cpp.
	void ArmOurImpactWindowFromSend(class AAsdaPlayerCharacter* Ch, class AAsdaMobActor* M, int32 WeaponSub, const TCHAR* Why);
	// ★ Для самотеста кадра удара: открыто ли окно «наш замах в полёте» по этой цели.
	bool IsOurImpactWindowOpenFor(int32 Rid) const { return IsOurSwingInFlightAgainst(Rid); }
	// ★★★ 18.10.2026 (бой: «добивает лишние взмахи», разбор у `CVarAsdaDeathAtLastPendingHit`): смерть моба — на кадре удара,
	//   который довёл полосу до 0; атакующие трупа сразу в боевую стойку (их тик, `AAsdaPlayerCharacter`). Ключ включён.
	static bool IsDeathAtLastPendingHit();
	// ★ 17.10.2026 (ревизия правки боя, п.6): наш замах прерван (отказ 4029/4027/4028, снятие/смена цели, своя смерть,
	//   заявка навыка) — окно сокращается до момента, уже назначенного ответом сервера (его цифры и смерть придут по
	//   своим таймерам), а если такого нет — придержанное применяется сразу и окно закрывается. Ключ 0 — ничего.
	//   OnlyRid ≥ 0 — только если окно открыто именно по этой цели (снятие/смена цели).
	void CutOurImpactWindow(const TCHAR* Why, int32 OnlyRid = -1);
	// ★ Для самотеста кадра удара: момент и цель последнего показанного результата удара (`ShowHitResult`, мир-время;
	//   −1 — не было) и их счётчик — фактический показ цифры, а не расчёт.
	double LastHitResultShownS = -1.0;
	int32  LastHitResultShownRid = -1;   // rid цели последнего показанного результата удара
	int32  HitResultsShown = 0;          // сколько результатов удара показано (по всем целям)
#if !UE_BUILD_SHIPPING
	// ★ 17.10.2026 (ревизия Ф0 п.13): наблюдения самотестов — только вне Shipping.
	// ★ 17.10.2026 (синхронизация боя, Ф0): одна запись наблюдения самотеста — что показал `ShowHitResult` (фактический стиль
	//   надписи, значение, авторство) и куда развернул моба флинч. Пишется ТОЛЬКО при `bRecordHitResults` (самотест).
	struct FAsdaHitResultSeen
	{
		int32  Rid = -1;            // цель (rid моба)
		int32  Raw = 0;             // значение из очереди/дельты (бит 31 — крит, отрицательные — сентинелы)
		int32  Style = -1;          // стиль FloatingText, которым надпись реально выведена (−1 — надписи не было)
		bool   bOurs = false;       // показан как НАШ удар (строка в журнал чата идёт только тогда)
		int32  AttackerUserIdx = -1;// атакующий, переданный показу (−1 — не передан)
		bool   bFlinchFaced = false;// флинч получил направление к атакующему (моб разворачивается)
		double AtS = -1.0;          // мир-время показа
		bool   bOnCorpse = false;   // ★ 18.10.2026: удар позже смертельного по трупу — погашен (`asda.Combat.DeathAtLastPendingHit`)
	};
	bool bRecordHitResults = false;            // ★ Ф0: самотест включает запись наблюдений `HitResultsSeen`
	TArray<FAsdaHitResultSeen> HitResultsSeen; // ★ Ф0: наблюдения показа результатов удара (только при записи, самотест)
	int32 CombatNoticeCalls = 0;               // ★ Ф0: сколько раз бой просил строку в журнал чата (`LogCombatNotice`)
	int32 DeltaNumbersSuppressed = 0;          // ★ Ф0: сколько цифр «по дельте HP» не показано — источник не наш замах (Р2)
	// ★ 17.10.2026 (Ф2): выстрел снаряда обычной атаки — момент вылета (кадр удара прохода), значение удара и атакующий.
	struct FAsdaShotSeen
	{
		int32  Rid = -1;              // цель
		int32  Raw = 0;               // значение удара, которое везёт снаряд
		int32  AttackerUserIdx = -1;  // атакующий
		double AtS = -1.0;            // мир-время вылета
	};
	TArray<FAsdaShotSeen> ShotsSeen;           // ★ Ф2: выстрелы обычной атаки (только при `bRecordHitResults`)
	int32 LedgerFlushCount = 0;                // ★ Ф2 (ревизия Ф1 п.1): сколько раз лента применила удержанное по потолку/сразу
	// ★ Ф2 (ревизия Ф1 п.3, самотест): сколько чужих игроков в реестре соседей (фейковые соседи прогона обязаны быть убраны).
	int32 DebugNetPlayersNum() const { return NetPlayers.Num(); }
#endif // !UE_BUILD_SHIPPING
	// ★ Ф0 (самотест): длина очереди ударов атакующего (`DmgBuffers`) — 0, если очереди нет.
	int32 GetDamageQueueLen(int32 AttackerUserIdx) const { const TArray<int32>* B = DmgBuffers.Find(AttackerUserIdx); return B ? B->Num() : 0; }

	// ═══ ★★★ 17.10.2026 (синхронизация боя, Ф5) РЕЖИМ ПОКАЗА ЦИФР УРОНА — разбор у поля `UAsdaGameUserSettings::DamageTextMode` ═══
	// Режим 0 / 1 / 2 из профиля (нет профиля — 1).
	static int32 GetDamageTextMode();
	// Показывать ли всплывающую надпись урона: 1 — всегда; 0 — только «касается меня» (я бью / бьют меня / у проков — бьёт моя
	//   выбранная цель); 2 — нет (тот же режим глушит и сам `UAsdaFloatingTextSubsystem::SpawnStyled`).
	static bool ShouldShowDamageText(bool bConcernsMe);
	// userIdx персонажа, выбранного целью локальным игроком (−1 — цель не персонаж/нет).
	int32 GetSelectedTargetUserIdx() const;

	// ═══ ★★★ 17.10.2026 (Ф4) PvP: ОБЫЧНАЯ АТАКА ИГРОКА ПО ИГРОКУ — полный разбор в .cpp (блок «Ф4 — PvP») ═══
	// 4202 — ответ на 4201 (принято: очередь урона и цель; отказ: нотис своему и стоп).
	void OnPvpAttackStart(const FAsdaPvpAttackPkt& P);
	// 4204 — ответ на 4203 (принято: очередь дописывается, у соседа — отрезок замаха; отказ: нотис своему и стоп).
	void OnPvpHit(const FAsdaPvpAttackPkt& P);
	// Снять PvP-серию атакующего (очередь, цель, признак серии) — эталонный покой `CChar_SetState(…, 0)`.
	void ResetPvpSeries(int32 AttackerUserIdx, const TCHAR* Why);
	// Длина PvP-очереди атакующего (0 — серии нет).
	int32 GetPvpQueueLen(int32 AttackerUserIdx) const;
	// Нотис отказа/гейта PvP: категория `CombatRefused` (системный чат, как отказы 4027/4029), цвет эталона.
	void PostPvpNotice(int32 LocId, const FColor& Col, const TCHAR* Why);
#if !UE_BUILD_SHIPPING
	// Наблюдение самотеста `asda.Combat.SelfTestPvp`: показанный PvP-удар (стиль −1 — надпись не показана).
	struct FAsdaPvpHitSeen
	{
		int32 Attacker = -1;   // атакующий
		int32 Victim = -1;     // жертва
		int32 Raw = 0;         // значение очереди
		int32 Style = -1;      // стиль показанной надписи (−1 — не показана)
		int32 Dmg = 0;         // урон, ушедший в HP жертвы
		int32 HpAfter = -1;    // HP жертвы после удара (копия порта)
		double AtS = -1.0;     // мир-время
	};
	bool bRecordPvpHits = false;               // самотест включает запись
	TArray<FAsdaPvpHitSeen> PvpHitsSeen;       // показанные PvP-удары
	TArray<int32> PvpNoticesSeen;              // нотисы/строки чата PvP (loc id) по порядку
	// ★ 18.10.2026 (ревизия Ф4, ВАЖНО 3) самотест `asda.Combat.SelfTestHitFrame`: оставшееся время (с) активных таймеров цифр
	//   своего замаха в порядке постановки (−1 — таймер уже не активен).
	void DebugOwnHitTimersRemaining(TArray<float>& Out) const;
#endif
	// ★ R5-fix8 (14.11.2026) самотест `asda.Combat.SelfTestObserverSync` (шаг своих часов): сколько раз перестановка своих кадров удара оставила
	//   уже наступивший таймер как есть (правка) и сколько раз завела его заново, сдвинув показ на кадр (прежнее поведение).
	int32 DebugOwnRetimeKeptDue = 0;
	int32 DebugOwnRetimeRearmedDue = 0;
	// ★ R5-fix8 самотест `asda.Fx.SelfTestVitalsState`: число показанных цифр дельты HP/MP (`OnVitalsFx`).
	int32 DebugVitalsDigits = 0;
private:
	// PvP-серия одного атакующего (зеркало эталонных очередей `playerArray` / второго канала и признака `field11C`).
	struct FAsdaPvpSeries
	{
		TArray<int32> Queue;        // очередь урона (снимается по значению на кадр удара)
		TArray<int32> SkIds;        // второй канал: навык (−1 — пусто; < 0 — по атакующему)
		TArray<int32> SkDmg;        // второй канал: урон
		uint8 Flags = 0;            // признак серии чужого (8 — после 4202, 0x40 — в серии)
		int32 TargetUserIdx = -1;   // цель-персонаж
		int32 WeaponSub = -1;       // подтип оружия атакующего
		bool bOwn = false;          // атакующий — я
	};
	TMap<int32, FAsdaPvpSeries> PvpSeries;   // userIdx атакующего → серия
	FTSTicker::FDelegateHandle PvpTickHandle;   // покадровое снятие значений на кадрах удара (`PvpHitFrameTick`)
	// Покадрово: у каждого атакующего PvP-серии — пройденные кадры удара клипа → по значению очереди на кадр.
	bool PvpHitFrameTick(float Dt);
	// Наполнить серию из пакета (bReset — сначала сбросить очередь и второй канал).
	void PvpFillSeries(FAsdaPvpSeries& S, const FAsdaPvpAttackPkt& P, bool bReset);
	// ★ 18.10.2026 (ревизия Ф4, мелочь 4): атакующий «занят» — эталонный `charIsActionStateNonCombat` (МИСНЕЙМ: 1 = персонаж ЗАНЯТ,
	//   состояние вне списка покоя 0/1/6–9/29/33–47/51/60–68; разбор у вызова в `OnPvpAttackStart`). Порт: свой — PvP-атака
	//   (ожидание 4202, подход, замах), цикл замаха или клип навыка; сосед — цикл/клип замаха или навыка.
	bool IsPvpAttackerBusy(bool bOwn, const class AAsdaPlayerController* PC, const class AAsdaPlayerCharacter* Atk) const;
	// ★ 18.10.2026 (ревизия Ф4, ВАЖНО 1): атакующий перешёл к бою с мобом (4027/4029, отказ боя с мобом) — его PvP-серия снимается;
	//   свой во время PvP-атаки — нет (поздний ответ прошлого боя с мобом; разбор у вызова в `OnCombatAnim`).
	void ResetPvpSeriesOnMobFight(int32 AttackerUserIdx, const TCHAR* Why);
	// Один удар по персонажу: надпись по роли и режиму показа, строки в чат, вычет HP, эффект, звук.
	void ResolvePvpHit(int32 AttackerIdx, int32 VictimIdx, int32 Raw, int32 CritBonus, int32 SkId, int32 SkDmg, int32 WeaponSub, int32 Step);
	// Эталонный `CCharacter_TakeDamage` вид 1 по копии HP персонажа.
	void ApplyPvpTakeDamage(int32 UserIdx, int32 Dmg, const TCHAR* Src);
	// Боевая строка в чат с подстановками (дорожка A, 10 000 мс, цвет эталона).
	void PostPvpChatLine(int32 NoticeId, const FString& Name, int32 Num, bool bHasNum, const FColor& Col);
	// Имя персонажа по userIdx (свой — имя своего персонажа; нет — пусто).
	FString PvpNameOf(int32 UserIdx) const;
	// Подтип оружия персонажа по userIdx (−1 — нет записи).
	int32 PvpWeaponSubOf(int32 UserIdx) const;
	// ★★★ 17.10.2026 (ревизия Ф2, мелочь 5): таймеры цифр/выстрелов СВОЕГО замаха с привязкой к кадру клипа (разбор у
	//   `AAsdaPlayerCharacter::ClipTargetFromDelay`). Смена скорости отрезка своего замаха переставляет их (`RetimeOwnHits`).
	struct FAsdaOwnHitTimer
	{
		FTimerHandle H;             // таймер показа
		int32 Serial = MIN_int32;   // номер прохода клипа цели (MIN — без привязки)
		float ClipPos = 0.f;        // позиция кадра в проходе (клип-с)
		int32 SwingGen = 0;         // ★ 18.10.2026 (ВАЖНО 3): номер замаха постановки (`AAsdaPlayerCharacter::GetAttackSwingGen`)
		int32 Id = 0;               // ★ 18.10.2026: номер таймера (переживает перестановку; учёт поставленных ударов)
		int32 Rid = -1;             // ★ ревизия 18.10 (п.11): цель удара (−1 — не задана)
		TFunction<void()> Fn;       // показ (цифра / выстрел)
	};
	TArray<FAsdaOwnHitTimer> OwnHitTimers;             // поставленные таймеры своего замаха
	TWeakObjectPtr<class AAsdaPlayerCharacter> OwnRetimeChar;   // свой персонаж, на чей `OnSegmentRateChanged` подписан мир
	FDelegateHandle OwnRetimeHandle;                    // подписка
	// Поставить таймер своего удара на DelayS с привязкой к кадру клипа Ch. ★ 18.10.2026: возвращает номер таймера (> 0; 0 — не
	//   поставлен): перестановка `RetimeOwnHits` заводит таймер заново (новый FTimerHandle), номер остаётся — по нему
	//   `LethalPlannedHitDelayS` читает честный остаток.
	//   ★ Ревизия 18.10 (п.11): Rid — цель удара (FIFO-план считает занятые кадры по этой цели).
	int32 ArmOwnHitTimer(class AAsdaPlayerCharacter* Ch, float DelayS, TFunction<void()> Fn, int32 Rid = -1);
	int32 NextOwnHitTimerId = 1;   // ★ 18.10.2026: следующий номер таймера своего удара
	// Переставить таймеры своего замаха под текущую скорость клипа.
	void RetimeOwnHits();
	// Показ одного результата удара над целью (сентинелы: -1 DODGE, -2 MISS, -3 IMMUNE, бит31 крит)
	//   + импакт-эффект попадания (SkillEffect_GetBasicAttackJebByClassWeapon: battk01..04 по оружию/удару).
	// ★ 17.10.2026 (Ф0, Р8): `AttackerUserIdx` — кто ударил (−1 — свой/неизвестен): флинч моба разворачивает его к соседу.
	//   ★ Ревизия 18.10 (п.1): PlannedSeq — номер поставленного удара (0 — не из плана): гашение по трупу по порядку ударов.
	void ShowHitResult(class AAsdaMobActor* Target, int32 RawDmg, bool bOurAttack, int32 WeaponSubType, int32 HitIdx,
	                   int32 AttackerUserIdx = -1, uint32 PlannedSeq = 0);
	// ★ 17.10.2026 (Ф0, Р2) [ПОРТ]: источник дельты HP моба — НАШ замах по нему (окно «замах в полёте» по этому rid открыто
	//   или только что закрылось, либо контроллер ведёт бой по этой цели). Иначе дельта рисуется только полосой.
	//   ★ Ф1 (п.7 проекта): по ленте попаданий — свой атакующий ЕДИНСТВЕННЫЙ с записью по мобу (ожидающей или закрытой не
	//   дольше `kOurDeltaSourceGraceS`); если по мобу бьёт и сосед — источник дельты не различить, авторства нет.
	bool IsOurSwingSourceForMob(const class AAsdaMobActor* M) const;
	// ★ 17.10.2026 (Ф0) [ОРИГИНАЛ]: конец серии СОСЕДА по отказу 4029 (`Recv_CastState` case 1/3/4/5/6/7, case 2 при HP цели
	//   ≤ 0, network.c:42084–42281): замах гаснет (`CChar_SetState(caster, 0, 20)`), очередь и цель серии сбрасываются.
	//   ★ Ф1 (ревизия Ф0 п.2/п.3/п.9): цель и её HP — из ПАКЕТА отказа (`UAsdaNetSubsystem::CombatRefused*`); код 2 при живой
	//   цели дописывает очередь пакета и закрывает серию (network.c:42204–42216); код 5 — покой только из боевого состояния.
	void StopPeerAttackSeries(int32 PeerUserIdx, uint8 Code);
	// Цифры+импакт по игроку-жертве (эталон Combat_ApplyDamage: кадр удара замаха моба).
	//   VictimUserIdx — userIdx жертвы из пакета (−1 = вычислить по актору); урон уходит в единый
	//   источник истины HP (UAsdaNetSubsystem::ApplyVitalsDamage), а не прямо в плашку.
	// `AttackerName` — имя БЬЮЩЕГО (моба): эталон кладёт в слот строки журнала именно его
	//   (`CClass_GetResourceFromField(pCaster)` в `Combat_ApplyDamage`).
	void ShowPlayerHit(AActor* Victim, int32 Damage, uint8 HitType, int32 VictimUserIdx = -1,
	                   const FString& AttackerName = FString());
	// ★ Единая точка боевой строки в журнал чата (нотисы 20011/20012/20013/20014/20015/1092/1159).
	void LogCombatNotice(int32 NoticeId, const FString& NameArg = FString(),
	                     int32 NumArg = 0, bool bHasNum = false);
	// Спавн импакт-эффекта (BP на базе AAsdaEffectActor) в мировой точке.
	//   `Scale` — эталонный `CEffectMgr_scaleAll` (см. `UAsdaSkillFxSubsystem::GetBasicAttackHitFxScale`):
	//   у цели-МОБА `%размера/100`, у цели-ПЕРСОНАЖА 1.5, у удара МОБА по игроку `scaleAll` нет ⇒ 1.0.
public:
	// ★ Спавн мирового эффекта по имени BP — ЕДИНАЯ точка. Публичная: её же зовёт HUD для
	//   эффекта повышения уровня (эталон Recv_LevelUp), чтобы не заводить второй спавнер.
	void SpawnImpact(const FString& BPName, const FVector& Loc, float Scale = 1.f);
private:
	// ★ AoI-деспаун мобов (эталон entityListUpdateFade@0xF78660: i[147]=10000мс рефреш каждой 4013-записью;
	//   истёк И сектор-дистанция >1, сектор = world/3000 = 30 тайлов → фейд 1с → удаление). Таймер 1с.
	void TickAoiDespawn();
	FTimerHandle AoiTimer;

	// ★★★ ПРЕДУПРЕЖДЕНИЕ О СЛОМАННОЙ ВЕЩИ (нотис 110303) — эталон `game_main.c:1508-1524`.
	//   Оригинал проверяет надетое РАЗ В 10 СЕКУНД (`timeGetTime() - g_dwDurabilityCheckLastTime
	//   > 0x2710`), перебирает 11 слотов экипировки и на ПЕРВОМ предмете с нулевой прочностью
	//   выводит сообщение и прекращает обход (`break`) — то есть не более одной строки за проход,
	//   сколько бы вещей ни было сломано.
	//   Гейт по виду предмета: `itemType <= 4 || (itemType >= 8 && itemType <= 9)`.
	void TickBrokenGearNotice();
	FTimerHandle BrokenGearTimer;
	// ★ КРУГ 10 (АВТО-AoI): клиент сам оценивает серверный AoI по НАБЛЮДАЕМЫМ пакетам — каждая
	//   ПРИМЕНЁННАЯ запись 4013/4019 (прошла гейт +27; отброшенные гейтом НЕ считаются — там мусор)
	//   даёт Чебышёв-дистанцию |mobTile − playerTile|.
	//   Эффективный сектор = max(30, МАКСИМУМ ЗА СЕССИЮ КАРТЫ): 30 = эталон → на стоке поведение 1:1.
	// ★ КРУГ 11: порог МОНОТОННЫЙ (расширение мгновенно в Note, СЖАТИЯ НЕТ; сброс к 30 по 4003).
	//   Прежнее окно 60 с со сжатием на сервере без повторных 4013 (наш, AoI=вся карта) через
	//   ~100 с без наблюдений (город/AFK) безвозвратно гасило всех мобов — сервер ре-транс не шлёт.
	//   Кольцо 60 с оставлено ТОЛЬКО как диагностика для `asda.Mobs.Dump`.
	void NoteAoiObservedTile(int32 TileX, int32 TileZ);   // зов: применённая запись 4013/4019
	void AdvanceAoiBuckets(int64 NowSec);                 // сдвиг кольца на текущую секунду
public:
	int32 AoiWindowMax() const;                           // максимум дистанций за окно 60 с (диагностика)
	// Состояние авто-оценки (public: печатается в `asda.Mobs.Dump`).
	struct FAsdaAoiAuto
	{
		int32 EffTiles = 30;       // действующий авто-сектор в тайлах (мин. 30 = эталон; макс. за сессию карты)
		int32 Buckets[60] = {};    // кольцо посекундных максимумов Чебышёв-дистанций (окно 60 с, диагностика)
		int64 CurSec = -1;         // абсолютная секунда текущего бакета
		double LastObsS = 0.0;     // GetTimeSeconds последнего наблюдения (для дампа)
		int32 LastLoggedCVar = INT32_MIN;   // последний залогированный режим CVar (лог смены)
	};
	FAsdaAoiAuto AoiAuto;
private:
	// 4007/4009: свой userIdx → NetMove/NetSnap павна; чужие → моб по NetRenderId / игрок по userIdx.
	//   ★ 27.09.2026: + подтип хода (@+3) и renderId цели хода (@+25) — см. `FOnAsdaEntityMove`.
	void OnNetEntityMove(int32 UserIdx, int32 Result, FVector2D CurGame, FVector2D DstGame, float Speed, bool bStop,
		int32 SubType, int32 MoveTgtRid);
	// ★★★ 27.09.2026 — грант своего хода по ЭТАЛОННОЙ схеме (`Recv_EntityMove@0xF3C2C0` case 1, 0xF3C362…0xF3C5C1;
	//   ключ `asda.Move.EtalonSpeedSync`). Подгонка скорости под серверное время, старт от позиции актора,
	//   при опоздании — 4008 в dst и снап. Разбор формулы — в определении.
	void ApplySelfMoveGrantEtalon(class AAsdaPlayerCharacter* Ch, class AAsdaPlayerController* PC, UAsdaNetSubsystem* Net,
		const FVector2D& CurGame, const FVector2D& DstGame, float Speed, int32 SubType);
	// ★ 4062 позиционный синк: тип-байт, цель и СКОРОСТЬ (единиц/мс). Текущую точку берём у актора —
	//   в пакете её нет (эталон netRecvEntityPosSync тоже берёт её из сущности).
	void OnNetEntityPosSync(int32 UserIdx, uint8 Kind, FVector2D DstGame, float Speed);
	// ★ 6071 `Recv_CharMoveToCell` — новая скорость движения сущности (посадка/слезание с транспорта,
	//   смена состояний скорости). Меняет темп УЖЕ ИДУЩЕГО шага, цель остаётся прежней.
	void OnNetEntitySpeed(int32 UserIdx, float Speed);
	// ★ СЕТЕВАЯ АРХИТЕКТУРА (как оригинал): запечённые акторы = превью редактора/ШАБЛОНЫ (в игре скрыты);
	//   игровые сущности = рантайм-КОПИИ шаблонов (SpawnActor Template) по пакетам. Реестры rid→копия:
	TMap<int32, TWeakObjectPtr<class AAsdaMobActor>> NetMobs;
	TMap<int32, TWeakObjectPtr<class AAsdaNpcActor>> NetNpcs;

public:
	// ★★★ КРУГ 8 — СЧЁТЧИКИ ОТБРАКОВКИ ЗАПИСЕЙ МОБОВ (за сессию, печатаются в `asda.Mobs.Dump`).
	//   Без них невозможно ответить на вопрос «сколько сервер прислал / сколько встало в мир / что
	//   и почему потеряли» — именно эта слепота держала баг 7 кругов.
	struct FAsdaMobStats
	{
		int32 Pkt4013 = 0;        // батчей 4013 принято
		int32 Pkt4019 = 0;        // одиночных 4019 принято
		int32 RecTotal = 0;       // записей ВСЕГО в батчах (по шагу 173)
		int32 RecGateOff = 0;     // отброшено гейтом *(u32)(rec+27)==0 (эталон: сущность не создаётся)
		int32 RecBadRid = 0;      // отброшено: rid < 0
		int32 Created = 0;        // создано новых акторов мобов
		int32 Updated = 0;        // применено к уже существующему rid
		int32 FailNoModel = 0;    // не создан: нет модели по type-id (DT_MobNames / ассеты)
		int32 FailNoTemplate = 0; // не создан: нет ни модели, ни запечённого шаблона
		int32 FailSpawn = 0;      // не создан: SpawnActor вернул nullptr
		int32 AoiDespawn = 0;     // ушло по AoI (10 с без 4013 + сектор дальше ±1)
		int32 Deaths = 0;         // смертей (HP<=0)
		int32 Revives = 0;        // регенов (труп → жив)
	};
	FAsdaMobStats MobStats;

	/** ★★★ ОБЩАЯ ТОЧКА «слоты званий → плашка над головой», доступная извне сети.
	 *  Нужна для СНЯТИЯ ЗВАНИЯ ВЖИВУЮ: когда сервер прислал только новую маску 6066 (без 6068),
	 *  окно званий обнуляет надетый слот у себя и обязано протолкнуть это в плашку ТЕМ ЖЕ ПУТЁМ,
	 *  что и приход 6068 — иначе над головой останется звание, которого у игрока уже нет.
	 *  У ЧУЖИХ персонажей ту же работу делает сам приход 6068 (рассылка сервера).
	 *  Пустой слот = −1. */
	void ApplyCharTitleSlots(int32 UserIdx, int32 TitleId0, int32 TitleId1, int32 Rank)
	{
		OnCharTitle(UserIdx, TitleId0, TitleId1, Rank);
	}
private:

	// ── ★ ДРУГИЕ ИГРОКИ (remote_player_display_re.md): спавн по снапшоту 4005/4065, движение 4007/4009
	//   по userIdx, деспавн 4011, смена частей экипа 0xFB5/0xFB7, неймплейт = JUI-окно TARGETMARK_MAN1
	//   (id 1660; имя = контрол TARGETMARK_MANNAME1/1661). Спавн/деспавн — через UAsdaFadeComponent.
	struct FAsdaNetPlayerEntry
	{
		TWeakObjectPtr<class AAsdaPlayerCharacter> Actor;
		FAsdaWornParts Parts;   // текущая внешность (для точечных обновлений 0xFB5/0xFB7)
		uint8   Sex = 1;
		FString Name;
		int32   CharId = 0;     // id персонажа в БД (снапшот 4005/4065) — по нему матчит клан-сброс 4133
		int32   ClanId = -1;    // клан (пакет 4109) — по нему матчит флаг клана 6542
		// ★ КЛАСС и УРОВЕНЬ соседа — 4005@38/@40 и 4065 rec@55/@57 (эталон кладёт их в `char+1080`
		//   и `char+1082`). Оба нужны ПАНЕЛИ ЦЕЛИ: класс выбирает ячейку атласа `char_emblems`
		//   (`HUD_UpdateTargetFrame`, ui_core.c:77548-77584), уровень идёт в спрайтовые цифры
		//   (`HUD_TargetLevelDigits`). До 2026-08-24 оба поля приходили в `FAsdaRemotePlayer`,
		//   но нигде не сохранялись — панель цели не смогла бы показать ни эмблему, ни уровень.
		int32   ClassId = 0;    // char+1080: 1..7 / 12..18 / 23..29 — три ветки классов; 0 = «нет класса»
		// ★ ПОДКЛАСС и СОЗВЕЗДИЕ соседа (17.09.2026, окно `OTHERCHARA_DATA` 4200). Оба байта лежат
		//   в снапшоте 4005 (@39 и @47) и оба нужны `CPlayerInfoWin_SetData@0xDA7E30`: подкласс
		//   решает, ЧЬЁ имя профессии печатать (char+1081, ноль = печатаем базовый класс),
		//   созвездие (char+1120) даёт название знака и выбирает ОДНУ из трёх иконок влечения.
		//   ⚠ «Фракция/раса» в разборе окна — МИСНЕЙМ: 110631 над этим полем читается
		//   «Знак зодиака», а таблицы за ним — `Constellation.BIN` / `Constellation_Match.BIN`.
		int32   SubClassId = 0; // char+1081; 0 = подкласса нет
		int32   Constellation = 0; // char+1120; 1..12, 0 = знак не выбран
		int32   Level = 0;      // char+1082
		// ★ ЗАТОЧКА (свечение оружия/щита, weapon_glow_re.md): байт +0x18 57-байтного инстанса.
		//   Приходит в 4005 (11×5б @+96: itemId dword + grade byte) и в 4021/0xFB5 (body+8).
		int32   WeaponGrade = 0;   // уровень заточки надетого оружия (слот 9)
		int32   ShieldItemId = 0;  // itemId щита (слот 8) — свой рендер щита ещё не портирован, свечение считаем
		int32   ShieldGrade = 0;   // уровень заточки щита
	};
	TMap<int32, FAsdaNetPlayerEntry> NetPlayers;   // userIdx → актор чужого игрока
	// ★ Ростер отряда (сессионные userNum согруппников БЕЗ себя) — источник `Party_FindMemberByUserIdx`
	//   эталона. Пока подсистемы отряда нет, множество пустое; см. `SetPartyRoster`.
	TSet<int32> PartyUserIdxs;
	// ★★★ Кто скрыт GM-невидимостью (сессионные userIdx, свой в том числе). Живёт ровно столько,
	//   сколько сессия: чистится в `ResetSessionState` вместе с реестром соседей.
	TSet<int32> GmHiddenUserIdx;
	void SpawnRemotePlayer(const struct FAsdaRemotePlayer& P);   // новый сосед → павн+внешность+неймплейт+фейд-ин
	// 0xFB5/0xFB7 (4021/4023): смена одной части экипа соседа. `PartType` — ЭКИП-слот (`EAsdaEquipSlot`,
	//   он же `itemType`), НЕ render-part. Последний параметр — УРОВЕНЬ ЗАТОЧКИ (исправлен миснейм
	//   `Color`: эталон network.c:40631 пишет этот байт в инстанс+0x18 как grade, цветом он не был).
	void OnCharPartUpdate(int32 UserIdx, int32 PartType, int32 ItemId, uint8 Grade);
	// 4036 соседа: новый уровень → в реестр (эталон пишет его в `CCharacter+1082`).
	void OnRemoteLevel(int32 UserIdx, int32 Level);
	// 6068: титулы игрока (ОБА слота) + rankValue → плашка (цвета по грейду редкости из title_list).
	void OnCharTitle(int32 UserIdx, int32 TitleId0, int32 TitleId1, int32 Rank);
	// 6078: только rankValue (бейдж позиции в лидерах, контрол 1668).
	void OnCharRank(int32 UserIdx, int32 Rank);
	// 4109 (чужой, userIdx>=0) / 4105 (свой, userIdx==-1): клан → плашка. 4133: сброс по charId+имени.
	void OnClanInfo(int32 UserIdx, int32 ClanId, const FString& ClanName, const FString& ClanRank);
	void OnClanReset(int32 CharId, const FString& CharName);
	// 6542: флаг (эмблема) клана — применить ко всем акторам с этим clanId (эталон обходит все сущности).
	void OnClanFlag(int32 ClanId, bool bFlagOn, const TArray<uint8>& Flag40);
	// ★ 4135: снапшот клана по charId (ОСНОВНОЙ путь клана соседей). Актор мог ещё не заспавниться
	//   (пакет идёт в связке 5417→…→4135, а спавн у нас отложен) — тогда в кэш, применение при спавне.
	void OnClanSnapshot(int32 CharId, int32 ClanId, const FString& ClanName, const FString& ClanRank,
		bool bFlagOn, const TArray<uint8>& Flag40);
	struct FAsdaPendingClan
	{
		int32   ClanId = -1;
		FString ClanName, ClanRank;
		bool    bFlagOn = false;
		TArray<uint8> Flag40;
	};
	TMap<int32, FAsdaPendingClan> PendingClanByCharId;   // charId → клан, пришедший до спавна актора
	// ★ 06.10.2026 (повторная ревизия портрета, R2-СРЕДН-1): ПОСТОЯННОЕ ЗЕРКАЛО кланового блока СВОЕГО персонажа.
	//   В оригинале `CCharacter` своего с блоком +2408..+2545 живёт всю сессию, а сервер шлёт 4135 своему РАНЬШЕ 4003
	//   (`CharMake@0x543A10`: GameServer.c:124111 → :124121). Порт пересоздаёт павн на каждом спавне — поэтому блок
	//   держим здесь и применяем при КАЖДОМ спавне своего павна. Пишут: 4135/6153 (снимок), 4105/4109 (имя/id), 6542
	//   (флаг); обнуляют: 4107/4117/4133 (OnClanReset своего) и полный сброс сессии.
	FAsdaPendingClan SelfClan;
	bool bSelfClanKnown = false;   // зеркало хоть раз заполнено в этой сессии
	// Свой павн жив (не уничтожен травелом/спавном) — единая проверка веток «это свой персонаж».
	AActor* LiveSelfActor() const { return IsValid(LocalPlayerActor) ? LocalPlayerActor.Get() : nullptr; }
	// Применить клан-данные к плашке актора (общая точка 4135/кэша).
	void ApplyClanToActor(AActor* A, const FAsdaPendingClan& C);
	// ★ Титул/ранг (6068/6078), пришедшие ДО спавна актора (сервер шлёт 6068 и для игроков ВНЕ AoI —
	//   лог: uid без спавна). X=titleId0, Y=titleId1, Z=rankValue. Применение при спавне.
	TMap<int32, FIntVector> PendingTitleByUserIdx;
	// Свой клан (для 6542 на своей плашке данных нет — плашка над собой скрыта; храним для HUD-портрета).
	int32 SelfClanId = -1;
	// Актор игрока по userIdx (чужой из NetPlayers или свой павн).
	AActor* FindPlayerActor(int32 UserIdx) const;
	// Имя своего персонажа (из char-list при спавне) — сверка клан-сброса 4133.
	FString SelfCharName;
	// Заполнить над-головную плашку актора (штатный UAsdaNameplateComponent из конструктора) именем.
	//   Сама плашка = порт JUI-окна 1:1 (MON/MAN1), живёт на акторе и видна в редакторе.
	class UAsdaNameplateComponent* FillNameplate(AActor* Owner, const FString& Name);

public:
	// Имя СВОЕГО персонажа (из char-list при спавне). Нужно проверке админ-режима (ник с префиксом GM_).
	UFUNCTION(BlueprintCallable, Category="Asda|World")
	const FString& GetSelfCharName() const { return SelfCharName; }
	/** ★ 09.11.2026: только самотесты (гейт «имя GM_» GM-меню) — подставить своё имя без спауна. */
	void SetSelfCharNameForSelfTest(const FString& Name) { SelfCharName = Name; }

	// Свой клан (−1 = нет). Нужен проверке доступности клан-чата (эталон: LocalPlayer clanId >= 0).
	UFUNCTION(BlueprintPure, Category="Asda|World")
	int32 GetSelfClanId() const { return SelfClanId; }

	// ★ ОБРАТНЫЙ поиск: актор игрока → сессионный userNum (i16). −1, если актор не игрок/не в реестре.
	//   Симметричен `ActorByUserIdx`: СВОЙ павн → `SelfUserNum` (0 — валидное значение), иначе ключ
	//   в `NetPlayers`. Нужен пакетам таргета (6538/4201), презентации смерти и применению навыков:
	//   заявка 5256 при цели-ПЕРСОНАЖЕ кладёт в hit-запись именно userNum цели
	//   (`Skill_SendSkillUseOnTarget@0x9FFB10` skill_buff.c:2768 и `skillSendUseSkillPacket@0xA02AB0`
	//   skill_buff.c:3858 — оба `Char_GetUserNum(targetChar)`).
	//   ⚠ Это userNum (i16), а НЕ charId (i32) — закон charid-vs-usernum.
	//   ★ 2026-08-10: сюда слит дубль `UserIdxByActor` — раньше существовали ОБЕ функции, и эта
	//   при цели «я сам» возвращала −1.
	int32 FindUserIdxByActor(const AActor* A) const;
	// ★ 06.11.2026: транспорт игрока `UserIdx` числится за ним (кэш `MountByUserIdx`) — см. `AAsdaPlayerCharacter::IsMountHeldByServer`.
	bool HasServerMount(int32 UserIdx) const { return MountByUserIdx.Contains(UserIdx); }

	// ══════════ ★★★ МНОЖИТЕЛЬ ТЕМПА АТАКИ (скорость анимаций боя) ══════════════════════════════
	//   Эталон: `CCharacter_AttackAnimTick@0x9A2200` (game.c:36385-36470) и три других пути атаки
	//   (`Player_ProcessAttackTick@0x99F2A0` — game.c:35138-35219, `CCharacter_ResolveBasicAttack` —
	//   92393-92452, `Skill_SpawnHitFeedback` — 33531-33605). Там это
	//   МНОЖИТЕЛЬ ШАГА ВРЕМЕНИ часов клипа замаха:
	//       if (dmgMultiplier != 0.0) animSpeed = (dmgMultiplier + 1.0) * animSpeed;
	//   а от этих часов у оригинала зависят ЧЕТЫРЕ вещи: момент урона, момент звука замаха,
	//   продвижение шага комбо и ОТПРАВКА ПАКЕТА АТАКИ В СЕТЬ. То есть это множитель ЧАСТОТЫ
	//   УДАРОВ, а не украшение картинки.
	//
	//   Сумма вкладов чисто аддитивная (`M`), итог = `M + 1`:
	//       + состояние 27                                     (без условий)
	//       + состояние 174, если подтип оружия == 11           (баллиста)
	//       − состояние 28                                     (без условий)
	//       + состояние 233, если подтип оружия == 3            (двуручный меч)
	//       − состояние 247                                    (без условий)
	//       + сумма `LinkedGroupCode/100` надетых предметов слотов 0,1,2,3,4,8,9
	//         ★ 15.10.2026 [ОРИГИНАЛ]: по умолчанию ВЫКЛЮЧЕНО (`asda.Combat.AttackTempoItems 0`) — NpcServer этого
	//         слагаемого не знает, и вещи гнали замах быстрее серверной длительности удара (до +340 % у шлемов).
	//
	//   ★ ЕДИНАЯ ТОЧКА НА ВЕСЬ ПОРТ — и это ИСПРАВЛЕНИЕ ВТОРОГО ДЕФЕКТА ОРИГИНАЛА: у него из
	//   четырёх путей атаки один (`CCharacter_ResolveBasicAttack@0xA47DC0`, авто-режим локального
	//   игрока, game.c:92393-92452) считает ТОЛЬКО ЧЕТЫРЕ вклада — без состояния 247 и без
	//   предметов, то есть у одного и того же персонажа темп зависел от того, каким путём пришла
	//   атака. У нас формула одна для всех путей и для всех персонажей (свой и соседи).
	//
	//   Работает и для СЕБЯ, и для СОСЕДЕЙ: список состояний приходит по проводу на каждого
	//   (`UAsdaEffectsSubsystem`), надетое своё лежит в контейнере 3 инвентаря, чужое — в реестре
	//   `NetPlayers` (там как раз слоты 0..4 + щит 8 + оружие 9, ровно нужный набор).
	//   Ключ отката к «темп всегда 1»: `asda.Combat.AttackTempo 0`.
	float GetAttackTempoMul(const class AAsdaPlayerCharacter* Ch) const;

	// ★ 15.10.2026 (ритм атаки): ВЕЛИЧИНА СОСТОЯНИЯ в целых процентах — `уровень · S + база` из записи навыка-источника
	//   (тот же счёт, что у вкладов темпа выше, одна функция `AsdaStatePct`). Нужна серверным часам атаки
	//   (`UAsdaAttackRhythmSubsystem`): NpcServer складывает бонус скорости B целыми процентами. 0 — состояния нет.
	//   OutStack — стек записи (+5); nullptr — не нужен.
	int32 GetStateBonusPct(int32 UserIdx, int32 StateId, int32* OutStack = nullptr) const;

	// ══════════ ★★★ МНОЖИТЕЛЬ ТЕМПА АНИМАЦИЙ НАВЫКОВ И ЭФФЕКТОВ (`animSpeedScale`) ═════════════
	//   ВТОРОЕ, ОТДЕЛЬНОЕ поле оригинала — `CCharacter + 0x500` (float с индексом 320). Путать его
	//   с множителем темпа атаки выше нельзя: разные формулы, разные потребители, и вместе они
	//   не применяются НИКОГДА (доказательство — ниже, пункт «границы»).
	//
	//   ★ ГДЕ СЧИТАЕТСЯ (эталон, оба места — пересборка внешности при смене экипировки):
	//       `CCharacter_LoadEquipPart@0x9E6D80`     (game.c:64371-64412) — ассемблерный вариант,
	//       `CCharacter_SetupModelFromItem@0x9E97A0` (game.c:65121-65138) — тот же код декомпилирован:
	//           statWeightSum = 0.0;
	//           for (i = 0; i < 10; ++i) if (i != 5 && i != 6 && i != 7)     // слоты 0,1,2,3,4,8,9
	//               rec = CItemManager_GetItemRecord(…, слот[i]);
	//               if (rec && (rec->itemSubType != 10 || rec->itemVariant != 1
	//                           || rec->itemSubType == 10 && rec->itemVariant == 2
	//                           || (double)rec->linkedGroupCode <= 0.0))
	//                   statWeightSum += (double)rec->linkedGroupCode / 100.0;
	//           self->animSpeedScale = statWeightSum + 1.0;
	//       Ещё два писателя: `CCharacter_ctor@0xA1B440` (game.c:79344) ставит 1.0, а
	//       `charCopyFromNetStruct@0xEFD880` (game.c:528645) переносит уже посчитанное значение.
	//   ★★★ ТРЕТИЙ ПИСАТЕЛЬ — СОСТОЯНИЕ 78, И ОН ПЕРЕБИВАЕТ ПРЕДМЕТЫ ЦЕЛИКОМ.
	//       `Skill_ApplyAttackSpeedBuff@0x99AFC0` (game.c:33330-33339; имя ВРЁТ — скорости атаки
	//       функция не касается) зовётся из тика состояния 24 КАЖДЫЙ КАДР применения навыка
	//       (`charUpdateActionStateTick@0xA216B0` game.c:81495 и его вторая копия `@0xA220E0`
	//       game.c:81867 — у второй имя тоже врёт, IDA зовёт её `Camera_UpdateListener`):
	//           entry = CList_FindByTypeSub(char + 638, 78, 0);   // `+638` = тот же список состояний,
	//                                                             //   что и `&char->gap_96A[128]`
	//           if (entry && (rec = CSkillMgr_GetSkillRecord(…, *(i16 *)(entry + 2))))
	//               char->animSpeedScale = (entry[9] * rec->s1PerLevel + rec->s1Base) / 100.0;
	//               char->animSpeedScale = char->animSpeedScale + 1.0;
	//       Носители состояния (живые данные `skill_info`, 1255 записей): навыки 630, 839, 1060
	//       «Повышение скорости навыков», слот 0, S1 = 0 + 2·уровень, кап 5 ⇒ до +0.10.
	//   ⚠ ЭТОТ ПИСАТЕЛЬ НАШЁЛСЯ ТОЛЬКО ВТОРЫМ МЕТОДОМ (закон false-negatives-method): он пишет поле
	//       формой `*((float *)this + 320)`, и поиск по имени `animSpeedScale` его НЕ ВИДИТ — по
	//       имени находятся лишь 165 вхождений из ~340. Проверять надо ОБА написания.
	//   ★ Уточнение второй ревизии: «иных СМЫСЛОВЫХ писателей нет». Пятая форма записи существует —
	//       типизированная копия персонажа `charCopyFromNetStruct@0xEFD880` переносит поле как есть,
	//       вместе с соседним `atkSpeedMul`; семантики она не добавляет.
	//   ★ У эталона отложен не только СПАД баффа 78, но и его ПОДЪЁМ: поле пишется только из тика
	//       состояния 24, то есть пока персонаж не начнёт применять навык, бафф в поле не попадает
	//       вовсе. Мы считаем значение на каждый запрос — обе половины «залипания» не воспроизводим.
	//       ★ Тот же счёт по ЧИТАТЕЛЯМ (уточнено независимой ревизией 16.09.2026): по имени поля
	//       видно 109 функций, по сырой форме — ещё 167, объединение ~275. Числа, полученные ОДНИМ
	//       способом поиска, в этой теме систематически занижены — не опирайтесь на них при доделке.
	//       БОЛЬШЕ ПИСАТЕЛЕЙ У ПОЛЯ ПЕРСОНАЖА НЕТ — перебраны обе формы во всём срезе.
	//   ★ ПРЕДМЕТНЫЙ ЦИКЛ ЗДЕСЬ — ТОТ ЖЕ, что и у темпа атаки (game.c:36452-36468), байт в байт,
	//       включая исключение удочек. НО СОСТОЯНИЙ (27/174/28/233/247/12) в нём НЕТ ВООБЩЕ:
	//       в обоих писателях между `sub_922F01` и присваиванием нет ни одного `CList_FindByTypeSub`.
	//       Поэтому вклад предметов даёт ОДИН общий помощник (`SumWornItemsTempoTerm`), а состояния
	//       добавляет только `GetAttackTempoMul` — второй копии формулы в порту нет.
	//
	//   ★ НА ЧТО ВЛИЯЕТ (287 функций-потребителей; все — ветка НАВЫКОВ, ни одной из ветки атаки).
	//     ⚠ ЧИСЛО ПОЛУЧЕНО ВТОРЫМ МЕТОДОМ. По имени поля находятся лишь 109 функций — остальные 178
	//     читают его сырой формой `*((float *)pChar + 320)`. Точный счёт: 259 функций умножают шаг
	//     времени на поле (297 строк), 50 штампуют им поле узла эффекта (57 строк), пересечение есть.
	//       (1) ЧАСЫ КЛИПА НАВЫКА. 259 тиков навыков и их помощников делают
	//               CObject_SetFloatDispatch(&слот.анимобъект, dt * animSpeedScale);
	//               progress = Object_GetFloat13(&слот.анимобъект);
	//           и по `progress` (это КЛИП-ВРЕМЯ в мс) сравнивают пороги фаз COMBO0/1/2, спавнят
	//           эффекты, играют звуки и завершают показ. Образец разобран целиком на
	//           `charFxAutoSkillTick@0xAB6570` (game.c:138717-138790).
	//       (2) ОБРАТНЫЙ ОТСЧЁТ ЗАРЯДА: `char_updateChargeStateVariant@0xA15DD0` (game.c:77202) и
	//           `char_updateChargeState@0xA162E0` (game.c:77304) вычитают `dt * animSpeedScale`.
	//       (3) СОБСТВЕННОЕ ВРЕМЯ ЗАСПАВНЕННОГО ЭФФЕКТА: 50 мест кладут значение в поле узла
	//           эффекта `+0x80C` (индекс 515 — последний dword узла размером 0x810; конструктор
	//           `CEffectNode_Ctor@0xF5CA70` ставит туда 1.0, game.c:565174). Читает его ровно одна
	//           точка — `objectMgr_TickEffects@0xF57990` (game.c:562489):
	//               objAnimRotateUpdateSpeed(узел + 12, времяМс, *(float *)(узел + 2060));
	//           а тот (`@0x114E260`, game.c:860296) считает `t = (now − start)/1000 * scale` и по
	//           этому `t` крутит ВСЁ: поворот, накопители эмиттеров и ВОЗРАСТ (значит и время
	//           жизни). То есть эффект целиком идёт быстрее ровно во столько же раз.
	//       ⚠ Эффекты УДАРА поле НЕ получают: ни `CSkillCast_ApplyEntityHitEffect@0xA7B4A0`, ни
	//           `CSkillCast_ApplyAoEHitEffects@0xA75EF0`, ни `Skill_PlayHitEffectOnChar/OnEntity/
	//           OnChar_Snd` его не пишут. Ускоряется только эффект КАСТА — и в порту так же.
	//           ★ Вторая ревизия: помощников удара в срезе не пять, а СЕМЬ — сверх названных есть
	//           `skillApplyHitEffects@0xA35C10` и `Skill_PlayHitEffectOnEntity_Snd@0xA7EE90`.
	//           Обе тоже проверены по всем формам записи: вхождений нет. Вывод прежний, но опирался
	//           он на неполный перебор — а это такая же ошибка, как неверный вывод.
	//
	//   ★ ГРАНИЦЫ (проверено, а не предположено): базовой атаки это поле НЕ касается.
	//       `charUpdateActionStateTick` (game.c:81423 и :81442) отдаёт в `CCharacter_AttackAnimTick`
	//       и `Player_ProcessAttackTick` СЫРОЙ `deltaTime`, а те умножают его на `atkSpeedMul`
	//       (соседнее поле +0x504) — и оно МЁРТВОЕ: пишут его только конструктор (1.0, game.c:79345)
	//       и копирование персонажа, так что оно всегда ровно 1.0. Двойного применения нет.
	//
	//   Ключ отката к «темп анимаций навыков всегда 1»: `asda.Combat.AnimSpeedScale 0`.
	//   Возвращает 1.0, когда считать нечем (мира нет, персонажа нет в реестре, сумма нулевая).
	float GetAnimSpeedScale(const class AAsdaPlayerCharacter* Ch) const;

	// ★ Снимок реестра соседа для UI (панель цели `PVP_TARGET`): имя, класс (`char+1080`),
	//   уровень (`char+1082`), клан. false = игрока с таким userNum в реестре нет (вне AoI или
	//   это СВОЙ павн — у себя эти поля живут в `UAsdaNetSubsystem::Self*`).
	//   Отдельный геттер вместо публичного доступа к `NetPlayers`: реестр остаётся приватным,
	//   а у UI нет соблазна писать в него (закон port-shared-points — одна точка чтения).
	bool GetNetPlayerInfo(int32 UserIdx, FString& OutName, int32& OutClassId, int32& OutLevel,
		int32& OutClanId) const;

	// ★ ВТОРАЯ ПОРЦИЯ полей соседа — подкласс (char+1081) и созвездие (char+1120). Отдельный
	//   геттер, а не расширение сигнатуры выше: у той 4 вызывающих (панель цели и др.), которым
	//   эти поля не нужны, и менять им контракт ради одного окна — лишний риск. Единственный
	//   потребитель — `UAsdaOtherCharaSubsystem` (окно `OTHERCHARA_DATA` 4200).
	//   false = соседа с таким userNum в реестре нет (вне AoI, или это свой павн).
	bool GetNetPlayerClassExtra(int32 UserIdx, int32& OutSubClassId, int32& OutConstellation) const;

	// ★ ПОЛ соседа (char+1079; 1 = муж, 2 = жен). Нужен выбору мужского/женского варианта иконки
	//   предмета: эталон передаёт этот байт в `CItemManager_GetImgOptionPtr`
	//   (`uiRefreshItemOptionSlots@0xDA77E0`, окно `OTHERCHARA_DATA` 4200). Реестр его уже хранит
	//   (`FAsdaNetPlayerEntry::Sex`), но наружу не отдавал — угадывать пол по имени актора нельзя.
	//   false = соседа с таким userNum в реестре нет.
	bool GetNetPlayerSex(int32 UserIdx, int32& OutSex) const;

	// ★ charId (i32) соседа по сессионному userNum (i16); 0 = такого соседа нет.
	//   Нужен пакетам 5478 и 4302 меню по игроку — они несут ОБА идентификатора (charid-vs-usernum).
	int32 GetNetPlayerCharId(int32 UserIdx) const;

	// ★ ДИАГНОСТИКА: поставить ФЕЙКОВОГО соседа-игрока без сервера (консоль asda.World.FakePeer).
	//   Нужна живой проверке контекстного меню по ПКМ: без сервера чужих игроков в мире нет, а
	//   проверять меню «вызовом обработчика» запрещено — нужен реальный актор под курсором.
	//   Идёт тем же путём, что и снапшот 4005/4065 (`SpawnRemotePlayer`), поэтому проверяет и
	//   реестр `NetPlayers`, и плашку, и поиск по userNum.
	void DebugSpawnFakePeer(int32 UserNum, const FString& Name, const FVector2D& PosGame);
	// ★ 10.10.2026 (копание): то же с кодом состояния появления ActionState (4065@102), как у настоящего снапшота (110/114 — копает).
	void DebugSpawnFakePeerState(int32 UserNum, const FString& Name, const FVector2D& PosGame, int32 ActionState);

	// ★ ПУБЛИЧНАЯ обёртка приватного `FindPlayerActor` (эталонный `CharManager_FindByUserIdx`).
	//   Нужна меню по игроку (окно 2000) и следованию: обе точки адресуют цель сессионным userNum.
	//   Заводить в подсистеме меню свой обход реестра было бы дублем (закон port-shared-points).
	AActor* GetPlayerActorByUserNum(int32 UserIdx) const;
	/** ★ Актор игрока по charId (i32 из БД). Нужен там, где известен ТОЛЬКО charId — например
	 *  состав отряда приходит пакетом 5107, а он несёт charId и имя, но НЕ userNum.
	 *  Ищет по тому же реестру `NetPlayers`, второй копии реестра не заводим. 0/не найден -> nullptr. */
	AActor* GetPlayerActorByCharId(int32 CharId) const;
	/** ★ userNum по charId (обратно к `GetNetPlayerCharId`). −1, если такого соседа нет. */
	int32 GetUserNumByCharId(int32 CharId) const;

	// ── ★ ЛУТ (порт loot_system_re.md) ────────────────────────────────────────
	// Живой лут-актор на тайле (ключ сущности = пара тайлов; nullptr если нет). Для контроллера (клик).
	class AAsdaLootActor* FindLootAtTile(const FIntPoint& Tile) const;
	// ★ 09.10.2026 (автолопата): порт поиска `sub_F2F110@0xF2F110` (через `sub_928C3F`) — ПЕРВЫЙ по списку лут-актор, у которого
	//   квадрат 3D-расстояния до точки `UeFrom` меньше `Radius²` (эталон: `Math_DistanceSquared3D(…) >= 90000` — пропуск,
	//   т.е. радиус 300 юнитов; единица игры = единица UE). Не «ближайший», а первый в порядке списка эталона: новый лут
	//   встаёт в голову, поэтому первый = самый поздний по 5005 (★ 09.10.2026, `NetLootSerial`). nullptr — нет.
	class AAsdaLootActor* FindFirstLootWithin(const FVector& UeFrom, float Radius) const;
private:
	// 5005: батч предметов на земле. На тайле максимум один предмет (занятый ключ — игнор, эталон);
	//   звук дропа — ОДИН на батч (только если есть DropperRid!=-1), категория = imgRecord+208.
	void OnLootSpawn(const TArray<FAsdaLootDrop>& Drops);
	// ★ 17.10.2026 (Ф1): сам спавн батча 5005 (прежнее тело `OnLootSpawn`); `OnLootSpawn` держит батч до показа смерти моба-
	//   дропнувшего, если её отложила лента попаданий (вещь не должна лечь под живым мобом).
	void SpawnLootNow(const TArray<FAsdaLootDrop>& Drops);
	// 5015: мгновенное удаление предмета с тайла (подбор любым/таймаут 5 мин на сервере).
	void OnLootRemove(FIntPoint Tile);

public:
	// ★ Звук падения предмета по его коду (таблица `Sod_drop_*`, категория из
	//   `UAsdaItemTableSubsystem::GetDropSoundCat`). Зовут двое: наземный лут при спауне и
	//   инвентарь при выбросе вещи — звук обязан быть один и тот же, поэтому точка ПУБЛИЧНАЯ.
	void PlayItemDropSound(int32 ItemCode) const;

	// ★ СВОЯ фракция/лагерь — ЧТЕНИЕ наружу. Значение приходит пакетом 6713
	//   (`Recv_ClanMyMembership@0xCA4AB0`) и в эталоне живёт в `g_pCampWarClan + 0x0B7C`.
	//   Нужен второй потребитель, кроме плашки: пакеты осады 6723 (вступление в осаду по клику
	//   на NPC на карте mapId 19) и 6749 (код меню 30078) кладут это же значение в тело.
	//   Возвращает: −2 = пакет ещё не приходил, −1 = фракции нет, иначе 0/1/2.
	int32 GetSelfCampId() const { return SelfCampId; }
	// ★ 26.10.2026 (bowfx, цели навыков) [ОРИГИНАЛ]: лагерь/фракция СОСЕДА (6722 `recvCampWarClanMemberUpdate`, кэш по charId) —
	//   эталонное `[116]` чужого персонажа на картах осады 19/23 (`netHandleFactionUpdateInRange@0xCA7820`). −1 — не известно.
	int32 GetPeerCampId(int32 UserIdx) const;

private:
	// 5013: результат подбора (1=ok+звук+нотис, 0/3→1012, 2→1011, 4→1030, 5→снять фантом @8/@10).
	void OnLootPickupResult(uint8 Result, int32 PickerUserNum, const TArray<uint8>& Body, int32 NewWeight);
	// 5017: сервер указал «подбери предмет на тайле» — тот же флоу, что клик (подойти/5012).
	void OnLootPickHint(uint8 Result, FIntPoint Tile);
	TMap<FIntPoint, TWeakObjectPtr<class AAsdaLootActor>> NetLoot;   // тайл → лут-актор
	// ★ 09.10.2026 (МЕЛК-4): порядковый номер появления лута по 5005 (тайл → номер). Эталон вставляет новый билборд
	//   в ГОЛОВУ списка сцены (`CEffectMgr_SpawnModelWithItemTexture`, effects_particles.c:57444-57447), а порядок TMap
	//   после удалений не равен порядку вставки — «первый в списке» = больший номер.
	TMap<FIntPoint, uint64> NetLootSerial;
	uint64 NetLootSerialNext = 0;   // следующий номер появления

	// Кэш «какое состояние уже раскатано на меши» — чтобы не перебирать акторы каждый кадр.
	int32 CityWarAppliedState = -2;
	int32 CityWarAppliedSlot = -1;
	// ★ Найденные слоты материалов города. Кэш ОБЯЗАТЕЛЕН: после первой подмены параметр `Tex`
	//   уже не равен родной `UNICON`/`UNICON03`, и повторный поиск по имени текстуры ничего бы
	//   не нашёл — смена владельца перестала бы перекрашивать флаги.
	struct FAsdaCityWarSlot
	{
		TWeakObjectPtr<class UMeshComponent> Mesh;
		int32 Index = 0;
		uint8 Kind = 0;   // 0 = флаг (UNICON), 1 = площадка (UNICON03), 2 = эмблема (UNICON_CLAN)
	};
	TArray<FAsdaCityWarSlot> CityWarSlots;
	bool bCityWarSlotsScanned = false;
	// Эмблема клана сейчас раскатана? (её появление/исчезновение — повод пересобрать слоты)
	bool bCityWarClanApplied = false;
	FDelegateHandle HCityWarInfo;

	// ★ СВОЯ фракция (6713 Recv_ClanMyMembership): -2 = пакет ещё не приходил (кэш до спавна павна).
	int32 SelfCampId = -2;         // -1=нет; 0=свет/синий, 1=тьма/красный, 2=хаос/зелёный
	int32 SelfFactionGrade = -1;   // звание (эмблема faction_grade.tex, ячейка звание-1)
	int32 SelfFactionPoints = 0;   // очки фракции (для будущего HUD/окна персонажа)

public:
	// ★ Подстройка посадки крыльев в пространстве кости «M»/«G» (кость на уровне таза, Z≈83 см мира).
	//   СДВИГ НАЗАД (юзер 2026-07-30: «на нужном уровне, но по центру тела — должны быть на краю спины»):
	//   ось глубины эффекта после канальных вращений = локальная Y (крыло сзади = −Y; замер мирового
	//   bbox глубины −14.9..+3.3 при лице +Y — передний край торчал внутрь торса на +3 см).
	//   −8 см уводит корень крыла за заднюю поверхность спины. Editor-Friendly: правится одним числом.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Wings")
	FVector WingBoneOffset = FVector(0.f, -8.f, 0.f);

	// ★ userNum ПАРТНЁРА-соулмейта данного игрока (−1, если пары нет). Нужен ветке Камня души
	//   приёмника 5029: эталон берёт его из `Field142C->gap_8B4[4]` (i16, ставится в `social.c:7314`)
	//   и вешает `eff_1_char_99_expmovg-a_01.jeb` + `S_c_SmExpGift.wav` именно НА ПАРТНЁРА
	//   (`item_inventory.c:39044-39061`). Имя `petChar` в декомпиле — артефакт, питомец ни при чём.
	int32 GetSoulPartnerUserIdx(int32 UserIdx) const
	{
		const FAsdaSoulState* S = SoulByUserIdx.Find(UserIdx);
		return (S && S->SoulIdx > 0) ? S->PartnerUserIdx : -1;
	}
public:
	// ★ ВРЕМЕННО СКРЫТЬ / ВЕРНУТЬ КРЫЛЬЯ своего персонажа — по просьбе владельца, для осмотра
	//   персонажа и съёмки. Это ЧИСТО ЛОКАЛЬНЫЙ показ: серверу ничего не шлётся, срок действия
	//   Plus Package не трогается, соседи по-прежнему видят крылья. При возврате состояние
	//   берётся из последнего пакета 6585, так что «включить» не выдумывает крыло, которого нет.
	//   Заодно снимается подмена бег-клипа на взмахи, поэтому со скрытыми крыльями персонаж снова
	//   бежит по земле — со следами и звуком шагов.
	//   Команды: `asda.Wings.Hide 0|1` и `asda.Wings.Toggle`.
	void SetLocalWingsHidden(bool bHide);
	bool AreLocalWingsHidden() const { return bLocalWingsHidden; }

private:
	bool bLocalWingsHidden = false;   // локальное скрытие крыльев своего персонажа

	// ── ★ КРЫЛЬЯ Plus Package (6585) + КОЛЬЦА SOULMATE (5220) — wings_soulmate_re.md ──
	// Состояние по charId (пакеты могут прийти до спавна актора — применяем и при спавне).
	struct FAsdaWingState { int32 WingId = -1; bool bOn = false; };
	TMap<int32, FAsdaWingState> WingByCharId;
	// Соулмейт по userIdx: SoulIdx>0 = кольцо ON; PartnerUserIdx — для сердечек-линка.
	struct FAsdaSoulState { int32 SoulIdx = -1; int32 PartnerUserIdx = -1; double NextHeartS = 0.0; };
	TMap<int32, FAsdaSoulState> SoulByUserIdx;
	// ★ 11.10.2026 (В0 аудита дублей №2, F14): число живых таймеров полёта сердец соул-линка (каждое сердце — один
	//   зацикленный таймер 0,03 с, снимается на конце полёта). Сбрасывается при загрузке карты (таймеры гибнут с миром).
	int32 ActiveSoulHeartTimers = 0;
	// ★ 11.10.2026: доступ самотеста `asda.Soul.SelfTestWave1A` (этап N: сердца F14, разрыв пары при userNum 0 — N3)
	//   к `SoulByUserIdx`, `TickSoulHearts`, `OnSoulmateBreak`, `ApplySoulRingToActor` и счётчику таймеров.
	friend struct FAsdaSoulWave1ATestAccess;
	// ★ 11.10.2026 (дедуп №2, В2-C): доступ самотестов `asda.Dedup.W2C.*` (сроки эффектов F2, фейды F1/F9, сердца F14) к
	//   закрытым штатным точкам (`PlayEntryFx`, `OnSoulmateBond`, `OnSoulmateState`, `OnEntityDespawn`, счётчик таймеров сердец).
	friend struct FAsdaDedupW2CTestAccess;
private:
	// 6585: применить крылья к актору (self/чужой). Эталон UpdateWingEffect: кость "M"(муж)/"G"(жен),
	//   jeb из PlusPackageTable по wingId (male/female), scale 1.0; гасится flag=0/деспауном.
	void OnWingState(int32 CharId, int32 WingId, int32 RemainSec, bool bOn);
	void ApplyWingToActor(AActor* Ch, bool bMale, int32 WingId, bool bOn);
	// 5220: кольцо у ног (eff_1_char_99_soullink01, кость root + Y-lift 15.0 — flt_13998C0 из live.dmp).
	void OnSoulmateState(int32 CharId, int32 UserIdx, int32 SoulIdx, int32 PartnerUserIdx);
	void ApplySoulRingToActor(AActor* Ch, bool bOn);

	// ★★★ ЕДИНЫЙ ПЕРЕБОР НАДЕТЫХ ПРЕДМЕТОВ ДЛЯ ОБЕИХ ШКАЛ ТЕМПА.
	//   Эталонный цикл `for (i = 0; i < 10; ++i) if (i != 5 && i != 6 && i != 7)` — слоты
	//   0,1,2,3,4,8,9 (пять видимых частей, щит и оружие) — стоит в ЧЕТЫРЁХ местах оригинала и
	//   везде одинаков: в трёх путях атаки (game.c:36452-36468 и др.) и в обоих писателях
	//   `animSpeedScale` (game.c:64371-64412, :65121-65138). Держим его ОДНОЙ функцией: ровно
	//   дублирование этой формулы было дефектом, который чинили 16.09.2026.
	//   Свой набор берётся из контейнера 3 инвентаря, чужой — из реестра `NetPlayers`.
	//   Возвращает СУММУ вкладов (`Σ LinkedGroupCode/100`), без прибавки 1.0.
	float SumWornItemsTempoTerm(int32 UserIdx) const;


public:
	// ★ 19.09.2026 (проверка пункта «Свечение», консоль `asda.Video.GlowSelect fx`): повесить на игрока офлайн
	//   эффекты ПРЕДМЕТОВ тем же штатным путём, что и пакеты сервера: свечение заточки (4021/5001 →
	//   ApplyWeaponGlowToActor), крылья (6585 → ApplyWingToActor), кольцо соулмейта (5220 → ApplySoulRingToActor).
	//   Ноль/false — эффект не трогать. Только для проверки кадрами без сервера; сети ничего не уходит.
	void DebugApplyItemFx(AActor* Ch, int32 WeaponItemId, int32 WeaponGrade, int32 WingId, bool bRing);

	// ★★★ КОЛЬЦО ПОД ВЫБРАННОЙ ЦЕЛЬЮ — ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (решение владельца 2026-08-21).
	//   В НАШЕЙ сборке под целью не рисуется НИЧЕГО (доказано двумя методами, `npc_visual_fix.md` §2.1:
	//   строк `FriendlySelect`/`UnfriendlySelect` в `AsdaStory.exe` 0 вхождений, файлов в нашем
	//   `EffectModel.bnz` нет, ни одна из пяти функций «квад по террейну» не смотрит на
	//   `g_nTargetUserIdx`). Кольцо взято у клиента-близнеца (`friendlyselect.jeb` / `unfriendlyselect.jeb`
	//   из `NEW\WorldOfDreams\...\EffectModel.bnz`, сборщик `Tools/asda_build_target_ring_fx.py`),
	//   потому что владелец играл в ТУ сборку и решил вернуть её поведение. Разбор — `npc_target_ring.md`.
	//   `bHostile`: true → красное `unfriendlyselect` (мобы), false → зелёное `friendlyselect`
	//   (NPC и игроки) — ровно как в близнеце, где два кольца лежат в двух соседних слотах одного
	//   объекта и выбираются признаком «друг/враг».
	//   Своей подсистемы у кольца НЕТ: спавн идёт через ту же `SpawnCharFx`/`DestroyCharFxByTag`,
	//   что у крыльев, соулмейта и свечения оружия.
	//   ★★★ ПОВЕДЕНИЕ — «ОДИН РАЗ СХЛОПНУЛСЯ И ОСТАЛСЯ ВИДИМЫМ» (2026-08-21, второе замечание
	//   владельца: «оно не должно просто отыграть и исчезнуть, а на определённом диаметре
	//   остановиться и быть видимым — собственно это и называется выделение»). Обод стягивается
	//   150→50 за 0.20 с (плюс 0.0672 с задержки) ОДИН РАЗ на каждое НОВОЕ выделение и ЗАМИРАЕТ
	//   на конечном диаметре, оставаясь на земле, пока цель выбрана; гасит его снятие выбора,
	//   смена цели, смерть цели или уход носителя из мира — то есть рантайм, а не срок жизни.
	//   Механика фриза: `end_mode = 3` у одиночного квада (`AAsdaEffectActor::Tick`, `case 3`:
	//   `LifeTimer = Period` без `bStopped`) + альфа конечного узла = стартовой; ставит канал
	//   сборки (`Tools/asda_build_target_ring_fx.py` → `asda_fx_common.hold_last_frame`).
	//   ★★★ ЭТО НЕ ОТСТУПЛЕНИЕ, А ВОССТАНОВЛЕНИЕ — перепроверено по коду близнеца 2026-08-21
	//   (`npc_target_ring_fix2.md` §2.2). У него на земле ОСТАЁТСЯ тот же квад 50×50, alpha 1.0,
	//   конечного цвета, и рисуется каждый кадр, пока цель выбрана: `endMode = 0` у эмиттера
	//   `xp::xpCSpriteKeyFrameAni` (анимация `0x132EB10`, case 0 @`0x132EE75`) означает
	//   «подставить КОНЕЧНЫЕ значения каналов и заморозить» (`node[0x114] = −1.0`), а отрисовка
	//   `vtbl[16]@0x13420A0` зовётся БЕЗУСЛОВНО. Прошлые отчёты ошибались дважды: читали разброс
	//   альфы как её конечное значение (в данных alpha 1.0 → 1.0, выцветания нет) и переносили
	//   на близнеца НАШУ трактовку `EndMode == 0` («погасить и очистить секцию»).
	//   Единственное настоящее расхождение — плавность: у близнеца `kfCount = 0` и обод меняет
	//   размер СКАЧКОМ, у нас он стягивается лерпом (закон конвейера нашей сборки, и владельцу
	//   плавность нравится). Пульса (`end_mode = 1`), который был до 21.08, нет: играет ОДИН раз.
	void ApplyTargetRingToActor(AActor* Ch, bool bOn, bool bHostile);

	// ★★★ ПОДОШВА ЦЕЛИ (мировой Z низа её видимой геометрии) — ЕДИНАЯ ТОЧКА.
	//   От неё считается высота кольца выбора (см. реализацию: там же разбор, почему нельзя
	//   отсчитывать от `GetActorLocation()`, и оба клампа). Публичной сделана ради диагностики
	//   `asda.Target.RingNearest`: печать должна спрашивать ТУ ЖЕ функцию, что и постановка
	//   кольца, иначе замер и код разъедутся ([[false-negatives-method]]).
	static float TargetSoleZ(const AActor* Ch);

	// ★★★ ГАБАРИТЫ ТОЛЬКО ПО ВИДИМОЙ ГЕОМЕТРИИ — ЕДИНАЯ ТОЧКА (её же использует `TargetSoleZ`).
	//   `AActor::GetActorBounds(bOnlyColliding=false)` складывает габариты ВСЕХ зарегистрированных
	//   примитивов, а у наших актёров среди них есть невидимые и «нетелесные»: капсула пикинга,
	//   виджет плашки имени, скрытая квестовая метка, а у `AAsdaPlayerCharacter` — ещё и
	//   `USpringArmComponent` с `UCameraComponent`, вынесенные на 700…1270 юнитов ЗА спину.
	//   Из-за них «центр актора» уезжает на сотни юнитов в сторону камеры — то есть это НЕ то, что
	//   эталон называет `soundPos` (центр тела сущности). Считаем по `UMeshComponent`, как и подошва.
	//   Возвращает false, если видимых мешей нет (вызывающий тогда падает на `GetActorBounds`).
	static bool VisibleMeshBounds(const AActor* A, FVector& OutOrigin, FVector& OutExtent);

	// ── ★ СВЕЧЕНИЕ ЗАТОЧЕННОГО ОРУЖИЯ/ЩИТА (weapon_glow_re.md) ──
	// ЭТАЛОН: CCharacter_UpdateHeldItemEffect@0xA4C070 (оружие, экип-слот 9) и
	//   CCharacter_UpdateEquipAuraEffect@0xA4DD60 (щит, экип-слот 8) — обе зовутся КАЖДЫЙ КАДР и для
	//   локального игрока (entity_char.c:8077), и для каждого видимого соседа (entity_char.c:8773).
	//   Порог заточки 5 (gradeThresh5), состояние эффекта: 5→"01", 6→"02", >=7→"03".
	//   Кость: оружие — "8_a1" (луки, itemSubType 8/10 — "_8_a1"); щит — "_7_a1".
	//   Крепление attach-10 = ПОЛНЫЙ трансформ живой кости (как крылья) ⇒ bFollowBoneFull.
	// Пересобирает свечение на акторе: гасит старое (теги WeaponGlowFx/ShieldGlowFx) и, если заточка >=5
	//   и ассет есть, спавнит новое. Нулевой itemId/grade<5 = просто погасить.
	void ApplyWeaponGlowToActor(AActor* Ch, int32 WeaponItemId, int32 WeaponGrade,
		int32 ShieldItemId, int32 ShieldGrade, int32 UserIdxForLog = -1);
	// Имя .jeb свечения 1:1 с эталоном (CCharacter_BuildWeaponJebFileName@0xA4CB10 /
	//   itemBuildJebEffectName@0xA4E790). bShield=true → фиксированная база "shd". Пусто = свечения нет.
	FString BuildGlowJebName(int32 ItemId, int32 Enhance, bool bShield) const;
	// Пересчитать свечение локального игрока по текущим данным NetSubsystem (4048/4021).
	void RefreshSelfWeaponGlow();
	// ★ Пересчитать НАБОР МОУШЕНОВ локального игрока при смене оружия на лету (4021/4048).
	//   Эталон: CCharacter_SetupModelFromItem@0x9E97A0 вместе с мешом оружия перезагружает клипы
	//   покоя/движения/атаки набором ch4_{m|f}_{SS} по ItemRecord[33]=ItemSubType.
	void RefreshSelfWeaponMotion();

	// ══════════ ★★★ СМЕНА ЭКИПИРОВКИ СВОЕГО ПЕРСОНАЖА — МОДЕЛЬ (equip_change_fix.md) ══════════
	//
	// ПОЧЕМУ ЭТО ЖИВЁТ ЗДЕСЬ, А НЕ В ПАКЕТЕ. Сервер РАССЫЛАЕТ 4021/4023 (`CharPutOnEquip@0x4CD730`,
	//   `CharPutOffEquip@0x4CD910`) через `SendPacket_ClippingSectorInOtherUser@0x41EA80`, а та
	//   ЯВНО ПРОПУСКАЕТ отправителя (`if (v14 != a3)`), то есть СЕБЕ смена внешности НЕ ПРИХОДИТ
	//   НИКОГДА. Оригинальный клиент меняет свою модель САМ, применяя ответ 5001:
	//     • надел  — `inventoryApplySlotItem@0xD60BA0` case 3 → `itemUI_ApplyEquipWithColor@0xC6B5B0`
    //                → `tagCHARACTER_SetPartsModelByType(localPlayer, ItemRecord->itemType, newItemId)`;
	//     • снял   — `slotItemDrop@0xD60EB0` case 3 → `cAvatar_PreviewPartModel@0xC6B8A0`
	//                → `tagCHARACTER_SetPartsModelByType(localPlayer, ItemRecord->itemType, 0)`.
	//   Слоты 5/6/7/10/11 модель не трогают вовсе (аксессуары; `slotItemDrop` их отсекает явно).
	//
	// ЧТО ДЕЛАЕТ. Обновляет ОДНУ часть в снимке внешности своего персонажа и пересобирает модель.
	//   `EquipSlot` = номер слота контейнера 3, он же `ItemRecord->itemType` (сервер сверяет их в
	//   `CharPutOnEquip`: `if (iSlotNum != iSlot)`), раскладка `tagCHARACTER_SetPartsModelByType
	//   @0x9E18D0`: 0 шлем, 1 тело, 2 штаны, 3 обувь, 4 перчатки, 8 щит, 9 оружие.
	//   `ItemId == 0` = снять. `Grade` — заточка (свечение); −1 = «не знаю, оставить прежнюю».
	void ApplySelfEquipPart(int32 EquipSlot, int32 ItemId, int32 Grade = -1);

	// ★★★ ПАКЕТНАЯ установка НЕСКОЛЬКИХ частей за ОДИН проход — нужна ПРИМЕРКЕ (магазин снаряжения
	//   6800, эталон `charApplyEquipPartsModels@0xC28440` раскатывает ВЕСЬ набор одним вызовом).
	//   ⚠ ПОЧЕМУ ОТДЕЛЬНАЯ ТОЧКА, А НЕ ЦИКЛ ИЗ `ApplySelfEquipPart`: та на КАЖДОМ вызове заново
	//   сверяет снимок с контейнером 3 (авторитет надетого) и затирает им Helmet/Chest/Pants/Shoes/
	//   Gloves/Weapon. Цикл «примерил шлем, примерил нагрудник» сбрасывал бы шлем обратно на реальный.
	//   Здесь сверка делается ОДИН раз, затем применяются ВСЕ пары, и меш пересобирается ОДИН раз.
	//   `SlotItemPairs`: X = слот (он же `ItemRecord->itemType`), Y = код предмета (0 = снять).
	void ApplySelfEquipParts(const TArray<FIntPoint>& SlotItemPairs, int32 Grade = -1);

	// ═══ ★★★ ОБЩАЯ ТОЧКА «ПРИМЕРОЧНЫЙ ОВЕРРАЙД ЭКИПИРОВКИ» ════════════════════════════════════
	//   (запись #42а `Docs/Port/PORT_PENDING_ON_SYSTEMS.md`; первый потребитель — парикмахерская
	//    15000, шаг 7 эталона `CCharacter_LoadHelmetPart(char, 0)` / возврат в
	//    `charAppearance_ApplyHairHelmet@0xCF2940`).
	//
	// ЗАЧЕМ ОТДЕЛЬНЫЙ СЛОЙ, А НЕ `ApplySelfEquipPart(0, 0)`. Снимок `SelfParts` — это ЗЕРКАЛО
	//   авторитета (контейнер 3, пакет 1027 + 5001), и `ApplySelfEquipParts` на КАЖДОМ вызове
	//   сверяется с ним заново. Если бы примерка писала прямо в снимок, то (а) любая смена
	//   экипировки во время примерки мгновенно вернула бы шлем на голову, (б) выход из окна
	//   пришлось бы «доверять» снимку, который к тому моменту уже устарел. Поэтому оверрайд —
	//   ОТДЕЛЬНЫЙ слой: авторитет не трогается, а модель собирается из `VisualSelfParts()` =
	//   снимок, поверх которого положены все активные оверрайды.
	//
	//   `Owner` — имя-ключ системы (у каждой свой набор слотов, снимаются независимо).
	//   `SlotItemPairs`: X = слот (он же `ItemRecord->itemType`: 0 шлем, 1 тело, 2 штаны, 3 обувь,
	//   4 перчатки, 9 оружие), Y = код предмета (0 = «снять на время примерки»).
	void PushEquipTryOn(FName Owner, const TArray<FIntPoint>& SlotItemPairs);
	// Снять оверрайд системы `Owner` и пересобрать модель по авторитету. Повторный вызов безвреден.
	void PopEquipTryOn(FName Owner);
	bool HasEquipTryOn(FName Owner) const { return EquipTryOn.Contains(Owner); }
	// Снимок внешности С УЧЁТОМ активных примерочных оверрайдов — им и собирается модель ИГРОКА.
	FAsdaWornParts VisualSelfParts() const;

	// ★★★ ПРИЧЁСКА — ОБЩАЯ ТОЧКА ДЛЯ ПАРИКМАХЕРСКОЙ (окно 15000, `npc_hairshop_30029.md`).
	//   Эталон меняет причёску ДВУМЯ путями и оба сводятся к перезагрузке меша волос:
	//     • себе  — `hairShop_HandleResult` пишет `char->gap_43B[38/39]` и зовёт
	//               `CCharacter_LoadHairModel(char, 0)` (`shop_trade.c:6236-6243`);
	//     • соседу — `CharPacket_ApplyHelmet` пишет то же слово по указателю персонажа, найденного
	//               по **userNum**, и зовёт `CCharacter_ApplyEquippedHelmet` (`entity_char.c:45378`).
	//   В порту обе ветки идут через ту же единственную пересборку `InitAppearance` →
	//   `UAsdaCharAssembly::BuildWornParts` (второго склада внешности не заводим).
	//   ★ ПРИМЕРКА: `bTemporary = true` НЕ трогает снимок `SelfParts`, чтобы «Вернуть как было»
	//     (кнопка 15182) откатывал вид без сетевого обмена.
	void ApplySelfHair(int32 HairStyle, int32 ColorIdx);

	// Причёска ЧУЖОГО игрока (пакет 5472). `UserNum` — сессионный индекс из тела **+4 (i16)**;
	//   поле +0 (charId) сервер шлёт, но эталон его НЕ читает (закон `charid-vs-usernum`).
	void ApplyRemoteHair(int32 UserNum, int32 HairStyle, int32 ColorIdx);

	// ═══ ★★★ 25.10.2026 [ОРИГИНАЛ] ПОКАЗ ШЛЕМА (жалоба игроков «Show Helmet не работает») ═══════════════════════
	//   Флаг живёт в снимке внешности (`FAsdaWornParts::bHelmetShown` = байт `CCharacter+0x32F78` эталона) и
	//   применяется ЕДИНСТВЕННОЙ пересборкой `InitAppearance` → `BuildWornParts` (шлем не рисуется, причёска обычная).
	//   Свой: 4003 [8] при входе/портале (`TrySpawnLocalPlayer`), «ОК» окна настроек (`optApplySettingsFromConfig`
	//   → `CCharacter_ApplyEquippedHelmet`). Сосед: 4005 [6] при появлении (`SpawnRemotePlayer`), 4025/111 живьём.
	//   Смена шлема при снятом флаге (5001/4021) идёт той же пересборкой — новый шлем тоже не рисуется.
	// Свой персонаж: флаг в снимок и пересборка, если флаг сменился (`Src` — для журнала).
	void ApplySelfHelmetShow(bool bShow, const TCHAR* Src);
	// Персонаж по userIdx (4025 действие 111): свой — веткой `ApplySelfHelmetShow`, сосед — его снимок и пересборка.
	//   Соседа нет в мире — ничего (эталон: `if (lockChar)`); при появлении флаг придёт в его 4005 [6].
	void ApplyRemoteHelmetShow(int32 UserIdx, bool bShow);
	// Флаг показа шлема соседа: true — сосед есть в реестре (значение в `bOutShow`). Нужен самотесту и дампу.
	bool GetRemoteHelmetShow(int32 UserIdx, bool& bOutShow) const;

	// Текущая причёска своего персонажа (для отката примерки парикмахерской).
	int32 GetSelfHairStyle() const { return SelfParts.HairStyle; }
	int32 GetSelfHairColor() const { return SelfParts.HairColor; }

	// Снимок внешности СВОЕГО персонажа (то, из чего собран его меш). Наполняется при спавне
	//   (`TrySpawnLocalPlayer`) и правится точечно в `ApplySelfEquipPart` — второго склада нет.
	//   ⚠ Оружие/щит и их заточка ЖИВУТ НЕ ЗДЕСЬ, а в `UAsdaNetSubsystem::SelfWeaponItemId/…` —
	//   там же, куда их кладут пакеты 4048/0x403, и оттуда их читает `RefreshSelfWeaponGlow`.
	//   Второго склада не заводим (иначе свечение и меш разъедутся, как уже было в 2026-07-31).
	FAsdaWornParts SelfParts;
	uint8          SelfSex = 1;         // 1 муж / 2 жен — тот же, что ушёл в `InitAppearance`
private:
	// Активные примерочные оверрайды: система → (слот → код предмета). См. `PushEquipTryOn`.
	TMap<FName, TMap<int32, int32>> EquipTryOn;
	// ★ ТРАНСПОРТ (5453 посадка / 5454 высадка по userIdx; mount_system_re.md): модель и набор сидячих
	//   анимаций берутся из таблицы спец-предметов по itemCode (SubType=15: ModelEntry/EffectEntry/HeightOffset).
	void OnMountState(int32 UserIdx, int32 ItemCode, bool bOn);
	TMap<int32, int32> MountByUserIdx;   // userIdx → itemCode транспорта (кэш до спавна актора)
	// ★ N12 (дедуп №2, В3, 12.10.2026): ИНДЕКС ТРАНСПОРТА «код → данные первой строки SubType 15 с этим кодом» вместо
	//   линейного перебора `DT_special_itemlist` на каждый 5453/спаун. Порядок «первая» — порядок строк таблицы, как у
	//   прежнего перебора (дубли кода берут первую строку). Строится лениво из `AsdaDt::Cached`, таблица удерживается
	//   миром (`HoldSpawnTables(MountIndex)`); другой объект таблицы (пересборка в редакторе) — индекс строится заново.
	struct FAsdaMountIndexEntry
	{
		FString Model;          // ModelEntry строки
		FString AnimSet;        // номер набора сидячих анимаций (хвост EffectEntry после последнего «_»)
		float   Height = 0.f;   // HeightOffset строки
	};
	TMap<int32, FAsdaMountIndexEntry> MountIndex;
	// Объект таблицы, из которого построен `MountIndex` (слабо: удержание — у `HeldSpawnTables`).
	TWeakObjectPtr<const UDataTable> MountIndexSource;
public:
	// Данные транспорта по коду предмета (SubType 15): модель, номер набора сидячих анимаций, подъём плашки.
	//   false — кода нет среди транспорта или у строки пустая модель. ★ N12: публичный — для дампа `asda.W3.MountDump`.
	bool LookupMount(int32 ItemCode, FString& OutModel, FString& OutAnimSet, float& OutHeight);
private:
	// ★ 6120: активный ПИТОМЕЦ игрока по charId (pet_system_re.md). Спавним AAsdaPetActor рядом с
	//   владельцем (жёсткое следование), тип −1 = убрать. Кэш — пакет приходит в AoI-бандле до спавна.
	void OnPeerPet(int32 CharId, int32 PetTypeId, FString Name);
	void ApplyPetToActor(AActor* Owner, int32 PetTypeId, const FString& Name);
	TMap<int32, TPair<int32, FString>> PetByCharId;              // charId → (petTypeId, имя)
	TMap<int32, TWeakObjectPtr<class AAsdaPetActor>> PetActors;  // charId → живой актор питомца

	// ═══ ★ ВОЛНА 6 ПИТОМЦЕВ: ОЧЕРЕДЬ 6120 СО СЛИВОМ РАЗ В 2000 мс (долг П1-3 реестра) ═════════
	// ПОЧЕМУ ОЧЕРЕДЬ, А НЕ ПРЯМОЕ ПРИМЕНЕНИЕ. Эталон приход 6120 НЕ применяет: приёмник
	//   `CPetData::EnqueuePeerPet@0xDCA620` (мисномер `charNearbyInsertEntry`, entity_char.c:48536)
	//   копирует 22 байта тела и кладёт их в контейнер `CPetData + 186*4 = +0x2E8`, и всё.
	//   Разбирает очередь СОСЕДНИЙ метод того же класса — `@0xDCA6B0` (мисномер
	//   `charSyncNearbyPositions`, :48550), который зовётся из главного цикла
	//   (`game_main.c:1486`) и НАЧИНАЕТСЯ с порога `timeGetTime() - last > 0x7D0` (2000 мс).
	//   В проходе он для каждой записи ищет персонажа `CharManager_FindByCharId(charId)`:
	//     • нашёл  → копирует 22 б в `персонаж + 0x4DA` и УДАЛЯЕТ запись из очереди (:48606-48608);
	//     • не нашёл → запись ОСТАЁТСЯ и будет повторена через 2000 мс (:48610-48611).
	//   ⇒ смысл очереди двойной: (1) задержка/склейка частых смен питомца, (2) УДЕРЖАНИЕ пакета,
	//   пришедшего раньше спавна владельца (закон `session-reset-drops-early-data`).
	// ЧТО СДЕЛАНО В ПОРТЕ. Удержание у нас было и до волны 6 — через `PetByCharId`, который
	//   читает `SpawnRemotePlayer`; порога 2000 мс не было вовсе. Теперь есть и порог, и очередь.
	//   Применение при спавне СОХРАНЕНО как осознанное УЛУЧШЕНИЕ (ключ `asda.Pet.PeerApplyOnSpawn`):
	//   питомец соседа появляется вместе с соседом, а не до 2 с спустя. Чтобы улучшение не дало
	//   двойного применения (пересоздание актора = мигание), спавн СНИМАЕТ свою запись с очереди.
	void TickPeerPetQueue();
	/** Применить одну запись очереди; true — владелец найден, запись можно убирать. */
	bool ApplyPeerPetEntry(int32 CharId, int32 PetTypeId, const FString& Name);
	/** Очередь 6120: charId, тип питомца (−1 = убрать), имя. Порядок прихода сохраняется. */
	TArray<TTuple<int32, int32, FString>> PeerPetQueue;
	FTimerHandle PeerPetTimer;
	// ★★★ 30.10.2026 [ИСПРАВЛЕНИЕ] СВОЙ ПИТОМЕЦ ПОСЛЕ ВХОДА В МИР И ПОРТАЛА (ключ `asda.Pet.SelfRestore`, разбор —
	//   `Reports/pet31_snowlion/SELF_RESTORE.md`). Свой 6120 при входе до клиента НЕ доходит: сервер рассылает его из
	//   `recvDBPetList` (GameServer.c:35440) по сектору РАНЬШЕ, чем персонаж в нём оказался (живой журнал владельца: 6106 с
	//   «призван», 6120 нет), а обход сектора себя исключает (`Send_MoveSectorInClippingUserInfo`, :11506). Эталон держит
	//   питомца в объекте персонажа только из 6120 (`@0xDCA6B0` → char+0x4DA), поэтому у порта призванный питомец после входа и
	//   после портала (актор пересоздаётся) пропадал, хотя панель «призван» горела. Здесь: если в списке 6106 есть призванный, а
	//   своего актора питомца этой породы нет и свой 6120 не ждёт в очереди — ставим его тем же `ApplyPetToActor`.
	//   Зовётся в конце спавна своего персонажа и на каждом сливе очереди (2 с — догоняет 6106, пришедший после спавна).
	void RestoreSelfPetFromList(const TCHAR* Why);

public:
	// ═══ ★ ХРАНИТЕЛЬ / «АНГЕЛ» (6156; Reports/soul_guardian_companion_re.md) ═══════════════
	// Эталон: `charHandleSlotKeyPacket@0xEF6D00` кладёт пару (userIdx → SoulGuardIndex) в карту
	//   mgr+0x17C, а применяет её `charVisibilityStateRefresh@0xEF72C0` КАЖДЫЙ КАДР из главного цикла
	//   (game_main.c:1504) — плюс пер-кадровое доведение из апдейта сущностей (§2.3):
	//     char[0x4F8] == −1 → charMgr_RemoveSummonById(мягко);  иначе → AddOrUpdateObject(...).
	//   6156 приходит ПРО КАЖДОГО игрока в AoI (§2.4), поэтому у соседей компаньон ПОЛНОЦЕННЫЙ.
	// ★ ОТСТУПЛЕНИЕ (помечено): эталон УДАЛЯЕТ запись карты, если персонажа ещё нет (§13 п.8), из-за
	//   чего хранитель соседа не покажется до следующего 6156. Мы запись храним и доводим каждый кадр.
	// ★★★ КЛЮЧ КАРТЫ = charId (`CUser+372`), НЕ сессионный userIdx — эталон достаёт владельца
	//   `CharManager_FindByCharId` (entity_char.c:50134). Подробное обоснование — у делегата
	//   `UAsdaNetSubsystem::OnSoulGuardIdx`. Для консольных эмуляций (`asda.SoulFx.Peer <id>`)
	//   оставлен фолбэк: если по charId владельца нет, ключ пробуется как userIdx.
	void OnSoulGuardIndex(int32 CharId, int32 SoulGuardIdx);
	// Создать/обновить/мягко убрать компаньона у КОНКРЕТНОГО игрока по текущему кэшу и опциям показа.
	void RefreshSoulGuardian(int32 CharId);
	// Пер-кадровое доведение всех записей (эталонный charVisibilityStateRefresh).
	void TickSoulGuardians();
	// Игрок применил АКТИВНЫЙ скилл хранителя: вариант 1/2/3 = SkillCoinTable[skillId].Field16
	//   (§7.2) → мотион 5/10/11 компаньона + эффект слота 31/32/33. Ключ — charId (как у карты 6156).
	void NotifySoulGuardianSkill(int32 CharId, int32 Variant);
	// ★★★ СЕТЕВОЙ ВХОД «сосед (или мы сами) применил скилл хранителя»: 6159/6160/6161 адресуют
	//   кастера СЕССИОННЫМ userIdx, а реестр компаньонов живёт на charId. Резолвим через актора.
	//   Вызывается ТОЛЬКО из Tick (закон no-reentrant-net-callbacks) — пакет лишь ставит заявку.
	void NotifySoulGuardianSkillByUserIdx(int32 UserIdx, int32 Variant);
	// Поставить заявку из колбэка пакета (6159/6160/6161) — применится в TickSoulGuardians.
	void QueueSoulGuardianSkill(int32 UserIdx, int32 Variant);
	void DrainPendingGuardSkills();
	// Компаньон, чей владелец — этот актор (обход реестра; реестр маленький — до 1 записи на игрока).
	class AAsdaSoulGuardianActor* FindSoulGuardianByOwner(const AActor* Owner) const;
	// Есть ли ЖИВОЙ компаньон у игрока (нужно плашке «SOUL GUARDER»: с ним она поднимается на 400).
	//   Ключ — charId; для своего игрока принимается и userIdx (см. фолбэк в FindSoulGuardian).
	bool HasSoulGuardian(int32 CharId) const;
	// Опции показа — эталонные `g_bFxShowSelfAngel` (0x2FFC91C) / `g_bFxShowOtherAngel` (0x2FFC91D),
	//   чекбоксы 877/879 окна графики, файл `./system/FxConfig.ini` секция [FxConfig].
	//   ★ Дефект оригинала при чтении (§2.5): `if (!strcmp(v,"true") || !strcmp(v,"0")) флаг = 1;` —
	//     значение "0" ВКЛЮЧАЕТ показ, выключает только "FALSE". У нас честный bool, дефолт = включено
	//     (совпадает с фактическим поведением оригинала). Хранится в GGameUserSettingsIni [FxConfig].
	void SetShowAngel(bool bSelf, bool bOn);
	bool GetShowAngel(bool bSelf) const { return bSelf ? bShowSelfAngel : bShowOtherAngel; }
	// Многострочный дамп компаньонов (консоль `asda.SoulFx.Dump`).
	FString DumpSoulGuardians() const;
	// Актор компаньона игрока (nullptr, если нет) — нужен консоли и диагностике.
	//   Ключ — charId; если по нему записи нет, ключ пробуется как userIdx (консольные эмуляции
	//   `asda.SoulFx.Peer` и старые вызовы вида `HasSoulGuardian(SelfUserIdx())`).
	class AAsdaSoulGuardianActor* FindSoulGuardian(int32 CharId) const;
private:
	void LoadAngelOptions();
	// Владелец по ключу карты 6156: сначала charId (эталонный CharManager_FindByCharId), затем
	//   фолбэк «ключ = userIdx» для консольных эмуляций.
	AActor* OwnerByGuardKey(int32 Key) const;
	TMap<int32, int32> SoulGuardIdxByCharId;                                  // карта эталона mgr+0x17C
	TMap<int32, TWeakObjectPtr<class AAsdaSoulGuardianActor>> SoulGuardians;  // charId → живой компаньон
	// ★ Заявки на боевой мотион хранителя, снятые с 6159/6160/6161. Из колбэка пакета актора НЕ
	//   трогаем (закон no-reentrant-net-callbacks) — применяем в TickSoulGuardians.
	TArray<TPair<int32, int32>> PendingGuardSkill;   // (userIdx кастера, вариант 1..3)
	bool bShowSelfAngel = true;      // g_bFxShowSelfAngel  (+0x1FC)
	bool bShowOtherAngel = true;     // g_bFxShowOtherAngel (+0x1FD)
	bool bAngelOptionsLoaded = false;
	FTSTicker::FDelegateHandle SoulGuardTickHandle;   // пер-кадровое доведение (эталон — каждый кадр)
	// ★ 6722: фракция/звание СОСЕДА по charId (faction_peer_re.md). Кэш — как в оригинале: запись живёт
	//   в списке до появления чара, слив идёт каждый кадр (netHandleFactionUpdateInRange@0xCA7820).
	void OnPeerFaction(int32 CharId, int32 CampId, int32 Grade);
	TMap<int32, FIntPoint> FactionByCharId;   // charId → (campId, grade)
	// ★ 6152: соулмейт-стейт СОСЕДА по charId (основной канал чужих колец — §J). Кэш по charId:
	//   пакет идёт в AoI-бандле сразу после 4005 и может опередить спавн актора.
	void OnSoulmatePeer(int32 CharId, int32 SoulIdx, int32 PartnerCharId);
	TMap<int32, int32> SoulIdxByCharId;   // charId → soulIdx (>0 = кольцо)
	// ★★★ ОДЕРЖИМОСТЬ СОСЕДА (SoulState 0/1/2, 2 = одержим). Разбирается сетевым слоем из ДВУХ
	//   каналов — 6152 (байт +12, ключ charId) и 5220 (байт +32, ключ userIdx); доказательство —
	//   `Reports/possess_broadcast_soulstate.md`. Здесь только показ: цвет имени (эталон) и
	//   наша метка «(Одержим)» в плашке. Приоритет источника — как у колец: charId основной.
	void OnPeerSoulStateByCharId(int32 CharId, int32 SoulState);
	void OnPeerSoulStateByUserIdx(int32 UserIdx, int32 SoulState);
	// Проставить SoulState актору (nullptr игнорируется): плашке — как есть (реагирует на 2: цвет имени +
	//   метка), персонажу — признак `== 1` (режим «душа»: модель + вся экипировка полупрозрачны α=0.3,
	//   `AAsdaPlayerCharacter::SetSoulTranslucent`). Обе ветки независимы.
	void ApplySoulStateToActor(AActor* Ch, int32 SoulState);
	// ★ СВОЙ SoulState — из единого поля `UAsdaNetSubsystem::SelfSoulState` (0/1/2, ответ 1017).
	//   `WireValue` — что про нас сказал провод (6152/5220) или −1; расходится — только запись в журнал.
	int32 SelfSoulStateAuthoritative(const TCHAR* Source, int32 WireValue) const;
	// SoulState для спавнящегося актора: сперва по charId (6152), затем по userIdx (5220), иначе 0.
	int32 ResolveSoulStateFor(int32 CharId, int32 UserIdx) const;
	// 5203 бонд: церемония soulmatec (attach 5 = следует за чаром) + звук S_c_SMMate01 у обоих (wings §F1).
	void OnSoulmateBond(int32 Subcode, int32 PairIdx, int32 InitiatorUidx, FString PartnerName);
	// 5207 разрыв: кольцо OFF у себя и партнёра (по сохранённому userIdx) (wings §F2).
	void OnSoulmateBreak(int32 Subcode, FString Name);
	// Сердечки-линк: раз в 1..5с (эталон rand()%5000, мин 1000) снаряд soullink02 → партнёр, импакт soullink03.
	void TickSoulHearts();
public:
	// Актор чужого игрока по имени (эталонный CharManager_FindByName). nullptr если нет.
	//   ПУБЛИЧНО: нужен и соулмейту (5203), и облачку чата (6719/5106 адресуют персонажа по имени).
	AActor* FindPlayerActorByName(const FString& Name) const;
	// Актор по userIdx (self при SelfUserNum, иначе NetPlayers). nullptr если не в мире.
	//   Публично: нужен меню смерти (имя инициатора воскрешения в опциях 0/1) и презентации смерти.
	AActor* ActorByUserIdx(int32 UserIdx) const;
	// (обратный поиск «актор → userNum» — единственный: `FindUserIdxByActor` выше)
	// ★ Актор игрока по charId (эталонный CharManager_FindByCharId): свой павн при совпадении с
	//   SelectedCharId, иначе поиск по NetPlayers.CharId. Нужен облачку чата (5089/4121 адресуют
	//   собеседника именно charId, а не userIdx).
	AActor* FindPlayerActorByCharId(int32 CharId) const;
	// ★★★ Дедуп №2, В2-A (N4, 11.10.2026): ДВЕ функции «актор игрока» вместо пяти разных семантик — `ActorByUserIdx` (выше) и
	//   `ActorByCharId`: обе = эталонные `CharManager_FindByUserIdx@0x9D1240` / `CharManager_FindByCharId` — СВОЙ павн первым (при
	//   `IsValid`), затем реестр соседей `NetPlayers`. Прежние имена — форварды: `FindPlayerActorByCharId` ≡ `ActorByCharId`;
	//   `GetPlayerActorByCharId` раньше искал ТОЛЬКО в `NetPlayers` (себя не находил, отряд и обводка цели шли через него) —
	//   ★ ИСПРАВЛЕНИЕ, ключ отката `asda.World.CharIdLookupSelf 0`; `FindPlayerActor`/`GetPlayerActorByUserNum` раньше смотрели
	//   `NetPlayers` ПЕРВЫМ (устаревшая запись соседа с моим номером отдавала чужого) — ★ ИСПРАВЛЕНИЕ, ключ отката
	//   `asda.World.UserIdxLookupSelfFirst 0`. charId 0 — никто; свой charId неизвестен (0) — себя не находит (`AsdaSelf::IsSelfCharId`).
	AActor* ActorByCharId(int32 CharId) const;
	// ★ 27.09.2026 ОБХОД ВСЕХ ПЕРСОНАЖЕЙ МИРА с их charId — свой павн (при известном `SelectedCharId`) и каждый
	//   живой актор реестра `NetPlayers`. Эталонная пара проходов: `LocalPlayer_UpdateRender@0x9D4170` (свой) и
	//   `CharManager_UpdateAll@0x9D56C0` (остальные). Нужен тику «короны» 6126 (`UAsdaKingCrownSubsystem`):
	//   `CharFx_UpdateKingCrown@0xA21110` зовётся для КАЖДОГО персонажа и сравнивает `g_nCrownOwnerId` с его charId.
	//   Реестр остаётся приватным — наружу только пары (charId, актор). bSelf = true у своего павна.
	void ForEachPlayerActorWithCharId(TFunctionRef<void(int32 CharId, AActor* Actor, bool bSelf)> Fn) const;
	// ★ 24.10.2026: то же с userIdx (ключ реестра `NetPlayers`; свой — `SelfUserNum`) — без обратного поиска актор → userIdx
	//   (`FindUserIdxByActor` линеен, в покадровом проходе давал O(N²)). charId 0 у своего (не известен) — свой всё равно отдаётся.
	void ForEachPlayerActorWithIds(TFunctionRef<void(int32 UserIdx, int32 CharId, AActor* Actor, bool bSelf)> Fn) const;
	// ★ Актор МОБА по сетевому renderId (эталонный Entity_FindByRenderId): облачко 8008.
	class AAsdaMobActor* FindNetMobByRid(int32 Rid) const;
	// ★ Аудит №3 (B6): ЕДИНАЯ развилка «(вид, индекс) → актор» — порт двухветочного поиска эталона
	//   (`Entity_FindByRenderId@0xF772D0` / `CharManager_FindByUserIdx@0x9D1240`). Была размножена в
	//   шести местах (визуал навыков, площадные удары, эффекты состояний, площадная заявка, хранитель).
	//   Вид цели передаётся ТОЛЬКО явным enum: сырой байт `kind` в разных каналах инвертирован
	//   (см. `AsdaTargetKindFromHitRec`). nullptr, если такой цели в мире нет — эталон в этом случае
	//   просто не выполняет ветку удара, показ при этом не рвётся.
	AActor* ActorByTarget(EAsdaTargetKind Kind, int32 Idx) const;
	// ★★★ Порт `objectsPickInBoundingBox` (эталон `Skill_SendAreaSkillTargetsCast@0xA01140`,
	//   skill_buff.c:3186-3200): renderId'ы СУЩНОСТЕЙ внутри осевого бокса в ИГРОВЫХ координатах
	//   (X = UE.Y, Y = −UE.X). Эталон отводит массив ровно на 20 целей и исключает свой renderId
	//   (у своего павна его нет вовсе, поэтому отдельного условия не нужно).
	//   Возвращает число найденных. Нужен площадным навыкам `castType == 0`, `resourceType` 2/3.
	//   ★ `ExcludeRenderId` — порт `entityQueryInRectExcl@0xF77390` (entity_char.c:54067): сущность
	//   с этим renderId в выборку НЕ попадает. Эталон так исключает ОСНОВНУЮ цель, которую
	//   `skillCastAreaSendPacket@0xA00570` кладёт в запись №0 своими руками (skill_buff.c:2929-2930).
	int32 PickEntityRenderIdsInGameBox(const FVector2D& Center, float HalfExtent,
		TArray<int32>& OutRenderIds, int32 MaxCount = 20, int32 ExcludeRenderId = -1) const;
	// ★ Порт `charManager_collectTargetsInRect@0x9DB560` (entity_char.c:9612): userIdx ИГРОКОВ
	//   в том же игровом боксе. Эталон СВОЙ персонаж из выборки ИСКЛЮЧАЕТ
	//   (`if (LOWORD(localPlayer->userNum) != LOWORD(i->userNum))`) и работает только в PvP-режиме
	//   (`buffFxSlots[92]`), поэтому здесь тот же смысл: цели-ПРОТИВНИКИ площадного каста.
	//   Нужен `skillCastAreaTargetsAndSend@0xA12800` (гейт — флаг `buffFxSlots[160]` = идёт PvP-бой,
	//   опкод 4206). `PrimaryUserIdx` — уже занятая запись №0 (в выборку не попадает).
	//   ★ GM-скрытые (`IsCharHiddenByGm`) пропускаются — эталон entity_char.c:9657 `(sub_93B218(i)&1)`.
	int32 PickPlayerUserIdxsInGameBox(const FVector2D& Center, float HalfExtent,
		TArray<int32>& OutUserIdxs, int32 MaxCount = 20, int32 PrimaryUserIdx = -1) const;
	// ★★★ Порт `charCollectPartyMembersInBox@0x9DB8E0` (entity_char.c:9704) — цели ГРУППОВОГО
	//   площадного баффа (`Skill_SendAreaSkillCast@0xA01DA0`, targetMode == 2; skill_buff.c:3487).
	//   Эталон: если `PrimaryUserIdx >= 0`, он идёт ПЕРВОЙ записью (это сам кастер — «и на себя»),
	//   дальше в бокс добираются ТОЛЬКО СОГРУППНИКИ (`Party_FindMemberByUserIdx`), сам кастер
	//   вторично не добавляется, потолок 20.
	int32 PickPartyUserIdxsInGameBox(const FVector2D& Center, float HalfExtent,
		TArray<int32>& OutUserIdxs, int32 MaxCount = 20, int32 PrimaryUserIdx = -1) const;
	// ★ Ростер группы. Пакетов группы (отряда) в порту ещё нет — список пуст, и это ЧЕСТНО означает
	//   «согруппников рядом нет», а не «функция не реализована»: эталон в этом случае собирает ровно
	//   одну запись — самого кастера. Заполняется, как только появится подсистема отряда; для живой
	//   проверки есть консольная команда `asda.Party.Set <uidx> [uidx…]`.
	void SetPartyRoster(const TArray<int32>& UserIdxs);
	// ★ 26.10.2026 (bowfx, ревизия целей навыков п.7) [ОРИГИНАЛ]: отряд — настоящий (`UAsdaPartySubsystem`, пакеты 5091…5107),
	//   ростер `SetPartyRoster` остаётся дев-подменой для проверок. Эталон: `Party_IsInParty` / `Party_FindMemberByUserIdx`.
	bool IsPartyMember(int32 UserIdx) const;
	bool IsInParty() const;
	// ★★★ ДИАГНОСТИКА ПОЛА (консоль `asda.Gender`): пол своего персонажа из КАЖДОГО источника
	//   (char-list 0x3EE rec+22, блок 1017 +27, снапшот 4005 body[37] / 4065 rec[54]) + фактические
	//   базовые меши павнов + пол каждого соседа. Нужна, чтобы расхождение источников было видно сразу.
	FString DumpGenderDiagnostics() const;
private:
	// Спавн attached-эффекта персонажа (BP_Fx_<jebBase>): follow-кость (или root), авто-смерть с носителем.
	// ★ Аудит №3 (A11): параметра `FollowBone` больше нет — привязку к кости ставит вызывающий через
	//   `AAsdaEffectActor::ConfigureBoneAttach` (единственная точка привязки эффекта к носителю).
	AActor* SpawnCharFx(const FString& JebBase, AActor* Owner, const FVector& Offset, FName Tag);
	// Таблица PlusPackage (id → базовые имена jeb муж/жен); лениво из Content/AsdaAssets/Data/Character.
	bool LookupWingJeb(int32 WingId, bool bMale, FString& OutJebBase);
	TMap<int32, TPair<FString, FString>> PlusPackageTable;   // id → (male, female)
	bool bPlusPackageLoaded = false;
public:
	// ═══ ★ СНАРЯДЫ ДАЛЬНОБОЙНОГО ОРУЖИЯ (projectile_re.md) ═══
	// Типы оружия со снарядом (эталон Player_ProcessAttackTick@0x99F2A0 / CCharacter_AttackAnimTick@0x9A2200:
	//   отдельные ветки switch(equippedWeaponType) — 8/0xA лук, 9 арбалет, 0xB баллиста; 0..5 — ближний бой).
	static bool IsRangedWeapon(int32 WeaponSubType)
	{
		return WeaponSubType == 8 || WeaponSubType == 9 || WeaponSubType == 10 || WeaponSubType == 11;
	}
	// Мировая точка выстрела: кость «8_b1» носителя (эталон 52082 = List_FindIndexByName(model,"8_b1")).
	//   bFound=false → кости нет, вернётся запасная точка (центр актора + рост/2).
	FVector ProjectileMuzzle(AActor* Attacker, bool& bFound) const;
	// Выстрел: снаряд от кости оружия к ЗАФИКСИРОВАННОЙ точке цели; по прилёте — OnHit (цифры+импакт+звук).
	//   Возвращает длительность полёта в секундах (0 = снаряд не создан).
	float FireProjectile(AActor* Attacker, AActor* Target, int32 WeaponSubType,
		int32 AttackerUserIdx, int32 TargetRid, TFunction<void()> OnHit);
	// Стрела НА ТЕТИВЕ (эталон charObjectArray_initSixSlots: меш ..._0 виден с начала замаха до выстрела).
	void SetNockedArrow(AActor* Attacker, bool bOn);

	// ★ Лут-сообщение → СИСТЕМНЫЙ ЧАТ (CHAT_NEW_SYSTEM 2572) + плавающий оверлей A/B (2557/2558) —
	//   ровно как эталон Net_HandleItemPickupResult/uiInventoryOnRecvItemAcquired через
	//   CChat_AddTokenizedLines. bTrackB = дорожка B (значимое событие: свой успешный подбор).
	//   HERO_NOTICE_WIN здесь НЕ используется — он только для события про другого игрока (6131).
	void ShowLootNotice(const FString& Text, const FLinearColor& Color, bool bTrackB = false);

	// ★ ЗОЛОТО: в 5013 по смещению body+12 лежит НЕ количество подобранного, а НОВЫЙ ИТОГ кошелька.
	//   Эталон uiInventoryOnRecvItemAcquired@0xD10E80 (ui_core.c:47091-47098):
	//       deltaQuantity = *(int *)(Src + 9) − thunk_getField770234(g_pGameClient);   // итог − прежний итог
	//       if (deltaQuantity > 0) { SetSlotPair(0, deltaQuantity, 0); loc 20262 }
	//       uiSetHonorPointsDisplay(g_pGameClient, *(int *)(Src + 9));                 // запомнить новый итог
	//   Порт держит тот же «прежний итог» здесь. −1 = ещё не знаем (первый пакет только сеет базу).
	int64 SelfGold = -1;

	// ★ 05.11.2026 (жалоба «в чате нет количества полученного»): ПРЕЖНЕЕ количество стопки для loc 20131.
	//   Эталон: `delta = Src+9 − g_aInvSlotItemCode[слот]` (ui_core.c:47100) — СТАРОЕ число в ячейке читается ДО
	//   `OnRecv_ItemUpdate`. В порту запись 5013 кладётся в модель сумки отложенно (очередь `Tick`, закон
	//   no-reentrant-net-callbacks), поэтому старое число берётся из модели. Если два 5013 в ОДНУ ячейку пришли за
	//   один кадр, модель ещё не видела первый — для второго «прежним» служит итог первого, запомненный здесь.
	//   Ключ = (контейнер << 16) | слот; запись действительна только в кадре `Frame` (GFrameCounter).
	struct FLootStackSeen
	{
		int32  ItemCode = 0;   // код предмета, итог которого запомнен
		int32  Count    = 0;   // НОВЫЙ итог в ячейке после этого 5013 (Src+9)
		uint64 Frame    = 0;   // кадр приёма; в другом кадре запись не используется (модель уже применила пакет)
	};
	TMap<int32, FLootStackSeen> LootStackSeenThisFrame;   // ячейка → итог, принятый в текущем кадре

	// ── Имена/модели/звук-базы сущностей из таблиц — ★ волна 3 п.14 (15.09.2026): единая точка
	//   `World/AsdaEntityText.h` (`MobName`/`MobModel`/`MobSoundBase`/`NpcName`/`NpcDisplayName`). Прежние
	//   `GetMobName`/`GetNpcName`/`GetMobSoundBase`/`GetMobModelName` и своя загрузка `EnsureNameTables`
	//   (сильные члены MobNameTable/NpcNameTable/MobSoundTable) удалены — тела совпадали с плашкой (K6/A11).
	// ★ УДЕРЖАНИЕ ТАБЛИЦ ИМЁН (15.09.2026, ревизия п.14). `AsdaDt::Cached` держит таблицу СЛАБО (так надо
	//   редактору, см. `AsdaDataTableCache.h`); прежние члены мира держали `DT_MobNames`/`DT_NpcNames`/`DT_MobSounds`
	//   СИЛЬНО всё время жизни GameInstance. Без этого в -game/cooked сборщик мусора выгружает таблицы между
	//   спаунами, и следующий спаун моба/NPC синхронно грузит их заново (рывок кадра). Здесь — только удержание:
	//   объекты берутся через ТУ ЖЕ `AsdaDt::Cached(AsdaEntityText::k*Path)`, разбора имён в мире нет.
	//   Момент — как у прежнего `EnsureNameTables`: первый запрос имени/модели/звук-базы из мира
	//   (сетевой спаун NPC, динамический спаун моба, `CreateNetMob`), один раз, все три таблицы разом.
	//   Возврат — сколько таблиц удержано (0..3); повторный вызов ничего не грузит. Публичный — для стенда GC.
	int32 HoldEntityNameTables();
	// Была ли уже попытка удержания (стенду GC — решить, возможен ли контроль «без удержания»).
	bool IsEntityNameTablesHeld() const { return bEntityNameTablesHeld; }
	// ★ УДЕРЖАНИЕ ТАБЛИЦ СПАУНА (15.09.2026, отчёт `Reports/dedup_audit/dt_gc_hold.md`). Тот же дефект, что у таблиц имён:
	//   `DT_MobStats`/`DT_MobTextures`/`DT_MobNameColors`/`DT_NpcQuestNpc`/`DT_Clan_*`/`DT_PetModels`/`DT_RideEffect`/`DT_SkillFx`
	//   читаются через слабые кэши (их же зовёт `OnConstruction` в редакторе), сильного владельца у них не было —
	//   в -game GC выгружал их, и следующий спаун моба / NPC / питомца / удар хранителя грузил таблицу заново.
	//   Мир держит их сильно (`FAsdaHeldTables`), лениво в месте первого использования группы: сетевой спаун
	//   моба/NPC/питомца/хранителя, пакет клана, пакет транспорта; экран логина ничего не грузит.
	//   Возврат — сколько таблиц группы удержано; повтор без загрузки. Публичные — для стенда `asda.DtHold.GcCheck`.
	int32 HoldSpawnTables(EAsdaSpawnTables Group);
	bool IsSpawnTablesHeld(EAsdaSpawnTables Group) const;
	const FAsdaHeldTables& GetHeldSpawnTables() const { return HeldSpawnTables; }
private:
	// Сильные ссылки на таблицы имён (порядок: мобы, NPC, звуки; отсутствующий ассет не кладётся).
	UPROPERTY(Transient) TArray<TObjectPtr<UDataTable>> HeldEntityNameTables;
	// Сильные ссылки на таблицы спауна по группам `EAsdaSpawnTables` (см. `HoldSpawnTables`).
	UPROPERTY(Transient) FAsdaHeldTables HeldSpawnTables;
	bool bEntityNameTablesHeld = false;   // как прежний bNameTablesLoaded: попытка одна (нет ассета → не повторяем)
public:
	// Шаблон-хелперы: запечённый актор нужного типа, ближайший к тайлу (источник меша/анимов/скейла).
	class AAsdaNpcActor* FindFreeNpc(int32 NpcType, int32 TileX, int32 TileZ) const;
	class AAsdaMobActor* FindFreeMob(int32 MobType, int32 TileX, int32 TileZ) const;
	// ★ ДИНАМИЧЕСКИЙ спаун моба (как оригинал CMob_Init: модель по type-id из Mob list, БЕЗ запечённых):
	//   имя модели = DT_MobNames строка MobId-1 колонка Model → ассеты /Models/MobSkel|/Models/Mob.
	//   Скелетал не найден → статик-фолбэк; ничего нет → nullptr (вызвавший падает на шаблон уровня).
	class AAsdaMobActor* SpawnNetMobDynamic(int32 MobType, const FTransform& Xf);
	// ★ Полное создание СЕТЕВОГО моба (общий код каналов 4013 / 4019 / 4017 flags&4): модель по type-id,
	//   позиция = cur-тайл пакета, HP, фейд-ин 1500 мс, плашка/звуки, регистрация в NetMobs.
	//   Chan — имя канала для лога («4013»/«4019»/«4017»). nullptr при неудаче (нет ни модели, ни шаблона).
	class AAsdaMobActor* CreateNetMob(int32 Rid, int32 Type, int32 Hp, FIntPoint CurTile, FIntPoint DstTile,
		int32 FaceDeg, float SpeedUnitsPerMs, int32 StateByte, const TCHAR* Chan);
	float TerrainZ(const FVector2D& UeXY) const;   // высота террейна трейсом (общий хелпер)

	bool bPendingSelfSpawn = false;   // 4003 пришёл раньше загрузки уровня
	FDelegateHandle PostLoadMapHandle;
};
