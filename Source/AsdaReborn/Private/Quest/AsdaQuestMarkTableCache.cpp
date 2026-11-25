// Copyright (c) 2026 @sitxovski (Discord)
// ★ R4-c (15.11.2026) [ПОРТ]: локальный кэш строк таблиц пересчёта меток — разбор в `AsdaQuestMarkTableCache.h`.
#include "AsdaQuestMarkTableCache.h"

AsdaQuestMarkTableCache::FSlot& AsdaQuestMarkTableCache::Slot(ETable T)
{
	static FSlot Slots[(int32)ETable::Count];
	return Slots[(int32)T];
}
