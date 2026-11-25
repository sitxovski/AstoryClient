// Copyright (c) 2026 @sitxovski (Discord)
// Строка таблицы монет навыков `FAsdaSkillCoinRow` (SkillCoinTable.bin). Разбор раскладки файла — в шапке `Data/AsdaSoulGuardRows.h`.
//   ★ R3 рефакторинга (11.11.2026): дословно вынесено из `Data/AsdaSoulGuardRows.h` (он включает этот файл — прежние потребители ничего
//   не теряют). Причина: `Items/AsdaItemTableSubsystem.h` (веер 264) включал `Data/AsdaSoulGuardRows.h` только ради этой строки.
//   Отражение UE (имя типа, модуль `/Script/AsdaReborn`) не менялось.
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataTable.h"      // FTableRowBase — база строки таблицы
#include "AsdaSkillCoinRow.generated.h"

// Одна из 1255 записей SkillCoinTable.bin. Ключ строки DataTable = SkillId.
USTRUCT(BlueprintType)
struct FAsdaSkillCoinRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") int32 SkillId = 0;   // +0
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") int32 Coin0 = 0;     // +4  монет в ряд 1
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") int32 Coin1 = 0;     // +8  монет в ряд 2
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") int32 Coin2 = 0;     // +12 монет в ряд 3
	// +16: −1 у обычных скиллов; у 1201..1254 — 1/2/3 = вариант боевого мотиона хранителя
	//   (1 → мотион 5 + эффект 31, 2 → мотион 10 + 32, 3 → мотион 11 + 33). См. шапку файла.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") int32 Field16 = -1;
};
