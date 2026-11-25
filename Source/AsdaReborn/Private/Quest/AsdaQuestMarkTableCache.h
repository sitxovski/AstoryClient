// Copyright (c) 2026 @sitxovski (Discord)
// ★ R4-c (15.11.2026) [ПОРТ]: ЛОКАЛЬНЫЙ КЭШ СТРОК ТАБЛИЦ ДЛЯ ПЕРЕСЧЁТА МЕТОК ЗАДАНИЙ.
//
// ЧТО БЫЛО. Пересчёт меток (`UAsdaQuestMarkerSubsystem::RebuildMarks`, раз в секунду) берёт строки таблиц через общую точку
//   `AsdaDt::Row` / `AsdaDt::RowByInt` (`AsdaDataTableCache.h`). Каждый такой вызов строит `FName` из длинного пути таблицы и
//   ищет таблицу в общем кэше; `RowByInt` ещё строит строку из числа и `FName` из неё; проверка «есть ли страница сценария»
//   строит строку «<сценарий>_<номер>» и `FName` из неё. В счёте строк облачка заданий это десятки вызовов на каждого NPC
//   текущей и соседних карт.
// ЧТО СТАЛО. Здесь — кэш ТОЛЬКО для подсистемы меток (общая точка `AsdaDataTableCache` не тронута): таблица держится слабой
//   ссылкой в своей ячейке, имя строки по числу и по паре «сценарий, номер» запоминается. Сама строка каждый раз берётся из
//   таблицы тем же `FindRow` — указатель на строку не хранится, устареть нечему. Результат тот же: та же строка либо nullptr.
// ★ R4-c (15.11.2026, закрытие): ключ `asda.Perf.QuestTableCache`, прежние вызовы `AsdaDt` и самотест равенства убраны — остался кэш.
// Только игровой поток (как и прежние статические кэши меток).
#pragma once

#include "CoreMinimal.h"
#include "AsdaDataTableCache.h"
#include "AsdaPerf.h"
#include "Engine/DataTable.h"

namespace AsdaQuestMarkTableCache
{
	// Таблицы пересчёта меток (у каждой — своя ячейка кэша; путь у ячейки всегда один и тот же).
	enum class ETable : uint8 { NsrHeader, Snq, SnqIct, NsrPage, Episode, QuestComplete, Count };

	// Ячейка кэша одной таблицы.
	struct FSlot
	{
		const TCHAR* Path = nullptr;                      // путь, с которым ячейка заведена (другой путь — отказ от кэша)
		TWeakObjectPtr<UDataTable> Dt;                    // таблица (слабо: ушла — берётся заново из общей точки)
		TMap<int32, FName> IntNames;                      // число → имя строки «<число>»
		TMap<FString, TMap<int32, FName>> PairNames;      // (приставка, число) → имя строки «<приставка>_<число>»; ключ без учёта регистра, как FName
	};

	// Ячейка таблицы.
	FSlot& Slot(ETable T);

	// Таблица: из ячейки (слабая ссылка), иначе общая точка `AsdaDt::Cached`.
	inline UDataTable* Table(ETable T, const TCHAR* Path)
	{
		FSlot& S = Slot(T);
		// Путь ячейки: тот же указатель либо та же строка (литерал пути в разных единицах трансляции может лежать по разным адресам).
		if (S.Path != Path)
		{
			if (!S.Path) { S.Path = Path; }
			else if (!Path || FCString::Strcmp(S.Path, Path) != 0) { return AsdaDt::Cached(Path); }   // ★ R4-c (15.11.2026, ревизия): пустой путь — в общую точку
		}
		if (UDataTable* D = S.Dt.Get()) { return D; }
		UDataTable* D = AsdaDt::Cached(Path);
		S.Dt = D;
		return D;
	}

	// Строка по имени (то же, что `AsdaDt::Row<TRow>(Path, FName(*RowName), Ctx)`).
	template <typename TRow>
	const TRow* Row(ETable T, const TCHAR* Path, const FString& RowName, const TCHAR* Ctx)
	{
		ASDA_PERF_SCOPE("Quest.TableRow");
		const UDataTable* Dt = Table(T, Path);
		return Dt ? Dt->FindRow<TRow>(FName(*RowName), Ctx, /*bWarnIfRowMissing*/false) : nullptr;
	}

	// Строка по числу (то же, что `AsdaDt::RowByInt<TRow>(Path, Key, Ctx)`).
	template <typename TRow>
	const TRow* RowByInt(ETable T, const TCHAR* Path, int32 Key, const TCHAR* Ctx)
	{
		ASDA_PERF_SCOPE("Quest.TableRow");
		const UDataTable* Dt = Table(T, Path);
		if (!Dt) { return nullptr; }
		FSlot& S = Slot(T);
		const FName* Name = S.IntNames.Find(Key);
		if (!Name) { Name = &S.IntNames.Add(Key, FName(*FString::FromInt(Key))); }
		return Dt->FindRow<TRow>(*Name, Ctx, /*bWarnIfRowMissing*/false);
	}

	// Строка «<приставка>_<число>» (то же, что `AsdaDt::Row<TRow>(Path, FName(*FString::Printf(TEXT("%s_%d"), *Prefix, N)), Ctx)`).
	template <typename TRow>
	const TRow* RowByPair(ETable T, const TCHAR* Path, const FString& Prefix, int32 N, const TCHAR* Ctx)
	{
		ASDA_PERF_SCOPE("Quest.TableRow");
		const UDataTable* Dt = Table(T, Path);
		if (!Dt) { return nullptr; }
		FSlot& S = Slot(T);
		TMap<int32, FName>* ByN = S.PairNames.Find(Prefix);
		if (!ByN) { ByN = &S.PairNames.Add(Prefix); }
		const FName* Name = ByN->Find(N);
		if (!Name) { Name = &ByN->Add(N, FName(*FString::Printf(TEXT("%s_%d"), *Prefix, N))); }
		return Dt->FindRow<TRow>(*Name, Ctx, /*bWarnIfRowMissing*/false);
	}
}
