// Copyright (c) 2026 @sitxovski (Discord)
// AsdaAttackExit — выход своего персонажа из атаки в мирный покой. Эталон и состав — в шапке заголовка.
#include "World/AsdaAttackExit.h"
#include "AsdaDevConsole.h"                   // ASDA_DEV_CVAR — ключ отката 4024/400
#include "AsdaNetSubsystem.h"                 // SendCharStatePos4024
#include "Skill/AsdaSkillFxSubsystem.h"       // StopSkillVisual — слот показа навыка (`effectSlots[100].field_4 = −1`)
#include "World/AsdaPlayerController.h"       // флаги боя, StopPvpAttack, ExitCombatStance
#include "World/AsdaPlayerCharacter.h"        // цикл клипа атаки, поза покоя, курс
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaAttackExit, Log, All);

// `AsdaSelfInAttackAction` / `AsdaLeaveAttackAction` — объявлены в заголовке (тела — `AsdaPlayerController_Move.cpp`).

// ★ [ОРИГИНАЛ] 01.11.2026: 4024/400 при выходе из атаки (`CCharacter_SetAction` case 0, entity_char.c:16545-16556).
static ASDA_DEV_CVAR(int32, CVarAttackExit400,
	TEXT("asda.Combat.AttackExit400"),
	1,
	TEXT("Выход своего персонажа из атаки в покой: 1 = как эталон — 4024 с actionType 400 и своим курсом; 0 = не слать (порт до 01.11)."),
	ECVF_Default);

bool AsdaAttackExit::SendAttackExit400(UGameInstance* GI, const TCHAR* Why)
{
	if (CVarAttackExit400.GetValueOnGameThread() == 0) { return false; }
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	const APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	const AAsdaPlayerCharacter* Ch = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
	if (!Net || !Ch) { return false; }
	// Курс — `effectSlots[112].gap_298+12`, то же поле, что у всех отправителей 4024 (`GetFacingDirGame`, шапка SendCharStatePos4024).
	const FVector2D Face = Ch->GetFacingDirGame();
	const bool bOk = Net->SendCharStatePos4024(400, (float)Face.X, (float)Face.Y);
	UE_LOG(LogAsdaAttackExit, Log, TEXT("[выход из атаки] 4024/400 (%s) -> %s"), Why ? Why : TEXT("?"), bOk ? TEXT("ушёл") : TEXT("не ушёл"));
	return bOk;
}

bool AsdaAttackExit::StopSelfActionToIdle(UGameInstance* GI, const TCHAR* Why)
{
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	if (!PC) { return false; }
	AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(PC->GetPawn());
	// Правило «действие 5» — общее (`AsdaSelfInAttackAction`); 400 шлётся ОДИН раз и до снятия флагов.
	const bool bWasAttack = AsdaSelfInAttackAction(PC);
	if (PC->bPvpAttack)
	{
		// Флаги PvP, таймер отложенного замаха, цикл клипа, очередь серии (`ResetPvpSeries`); 4024/400 шлёт сам `StopPvpAttack`
		//   (`AsdaLeaveAttackAction` первой строкой) — здесь второй раз не зовём.
		PC->StopPvpAttack(Why);
	}
	else
	{
		AsdaLeaveAttackAction(PC, Why);   // было действие 5 ⇒ 4024/400, пока флаги ещё стоят
		// Как клик по земле (`AAsdaPlayerController::OnLeftClick`, ветка земли): флаги боя, ожидание ответа, цикл клипа.
		PC->bAutoAttack = false; PC->bCombatStarted = false; PC->bInCombatStance = false; PC->bWaitingCombatAck = false;
		PC->GetWorldTimerManager().ClearTimer(PC->DeferredAttackSendTimer);
		if (Ch) { Ch->StopAttackAnim(); }
	}
	// Субрежим 0 — мирный покой (не боевая стойка 20).
	PC->ExitCombatStance();
	if (Ch)
	{
		if (UAsdaSkillFxSubsystem* Fx = GI->GetSubsystem<UAsdaSkillFxSubsystem>()) { Fx->StopSkillVisual(Ch); }
		if (!Ch->IsIncapacitated())
		{
			// Как вход в ремесло/гачу (`CharFx_AutoSabzil_State(…, 0)`): сидение снято сразу, поза покоя.
			Ch->DropSitForEtalonState(/*bIdleClip*/ true, Why);
			Ch->SetAnimState(EAsdaPlayerState::Idle);
		}
	}
	UE_LOG(LogAsdaAttackExit, Log, TEXT("[выход из атаки] %s: действие → 0 (было 5: %s)"), Why ? Why : TEXT("?"), bWasAttack ? TEXT("да") : TEXT("нет"));
	return bWasAttack;
}
