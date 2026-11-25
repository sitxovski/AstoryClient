// Copyright (c) 2026 @sitxovski (Discord)
// AsdaAttackExit — ВЫХОД СВОЕГО ПЕРСОНАЖА ИЗ АТАКИ В МИРНЫЙ ПОКОЙ (общая точка, 01.11.2026, ревизия бага 10). [ОРИГИНАЛ]
//
// ЭТАЛОН. `CCharacter_SetAction@0x9EE720` case 0 (entity_char.c:16510-16600) при смене действия на 0:
//   • прошлое действие было 5 (атака) ⇒ `Send_CharStatePos(свой userIdx, курс, 400)` (entity_char.c:16545-16556) — 4024 с
//     actionType 400 «вышел из атаки». Это ЕДИНСТВЕННОЕ место эталона, где шлётся 400 (сверено по всем вызовам
//     `Send_CharStatePos`: остальные — 0, 100…117 эмоции/сидение/рыбалка/копание, `CGameUI_UpdateFadeAnim` и
//     `charEmoteSabzilAction` передают коды из таблиц эмоций). Значит 400 уходит при ЛЮБОМ переходе 5 → 0 своего персонажа.
//   • `netWord54 = −1` («кто ударил своего») — в порту его ведёт цепочка ESC (`AsdaEscChain::TickSelfState`) по переходу
//     состояния в 0; отдельного действия не нужно.
//   • субрежим ≠ 20 ⇒ `effectSlots[100].field_4 = −1` — слот показа навыка; аналог порта — `UAsdaSkillFxSubsystem::StopSkillVisual`.
//   • очередь замаха (ожидание ответа, отложенная отправка), цикл клипа атаки и стойка — флаги контроллера и персонажа.
//
// ПОРТ. Порт переходов 5 → 0 «одной функцией» не имеет: клик по земле, клавиатура, ремесло, гача делают свои куски.
//   Эта точка собирает полный эталонный набор и может быть позвана любым из них. Сейчас её зовёт приём 5030 код 2
//   (`UAsdaArrowAmmoSubsystem`, `CharFx_AutoSabzil_State(…, 0, 0, 0)` — «стрел больше нет»).
//   Ключ `asda.Combat.AttackExit400` (1 — слать 4024/400 при выходе из атаки, 0 — не слать, как порт до правки).
#pragma once

#include "CoreMinimal.h"

class UGameInstance;

namespace AsdaAttackExit
{
	// 4024 actionType 400 со своим курсом (`Send_CharStatePos(…, 400)`). Без проверки «было ли действие 5» — это забота
	//   вызывающего. false — ключ выключен, нет сети/персонажа или отправка не прошла.
	ASDAREBORN_API bool SendAttackExit400(UGameInstance* GI, const TCHAR* Why);

	// Свой персонаж — в действие 0, субрежим 0 (мирный покой): PvP-атака — `StopPvpAttack`, обычная — как клик по земле
	//   (флаги боя, таймер отложенного замаха, цикл клипа), затем стойка снята, слот показа навыка погашен, поза покоя.
	//   Было действие 5 (атака, правило `AsdaSelfInAttackAction`) ⇒ 4024/400 ОДИН раз до снятия флагов (`AsdaLeaveAttackAction`;
	//   на PvP-ветке её зовёт сам `StopPvpAttack`). Возврат — было ли действие 5.
	ASDAREBORN_API bool StopSelfActionToIdle(UGameInstance* GI, const TCHAR* Why);
}

class AAsdaPlayerController;

// ═══ ★ 01.11.2026 (bug-duel-ranged): ЕДИНОЕ правило «своё действие 5» и момент 4024/400 на весь порт (тела —
//   `AsdaPlayerController_Move.cpp`, разбор там же). 400 уходит ДО снятия флагов боя и ДО 4006 хода: сервер 4024 «в движении» не
//   принимает (`sub_523ED0`, GameServer.c:124817).
// Идёт ли своё действие 5 (замах атаки): не ожидание ответа на 4201 (22), не подход (15), не навык (24), не смерть (6).
bool AsdaSelfInAttackAction(const AAsdaPlayerController* PC);
// Выход из действия 5 в покой: было действие 5 ⇒ `AsdaAttackExit::SendAttackExit400`. Возврат — ушёл ли 400.
bool AsdaLeaveAttackAction(AAsdaPlayerController* PC, const TCHAR* Why);
// Конец PvP-атаки ради НОВОЙ атаки (эталон: прямая запись 5, без прохода через 0) — `StopPvpAttack` без 400.
void AsdaStopPvpAttackForNewAttack(AAsdaPlayerController* PC, const TCHAR* Why);
