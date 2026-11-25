// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPakIntegrity.h — ★ ОБРАБОТЧИК ОТКАЗА ПОДПИСИ АРХИВОВ (волна В2 античита, 12.10.2026).
//   Движок при расхождении хэша блока .pak/.ucas лишь пишет Warning и зовёт делегат — стандартного обработчика
//   НЕТ (IPlatformFilePak.cpp:3200–3215, IoDispatcherFileBackend.cpp:1723–1750 → :5767 BroadcastPakChunkSignature-
//   CheckFailure; потребитель в движке один — счётчик в тесте PakFile.cpp:1960). Без своего обработчика
//   подменённый блок читался бы дальше. Здесь: лог, сообщение игроку и немедленный выход с кодом 3.
#pragma once

#include "CoreMinimal.h"

namespace AsdaPakIntegrity
{
	// Привязать обработчики к FPakPlatformFile::GetPakSigningFailureHandlerData() (повторный вызов — без дублей).
	void Register();
}
