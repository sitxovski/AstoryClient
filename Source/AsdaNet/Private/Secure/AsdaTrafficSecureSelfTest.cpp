// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ шифрования трафика (волна В7b): `asda.SelfTest.TrafficSecure [quit]`.
//   Петля ВНУТРИ клиента, БЕЗ сети (-AsdaNoNet дружелюбно): ClientChannel ↔ ServerChannel в одном
//   процессе через байтовые буферы. Эфемерная долговременная пара генерится на месте (keygen не нужен),
//   открытый ключ отдаётся клиенту — проверяется подпись сервера, затем AEAD в обе стороны байт-в-байт.
//   Ничего не отправляет в сеть. Зеркало stand_wire (AstoryServer/Patches/AsdaSecure/wire/stand_wire.cpp).
#include "CoreMinimal.h"

#if PLATFORM_WINDOWS && !UE_BUILD_SHIPPING

#include "Secure/AsdaSecure.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include <vector>
#include <cstring>
// ★ EAC ред. 3: независимый эталон вывода ключей и привязки (CNG напрямую, мимо кода AsdaSecure) — случай 13.
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <bcrypt.h>
#include "Windows/HideWindowsPlatformTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTrafficSecure, Log, All);

namespace AsdaTrafficSecureTest
{
	using namespace AsdaSecure;

	struct FRun
	{
		int32 Ok = 0, Fail = 0;
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaTrafficSecure, Log, TEXT("ШИФР-ТРАФИК ok   %s"), *What); }
			else { ++Fail; UE_LOG(LogAsdaTrafficSecure, Error, TEXT("ШИФР-ТРАФИК FAIL %s"), *What); }
		}
	};

	static std::vector<uint8_t> MakeFrame(uint16 op, int n, uint8 fill)
	{
		std::vector<uint8_t> f;
		const uint16 size = (uint16)(9 + n + 1);
		f.push_back(0xFB);
		f.push_back(size & 0xFF); f.push_back(size >> 8);
		f.push_back(0); f.push_back(0); f.push_back(0); f.push_back(0);
		f.push_back(op & 0xFF); f.push_back(op >> 8);
		for (int i = 0; i < n; ++i) { f.push_back(fill); }
		f.push_back(0xFE);
		return f;
	}

	// ── ★ EAC ред. 3 (CONTRACT.md §8.1): тестовый вектор привязки канала ─────────────────────────────────
	//   Источник — `Reports/anticheat/eac/bind_test_vector.txt` (генератор серверного исполнителя
	//   `AstoryServer/Patches/AsdaSecure/tools/gen_bind_vector.py`, Python cryptography + hmac — независимо от CNG-кода; сверен его
	//   стендом stand_bind.cpp). Скаляры dC/dS — только для теста, к ключу сервера отношения не имеют.
	struct FBindVec
	{
		uint8 Sub;                 // под-сервер рукопожатия (2 Game, 3 Indun)
		const char* DC;            // закрытый скаляр эфемерного ключа клиента
		const char* PubC;          // ePubC (X||Y)
		const char* DS;            // закрытый скаляр эфемерного ключа сервера
		const char* PubS;          // ePubS (X||Y)
		const char* NonceC;        // nonce клиента
		const char* NonceS;        // nonce сервера
		const char* Raw;           // X общего секрета ECDH (big-endian)
		const char* Prk;           // HMAC-SHA256(salt = nonceC||nonceS, raw)
		const char* InfoBind;      // info_bind (147 б)
		const char* Okm;           // c2sKey(32) c2sIv(12) s2cKey(32) s2cIv(12) — НЕ меняется ред. 3
		const char* Bind;          // HKDF-Expand(prk, info_bind, 32)
	};
	static const FBindVec kBindVec[2] =
	{
		{ 2,
		  "b87fd651f578c38d938f81f6241460ebf5776f6ccd416e2e551cf3783a3885c5",
		  "729277db115986c76d33b4a78d41765aaaa6b27f5df153de67291ac17ab30cba3349fbe4939b7df2d5ace45dcd56d614caa65f780167b66037c0892f49cf78e2",
		  "441a89f5a5d0a83e8dcbf92664f32cb2c0f87f36a2c1565a11c7b05f47327f4c",
		  "121a180207cd2805301034ce539b3d68f0be4c6dc92723ab786e68bf28861699fe67e6813383cb5927fe223ce1e86879f77eb0184fddf3acbb460de42bd5a77f",
		  "4e27df2d1c928204315e6e6eb257da03",
		  "ae08d51637eac56c80d3b3feef121957",
		  "df5d71a0dc960c8e2d2e15dd955ead1833a00fac83a5c373b58fc6660f798c52",
		  "ad237095332240dc373da17ad5d4be3d72989dd65b86ce1ffbd416d4a0548ea1",
		  "010202729277db115986c76d33b4a78d41765aaaa6b27f5df153de67291ac17ab30cba3349fbe4939b7df2d5ace45dcd56d614caa65f780167b66037c0892f49cf78e2121a180207cd2805301034ce539b3d68f0be4c6dc92723ab786e68bf28861699fe67e6813383cb5927fe223ce1e86879f77eb0184fddf3acbb460de42bd5a77f4173646145616342696e642f76310000",
		  "1aa5a227bb58bb1a5e738af49fca66186c9793308b6f63d0756c7c70a891d957" "6b455858ae185582f9536ab4"
		  "f965020c6b70b10b622a265ed38853359f88bda25d391c6ae9fa6ad968f18e20" "bd69bbd69dcbdc7f89c91524",
		  "c9b8a71b201df85edf5362567241ef5cccc9529c2cbaf2d97cb429e40cd6b4f5" },
		{ 3,
		  "8f3709dbf66bbae777c6235f0cf631e572990b660aa625af0bc76afa8d325280",
		  "b48cdc0062ce16d7966ec1af321630b69463245d31cdfefea8b9dc555e10cb54109c68d2b743e14b9017b61b62a0664b148ca625b1592e4c398b303e1f7b81e6",
		  "983eb0742c976cb2197e87bb45a58efe253ba65955a81176dcc4b9145f51a205",
		  "442e65232342942c5b5c9d24ab4cf39ba637ce17f067501d1df9664c7eb130801214f5229e2b646d263941ddf03f07d00a195b5cbf8664da11cb7717fde305f2",
		  "5bc3bce917ca5a1b393567d119a8f6ec",
		  "33828220443b59b3966658c35e0725e7",
		  "f0bda425d5437f393549e5c9741c97f7a089c206bc702dffaf9191c5abf28572",
		  "408c82019fa7f188c880b7b158770b9ea949e206f1fab1c15ff10617e17617be",
		  "010303b48cdc0062ce16d7966ec1af321630b69463245d31cdfefea8b9dc555e10cb54109c68d2b743e14b9017b61b62a0664b148ca625b1592e4c398b303e1f7b81e6442e65232342942c5b5c9d24ab4cf39ba637ce17f067501d1df9664c7eb130801214f5229e2b646d263941ddf03f07d00a195b5cbf8664da11cb7717fde305f24173646145616342696e642f76310000",
		  "6e1b101dd09f7f2888d131b61cfb7d2d2f5f87b764fef5567a1ea8dfddad844e" "542a1a5b867f14ce034773c1"
		  "265c855facbe741270a40df7f79b9ab55a0db0caddcfc3e2a45ea30f773f2efd" "d8e308ecd42578a794a08a02",
		  "108bb267bb1e1eb6ae176cda5d177099bfa988aaefe552bb3ea504b1b895e690" },
	};

	// hex → байты (строчные/прописные).
	static std::vector<uint8_t> Unhex(const char* H)
	{
		std::vector<uint8_t> Out;
		auto Nib = [](char C) -> int { return (C >= '0' && C <= '9') ? C - '0' : (C >= 'a' && C <= 'f') ? C - 'a' + 10 : (C >= 'A' && C <= 'F') ? C - 'A' + 10 : 0; };
		const size_t N = strlen(H);
		for (size_t i = 0; i + 1 < N; i += 2) { Out.push_back((uint8_t)((Nib(H[i]) << 4) | Nib(H[i + 1]))); }
		return Out;
	}

	// Эталонная криптография теста — CNG напрямую (провайдеры открываются на время теста).
	struct FRefCng
	{
		BCRYPT_ALG_HANDLE Ecdh = nullptr;   // ECDH P-256
		BCRYPT_ALG_HANDLE Hmac = nullptr;   // HMAC-SHA256
		BCRYPT_ALG_HANDLE Aes = nullptr;    // AES-GCM
		bool Open()
		{
			return BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&Ecdh, BCRYPT_ECDH_P256_ALGORITHM, nullptr, 0))
				&& BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&Hmac, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG))
				&& BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&Aes, BCRYPT_AES_ALGORITHM, nullptr, 0))
				&& BCRYPT_SUCCESS(BCryptSetProperty(Aes, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0));
		}
		~FRefCng()
		{
			if (Ecdh) { BCryptCloseAlgorithmProvider(Ecdh, 0); }
			if (Hmac) { BCryptCloseAlgorithmProvider(Hmac, 0); }
			if (Aes)  { BCryptCloseAlgorithmProvider(Aes, 0); }
		}
		// Импорт эфемерной пары ECDH по скаляру d и X||Y.
		BCRYPT_KEY_HANDLE ImportPriv(const uint8_t* D, const uint8_t* Xy) const
		{
			uint8_t Blob[8 + 64 + 32]; const ULONG Magic = BCRYPT_ECDH_PRIVATE_P256_MAGIC, Cb = 32;
			memcpy(Blob, &Magic, 4); memcpy(Blob + 4, &Cb, 4); memcpy(Blob + 8, Xy, 64); memcpy(Blob + 72, D, 32);
			BCRYPT_KEY_HANDLE K = nullptr;
			if (!BCRYPT_SUCCESS(BCryptImportKeyPair(Ecdh, nullptr, BCRYPT_ECCPRIVATE_BLOB, &K, Blob, sizeof(Blob), 0))) { K = nullptr; }
			SecureZeroMemory(Blob, sizeof(Blob));
			return K;
		}
		// X(d * PeerPub), big-endian (CNG отдаёт little-endian — разворот, как в AsdaSecure).
		bool RawX(BCRYPT_KEY_HANDLE Priv, const uint8_t* PeerXy, uint8_t Out[32]) const
		{
			uint8_t Blob[8 + 64]; const ULONG Magic = BCRYPT_ECDH_PUBLIC_P256_MAGIC, Cb = 32;
			memcpy(Blob, &Magic, 4); memcpy(Blob + 4, &Cb, 4); memcpy(Blob + 8, PeerXy, 64);
			BCRYPT_KEY_HANDLE Peer = nullptr; BCRYPT_SECRET_HANDLE Sec = nullptr;
			if (!BCRYPT_SUCCESS(BCryptImportKeyPair(Ecdh, nullptr, BCRYPT_ECCPUBLIC_BLOB, &Peer, Blob, sizeof(Blob), 0))) { return false; }
			const bool bAgree = BCRYPT_SUCCESS(BCryptSecretAgreement(Priv, Peer, &Sec, 0));
			BCryptDestroyKey(Peer);
			if (!bAgree) { return false; }
			ULONG Got = 0;
			const bool bOk = BCRYPT_SUCCESS(BCryptDeriveKey(Sec, BCRYPT_KDF_RAW_SECRET, nullptr, Out, 32, &Got, 0)) && Got == 32;
			BCryptDestroySecret(Sec);
			for (int i = 0; i < 16; ++i) { const uint8_t T = Out[i]; Out[i] = Out[31 - i]; Out[31 - i] = T; }
			return bOk;
		}
		bool HmacSha256(const uint8_t* Key, size_t KeyLen, const uint8_t* Msg, size_t MsgLen, uint8_t Mac[32]) const
		{
			BCRYPT_HASH_HANDLE H = nullptr;
			if (!BCRYPT_SUCCESS(BCryptCreateHash(Hmac, &H, nullptr, 0, (PUCHAR)Key, (ULONG)KeyLen, 0))) { return false; }
			const bool bOk = BCRYPT_SUCCESS(BCryptHashData(H, (PUCHAR)Msg, (ULONG)MsgLen, 0)) && BCRYPT_SUCCESS(BCryptFinishHash(H, Mac, 32, 0));
			BCryptDestroyHash(H);
			return bOk;
		}
		// HKDF-Expand (RFC 5869): T(i) = HMAC(prk, T(i-1) | info | i).
		bool Expand(const uint8_t Prk[32], const std::vector<uint8_t>& Info, uint8_t* Out, size_t Len) const
		{
			std::vector<uint8_t> T; size_t Done = 0; uint8_t Ctr = 1;
			while (Done < Len)
			{
				std::vector<uint8_t> M(T); M.insert(M.end(), Info.begin(), Info.end()); M.push_back(Ctr++);
				uint8_t Mac[32];
				if (!HmacSha256(Prk, 32, M.data(), M.size(), Mac)) { return false; }
				T.assign(Mac, Mac + 32);
				const size_t Take = (Len - Done < 32) ? (Len - Done) : 32;
				memcpy(Out + Done, Mac, Take); Done += Take;
			}
			return true;
		}
		// Эталонный вывод по формулам вектора: okm(88) ключей направлений и bind(32); prk и info_bind — для сверки.
		bool Derive(const uint8_t* Ch, const uint8_t* Sh, const uint8_t Raw[32], uint8_t Prk[32], std::vector<uint8_t>& InfoBind,
			uint8_t Okm[88], uint8_t Bind[32]) const
		{
			uint8_t Salt[32]; memcpy(Salt, Ch + 68, 16); memcpy(Salt + 16, Sh + 68, 16);
			if (!HmacSha256(Salt, 32, Raw, 32, Prk)) { return false; }
			std::vector<uint8_t> Info;
			Info.push_back(1); Info.push_back(Ch[2]); Info.push_back(Sh[2]);
			Info.insert(Info.end(), Ch + 4, Ch + 68); Info.insert(Info.end(), Sh + 4, Sh + 68);
			InfoBind = Info;
			static const uint8_t kKeyTag[16] = { 'A','s','d','a','S','e','c','u','r','e','/','v','1',0,0,0 };
			static const uint8_t kBindTag[16] = { 'A','s','d','a','E','a','c','B','i','n','d','/','v','1',0,0 };
			Info.insert(Info.end(), kKeyTag, kKeyTag + 16);
			InfoBind.insert(InfoBind.end(), kBindTag, kBindTag + 16);
			return Expand(Prk, Info, Okm, 88) && Expand(Prk, InfoBind, Bind, 32);
		}
		// Расшифровать кадр AsdaSecure [u32 len][u64 seq][ct][tag16] ключом/IV направления (IV = base XOR be(seq)).
		bool OpenFrame(const uint8_t Key[32], const uint8_t BaseIv[12], const std::vector<uint8_t>& Frame, std::vector<uint8_t>& Plain) const
		{
			if (Frame.size() < 12 + 16) { return false; }
			uint32_t CLen = 0; uint64_t Seq = 0; memcpy(&CLen, Frame.data(), 4); memcpy(&Seq, Frame.data() + 4, 8);
			if (CLen < 16 || Frame.size() != 12 + (size_t)CLen) { return false; }
			const size_t N = CLen - 16;
			uint8_t Iv[12]; memcpy(Iv, BaseIv, 12);
			for (int i = 0; i < 8; ++i) { Iv[11 - i] ^= (uint8_t)(Seq >> (8 * i)); }
			BCRYPT_KEY_HANDLE K = nullptr;
			if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(Aes, &K, nullptr, 0, (PUCHAR)Key, 32, 0))) { return false; }
			BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO Mi; BCRYPT_INIT_AUTH_MODE_INFO(Mi);
			uint8_t Aad[12]; memcpy(Aad, Frame.data(), 12);
			Mi.pbNonce = Iv; Mi.cbNonce = 12; Mi.pbAuthData = Aad; Mi.cbAuthData = 12;
			Mi.pbTag = (PUCHAR)(Frame.data() + 12 + N); Mi.cbTag = 16;
			Plain.resize(N); ULONG Done = 0;
			const bool bOk = BCRYPT_SUCCESS(BCryptDecrypt(K, (PUCHAR)(Frame.data() + 12), (ULONG)N, &Mi, nullptr, 0, Plain.data(), (ULONG)N, &Done, 0));
			BCryptDestroyKey(K);
			return bOk;
		}
	};
}

static void AsdaTrafficSecure_Run(const TArray<FString>& Args)
{
	using namespace AsdaTrafficSecureTest;
	FRun R;
	bool bQuit = false;
	for (const FString& A : Args) { if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; } }

	if (!AsdaSecure::Init()) { UE_LOG(LogAsdaTrafficSecure, Error, TEXT("ШИФР-ТРАФИК ИТОГ: ОТКАЗ — CNG недоступен")); if (bQuit && GEngine) { GEngine->Exec(nullptr, TEXT("quit")); } return; }

	PrivateKey priv{};
	R.Expect(GenerateLongTermKey(priv), TEXT("генерация долговременной пары сервера"));
	PublicKey pub{}; FMemory::Memcpy(pub.xy, priv.xy, kPubLen);

	ServerChannel srv; srv.Reset(); srv.SetIdentity(SubServer::Login, &priv); srv.SetAllowLegacy(true);
	ClientChannel cli; cli.Reset();

	// 1. Рукопожатие.
	std::vector<uint8_t> cHello, sParser, sSocket, cParser;
	R.Expect(cli.Start(SubServer::Login, pub, cHello) && cHello.size() == kClientHelloLen, TEXT("ClientHello 84 б"));
	Err e = srv.OnRecv(cHello.data(), cHello.size(), sParser, sSocket);
	R.Expect(e == Err::Ok && sSocket.size() == kServerHelloLen && sParser.empty(), TEXT("сервер → ServerHello 148 б"));
	e = cli.OnRecv(sSocket.data(), sSocket.size(), cParser);
	R.Expect(e == Err::Ok && cli.IsReady(), TEXT("клиент проверил подпись, готов"));

	// 2. Клиент → сервер, байт-в-байт.
	std::vector<uint8_t> f1 = MakeFrame(0x3EB, 32, 0xAB);
	std::vector<uint8_t> enc; cli.OnSend(f1.data(), f1.size(), enc);
	R.Expect(!enc.empty(), TEXT("кадр клиента зашифрован"));
	std::vector<uint8_t> dec, dummy;
	e = srv.OnRecv(enc.data(), enc.size(), dec, dummy);
	R.Expect(e == Err::Ok && dec == f1, TEXT("старый сервер видит кадр БАЙТ-В-БАЙТ (c2s)"));

	// 3. Сервер → клиент, байт-в-байт.
	std::vector<uint8_t> f2 = MakeFrame(0x3EC, 64, 0x7E);
	std::vector<uint8_t> enc2; srv.OnSend(f2.data(), f2.size(), enc2);
	std::vector<uint8_t> dec2;
	e = cli.OnRecv(enc2.data(), enc2.size(), dec2);
	R.Expect(e == Err::Ok && dec2 == f2, TEXT("клиент видит кадр БАЙТ-В-БАЙТ (s2c)"));

	// 4. Порча тега → отказ.
	std::vector<uint8_t> enc3; cli.OnSend(f1.data(), f1.size(), enc3);
	if (!enc3.empty()) { enc3.back() ^= 0xFF; }
	std::vector<uint8_t> dec3, dummy2;
	e = srv.OnRecv(enc3.data(), enc3.size(), dec3, dummy2);
	R.Expect(e == Err::BadTag, TEXT("порча тега GCM → отказ BadTag"));

	// 5. Старый клиент (0xFB) на отдельном канале → Legacy, байты как есть.
	ServerChannel leg; leg.Reset(); leg.SetIdentity(SubServer::Login, &priv); leg.SetAllowLegacy(true);
	std::vector<uint8_t> old = MakeFrame(0x3E8, 16, 0x42), legParser, legSock;
	e = leg.OnRecv(old.data(), old.size(), legParser, legSock);
	R.Expect(e == Err::Ok && leg.GetMode() == Mode::Legacy && legParser == old, TEXT("старый клиент: Legacy, байты без изменений"));

	// ── ★ В7d: разрыв и переход ──────────────────────────────────────────────────────────────────────────
	// 6. Сервер со строгим режимом (AllowLegacy=0): старый клиент 0xFB → отказ BadMagic (сервер рвёт, вид 58).
	{
		ServerChannel strict; strict.Reset(); strict.SetIdentity(SubServer::Login, &priv); strict.SetAllowLegacy(false);
		std::vector<uint8_t> p6, s6;
		e = strict.OnRecv(old.data(), old.size(), p6, s6);
		R.Expect(e == Err::BadMagic && p6.empty(), TEXT("AllowLegacy=0: старый клиент (0xFB) → BadMagic, ничего разборщику"));
	}
	// 7. Сервер: мусор до рукопожатия → BadMagic; повтор кадра клиента → Replay; длина > 64 КБ → Format.
	{
		ServerChannel s7; s7.Reset(); s7.SetIdentity(SubServer::Login, &priv); s7.SetAllowLegacy(true);
		uint8_t junk[8] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
		std::vector<uint8_t> a, b;
		R.Expect(s7.OnRecv(junk, 8, a, b) == Err::BadMagic, TEXT("сервер: мусор до рукопожатия → BadMagic"));

		ServerChannel s8; s8.Reset(); s8.SetIdentity(SubServer::Login, &priv);
		ClientChannel c8; c8.Reset();
		std::vector<uint8_t> h8, sp8, ss8, cp8;
		c8.Start(SubServer::Login, pub, h8); s8.OnRecv(h8.data(), h8.size(), sp8, ss8); c8.OnRecv(ss8.data(), ss8.size(), cp8);
		std::vector<uint8_t> e8; c8.OnSend(f1.data(), f1.size(), e8);
		std::vector<uint8_t> d8a, d8b, x;
		s8.OnRecv(e8.data(), e8.size(), d8a, x);
		R.Expect(s8.OnRecv(e8.data(), e8.size(), d8b, x) == Err::Replay, TEXT("сервер: повтор кадра клиента → Replay"));

		ServerChannel s9; s9.Reset(); s9.SetIdentity(SubServer::Login, &priv);
		ClientChannel c9; c9.Reset();
		std::vector<uint8_t> h9, sp9, ss9, cp9;
		c9.Start(SubServer::Login, pub, h9); s9.OnRecv(h9.data(), h9.size(), sp9, ss9); c9.OnRecv(ss9.data(), ss9.size(), cp9);
		uint8_t hdr[12] = {}; const uint32 clen = (uint32)(kMaxPlain + kTagLen + 1); FMemory::Memcpy(hdr, &clen, 4);
		std::vector<uint8_t> d9;
		R.Expect(s9.OnRecv(hdr, 12, d9, x) == Err::Format, TEXT("сервер: длина кадра > 64 КБ → Format"));
	}
	// 8. Клиент против сервера БЕЗ шифрования: ответ старым кадром → BadMagic сразу (понятная причина, без ожидания).
	{
		ClientChannel c10; c10.Reset(); std::vector<uint8_t> h10, d10;
		c10.Start(SubServer::Login, pub, h10);
		R.Expect(c10.OnRecv(old.data(), old.size(), d10) == Err::BadMagic && !c10.IsReady(),
			TEXT("клиент: сервер без шифрования ответил 0xFB → BadMagic сразу"));
	}
	// 9. Клиент: часть ServerHello → нет ошибки и нет готовности (добирает срок asda.Net.SecureHandshakeTimeoutSec);
	//    подменённая подпись → BadSignature.
	{
		ServerChannel s11; s11.Reset(); s11.SetIdentity(SubServer::Login, &priv);
		ClientChannel c11; c11.Reset(); std::vector<uint8_t> h11, sp11, ss11, d11;
		c11.Start(SubServer::Login, pub, h11); s11.OnRecv(h11.data(), h11.size(), sp11, ss11);
		R.Expect(c11.OnRecv(ss11.data(), 40, d11) == Err::Ok && !c11.IsReady(), TEXT("клиент: часть ServerHello — ждёт (ни ошибки, ни готовности)"));
		ss11[100] ^= 0x01;
		R.Expect(c11.OnRecv(ss11.data() + 40, ss11.size() - 40, d11) == Err::BadSignature, TEXT("клиент: подменённая подпись ServerHello → BadSignature"));
	}
	// 10. Клиент: порча / повтор кадра сервера → BadTag / Replay (FAsdaConnection рвёт соединение с причиной в логе).
	{
		ServerChannel s12; s12.Reset(); s12.SetIdentity(SubServer::Game, &priv);
		ClientChannel c12; c12.Reset(); std::vector<uint8_t> h12, sp12, ss12, d12;
		c12.Start(SubServer::Game, pub, h12); s12.OnRecv(h12.data(), h12.size(), sp12, ss12); c12.OnRecv(ss12.data(), ss12.size(), d12);
		std::vector<uint8_t> z1, z2; s12.OnSend(f2.data(), f2.size(), z1);
		std::vector<uint8_t> r1, r2, r3;
		c12.OnRecv(z1.data(), z1.size(), r1);
		R.Expect(c12.OnRecv(z1.data(), z1.size(), r2) == Err::Replay, TEXT("клиент: повтор кадра сервера → Replay"));
		// Порча — на СВЕЖЕЙ паре: после Replay отвергнутый кадр остаётся в накопителе сессии (соединение всё равно рвётся).
		ServerChannel s12b; s12b.Reset(); s12b.SetIdentity(SubServer::Game, &priv);
		ClientChannel c12b; c12b.Reset(); std::vector<uint8_t> h12b, sp12b, ss12b, d12b;
		c12b.Start(SubServer::Game, pub, h12b); s12b.OnRecv(h12b.data(), h12b.size(), sp12b, ss12b); c12b.OnRecv(ss12b.data(), ss12b.size(), d12b);
		s12b.OnSend(f2.data(), f2.size(), z2); z2[14] ^= 0x80;
		R.Expect(c12b.OnRecv(z2.data(), z2.size(), r3) == Err::BadTag, TEXT("клиент: порча кадра сервера → BadTag"));
	}
	// 11. Кадр, отправленный ДО ServerHello (вход в мир 20000 сразу после connect), уходит Flush сразу после готовности.
	{
		ServerChannel s13; s13.Reset(); s13.SetIdentity(SubServer::Game, &priv);
		ClientChannel c13; c13.Reset(); std::vector<uint8_t> h13, sp13, ss13, d13, q13;
		c13.Start(SubServer::Game, pub, h13);
		std::vector<uint8_t> enter = MakeFrame(20000, 40, 0x2C);
		c13.OnSend(enter.data(), enter.size(), q13);
		R.Expect(q13.empty() && c13.HasPending(), TEXT("кадр до рукопожатия — в очереди клиента"));
		s13.OnRecv(h13.data(), h13.size(), sp13, ss13); c13.OnRecv(ss13.data(), ss13.size(), d13);
		std::vector<uint8_t> fl, srvSeen, xx;
		R.Expect(c13.Flush(fl) == Err::Ok && !fl.empty() && !c13.HasPending(), TEXT("Flush после ServerHello выдал шифротекст очереди"));
		R.Expect(s13.OnRecv(fl.data(), fl.size(), srvSeen, xx) == Err::Ok && srvSeen == enter, TEXT("сервер получил отложенный кадр БАЙТ-В-БАЙТ без следующей отправки"));
	}
	// 11b. ★ EAC Э1 (16.10.2026): под-сервер Eac = 4 (канал 15901 к сайдкару) — рукопожатие и сырой поток (кадр EAC
	//   `u16 len · u8 type`, не FB..FE) в обе стороны; сервер с другой ролью отвергает ClientHello EAC (BadSubServer).
	{
		R.Expect((uint8)SubServer::Eac == 4, TEXT("SubServer::Eac = 4 (CONTRACT.md §3)"));
		ServerChannel s14; s14.Reset(); s14.SetIdentity(SubServer::Eac, &priv); s14.SetAllowLegacy(false);
		ClientChannel c14; c14.Reset(); std::vector<uint8_t> h14, sp14, ss14, d14;
		R.Expect(c14.Start(SubServer::Eac, pub, h14) && h14.size() == kClientHelloLen && h14[2] == 4, TEXT("ClientHello EAC: байт под-сервера = 4"));
		R.Expect(s14.OnRecv(h14.data(), h14.size(), sp14, ss14) == Err::Ok && c14.OnRecv(ss14.data(), ss14.size(), d14) == Err::Ok && c14.IsReady(),
			TEXT("рукопожатие EAC: подпись сервера проверена, канал готов"));
		const std::vector<uint8_t> ping = { 0x01, 0x00, 0x04 };   // кадр EAC PING: len=1, type=4
		std::vector<uint8_t> ct, seen, back;
		R.Expect(c14.OnSend(ping.data(), ping.size(), ct) == Err::Ok && s14.OnRecv(ct.data(), ct.size(), seen, back) == Err::Ok && seen == ping,
			TEXT("сырой поток EAC клиент→сервер байт-в-байт"));
		const std::vector<uint8_t> pong = { 0x01, 0x00, 0x84 };   // PONG
		std::vector<uint8_t> ct2, seen2;
		R.Expect(s14.OnSend(pong.data(), pong.size(), ct2) == Err::Ok && c14.OnRecv(ct2.data(), ct2.size(), seen2) == Err::Ok && seen2 == pong,
			TEXT("сырой поток EAC сервер→клиент байт-в-байт"));
		ServerChannel sg; sg.Reset(); sg.SetIdentity(SubServer::Game, &priv); sg.SetAllowLegacy(false);
		std::vector<uint8_t> spg, ssg;
		R.Expect(sg.OnRecv(h14.data(), h14.size(), spg, ssg) != Err::Ok, TEXT("ClientHello EAC игровому серверу (роль Game) отвергнут"));
	}
	// 13. ★ EAC ред. 3 (CONTRACT.md §8.1): экспортёр привязки канала.
	{
		FRefCng Ref;
		const bool bRef = Ref.Open();
		R.Expect(bRef, TEXT("привязка: эталонные провайдеры CNG теста открыты"));
		// 13a. Вектор серверного исполнителя (bind_test_vector.txt) — независимым эталоном байт-в-байт.
		for (const FBindVec& V : kBindVec)
		{
			const std::vector<uint8_t> dC = Unhex(V.DC), pubC = Unhex(V.PubC), dS = Unhex(V.DS), pubS = Unhex(V.PubS);
			const std::vector<uint8_t> nC = Unhex(V.NonceC), nS = Unhex(V.NonceS), raw = Unhex(V.Raw), prk = Unhex(V.Prk);
			const std::vector<uint8_t> ib = Unhex(V.InfoBind), okm = Unhex(V.Okm), bind = Unhex(V.Bind);
			uint8_t ch[kClientHelloLen] = { kClientMagic, kVersion, V.Sub, 0 }; memcpy(ch + 4, pubC.data(), 64); memcpy(ch + 68, nC.data(), 16);
			uint8_t sh[kServerHelloLen] = { kServerMagic, kVersion, V.Sub, 0 }; memcpy(sh + 4, pubS.data(), 64); memcpy(sh + 68, nS.data(), 16);
			const TCHAR* Tag = (V.Sub == 2) ? TEXT("game") : TEXT("indun");
			BCRYPT_KEY_HANDLE kc = bRef ? Ref.ImportPriv(dC.data(), pubC.data()) : nullptr;
			BCRYPT_KEY_HANDLE ks = bRef ? Ref.ImportPriv(dS.data(), pubS.data()) : nullptr;
			uint8_t rawC[32] = {}, rawS[32] = {};
			const bool bEcdh = kc && ks && Ref.RawX(kc, pubS.data(), rawC) && Ref.RawX(ks, pubC.data(), rawS);
			R.Expect(bEcdh && memcmp(rawC, rawS, 32) == 0 && memcmp(rawC, raw.data(), 32) == 0,
				FString::Printf(TEXT("вектор %s: ECDH X одинаков у сторон и == raw вектора"), Tag));
			uint8_t P[32], O[88], Bd[32]; std::vector<uint8_t> InfoBind;
			const bool bDer = bEcdh && Ref.Derive(ch, sh, rawC, P, InfoBind, O, Bd);
			R.Expect(bDer && memcmp(P, prk.data(), 32) == 0 && InfoBind == ib, FString::Printf(TEXT("вектор %s: prk и info_bind == вектор"), Tag));
			R.Expect(bDer && memcmp(O, okm.data(), 88) == 0, FString::Printf(TEXT("вектор %s: ключи направлений (okm 88 б) == вектор — не изменились"), Tag));
			R.Expect(bDer && memcmp(Bd, bind.data(), 32) == 0, FString::Printf(TEXT("вектор %s: bind == вектор байт-в-байт"), Tag));
			if (kc) { BCryptDestroyKey(kc); }
			if (ks) { BCryptDestroyKey(ks); }
		}
		// 13b. Session-КЛИЕНТ (код AsdaSecure) против эталона: серверная сторона — фиксированный ключ вектора.
		for (const FBindVec& V : kBindVec)
		{
			const std::vector<uint8_t> dS = Unhex(V.DS), pubS = Unhex(V.PubS), nS = Unhex(V.NonceS);
			Session C; uint8_t ch[kClientHelloLen];
			const bool bHello = C.ClientHello((SubServer)V.Sub, pub, ch);
			uint8_t pre[kBindLen]; memset(pre, 0xEE, sizeof(pre));
			bool bUntouched = true; const bool bPre = C.ChannelBinding(pre); for (uint8_t X : pre) { bUntouched &= (X == 0xEE); }
			R.Expect(bHello && !bPre && bUntouched, TEXT("Session до ClientFinish: ChannelBinding = false, буфер не тронут"));
			uint8_t sh[kServerHelloLen] = { kServerMagic, kVersion, V.Sub, 0 }; memcpy(sh + 4, pubS.data(), 64); memcpy(sh + 68, nS.data(), 16);
			const bool bFin = C.ClientFinish(sh, sizeof(sh)) == Err::Ok;
			BCRYPT_KEY_HANDLE ks = bRef ? Ref.ImportPriv(dS.data(), pubS.data()) : nullptr;
			uint8_t raw[32] = {}, P[32], O[88], Bd[32]; std::vector<uint8_t> InfoBind;
			const bool bDer = ks && Ref.RawX(ks, ch + 4, raw) && Ref.Derive(ch, sh, raw, P, InfoBind, O, Bd);
			if (ks) { BCryptDestroyKey(ks); }
			uint8_t got[kBindLen] = {};
			R.Expect(bFin && bDer && C.ChannelBinding(got) && memcmp(got, Bd, 32) == 0,
				FString::Printf(TEXT("Session(клиент, sub %u).ChannelBinding == эталон"), (uint32)V.Sub));
			R.Expect(memcmp(got, O, 32) != 0 && memcmp(got, O + 44, 32) != 0, TEXT("bind не совпадает с ключами направлений"));
			// Ключи направлений клиента — те же, что по формуле до ред. 3: кадр клиента открывается эталонным c2sKey/c2sIv.
			std::vector<uint8_t> f = MakeFrame(0x4E21, 24, 0x5D), sealed, plain;
			R.Expect(C.Seal(f.data(), f.size(), sealed) == Err::Ok && Ref.OpenFrame(O, O + 32, sealed, plain) && plain == f,
				TEXT("ключ c2s клиента == эталон (кадр открыт эталонным ключом) — рукопожатие и ключи не менялись"));
			C.Reset();
			R.Expect(!C.ChannelBinding(got), TEXT("Session после Reset: ChannelBinding = false"));
		}
		// 13c. Session-СЕРВЕР против эталона: клиентская сторона — фиксированный ключ вектора.
		{
			const FBindVec& V = kBindVec[0];
			const std::vector<uint8_t> dC = Unhex(V.DC), pubC = Unhex(V.PubC), nC = Unhex(V.NonceC);
			uint8_t ch[kClientHelloLen] = { kClientMagic, kVersion, (uint8)SubServer::Game, 0 }; memcpy(ch + 4, pubC.data(), 64); memcpy(ch + 68, nC.data(), 16);
			Session S; uint8_t sh[kServerHelloLen];
			const bool bAcc = S.ServerAccept(ch, sizeof(ch), SubServer::Game, priv, sh) == Err::Ok;
			BCRYPT_KEY_HANDLE kc = bRef ? Ref.ImportPriv(dC.data(), pubC.data()) : nullptr;
			uint8_t raw[32] = {}, P[32], O[88], Bd[32]; std::vector<uint8_t> InfoBind;
			const bool bDer = kc && Ref.RawX(kc, sh + 4, raw) && Ref.Derive(ch, sh, raw, P, InfoBind, O, Bd);
			if (kc) { BCryptDestroyKey(kc); }
			uint8_t got[kBindLen] = {};
			R.Expect(bAcc && bDer && S.ChannelBinding(got) && memcmp(got, Bd, 32) == 0, TEXT("Session(сервер).ChannelBinding == эталон"));
			std::vector<uint8_t> f = MakeFrame(0x0FA5, 16, 0x11), sealed, plain;
			R.Expect(S.Seal(f.data(), f.size(), sealed) == Err::Ok && Ref.OpenFrame(O + 44, O + 76, sealed, plain) && plain == f,
				TEXT("ключ s2c сервера == эталон (кадр открыт эталонным ключом)"));
		}
		// 13d. Каналы: одно соединение — одинаковый bind у сторон; другое соединение — другой; Legacy / до готовности — false.
		{
			ServerChannel s1; s1.Reset(); s1.SetIdentity(SubServer::Game, &priv); s1.SetAllowLegacy(false);
			ClientChannel c1; c1.Reset(); std::vector<uint8_t> h1, p1, k1, d1;
			uint8_t b1[kBindLen], b2[kBindLen], b3[kBindLen], b4[kBindLen];
			c1.Start(SubServer::Game, pub, h1);
			R.Expect(!c1.ChannelBinding(b2) && !s1.ChannelBinding(b1), TEXT("каналы до рукопожатия: ChannelBinding = false"));
			s1.OnRecv(h1.data(), h1.size(), p1, k1);
			R.Expect(s1.ChannelBinding(b1) && !c1.ChannelBinding(b2), TEXT("сервер готов раньше клиента: у сервера bind есть, у клиента ещё нет"));
			c1.OnRecv(k1.data(), k1.size(), d1);
			R.Expect(c1.ChannelBinding(b2) && memcmp(b1, b2, kBindLen) == 0, TEXT("одно соединение: bind клиента == bind сервера"));
			ServerChannel s2; s2.Reset(); s2.SetIdentity(SubServer::Game, &priv);
			ClientChannel c2; c2.Reset(); std::vector<uint8_t> h2, p2, k2, d2;
			c2.Start(SubServer::Game, pub, h2); s2.OnRecv(h2.data(), h2.size(), p2, k2); c2.OnRecv(k2.data(), k2.size(), d2);
			R.Expect(c2.ChannelBinding(b3) && s2.ChannelBinding(b4) && memcmp(b3, b4, kBindLen) == 0 && memcmp(b1, b3, kBindLen) != 0,
				TEXT("другое соединение: bind другой (у его сторон — одинаковый)"));
			ClientChannel cl; cl.Reset(); cl.SetLegacy(true); std::vector<uint8_t> hl; cl.Start(SubServer::Game, pub, hl);
			ServerChannel sl; sl.Reset(); sl.SetIdentity(SubServer::Game, &priv); sl.SetAllowLegacy(true);
			std::vector<uint8_t> lp, ls; sl.OnRecv(old.data(), old.size(), lp, ls);
			R.Expect(!cl.ChannelBinding(b1) && !sl.ChannelBinding(b1) && sl.GetMode() == Mode::Legacy, TEXT("Legacy (клиент и сервер): ChannelBinding = false"));
			s1.Reset();
			R.Expect(!s1.ChannelBinding(b1), TEXT("ServerChannel после Reset: false"));
		}
	}
	// 12. Умолчание клиента для альфы: asda.Net.Secure = 1 (в Shipping — константа с тем же умолчанием).
	{
		IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Net.Secure"));
		R.Expect(V != nullptr && V->GetInt() == 1, TEXT("asda.Net.Secure по умолчанию = 1 (клиент шифрует)"));
		IConsoleVariable* T = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Net.SecureHandshakeTimeoutSec"));
		R.Expect(T != nullptr && T->GetFloat() > 0.f, TEXT("asda.Net.SecureHandshakeTimeoutSec задан (> 0 с) — без ответа сервера разрыв, не зависание"));
	}

	UE_LOG(LogAsdaTrafficSecure, Log, TEXT("ШИФР-ТРАФИК ИТОГ: ok=%d fail=%d — %s"), R.Ok, R.Fail,
		R.Fail == 0 ? TEXT("ВСЁ СОШЛОСЬ") : TEXT("ЕСТЬ ПРОВАЛЫ"));
	if (bQuit && GEngine) { GEngine->Exec(nullptr, TEXT("quit")); }
}

static FAutoConsoleCommand GAsdaSelfTestTrafficSecure(   // ★ В7c: в UE 5.8 класс с аргументами — FAutoConsoleCommand + FConsoleCommandWithArgsDelegate
	TEXT("asda.SelfTest.TrafficSecure"),
	TEXT("asda.SelfTest.TrafficSecure [quit] — офлайн-петля шифрования трафика (рукопожатие, AEAD обе стороны байт-в-байт, порча тега, Legacy). Сеть не нужна."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&AsdaTrafficSecure_Run));

#endif // PLATFORM_WINDOWS && !UE_BUILD_SHIPPING
