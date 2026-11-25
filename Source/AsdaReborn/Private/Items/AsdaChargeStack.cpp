// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ [ИСПРАВЛЕНИЕ] 01.11.2026 (клиент 5.1.4) — таблица ключей P100_ChargeStack и ключ поведения (шапка — AsdaChargeStack.h).
#include "Items/AsdaChargeStack.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "AsdaDevConsole.h"   // ASDA_DEV_CVAR — ключ вне Shipping (в Shipping — постоянное умолчание)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaChargeStack, Log, All);

// Ключ исправления: 1 — подбор ячеек коробки по правилу P100 (стопка того же ключа принимает заряды без новой ячейки);
//   0 — эталон `itemFindSowelSlots`: под каждый спецпредмет состава — свободная ячейка сумки.
static ASDA_DEV_CVAR(int32, CVarChargeStackReserve,
	TEXT("asda.Item.ChargeStackReserve"),
	1,
	TEXT("[ИСПРАВЛЕНИЕ] 01.11.2026 (P100_ChargeStack): 1 — коробка (6096) не требует свободную ячейку сумки под складываемый "
	     "спецпредмет, если в сумке есть ячейка того же ключа (группа + привязка) и заряды влезают в потолок 30000 — правило "
	     "сервера P100 один в один; 0 — как эталон, под каждый спецпредмет состава нужна свободная ячейка."),
	ECVF_Default);

namespace
{
	// Кэш «код → (ключ, заряды)»: строится один раз на загруженную таблицу (пересобирается, если DT сменилась).
	struct FChargeCache
	{
		TWeakObjectPtr<const UDataTable> Dt;   // таблица, по которой построен кэш
		TMap<int32, FIntPoint> ByCode;         // код → (X = ключ, Y = заряды)
	};

	// Единственный экземпляр кэша (игровой поток).
	FChargeCache& Cache()
	{
		static FChargeCache C;
		return C;
	}

	// Актуальный кэш для таблицы из реестра IT (nullptr — таблицы нет).
	const TMap<int32, FIntPoint>* Ensure(const UAsdaItemTableSubsystem* IT)
	{
		const UDataTable* Dt = IT ? IT->GetDataTable(AsdaChargeStack::TableName()) : nullptr;
		FChargeCache& C = Cache();
		if (!Dt)
		{
			C.Dt.Reset();
			C.ByCode.Reset();
			return nullptr;
		}
		if (C.Dt.Get() == Dt) { return &C.ByCode; }
		C.Dt = Dt;
		C.ByCode.Reset();
		int32 Bad = 0;   // строк с ключом/зарядами ≤ 0 или дублем кода
		for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
		{
			const FAsdaChargeGroupRow* R = reinterpret_cast<const FAsdaChargeGroupRow*>(It.Value);
			if (!R || R->Code <= 0 || R->GroupKey <= 0 || R->Charges <= 0 || C.ByCode.Contains(R->Code)) { ++Bad; continue; }
			C.ByCode.Add(R->Code, FIntPoint(R->GroupKey, R->Charges));
		}
		TSet<int32> Keys;   // различных ключей (сверка с генератором: 63)
		for (const TPair<int32, FIntPoint>& P : C.ByCode) { Keys.Add(P.Value.X); }
		UE_LOG(LogAsdaChargeStack, Log, TEXT("[заряды P100] таблица ключей: кодов %d, ключей %d, отброшено строк %d"),
			C.ByCode.Num(), Keys.Num(), Bad);
		return &C.ByCode;
	}
}

FName AsdaChargeStack::TableName()
{
	static const FName N(TEXT("ChargeGroup"));
	return N;
}

bool AsdaChargeStack::IsOn()
{
	return CVarChargeStackReserve.GetValueOnGameThread() != 0;
}

bool AsdaChargeStack::Lookup(const UAsdaItemTableSubsystem* IT, int32 Code, int32& OutKey, int32& OutCharges)
{
	OutKey = 0;
	OutCharges = 0;
	const TMap<int32, FIntPoint>* M = Ensure(IT);
	const FIntPoint* P = M ? M->Find(Code) : nullptr;
	if (!P) { return false; }
	OutKey = P->X;
	OutCharges = P->Y;
	return true;
}

int32 AsdaChargeStack::NumCodes(const UAsdaItemTableSubsystem* IT)
{
	const TMap<int32, FIntPoint>* M = Ensure(IT);
	return M ? M->Num() : 0;
}
