// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPakIntegrity.cpp — реакция на нарушение целостности архивов (см. заголовок).
#include "Crypto/AsdaPakIntegrity.h"
#include "IPlatformFilePak.h"
#include "Misc/App.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformMisc.h"
#include "GenericPlatform/GenericPlatformMisc.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPakIntegrity, Log, All);

namespace
{
	// Признак, что обработчики уже привязаны (регистрант статический, но защищаемся от повторного Register()).
	bool GAsdaPakIntegrityBound = false;

	// Общий выход: сообщение игроку (если не -unattended) и принудительное завершение с кодом 3.
	void FailAndExit(const FString& What)
	{
		UE_LOG(LogAsdaPakIntegrity, Error, TEXT("[ЦЕЛОСТНОСТЬ] %s — файлы клиента повреждены или изменены, работа прекращена."), *What);
		GLog->Flush();
		if (!FApp::IsUnattended())
		{
			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok,
				TEXT("Файлы игры повреждены или изменены. Переустановите клиент.\nGame files are corrupted or modified. Please reinstall the client."),
				TEXT("Asda Reborn"));
		}
		FPlatformMisc::RequestExitWithStatus(true, 3, TEXT("AsdaPakIntegrity"));
	}

	// Расхождение хэша блока (.pak — FPakPrecacher; .utoc/.ucas — IoDispatcher через лямбду в IPlatformFilePak.cpp:5767).
	void OnChunkSignatureFailed(const FPakChunkSignatureCheckFailedData& Data)
	{
		FailAndExit(FString::Printf(TEXT("хэш блока %d архива '%s' не совпал с подписью"), Data.ChunkIndex, *Data.PakFilename));
	}

	// Нет/не прошла проверку таблица подписей .sig (IPlatformFilePak.cpp:4880–4899).
	void OnPrincipalTableFailed(const FString& PakFilename)
	{
		FailAndExit(FString::Printf(TEXT("таблица подписей архива '%s' отсутствует или не подтверждена ключом"), PakFilename.IsEmpty() ? TEXT("?") : *PakFilename));
	}
}

namespace AsdaPakIntegrity
{
	void Register()
	{
		if (GAsdaPakIntegrityBound)
		{
			return;
		}
		GAsdaPakIntegrityBound = true;
		FPakPlatformFile::FPakSigningFailureHandlerData& Handlers = FPakPlatformFile::GetPakSigningFailureHandlerData();
		FScopeLock Lock(&Handlers.GetLock());
		Handlers.GetPakChunkSignatureCheckFailedDelegate().AddStatic(&OnChunkSignatureFailed);
		Handlers.GetPrincipalSignatureTableCheckFailedDelegate().AddStatic(&OnPrincipalTableFailed);
	}
}
