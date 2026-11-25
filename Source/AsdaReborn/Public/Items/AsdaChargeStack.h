// Copyright (c) 2026 @sitxovski (Discord)
#pragma once
// ★★★ [ИСПРАВЛЕНИЕ] 01.11.2026 (клиент 5.1.4, решение владельца) — P100_ChargeStack на стороне клиента.
//   Сервер (патч AsdaPatch P100_ChargeStack, проект `Reports/item_dedup/charges/P100_PROJECT.md`) при выдаче
//   складываемого спецпредмета (коды 1..637 из групп v2 «очевидных дублей», режим расхода 2, бессрочные) ПРИБАВЛЯЕТ
//   заряды к уже лежащей в сумке (контейнер 1) ячейке того же КЛЮЧА (ключ = (группа + 1)·10 + привязка FT ток.24),
//   новую ячейку не занимает; потолок ячейки — 30 000 зарядов. Ключи и заряды — ОДНА таблица с сервером:
//   генератор `AstoryServer/Patches/AsdaPatch/tools/gen_charge_groups.py` → `p100_data/charge_groups_client.csv`
//   (173 кода; кода нет в таблице = не складывать), в игре — `DT_ChargeGroup` (импорт `Tools/asda_import_charge_groups.py`).
//   Клиентская часть: подбор ячеек-приёмников коробки ДО отправки 6096 (порт `itemFindSowelSlots@0xDAACF0`,
//   `UAsdaInventorySubsystem::ReservePackageSlots`) больше не требует свободную ячейку под спецпредмет, заряды которого
//   сольются в лежащую стопку. Правило — ОДИН В ОДИН с сервером P100 (`AsdaPackageReserve::Compute`).
//   Ключ `asda.Item.ChargeStackReserve`: 1 — новое правило; 0 — эталон (под каждый спецпредмет — свободная ячейка).
//   Заголовок «холодный»: его включают только .cpp подбора ячеек, таблицы предметов и самотест.
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AsdaChargeStack.generated.h"

class UAsdaItemTableSubsystem;
struct FAsdaItemInstance;
struct FAsdaPackageEntry;

// Строка `DT_ChargeGroup` = строка `charge_groups_client.csv` («Name,Code,GroupKey,Charges»; Name = код строкой).
USTRUCT(BlueprintType)
struct FAsdaChargeGroupRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ChargeGroup") int32 Code = 0;       // код спецпредмета (1..637)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ChargeGroup") int32 GroupKey = 0;   // ключ слияния P100 (> 0)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ChargeGroup") int32 Charges = 0;    // заряды одной выдачи (FT ток.9)
};

namespace AsdaChargeStack
{
	/** Потолок зарядов в одной ячейке — как `patches.ini [P100_ChargeStack] Cap=30000` сервера. */
	inline constexpr int32 kCap = 30000;

	/** Имя таблицы в реестре `UAsdaItemTableSubsystem::GetDataTable` (регистрация — `GExtraTables`). */
	ASDAREBORN_API FName TableName();

	/** Ключ `asda.Item.ChargeStackReserve` включён (1 — правило P100 при подборе ячеек коробки; 0 — эталон). */
	ASDAREBORN_API bool IsOn();

	/** Ключ слияния и заряды кода по `DT_ChargeGroup`. false — код не складывается (нет строки / ключ ≤ 0 / таблицы нет). */
	ASDAREBORN_API bool Lookup(const UAsdaItemTableSubsystem* IT, int32 Code, int32& OutKey, int32& OutCharges);

	/** Число кодов в загруженной таблице (0 — таблицы нет); для журнала и самотеста. */
	ASDAREBORN_API int32 NumCodes(const UAsdaItemTableSubsystem* IT);
}

namespace AsdaPackageReserve
{
	/** Подбор до пяти ячеек-приёмников под состав коробки — тело `ReservePackageSlots` (порт `itemFindSowelSlots@0xDAACF0`).
	 *  Contents     — позиции состава по порядку таблицы (`GetPackageContents`);
	 *  Bag/BagSize  — сумка (контейнер 1) и её доступная ёмкость (`GetContainerSize(1)`: 30 или 60);
	 *  Spend2       — расходники (контейнер 2, 60);
	 *  bChargeMerge — [ИСПРАВЛЕНИЕ] правило P100 для складываемых спецпредметов (false — эталон);
	 *  OutSlots15   — 5 троек {u8 контейнер, i16 слот}; OutCount — позиций с контейнером ≠ 0xFF;
	 *  OutShort*    — нехватка ячеек по контейнерам; OutMerged (не обязателен) — позиций, слитых в стопку без новой ячейки.
	 *  Возврат: OutCount > 0. */
	ASDAREBORN_API bool Compute(const UAsdaItemTableSubsystem* IT, const TArray<FAsdaPackageEntry>& Contents,
		const TArray<FAsdaItemInstance>* Bag, int32 BagSize, const TArray<FAsdaItemInstance>* Spend2, bool bChargeMerge,
		TArray<uint8>& OutSlots15, int32& OutCount, int32& OutShortSpend, int32& OutShortBag, int32* OutMerged = nullptr);
}
