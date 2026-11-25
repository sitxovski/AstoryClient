// Copyright (c) 2026 @sitxovski (Discord)
// Реализация визуального слоя навыков (клип применения + эффекты + звук).
// Обоснование каждой константы — в шапке `Public/Skill/AsdaSkillFxSubsystem.h` и в
// `Reports/skill_fx_anim.md`.
#include "Skill/AsdaSkillFxSubsystem.h"
#include "AsdaNetSubsystem.h"   // ★ R1: прямое включение (раньше приходило транзитивно)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "UI/AsdaSkillUseSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaCombatTrace.h"   // ★ 18.10.2026: журнал боя — записи урона навыка, клип, кадр удара, снаряд, эффекты
#include "World/AsdaFloatingTextSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "AsdaSkillFxSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSkillFxSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaSkillFx);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

// ★★★ 31.10.2026 [ИСПРАВЛЕНИЕ] (баг игроков «ударил навыком — над целью нет цифр»): урон навыка — на КАДРЕ УДАРА и у навыков, в строке
//   клипа которых нет эффекта/звука удара. Тики эталона показывают урон по порогу COMBO0 БЕЗ условия «есть эффект»:
//   `Skill_TickSingleTargetHit` (908, skill_buff.c:15378 `if (COMBO <= прогресс)` → `CharFx_ShowSkillDamageText`),
//   `CharSkill_UpdateTornadoFx` (902), `CharSkill_UpdateValkyrieFx` (512 — эффекты удара зашиты в коде тика, поэтому в DT_SkillClips их нет).
//   Порт считал такую фазу «несуществующей» и выпускал записи 5257/5270 СРАЗУ при приёме пакета — цифра всплывала в начале замаха
//   и гасла (1 с) раньше удара (у 908 COMBO0 1400 мс, 931 — 2133 мс). По живым журналам игроков 30–31.10 так уходило 126 из 213 выпусков.
//   ★ Ревизия 31.10 (А1): НЕ ВСЯ ТАБЛИЦА. Ждут только тики из белого списка `IsHitFrameTick` — у них в эталоне есть ветка
//   «COMBO0 ≤ прогресс → показ урона» напрямую или через помощники (`Skill_PlayHitEffectOnEntity_Snd`, `CharFx_ApplySkillHitsToTargets`,
//   `charFx_ApplySkillHitsToTargets`, `CSkillCast_ApplyAoEHitEffects`). Список снят скриптом по графу вызовов эталона (глубина 3 и 6 дают
//   одно и то же: 8 тиков из 108 под условием) и проверен построчно: порог стоит над вызовом. Те же тики применяют эффекты состояния
//   (`Skill_ApplyEffectResult`) на этом же кадре, а обработчик 5257 (`Recv_SkillCast`) их не применяет вовсе — ожидание верно и для них.
//   Остальные тики (темп, звук, выход по паузе, автодействия) урона не показывают — их записи выпускаются, как раньше, сразу.
//   1 — тики белого списка ведут урон на COMBO0 (как эталон); 0 — прежний порт.
int32 GAsdaSkillHitFrameAlways = 1;
static ASDA_DEV_CVAR_REF(CVarAsdaSkillHitFrameAlways,
	TEXT("asda.Skill.HitFrameAlways"),
	GAsdaSkillHitFrameAlways,
	TEXT("1 — урон навыка (цифра, HP, строка чата, эффекты) на кадре удара COMBO0 у тиков эталона с показом урона по порогу, даже без эффекта удара в строке клипа; 0 — прежний порт (сразу при приёме 5257/5270)."),
	ECVF_Default);

// ★★★ 10.11.2026 [ОРИГИНАЛ] (job4-vfx, баг «у Blow нет эффектов»): два вида цели записи эффекта удара для тиков воина, которые сами
//   кладут имя эффекта в поле слота и зовут `Skill_PlayHitEffectOn(Char|Entity)_Snd` для ОСНОВНОЙ цели, а доп. цели обходят
//   `CharFx_ApplySkillHitsToTargets@0xA88300` / `charFx_ApplySkillHitsToTargets@0xA87C70` (effects_particles.c:14108 / 13957):
//     · «prim» — эффект только у основной цели (любого вида): помощник удара; в обходе доп. целей эталон его НЕ повторяет
//       (ветка мобов — только урон, effects_particles.c:14142-14160);
//     · «xchar» — эффект у каждой ДОПОЛНИТЕЛЬНОЙ цели-ИГРОКА записей с флагом попадания (`Effect_SpawnByName(…, posVec, 0, 6, …)`,
//       effects_particles.c:14225, режим 6 = следование); у мобов — нет. ★ Ревизия 2 (10.11): основную цель обход ПРОПУСКАЕТ —
//       эффект рождается только при `*((__int16*)pChar+104190) != userIdx цели` (word 104190 = +0x32DFC = renderId основной цели,
//       effects_particles.c:14204), а промах (`(*((_WORD*)pChar+i+104214) & 1) == 0`) даёт только MISS/нотис 20010 без эффекта
//       (:14213-14240). Прежняя пометка «с i = 0, т.е. и основной» была неверной: эффект у основной цели-игрока двоился.
//   Без этих видов `SpawnAoeImpactFx` повторял эффект удара у каждой доп. цели (507 «Удар мощи», 516 «Вихрь» — площадные).
//     · «pchar» — эффект только у ОСНОВНОЙ цели-ИГРОКА (у моба и у доп. целей — нет): 907 «Метеор» по игроку сразу рождает
//       `bigmeob03` у цели (`Effect_SpawnByName(…, 9, …)`, `SkillEffect_Update_Explosion@0xA70840`, effects_particles.c:8876);
//     · «rchar» — эффект у КАЖДОЙ цели-ИГРОКА записей, ВКЛЮЧАЯ основную, только при флаге попадания: обработчик записи вида 13
//       `skillFx_applySkillResultToTargets@0xA5E580` (ветка `resultFlags & 8`, effects_particles.c:6261-6279) — `CEffectMgr_CreateEffect(…, 9)`
//       у каждого персонажа с `(флаг & 1)`; у 907 по игроку это отложенная запись `fireragb03` (effects_particles.c:8885).
//   1 — как эталон; 0 — прежний порт (виды prim/xchar/pchar/rchar = без вида: эффект у основной и у всех доп. целей).
int32 GAsdaSkillHitFxTargetSplit = 1;
static ASDA_DEV_CVAR_REF(CVarAsdaSkillHitFxTargetSplit,
	TEXT("asda.Skill.HitFxTargetSplit"),
	GAsdaSkillHitFxTargetSplit,
	TEXT("1 — виды цели «prim» (только основная) и «xchar» (каждая цель-игрок) в записях эффекта удара, как эталон; 0 — прежний порт (эффект удара у основной и всех доп. целей)."),
	ECVF_Default);

// ★★★ 10.11.2026 [ОРИГИНАЛ] (job4-vfx, решение владельца): ПРОМАХ ПО ОСНОВНОЙ ЦЕЛИ у тиков, бьющих через помощники
//   `Skill_PlayHitEffectOnEntity_Snd@0xA7EE90` (skill_buff.c:18586) / `Skill_PlayHitEffectOnChar_Snd@0xA7E230` (skill_buff.c:18186):
//   помощник проверяет бит попадания фазы (`pChar[1].gap_0[4 или 6] & 1`) — попадание: эффект удара + звук удара + урон; промах: ТОЛЬКО
//   «MISS» (`FloatingText_Spawn(…, "MISS")`, skill_buff.c:18733). Тики-вызыватели (полный перебор декомпила): 503 Char_UpdateActionAnimWithHit,
//   505 skillStrikeUpdate, 507 charSkill_updateMageDeabolFx, 508 charFx_dragonFlySkill_update, 510 CharEffect_UpdateSkillTickMulti,
//   511 Char_UpdateSpearSmashAnim, 512 CharSkill_UpdateValkyrieFx, 515 CharSkill_UpdateSkillFx_Variant1, 516 CharFx_CycleCutscene_Update
//   (+ charFx_UpdateAutoAction — не навык таблицы). Прочие тики (`SkillEffect_UpdateScaledStrikeHit`, `CSkillCast_UpdateMeleeHit` …) и в
//   оригинале рождают эффект и звук при промахе — их не трогаем. 1 — как эталон; 0 — прежний порт (эффект и звук при любом исходе).
int32 GAsdaSkillPrimaryMissNoHitFx = 1;
static ASDA_DEV_CVAR_REF(CVarAsdaSkillPrimaryMissNoHitFx,
	TEXT("asda.Skill.PrimaryMissNoHitFx"),
	GAsdaSkillPrimaryMissNoHitFx,
	TEXT("[ОРИГИНАЛ] 1 — у тиков на помощниках Skill_PlayHitEffectOn*_Snd (503/505/507/508/510/511/512/515/516) промах по основной цели даёт только «MISS», без эффекта и звука удара; 0 — прежний порт."),
	ECVF_Default);

// Идёт удар фазы с промахом по основной цели (тик на помощнике): эффекты у основной цели и звук удара не рождаются. Ставит FirePhase.
bool GAsdaSkillPrimaryMissGate = false;
// ★ 10.11.2026 (ревизия 4): вид выпускаемой отложенной записи для `ReleaseHits` (0 — не запись). Вид 9 (911, `skillResultApplyDamage`,
//   skill_buff.c:12118-12147) отлёта и смены состояния не делает; вид 13 (907, `skillFx_applySkillResultToTargets`, effects_particles.c:6209-6220)
//   при behaviorType ≠ 4 ставит отлёт КАЖДОЙ задетой цели-мобу (не только основной). Ставит FireRecordFlight.
int32 GAsdaSkillRecordReleaseKind = 0;
// ★★★ 10.11.2026 [ОРИГИНАЛ] (job4-vfx, «do it right»): ЗАПИСЬ 5257/5270 С БИТОМ ПРОМАХА И НЕНУЛЕВЫМ УРОНОМ. Эталон цифру урона рисует и HP
//   вычитает только под битом попадания: помощники удара (`Skill_PlayHitEffectOn*_Snd`, skill_buff.c:18586/18186 — ветка промаха = только
//   «MISS», skill_buff.c:18733), тики ближнего боя (ветка промаха — «MISS»), обработчики отложенных записей вида 9/13 (`& 1`, skill_buff.c:12128,
//   effects_particles.c:6205) — при промахе ничего. Порт выбирал «MISS»/цифру по величине урона и показывал число. 1 — урон такой записи
//   обнуляется (цифры и вычитания нет), «MISS» — там, где тик ведёт удар (у отложенных записей — ничего); 0 — прежний порт.
int32 GAsdaSkillMissNoDamageText = 1;
static ASDA_DEV_CVAR_REF(CVarAsdaSkillMissNoDamageText,
	TEXT("asda.Skill.MissNoDamageText"),
	GAsdaSkillMissNoDamageText,
	TEXT("[ОРИГИНАЛ] 1 — запись 5257/5270 без бита попадания (и без IMMUNE) с ненулевым уроном: без цифры и вычитания HP, «MISS» где тик ведёт удар; 0 — прежний порт."),
	ECVF_Default);

// ★★★ 10.11.2026 [ИСПРАВЛЕНИЕ] (job4-vfx, решение владельца «5. yes» / «3. do it»): ЗВУК УДАРА ПО МОБУ — в точке ЦЕЛИ. Оригинал играет его
//   у КАСТЕРА (`Skill_PlayHitEffectOnEntity_Snd`: `Audio_Play3DSound(pChar->soundPos…)`, skill_buff.c:18717-18719; записи вида 9/13 —
//   `pOwnerChar+344/+444`), из-за чего удар по далёкому мобу звучит «у мага». 1 — у цели-моба (звук удара, «@caster» звуков записи
//   при цели-мобе и звук удара моба `_99` записи); 0 — как оригинал, у кастера. У цели-игрока звук и в оригинале у цели — не меняется.
int32 GAsdaSkillMobHitSoundAtTarget = 1;
static ASDA_DEV_CVAR_REF(CVarAsdaSkillMobHitSoundAtTarget,
	TEXT("asda.Skill.MobHitSoundAtTarget"),
	GAsdaSkillMobHitSoundAtTarget,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — звук удара навыка по мобу звучит у моба; 0 — как оригинал, у кастера."),
	ECVF_Default);

// ★ 05.11.2026 [ОРИГИНАЛ] «Граната» 604/607 (`CharSkill_UpdateGrenadeFx@0xB43EC0`): на ПЕРВОМ тике показа (`+45429 != 1`, без порога COMBO)
//   узел снаряда `skillProjectile_createNode` с моделью `grened01`, временем полёта 1500 мс (0x44BB8000) и дугой `effectBeamUpdate`
//   (вершина +750 над концом, конец следует за целью, старт — позиция кастера +50). Прежний порт пускал общий прямой носитель `.inl`
//   (2.5 юн/мс — 0.05…0.11 с полёта) на пороге COMBO0 = 233 мс: граната «не летела», урон казался мгновенным. 0 — прежний порт.
#if ASDA_DEV_CONSOLE   // ★ R3-c чистка: в Shipping этот ключ — константа из AsdaSkillFxSubsystem_Private.h
ASDA_DEV_CVAR(int32, CVarAsdaSkillGrenadeArc, TEXT("asda.Skill.GrenadeArc"), kAsdaSkillGrenadeArcDefault,
	TEXT("[ОРИГИНАЛ] Граната 604/607: 1 — бросок на первом кадре, дуга 1500 мс с вершиной +750 и самонаведением (effectBeamUpdate); ")
	TEXT("0 — прежний порт (прямой носитель 2.5 юн/мс на пороге COMBO0)."),
	ECVF_Default);
#endif // ASDA_DEV_CONSOLE

// Показ запущен ответом 5270 (каст) — взводит `UAsdaSkillUseSubsystem` на время вызова `PlaySkillVisual`.
bool GAsdaShowFromCastIdle = false;
void AsdaSkillFxSetShowFromCastIdle(bool bOn) { GAsdaShowFromCastIdle = bOn; }

double GAsdaChargePreLastS = -1.0;      // самотест: когда модель родилась
double GAsdaChargeLaunchLastS = -1.0;   // самотест: когда снята вылетом
void AsdaSkillFxDebugChargeTimes(double& OutPreS, double& OutLaunchS) { OutPreS = GAsdaChargePreLastS; OutLaunchS = GAsdaChargeLaunchLastS; }

// ★★★ 05.11.2026 [ОРИГИНАЛ] (баг «Rising Dragon: два удара, одна цифра»; `asda.Skill.HitRecordPerPhase`). Сервер на многоударный навык
//   шлёт в 5257/5270 НЕСКОЛЬКО записей по ОДНОЙ цели — по записи на удар, у каждой свой урон и своё «HP после» (живые журналы 01–05.11:
//   504, 506, 508, 509, 510, 513, 701 по 5270 и 903 по 5257 — всегда 2 записи на одну цель). Эталон кладёт их подряд:
//   `skillParseQuickSlotPacketSimple@0xA86890` (skill_buff.c:19009) — урон записи i в `pChar + 172632 + 4·i`
//   (= `effectSlots[100].field_64 + i`), и тик на фазе k показывает ДВОЙНОЕ СЛОВО k: помощники `Skill_PlayHitEffectOnEntity_Snd@0xA7EE90`
//   (skill_buff.c:18586; фаза 0 — `field_64`, фаза 1 — `field_68`, фаза ≥ 2 — выход), `Skill_PlayHitEffectOnEntity`,
//   `Skill_PlayHitEffectOnChar`; прямо `*(&field_64 + field_3A0)` — `CSkillEffect_Update` (effects_particles.c:25294/25341),
//   `CharSkill_UpdateSkillFx_Variant2` (стадия 0 `field_64`, 1 `field_68`, 2 `gap_6C`; skill_buff.c:24459/24257/24052),
//   `CharFx_UpdateAutoCastState` (фаза 0 `field_64`, 1 `field_68`; effects_particles.c:9801/9611). Порт считал все записи
//   «безфазными» (`Phase = −1`) и выпускал их на первой же фазе — две цифры вспыхивали разом и ложились одна на другую.
//   Тики ниже — ровно те многофазные (2+ порога COMBO в DT_SkillClips) тики, чей показ урона идёт по номеру фазы. НЕ входят:
//   `CSkillCast_UpdateMultiHitCombo` (502 — цифра только на фазе 0), `CCharacter_UpdateSkillEffectTick` (707 — одна запись делится
//   на три, `SkillSplitsDamageIn3`), `SkillEffect_Update_Explosion` (907) и `charFxFbladgUpdate` (594) — урон идёт другим механизмом
//   (★ 10.11.2026: у 907/911 — отложенные записи, `SkillRecordWaves`; их фаза 0 ставится там же, где bPhaseUsed).
//   1 — запись k по одной цели ждёт фазу k своего тика (как эталон); 0 — прежний порт (все записи на первой фазе).
int32 GAsdaSkillHitRecordPerPhase = 1;
static ASDA_DEV_CVAR_REF(CVarAsdaSkillHitRecordPerPhase,
	TEXT("asda.Skill.HitRecordPerPhase"),
	GAsdaSkillHitRecordPerPhase,
	TEXT("[ОРИГИНАЛ] 1 — у многоударных навыков (несколько записей 5257/5270 по одной цели) запись k показывается на фазе удара k, как эталон; ")
	TEXT("0 — прежний порт (все цифры разом на первой фазе)."),
	ECVF_Default);

// Тик многофазного навыка с показом урона по номеру фазы (разбор и происхождение — в шапке ключа выше).
bool AsdaSkillIsPhaseIndexedTick(const FString& TickFn)
{
	static const TCHAR* const kTicks[] = {
		TEXT("Char_UpdateActionAnimWithHit"),       // 503 → Skill_PlayHitEffectOnEntity_Snd
		TEXT("CharSkill_UpdateFataleFx"),           // 504 → Skill_PlayHitEffectOnEntity / OnChar (skill_buff.c:22275)
		TEXT("charFxAutoSkillTick"),                // 506 → Skill_PlayHitEffectOnEntity / OnChar
		TEXT("charFx_dragonFlySkill_update"),       // 508 → Skill_PlayHitEffectOnEntity_Snd / OnChar (effects_particles.c:25555)
		TEXT("charAnim_UpdateActionBlend"),         // 509 → Skill_PlayHitEffectOnEntity / OnChar
		TEXT("CharEffect_UpdateSkillTickMulti"),    // 510 → Skill_PlayHitEffectOnEntity_Snd
		TEXT("CSkillEffect_Update"),                // 513 → *(&field_64 + field_3A0)
		TEXT("CharSkill_UpdateSkillFx_Variant2"),   // 701 → field_64 / field_68 / gap_6C по стадии
		TEXT("CharFx_UpdateAutoCastState"),         // 903 → field_64 / field_68 по фазе
	};
	for (const TCHAR* T : kTicks) { if (TickFn.Equals(T, ESearchCase::CaseSensitive)) { return true; } }
	return false;
}

// ★★★ 08.11.2026 [ОРИГИНАЛ] (баг «Starlight Slasher: 4 удара приходят одновременно», пачка 17; `asda.Skill.HexaFourHits`).
//   Навык 513 «Сияющий резак» — ЧЕТЫРЕ удара. Живые журналы (Player5, 01–05.11): сервер на каждый каст шлёт ОДИН 5270 с ЧЕТЫРЬМЯ записями
//   по одной цели (31 из 31; заявка 5256 клиента — одна запись, сервер размножает), у каждой свой урон и «HP после». Эталон
//   `CSkillEffect_Update` (effects_particles.c:24962) ведёт `field_3A0` = 0..3: порог фазы k — `*(&slot.color + k)` (≤ прогресса клипа),
//   урон фазы k — `*(&effectSlots[100].field_64 + k)` (моб) / `*(&effectSlots[101].field_E4 + k)` (игрок) = запись k пакета. Пороги —
//   ЧЕТЫРЕ числа `combo time.bin` +84[0..3] (`SkillEffect_SetupHexa@0xAA4A40` грузит их циклом `colorIdx < 4`): у клипов
//   `ch4_{m,f}_01_sq-hexa-{s,m,l}_02` 666.7 / 1100 / 1533.3 / 2500 мс. Порт брал из таблицы три порога (DT_SkillClips: Combo0..2) и вёл три
//   фазы: до 05.11 все 4 записи выходили на фазе 0 (живой журнал 01.11 00:45:20 — четыре цифры за 1 мс), после правки 05.11 — 1/1/2 на
//   667/1100/1533, четвёртого удара не было вовсе. Программа фаз — из тела тика:
//     · старт тика (`field_160 == -1`): эффект `field_68` (hexaa07/08/09 по размеру цели) у ЦЕЛИ (+25 по высоте) — один раз;
//     · фазы 0..2: `field_168` (hexah02) в точке луча «излучатель → цель» (`effectCollideRayWithObject`), звуки `S_c_F3hexsla02`
//       и `S_c_F3hexsla01` у кастера, цифра урона, клип 13 цели и отлёт;
//     · фаза 3: литерал `eff_1_char_01_hexab03-a_00.jeb` у цели и звук `S_c_F3hexsla03` (флаг `HIWORD(pChar[1].posVec[0])` = слово
//       +0x0A = бит попадания фазы 3), цифра урона, клип 13 и отлёт.
//   В строке клипа 513 эти поля сняты неточно (каст «у кастера», эффекты удара только на фазе 1, звук удара одной строкой
//   «S_c_hexa6.wav;S_c_F3hexsla03.wav» — в живом журнале «звук не найден»; `S_c_hexa6` = `field_598`, этот тик его не играет).
//   1 — четыре фазы и программа эталона; 0 — прежний порт (три фазы строки клипа).
int32 GAsdaSkillHexaFourHits = 1;
static ASDA_DEV_CVAR_REF(CVarAsdaSkillHexaFourHits,
	TEXT("asda.Skill.HexaFourHits"),
	GAsdaSkillHexaFourHits,
	TEXT("[ОРИГИНАЛ] Навык 513 «Starlight Slasher»: 1 — четыре удара на порогах combo time.bin 667/1100/1533/2500 мс, запись k на ударе k, ")
	TEXT("эффекты и звуки по тику CSkillEffect_Update; 0 — прежний порт (три фазы строки клипа, звук удара не находился)."),
	ECVF_Default);

	// ══ ★★★ СТРОГАЯ (ЭТАЛОННАЯ) ГЕОМЕТРИЯ КВАДОВ И КОЛЕЦ У ЭФФЕКТОВ ПЕРСОНАЖА ═══════════════════
	// СИМПТОМ (живой тест 2026-08-10): эффект каста навыка 707 `eff_1_char_07_runfire01-a_00` —
	//   по замыслу ВСПЫШКА ВЫСТРЕЛА У ОРУЖИЯ — выглядел «защитной сферой», надетой на персонажа.
	// ПРИЧИНА (по данным .jeb + по коду `AAsdaEffectActor`): у этого эффекта КАНАЛЬНЫЕ ВРАЩЕНИЯ
	//   эмиттеров НЕ нулевые — `cylinder/cylinder01/cylinder02` (0xE) имеют `rotX = −90°` при
	//   pos-канале (0, −80, −100), `TSC_SW03…08` (0xE) — `rotX = 90°, rotY = 90°` при (−25, −80, −80),
	//   квады 0xB `sprite…05` — `rot = (90, −90, 0)` при roll 90° и pos (50, 12, −80) / (0, 80, 0).
	//   По КАНОНУ 0xE (дизасм ring-Draw `0x1215C10`, [[effects-conversion-model]]) центр кольца =
	//   `basePos + (posChan·R)·(R·N48·N520)`, т.е. канальные вращения действуют на pos-канал ДВАЖДЫ:
	//   у конусов `R² = RotX(−180°)` переносит (0, −80, −100) → (0, +80, +100), то есть РОВНО в точку
	//   дула (80 юн вверх, 100 вперёд). Без `R²` конус остаётся ПОД ногами и ЗА спиной, а квады
	//   получают вдобавок «швартовку» `RotY(π)` и roll, крутящий сам ЦЕНТР (тот же дефект, что
	//   чинили у свечения заточки — [[weapon-glow-saga]] причины 1, 2 и 6). Все 24 эмиттера при этом
	//   рассыпаются вокруг персонажа кольцом ±80…100 юн — это и читается как «щит/сфера».
	// ПОЧЕМУ ГЕЙТ НЕ СРАБАТЫВАЛ: эталонную ветку включает АКТОРНЫЙ флаг `bStrictQuadAttach`
	//   (`AsdaEffectActor.cpp`: `bEtalonMapPath = !bBaseAlreadyUE && (!bStrictRingBasePos ||
	//   bStrictQuadAttach)` и `if (bAttachedQuad && !bStrictAtt) G = RY(G, PI)`). Его ставили только
	//   свечение заточки (`WeaponGlow`) и эффекты Хранителя (`OwnerBoundFx`); эффекты навыков шли
	//   БЕЗ него — картовой веткой. Между тем в оригинале ветка отрисовки квада/кольца ОДНА
	//   (`spriteBuildTransformAndDraw@0x120CF10` / ring-Draw `0x1215C10`) и от кода привязки
	//   (6 «мировая матрица персонажа» / 7 / 10 «кость») НЕ ЗАВИСИТ — attach выбирает лишь матрицу
	//   узла N520. Значит строгая ветка обязана работать у ВСЕХ эффектов навыка: и у каста, и у
	//   удара, и у шлейфа снаряда. `strict_ring_base_pos` у char-эффектов ставит билдер
	//   (`asda_build_char_fx.py`), поэтому без `bStrictQuadAttach` кольца теряли именно `R²`.
	// ОТКАТ: `asda.Skill.StrictFx 0` (флаг только для эффектов, спавнимых ЭТОЙ подсистемой;
	//   карты, крылья, соулмейт, маунт и башня alpen не затронуты — у них своя ветка).
	// ══ ★ ИЗМЕРИТЕЛЬНЫЙ МНОЖИТЕЛЬ ГЕОМЕТРИИ ЭФФЕКТОВ НАВЫКА (дефолт 1.0 = ЭТАЛОН) ═══════════════
	// Зачем: «флеш удара выглядит слишком большим» — суждение ГЛАЗАМИ, а вердикт по визуалу может
	//   дать только игра ([[weapon-glow-saga]]). Размеры при этом БЕРУТСЯ ИЗ ДАННЫХ `.jeb`
	//   (полу-экстенты halfW/halfH, дизасм `effectQuad_updateAndDrawBillboard@0x120AEB0`), и
	//   единственный законный внешний множитель — `CEffectMgr_scaleAll` (см. `GetTargetSizePercent`).
	//   Поэтому вместо «подкрутить на глаз в коде» даём ИНСТРУМЕНТ ЗАМЕРА: игрок ставит множитель,
	//   называет число, при котором картинка совпадает с оригиналом, — и уже ЭТО число становится
	//   поводом искать, какой множитель эталона мы потеряли. Ноль догадок в коде.
	//   ⚠ Действует ТОЛЬКО на эффекты, спавнимые ЭТОЙ подсистемой (карты/крылья/хранитель — нет).
	float GAsdaSkillFxScale = 1.f;

namespace {
	FAutoConsoleVariableRef GSkillFxScaleRef(
		TEXT("asda.Skill.FxScale"),
		GAsdaSkillFxScale,
		TEXT("Навыки: измерительный множитель геометрии эффектов (1 = эталон/данные .jeb)"));
}

	bool GAsdaSkillFxStrictQuad = true;

namespace {
	FAutoConsoleVariableRef GSkillFxStrictQuadRef(
		TEXT("asda.Skill.StrictFx"),
		GAsdaSkillFxStrictQuad,
		TEXT("Навыки: эталонная геометрия квадов/колец у эффектов навыка (1 = канон ring-Draw 0x1215C10 ")
		TEXT("с двойным R и без π-швартовки; 0 = прежняя картовая ветка)"));
}

void UAsdaSkillFxSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UAsdaSkillFxSubsystem::TickActive), 0.f);
	// ★ Флаги попадания из 29-байтных записей 5257/5270 (см. шапку CastHitFlags в .h).
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnSkillCastRecs.AddUObject(this, &UAsdaSkillFxSubsystem::OnNetSkillCastRecs);
		// ★★★ ТОТ ЖЕ ОБРАБОТЧИК ДЛЯ 6159/6160 (скиллы ХРАНИТЕЛЯ ДУШИ). Эталон обе шапки разбирает
		//   ОДНОЙ `Recv_SkillCast@0xF4E370` и кладёт записи в ТЕ ЖЕ массивы целей/урона, значит и
		//   парковка до кадра удара у них общая. Канал делегата остался отдельным только из-за
		//   нотисов отказа Хранителя — см. шапку `OnSoulSkillCastRecs`.
		Net->OnSoulSkillCastRecs.AddUObject(this, &UAsdaSkillFxSubsystem::OnNetSkillCastRecs);
	}
}

void UAsdaSkillFxSubsystem::Deinitialize()
{
	if (TickHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); }
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnSkillCastRecs.RemoveAll(this);
		Net->OnSoulSkillCastRecs.RemoveAll(this);
	}
	Active.Reset();
	Flights.Reset();
	BySkill.Reset();
	CastHitFlags.Reset();
	PendingCastHits.Reset();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaSkillFxSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Полёты снарядов и отложенные удары держат слабые ссылки на АКТОРОВ мира и ключуются
	// сессионными userIdx кастера — гасим при любой причине, включая смену карты.
	Active.Reset();
	Flights.Reset();
	CastHitFlags.Reset();
	PendingCastHits.Reset();
	(void)Reason;
}
