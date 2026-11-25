// Copyright (c) 2026 @sitxovski (Discord)
// AsdaAntiCheatSettings.h — настройки клиентского EAC (DefaultGame.ini, секция [/Script/AsdaAntiCheat.AsdaAntiCheatSettings]).
//   Ключи — CONTRACT.md §7. Хост сайдкара отдельно не задаётся: это хост LoginManager из текущего конфига клиента
//   (тот, что уходит в UAsdaNetSubsystem::ConnectToLogin). ClientSecret в репозиторий не кладётся — вписывает координатор.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AsdaAntiCheatSettings.generated.h"

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Asda Anti-Cheat (EAC)"))
class ASDAANTICHEAT_API UAsdaAntiCheatSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// База сервиса входа AsdaId (POST {AuthBaseUrl}/token, /refresh). В Shipping — только https://.
	UPROPERTY(config, EditAnywhere, Category = "AsdaId")
	FString AuthBaseUrl = TEXT("https://@@ASTORY_AUTH_HOST@@");

	// Порт сайдкара EAC (TCP, AsdaSecure под-сервер Eac=4) на хосте LoginManager.
	UPROPERTY(config, EditAnywhere, Category = "Sidecar")
	int32 SidecarPort = 15901;

	// EOS ProductId (несекретный, CONTRACT.md §0).
	UPROPERTY(config, EditAnywhere, Category = "EOS")
	FString ProductId;

	// EOS SandboxId (несекретный).
	UPROPERTY(config, EditAnywhere, Category = "EOS")
	FString SandboxId;

	// EOS DeploymentId (несекретный).
	UPROPERTY(config, EditAnywhere, Category = "EOS")
	FString DeploymentId;

	// ClientId игрового клиента «Alpha» (GameClient, несекретный).
	UPROPERTY(config, EditAnywhere, Category = "EOS")
	FString ClientId;

	// Секрет игрового клиента. Только из ini; в редакторе не показывается, в журнал не пишется. Пусто — платформа EOS
	//   создаётся без секрета (Connect-вход тогда отвергается бэкендом Epic; статус «отказ», игра продолжается).
	UPROPERTY(config)
	FString ClientSecret;

	// Раздел в Project Settings.
	virtual FName GetCategoryName() const override { return TEXT("Asda"); }
};
