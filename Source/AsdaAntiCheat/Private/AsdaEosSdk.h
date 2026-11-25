// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEosSdk.h — загрузка EOS SDK 1.19.2.1 (DLL полным путём) и EOS_Initialize один раз на процесс.
//   Почему полным путём: в Engine/Binaries/Win64 лежит одноимённая EOSSDK-Win64-Shipping.dll движка (1.19.1.2) — см.
//   ★ ред. 3 (E1-17): наша копия — в подпапке Binaries/Win64/AsdaEOS, отложенный импорт привязан к ней хуком delay-load.
//   Source/ThirdParty/AsdaEOSSDK/AsdaEOSSDK.Build.cs. EOS_Shutdown — только при выгрузке модуля (повторный
//   EOS_Initialize в том же процессе SDK не поддерживает, а подсистема в PIE создаётся заново).
#pragma once

#include "CoreMinimal.h"

class FAsdaEosSdk
{
public:
	// Единственный экземпляр на процесс.
	static FAsdaEosSdk& Get();

	// Загрузить DLL и выполнить EOS_Initialize (+ лог SDK в LogAsdaAntiCheat). Повторный вызов — итог первого.
	//   false — SDK недоступен (причина в OutWhy и журнале).
	bool EnsureInitialized(FString& OutWhy);
	// SDK загружен и инициализирован.
	bool IsReady() const { return bReady; }
	// Версия загруженной DLL (EOS_GetVersion) — для журнала.
	const FString& GetVersion() const { return Version; }
	// Полный путь загруженной DLL — для журнала.
	const FString& GetDllPath() const { return DllPath; }
	// ★ ред. 3 (E1-17): дескриптор нашей копии DLL (HMODULE) для хука отложенной загрузки; nullptr — ещё не загружена.
	void* GetDllHandleRaw() const { return DllHandle; }
	// EOS_Shutdown и выгрузка DLL (ShutdownModule).
	void Shutdown();

private:
	bool bTried = false;       // загрузка уже выполнялась
	bool bReady = false;       // DLL загружена и EOS_Initialize прошёл
	bool bOwnsSdk = false;     // EOS_Initialize выполнили мы (не EOSShared редактора) — нам и EOS_Shutdown, и лог-колбэк
	FString FailWhy;           // причина неудачи первой попытки
	void* DllHandle = nullptr; // дескриптор DLL
	FString Version;           // EOS_GetVersion
	FString DllPath;           // путь DLL
};
