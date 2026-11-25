// Copyright (c) 2026 @sitxovski (Discord)
// Crypto.h — SHA-256 (Windows CNG) и проверка подписи Ed25519 (OpenSSL 1.1.1 из движка).
#pragma once
#include <array>
#include <string>

#include "Util.h"

namespace asda {

// SHA-256 (32 байта)
using Digest = std::array<uint8_t, 32>;

// Потоковый SHA-256 на CNG (BCrypt)
class Sha256 {
public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
    // Добавить данные
    void Update(const void* p, size_t n);
    // Итог (объект после этого не используется)
    Digest Final();

private:
    void* hash_ = nullptr;     // BCRYPT_HASH_HANDLE
};

// SHA-256 буфера
Digest Sha256Of(const void* p, size_t n);
// SHA-256 буфера, hex
std::string Sha256Hex(const void* p, size_t n);
// SHA-256 файла (широкий путь), hex; UpdateError при ошибке чтения
std::string Sha256FileHex(const std::wstring& path);
// Проверка Ed25519: открытый ключ 32 б, подпись 64 б
bool Ed25519Verify(const uint8_t pub[32], const uint8_t sig[64], const uint8_t* msg, size_t n);

}  // namespace asda
