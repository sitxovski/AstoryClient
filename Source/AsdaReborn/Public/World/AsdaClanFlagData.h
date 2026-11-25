// Copyright (c) 2026 @sitxovski (Discord)
// Дизайн флага (эмблемы) клана `FAsdaClanFlagData` — 40-байтный блок пакета 6542. Методы — в `AsdaClanFlag.cpp`, как раньше.
//   ★ R3 рефакторинга (11.11.2026): дословно вынесено из `World/AsdaClanFlag.h` (он включает этот файл — прежние потребители ничего
//   не теряют). Причина: `World/AsdaPlayerCharacter.h` и `World/AsdaNameplateComponent.h` включали композитор флага `World/AsdaClanFlag.h` только ради этой структуры.
//   Отражение UE (имя типа, модуль `/Script/AsdaReborn`) не менялось.
#pragma once
#include "CoreMinimal.h"
#include "AsdaClanFlagData.generated.h"

// 40-байтный блок дизайна флага клана (эталон CCharacter+2506, пакет 6542 [3..42]).
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaClanFlagData
{
	GENERATED_BODY()

	// Индекс ФОН-рамки (0-based) в Clan_BorderImage.bin. −1 = нет флага.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|ClanFlag")
	int32 BorderIdx = -1;
	// Тинт рамки (ARGB как в оригинале: 0xAARRGGBB).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|ClanFlag")
	int32 BorderColor = 0;
	// Цвет фона (Clear render-target'а), ARGB.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|ClanFlag")
	int32 BgColor = 0;
	// Индекс СИМВОЛА (инсигнии, 0-based) в Clan_Sign.bin.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|ClanFlag")
	int32 SignIdx = -1;
	// Тинт символа, ARGB.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|ClanFlag")
	int32 SignColor = 0;

	// Есть ли что печь (эталон печёт при clanFlagOn; индексы <0 = запись пустая).
	bool IsValid() const { return BorderIdx >= 0 || SignIdx >= 0; }
	// Ключ кэша (одинаковый дизайн — один render-target на всех членов клана, как объект флага в оригинале).
	FString CacheKey() const;
	// Разбор 40 байт «как в памяти клиента» (пакет 6542, поля через 8 байт).
	static FAsdaClanFlagData FromWire(const uint8* B40);
};
