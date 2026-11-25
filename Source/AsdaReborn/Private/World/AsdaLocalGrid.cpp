// Copyright (c) 2026 @sitxovski (Discord)
// ★ R5-fix (13.11.2026, чистка): сетка проходимости локального игрока — см. AsdaLocalGrid.h.
#include "World/AsdaLocalGrid.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWalkGrid.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

const UAsdaWalkGrid* AsdaLocalGrid::Get(const UGameInstance* GI, AAsdaPlayerCharacter*& OutPlayer)
{
	OutPlayer = nullptr;
	if (!GI) { return nullptr; }
	const UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	if (!WS) { return nullptr; }
	AAsdaPlayerCharacter* Local = Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor);
	// Подсистема живёт на GameInstance и переживает травел: павн предыдущего уровня к этому моменту
	//   уже уничтожен, но ссылка может быть ещё жива → берём только валидный павн ТЕКУЩЕГО мира.
	if (!IsValid(Local) || Local->GetWorld() != GI->GetWorld()) { return nullptr; }
	OutPlayer = Local;
	return Local->WalkGrid;
}
