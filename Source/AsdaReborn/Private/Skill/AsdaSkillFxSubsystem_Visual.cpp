// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillFxSubsystem_Visual.cpp — ★ R3-c 12.11.2026 (разрез AsdaSkillFxSubsystem.cpp, дословный перенос): визуал навыка: PlaySkillVisual, StopSkillVisual, TickActive.
//   Общее для частей — в AsdaSkillFxSubsystem_Private.h; шапка и реверс — в AsdaSkillFxSubsystem.cpp.
#include "Skill/AsdaSkillFxSubsystem.h"
#include "AsdaNetSubsystem.h"   // ★ R1: прямое включение (раньше приходило транзитивно)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "AsdaFxClassCache.h"   // ★ R4, доля a: предзагрузка классов эффектов фаз удара

#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Npc/AsdaMobActor.h"
#include "Resources/AsdaModelAnimSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Skill/AsdaSkillClipRow.h"
#include "UI/AsdaSkillUseSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldSubsystem.h"
#include "AsdaSelf.h"   // ★ «это я?» одной точкой (E2, 14.09.2026)
#include "World/AsdaCombatTrace.h"   // ★ 18.10.2026: журнал боя — записи урона навыка, клип, кадр удара, снаряд, эффекты
#include "World/AsdaBlendAnimInstance.h"   // ★ 22.10.2026: плавность — клип навыка через инстанс со смешиванием
#include "UI/AsdaSkillDatabase.h"          // ★ 05.11.2026: behaviorType навыка — клетка 8215 (SendHitConfirm8215)
#include "World/AsdaPlayerController.h"    // ★ 29.10.2026 (Charge): сброс маршрута своего персонажа на время рывка
#include "AsdaSkillFxSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSkillFxSubsystem*.cpp

// ★ 29.10.2026 (баг игроков «Charge: эффекта нет, стан с задержкой, волк успевает ударить») — рывок 537/597, разбор у `TickDash`.
//   1 — как оригинал (`Char_UpdateDash@0xB3B010`): кастер едет к цели за COMBO0, по прибытии отброс цели, 8215, состояния своего удара,
//   4008 вид 15; 0 — прежний порт (клип на месте, записи 5257 выпускаются сразу).
static ASDA_DEV_CVAR(int32, CVarAsdaSkillDash, TEXT("asda.Skill.Dash"), 1,
	TEXT("Рывок навыков 537 Charge / 597 Mad Charge: 1 = как оригинал (ход к цели за 700 мс, отброс, стан на прибытии), 0 = прежний порт"),
	ECVF_Default);

// ★★★ 04.11.2026 [ОРИГИНАЛ] (жалоба владельца: у «Огненного шара» мага нет снаряда; аудит Tools/asda_skill_vfx_audit.py): тики, которые
//   запускают удар/снаряд по флагу «ответ сервера пришёл» (`gap_0[2] & 1` / `+104193 & 1`) БЕЗ сравнения с порогом COMBO, — у эталона
//   стреляют в первом же кадре показа: CEntity_PlayEffectWithBone@0xA630B0 (905) и ещё 10 тиков ниже (702/703/704/708/709/712/714/910/
//   914/915). SkillEffect_UpdateBowshot (710) и CharEffect_UpdateSkillTickAoE (713) ждут свой счётчик — время клипа ≥ 2500 мс. Порт
//   ждал Combo0Ms: у 905 порог 3233 мс длиннее клипа 2000 мс — фаза 0 не наступала, эффекта не было, урон выпускался концом показа.
//   Полоса применения по-прежнему COMBO0 (её читает TryGetCastBarMaxMs прямо из строки). Ключ: 1 — как эталон, 0 — прежний порт.
static ASDA_DEV_CVAR(int32, CVarAsdaSkillFlagTriggerTicks, TEXT("asda.Skill.FlagTriggerTicks"), 1,
	TEXT("[ОРИГИНАЛ] Фаза 0 у тиков «по флагу ответа»: 1 — первый кадр показа (Bowshot/TickAoE — 2500 мс клипа), как эталон; ")
	TEXT("0 — прежний порт (ждать Combo0Ms; у 905 «Огненный шар» фаза не наступала)."),
	ECVF_Default);

// ★★★ 06.11.2026 [ОРИГИНАЛ] (баг «часть навыков срабатывает до конца времени каста»): ЗАРЯД СОСТОЯНИЙ 13/27.
//   Эталон: ответ 5270 → состояние 14 (вход обнуляет счётчик, держится один кадр, entity_char.c:16950/23164) → 13/27: клип с начала и
//   колбэк cb2 навыка (`sub_92BAC0`, таблица 2 `CharSkillEffect_InitDispatchTable`) пишет заряд `field_59C` (+190160);
//   `char_updateChargeState@0xA162E0` убывает его на dt·animSpeed и только на нуле ставит 24, а тик показа (таблица 5) идёт ТОЛЬКО в 24.
//   Значит, у этих навыков удар/снаряд — в конце заряда, а не в первом кадре клипа. Заряд ≈ времени каста .sif +0x2D (его ждёт и сервер:
//   `CheckSkillCastingTime@0x47B480` → 8207/8208 → урон и 5271). `asda.Skill.FlagTriggerTicks` (04.11) считал «первый кадр показа» от 5270
//   и стрелял на 1.2–3.4 с раньше. Ответ 5257 заряда не имеет (`field_56C = 1` → сразу 24). Ключ: 1 — как эталон; 0 — прежний порт.
static ASDA_DEV_CVAR(int32, CVarAsdaSkillCastCharge, TEXT("asda.Skill.CastCharge"), 1,
	TEXT("[ОРИГИНАЛ] Заряд 13/27 навыков с кастом (ответ 5270): удар и снаряд не раньше конца заряда (702/703/704/708/709/711/712/714/910 — ")
	TEXT("2000 мс, 905 — 1200, 915 — 3360), как эталон. 0 = прежний порт (удар в первом кадре показа, раньше конца каста)."),
	ECVF_Default);

// Заряд навыка, мс: значения cb2 эталона — `setFloatPair100_300@0xA85070`, `sub_A854B0` (2000), `skillSpawnEndEffect@0xA850F0` (905: 1200),
//   `skillCastEffectAndSound@0xA852C0` (915: 3360), `sub_A85470` (914: 1500), `sub_A850B0` (701/707/903: 100). 0 — cb2 заряда не пишет.
static float AsdaCastChargeMs(int32 SkillId)
{
	switch (SkillId)
	{
	case 702: case 703: case 704: case 708: case 709: case 711: case 712: case 713: case 714: case 910: return 2000.f;
	case 914: return 1500.f;
	case 905: return 1200.f;
	case 915: return 3360.f;
	case 701: case 707: case 903: return 100.f;
	default: return 0.f;
	}
}

// ★ 06.11.2026 [ОРИГИНАЛ] (тот же ключ): ПОЯВЛЕНИЕ СНАРЯДА ДО ВЫЛЕТА. `char_updateChargeState` при остатке заряда ≤ `field_5AC` (+190176, пишет
//   тот же cb2) зовёт cb4 — `effectSpawnAtBoneItem@0xA85A90` и родственные: они только РОЖДАЮТ модель снаряда у кости (`effect_spawnDirectedInstance`);
//   вылет — `cEffectLaunchDirected` в первом кадре 24 (`CharSkill_UpdateSkillFx_Sound27Decal`, skill_buff.c:23044). Порог, мс; 0 — у навыка cb4 нет.
static float AsdaCastPreSpawnMs(int32 SkillId)
{
	switch (SkillId)
	{
	case 702: case 703: case 704: case 708: case 709: case 711: case 712: case 714: case 910: return 100.f;
	case 905: return 300.f;
	case 915: return 550.f;
	default: return 0.f;
	}
}

static TMap<TWeakObjectPtr<AActor>, FAsdaChargePre> GAsdaChargePre;   // кастер → ожидание (живёт от старта показа до вылета)

// Порог фазы 0 (клип-мс) с учётом тиков «по флагу ответа» — см. шапку ключа выше.
static float AsdaPhase0TriggerMs(const FString& TickFn, float Combo0Ms)
{
	// Граната: бросок на первом тике показа — разбор у ключа asda.Skill.GrenadeArc.
	if (TickFn == TEXT("CharSkill_UpdateGrenadeFx") && CVarAsdaSkillGrenadeArc.GetValueOnGameThread() != 0) { return 0.f; }
	if (CVarAsdaSkillFlagTriggerTicks.GetValueOnGameThread() == 0) { return Combo0Ms; }
	static const TCHAR* const kOnFlag[] = {
		TEXT("CEntity_PlayEffectWithBone"), TEXT("CharEffect_UpdateActiveSkill"), TEXT("CharSkill_UpdateBlastFx"),
		TEXT("CharSkill_UpdateBlastFx2"), TEXT("CharSkill_UpdateBlastoFx"), TEXT("CharSkill_UpdateGosharroFx"),
		TEXT("CharSkill_UpdateSkillFx_Sound11Drain"), TEXT("CharSkill_UpdateSkillFx_Sound22"), TEXT("CharSkill_UpdateSkillFx_Sound27Decal"),
		TEXT("CharSkill_UpdateTyphoonFx"), TEXT("SkillEffect_UpdateLightningBolt") };
	for (const TCHAR* K : kOnFlag) { if (TickFn == K) { return 0.f; } }
	if (TickFn == TEXT("SkillEffect_UpdateBowshot") || TickFn == TEXT("CharEffect_UpdateSkillTickAoE")) { return 2500.f; }
	return Combo0Ms;
}

// Белый список тиков (колонка TickFn `DT_SkillClips`) — разбор и происхождение в шапке ключа выше. Путь до показа урона:
static bool IsHitFrameTick(const FString& TickFn)
{
	static const TCHAR* const kTicks[] = {
		TEXT("Skill_TickSingleTargetHit"),          // → Combat_ShowDamageFeedback / CharFx_ShowSkillDamageText (skill_buff.c:15378)
		TEXT("CharSkill_UpdateTornadoFx"),          // → CharFx_ShowSkillDamageText (skill_buff.c:15992)
		TEXT("CharSkill_UpdateValkyrieFx"),         // → Skill_PlayHitEffectOnEntity_Snd → CharFx_ShowSkillDamageText (skill_buff.c:22601)
		TEXT("charFx_dragonFlySkill_update"),       // → Skill_PlayHitEffectOnEntity_Snd → … (effects_particles.c:25465)
		TEXT("CharSkill_UpdateSkillFx_Variant1"),   // → Skill_PlayHitEffectOnEntity_Snd / charFx_ApplySkillHitsToTargets (skill_buff.c:22459)
		TEXT("charSkill_updateMageDeabolFx"),       // → CharFx_ApplySkillHitsToTargets → CharFx_ShowSkillDamageText (skill_buff.c:22324)
		TEXT("CharFx_CycleCutscene_Update"),        // → CharFx_ApplySkillHitsToTargets → … (effects_particles.c:25604)
		TEXT("Char_UpdateSkillAnim_MultiPhase"),    // → CSkillCast_ApplyAoEHitEffects → Combat_ShowDamageFeedback (entity_char.c:29542)
	};
	for (const TCHAR* T : kTicks) { if (TickFn.Equals(T, ESearchCase::CaseSensitive)) { return true; } }
	return false;
}

// Четвёртый порог удара (combo time.bin +84[3] × 1000/30, мс) по ключу клипа. Таблица снята скриптом
//   `Reports/batch17/starlight/scan_multihit.py` (он же сверяет её с файлом): среди всех строк DT_SkillClips четвёртый порог
//   ненулевой только у этих шести клипов. DT_SkillClips хранит три порога — колонку не добавляем (переимпорт таблицы), берём отсюда.
static float AsdaCombo3Ms(const FString& ComboKey)
{
	static const TCHAR* const kKeys[] = {
		TEXT("ch4_m_01_sq-hexa-s_02"), TEXT("ch4_m_01_sq-hexa-m_02"), TEXT("ch4_m_01_sq-hexa-l_02"),
		TEXT("ch4_f_01_sq-hexa-s_02"), TEXT("ch4_f_01_sq-hexa-m_02"), TEXT("ch4_f_01_sq-hexa-l_02"),
	};
	for (const TCHAR* K : kKeys) { if (ComboKey.Equals(K, ESearchCase::IgnoreCase)) { return 2500.f; } }   // 75 кадров × 1000/30
	return 0.f;
}

namespace {
	// Корень скелетальных ассетов персонажа (там же лежат anim_00..85 и наши anim_100..).
	const TCHAR* kCharRoot = AsdaAssetPaths::kSkelRootCharacters;   // ★ волна 2 п. 7: корень — реестр путей

// ★★★ МАСШТАБ ЭФФЕКТА УДАРА ПО ЦЕЛИ — ИСПРАВЛЕНО 2026-08-10 (аудит масштабов).
//   Эталон: `CEffectMgr_scaleAll(fx, sub_936A29(цель) / 100.0)`, где `sub_936A29` = `get_o730`
//   (`entity_char.c:495`, `return *(this + 1840)` = поле сущности **+7360**) — ПРОЦЕНТНЫЙ
//   размер сущности: 100 ⇒ множитель 1.0. Писателя этого поля в срезе НЕТ (проверено по
//   эталону и по мастер-декомпилу — закон dead-code-markings-suspect: «писателей в срезе нет»),
//   то есть у обычных целей множитель = 1.0.
//   ★ БЫЛО (баг): множителем брался `Target->GetActorScale3D()`. Но в порту масштаб актора —
//   это НЕ «процент размера сущности», а КОНВЕРСИЯ ЕДИНИЦ импорта: мобы спавнятся со скейлом
//   **0.01** (`AsdaWorldSubsystem.cpp`: `FTransform SpawnXf(Rot, …, FVector(0.01f))`, тот же
//   скейл у запечённых шаблонов). Итог: КАЖДЫЙ эффект удара, рождённый у цели (по разбору
//   §13.2 это `world:target` 172 + `matrix:hitpoint` 38 + `matrix:target` 32 + `world:hitpoint` 8
//   = 250 записей из 254), получал `scaleAll(0.01)` — геометрия квадов/колец схлопывалась в
//   точку, а РАЗМЕР ЧАСТИЦ (`PScale`, закон effects-smoke-wave «частица НИКОГДА не ×s») не
//   менялся вовсе ⇒ от эффекта оставался только несоразмерно КРУПНЫЙ спрайтовый флеш.
//   ★★★ 2026-08-11 ПОЛЕ НАЙДЕНО И ПОДКЛЮЧЕНО — см. `UAsdaSkillFxSubsystem::GetTargetSizePercent`
//   (писатели `+7360`: конструктор `gameObjCtorInit@0xF810B0+0x28D` кладёт 100, загрузчик моба
//   `mobEffect_LoadArchiveStep@0xF81B90+0xF0C` — `Mob list.BIN +0x108`). Свободной функции
//   `HitFxScaleOf` здесь больше нет: множитель берётся у подсистемы, потому что источник —
//   таблица `DT_MobStats`, а не свойство актора.
	// Путь ассета клипа: <root>/ch1_{g}_cremmy_preview/SkeletalMeshes/ch1_{g}_cremmy_previewanim_NN.
	FString ClipAssetPath(uint8 Sex, int32 AnimIdx)
	{
		// ★ ИСПРАВЛЕНИЕ D3 (волна 2 п. 7, 14.09.2026): было «anim_%d» — единственная копия без ведущего нуля (клипы на диске
		//   anim_00…anim_09, все прочие места — anim_%02d). Для AnimIdx >= 10 строка та же (в SkillClips.csv 209 из 209 строк
		//   с AnimIdx >= 10 — на текущих данных поведение не меняется); для 0…9 теперь находится ассет.
		return AsdaAssetPaths::SkelAnim(kCharRoot, AsdaAssetPaths::CharPreviewName(Sex != 2), AnimIdx);
	}
}

bool UAsdaSkillFxSubsystem::IsSkillVisualActive(const AAsdaPlayerCharacter* Caster) const
{
	for (const FAsdaSkillFxActive& A : Active)
	{
		if (A.Caster.Get() == Caster) { return true; }
	}
	return false;
}

bool UAsdaSkillFxSubsystem::PlaySkillVisual(AAsdaPlayerCharacter* Caster, int32 SkillId, int32 CasterUserIdx)
{
	if (!Caster || !Caster->BaseSkel) { return false; }
	UWorld* W = Caster->GetWorld();
	if (!W) { return false; }

	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const UAsdaNetSubsystem* NetS = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	// ★★★ КТО КАСТУЕТ. Индекс приходит из пакета; −1 (консоль, ручной вызов) восстанавливаем
	//   обратным поиском по актору — единственным в проекте (`FindUserIdxByActor`, свой павн тоже).
	int32 CasterIdx = CasterUserIdx;
	if (CasterIdx < 0 && WS) { CasterIdx = WS->FindUserIdxByActor(Caster); }
	const bool bOurs = AsdaSelf::IsSelfUserIdx(NetS, CasterIdx);   // E2

	// ★★★ ЦЕЛЬ РАЗБИРАЕТСЯ ДО ВЫБОРА СТРОКИ: у части навыков (напр. 513 `SkillEffect_SetupHexa`)
	//   и КЛИП, и `.jeb` зависят от РАЗМЕРНОГО КЛАССА ЦЕЛИ (`сущность + 7356`), а не от оружия.
	// ★★★ ИСТОЧНИК ЦЕЛИ — ЗАПИСИ ПАКЕТА, А НЕ ВЫБОР UI. Эталон пишет `pChar->targetRenderId` из
	//   `targetWords[0]` (поле `+1` первой 29-байтной записи), и это ЕДИНСТВЕННЫЙ источник цели
	//   показа, одинаковый у кастера и у наблюдателя. Выбранная в UI цель — ЗАПАСНОЙ путь и только
	//   для СВОЕГО каста: у навыков без записей (само-бафф, тогл) сервер целей не шлёт вовсе, а у
	//   соседа наша выбранная цель — посторонний актор.
	bool bTargetIsMob = false;
	AActor* TargetActor = nullptr;
	const TCHAR* TargetSrc = TEXT("записей нет");
	{
		int32 RKind = -1, RIdx = -1;
		if (FindCastRecTarget(CasterIdx, SkillId, RKind, RIdx) && WS)
		{
			// ★ Аудит №3 (B6): `RKind` пришёл из `FindCastRecTarget`, а тот отдаёт СЫРОЙ `rec[0]`
			//   29-байтной записи (1 = ПЕРСОНАЖ → userNum, иначе СУЩНОСТЬ → renderId; эталон
			//   `skillParseQuickSlotEntry@0xA869F0`). Перевод в вид — на этом месте, поиск — единой точкой.
			const EAsdaTargetKind RKindEnum = AsdaTargetKindFromHitRec(RKind);
			bTargetIsMob = (RKindEnum == EAsdaTargetKind::Entity);
			TargetActor = WS->ActorByTarget(RKindEnum, RIdx);
			TargetSrc = TEXT("запись пакета");
			if (!TargetActor)
			{
				// Эталон в этом случае просто не выполняет ветку удара по этой цели — курс и показ
				//   при этом ставятся. Пишем в лог (закон jeb-emitter-routing-hole), показ не рвём.
				UE_LOG(LogAsdaSkillFx, Log,
					TEXT("[навыки-визуал] навык %d, кастер uidx=%d: цель записи (kind=%d idx=%d) в мире ")
					TEXT("не найдена — показ идёт без цели"), SkillId, CasterIdx, RKind, RIdx);
			}
		}
		else if (bOurs)
		{
			TargetActor = ResolveTarget(bTargetIsMob);
			TargetSrc = TEXT("выбор UI (свой каст)");
		}
	}
	const int32 SizeClass = GetTargetSizeClass(TargetActor);

	FString Why;
	const FAsdaSkillClipRow* R = ResolveRow(SkillId, Caster->Sex, Caster->EquippedWeaponSubType, &Why, SizeClass);
	if (!R)
	{
		UE_LOG(LogAsdaSkillFx, Warning, TEXT("[навыки-визуал] навык %d: %s"), SkillId, *Why);
		ASDA_CTRACE(EAsdaCT::Swing, TEXT("навык %d (кастер %d): клипа НЕТ — %s"), SkillId, CasterIdx, *Why);
		return false;
	}
	if (R->AnimIdx < 0)
	{
		UE_LOG(LogAsdaSkillFx, Warning,
			TEXT("[навыки-визуал] навык %d (%s): клип %s НЕ ИМПОРТИРОВАН — запусти "
			     "Tools/export_char_skillclips.py + Tools/import_char_skillclips.py"),
			SkillId, *R->SkillName, *R->Clip);
		ASDA_CTRACE(EAsdaCT::Swing, TEXT("навык %d (кастер %d): клип %s НЕ импортирован — показа нет"), SkillId, CasterIdx, *R->Clip);
		return false;
	}

	// ★ Разворот к цели навыка взводит мир-подсистема ещё в кадре пакета 5257/5270
	//   (`OnSkillCastFaceTarget`) — то есть ДО этого вызова. `StopSkillVisual` ниже гасит его как
	//   «конец состояния 24», поэтому признак снимаем ЗАРАНЕЕ и восстанавливаем точным сроком
	//   (конец клипа применения) сразу после его вычисления.
	//   ⚠ Гейт клипа (`SkillAnimUntilS`) снимается тем же `StopSkillVisual` и взводится ниже.
	const bool bFaceArmed = Caster->IsSkillFaceActive();
	StopSkillVisual(Caster);   // повторное применение перезапускает показ (эталон: слот один)

	// 1) КЛИП ПРИМЕНЕНИЯ — отдельный анимобъект, набор оружия не переписывается (§7.2).
	//    ★ Пол берётся С САМОГО КАСТЕРА (`Caster->Sex`), поэтому у соседа играет ЕГО набор клипов:
	//      и `Sex`, и `EquippedWeaponSubType` выставляет сборка внешности соседа
	//      (`RebuildAppearance` → `SetWeaponFromItem`), то есть `ResolveRow` даёт ту же строку
	//      `DT_SkillClips`, что и на клиенте самого кастера.
	// ★★★ МНОЖИТЕЛЬ ТЕМПА АНИМАЦИЙ НАВЫКОВ (`animSpeedScale`, `CCharacter + 0x500`). Снимается ОДИН
	//   раз на старте показа и дальше живёт в записи: эталон тоже пересчитывает поле только при
	//   смене экипировки, а тик навыка читает готовое. Разбор потока — у `GetAnimSpeedScale`.
	const float AnimScale = FMath::Max(0.1f, Caster->AnimSpeedScaleMul());

	float Len = 0.f;
	if (UAnimSequence* Clip = UAsdaModelAnimSubsystem::LoadClipChecked(ClipAssetPath(Caster->Sex, R->AnimIdx)))
	{
		// ★ Волна В5 (рыбалка, 08.10.2026): состояние каста (24) перебивает действие рыбалки 60…67 — снять его признак,
		//   иначе по концу своего таймера клип рыбалки встал бы поверх клипа навыка.
		if (Caster->IsFishActionActive()) { Caster->StopFishAction(TEXT("навык")); }
		// ★ 30.10.2026: и позы 47/51 CampWar/дуэли (`CChar_SetState(…, 24)` переписывает `+47551`).
		if (const int32 Cp = Caster->GetCampPoseState()) { Caster->LeaveCampPose(Cp, TEXT("навык")); }
		// ★ 24.10.2026 (выход из сидения 1:1): состояние 24 так же переписывает 7/8/9 (`CChar_SetState`) — без «встаёт»; иначе таймер
		//   фазы сидения перематывал бы клип навыка, а по концу навыка персонаж оставался бы «сидящим».
		Caster->DropSitForEtalonState(/*bIdleClip*/ false, TEXT("навык 24"));
		// ★★★ 17.10.2026 (ревизия Ф2, ВАЖНО 3) [ОРИГИНАЛ]: навык СОСЕДА посреди серии обычной атаки — эталон переводит его в состояния
		//   24/25, тик атаки стоит. У порта цикл замаха соседа (Ф2 — отрезки от пакетов, их таймеры `BeginPeerSegment` ставили
		//   `SetPlayRate` уже на клипе навыка, а конец прохода `OnAttackClipEnd` перебивал навык клипом атаки до «тишины») жил дальше.
		//   Снимаем цикл тем же выходом, что у своего навыка (`SuspendAttackLoopForSkill`: цикл, отрезки, «тишина»); следующий
		//   4029/4204 соседа начнёт замах «взводом». Свой персонаж снимает цикл заявкой навыка (`NoteSkillStart`) — его не трогаем.
		if (!Caster->IsLocalPlayerCharacter() && (Caster->IsAttackLoopAlive() || Caster->IsPeerPacketDriven()))
		{
			Caster->SuspendAttackLoopForSkill();
		}
		// ★ 22.10.2026 (плавность, `World/AsdaBlendAnimInstance.h`): вход в клип навыка — короткий бленд эталона
		//   (`asda.Anim.BlendAttackSec`, [ОРИГИНАЛ] 60 мс); штатный `PlayAnimation` здесь сбросил бы инстанс со смешиванием.
		AsdaAnimSmooth::PlayClip(Caster->BaseSkel, Clip, false, AsdaAnimSmooth::BlendAttackSec());
		// ★★★ 16.09.2026 (ревизия темпа атаки, СРЕДН): скорость воспроизведения живёт на КОМПОНЕНТЕ,
		//   а не на клипе. Клипы атаки играют на скорости темпа (`PlayWorldClip`), и без явного сброса
		//   она ПРОТЕКАЛА СЮДА: клип навыка играл в темпе предыдущего замаха, а `Len` берётся в
		//   клип-времени — гейты и таймеры показа переживали картинку. Это единственный путь мимо
		//   `PlayWorldClip`, поэтому сброс нужен именно здесь.
		// ★★★ 16.09.2026 (вторая половина системы темпа): скорость клипа НАВЫКА = `animSpeedScale`.
		//   Эталон гонит часы анимобъекта навыка шагом `dt * animSpeedScale`
		//   (`charFxAutoSkillTick@0xAB6570`, game.c:138717/138734 — и так в ~100 тиках навыков),
		//   то есть при множителе 1.2 клип применения физически идёт на 20 % быстрее.
		//   ⚠ Ровно 1.0 ставить всё равно НУЖНО было и раньше: скорость живёт на КОМПОНЕНТЕ, и без
		//   явной установки сюда протекала скорость темпа предыдущего замаха (`PlayWorldClip`).
		Caster->BaseSkel->SetPlayRate(AnimScale);
		// ★ Длина клипа — в КЛИП-ВРЕМЕНИ. Ниже она делится на множитель, потому что все сроки показа
		//   (`EndTimeS`, гейты, время жизни эффектов) отмеряются РЕАЛЬНЫМ временем мира.
		Len = Clip->GetPlayLength();
	}
	else
	{
		UE_LOG(LogAsdaSkillFx, Warning, TEXT("[навыки-визуал] ассет клипа не загрузился: %s"),
			*ClipAssetPath(Caster->Sex, R->AnimIdx));
		ASDA_CTRACE(EAsdaCT::Swing, TEXT("навык %d (кастер %d): ассет клипа anim_%d не загрузился — показа нет"), SkillId, CasterIdx, R->AnimIdx);
		return false;
	}

	FAsdaSkillFxActive A;
	A.Caster = Caster;
	A.SkillId = SkillId;
	A.Clip = R->Clip;
	A.AnimIdx = R->AnimIdx;
	A.AnimScale = AnimScale;
	A.StartTimeS = W->GetTimeSeconds();
	// ★★★ РЕАЛЬНАЯ длительность показа = длина клипа / множитель темпа анимаций. Клип играет со
	//   скоростью `AnimScale`, поэтому его `GetPlayLength()` (клип-секунды) проходит быстрее.
	//   Смешивать клип-время с реальным нельзя — тот же закон, что и у замаха
	//   (`AAsdaPlayerCharacter::GetAttackSwingElapsedSec`).
	A.EndTimeS = A.StartTimeS + Len / AnimScale;
	// ★ Точный срок эталонного состояния 24 = конец клипа применения (тик 24: `obj[13] >= obj[11]`).
	//   Сторож 2 с, поставленный в кадре пакета, заменяется на него.
	if (bFaceArmed) { Caster->SkillFaceUntilS = A.EndTimeS; }
	// ★★★ ГЕЙТ КЛИПА ПРИМЕНЕНИЯ. У эталона клип каста живёт в ОТДЕЛЬНОМ анимобъекте, а сам персонаж
	//   стоит в состоянии 24 — то есть НЕ в состоянии 0, и покадровая вариативность покоя (фиджет
	//   idle03, ветка состояния 0) до него не доходит по устройству. В порту клип один на скелет,
	//   поэтому «состояние 24 идёт» держим временем: `IsSkillAnimActive()` запирает подмену клипа
	//   ровно так же, как `IsAttackAnimActive()` запирает её на время замаха. Без этого фиджет
	//   покоя (срабатывает через 6..12 с простоя) обрывал клип навыка на полкадра — и у СВОЕГО
	//   персонажа тоже, но у соседа это стало заметно сразу: он в покое почти всегда.
	Caster->SkillAnimUntilS = A.EndTimeS;
	// ★ Кость эффектов — «кончик оружия»/«тетива» по подтипу оружия (эталонный attach 7, кость даёт
	//   ПОЗИЦИЮ). Развилка — см. FxBoneFor выше.
	A.FxBone = FName(FxBoneFor(Caster->EquippedWeaponSubType));
	// ★ Кость СТАРТА СНАРЯДА фиксируется ПРАВИЛОМ ОРУЖИЯ и строкой таблицы НЕ перебивается
	//   (`Char_UpdateSkillAnim_MultiPhase@0xA76E70`; у 707 это «8_b1» против «8_a1» у каста).
	A.ProjBone = A.FxBone;
	// ★★★ КОСТЬ-ИЗ-ТИКА (хвост §13.7 п.1 skill_fx_anim.md ЗАКРЫТ 2026-08-10). Привязка
	//   «какой boneIdx попал в какой вызов» РАЗОБРАНА по телам тиков (не угадана):
	//     · тики КАСТА с одной костью — `charSkill_updateMageDeabolFx` (507, "8"),
	//       `charFx_dragonFlySkill_update` (508, "8_End"), `CharSkill_UpdateValkyrieFx` (512,
	//       "8_End"), `CharSkill_UpdateSkillFx_Variant1` (515, "8_a1"): единственный
	//       `List_FindIndexByName` стоит прямо перед `Effect_SpawnByName`, и его boneIdx уходит
	//       В ЭТОТ вызов (skill_buff.c:22380/22657, effects_particles.c:25522);
	//     · `CCharacter_UpdateSkillEffectTick` (707): ПЕРВЫЙ спавн (эффект каста) берёт "8_a1"
	//       (`boneIndex8a1`, entity_char.c, поиск СРАЗУ перед спавном), а "8_b1" идёт только в
	//       СЛЕДУЮЩИЕ фазы (старт носителя) — т.е. каст-эффект у 707 живёт на "8_a1";
	//     · тики УДАРА `CSkillCast_UpdateRangedHit`/`_UpdateMeleeHit`/`Update_Skill0x388`/`0x390`
	//       (901/909/904/912): единственная кость "_8" из списка МОДЕЛИ персонажа уходит в
	//       bone-анкерный спавн эффекта (`Effect_SpawnByName(..., boneIdx, 7, ...)`).
	//   У 902/911/917 в теле тика поиска кости НЕТ ВООБЩЕ (спавн по позиции) — их значение колонки
	//   является посторонним литералом; защита ниже (кости нет на скелете → правило оружия) не даёт
	//   ему навредить, а факт попадает в лог.
	if (!R->FxCastBone.IsEmpty())
	{
		const FName RowBone(*R->FxCastBone);
		if (Caster->BaseSkel && Caster->BaseSkel->GetBoneIndex(RowBone) != INDEX_NONE)
		{
			A.FxBone = RowBone;
		}
		else
		{
			UE_LOG(LogAsdaSkillFx, Log,
				TEXT("[навыки-визуал] навык %d: кость тика «%s» на скелете НЕ найдена — остаётся ")
				TEXT("правило оружия (%s)"), SkillId, *R->FxCastBone, *A.FxBone.ToString());
		}
	}
	// ★★★ ЦЕЛЬ ФИКСИРУЕТСЯ ЗДЕСЬ И БОЛЬШЕ НЕ ОТСЛЕЖИВАЕТСЯ — эталон запоминает `targetRenderId`
	//   в слоте на старте показа, а снаряд считает направление ровно один раз (наведения нет).
	A.Target = TargetActor;
	A.bTargetIsMob = bTargetIsMob;
	// ★ Позиция цели снимается на старте (и освежается тиком, пока актор жив) — ею пользуется спавн
	//   удара, когда цель уже умерла и удалена (см. комментарий у SpawnFx).
	if (TargetActor)
	{
		A.LastTargetLoc = TargetActor->GetActorLocation();
		A.bHasTargetLoc = true;
	}
	// Пороги фаз и их наполнение.
	A.PhaseMs[0] = AsdaPhase0TriggerMs(R->TickFn, R->Combo0Ms);   // ★ 04.11.2026: тики «по флагу ответа» — разбор у функции
	// ★ 06.11.2026 [ОРИГИНАЛ] (`asda.Skill.CastCharge`): показ от 5270 — тик не раньше конца заряда 13/27. Заряд идёт тем же множителем
	//   скорости, что и клип, поэтому порог — в клип-мс. Не дальше конца клипа (иначе фаза не наступит, урон уйдёт концом показа).
	if (GAsdaShowFromCastIdle && CVarAsdaSkillCastCharge.GetValueOnGameThread() != 0)
	{
		const float ChargeMs = FMath::Min(AsdaCastChargeMs(SkillId), Len * 1000.f - 1.f);
		if (ChargeMs > A.PhaseMs[0])
		{
			UE_LOG(LogAsdaSkillFx, Log, TEXT("[навыки-заряд] навык %d: удар не раньше конца заряда %.0f мс (порог был %.0f)"), SkillId, ChargeMs, A.PhaseMs[0]);
			A.PhaseMs[0] = ChargeMs;
		}
		// Порог cb4 — и у навыков, чей порог фазы уже равен заряду (711: COMBO0 = 2000).
		if (const float PreMs = AsdaCastPreSpawnMs(SkillId); PreMs > 0.f && ChargeMs > PreMs)
		{
			FAsdaChargePre Pre; Pre.SkillId = SkillId; Pre.AtMs = ChargeMs - PreMs;
			GAsdaChargePre.Add(TWeakObjectPtr<AActor>(Caster), Pre);
		}
	}
	A.PhaseMs[1] = R->Combo1Ms;
	A.PhaseMs[2] = R->Combo2Ms;
	ParseFxList(R->FxHit0, A.Hit0);
	ParseFxList(R->FxHit1, A.Hit1);
	ParseFxList(R->FxHit2, A.Hit2);
	A.SndHit = R->SndHit;
	// ★ 08.11.2026 [ОРИГИНАЛ] (`asda.Skill.HexaFourHits`, разбор у ключа): 513 — четвёртый порог и программа фаз тика `CSkillEffect_Update`.
	const bool bHexa4 = IsHexaFourHitTick(R->TickFn) && AsdaCombo3Ms(R->ComboKey) > 0.f;
	FString HexaCastJeb, HexaHitJeb;   // поля слота 0x68 / 0x168 строки клипа (колонки Jebs/JebSlots — позиция в позицию)
	if (bHexa4)
	{
		TArray<FString> Jebs, Slots;
		R->Jebs.ParseIntoArray(Jebs, TEXT(";"), true);
		R->JebSlots.ParseIntoArray(Slots, TEXT(";"), true);
		for (int32 j = 0; j < Jebs.Num() && j < Slots.Num(); ++j)
		{
			if (Slots[j].Equals(TEXT("0x68"), ESearchCase::IgnoreCase)) { HexaCastJeb = Jebs[j]; }
			if (Slots[j].Equals(TEXT("0x168"), ESearchCase::IgnoreCase)) { HexaHitJeb = Jebs[j]; }
		}
		A.PhaseMs[3] = AsdaCombo3Ms(R->ComboKey);
		// Фазы 0..2: `field_168` в точке луча; фаза 3: литерал hexab03 у цели (тело тика, разбор у ключа).
		const FString HitSpec = HexaHitJeb.IsEmpty() ? FString() : HexaHitJeb + TEXT("@world:hitpoint");
		ParseFxList(HitSpec, A.Hit0);
		ParseFxList(HitSpec, A.Hit1);
		ParseFxList(HitSpec, A.Hit2);
		ParseFxList(TEXT("eff_1_char_01_hexab03-a_00.jeb@world:target"), A.Hit3);
		for (int32 Ph = 0; Ph < 3; ++Ph) { A.PhaseSnd[Ph] = TEXT("S_c_F3hexsla02.wav;S_c_F3hexsla01.wav"); }
		A.PhaseSnd[3] = TEXT("S_c_F3hexsla03.wav");
	}
	// ★★★ ПОЧЕМУ ФАЗЫ 1 и 2 ДОПОЛНИТЕЛЬНО ГЕЙТЯТСЯ ПО `COMBO > 0`.
	//   Эффекты удара живут в ОБЩЕМ помощнике (`CSkillCast_ApplyEntityHitEffect@0xA7B4A0`), у
	//   которого внутри есть ветки ВСЕХ фаз. Но сколько фаз тик реально доходит — решают ДАННЫЕ:
	//   `charFx_autoSkill501Tick@0xAB54B0` зовёт помощника только при `!field_3A0` (фаза 0), а
	//   счётчик фазы растёт лишь когда `COMBO[фаза] < прогресс`. У навыка 501 COMBO1 = COMBO2 = 0
	//   (в `combo time.bin` второго удара нет) — значит ветка `+0x168` помощника из 501 НЕДОСТИЖИМА.
	//   Поэтому: фаза без порога в данных = фазы нет. Иначе разбор общего помощника «дарил» бы
	//   навыку лишний удар, которого в оригинале не происходит.
	//   ★ 31.10.2026 [ИСПРАВЛЕНИЕ] фаза 0 с порогом COMBO0 > 0 у тиков с показом урона по порогу — урон ждёт её (`asda.Skill.HitFrameAlways`).
	A.bPhaseUsed[0] = A.Hit0.Num() > 0 || !A.SndHit.IsEmpty() || (GAsdaSkillHitFrameAlways != 0 && A.PhaseMs[0] > 0.f && IsHitFrameTick(R->TickFn));
	// ★ 10.11.2026 (job4-vfx): навык с отложенными записями (907, 911 — SkillRecordWaves) строит их на пороге COMBO0
	//   (`field_3A0 == 0` / `+47781 == 0` тиков эталона) — фаза 0 обязана быть, иначе записи выпускались бы сразу при приёме.
	{
		TArray<TPair<float, int32>> RecW;
		if (A.PhaseMs[0] > 0.f && SkillRecordWaves(A.SkillId, 0, RecW)) { A.bPhaseUsed[0] = true; }
	}
	A.bPhaseUsed[1] = A.Hit1.Num() > 0 && A.PhaseMs[1] > 0.f;
	A.bPhaseUsed[2] = A.Hit2.Num() > 0 && A.PhaseMs[2] > 0.f;
	// ★ 05.11.2026 [ОРИГИНАЛ] (`asda.Skill.HitRecordPerPhase`): у тика с показом урона по номеру фазы каждый порог COMBO > 0 —
	//   удар (эталон сравнивает прогресс с `COMBO[field_3A0]` и на нём берёт урон записи `field_3A0`), даже если эффекта удара
	//   у фазы в строке клипа нет (так было у 508 до 10.11.2026, когда FxHit1 был пуст; теперь там drgflyb03 — правило нужно прочим тикам).
	if (GAsdaSkillHitRecordPerPhase != 0 && AsdaSkillIsPhaseIndexedTick(R->TickFn))
	{
		for (int32 Ph = 0; Ph < 3; ++Ph) { if (A.PhaseMs[Ph] > 0.f) { A.bPhaseUsed[Ph] = true; } }
	}
	if (bHexa4)
	{
		for (int32 Ph = 0; Ph < 4; ++Ph) { A.bPhaseUsed[Ph] = A.PhaseMs[Ph] > 0.f; }   // эталон: удар на каждом пороге 0..3
	}
	// ★★★ СТЫК С УРОНОМ. `Proj` (тело тика) — авторитет признака «снаряд»; `TickFn` печатается в
	//   логе, чтобы момент удара можно было сверить с конкретной функцией эталона. `bPhaseHasHitFx`
	//   — «эта фаза реально ВЕДЁТ УДАР» (гейт текста «MISS», см. ReleaseHits).
	A.bRowProj = (R->Proj != 0);
	A.bRowProjDelay = (R->ProjDelay != 0);
	A.TickFn = R->TickFn;
	// ★★★ Кастер — тот, кого прислал пакет (свой ИЛИ сосед). Раньше здесь жёстко стоял `SelfUserNum`,
	//   и записи урона соседа не находили его показ ни в `DeferSkillHit`, ни в `FlushPendingCastHits`.
	A.CasterUserIdx = CasterIdx;
	auto HasImpactFx = [](const TArray<FAsdaSkillFxSpawn>& L)
	{
		for (const FAsdaSkillFxSpawn& S : L)
		{
			if (S.Anchor != FName(TEXT("projectile"))) { return true; }
		}
		return false;
	};
	// ★ У снаряда эффект удара приезжает подсекцией `2·фаза+4` и в программе показа может не
	//   значиться — там «удар ведётся» по самому факту носителя.
	A.bPhaseHasHitFx[0] = HasImpactFx(A.Hit0) || A.bRowProj;
	A.bPhaseHasHitFx[1] = HasImpactFx(A.Hit1) || A.bRowProj;
	A.bPhaseHasHitFx[2] = HasImpactFx(A.Hit2) || A.bRowProj;
	A.bPhaseHasHitFx[3] = HasImpactFx(A.Hit3) || A.bRowProj;
	// ★ 29.10.2026 (Charge): тик слота 046 — `Char_UpdateDash` (537 и 597 делят загрузчик и тик, таблица skill_buff.c:20100/20333).
	if ((SkillId == 537 || SkillId == 597) && CVarAsdaSkillDash.GetValueOnGameThread() != 0)
	{
		BeginDash(A, Caster, bOurs);
	}

	// 2) ЭФФЕКТЫ КАСТА — только те, что эталонный тик спавняет на ПЕРВОМ КАДРЕ.
	//    ★★★ Раньше здесь вываливались ВСЕ `.jeb` строки разом — и эффект удара играл в момент
	//    замаха, в позиции кастера. Именно поэтому «импакта не видно»: он уже отыграл до удара.
	TArray<FAsdaSkillFxSpawn> CastList;
	// ★ 08.11.2026 [ОРИГИНАЛ] (`asda.Skill.HexaFourHits`): тик 513 на первом кадре спавнит `field_68` у ЦЕЛИ, а не у кастера.
	ParseFxList(bHexa4 && !HexaCastJeb.IsEmpty() ? HexaCastJeb + TEXT("@world:target") : R->FxCast, CastList);
	int32 NCast = 0;
	for (const FAsdaSkillFxSpawn& S : CastList)
	{
		if (SpawnFx(A, S, false)) { ++NCast; }
	}

	// 3) ЗВУК КАСТА — 3D в позиции персонажа (поле слота `pEffectSoundObj`; эталон играет его из
	//    ветки массива 3, `effectPlayGenderSoundCastBone@0xA85800`, каналы 20..27 по полу).
	if (UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		const FString Cast = R->SndCast.IsEmpty() ? FString() : R->SndCast;
		if (!Cast.IsEmpty())
		{
			Snd->PlaySound3DAs(FName(*FPaths::GetBaseFilename(Cast).ToLower()), Caster->GetActorLocation(), EAsdaSoundClass::Magic, Caster);   // ★ 30.10: «магия»
		}
	}

	// ★ R4, доля a (15.11.2026) [ПОРТ, производительность]: классы эффектов ФАЗ УДАРА (снаряды, удары — списки Hit0..Hit3) запрашиваются
	//   асинхронно при старте показа по пакету каста: до первой фазы сотни миллисекунд, к ней класс будет в памяти, и синхронная
	//   загрузка в кадре удара не понадобится. Момент и состав показа не меняются — спавн идёт прежним кодом. Блок стоит ПОСЛЕ
	//   рождения эффектов каста: те грузятся синхронно в этом же кадре, и их загрузка не должна дожидаться ещё и предзагрузки.
	{
		TArray<FString> PreJebs;
		for (const TArray<FAsdaSkillFxSpawn>* L : { &A.Hit0, &A.Hit1, &A.Hit2, &A.Hit3 })
		{
			for (const FAsdaSkillFxSpawn& S : *L) { PreJebs.AddUnique(S.Jeb); }
		}
		AsdaFxClassCache::PreloadJebs(PreJebs);
	}

	Active.Add(MoveTemp(A));
	const FAsdaSkillFxActive& AA = Active.Last();
	UE_LOG(LogAsdaSkillFx, Log,
		TEXT("[навыки-визуал] навык %d «%s»: кастер uidx=%d%s (пол %d, оружие %d); клип %s (anim_%d, ")
		TEXT("%.2fс) кость %s, каст: эффектов %d + звук «%s»; фазы COMBO %.0f/%.0f/%.0f мс — ")
		TEXT("ударов %d/%d/%d, звук удара «%s», снаряд %s; цель %s (%s, источник: %s); ")
		TEXT("источник строки %s, выбор: %s"),
		SkillId, *R->SkillName, AA.CasterUserIdx, bOurs ? TEXT(" (мой)") : TEXT(" (СОСЕД)"),
		(int32)Caster->Sex, Caster->EquippedWeaponSubType,
		*R->Clip, R->AnimIdx, Len, *AA.FxBone.ToString(), NCast, *R->SndCast,
		AA.PhaseMs[0], AA.PhaseMs[1], AA.PhaseMs[2],
		AA.Hit0.Num(), AA.Hit1.Num(), AA.Hit2.Num(), *AA.SndHit,
		R->Proj ? TEXT("ДА") : TEXT("нет"),
		AA.Target.IsValid() ? *AA.Target->GetName() : TEXT("не выбрана"),
		AA.bTargetIsMob ? TEXT("моб") : TEXT("игрок"), TargetSrc, *R->Source, *Why);
	// ★ Закон jeb-emitter-routing-hole: неразобранное обязано ЛОГИРОВАТЬСЯ, а не проглатываться.
	if (!R->FxNote.IsEmpty())
	{
		UE_LOG(LogAsdaSkillFx, Warning,
			TEXT("[навыки-визуал] навык %d: тик %s — НЕ РАЗОБРАНО: %s"),
			SkillId, *R->TickFn, *R->FxNote);
	}
	ASDA_CTRACE(bOurs ? EAsdaCT::Swing : EAsdaCT::Peer,
		TEXT("КЛИП НАВЫКА %d «%s» старт: кастер %d%s, клип %s (anim_%d) %.2f с клипа × темп %.2f = %.2f с; кадры удара COMBO %.0f/%.0f/%.0f мс (фаз %d/%d/%d), снаряд %s; цель %s (%s, %s); эффектов каста %d"),
		SkillId, *R->SkillName, AA.CasterUserIdx, bOurs ? TEXT(" (я)") : TEXT(" (сосед)"), *R->Clip, R->AnimIdx, Len, AnimScale, Len / AnimScale,
		AA.PhaseMs[0], AA.PhaseMs[1], AA.PhaseMs[2],
		AA.bPhaseUsed[0] ? 1 : 0, AA.bPhaseUsed[1] ? 1 : 0, AA.bPhaseUsed[2] ? 1 : 0,
		R->Proj ? TEXT("ДА") : (R->ProjDelay ? TEXT("бестелесный") : TEXT("нет")),
		AA.Target.IsValid() ? *AA.Target->GetName() : TEXT("НЕТ"), AA.bTargetIsMob ? TEXT("моб") : TEXT("игрок"), TargetSrc, NCast);
	// ★★★ И ТОЛЬКО ТЕПЕРЬ — записи урона из 5257/5270, которые приехали до постановки показа.
	//   Порядок эталона (`Recv_SkillCast@0xF4E370`): клип применения → раскладка 29-байтных записей
	//   → состояние 24. Раньше здесь не было ничего, и урон навыка не применялся вообще.
	//   ⚠ `AA` дальше НЕ трогаем: выпуск урона может убить моба и погасить показ (`StopSkillVisual`),
	//   т.е. переложить массив `Active`. Поэтому кастера берём в локальную переменную ДО вызова.
	const int32 FlushCaster = AA.CasterUserIdx;
	FlushPendingCastHits(FlushCaster, SkillId, /*bTimedOutOnly=*/false);
	return true;
}

void UAsdaSkillFxSubsystem::StopSkillVisual(AAsdaPlayerCharacter* Caster)
{
	bool bStoppedAny = false;   // ★ был ли у этого кастера показ (см. возврат скорости ниже)
	for (int32 i = Active.Num() - 1; i >= 0; --i)
	{
		if (Active[i].Caster.Get() != Caster) { continue; }
		ASDA_CTRACE(EAsdaCT::Swing, TEXT("показ навыка %d (кастер %d) СНЯТ досрочно: эффектов гасится %d, неотпущенных записей урона %d → выпуск сразу; фазы выстрелили %d%d%d"),
			Active[i].SkillId, Active[i].CasterUserIdx, Active[i].Fx.Num(), Active[i].PendingHits.Num(),
			Active[i].bPhaseFired[0] ? 1 : 0, Active[i].bPhaseFired[1] ? 1 : 0, Active[i].bPhaseFired[2] ? 1 : 0);
		bStoppedAny = true;
		for (const TWeakObjectPtr<AActor>& Fx : Active[i].Fx)
		{
			if (AActor* A = Fx.Get()) { A->Destroy(); }
		}
		// ★ Ни одна запись 4102 не должна пропасть (no-simplifications-stubs): показ снят до порога
		//   фазы (отказ сервера / прерывание) ⇒ применяем урон немедленно, как эталонный
		//   обработчик 4102 без визуального слоя.
		ReleaseHits(Active[i].PendingHits, /*bHasHitFx=*/false, TEXT("показ снят до порога фазы"));
		Active.RemoveAt(i);
	}
	// ★ Показ снят ⇒ состояния 24 больше нет ⇒ доворот к цели навыка прекращается, и гейт клипа
	//   применения тоже (иначе персонаж остался бы в позе последнего кадра до конца срока).
	if (Caster)
	{
		Caster->SkillFaceUntilS = 0.0; Caster->SkillAnimUntilS = 0.0;
		// ★ Вернуть скорость воспроизведения: она живёт на КОМПОНЕНТЕ, и при обрыве показа на нём
		//   осталась бы скорость анимаций навыка — до ближайшего `PlayWorldClip`. Тот же класс
		//   протечки, от которого порт защищается в `PlayWorldClip` и при запуске клипа навыка.
		// ★★★ 16.09.2026, ВТОРАЯ РЕВИЗИЯ — ИСПРАВЛЕН РЕГРЕСС МОЕЙ ЖЕ ПРАВКИ. Сброс стоял БЕЗУСЛОВНО
		//   и срабатывал, даже когда показа у этого кастера не было вовсе. А сюда приходят и отказы
		//   сервера (5257/5271) — без гейта «показ есть» и без остановки замаха. Если в этот момент
		//   шла базовая атака, её клип играет на `AttackClipTempo`, и безусловный сброс ронял его на
		//   1.0, оставив поле прежним: пройденное время замаха занижалось, а на нём висят момент
		//   удара и вылет снаряда. Поэтому: трогаем скорость ТОЛЬКО если показ действительно сняли,
		//   и возвращаем не константу, а темп текущего клипа — его держит `PlayWorldClip`.
		if (bStoppedAny && Caster->BaseSkel)
		{
			Caster->BaseSkel->SetPlayRate(FMath::Max(0.1f, Caster->AttackClipTempo));
		}
	}
}

bool UAsdaSkillFxSubsystem::TickActive(float /*Dt*/)
{
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return true; }
	const double Now = W->GetTimeSeconds();
	// ★ Урон 5257/5270, за которым показ так и не пришёл (чужой каст, у навыка нет строки, отказ):
	//   ни одна запись не должна пропасть (no-simplifications-stubs).
	FlushPendingCastHits(/*CasterUserIdx=*/-1, /*SkillId=*/-1, /*bTimedOutOnly=*/true);
	for (int32 i = Active.Num() - 1; i >= 0; --i)
	{
		AAsdaPlayerCharacter* C = Active[i].Caster.Get();
		if (!C)
		{
			// ★★★ П16 (2026-08-11): кастер исчез (ушёл из AoI, вышел из игры, сменилась карта) —
			//   раньше запись просто выбрасывалась, и ВСЕ припаркованные в ней записи урона
			//   пропадали МОЛЧА. Это прямое нарушение собственного инварианта порта «ни одна запись
			//   4102/5257 не должна пропасть» (no-simplifications-stubs) и единственная из четырёх
			//   веток снятия показа, где не звали `ReleaseHits` (ср. `:1583`, `:1643`, `:1654`).
			//   Эффекты каста при этом гаснут сами: каждому при спавне выставлен `SetLifeSpan`
			//   (клип + `kFxExtraLifeSec`), удару — `NaturalLifeSpan` (В2-C F2); но актор-носитель уже мог быть
			//   уничтожен вместе с кастером, поэтому гасим явно — как это делает `StopSkillVisual`.
			for (const TWeakObjectPtr<AActor>& Fx : Active[i].Fx)
			{
				if (AActor* A = Fx.Get()) { A->Destroy(); }
			}
			ASDA_CTRACE(EAsdaCT::Swing, TEXT("показ навыка %d: кастер %d ИСЧЕЗ из мира — эффекты погашены, записей урона %d → выпуск"),
				Active[i].SkillId, Active[i].CasterUserIdx, Active[i].PendingHits.Num());
			ReleaseHits(Active[i].PendingHits, /*bHasHitFx=*/false, TEXT("кастер исчез из мира"));
			Active.RemoveAt(i);
			continue;
		}

		// ★ Пока актор цели жив — освежаем её последнюю известную позицию (после смерти/удаления
		//   актора спавн удара пользуется именно ею, а не позицией кастера).
		if (AActor* T = Active[i].Target.Get())
		{
			Active[i].LastTargetLoc = T->GetActorLocation();
			Active[i].bHasTargetLoc = true;
		}

		// ★★★ ПОРОГИ ФАЗ. Эталон (`Char_UpdateSkillAnim_MultiPhase@0xA76E70`,
		//   `charFx_autoSkill501Tick@0xAB54B0`): фазы идут СТРОГО ПО ОЧЕРЕДИ — сравнивается
		//   `COMBO[текущаяФаза] <= прогресс`, и только после срабатывания счётчик фазы растёт
		//   (`++effectSlots[112].field_3A0`). Поэтому фаза i не может выстрелить раньше i−1.
		//   Единицы — МИЛЛИСЕКУНДЫ (`CComboManager_LoadComboTime@0x9BC5F0` переводит кадры в мс
		//   ещё при загрузке файла; COMBO из `SkillInfo.inl` изначально в мс).
		//   ★★★ 16.09.2026 (вторая половина системы темпа): сравнивать надо В КЛИП-ВРЕМЕНИ. Эталон
		//   сличает пороги с `Object_GetFloat13(анимобъект)` — а тот идёт шагом `dt · animSpeedScale`,
		//   то есть быстрее реального времени во столько же раз. COMBO в `PhaseMs` лежат в клип-мс
		//   (так их и загружает оригинал), поэтому переводим ПРОГРЕСС, а не пороги: пороги остаются
		//   ровно тем, что написано в данных, и лог показа печатает их без пересчёта.
		const float ProgressMs = float((Now - Active[i].StartTimeS) * 1000.0) * Active[i].AnimScale;
		// ★ 29.10.2026 (Charge): рывок ведёт свой тик. Записи урона выпускаются ПОСЛЕ правок записи показа (выпуск может убить
		//   моба и переложить `Active`), и по ним запись больше не трогаем; показ закрывается на фазе 3 (клип обрывается на COMBO0).
		if (Active[i].bDash)
		{
			const bool bDashDone = TickDash(Active[i], ProgressMs);
			TArray<UAsdaNetSubsystem::FAsdaSkillHitRec> DashHits;
			if (Active[i].DashPhase >= 1 && Active[i].PendingHits.Num() > 0) { DashHits = MoveTemp(Active[i].PendingHits); Active[i].PendingHits.Reset(); }
			if (bDashDone) { Active[i].EndTimeS = Now; }
			if (DashHits.Num() > 0)
			{
				ReleaseHits(DashHits, /*bHasHitFx=*/false, TEXT("рывок: прибытие (Skill_ApplyEffectResult)"));
				continue;   // запись показа могла уйти из `Active`; закрытие — следующим тиком
			}
		}
		for (int32 Ph = 0; Ph < 4; ++Ph)   // ★ 08.11: четвёртая фаза — только у тика с 4-м порогом (513), у прочих bPhaseUsed[3] = false
		{
			if (Active[i].bPhaseFired[Ph]) { continue; }
			if (!Active[i].bPhaseUsed[Ph]) { break; }        // фазы нет — следующих тоже нет
			if (Ph > 0 && !Active[i].bPhaseFired[Ph - 1]) { break; }
			// ★ 06.11.2026 [ОРИГИНАЛ] (`asda.Skill.CastCharge`): порог cb4 — модель снаряда появляется у кости до вылета (разбор у `AsdaCastPreSpawnMs`).
			if (Ph == 0 && GAsdaChargePre.Num() > 0)
			{
				FAsdaChargePre* Pre = GAsdaChargePre.Find(TWeakObjectPtr<AActor>(C));
				if (Pre && Pre->SkillId == Active[i].SkillId && !Pre->bSpawned && ProgressMs >= Pre->AtMs)
				{
					Pre->bSpawned = true;
					if (const FAsdaSkillFxSpawn* Pj = Active[i].Hit0.FindByPredicate([](const FAsdaSkillFxSpawn& S) { return S.Anchor == FName(TEXT("projectile")); }))
					{
						FAsdaSkillFxSpawn Held = *Pj;
						Held.Anchor = FName(TEXT("bone")); Held.Origin = FName(TEXT("bone")); Held.TargetKind = NAME_None;
						if (AActor* Model = SpawnFx(Active[i], Held, /*bImpact=*/false))
						{
							Model->SetLifeSpan((Active[i].PhaseMs[0] - Pre->AtMs) / 1000.f / FMath::Max(0.01f, Active[i].AnimScale) + 0.3f);   // страховка: сброс показа
							Pre->Model = Model;
							GAsdaChargePreLastS = FPlatformTime::Seconds();
							UE_LOG(LogAsdaSkillFx, Log, TEXT("[навыки-заряд] навык %d: снаряд «%s» у кости за %.0f мс до вылета"), Active[i].SkillId, *Pj->Jeb, Active[i].PhaseMs[0] - Pre->AtMs);
						}
					}
				}
			}
			if (ProgressMs < Active[i].PhaseMs[Ph]) { break; }
			Active[i].bPhaseFired[Ph] = true;
			// Вылет: модель у кости сменяет летящий носитель (эталон запускает тот же экземпляр — `cEffectLaunchDirected`).
			if (Ph == 0 && GAsdaChargePre.Num() > 0)
			{
				const TWeakObjectPtr<AActor> CasterKey(C);
				if (const FAsdaChargePre* Pre = GAsdaChargePre.Find(CasterKey))
				{
					if (AActor* Model = Pre->Model.Get()) { Model->Destroy(); GAsdaChargeLaunchLastS = FPlatformTime::Seconds(); }
					GAsdaChargePre.Remove(CasterKey);
				}
			}
#if !UE_BUILD_SHIPPING
			if (bRecordPhases)   // ★ Ф2: наблюдение самотеста — фактический и расчётный момент порога фазы (ревизия Ф2: только отладка)
			{
				FAsdaPhaseFiredSeen& E = PhasesFiredSeen.AddDefaulted_GetRef();
				E.CasterUserIdx = Active[i].CasterUserIdx; E.SkillId = Active[i].SkillId; E.Phase = Ph; E.AtS = Now;
				E.PlannedS = Active[i].StartTimeS + (double)Active[i].PhaseMs[Ph] / 1000.0 / (double)FMath::Max(0.01f, Active[i].AnimScale);
			}
#endif
			FirePhase(Active[i], Ph);
		}

		if (Now < Active[i].EndTimeS) { continue; }
		// Клип доиграл — вернуть персонажа в его состояние. SetAnimState отсекает равное состояние,
		// поэтому сбрасываем через None (эта ветка switch клип не трогает).
		// ⚠ Гейт клипа снимаем ДО SetAnimState: иначе он же и запретил бы восстановление клипа покоя.
		C->SkillAnimUntilS = 0.0;
		// ★★★ ПО ТРУПУ СОСТОЯНИЕ НЕ ВОССТАНАВЛИВАЕМ. Персонаж мог умереть, пока клип навыка доигрывал
		//   (у соседа это обычное дело: 6001/4053 приходят посреди каста). Эталон из состояния 24 в
		//   этом случае не выходит — его перевело в состояние 6 (`CChar_SetState case 6`,
		//   entity_char.c:16863), а тик 24 по мёртвому персонажу уже не крутится. Без этой проверки
		//   `SetAnimState(Idle)` подменял бы позу трупа клипом покоя — «оживший» мертвец.
		//   ⚠ Записи урона всё равно ВЫПУСКАЕМ: терять их нельзя (no-simplifications-stubs).
		if (C->IsIncapacitated())
		{
			C->SkillFaceUntilS = 0.0;
			ASDA_CTRACE(EAsdaCT::Swing, TEXT("клип навыка %d доиграл, но кастер %d мёртв — поза трупа не трогается, записей урона %d → выпуск"),
				Active[i].SkillId, Active[i].CasterUserIdx, Active[i].PendingHits.Num());
			ReleaseHits(Active[i].PendingHits, /*bHasHitFx=*/false, TEXT("кастер умер до конца клипа"));
			Active.RemoveAt(i);
			continue;
		}
		const EAsdaPlayerState Want = C->IsMoving() ? EAsdaPlayerState::Move : EAsdaPlayerState::Idle;
		C->SetAnimState(EAsdaPlayerState::None);
		C->SetAnimState(Want);
		// ★ Конец клипа = выход из эталонного состояния 24 ⇒ доворот к цели навыка прекращается
		//   (гейт `objAI_FaceTargetByUserIdx` состоянием больше не выполняется).
		C->SkillFaceUntilS = 0.0;
		ASDA_CTRACE(EAsdaCT::Swing, TEXT("клип навыка %d ДОИГРАЛ (кастер %d): поза → %s, фазы выстрелили %d%d%d, остаток записей урона %d → выпуск"),
			Active[i].SkillId, Active[i].CasterUserIdx, Want == EAsdaPlayerState::Move ? TEXT("бег") : TEXT("покой"),
			Active[i].bPhaseFired[0] ? 1 : 0, Active[i].bPhaseFired[1] ? 1 : 0, Active[i].bPhaseFired[2] ? 1 : 0, Active[i].PendingHits.Num());
		// Остаток записей урона (пришли уже после всех порогов) — применяем сразу, не теряем.
		ReleaseHits(Active[i].PendingHits, /*bHasHitFx=*/false, TEXT("конец клипа"));
		Active.RemoveAt(i);
	}

	// ★ Подметание носителей: снаряд мог быть уничтожен без прилёта (смена карты, смерть цели,
	//   выгрузка мира) — записи 4102 из него применяем немедленно, иначе они пропадут.
	// ★ 10.11.2026 (ревизия 4): созревшие отложенные записи — копим и выпускаем ПО ВОЗРАСТАНИЮ срока (обход ниже идёт с конца:
	//   при подвисании первой вышла бы волна с бо́льшей задержкой, несущая «HP после»).
	TArray<FAsdaSkillFxFlight> MatureRecords;
	for (int32 i = Flights.Num() - 1; i >= 0; --i)
	{
		// ★ БЕСТЕЛЕСНЫЙ носитель: актора нет, ждём ТОЛЬКО время трассы (эффекты уже отыграны).
		if (Flights[i].bDelayOnly)
		{
			if (Now < Flights[i].ArriveAtS) { continue; }
			if (Flights[i].bRecord)   // ★ 10.11.2026 (job4-vfx): отложенная запись эталона — выпуск после обхода, по сроку (ревизия 4)
			{
				MatureRecords.Add(MoveTemp(Flights[i]));
				Flights.RemoveAt(i);
				continue;
			}
			ASDA_CTRACE(EAsdaCT::HitFrame, TEXT("навык %d фаза %d: бестелесная трасса ДОШЛА — записей урона %d → выпуск"),
				Flights[i].SkillId, Flights[i].Phase, Flights[i].Hits.Num());
			ReleaseHits(Flights[i].Hits, Flights[i].bHasHitFx, TEXT("трасса без визуала (ProjDelay)"));
			Flights.RemoveAt(i);
			continue;
		}
		if (Flights[i].Proj.IsValid() && !Flights[i].bArrived) { continue; }
		if (!Flights[i].bArrived)
		{
			ASDA_CTRACE(EAsdaCT::HitFrame, TEXT("навык %d фаза %d: носитель ИСЧЕЗ, не долетев — записей урона %d → выпуск"),
				Flights[i].SkillId, Flights[i].Phase, Flights[i].Hits.Num());
			ReleaseHits(Flights[i].Hits, Flights[i].bHasHitFx, TEXT("носитель исчез, не долетев"));
		}
		Flights.RemoveAt(i);
	}
	MatureRecords.Sort([](const FAsdaSkillFxFlight& L, const FAsdaSkillFxFlight& R) { return L.ArriveAtS < R.ArriveAtS; });
	for (FAsdaSkillFxFlight& Rec : MatureRecords) { FireRecordFlight(Rec); }
	return true;
}
