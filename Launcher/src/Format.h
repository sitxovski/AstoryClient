// Copyright (c) 2026 @sitxovski (Discord)
// Format.h — формат обновлений v1 (FORMAT_v1.md): пути, версии, конверты подписи, списки кусков, безопасный zstd.
// Порт Tools/asda_update_format.py; тексты ошибок совпадают с эталоном (их ключевые слова проверяет самотест).
#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <rapidjson/document.h>

#include "Crypto.h"
#include "Util.h"

namespace asda {

// Предел размера куска (исходного и хранимого), байт
constexpr uint32_t kPieceMax = 4u << 20;
// Предел длины пути манифеста
constexpr size_t kPathMax = 260;
// Служебная папка клиента и суффикс временного файла
constexpr const char* kStateDirName = ".asdaupd";
constexpr const char* kTmpSuffix = ".asdaupd-tmp";
// Кодеки хранения куска
constexpr uint8_t kCodecRaw = 0, kCodecZstd = 1;

// Версия «канал-X.Y.Z-ММ.ГГГГ»
struct Version {
    std::string channel;     // канал (alpha)
    int major = 0, minor = 0, patch = 0;
    int month = 0, year = 0; // справочно, порядок не задаёт
    // Ключ сравнения (семвер)
    bool operator<(const Version& o) const;
};
// Разбор версии; UpdateError при неверном формате
Version ParseVersion(const std::string& v);

// Проверка относительного пути манифеста (FORMAT_v1 §6 п. 8); UpdateError при нарушении
void CheckRelPath(const std::string& p);
// Строгий SHA-256 hex; UpdateError иначе
void CheckHex64(const std::string& h, const std::string& what);
// id ключа: первые 8 байт SHA-256 открытого ключа, hex
std::string KeyId(const uint8_t pub[32]);

// Распаковка zstd с защитой от бомб: размер кадра известен и равен expected (или ≤ limit) ДО распаковки
Bytes SafeZstd(const uint8_t* p, size_t n, std::optional<uint64_t> expected, std::optional<uint64_t> limit);

// Кусок из .plst
struct Piece {
    Digest hash{};           // SHA-256 исходных байт
    uint32_t size = 0;       // исходный размер
    uint32_t stored = 0;     // хранимый размер
    uint8_t codec = 0;       // 0 как есть, 1 zstd
    std::string pack;        // sha256 пачки, hex
    uint64_t offset = 0;     // смещение в пачке
};
// Разбор .plst (FORMAT_v1 §5, §6 п. 10); UpdateError при порче
std::vector<Piece> PlstUnpack(const Bytes& data);

// Доверенные ключи: id → открытый ключ
using TrustedKeys = std::map<std::string, std::array<uint8_t, 32>>;
// Проверка конверта вида kind ("keyring"/"channel"/"version") и разбор нагрузки (подпись ДО разбора)
rapidjson::Document EnvelopeOpen(const std::string& kind, const Bytes& envelopeJson, const TrustedKeys& trusted);
// Неотозванные ключи выпуска из нагрузки keyring
TrustedKeys KeyringTrusted(const rapidjson::Document& keyring);

// Поле-строка объекта JSON или пусто
std::string JStr(const rapidjson::Value& o, const char* name);
// Поле-целое (int64) объекта JSON; false, если нет или не целое
bool JInt(const rapidjson::Value& o, const char* name, int64_t& out);

}  // namespace asda
