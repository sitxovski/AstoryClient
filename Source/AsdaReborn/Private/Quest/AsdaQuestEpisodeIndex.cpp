// Copyright (c) 2026 @sitxovski (Discord)
// ★ R4-c (14.11.2026) [ПОРТ]: строка эпизода по номеру задания — разбор в `AsdaQuestEpisodeIndex.h`.
#include "AsdaQuestEpisodeIndex.h"

#include "AsdaPerf.h"
#include "Engine/DataTable.h"
#include "Npc/AsdaNpcDialogTypes.h"   // FAsdaEpisodeRow

const FAsdaEpisodeRow* AsdaQuestEpisodeIndex::LastByQuestKey(const UDataTable* Dt, int32 QuestKey)
{
	ASDA_PERF_SCOPE("Quest.EpisodeByQuestKey");
	if (!Dt) { return nullptr; }
	static TWeakObjectPtr<const UDataTable> IndexedFor;   // для какой таблицы построен индекс
	static int32 IndexedRows = -1;                        // сколько в ней было строк
	static TMap<int32, FName> RowByKey;                   // номер задания → имя ПОСЛЕДНЕЙ строки с ним (в порядке обхода таблицы)
	const TMap<FName, uint8*>& Rows = Dt->GetRowMap();
	if (IndexedFor.Get() != Dt || IndexedRows != Rows.Num())
	{
		RowByKey.Reset();
		for (const TPair<FName, uint8*>& It : Rows)
		{
			const FAsdaEpisodeRow* R = reinterpret_cast<const FAsdaEpisodeRow*>(It.Value);
			if (R) { RowByKey.Add(R->QuestKey, It.Key); }   // `Add` заменяет прежнюю запись — остаётся последняя
		}
		IndexedFor = Dt;
		IndexedRows = Rows.Num();
	}
	const FName* Name = RowByKey.Find(QuestKey);
	if (!Name) { return nullptr; }
	uint8* const* Row = Rows.Find(*Name);
	return Row ? reinterpret_cast<const FAsdaEpisodeRow*>(*Row) : nullptr;
}
