// Copyright (c) 2026 @sitxovski (Discord)
// Format.cpp — реализация формата v1 (см. Format.h). Эталон — Tools/asda_update_format.py.
#include "Format.h"

#include <cstring>

#include <rapidjson/error/en.h>

#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

namespace asda {

// Префикс подписываемого сообщения (разделение доменов)
static const char kSigPrefix[] = "ASDA-UPD-v1";

bool Version::operator<(const Version& o) const {
    if (major != o.major) return major < o.major;
    if (minor != o.minor) return minor < o.minor;
    return patch < o.patch;
}

// Разбор десятичного числа из [p, e); false, если не цифры или пусто
static bool ParseUInt(const char*& p, const char* e, int maxDigits, int& out) {
    const char* s = p;
    long long v = 0;
    while (p < e && *p >= '0' && *p <= '9' && p - s < maxDigits) v = v * 10 + (*p++ - '0');
    if (p == s || v > 0x7fffffff) return false;
    out = (int)v;
    return true;
}

Version ParseVersion(const std::string& v) {
    Version r;
    const char* p = v.c_str();
    const char* e = p + v.size();
    const char* s = p;
    while (p < e && *p >= 'a' && *p <= 'z') ++p;
    bool ok = p > s && p < e && *p == '-';
    if (ok) {
        r.channel.assign(s, p);
        ++p;
        ok = ParseUInt(p, e, 9, r.major) && p < e && *p++ == '.' && ParseUInt(p, e, 9, r.minor) && p < e && *p++ == '.' &&
             ParseUInt(p, e, 9, r.patch) && p < e && *p++ == '-';
        const char* m0 = p;
        ok = ok && ParseUInt(p, e, 2, r.month) && p < e && *p++ == '.';
        (void)m0;
        const char* y0 = p;
        ok = ok && ParseUInt(p, e, 4, r.year) && p - y0 == 4 && p == e;
    }
    if (!ok) throw UpdateError("версия «" + v + "» не в формате канал-X.Y.Z-ММ.ГГГГ");
    if (r.month < 1 || r.month > 12) throw UpdateError(Fmt("месяц версии %d вне 1..12", r.month));
    return r;
}

// Символ сегмента из белого списка A-Za-z0-9._+()- и пробел
static bool SegChar(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
           c == '+' || c == '(' || c == ')' || c == '-' || c == ' ';
}

void CheckRelPath(const std::string& p) {
    if (p.empty() || p.size() > kPathMax) throw UpdateError(Fmt("путь: пустой или длиннее %d", (int)kPathMax));
    if (p[0] == '/' || p.find('\\') != std::string::npos || p.find(':') != std::string::npos ||
        p.find('\0') != std::string::npos)
        throw UpdateError("путь «" + p + "»: абсолютный, с \\, : или NUL");
    std::vector<std::string> segs;
    size_t a = 0;
    for (;;) {
        size_t b = p.find('/', a);
        segs.push_back(p.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    static const char* devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "COM0", "COM1", "COM2", "COM3",
                                    "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT0", "LPT1", "LPT2", "LPT3",
                                    "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (const auto& seg : segs) {
        if (seg.empty() || seg == "." || seg == ".." || seg.back() == ' ' || seg.back() == '.')
            throw UpdateError("путь «" + p + "»: пустой/точечный сегмент");
        for (char c : seg)
            if (!SegChar(c))
                throw UpdateError("путь «" + p + "»: недопустимые символы (белый список A-Za-z0-9._+()- и пробел)");
        for (size_t i = 0; i + 1 < seg.size(); ++i)
            if (seg[i] == '~' && seg[i + 1] >= '0' && seg[i + 1] <= '9')
                throw UpdateError("путь «" + p + "»: похоже на короткое имя 8.3");
        std::string stem = seg.substr(0, seg.find('.'));
        while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.')) stem.pop_back();
        for (auto& c : stem)
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        for (const char* d : devices)
            if (stem == d) throw UpdateError("путь «" + p + "»: зарезервированное имя Windows");
    }
    std::string low = LowerAscii(p);
    size_t ts = std::strlen(kTmpSuffix);
    if (LowerAscii(segs[0]) == kStateDirName || (low.size() >= ts && low.compare(low.size() - ts, ts, kTmpSuffix) == 0))
        throw UpdateError("путь «" + p + "»: служебная папка или временный файл клиента");
}

void CheckHex64(const std::string& h, const std::string& what) {
    bool ok = h.size() == 64;
    for (char c : h)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) ok = false;
    if (!ok) throw UpdateError(what + ": не SHA-256 hex");
}

std::string KeyId(const uint8_t pub[32]) {
    Digest d = Sha256Of(pub, 32);
    return ToHex(d.data(), 8);
}

Bytes SafeZstd(const uint8_t* p, size_t n, std::optional<uint64_t> expected, std::optional<uint64_t> limit) {
    unsigned long long cs = ZSTD_getFrameContentSize(p, n);
    if (cs == ZSTD_CONTENTSIZE_ERROR) throw UpdateError("zstd: повреждённый заголовок кадра");
    if (cs == ZSTD_CONTENTSIZE_UNKNOWN) throw UpdateError("zstd: размер в кадре не указан");
    if ((expected && cs != *expected) || (limit && cs > *limit))
        throw UpdateError(Fmt("zstd: заявлено %llu байт, ожидалось %s", cs,
                              expected ? Fmt("%llu", (unsigned long long)*expected).c_str()
                                       : Fmt("≤ %llu", (unsigned long long)*limit).c_str()));
    Bytes out((size_t)cs);
    size_t r = ZSTD_decompress(out.data(), out.size(), p, n);
    if (ZSTD_isError(r)) throw UpdateError(std::string("zstd: ") + ZSTD_getErrorName(r));
    if (r != cs) throw UpdateError(Fmt("zstd: распаковано %zu вместо %llu", r, cs));
    return out;
}

// Чтение little-endian
static uint32_t Le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t Le64(const uint8_t* p) { return Le32(p) | (uint64_t)Le32(p + 4) << 32; }

std::vector<Piece> PlstUnpack(const Bytes& d) {
    if (d.size() < 16 || std::memcmp(d.data(), "ASPL", 4) != 0) throw UpdateError("plst: нет магии");
    uint32_t ver = Le32(&d[4]), npk = Le32(&d[8]), n = Le32(&d[12]);
    uint64_t want = 16ull + 32ull * npk + 56ull * n;          // в u64: в u32 переполняется
    if (ver != 1 || d.size() != want) throw UpdateError("plst: версия или длина не сходятся");
    std::vector<std::string> packs(npk);
    for (uint32_t i = 0; i < npk; ++i) packs[i] = ToHex(&d[16 + 32ull * i], 32);
    std::vector<Piece> out(n);
    const uint8_t* b = d.data() + 16 + 32ull * npk;
    for (uint32_t i = 0; i < n; ++i, b += 56) {
        Piece& pc = out[i];
        std::memcpy(pc.hash.data(), b, 32);
        pc.size = Le32(b + 32);
        pc.stored = Le32(b + 36);
        pc.codec = b[40];
        uint32_t pi = Le32(b + 44);
        pc.offset = Le64(b + 48);
        if (pi >= npk || (pc.codec != kCodecRaw && pc.codec != kCodecZstd) || pc.size == 0 || pc.size > kPieceMax ||
            pc.stored == 0 || pc.stored > kPieceMax || (pc.codec == kCodecRaw && pc.stored != pc.size) ||
            pc.offset > UINT64_MAX - pc.stored)                       // перенос u64 в offset+stored (ревизия В4)
            throw UpdateError(Fmt("plst: кусок %u с неверными полями", i));
        pc.pack = packs[pi];
    }
    return out;
}

std::string JStr(const rapidjson::Value& o, const char* name) {
    if (!o.IsObject()) return {};
    auto it = o.FindMember(name);
    return (it != o.MemberEnd() && it->value.IsString()) ? std::string(it->value.GetString(), it->value.GetStringLength())
                                                         : std::string();
}

bool JInt(const rapidjson::Value& o, const char* name, int64_t& out) {
    if (!o.IsObject()) return false;
    auto it = o.FindMember(name);
    if (it == o.MemberEnd() || !it->value.IsInt64()) return false;
    out = it->value.GetInt64();
    return true;
}

rapidjson::Document EnvelopeOpen(const std::string& kind, const Bytes& envelopeJson, const TrustedKeys& trusted) {
    rapidjson::Document env;
    env.Parse<rapidjson::kParseValidateEncodingFlag>((const char*)envelopeJson.data(), envelopeJson.size());
    Bytes payload;
    if (env.HasParseError() || !env.IsObject() || !env.HasMember("payload") || !env["payload"].IsString() ||
        !env.HasMember("sigs") || !Base64Decode(env["payload"].GetString(), payload))
        throw UpdateError("конверт " + kind + ": повреждён");
    std::string msg = std::string(kSigPrefix, sizeof(kSigPrefix)) + kind + std::string(1, '\0');
    msg.append((const char*)payload.data(), payload.size());
    const rapidjson::Value& sigs = env["sigs"];
    if (sigs.IsArray()) {
        for (const auto& s : sigs.GetArray()) {
            if (!s.IsObject() || !s.HasMember("key") || !s["key"].IsString() || !s.HasMember("sig") || !s["sig"].IsString())
                continue;                                    // мусорные типы — «нет подписи», а не исключение
            auto it = trusted.find(s["key"].GetString());
            Bytes sig;
            if (it == trusted.end() || !Base64Decode(s["sig"].GetString(), sig) || sig.size() != 64) continue;
            if (!Ed25519Verify(it->second.data(), sig.data(), (const uint8_t*)msg.data(), msg.size())) continue;
            rapidjson::Document obj;
            obj.Parse<rapidjson::kParseValidateEncodingFlag>((const char*)payload.data(), payload.size());
            if (obj.HasParseError()) throw UpdateError("конверт " + kind + ": нагрузка не JSON");
            if (!obj.IsObject() || JStr(obj, "format") != "asda-" + kind + "/1")
                throw UpdateError("конверт " + kind + ": чужой формат нагрузки");
            return obj;
        }
    }
    throw UpdateError("конверт " + kind + ": нет действительной подписи доверенным ключом");
}

TrustedKeys KeyringTrusted(const rapidjson::Document& keyring) {
    TrustedKeys out;
    auto it = keyring.FindMember("release_keys");
    if (it == keyring.MemberEnd() || !it->value.IsArray()) return out;
    for (const auto& k : it->value.GetArray()) {
        Bytes pub;
        if (!k.IsObject() || !FromHex(JStr(k, "pub"), pub) || pub.size() != 32) continue;
        // отозван — любое «истинное» значение, как в эталоне (not k.get("revoked")): false/null/0/"" — нет (ревизия В4, мелочь)
        auto rv = k.FindMember("revoked");
        bool revoked = false;
        if (rv != k.MemberEnd()) {
            const auto& v = rv->value;
            revoked = !(v.IsNull() || (v.IsBool() && !v.GetBool()) || (v.IsNumber() && v.GetDouble() == 0) ||
                        (v.IsString() && v.GetStringLength() == 0) || (v.IsArray() && v.Empty()) ||
                        (v.IsObject() && v.MemberCount() == 0));
        }
        if (revoked || KeyId(pub.data()) != JStr(k, "id")) continue;
        std::array<uint8_t, 32> a{};
        std::memcpy(a.data(), pub.data(), 32);
        out[JStr(k, "id")] = a;
    }
    return out;
}

}  // namespace asda
