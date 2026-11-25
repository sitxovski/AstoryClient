// Copyright (c) 2026 @sitxovski (Discord)
// ★ R5-fix (13.11.2026, чистка): СЕТКА ПРОХОДИМОСТИ ЛОКАЛЬНОГО ИГРОКА — одна копия. До R5-fix её дословно держали
//   `UAsdaMinimapSubsystem::GetGrid` (миникарта) и `UAsdaZoneSubsystem::GetPlayerGrid` (зоны и музыка) — пункт B17 аудита
//   дублей 14.09.2026. Оба метода остались (их зовут 17 мест) и стали вызовом этой функции.
#pragma once

#include "CoreMinimal.h"

class UGameInstance;
class UAsdaWalkGrid;
class AAsdaPlayerCharacter;

namespace AsdaLocalGrid
{
	// Сетка проходимости карты, на которой стоит локальный игрок, и сам игрок в OutPlayer. nullptr (и OutPlayer = nullptr) —
	//   нет GameInstance, подсистемы мира или валидного павна ТЕКУЩЕГО мира.
	const UAsdaWalkGrid* Get(const UGameInstance* GI, AAsdaPlayerCharacter*& OutPlayer);
}
