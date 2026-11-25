// Copyright (c) 2026 @sitxovski (Discord)
// Реализация удержания таблиц — обоснование и семантика в AsdaHeldTables.h.
#include "AsdaHeldTables.h"
#include "AsdaDataTableCache.h"   // AsdaDt::Cached — та же точка загрузки, что у всех потребителей

int32 FAsdaHeldTables::Hold(FName Group, TConstArrayView<const TCHAR*> Paths)
{
	if (const int32* Done = GroupCounts.Find(Group)) { return *Done; }   // повтор: без загрузки
	int32 N = 0;
	for (const TCHAR* Path : Paths)
	{
		if (UDataTable* T = AsdaDt::Cached(Path))
		{
			Tables.AddUnique(T);
			++N;
		}
	}
	GroupCounts.Add(Group, N);
	return N;
}

int32 FAsdaHeldTables::HoldTable(FName Group, UDataTable* Table)
{
	if (const int32* Done = GroupCounts.Find(Group)) { return *Done; }
	const int32 N = Table ? 1 : 0;
	if (Table) { Tables.AddUnique(Table); }
	GroupCounts.Add(Group, N);
	return N;
}
