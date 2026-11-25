// Copyright (c) 2026 @sitxovski (Discord)
// AsdaFxDeadHost.h — ★ R5-fix5 (13.11.2026) [ОРИГИНАЛ]: следящий эффект гаснет, когда его носитель-моб умер.
//   Эталон: обработчики привязки эффекта к сущности (режимы 3/9/12/13 — `effectSyncPosToRenderEntity@0xF583E0`,
//   `effectSyncPosToOwnerEntity@0xF58E40`, `effectUpdateEntityAttachPos@0xF59410`, `effectPlaceAboveEntity@0xF595B0`) при
//   состоянии носителя `[120] == 10` (умирает) ставят узлу эффекта байт `+1 = 1`, и `objectMgr_TickEffects@0xF57990` удаляет
//   узел следующим проходом. Порт гасил такой эффект только при удалении актора-носителя — эффект висел над трупом.
//   ОБЛАСТЬ: только эффекты, рождённые НЕ веткой удара — их помечает вызывающий (`Mark`):
//     (а) эффект состояния игрока на его цели-мобе (состояние 296 «Похищение крови», `CharFx_SpawnBlossomEffect@0x9B4ED0`);
//     (б) записи фазы каста с привязкой к цели-мобу (навыки 756 и 908: `CharFx_UpdateTrapSkillEffect@0xB3F250`,
//         `CharFx_PlaySkillDakhlEffect@0xA84130`).
//   Эффекты ветки удара НЕ помечаются: смерть моба в порту придержана до кадра удара (лента попаданий), эффект удара рождается
//   тем же вызовом — общее правило гасило бы эффект добивающего удара в первом же кадре (разбор — Reports\refactor_2026-11\r5\fix\a\REPORT4.md).
//   Ключ `asda.Fx.DieWithDeadMob`: 1 = как эталон, 0 = как было (эффект живёт до своего срока или до удаления трупа).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

namespace AsdaFxDeadHost
{
	// Метка эффекта «гаснуть вместе с умершим носителем-мобом» (тег актора эффекта).
	inline const FName& Tag()
	{
		static const FName T(TEXT("AsdaDieWithDeadMobHost"));
		return T;
	}

	// Пометить эффект: его рождает не ветка удара, и он следит за мобом. Метка ставится при любом значении ключа (ключ читает
	//   только `ShouldDie`), поэтому включение ключа на живой сцене гасит и уже висящие над трупом эффекты.
	inline void Mark(AActor* Fx)
	{
		if (Fx) { Fx->Tags.AddUnique(Tag()); }
	}

	// Ключ `asda.Fx.DieWithDeadMob` включён (в Shipping — константа 1). Определение — AsdaEffectActor_Simulate.cpp.
	bool IsOn();

	// Помеченный эффект пора гасить: ключ включён, носитель — моб, и он мёртв. Определение — AsdaEffectActor_Simulate.cpp.
	bool ShouldDie(const AActor* Fx, const AActor* Host);
}
