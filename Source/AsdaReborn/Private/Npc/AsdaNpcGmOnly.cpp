// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ 09.11.2026 NPC «только для GM_» — клиентский фильтр видимости (разбор и правило — в Public/Npc/AsdaNpcGmOnly.h).
#include "Npc/AsdaNpcGmOnly.h"
#include "AsdaNetSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "Engine/GameInstance.h"
#include "AsdaNpcLists.inl"   // ★ встроенные списки (не ini — владелец 09.11)

namespace AsdaNpcGmOnlyLocal
{
	static const TCHAR* const kPrefix = TEXT("GM_");   // префикс имени персонажа (решение владельца)
}

bool AsdaNpcGmOnly::IsGmOnly(int32 NpcId, int32 MapId)
{
	return AsdaNpcLists::Has(AsdaNpcLists::kGmOnly, NpcId, MapId);
}

int32 AsdaNpcGmOnly::CurrentMap(const UGameInstance* GI)
{
	const UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	return LM ? LM->CurrentMapId : -1;
}

bool AsdaNpcGmOnly::SelfIsGm(const UGameInstance* GI)
{
	// Имя своего персонажа — общая точка «своё имя» (мир / запись char-list, обе приходят от сервера при входе).
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	return Net && Net->GetSelfCharNameForOperatorCheck().StartsWith(AsdaNpcGmOnlyLocal::kPrefix, ESearchCase::IgnoreCase);
}

bool AsdaNpcGmOnly::HiddenForSelfOnMap(const UGameInstance* GI, int32 NpcId, int32 MapId)
{
	return IsGmOnly(NpcId, MapId) && !SelfIsGm(GI);
}

bool AsdaNpcGmOnly::HiddenForSelf(const UGameInstance* GI, int32 NpcId)
{
	return HiddenForSelfOnMap(GI, NpcId, CurrentMap(GI));
}
