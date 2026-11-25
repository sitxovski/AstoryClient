// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ 09.11.2026 [ПОРТ, РЕШЕНИЕ ВЛАДЕЛЬЦА] NPC «ТОЛЬКО ДЛЯ GM_» — клиентский фильтр видимости (пачка 20, баг 7).
//   Владелец 09.11: «Agnes bring back… Other missing npc bring back but display only for "GM_" users (client filter)»; уточнения —
//   стоковые NPC видны всем («yes, but make sure you are not duplicating them»), стражи шахты — всем («okay, then make it visible»),
//   Arshes 37 — «should be in alpen map, but hidden for players».
//   Сервер спавнит этих NPC всем (P119); клиент показывает их (модель, имя, метки над головой и на карте, клик, разговор) только
//   персонажу, чьё имя начинается с «GM_»; остальным NPC не виден и не кликается.
//   СПИСОК — пары (NPC, карта), встроенные в бинарь: `AsdaNpcLists::kGmOnly` (Private/Npc/AsdaNpcLists.inl). Пары, а не id: один NPC
//   стоит на нескольких картах (Arshes: сток — Рейн, всем; Альпия — только GM_). НЕ в ini и без консольного ключа: владелец 09.11 —
//   «Internal mechanics needed». Имя своего персонажа — общая точка `GetSelfCharNameForOperatorCheck` (char-list от сервера).
//   Где действует: «удочерение» 4067 (`UAsdaWorldSubsystem::ProcessEnvBatches` — актор остаётся скрытым и без коллизии),
//   дальность обзора (`UAsdaViewRangeSubsystem::StepNpcs` — не возвращает видимость), метки квестов на карте и над головой
//   (`UAsdaQuestMarkerSubsystem` — записи таких NPC пропускаются по карте записи).
#pragma once

#include "CoreMinimal.h"

class UGameInstance;

namespace AsdaNpcGmOnly
{
	// NPC на этой карте — «только для GM_» (`AsdaNpcLists::kGmOnly`).
	ASDAREBORN_API bool IsGmOnly(int32 NpcId, int32 MapId);
	// Текущая карта своего персонажа (−1 — неизвестна).
	ASDAREBORN_API int32 CurrentMap(const UGameInstance* GI);
	// Имя своего персонажа начинается с «GM_» (без учёта регистра).
	ASDAREBORN_API bool SelfIsGm(const UGameInstance* GI);
	// Скрыть этого NPC карты MapId от своего персонажа: пара в списке и свой персонаж не GM_.
	ASDAREBORN_API bool HiddenForSelfOnMap(const UGameInstance* GI, int32 NpcId, int32 MapId);
	// То же для текущей карты своего персонажа.
	ASDAREBORN_API bool HiddenForSelf(const UGameInstance* GI, int32 NpcId);
}
