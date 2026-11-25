// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillFxSubsystem_Spawn.cpp — ★ R3-c 12.11.2026 (разрез AsdaSkillFxSubsystem.cpp, дословный перенос): показ: SpawnFx, площадные удары, снаряд фазы, прилёт, FirePhase.
//   Общее для частей — в AsdaSkillFxSubsystem_Private.h; шапка и реверс — в AsdaSkillFxSubsystem.cpp.
#include "Skill/AsdaSkillFxSubsystem.h"
#include "AsdaNetSubsystem.h"   // ★ R1: прямое включение (раньше приходило транзитивно)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "AsdaEffectActor.h"
#include "AsdaFxDeadHost.h"   // ★ R5-fix5 (13.11.2026): метка «гаснуть вместе с умершим мобом-носителем»
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Resources/AsdaModelAnimSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaProjectileActor.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaCombatTrace.h"   // ★ 18.10.2026: журнал боя — записи урона навыка, клип, кадр удара, снаряд, эффекты
#include "UI/AsdaSkillDatabase.h"          // ★ 05.11.2026: behaviorType навыка — клетка 8215 (SendHitConfirm8215)
#include "UI/AsdaEffectsSubsystem.h"
#include "AsdaSkillFxSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSkillFxSubsystem*.cpp

// ★ 01.11.2026 [ИСПРАВЛЕНИЕ] (решение владельца: «свечение соулмейтов на 50 % менее яркое»). Показ навыков Дружбы — коды
//   1286…1355 (ремап `sub_ACEFC0`: id 30…99 + 1256, `UAsdaSkillUseSubsystem::RemapSoulmateSkillId`; заняты 1287…1300 —
//   `charFxInitSoulSkillVtbl@0xACEE20`): `eff_1_char_99_soulritec-a_00` (1287/1289/1291/1293/1298/1300), `soulcallh-a_00`
//   (1288/1296), `attbomg-a_01` + `attbomh-a_00` (1290/1294/1299). Все три — аддитивные (.jeb blend 4/1), белое «свечение» тела —
//   это наложение их слоёв плюс свет эффекта (`FxLight`). Яркость аддитивного слоя = цвет × альфа, поэтому множитель кладётся в
//   рантайм-альфу всего эффекта `AAsdaEffectActor::FxAlphaScale` — ею же умножается и сила света эффекта (AsdaFxLightComponent).
//   Общие мастера и ассеты эффектов не трогаются. 1 = как в оригинале (прежнее поведение), 0.5 = вдвое тусклее (умолчание).
// ★★★ R5-fix6 (13.11.2026) [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА, решение владельца 13.11]: ЭФФЕКТ УДАРА НАВЫКА У ЦЕЛИ-ИГРОКА СЛЕДИТ ЗА НЕЙ.
//   Эталон, ветка целей-игроков кадра удара (`skillFx_applySkillResultToTargets@0xA5E580`, effects_particles.c:6262-6278):
//   `CEffectMgr_CreateEffect(имя, 1, позиция цели-игрока, userIdx цели-игрока, 9, 0, 1.0)` — эффект создаётся режимом 9
//   («каждый кадр в позиции носителя», `effectSyncPosToOwnerEntity@0xF58E40`), носителем назван САМ игрок-цель. Но обработчик
//   режима 9 ищет носителя по этому номеру в списке МОБОВ (`Entity_FindByRenderId(g_pCharManager, …)`): игрока там нет, и эффект
//   либо удаляется следующим проходом (носитель не найден), либо едет за чужим мобом, чей номер совпал с userIdx игрока.
//   ЗАМЫСЕЛ читается из вызова: позиция и номер взяты у цели-игрока, режим — слежение; ошибка только в том, где ищется носитель.
//   ПОРТ: эффект рождается у цели-игрока и виден (так было и раньше), за мобом с совпавшим номером не едет (носитель — актор
//   игрока, а не номер), а при ключе 1 ещё и следит за игроком-целью, как задумано режимом 9; при 0 — стоит в точке рождения
//   (прежний порт).
//   ★ ОБЛАСТЬ (ревизия): только путь `skillFx_applySkillResultToTargets` — гранаты 604 и 607 (обработчик таблицы, skill_buff.c:11680)
//   и запись вида 13 «Метеора» 907 (`packBuildEntryRecord`, effects_particles.c:8885; в таблице — вид цели «rchar»). У других
//   записей удара «track» ветка цели-игрока оригинала иная: `Effect_SpawnByName(…, носитель 0, 9, …)` (906 «Молния» —
//   effects_particles.c:9035; основная запись 907 — :8876): носитель не назван, обработчик режима 9 при нулевом носителе ничего
//   не делает — эффект у оригинала стоит на месте, и порт его не трогает.
//   ★ R5-fix8 (14.11.2026): навык 1064 прослежен (`Char_UpdateSkillAnim_MultiPhase@0xA76E70`, entity_char.c:29542): эффект удара
//   едет в снаряде (`effect_spawnDirectedFromTo`), по прилёте ветка цели-игрока (`CEffect_UpdateSkillCast@0xA5A060`,
//   effects_particles.c:5416-5428) создаёт его `CEffectMgr_CreateEffect_F561F0@0xF561F0` — создателем БЕЗ носителя и режима, с
//   нулём в параметре a7 (поле `sortKey`): эффект стоит в точке, номер игрока-цели никуда не передан. Доказательства замысла слежения нет —
//   по правилу этого ключа (только пути, где оригинал назвал носителем игрока-цель) 1064 НЕ входит; значение ключа 2 включает
//   слежение и для него (на выбор владельца, по аналогии с веткой цели-моба).
//   Записи «attach» (режим 13)
//   у цели-игрока не тронуты: у эталона ветка цели-игрока прилёта снаряда (`CEffect_UpdateSkillCast@0xA5A060`,
//   effects_particles.c:5440-5458) эффекта режимом 13 не создаёт вовсе — замысла слежения там нет. Записи фазы каста не тронуты.
// ★★★ R5-fix8 (14.11.2026) [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА]: ЗАПИСИ «track» ФАЗЫ КАСТА У ЦЕЛИ-ИГРОКА (ключ `asda.Skill.CastFxTrackPlayer`).
//   • 756 «Ловушка» (`CharFx_UpdateTrapSkillEffect@0xB3F250`, effects_particles.c:53201). Цель-моб: `CEffectMgr_CreateEffect(имя, 1,
//     позиция, renderId цели, 9, 0, 1.0)` — эффект следит за мобом. Цель-игрок (:53268-53274): `CEffectMgr_CreateEffect_F561F0(имя,
//     1, позиция, (float)userIdx цели, 1.0)` — создатель без носителя, а номер игрока-цели (`Target_GetSelectedUserIdx`) передан в
//     параметр a7 создателя (поле эффекта `sortKey`, effects_particles.c:60122; назначение поля не прослежено — чтений по имени в
//     срезе декомпила нет). Замысел читается из вызова: номер цели взят так же, как у моба, но попал не в носитель —
//     эффект у оригинала стоит на месте. ДЕФЕКТ ОРИГИНАЛА; у порта эффект стоял на месте.
//     Правка: эффект фазы каста 756 у цели-игрока следит за ней (режим 9, без подъёма) — значение 1.
//   • 908 (`CharFx_PlaySkillDakhlEffect@0xA84130`, effects_particles.c:12448). Цель-моб — режим 9 с renderId; цель-игрок
//     (:12484-12491) — `CEffectMgr_CreateEffect_F561F0(имя, 1, позиция, 0, 1.0)`: носитель не назван, a7 ноль. Следа замысла
//     слежения нет — по умолчанию как у оригинала (стоит); значение 2 включает слежение и для 908 (на выбор владельца).
//   0 = прежний порт (оба стоят).
static ASDA_DEV_CVAR(int32, CVarAsdaSkillCastFxTrackPlayer, TEXT("asda.Skill.CastFxTrackPlayer"), 1,
	TEXT("Эффект «track» фазы каста у цели-ИГРОКА: 1 (по умолчанию) = «Ловушка» 756 следит за игроком-целью (дефект оригинала исправлен: номер ")
	TEXT("цели передан не в носитель); 2 = то же и для навыка 908 (у оригинала стоит на месте); 0 = прежний порт: эффекты стоят."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarAsdaSkillHitFxTrackPlayer, TEXT("asda.Skill.HitFxTrackPlayer"), 1,
	TEXT("Эффект удара гранат 604/607 и записи «Метеора» 907 (режим 9 оригинала, носитель — игрок-цель) у цели-ИГРОКА: 1 = следит за игроком-целью (дефект оригинала исправлен: ")
	TEXT("оригинал искал носителя среди мобов — эффект пропадал или уезжал за чужим мобом); 0 = прежний порт: эффект стоит в точке рождения."),
	ECVF_Default);

static ASDA_DEV_CVAR(float, CVarAsdaSoulSkillGlowScale, TEXT("asda.Soul.SkillGlowScale"), 0.5f,
	TEXT("Яркость показа навыков Дружбы (соулмейта) 1286…1355: множитель альфы и света их эффектов, 0..1. 1 = как в оригинале, 0.5 = вдвое тусклее (решение владельца 01.11.2026)"),
	ECVF_Default);

// Показ навыка Дружбы? Диапазон ремапа `sub_ACEFC0` (id 30…99 → +1256, всё ≥ 0x54C отсекается).
static bool AsdaIsSoulSkillVisual(int32 SkillId) { return SkillId >= 30 + 1256 && SkillId < 0x54C; }

float AsdaSoulSkillGlowFactor(const AActor* Fx)
{
	if (!Fx || !Fx->ActorHasTag(kAsdaSoulSkillGlowTag)) { return 1.f; }
	return FMath::Clamp(CVarAsdaSoulSkillGlowScale.GetValueOnGameThread(), 0.f, 1.f);
}

// Тик бьёт основную цель через помощник `Skill_PlayHitEffectOn*_Snd` (перечень — у ключа выше).
static bool IsHitHelperTick(const FString& TickFn)
{
	static const TCHAR* kTicks[] = {
		TEXT("Char_UpdateActionAnimWithHit"), TEXT("skillStrikeUpdate"), TEXT("charSkill_updateMageDeabolFx"),
		TEXT("charFx_dragonFlySkill_update"), TEXT("CharEffect_UpdateSkillTickMulti"), TEXT("Char_UpdateSpearSmashAnim"),
		TEXT("CharSkill_UpdateValkyrieFx"), TEXT("CharSkill_UpdateSkillFx_Variant1"), TEXT("CharFx_CycleCutscene_Update"),
		TEXT("charFx_UpdateAutoAction") };
	for (const TCHAR* T : kTicks) { if (TickFn == T) { return true; } }
	return false;
}

static constexpr float kGrenadeFlightS = 1.5f;     // `projectileSpeed` = 1500.0f — длительность полёта, мс (дельта движка — мс)
static constexpr float kGrenadeApexUp = 750.f;     // `beamSelf[7] = конец.y + 750.0`
static constexpr float kGrenadeStartUp = 50.f;     // `self[4] = позиция кастера.y + 50.0` (skillProjectile_init)

namespace {
	// Запас жизни эффект-актора сверх длины клипа (эффекты доигрывают после конца анимации).
	constexpr float kFxExtraLifeSec = 3.0f;
	// ★ Сколько живёт эффект УДАРА: он рождается в момент попадания и с клипом уже не связан.
	//   Эталон отдаёт его общему менеджеру эффектов (`CEffectMgr_CreateEffect_F561F0`), который
	//   гасит одноразовый эффект по концу его собственных эмиттеров/контейнера.
	//   ★ В2-C F2 (11.10.2026): прежний жёсткий лимит `kHitFxLifeSec = 4.0` снят — срок удара теперь
	//   `AAsdaEffectActor::NaturalLifeSpan()` (разбор у вызова в `SpawnFx`).
	// ★ СКОРОСТЬ СНАРЯДА НАВЫКА — ровно та же, что у стрелы: `dist/2.5`, т.е. 2.5 юнита/мс
	//   (`effect_spawnDirectedFromTo@0xA59B30`: `sub_91E5AF(...); result = v165 / 2.5;` — и это тот же
	//   литерал 0x40200000, что в `collisionTrace_Init` стрелы). Отдельной скорости у навыков нет.
	constexpr float kSkillProjSpeedUnitsPerMs = 2.5f;
}

AActor* UAsdaSkillFxSubsystem::SpawnFx(FAsdaSkillFxActive& A, const FAsdaSkillFxSpawn& S, bool bImpact,
	AActor* OverrideTarget, bool bOverrideIsMob)
{
	AAsdaPlayerCharacter* C = A.Caster.Get();
	UWorld* W = C ? C->GetWorld() : nullptr;
	if (!C || !W) { return nullptr; }
	// ★ 10.11.2026 (job4-vfx): эффект отложенной записи («~rec») рождается только при её выпуске (`FireRecordFlight`), прочие — только вне его.
	if (S.bRecord != bSpawningRecord) { return nullptr; }
	// ★ 10.11.2026: промах по основной цели у тика на помощнике — эффект удара у основной цели не рождается (только «MISS»).
	if (GAsdaSkillPrimaryMissGate && bImpact && !OverrideTarget && (S.Origin == FName(TEXT("target")) || S.Origin == FName(TEXT("hitpoint")))) { return nullptr; }
	// ★ Площадная цель подменяет основную ТОЛЬКО в позиционировании/масштабе/привязке к цели —
	//   всё остальное (кость кастера, поворот, время жизни) остаётся программой показа.
	AActor* Tgt = OverrideTarget ? OverrideTarget : A.Target.Get();
	const bool bTgtIsMob = OverrideTarget ? bOverrideIsMob : A.bTargetIsMob;

	// ★ Ветка ЦЕЛИ у эталона раздельная: `mob` — та, что ищет `Entity_FindByRenderId`,
	//   `char` — та, что ищет `CharManager_FindByUserIdx`. Ветка не той цели просто не выполняется.
	if (S.TargetKind == FName(TEXT("mob")) && !bTgtIsMob) { return nullptr; }
	// ★ 05.11.2026 [ОРИГИНАЛ] вид «main» — повтор эффекта удара у ОСНОВНОЙ цели-моба: обработчик записи вида 13
	//   `skillFx_applySkillResultToTargets` (`lockedTargetId == цель` → `CEffectMgr_CreateEffect(…, 9)`) после цикла по целям
	//   `effectBeamUpdate` — у гранаты основная цель получает grebom01 дважды. Только сущность (ветка `resultFlags & 1`).
	if (S.TargetKind == FName(TEXT("main")) && (!bTgtIsMob || OverrideTarget)) { return nullptr; }
	if (S.TargetKind == FName(TEXT("char")) && bTgtIsMob) { return nullptr; }
	// ★ 10.11.2026 (job4-vfx) виды «prim»/«xchar» — см. шапку `asda.Skill.HitFxTargetSplit`.
	if (GAsdaSkillHitFxTargetSplit != 0 && S.TargetKind == FName(TEXT("prim")) && OverrideTarget) { return nullptr; }
	if (GAsdaSkillHitFxTargetSplit != 0 && S.TargetKind == FName(TEXT("xchar")) && (bTgtIsMob || !OverrideTarget)) { return nullptr; }   // ★ ревизия 2: основная цель — только «prim»
	// ★ 10.11.2026 (job4-vfx, решение владельца: 907 в PvP как в оригинале) виды «pchar»/«rchar» — см. шапку `asda.Skill.HitFxTargetSplit`.
	if (GAsdaSkillHitFxTargetSplit != 0 && S.TargetKind == FName(TEXT("pchar")) && (bTgtIsMob || OverrideTarget)) { return nullptr; }
	if (GAsdaSkillHitFxTargetSplit != 0 && S.TargetKind == FName(TEXT("rchar")))
	{
		if (bTgtIsMob) { return nullptr; }
		// Основная цель: эффект только при записи ответа с флагом попадания (доп. цели фильтрует `SpawnAoeImpactFx`).
		//   ★ Ревизия 3: записи нет — эффекта нет (обработчик вида 13 идёт только по записям, effects_particles.c:6261-6264).
		if (!OverrideTarget)
		{
			UGameInstance* RGI = GetGameInstance();
			UAsdaWorldSubsystem* RWS = RGI ? RGI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			bool bHitRec = false;
			for (const UAsdaNetSubsystem::FAsdaSkillHitRec& H : A.PendingHits)
			{
				const EAsdaTargetKind HK = AsdaTargetKindFromHitRec(H.TargetKind);
				if (!RWS || HK == EAsdaTargetKind::Entity || H.TargetIdx < 0) { continue; }
				if (RWS->ActorByTarget(HK, H.TargetIdx) == Tgt) { bHitRec = (H.HitFlags & 1) != 0; break; }
			}
			if (!bHitRec) { return nullptr; }
		}
	}

	// ═══ ★★★ ТОЧКА РОЖДЕНИЯ — ПО КОЛОНКЕ `Origin`, А НЕ ПО ФАЗЕ ════════════════════════════════
	// БЫЛО: «эффект удара ⇒ всегда у цели, эффект каста ⇒ всегда у персонажа». Это НЕ то, что делает
	//   эталон: точку он берёт из АРГУМЕНТОВ вызова спавна, и она бывает любой в обеих фазах.
	//   Живые расхождения, которые это чинит:
	//     · навык 905 — `CEntity_PlayEffectWithBone` спавнит `слот+0x368` через
	//       `effectMgrSpawnJEB(…, МИРОВАЯ МАТРИЦА КАСТЕРА)` НА ПОРОГЕ УДАРА ⇒ у ПЕРСОНАЖА,
	//       а порт уводил его к цели;
	//     · 38 записей `matrix:hitpoint` (`CSkillCast_ApplyEntityHitEffect@0xA7B4A0`,
	//       `Skill_PlayHitEffect*`) — точка ЛУЧА «кастер→цель» × AABB цели, а не центр цели;
	//     · 4 записи `char:target` в КАСТЕ — эффект старта рождается У ЦЕЛИ.
	//   Значения `Origin` сняты механически (`_origin_near` в `asda_dump_skill_dispatch.py`);
	//   у ВСЕХ 254 записей удара источник определён, пустых нет.
	const bool bHasBone = C->BaseSkel && C->BaseSkel->GetBoneIndex(A.FxBone) != INDEX_NONE;
	const FVector CasterLoc = C->GetActorLocation();
	const FVector BoneLoc = bHasBone ? C->BaseSkel->GetBoneLocation(A.FxBone) : CasterLoc;
	// ★★★ ТОЧКА ЦЕЛИ БЕЗ ОТКАТА НА КАСТЕРА (симптом «эффекты на себе», живой тест 2026-08-10).
	//   Цель здесь сплошь и рядом уже МЕРТВА (мобов убивают с одного удара; порог фазы/прилёт
	//   наступает через 0.3-0.7 с после смерти) — прежняя ветка `Tgt ? … : CasterLoc` рожала эффект
	//   УДАРА на самом кастере. Эталон: ближний тик ищет цель по renderId (`Entity_FindByRenderId`)
	//   и при её отсутствии ветку удара НЕ ВЫПОЛНЯЕТ; носитель летит в зафиксированную позицию.
	//   Порт: последняя известная позиция цели (`A.LastTargetLoc`, освежается тиком) — а если цели
	//   не было вовсе, эффект «у цели» не спавнится и это видно в логе.
	const bool bKnowTargetLoc = (Tgt != nullptr) || A.bHasTargetLoc;
	const FVector TargetLoc = Tgt ? Tgt->GetActorLocation()
	                              : (A.bHasTargetLoc ? A.LastTargetLoc : CasterLoc);
	if (!bKnowTargetLoc && (S.Origin == FName(TEXT("target")) || S.Origin == FName(TEXT("hitpoint"))))
	{
		UE_LOG(LogAsdaSkillFx, Log,
			TEXT("[навыки-визуал] навык %d: эффект %s рождается «у цели», а цели нет и не было — ")
			TEXT("не спавним (эталон: ветка удара без цели не выполняется)"), A.SkillId, *S.Jeb);
		ASDA_CTRACE(EAsdaCT::Fx, TEXT("навык %d: эффект %s «у цели» НЕ рождён — цели нет и не было"), A.SkillId, *S.Jeb);
		return nullptr;
	}

	FVector Loc;
	if (S.Origin == FName(TEXT("target"))) { Loc = TargetLoc; }
	else if (S.Origin == FName(TEXT("caster"))) { Loc = CasterLoc; }
	else if (S.Origin == FName(TEXT("bone"))) { Loc = BoneLoc; }
	else if (S.Origin == FName(TEXT("hitpoint")))
	{
		// ★ Эталон: `effectCollideRayWithObject(кастер, …, цель)` — пересечение отрезка
		//   «кастер → цель» с AABB цели, при промахе луча берётся позиция цели. Порт считает то же
		//   по РЕАЛЬНОМУ AABB актора цели (`GetActorBounds`), а не по выдуманному радиусу.
		Loc = TargetLoc;
		if (Tgt)
		{
			FVector Org, Ext;
			Tgt->GetActorBounds(/*bOnlyCollidingComponents=*/false, Org, Ext);
			const FBox Box(Org - Ext, Org + Ext);
			FVector HitLoc, HitNormal;
			float HitTime = 0.f;
			const FVector From = bHasBone ? BoneLoc : CasterLoc;
			// Луч ведём ОТ КАСТЕРА В ЦЕНТР AABB цели — так же, как эталон строит отрезок
			// `lineSegmentInit(кастер.soundPos, цель.gap_18C[40..48])` и ищет его вход в бокс.
			Loc = (FMath::LineExtentBoxIntersection(Box, From, Org, FVector::ZeroVector,
				HitLoc, HitNormal, HitTime))
				? HitLoc                           // первая точка касания = «точка попадания»
				: Org;                             // луч мимо — эталон берёт позицию цели
		}
	}
	else
	{
		// Источник не определился (в текущей таблице таких записей удара НЕТ — только 3 записи
		// КАСТА). Запасной вариант тот же, что был раньше, и он ОБЯЗАН быть виден в логе.
		// ★ Запасной «удар у цели» без известной позиции цели — НЕ спавним (иначе он снова
		//   родился бы на кастере — ровно симптом «эффекты на себе»).
		if (bImpact && !bKnowTargetLoc)
		{
			UE_LOG(LogAsdaSkillFx, Log,
				TEXT("[навыки-визуал] навык %d: запасной вариант эффекта удара %s требует цель, ")
				TEXT("а её нет и не было — не спавним"), A.SkillId, *S.Jeb);
			ASDA_CTRACE(EAsdaCT::Fx, TEXT("навык %d: запасной эффект удара %s НЕ рождён — цели нет и не было"), A.SkillId, *S.Jeb);
			return nullptr;
		}
		Loc = bImpact ? TargetLoc : (S.Anchor == FName(TEXT("bone")) ? BoneLoc : CasterLoc);
		UE_LOG(LogAsdaSkillFx, Warning,
			TEXT("[навыки-визуал] навык %d: у эффекта %s ТОЧКА РОЖДЕНИЯ не разобрана "
			     "(привязка %s) — взят запасной вариант «%s»"),
			A.SkillId, *S.Jeb, *S.Anchor.ToString(), bImpact ? TEXT("у цели") : TEXT("у персонажа"));
	}

	// ★★★ ОРИЕНТАЦИЯ. Эталон передаёт спавнеру ЛИБО одну точку (`Effect_SpawnByName`,
	//   `CEffectMgr_CreateEffect_F561F0`, `effectMgrCreateEffectEx` — ориентация единичная), ЛИБО
	//   ПОЛНУЮ МИРОВУЮ МАТРИЦУ (`effectMgrSpawnJEB(…, pChar->worldMatrix)` — привязка `matrix`),
	//   у которой поворот всегда КАСТЕРА (translation при этом подменяется на точку рождения:
	//   `matrixA[12..14] = hitPos`). Порт спавнил ВСЁ с нулевым поворотом — эффекты `matrix`
	//   (74 записи удара: «взрыв меча», «удар копьём», «крушение») смотрели строго на север
	//   независимо от разворота персонажа. Это и есть «эффекты выглядят криво».
	const FRotator Rot = (S.Anchor == FName(TEXT("matrix")))
		? FRotator(0.f, C->GetActorRotation().Yaw, 0.f) : FRotator::ZeroRotator;

	AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnByJebName(W, S.Jeb, Loc, Rot,
		bImpact ? Tgt : Cast<AActor>(C));
	if (!Fx)
	{
		UE_LOG(LogAsdaSkillFx, Warning,
			TEXT("[навыки-визуал] BP_Fx_%s не найден (запусти Tools/asda_build_skill_fx.py)"),
			*S.Jeb.Replace(TEXT("-"), TEXT("_")));
		ASDA_CTRACE(EAsdaCT::Fx, TEXT("навык %d: эффект %s НЕ рождён — ассет BP_Fx не найден"), A.SkillId, *S.Jeb);
		return nullptr;
	}

	// ═══ КАК ЭФФЕКТ ДЕРЖИТСЯ — СТРОГО ПО ПРИВЯЗКЕ (одинаково в касте и в ударе) ═════════════════
	// Прежде ветка выбиралась по `bImpact`: любой эффект удара считался «свободным у цели», и
	//   привязки `bone`/`follow`, которые эталон применяет и на пороге удара, терялись.
	// ★ 04.11.2026 [ОРИГИНАЛ] «track» = attach-режимы 3/9/12/13/14/15 эталона (effectSyncPosToOwnerEntity@0xF58E40 и др. из таблицы
	//   обработчиков CEffectMgr_Construct: эффект каждый кадр в точке цели). Раньше эти эффекты рождались неподвижными в точке цели на
	//   момент спавна — «Молния» мага оставалась на месте, когда моб уходил.
	//   Обработчик ищет носителя по renderId только среди МОБОВ — у цели-персонажа эффект эталона носителя не находит (дефект
	//   оригинала; исправление — ветка `asda.Skill.HitFxTrackPlayer` ниже, R5-fix6).
	//   ★ R5-fix3 (13.11.2026, уточнение по декомпилу): «только среди мобов» следует из КОНТЕЙНЕРА поиска, а не из `v1[116] == 23`:
	//   `Entity_FindByRenderId(g_pCharManager, …)` обходит список сущностей-мобов (тот же, что грузит `mobEffectStreamArchiveSteps`);
	//   игроки лежат в другом списке (`CharManager_FindByUserIdx`). Поле [116] (+0x1D0) — `loadSubState`, стадия загрузки моба
	//   (23 = загружен). Правила обработчиков 3/9/12/13 (0xF583E0, 0xF58E40, 0xF59410, 0xF595B0) одинаковы: носитель не найден →
	//   эффект помечен мёртвым; найден и загружен: состояние [120] == 10 (умирает) → эффект помечен мёртвым, иначе эффект ставится
	//   в позицию моба; найден, но ещё грузится → эффект стоит. Порт гасит эффект при удалении актора-носителя
	//   (`AAsdaEffectActor::Tick`, ветка `bFollowCharPos`); по смерти моба (состояние 10) — НЕ гасит: отличие от эталона, не правлено.
	if ((S.Anchor == FName(TEXT("attach")) || S.Anchor == FName(TEXT("track"))) && bTgtIsMob && Tgt)
	{
		// APPEND_TYPE 1 → эталонный attach 13 (`CEffect_UpdateSkillCast@0xA5A060`: `effectMgrCreateEffectEx(…, 13, …)` + scaleAll):
		//   `effectPlaceAboveEntity@0xF595B0` каждый кадр ставит эффект НАД мобом — +200/+400/+600 по классу размера `target[1839]`.
		//   «track» → attach 9 (`effectSyncPosToOwnerEntity`): каждый кадр в позиции моба, без подъёма.
		//   ★ 04.11.2026 [ОРИГИНАЛ]: прежде «attach» прикреплялся без подъёма и к цели-персонажу тоже; оба обработчика эталона ведут
		//   только мобов (носитель ищется в списке мобов `g_pCharManager`; `v1[116] == 23` — «моб загружен», см. уточнение R5-fix3 выше),
		//   у цели-персонажа эффект стоит на месте. Таблица режимов — общая `AAsdaEffectActor::EtalonAttach`.
		EAsdaFxAttachMode Mode = EAsdaFxAttachMode::CharPos;
		float RiseZ = 0.f;
		AAsdaEffectActor::EtalonAttach(S.Anchor == FName(TEXT("attach")) ? 13 : 9, GetTargetSizeClass(Tgt), 0.f, Mode, RiseZ);
		Fx->SetActorLocation(Tgt->GetActorLocation() + FVector(0.f, 0.f, RiseZ));
		Fx->ConfigureBoneAttach(Tgt, nullptr, NAME_None, Mode, FVector(0.f, 0.f, RiseZ));
		// ★ R5-fix5 (13.11.2026) [ОРИГИНАЛ]: запись ФАЗЫ КАСТА на цели-мобе (навыки 756 и 908, режим 9) гаснет, когда моб умер, —
		//   обработчик `effectSyncPosToOwnerEntity@0xF58E40`: состояние носителя 10 → эффект удаляется. Эффект ветки удара
		//   (`bImpact`) НЕ помечается: он рождается тем же вызовом, что и смерть от добивающего удара. Разбор — AsdaFxDeadHost.h.
		if (!bImpact) { AsdaFxDeadHost::Mark(Fx); }
	}
	else if (S.Anchor == FName(TEXT("track")) && !bTgtIsMob && Cast<AAsdaPlayerCharacter>(Tgt)
		&& (bImpact
			// удар: путь оригинала, где носителем назван игрок-цель (значение 1); 1064 — только по значению 2 (R5-fix8, разбор у ключа)
			? (CVarAsdaSkillHitFxTrackPlayer.GetValueOnGameThread() != 0
				&& (A.SkillId == 604 || A.SkillId == 607 || S.TargetKind == FName(TEXT("rchar"))
					|| (A.SkillId == 1064 && CVarAsdaSkillHitFxTrackPlayer.GetValueOnGameThread() >= 2)))
			// фаза каста (R5-fix8): 756 — дефект оригинала (значение 1), 908 — по значению 2; разбор у `asda.Skill.CastFxTrackPlayer`
			: ((A.SkillId == 756 && CVarAsdaSkillCastFxTrackPlayer.GetValueOnGameThread() >= 1)
				|| (A.SkillId == 908 && CVarAsdaSkillCastFxTrackPlayer.GetValueOnGameThread() >= 2))))
	{
		// ★ R5-fix6 (13.11.2026) [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА]: эффект удара «track» у цели-игрока следит за ней (режим 9 без подъёма) —
		//   разбор у ключа `asda.Skill.HitFxTrackPlayer` в шапке файла. Метки «гаснуть с умершим мобом» нет: носитель — игрок.
		EAsdaFxAttachMode Mode = EAsdaFxAttachMode::CharPos;
		float RiseZ = 0.f;
		AAsdaEffectActor::EtalonAttach(9, 0, 0.f, Mode, RiseZ);
		Fx->SetActorLocation(Tgt->GetActorLocation() + FVector(0.f, 0.f, RiseZ));
		Fx->ConfigureBoneAttach(Tgt, nullptr, NAME_None, Mode, FVector(0.f, 0.f, RiseZ));
	}
	else if (S.Anchor == FName(TEXT("bone")) && S.Origin != FName(TEXT("target")))
	{
		// attach 7/10: кость даёт ПОЗИЦИЮ (type-2 модель эффектов, [[effects-conversion-model]] §5).
		Fx->ConfigureBoneAttach(C, C->BaseSkel, A.FxBone, EAsdaFxAttachMode::OwnerBoundFx);
	}
	else if ((S.Anchor == FName(TEXT("follow")) || S.Anchor == FName(TEXT("char")))
		&& S.Origin != FName(TEXT("target")))
	{
		// attach-6 эталона: эффект живёт в МИРОВОЙ МАТРИЦЕ персонажа (не на кости).
		Fx->ConfigureBoneAttach(C, C->BaseSkel, NAME_None, EAsdaFxAttachMode::CharWorld);
	}
	else if (GAsdaSkillHitFxTargetSplit != 0 && S.Anchor == FName(TEXT("follow")) && S.Origin == FName(TEXT("target")) && !bTgtIsMob)
	{
		// ★ 10.11.2026 (job4-vfx, решение владельца) [ОРИГИНАЛ]: «follow:target» у цели-ПЕРСОНАЖА — attach-6 эталона в мировой
		//   матрице ЦЕЛИ: `Effect_SpawnByName(…, pTargetChar->soundPos*, 0, pTargetChar->posVec, 0, 6, …)` (effects_particles.c:14225,
		//   обход игроков `CharFx_ApplySkillHitsToTargets@0xA88300`) — эффект идёт за целью, а не стоит в точке рождения.
		//   Прежде эффект ставился один раз (свободный). Ключ 0 (`asda.Skill.HitFxTargetSplit`) — прежнее поведение.
		if (AAsdaPlayerCharacter* TgtChar = Cast<AAsdaPlayerCharacter>(Tgt))
		{
			Fx->ConfigureBoneAttach(TgtChar, TgtChar->BaseSkel, NAME_None, EAsdaFxAttachMode::CharWorld);
		}
	}
	// `world` / `matrix` и всё, что рождено У ЦЕЛИ, — свободный эффект, поставленный один раз.

	// ★★★ ЭТАЛОННАЯ ГЕОМЕТРИЯ КВАДОВ/КОЛЕЦ — см. блок про `GAsdaSkillFxStrictQuad` в шапке файла.
	//   ⚠ СТРОГО ПОСЛЕ `ConfigureBoneAttach`: тот сам выставляет `bStrictQuadAttach` по режиму
	//   привязки (у `CharWorld` — в false), и присваивание до него было бы затёрто.
	Fx->bStrictQuadAttach = GAsdaSkillFxStrictQuad;

	// ★ 01.11.2026 [ИСПРАВЛЕНИЕ]: свечение показа навыков Дружбы — вполовину (ключ `asda.Soul.SkillGlowScale`, разбор у ключа).
	//   Множитель ДОМНОЖАЕТСЯ к текущему (эффект мог родиться уже притушенным GM-«скрыться»). Тег держит его при пересчётах
	//   GM-«скрыться» (`AAsdaPlayerCharacter::ApplyGmHideToAttachedFx` ставит α абсолютной и домножает `AsdaSoulSkillGlowFactor`).
	if (AsdaIsSoulSkillVisual(A.SkillId))
	{
		Fx->Tags.AddUnique(kAsdaSoulSkillGlowTag);
		const float GlowK = AsdaSoulSkillGlowFactor(Fx);
		if (GlowK < 1.f) { Fx->SetFxAlphaScale(Fx->FxAlphaScale * GlowK); }
	}

	if (bImpact)
	{
		// ★ Масштаб по цели — `CEffectMgr_scaleAll(fx, масштабЦели/100)`. Эталон делает это ровно в
		//   двух местах, и оба — эффект удара У ЦЕЛИ: `SkillEffect_UpdateScaledStrikeHit@0xA72610`
		//   (ветка цели-МОБА) и `CEffect_UpdateSkillCast@0xA5A060` (прилёт носителя). Эффект удара,
		//   рождённый У КАСТЕРА (`matrix:caster`, навык 905), эталон НЕ масштабирует — и мы тоже.
		// ★ 04.11.2026 [ОРИГИНАЛ]: «track» (attach 9 и др.) не масштабируется — спавны эталона `CEffectMgr_CreateEffect(…, renderId, 9, 0, 1.0f)`
		//   scaleAll не зовут (он есть только в SkillEffect_UpdateScaledStrikeHit и CEffect_UpdateSkillCast).
		if ((S.Origin == FName(TEXT("target")) || S.Origin == FName(TEXT("hitpoint"))) && S.Anchor != FName(TEXT("track")))
		{
			// ★★★ ИСТОЧНИК МНОЖИТЕЛЯ ПОДКЛЮЧЕН 2026-08-11: `sub_936A29` = `get_o730@0x99F150` =
			//   `*(entity + 7360)` — ПРОЦЕНТНЫЙ размер, который загрузчик моба берёт из
			//   `Mob list.BIN +0x108` (см. `GetTargetSizePercent`). Значения 100 в данных нет ни у
			//   одного из 854 типов (50…500) ⇒ прежняя заглушка `1.0` делала эффект удара мельче
			//   реального в 1.2…5 раз у ВСЕХ мобов.
			// ★★★ 2026-08-14: множитель берётся ЕДИНЫМ законом `GetSkillHitFxScale` — у цели-МОБА
			//   `%размера/100`, у цели-ПЕРСОНАЖА эталонная КОНСТАНТА 2.0
			//   (`effectSpawnSkillProjectiles@0xA77C20` / `CSkillCast_ApplyAoEHitEffects@0xA75EF0`).
			//   Раньше персонаж получал 1.0, потому что `GetTargetSizePercent` отдаёт для него 100.
			// ★ 04.11.2026 [ОРИГИНАЛ]: прилёт носителя `.inl` у цели-персонажа — `CEffect_UpdateSkillCast` спавнит удар с 1.0 без scaleAll.
			const float HitK = (bSpawnAtInlArrival && !bTgtIsMob) ? 1.f : GetSkillHitFxScale(Tgt);
			Fx->SetActorScale3D(FVector(HitK * GAsdaSkillFxScale));
		}
		else if (!FMath::IsNearlyEqual(GAsdaSkillFxScale, 1.f))
		{
			Fx->SetActorScale3D(FVector(GAsdaSkillFxScale));
		}
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-C, F2): срок эффекта удара — `NaturalLifeSpan` (до конца контейнера 0/2:
		//   эталон отдаёт удар менеджеру эффектов без хэндла гашения, `CEffectMgr_CreateEffect_F561F0`). Было жёсткое
		//   `kHitFxLifeSec` 4,0 с. Снимок `asda.Dedup.W2C.Life` по всем 73 эффектам удара DT_SkillClips (у всех контейнер endMode 0):
		//   у 8 natural длиннее 4 с (до 20,5 с у `ch1_a_4th_mg_03_2`) — их хвост обрезался; у 64 короче — после конца контейнера
		//   эффект и так не рисуется (гашение по контейнеру, 08.10), актор лишь дольше висел невидимым.
		Fx->SetLifeSpan(Fx->NaturalLifeSpan());
	}
	else
	{
		if (!FMath::IsNearlyEqual(GAsdaSkillFxScale, 1.f)) { Fx->SetActorScale3D(FVector(GAsdaSkillFxScale)); }
		// ★★★ СОБСТВЕННОЕ ВРЕМЯ ЭФФЕКТА КАСТА = `animSpeedScale` (эталонное поле узла эффекта
		//   `+0x80C`, индекс 515). 50 мест оригинала кладут туда значение сразу после спавна
		//   (`charFxAutoSkillTick@0xAB6570`, game.c:138764 — и так далее), а читает его единственная
		//   точка `objectMgr_TickEffects@0xF57990` (game.c:562489) и отдаёт в
		//   `objAnimRotateUpdateSpeed@0x114E260`, который умножает на него ВЕСЬ прошедший срок
		//   эффекта: поворот, накопители эмиттеров и возраст (а значит и время жизни).
		//   У нас ровно тот же смысл имеет `AAsdaEffectActor::TimeScale` (множитель шага симуляции).
		//   ⚠ ТОЛЬКО ЭФФЕКТ КАСТА. Эффекты УДАРА (ветка `bImpact` выше) поле не получают ни в одном
		//   из пяти помощников оригинала (`CSkillCast_ApplyEntityHitEffect@0xA7B4A0`,
		//   `CSkillCast_ApplyAoEHitEffects@0xA75EF0`, `Skill_PlayHitEffectOnChar/OnEntity/OnChar_Snd`)
		//   — перебраны их тела целиком, вхождений нет. Воспроизводим как есть.
		Fx->TimeScale = A.AnimScale;
		// ★ Время жизни уже в РЕАЛЬНЫХ секундах: `EndTimeS − StartTimeS` посчитано с учётом множителя.
		Fx->SetLifeSpan(float(A.EndTimeS - A.StartTimeS) + kFxExtraLifeSec);
	}
	A.Fx.Add(Fx);
	ASDA_CTRACE(EAsdaCT::Fx, TEXT("навык %d: эффект %s %s — на %s (%s), точка «%s», привязка «%s», масштаб %.2f, живёт %.2f с"),
		A.SkillId, *S.Jeb, bImpact ? TEXT("УДАРА") : TEXT("каста"),
		Tgt && (S.Origin == FName(TEXT("target")) || S.Origin == FName(TEXT("hitpoint"))) ? *Tgt->GetName() : *C->GetName(),
		(S.Origin == FName(TEXT("target")) || S.Origin == FName(TEXT("hitpoint"))) ? (bTgtIsMob ? TEXT("цель-моб") : TEXT("цель-игрок")) : TEXT("кастер"),
		*S.Origin.ToString(), *S.Anchor.ToString(), Fx->GetActorScale3D().X, Fx->GetLifeSpan());
	return Fx;
}

// ══ ★★★★★ ПЛОЩАДНЫЕ ЭФФЕКТЫ УДАРА — `CSkillCast_ApplyAoEHitEffects@0xA75EF0` ═══════════════════
//   Эталон (skill_buff.c:16533-16600 и 16829-16860) на пороге удара крутит ДВА цикла по 20:
//     for (hitIdx = 0; hitIdx < 20; ++hitIdx)
//         if (*((__int16*)pChar + hitIdx + 86276) != -1)                    // массив СУЩНОСТЕЙ
//             pTargetEntity = Entity_FindByRenderId(…);
//             if (*((_WORD*)pChar + 104194) & 1)  { …спавн эффектов ударных подсекций 2 и 10… }
//             damage = *((_DWORD*)pChar + hitIdx + 43158);                  // урон ЭТОЙ цели
//             CClass_SubtractStatClamped(pTargetEntity, damage, 0);
//             CharFx_ShowSkillDamageText(…, pTargetEntity, damage, …);      // цифра НАД КАЖДОЙ
//     for (effectPass = 0; effectPass < 20; ++effectPass)                   // массив ПЕРСОНАЖЕЙ
//             … то же самое через Combat_ShowDamageFeedback …
//   Массивы наполняет парсер ответа 5257/5270 (`skillParseQuickSlotPacketSimple@0xA86890`,
//   skill_buff.c:19017-19031): renderId'ы сущностей → `char+172552`, урон → `char+172632`;
//   userNum'ы персонажей → `char+172592`, урон → `char+174232`.
//   ⇒ ИСТОЧНИК СПИСКА ЦЕЛЕЙ ДЛЯ ВИЗУАЛА — ИМЕННО ЗАПИСИ ОТВЕТА, а не выбранная цель UI.
//   В порту эти записи лежат в `A.PendingHits` / `F.Hits` — отсюда и берём.
void UAsdaSkillFxSubsystem::CollectAoeTargets(const FAsdaSkillFxActive& A,
	const TArray<UAsdaNetSubsystem::FAsdaSkillHitRec>& Hits,
	TArray<TPair<AActor*, bool>>& OutTargets) const
{
	OutTargets.Reset();
	if (Hits.Num() <= 1) { return; }                       // одна запись — площади нет
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!WS) { return; }
	const AActor* Primary = A.Target.Get();
	TSet<const AActor*> Seen;
	if (Primary) { Seen.Add(Primary); }
	for (const UAsdaNetSubsystem::FAsdaSkillHitRec& H : Hits)
	{
		if (H.TargetIdx < 0) { continue; }
		// ★ Аудит №3 (B6): вид цели — из СЫРОГО `rec[0]` 29-байтной записи ответа 5257/5270 через
		//   `AsdaTargetKindFromHitRec` (1 = ПЕРСОНАЖ; доказательство — `skillParseQuickSlotEntry@0xA869F0`
		//   и `CSkillCast_ApplyAoEHitEffects@0xA75EF0`, см. заголовок мира). Поиск — единой точкой.
		const EAsdaTargetKind Kind = AsdaTargetKindFromHitRec(H.TargetKind);
		const bool bMob = (Kind == EAsdaTargetKind::Entity);
		AActor* T = WS->ActorByTarget(Kind, H.TargetIdx);
		if (!T || Seen.Contains(T)) { continue; }          // цель уже мертва/выгружена — эталон её тоже пропускает
		Seen.Add(T);
		OutTargets.Add(TPair<AActor*, bool>(T, bMob));
	}
}

int32 UAsdaSkillFxSubsystem::SpawnAoeImpactFx(FAsdaSkillFxActive& A,
	const TArray<FAsdaSkillFxSpawn>& List,
	const TArray<UAsdaNetSubsystem::FAsdaSkillHitRec>& Hits)
{
	TArray<TPair<AActor*, bool>> Extra;
	CollectAoeTargets(A, Hits, Extra);
	if (Extra.Num() == 0) { return 0; }
	// ★ 10.11.2026 (ревизия 2 job4-vfx): флаг попадания каждой цели-игрока — для вида «xchar» (эталон: промах = без эффекта).
	TMap<const AActor*, int32> CharHitFlags;   // актор цели-игрока → HitFlags его записи ответа
	{
		UGameInstance* GI = GetGameInstance();
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		for (const UAsdaNetSubsystem::FAsdaSkillHitRec& H : Hits)
		{
			const EAsdaTargetKind Kind = AsdaTargetKindFromHitRec(H.TargetKind);
			if (!WS || H.TargetIdx < 0 || Kind == EAsdaTargetKind::Entity) { continue; }
			if (const AActor* T = WS->ActorByTarget(Kind, H.TargetIdx)) { CharHitFlags.FindOrAdd(T) |= H.HitFlags; }
		}
	}
	int32 N = 0;
	for (const TPair<AActor*, bool>& T : Extra)
	{
		for (const FAsdaSkillFxSpawn& S : List)
		{
			if (S.Anchor == FName(TEXT("projectile"))) { continue; }
			// Эффекты, которые рождаются У КАСТЕРА (`caster`/`bone`), у эталона живут ОДИН РАЗ на каст
			//   (это дуло/замах), а не по разу на цель — их не дублируем.
			if (S.Origin != FName(TEXT("target")) && S.Origin != FName(TEXT("hitpoint"))) { continue; }
			if (S.TargetKind == FName(TEXT("main"))) { continue; }   // ★ 05.11: «main» — только основная цель (обработчик записи вида 13)
			if (GAsdaSkillHitFxTargetSplit != 0 && S.TargetKind == FName(TEXT("prim"))) { continue; }   // ★ 10.11: «prim» — только основная цель
			if (GAsdaSkillHitFxTargetSplit != 0 && S.TargetKind == FName(TEXT("pchar"))) { continue; }  // ★ 10.11 (907): «pchar» — только основная цель-игрок
			// ★ Ревизия 3: «rchar» — ветка цели-ИГРОКА эталона; при основной цели-мобе (флаги 1|8, network.c:45573-45578) тик идёт
			//   веткой моба (effects_particles.c:8851) и записи `fireragb03` нет вовсе — доп. игроки её не получают.
			if (GAsdaSkillHitFxTargetSplit != 0 && S.TargetKind == FName(TEXT("rchar")) && A.bTargetIsMob) { continue; }
			if (GAsdaSkillHitFxTargetSplit != 0 && (S.TargetKind == FName(TEXT("xchar")) || S.TargetKind == FName(TEXT("rchar"))))   // ★ ревизия 2: промах — без эффекта
			{
				const int32* F = CharHitFlags.Find(T.Key);
				if (!F || (*F & 1) == 0) { continue; }
			}
			if (SpawnFx(A, S, /*bImpact=*/true, T.Key, T.Value)) { ++N; }
		}
	}
	UE_LOG(LogAsdaSkillFx, Log,
		TEXT("[навыки-визуал] навык %d: ПЛОЩАДЬ — эффектов удара у %d дополнительных целей: %d ")
		TEXT("(эталон CSkillCast_ApplyAoEHitEffects@0xA75EF0, до 20 целей)"),
		A.SkillId, Extra.Num(), N);
	ASDA_CTRACE(EAsdaCT::Fx, TEXT("навык %d: ПЛОЩАДЬ — эффекты удара у %d доп. целей, рождено %d"), A.SkillId, Extra.Num(), N);
	return N;
}

// ★★★ НОСИТЕЛЬ СНАРЯДА ФАЗЫ. `effect_spawnDirectedFromTo@0xA59B30` заполняет носитель 1880 б
//   (свой на каждую фазу): визуал полёта = `.jeb` подсекции 2·фаза+3, ЭФФЕКТ УДАРА = `.jeb`
//   подсекции 2·фаза+4, ЗВУК УДАРА = её `SOUND_FILE_NAME_A`, УРОН = поле `+1784`, старт =
//   трансляция матрицы кости оружия, цель — зафиксированная позиция, скорость `dist/2.5`.
//   Прилёт (`CEffect_UpdateSkillCast@0xA5A060`) спавнит эффект удара, играет звук и ТОЛЬКО ПОТОМ
//   показывает урон ([[weapon-projectiles]]: «урон едет В СНАРЯДЕ и показывается по прилёте»).
bool UAsdaSkillFxSubsystem::LaunchPhaseProjectile(FAsdaSkillFxActive& A, int32 Phase,
	const FAsdaSkillFxSpawn* ProjSpawn)
{
	AAsdaPlayerCharacter* C = A.Caster.Get();
	AActor* Tgt = A.Target.Get();
	UWorld* W = C ? C->GetWorld() : nullptr;
	if (!C || !W || !Tgt || !C->BaseSkel)
	{
		ASDA_CTRACE(EAsdaCT::HitFrame, TEXT("навык %d фаза %d: снаряд НЕ пущен — %s; удар фазы пойдёт ближним путём"),
			A.SkillId, Phase, !Tgt ? TEXT("цели нет (умерла/исчезла)") : TEXT("нет кастера/мира/скелета"));
		return false;
	}

	// ★ Кость СТАРТА НОСИТЕЛЯ — своя (см. `FAsdaSkillFxActive::ProjBone`): у 707 это «8_b1»,
	//   а «8_a1» из строки таблицы принадлежит эффекту КАСТА.
	const FName LaunchBone = A.ProjBone.IsNone() ? A.FxBone : A.ProjBone;
	const FVector From = (C->BaseSkel->GetBoneIndex(LaunchBone) != INDEX_NONE)
		? C->BaseSkel->GetBoneLocation(LaunchBone) : C->GetActorLocation();
	const FVector To = Tgt->GetActorLocation();
	AAsdaProjectileActor* P = W->SpawnActor<AAsdaProjectileActor>(
		AAsdaProjectileActor::StaticClass(), From, FRotator::ZeroRotator);
	if (!P) { return false; }

	// ★ У снаряда НАВЫКА меша нет — визуал только `.jeb` (в отличие от стрелы, у которой меш
	//   `ch1_n_00_arrs001_00_00_1`): `effect_spawnDirectedFromTo` грузит в носитель ТОЛЬКО эффект
	//   (`CEffect_LoadJEB_Wrapper`), модели там не заводится.
	if (P->ArrowMesh) { P->ArrowMesh->SetVisibility(false, true); }
	if (ProjSpawn)
	{
		// ★★★ ВИЗУАЛ ПОЛЁТА КРЕПИТСЯ `KeepWorldTransform`, А НЕ `KeepRelativeTransform`.
		//   СИМПТОМ (живой тест 2026-08-10): «снаряд не вылетает, либо его не видно» — при том что
		//   в логе носитель честно стартовал (`фаза 0/1/2: СНАРЯД …, 1582 uu, 0.63с`) и по прилёте
		//   отрабатывали и удар, и звук, и урон. Визуала не было ФИЗИЧЕСКИ: у `KeepRelativeTransform`
		//   UE (`USceneComponent::AttachToComponent`, ветка `EAttachmentRule::KeepRelative`) НЕ трогает
		//   `RelativeLocation`, а у только что заспавненного НЕПРИКРЕПЛЁННОГО актора относительная
		//   позиция РАВНА мировой. После прикрепления мир пересчитывается как «родитель ∘ относительная»
		//   ⇒ шлейф улетал в 2×`From`: при старте (−12321, 9721, 1357) это (−24643, 19443, 2714),
		//   за пределы видимости. Дальше `Launch()` ещё и разворачивал носителя
		//   (`SetActorRotation(FlightDir.Rotation())`), доворачивая этот огромный рычаг.
		//   Рабочий образец рядом: шлейф обычной СТРЕЛЫ (`AsdaWorldSubsystem::SpawnCharFx`, снаряд
		//   `eff_0_char_03_flyarr01-a_00`) крепится именно `KeepWorldTransform` — и он виден.
		P->TrailFx = AAsdaEffectActor::SpawnByJebName(W, ProjSpawn->Jeb, From, FRotator::ZeroRotator, P);
		if (AActor* T = P->TrailFx)
		{
			T->AttachToActor(P, FAttachmentTransformRules::KeepWorldTransform);
			// Шлейф — тот же эффект ПЕРСОНАЖА (`eff_1_char_*`), геометрия по канону (см. шапку файла).
			if (AAsdaEffectActor* TE = Cast<AAsdaEffectActor>(T)) { TE->bStrictQuadAttach = GAsdaSkillFxStrictQuad; }
		}
	}
	else
	{
		// ★ Сюда попасть больше нельзя: `FirePhase` пускает носитель ТОЛЬКО при наличии записи
		//   `@projectile`. Оставлено страховкой на случай ручного вызова — закон
		//   jeb-emitter-routing-hole: неучтённый случай обязан ПОПАСТЬ В ЛОГ.
		UE_LOG(LogAsdaSkillFx, Warning,
			TEXT("[навыки-визуал] навык %d фаза %d: носитель пущен БЕЗ имени .jeb полёта (тик %s)"),
			A.SkillId, Phase, *A.TickFn);
	}

	// По прилёте — эффект удара + звук удара у цели, затем УРОН (порядок эталона).
	// ⚠ `OnArrive` — одноадресный делегат и он ЗАНЯТ ВИЗУАЛОМ: урон подключён РАСШИРЕНИЕМ этой же
	//   точки (`OnFlightArrived`), а не вторым перехватом делегата.
	TWeakObjectPtr<UAsdaSkillFxSubsystem> WeakThis(this);
	TWeakObjectPtr<AAsdaProjectileActor> WeakProj(P);
	P->OnArrive.BindLambda([WeakThis, WeakProj]()
	{
		if (UAsdaSkillFxSubsystem* Self = WeakThis.Get()) { Self->OnFlightArrived(WeakProj.Get()); }
	});
	// ★ 05.11.2026 [ОРИГИНАЛ] граната — дугой (разбор у ключа asda.Skill.GrenadeArc): старт — позиция кастера +50, а не кость оружия.
	const bool bGrenadeArc = (A.TickFn == TEXT("CharSkill_UpdateGrenadeFx")) && CVarAsdaSkillGrenadeArc.GetValueOnGameThread() != 0;
	if (bGrenadeArc)
	{
		const FVector ArcFrom = C->GetActorLocation() + FVector(0.f, 0.f, kGrenadeStartUp);
		P->SetActorLocation(ArcFrom);
		if (P->TrailFx) { P->TrailFx->SetActorLocation(ArcFrom); }
		P->LaunchArc(ArcFrom, Tgt, To, kGrenadeFlightS, kGrenadeApexUp);
	}
	else
	{
		P->Launch(From, To, kSkillProjSpeedUnitsPerMs);
	}

	// Регистрируем полёт. ★ Записи 4102 этого навыка теперь паркуются В НЁМ (а не в показе) —
	//   именно так «урон едет в снаряде»: клип может доиграть раньше, чем носитель долетит.
	FAsdaSkillFxFlight F;
	F.Proj = P;
	F.Target = Tgt;
	F.DestLoc = To;                     // ★ зафиксированная точка прилёта (эталон: наведения нет)
	F.Caster = C;
	F.SkillId = A.SkillId;
	F.CasterUserIdx = A.CasterUserIdx;
	F.Phase = Phase;
	F.bHasHitFx = A.bPhaseHasHitFx[Phase];
	F.SndHit = A.SndHit;
	F.bTargetIsMob = A.bTargetIsMob;
	F.HitFx = PhaseHitList(A, Phase);
	F.bInlCarrier = (A.TickFn == TEXT("Char_UpdateSkillAnim_MultiPhase"));
	// Всё, что приехало для ЭТОЙ фазы, переезжает в носитель (у многоударного навыка каждая фаза
	//   везёт свою часть урона — см. SkillSplitsDamageIn3; у остальных запись «безфазная»).
	TakeHitsForPhase(A.PendingHits, Phase, F.Hits);
	Flights.Add(MoveTemp(F));

	UE_LOG(LogAsdaSkillFx, Log,
		TEXT("[навыки-визуал] навык %d фаза %d: СНАРЯД %s, старт кость %s %s, %.0f uu, %.2fс ")
		TEXT("(2.5 юн/мс) — удар, звук и УРОН по прилёте (записей 4102 в носителе: %d)"),
		A.SkillId, Phase, ProjSpawn ? *ProjSpawn->Jeb : TEXT("без визуала"),
		*LaunchBone.ToString(), *From.ToCompactString(),
		FVector::Dist(From, To),
		AAsdaProjectileActor::FlightTimeSeconds(FVector::Dist(From, To), kSkillProjSpeedUnitsPerMs),
		Flights.Last().Hits.Num());
	ASDA_CTRACE(EAsdaCT::HitFrame, TEXT("навык %d фаза %d: СНАРЯД пущен %s → %s, %.0f юн, прилёт через %.2f с; в носителе записей урона %d (урон по прилёте)"),
		A.SkillId, Phase, ProjSpawn ? *ProjSpawn->Jeb : TEXT("без визуала"), *Tgt->GetName(), FVector::Dist(From, To),
		AAsdaProjectileActor::FlightTimeSeconds(FVector::Dist(From, To), kSkillProjSpeedUnitsPerMs), Flights.Last().Hits.Num());
	return true;
}

// Прилёт носителя — эталонный `CEffect_UpdateSkillCast@0xA5A060`: эффект удара (`+8`), звук удара
//   (`+264`), и ТОЛЬКО ПОТОМ урон (`+1784` → `Combat_ShowDamageFeedback@0xA80390`).
void UAsdaSkillFxSubsystem::OnFlightArrived(AAsdaProjectileActor* P)
{
	const int32 Idx = Flights.IndexOfByPredicate(
		[P](const FAsdaSkillFxFlight& F) { return F.Proj.Get() == P; });
	if (Idx == INDEX_NONE) { return; }
	// ★ Снимаем полёт со списка СРАЗУ и работаем с копией: рассылка урона может привести к смерти
	//   цели, снятию таргета и гашению показа — держать ссылку внутрь `Flights` в этот момент нельзя.
	FAsdaSkillFxFlight F = MoveTemp(Flights[Idx]);
	Flights.RemoveAt(Idx);
	F.bArrived = true;

	// 1) Эффект удара и звук — у ЦЕЛИ. Ищем живой показ этого навыка; если клип уже доиграл,
	//    показа нет — собираем временную запись, чтобы `SpawnFx`/`PlayHitSound` отработали как есть.
	FAsdaSkillFxActive* Act = Active.FindByPredicate([&F](const FAsdaSkillFxActive& X)
		{ return X.SkillId == F.SkillId && X.Caster.Get() == F.Caster.Get(); });
	FAsdaSkillFxActive Tmp;
	if (!Act)
	{
		Tmp.Caster = F.Caster;
		Tmp.SkillId = F.SkillId;
		Tmp.Target = F.Target;
		Tmp.SndHit = F.SndHit;
		Tmp.bTargetIsMob = F.bTargetIsMob;
		Tmp.Hit0 = F.HitFx;
		Act = &Tmp;
	}
	// ★ Эффект удара по прилёте рождается В ТОЧКЕ ПРИЛЁТА носителя — она зафиксирована на выстреле
	//   (эталон `CEffect_UpdateSkillCast`: носитель сам стоит в этой точке). Если актор цели уже
	//   удалён (мгновенная смерть моба), спавн берёт её из LastTargetLoc — а не позицию кастера.
	if (!F.DestLoc.IsNearlyZero())
	{
		Act->LastTargetLoc = F.DestLoc;
		Act->bHasTargetLoc = true;
	}
	int32 NFx = 0;
	{
		const TArray<FAsdaSkillFxSpawn>& LAll = (Act == &Tmp) ? Tmp.Hit0
			: PhaseHitList(*Act, F.Phase);
		// ★ 04.11.2026 [ОРИГИНАЛ]: площадной удар («aoe») уже рождён на пороге фазы (FirePhase) — носитель везёт только свой (`+8`).
		const TArray<FAsdaSkillFxSpawn> L = LAll.FilterByPredicate([](const FAsdaSkillFxSpawn& S) { return S.TargetKind != FName(TEXT("aoe")); });
		TGuardValue<bool> InlArrival(bSpawnAtInlArrival, F.bInlCarrier);
		for (const FAsdaSkillFxSpawn& S : L)
		{
			if (S.Anchor == FName(TEXT("projectile"))) { continue; }
			if (SpawnFx(*Act, S, /*bImpact=*/true)) { ++NFx; }
		}
		// ★★★ И У КАЖДОЙ ОСТАЛЬНОЙ ЗАДЕТОЙ ЦЕЛИ (площадь) — см. CollectAoeTargets/SpawnAoeImpactFx.
		NFx += SpawnAoeImpactFx(*Act, L, F.Hits);
		PlayHitSound(*Act, F.Phase);
	}

	// 2) И ТОЛЬКО ПОТОМ — урон/цифра/HP.
	UE_LOG(LogAsdaSkillFx, Log,
		TEXT("[навыки-визуал] навык %d фаза %d: ПРИЛЁТ — эффектов удара %d, звук «%s», записей урона %d"),
		F.SkillId, F.Phase, NFx, *Act->SndHit, F.Hits.Num());
	ASDA_CTRACE(EAsdaCT::HitFrame, TEXT("навык %d фаза %d: ПРИЛЁТ снаряда (кастер %d) — цель %s, эффектов удара %d, звук «%s», записей урона %d → выпуск"),
		F.SkillId, F.Phase, F.CasterUserIdx, F.Target.IsValid() ? *F.Target->GetName() : TEXT("нет (точка прилёта)"), NFx, *Act->SndHit, F.Hits.Num());
	ReleaseHits(F.Hits, F.bHasHitFx, TEXT("прилёт снаряда"));
}

void UAsdaSkillFxSubsystem::FirePhase(FAsdaSkillFxActive& A, int32 Phase)
{
	AAsdaPlayerCharacter* C = A.Caster.Get();
	if (!C) { return; }
	const TArray<FAsdaSkillFxSpawn>& List = PhaseHitList(A, Phase);
	AActor* Tgt = A.Target.Get();

	// ★★★ СНАРЯД С ВИЗУАЛОМ ПОЛЁТА. Теперь `Proj` и запись `@projectile` СОГЛАСОВАНЫ: колонка
	//   гейтится данными (подсекция 3/5/7 `.inl` либо поле слота, из которого тик грузит `.jeb`
	//   полёта), а имя достаёт `_proj_fields` в пайплайне. Прежней ветки «`Proj == 1`, имени нет —
	//   пускаем невидимый носитель» больше нет: она била по 78 строкам, у которых снаряда НЕТ.
	const FAsdaSkillFxSpawn* Proj = List.FindByPredicate(
		[](const FAsdaSkillFxSpawn& S) { return S.Anchor == FName(TEXT("projectile")); });
	// ★ 04.11.2026 [ОРИГИНАЛ]: записи вида «aoe» — площадной удар `CSkillCast_ApplyAoEHitEffects@0xA75EF0` (подсекции 2/10 `.inl`).
	//   Тик зовёт его на пороге фазы 0 НЕЗАВИСИМО от носителя, поэтому у навыка со снарядом (820 «Меткий стрелок»: 2b подсекции 2 и 2b
	//   подсекции 4) удар рождается дважды — сейчас (площадь) и по прилёте (носитель). Без снаряда «aoe» спавнится вместе с остальными.
	const FName kAoe(TEXT("aoe"));
	bool bAoeDone = false;
	if (Proj && List.ContainsByPredicate([&kAoe](const FAsdaSkillFxSpawn& S) { return S.TargetKind == kAoe; }))
	{
		const TArray<FAsdaSkillFxSpawn> Aoe = List.FilterByPredicate([&kAoe](const FAsdaSkillFxSpawn& S) { return S.TargetKind == kAoe; });
		TArray<UAsdaNetSubsystem::FAsdaSkillHitRec> Peek;
		for (const UAsdaNetSubsystem::FAsdaSkillHitRec& R : A.PendingHits) { if (R.Phase < 0 || R.Phase == Phase) { Peek.Add(R); } }
		int32 NAoe = 0;
		for (const FAsdaSkillFxSpawn& S : Aoe) { if (SpawnFx(A, S, true)) { ++NAoe; } }
		NAoe += SpawnAoeImpactFx(A, Aoe, Peek);
		bAoeDone = true;
		ASDA_CTRACE(EAsdaCT::Fx, TEXT("навык %d фаза %d: площадной удар до вылета носителя — эффектов %d"), A.SkillId, Phase, NAoe);
	}
	if (Proj && LaunchPhaseProjectile(A, Phase, Proj))
	{
		return;                                     // урон, удар и звук доедут с носителем
	}
	if (A.bRowProj && !Proj)
	{
		// Закон jeb-emitter-routing-hole: не проглатываем. Данные говорят «снаряд есть», а имени
		// `.jeb` полёта в программе показа нет — такого в текущей таблице быть не должно.
		UE_LOG(LogAsdaSkillFx, Warning,
			TEXT("[навыки-визуал] навык %d фаза %d: колонка Proj=1, но записи @projectile в программе "
			     "показа нет — перезапусти Tools/asda_dump_skill_dispatch.py"), A.SkillId, Phase);
	}

	// Ближний бой: удар происходит ПРЯМО СЕЙЧАС.
	// ★ 10.11.2026 [ОРИГИНАЛ]: промах по основной цели у тика на помощнике `Skill_PlayHitEffectOn*_Snd` — бит попадания записи этой фазы.
	bool bPrimaryMiss = false;
	if (GAsdaSkillPrimaryMissNoHitFx != 0 && IsHitHelperTick(A.TickFn) && A.Target.IsValid())
	{
		UGameInstance* MGI = GetGameInstance();
		UAsdaWorldSubsystem* MWS = MGI ? MGI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		bool bFound = false;
		for (const UAsdaNetSubsystem::FAsdaSkillHitRec& H : A.PendingHits)
		{
			if (!MWS || H.TargetIdx < 0 || !(H.Phase < 0 || H.Phase == Phase)) { continue; }
			if (MWS->ActorByTarget(AsdaTargetKindFromHitRec(H.TargetKind), H.TargetIdx) == A.Target.Get()) { bPrimaryMiss = (H.HitFlags & 1) == 0; bFound = true; break; }
		}
		// ★ Ревизия самотеста (503/508/510 — два порога): запись основной цели ушла на фазе 0, а фаза 1 помощника читает бит той же
		//   записи (`pChar[1].gap_0[6]`) — берём запомненный флаг основной цели этого кастера (CastHitFlags, bMain, срок 10 с).
		if (!bFound && MWS)
		{
			const double NowS = FPlatformTime::Seconds();
			for (const TPair<int64, FCastHitFlag>& It : CastHitFlags)
			{
				if (!It.Value.bMain || (int32)(uint16)(It.Key >> 32) != (int32)(uint16)A.CasterUserIdx || NowS - It.Value.AtS > 10.0) { continue; }
				const int32 KKind = (int32)((It.Key >> 16) & 1), KIdx = (int32)(int16)(It.Key & 0xFFFF);
				if (MWS->ActorByTarget(AsdaTargetKindFromHitRec(KKind), KIdx) == A.Target.Get()) { bPrimaryMiss = (It.Value.HitFlags & 1) == 0; break; }
			}
		}
		if (bPrimaryMiss)
		{
			ASDA_CTRACE(EAsdaCT::HitFrame, TEXT("навык %d фаза %d: ПРОМАХ по основной цели (тик %s на помощнике удара) — без эффекта и звука удара, только MISS"),
				A.SkillId, Phase, *A.TickFn);
		}
	}
	TGuardValue<bool> MissGate(GAsdaSkillPrimaryMissGate, bPrimaryMiss);
	int32 NFx = 0;
	for (const FAsdaSkillFxSpawn& S : List)
	{
		if (S.Anchor == FName(TEXT("projectile"))) { continue; }
		if (bAoeDone && S.TargetKind == kAoe) { continue; }   // площадной удар уже рождён выше
		if (SpawnFx(A, S, true)) { ++NFx; }
	}
	// ★★★ ПЛОЩАДЬ: эффект удара рождается у КАЖДОЙ задетой цели (эталон крутит блок по 20 записям).
	//   Список берём из ещё не выпущенных записей этой фазы — они уйдут в `ReleaseHits` ниже.
	{
		TArray<UAsdaNetSubsystem::FAsdaSkillHitRec> Peek;
		for (const UAsdaNetSubsystem::FAsdaSkillHitRec& R : A.PendingHits)
		{
			if (R.Phase < 0 || R.Phase == Phase) { Peek.Add(R); }
		}
		NFx += SpawnAoeImpactFx(A, List, Peek);
	}
	PlayHitSound(A, Phase);
	UE_LOG(LogAsdaSkillFx, Log,
		TEXT("[навыки-визуал] навык %d ФАЗА %d (COMBO%d = %.0f мс, тик %s): эффектов удара %d, ")
		TEXT("звук «%s», цель %s, записей урона %d"),
		A.SkillId, Phase, Phase, A.PhaseMs[Phase], *A.TickFn, NFx, A.PhaseSnd[Phase].IsEmpty() ? *A.SndHit : *A.PhaseSnd[Phase],
		Tgt ? *Tgt->GetName() : TEXT("НЕТ (эталон в этой ветке эффект удара не спавнит)"),
		A.PendingHits.Num());
	ASDA_CTRACE(EAsdaCT::HitFrame, TEXT("КАДР УДАРА навыка %d (кастер %d) фаза %d: COMBO%d = %.0f мс клипа, прошло %.0f мс реального, тик %s; цель %s; эффектов удара %d, звук «%s»; записей урона ждут %d"),
		A.SkillId, A.CasterUserIdx, Phase, Phase, A.PhaseMs[Phase],
		C->GetWorld() ? (C->GetWorld()->GetTimeSeconds() - A.StartTimeS) * 1000.0 : 0.0, *A.TickFn,
		Tgt ? *Tgt->GetName() : TEXT("НЕТ"), NFx, *A.SndHit, A.PendingHits.Num());

	// ★★★ БЕСТЕЛЕСНЫЙ НОСИТЕЛЬ (`effectInitCollisionTrace@0xA596F0`, навыки 905/915). Эффекты уже
	//   отыграл сам тик (выше), а по трассе едет ТОЛЬКО УРОН — с той же скоростью `dist/2.5`.
	//   Прежде порт применял урон мгновенно: цифра появлялась раньше, чем «долетал» огненный шар.
	if (A.bRowProjDelay && Tgt && A.PendingHits.Num() > 0)
	{
		const FVector From = (C->BaseSkel && C->BaseSkel->GetBoneIndex(A.FxBone) != INDEX_NONE)
			? C->BaseSkel->GetBoneLocation(A.FxBone) : C->GetActorLocation();
		const float Dist = float(FVector::Dist(From, Tgt->GetActorLocation()));
		const float Sec = AAsdaProjectileActor::FlightTimeSeconds(Dist, kSkillProjSpeedUnitsPerMs);
		FAsdaSkillFxFlight F;
		F.bDelayOnly = true;
		F.Target = Tgt;
		F.DestLoc = Tgt->GetActorLocation();   // ★ точка трассы фиксируется на запуске
		F.Caster = C;
		F.SkillId = A.SkillId;
		F.CasterUserIdx = A.CasterUserIdx;
		F.Phase = Phase;
		F.bHasHitFx = A.bPhaseHasHitFx[Phase];
		F.bTargetIsMob = A.bTargetIsMob;
		F.ArriveAtS = C->GetWorld()->GetTimeSeconds() + Sec;
		// ★ ТОЛЬКО ЗАПИСИ СВОЕЙ ФАЗЫ. Прежде здесь забирались ВСЕ припаркованные записи разом
		//   (`MoveTemp(A.PendingHits)`), хотя две соседние точки (`:950` и ниже) уже пользуются
		//   `TakeHitsForPhase`. У многоударного навыка это сбивало порядок: первый же носитель
		//   уносил урон всех фаз, а следующим доставалось пусто. Для одиночного удара разницы нет,
		//   поэтому дефект и не бросался в глаза.
		TakeHitsForPhase(A.PendingHits, Phase, F.Hits);
		Flights.Add(MoveTemp(F));
		UE_LOG(LogAsdaSkillFx, Log,
			TEXT("[навыки-визуал] навык %d фаза %d: БЕСТЕЛЕСНЫЙ носитель (ProjDelay) — %.0f uu, "
			     "урон через %.2fс, визуал полёта у него отсутствует ПО УСТРОЙСТВУ"),
			A.SkillId, Phase, Dist, Sec);
		ASDA_CTRACE(EAsdaCT::HitFrame, TEXT("навык %d фаза %d: бестелесная трасса (ProjDelay) %.0f юн — урон через %.2f с, записей %d"),
			A.SkillId, Phase, Dist, Sec, Flights.Last().Hits.Num());
		return;
	}

	// ★★★ УРОН — ЗДЕСЬ ЖЕ, ПОСЛЕ ЭФФЕКТА И ЗВУКА. Эталон (`CSkillCast_UpdateMeleeHit@0xA64BB0`,
	//   `skill_buff.c:14225-14257`; `SkillEffect_UpdateScaledStrikeHit@0xA72610`,
	//   `effects_particles.c:10160-10225`) в ветке ПОПАДАНИЯ делает одним блоком: эффект удара →
	//   звук → `CClass_SubtractStatClamped` → текст урона; в ветке ПРОМАХА — тот же эффект и звук,
	//   а «MISS» рисуется ПОСЛЕ них. Именно этот порядок здесь и воспроизведён.
	{
		TArray<UAsdaNetSubsystem::FAsdaSkillHitRec> Mine;
		TakeHitsForPhase(A.PendingHits, Phase, Mine);
		// ★ 10.11.2026 (job4-vfx) [ОРИГИНАЛ]: записи с задержкой (`RecDelayMs`, см. SkillRecordWaves) тик эталона не применяет сам —
		//   он кладёт их отложенными записями (`packBuildEntryRecord`), и урон/эффект/звук наступают при выпуске записи. Группы по
		//   задержке → рамки `FAsdaSkillFxFlight` (bDelayOnly + bRecord) с моментом выпуска по времени мира (как ProjDelay).
		TMap<int32, FAsdaSkillFxFlight> ByDelay;   // задержка, мс (целые) → рамка записи
		for (int32 i = Mine.Num() - 1; i >= 0; --i)
		{
			if (Mine[i].RecDelayMs <= 0.f) { continue; }
			const int32 Ms = FMath::RoundToInt(Mine[i].RecDelayMs);
			FAsdaSkillFxFlight* F = ByDelay.Find(Ms);
			if (!F)
			{
				F = &ByDelay.Add(Ms);
				F->bDelayOnly = true;
				F->bRecord = true;
				F->Target = A.Target;
				F->Caster = A.Caster;
				F->SkillId = A.SkillId;
				F->CasterUserIdx = A.CasterUserIdx;
				F->Phase = Phase;
				F->bHasHitFx = A.bPhaseHasHitFx[Phase];
				F->SndHit = A.SndHit;
				F->bTargetIsMob = A.bTargetIsMob;
				F->DestLoc = A.Target.IsValid() ? A.Target->GetActorLocation() : FVector::ZeroVector;
				for (const FAsdaSkillFxSpawn& Sp : PhaseHitList(A, Phase)) { if (Sp.bRecord) { F->HitFx.Add(Sp); } }
				F->ArriveAtS = (C && C->GetWorld()) ? C->GetWorld()->GetTimeSeconds() + Ms / 1000.0 : 0.0;
			}
			F->Hits.Insert(Mine[i], 0);
			Mine.RemoveAt(i);
		}
		ByDelay.KeySort([](int32 L, int32 R) { return L < R; });   // по возрастанию задержки — порядок выпуска
		for (TPair<int32, FAsdaSkillFxFlight>& It : ByDelay)
		{
			ASDA_CTRACE(EAsdaCT::HitFrame, TEXT("навык %d фаза %d: ОТЛОЖЕННАЯ ЗАПИСЬ через %d мс — записей урона %d, эффектов записи %d"),
				A.SkillId, Phase, It.Key, It.Value.Hits.Num(), It.Value.HitFx.Num());
			if (C && C->GetWorld()) { DebugRecordArmTimes.Add(C->GetWorld()->GetTimeSeconds()); if (DebugRecordArmTimes.Num() > 64) { DebugRecordArmTimes.RemoveAt(0); } }
			Flights.Add(MoveTemp(It.Value));
		}
		ReleaseHits(Mine, A.bPhaseHasHitFx[Phase], TEXT("порог фазы"));
	}
}
