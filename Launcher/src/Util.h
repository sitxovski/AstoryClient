// Copyright (c) 2026 @sitxovski (Discord)
// Util.h — общие мелочи лаунчера: строки UTF-8/UTF-16, hex, base64, исключение обновления.
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace asda {

// Байтовый буфер
using Bytes = std::vector<uint8_t>;

// Отказ обновления (подпись, откат, порча, путь, сеть). Сообщение — UTF-8, по-русски; ключевые слова совпадают с
// эталонным клиентом (asda_update_client.py), их проверяет сквозной самотест.
struct UpdateError : std::runtime_error {
    explicit UpdateError(const std::string& msg) : std::runtime_error(msg) {}
};

// Отмена пользователем (не ошибка: состояние pending остаётся, игра не запускается до завершения)
struct Cancelled : std::runtime_error {
    Cancelled() : std::runtime_error("отменено") {}
};

// UTF-8 → UTF-16
std::wstring Widen(const std::string& s);
// UTF-16 → UTF-8
std::string Narrow(const std::wstring& s);
// Байты → строчный hex
std::string ToHex(const uint8_t* p, size_t n);
// Строгий hex (чётная длина, 0-9a-f/A-F) → байты; false при ошибке
bool FromHex(const std::string& s, Bytes& out);
// Строгий base64 (RFC 4648, с дополнением '=') → байты; false при ошибке
bool Base64Decode(const std::string& s, Bytes& out);
// Строка в нижнем регистре (ASCII)
std::string LowerAscii(std::string s);
// Форматирование printf → std::string
std::string Fmt(const char* fmt, ...);
// Текст последней ошибки Windows (UTF-8)
std::string LastErrorText(unsigned long code);

}  // namespace asda
