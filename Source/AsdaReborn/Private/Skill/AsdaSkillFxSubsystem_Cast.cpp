// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillFxSubsystem_Cast.cpp — ★ R3-c 12.11.2026 (разрез AsdaSkillFxSubsystem.cpp, дословный перенос): записи каста 5257: волны урона, полёт записи, отложенные удары каста.
//   Общее для частей — в AsdaSkillFxSubsystem_Private.h; шапка и реверс — в AsdaSkillFxSubsystem.cpp.
#include "Skill/AsdaSkillFxSubsystem.h"
#include "AsdaNetSubsystem.h"   // ★ R1: прямое включение (раньше приходило транзитивно)

#include "AsdaEffectActor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Npc/AsdaMobActor.h"
#include "Resources/AsdaModelAnimSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldSubsystem.h"
#include "AsdaSelf.h"   // ★ «это я?» одной точкой (E2, 14.09.2026)
#include "World/AsdaCombatTrace.h"   // ★ 18.10.2026: журнал боя — записи урона навыка, клип, кадр удара, снаряд, эффекты
#include "UI/AsdaSkillDatabase.h"          // ★ 05.11.2026: behaviorType навыка — клетка 8215 (SendHitConfirm8215)
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaSkillUseSubsystem.h"      // ★ R5-fix8: 5257/5270 в очереди навыков (запись 4102 того же прохода сети)
#include "AsdaDevConsole.h"                // ★ R5-fix8: ASDA_DEV_CVAR
#include "AsdaSkillFxSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSkillFxSubsystem*.cpp

// ★★★ ПРИЁМ 29-БАЙТНЫХ ЗАПИСЕЙ 5257/5270 — ЭТО И ЕСТЬ КАНАЛ УРОНА НАВЫКА.
//   Сетевой колбэк по-прежнему ничего тяжёлого не делает (закон no-reentrant-net-callbacks): он
//   (а) запоминает флаг попадания по ключу «кастер+цель» — им пользуется гейт «MISS», и
//   (б) складывает готовые записи урона на полку `PendingCastHits`; разбирает полку
//   `FlushPendingCastHits` — из `PlaySkillVisual` (тот же логический шаг, что у эталона) либо из
//   тика по истечении срока ожидания показа.
//   ⚠ Раньше здесь брались ТОЛЬКО флаги, а урон ждали из 4102 — и он не приходил никогда.
//   ★ Д4 (уточнено 2026-08-11): формулировка «4102 несёт применение эффекта, урон в нём 0» НЕВЕРНА.
//   **4102 — канал ударов МОБОВ**: записи по 17 б, `rec+5` (i32) — это УРОН
//   (`network.c:45239` `CCharacter_TakeDamage2(…, *(_DWORD*)&v16[17*i+5], 3, …)` → `combatFloatingDamageText`;
//   также `:45265`, `:45277`), а все четыре отправителя лежат внутри `sub_4283F0@0x4283F0`
//   (**CMonsterManager**, `GameServer.c:17470/17481/17511/17515`); в `NpcServer.exe.c` — 0 вхождений.
//   Верно лишь то, что урон НАВЫКА ИГРОКА через 4102 не приходит — он едет в 5257/5270 (`rec+3`).
void UAsdaSkillFxSubsystem::OnNetSkillCastRecs(int32 CasterUserIdx, int32 SkillCode,
	const TArray<UAsdaNetSubsystem::FAsdaSkillCastRec>& Recs)
{
	const double Now = FPlatformTime::Seconds();
#if ASDA_COMBAT_TRACE
	const bool bTrMine = AsdaSelf::IsSelfUserIdx(GetGameInstance(), CasterUserIdx);   // свой каст или сосед — колонка ЭТАП
#endif
	ASDA_CTRACE(bTrMine ? EAsdaCT::Recv : EAsdaCT::Peer, TEXT("5257/5270 записи навыка %d: кастер userIdx=%d%s, записей %d"),
		SkillCode, CasterUserIdx, bTrMine ? TEXT(" (я)") : TEXT(" (сосед)"), Recs.Num());
	FPendingCastHits Batch;
	Batch.CasterUserIdx = CasterUserIdx;
	Batch.SkillId = SkillCode;
	Batch.AtS = Now;
	bool bMainSeen = false;   // ★ 05.11: первая НЕПУСТАЯ запись = основная цель (эталон targetRenderId)
	for (const UAsdaNetSubsystem::FAsdaSkillCastRec& R : Recs)
	{
		if (R.TargetIdx < 0)
		{
			ASDA_CTRACE_V(EAsdaCT::Recv, TEXT("  запись навыка %d пустая (сентинел idx −1) — пропуск"), SkillCode);
			continue;
		}              // сентинел −1 — запись пустая
		ASDA_CTRACE(bTrMine ? EAsdaCT::Recv : EAsdaCT::Peer,
			TEXT("  запись: цель %s idx=%d урон=%d HP после=%d флаги=0x%02X (попадание=%s крит=%s IMMUNE=%s) состояние=%d всплывашка=%d"),
			R.TargetKind == 1 ? TEXT("игрок") : TEXT("моб"), R.TargetIdx, R.Damage, R.HpAfter, R.HitFlags,
			(R.HitFlags & 1) ? TEXT("да") : TEXT("НЕТ"), (R.HitFlags & 4) ? TEXT("ДА") : TEXT("нет"),
			(R.HitFlags & 0x20) ? TEXT("да") : TEXT("нет"), R.StateId, R.PopupValue);
		FCastHitFlag& F = CastHitFlags.FindOrAdd(CastHitKey(CasterUserIdx, R.TargetKind, R.TargetIdx));
		F.HitFlags = R.HitFlags;
		F.AtS = Now;
		F.bMain = !bMainSeen; bMainSeen = true;   // ★ 05.11: основная цель — первая запись (эталон targetRenderId)

		// Перекладка «запись каста → запись удара». Виды цели совпадают: 1 = игрок, 0 = сущность.
		UAsdaNetSubsystem::FAsdaSkillHitRec H;
		H.TargetKind = (R.TargetKind == 1) ? 1 : 0;
		H.TargetIdx  = R.TargetIdx;
		H.SkillId    = SkillCode;
		H.Damage     = R.Damage;
		H.HpAfter    = R.HpAfter;
		// Эталонный `applyFlag` 4102 = «результат применять». У 29-байтной записи ту же роль играет
		//   бит0 байта `+7` (`charSplitSkillStatusByFlag@0xA28870` кладёт его в массив флагов, а
		//   `CharFx_ShowSkillDamageText@0xA7FC00` по нему ветвится).
		H.ApplyFlag  = (R.HitFlags & 1) ? 1 : 0;
		H.CasterIdx  = CasterUserIdx;
		H.HitFlags   = R.HitFlags;
		H.RecvAtS    = Now;            // ★ 04.11.2026 (status_effect_expiry): receipt time of the cast result (stale hit-frame state gate)
		H.BuffStacks = R.BuffStacks;   // ★ bowfx: стеки слотов buff0/1/2 — для `Skill_ApplyEffectResult` на моменте удара
		if (SkillSplitsDamageIn3(SkillCode))
		{
			// ★★★ Многоударный навык: сервер шлёт ОДНУ величину, клиент делит её на три и
			//   показывает по одной на фазу (доказательство — в шапке SkillSplitsDamageIn3, .h).
			//   HP «после удара» несёт ТОЛЬКО последняя часть: иначе первая же приводила бы полосу
			//   к финальному значению и следующие две цифры бежали бы по уже мёртвой цели.
			int32 Parts[3] = { 0, 0, 0 };
			SplitDamageIn3(R.Damage, Parts);
			for (int32 Ph = 0; Ph < 3; ++Ph)
			{
				UAsdaNetSubsystem::FAsdaSkillHitRec P = H;
				P.Damage  = Parts[Ph];
				P.Phase   = Ph;
				P.HpAfter = (Ph == 2) ? R.HpAfter : -1;   // −1 = «сервер HP не сообщил»
				Batch.Hits.Add(P);
			}
			UE_LOG(LogAsdaSkillFx, Log,
				TEXT("[навыки-стык] навык %d многоударный (парсер skillParseQuickSlotPacket@0xA86BD0): ")
				TEXT("урон %d → фазы %d / %d / %d"), SkillCode, R.Damage, Parts[0], Parts[1], Parts[2]);
			ASDA_CTRACE(EAsdaCT::Queue, TEXT("навык %d многоударный: урон %d по idx=%d делится на фазы %d / %d / %d (HP после — только у 3-й)"),
				SkillCode, R.Damage, R.TargetIdx, Parts[0], Parts[1], Parts[2]);
			continue;
		}
		// ★ 10.11.2026 (job4-vfx) [ОРИГИНАЛ]: ОТЛОЖЕННЫЕ ЗАПИСИ — урон наступает при выпуске записи эталона, а не на пороге фазы
		//   (тело и доказательства — SkillRecordWaves). Каждая доля — своя запись ленты (ArmSkillImpact ниже взводится по каждой),
		//   «HP после удара» несёт только последняя — как у многоударного выше.
		{
			TArray<TPair<float, int32>> Waves;
			if (SkillRecordWaves(SkillCode, R.Damage, Waves))
			{
				for (int32 Wv = 0; Wv < Waves.Num(); ++Wv)
				{
					UAsdaNetSubsystem::FAsdaSkillHitRec P = H;
					P.RecDelayMs = Waves[Wv].Key;
					P.Damage = Waves[Wv].Value;
					P.HpAfter = (Wv == Waves.Num() - 1) ? R.HpAfter : -1;   // −1 = «сервер HP не сообщил»
					Batch.Hits.Add(P);
				}
				ASDA_CTRACE(EAsdaCT::Queue, TEXT("навык %d: урон %d по idx=%d — отложенных записей %d (первая через %.0f мс)"),
					SkillCode, R.Damage, R.TargetIdx, Waves.Num(), Waves.Num() > 0 ? Waves[0].Key : 0.f);
				continue;
			}
		}
		Batch.Hits.Add(H);
	}
	if (Batch.Hits.Num() > 0)
	{
		// ★ 17.10.2026 (синхронизация боя, Ф1) [ПОРТ]: каждая запись урона по МОБУ — запись ленты попаданий мира: пока она на
		//   полке/в показе (до порога фазы или прилёта носителя), серверные HP и смерть этого моба ждут её попадания на этом
		//   экране (`UAsdaWorldSubsystem::ArmSkillImpact`; закрывает её `ApplySkillHit` при выпуске). Ключи отката — у ленты.
		if (UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
		{
			for (const UAsdaNetSubsystem::FAsdaSkillHitRec& H : Batch.Hits)
			{
				if (H.TargetKind == 0) { WS->ArmSkillImpact(CasterUserIdx, H.TargetIdx, /*bFrom4102=*/false, H.Damage, H.HpAfter, H.RecDelayMs / 1000.f); }   // ★ 10.11: + задержка записи
			}
		}
		ASDA_CTRACE(EAsdaCT::Queue, TEXT("навык %d кастер %d: %d записей урона → на ПОЛКУ (ждут показа/кадра удара, срок %.2f с), полок всего %d"),
			SkillCode, CasterUserIdx, Batch.Hits.Num(), kCastHitsGraceS, PendingCastHits.Num() + 1);
		PendingCastHits.Add(MoveTemp(Batch));
		// ★ 10.11.2026 (ревизия 5): отложенные записи, пришедшие при уже ИДУЩЕМ показе этого кастера (5270 показ не запускает), — разобрать
		//   сразу, а не через сторожа kCastHitsGraceS (иначе срок записи сдвигался на +0.35 с от прихода).
		{
			const bool bHasRec = PendingCastHits.Last().Hits.ContainsByPredicate([](const UAsdaNetSubsystem::FAsdaSkillHitRec& X) { return X.RecDelayMs > 0.f; });
			const bool bShow = Active.ContainsByPredicate([CasterUserIdx, SkillCode](const FAsdaSkillFxActive& X) { return X.CasterUserIdx == CasterUserIdx && X.SkillId == SkillCode && X.bPhaseFired[0]; });   // порог фазы 0 позади
			if (bHasRec && bShow) { FlushPendingCastHits(CasterUserIdx, SkillCode, /*bTimedOutOnly=*/false); }
		}
	}
	// Подметание протухших ключей (память не растёт бесконечно).
	for (auto It = CastHitFlags.CreateIterator(); It; ++It)
	{
		if (Now - It->Value.AtS > 10.0) { It.RemoveCurrent(); }
	}
}

// ★ Деление урона на три части — БАЙТ В БАЙТ `skillParseQuickSlotPacket@0xA86BD0`
//   (skill_buff.c:19092-19104): целочисленное `d/3` дважды, остаток — в третью часть.
void UAsdaSkillFxSubsystem::SplitDamageIn3(int32 Damage, int32 (&OutParts)[3])
{
	if (Damage >= 3)
	{
		OutParts[0] = Damage / 3;
		OutParts[1] = Damage / 3;
		OutParts[2] = Damage - (OutParts[0] + OutParts[1]);
	}
	else
	{
		OutParts[0] = 0;
		OutParts[1] = 0;
		OutParts[2] = Damage;      // эталон: при уроне < 3 весь он уходит в ТРЕТЬЮ часть
	}
}

// ★★★ 10.11.2026 (job4-vfx) [ОРИГИНАЛ]: ОТЛОЖЕННЫЕ ЗАПИСИ навыков. Полный перечень вызовов `packBuildEntryRecord@0xA5D390` (file_vfs.c:511)
//   в эталоне — 5 мест; задержка — второй аргумент, он ложится в `node[0]` узла `skillProjectileList`, а `animSeq_TickAndFireEvents@0xA5D5E0`
//   (model_anim.c:446-470) зовёт обработчик вида узла (таблица `skill_buff.c:11666-11682`, вид 9 → `skillResultApplyDamage@0xA5E170`,
//   вид 13 → `skillFx_applySkillResultToTargets@0xA5E580`), когда накопленное время ≥ `node[0]`. Время копится `deltaTime` тика персонажа
//   (`charEntityUpdateTick`, entity_char.c:22477) — в МИЛЛИСЕКУНДАХ: тот же шаг тики навыков сравнивают с порогами COMBO в мс (колонка
//   Combo0Ms), а волны 911 идут шагом +100 (8 волн за 0.8 с при клипе ~2 с; в секундах это были бы 100 с — второй метод).
//     · 907 «Метеор» (`SkillEffect_Update_Explosion@0xA70840`, effects_particles.c:8862/8885): одна запись вида 13 через 400 мс
//       (`effectScaleBits = 0x43C80000` = 400.0) — весь урон, эффект `bigmeob03`/`fireragb03` и звук записи;
//     · 911 «Огненный шторм» (`SkillEffect_UpdateMultiWaveAoEHit@0xA6D390`, effects_particles.c:8370-8490): 9 записей вида 9 через
//       400, 500, … 1200 мс (`waveDelay` = 400, +100 после каждой); урон делится: волны 0..7 — `d / 11` (целочисленно), волна 8 — остаток
//       `d − 8·(d/11)` (effects_particles.c:8440-8465); обработчик вида 9 эффектов не рождает — цифры урона и звук удара моба;
//     · `effectBeamUpdate@0xA5F3F0` (effects_particles.c:6432) — задержка 1.0 мс (запись выпускается следующим кадром): у порта это
//       выпуск на прилёте носителя без задержки — отдельной рамки не нужно.
bool UAsdaSkillFxSubsystem::SkillRecordWaves(int32 SkillId, int32 Damage, TArray<TPair<float, int32>>& OutWaves)
{
	OutWaves.Reset();
	if (SkillId == 907)
	{
		OutWaves.Emplace(400.f, Damage);
		return true;
	}
	if (SkillId == 911)
	{
		// ★ Ревизия 5 [ОРИГИНАЛ]: разрядность эталона (effects_particles.c:8442-8475). Доля волн 0..7 — `(WORD)(урон / 11)` в
		//   `_WORD perTargetDamageTable[]`, сумма — в 16-битной ячейке `targetSumBase`, доля волны 8 — `(WORD)(младшие 16 бит урона − сумма)`;
		//   в строку волны (`waveDamageRows`, dword) доля идёт со ЗНАКОВЫМ расширением `(__int16)`. При уроне ≤ 32767 это d/11 ×8 + остаток.
		const uint16 Part16 = (uint16)(Damage / 11);
		const uint16 Sum16 = (uint16)(Part16 * 8);
		const uint16 Last16 = (uint16)((uint16)Damage - Sum16);
		for (int32 n = 0; n < 9; ++n)
		{
			OutWaves.Emplace(400.f + 100.f * n, (int32)(int16)(n < 8 ? Part16 : Last16));
		}
		return true;
	}
	return false;
}

// ★ 10.11.2026 (job4-vfx): ВЫПУСК ОТЛОЖЕННОЙ ЗАПИСИ. Порядок обработчиков эталона (вид 13 — effects_particles.c:6195-6280,
//   вид 9 — skill_buff.c:12118-12172): по целям-мобам — урон/цифра, эффект записи у основной цели, звук записи у кастера, звук удара
//   моба (фаза 11 = `_99`, `Sound_BuildEntryWavName(моб, 11)`) у КАСТЕРА по каждой найденной цели-мобу; по целям-игрокам с попаданием —
//   цифра, эффект записи в точке игрока и звук записи у кастера. Эффекты/звуки записи — элементы «~rec» строк навыка.
void UAsdaSkillFxSubsystem::FireRecordFlight(FAsdaSkillFxFlight& F)
{
	AAsdaPlayerCharacter* C = F.Caster.Get();
	FAsdaSkillFxActive Tmp;
	Tmp.Caster = F.Caster;
	Tmp.SkillId = F.SkillId;
	Tmp.CasterUserIdx = F.CasterUserIdx;
	Tmp.Target = F.Target;
	Tmp.SndHit = F.SndHit;
	Tmp.bTargetIsMob = F.bTargetIsMob;
	Tmp.PendingHits = F.Hits;   // основная цель вида «rchar» и звук «/rchar» смотрят флаги записей
	if (!F.DestLoc.IsNearlyZero()) { Tmp.LastTargetLoc = F.DestLoc; Tmp.bHasTargetLoc = true; }
	int32 NFx = 0;
	if (C)
	{
		TGuardValue<bool> RecGuard(bSpawningRecord, true);
		for (const FAsdaSkillFxSpawn& S : F.HitFx)
		{
			if (S.Anchor == FName(TEXT("projectile"))) { continue; }
			if (SpawnFx(Tmp, S, /*bImpact=*/true)) { ++NFx; }
		}
		NFx += SpawnAoeImpactFx(Tmp, F.HitFx, F.Hits);
		PlayHitSound(Tmp, F.Phase);
		// Звук удара моба (фаза 11) у кастера — по каждой найденной цели-мобу записи (и при промахе: вызов стоит вне `& 1`).
		UGameInstance* GI = GetGameInstance();
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;
		for (const UAsdaNetSubsystem::FAsdaSkillHitRec& H : F.Hits)
		{
			if (!WS || !Snd || AsdaTargetKindFromHitRec(H.TargetKind) != EAsdaTargetKind::Entity || H.TargetIdx < 0) { continue; }
			const AAsdaMobActor* M = Cast<AAsdaMobActor>(WS->ActorByTarget(EAsdaTargetKind::Entity, H.TargetIdx));
			if (!M || M->SoundBase.IsEmpty()) { continue; }
			const bool bAtMob = GAsdaSkillMobHitSoundAtTarget != 0;   // ★ 10.11 [ИСПРАВЛЕНИЕ]: звук удара по мобу — у моба (0 — у кастера, как оригинал)
			Snd->PlaySound3DAs(FName(*FString::Printf(TEXT("%s_99"), *M->SoundBase)), bAtMob ? M->GetActorLocation() : C->GetActorLocation(), EAsdaSoundClass::Auto, bAtMob ? (const AActor*)M : (const AActor*)C);
		}
	}
	if (C && C->GetWorld()) { DebugRecordFireTimes.Add(C->GetWorld()->GetTimeSeconds()); if (DebugRecordFireTimes.Num() > 64) { DebugRecordFireTimes.RemoveAt(0); } }
	ASDA_CTRACE(EAsdaCT::HitFrame, TEXT("навык %d фаза %d: отложенная запись ВЫПУЩЕНА — эффектов записи %d, записей урона %d"),
		F.SkillId, F.Phase, NFx, F.Hits.Num());
	// ★ Ревизия 4: вид записи — для отлёта (ReleaseHits); у вида 13 (907) цифра урона только при попадании (effects_particles.c:6205-6211),
	//   «MISS» обработчик не рисует — гейт MISS снят. У вида 9 (911) — гейт как у порога фазы.
	const int32 RecKind = (F.SkillId == 911) ? 9 : 13;
	TGuardValue<int32> KindGuard(GAsdaSkillRecordReleaseKind, RecKind);
	ReleaseHits(F.Hits, RecKind == 13 ? false : true, TEXT("отложенная запись (packBuildEntryRecord)"));   // вид 9: гейт MISS — правило вида выше
}

// ★ Отбор записей своей фазы. `Phase < 0` — «безфазная» запись (все навыки, кроме многоударных):
//   она принадлежит ЛЮБОЙ фазе и выпускается первой же — прежнее поведение без изменений.
void UAsdaSkillFxSubsystem::TakeHitsForPhase(TArray<UAsdaNetSubsystem::FAsdaSkillHitRec>& From,
	int32 Phase, TArray<UAsdaNetSubsystem::FAsdaSkillHitRec>& Out)
{
	Out.Reset();
	for (int32 i = 0; i < From.Num(); )
	{
		if (From[i].Phase < 0 || From[i].Phase == Phase)
		{
			Out.Add(From[i]);
			From.RemoveAt(i);
			continue;
		}
		++i;
	}
}

// ★★★ R5-fix8 (14.11.2026) [ОРИГИНАЛ]: 5257 И 4102 ОДНИМ ПРОХОДОМ СЕТИ (ключ `asda.Skill.Hit4102WaitsQueuedCast`).
//   Оригинал запускает клип навыка прямо в приёме 5257 (`Recv_SkillCast@0xF4E370`, network.c:44805: `packPathDispatchEntryHandler`
//   → `charSkillResetEffectSlotsAndDispatch` → `CChar_SetState(pChar, …, 24, …)`, :44926-44935) — запись 4102, разобранная следом
//   в том же чтении сокета, уже видит идущий показ и ждёт порога его фазы. Порт по закону «сеть из колбэка не трогаем» кладёт
//   5257 в очередь подсистемы навыков и запускает показ СЛЕДУЮЩИМ тиком: запись 4102 того же прохода показа не находила и
//   применялась сразу — смерть и цифра раньше порога фазы (самотест наблюдателя: «смерть 0,251 с против порога фазы 0,704»).
//   В живой игре так выходит, когда оба пакета пришли одним чтением сокета (длинный кадр, склейка TCP).
//   Правка: запись 4102 кастера, у которого в очереди навыков стоит успешный 5257/5270, кладётся на ту же полку, что и записи
//   самого 5257 (`PendingCastHits`): показ, стартуя, разбирает полку и паркует запись на свою фазу; показа не случилось —
//   полка отдаёт запись по сроку `kCastHitsGraceS`, как записи 5257. 1 = правка; 0 = как было (запись применяется сразу).
//   Пределы (те же, что у записей самого 5257): показ по очереди не стартовал (кастер не найден, нет строки клипа) — запись
//   применяется на `kCastHitsGraceS` позже прежнего; при кадре длиннее этого срока сторож полки может сработать раньше тика
//   подсистемы навыков (порядок тиков двух подсистем не закреплён) — запись уйдёт по ветке «показа нет», как до правки.
static ASDA_DEV_CVAR(int32, CVarAsdaHit4102WaitsQueuedCast,
	TEXT("asda.Skill.Hit4102WaitsQueuedCast"), 1,
	TEXT("[ОРИГИНАЛ] 1 (по умолчанию) = запись урона 4102 ждёт показ навыка, чей 5257/5270 принят тем же проходом сети и ещё не разобран ")
	TEXT("(у оригинала клип стартует в самом приёме 5257). 0 = как было: такая запись применяется сразу, раньше порога фазы."),
	ECVF_Default);

bool UAsdaSkillFxSubsystem::ShelveHitForQueuedCast(const UAsdaNetSubsystem::FAsdaSkillHitRec& R)
{
	if (CVarAsdaHit4102WaitsQueuedCast.GetValueOnGameThread() == 0) { return false; }
	const UAsdaSkillUseSubsystem* Use = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
	int32 QueuedSkill = -1;   // навык ждущего разбора 5257/5270 этого кастера
	if (!Use || !Use->HasQueuedCast(R.CasterIdx, QueuedSkill)) { return false; }
	FPendingCastHits Batch;
	Batch.CasterUserIdx = R.CasterIdx;
	Batch.SkillId = QueuedSkill;
	Batch.AtS = FPlatformTime::Seconds();
	Batch.Hits.Add(R);
	PendingCastHits.Add(MoveTemp(Batch));
	ASDA_CTRACE(EAsdaCT::Queue, TEXT("запись урона 4102 навыка %d (idx=%d урон=%d кастер %d) → на ПОЛКУ: 5257/5270 навыка %d этого кастера принят тем же проходом сети и ждёт разбора (показ стартует следующим тиком)"),
		R.SkillId, R.TargetIdx, R.Damage, R.CasterIdx, QueuedSkill);
	return true;
}

// ══ ★★★ РАЗБОР ПОЛКИ 5257/5270 ═══════════════════════════════════════════════════════════════
//   `bTimedOutOnly` — режим тика: отдаём только те батчи, за которыми показ так и не пришёл.
void UAsdaSkillFxSubsystem::FlushPendingCastHits(int32 CasterUserIdx, int32 SkillId, bool bTimedOutOnly)
{
	if (PendingCastHits.Num() == 0) { return; }
	const double Now = FPlatformTime::Seconds();
	for (int32 i = PendingCastHits.Num() - 1; i >= 0; --i)
	{
		const FPendingCastHits& P = PendingCastHits[i];
		if (bTimedOutOnly)
		{
			if (Now - P.AtS < kCastHitsGraceS) { continue; }
		}
		else
		{
			if (P.CasterUserIdx != CasterUserIdx) { continue; }
			// Код навыка сверяем, когда он у записи есть: у одного персонажа одновременно идёт
			//   ровно один каст (эталон: единственный слот `effectSlots[100].field_4`).
			if (SkillId >= 0 && P.SkillId >= 0 && P.SkillId != SkillId) { continue; }
		}
		FPendingCastHits Take = MoveTemp(PendingCastHits[i]);
		PendingCastHits.RemoveAt(i);

		// ★ 05.11.2026 [ОРИГИНАЛ] (`asda.Skill.HitRecordPerPhase`, разбор у ключа): несколько «безфазных» записей по ОДНОЙ цели у
		//   показа с тиком по номеру фазы — запись k ждёт фазу k (эталон: урон записи k = двойное слово k массива, его читает фаза k).
		//   Записей больше, чем фаз-ударов, у живого сервера не бывает; если всё же пришли — лишние ждут последний удар (сумма урона
		//   и итоговое HP сервера сохраняются). Записи по РАЗНЫМ целям (площадь) не трогаем — их тик перебирает на одной фазе.
		if (GAsdaSkillHitRecordPerPhase != 0 && Take.Hits.Num() >= 2)
		{
			const UAsdaNetSubsystem::FAsdaSkillHitRec& H0 = Take.Hits[0];
			bool bSameTarget = true;
			for (const UAsdaNetSubsystem::FAsdaSkillHitRec& H : Take.Hits)
			{
				// ★ 10.11.2026: доли отложенных записей (RecDelayMs > 0, 911 — 9 волн) — не «запись на фазу», их ведёт задержка записи.
				if (H.Phase >= 0 || H.RecDelayMs > 0.f || H.TargetKind != H0.TargetKind || H.TargetIdx != H0.TargetIdx) { bSameTarget = false; break; }
			}
			const FAsdaSkillFxActive* Show = nullptr;
			for (const FAsdaSkillFxActive& A : Active)
			{
				if (A.CasterUserIdx != Take.CasterUserIdx) { continue; }
				if (Take.SkillId >= 0 && A.SkillId != Take.SkillId) { continue; }
				Show = &A;
			}
			if (bSameTarget && Show && (AsdaSkillIsPhaseIndexedTick(Show->TickFn) || IsHexaFourHitTick(Show->TickFn)))
			{
				int32 Ph[4] = { -1, -1, -1, -1 };
				int32 NPh = 0;
				for (int32 p = 0; p < 4; ++p) { if (Show->bPhaseUsed[p] && !Show->bPhaseFired[p]) { Ph[NPh++] = p; } }   // ★ 08.11: 4 фазы (513)
				if (NPh >= 2)
				{
					for (int32 k = 0; k < Take.Hits.Num(); ++k) { Take.Hits[k].Phase = Ph[FMath::Min(k, NPh - 1)]; }
					UE_LOG(LogAsdaSkillFx, Log,
						TEXT("[навыки-стык] навык %d (тик %s): записей по одной цели %d — по удару на фазу (фаз-ударов %d; эталон field_64 + фаза)"),
						Take.SkillId, *Show->TickFn, Take.Hits.Num(), NPh);
					ASDA_CTRACE(EAsdaCT::Queue, TEXT("навык %d кастер %d: %d записей по одной цели idx=%d — запись k ждёт фазу удара k (фаз %d)"),
						Take.SkillId, Take.CasterUserIdx, Take.Hits.Num(), H0.TargetIdx, NPh);
				}
			}
		}

		TArray<UAsdaNetSubsystem::FAsdaSkillHitRec> NoShow;
		int32 Parked = 0;
		for (const UAsdaNetSubsystem::FAsdaSkillHitRec& H : Take.Hits)
		{
			if (DeferSkillHit(H)) { ++Parked; } else { NoShow.Add(H); }
		}
		UE_LOG(LogAsdaSkillFx, Log,
			TEXT("[навыки-стык] 5257/5270: записей урона %d (навык %d, кастер uidx=%d) — ")
			TEXT("припарковано %d, немедленно %d%s"),
			Take.Hits.Num(), Take.SkillId, Take.CasterUserIdx, Parked, NoShow.Num(),
			bTimedOutOnly ? TEXT(" [показа не дождались]") : TEXT(""));
		ASDA_CTRACE(EAsdaCT::Queue, TEXT("полка навыка %d кастер %d снята%s: записей %d — припарковано к кадру удара %d, немедленно %d"),
			Take.SkillId, Take.CasterUserIdx, bTimedOutOnly ? TEXT(" ПО СРОКУ (показа не дождались)") : TEXT(" показом"),
			Take.Hits.Num(), Parked, NoShow.Num());
		if (NoShow.Num() > 0)
		{
			// Показа нет ⇒ ветки удара тика эталона нет ⇒ «MISS» не рисуем (bHasHitFx = false).
			ReleaseHits(NoShow, /*bHasHitFx=*/false, TEXT("показа навыка нет"));
		}
	}
}
