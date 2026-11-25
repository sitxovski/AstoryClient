// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEffectsSubsystem_Mob.cpp — ★ R3-c 12.11.2026 (разрез AsdaEffectsSubsystem.cpp, дословный перенос): состояния мобов: списки, цепочки, слоты DoT, блок хода/анимации, очистка.
//   Общее для частей — в AsdaEffectsSubsystem_Private.h; шапка и реверс — в AsdaEffectsSubsystem.cpp.
#include "UI/AsdaEffectsSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaSoulSlotSubsystem.h"   // ★ 06.11.2026: запуск и конец слотов Дружбы

#include "World/AsdaWorldSubsystem.h"
#include "Npc/AsdaMobActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "World/AsdaCombatTrace.h"          // журнал боя: эффекты состояний (наложены/сняты/истекли)
#include "AsdaEffectsSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaEffectsSubsystem*.cpp

// ★★★ 26.10.2026 (bowfx, баг игроков «яд/кровь не видны, ловушка не видна») [ОРИГИНАЛ] ключи урона по времени и состояний мобов.
//   asda.Dot.Slots: 1 — 6079 разбирается как `pktHandlerSkillCast@0xF4EFA0` (состояние моба + слот урона по времени, тики с цифрой
//   над целью и строкой 20013, `Skill_Buff_TickAll@0xF83A00` / `charDot_TickAll@0xA2BFD0`); 0 — прежний порт (6079 без разбора).
static ASDA_DEV_CVAR(int32, CVarDotSlots, TEXT("asda.Dot.Slots"), 1,
	TEXT("6079 «урон по времени наложен»: 1 = как оригинал (состояние на мобе + тики урона с цифрой), 0 = прежний порт (не разбирать)"),
	ECVF_Default);
// asda.Mob.ControlFreeze: 1 — моб под оглушением/сном/ЛОВУШКОЙ (3,14,8,2,9) не сдвигается по пути, под оледенением/окаменением
//   (0,1) ещё и анимация стоит (`charUpdateActionState23@0xF844F0` +6986/+6987); 0 — прежний порт (моб доходит маршрут).
static ASDA_DEV_CVAR(int32, CVarMobControlFreeze, TEXT("asda.Mob.ControlFreeze"), 1,
	TEXT("Моб под контролем: 1 = как оригинал (ловушка/стан/сон держат на месте, лёд/камень замораживают анимацию), 0 = прежний порт"),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarMobStaleHitGate, TEXT("asda.Buff.MobStaleHitGate"), 1,
	TEXT("Mob state on hit frame: 1 = skip a state the server already ended (4017) after the cast result (fix: no eternal stun/debuff), 0 = always add (original)"),
	ECVF_Default);
static bool AsdaDotSlotsOn() { return CVarDotSlots.GetValueOnGameThread() != 0; }
static bool AsdaMobControlFreezeOn() { return CVarMobControlFreeze.GetValueOnGameThread() != 0; }

// ═══════════════ СОСТОЯНИЯ СУЩНОСТЕЙ (вторая таблица обработчиков) ═════════════════════════════
FAsdaEffectList& UAsdaEffectsSubsystem::MobListFor(int32 RenderId)
{
	return MobLists.FindOrAdd(RenderId);
}

const TArray<FAsdaEffectEntry>& UAsdaEffectsSubsystem::GetMobList(int32 RenderId) const
{
	if (const FAsdaEffectList* L = MobLists.Find(RenderId)) { return L->Items; }
	return EmptyList;
}

// Эталон skillBuffAdd@0xF836E0: тот же 40-байтный узел, тот же поиск по паре (id, type == 0),
//   тот же перезапуск эффектов при повторном наложении.
void UAsdaEffectsSubsystem::AddOrUpdateMobSkill(int32 RenderId, int32 StateId, int32 Stack,
                                                int32 DurationSec, int32 CasterUserIdx)
{
	FAsdaEffectList& L = MobListFor(RenderId);
	FAsdaEffectEntry* E = nullptr;
	for (FAsdaEffectEntry& It : L.Items)
	{
		if (It.Type == 0 && It.Id == StateId) { E = &It; break; }
	}
	ASDA_CTRACE_MOB(RenderId, EAsdaCT::Fx, TEXT("состояние %d %s у моба rid=%d: стек=%d срок=%d с (0 — без срока) кастер=%d"),
		StateId, E ? TEXT("ОБНОВЛЕНО") : TEXT("НАЛОЖЕНО"), RenderId, Stack, DurationSec, CasterUserIdx);
	if (!E) { E = &L.Items.AddDefaulted_GetRef(); }
	StopBuffFx(*E);
	E->bFxSpawned = false;
	E->Id = StateId; E->Type = 0; E->Stack = Stack; E->TargetStack = Stack;
	E->CasterUserIdx = CasterUserIdx;
	// ⚠ ЧЕСТНО: остатка у сущности в 4102 НЕТ. Эталонный `skillBuffAdd` тоже собирает запись из
	//   4-байтного локального буфера (`sub_93A782(v4); v4[1] = стек;`) — поля длительности там
	//   попросту нет, и «протухание» состояния сущности ведёт НЕ таймер списка, а
	//   `Skill_Buff_TickAll@0xF83A00` (счётчик тиков урона) либо смерть/уход сущности. Поэтому
	//   0 = «без срока»: тик остатков сущности его не трогает (см. TickMobEffects).
	E->RemainSec = DurationSec;
	E->LastTickMs = AsdaNowMs();
	SpawnBuffFx(*E, RenderId, /*bMob=*/true);
}

bool UAsdaEffectsSubsystem::RemoveMobBySkillState(int32 RenderId, int32 StateId)
{
	FAsdaEffectList* L = MobLists.Find(RenderId);
	if (!L) { return false; }
	for (int32 i = 0; i < L->Items.Num(); ++i)
	{
		if (L->Items[i].Type == 0 && L->Items[i].Id == StateId)
		{
			ASDA_CTRACE_MOB(RenderId, EAsdaCT::Fx, TEXT("состояние %d СНЯТО у моба rid=%d (стек был %d)"), StateId, RenderId, L->Items[i].Stack);
			StopBuffFx(L->Items[i]);
			BuffFxWaitSince.Remove(AsdaBuffFxKey(RenderId, /*bMob=*/true, StateId));
			L->Items.RemoveAt(i);
			return true;
		}
	}
	return false;
}

// ══ ★★★ 26.10.2026 (bowfx) ЦЕПОЧКА СОСТОЯНИЙ МОБА И УРОН ПО ВРЕМЕНИ — разбор у объявлений в заголовке ═══════════════
// ★★★ 06.11.2026 (баг Player5 27.10 «Spade Immunity do not show»: цель не бьётся, значка нет). Разбор: моб Spade (тип 290) и 436/734/753
//   — навык моба 20 «셀프 물리 무적» (msif: состояние 5), шахматные 303–310 и 449–456 — навык 29 «셀프 무적» (5 + 6). Навык на себя приходит
//   8012 (запись вид 0, цель = сам моб, флаги 3), сервер кладёт состояние в список моба (4017 @8), удар по нему — результат −3.
//   Каналы состояний сущности у оригинала: 4102/6079/свой удар и `Recv_MobSpawn@0xF305E0` (28 id записи @63, глубины @119 →
//   `skillBuffAdd`, только `buff0SubType == 1` — усиления). 4017 (`Skill_Buff_RefreshStackDepth`) записей НЕ заводит.
//   ДВЕ ДЫРЫ:
//   • [ОРИГИНАЛ] порт список записи 4013/4019 не читал вовсе: у моба, вошедшего в обзор уже с усилением, значка и эффекта не было.
//     Ключ `asda.Buff.MobSpawnStates` (0 — прежний порт).
//   • [ИСПРАВЛЕНИЕ дефекта оригинала] усиление, которое моб наложил на глазах у игрока, у оригинала не показывается до повторного входа
//     в обзор. Порт заводит запись по списку 4017 с тем же отбором `buff0SubType == 1`; снимает её тот же 4017.
//     Ключ `asda.Buff.MobListBuffs` (0 — как оригинал).
static ASDA_DEV_CVAR(int32, CVarAsdaMobSpawnStates, TEXT("asda.Buff.MobSpawnStates"), 1,
	TEXT("[ОРИГИНАЛ 06.11] 1 = усиления из записи 4013/4019 (28 id @63, глубины @119, buff0SubType 1) ложатся на моба — значок цели и эффект; ")
	TEXT("0 = прежний порт (список записи не читается)."), ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarAsdaMobListBuffs, TEXT("asda.Buff.MobListBuffs"), 1,
	TEXT("[ИСПРАВЛЕНИЕ 06.11] 1 = усиление моба из списка 4017, которого нет на клиенте (иммунитет, щит и т.п., наложенные на глазах), заводится ")
	TEXT("и видно значком цели; 0 = как оригинал (4017 записей не заводит)."), ECVF_Default);

// Завести на мобе усиления из серверного списка (id и глубины параллельно; −1 — пусто). bSpawnRecord: запись 4013/4019, иначе 4017.
//   Возврат — сколько записей заведено. Уже висящие не трогаются: их глубину ведёт 4017, а эффект не перезапускается.
int32 AsdaMobAddListedBuffs(UAsdaEffectsSubsystem* Fx, int32 Rid, const TArray<int32>& Ids, const TArray<int32>& Levels, bool bSpawnRecord)
{
	if (!Fx || (bSpawnRecord ? CVarAsdaMobSpawnStates : CVarAsdaMobListBuffs).GetValueOnGameThread() == 0) { return 0; }
	int32 N = 0;
	for (int32 i = 0; i < Ids.Num(); ++i)
	{
		const int32 Id = Ids[i];
		if (Id < 0 || Id >= 350 || Id == 177) { continue; }   // отсевы `skillBuffAdd`
		const FAsdaSkillEffectMeta& M = Fx->GetSkillMeta(Id);
		if (!M.bValid || M.Buff0SubType != 1) { continue; }   // `skillRecord->buff0SubType[0] == 1`
		bool bHas = false;
		for (const FAsdaEffectEntry& E : Fx->GetMobList(Rid)) { if (E.Type == 0 && E.Id == Id) { bHas = true; break; } }
		if (bHas) { continue; }
		const int32 Stack = Levels.IsValidIndex(i) ? Levels[i] : 0;
		Fx->AddOrUpdateMobSkill(Rid, Id, Stack, /*DurationSec=*/0, /*CasterUserIdx=*/-1);
		UE_LOG(LogAsdaBuff, Log, TEXT("[состояния-моба] rid=%d: усиление %d из %s (стек %d)"), Rid, Id, bSpawnRecord ? TEXT("записи 4013/4019") : TEXT("списка 4017"), Stack);
		ASDA_CTRACE_MOB(Rid, EAsdaCT::Fx, TEXT("усиление %d на моба rid=%d из %s, стек %d"), Id, Rid, bSpawnRecord ? TEXT("записи 4013/4019") : TEXT("списка 4017"), Stack);
		++N;
	}
	return N;
}

bool UAsdaEffectsSubsystem::ApplyMobSkillState(int32 RenderId, int32 StateId, int32 SourceSkill, int32 Stack, bool bSetStack,
                                               int32 CasterUserIdx)
{
	// `*(this + 4669) = кастер` эталон пишет ДО отсевов — у нас кастер живёт в записи состояния (FAsdaEffectEntry::CasterUserIdx).
	if (StateId < 0 || StateId >= 350 || StateId == 177) { return false; }
	FAsdaEffectList& L = MobListFor(RenderId);
	FAsdaEffectEntry* Old = nullptr;
	for (FAsdaEffectEntry& It : L.Items) { if (It.Type == 0 && It.Id == StateId) { Old = &It; break; } }
	// [ОРИГИНАЛ] навык 1046 в `buff_ApplySkillBuff` (не в skillBuffAdd) записи НЕ создаёт и не переписывает (skill_buff.c:38957/38965).
	const bool bNoEntry1046 = !bSetStack && SourceSkill == 1046;
	if (Old)
	{
		const FAsdaSkillEffectMeta& NewRec = GetSkillMeta(SourceSkill);
		const FAsdaSkillEffectMeta& OldRec = GetSkillMeta(Old->IdFlag);
		if (NewRec.bValid && OldRec.bValid)
		{
			// [ОРИГИНАЛ] «слабее не перебивает»: `if (rec(a5)->skillGradeNum < rec(+2)->skillGradeNum) return 0;`
			if (NewRec.GradeNum < OldRec.GradeNum)
			{
				ASDA_CTRACE_MOB(RenderId, EAsdaCT::Fx, TEXT("состояние %d у моба rid=%d НЕ перебито: грейд навыка %d (%d) < грейда висящего %d (%d)"),
					StateId, RenderId, SourceSkill, NewRec.GradeNum, Old->IdFlag, OldRec.GradeNum);
				return false;
			}
			// `buffStopEffectsBySkillId` гасит И УДАЛЯЕТ узел; затем `effectQueueTimedEvent` ставит новый В КОНЕЦ списка
			//   (у 1046 в buff_ApplySkillBuff нового узла нет).
			RemoveMobBySkillState(RenderId, StateId);
			if (bNoEntry1046) { return true; }
			AddOrUpdateMobSkill(RenderId, StateId, bSetStack ? Stack : 0, /*DurationSec=*/0, CasterUserIdx);
		}
		else
		{
			// Записи навыка нет: эталон гасит только эффекты (`CEffectMgr_StopByOwner` +24/+28), узел и стек остаются.
			const int32 KeepStack = Old->Stack;
			AddOrUpdateMobSkill(RenderId, StateId, bSetStack ? Stack : KeepStack, /*DurationSec=*/0, CasterUserIdx);
		}
	}
	else
	{
		if (bNoEntry1046) { return true; }
		AddOrUpdateMobSkill(RenderId, StateId, bSetStack ? Stack : 0, /*DurationSec=*/0, CasterUserIdx);
	}
	// +2 — навык-источник (по нему сверяется грейд следующего наложения).
	for (FAsdaEffectEntry& It : MobListFor(RenderId).Items)
	{
		if (It.Type == 0 && It.Id == StateId) { It.IdFlag = SourceSkill; break; }
	}
	UE_LOG(LogAsdaBuff, Log, TEXT("[состояния-моба] rid=%d: состояние %d от навыка %d (%s, стек %d, кастер uidx=%d)"),
		RenderId, StateId, SourceSkill, bSetStack ? TEXT("skillBuffAdd") : TEXT("buff_ApplySkillBuff"), bSetStack ? Stack : 0, CasterUserIdx);
	return true;
}

int32 UAsdaEffectsSubsystem::ApplyMobSkillChain(int32 RenderId, int32 SkillId, int32 CasterUserIdx, const uint8* Stacks3,
                                               bool bRequireDuration, bool bOnlySlot0, double CastRecvAtS)
{
	const FAsdaSkillEffectMeta& M = GetSkillMeta(SkillId);
	if (!M.bValid) { return 0; }   // эталон: `if (pSkillRecord)` — нет записи навыка, нет и цепочки
	int32 N = 0;
	for (int32 k = 0; k < (bOnlySlot0 ? 1 : 3); ++k)
	{
		if (bRequireDuration && M.BuffDurationMs[k] == 0) { continue; }   // `if (*(_DWORD*)pSkillRecord->buffNDuration)`
		const int32 Stack = Stacks3 ? (int32)Stacks3[k] : 0;
		// ★ 04.11.2026 (status_effect_expiry) [ИСПРАВЛЕНИЕ ДЕФЕКТА]: the server already ended this state after our cast result
		//   arrived — its instance is over; re-adding it on the (later) hit frame left it forever (4017 is sent only on change).
		if (CastRecvAtS >= 0.0 && CVarMobStaleHitGate.GetValueOnGameThread() != 0
			&& IsMobStateEndedSince(RenderId, M.BuffStateId[k], CastRecvAtS))
		{
			UE_LOG(LogAsdaBuff, Log, TEXT("[состояния-моба] rid=%d: состояние %d навыка %d на кадре удара НЕ наложено — сервер уже снял его (4017) после прихода результата каста"),
				RenderId, M.BuffStateId[k], SkillId);
			ASDA_CTRACE_MOB(RenderId, EAsdaCT::Fx, TEXT("состояние %d навыка %d: кадр удара позже снятия сервером (4017) — не накладывается (asda.Buff.MobStaleHitGate)"),
				M.BuffStateId[k], SkillId);
			continue;
		}
		if (ApplyMobSkillState(RenderId, M.BuffStateId[k], SkillId, Stack, /*bSetStack=*/bRequireDuration, CasterUserIdx)) { ++N; }
	}
	return N;
}

void UAsdaEffectsSubsystem::OnDotSlotPacket(const FAsdaDotSlotPkt& P)
{
	if (!AsdaDotSlotsOn())
	{
		UE_LOG(LogAsdaBuff, Verbose, TEXT("[урон по времени] 6079 пропущен — ключ отката (прежний порт)"));
		return;
	}
	if (P.Ok != 1) { return; }   // эталон: `if (*(_BYTE*)a1 == 1)`
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* W = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!W) { return; }
	// Слот 1:1 с `skillInitDotSlot@0xA40D20` / `skillInitDotDamageSlot@0xA40E00`: только при ненулевом тике;
	//   ОСТАТОК = длительность / шаг · тик (как `(unsigned __int64)(a5 / a6 * a3)`).
	auto InitSlot = [this, &P](bool bMob, int32 Key, int32 Caster)
	{
		if (P.TickDamage == 0) { return; }
		// ⚠ [ИСПРАВЛЕНИЕ] шаг 0: эталон делит на ноль (inf → мусор в остатке); такой слот не заводим.
		if (P.IntervalMs <= 0)
		{
			UE_LOG(LogAsdaBuff, Warning, TEXT("[урон по времени] 6079: шаг тика %d — слот не заведён"), P.IntervalMs);
			return;
		}
		FAsdaDotSlot& S = (bMob ? MobDotSlots : CharDotSlots).FindOrAdd(Key).FindOrAdd(P.StateId);
		S.DurationMs = (float)P.DurationMs;
		S.IntervalMs = (float)P.IntervalMs;
		S.TickDamage = P.TickDamage;
		S.Remaining  = (int32)(int64)((double)S.DurationMs / (double)S.IntervalMs * (double)P.TickDamage);
		S.AccumMs    = 0.f;
		S.SkillId    = P.SkillId;
		S.CasterUserIdx = Caster;
		ASDA_CTRACE_IF(bMob ? AsdaCombatTrace::IsEngaged(Key) : AsdaCombatTrace::IsUserRelevant(Key), EAsdaCT::Fx,
			TEXT("6079: СЛОТ урона по времени у %s %d: состояние %d (навык %d), %d мс, шаг %d мс, тик %d, всего %d, кастер uidx=%d"),
			bMob ? TEXT("моба rid") : TEXT("игрока uidx"), Key, P.StateId, P.SkillId, P.DurationMs, P.IntervalMs, P.TickDamage, S.Remaining, Caster);
	};
	const bool bCasterKnown = W->ActorByTarget(EAsdaTargetKind::Character, P.CasterIdx) != nullptr;   // `CharManager_FindByUserIdx(a1+1)`
	const bool bOnChar = (P.TargetKind != 0);
	UE_LOG(LogAsdaBuff, Log, TEXT("[урон по времени] 6079: кастер uidx=%d (%s) → %s %d, навык %d, состояние %d, ур. %d, %d мс / шаг %d мс, тик %d"),
		P.CasterIdx, bCasterKnown ? TEXT("в мире") : TEXT("не в мире"), bOnChar ? TEXT("игрок uidx") : TEXT("моб rid"), P.TargetIdx,
		P.SkillId, P.StateId, P.Level, P.DurationMs, P.IntervalMs, P.TickDamage);
	if (bCasterKnown)
	{
		if (bOnChar)
		{
			if (W->ActorByTarget(EAsdaTargetKind::Character, P.TargetIdx)) { InitSlot(/*bMob=*/false, P.TargetIdx, P.CasterIdx); }
		}
		else if (W->FindNetMobByRid(P.TargetIdx))
		{
			ApplyMobSkillState(P.TargetIdx, P.StateId, P.SkillId, 0, /*bSetStack=*/false, P.CasterIdx);   // buff_ApplySkillBuff
			InitSlot(/*bMob=*/true, P.TargetIdx, P.CasterIdx);                                          // skillInitDotSlot
		}
		return;
	}
	// Кастера в мире нет: `else if (CSkillMgr_GetSkillRecord(a1+6))`.
	if (!GetSkillMeta(P.SkillId).bValid) { return; }
	if (bOnChar)
	{
		// Поле «кто наложил» эталон в этой ветке не пишет — у слота кастер неизвестен.
		if (W->ActorByTarget(EAsdaTargetKind::Character, P.TargetIdx)) { InitSlot(/*bMob=*/false, P.TargetIdx, -1); }
	}
	else if (W->FindNetMobByRid(P.TargetIdx))
	{
		ApplyMobSkillState(P.TargetIdx, P.StateId, P.SkillId, 0, /*bSetStack=*/false, P.CasterIdx);   // только состояние, слота нет
	}
}

const FAsdaDotSlot* UAsdaEffectsSubsystem::FindDotSlot(bool bMob, int32 Key, int32 StateId) const
{
	const TMap<int32, FAsdaDotSlot>* M = (bMob ? MobDotSlots : CharDotSlots).Find(Key);
	return M ? M->Find(StateId) : nullptr;
}

int32 UAsdaEffectsSubsystem::GetDotSlotCount(bool bMob, int32 Key) const
{
	const TMap<int32, FAsdaDotSlot>* M = (bMob ? MobDotSlots : CharDotSlots).Find(Key);
	return M ? M->Num() : 0;
}

void UAsdaEffectsSubsystem::ClearDotSlots(bool bMob, int32 Key, int32 StateId)
{
	TMap<int32, TMap<int32, FAsdaDotSlot>>& Map = bMob ? MobDotSlots : CharDotSlots;
	TMap<int32, FAsdaDotSlot>* M = Map.Find(Key);
	if (!M) { return; }
	if (StateId < 0) { M->Reset(); } else { M->Remove(StateId); }
	if (M->Num() == 0) { Map.Remove(Key); }
}

bool UAsdaEffectsSubsystem::HasMobMoveLock(int32 RenderId) const
{
	if (!AsdaMobControlFreezeOn()) { return false; }
	const FAsdaEffectList* L = MobLists.Find(RenderId);
	if (!L) { return false; }
	for (const FAsdaEffectEntry& E : L->Items)
	{
		// `cclassHasAnyBuffType@0xF8E610`: 3, 14, 8, 2, 9 (type 0).
		if (E.Type == 0 && (E.Id == 3 || E.Id == 14 || E.Id == 8 || E.Id == 2 || E.Id == 9)) { return true; }
	}
	return false;
}

bool UAsdaEffectsSubsystem::HasMobAnimFreeze(int32 RenderId) const
{
	if (!AsdaMobControlFreezeOn()) { return false; }
	const FAsdaEffectList* L = MobLists.Find(RenderId);
	if (!L) { return false; }
	for (const FAsdaEffectEntry& E : L->Items)
	{
		if (E.Type == 0 && (E.Id == 0 || E.Id == 1)) { return true; }   // `cclassHasBuffType0Or1@0xF8E5A0`
	}
	return false;
}

// ★ R4, доля a (14.11.2026): оба признака одним проходом (объявление и довод — в заголовке).
void UAsdaEffectsSubsystem::MobControlFlags(int32 RenderId, bool& bOutAnimFreeze, bool& bOutMoveLock) const
{
	bOutAnimFreeze = false;
	bOutMoveLock = false;
	if (MobLists.Num() == 0 || !AsdaMobControlFreezeOn()) { return; }
	const FAsdaEffectList* L = MobLists.Find(RenderId);
	if (!L) { return; }
	for (const FAsdaEffectEntry& E : L->Items)
	{
		if (E.Type != 0) { continue; }
		if (E.Id == 0 || E.Id == 1) { bOutAnimFreeze = true; }
		else if (E.Id == 3 || E.Id == 14 || E.Id == 8 || E.Id == 2 || E.Id == 9) { bOutMoveLock = true; }
	}
}

void UAsdaEffectsSubsystem::TickDotSlots(float DeltaMs)
{
	if (DeltaMs <= 0.f || (MobDotSlots.Num() == 0 && CharDotSlots.Num() == 0)) { return; }
	// Эталон тикает каждую сущность своим `Skill_Buff_TickAll` и каждого персонажа своим `charDot_TickAll`;
	//   за кадр — НЕ БОЛЕЕ одного тика на слот (`if (накопитель >= шаг)`, а не `while`).
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const bool bMob = (Pass == 0);
		TMap<int32, TMap<int32, FAsdaDotSlot>>& Map = bMob ? MobDotSlots : CharDotSlots;
		TArray<TPair<int32, int32>> Drop;   // (носитель, состояние) — погасить после прохода
		TArray<FAsdaDotTickEvent> Due;      // события кадра: подписчик зовётся ПОСЛЕ обхода (он может менять карту слотов)
		for (TPair<int32, TMap<int32, FAsdaDotSlot>>& Host : Map)
		{
			for (TPair<int32, FAsdaDotSlot>& It : Host.Value)
			{
				FAsdaDotSlot& S = It.Value;
				S.AccumMs += DeltaMs;
				if (S.AccumMs < S.IntervalMs) { continue; }
				S.Remaining -= S.TickDamage;
				S.AccumMs -= S.IntervalMs;
				FAsdaDotTickEvent& E = Due.AddDefaulted_GetRef();
				E.bMob = bMob; E.Key = Host.Key; E.StateId = It.Key; E.SkillId = S.SkillId;
				E.Damage = S.TickDamage; E.CasterUserIdx = S.CasterUserIdx; E.Remaining = S.Remaining;
			}
		}
		for (FAsdaDotTickEvent& E : Due)
		{
			OnDotTick.Broadcast(E);
			// Остаток исчерпан — слот обнуляется (`… = 0; LOWORD(+24) = 0`); подписчик мог велеть погасить раньше.
			if (E.bStopSlot || E.Remaining <= 0) { Drop.Emplace(E.Key, E.StateId); }
		}
		for (const TPair<int32, int32>& D : Drop) { ClearDotSlots(bMob, D.Key, D.Value); }
	}
}

// Эталон Skill_Buff_TickAll@0xF83A00, первая ветка: HP <= 0 либо state == 10 (смерть) ⇒ оба хэндла
//   уходят в CEffectMgr_StopByOwner. Уход из AoI (4011) в порту приходит тем же путём.
void UAsdaEffectsSubsystem::ClearMobEffects(int32 RenderId)
{
	FAsdaEffectList* L = MobLists.Find(RenderId);
	if (!L) { return; }
	ASDA_CTRACE_MOB(RenderId, EAsdaCT::Fx, TEXT("все состояния моба rid=%d сняты (смерть/уход из обзора): %d шт."), RenderId, L->Items.Num());
	ClearDotSlots(/*bMob=*/true, RenderId);   // ★ bowfx: слоты урона по времени живут в самой сущности эталона и гибнут с ней
	for (FAsdaEffectEntry& E : L->Items)
	{
		StopBuffFx(E);
		BuffFxWaitSince.Remove(AsdaBuffFxKey(RenderId, /*bMob=*/true, E.Id));
	}
	MobLists.Remove(RenderId);
	ForgetMobServerStates(RenderId);   // ★ 04.11.2026 (status_effect_expiry): entity gone — its server list too
}

// ★★★ 04.11.2026 (status_effect_expiry) — разбор у объявления. Called from 4017 BEFORE the client-list pass.
void UAsdaEffectsSubsystem::NoteMobServerStateList(int32 RenderId, const TArray<int32>& BuffIds)
{
	TSet<int32> Now;
	for (const int32 Id : BuffIds) { if (Id >= 0) { Now.Add(Id); } }
	const double T = FPlatformTime::Seconds();
	if (const TSet<int32>* Prev = MobServerStates.Find(RenderId))
	{
		for (const int32 Id : *Prev)
		{
			if (!Now.Contains(Id)) { MobStateEndedAtS.Add(((int64)RenderId << 32) | (uint32)Id, T); }
		}
	}
	MobServerStates.Add(RenderId, MoveTemp(Now));
}

bool UAsdaEffectsSubsystem::IsMobStateEndedSince(int32 RenderId, int32 StateId, double SinceS) const
{
	if (const TSet<int32>* Cur = MobServerStates.Find(RenderId); Cur && Cur->Contains(StateId)) { return false; }   // server still has it
	const double* At = MobStateEndedAtS.Find(((int64)RenderId << 32) | (uint32)StateId);
	return At && *At >= SinceS;
}

void UAsdaEffectsSubsystem::ForgetMobServerStates(int32 RenderId)
{
	if (RenderId < 0) { MobServerStates.Reset(); MobStateEndedAtS.Reset(); return; }
	MobServerStates.Remove(RenderId);
	for (auto It = MobStateEndedAtS.CreateIterator(); It; ++It) { if ((int32)(It.Key() >> 32) == RenderId) { It.RemoveCurrent(); } }
}

// ★★★ П6 (2026-08-11): ЭФФЕКТЫ СОСТОЯНИЙ УШЕДШЕГО ИГРОКА.
//   ЭТАЛОН дыры не имеет ПО ПОСТРОЕНИЮ: `Recv_LogOutPacket@0xF3AE00` (`network.c:39521`) зовёт
//   `CharManager_RemoveNode(pChar)`, и список состояний персонажа вместе с хэндлами эффектов
//   (`rec+24` / `rec+28`) умирает вместе с ОБЪЕКТОМ персонажа — гасить отдельно нечего.
//   В порту список состояний живёт в подсистеме и переживал уход владельца: и карта `Lists`,
//   и сами эффект-акторы оставались навсегда. Хуже того, сервер ПЕРЕИСПОЛЬЗУЕТ userIdx (слот
//   сессии) — следующий вошедший в тот же слот наследовал бы чужие эффекты (ровно та же грабля,
//   что уже чинили у транспорта/крыльев в `UAsdaWorldSubsystem::OnEntityDespawn`).
//   ⚠ Здесь снимается ВСЁ, включая `type == 2`: это не `buffListClearAllStopFx` (та чистит живого
//   персонажа и предметные эффекты бережёт), а гибель самого объекта-носителя.
void UAsdaEffectsSubsystem::ClearCharEffects(int32 UserIdx)
{
	// ★ 06.11.2026 [ОРИГИНАЛ]: дух и постоянные эффекты слотов уходят вместе с объектом персонажа (`entity_char.c:7076`).
	if (UAsdaSoulSlotSubsystem* Slots = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoulSlotSubsystem>() : nullptr) { Slots->ForgetChar(UserIdx); }
	ClearDotSlots(/*bMob=*/false, UserIdx);   // ★ bowfx: слоты урона по времени уходят вместе с объектом персонажа
	FAsdaEffectList* L = Lists.Find(UserIdx);
	if (!L) { return; }
	const int32 N = L->Items.Num();
	for (FAsdaEffectEntry& E : L->Items)
	{
		StopBuffFx(E);
		BuffFxWaitSince.Remove(AsdaBuffFxKey(UserIdx, /*bMob=*/false, E.Id));
	}
	Lists.Remove(UserIdx);
	if (N > 0)
	{
		UE_LOG(LogAsdaBuff, Log,
			TEXT("[состояния] игрок userIdx=%d ушёл (4011) — снято состояний: %d, эффекты погашены"),
			UserIdx, N);
	}
}

// ★ 4011 у ОРИГИНАЛА адресует ИГРОКА (`Recv_LogOutPacket` ищет в игроцких бакетах
//   `CharManager_FindByUserIdx`), а в порту тем же делегатом приходит и уход сущности из AoI.
//   Поэтому чистим ОБА реестра по одному ключу: попадёт ровно тот, который существует.
void UAsdaEffectsSubsystem::OnEntityDespawn(int32 Key)
{
	ClearMobEffects(Key);
	// ★ ЗАЩИТА: свой персонаж по 4011 не уходит НИКОГДА (эталон рассылает пакет о ДРУГИХ), а
	//   renderId сущности и userIdx игрока — РАЗНЫЕ пространства номеров ([[charid-vs-usernum]]),
	//   и они могут совпасть числом. Без этого гейта деспавн моба с renderId, равным своему
	//   userIdx, снял бы СВОИ состояния. Ту же двусмысленность порт уже имел на стороне мобов.
	if (Key != SelfUserIdx()) { ClearCharEffects(Key); }
}

void UAsdaEffectsSubsystem::TickMobEffects()
{
	if (MobLists.Num() == 0) { return; }
	const int64 Now = AsdaNowMs();
	TArray<int32> DeadHosts;
	for (TPair<int32, FAsdaEffectList>& It : MobLists)
	{
		USkeletalMeshComponent* Skel = nullptr;
		AActor* Host = ResolveHost(It.Key, /*bMob=*/true, Skel);
		if (!Host)
		{
			// Сущности больше нет (умерла/ушла) — эталон гасит её эффекты вместе с ней.
			DeadHosts.Add(It.Key);
			continue;
		}
		// ★ 04.11.2026 (status_effect_expiry) [ИСПРАВЛЕНИЕ]: моб УМЕР, а труп ещё в мире (клип смерти + фейд). Эталон
		//   `Skill_Buff_TickAll@0xF83A00` шапкой `ent[284] <= 0 || state == 10` гасит эффекты состояния уже в момент смерти; порт
		//   держал их на трупе до удаления актора. Список сущности после смерти не нужен (оживление 4017 flags&4 — новая жизнь).
		if (const AAsdaMobActor* Mob = Cast<AAsdaMobActor>(Host); Mob && (Mob->IsDead() || Mob->bServerDead) && It.Value.Items.Num() > 0)
		{
			DeadHosts.Add(It.Key);
			continue;
		}
		TArray<int32> Expired;
		for (FAsdaEffectEntry& E : It.Value.Items)
		{
			if (E.RemainSec <= 0) { continue; }   // «без срока» (см. AddOrUpdateMobSkill)
			const int64 Dt = Now - E.LastTickMs;
			if (Dt < 1000) { continue; }
			E.RemainSec -= (int32)(Dt / 1000);
			E.LastTickMs = Now - (Dt % 1000);
			if (E.RemainSec <= 0) { Expired.Add(E.Id); }
		}
		for (int32 Id : Expired) { RemoveMobBySkillState(It.Key, Id); }
	}
	for (int32 Rid : DeadHosts) { ClearMobEffects(Rid); }
}

FString UAsdaEffectsSubsystem::DumpBuffFx() const
{
	EnsureBuffFxLoaded();
	FString S = TEXT("=== ВИЗУАЛ СОСТОЯНИЙ (третий канал навыков) ===\n");
	S += FString::Printf(TEXT(" канал %s; строк в DT_BuffFx %d; ожидают носителя %d\n"),
		bBuffFxEnabled ? TEXT("ВКЛЮЧЁН") : TEXT("ВЫКЛЮЧЕН"), BuffFx.Num(), BuffFxWaitSince.Num());
	auto Dump = [this, &S](const TMap<int32, FAsdaEffectList>& Map, const TCHAR* What, bool bMob)
	{
		for (const TPair<int32, FAsdaEffectList>& It : Map)
		{
			for (const FAsdaEffectEntry& E : It.Value.Items)
			{
				if (E.Type != 0) { continue; }
				const FAsdaBuffFxRow* R = BuffFx.Find(E.Id);
				const FString Prog = R ? (bMob ? R->MobFx : R->PlayerFx) : FString();
				S += FString::Printf(TEXT(" %s %d: состояние %-3d «%s» остаток=%d fx0=%s fx1=%s спавн=%s\n     программа: %s\n"),
					What, It.Key, E.Id, R ? *R->BuffName : TEXT("?"), E.RemainSec,
					E.Fx0.IsValid() ? TEXT("жив") : TEXT("—"), E.Fx1.IsValid() ? TEXT("жив") : TEXT("—"),
					E.bFxSpawned ? TEXT("да") : TEXT("ждёт носителя"),
					Prog.IsEmpty() ? TEXT("(пусто — в оригинале noop_stub3 либо файла .jeb нет ни в одной сборке)") : *Prog);
			}
		}
	};
	Dump(Lists, TEXT("персонаж userIdx"), /*bMob=*/false);
	Dump(MobLists, TEXT("сущность rid"), /*bMob=*/true);
	return S;
}
