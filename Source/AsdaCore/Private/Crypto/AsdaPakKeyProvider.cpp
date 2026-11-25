// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPakKeyProvider.cpp — восстановление ключа pak/IoStore из замаскированных долей (см. заголовок).
//   Схема долей (та же в Tools/gen_build_crypto.py: split_key/restore_key):
//     ключ = XOR всех долей; доля j хранится как PART_j (переставленная перестановкой PERM_j и замаскированная
//     потоком xorshift64 от SEED_j): ключ[PERM_j[i]] ^= PART_j[i] ^ mask_j[i].
//   Это защита от любопытных (grep по exe не даёт ключ), не от решительного: ключ восстановим из бинаря.
#include "Crypto/AsdaPakKeyProvider.h"
#include "Misc/SecureHash.h"

#if __has_include("AsdaPakKey.generated.h")
#include "AsdaPakKey.generated.h"
#endif

#ifndef ASDA_PAKKEY_PRESENT
// Набора ключей нет (генератор не запускался): провайдер собирается, но ключей не выдаёт.
#define ASDA_PAKKEY_PRESENT 0
#endif

namespace
{
	// Поток маски: xorshift64 от seed, по байту из слова на каждую позицию (формула 1:1 с генератором).
	void MaskStream(uint64 Seed, uint8 Out[32])
	{
		uint64 X = Seed;
		for (int32 I = 0; I < 32; ++I)
		{
			X ^= X << 13;
			X ^= X >> 7;
			X ^= X << 17;
			Out[I] = static_cast<uint8>(X >> ((I % 8) * 8));
		}
	}
}

namespace AsdaPakKey
{
	bool IsConfigured()
	{
		return ASDA_PAKKEY_PRESENT != 0;
	}

	const TCHAR* BuildId()
	{
#if ASDA_PAKKEY_PRESENT
		return ASDA_PAKKEY_BUILD_ID;
#else
		return TEXT("");
#endif
	}

	void RestoreEncryptionKey(unsigned char OutKey[32])
	{
		FMemory::Memzero(OutKey, 32);
#if ASDA_PAKKEY_PRESENT
		for (int32 Share = 0; Share < ASDA_PAKKEY_SHARES; ++Share)
		{
			uint8 Mask[32];
			MaskStream(ASDA_PAKKEY_SEED[Share], Mask);
			for (int32 I = 0; I < 32; ++I)
			{
				OutKey[ASDA_PAKKEY_PERM[Share][I]] ^= static_cast<uint8>(ASDA_PAKKEY_PART[Share][I] ^ Mask[I]);
			}
			FMemory::Memzero(Mask, sizeof(Mask));
		}
#endif
	}

	void GetSigningPublicKey(TArray<uint8>& OutExponent, TArray<uint8>& OutModulus)
	{
		OutExponent.Reset();
		OutModulus.Reset();
#if ASDA_PAKKEY_PRESENT && ASDA_PAKKEY_SIGNING
		OutExponent.Append(ASDA_PAKSIGN_EXPONENT, UE_ARRAY_COUNT(ASDA_PAKSIGN_EXPONENT));
		OutModulus.Append(ASDA_PAKSIGN_MODULUS, UE_ARRAY_COUNT(ASDA_PAKSIGN_MODULUS));
#endif
	}

	bool IsSigningEnabled()
	{
#if ASDA_PAKKEY_PRESENT && ASDA_PAKKEY_SIGNING
		return true;
#else
		return false;
#endif
	}

	bool VerifyFingerprint()
	{
#if ASDA_PAKKEY_PRESENT
		uint8 Key[32];
		RestoreEncryptionKey(Key);
		FSHAHash Hash;
		FSHA1::HashBuffer(Key, sizeof(Key), Hash.Hash);
		FMemory::Memzero(Key, sizeof(Key));
		return FMemory::Memcmp(Hash.Hash, ASDA_PAKKEY_FINGERPRINT, UE_ARRAY_COUNT(ASDA_PAKKEY_FINGERPRINT)) == 0;
#else
		return false;
#endif
	}
}
