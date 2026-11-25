// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPvpRelation — ОТНОШЕНИЕ «Я ↔ ДРУГОЙ ПЕРСОНАЖ» для целей навыков и площадного сбора (★ 26.10.2026, bowfx: баг владельца
//   «вражеские навыки ложатся на мирных»). Одна точка вместо копий в Tab-цели, площадном сборе и гейте заявки навыка.
//
// ЭТАЛОН (сборка IDB; разбор и таблица — `Reports/skill_targets/RULES.md`):
//   · свой флаг PvP `[160]`, режим `[92]` (0 нет / 1 дуэль / 2 свободный / 4 командный), соперник `[80]`, команда `[116]` — опкод 4206
//     (`network.c:44129`); на картах осады 19/23 эталон сам ставит `[92] = 4` и `[116]` = свой лагерь (`game_main.c:3910-3916`,
//     `get_o5BE(g_pCampWarClan)` = `+0x0B7C` — у порта `UAsdaWorldSubsystem::GetSelfCampId`);
//   · площадной сбор персонажей `charManager_collectTargetsInRect@0x9DB560` (entity_char.c:9657-9690): себя, основную цель, мёртвых
//     (6/29) и GM-скрытых не берёт; режим 0 — никого; 1 — только соперник (и сбор на нём кончается); 4 — без своей команды;
//     2 — без своего клана (`+0x968`, > 0), своего Друга (`+0x8A7`) и согруппников (`Party_FindMemberByUserIdx`);
//   · сервер `sub_556DB0@0x556DB0` (GameServer.c:131710) решает так же по режиму кастера (враждебный навык = `SkillInfo+52 == 6`).
#pragma once

#include "CoreMinimal.h"

class UGameInstance;

namespace AsdaPvpRel
{
	// Свой PvP-срез так, как его видит эталон в момент решения.
	struct FSelf
	{
		bool  bOn = false;          // `[160]` — флаг PvP
		int32 Mode = 0;             // `[92]`  — 0 / 1 дуэль / 2 свободный / 4 командный
		int32 Rival = -1;           // `[80]`  — соперник дуэли (userIdx)
		int32 Team = -1;            // `[116]` — своя команда
		bool  bCampWarMap = false;  // карта осады 19/23 (режим 4 и команда = лагерь ставятся локально)
		int32 SelfIdx = -1;         // свой userIdx (−1 — ещё не известен)
	};

	// Свой срез. Ключ `asda.Skill.TargetRelation 0` — прежний порт (флаг PvP — `UAsdaSkillUseSubsystem::bSelfPvpMode`, без карт осады).
	ASDAREBORN_API FSelf GetSelf(const UGameInstance* GI);
	// Включены ли эталонные гейты отношения (ключ `asda.Skill.TargetRelation`, по умолчанию 1).
	ASDAREBORN_API bool IsOn();
	// Команда `[116]` ДРУГОГО персонажа: на картах осады — его лагерь (6722), иначе команда из 4206; −1 — нет.
	ASDAREBORN_API int32 PeerTeam(const UGameInstance* GI, int32 UserIdx, const FSelf& Me);

	// Результат проверки кандидата площадного сбора.
	enum class EArea : uint8 { Skip, Add, AddAndStop };
	// Фильтр `charManager_collectTargetsInRect` для персонажа UserIdx (основная цель — PrimaryUserIdx, её эталон пропускает).
	//   OutWhy — причина пропуска для журнала (может быть nullptr).
	ASDAREBORN_API EArea AreaCandidate(const UGameInstance* GI, const FSelf& Me, int32 UserIdx, int32 PrimaryUserIdx, FString* OutWhy = nullptr);
	// «Свой» в свободном PvP (режим 2): тот же клан (> 0), Друг или согруппник.
	ASDAREBORN_API bool IsFriendlyInFreePvp(const UGameInstance* GI, const FSelf& Me, int32 UserIdx);
}
