// Copyright (c) 2026 @sitxovski (Discord)
// Crypto.cpp — SHA-256 на CNG, Ed25519 на OpenSSL EVP (см. Crypto.h).
#include "Crypto.h"

#include <windows.h>
#include <bcrypt.h>
#include <openssl/evp.h>

#include <mutex>

namespace asda {

// Провайдер SHA-256 CNG: открывается один раз (потокобезопасно)
static BCRYPT_ALG_HANDLE ShaAlg() {
    static BCRYPT_ALG_HANDLE alg = nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) alg = nullptr;
    });
    if (!alg) throw UpdateError("CNG: SHA-256 недоступен");
    return alg;
}

Sha256::Sha256() {
    BCRYPT_HASH_HANDLE h = nullptr;
    if (BCryptCreateHash(ShaAlg(), &h, nullptr, 0, nullptr, 0, 0) != 0) throw UpdateError("CNG: не создан хэш");
    hash_ = h;
}

Sha256::~Sha256() {
    if (hash_) BCryptDestroyHash((BCRYPT_HASH_HANDLE)hash_);
}

void Sha256::Update(const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    while (n) {                                   // BCryptHashData принимает ULONG
        ULONG part = (ULONG)(n > 0x40000000 ? 0x40000000 : n);
        BCryptHashData((BCRYPT_HASH_HANDLE)hash_, (PUCHAR)b, part, 0);
        b += part;
        n -= part;
    }
}

Digest Sha256::Final() {
    Digest d{};
    BCryptFinishHash((BCRYPT_HASH_HANDLE)hash_, d.data(), (ULONG)d.size(), 0);
    return d;
}

Digest Sha256Of(const void* p, size_t n) {
    Sha256 h;
    h.Update(p, n);
    return h.Final();
}

std::string Sha256Hex(const void* p, size_t n) {
    Digest d = Sha256Of(p, n);
    return ToHex(d.data(), d.size());
}

std::string Sha256FileHex(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) throw UpdateError("не открыт файл для хэша: " + LastErrorText(GetLastError()));
    Sha256 h;
    Bytes buf(4 << 20);
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(f, buf.data(), (DWORD)buf.size(), &got, nullptr)) {
            DWORD e = GetLastError();
            CloseHandle(f);
            throw UpdateError("ошибка чтения файла: " + LastErrorText(e));
        }
        if (!got) break;
        h.Update(buf.data(), got);
    }
    CloseHandle(f);
    Digest d = h.Final();
    return ToHex(d.data(), d.size());
}

bool Ed25519Verify(const uint8_t pub[32], const uint8_t sig[64], const uint8_t* msg, size_t n) {
    EVP_PKEY* key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, pub, 32);
    if (!key) return false;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    bool ok = ctx && EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key) == 1 &&
              EVP_DigestVerify(ctx, sig, 64, msg, n) == 1;
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    return ok;
}

}  // namespace asda
