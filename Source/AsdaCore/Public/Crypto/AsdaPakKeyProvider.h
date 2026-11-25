// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPakKeyProvider.h — ★ ПРОВАЙДЕР КЛЮЧЕЙ pak/IoStore (волна В2 античита, 12.10.2026).
//   Ключ шифрования архивов сборки НЕ лежит в исполняемом файле открытым текстом: Tools/gen_build_crypto.py
//   раскладывает его на замаскированные доли (AsdaPakKey.generated.h), а здесь он собирается ТОЛЬКО в памяти,
//   когда движок просит ключ через FCoreDelegates::GetPakEncryptionKeyDelegate (IPlatformFilePak.cpp:4913–4921,
//   EncryptionKeyManager.cpp:12–21). Открытый ключ подписи отдаётся через GetPakSigningKeysDelegate.
//   Привязка делегатов — в регистранте первичного игрового модуля (Source/AsdaReborn/Private/AsdaReborn.cpp),
//   там же сняты штатные макросы движка, встраивающие ключ открытым текстом (TargetRules.cs:3434–3459).
#pragma once

#include "CoreMinimal.h"

namespace AsdaPakKey
{
	// Есть ли сгенерированный набор ключей (заголовок AsdaPakKey.generated.h найден при компиляции).
	ASDACORE_API bool IsConfigured();

	// Идентификатор набора ключей (дата-время генерации + случайный хвост); пустая строка — набора нет.
	ASDACORE_API const TCHAR* BuildId();

	// Восстановить AES-256 ключ из долей. Сигнатура совпадает с колбэком RegisterEncryptionKeyCallback
	// (CoreDelegates.cpp:400). При отсутствии набора — нули (движок сочтёт ключ невалидным).
	ASDACORE_API void RestoreEncryptionKey(unsigned char OutKey[32]);

	// Открытый ключ подписи: экспонента и модуль little-endian (как ожидает FRSA::CreateKey). Сигнатура совпадает
	// с колбэком RegisterSigningKeyCallback (CoreDelegates.cpp:392). Без набора или без подписи — пустые массивы.
	ASDACORE_API void GetSigningPublicKey(TArray<uint8>& OutExponent, TArray<uint8>& OutModulus);

	// Включена ли подпись в наборе (bEnablePakSigning при генерации).
	ASDACORE_API bool IsSigningEnabled();

	// Самопроверка: восстановленный ключ совпадает с отпечатком набора (первые 8 байт SHA-1).
	ASDACORE_API bool VerifyFingerprint();
}
