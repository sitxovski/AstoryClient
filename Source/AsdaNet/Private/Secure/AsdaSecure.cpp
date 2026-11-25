// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSecure.cpp — реализация криптопрослойки на Windows CNG (bcrypt.dll). См. AsdaSecure.h.
//
// Соответствие решениям владельца:
//   • ECDH P-256 эфемерный на подключение (BCRYPT_ECDH_P256_ALGORITHM, BCryptSecretAgreement);
//   • ECDSA P-256 долговременный (BCRYPT_ECDSA_P256_ALGORITHM) — подпись сервера от посредника;
//   • HKDF-SHA256 (RFC 5869) над общим секретом → ключи c2s/s2c и базовые IV (вручную на HMAC-SHA256,
//     т.к. BCRYPT_HKDF_ALGORITHM есть не во всех сборках Windows — чтобы DLL шла и на старой ОС сервера);
//   • AES-256-GCM (BCRYPT_AES_ALGORITHM + BCRYPT_CHAIN_MODE_GCM), IV = ivBase XOR seq, seq растёт, anti-replay.
//
// Никаких OpenSSL/libsodium. Сборка /MT, только bcrypt.lib + ntdll (NTSTATUS).
// ★ КОПИЯ для клиента UE (волна В7b). Оригинал: AstoryServer/Patches/AsdaSecure/src/AsdaSecure.cpp.
//   Отличие от оригинала ТОЛЬКО в блоке include ниже: заголовки Windows обёрнуты UE-шимами
//   AllowWindowsPlatformTypes/HideWindowsPlatformTypes (иначе типы Windows конфликтуют с UE).
//   Логика крипто НЕ меняется. При правке крипто — править оригинал и пересинхронизировать копию.
#include "AsdaSecure.h"
#include <cstring>
#include <cstdio>
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"
#include <windows.h>
#include <bcrypt.h>
#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"
// bcrypt подключается через AsdaNet.Build.cs (PublicSystemLibraries на Win64).
// ★ В7c (сборка Editor): HideWindowsPlatformAtomics снимает макросы Interlocked* — возвращаем интринсик MSVC
//   с той же семантикой (полный барьер), чтобы тело Init() осталось дословно как в оригинале.
#ifndef InterlockedExchange
#define InterlockedExchange _InterlockedExchange
#endif
// ★ В7c: _wfopen в WritePublicHeader — CRT-предупреждение C4996 (в оригинале гасится /D_CRT_SECURE_NO_WARNINGS из build.bat;
//   в UE определение до PCH не действует). Только этот файл, только это предупреждение.
#ifdef _MSC_VER
#pragma warning(disable: 4996)
#endif

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef NT_SUCCESS
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

namespace AsdaSecure
{
    // ── Общие провайдеры (открываются один раз) ───────────────────────────────────────────
    namespace
    {
        BCRYPT_ALG_HANDLE g_hEcdh   = nullptr;   // ECDH P-256 (эфемерные ключи)
        BCRYPT_ALG_HANDLE g_hEcdsa  = nullptr;   // ECDSA P-256 (долговременные ключи)
        BCRYPT_ALG_HANDLE g_hAes    = nullptr;   // AES, режим GCM
        BCRYPT_ALG_HANDLE g_hSha256 = nullptr;   // HMAC-SHA256 (для HKDF)
        BCRYPT_ALG_HANDLE g_hRng    = nullptr;   // системный RNG
        volatile long     g_status  = 0;         // последний NTSTATUS-отказ
        volatile long     g_inited  = 0;         // 0 — не инициализировано, 1 — ок (InterlockedCompareExchange)

        inline void SetStatus(NTSTATUS s) { g_status = (long)s; }

        // «ASDAKEY1» — метка файла закрытого ключа.
        const uint8_t kKeyFileTag[8] = { 'A','S','D','A','K','E','Y','1' };

        // Собрать публичный BLOB CNG (BCRYPT_ECCPUBLIC_BLOB) из сырых X||Y для импорта.
        //   magic(4) cbKey(4) X(32) Y(32). magic зависит от алгоритма (ECDH vs ECDSA).
        bool MakePublicBlob(ULONG magic, const uint8_t xy[kPubLen], uint8_t out[8 + kPubLen])
        {
            ULONG cb = 32;
            memcpy(out + 0, &magic, 4);
            memcpy(out + 4, &cb, 4);
            memcpy(out + 8, xy, kPubLen);
            return true;
        }
        // Собрать приватный BLOB CNG (BCRYPT_ECCPRIVATE_BLOB): magic(4) cbKey(4) X(32) Y(32) d(32).
        bool MakePrivateBlob(ULONG magic, const PrivateKey& key, uint8_t out[8 + kPubLen + kPrivLen])
        {
            ULONG cb = 32;
            memcpy(out + 0, &magic, 4);
            memcpy(out + 4, &cb, 4);
            memcpy(out + 8, key.xy, kPubLen);
            memcpy(out + 8 + kPubLen, key.d, kPrivLen);
            return true;
        }

        // HMAC-SHA256(key,msg) → mac[32].
        bool HmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen, uint8_t mac[32])
        {
            BCRYPT_HASH_HANDLE h = nullptr;
            NTSTATUS s = BCryptCreateHash(g_hSha256, &h, nullptr, 0, (PUCHAR)key, (ULONG)keyLen, 0);
            if (!NT_SUCCESS(s)) { SetStatus(s); return false; }
            bool ok = NT_SUCCESS(s = BCryptHashData(h, (PUCHAR)msg, (ULONG)msgLen, 0))
                   && NT_SUCCESS(s = BCryptFinishHash(h, mac, 32, 0));
            if (!ok) SetStatus(s);
            BCryptDestroyHash(h);
            return ok;
        }

        // HKDF-SHA256 (RFC 5869): extract(salt, ikm) → prk; expand(prk, info, L) → okm.
        bool HkdfExtract(const uint8_t* salt, size_t saltLen, const uint8_t* ikm, size_t ikmLen, uint8_t prk[32])
        {
            uint8_t zero[32] = {0};
            if (!salt || saltLen == 0) { salt = zero; saltLen = 32; }
            return HmacSha256(salt, saltLen, ikm, ikmLen, prk);
        }
        bool HkdfExpand(const uint8_t prk[32], const uint8_t* info, size_t infoLen, uint8_t* okm, size_t okmLen)
        {
            uint8_t t[32]; size_t tLen = 0; uint8_t counter = 1; size_t done = 0;
            while (done < okmLen)
            {
                // T(n) = HMAC(prk, T(n-1) | info | counter)
                uint8_t buf[32 + 256 + 1];
                if (infoLen > 256) return false;
                size_t off = 0;
                memcpy(buf + off, t, tLen); off += tLen;
                memcpy(buf + off, info, infoLen); off += infoLen;
                buf[off++] = counter;
                if (!HmacSha256(prk, 32, buf, off, t)) return false;
                tLen = 32;
                size_t take = (okmLen - done < 32) ? (okmLen - done) : 32;
                memcpy(okm + done, t, take);
                done += take; ++counter;
            }
            return true;
        }
    }

    const char* ErrName(Err e)
    {
        switch (e)
        {
        case Err::Ok:           return "Ok";
        case Err::Arg:          return "Arg";
        case Err::Cng:          return "Cng";
        case Err::BadMagic:     return "BadMagic";
        case Err::BadVersion:   return "BadVersion";
        case Err::BadSubServer: return "BadSubServer";
        case Err::BadSignature: return "BadSignature";
        case Err::BadTag:       return "BadTag";
        case Err::Replay:       return "Replay";
        case Err::Format:       return "Format";
        case Err::State:        return "State";
        case Err::Legacy:       return "Legacy";
        }
        return "?";
    }

    long LastStatus() { return g_status; }
    void Wipe(void* p, size_t n) { if (p && n) SecureZeroMemory(p, n); }

    bool Init()
    {
        if (g_inited) return true;
        NTSTATUS s;
        if (!NT_SUCCESS(s = BCryptOpenAlgorithmProvider(&g_hEcdh,   BCRYPT_ECDH_P256_ALGORITHM,  nullptr, 0))) { SetStatus(s); return false; }
        if (!NT_SUCCESS(s = BCryptOpenAlgorithmProvider(&g_hEcdsa,  BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0))) { SetStatus(s); return false; }
        if (!NT_SUCCESS(s = BCryptOpenAlgorithmProvider(&g_hAes,    BCRYPT_AES_ALGORITHM,        nullptr, 0))) { SetStatus(s); return false; }
        if (!NT_SUCCESS(s = BCryptSetProperty(g_hAes, BCRYPT_CHAINING_MODE,
                             (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0)))             { SetStatus(s); return false; }
        if (!NT_SUCCESS(s = BCryptOpenAlgorithmProvider(&g_hSha256, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG))) { SetStatus(s); return false; }
        if (!NT_SUCCESS(s = BCryptOpenAlgorithmProvider(&g_hRng,    BCRYPT_RNG_ALGORITHM,        nullptr, 0))) { SetStatus(s); return false; }
        InterlockedExchange(&g_inited, 1);
        return true;
    }

    bool RandomBytes(void* out, size_t len)
    {
        if (!Init()) return false;
        NTSTATUS s = BCryptGenRandom(g_hRng, (PUCHAR)out, (ULONG)len, 0);
        if (!NT_SUCCESS(s)) { SetStatus(s); return false; }
        return true;
    }

    bool GenerateLongTermKey(PrivateKey& out)
    {
        if (!Init()) return false;
        BCRYPT_KEY_HANDLE k = nullptr; NTSTATUS s;
        if (!NT_SUCCESS(s = BCryptGenerateKeyPair(g_hEcdsa, &k, 256, 0)) ||
            !NT_SUCCESS(s = BCryptFinalizeKeyPair(k, 0))) { SetStatus(s); if (k) BCryptDestroyKey(k); return false; }
        uint8_t blob[8 + kPubLen + kPrivLen]; ULONG cb = 0;
        s = BCryptExportKey(k, nullptr, BCRYPT_ECCPRIVATE_BLOB, blob, sizeof(blob), &cb, 0);
        BCryptDestroyKey(k);
        if (!NT_SUCCESS(s) || cb != sizeof(blob)) { SetStatus(s); return false; }
        memcpy(out.xy, blob + 8, kPubLen);
        memcpy(out.d,  blob + 8 + kPubLen, kPrivLen);
        Wipe(blob, sizeof(blob));
        return true;
    }

    bool SignData(const PrivateKey& key, const uint8_t* data, size_t len, uint8_t sig[kSigLen])
    {
        if (!Init()) return false;
        uint8_t hash[32];
        { BCRYPT_HASH_HANDLE h = nullptr; BCRYPT_ALG_HANDLE a = nullptr; NTSTATUS s;
          if (!NT_SUCCESS(s = BCryptOpenAlgorithmProvider(&a, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) { SetStatus(s); return false; }
          bool ok = NT_SUCCESS(s = BCryptCreateHash(a, &h, nullptr, 0, nullptr, 0, 0))
                 && NT_SUCCESS(s = BCryptHashData(h, (PUCHAR)data, (ULONG)len, 0))
                 && NT_SUCCESS(s = BCryptFinishHash(h, hash, 32, 0));
          if (h) BCryptDestroyHash(h); BCryptCloseAlgorithmProvider(a, 0);
          if (!ok) { SetStatus(s); return false; } }
        uint8_t blob[8 + kPubLen + kPrivLen]; MakePrivateBlob(BCRYPT_ECDSA_PRIVATE_P256_MAGIC, key, blob);
        BCRYPT_KEY_HANDLE k = nullptr; NTSTATUS s;
        s = BCryptImportKeyPair(g_hEcdsa, nullptr, BCRYPT_ECCPRIVATE_BLOB, &k, blob, sizeof(blob), 0);
        Wipe(blob, sizeof(blob));
        if (!NT_SUCCESS(s)) { SetStatus(s); return false; }
        ULONG cb = 0;
        s = BCryptSignHash(k, nullptr, hash, 32, sig, kSigLen, &cb, 0);
        BCryptDestroyKey(k);
        if (!NT_SUCCESS(s) || cb != kSigLen) { SetStatus(s); return false; }
        return true;
    }

    bool VerifyData(const PublicKey& pub, const uint8_t* data, size_t len, const uint8_t sig[kSigLen])
    {
        if (!Init()) return false;
        uint8_t hash[32];
        { BCRYPT_HASH_HANDLE h = nullptr; BCRYPT_ALG_HANDLE a = nullptr; NTSTATUS s;
          if (!NT_SUCCESS(s = BCryptOpenAlgorithmProvider(&a, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) { SetStatus(s); return false; }
          bool ok = NT_SUCCESS(s = BCryptCreateHash(a, &h, nullptr, 0, nullptr, 0, 0))
                 && NT_SUCCESS(s = BCryptHashData(h, (PUCHAR)data, (ULONG)len, 0))
                 && NT_SUCCESS(s = BCryptFinishHash(h, hash, 32, 0));
          if (h) BCryptDestroyHash(h); BCryptCloseAlgorithmProvider(a, 0);
          if (!ok) { SetStatus(s); return false; } }
        uint8_t blob[8 + kPubLen]; MakePublicBlob(BCRYPT_ECDSA_PUBLIC_P256_MAGIC, pub.xy, blob);
        BCRYPT_KEY_HANDLE k = nullptr; NTSTATUS s;
        if (!NT_SUCCESS(s = BCryptImportKeyPair(g_hEcdsa, nullptr, BCRYPT_ECCPUBLIC_BLOB, &k, blob, sizeof(blob), 0))) { SetStatus(s); return false; }
        s = BCryptVerifySignature(k, nullptr, hash, 32, (PUCHAR)sig, kSigLen, 0);
        BCryptDestroyKey(k);
        return NT_SUCCESS(s);    // STATUS_INVALID_SIGNATURE → false (это не ошибка CNG, а честный отказ)
    }

    bool SavePrivateKeyFile(const wchar_t* path, const PrivateKey& key)
    {
        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        DWORD wr = 0; bool ok = true;
        ok = ok && WriteFile(f, kKeyFileTag, 8, &wr, nullptr) && wr == 8;
        ok = ok && WriteFile(f, key.d,  kPrivLen, &wr, nullptr) && wr == kPrivLen;
        ok = ok && WriteFile(f, key.xy, kPubLen,  &wr, nullptr) && wr == kPubLen;
        CloseHandle(f);
        return ok;
    }

    bool LoadPrivateKeyFile(const wchar_t* path, PrivateKey& out)
    {
        HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        uint8_t buf[8 + kPrivLen + kPubLen]; DWORD rd = 0;
        bool ok = ReadFile(f, buf, sizeof(buf), &rd, nullptr) && rd == sizeof(buf);
        CloseHandle(f);
        if (!ok || memcmp(buf, kKeyFileTag, 8) != 0) { Wipe(buf, sizeof(buf)); return false; }
        memcpy(out.d,  buf + 8, kPrivLen);
        memcpy(out.xy, buf + 8 + kPrivLen, kPubLen);
        Wipe(buf, sizeof(buf));
        return true;
    }

    bool WritePublicHeader(const wchar_t* path, const PublicKey& pub, const char* symbol)
    {
        FILE* f = _wfopen(path, L"wb");
        if (!f) return false;
        fprintf(f, "// server_public.h — долговременный ОТКРЫТЫЙ ключ сервера (ECDSA P-256, X||Y).\n");
        fprintf(f, "// Сгенерирован AsdaSecure keygen. Вшивается в клиент; проверяет подпись ServerHello.\n");
        fprintf(f, "// Закрытый ключ — только в файле рядом с серверной DLL, в исходники не попадает.\n");
        fprintf(f, "#pragma once\n#include <cstdint>\nstatic const uint8_t %s[64] = {\n    ", symbol ? symbol : "kAsdaServerPublicKey");
        for (int i = 0; i < 64; ++i) { fprintf(f, "0x%02X%s", pub.xy[i], (i == 63) ? "" : ((i % 12 == 11) ? ",\n    " : ", ")); }
        fprintf(f, "\n};\n");
        fclose(f);
        return true;
    }

    // ── Session ────────────────────────────────────────────────────────────────────────────
    struct Session::Impl
    {
        BCRYPT_KEY_HANDLE eph  = nullptr;   // эфемерный ECDH-ключ этой стороны (до вывода ключей)
        BCRYPT_KEY_HANDLE tx   = nullptr;   // AES-GCM ключ исходящего направления
        BCRYPT_KEY_HANDLE rx   = nullptr;   // AES-GCM ключ входящего направления
        uint8_t  myPub[kPubLen]  = {0};     // свой эфемерный открытый
        uint8_t  myNonce[kNonceLen] = {0};
        uint8_t  txIv[kIvLen]    = {0};     // базовый IV исходящего (XOR seq даёт IV кадра)
        uint8_t  rxIv[kIvLen]    = {0};
        uint8_t  txKey[kKeyLen]  = {0};     // держим сырьё ключа только для импорта (стенд может сверить)
        uint8_t  rxKey[kKeyLen]  = {0};
        uint8_t  clientHello[kClientHelloLen] = {0};  // своё ClientHello (клиенту нужно в ClientFinish для KDF)
        uint8_t  bind[kBindLen]  = {0};     // ★ EAC ред. 3: привязка канала HKDF-Expand(prk, info_bind, 32) (ChannelBinding)
        bool     bindReady = false;         // ★ EAC ред. 3: bind выведен в этом рукопожатии
    };

    Session::Session()
        : m_impl(new Impl()), m_ready(false), m_lastErr(Err::Ok),
          m_txSeq(0), m_rxSeq(0), m_inOff(0) { Init(); }

    Session::~Session() { Reset(); delete m_impl; }

    void Session::Reset()
    {
        if (m_impl)
        {
            if (m_impl->eph) BCryptDestroyKey(m_impl->eph);
            if (m_impl->tx)  BCryptDestroyKey(m_impl->tx);
            if (m_impl->rx)  BCryptDestroyKey(m_impl->rx);
            Wipe(m_impl->txKey, kKeyLen); Wipe(m_impl->rxKey, kKeyLen);
            Wipe(m_impl->txIv, kIvLen);   Wipe(m_impl->rxIv, kIvLen);
            Wipe(m_impl->bind, kBindLen); m_impl->bindReady = false;   // ★ EAC ред. 3
            *m_impl = Impl();
        }
        m_ready = false; m_lastErr = Err::Ok;
        m_txSeq = 0; m_rxSeq = 0; m_inOff = 0;
        m_in.clear();
    }

    // Общий секрет ECDH → HKDF → два ключа + два IV. Порядок меток фиксирует направления так,
    // что у клиента tx = c2s, у сервера tx = s2c (симметрично, без путаницы направлений).
    bool Session::DeriveKeys(const uint8_t* clientHello, const uint8_t* serverHello, bool isClient)
    {
        // Импортировать эфемерный открытый ключ ДРУГОЙ стороны из его приветствия.
        const uint8_t* peerPub = isClient ? (serverHello + 4) : (clientHello + 4);
        uint8_t blob[8 + kPubLen]; MakePublicBlob(BCRYPT_ECDH_PUBLIC_P256_MAGIC, peerPub, blob);
        BCRYPT_KEY_HANDLE peer = nullptr; NTSTATUS s;
        if (!NT_SUCCESS(s = BCryptImportKeyPair(g_hEcdh, nullptr, BCRYPT_ECCPUBLIC_BLOB, &peer, blob, sizeof(blob), 0))) { SetStatus(s); return false; }

        BCRYPT_SECRET_HANDLE secret = nullptr;
        s = BCryptSecretAgreement(m_impl->eph, peer, &secret, 0);
        BCryptDestroyKey(peer);
        if (!NT_SUCCESS(s)) { SetStatus(s); return false; }

        // Сырой общий X берём через BCRYPT_KDF_RAW_SECRET (возвращает little-endian — нормализуем).
        ULONG cb = 0;
        BCryptDeriveKey(secret, BCRYPT_KDF_RAW_SECRET, nullptr, nullptr, 0, &cb, 0);
        uint8_t raw[48] = {0};
        if (cb == 0 || cb > sizeof(raw)) { BCryptDestroySecret(secret); SetStatus((NTSTATUS)-1); return false; }
        s = BCryptDeriveKey(secret, BCRYPT_KDF_RAW_SECRET, nullptr, raw, cb, &cb, 0);
        BCryptDestroySecret(secret);
        if (!NT_SUCCESS(s)) { SetStatus(s); return false; }
        for (ULONG i = 0; i < cb / 2; ++i) { uint8_t t = raw[i]; raw[i] = raw[cb-1-i]; raw[cb-1-i] = t; }  // LE→BE

        // Соль HKDF = оба nonce (клиент||сервер); info несёт версию, под-сервер и оба открытых ключа —
        // так ключ привязан к конкретному рукопожатию и под-серверу.
        uint8_t salt[2 * kNonceLen];
        memcpy(salt,             clientHello + 4 + kPubLen, kNonceLen);
        memcpy(salt + kNonceLen, serverHello + 4 + kPubLen, kNonceLen);
        uint8_t info[3 + 2 * kPubLen + 16];
        size_t io = 0;
        info[io++] = kVersion;
        info[io++] = clientHello[2];                 // под-сервер из ClientHello
        info[io++] = serverHello[2];
        memcpy(info + io, clientHello + 4, kPubLen); io += kPubLen;
        memcpy(info + io, serverHello + 4, kPubLen); io += kPubLen;
        memcpy(info + io, "AsdaSecure/v1\0\0", 16);   io += 16;

        uint8_t prk[32];
        if (!HkdfExtract(salt, sizeof(salt), raw, cb, prk)) { Wipe(raw, sizeof(raw)); return false; }
        Wipe(raw, sizeof(raw));
        // 2*(32 ключ + 12 IV) = 88 байт выходного материала; первая половина — c2s, вторая — s2c.
        uint8_t okm[2 * (kKeyLen + kIvLen)];
        if (!HkdfExpand(prk, info, sizeof(info), okm, sizeof(okm))) { Wipe(prk, 32); return false; }
        // ★ EAC ред. 3 (контракт §8.1): привязка канала — из того же prk ДО его затирания; info_bind = info с меткой
        //   "AsdaEacBind/v1\0\0" (14 символов + 2 нуля) вместо "AsdaSecure/v1\0\0\0". Ключи и рукопожатие от этого не зависят:
        //   отказ HMAC здесь не рвёт рукопожатие — ChannelBinding просто вернёт false.
        {
            uint8_t infoBind[sizeof(info)];
            memcpy(infoBind, info, sizeof(info));
            memcpy(infoBind + sizeof(info) - 16, "AsdaEacBind/v1\0\0", 16);
            m_impl->bindReady = HkdfExpand(prk, infoBind, sizeof(infoBind), m_impl->bind, kBindLen);
            if (!m_impl->bindReady) Wipe(m_impl->bind, kBindLen);
        }
        Wipe(prk, 32);

        const uint8_t* c2sKey = okm;
        const uint8_t* c2sIv  = okm + kKeyLen;
        const uint8_t* s2cKey = okm + kKeyLen + kIvLen;
        const uint8_t* s2cIv  = okm + 2 * kKeyLen + kIvLen;
        const uint8_t* txKey = isClient ? c2sKey : s2cKey;
        const uint8_t* txIv  = isClient ? c2sIv  : s2cIv;
        const uint8_t* rxKey = isClient ? s2cKey : c2sKey;
        const uint8_t* rxIv  = isClient ? s2cIv  : c2sIv;
        memcpy(m_impl->txKey, txKey, kKeyLen); memcpy(m_impl->txIv, txIv, kIvLen);
        memcpy(m_impl->rxKey, rxKey, kKeyLen); memcpy(m_impl->rxIv, rxIv, kIvLen);
        Wipe(okm, sizeof(okm));

        NTSTATUS s1 = BCryptGenerateSymmetricKey(g_hAes, &m_impl->tx, nullptr, 0, m_impl->txKey, kKeyLen, 0);
        NTSTATUS s2 = BCryptGenerateSymmetricKey(g_hAes, &m_impl->rx, nullptr, 0, m_impl->rxKey, kKeyLen, 0);
        if (!NT_SUCCESS(s1) || !NT_SUCCESS(s2)) { SetStatus(NT_SUCCESS(s1) ? s2 : s1); return false; }

        if (m_impl->eph) { BCryptDestroyKey(m_impl->eph); m_impl->eph = nullptr; }
        m_ready = true;
        return true;
    }

    bool Session::ClientHello(SubServer sub, const PublicKey& serverPub, uint8_t out[kClientHelloLen])
    {
        (void)serverPub;   // открытый ключ сервера нужен ClientFinish; тут только эфемерная пара
        if (!Init()) { m_lastErr = Err::Cng; return false; }
        Reset();
        NTSTATUS s;
        if (!NT_SUCCESS(s = BCryptGenerateKeyPair(g_hEcdh, &m_impl->eph, 256, 0)) ||
            !NT_SUCCESS(s = BCryptFinalizeKeyPair(m_impl->eph, 0))) { SetStatus(s); m_lastErr = Err::Cng; return false; }
        uint8_t blob[8 + kPubLen]; ULONG cb = 0;
        if (!NT_SUCCESS(s = BCryptExportKey(m_impl->eph, nullptr, BCRYPT_ECCPUBLIC_BLOB, blob, sizeof(blob), &cb, 0)) || cb != sizeof(blob))
            { SetStatus(s); m_lastErr = Err::Cng; return false; }
        memcpy(m_impl->myPub, blob + 8, kPubLen);
        if (!RandomBytes(m_impl->myNonce, kNonceLen)) { m_lastErr = Err::Cng; return false; }
        out[0] = kClientMagic; out[1] = kVersion; out[2] = (uint8_t)sub; out[3] = 0;
        memcpy(out + 4, m_impl->myPub, kPubLen);
        memcpy(out + 4 + kPubLen, m_impl->myNonce, kNonceLen);
        memcpy(m_impl->clientHello, out, kClientHelloLen);   // понадобится в ClientFinish для KDF
        m_lastErr = Err::Ok;
        return true;
    }

    Err Session::ServerAccept(const uint8_t* hello, size_t len, SubServer sub, const PrivateKey& serverKey,
                              uint8_t out[kServerHelloLen])
    {
        if (!Init()) return m_lastErr = Err::Cng;
        if (!hello || len != kClientHelloLen) return m_lastErr = Err::Arg;
        if (hello[0] != kClientMagic)         return m_lastErr = Err::BadMagic;
        if (hello[1] != kVersion)             return m_lastErr = Err::BadVersion;
        if (hello[2] != (uint8_t)sub)         return m_lastErr = Err::BadSubServer;
        Reset();
        NTSTATUS s;
        if (!NT_SUCCESS(s = BCryptGenerateKeyPair(g_hEcdh, &m_impl->eph, 256, 0)) ||
            !NT_SUCCESS(s = BCryptFinalizeKeyPair(m_impl->eph, 0))) { SetStatus(s); return m_lastErr = Err::Cng; }
        uint8_t blob[8 + kPubLen]; ULONG cb = 0;
        if (!NT_SUCCESS(s = BCryptExportKey(m_impl->eph, nullptr, BCRYPT_ECCPUBLIC_BLOB, blob, sizeof(blob), &cb, 0)))
            { SetStatus(s); return m_lastErr = Err::Cng; }
        memcpy(m_impl->myPub, blob + 8, kPubLen);
        if (!RandomBytes(m_impl->myNonce, kNonceLen)) return m_lastErr = Err::Cng;

        out[0] = kServerMagic; out[1] = kVersion; out[2] = (uint8_t)sub; out[3] = 0;
        memcpy(out + 4, m_impl->myPub, kPubLen);
        memcpy(out + 4 + kPubLen, m_impl->myNonce, kNonceLen);
        // Подпись над (клиентский ePub || серверный ePub || под-сервер) — привязка сервера к рукопожатию.
        uint8_t signed_[kPubLen + kPubLen + 1];
        memcpy(signed_, hello + 4, kPubLen);
        memcpy(signed_ + kPubLen, m_impl->myPub, kPubLen);
        signed_[2 * kPubLen] = (uint8_t)sub;
        if (!SignData(serverKey, signed_, sizeof(signed_), out + 4 + kPubLen + kNonceLen)) return m_lastErr = Err::Cng;

        if (!DeriveKeys(hello, out, /*isClient=*/false)) return m_lastErr = Err::Cng;
        return m_lastErr = Err::Ok;
    }

    bool Session::ChannelBinding(uint8_t out[kBindLen]) const
    {
        if (!out || !m_ready || !m_impl || !m_impl->bindReady) return false;
        memcpy(out, m_impl->bind, kBindLen);
        return true;
    }

    Err Session::ClientFinish(const uint8_t* hello, size_t len)
    {
        if (!hello || len != kServerHelloLen) return m_lastErr = Err::Arg;
        if (hello[0] != kServerMagic)         return m_lastErr = Err::BadMagic;
        if (hello[1] != kVersion)             return m_lastErr = Err::BadVersion;
        // Подпись ServerHello проверяет ClientChannel (у него вшит открытый ключ сервера) ДО вызова
        // ClientFinish; сюда мы попадаем только с уже проверенным приветствием. Здесь — вывод ключей
        // из нашего сохранённого ClientHello и принятого ServerHello.
        if (!DeriveKeys(m_impl->clientHello, hello, /*isClient=*/true)) return m_lastErr = Err::Cng;
        return m_lastErr = Err::Ok;
    }

    // IV кадра = базовый IV XOR big-endian(seq) в младшие 8 байт.
    static void MakeIv(const uint8_t base[kIvLen], uint64_t seq, uint8_t iv[kIvLen])
    {
        memcpy(iv, base, kIvLen);
        for (int i = 0; i < 8; ++i) iv[kIvLen - 1 - i] ^= (uint8_t)(seq >> (8 * i));
    }

    Err Session::Seal(const uint8_t* plain, size_t len, std::vector<uint8_t>& out)
    {
        if (!m_ready) return m_lastErr = Err::State;
        if (len > kMaxPlain) return m_lastErr = Err::Arg;
        uint64_t seq = ++m_txSeq;
        uint8_t iv[kIvLen]; MakeIv(m_impl->txIv, seq, iv);

        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        uint8_t tag[kTagLen];
        uint8_t aad[kFrameHdrLen];
        uint32_t clen = (uint32_t)(len + kTagLen);
        memcpy(aad, &clen, 4); memcpy(aad + 4, &seq, 8);   // AAD = len||seq (защищает заголовок кадра)
        info.pbNonce = iv; info.cbNonce = kIvLen;
        info.pbTag = tag;  info.cbTag = kTagLen;
        info.pbAuthData = aad; info.cbAuthData = kFrameHdrLen;

        size_t base = out.size();
        out.resize(base + kFrameHdrLen + len + kTagLen);
        uint8_t* p = out.data() + base;
        memcpy(p, &clen, 4); memcpy(p + 4, &seq, 8);
        ULONG done = 0;
        NTSTATUS s = BCryptEncrypt(m_impl->tx, (PUCHAR)plain, (ULONG)len, &info, nullptr, 0,
                                   p + kFrameHdrLen, (ULONG)len, &done, 0);
        if (!NT_SUCCESS(s)) { out.resize(base); SetStatus(s); return m_lastErr = Err::Cng; }
        memcpy(p + kFrameHdrLen + len, tag, kTagLen);
        return m_lastErr = Err::Ok;
    }

    Err Session::Feed(const uint8_t* data, size_t len)
    {
        if (!data || len == 0) return m_lastErr = Err::Ok;
        m_in.insert(m_in.end(), data, data + len);
        return m_lastErr = Err::Ok;
    }

    bool Session::Open(const uint8_t* frame, size_t frameLen, std::vector<uint8_t>& out)
    {
        uint32_t clen; uint64_t seq;
        memcpy(&clen, frame, 4); memcpy(&seq, frame + 4, 8);
        if (clen < kTagLen || clen > kMaxPlain + kTagLen) { m_lastErr = Err::Format; return false; }
        if (frameLen < kFrameHdrLen + clen)               { m_lastErr = Err::Format; return false; }
        // Anti-replay: seq обязан быть ровно «следующим». TCP упорядочен — всё иное есть атака/сбой.
        if (seq != m_rxSeq + 1) { m_lastErr = Err::Replay; return false; }
        size_t plainLen = clen - kTagLen;
        const uint8_t* ct  = frame + kFrameHdrLen;
        const uint8_t* tag = ct + plainLen;
        uint8_t iv[kIvLen]; MakeIv(m_impl->rxIv, seq, iv);

        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        uint8_t aad[kFrameHdrLen]; memcpy(aad, frame, kFrameHdrLen);
        info.pbNonce = iv; info.cbNonce = kIvLen;
        info.pbTag = (PUCHAR)tag; info.cbTag = kTagLen;
        info.pbAuthData = aad; info.cbAuthData = kFrameHdrLen;

        size_t base = out.size();
        out.resize(base + plainLen);
        ULONG done = 0;
        NTSTATUS s = BCryptDecrypt(m_impl->rx, (PUCHAR)ct, (ULONG)plainLen, &info, nullptr, 0,
                                   out.data() + base, (ULONG)plainLen, &done, 0);
        if (!NT_SUCCESS(s)) { out.resize(base); m_lastErr = Err::BadTag; return false; }   // STATUS_AUTH_TAG_MISMATCH
        m_rxSeq = seq;
        m_lastErr = Err::Ok;
        return true;
    }

    bool Session::Next(std::vector<uint8_t>& out)
    {
        out.clear();
        if (!m_ready) { m_lastErr = Err::State; return false; }
        size_t avail = m_in.size() - m_inOff;
        if (avail < kFrameHdrLen) return false;             // заголовок ещё не весь
        uint32_t clen; memcpy(&clen, m_in.data() + m_inOff, 4);
        if (clen < kTagLen || clen > kMaxPlain + kTagLen) { m_lastErr = Err::Format; return false; }
        if (avail < kFrameHdrLen + clen) return false;      // тело ещё не всё
        bool ok = Open(m_in.data() + m_inOff, kFrameHdrLen + clen, out);
        if (!ok) return false;
        m_inOff += kFrameHdrLen + clen;
        // Сжать накопитель, когда разобранный префикс велик.
        if (m_inOff > 65536 || m_inOff == m_in.size())
        {
            m_in.erase(m_in.begin(), m_in.begin() + m_inOff);
            m_inOff = 0;
        }
        return true;
    }
}
