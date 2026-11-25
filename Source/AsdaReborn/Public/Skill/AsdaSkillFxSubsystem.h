// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaSkillFxSubsystem — ВИЗУАЛЬНЫЙ слой применения навыка: клип применения + эффекты + звук.
//
// ЧТО ЭТО ЗАКРЫВАЕТ. До сих пор клип и COMBO0 были неизвестны почти для всех навыков — «дыра №1»
//   отчёта `Reports/skill_use_system.md` §9 (таблица `CharSkillEffect_InitDispatchTable@0xA89510`
//   не была снята). Теперь она снята целиком (`Tools/asda_dump_skill_dispatch.py`) и лежит в
//   DataTable `/Game/AsdaAssets/Data/DT_SkillClips` (строка = `FAsdaSkillClipRow`).
//
// ГРАНИЦА ОТВЕТСТВЕННОСТИ. Подсистема НЕ трогает сеть, окно навыков, хотбар и полосу применения —
//   она только ПОКАЗЫВАЕТ. Точки стыка для сетевого/UI-слоя:
//     · `GetCastBarMaxMs(...)` — максимум полосы применения (COMBO0) для конкретного навыка/оружия/пола;
//       ровно это значение эталон кладёт в `слот+0x58` при загрузке клипа (§1.6-1.7 отчёта);
//     · `PlaySkillVisual(...)` — вызвать при СТАРТЕ применения (отправка заявки / приход 5257 code=1);
//     · `StopSkillVisual(...)` — при отказе/прерывании (эталон: `effectSlots[100].field_4 = −1`).
//
// ЭТАЛОННЫЕ ФАКТЫ, на которых стоит реализация:
//   · Клип каста — ОТДЕЛЬНЫЙ анимобъект; набор оружия `ch4_{m,f}_{SS}` на время навыка НЕ переписывается
//     (`skill_use_system.md` §7.2). Имя клипа зашито в загрузчик, а не выводится из надетого оружия;
//     ветвление — по полу и `equippedWeaponType`.
//   · Эффекты навыка загрузчик кладёт в поля слота (`slot+0x68`, `+0x168`, `+0x268`, `+0x368`),
//     а спавнит их ТИК:
//     `Effect_SpawnByName(mgr, имя, 1, x,y,z, slot, pos, boneIdx, /*attach*/7, 0, 1.0f)` по кости
//     **`8_a_End`** (кончик оружия) — `SkillEffect_UpdateScaledStrikeHit@0xA72610`, режим attach-10
//     (кость даёт ПОЗИЦИЮ; type-2 модель эффектов). ★ ИСКЛЮЧЕНИЕ: при `equippedWeaponType` 8..0xB
//     (лук/арбалет/баллиста) эталон берёт кость **`8_b1`** — `Char_UpdateSkillAnim_MultiPhase@0xA76E70`
//     (`entity_char.c:29620-29623`). Привязка .jeb → поле слота сохранена в колонке `JebSlots`
//     таблицы (порядок значащий, сортировке не подлежит).
//   · У 38 навыков клип/эффекты берутся не из тела загрузчика, а из `system\SkillInfo.inl` — колонка
//     `Source` в строке таблицы. Подробности раскладки — в шапке `AsdaSkillClipRow.h`.
//   · Скорость/масштаб времени клипа — `pChar->animSpeedScale` (`CObject_SetFloatDispatch(obj, dt·scale)`),
//     дефолт 1.0; отдельного «скила ускорения каста» в срезе клиента нет.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
// ★★★ СТЫК С УРОНОМ: подсистеме нужна РАСКЛАДКА записи 4102 (`FAsdaSkillHitRec`), потому что
//   именно она решает, В КАКОЙ МОМЕНТ эта запись становится видимой (порог фазы / прилёт снаряда).
//   Своей копии структуры не заводим — закон дедупа, единственное объявление (с R1 — в Net/AsdaNetWorldTypes.h; AsdaNetSubsystem.h его включает).
#include "Net/AsdaNetWorldTypes.h"   // ★ R1: FAsdaSkillHitRec / FAsdaSkillCastRec — типы без класса сети
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaHeldTables.h"                // FAsdaHeldTables — удержание DT_SkillClips на сессию (15.09.2026, dt_gc_hold)
#include "AsdaSkillFxSubsystem.generated.h"

struct FAsdaSkillClipRow;
class AAsdaPlayerCharacter;
class AAsdaEffectActor;
class AAsdaProjectileActor;
class UDataTable;

// ★ Одна запись программы показа: «этот `.jeb` — вот с такой привязкой и для такой цели».
//   Разбор строки таблицы вида `имя.jeb@привязка/видЦели` (см. шапку FAsdaSkillClipRow).
USTRUCT()
struct FAsdaSkillFxSpawn
{
	GENERATED_BODY()

	UPROPERTY() FString Jeb;        // базовое имя .jeb (без расширения)
	UPROPERTY() FName Anchor;       // КАК держится: bone / follow / char / world / matrix / attach / projectile
	// ★★★ ГДЕ РОЖДАЕТСЯ: caster / target / bone (кость оружия кастера) / hitpoint (луч × AABB цели).
	//   Снято МЕХАНИЧЕСКИ из аргументов позиции вызова спавна в эталоне, а не выведено из фазы.
	//   NAME_None — источник не определился: берём запасной (удар → цель, каст → кастер) И ПИШЕМ В ЛОГ.
	UPROPERTY() FName Origin;
	UPROPERTY() FName TargetKind;   // mob / char / NAME_None (ветка общая)
	// ★ 10.11.2026 (job4-vfx): суффикс «~rec» — эффект рождает ОТЛОЖЕННАЯ запись эталона (`packBuildEntryRecord`), а не порог фазы:
	//   спавн — при выпуске записи (`FAsdaSkillFxFlight::bRecord`), через задержку `FAsdaSkillHitRec::RecDelayMs`.
	UPROPERTY() bool bRecord = false;
};

// Активный показ навыка на одном персонаже.
USTRUCT()
struct FAsdaSkillFxActive
{
	GENERATED_BODY()

	// Кто применяет.
	UPROPERTY() TWeakObjectPtr<AAsdaPlayerCharacter> Caster;
	// Код навыка.
	UPROPERTY() int32 SkillId = 0;
	// Момент старта показа и момент окончания клипа (секунды мира).
	UPROPERTY() double StartTimeS = 0.0;
	UPROPERTY() double EndTimeS = 0.0;
	// Заспавненные эффект-акторы (гасим их вместе с показом).
	UPROPERTY() TArray<TWeakObjectPtr<AActor>> Fx;
	// Имя выбранного клипа и индекс ассета — для `asda.VerifySkillFx`.
	UPROPERTY() FString Clip;
	UPROPERTY() int32 AnimIdx = -1;
	// ★★★ МНОЖИТЕЛЬ ТЕМПА АНИМАЦИЙ НАВЫКОВ (эталонное `pChar->animSpeedScale`, `CCharacter+0x500`),
	//   ЗАФИКСИРОВАННЫЙ НА ЭТОТ ПОКАЗ. Снимается один раз на старте — ровно как эталон, который
	//   пересчитывает поле только при смене экипировки (`CCharacter_SetupModelFromItem@0x9E97A0`),
	//   а тик навыка читает уже готовое значение. Спрашивать его заново каждый кадр нельзя: смена
	//   вещи посреди каста сдвинула бы пороги фаз относительно уже проигранной части клипа.
	//   На этой скорости играет клип применения, по ней же пересчитан `EndTimeS` и переводится
	//   реальное время в клип-время при сравнении с порогами COMBO.
	UPROPERTY() float AnimScale = 1.f;

	// ── ★★★ ПОФАЗНАЯ ЧАСТЬ ──────────────────────────────────────────────────────────────────────
	// Пороги фаз в МИЛЛИСЕКУНДАХ от старта клипа (COMBO0/1/2 из таблицы).
	// ⚠ БЕЗ UPROPERTY: UHT запрещает массивы bool, а держать три поля рядом с разной разметкой —
	//   верный способ рассинхронизировать их при правке. Ссылок на UObject тут нет, GC не нужен.
	// ★ 08.11.2026 [ОРИГИНАЛ] (баг «Starlight Slasher: 4 удара разом», `asda.Skill.HexaFourHits`): фаз-ударов ЧЕТЫРЕ, не три —
	//   `CSkillEffect_Update@эталон` (навык 513) ведёт `field_3A0` = 0..3, пороги — четыре числа `combo time.bin` (+84[0..3]),
	//   которые `SkillEffect_SetupHexa` грузит циклом `colorIdx < 4`. Индекс 3 используют только тики с четвёртым ударом.
	float PhaseMs[4] = { 0.f, 0.f, 0.f, 0.f };
	// Есть ли у фазы что играть (COMBO задан И список эффектов/звук непуст).
	bool bPhaseUsed[4] = { false, false, false, false };
	// Фаза уже отработала (эталонный `effectSlots[112].field_394` — «сработало один раз»).
	bool bPhaseFired[4] = { false, false, false, false };
	// Эффекты удара по фазам.
	UPROPERTY() TArray<FAsdaSkillFxSpawn> Hit0;
	UPROPERTY() TArray<FAsdaSkillFxSpawn> Hit1;
	UPROPERTY() TArray<FAsdaSkillFxSpawn> Hit2;
	// ★ 08.11.2026: эффекты ЧЕТВЁРТОГО удара (фаза 3) — пока только у 513 (`eff_1_char_01_hexab03-a_00.jeb` у цели, литерал тика эталона).
	UPROPERTY() TArray<FAsdaSkillFxSpawn> Hit3;
	// ★ 08.11.2026: звук удара ПО ФАЗАМ (пусто — общий `SndHit`). У 513 эталон играет на фазах 0..2 `S_c_F3hexsla02`+`S_c_F3hexsla01`,
	//   на фазе 3 — `S_c_F3hexsla03`; имена через «;», каждое играется отдельно.
	FString PhaseSnd[4];
	// Звук удара (играет в каждой сработавшей фазе, как в эталоне).
	UPROPERTY() FString SndHit;
	// ★ ЦЕЛЬ ФИКСИРУЕТСЯ НА СТАРТЕ ПОКАЗА и больше не отслеживается — так же, как эталон
	//   запоминает `targetRenderId` в слоте, а снаряд считает направление ОДИН РАЗ (наведения нет).
	UPROPERTY() TWeakObjectPtr<AActor> Target;
	UPROPERTY() bool bTargetIsMob = false;
	// ★★★ ПОСЛЕДНЯЯ ИЗВЕСТНАЯ ПОЗИЦИЯ ЦЕЛИ (симптом «эффекты на себе», живой тест 2026-08-10).
	//   Мобов здесь убивают с одного удара: цель умирает и её актор УДАЛЯЕТСЯ раньше, чем наступает
	//   порог фазы / прилетает носитель. Прежний спавн падал в запасную ветку `Tgt ? … : CasterLoc` —
	//   эффект УДАРА рождался НА КАСТЕРЕ. Эталон так не делает: носитель летит В ЗАФИКСИРОВАННУЮ
	//   ПОЗИЦИЮ (`effect_spawnDirectedFromTo` — цель зафиксирована), а ближний тик ищет цель по
	//   renderId и при её отсутствии ветку удара просто НЕ ВЫПОЛНЯЕТ. Порт: позиция цели снимается на
	//   старте, освежается тиком, пока актор жив, и используется спавном, когда актора уже нет;
	//   если цели не было ВООБЩЕ — эффект с точкой рождения «у цели» не спавнится (и это в логе).
	FVector LastTargetLoc = FVector::ZeroVector;
	bool bHasTargetLoc = false;
	// Кость, с которой стартуют эффекты КАСТА («8_a_End»/«8_b1» по оружию либо кость из тела тика).
	UPROPERTY() FName FxBone;
	// ★★★ КОСТЬ СТАРТА СНАРЯДА — ОТДЕЛЬНАЯ ОТ КОСТИ КАСТА. Доказательство — тело тика навыка 707
	//   `CCharacter_UpdateSkillEffectTick@0xAC9DD0` (`entity_char.c`): первый спавн (эффект каста)
	//   ищет `"8_a1"`, а КАЖДАЯ фаза носителя перед `effect_spawnDirectedFromTo@0xA59B30` ищет
	//   `"8_b1"` (`entity_char.c:33027-33031`). Общее правило подтверждает
	//   `Char_UpdateSkillAnim_MultiPhase@0xA76E70` (`entity_char.c:29620-29623`): подтипы оружия
	//   8..0xB (лук/арбалет/баллиста) стартуют с «тетивы» `8_b1`, остальные — с «кончика» `8_a_End`.
	//   Порт держал ОДНО поле и подставлял в снаряд кость КАСТА из строки таблицы (`FxCastBone`),
	//   т.е. у 707 пускал носитель из `8_a1`.
	UPROPERTY() FName ProjBone;

	// ── ★★★ СТЫК «УРОН ↔ ВИЗУАЛ» ────────────────────────────────────────────────────────────────
	// Сессионный userIdx кастера — СВОЙ ЛИБО ЧУЖОЙ (эталон ставит показ любому персонажу, найденному
	//   по `userIdx` из пакета). Им сходятся записи урона (`FAsdaSkillHitRec::CasterIdx`) и этот показ.
	UPROPERTY() int32 CasterUserIdx = -1;
	// Имя тик-функции эталона (колонка TickFn) — печатается в логе стыка, чтобы по нему можно было
	//   найти в эталоне ту самую ветку, из которой взят момент удара.
	UPROPERTY() FString TickFn;
	// Тик эталона заводит НОСИТЕЛЬ СНАРЯДА (колонка `Proj` = `effect_spawnDirectedFromTo@0xA59B30`).
	//   ★ Это АВТОРИТЕТ признака «снаряд», а не наличие записи `@projectile` в программе показа:
	//   `analyse_tick` вытащил `@projectile` только у 2 строк из 1664, тогда как `Proj == 1` стоит
	//   у 90 — имя .jeb полёта тик берёт из подсекции `2·фаза+3` записи `SkillInfo.inl`, и для
	//   хардкод-загрузчиков механический разбор до него не добирается (честно пишем в лог).
	UPROPERTY() bool bRowProj = false;
	// ★ Тик заводит БЕСТЕЛЕСНЫЙ носитель (`effectInitCollisionTrace@0xA596F0`, колонка `ProjDelay`):
	//   визуала полёта нет ПО УСТРОЙСТВУ (в носитель не грузится ни `.jeb`, ни имя удара, ни звук) —
	//   эффекты играет сам тик В МОМЕНТ ЗАПУСКА, а по трассе едет ТОЛЬКО УРОН (задержка `dist/2.5`).
	UPROPERTY() bool bRowProjDelay = false;
	// Есть ли у фазы СВОЙ эффект удара (непустой FxHit<N> без записи снаряда). Этим гейтится «MISS»:
	//   эталон рисует его только там, где тик реально ВЕДЁТ УДАР (см. FirePhase).
	bool bPhaseHasHitFx[4] = { false, false, false, false };
	// ★ Записи 4102, пойманные ДО момента показа. БЕЗ UPROPERTY: это POD сетевой записи, ссылок на
	//   UObject внутри нет. Выпускаются в `FirePhase` (ближний бой), носителем снаряда (по прилёте)
	//   либо при снятии показа (чтобы ни одна запись не потерялась — no-simplifications-stubs).
	TArray<FAsdaSkillHitRec> PendingHits;
	// ★★★ 29.10.2026 РЫВОК Charge 537 / Mad Charge 597 — тик `Char_UpdateDash@0xB3B010` (разбор — у `TickDash`).
	bool bDash = false;                              // показ ведёт рывок (ключ asda.Skill.Dash)
	int32 DashPhase = 0;                             // фаза тика `pChar+47781`: 0 — едем, 1 — прибытие, 2 — исход, 3 — конец
	bool bDashMob = true;                            // ветка цели: моб (`+208386 & 1`) / игрок (`& 8`)
	int32 DashTargetIdx = -1;                        // renderId моба / userIdx игрока (из записи пакета)
	FVector2D DashStartG = FVector2D::ZeroVector;    // старт (game XZ, `+47635..47637`)
	FVector2D DashEndG = FVector2D::ZeroVector;      // конец (game XZ, `+47638..47640`)
	float DashMs = 700.f;                            // длительность = COMBO0 (`pChar+368·idx+6355`), клип-мс
	uint8 DashFlags = 0;                             // байт [7] записи цели (`+208388`): 0x02 — состояние выпало, 0x20 — IMMUNE
	bool bDashFlagsKnown = false;                    // запись цели найдена в показе
};

// ★★★ НОСИТЕЛЬ В ПОЛЁТЕ: «урон едет В СНАРЯДЕ» ([[weapon-projectiles]]).
//   Эталон кладёт урон в поле носителя `+1784`, а показывает его `CEffect_UpdateSkillCast@0xA5A060`
//   → `Combat_ShowDamageFeedback@0xA80390` / `CharFx_ShowSkillDamageText@0xA7FC00` — ПО ПРИЛЁТЕ,
//   после спавна эффекта удара (`+8`) и звука удара (`+264`). Порт возит в носителе ту же запись
//   4102. Живёт ОТДЕЛЬНО от показа: клип может доиграть раньше, чем снаряд долетит.
struct FAsdaSkillFxFlight
{
	TWeakObjectPtr<AAsdaProjectileActor> Proj;   // сам носитель (умер без прилёта ⇒ выпускаем аварийно)
	TWeakObjectPtr<AActor> Target;               // цель, зафиксированная на выстреле
	TWeakObjectPtr<AAsdaPlayerCharacter> Caster; // кастер (для звука/эффекта удара)
	int32 SkillId = 0;
	int32 CasterUserIdx = -1;
	int32 Phase = 0;
	bool bHasHitFx = false;                      // есть ли у фазы эффект удара (гейт «MISS»)
	bool bArrived = false;
	// ★ БЕСТЕЛЕСНЫЙ носитель (`effectInitCollisionTrace`): актора-снаряда нет, эффекты уже отыграл
	//   сам тик — по трассе едет только УРОН. Момент прилёта считаем по времени мира.
	bool bDelayOnly = false;
	double ArriveAtS = 0.0;
	// ★ ЗАФИКСИРОВАННАЯ ТОЧКА ПРИЛЁТА. Эталонный носитель летит В ПОЗИЦИЮ, снятую на выстреле
	//   (`effect_spawnDirectedFromTo@0xA59B30` — цель зафиксирована, наведения нет), и эффект удара
	//   по прилёте рождается ИМЕННО ТАМ — даже если сущность-цель уже умерла и удалена. Прежний порт
	//   в этом случае падал в `CasterLoc` — «эффект удара на себе».
	FVector DestLoc = FVector::ZeroVector;
	// ★ Своя копия эффекта и звука УДАРА: носитель обязан отыграть их даже если клип уже доиграл
	//   и показа в `Active` больше нет (эталон держит их прямо в носителе — поля `+8` и `+264`).
	TArray<FAsdaSkillFxSpawn> HitFx;
	FString SndHit;
	bool bTargetIsMob = false;                   // ветка позиции звука удара (кастер / цель)
	TArray<FAsdaSkillHitRec> Hits;
	// ★ 04.11.2026 [ОРИГИНАЛ]: носитель тика `.inl` (`Char_UpdateSkillAnim_MultiPhase` → `effect_spawnDirectedFromTo`) — его прилёт
	//   разбирает `CEffect_UpdateSkillCast@0xA5A060`, у которого ветка цели-ПЕРСОНАЖА спавнит удар `CEffectMgr_CreateEffect_F561F0(…, 1.0)`
	//   без scaleAll (×1, а не ×2 площадной ветки). Ставит `LaunchPhaseProjectile`.
	bool bInlCarrier = false;
	// ★ 10.11.2026 (job4-vfx): ОТЛОЖЕННАЯ ЗАПИСЬ эталона (`packBuildEntryRecord@0xA5D390` → узел `skillProjectileList`; выпуск —
	//   `animSeq_TickAndFireEvents@0xA5D5E0`, когда накопленное время тика персонажа ≥ задержки узла, мс). При выпуске: эффекты
	//   `HitFx` с флагом `bRecord`, звуки «~rec» строки `SndHit`, звук удара моба (фаза 11) у кастера по каждой цели-мобу, затем урон.
	bool bRecord = false;
};

// ★ 01.11.2026 [ИСПРАВЛЕНИЕ] (soulmate-glow): собственный множитель яркости эффекта показа навыка Дружбы — значение ключа
//   `asda.Soul.SkillGlowScale` для эффекта с тегом показа соулмейта (ставит `SpawnFx`), 1 для любого другого актора.
//   Нужен тем, кто выставляет `AAsdaEffectActor::FxAlphaScale` абсолютным значением (GM-«скрыться»), чтобы не терять его.
ASDAREBORN_API float AsdaSoulSkillGlowFactor(const AActor* Fx);

UCLASS()
class ASDAREBORN_API UAsdaSkillFxSubsystem : public UGameInstanceSubsystem,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Полёты снарядов и отложенные удары держат слабые ссылки на акторов МИРА.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

#if !UE_BUILD_SHIPPING
	// ★ 17.10.2026 (синхронизация боя, Ф2, ревизия Ф1 п.4) — наблюдение самотеста: момент срабатывания порога фазы показа навыка
	//   (фактический и по данным: старт клипа + COMBO[фаза] / скорость анимации). Пишется только при `bRecordPhases`.
	struct FAsdaPhaseFiredSeen
	{
		int32 CasterUserIdx = -1;   // кастер
		int32 SkillId = 0;          // навык
		int32 Phase = 0;            // фаза 0..2
		double AtS = 0.0;           // мир-время срабатывания (кадр тика)
		double PlannedS = 0.0;      // мир-время порога по данным (StartTimeS + COMBO / 1000 / AnimScale)
	};
	bool bRecordPhases = false;                    // самотест включает запись `PhasesFiredSeen`
	TArray<FAsdaPhaseFiredSeen> PhasesFiredSeen;   // сработавшие пороги фаз (только при записи)
#endif

	// ── ТОЧКИ СТЫКА ДЛЯ СЕТЕВОГО/UI-СЛОЯ ────────────────────────────────────────────────────────────
	// ★★★ ДВА РАЗНЫХ НУЛЯ. `GetCastBarMaxMs` возвращает 0 и когда строки нет, и когда строка есть,
	//   а `Combo0Ms == 0` (174 строки из 1664: 69 клипов без записи в `combo time.bin` + 105 с
	//   hit[0] == 0). Это РАЗНЫЕ случаи: во втором ноль — данные оригинала, и эталон честно
	//   отрабатывает его подменой `max = 1.0`. Кому важно различать — звать `TryGetCastBarMaxMs`.
	//   ⚠ ЕДИНИЦА `max = 1.0` — ОДНА МИЛЛИСЕКУНДА, а не «кадр» и не «полоса целиком»: значения
	//     `combo time.bin` переводятся из кадров в мс ЕЩЁ ПРИ ЗАГРУЗКЕ
	//     (`CComboManager_LoadComboTime@0x9BC5F0`, `item_inventory.c:428-435`, ×33.333328), а COMBO0
	//     из `SkillInfo.inl` изначально в мс. Наблюдаемый итог — полоса заполняется мгновенно.
	// Есть ли у навыка величина максимума. false — строки нет (величина НЕИЗВЕСТНА, OutMaxMs = 0);
	//   true при OutMaxMs == 0 — «ноль по данным оригинала».
	UFUNCTION(BlueprintCallable, Category = "Asda|SkillFx")
	bool TryGetCastBarMaxMs(int32 SkillId, int32 Sex, int32 WeaponSubType, float& OutMaxMs) const;

	// Максимум полосы применения (COMBO0, мс) для навыка при данном поле и подтипе оружия.
	//   0 — либо строки нет, либо COMBO0 нулевой; различать — `TryGetCastBarMaxMs`.
	UFUNCTION(BlueprintCallable, Category = "Asda|SkillFx")
	float GetCastBarMaxMs(int32 SkillId, int32 Sex, int32 WeaponSubType) const;

	// Показать применение навыка: клип + эффекты + звук. false — строки/ассета нет (пишет предупреждение).
	// ★★★ `CasterUserIdx` — СЕССИОННЫЙ ИНДЕКС КАСТЕРА, и он может быть ЧУЖИМ. Эталонный
	//   `Recv_SkillCast@0xF4E370` (network.c:44835) берёт кастера ИЗ ПАКЕТА
	//   (`CharManager_FindByUserIdx(g_CharManager, *(WORD*)(header+1))`) и ставит показ ЛЮБОМУ
	//   найденному персонажу: признак «локальный игрок» (`CClass_GetField47565`) закрывает там
	//   ровно две вещи — сброс иконки в окне навыков и текст отказа. −1 = «не знаю» ⇒ индекс
	//   восстанавливается обратным поиском по актору (`FindUserIdxByActor`), как раньше.
	UFUNCTION(BlueprintCallable, Category = "Asda|SkillFx")
	bool PlaySkillVisual(AAsdaPlayerCharacter* Caster, int32 SkillId, int32 CasterUserIdx = -1);

	// Погасить показ (отказ сервера / прерывание / смерть). Возвращает состояние персонажа в покой/бег.
	UFUNCTION(BlueprintCallable, Category = "Asda|SkillFx")
	void StopSkillVisual(AAsdaPlayerCharacter* Caster);

	// Идёт ли сейчас показ навыка у персонажа (гейт для смены состояния анимации).
	UFUNCTION(BlueprintPure, Category = "Asda|SkillFx")
	bool IsSkillVisualActive(const AAsdaPlayerCharacter* Caster) const;
	// ★ 29.10.2026 (Charge): идёт ли у кастера рывок и его фаза/концы (журнал, самотест `asda.Skill.SelfTestCharge`).
	bool GetDashState(const AAsdaPlayerCharacter* Caster, int32& OutPhase, FVector2D& OutStartG, FVector2D& OutEndG) const;

	// ══ ★★★ СТЫК «УРОН ↔ ВИЗУАЛ» ══════════════════════════════════════════════════════════════
	// ЗАЧЕМ. Урон навыка приезжает пакетом 4102 в СВОЁ время (сервер считает попадание сразу), а
	//   эталон показывает и урон, и вспышку в ОДИН И ТОТ ЖЕ момент: на пороге фазы `COMBO[фаза] <=
	//   прогресс клипа` (ближний бой) либо по ПРИЛЁТУ носителя (снаряд). Пока порт применял 4102
	//   «по приходу пакета», цифра и вспышка расходились во времени.
	//   Эталонные точки: `CSkillCast_ApplyEntityHitEffect@0xA7B4A0` и `CSkillCast_UpdateMeleeHit@
	//   0xA64BB0` (ближний бой — эффект, звук, `CClass_SubtractStatClamped`, текст урона ОДНИМ
	//   блоком), `CEffect_UpdateSkillCast@0xA5A060` (снаряд — эффект `+8`, звук `+264`, и ТОЛЬКО
	//   ПОТОМ `Combat_ShowDamageFeedback`).
	//
	// Принять запись 4102 на хранение до момента показа.
	//   true  — запись припаркована (мир-подсистема НЕ применяет её сейчас; она придёт делегатом
	//           `OnSkillHitMoment` ровно в момент удара);
	//   false — подходящего показа/носителя нет (чужой каст, бафф без клипа, все фазы уже
	//           отработали) ⇒ применять НЕМЕДЛЕННО, как эталонный обработчик 4102 и делает.
	bool DeferSkillHit(const FAsdaSkillHitRec& R);
	// ★ R5-fix8 (14.11.2026): запись 4102 кастера, чей успешный 5257/5270 принят этим же проходом сети и ещё ждёт разбора (показ
	//   навыка стартует следующим тиком), — на полку `PendingCastHits`: она дождётся показа и ляжет на его фазу. true — принята
	//   на полку (мир её сейчас не применяет). Ключ `asda.Skill.Hit4102WaitsQueuedCast`, разбор — у определения.
	bool ShelveHitForQueuedCast(const FAsdaSkillHitRec& R);

	// ★★★ ЕСТЬ ЛИ ОТЛОЖЕННАЯ СМЕРТЕЛЬНАЯ ЗАПИСЬ по этой цели — то есть уже принятая от сервера
	//   запись каста, у которой `HpAfter <= 0` (и != −1 «сервер HP не сообщил»), и которая ждёт
	//   кадра удара. Ищем во ВСЕХ трёх местах хранения: полка `PendingCastHits` (пакет пришёл
	//   раньше показа), `Active[].PendingHits` (показ идёт, фаза ещё не выстрелила) и
	//   `Flights[].Hits` (урон едет в носителе). Потребитель — мир-подсистема
	//   (`UAsdaWorldSubsystem::IsMobDeathHeldByPendingHit`).
	//   ⚠ ЭТОТ ПОТРЕБИТЕЛЬ ПО УМОЛЧАНИЮ ВЫКЛЮЧЕН (отчёт §28): полка наполняется у ЛЮБОГО кастера, а
	//   разбирается кадром удара только у локального (показ навыка запускается лишь для своего
	//   персонажа) ⇒ гейт держал смерть у кастера до 1767 мс, а у наблюдателя — 0.35 с, и клиенты
	//   расходились. Предикат оставлен рабочим для режима `asda.Skill.HoldMobDeath 1` и для
	//   диагностики `asda.VerifySkillHitJoin`.
	bool HasPendingLethalHit(int32 TargetKind, int32 TargetIdx) const;

	// Момент показа наступил: применяй урон/HP/цифру. `bShowMissText` — рисовать ли «MISS»
	//   (эталон рисует его при НУЛЕВОМ уроне и только там, где тик реально ведёт удар).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaSkillHitMoment,
		const FAsdaSkillHitRec& /*rec*/, bool /*bShowMissText*/);
	FOnAsdaSkillHitMoment OnSkillHitMoment;

	// Состояние стыка для консоли `asda.VerifySkillHitJoin` (что припарковано и чего ждёт).
	void DumpHitJoin() const;

	// ★ Состояние ПОКАЗА для консоли `asda.VerifySkillFx`: какие эффект-акторы живы, как они держатся,
	//   включена ли эталонная геометрия квадов/колец и где они относительно кости оружия.
	//   ⚠ Эффекты каста НЕ являются дочерними акторами персонажа (`ConfigureBoneAttach` намеренно НЕ
	//   парентит — парентинг ломает FTransform у зеркального скелета), поэтому `GetAttachedActors`
	//   их не видит и старая проверка печатала «эффект-акторов на персонаже 0» при живом показе.
	void DumpActiveFx() const;

	// ── РАЗБОР ТАБЛИЦЫ ──────────────────────────────────────────────────────────────────────────────
	// Выбор строки: пол → РАЗМЕРНЫЙ КЛАСС ЦЕЛИ (сущность +7356, если у навыка есть такие ветки) →
	//   подтип оружия (точное совпадение, затем ветка default) → первая подходящая.
	//   OutWhy (опц.) — текстовое объяснение выбора для консольной проверки.
	const FAsdaSkillClipRow* ResolveRow(int32 SkillId, int32 Sex, int32 WeaponSubType,
		FString* OutWhy = nullptr, int32 TargetSizeClass = 0) const;

	// ★★★ Размерный класс ЦЕЛИ (0/1/2) — то же, что эталон читает из `сущность + 7356`.
	//   ИСТОЧНИК НАЙДЕН И ПОДКЛЮЧЁН 2026-08-11 (прежняя запись «писателя в срезе нет» ОПРОВЕРГНУТА
	//   сканом `.text` по disp32):
	//     · `gameObjCtorInit@0xF810B0+0x280` (VA `0xF81330`) — `mov dword [obj+0x1CBC], 0`
	//       ⇒ у всего, что не моб (персонажи, NPC), класс 0;
	//     · `mobEffect_LoadArchiveStep@0xF81B90+0xEEF` (VA `0xF82A7F`) —
	//       `mov ecx,[entity+0x1D4]; mov ecx, g_MobList(0x0349FAE8); call →jmp 0xF82F10;
	//        mov [entity+0x1CBC], eax`, где `sub_F82F10` = `MobList.rec[272·typeIdx + 0x90]`.
	//   ⇒ значение приходит из `Mob list.BIN +0x90` по TYPE-ID моба (в порту — `DT_MobStats.SizeClass`).
	//   Переопределяется консолью `asda.Skill.TargetSize <0|1|2>` для живой проверки всех трёх веток.
	UFUNCTION(BlueprintCallable, Category = "Asda|SkillFx")
	int32 GetTargetSizeClass(const AActor* Target) const;

	// ★★★ ПРОЦЕНТНЫЙ РАЗМЕР ЦЕЛИ (`сущность + 7360`, геттер `get_o730@0x99F150` = `sub_936A29`).
	//   Множитель эффекта удара: `CEffectMgr_scaleAll(fx+12, sub_936A29(цель) / 100.0)`
	//   (`SkillEffect_UpdateScaledStrikeHit@0xA72610`, `effects_particles.c:10167`).
	//   Писатели те же две точки: конструктор кладёт **100** (VA `0xF8133D`), загрузчик моба —
	//   `MobList.rec[272·typeIdx + 0x108]` (VA `0xF82A9C` → `sub_F82F50`).
	//   Возвращает ПРОЦЕНТ (100 = «как есть»), не множитель.
	UFUNCTION(BlueprintCallable, Category = "Asda|SkillFx")
	static int32 GetTargetSizePercent(const AActor* Target);

	// ★ УДЕРЖАНИЕ `DT_SkillClips` (15.09.2026, отчёт dt_gc_hold). `Table` ниже — только слабая ссылка: в -game GC
	//   выгружал таблицу, и следующий навык грузил её заново и перестраивал индекс `BySkill`.
	//   Сильная ссылка ставится лениво в `EnsureTable` (первый навык). Возврат — 0/1. Публичный — для стенда.
	int32 HoldGameTables() const;
	const FAsdaHeldTables& GetHeldTables() const { return HeldTables; }

	// ★★★ ПОЛНЫЙ ЗАКОН МАСШТАБА ЭФФЕКТА УДАРА (реверс 2026-08-14, `Reports/skill_fx_scale.md` §12).
	//   Эталон выбирает множитель ПО ВИДУ ЦЕЛИ, и это ЗАМЕНА, а не произведение: каждый заспавненный
	//   эффект получает РОВНО ОДИН `CEffectMgr_scaleAll`.
	//     цель — сущность/МОБ  → `%размера моба / 100` (`sub_936A29(цель)/100`, 17 сайтов);
	//     цель — ПЕРСОНАЖ      → КОНСТАНТА: **2.0** у эффектов НАВЫКА
	//                            (`effectSpawnSkillProjectiles@0xA77C20`, `effects_particles.c:11389`;
	//                             `CSkillCast_ApplyAoEHitEffects@0xA75EF0`, `skill_buff.c:17066`),
	//                            **1.5** у эффектов ОБЫЧНОЙ АТАКИ (10 сайтов, см. AsdaWorldSubsystem).
	//   ⚠ Известное исключение эталона: у `SkillEffect_UpdateScaledStrikeHit@0xA72610` ветка
	//     цели-ПЕРСОНАЖА `scaleAll` не зовёт вовсе (доказано: в теле 0xA72610 единственный вызов —
	//     строка 10169, ветка МОБА). Порт идёт по записям ответа 5257/5270, то есть по пути
	//     `netApplySkillHitResultPacket@0xF4F8A0 → effectSpawnSkillProjectiles`, где для персонажа
	//     стоит именно 2.0 — поэтому берём 2.0 (обоснование и риск — в отчёте).
	static constexpr float kCharHitFxScaleSkill = 2.0f;
	// ★ Обычная атака ПО ПЕРСОНАЖУ — 1.5. Ровно 10 сайтов эталона, все три функции обычного боя:
	//   `CCharacter_AttackAnimTick@0x9A2200` (`entity_char.c:2213` обычный удар оружия,
	//   `:2283`/`:2294` крит `cattk01-a_00` / `_1`),
	//   `Combat_ShowDamageResultText@0xA56930` (`combat_pvp.c:3228`/`:3235` обычный `battk03-a_00`/`_1`,
	//   `:3395`/`:3406` крит `cattk01-a_00`/`_1`),
	//   `CCharacter_ResolveBasicAttack@0xA47DC0` (`entity_char.c:27055` обычный, `:27167`/`:27178` крит).
	//   Выбор `-a_00` vs `-a_00_1` — по `attacker == Target_GetSelectedUserIdx(локальный игрок)`,
	//   на МАСШТАБ не влияет (1.5 в обеих ветках). Удар МОБА по игроку (`Combat_ApplyDamage@0xF7C470`,
	//   `eff_0_mon_99_battk01-a_00.jeb`) `scaleAll` НЕ получает — там 1.0.
	static constexpr float kCharHitFxScaleBasic = 1.5f;
	// Итоговый множитель эффекта удара НАВЫКА по данной цели (без отладочного `asda.Skill.FxScale`).
	UFUNCTION(BlueprintCallable, Category = "Asda|SkillFx")
	static float GetSkillHitFxScale(const AActor* Target);
	// Итоговый множитель импакта ОБЫЧНОЙ АТАКИ по данной цели. Живёт рядом с предыдущим сознательно:
	//   закон один, отличается только константа для целей-персонажей.
	UFUNCTION(BlueprintCallable, Category = "Asda|SkillFx")
	static float GetBasicAttackHitFxScale(const AActor* Target);

	// Ручное переопределение размерного класса (−1 = не задано, работает штатный источник).
	UPROPERTY() int32 TargetSizeOverride = -1;
	// ★ 04.11.2026: идёт спавн удара по прилёту носителя `.inl` (FAsdaSkillFxFlight::bInlCarrier) — у цели-персонажа масштаб ×1.
	bool bSpawnAtInlArrival = false;
	// ★ 10.11.2026 (job4-vfx): идёт выпуск отложенной записи — рождаются только эффекты/звуки с «~rec» (вне выпуска — только без него).
	bool bSpawningRecord = false;
	// ★ 10.11.2026 (job4-vfx): моменты выпуска отложенных записей (время мира) — только для самотеста `asda.Skill.SelfTestAnimAudit`
	//   (сверка числа записей и их задержки от порога фазы); тот очищает массив перед кастом. Растёт по записи на выпуск.
	TArray<double> DebugRecordFireTimes;
	TArray<double> DebugRecordArmTimes;    // ★ 10.11.2026: моменты ВЗВОДА тех же записей (порог фазы), по порядку задержки — пара к DebugRecordFireTimes
	// ★ 10.11.2026 (job4-vfx): ОТЛОЖЕННЫЕ ЗАПИСИ навыка — (задержка мс, доля урона) на каждую запись `packBuildEntryRecord`
	//   эталона с задержкой > кадра; false — у навыка таких записей нет (урон выпускается на пороге фазы). Тело и доказательства — в .cpp.
	static bool SkillRecordWaves(int32 SkillId, int32 Damage, TArray<TPair<float, int32>>& OutWaves);
	// Выпуск отложенной записи (рамка `FAsdaSkillFxFlight::bRecord`): эффекты и звуки записи, звук удара моба, урон.
	void FireRecordFlight(FAsdaSkillFxFlight& F);
	// Все строки навыка (для `asda.Skill.ClipInfo`).
	void CollectRows(int32 SkillId, TArray<const FAsdaSkillClipRow*>& Out) const;

	// ★ Разбор ячейки программы показа («имя.jeb@привязка/видЦели;…») — публично, чтобы консольная
	//   проверка печатала ровно то, что исполнит рантайм.
	static void ParseFxList(const FString& Cell, TArray<FAsdaSkillFxSpawn>& Out);

private:
	// Ленивая загрузка DT_SkillClips (weak-ptr: сырой static не GC-рут — та же грабля, что роняла char-select).
	void EnsureTable() const;
	// Тик показа: пороги фаз COMBO0/1/2 и снятие завершившихся клипов.
	bool TickActive(float Dt);
	// ★ 29.10.2026 (Charge): завести рывок на старте показа (цель, концы отрезка, эффект/звук фазы 0) и вести его по прогрессу
	//   клипа. `TickDash` возвращает true, когда рывок окончен (фаза 3) и показ надо закрыть.
	void BeginDash(FAsdaSkillFxActive& A, const class AAsdaPlayerCharacter* Caster, bool bOurs);
	bool TickDash(FAsdaSkillFxActive& A, float ProgressMs);
	// ★★★ ВЫСТРЕЛ ФАЗЫ: эффект удара + звук удара (либо снаряд, который довезёт их до цели).
	//   Эталон: `CSkillCast_ApplyEntityHitEffect@0xA7B4A0` (цель-сущность) и
	//   `CSkillCast_ApplySingleTargetHit@0xA7AF20` (цель-игрок) — оба вызываются из тика ровно тогда,
	//   когда `COMBO[фаза] <= прогресс клипа`, и оба сначала ставят флаг «сработало».
	void FirePhase(FAsdaSkillFxActive& A, int32 Phase);
	// Спавн ОДНОГО эффекта программы показа с учётом привязки. Возвращает актор (или nullptr).
	// ★★★ `OverrideTarget` — ПЛОЩАДНАЯ ЦЕЛЬ. Эталон `CSkillCast_ApplyAoEHitEffects@0xA75EF0`
	//   (skill_buff.c:16533-16600) крутит ОДИН И ТОТ ЖЕ блок спавна по 20 записям массива целей
	//   (`pChar + 86276` — renderId сущностей, `+104214` — персонажи), то есть эффект удара рождается
	//   у КАЖДОЙ задетой цели, а не только у выбранной. `nullptr` = основная цель показа (`A.Target`).
	AActor* SpawnFx(FAsdaSkillFxActive& A, const FAsdaSkillFxSpawn& S, bool bImpact,
		AActor* OverrideTarget = nullptr, bool bOverrideIsMob = true);
	// Акторы ДОПОЛНИТЕЛЬНЫХ площадных целей этой фазы (из 29-байтных записей 5257/5270), кроме
	//   основной цели показа. Порядок — как в пакете.
	void CollectAoeTargets(const FAsdaSkillFxActive& A,
		const TArray<FAsdaSkillHitRec>& Hits,
		TArray<TPair<AActor*, bool>>& OutTargets) const;
	// Спавн всей программы удара фазы у каждой дополнительной цели + счётчик рождённых эффектов.
	int32 SpawnAoeImpactFx(FAsdaSkillFxActive& A, const TArray<FAsdaSkillFxSpawn>& List,
		const TArray<FAsdaSkillHitRec>& Hits);
	// Звук удара (поле слота `+0x598`) — играет в КАЖДОЙ сработавшей фазе, как в эталоне.
	// ★ 08.11.2026: `Phase` ≥ 0 — звук своей фазы (`PhaseSnd`, если задан), иначе общий `SndHit` (как прежде); список «;» в `PhaseSnd` играется поимённо.
	void PlayHitSound(const FAsdaSkillFxActive& A, int32 Phase = -1);
	// Текущая цель показа ЛОКАЛЬНОГО игрока: эталонный `Target_GetSelectedUserIdx` → актор
	//   моба/игрока. Только ЧТЕНИЕ. ⚠ Запасной путь: для ЧУЖОГО каста выбранная НАМИ цель никакого
	//   отношения к соседу не имеет — там цель берётся из записи пакета (см. `FindCastRecTarget`).
	AActor* ResolveTarget(bool& bOutIsMob) const;
	// ★★★ ЦЕЛЬ ПОКАЗА ИЗ ЗАПИСЕЙ ПАКЕТА — 1:1 эталон. `Recv_SkillCast@0xF4E370` (network.c:44878,
	//   44888, 44926) во всех трёх ветках `modeMask` кладёт в `pChar->targetRenderId` поле `+1`
	//   ПЕРВОЙ 29-байтной записи (`targetWords[0]`), а вид цели различает байтом `+0` записи
	//   (`== 1` → цель-ПЕРСОНАЖ, бит 8 маски; иначе цель-СУЩНОСТЬ, бит 1). Тот же код у
	//   `Recv_SkillCastIdle@0xF4D9C0` (network.c:44725, 44735, 44768). Ищем в полке `PendingCastHits`
	//   батч этого кастера (при совпадении — и этого навыка), потому что порт разносит один пакет на
	//   два шага: записи приезжают делегатом РАНЬШЕ, чем очередь событий доносит сам ответ 5257/5270.
	bool FindCastRecTarget(int32 CasterUserIdx, int32 SkillId, int32& OutKind, int32& OutIdx) const;
	// ★★★ Выпустить припаркованные записи 4102 — «момент показа наступил». `bHasHitFx` — ведёт ли
	//   эта фаза реальный удар (гейт «MISS»), `Where` — человекочитаемая причина для лога.
	void ReleaseHits(TArray<FAsdaSkillHitRec>& Hits, bool bHasHitFx, const TCHAR* Where);
	// ★ 05.11.2026 [ОРИГИНАЛ] 8215 вид 0 — подтверждение попадания своего навыка по мобу на показе записи (разбор в .cpp, ключ
	//   asda.Skill.HitConfirm8215).
	void SendHitConfirm8215(const FAsdaSkillHitRec& R);
	// ★ 05.11.2026 [ОРИГИНАЛ] Состояние 285 «Отпор» (навыки 176/183) — обработчики таблиц состояний эталона: у ПЕРСОНАЖА
	//   `charSkillCastGroundTarget@0x9B4B70` (слот 52304+285), у МОБА `input_SkillCastAtTargetOrGround@0xF6EDF0` (слот 1844+285). Оба
	//   шлют 8215 ВИД 1 (отброс NpcServer `sub_436E90`) по цели с клеткой отброса навыка 285 (behaviorType 0). Вызов — из
	//   `UAsdaEffectsSubsystem::SpawnBuffFx` (момент наложения); ключ asda.Skill.RepulseState285.
public:
	void OnRepulseState285(int32 HostKey, bool bMob, int32 CasterUserIdx);
	// ★ 05.11.2026 [ОРИГИНАЛ] отлёт цели-моба по behaviorType навыка (`charSkillCast_SetCastTargetDir@0xA296C0`): 0 — сильный (если в точке
	//   отлёта есть земля), 1/2 — слабый, прочее — нет. PushDir — куда летит моб. Ключ asda.Skill.KnockBack.
	void ApplyKnockBack(class AAsdaMobActor* M, int32 SkillId, const FVector2D& PushDirGame, const FVector2D& FaceDirGame = FVector2D::ZeroVector);
private:
	// Повтор вызова на ту же запись (SpawnBuffFx повторяет, пока носителя нет) — один пакет на наложение: метка «носитель → время».
	TMap<int64, double> Repulse285SentS;
	// ★ Носитель снаряда фазы: спавн + регистрация полёта. Возвращает false — носитель не создан.
	bool LaunchPhaseProjectile(FAsdaSkillFxActive& A, int32 Phase, const FAsdaSkillFxSpawn* ProjSpawn);
	// Прилёт носителя: эффект удара + звук удара у цели, затем выпуск урона (порядок эталона).
	void OnFlightArrived(AAsdaProjectileActor* P);

	// ★★★ ФЛАГИ ПОПАДАНИЯ ИЗ 29-БАЙТНЫХ ЗАПИСЕЙ 5257/5270 (хвост §14.6 п.2 закрыт).
	//   Эталон различает попадание/промах байтом `+7` записи (бит0 — «урон применяется»,
	//   `charSplitSkillStatusByFlag@0xA28870`, §15.4), а порт до сих пор выводил «MISS» эвристикой
	//   по 4102 (`Damage == 0 && ApplyFlag == 1`). Теперь сетевые записи разбираются
	//   (`UAsdaNetSubsystem::ParseSkillCastRecs29`), флаг хранится по ключу
	//   (кастер, вид цели, индекс) и в `ReleaseHits` он АВТОРИТЕТ; эвристика остаётся запасной
	//   для записей 4102, к которым 29-байтная пара не пришла.
	void OnNetSkillCastRecs(int32 CasterUserIdx, int32 SkillCode,
		const TArray<FAsdaSkillCastRec>& Recs);
	struct FCastHitFlag { int32 HitFlags = 0; double AtS = 0.0; bool bMain = false; };   // bMain — ПЕРВАЯ запись каста (основная цель)
	static FORCEINLINE int64 CastHitKey(int32 CasterIdx, int32 TargetKind, int32 TargetIdx)
	{
		return ((int64)(uint16)CasterIdx << 32) | ((int64)(TargetKind ? 1 : 0) << 16) | (uint16)TargetIdx;
	}
	TMap<int64, FCastHitFlag> CastHitFlags;

	// ══ ★★★ УРОН НАВЫКА ИЗ 5257/5270 — ПРОМЕЖУТОЧНАЯ ПОЛКА ═══════════════════════════════════════
	//   Эталонный `Recv_SkillCast@0xF4E370` делает всё ОДНИМ вызовом: грузит клип применения
	//   (`packPathDispatchEntryHandler`, массив 0), раскладывает 29-байтные записи по массивам
	//   урона персонажа (`charSkillResetEffectSlotsAndDispatch@0xA29440` → `sub_A86700` →
	//   `skillParseQuickSlotPacketSimple@0xA86890`) и ставит состояние 24. Порт обязан разнести это
	//   на два шага: сетевой колбэк ничего тяжёлого не делает (закон no-reentrant-net-callbacks), а
	//   показ запускается уже из тика `UAsdaSkillUseSubsystem`. Поэтому записи кладутся СЮДА и
	//   разбираются в `FlushPendingCastHits`, который зовёт сам `PlaySkillVisual` сразу после
	//   постановки показа — то есть в том же логическом шаге, что и эталон.
	//   Батч, за которым показ так и не пришёл (чужой каст, у навыка нет строки в `DT_SkillClips`,
	//   отказ) выпускается тиком по истечении `kCastHitsGraceS` — терять урон нельзя.
	struct FPendingCastHits
	{
		int32 CasterUserIdx = -1;
		int32 SkillId = -1;
		double AtS = 0.0;
		TArray<FAsdaSkillHitRec> Hits;
	};
	TArray<FPendingCastHits> PendingCastHits;
	static constexpr double kCastHitsGraceS = 0.35;   // ждём показ не дольше трёх кадров

	// ══ ★★★ МНОГОУДАРНЫЙ НАВЫК: ОДНА ЗАПИСЬ СЕРВЕРА → ТРИ ЦИФРЫ ══════════════════════════════════
	//   Массив целей 5257/5270 разбирает НЕ одна функция, а та, что стоит в МАССИВЕ 6 таблицы
	//   `CharSkillEffect_InitDispatchTable@0xA89510` (диспетчер `sub_A86700`: `v6[activeId + 8132]`).
	//   Вариантов три, и один из них ДЕЛИТ урон записи на три части:
	//     `skillParseQuickSlotPacketSimple@0xA86890` — 1 цифра на цель (174 навыка);
	//     `skillParseQuickSlotEntry@0xA869F0`        — 1 цифра на цель, ветка по виду записи (38);
	//     `skillParseQuickSlotPacket@0xA86BD0`       — ТРИ части на цель, шаг записи 80 б:
	//         урон >= 3 : part0 = d/3, part1 = d/3, part2 = d − part0 − part1;
	//         урон  < 3 : part0 = 0,   part1 = 0,   part2 = d.
	//   По дампу `Reports/skill_dispatch_table.tsv` (колонка `cb6`) третий вариант стоит РОВНО
	//   у ОДНОГО навыка — **707 «Повторный выстрел»**, и у него ровно три фазы COMBO, каждая со
	//   своим носителем. Что часть i принадлежит фазе i — ДОКАЗАНО телом тика
	//   `CCharacter_UpdateSkillEffectTick@0xAC9DD0`: ветка `phaseIndex == 0` берёт величину из
	//   `effectSlots[100].field_64`, `== 1` — из `field_68`, `== 2` — из `gap_6C` (entity_char.c:
	//   33454 / 33041 / 33248), а ветка цели-ИГРОКА — из `effectSlots[101].field_E4/E8/EC`
	//   (33708 / 33915 / 34121). Обе тройки лежат ровно на `pChar + 172632/172636/172640` и
	//   `pChar + 174232/174236/174240` — то есть на part0/part1/part2 ПЕРВОЙ цели в массивах моба и
	//   игрока соответственно (декомпилятор рисует поля `effectSlots[...]` со сдвигом +16 к сырой
	//   адресной арифметике парсера; сдвиг один и тот же в обеих тройках, что его и подтверждает).
	//   ⚠ Если дамп пересняли и вариант `skillParseQuickSlotPacket` появился у других кодов —
	//     список ниже надо расширить по колонке `cb6`, иначе их урон покажется одной цифрой.
	static bool SkillSplitsDamageIn3(int32 SkillId) { return SkillId == 707; }
	// Разложить урон записи на три части ровно так, как `skillParseQuickSlotPacket@0xA86BD0`.
	static void SplitDamageIn3(int32 Damage, int32 (&OutParts)[3]);
	// Забрать из `From` записи, относящиеся к фазе `Phase` (и все «безфазные», Phase < 0).
	static void TakeHitsForPhase(TArray<FAsdaSkillHitRec>& From, int32 Phase,
		TArray<FAsdaSkillHitRec>& Out);
	// Раздать батчи этого кастера (и, при совпадении, этого навыка) по показам/носителям;
	//   что не припарковалось — применить немедленно. bAny = отдать ВСЁ, что пролежало дольше срока.
	void FlushPendingCastHits(int32 CasterUserIdx, int32 SkillId, bool bTimedOutOnly);

	// Кэш таблицы и индекс «код навыка → строки».
	mutable TWeakObjectPtr<UDataTable> Table;
	// Сильная ссылка на ту же таблицу на игровую сессию (см. `HoldGameTables`).
	UPROPERTY(Transient)
	mutable FAsdaHeldTables HeldTables;
	mutable bool bTableTried = false;
	mutable TMap<int32, TArray<const FAsdaSkillClipRow*>> BySkill;

	// ★ Аудит №3 (Дд1): своей загрузки `DT_MobStats` и своей карты `MobSizeByMobId` здесь БОЛЬШЕ НЕТ.
	//   Размерные поля типа моба (класс `+0x90` → сущность `+7356`, процент `+0x108` → `+7360`)
	//   читаются из ЕДИНОЙ точки `AAsdaMobActor::FindMobStatsRow(MobId)` — там же, где их берёт сам моб
	//   для уровня/агрессии/высоты плашки. Две копии одних чисел в памяти сняты.

	// Текущие показы.
	UPROPERTY() TArray<FAsdaSkillFxActive> Active;
	// ★ Носители в полёте. Живут ДОЛЬШЕ показа: клип может доиграть раньше прилёта.
	//   БЕЗ UPROPERTY: внутри только слабые ссылки и POD сетевых записей, GC-корень не нужен.
	TArray<FAsdaSkillFxFlight> Flights;
	// Хэндл тикера.
	FTSTicker::FDelegateHandle TickHandle;
};
