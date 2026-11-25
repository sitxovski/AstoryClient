// Copyright (c) 2026 @sitxovski (Discord)
// Http.h — источники хранилища обновлений: HTTPS (WinHTTP) и локальная папка (только тестовая сборка).
#pragma once
#include <atomic>
#include <memory>
#include <string>

#include "Util.h"

namespace asda {

// Источник файлов хранилища (FORMAT_v1 §2): целиком с пределом и диапазоном байт
class Source {
public:
    virtual ~Source() = default;
    // Файл целиком; больше limit — UpdateError
    virtual Bytes Get(const std::string& rel, size_t limit) = 0;
    // Диапазон [start, start+len); ровно len байт или UpdateError
    virtual Bytes GetRange(const std::string& rel, uint64_t start, uint32_t len) = 0;
    // Скачано байт (для статистики/скорости)
    std::atomic<uint64_t> downloaded{0};
};

// HTTPS-источник: только https:// (http — лишь 127.0.0.1/localhost в тестовой сборке), без переадресаций,
// TLS 1.2+, Range-ответ строго 206 + Content-Range ровно на запрошенное (FORMAT_v1 §6 п. 12).
std::unique_ptr<Source> MakeHttpSource(const std::string& baseUrl);
// Локальная папка-хранилище (тесты)
std::unique_ptr<Source> MakeLocalSource(const std::wstring& root);
// По строке: URL (есть "://") → HTTPS, иначе папка
std::unique_ptr<Source> MakeSource(const std::string& s);

}  // namespace asda
