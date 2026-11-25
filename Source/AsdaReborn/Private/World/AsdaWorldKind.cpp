// Copyright (c) 2026 @sitxovski (Discord)
#include "World/AsdaWorldKind.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "Login/AsdaLoginGameMode.h"   // режим сцены логина

namespace AsdaWorldKind
{
	bool IsLoginScene(const UWorld* World)
	{
		if (!World) { return false; }
		// Мир игры/PIE: режим создан при загрузке карты (до BeginPlay и PostLoadMap) — решает он.
		if (const AGameModeBase* GM = World->GetAuthGameMode())
		{
			return GM->IsA<AAsdaLoginGameMode>();
		}
		// Режима нет (редактор вне PIE, где `GameMode` не создаётся) — прежний признак по имени карты.
		return World->GetMapName().Contains(TEXT("Login"));
	}

	bool IsLoadingMap(const UWorld* World)
	{
		return World && World->GetName().Contains(TEXT("L_Loading"));
	}
}
