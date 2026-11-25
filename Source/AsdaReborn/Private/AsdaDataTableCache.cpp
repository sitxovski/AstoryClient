// Copyright (c) 2026 @sitxovski (Discord)
// Реализация ленивого кэша `UDataTable` по пути — обоснование и семантика в AsdaDataTableCache.h.
#include "AsdaDataTableCache.h"
#include "UObject/UObjectGlobals.h"

namespace AsdaDt
{
	UDataTable* Cached(const TCHAR* ObjectPath)
	{
		if (!ObjectPath || !*ObjectPath) { return nullptr; }
		// Ключ — сам путь ассета. `FName` вместо `FString`: сравнение по индексу, а путей десятки.
		const FName Key(ObjectPath);
		// ★ СЛАБЫЕ указатели: выгруженный/пересобранный в редакторе ассет подхватится следующим
		//   вызовом. Это ровно то, что делали девять прежних копий `static TWeakObjectPtr`.
		static TMap<FName, TWeakObjectPtr<UDataTable>> GCache;
		if (TWeakObjectPtr<UDataTable>* Found = GCache.Find(Key))
		{
			if (Found->IsValid()) { return Found->Get(); }
		}
		// `LOAD_NoWarn | LOAD_Quiet` — как во всех прежних телах: отсутствие опциональной таблицы
		//   не должно сыпать в лог движка, о нём сообщает сам вызывающий своим сообщением.
		UDataTable* Dt = LoadObject<UDataTable>(nullptr, ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		GCache.Add(Key, Dt);
		return Dt;
	}
}
