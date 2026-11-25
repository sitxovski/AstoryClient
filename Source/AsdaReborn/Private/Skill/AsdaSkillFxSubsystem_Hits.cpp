// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillFxSubsystem_Hits.cpp — ★ R3-c 12.11.2026 (разрез AsdaSkillFxSubsystem.cpp, дословный перенос): удары: подтверждение 8215, отбрасывание, состояние 285, выпуск ударов, дампы, звук удара.
//   Общее для частей — в AsdaSkillFxSubsystem_Private.h; шапка и реверс — в AsdaSkillFxSubsystem.cpp.
#include "Skill/AsdaSkillFxSubsystem.h"
#include "AsdaNetSubsystem.h"   // ★ R1: прямое включение (раньше приходило транзитивно)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "AsdaEffectActor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Npc/AsdaMobActor.h"
#include "Resources/AsdaModelAnimSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaProjectileActor.h"
#include "World/AsdaWorldSubsystem.h"
#include "AsdaSelf.h"   // ★ «это я?» одной точкой (E2, 14.09.2026)
#include "World/AsdaCombatTrace.h"   // ★ 18.10.2026: журнал боя — записи урона навыка, клип, кадр удара, снаряд, эффекты
#include "World/AsdaCoord.h"               // ★ 29.10.2026 (Charge): тайлы 8215, game ↔ UE
#include "World/AsdaPathfinder.h"          // ★ 29.10.2026 (Charge): проходимость отброса
#include "UI/AsdaSkillDatabase.h"          // ★ 05.11.2026: behaviorType навыка — клетка 8215 (SendHitConfirm8215)
#include "UI/AsdaEffectsSubsystem.h"
#include "AsdaSkillFxSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSkillFxSubsystem*.cpp

// ★ 05.11.2026 [ОРИГИНАЛ] Подтверждение попадания 8215 вид 0 своего кастера по мобу — разбор у `UAsdaNetSubsystem::SendSkillHit8215`.
//   1 — как эталон (на показе каждой записи с битом 0, кроме рывка — у него свой 8215 вид 1); 0 — прежний порт (не слать: NpcServer
//   не списывал отложенный урон с мобов вне боя — «зомби» после площадной гранаты).
static ASDA_DEV_CVAR(int32, CVarAsdaSkillHitConfirm8215, TEXT("asda.Skill.HitConfirm8215"), 1,
	TEXT("[ОРИГИНАЛ] 8215 вид 0 (CounterAttack NpcServer) на каждое попадание своего навыка по мобу: 1 — как эталон; ")
	TEXT("0 — прежний порт (не слать; отложенный урон мобов вне боя не списывался)."),
	ECVF_Default);

// ══ ★★★ ВЫПУСК ПРИПАРКОВАННЫХ ЗАПИСЕЙ 4102 ═══════════════════════════════════════════════════
// ★ 05.11.2026 [ОРИГИНАЛ] 8215 вид 0 на показ записи своего кастера по мобу с битом попадания (`(флаги & 1) != 0` и `pOwnerChar+190260`
//   во всех ~40 путях эталона). Клетка — `netSendSkillCastGroundTarget@0xA040E0`: `behaviorType != 0` — клетка цели; 0 — точка отброса
//   «цель − 450·normalize(кастер − цель)», если на неё есть земля (`GroundTarget_PickOnMesh`; у нас — проходимость сетки), иначе клетка цели.
//   Рывок (537/597, `Char_UpdateDash`) шлёт свой 8215 вид 1 и вида 0 не шлёт — пропускаем.
void UAsdaSkillFxSubsystem::SendHitConfirm8215(const UAsdaNetSubsystem::FAsdaSkillHitRec& R)
{
	if (CVarAsdaSkillHitConfirm8215.GetValueOnGameThread() == 0) { return; }
	if (R.TargetKind != 0 || R.TargetIdx < 0 || R.SkillId <= 0) { return; }
	if (R.SkillId == 537 || R.SkillId == 597) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!Net || !WS || !AsdaSelf::IsSelfUserIdx(Net, R.CasterIdx)) { return; }
	// Бит 0 флагов записи — по сетевой записи 5257/5270; у записи 4102 флагов нет (там 1 «обычное попадание»).
	uint8 Flags = (uint8)R.HitFlags;
	if (const FCastHitFlag* F = CastHitFlags.Find(CastHitKey(R.CasterIdx, R.TargetKind, R.TargetIdx))) { Flags = (uint8)F->HitFlags; }
	if ((Flags & 1) == 0) { return; }
	AActor* M = WS->ActorByTarget(EAsdaTargetKind::Entity, R.TargetIdx);
	if (!M) { return; }   // эталон: `Entity_FindByRenderId(target)` не нашёл — пакета нет
	const FVector2D TG = AsdaCoord::UeToGame2D(M->GetActorLocation());
	FVector2D Cell = TG;
	const UAsdaSkillDatabase* SkDb = GI->GetSubsystem<UAsdaSkillDatabase>();
	const FAsdaSkillDef* D = SkDb ? SkDb->Get(R.SkillId) : nullptr;
	if (D && D->BehaviorType == 0)
	{
		AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor);
		if (Me)
		{
			const FVector2D Dir = (Me->GetGamePosXZ() - TG).GetSafeNormal();
			const FVector2D Knock = TG - Dir * 450.f;
			const bool bGround = !Me->WalkGrid || AsdaPath::IsLineWalkable(Me->WalkGrid, Knock / 100.f, Knock / 100.f);
			if (bGround) { Cell = Knock; }
		}
	}
	const FIntPoint Tile = AsdaCoord::GameToTile(Cell);
	Net->SendSkillHit8215(R.CasterIdx, R.TargetIdx, R.SkillId, Tile.X, Tile.Y);
}

// ★ 05.11.2026 [ОРИГИНАЛ] ОТЛЁТ ЦЕЛИ-МОБА — `charSkillCast_SetCastTargetDir@0xA296C0`: case 0 (сильный) — только если в точке отлёта
//   «цель − 450·normalize(кастер − цель)» есть земля (`GroundTarget_PickOnMesh`; у нас — проходимость сетки) и цель не ранга 3 и не в 14;
//   case 1/2 (слабый) — без пика земли; 3/4/прочее — только направление (сдвига и клипа нет). Зовут его ~9 путей показа удара по ОСНОВНОЙ
//   цели (`targetRenderId`), с битом попадания, у ЛЮБОГО кастера (`CSkillCast_ApplyEntityHitEffect`, `Skill_PlayHitEffectOnEntity`,
//   `CEntity_PlayEffectWithBone`, тики навыков…). 1 — как эталон, 0 — прежний порт (моб не отлетает).
static ASDA_DEV_CVAR(int32, CVarAsdaSkillKnockBack, TEXT("asda.Skill.KnockBack"), 1,
	TEXT("[ОРИГИНАЛ] Отлёт моба от удара навыка по behaviorType (0 — 450 ед. за 600 мс, клип 14; 1/2 — клип 15 на месте): 1 — как эталон; ")
	TEXT("0 — прежний порт (моб не отлетает)."),
	ECVF_Default);

void UAsdaSkillFxSubsystem::ApplyKnockBack(AAsdaMobActor* M, int32 SkillId, const FVector2D& PushDirGame, const FVector2D& FaceDirGame)
{
	if (!M || CVarAsdaSkillKnockBack.GetValueOnGameThread() == 0) { return; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaSkillDatabase* SkDb = GI ? GI->GetSubsystem<UAsdaSkillDatabase>() : nullptr;
	const FAsdaSkillDef* D = SkDb ? SkDb->Get(SkillId) : nullptr;
	if (!D) { return; }
	if (D->BehaviorType == 0)
	{
		UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
		const AAsdaPlayerCharacter* Me = WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor) : nullptr;
		const FVector2D TG = AsdaCoord::UeToGame2D(M->GetActorLocation());
		const FVector2D End = TG + PushDirGame.GetSafeNormal() * 450.f;
		if (Me && Me->WalkGrid && !AsdaPath::IsLineWalkable(Me->WalkGrid, End / 100.f, End / 100.f)) { return; }   // земли нет — отлёта нет
		M->NetKnockBack(1, PushDirGame, FaceDirGame);
	}
	else if (D->BehaviorType == 1 || D->BehaviorType == 2)
	{
		M->NetKnockBack(2, PushDirGame, FaceDirGame);
	}
}

// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] серверная клетка после «Отпора»: NpcServer на 8215 вид 1 (`sub_436E90` → `sub_4349A0@0x4349A0`, дизассемблер:
//   клетка → +0x7698/+0x769C, мир → +0x76D0/+0x76D4) ПЕРЕСТАВЛЯЕТ моба в клетку отброса, а клиент эталона на входе в 14 запоминает клетку
//   ДО отлёта (`ent[282] = ent[144]`), и доводчик `charCastDirectionalEffectIfFar` на конце клипа рывком возвращает моба туда (≥3 тайлов),
//   хотя сервер держит его в точке отброса. 1 — запомненная клетка = клетка отброса (моб остаётся, где его видит сервер); 0 — как эталон.
static ASDA_DEV_CVAR(int32, CVarAsdaSkillRepulse285Cell, TEXT("asda.Skill.Repulse285Cell"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] После отлёта от «Отпора» (285) серверная клетка моба = клетка отброса из 8215 (сервер переставил моба): 1 — моб не ")
	TEXT("возвращается рывком; 0 — как эталон (доводчик ставит на клетку до отлёта)."),
	ECVF_Default);

// ★ 05.11.2026 [ОРИГИНАЛ] Состояние 285 «Отпор» — разбор у объявления. Клетка — как у `netSendSkillCastGroundTarget` при behaviorType
//   навыка 285 = 0: «цель − 450·normalize(кастер − цель)», если там есть земля (`GroundTarget_PickOnMesh`; у нас — проходимость сетки),
//   иначе клетка цели. Персонаж: цель = его текущая цель-СУЩНОСТЬ (`pChar + 86276` — первая запись целей), отправитель — сам носитель
//   (эталон шлёт и у наблюдателя, как рывок; сервер подменяет отправителя). Моб: кастер = кто наложил (`mob + 4669`).
//   [ПОРТ] у своего кастера по мобу эталон считает точку `cameraComputeCollisionTarget` (по курсу кастера, знак — ветка FPU, в декомпиле
//   неоднозначна); берём ту же точку «от кастера», что и у прочих путей. Плавный клиентский отлёт (состояние 14) в порту не реализован —
//   моба переставляет сервер.
static ASDA_DEV_CVAR(int32, CVarAsdaSkillRepulse285, TEXT("asda.Skill.RepulseState285"), 1,
	TEXT("[ОРИГИНАЛ] Состояние 285 «Отпор»: 1 — 8215 вид 1 по цели (отброс NpcServer), как обработчики эталона; 0 — прежний порт (ничего)."),
	ECVF_Default);

void UAsdaSkillFxSubsystem::OnRepulseState285(int32 HostKey, bool bMob, int32 CasterUserIdx)
{
	if (CVarAsdaSkillRepulse285.GetValueOnGameThread() == 0 || HostKey < 0) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	UWorld* W = GetWorld();
	if (!Net || !WS || !W) { return; }
	const int64 K = ((int64)HostKey << 1) | (bMob ? 1 : 0);
	const double Now = W->GetTimeSeconds();
	if (const double* Was = Repulse285SentS.Find(K); Was && Now - *Was < 1.0) { return; }   // одно наложение — один пакет
	AActor* Owner = nullptr; AActor* Target = nullptr; int32 SenderIdx = -1;
	if (bMob)
	{
		Target = WS->ActorByTarget(EAsdaTargetKind::Entity, HostKey);
		Owner = (CasterUserIdx >= 0) ? WS->ActorByTarget(EAsdaTargetKind::Character, CasterUserIdx) : nullptr;
		if (!Owner && AsdaSelf::IsSelfUserIdx(Net, CasterUserIdx)) { Owner = WS->LocalPlayerActor; }
		SenderIdx = CasterUserIdx;
	}
	else
	{
		AActor* HostA = AsdaSelf::IsSelfUserIdx(Net, HostKey) ? (AActor*)WS->LocalPlayerActor.Get()
			: WS->ActorByTarget(EAsdaTargetKind::Character, HostKey);
		AAsdaPlayerCharacter* Host = Cast<AAsdaPlayerCharacter>(HostA);
		Owner = Host;
		Target = Host ? Cast<AAsdaMobActor>(Host->AttackFaceTarget.Get()) : nullptr;   // `pChar + 86276` — цель-сущность
		SenderIdx = HostKey;
	}
	AAsdaMobActor* M = Cast<AAsdaMobActor>(Target);
	if (!M || !Owner || SenderIdx < 0) { return; }   // эталон: `Entity_FindByRenderId` не нашёл / носителя нет — пакета нет
	const FVector2D TG = AsdaCoord::UeToGame2D(M->GetActorLocation());
	const FVector2D OG = AsdaCoord::UeToGame2D(Owner->GetActorLocation());
	FVector2D Cell = TG;
	// Направление отлёта. Свой кастер по мобу — `netSendSkillCastAtTarget` → `cameraComputeCollisionTarget@0xF88040`: ДИЗАССЕМБЛЕР
	//   (0xF8809B..0xF8811D): d = −forward кастера; сравнение `|кастер − цель|²` с `|кастер − 1.1·d|²` (вторая — квадрат МИРОВОЙ точки,
	//   всегда больше) ⇒ переход 0xF8811D берётся всегда, знак не меняется; точка = цель − 450·d = цель + 450·forward кастера.
	//   Прочие ветки — `cameraComputeSpringTarget`: цель − 450·normalize(кастер − цель).
	FVector2D Push = (TG - OG).GetSafeNormal();
	if (bMob && AsdaSelf::IsSelfUserIdx(Net, CasterUserIdx))
	{
		if (const AAsdaPlayerCharacter* OC = Cast<AAsdaPlayerCharacter>(Owner))
		{
			const FVector2D Fw = OC->GetFacingDirGame();
			if (!Fw.IsNearlyZero()) { Push = Fw.GetSafeNormal(); }
		}
	}
	const FVector2D Knock = TG + Push * 450.f;
	const AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor);
	if (!Me || !Me->WalkGrid || AsdaPath::IsLineWalkable(Me->WalkGrid, Knock / 100.f, Knock / 100.f)) { Cell = Knock; }
	const FIntPoint Tile = AsdaCoord::GameToTile(Cell);
	Repulse285SentS.Add(K, Now);
	Net->SendDash8215(SenderIdx, M->NetRenderId, 285, Tile.X, Tile.Y);   // тело 8215 вида 1 — то же, что у рывка; навык = номер состояния
	const AAsdaPlayerCharacter* OPk = Cast<AAsdaPlayerCharacter>(Owner);
	ApplyKnockBack(M, 285, Push, OPk ? -OPk->GetFacingDirGame() : FVector2D::ZeroVector);
	if (CVarAsdaSkillRepulse285Cell.GetValueOnGameThread() != 0 && M->IsKnockActive() && Cell == Knock)
	{
		M->RememberServerCell(AsdaCoord::TileCenterGame(Tile));   // сервер поставил моба в клетку отброса (`sub_4349A0`)
	}   // клиентский отлёт: `charSkillCast_SetCastTargetDir` / `skillSetupTargetPos` (навык 285 — behaviorType 0)
	ASDA_CTRACE(EAsdaCT::Send, TEXT("состояние 285 «Отпор» на %s idx=%d: 8215 вид 1 по мобу rid=%d, отправитель %d, клетка (%d,%d)"),
		bMob ? TEXT("мобе") : TEXT("персонаже"), HostKey, M->NetRenderId, SenderIdx, Tile.X, Tile.Y);
}

void UAsdaSkillFxSubsystem::ReleaseHits(TArray<UAsdaNetSubsystem::FAsdaSkillHitRec>& Hits,
	bool bHasHitFx, const TCHAR* Where)
{
	if (Hits.Num() == 0) { return; }
	// ★ Забираем список СЕБЕ до рассылки: подписчик (мир-подсистема) на смерти моба снимает цель,
	//   гасит показ и т.п. — исходный массив к возврату может уже не существовать.
	TArray<UAsdaNetSubsystem::FAsdaSkillHitRec> Batch = MoveTemp(Hits);
	Hits.Reset();
	for (const UAsdaNetSubsystem::FAsdaSkillHitRec& R : Batch)
	{
		// ★★★ ПРОМАХ. Эталон различает попадание и промах по флагу сети (`pCaster[1].gap_0[4] & 1`,
		//   байт `+7` 29-байтной записи 5257/5270 — в порту эти записи ещё не разбираются, §14.6 п.2),
		//   а ТЕКСТ выбирает по величине урона: `CharFx_ShowSkillDamageText@0xA7FC00` и
		//   `Combat_ShowDamageFeedback@0xA80390` — `if (урон) {цифра} else {«MISS» стилем 5 + нотис
		//   20010/1159}`. Порт берёт ту же величину из единственного разобранного канала — 4102.
		// ⚠ ГЕЙТ `bHasHitFx` обязателен: без него «MISS» рисовался бы на КАЖДОМ площадном баффе с
		//   нулевым уроном (живой пакет 06:00:30: навык 185 «Ледяная ловушка», урон 0 по трём мобам).
		//   В эталоне такие навыки идут ДРУГИМ тиком (`charTickAutoActionLoop`), у которого ветки
		//   удара нет вовсе, поэтому до текста «MISS» дело не доходит.
		// ★★★ ФЛАГ ПОПАДАНИЯ (байт +7 29-байтной записи 5257/5270, бит0) — АВТОРИТЕТ, когда сетевая
		//   запись по этой цели пришла: бит0 == 0 при нулевом уроне = настоящий ПРОМАХ; бит0 == 1 при
		//   нулевом уроне = попадание без урона (бафф) — «MISS» НЕ рисуется. Эвристика по 4102
		//   (`ApplyFlag == 1`) остаётся запасной для записей без 29-байтной пары.
		bool bShowMiss = bHasHitFx && (R.Damage == 0);
		if (bShowMiss)
		{
			const double Now = FPlatformTime::Seconds();
			if (const FCastHitFlag* F = CastHitFlags.Find(CastHitKey(R.CasterIdx, R.TargetKind, R.TargetIdx));
				F && Now - F->AtS <= 10.0)
			{
				bShowMiss = (F->HitFlags & 1) == 0;    // явный флаг сети
				// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (ревизия): нулевая ДОЛЯ многоударного (`SplitDamageIn3`: урон < 3 → 0/0/d, `Phase >= 0`) при
				//   бите 0 = 1 — эталон зовёт показ с уроном 0 (`CharFx_ShowSkillDamageText` effects_particles.c:7681-7693 → ветка
				//   «урон 0»: «MISS» + 20010 кастеру); прежде порт глушил её как «попадание без урона (бафф)».
				if (!bShowMiss && R.Phase >= 0 && GAsdaSkillHitFrameAlways != 0) { bShowMiss = true; }
			}
			else
			{
				bShowMiss = (R.ApplyFlag == 1);        // запасная эвристика (как раньше)
			}
		}
		// ★ Ревизия 5 [ОРИГИНАЛ]: запись вида 9 (911) — показ урона только при бите попадания (`& 1`, skill_buff.c:12130-12136 / 12160),
		//   и с уроном 0 этот показ рисует «MISS» (`CharFx_ShowSkillDamageText` / `Combat_ShowDamageFeedback` — ветка «урон 0»).
		if (GAsdaSkillRecordReleaseKind == 9) { bShowMiss = (R.Damage == 0) && (R.HitFlags & 1) != 0; }
		ASDA_CTRACE(EAsdaCT::Queue, TEXT("ВЫПУСК записи урона навыка %d → %s idx=%d: урон=%d HP после=%d крит=%s MISS=%s фаза=%d кастер=%d — момент «%s»%s"),
			R.SkillId, R.TargetKind == 1 ? TEXT("игрок") : TEXT("моб"), R.TargetIdx, R.Damage, R.HpAfter,
			(R.HitFlags & 4) ? TEXT("ДА") : TEXT("нет"), bShowMiss ? TEXT("ДА") : TEXT("нет"), R.Phase, R.CasterIdx, Where,
			bHasHitFx ? TEXT("") : TEXT(" (без ветки удара — MISS не рисуется)"));
		// ★ 10.11.2026 [ОРИГИНАЛ] `asda.Skill.MissNoDamageText` — разбор у ключа: промах с ненулевым уроном — без цифры и вычитания.
		UAsdaNetSubsystem::FAsdaSkillHitRec RB = R;
		if (GAsdaSkillMissNoDamageText != 0 && !R.bFrom4102 && (R.HitFlags & 0x21) == 0 && R.Damage != 0)
		{
			RB.Damage = 0;
			bShowMiss = (GAsdaSkillRecordReleaseKind == 0) && bHasHitFx;
			ASDA_CTRACE(EAsdaCT::Queue, TEXT("запись навыка %d → idx=%d: бит попадания 0 при уроне %d — цифры и вычитания нет (эталон), MISS=%s"),
				R.SkillId, R.TargetIdx, R.Damage, bShowMiss ? TEXT("да") : TEXT("нет"));
		}
		OnSkillHitMoment.Broadcast(RB, bShowMiss);
		SendHitConfirm8215(R);   // ★ 05.11.2026 [ОРИГИНАЛ] подтверждение попадания своему серверу — разбор у ключа asda.Skill.HitConfirm8215
		// ★ 05.11.2026 [ОРИГИНАЛ] отлёт основной цели-моба (разбор у ключа asda.Skill.KnockBack). Рывок (537/597) отлёт делает сам.
		if (R.TargetKind == 0 && R.TargetIdx >= 0 && R.SkillId != 537 && R.SkillId != 597)
		{
			const FCastHitFlag* F = CastHitFlags.Find(CastHitKey(R.CasterIdx, R.TargetKind, R.TargetIdx));
			UGameInstance* GIk = GetGameInstance();
			UAsdaWorldSubsystem* WSk = GIk ? GIk->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			UAsdaNetSubsystem* Nk = GIk ? GIk->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			// ★ 10.11.2026 (ревизия 4): у выпуска записи вида 9 отлёта нет; у вида 13 — каждой задетой цели-мобу.
			const bool bKnockByRec = GAsdaSkillRecordReleaseKind != 9 && (F && (F->bMain || GAsdaSkillRecordReleaseKind == 13));
			if (F && bKnockByRec && (F->HitFlags & 1) && WSk)
			{
				AAsdaMobActor* Mk = Cast<AAsdaMobActor>(WSk->ActorByTarget(EAsdaTargetKind::Entity, R.TargetIdx));
				AActor* Ck = (Nk && AsdaSelf::IsSelfUserIdx(Nk, R.CasterIdx)) ? (AActor*)WSk->LocalPlayerActor.Get() : (AActor*)WSk->ActorByTarget(EAsdaTargetKind::Character, R.CasterIdx);
				if (Mk && Ck)
				{
					const AAsdaPlayerCharacter* Pk = Cast<AAsdaPlayerCharacter>(Ck);
					ApplyKnockBack(Mk, R.SkillId, AsdaCoord::UeToGame2D(Mk->GetActorLocation()) - AsdaCoord::UeToGame2D(Ck->GetActorLocation()),
						Pk ? -Pk->GetFacingDirGame() : FVector2D::ZeroVector);
				}
			}
		}
	}
	UE_LOG(LogAsdaSkillFx, Log,
		TEXT("[навыки-стык] выпущено записей 4102: %d — момент «%s»"), Batch.Num(), Where);
}

// ★ Печать состояния ПОКАЗА (консоль `asda.VerifySkillFx`) — см. объявление в заголовке.
void UAsdaSkillFxSubsystem::DumpActiveFx() const
{
	UE_LOG(LogAsdaSkillFx, Display,
		TEXT("=== ПОКАЗ НАВЫКОВ: активных %d, носителей в полёте %d, эталонная геометрия квадов/колец = %s"),
		Active.Num(), Flights.Num(), GAsdaSkillFxStrictQuad ? TEXT("ВКЛ (канон)") : TEXT("выкл (картовая ветка)"));
	for (const FAsdaSkillFxActive& A : Active)
	{
		const AAsdaPlayerCharacter* C = A.Caster.Get();
		const bool bBone = C && C->BaseSkel && C->BaseSkel->GetBoneIndex(A.FxBone) != INDEX_NONE;
		const FVector BoneLoc = bBone ? C->BaseSkel->GetBoneLocation(A.FxBone) : FVector::ZeroVector;
		UE_LOG(LogAsdaSkillFx, Display,
			TEXT("  навык %d, клип %s (anim_%d), кость каста %s%s, кость снаряда %s, эффект-акторов %d"),
			A.SkillId, *A.Clip, A.AnimIdx, *A.FxBone.ToString(),
			bBone ? *FString::Printf(TEXT(" мир=%s"), *BoneLoc.ToCompactString()) : TEXT(" (НЕТ НА СКЕЛЕТЕ)"),
			*A.ProjBone.ToString(), A.Fx.Num());
		for (const TWeakObjectPtr<AActor>& W : A.Fx)
		{
			const AAsdaEffectActor* E = Cast<AAsdaEffectActor>(W.Get());
			if (!E) { continue; }
			// Как держится: attach-6 (мировая матрица персонажа) / attach-10 (кость) / свободный.
			const TCHAR* Hold = E->bFollowCharWorld ? TEXT("attach-6 (мир.матрица персонажа)")
				: (E->bFollowBoneFull ? TEXT("attach-10 (кость)") : TEXT("свободный"));
			// ★ ЗАМЕР МАСШТАБА И ДЛИТЕЛЬНОСТИ (аудит §14): реальный габарит на экране = мировые
			//   границы отрисованной геометрии; «жить осталось» = остаток `SetLifeSpan`;
			//   «программа» = самый долгий эмиттер (startDelay + период) по данным `.jeb`.
			FVector BOrig, BExt;
			E->GetActorBounds(/*bOnlyCollidingComponents=*/false, BOrig, BExt);
			float Span = 0.f;
			int32 NLoop = 0;
			for (const FAsdaFxEmitter& Em : E->Emitters)
			{
				Span = FMath::Max(Span, Em.StartDelay + Em.Period);
				if (Em.EndMode != 0) { ++NLoop; }
			}
			UE_LOG(LogAsdaSkillFx, Display,
				TEXT("      %-40s эмиттеров=%2d (луперов %d) держится: %-32s strictQuad=%d scale=%.3f ")
				TEXT("габарит=%.0f×%.0f×%.0f uu программа=%.2fс осталось=%.2fс мир=%s до кости=%.0f uu"),
				*E->EffectName, E->Emitters.Num(), NLoop, Hold, E->bStrictQuadAttach ? 1 : 0,
				E->GetActorScale3D().X, BExt.X * 2.f, BExt.Y * 2.f, BExt.Z * 2.f,
				Span, E->GetLifeSpan(),
				*E->GetActorLocation().ToCompactString(),
				bBone ? FVector::Dist(E->GetActorLocation(), BoneLoc) : -1.f);
		}
	}
	for (const FAsdaSkillFxFlight& F : Flights)
	{
		const AAsdaProjectileActor* P = F.Proj.Get();
		UE_LOG(LogAsdaSkillFx, Display,
			TEXT("  носитель навыка %d фаза %d: %s, шлейф=%s, цель=%s"),
			F.SkillId, F.Phase,
			P ? *P->GetActorLocation().ToCompactString() : TEXT("(актор мёртв)"),
			(P && P->TrailFx) ? *P->TrailFx->GetActorLocation().ToCompactString() : TEXT("НЕТ"),
			*F.DestLoc.ToCompactString());
	}
}

// Печать состояния стыка (консоль `asda.VerifySkillHitJoin`).
void UAsdaSkillFxSubsystem::DumpHitJoin() const
{
	UE_LOG(LogAsdaSkillFx, Display, TEXT("=== СТЫК «урон ↔ визуал»: показов %d, носителей в полёте %d"),
		Active.Num(), Flights.Num());
	for (const FAsdaSkillFxActive& A : Active)
	{
		UE_LOG(LogAsdaSkillFx, Display,
			TEXT("  показ навык %d (кастер uidx=%d, тик %s, снаряд=%s): фазы COMBO %.0f/%.0f/%.0f мс ")
			TEXT("исп=%d%d%d выстрел=%d%d%d удар-эфф=%d%d%d; припарковано записей 4102: %d"),
			A.SkillId, A.CasterUserIdx, *A.TickFn, A.bRowProj ? TEXT("ДА") : TEXT("нет"),
			A.PhaseMs[0], A.PhaseMs[1], A.PhaseMs[2],
			A.bPhaseUsed[0], A.bPhaseUsed[1], A.bPhaseUsed[2],
			A.bPhaseFired[0], A.bPhaseFired[1], A.bPhaseFired[2],
			A.bPhaseHasHitFx[0], A.bPhaseHasHitFx[1], A.bPhaseHasHitFx[2],
			A.PendingHits.Num());
		for (const UAsdaNetSubsystem::FAsdaSkillHitRec& R : A.PendingHits)
		{
			UE_LOG(LogAsdaSkillFx, Display,
				TEXT("      запись: цель kind=%d idx=%d, навык/эффект=%d, урон=%d, HP после=%d, ")
				TEXT("apply=%d, флаги=0x%02X, фаза=%d"),
				R.TargetKind, R.TargetIdx, R.SkillId, R.Damage, R.HpAfter, R.ApplyFlag,
				R.HitFlags, R.Phase);
		}
	}
	for (const FAsdaSkillFxFlight& F : Flights)
	{
		UE_LOG(LogAsdaSkillFx, Display,
			TEXT("  носитель навык %d фаза %d (кастер uidx=%d, цель %s, жив=%s): записей 4102 в полёте %d"),
			F.SkillId, F.Phase, F.CasterUserIdx,
			F.Target.IsValid() ? *F.Target->GetName() : TEXT("нет"),
			F.Proj.IsValid() ? TEXT("да") : TEXT("НЕТ"), F.Hits.Num());
	}
}

// ══ ★★★ ПАРКОВКА ЗАПИСИ 4102 ══════════════════════════════════════════════════════════════════
// ⚠ СОПОСТАВЛЕНИЕ ИДЁТ ПО КАСТЕРУ, А НЕ ПО КОДУ НАВЫКА — и это не упрощение, а факт данных.
//   Поле `+3` записи 4102 — id ПРИМЕНЁННОГО ЭФФЕКТА, а не код каста: сервер
//   (`CMonsterManager::sub_4283F0`, `GameServer.c:17443-17520`) ветвится по нему как по коду
//   суб-эффекта (110/160/173/181..185/220/248/577/924), и живой пакет 06:00:30 несёт `skillId=185`
//   на касте навыка **784** («Ледяная ловушка»). Эталон это различие и не использует: применение
//   живёт в ЕДИНСТВЕННОМ слоте персонажа (`effectSlots[100].field_4`), одновременно у персонажа
//   идёт ровно один каст — поэтому тик показывает то, что сервер прислал ЭТОМУ кастеру.
//   Совпадение кода навыка используем как ПРЕДПОЧТЕНИЕ (если совпал — берём именно его).
bool UAsdaSkillFxSubsystem::DeferSkillHit(const UAsdaNetSubsystem::FAsdaSkillHitRec& R)
{
	// 1) Носитель в полёте у этого кастера — урон едет В НЁМ ([[weapon-projectiles]]).
	FAsdaSkillFxFlight* PickF = nullptr;
	for (FAsdaSkillFxFlight& F : Flights)
	{
		// ★ Бестелесный носитель (`bDelayOnly`) актора не имеет — проверка `Proj.IsValid()` его бы
		//   отбросила, и урон 905/915 снова применялся бы мгновенно.
		if (F.bArrived || F.CasterUserIdx != R.CasterIdx) { continue; }
		if (F.bRecord) { continue; }   // ★ 10.11.2026: рамка отложенной записи — свои записи уже взяла, чужие не принимает
		if (!F.bDelayOnly && !F.Proj.IsValid()) { continue; }
		// ★ Запись с НОМЕРОМ ФАЗЫ (многоударный навык) едет только в носитель СВОЕЙ фазы.
		if (R.Phase >= 0 && F.Phase != R.Phase) { continue; }
		if (F.SkillId == R.SkillId) { PickF = &F; break; }        // точное совпадение — приоритет
		if (!PickF) { PickF = &F; }
	}
	if (PickF)
	{
		ASDA_CTRACE(EAsdaCT::Queue, TEXT("запись урона навыка %d (idx=%d урон=%d) припаркована В СНАРЯД фазы %d навыка %d — урон по прилёте"),
			R.SkillId, R.TargetIdx, R.Damage, PickF->Phase, PickF->SkillId);
		PickF->Hits.Add(R);
		return true;
	}
	// 2) Идущий показ этого кастера, у которого ещё есть невыстрелившая фаза.
	FAsdaSkillFxActive* PickA = nullptr;
	for (FAsdaSkillFxActive& A : Active)
	{
		if (A.CasterUserIdx != R.CasterIdx) { continue; }
		bool bPhaseAhead = false;
		// ★ 29.10.2026 (Charge): у рывка записи ждут ПРИБЫТИЯ (фаза 1 тика `Char_UpdateDash` — там `Skill_ApplyEffectResult`).
		if (A.bDash && A.DashPhase < 1) { bPhaseAhead = true; }
		else if (R.Phase >= 0)
		{
			// ★ Запись многоударного навыка ждёт СВОЮ фазу — и только её.
			bPhaseAhead = A.bPhaseUsed[R.Phase] && !A.bPhaseFired[R.Phase];
		}
		else
		{
			for (int32 Ph = 0; Ph < 4; ++Ph)   // ★ 08.11: 4 фазы (513)
			{
				if (A.bPhaseUsed[Ph] && !A.bPhaseFired[Ph]) { bPhaseAhead = true; break; }
			}
		}
		if (!bPhaseAhead) { continue; }   // все фазы отработали — момент показа уже прошёл
		if (A.SkillId == R.SkillId) { PickA = &A; break; }
		if (!PickA) { PickA = &A; }
	}
	if (!PickA && R.RecDelayMs > 0.f)
	{
		// ★ Ревизия 5 [ПОРТ]: доля ОТЛОЖЕННОЙ записи (907/911), пришедшая, когда порог фазы показа уже прошёл. У эталона так не бывает:
		//   5257/5270 сами запускают клип (`Recv_SkillCast@0xF4E370`), записи строит тик на пороге (`if (!*(pChar+47781))`, один раз),
		//   срок узла — от постройки (`objInitTimed` обнуляет накопленное время). Порт показ запускает раньше прихода записей, поэтому
		//   запасной путь: срок — от ПРИХОДА записи (как «постройка сейчас»); эффекты/звуки записи — из строки показа этого кастера.
		UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		const int32 Ms = FMath::RoundToInt(R.RecDelayMs);
		FAsdaSkillFxFlight* F = Flights.FindByPredicate([&R, Ms, W](const FAsdaSkillFxFlight& X)
			{ return X.bRecord && !X.bArrived && X.CasterUserIdx == R.CasterIdx && X.SkillId == R.SkillId
				&& W && FMath::IsNearlyEqual(X.ArriveAtS, W->GetTimeSeconds() + Ms / 1000.0, 0.02); });
		if (!F)
		{
			F = &Flights.AddDefaulted_GetRef();
			F->bDelayOnly = true;
			F->bRecord = true;
			F->SkillId = R.SkillId;
			F->CasterUserIdx = R.CasterIdx;
			F->ArriveAtS = W ? W->GetTimeSeconds() + Ms / 1000.0 : 0.0;
			if (const FAsdaSkillFxActive* Show = Active.FindByPredicate([&R](const FAsdaSkillFxActive& X) { return X.CasterUserIdx == R.CasterIdx && X.SkillId == R.SkillId; }))
			{
				F->Caster = Show->Caster; F->Target = Show->Target; F->bTargetIsMob = Show->bTargetIsMob; F->SndHit = Show->SndHit; F->Phase = 0;
				F->bHasHitFx = Show->bPhaseHasHitFx[0];
				F->DestLoc = Show->Target.IsValid() ? Show->Target->GetActorLocation() : FVector::ZeroVector;
				for (const FAsdaSkillFxSpawn& Sp : PhaseHitList(*Show, 0)) { if (Sp.bRecord) { F->HitFx.Add(Sp); } }
			}
			if (W) { DebugRecordArmTimes.Add(W->GetTimeSeconds()); if (DebugRecordArmTimes.Num() > 64) { DebugRecordArmTimes.RemoveAt(0); } }
		}
		F->Hits.Add(R);
		ASDA_CTRACE(EAsdaCT::Queue, TEXT("запись урона навыка %d (idx=%d урон=%d кастер %d) пришла после порога фазы — отложенная запись через %d мс от прихода"),
			R.SkillId, R.TargetIdx, R.Damage, R.CasterIdx, Ms);
		return true;
	}
	if (!PickA)
	{
		ASDA_CTRACE(EAsdaCT::Queue, TEXT("запись урона навыка %d (idx=%d урон=%d кастер %d) НЕ припаркована: нет показа с невыстрелившей фазой — выпуск сразу"),
			R.SkillId, R.TargetIdx, R.Damage, R.CasterIdx);
		return false;
	}
	ASDA_CTRACE(EAsdaCT::Queue, TEXT("запись урона навыка %d (idx=%d урон=%d) припаркована в ПОКАЗ навыка %d — ждёт кадра удара (фаза %d)"),
		R.SkillId, R.TargetIdx, R.Damage, PickA->SkillId, R.Phase);
	PickA->PendingHits.Add(R);
	return true;
}

// ══ ★★★ «ЭТУ ЦЕЛЬ УЖЕ ЖДЁТ СМЕРТЕЛЬНАЯ ЗАПИСЬ» ════════════════════════════════════════════════
//   Объявление и назначение — в заголовке. Ровно один предикат на все три места хранения записей.
bool UAsdaSkillFxSubsystem::HasPendingLethalHit(int32 TargetKind, int32 TargetIdx) const
{
	// −1 в `HpAfter` = «сервер HP не сообщил», такой записью цель НЕ убьётся ⇒ держать смерть нельзя.
	auto Lethal = [TargetKind, TargetIdx](const UAsdaNetSubsystem::FAsdaSkillHitRec& R)
	{
		return R.TargetKind == TargetKind && R.TargetIdx == TargetIdx
			&& R.HpAfter <= 0 && R.HpAfter != -1;
	};
	for (const FPendingCastHits& P : PendingCastHits)
	{
		for (const UAsdaNetSubsystem::FAsdaSkillHitRec& R : P.Hits) { if (Lethal(R)) { return true; } }
	}
	for (const FAsdaSkillFxActive& A : Active)
	{
		for (const UAsdaNetSubsystem::FAsdaSkillHitRec& R : A.PendingHits) { if (Lethal(R)) { return true; } }
	}
	for (const FAsdaSkillFxFlight& F : Flights)
	{
		for (const UAsdaNetSubsystem::FAsdaSkillHitRec& R : F.Hits) { if (Lethal(R)) { return true; } }
	}
	return false;
}

void UAsdaSkillFxSubsystem::PlayHitSound(const FAsdaSkillFxActive& A, int32 Phase)
{
	// ★ 10.11.2026 (job4-vfx): при выпуске отложенной записи звучат только элементы «~rec» строки SndHit (разбор ниже).
	if (GAsdaSkillPrimaryMissGate) { return; }   // ★ 10.11.2026 [ОРИГИНАЛ]: промах по основной цели у тика на помощнике — звука удара нет
	const bool bQualified = A.SndHit.Contains(TEXT("/")) || A.SndHit.Contains(TEXT("@")) || A.SndHit.Contains(TEXT("~"));
	if (bSpawningRecord && !bQualified) { return; }
	// ★ 08.11.2026 (`asda.Skill.HexaFourHits`): звук своей фазы — список «;», каждое имя отдельным звуком (тик 513 играет два подряд).
	if (!bSpawningRecord && Phase >= 0 && Phase < 4 && !A.PhaseSnd[Phase].IsEmpty())
	{
		AAsdaPlayerCharacter* PC = A.Caster.Get();
		UAsdaSoundSubsystem* PSnd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;
		if (!PC || !PSnd) { return; }
		const AActor* PTgt = A.Target.Get();
		const FVector PAt = (PTgt && (!A.bTargetIsMob || GAsdaSkillMobHitSoundAtTarget != 0)) ? PTgt->GetActorLocation() : PC->GetActorLocation();   // ★ 10.11 [ИСПРАВЛЕНИЕ] asda.Skill.MobHitSoundAtTarget
		TArray<FString> Names;
		A.PhaseSnd[Phase].ParseIntoArray(Names, TEXT(";"), true);
		for (const FString& N : Names)
		{
			PSnd->PlaySound3DAs(FName(*FPaths::GetBaseFilename(N).ToLower()), PAt, EAsdaSoundClass::Magic, PTgt ? PTgt : PC);
		}
		return;
	}
	if (A.SndHit.IsEmpty()) { return; }
	AAsdaPlayerCharacter* C = A.Caster.Get();
	UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;
	if (!C || !Snd) { return; }
	// ★ 10.11.2026 (job4-vfx, 907 в PvP как в оригинале): СПИСОК звуков с уточнителями — `имя[/char|/mob][@caster|@target]` через «;».
	//   Разбирается, только если в строке есть «/» или «@» (прежние строки, в т.ч. «a;b» у 513/915, идут старой веткой целиком).
	//   «/char» — только при цели-игроке, «/mob» — только при цели-мобе, «/rchar» — при цели-игроке по разу на каждую запись игрока
	//   с попаданием; «@caster»/«@target» — точка звука вместо общего правила ниже.
	//   907: `S_c_M3frastr03.wav@caster` — звук отложенной записи вида 13 у кастера (`pOwnerChar+344`, effects_particles.c:6226/6263;
	//   имя кладёт загрузчик в `field_598`, entity_char.c:30551), `S_c_R3blast02.wav/char@target` — у цели-игрока (effects_particles.c:8878).
	if (bQualified)
	{
		const AActor* QTgt = A.Target.Get();
		TArray<FString> Items;
		A.SndHit.ParseIntoArray(Items, TEXT(";"), true);
		for (FString It : Items)
		{
			// «~rec» — звук отложенной записи: только при её выпуске; без суффикса — только на пороге фазы.
			const bool bRecItem = It.RemoveFromEnd(TEXT("~rec"));
			if (bRecItem != bSpawningRecord) { continue; }
			FString Where;
			if (It.Split(TEXT("@"), &It, &Where)) { Where = Where.ToLower(); }
			FString Name = It, Kind;
			if (It.Split(TEXT("/"), &Name, &Kind)) { Kind = Kind.ToLower(); }
			if ((Kind == TEXT("char") || Kind == TEXT("rchar")) && A.bTargetIsMob) { continue; }
			if (Kind == TEXT("mob") && !A.bTargetIsMob) { continue; }
			// ★ Ревизия 3: «/rchar» — звук записи вида 13 по разу на КАЖДУЮ запись цели-игрока с флагом попадания
			//   (effects_particles.c:6276-6277, внутри цикла под `& 1`); ни одного попадания — звука нет.
			int32 Times = 1;
			if (Kind == TEXT("rchar"))
			{
				Times = 0;
				for (const UAsdaNetSubsystem::FAsdaSkillHitRec& H : A.PendingHits)
				{
					if (AsdaTargetKindFromHitRec(H.TargetKind) != EAsdaTargetKind::Entity && H.TargetIdx >= 0 && (H.HitFlags & 1) != 0
						&& (H.Phase < 0 || H.Phase == Phase)) { ++Times; }
				}
			}
			// ★ 10.11 [ИСПРАВЛЕНИЕ] asda.Skill.MobHitSoundAtTarget: при цели-мобе звук — у моба (и «@caster» тоже).
			const bool bMobAtTgt = A.bTargetIsMob && GAsdaSkillMobHitSoundAtTarget != 0 && QTgt;
			const bool bAtTgt = Where.IsEmpty() ? ((!A.bTargetIsMob && QTgt) || bMobAtTgt) : ((Where == TEXT("target") && QTgt) || bMobAtTgt);
			const FVector QAt = bAtTgt ? QTgt->GetActorLocation() : C->GetActorLocation();
			for (int32 k = 0; k < Times; ++k)
			{
				Snd->PlaySound3DAs(FName(*FPaths::GetBaseFilename(Name).ToLower()), QAt, EAsdaSoundClass::Magic, bAtTgt ? QTgt : C);
			}
		}
		return;
	}
	// Эталон играет звук удара в позиции КАСТЕРА (`sound3DPlayAtOrigin(..., pCaster->soundPos*)`)
	// в ветке цели-моба и в позиции ЦЕЛИ в ветке цели-игрока (`CSkillCast_ApplySingleTargetHit`).
	const AActor* Tgt = A.Target.Get();
	const FVector At = (Tgt && (!A.bTargetIsMob || GAsdaSkillMobHitSoundAtTarget != 0)) ? Tgt->GetActorLocation() : C->GetActorLocation();   // ★ 10.11 [ИСПРАВЛЕНИЕ]
	Snd->PlaySound3DAs(FName(*FPaths::GetBaseFilename(A.SndHit).ToLower()), At, EAsdaSoundClass::Magic, Tgt ? Tgt : C);   // ★ 30.10: «магия»
}
