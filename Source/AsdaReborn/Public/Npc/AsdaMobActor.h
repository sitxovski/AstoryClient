// Copyright (c) 2026 @sitxovski (Discord)
// AAsdaMobActor — моб/босс в уровне (порт AsdaStory). Editor-Friendly: авторская расстановка запекается
//   из СЕРВЕРНЫХ MonsterData_Info\<Map>.svr (те же файлы читает GameServer — позиции 1:1 с рантаймом),
//   видна и редактируется в редакторе (спавн-точки, рамки роуминга, пути патруля).
// АРХИТЕКТУРА adoption (как NPC): при входе в мир сетевой батч-спавн 4013 (записи 173б) НЕ создаёт
//   дубль — находит запечённого актора по MobId+спавн-тайлу и привязывает renderId (NetRenderId);
//   дальше сущностью управляет сеть (позиция/HP/смерть/респавн). До adoption актор — «превью» (idle).
// Эталон: запись .svr (65_npc_mob_placement.md §2.2): роам-рамка/патруль — АВТОРСКИЕ данные (AI сервера
//   их не потребляет: home-pos + per-type wander); рисуем как справку дизайнеру.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AsdaMobActor.generated.h"

// ★ Какой именно фейд-аут сейчас идёт у моба (эталонные флаги сущности +0x1E4):
//   Corpse = флаг 4 (посмертный, ставится когда доигран death-клип, таймер +0x258 = 1500.0f) → по
//            истечении флаг 0x10 → entityListUpdateFade@0xF78660 зовёт charRemoveEntity — СУЩНОСТЬ УДАЛЯЕТСЯ;
//   Aoi    = флаг 0x100 (уход из зоны видимости, таймер +0x248 = 10000мс) → фейд 1000мс → charDespawnRemove.
//   Различать обязательно: реген (4013, ветка «rid найден») отменяет ТОЛЬКО AoI-фейд
//   (`if (mob[121] & 0x100) mob[121] = 1;`), посмертный НЕ отменяет.
enum class EAsdaMobFade : uint8 { None, Corpse, Aoi };

// ★★★ ОТЛОЖЕННЫЙ ПАКЕТ 4015 — порт эталонного буфера `entity[1753]` (флаг «есть отложенный») +
//   `entity+1754` (37 байт тела пакета, +0x1B68). Пакет движения, пришедший, пока моб В ЗАМАХЕ
//   (состояние 3) или ВО ФЛИНЧЕ (13/14), эталон НЕ выбрасывает — он кладёт его в буфер и применяет
//   в тот кадр, когда клип доигран. Ставят буфер: `Recv_CombatHit@0xF310E0` case 5 (network.c:37859,
//   состояния 3/13/14), case 7 (:37945, только состояние 3), case 0xE (:38014, состояния 13/14).
//   Сливают буфер ТРИ тика, все по КОНЦУ КЛИПА:
//     · состояние 3  — `Combat_ApplyDamage@0xF7C470` (combat_pvp.c:8170): kind 5 → playerCastDirectionalEffect2,
//                      kind 7 → playerCastDirectionalEffect, буфер пуст → `SetActionState(6)`;
//     · состояние 13 — `charAction_tickWarp23@0xF79CE0` (entity_char.c:54826 и :54900);
//     · состояние 14 — `effectObjectTickActionState@0xF7A720` / `effectTimer_updateTick@0xF7AC70`.
//   Скорость при отложенном применении эталон берёт СЫРУЮ (`100.0/буфер+15`), без нормировки по
//   времени отрезка — во всех четырёх местах слива одинаково.
struct FAsdaMobPendingAction
{
	bool      bValid   = false;                    // эталон entity[1753]
	uint8     Kind     = 0;                        // буфер +6
	FVector2D CurGame  = FVector2D::ZeroVector;    // буфер +7/+9   (тайл → центр клетки, game XZ)
	FVector2D DstGame  = FVector2D::ZeroVector;    // буфер +11/+13
	float     Speed    = 0.f;                      // 100 / (i16 буфер+15), юнитов/мс
	float     AnimRate = 1.f;                      // (i16 буфер+17) × 1e-4 — темп клипа
};

UCLASS()
class ASDAREBORN_API AAsdaMobActor : public AActor
{
	GENERATED_BODY()

public:
	AAsdaMobActor();

	// Скелет-меш моба (MobSkel; 33 модели без клипов — статик-фолбэк через StaticMeshComp).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Mob")
	TObjectPtr<class USkeletalMeshComponent> Mesh;
	// Фолбэк для моделей без анимаций (tree/stone-декор — импортированы статикой).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Mob")
	TObjectPtr<class UStaticMeshComponent> StaticMeshComp;

	// ★ ХИТБОКС ДЛЯ ПИКИНГА (эталон: клик/наведение проверяют габариты сущности, а не полигоны).
	//   Скелетал-меш без физ-ассета трейсом НЕ ловится, поэтому нужна простая капсула (QueryOnly).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Mob")
	TObjectPtr<class UCapsuleComponent> PickHitbox;

	// ★ Над-головная плашка (порт JUI TARGETMARK_MON 1:1): имя + 4-сегментная полоса HP.
	//   Создаётся в конструкторе → видна в РЕДАКТОРЕ. HP показывается при выборе целью (эталон).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Mob")
	TObjectPtr<class UAsdaNameplateComponent> Nameplate;

	// Отображаемое имя моба (DT_MobNames по MobId) — для плашки; заполняется при запекании/спавне.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob")
	FString MobName;

	// ★ Звук-база моба (Mob_list @+0x44, DT_MobSounds): wav = <база>_<фаза> (эталон
	//   Sound_BuildEntryWavName@0xF71450). Фазы: 4=смерть, 5+вариант=удар, 30/31=рык замаха.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob")
	FString SoundBase;
	// Сыграть фазовый звук моба 3D на своей позиции (пусто/не найден — тихо).
	void PlayMobSound(int32 Phase);

	// ★ ДОВОДКА 07.09.2026 (В1 ревизии review_cpu_insights.md): включить/выключить тик моба и его
	//   тикающих компонентов (меш + плашка). Нужно ДЕВ-КОМАНДАМ, которые показывают ЗАПЕЧЁННОГО
	//   (навсегда скрытого) моба: после волны «сна запечённых» (asda.Tick.DormantBaked) он виден,
	//   но без позы и без плашки. Игровой путь этим не пользуется — там всё делает BeginPlay.
	void SetDormantComponentTicks(bool bEnabled);

	// Идл-анимация (клип _0). Играет в редакторе (animation_data) и в BeginPlay.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob")
	TObjectPtr<class UAnimSequence> IdleAnim;

	// TYPE-ID моба (1-based, строка Mob list.BIN = id-1). Ключ adoption при 4013.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob")
	int32 MobId = 0;

	// Авторские данные размещения (.svr).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob")
	int32 TileX = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob")
	int32 TileZ = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob")
	int32 RegenTag = 0;          // regen-тег (5000/40000/600000…; живой респавн — из таблицы типа)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob")
	int32 GroupId = -1;          // -1=одиночка, ≥0=патруль-группа
	// Рамка роуминга [minX,minZ,maxX,maxZ] в тайлах (авторская; 0 0 0 0 = нет).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob")
	TArray<int32> RoamRect;
	// Путь патруля (пары тайлов, wp0=спавн; альтернатива рамке).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob")
	TArray<FIntPoint> Patrol;
	// ★ .svr +0x49==1 — CALL/EVENT-моб (сервер: LoadMonster обнуляет авто-реген; спавн по CallMobTable/
	//   событию — боссы kaiya/firegolem/spade/bigfoot и т.п.). В игре скрыт до вызова (место видно в редакторе).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob")
	bool bCallMob = false;

	// ── ★ ЭТАЛОННЫЕ ЧИСЛОВЫЕ ПОЛЯ ТИПА (DT_MobStats, строка MobId−1 — см. Data/AsdaMobStatsRow.h) ──
	// Уровень типа (Mob list.BIN +0x98; клиент кладёт его в сущность +0x1DC, CMob_Init@0xF83100)
	//   и байт агрессии (+0xFC → сущность +6979) — вместе задают ЦВЕТ ИМЕНИ по разнице уровней.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob|Stats")
	int32 MobLevel = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob|Stats")
	int32 MobAggression = 0;

	// ── ★★★ Д8: ТРИ ПОЛЯ ТИПА, КОТОРЫЕ ЭТАЛОН ПЕРЕНОСИТ В СУЩНОСТЬ ПРИ КАЖДОМ СПАУНЕ ──────────────
	//   `Recv_MobSpawn@0xF305E0` (network.c:37178-37181) и дословный повтор в ветке спауна из пакета
	//   удара (`Recv_CombatHit@0xF310E0`, game.c:548220-548222):
	//       ent+6978 ← Mob list.BIN +0xF8   — РАНГ/иконка (0..5, шесть контролов панели цели)
	//       ent+6977 ← Mob list.BIN +0x100  — «АТАКУЕТ ПЕРВЫМ» (контрол BAT_FIRST панели цели)
	//       ent+6979 ← Mob list.BIN +0xFC   — агрессия (уже есть выше: MobAggression)
	//   и `ent+18700 ← +0x10C` — ЧИСЛО «ЖИЗНЕЙ» = сегментов полосы HP у боссов.
	//   До 2026-08-11 ни одно из трёх нигде не читалось: у босса полоса была одноsegментной, а панель
	//   цели не знала ни ранга, ни признака «бьёт первым».
	// ★★★ МИСНЕЙМ (вскрыт 2026-08-24 при порте панели цели, подробности в `AsdaMobStatsRow.h`):
	//   `MobRank` (+0xF8 → ent+6978) — это ИНДЕКС СТИХИИ 0..5 (метки `PVP_MARK00..05`), а НЕ ранг.
	//   Настоящий ранг/кольцо (Normal/Named/Elite/Boss) живёт в `MobAggression` (+0xFC → ent+6979).
	//   Имя поля не меняю: оно уже прописано в `DT_MobStats` и в коде спауна.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob|Stats")
	int32 MobRank = 0;          // +0xF8  → сущность +6978 — ФАКТИЧЕСКИ индекс стихии (0..5)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob|Stats")
	int32 MobAttackFirst = 0;   // +0x100 → сущность +6977
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob|Stats")
	int32 MobLives = 0;         // +0x10C → сущность +18700 (сегменты полосы HP)

	// ★ ПОРТРЕТ ТИПА для панели цели (Mob list.BIN +0xD4 имя атласа, +0xF4 номер ячейки 46×46,
	//   сетка 11 колонок). Эталон: `hudShowMobTargetInfo`, ui_core.c:75762-75769.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob|Stats")
	FString MobPortraitTex;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob|Stats")
	int32 MobPortraitCell = 0;
	// ★★★ MaxHp ТИПА (Mob list.BIN +0x94, аксессор `sub_932483 → 0xF31040`). Эталон кладёт его в
	//   `ent[285]` при КАЖДОМ спауне и НИКОГДА не берёт максимум из пакета: пакет несёт только
	//   ТЕКУЩИЙ HP (`ent[283] = ent[284] = @25` в 4015, `@92` в 4017, `@9` в 4013/4019).
	//   0 = строки типа нет ⇒ вызывающий оставляет прежнее поведение (максимум = текущий HP).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob|Stats")
	int32 MobTypeMaxHp = 0;

	// ★★★ ВЫСОТА ПЛАШКИ ИМЕНИ — КОНСТАНТА ИЗ ДАННЫХ (закон [[nameplate-height-from-data]]).
	//   Полу-размер AABB по Y из Mob list.BIN +0x84+4 (аксессор sub_930AC5 → 0xF31080).
	//   Спавн (Recv_MobSpawn@0xF305E0, network.c:37194-37205) строит AABB:
	//     half = (+0x84 три float);  центр = мировая позиция;  центр.Y += half.Y
	//   Плашка садится на ВЕРХ AABB (helper_getSumF = центр.Y + half.Y = земля + 2·halfY) и
	//   поднимается ещё на +50.0 (game_main.c:4541 hover / 4750 выбранная цель).
	//   ⇒ H = 2·PlateHalfY + PlateExtraOffset. Габариты меша НЕ участвуют (они раздуты запасом
	//   на анимацию — именно из-за них имя у высоких мобов улетало выше нужного).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob|Stats")
	float PlateHalfY = 0.f;
	// Эталонная константа +50.0 (общая для hover- и target-плашки сущности). Editor-Friendly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob|Stats")
	float PlateExtraOffset = 50.f;
	// Ручной оверрайд высоты (>0 — победит над расчётом; 0 = по данным).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob|Stats")
	float PlateHeightOverride = 0.f;

	// ★ Аудит №3 (Дд1): ЕДИНАЯ точка доступа к `DT_MobStats` на весь порт. Раньше таблицу грузили и
	//   кэшировали ДВА независимых потребителя (`ResolveMobStats` здесь и `EnsureMobSizeTable`
	//   в `UAsdaSkillFxSubsystem`) — одни и те же числа лежали в памяти двумя копиями, а правка
	//   данных разъезжалась между ними. Теперь загрузка одна и кэш строк один.
	//   Возвращает строку типа моба (RowName = MobId − 1) или nullptr, если таблицы/строки нет.
	static const struct FAsdaMobStatsRow* FindMobStatsRow(int32 InMobId);
	// ★ Объектные пути таблиц ТИПА моба — одно объявление на порт (15.09.2026, dt_gc_hold): их читают
	//   `FindMobStatsRow`/`ApplyMobTextures`, а удерживает на сессию `UAsdaWorldSubsystem::HoldSpawnTables(Mob)`.
	static constexpr const TCHAR* kMobStatsPath    = TEXT("/Game/AsdaAssets/Data/Mob/DT_MobStats.DT_MobStats");
	static constexpr const TCHAR* kMobTexturesPath = TEXT("/Game/AsdaAssets/Data/Mob/DT_MobTextures.DT_MobTextures");

	// Подтянуть строку DT_MobStats по MobId (уровень/агрессия/полу-размер). Идемпотентно.
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Asda|Mob|Stats")
	void ResolveMobStats();
	// Эталонная высота плашки этого моба (0 — данных нет ⇒ остаётся расчёт по капсуле).
	UFUNCTION(BlueprintCallable, Category="Asda|Mob|Stats")
	float ResolvePlateHeight() const;
	// Применить высоту плашки (фиксированная эталонная константа вместо габаритов меша).
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Asda|Mob|Stats")
	void ApplyNameplateHeight();

	// renderId сетевой сущности (4013) — только у РАНТАЙМ-копий (-1 = запечённый превью-актор).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Mob")
	int32 NetRenderId = -1;

	// ★ HP цели-моба (эталон spawn_despawn.md §3.1: запись 4013 +9 i32 = curHp=maxHp при спавне; поля
	//   сущности +1132 curHp / +1140 maxHp / +18700 сегментов). Заполняется из сети, для MON-неймплейта.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Mob", Transient)
	int32 CurHp = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Mob", Transient)
	int32 MaxHp = 0;

	// ★★★ 18.10.2026 (бой: «HP выше максимума», `Reports/combat_trace_2026-10-18/bugs_death_and_ledger.md` §2) [ОРИГИНАЛ]:
	//   максимум моба `ent[285]` эталон берёт ТОЛЬКО из таблицы типа (`sub_932483`), пакеты HP его не меняют; при cur > max
	//   полоса не растягивается (`ui_core.c:3828` — `if (max < cur) return 0;`, обычная — `SetRange(0, max)`, упор в 100 %).
	//   Порт поднимал MaxHp вслед за HP пакета — это прятало дефект клиента (54/54 вместо видимой аномалии 54/45).
	//   Ключ `asda.Combat.MobMaxHpFixed` (1 — по умолчанию — max пакетами не поднимается, строка журнала «АНОМАЛИЯ: HP > MaxHp»;
	//   0 — прежний подъём). Без строки таблицы (`MobTypeMaxHp <= 0`) max по-прежнему берётся из пакета — другого источника нет.
	static bool IsMaxHpFixed();
	// HP пакета Hp из канала Chan выше MaxHp. Единая точка для всех каналов HP моба.
	//   ★ Ревизия 18.10: DT_MobStats уже синхронизирован с сервером (Monster.ai +0x2D, 186 правок 18.10), у живых типов табличный
	//   max = серверному. Поэтому канал СПАУНА/РЕСПАУНА (bSpawnChan: 4013 создание и повтор, 4019, 4017 flags&4, оживление) HP выше
	//   таблицы считает серверным максимумом и поднимает max (страховка для типов без синхронизации); боевые каналы (4015, 4029,
	//   лента, снимки) при ключе 1 и известном максимуме типа max НЕ поднимают — строка «АНОМАЛИЯ» (один раз на моба), CurHp
	//   хранится как есть, полоса упирается в 100 %. Ключ 0 / нет строки типа — прежний подъём в любом канале.
	void ApplyPacketHpToMax(int32 Hp, const TCHAR* Chan, bool bSpawnChan = false);
	bool bHpAboveMaxNoted = false;   // ★ ревизия 18.10: строка «АНОМАЛИЯ: HP > MaxHp» по этому мобу уже была (пишется один раз)

	// ★ СЕТЕВАЯ АРХИТЕКТУРА (2026-07-23, как оригинал): запечённые = превью редактора/шаблоны (в игре
	//   скрыты BeginPlay-ом); игровые мобы = рантайм-копии (SpawnActor Template) по 4013, деспавн 4011.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Mob", Transient)
	bool bNetSpawned = false;

	// ── Клип-слоты мобовой анимации (MobMotion <name>_N.jbb → anim_N; состояние поле+120 сущности = индекс
	//   клипа). РЕВЕРС 2026-07-23: ДВИЖЕНИЕ = 2 (projInitTrajectoryToTarget@0xF85690 и charMoveStepToCell-путь
	//   ставят state=2; прежний 1 давал «боевую» стойку), 5/7 = прыжковые/полётные траектории. Idle=0.
	//   ★ СМЕРТЬ = 4 (РЕВЕРС 2026-07-29: тик state-10 charAnimAdvanceActionTimer@0xF80930 зовёт
	//   objectSetIndexAndDispatch(this, 4, t) и меряет длину клип-записи model+256 = слот 256/64 = 4;
	//   SetActionState case 0xA сбрасывает время того же слота. Прежняя 6 была догадкой по звук-контексту —
	//   у моделей без anim_06 PlayClip молча не срабатывал → «смерть с анимацией бега»). Attack — фаза боя. ──
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob") int32 ClipIdle   = 0;
	// ★★★ БОЕВАЯ СТОЙКА = слот 1 (РЕВЕРС 2026-08-11): состояние 6 тикает
	//   `charAction_tickWarp23State@0xF79B80`, а он крутит ровно `objectSetIndexAndDispatch(this, 1, t)`.
	//   В состояние 6 эталон уводит моба ПО КОНЦУ ЗАМАХА (Combat_ApplyDamage, буфер пуст), ПО КОНЦУ
	//   ФЛИНЧА (charAction_tickWarp23) и ПО ПРИБЫТИИ в точку преследования (charAnim_UpdateActionTick).
	//   Прежний порт возвращал моба в мирный idle (слот 0) — «моб после удара расслабляется».
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob") int32 ClipCombatIdle = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob") int32 ClipMove   = 2;
	// ★★★ БЕГ = слот 3 (РЕВЕРС 2026-08-11): покадровый тик движения `effect_UpdateProjectileTick@0xF7BDD0`
	//   выбирает слот ПО СОСТОЯНИЮ: `if (ent[120] == 2) objectSetIndexAndDispatch(ent, 2, t)`
	//   (ходьба + звук фазы 2) `else objectSetIndexAndDispatch(ent, 3, t)` (бег + звук фазы 12).
	//   Состояния 5 (kind=5 CHASE / kind=0xE FEAR) и 7 (kind=7 BATTLEMOVE) идут в ветку «иначе» ⇒ БЕГ.
	//   Все 99 моб-скелеталок порта имеют anim_01 и anim_03 — фолбэк не нужен.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob") int32 ClipRun    = 3;
	// ★ ЗАМАХ = слот 5 + вариант удара (РЕВЕРС 2026-07-29 Combat_ApplyDamage@0xF7C470:
	//   actionSlot = node[145] + 5, node[145] = u8@+29 пакета 4015 kind=3; звук той же фазы 5+вариант).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob") int32 ClipAttackBase = 5;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob") int32 ClipDeath  = 4;
	// ★ ХИТ-ФЛИНЧ (эталон SetActionState case 0xD: SetField13(model+512) → слот 512/64 = 8):
	//   моб дёргается на КАЖДОМ полученном ударе — без этого «моб не реагирует, что его бьют».
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob") int32 ClipHit    = 8;
	// Доворот меша под базовую ориентацию модели моба (как MeshYawOffset игрока).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Mob") float MeshYawOffset = -90.f;   // компенсация зеркала (RH-фикс)
	// ★ 22.10.2026 (плавность, `World/AsdaBlendAnimInstance.h`): ЛОГИЧЕСКИЙ курс меша (UE-градусы, с доворотом `MeshYawOffset`) —
	//   цель доворота видимой модели (`asda.Anim.MobTurnDegPerSec`, [ОРИГИНАЛ] 360 °/с). До первого поворота — поворот меша.
	float GetFacingYawLogical() const;
	// ★ 22.10.2026: повернуть СРАЗУ (логический и видимый курс) по игровому направлению. Зовут только самотесты (спавн и
	//   прочие пути идут через `FaceGameDir`: в первые 0.25 с жизни актора он и так ставит курс сразу).
	void SnapFaceGameDir(const FVector2D& DirGame);

	// ── Сетевые реакции (сервер-авторитет) ──
	// Движение (4007 по renderId): интерполяция cur→dst со скоростью сервера + поворот + клип ходьбы. game XZ.
	void NetMove(const FVector2D& CurGame, const FVector2D& DstGame, float SpeedUnitsPerMs);
	// Стоп/снап (4009): позиция + idle.
	void NetStop(const FVector2D& CurGame);
	// ★ ТОЛЬКО ТРАЕКТОРИЯ, БЕЗ снапа позиции (эталон Recv_MobSpawnLoop@0xF30330 → projInitTrajectoryToTarget):
	//   пакет 4019 для МОБА (категория entity[116]==23) позицию НЕ задаёт — `if (entity[116] != 23)
	//   playerSetWorldPosition(...)`. Идём к dst из ТЕКУЩЕЙ точки. dst == текущая → просто встать (idle).
	//   AnimRate — темп клипа ходьбы из пакета 4015 @17 (эталон ent[122]); 4013/4019 его не несут ⇒ 1.0.
	void NetTrajectory(const FVector2D& DstGame, float SpeedUnitsPerMs, float AnimRate = 1.f);
	// ★★★ ЭТАЛОННАЯ СИНХРОНИЗАЦИЯ ПОЗИЦИИ МОБА («догоняющая скорость», Recv_CombatHit@0xF310E0 case 2).
	//   Оригинал мобу позицию из move-пакета НЕ ставит (гейт `targetEntity[116] != 23`), но и не даёт
	//   рассинхрону копиться: он ПЕРЕСЧИТЫВАЕТ СКОРОСТЬ так, чтобы моб пришёл в dst в тот же момент
	//   времени, что и на сервере. Дословно (network.c:37613-37637, дизасм 0xF3131C..0xF313AA):
	//       v263 = 0; v260 = 0;                                  // обе точки кладутся в плоскость y=0
	//       var_2C = 100.0 / (i16)pkt[15];                       // «скорость пакета», юнитов/мс
	//       mathVec3SubLen(cur, 0, cur.z, dst) → var_34;         // |dst − cur ПАКЕТА| — длина отрезка
	//       var_30 = var_34 / var_2C;                            // ВРЕМЯ отрезка по серверу, мс
	//       v19 = CClass_SetField177(target, …);                 // ТЕКУЩАЯ позиция сущности (нода +177,
	//                                                            //   playerSetWorldPosition кладёт туда y=0)
	//       mathVec3SubLen(v19, dst) → var_34;                   // |dst − НАША позиция|
	//       projInitTrajectoryToTarget(target, dst, var_34 / var_30);   // ← скорость, а не 100/spd!
	//   ⇒ v' = (100/spd) · |dst − pos| / |dst − curПакета|. Отстал — поедет быстрее и догонит РОВНО к
	//   концу отрезка; убежал вперёд — поедет медленнее. Каждый серверный отрезок заканчивается в
	//   серверной точке в серверное время, поэтому рассинхрон НЕ НАКАПЛИВАЕТСЯ и снапы не нужны.
	//   Порт до круга 7 передавал в траекторию сырую 100/spd — и любое отставание (хит-флинч, замах,
	//   потерянный пакет) оставалось навсегда: «моб идёт — расходится, моб стоит — совпадает».
	void NetTrajectoryTimed(const FVector2D& CurPktGame, const FVector2D& DstGame, float PktSpeedUnitsPerMs,
	                        float AnimRate = 1.f);

	// ══════════ ★★★ ПРЕСЛЕДОВАНИЕ И БОЕВОЕ ПЕРЕМЕЩЕНИЕ (4015 kind 5 / 7 / 0xE) ══════════
	// Эталонное состояние сущности (+0x1E0, поле 120), которым моб ЕДЕТ прямо сейчас. Ставят его
	//   НЕ через SetActionState, а сами функции траектории:
	//     projInitTrajectoryToTarget@0xF85690   → ent[120] = 2  (ходьба, kind=2)
	//     playerCastDirectionalEffect2@0xF865C0 → ent[120] = 5  (бег к цели, kind=5 и kind=0xE)
	//     playerCastDirectionalEffect@0xF86180  → ent[120] = 7  (боевое перемещение, kind=7)
	//   От состояния зависят КЛИП (2 или 3), ЗВУК ШАГА (фаза 2 или 12) и то, куда моб уйдёт
	//   ПО ПРИБЫТИИ: 2 → покой (1), 5 → боевая стойка (6), 7 → «в бою? 6 : 1».
	uint8 NetMoveState = 2;
	// ★★★ ПРЕСЛЕДОВАНИЕ/БОЕВОЕ ПЕРЕМЕЩЕНИЕ с эталонной нормировкой скорости по времени отрезка
	//   (та же формула, что у kind=2 — см. NetTrajectoryTimed). MoveState = 5 или 7.
	void NetChaseTimed(uint8 MoveState, const FVector2D& CurPktGame, const FVector2D& DstGame,
	                   float PktSpeedUnitsPerMs, float AnimRate);
	// То же с СЫРОЙ скоростью пакета — так эталон применяет ОТЛОЖЕННЫЙ пакет (все четыре места слива).
	void NetChaseRaw(uint8 MoveState, const FVector2D& DstGame, float SpeedUnitsPerMs, float AnimRate);

	// ★★★ ФЛАГ «В БОЮ» — порт эталонного `*((_WORD*)entity + 237)` (байтовый оффсет +0x1DA).
	//   Ставится в 1 состояниями 3/4/5/6/0x11 и вручную веткой kind=5 (перед рыком агра);
	//   сбрасывается в 0 состояниями 1, 2 и 10 (CCharacter_SetActionState@0xF876D0), а также
	//   апдейтом HP из 4017 в состояниях 1/2/7 (network.c:38467). Читают его: рык агра (один раз
	//   на агро) и прибытие в состоянии 7 (charUpdateMoveTick: «в бою → 6, иначе → 1»).
	bool IsCombatStance() const { return bCombatStance; }
	// ★ Рык агра — эталон case 5: `if (!ent[237]) { звук фазы 3; ent[237] = 1; }`. Ровно один раз
	//   за агро: пока моб не вернётся в покой/ходьбу, флаг держит звук закрытым.
	void PlayAggroRoarOnce();
	// ★ Запомнить серверную целевую клетку — порт `charSetTerrainCellIndex@0xF86C60`
	//   (`ent[282] = terrainCellIndexFromXZChecked(...)`). Зовут её kind=3 (на cur), kind=5/7/0xE
	//   (на dst). Читатель — `charCastDirectionalEffectIfFar@0xF869E0` (см. .cpp).
	void RememberServerCell(const FVector2D& CellGame);
	FVector2D GetServerCellGame() const { return ServerCellGame; }
	// ★★★ ДОВОДЧИК НА СЕРВЕРНУЮ КЛЕТКУ — порт `charCastDirectionalEffectIfFar@0xF869E0`.
	//   Эталон зовёт его по концу клипа замаха/флинча при ПУСТОМ буфере отложенного пакета
	//   (`charAction_tickWarp23`, entity_char.c:54883 и :54960; `effectObjectTickActionState`,
	//   effects_particles.c:66311) — ровно ПОСЛЕ `SetActionState(6)`. Разбор — в .cpp.
	void CastDirectionalEffectIfFar();
	// ★ Порт `ent[1749]` — userIdx цели преследования (`ent[1749] = pkt@0`), который ставят ветки
	//   4015 kind=5 / 7 / 0xE. Читает его ровно доводчик выше (CharManager_FindByUserIdx).
	void SetChaseTargetUserIdx(int32 UserIdx) { ChaseTargetUserIdx = UserIdx; }
	int32 GetChaseTargetUserIdx() const { return ChaseTargetUserIdx; }
	// ★ Порт `*((_WORD*)ent + 3492) = 1` — флаг ветки 4015 kind=0xE (FEAR). Его читает тик прибытия
	//   `charAnim_UpdateActionTick@0xF80A50`: при взведённом флаге он гасит его и оставляет ТЕКУЩУЮ
	//   позицию (`playerSetWorldPosition(CClass_SetField177(ent))`) вместо снапа на конец кривой
	//   (`ent[132..134]`). Состояние при этом всё равно становится 6 (боевая стойка).
	void MarkKeepPosOnArrive() { bKeepPosOnArrive = true; }
	// ★ Порт `Entity_GetActionState` — эталонный НОМЕР состояния сущности, собранный из полей порта.
	//   Нужен там, где эталон ветвится прямо по номеру (4017 «прочие флаги»: state ∈ {1,2,7}).
	int32 GetEtalonActionState() const;
	// ★ Прямая установка/сброс `*((_WORD*)ent + 237)` там, где эталон пишет поле сам, без SetActionState
	//   (4017 «прочие флаги»: `*((_WORD*)entity + 237) = 0`).
	void SetCombatStance(bool bOn) { bCombatStance = bOn; }

	// ═══ ★★★ 30.10.2026 НАВЫК МОБА (8011/8012) — хуки автомата `UAsdaMobSkillSubsystem` [ОРИГИНАЛ] ═══
	//   Эталон: состояния 17 (пре-каст, `charActionTick_State23@0xF7E870`), 16 (каст, `charState_UpdateCast@0xF7ECD0`),
	//   15 (атака навыком, `charExecuteAttackAction@0xF7F1F0`). Сам автомат живёт в подсистеме; актёр даёт ей клип,
	//   номер состояния и признак «моба перевели в другое состояние» (любой `SetActionState` эталона перебивает 15/16/17:
	//   движение/замах 4015, запись 4013, удар игрока — network.c:37557, effects_particles.c:6218).
	// Проиграть клип СЛОТА навыка (эталон 18/19/20/21 → файлы `_11.._14`, см. `PlayClip`). Возвращает длину клипа, с (0 — клипа нет).
	float PlaySkillClip(int32 Slot, bool bLoop);
	// ★ 30.10.2026 [ОРИГИНАЛ] Файл клипа для СЛОТА анимации моба (`Effect_BuildJbbSuffixByType@0xF717C0` + загрузчик
	//   `mobEffect_LoadArchiveStep@0xF81B90`): 0–5 → `_0.._5`, **6 → `_5`**, 7/8 → `_7/_8`, 14 → `_9`, 15 → `_10`, 18–21 → `_11.._14`;
	//   9–13, 16, 17 — пусто (−1). При `asda.Mob.ClipSlotMap 0` слоты 0–8 = файл (прежний порт), 14/15/18–21 — как оригинал.
	static int32 ClipFileForSlot(int32 Slot);
	// Счётчик смен клипа: растёт при КАЖДОМ `PlayClip` — по нему автомат видит, что моба перевели в другое состояние.
	int32 GetClipSerial() const { return ClipSerial; }
	// Номер состояния навыка (15/16/17; 0 — не в навыке). Сбрасывается любым клипом, кроме клипа навыка.
	void SetSkillActionState(int32 State) { SkillActionState = State; }
	// Вход в 15/16/17 (эталон `CCharacter_SetActionState`: прежнее состояние кончается — замах 3, флинч 13 и движение больше не
	//   тикают, их слива буфера нет). Труп не трогает (`if (ent[120] != 10)`); для 17 — «в бою» (case 0x11).
	void EnterSkillState(int32 State);
	int32 GetSkillActionState() const { return SkillActionState; }
	// Плавный разворот к игровой точке (эталон `charUpdateActionState23` в состояниях 3/15/16/17 — к цели ent[1749]).
	void FaceTowardGame(const FVector2D& TargetGame);
	// Конец навыка → боевая стойка (эталон `SetActionState(6)`).
	void EnterCombatIdleFromSkill() { EnterCombatIdle(); }

	// ★★★ Положить пакет в буфер отложенных (эталон entity[1753] + 37 байт в entity+1754).
	void BufferNetAction(uint8 Kind, const FVector2D& CurGame, const FVector2D& DstGame,
	                     float SpeedUnitsPerMs, float AnimRate);
	bool HasPendingNetAction() const { return Pending.bValid; }
	const FAsdaMobPendingAction& GetPendingNetAction() const { return Pending; }
	// ★ Замер рассинхрона: |текущая позиция − клетка cur из пакета| (юниты). Копит статистику для
	//   asda.Mobs.Dump и возвращает значение — доказательная метрика «совпали/не совпали с сервером».
	float NoteDesync(const FVector2D& CurPktGame);
	// Накопленная статистика рассинхрона (для дампа).
	float DesyncLastU = 0.f;
	float DesyncMaxU  = 0.f;
	double DesyncSumU = 0.0;
	int32  DesyncN    = 0;
	// ★ «СТОЯТЬ НА МЕСТЕ» — цель НЕ назначается и позиция НЕ трогается. Эталон Recv_MobSpawn@0xF305E0
	//   ветка состояния 2: при cur==dst вызывается только CCharacter_SetActionState(mob,1), никакой
	//   траектории. Сюда же уходит вырожденная скорость (пакет с spd=0 — 100.0/0 = +INF).
	void NetHalt();
	// ★ БЕЗУСЛОВНЫЙ снап на клетку пакета — порт `playerSetWorldPosition(mob, CMap_GetTileAt(cur))`
	//   из ОБЩЕГО хвоста Recv_MobSpawn@0xF305E0 (дизасм: CMap_GetTileAt @0xF3086A, вызов @0xF30899;
	//   хвост общий для веток «rid найден» (jmp @0xF30699) и «создан» (jmp @0xF3075C)). Порога там
	//   НЕТ — 4013 позиционирует моба авторитетно. Порог 150 есть только у 4015 kind=1.
	void NetSnapPos(const FVector2D& CurGame);
	// ★ Замах (эталон state 3 + Combat_ApplyDamage): моб ОСТАНАВЛИВАЕТСЯ, поворачивается к жертве,
	//   играет клип 5+Variant one-shot; на время клипа движение/стоп его не перебивают (гейт 13/14).
	//   Возвращает длительность клипа (сек; 0 если клип не найден) — для тайминга «кадра удара».
	//   AnimRate — темп клипа замаха из пакета (@19 × 1e-4, эталон ent[4826]; тик Combat_ApplyDamage
	//   считает `ent[113] += dt * ent[4826]`, т.е. клип идёт быстрее/медленнее ровно в это число раз).
	// ★ bRoarAndFace: рык замаха + разворот к жертве есть ТОЛЬКО в ветке 4015 kind=3 (звук строит сам
	//   обработчик пакета). Запись 4013 в состоянии 3 делает лишь `playerSetWorldPosition(cur)` +
	//   `CCharacter_SetActionState(mob, 3)`, а SetActionState (0xF876D0, case 3) не играет НИЧЕГО и
	//   никого не поворачивает — там этот флаг снимается.
	// ★ 30.10.2026 [ОРИГИНАЛ] AttackResult — сырой результат удара `@21` (ent[1743]): `Combat_ApplyDamage@0xF7C470` берёт слот
	//   `вариант + 5`, а при знаковом `ent[1743] < −1` (крит бит 31, коды −2…) — слот 7 (`cmp [+0x1b3c],−1; jge`); ключ `asda.Mob.ClipSlotMap`.
	float NetAttack(int32 Variant, const FVector2D& VictimGame, float AnimRate = 1.f, bool bRoarAndFace = true, int32 AttackResult = 0);
	bool IsAttackAnimActive() const { return GetWorld() && GetWorld()->GetTimeSeconds() < AttackAnimUntilS; }
	// ★ Хит-состояние (эталон state 13): пока моб «вздрагивает», он СТОИТ — move/stop-пакеты
	//   игнорируются (эталон Recv_CombatHit kind=1/2: `if (state==13||state==14) return`).
	bool IsHitAnimActive() const { return GetWorld() && GetWorld()->GetTimeSeconds() < HitAnimUntilS; }
	// ★★★ ЭТАЛОННЫЙ ГЕЙТ ДВИЖЕНИЯ — ТОЛЬКО состояния 13/14 (хит-флинч), Recv_CombatHit@0xF310E0:
	//   case 1: `if (GetActionState==13 || ==14) return;`
	//   case 2: `… if (GetActionState==13 || ==14) return;  SetActionState(target, 2); …траектория…`
	//   Состояние 3 (ЗАМАХ) в этих гейтах ОТСУТСТВУЕТ: пришедший во время замаха move-пакет
	//   переводит моба в state 2 и он идёт (SetActionState(2) прерывает клип замаха). Порт до круга 7
	//   блокировал движение на ВЕСЬ клип замаха (~1 с на каждый удар моба) — сервер в это время моба
	//   вёл, а клиентский стоял: чистое отставание, которое без «догоняющей скорости» уже не гасилось.
	bool IsMoveGated() const { return IsHitAnimActive(); }
	// Сохранено для не-двигательных мест (звук/флинч/смерть), где гейт замаха эталонный.
	bool IsActionLocked() const { return IsAttackAnimActive() || IsHitAnimActive(); }
	// ★ Получен удар: разворот К АТАКУЮЩЕМУ (агро-визуал) + хит-флинч (клип 8) + звук хита.
	//   Не перебивает замах/смерть (эталонные гейты). AttackerGame — позиция бьющего (game XZ).
	void NetHitFlinch(const FVector2D& AttackerGame = FVector2D::ZeroVector);
	// ★ 05.11.2026 [ОРИГИНАЛ] ОТЛЁТ — состояние 14 сущности (`charSkillCast_SetCastTargetDir@0xA296C0` / `skillSetupTargetPos@0xA29C00` →
	//   `charSetMoveTarget` + `CCharacter_SetActionState(14)`; тик `charUpdateActionState23@0xF844F0` case 13):
	//   Type 1 (behaviorType 0, сильный) — клип слота 14, позиция = старт + PushDir·450·min(t/600 мс, 1), на 600 мс снап в конец;
	//   Type 2 (behaviorType 1/2, слабый) — клип слота 15, без сдвига. Состояние держится до конца клипа
	//   (`effectObjectTickActionState`/`effectTimer_updateTick`): серверные ходы откладываются (+1753) и сливаются по концу — как флинч.
	//   Не отлетают: труп, ранг 3 (`+6979 == 3`), уже в отлёте (`state != 14`). PushDir — единичный, куда летит моб (от кастера).
	//   FaceDirGame — курс моба на время отлёта (эталон `+636 = −forward кастера`); нулевой — «к кастеру» (−PushDir).
	void NetKnockBack(int32 Type, const FVector2D& PushDirGame, const FVector2D& FaceDirGame = FVector2D::ZeroVector);
	bool IsKnockActive() const { return bKnockState && IsHitAnimActive(); }
	// ★★★ СМЕРТЬ И ПОЛНЫЙ ЖИЗНЕННЫЙ ЦИКЛ ТРУПА — 1:1 с эталоном (дизасм + декомпил, круг 6):
	//   1) HP<=0 (4015/4029) или 4017 flags==0 → CCharacter_SetActionState(mob, 10), HP=0;
	//      покадрово entityListUpdateFade@0xF78660 держит state 10 (`if (i[284] <= 0) SetActionState(i,10)`)
	//      и ставит флаг 0x20 (`sub_92C3AD(i, 32)`).
	//   2) Тик состояния 10 charAnimAdvanceActionTimer@0xF80930: играет death-клип; когда клип доигран
	//      (`clipLen <= t`) → `flags |= 4` и `+0x258 = 1500.0f`, дальше кадр замирает на последнем.
	//   3) ФЕЙД ТРУПА: charUpdateActionState23@0xF844F0 (флаг 4) и charUpdateRotateAndFx@0xF86CB0
	//      (флаги 0x20 и 4 — ДВА вычитания, 0xF86EED/0xF86F1F) вычитают dt из +0x258 ТРИЖДЫ за кадр,
	//      альфа = +0x258/1500 ⇒ видимое затухание ≈ 500 мс; по уходу в минус: альфа 0,
	//      `flags &= ~4; flags |= 0x10`.
	//   4) УДАЛЕНИЕ: entityListUpdateFade@0xF78660 `if (sub_9296AD(i,16)) charRemoveEntity(...)` —
	//      сущность уничтожается, renderId ОСВОБОЖДАЕТСЯ. Труп НЕ встаёт: респавн приходит СЕРВЕРОМ
	//      (4019 / 4013 / 4017 flags&4) примерно через 5 с и создаёт моба ЗАНОВО в СЕРВЕРНОЙ клетке.
	void NetDeath();
	// ★ Запуск фейд-аута нужного вида (см. EAsdaMobFade). По завершении актор уничтожается —
	//   как charRemoveEntity/charDespawnRemove в эталоне; rid освобождается (weak-ptr в NetMobs гаснет).
	void StartFadeOut(EAsdaMobFade Kind, float DurationS);
	bool IsFadingOut() const { return FadeKind != EAsdaMobFade::None; }
	EAsdaMobFade GetFadeKind() const { return FadeKind; }
	// Время смерти (WorldTime) и момент старта посмертного фейда — для asda.Mobs.Dump.
	double GetDeathAtS() const { return DeathAtS; }
	double GetCorpseFadeAtS() const { return CorpseFadeAtS; }
	// ★ Эталонная длительность фейдов (константы движка): 1500 мс — фейд-ин сущности (флаг 1,
	//   charUpdateActionState23 `f146/1500`); ~500 мс — посмертный (1500 мс на ТРИ вычитания за кадр);
	//   1000 мс — AoI (entityListUpdateFade `i[146]/1000.0`).
	static constexpr float FadeInSeconds     = 1.5f;
	static constexpr float CorpseFadeSeconds = 0.5f;
	static constexpr float AoiFadeSeconds    = 1.0f;
	// ★★★ ОЖИВЛЕНИЕ ТОЙ ЖЕ СУЩНОСТИ (реген). Эталон Recv_MobSpawn@0xF305E0 ветка «rid найден»:
	//   HP восстановлено, AoI-таймер взведён, идущий фейд отменён — НОВАЯ сущность НЕ создаётся,
	//   позиция здесь НЕ трогается (её ставит только 4013; 4019 мобу позицию не ставит вовсе).
	void NetRevive(int32 NewHp);
	// ★★★ КРУГ 9: поле PendingReviveHp УДАЛЕНО. Эталонный хендлер 4019 ActionState НЕ ТРОГАЕТ
	//   (дизасм 0xF30330..0xF304AE: только entity[283]/[284]=HP@+9, sub_9325BE(флаги@+27, буфы@+31),
	//   гейт `entity[116]!=23` на позицию, траектория при entity[237]==0 — ни одного call
	//   SetActionState@0x93C0D2), а entityListUpdateFade@0xF78660 умеет только ЗАГОНЯТЬ в state 10
	//   (`if (i[284] <= 0) SetActionState(i,10)`) — обратной ветки «HP>0 → выйти из 10» НЕ СУЩЕСТВУЕТ.
	//   Труп с возвращённым по 4019 HP доигрывает смерть до удаления. Оживляют ТОЛЬКО 4013 (SetActionState
	//   в хвосте) и 4017 flags&4 (SetActionState(entity,1)) — см. NetRevive.
	// ★ Отмена AoI-фейда у живого моба повторной 4013 (эталон `if (mob[121]&0x100) mob[121]=1` + фейд-ин).
	void CancelAoiFade();
	bool IsDead() const { return bDead; }
	// Для доказательного дампа asda.Mobs.Dump (идёт ли моб куда-то и куда — game XZ).
	bool IsMoving() const { return bMoving; }
	FVector2D GetMoveDstGame() const { return MoveDstGame; }
	// ★ 15.10.2026 (волна mobpos_swing, ревизия СРЕДН-2): точка, откуда начата текущая траектория (game XZ), — признак
	//   «призрака»: живая клетка сервера в начале траектории = сервер моба не повёл.
	FVector2D GetMoveStartGame() const { return MoveStartGame; }
	// ★ То же: идущий моб отстал/обогнал живую клетку сервера (4017/4027) — эталонная нормировка скорости по времени отрезка
	//   (как `NetTrajectoryTimed`): прийти в цель одновременно с сервером, без снапа и без остановки. [ПОРТ]
	void CatchUpToServerCell(const FVector2D& LiveGame);
	// ★ 07.11.2026 [ИСПРАВЛЕНИЕ] ОТРЕЗОК СЕРВЕРА (дефект оригинала «моб телепортируется к игроку»). Попадание снимает у актора траекторию
	//   (состояние 13/14), а NpcServer на флинч не останавливается и под станом лишь замирает, потом ведёт ТОТ ЖЕ отрезок: нового 4015 нет.
	//   Актор стоял, и его ставил рывком доводчик или снап ≥ 3 клеток. Теперь моб помнит отрезок и добегает. Разбор: Reports/mob_stun_teleport/.
	// Запомнить отрезок из пакета 4015 (виды 2/5/7) либо снять его (любой другой вид). Зовётся ДО гейтов состояния.
	void NoteServerMovePacket(uint8 Kind, const FVector2D& CurGame, const FVector2D& DstGame, float SpeedUnitsPerMs, float AnimRate);
	// Снять отрезок: сервер моба больше по нему не ведёт (пакет 4013, остановка).
	void ClearServerSegment() { bSegActive = false; }
	// Есть ли отрезок, по которому сервер ведёт моба: такой стоящий актор не «призрак», он добежит сам.
	bool HasServerSegment() const { return bSegActive; }
	// Поставить на клетку сервера: короткий видимый проход вместо мгновенного снапа (ключ asda.Mob.SnapAsGlide).
	void NetSnapOrGlide(const FVector2D& CurGame);
	// Идёт ли короткий проход (самотест).
	bool IsGliding() const { return bGlide; }
	// ★ «ПОМЕЧЕН ТРУПОМ» — порт флага 0x20 эталона (Recv_CastState@0xF41620: как только остаточный HP
	//   в ответе на замах <= 0, вызывается sub_92C3AD(target,32) → target[121] |= 0x20). Флаг ставится
	//   СРАЗУ по приходу пакета и означает «сервер уже засчитал смерть»: боевой цикл по такой цели
	//   НОВЫХ замахов не начинает (Player_UpdateAutoActionState@0xA249A0 проверяет `(v397[121] & 0x20) == 0`).
	//   Сама смерть при этом ещё НЕ наступила: HP цели уходит в 0 только на КАДРЕ УДАРА текущего замаха
	//   (урон применяет тик анимации атаки), и лишь тогда цель уходит в state 10, а атакующий — в покой.
	//   Разделение флага и смерти и есть лекарство от «мгновенный кил съедает анимацию» (юзер 2026-07-31).
	bool bServerDead = false;

	// ★ AoI-жизнь (эталон i[147]=10000мс, Recv_MobSpawn:547128): время последнего сетевого апдейта (4013);
	//   рефрешится WorldSubsystem-ом; истёк 10с + >1 сектора (world/3000) от игрока → фейд 1с → Destroy.
	double LastNetUpdateS = 0.0;

	virtual void Tick(float DeltaSeconds) override;

	// ★ Эталонные текстуры ПО ТИПУ моба (DT_MobTextures, строка MobId-1): оригинал берёт .TEX не из
	//   модели, а по MobId (см. Data/AsdaMobTextureRow.h — .MON/.MLK/.MBB, modelBuildMeshFromCache@0x11A45E0).
	//   Идемпотентно; зовётся из OnConstruction и BeginPlay, доступно из редактора/BP и скриптов запекания.
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Asda|Mob")
	void ApplyMobTextures();

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;   // имя в плашку (видно в редакторе)

private:
	// Rate — эталонный темп проигрывания клипа (ent[122] для движения, ent[4826] для замаха).
	// ClipIdx — СЛОТ анимации эталона (ClipIdle 0 … ClipHit 8, навык 18–21); файл — `ClipFileForSlot`.
	void PlayClip(int32 ClipIdx, bool bLoop, float Rate = 1.f);
	float TerrainZ(float UeX, float UeY) const;
	void FaceGameDir(const FVector2D& DirGame);
	// ★ 22.10.2026 (плавность): доворот видимого меша к логическому курсу (каждый кадр, на любом выходе из Tick).
	void TickTurnSmooth(float DeltaSeconds);
	float FacingYawLogical = 0.f;   // логический курс меша (UE-градусы, с доворотом)
	bool bFacingYawSet = false;     // логический курс задан хоть раз

	// ★ Джиттер точки назначения — эталон playerCastDirectionalEffect/2: перед построением кривой
	//   к dst добавляется ±30 юнитов по ОДНОМУ ИЗ 8 направлений (случайный индекс 0..7). Ровно это
	//   и разводит стаю по соседним клеткам вместо «все в одну точку». У kind=2
	//   (projInitTrajectoryToTarget) джиттера нет — там свой сдвиг rand()%10 по обеим осям.
	FVector2D ApplyCastJitter(const FVector2D& DstGame);
	// ★ Общее ядро kind=5/7/0xE: джиттер + клип бега + состояние 5/7 + звук шага фазы 12.
	void StartChase(uint8 MoveState, const FVector2D& DstGame, float SpeedUnitsPerMs, float AnimRate);
	// ★ Слив буфера отложенного пакета по концу клипа замаха/флинча (см. FAsdaMobPendingAction).
	void FlushPendingNetAction();
	// ★ Эталонные переходы состояний по прибытии/по концу клипа.
	void EnterIdle();          // SetActionState(1): клип 0, флаг «в бою» = 0
	void EnterCombatIdle();    // SetActionState(6): клип 1, флаг «в бою» = 1
	float CurrentClipLength() const;   // длина клипа в single-node инстансе (сек)
	// ★ Звук шага (фаза 2 ходьба / 12 бег) с однократной проверкой наличия wav. Звуковой движок
	//   оригинала на отсутствующий файл просто молча ничего не делает, а наш PlaySound3D пишет
	//   Warning — на каждом обороте клипа это был бы поток предупреждений (wav фазы 2 есть лишь у
	//   28 из 650 звук-баз, фазы 12 — у 13). Кэш даёт ТО ЖЕ поведение при чистом логе.
	void PlayStepSound(int32 Phase);
	int8 StepSndWalk = -1;   // -1 не проверяли, 0 нет файла, 1 есть
	int8 StepSndRun  = -1;

	FVector2D MoveDstGame = FVector2D::ZeroVector;
	float MoveSpeed = 0.35f;
	FVector2D MoveStartGame = FVector2D::ZeroVector;   // ★ mobpos_swing: начало текущей траектории
	float MoveSrvSpeed = 0.35f;                         // ★ mobpos_swing: скорость сервера на отрезке (100/spd пакета), юн/мс
	bool bMoving = false;
	bool bDead = false;
	// ── Порт эталонных полей сущности, участвующих в движении ──
	bool bCombatStance = false;        // *((_WORD*)ent + 237) — «в бою»
	FVector2D ServerCellGame = FVector2D::ZeroVector;   // ent[282] — последняя серверная целевая клетка
	bool bHasServerCell = false;
	int32 ChaseTargetUserIdx = -1;     // ent[1749] — userIdx цели преследования (пишут kind=5/7/0xE)
	bool bKeepPosOnArrive = false;     // *((_WORD*)ent + 3492) — «по прибытии позицию НЕ снапить» (kind=0xE)
	int32 LastCastDirIdx = -1;         // ent[1837] — предыдущий индекс направления джиттера
	float MoveAnimRate = 1.f;          // ent[122] — темп клипа движения (пакет @17 × 1e-4)
	float MoveClipTimeS = 0.f;         // ent[113] — время клипа движения (для звука шага по циклу)
	float MoveClipLenS = 0.f;          // длина текущего клипа движения (сек)
	bool bAnimGateWas = false;         // был ли моб в замахе/флинче на прошлом кадре (детектор конца клипа)
	TWeakObjectPtr<class UAsdaEffectsSubsystem> PerfFxSub;   // ★ R4, доля a: подсистема эффектов (чтобы не искать её каждый кадр)
	bool bStepSoundDue = false;        // «ent[113] == 0» — на этом кадре звук шага должен прозвучать
	FAsdaMobPendingAction Pending;     // ent[1753] / ent+1754
	// ── ★ 07.11.2026 [ИСПРАВЛЕНИЕ] отрезок сервера (см. NoteServerMovePacket) ──
	bool bSegActive = false;           // сервер ведёт моба по отрезку
	bool bSegKeep = false;             // NetTrajectory вызван изнутри — отрезок не снимать
	uint8 SegState = 5;                // вид движения отрезка: 2/5/7
	FVector2D SegDstGame = FVector2D::ZeroVector;   // конец отрезка
	float SegRemainS = 0.f;            // сколько серверу ещё идти, с (под станом не убывает: сервер стоит)
	float SegSrvSpeed = 0.35f;         // скорость сервера на отрезке, юн/мс
	float SegAnimRate = 1.f;           // темп клипа из пакета
	// Скорость добега до конца отрезка: прийти вместе с сервером, не быстрее kSegCatchUpMul × скорости сервера.
	float SegResumeSpeed(float DistUnits) const;
	// Возобновить бег по отрезку (конец флинча, отлёта или стана).
	void ResumeServerSegment();
	// ── ★ 07.11.2026 [ИСПРАВЛЕНИЕ] короткий проход вместо снапа (см. NetSnapOrGlide) ──
	bool bGlide = false;               // проход идёт
	FVector2D GlideStartG = FVector2D::ZeroVector;  // откуда
	FVector2D GlideDstG = FVector2D::ZeroVector;    // куда
	float GlideElapsedS = 0.f;         // прошло, с
	float GlideDurS = 0.f;             // длительность, с
	// ── Посмертие (см. NetDeath): когда умер, когда стартует фейд трупа, какой фейд идёт ──
	double DeathAtS = 0.0;        // WorldTime смерти (0 = жив)
	double CorpseFadeAtS = 0.0;   // WorldTime старта фейда = смерть + длина death-клипа (эталон: клип доигран)
	EAsdaMobFade FadeKind = EAsdaMobFade::None;
	double AttackAnimUntilS = 0.0;   // конец текущего замаха (WorldTime) — гейт NetMove/NetStop
	double HitAnimUntilS = 0.0;      // конец хит-флинча (state 13): моб стоит, движение игнорируется
	// ★ 05.11.2026 отлёт (состояние 14) — см. NetKnockBack. Удержание и слив буфера — общий `HitAnimUntilS`.
	bool bKnockState = false;        // текущее удержание — отлёт (14), а не флинч (13)
	bool bKnockMove = false;         // сильный отлёт: позиция ещё едет (первые 600 мс)
	FVector2D KnockStartG = FVector2D::ZeroVector;   // `ent+0x1D8+80` — позиция на старте отлёта
	FVector2D KnockDirG = FVector2D::ZeroVector;     // единичный вектор отлёта (от кастера)
	float KnockElapsedMs = 0.f;      // `ent+596` — прошло мс отлёта
	// ── ★ 30.10.2026 навык моба (см. PlaySkillClip) ──
	int32 ClipSerial = 0;            // число вызовов PlayClip (смена состояния)
	int32 SkillActionState = 0;      // ent[120] ∈ {15,16,17} пока идёт навык; 0 — нет
	bool bPlayingSkillClip = false;  // PlayClip вызван из PlaySkillClip (состояние навыка не сбрасывать)
};
