// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillFxSubsystem_Dash.cpp — ★ R3-c 12.11.2026 (разрез AsdaSkillFxSubsystem.cpp, дословный перенос): рывок: BeginDash, TickDash, GetDashState.
//   Общее для частей — в AsdaSkillFxSubsystem_Private.h; шапка и реверс — в AsdaSkillFxSubsystem.cpp.
#include "Skill/AsdaSkillFxSubsystem.h"
#include "World/AsdaCombatChat.h"   // ★ R5: AsdaCombatChat::Line — строка боя в системный чат (одна копия)
#include "AsdaNetSubsystem.h"   // ★ R1: прямое включение (раньше приходило транзитивно)

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Npc/AsdaMobActor.h"
#include "Resources/AsdaModelAnimSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldSubsystem.h"
#include "AsdaSelf.h"   // ★ «это я?» одной точкой (E2, 14.09.2026)
#include "World/AsdaCombatTrace.h"   // ★ 18.10.2026: журнал боя — записи урона навыка, клип, кадр удара, снаряд, эффекты
#include "World/AsdaCoord.h"               // ★ 29.10.2026 (Charge): тайлы 8215, game ↔ UE
#include "World/AsdaPathfinder.h"          // ★ 29.10.2026 (Charge): проходимость отброса
#include "UI/AsdaSkillDatabase.h"          // ★ 05.11.2026: behaviorType навыка — клетка 8215 (SendHitConfirm8215)
#include "World/AsdaPlayerController.h"    // ★ 29.10.2026 (Charge): сброс маршрута своего персонажа на время рывка
#include "World/AsdaFloatingTextSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "AsdaSkillFxSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSkillFxSubsystem*.cpp

// ═══ ★★★ 29.10.2026 РЫВОК Charge 537 / Mad Charge 597 — `Char_UpdateDash@0xB3B010` (entity_char.c:42387-42870) [ОРИГИНАЛ] ═══
//   Баг игроков 19–24.10: «Charge ничего не делает, стан с задержкой, волк успевает ударить». Журналы 8 кастов: сервер вешает стан
//   (состояния 3 и 283) в тот же тик, что 5257, и моб молчит ровно 4.0 с — задержка была в порту: (а) до bowfx 26.10 клиент не
//   ставил состояние своего удара (исправлено, `asda.Skill.OwnHitStates`), (б) рывка не было — клип играл на месте, записи 5257
//   выпускались сразу. Тик эталона, у ЛЮБОГО персонажа в состоянии 24 (5257 приходит всему сектору):
//     · время = клип-мс (шаг dt·темп), длительность = COMBO0 (`pChar+368·idx+6355`, 700 мс);
//     · вход: ветка МОБ (`+208386 & 1`, цель renderId) / ИГРОК (`& 8`, userIdx); конец — позиция моба, а у моба с байтом
//       `+6979 == 3` и размерным классом ≥ 1 — позиция − курс_кастера × класс·100; у игрока — позиция − курс × 100; цели нет → фаза 3;
//     · фаза 0: точка = старт + (конец − старт)·t, t = время/COMBO0, только при 0<t<1 (`skillBuildBezierCurve` по двум точкам);
//       свой — каждый кадр сброс пути;
//     · фаза 1 (время > COMBO0) МОБ: при бите 0x02 записи — отброс цели на +100 по курсу кастера (если по отрезку есть земля;
//       `playerSetWorldPosition` отказывает при +6986/+6987), 8215, `Skill_ApplyEffectResult(…, 0x219)`; свой — 4008 вид 15 с ТЕКУЩЕЙ
//       точкой; в том же кадре фаза 2: при 0x02 — цель разворачивается к кастеру, иначе «IMMUNE» (0x20, нотис 1092) / «MISS»
//       (нотис 1159) стилем 5; свой — в конечную точку. ИГРОК: фаза 1 свой — в конечную точку + 4008 вид 15 с ней; фаза 2 — MISS/IMMUNE;
//     · фаза 3: клип обрывается (состояние −1) — у нас закрытие показа.
//   ★ Дизасм 0xB3B7F8 (`push 0x2017`): между проверкой бита 0x02 (0xB3B57E) и отправкой 8215 проверки «свой» (`+0x2E734`) НЕТ —
//   8215 и «MISS» делает и наблюдатель; сервер подменяет отправителя (`sub_428020`). Повторяем.
//   `Skill_ApplyEffectResult` = выпуск припаркованных записей 5257 в `TickActive` (состояния — только у своего кастера).
void UAsdaSkillFxSubsystem::BeginDash(FAsdaSkillFxActive& A, const AAsdaPlayerCharacter* Caster, bool bOurs)
{
	A.bDash = true;
	A.DashPhase = 0;
	A.DashMs = A.PhaseMs[0];
	if (!Caster) { A.DashPhase = 3; return; }
	A.DashStartG = Caster->GetGamePosXZ();
	A.DashEndG = A.DashStartG;
	AActor* T = A.Target.Get();
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!T)
	{
		A.DashPhase = 3;   // `pTargetEntity == nullptr` → фаза 3 (клип обрывается)
		ASDA_CTRACE(bOurs ? EAsdaCT::Swing : EAsdaCT::Peer, TEXT("РЫВОК навыка %d (кастер %d): цели в мире нет — фаза 3, рывка нет"), A.SkillId, A.CasterUserIdx);
		return;
	}
	A.bDashMob = A.bTargetIsMob;
	const FVector2D TG = AsdaCoord::UeToGame2D(T->GetActorLocation());
	// Курс кастера (`getVector3_98(pChar+2)`): к этому кадру разворот к цели навыка уже взведён (`OnSkillCastFaceTarget`, кадр 5257);
	//   логический курс ставится сразу. Нулевой — направление на цель.
	FVector2D Fwd = Caster->GetFacingDirGame();
	if (Fwd.IsNearlyZero()) { Fwd = (TG - A.DashStartG).GetSafeNormal(); }
	if (A.bDashMob)
	{
		const AAsdaMobActor* M = Cast<AAsdaMobActor>(T);
		A.DashTargetIdx = M ? M->NetRenderId : -1;
		const int32 SizeClass = GetTargetSizeClass(T);   // `target[1839]` = Mob list +0x90
		A.DashEndG = (M && M->MobAggression == 3 && SizeClass >= 1) ? (TG - Fwd * (float)SizeClass * 100.f) : TG;
	}
	else
	{
		A.DashTargetIdx = WS ? WS->FindUserIdxByActor(T) : -1;
		A.DashEndG = TG - Fwd * 100.f;
	}
	// Байт [7] записи этой цели (`charSplitSkillStatusByFlag` → +208388/+208428) — флаги исхода. Записи паркует в показ
	//   `FlushPendingCastHits` в конце `PlaySkillVisual`; флаги берём при прибытии (`TickDash`).
	ASDA_CTRACE(bOurs ? EAsdaCT::Swing : EAsdaCT::Peer, TEXT("РЫВОК навыка %d (кастер %d%s) СТАРТ: цель %s idx=%d, (%.0f,%.0f) → (%.0f,%.0f), длина %.0f, за %.0f клип-мс"),
		A.SkillId, A.CasterUserIdx, bOurs ? TEXT(", я") : TEXT(", сосед"), A.bDashMob ? TEXT("моб") : TEXT("игрок"), A.DashTargetIdx,
		A.DashStartG.X, A.DashStartG.Y, A.DashEndG.X, A.DashEndG.Y, FVector2D::Distance(A.DashStartG, A.DashEndG), A.DashMs);
	UE_LOG(LogAsdaSkillFx, Log, TEXT("[навыки-рывок] навык %d кастер uidx=%d%s: цель %s idx=%d, (%.0f,%.0f) → (%.0f,%.0f) за %.0f клип-мс"),
		A.SkillId, A.CasterUserIdx, bOurs ? TEXT(" (мой)") : TEXT(" (СОСЕД)"), A.bDashMob ? TEXT("моб") : TEXT("игрок"), A.DashTargetIdx,
		A.DashStartG.X, A.DashStartG.Y, A.DashEndG.X, A.DashEndG.Y, A.DashMs);
}

bool UAsdaSkillFxSubsystem::TickDash(FAsdaSkillFxActive& A, float ProgressMs)
{
	if (A.DashPhase >= 3) { return true; }
	AAsdaPlayerCharacter* C = A.Caster.Get();
	if (!C) { A.DashPhase = 3; return true; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bOurs = AsdaSelf::IsSelfUserIdx(Net, A.CasterUserIdx);
	// Свой — каждый кадр рывка сброс пути (`listClearNodes(dword_351F9AC)`): клик по земле во время рывка не уводит персонажа.
	if (bOurs)
	{
		if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(C->GetController())) { PC->CancelRoute(); }
	}
	// Шапка тика: `if (FrameTimeB[фаза] < elapsed) ++фаза` — порог фазы 0 = COMBO0.
	if (A.DashPhase == 0 && ProgressMs > A.DashMs) { A.DashPhase = 1; }
	if (A.DashPhase == 0)
	{
		const float T = (A.DashMs > 0.f) ? (ProgressMs / A.DashMs) : 1.f;
		if (T > 0.f && T < 1.f)
		{
			const FVector2D P = A.DashStartG + (A.DashEndG - A.DashStartG) * T;
			if (P.X > 100.f && P.Y > 100.f) { C->SetDashPosGame(P); }   // `x > 100 && z > 100` — как у эталона
		}
		return false;
	}
	// Записи этой цели: байт [7] — бит 0x02 «состояние выпало», 0x20 IMMUNE. Записи припаркованы в показ (`PendingHits`).
	if (!A.bDashFlagsKnown)
	{
		for (const UAsdaNetSubsystem::FAsdaSkillHitRec& R : A.PendingHits)
		{
			const bool bRecMob = (AsdaTargetKindFromHitRec(R.TargetKind) == EAsdaTargetKind::Entity);
			if (bRecMob == A.bDashMob && R.TargetIdx == A.DashTargetIdx) { A.DashFlags = (uint8)R.HitFlags; A.bDashFlagsKnown = true; break; }
		}
	}
	AActor* Tgt = (WS && A.DashTargetIdx >= 0)
		? WS->ActorByTarget(A.bDashMob ? EAsdaTargetKind::Entity : EAsdaTargetKind::Character, A.DashTargetIdx) : nullptr;
	auto MissOrImmune = [GI, &A, Tgt]()
	{
		if (!Tgt) { return; }
		const bool bImm = (A.DashFlags & 0x20) != 0;
		if (UAsdaFloatingTextSubsystem* FT = GI->GetSubsystem<UAsdaFloatingTextSubsystem>()) { FT->SpawnOverActor(5, bImm ? TEXT("IMMUNE") : TEXT("MISS"), Tgt); }
		// ★ R5 (12.11.2026): общая строка боя; `CChat_AddTokenizedLines(…, −1, 10000, 0)`
		AsdaCombatChat::Line(GI, bImm ? 1092 : 1159, TArray<FString>(), TArray<int32>(), UAsdaChatSubsystem::ColWhite());
		ASDA_CTRACE(EAsdaCT::DmgText, TEXT("РЫВОК навыка %d: исход «%s» над целью idx=%d (флаги 0x%02X), нотис %d"), A.SkillId, bImm ? TEXT("IMMUNE") : TEXT("MISS"),
			A.DashTargetIdx, (int32)A.DashFlags, bImm ? 1092 : 1159);
	};
	if (A.bDashMob)
	{
		AAsdaMobActor* M = Cast<AAsdaMobActor>(Tgt);
		if (A.DashPhase == 1)
		{
			if (M)
			{
				if (A.DashFlags & 0x02)
				{
					const FVector2D TG = AsdaCoord::UeToGame2D(M->GetActorLocation());
					FVector2D Fwd = C->GetFacingDirGame();
					if (Fwd.IsNearlyZero()) { Fwd = (TG - C->GetGamePosXZ()).GetSafeNormal(); }
					const FVector2D NewG = TG + Fwd * 100.f;
					// `targetCheckWalkLineToPoint@0xA88A40` — 101 проба «есть земля» по отрезку. [ПОРТ] у нас — проходимость сетки карты
					//   (`AsdaPath::IsLineWalkable`, порт `mapTraceWalkLineBlocked`): моб не уезжает в стену; нет сетки — «земля есть».
					const bool bWalk = !C->WalkGrid || AsdaPath::IsLineWalkable(C->WalkGrid, TG / 100.f, NewG / 100.f);
					if (bWalk)
					{
						const UAsdaEffectsSubsystem* Fx = GI->GetSubsystem<UAsdaEffectsSubsystem>();
						const bool bLocked = Fx && (Fx->HasMobMoveLock(M->NetRenderId) || Fx->HasMobAnimFreeze(M->NetRenderId));
						if (!bLocked) { M->NetSnapPos(NewG); }   // `playerSetWorldPosition`: отказ при +6986/+6987
						const FIntPoint Tile = AsdaCoord::GameToTile(NewG);
						if (Net) { Net->SendDash8215(A.CasterUserIdx, M->NetRenderId, 537, Tile.X, Tile.Y); }   // 537 зашит и для 597 [ОРИГИНАЛ]
						ASDA_CTRACE(bOurs ? EAsdaCT::Skill : EAsdaCT::Peer, TEXT("РЫВОК навыка %d ПРИБЫЛ: моб rid=%d отброшен %s на (%.0f,%.0f), 8215 тайл (%d,%d)"),
							A.SkillId, M->NetRenderId, bLocked ? TEXT("НЕТ (лёд/камень/стан — позиция стоит)") : TEXT("да"), NewG.X, NewG.Y, Tile.X, Tile.Y);
					}
					else
					{
						ASDA_CTRACE(bOurs ? EAsdaCT::Skill : EAsdaCT::Peer, TEXT("РЫВОК навыка %d ПРИБЫЛ: точка отброса моба rid=%d непроходима — без отброса и 8215"),
							A.SkillId, M->NetRenderId);
					}
				}
				A.DashPhase = 2;
			}
			else
			{
				A.DashPhase = 3;
			}
			if (bOurs && Net)
			{
				const FVector2D Cur = C->GetGamePosXZ();
				C->SetDashPosGame(Cur);
				Net->SendSkillActionStop4008K15((float)Cur.X, (float)Cur.Y);
			}
		}
		if (A.DashPhase == 2)
		{
			if (M)
			{
				if (A.DashFlags & 0x02)
				{
					M->SnapFaceGameDir(C->GetGamePosXZ() - AsdaCoord::UeToGame2D(M->GetActorLocation()));   // цель — к кастеру
				}
				else
				{
					MissOrImmune();
				}
			}
			if (bOurs) { C->SetDashPosGame(A.DashEndG); }
			A.DashPhase = 3;
		}
	}
	else
	{
		if (A.DashPhase == 1)
		{
			if (bOurs && Net)
			{
				C->SetDashPosGame(A.DashEndG);
				Net->SendSkillActionStop4008K15((float)A.DashEndG.X, (float)A.DashEndG.Y);
			}
			// Фаза 1 → 2 у ветки игрока идёт шапкой СЛЕДУЮЩЕГО кадра (`FrameTimeB[1] = 0 < elapsed`).
			A.DashPhase = 2;
			return false;
		}
		if (A.DashPhase == 2)
		{
			// ★ [ИСПРАВЛЕНИЕ] эталон в ветке игрока читает бит 0x02 из массива флагов МОБОВ (`+208388`), а 0x20 — из массива игроков
			//   (`+208428`): у PvP-рывка исход шёл по чужим/пустым флагам. Берём флаги записи самой цели-игрока.
			if (Tgt && !(A.DashFlags & 0x02)) { MissOrImmune(); }
			A.DashPhase = 3;
		}
	}
	if (A.DashPhase >= 3)
	{
		ASDA_CTRACE(bOurs ? EAsdaCT::Swing : EAsdaCT::Peer, TEXT("РЫВОК навыка %d (кастер %d) КОНЕЦ на %.0f клип-мс: флаги цели 0x%02X%s — клип обрывается"),
			A.SkillId, A.CasterUserIdx, ProgressMs, (int32)A.DashFlags, A.bDashFlagsKnown ? TEXT("") : TEXT(" (записи цели нет)"));
		return true;
	}
	return false;
}

bool UAsdaSkillFxSubsystem::GetDashState(const AAsdaPlayerCharacter* Caster, int32& OutPhase, FVector2D& OutStartG, FVector2D& OutEndG) const
{
	for (const FAsdaSkillFxActive& A : Active)
	{
		if (A.Caster.Get() != Caster || !A.bDash) { continue; }
		OutPhase = A.DashPhase; OutStartG = A.DashStartG; OutEndG = A.DashEndG;
		return true;
	}
	return false;
}
