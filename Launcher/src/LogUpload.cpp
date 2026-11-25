// Copyright (c) 2026 @sitxovski (Discord)
// LogUpload.cpp — ★ 19.10.2026 выгрузка запечатанных журналов прошлых игровых сессий (см. LogUpload.h и 00_SPEC.md).
#include "LogUpload.h"

#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <wincrypt.h>
#include <winhttp.h>

#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <rapidjson/document.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

#include "Config.h"
#include "Crypto.h"
#include "Launch.h"
#include "Util.h"

namespace asda {

namespace {

// ── пределы протокола (00_SPEC.md) ──────────────────────────────────────────────────────────────────────────────
constexpr size_t kMaxFileBytes = 4u << 20;                        // один файл (сервер: ≤ 4 МиБ; тело с одним файлом ≤ 8 МиБ)
constexpr uint64_t kHour = 3600ull * 10000000ull;                 // скользящее окно лимита запросов (FILETIME)
constexpr size_t kSentLimit = 4096;                               // предел upload_sent.txt
constexpr size_t kKeepUploaded = 10;                              // сколько хранить в Uploaded (и в Uploaded\rejected)
constexpr uint64_t kTicksPerSec = 10000000ull;                    // FILETIME: 100-нс тики
constexpr uint64_t kMaxAge = 14ull * 86400 * kTicksPerSec;        // незагруженное старше — удалить без выгрузки
constexpr uint64_t kSessionSlackBefore = 60 * kTicksPerSec;       // запас начала сессии при сопоставлении времени
constexpr uint64_t kSessionSlackAfter = 120 * kTicksPerSec;       // запас конца сессии
constexpr uint64_t kPidMatchWindow = 3600 * kTicksPerSec;         // pid падения ↔ сессии засчитывается в пределах часа
constexpr uint64_t kNearestLimit = 24ull * 3600 * kTicksPerSec;   // дальше суток — сессию не приписываем (логин пуст)
constexpr size_t kMetaLimit = 4096;                               // предел .alog.meta
constexpr size_t kAccountMax = 32;                                // логин учётки: 1..32 [A-Za-z0-9_.@-]
constexpr size_t kRespLimit = 4096;                               // сколько тела ответа дочитывать (ради keep-alive)

// ── формат AsdaLogSeal (Tools/asda_logseal.py, шапка) ──────────────────────────────────────────────────────────
constexpr uint8_t kSegMagic[8] = {'A', 'S', 'L', 'G', 'S', 'E', 'G', '1'};   // метка сегмента
constexpr size_t kSegHdr = 128;                                   // заголовок сегмента
constexpr size_t kFrameHdr = 12;                                  // u32 clen + u64 seq
constexpr uint32_t kTag = 16;                                     // тег GCM
constexpr uint32_t kMaxPlain = 64 * 1024;                         // AsdaSecure::kMaxPlain
constexpr size_t kMaxSegments = 4096;                             // сегментов в файле (предел приёмника)

// Виды файлов в конверте
enum Kind : uint8_t { kKindGame = 1, kKindEac = 2, kKindCrash = 3 };

// Файл-кандидат
struct Item {
    std::wstring dir, name;          // папка и имя
    uint8_t kind = 0;                // вид в конверте
    uint64_t mtime = 0, ctime = 0;   // время изменения / создания (FILETIME UTC)
    uint64_t size = 0;               // размер на диске
    DWORD volume = 0;                // серийный номер тома (опознание файла при переносе)
    uint64_t index = 0;              // индекс файла на томе
    Bytes data;                      // корректный префикс для отправки
    std::string account;             // логин учётки (может быть пустым)
    std::wstring Path() const { return dir + L"\\" + name; }
};

// Сессия для сопоставления логина (Sessions и Sessions\Uploaded)
struct Session {
    uint64_t ctime = 0, mtime = 0;   // начало (создание файла) и конец (последняя запись)
    unsigned long pid = 0;           // pid из имени <время>_<pid>.alog (0 — не разобран)
    std::string account;             // логин из .alog.meta (пусто — нет/неверный)
};

// FILETIME → 64 бита
uint64_t Ft(const FILETIME& f) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; }

// Текущее время UTC (FILETIME)
uint64_t NowFt() {
    FILETIME f;
    GetSystemTimeAsFileTime(&f);
    return Ft(f);
}

// Оканчивается ли имя на суффикс (без учёта регистра)
bool EndsWithI(const std::wstring& s, const wchar_t* suf) {
    size_t n = wcslen(suf);
    return s.size() >= n && _wcsicmp(s.c_str() + s.size() - n, suf) == 0;
}

// Начинается ли имя с префикса (без учёта регистра)
bool StartsWithI(const std::wstring& s, const wchar_t* pre) {
    size_t n = wcslen(pre);
    return s.size() >= n && _wcsnicmp(s.c_str(), pre, n) == 0;
}

// Обычные файлы папки (без подпапок и точек повторного разбора), отобранные предикатом
template <class Pred>
std::vector<WIN32_FIND_DATAW> ListFiles(const std::wstring& dir, Pred pred) {
    std::vector<WIN32_FIND_DATAW> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        if (pred(std::wstring(fd.cFileName))) out.push_back(fd);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

// Есть ли путь (файл или папка)
bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

// Путь — точка повторного разбора (джанкшен/символическая ссылка). ★ ревизия М8: в такие папки не заходим вовсе —
// перенос, удаление и чистка работали бы в чужой папке.
bool IsReparse(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
}

// Родительская папка пути
std::wstring ParentDir(const std::wstring& p) {
    size_t at = p.rfind(L'\\');
    return at == std::wstring::npos ? std::wstring() : p.substr(0, at);
}

// pid из имени «…_<цифры>.<расширение>» (0 — нет)
unsigned long PidFromName(const std::wstring& name) {
    size_t dot = name.rfind(L'.'), us = name.rfind(L'_', dot);
    if (dot == std::wstring::npos || us == std::wstring::npos || dot - us < 2 || dot - us > 11) return 0;
    unsigned long v = 0;
    for (size_t i = us + 1; i < dot; ++i) {
        if (name[i] < L'0' || name[i] > L'9') return 0;
        v = v * 10 + (name[i] - L'0');
    }
    return v;
}

// ★ 31.10.2026 pid из имени файла сессии игры «ГГГГММДД-ЧЧММСС_<pid>[_<n>][_part<N>].alog»: «_<n>» — повтор занятого имени,
// «_part<N>» — часть журнала сессии (ротация клиента, AsdaLogSealRetail.cpp) — у всех частей одной сессии тот же pid.
// PidFromName брал число после ПОСЛЕДНЕГО «_» — у «…_part3» и «…_2» выходил 0 или номер, и падение не находило свою сессию по pid.
// 0 — имя не файл сессии. Разбор ровно одного имени; префиксы FreeName снимает SessionPidFromName ниже.
unsigned long SessionPidFromExactName(const std::wstring& name) {
    if (name.size() < 22 || name[8] != L'-' || name[15] != L'_' || !EndsWithI(name, L".alog")) return 0;
    for (size_t i = 0; i < 15; ++i)
        if (i != 8 && (name[i] < L'0' || name[i] > L'9')) return 0;
    const size_t end = name.size() - 5;   // до «.alog»
    size_t i = 16;
    uint64_t v = 0;
    while (i < end && name[i] >= L'0' && name[i] <= L'9' && i - 16 < 10) v = v * 10 + (name[i++] - L'0');
    if (i == 16 || v == 0 || v > 0xFFFFFFFFull) return 0;
    auto digits = [&](size_t& at) {   // одна или больше цифр (не больше 4) с позиции at
        size_t s = at;
        while (at < end && name[at] >= L'0' && name[at] <= L'9' && at - s < 4) ++at;
        return at > s;
    };
    if (i < end && name[i] == L'_' && i + 1 < end && name[i + 1] >= L'0' && name[i + 1] <= L'9') {   // «_<n>»
        ++i;
        if (!digits(i)) return 0;
    }
    if (i < end) {   // «_part<N>»
        if (end - i < 6 || _wcsnicmp(name.c_str() + i, L"_part", 5) != 0) return 0;
        i += 5;
        if (!digits(i)) return 0;
    }
    return i == end ? (unsigned long)v : 0;
}

// Имя «штампа» FreeName «ГГГГММДД-ЧЧММСС_» в начале (n — с какой позиции).
bool HasStampPrefix(const std::wstring& name, size_t n) {
    if (name.size() < n + 16 || name[n + 8] != L'-' || name[n + 15] != L'_') return false;
    for (size_t i = 0; i < 15; ++i)
        if (i != 8 && (name[n + i] < L'0' || name[n + i] > L'9')) return false;
    return true;
}

// pid сессии с учётом переименования FreeName (Uploaded\: «<штамп>_<имя>» и «<штамп>_<N>_<имя>», ★ 31.10 ревизия): имя
// разбирается как есть; не вышло — без штампа «ГГГГММДД-ЧЧММСС_», затем ещё и без «<N>_» (N — 1..2 цифры, как у FreeName).
unsigned long SessionPidFromName(const std::wstring& name) {
    if (unsigned long pid = SessionPidFromExactName(name)) return pid;
    if (!HasStampPrefix(name, 0)) return 0;
    if (unsigned long pid = SessionPidFromExactName(name.substr(16))) return pid;
    size_t i = 16;
    while (i < name.size() && i < 18 && name[i] >= L'0' && name[i] <= L'9') ++i;
    return i > 16 && i < name.size() && name[i] == L'_' ? SessionPidFromExactName(name.substr(i + 1)) : 0;
}

// Логин учётки допустим: 1..32 символа [A-Za-z0-9_.@-] (как в БД, 00_SPEC.md «★ Дополнение»)
bool ValidAccount(const std::string& a) {
    if (a.empty() || a.size() > kAccountMax) return false;
    for (char c : a)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
              c == '@' || c == '-'))
            return false;
    return true;
}

// Прочитать небольшой файл целиком (не ссылку); false — нет/велик/ошибка
bool ReadSmall(const std::wstring& p, size_t limit, std::string& out) {
    HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION bi{};
    bool ok = GetFileInformationByHandle(f, &bi) && !(bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
              !bi.nFileSizeHigh && bi.nFileSizeLow <= limit;
    if (ok) {
        out.assign(bi.nFileSizeLow, '\0');
        DWORD got = 0;
        ok = out.empty() || (ReadFile(f, out.data(), (DWORD)out.size(), &got, nullptr) && got == out.size());
    }
    CloseHandle(f);
    return ok;
}

// Логин из <файл сессии>.meta (UTF-8 JSON {"account":"…","session":"…"}); пусто — нет/неверный
std::string ReadMetaAccount(const std::wstring& sessionPath) {
    std::string js;
    if (!ReadSmall(sessionPath + L".meta", kMetaLimit, js)) return "";
    rapidjson::Document d;
    d.Parse(js.data(), js.size());
    if (d.HasParseError() || !d.IsObject() || !d.HasMember("account") || !d["account"].IsString()) return "";
    std::string a(d["account"].GetString(), d["account"].GetStringLength());
    return ValidAccount(a) ? a : "";
}

// Точка P-256 (X||Y, 64 б) лежит на кривой и не бесконечность
bool OnP256(const uint8_t* xy) {
    static EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);   // общая группа, не освобождается
    if (!group) return false;
    uint8_t oct[65];
    oct[0] = 0x04;   // несжатая форма
    std::memcpy(oct + 1, xy, 64);
    EC_POINT* pt = EC_POINT_new(group);
    if (!pt) return false;
    bool ok = EC_POINT_oct2point(group, pt, oct, sizeof(oct), nullptr) == 1 && !EC_POINT_is_at_infinity(group, pt) &&
              EC_POINT_is_on_curve(group, pt, nullptr) == 1;
    EC_POINT_free(pt);
    return ok;
}

// Допустим ли вид сегмента в файле этого вида конверта (как KindAllowed приёмника AstoryServer/LogDrop):
// журнал игры — журнал движка (3) или перенос старых (5); журнал EAC — 1 или запасной маркер 2; падение — 4 или перенос (5)
bool SegKindAllowed(uint8_t envKind, uint8_t segKind) {
    switch (envKind) {
        case kKindGame: return segKind == 3 || segKind == 5;
        case kKindEac: return segKind == 1 || segKind == 2;
        case kKindCrash: return segKind == 4 || segKind == 5;
    }
    return false;
}

// keyId сегмента (8 б) — один из ключей журналов, которые принимает приёмник (Config.h kLogSealKeyIds)
bool KeyIdKnown(const uint8_t* kid) {
    for (const char* hex : config::kLogSealKeyIds) {
        Bytes k;
        if (FromHex(hex, k) && k.size() == 8 && std::memcmp(k.data(), kid, 8) == 0) return true;
    }
    return false;
}

// Длина самого длинного корректного префикса контейнера AsdaLogSeal ≤ limit (граница кадра; сегмент без кадров не
// засчитывается). Правила — как разбор приёмника (AstoryServer/LogDrop, Container.Validate) и asda_logseal.parse: метка,
// версия 1, вид сегмента под вид файла, нулевые резервы (10–11, 23, 120–127), один keyId на файл, журнальный ClientHello
// (A5 01 4C 00), эфемерный ключ на P-256, кадры clen ∈ [17, 64 КиБ + 16] с seq 1, 2, 3…, кадр целиком в файле,
// не больше kMaxSegments сегментов; keyId — один из вшитых текущих (KeyIdKnown, Config.h kLogSealKeyIds).
// 0 — корректного кадра нет вовсе.
size_t SealedPrefix(const uint8_t* d, size_t n, size_t limit, uint8_t envKind) {
    size_t pos = 0, good = 0, segs = 0;
    const uint8_t* kid = nullptr;   // keyId первого сегмента
    while (pos < n && segs < kMaxSegments) {
        if (n - pos < kSegHdr || std::memcmp(d + pos, kSegMagic, 8) != 0 || d[pos + 8] != 1 ||
            !SegKindAllowed(envKind, d[pos + 9]) || d[pos + 10] || d[pos + 11] || d[pos + 20] != 0xA5 || d[pos + 21] != 1 ||
            d[pos + 22] != 0x4C || d[pos + 23] != 0 || (kid && std::memcmp(kid, d + pos + 12, 8) != 0) ||
            !KeyIdKnown(d + pos + 12))
            break;
        bool reservedOk = true;
        for (size_t i = 120; i < kSegHdr; ++i) reservedOk = reservedOk && d[pos + i] == 0;
        if (!reservedOk || !OnP256(d + pos + 24)) break;
        if (!kid) kid = d + pos + 12;
        size_t p = pos + kSegHdr;
        uint64_t expect = 1;
        bool stop = false;
        while (p < n && !(n - p >= 4 && std::memcmp(d + p, kSegMagic, 4) == 0)) {
            if (n - p < kFrameHdr) {   // обрезанный заголовок кадра
                stop = true;
                break;
            }
            uint32_t clen;
            uint64_t seq;
            std::memcpy(&clen, d + p, 4);
            std::memcpy(&seq, d + p + 4, 8);
            if (clen < kTag + 1 || clen > kMaxPlain + kTag || seq != expect || n - p - kFrameHdr < clen ||
                p + kFrameHdr + clen > limit) {
                stop = true;   // порча, пропуск seq, обрезанный кадр или предел размера
                break;
            }
            p += kFrameHdr + clen;
            ++expect;
            good = p;
        }
        if (stop || expect == 1) break;   // дальше файл не берём; сегмент без кадров приёмник отвергает
        pos = p;
        ++segs;
    }
    return good;
}

// Свободное имя в папке: имя как есть, иначе <время изменения>_<имя>, иначе с номером
std::wstring FreeName(const std::wstring& dir, const std::wstring& name, uint64_t mtime) {
    if (!Exists(dir + L"\\" + name) && !Exists(dir + L"\\" + name + L".meta")) return name;
    FILETIME ft{(DWORD)mtime, (DWORD)(mtime >> 32)}, lt;
    SYSTEMTIME st{};
    FileTimeToLocalFileTime(&ft, &lt);
    FileTimeToSystemTime(&lt, &st);
    std::wstring stamp = Widen(Fmt("%04u%02u%02u-%02u%02u%02u_", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond));
    for (int i = 1; i < 100; ++i) {
        std::wstring n = stamp + (i > 1 ? Widen(Fmt("%d_", i)) : L"") + name;
        if (!Exists(dir + L"\\" + n)) return n;
    }
    return L"";
}

// Перенести файл в папку назначения через дескриптор, если это ТОТ ЖЕ файл, что читали (том+индекс+размер+время);
// у файла сессии рядом переносится и .meta. true — перенесён.
bool MoveChecked(const Item& it, const std::wstring& destDir, std::wstring& newName) {
    // папки назначения ниже папки файла: Uploaded и Uploaded\rejected (по одному уровню — CreateDirectory не рекурсивна)
    for (size_t at = it.dir.size(); (at = destDir.find(L'\\', at + 1)) != std::wstring::npos;) {
        CreateDirectoryW(destDir.substr(0, at).c_str(), nullptr);
        if (IsReparse(destDir.substr(0, at))) return false;   // ★ ревизия М8: Uploaded — джанкшен → не переносим
    }
    CreateDirectoryW(destDir.c_str(), nullptr);
    if (IsReparse(destDir)) return false;
    newName = FreeName(destDir, it.name, it.mtime);
    if (newName.empty()) return false;
    std::wstring src = it.Path(), dst = destDir + L"\\" + newName;
    HANDLE h = CreateFileW(src.c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION bi{};
    bool same = GetFileInformationByHandle(h, &bi) && bi.dwVolumeSerialNumber == it.volume &&
                (((uint64_t)bi.nFileIndexHigh << 32) | bi.nFileIndexLow) == it.index &&
                (((uint64_t)bi.nFileSizeHigh << 32) | bi.nFileSizeLow) == it.size && Ft(bi.ftLastWriteTime) == it.mtime;
    bool ok = false;
    if (same) {
        std::vector<uint8_t> buf(sizeof(FILE_RENAME_INFO) + dst.size() * sizeof(wchar_t));
        auto* ri = (FILE_RENAME_INFO*)buf.data();
        ri->ReplaceIfExists = FALSE;
        ri->RootDirectory = nullptr;
        ri->FileNameLength = (DWORD)(dst.size() * sizeof(wchar_t));
        std::memcpy(ri->FileName, dst.c_str(), dst.size() * sizeof(wchar_t));
        ok = SetFileInformationByHandle(h, FileRenameInfo, ri, (DWORD)buf.size()) != FALSE;
    }
    CloseHandle(h);
    if (ok && it.kind == kKindGame && Exists(src + L".meta")) MoveFileExW((src + L".meta").c_str(), (dst + L".meta").c_str(), 0);
    return ok;
}

// Удалить файл и его .meta
void DeleteWithMeta(const std::wstring& p) {
    DeleteFileW(p.c_str());
    DeleteFileW((p + L".meta").c_str());
}

// Журнал/падение по расширению
bool IsLogName(const std::wstring& n) { return EndsWithI(n, L".alog") || EndsWithI(n, L".acrash"); }

// Оставить в папке kKeepUploaded самых свежих журналов (с их .meta); осиротевшие .meta — удалить
void Prune(const std::wstring& dir) {
    if (IsReparse(dir) || IsReparse(ParentDir(dir))) return;   // ★ ревизия М8: Uploaded/rejected — джанкшен → не чистим
    auto files = ListFiles(dir, IsLogName);
    std::sort(files.begin(), files.end(), [](const WIN32_FIND_DATAW& a, const WIN32_FIND_DATAW& b) {
        return Ft(a.ftLastWriteTime) > Ft(b.ftLastWriteTime);
    });
    for (size_t i = kKeepUploaded; i < files.size(); ++i) DeleteWithMeta(dir + L"\\" + files[i].cFileName);
    for (auto& m : ListFiles(dir, [](const std::wstring& n) { return EndsWithI(n, L".meta"); })) {
        std::wstring base = dir + L"\\" + m.cFileName;
        base.resize(base.size() - 5);
        if (!Exists(base)) DeleteFileW((dir + L"\\" + m.cFileName).c_str());
    }
}

// id установки: 32 строчных hex из файла; нет/неверный — новый случайный (CNG) и запись атомарно
std::string InstallId(const std::wstring& file) {
    std::string s;
    if (ReadSmall(file, 256, s)) {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        bool ok = s.size() == 32;
        for (char c : s) ok = ok && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
        if (ok) return s;
    }
    uint8_t rnd[16];
    if (BCryptGenRandom(nullptr, rnd, sizeof(rnd), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return "";
    s = ToHex(rnd, sizeof(rnd));
    size_t slash = file.rfind(L'\\');
    if (slash != std::wstring::npos) CreateDirectoryW(file.substr(0, slash).c_str(), nullptr);
    std::wstring tmp = file + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        bool ok = WriteFile(f, s.data(), (DWORD)s.size(), &w, nullptr) && w == s.size() && FlushFileBuffers(f);
        CloseHandle(f);
        if (!ok || !MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            DeleteFileW(tmp.c_str());
    }
    return s;
}

// Прочитать кандидата: открыть без общего доступа на запись (кто-то пишет — false), запомнить опознание файла,
// взять корректный префикс. prefixLen = 0 — корректных кадров нет.
bool ReadCandidate(Item& it, size_t& prefixLen) {
    HANDLE f = CreateFileW(it.Path().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION bi{};
    bool ok = GetFileInformationByHandle(f, &bi) && !(bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
    if (ok) {
        it.volume = bi.dwVolumeSerialNumber;
        it.index = ((uint64_t)bi.nFileIndexHigh << 32) | bi.nFileIndexLow;
        it.size = ((uint64_t)bi.nFileSizeHigh << 32) | bi.nFileSizeLow;
        it.mtime = Ft(bi.ftLastWriteTime);
        it.ctime = Ft(bi.ftCreationTime);
        Bytes b((size_t)std::min<uint64_t>(it.size, kMaxFileBytes));
        DWORD got = 0;
        ok = b.empty() || (ReadFile(f, b.data(), (DWORD)b.size(), &got, nullptr) && got == b.size());
        if (ok) {
            prefixLen = SealedPrefix(b.data(), b.size(), kMaxFileBytes, it.kind);
            b.resize(prefixLen);
            it.data = std::move(b);
        }
    }
    CloseHandle(f);
    return ok;
}

// Расстояние от момента t до интервала сессии (0 — внутри с запасом)
uint64_t Distance(const Session& s, uint64_t t) {
    uint64_t start = std::min(s.ctime, s.mtime);   // скопированный файл: создание позже изменения — считаем точкой
    uint64_t lo = start > kSessionSlackBefore ? start - kSessionSlackBefore : 0, hi = s.mtime + kSessionSlackAfter;
    if (t >= lo && t <= hi) return 0;
    return t < lo ? lo - t : t - hi;
}

// Логин для журнала EAC/падения: сессия того же pid (падение, в пределах часа), иначе ближайшая по времени (≤ суток)
std::string AccountFor(const Item& it, const std::vector<Session>& sessions) {
    unsigned long pid = it.kind == kKindCrash ? PidFromName(it.name) : 0;
    const Session* best = nullptr;
    uint64_t bestD = ~0ull;
    if (pid)
        for (auto& s : sessions)
            if (s.pid == pid && Distance(s, it.mtime) <= kPidMatchWindow && Distance(s, it.mtime) < bestD) {
                best = &s;
                bestD = Distance(s, it.mtime);
            }
    if (!best)
        for (auto& s : sessions) {
            uint64_t d = Distance(s, it.mtime);
            if (d < bestD || (d == bestD && best && s.mtime > best->mtime)) {
                best = &s;
                bestD = d;
            }
        }
    return best && bestD <= kNearestLimit ? best->account : "";
}

// Сессии для сопоставления логина: Sessions\*.alog и Sessions\Uploaded\*.alog
std::vector<Session> CollectSessions(const std::wstring& sessionsDir) {
    std::vector<Session> out;
    for (const std::wstring& dir : {sessionsDir, sessionsDir + L"\\Uploaded"}) {
        if (IsReparse(dir)) continue;   // ★ ревизия М8
        for (auto& fd : ListFiles(dir, [](const std::wstring& n) { return EndsWithI(n, L".alog"); })) {
            Session s;
            s.ctime = Ft(fd.ftCreationTime);
            s.mtime = Ft(fd.ftLastWriteTime);
            s.pid = SessionPidFromName(fd.cFileName);   // ★ 31.10: и у частей «_partN», и у «_2»
            s.account = ReadMetaAccount(dir + L"\\" + fd.cFileName);
            out.push_back(std::move(s));
        }
    }
    return out;
}

// Дописать u32 LE
void PutU32(std::string& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back((char)(v >> (8 * i)));
}

// Файл времени отправок (лимит запросов за час): upload_sent.txt рядом с install.id
std::wstring SentFile(const std::wstring& installIdFile) {
    size_t slash = installIdFile.rfind(L'\\');
    return (slash == std::wstring::npos ? std::wstring() : installIdFile.substr(0, slash + 1)) + L"upload_sent.txt";
}

// Времена отправок (FILETIME, по строке) за последний час; записи «из будущего» дальше часа (перевод часов) — отбросить
std::vector<uint64_t> LoadSent(const std::wstring& path, uint64_t now) {
    std::vector<uint64_t> out;
    std::string s;
    if (!ReadSmall(path, kSentLimit, s)) return out;
    size_t at = 0;
    while (at < s.size()) {
        size_t e = s.find('\n', at);
        if (e == std::string::npos) e = s.size();
        uint64_t t = _strtoui64(s.substr(at, e - at).c_str(), nullptr, 10);
        if (t && t + kHour > now && t < now + kHour) out.push_back(t);
        at = e + 1;
    }
    return out;
}

// Сохранить времена отправок (временный файл → замена)
void SaveSent(const std::wstring& path, const std::vector<uint64_t>& times) {
    std::string s;
    for (uint64_t t : times) s += Fmt("%llu\n", (unsigned long long)t);
    std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    bool ok = WriteFile(f, s.data(), (DWORD)s.size(), &w, nullptr) && w == s.size();
    CloseHandle(f);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) DeleteFileW(tmp.c_str());
}

// RAII-дескриптор WinHTTP
struct HNet {
    HINTERNET h = nullptr;
    explicit HNet(HINTERNET x) : h(x) {}
    ~HNet() {
        if (h) WinHttpCloseHandle(h);
    }
    HNet(const HNet&) = delete;
    HNet& operator=(const HNet&) = delete;
};

}  // namespace

const char* LogUploadResultName(LogUploadResult r) {
    switch (r) {
        case LogUploadResult::Nothing: return "nothing";
        case LogUploadResult::Uploaded: return "uploaded";
        case LogUploadResult::Rejected: return "rejected";
        case LogUploadResult::Retry: return "retry";
        case LogUploadResult::Throttled: return "throttled";
        case LogUploadResult::GameRunning: return "game-running";
        case LogUploadResult::Stopped: return "stopped";
    }
    return "?";
}

LogUploadOptions DefaultLogUploadOptions(std::function<void(const std::string&)> log) {
    LogUploadOptions o;
    o.log = std::move(log);
    PWSTR base = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base) == S_OK) {
        o.savedDir = std::wstring(base) + L"\\" + config::kGameSavedSubdir;
        o.installIdFile = std::wstring(base) + L"\\" + config::kLauncherDataSubdir + L"\\install.id";
    }
    if (base) CoTaskMemFree(base);
    return o;
}

LogUploader::~LogUploader() {
    RequestStop();
    if (thread_.joinable()) thread_.join();   // идущий запрос уже прерван — поток выходит сразу
    Disconnect();
}

void LogUploader::Start(LogUploadOptions o) {
    if (thread_.joinable()) return;
    stop_ = false;
    done_ = false;
    thread_ = std::thread([this, o = std::move(o)] {
        try {
            Pass(o);
        } catch (const std::exception& e) {   // выгрузка не должна ронять лаунчер ни при каких условиях
            if (o.log) o.log(std::string("журналы: сбой выгрузки: ") + e.what());
        } catch (...) {
            if (o.log) o.log("журналы: сбой выгрузки");
        }
        done_ = true;
    });
}

LogUploadStats LogUploader::RunNow(const LogUploadOptions& o) {
    stop_ = false;
    return Pass(o);
}

void LogUploader::RequestStop() {
    stop_ = true;
    AbortRequest();
    cv_.notify_all();
}

void LogUploader::JoinIfDone() {
    if (thread_.joinable() && done_) thread_.join();
}

void LogUploader::AbortRequest() {
    std::lock_guard<std::mutex> lk(m_);
    if (req_) {   // синхронный запрос WinHTTP прерывается закрытием его дескриптора
        WinHttpCloseHandle((HINTERNET)req_);
        req_ = nullptr;
    }
}

// SPKI корня цепочки сервера: SHA-256 (hex) DER SubjectPublicKeyInfo корня входит в config::kIngestRootSpki
// (ISRG Root X1/X2 и Root YE). Цепочку строит CryptoAPI от сертификата, предъявленного в этом соединении; ошибки
// доверия цепочки — отказ. why — причина отказа (имя корня).
static bool PinnedChain(HINTERNET req, const std::vector<std::string>& pins, std::string& why) {
    PCCERT_CONTEXT leaf = nullptr;
    DWORD sz = sizeof(leaf);
    if (!WinHttpQueryOption(req, WINHTTP_OPTION_SERVER_CERT_CONTEXT, &leaf, &sz) || !leaf) {
        why = "нет сертификата сервера";
        return false;
    }
    CERT_CHAIN_PARA para{};
    para.cbSize = sizeof(para);
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    bool ok = CertGetCertificateChain(nullptr, leaf, nullptr, leaf->hCertStore, &para, 0, nullptr, &chain) && chain &&
              chain->cChain > 0 && chain->rgpChain[0]->cElement > 0;
    if (!ok) {
        why = "цепочка сертификата не построена";
    } else if (chain->TrustStatus.dwErrorStatus != CERT_TRUST_NO_ERROR) {
        why = Fmt("цепочка недоверенная (0x%08lx)", (unsigned long)chain->TrustStatus.dwErrorStatus);
        ok = false;
    } else {
        const CERT_SIMPLE_CHAIN* sc = chain->rgpChain[0];
        PCCERT_CONTEXT root = sc->rgpElement[sc->cElement - 1]->pCertContext;
        BYTE* der = nullptr;
        DWORD derLen = 0;
        ok = CryptEncodeObjectEx(X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO, &root->pCertInfo->SubjectPublicKeyInfo,
                                 CRYPT_ENCODE_ALLOC_FLAG, nullptr, &der, &derLen) != FALSE;
        std::string spki = ok ? Sha256Hex(der, derLen) : "";
        if (der) LocalFree(der);
        ok = ok && std::find(pins.begin(), pins.end(), spki) != pins.end();
        if (!ok) {
            wchar_t name[256]{};
            CertGetNameStringW(root, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 255);
            why = "корень цепочки не наш: «" + Narrow(name) + "» SPKI " + spki.substr(0, 16);
        }
    }
    if (chain) CertFreeCertificateChain(chain);
    CertFreeCertificateContext(leaf);
    return ok;
}


bool LogUploader::Connect(const LogUploadOptions& o, std::string& why) {
    if (connect_) return true;
    // Приёмник: в боевой сборке — только вшитый узел по https; в тестовой — ещё http://127.0.0.1/localhost
    host_ = config::kIngestHost;
    path_ = config::kIngestPath;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    secure_ = true;
#if defined(ASDA_LAUNCHER_TEST_BUILD)
    if (!o.endpoint.empty()) {
        std::wstring w = Widen(o.endpoint);
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        wchar_t h[256]{}, p[1024]{};
        uc.lpszHostName = h;
        uc.dwHostNameLength = 255;
        uc.lpszUrlPath = p;
        uc.dwUrlPathLength = 1023;
        if (!WinHttpCrackUrl(w.c_str(), 0, 0, &uc)) {
            why = "неверный адрес приёмника";
            return false;
        }
        host_ = h;
        path_ = p;
        port = uc.nPort;
        secure_ = uc.nScheme == INTERNET_SCHEME_HTTPS;
        bool local = host_ == L"127.0.0.1" || host_ == L"localhost";
        if (!(secure_ && host_ == config::kIngestHost) && !(!secure_ && local)) {
            why = "тестовый приёмник: только https://@@ASTORY_AUTH_HOST@@ или http://127.0.0.1";
            return false;
        }
    }
#else
    (void)o;
#endif
    // ★ ревизия В5: прямое соединение, без системного прокси (Fiddler/mitmproxy не подхватываются сами); /status (Ping.cpp)
    //   по-прежнему ходит через WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY — его это не касается.
    HINTERNET s = WinHttpOpen(L"AsdaLauncher/1", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) {
        why = "WinHTTP: " + LastErrorText(GetLastError());
        return false;
    }
    DWORD protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos))) {
        protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;   // TLS 1.3 нет в старых Windows 10
        WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos));
    }
    DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;   // переадресации запрещены: только этот узел
    WinHttpSetOption(s, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));
    const int t = (int)config::kIngestTimeoutMs;
    WinHttpSetTimeouts(s, t, t, t, t);
    HINTERNET c = WinHttpConnect(s, host_.c_str(), port, 0);
    if (!c) {
        why = "WinHTTP: " + LastErrorText(GetLastError());
        WinHttpCloseHandle(s);
        return false;
    }
    session_ = s;
    connect_ = c;
    return true;
}

void LogUploader::Disconnect() {
    if (connect_) WinHttpCloseHandle((HINTERNET)connect_);
    if (session_) WinHttpCloseHandle((HINTERNET)session_);
    connect_ = session_ = nullptr;
}

bool LogUploader::Post(const LogUploadOptions& o, const std::string& body, const std::string& installId,
                       unsigned long& status, std::string& why) {
    // ★ ревизия М7: одна сессия и одно соединение WinHTTP на весь проход (keep-alive) — без нового рукопожатия на файл
    if (!Connect(o, why)) return false;
    // Сертификат проверяется WinHTTP по умолчанию (флаги SECURITY_FLAG_IGNORE_* не ставятся нигде) + отзыв + пиннинг корня
    HINTERNET r = WinHttpOpenRequest((HINTERNET)connect_, L"POST", path_.c_str(), nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, secure_ ? WINHTTP_FLAG_SECURE : 0);
    if (!r) {
        why = "WinHTTP: " + LastErrorText(GetLastError());
        return false;
    }
    if (secure_) {   // ★ ревизия В5: проверка отзыва сертификатов цепочки
        DWORD rev = WINHTTP_ENABLE_SSL_REVOCATION;
        WinHttpSetOption(r, WINHTTP_OPTION_ENABLE_FEATURE, &rev, sizeof(rev));
    }
    {
        std::lock_guard<std::mutex> lk(m_);
        if (stop_) {
            WinHttpCloseHandle(r);
            why = "остановлено";
            return false;
        }
        req_ = r;
    }
    // Сторож: весь запрос — не дольше kIngestTimeoutMs, затем дескриптор закрывается (синхронные вызовы возвращают ошибку)
    std::atomic<bool> finished{false}, timedOut{false};
    std::thread watchdog([&] {
        std::unique_lock<std::mutex> lk(m_);
        bool fin = cv_.wait_for(lk, std::chrono::milliseconds(config::kIngestTimeoutMs),
                                [&] { return finished.load() || stop_.load(); });
        if (!fin && req_ == r) {
            timedOut = true;
            WinHttpCloseHandle(r);
            req_ = nullptr;
        }
    });
    // Заголовки (с Content-Length = размер тела) уходят вместе с TLS-рукопожатием; ТЕЛО — только после проверки корня
    // цепочки (★ ревизия В5): чужому корню (перехват) достаются лишь путь и заголовки, но не журнал.
    std::wstring headers = L"Content-Type: application/octet-stream\r\nX-Asda-Install: " + Widen(installId) + L"\r\n";
    bool ok = WinHttpSendRequest(r, headers.c_str(), (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, (DWORD)body.size(), 0) != FALSE;
    DWORD err = ok ? 0 : GetLastError();
    bool pinFail = false;
    if (ok && secure_) {
        std::vector<std::string> pins;
        for (const char* p : config::kIngestRootSpki) pins.push_back(p);
#if defined(ASDA_LAUNCHER_TEST_BUILD)
        if (!o.pinsOverride.empty()) pins = o.pinsOverride;
#endif
        std::string pw;
        if (!PinnedChain(r, pins, pw)) {
            ok = false;
            pinFail = true;
            why = "сертификат: " + pw;
        }
    }
    for (size_t at = 0; ok && at < body.size();) {   // тело кусками по 64 КиБ
        DWORD part = (DWORD)std::min<size_t>(body.size() - at, 64 * 1024), wrote = 0;
        ok = WinHttpWriteData(r, body.data() + at, part, &wrote) && wrote == part;
        if (!ok) err = GetLastError();
        at += part;
    }
    if (ok) {
        ok = WinHttpReceiveResponse(r, nullptr) != FALSE;
        if (!ok) err = GetLastError();
    }
    if (ok) {
        DWORD st = 0, sz = sizeof(st);
        ok = WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &st, &sz,
                                 WINHTTP_NO_HEADER_INDEX);
        status = st;
        size_t drained = 0;   // тело ответа (без подробностей по протоколу) — дочитать немного и бросить
        while (ok && drained < kRespLimit) {
            char buf[512];
            DWORD got = 0;
            if (!WinHttpReadData(r, buf, sizeof(buf), &got) || !got) break;
            drained += got;
        }
    }
    {
        std::lock_guard<std::mutex> lk(m_);
        finished = true;
        if (req_ == r) {   // иначе дескриптор уже закрыл сторож или RequestStop
            WinHttpCloseHandle(r);
            req_ = nullptr;
        }
    }
    cv_.notify_all();
    watchdog.join();
    if (!ok) {
        if (!pinFail)
            why = timedOut ? Fmt("таймаут %u с", config::kIngestTimeoutMs / 1000)
                  : stop_   ? std::string("остановлено")
                            : "сеть: " + LastErrorText(err);
        Disconnect();   // после сбоя соединение не переиспользуем
        return false;
    }
    return true;
}

LogUploadStats LogUploader::Pass(const LogUploadOptions& o) {
    LogUploadStats st;
    auto log = [&](const std::string& s) {
        if (o.log) o.log("журналы: " + s);
    };
    if (o.savedDir.empty()) return st;
    if (GameRunning()) {   // игра пишет свои журналы — ничего не трогаем
        log("игра запущена — выгрузка пропущена");
        st.result = LogUploadResult::GameRunning;
        return st;
    }
    const std::wstring logsDir = o.savedDir + L"\\Logs", sessionsDir = logsDir + L"\\Sessions",
                       crashDir = o.savedDir + L"\\Crashes";
    // ★ ревизия М8: Saved, Logs, Sessions, Crashes — не джанкшены (иначе папка пропускается целиком)
    if (IsReparse(o.savedDir) || IsReparse(ParentDir(o.savedDir))) {
        log("папка Saved — точка повторного разбора, выгрузка пропущена");
        return st;
    }
    const bool logsOk = !IsReparse(logsDir), sessionsOk = logsOk && !IsReparse(sessionsDir), crashOk = !IsReparse(crashDir);
    if (!logsOk || !sessionsOk || !crashOk) log("папка журналов — точка повторного разбора, пропущена");
    // 1) кандидаты трёх видов
    std::vector<Item> cand;
    auto add = [&](const std::wstring& dir, uint8_t kind, auto pred) {
        bool ok = dir == sessionsDir ? sessionsOk : dir == logsDir ? logsOk : crashOk;
        if (!ok) return;
        for (auto& fd : ListFiles(dir, pred)) {
            Item it;
            it.dir = dir;
            it.name = fd.cFileName;
            it.kind = kind;
            it.mtime = Ft(fd.ftLastWriteTime);
            it.ctime = Ft(fd.ftCreationTime);
            cand.push_back(std::move(it));
        }
    };
    add(sessionsDir, kKindGame, [](const std::wstring& n) { return EndsWithI(n, L".alog"); });
    add(logsDir, kKindEac, [](const std::wstring& n) { return StartsWithI(n, L"AsdaEac") && EndsWithI(n, L".alog"); });
    add(crashDir, kKindCrash, [](const std::wstring& n) { return EndsWithI(n, L".acrash"); });
    // 2) старше 14 дней — удалить без выгрузки (и осиротевшие .meta сессий того же возраста)
    const uint64_t now = NowFt();
    for (auto it = cand.begin(); it != cand.end();) {
        if (now > it->mtime && now - it->mtime > kMaxAge) {
            DeleteWithMeta(it->Path());
            ++st.expired;
            it = cand.erase(it);
        } else {
            ++it;
        }
    }
    if (sessionsOk)
        for (auto& m : ListFiles(sessionsDir, [](const std::wstring& n) { return EndsWithI(n, L".alog.meta"); })) {
            std::wstring p = sessionsDir + L"\\" + m.cFileName;
            if (!Exists(p.substr(0, p.size() - 5)) && now > Ft(m.ftLastWriteTime) && now - Ft(m.ftLastWriteTime) > kMaxAge)
                DeleteFileW(p.c_str());
        }
    if (st.expired) log(Fmt("удалено без выгрузки (старше 14 дней): %d", st.expired));
    if (cand.empty()) return st;
    std::sort(cand.begin(), cand.end(), [](const Item& a, const Item& b) { return a.mtime > b.mtime; });
    // 3) свой лимит: не больше kIngestMaxPerRun запросов за запуск и kIngestMaxPerHour за скользящий час
    unsigned perHour = config::kIngestMaxPerHour;
#if defined(ASDA_LAUNCHER_TEST_BUILD)
    if (o.maxPerHour >= 0) perHour = (unsigned)o.maxPerHour;
#endif
    const std::wstring sentFile = SentFile(o.installIdFile);
    std::vector<uint64_t> sent = LoadSent(sentFile, now);
    std::vector<Session> sessions = CollectSessions(sessionsDir);
    std::string installId;
    int busy = 0, cut = 0;
    bool stopRun = false;   // повтор (429/5xx/сеть) или остановка — дальше в этот запуск не шлём
    for (auto& it : cand) {
        if (stopRun || stop_ || st.requests >= (int)config::kIngestMaxPerRun) break;
        size_t prefix = 0;
        if (!ReadCandidate(it, prefix)) {   // файл открыт на запись другим процессом или не читается
            ++busy;
            continue;
        }
        if (!prefix) {   // ни одного корректного кадра или чужой/старый keyId — на сервер не отправляем
            std::wstring nn;
            if (MoveChecked(it, it.dir + L"\\Uploaded\\rejected", nn)) ++st.rejectedLocal;
            continue;
        }
        if (sent.size() >= perHour) {   // лимит приёмника на IP/установку не превышаем — файл ждёт следующего запуска
            st.throttled = true;
            break;
        }
        if (installId.empty()) installId = InstallId(o.installIdFile);
        if (installId.empty()) {
            log("нет id установки (CNG) — выгрузка пропущена");
            stopRun = true;
            ++st.retry;
            break;
        }
        bool wasCut = prefix < it.size;
        cut += wasCut;
        it.account = it.kind == kKindGame ? ReadMetaAccount(it.Path()) : AccountFor(it, sessions);
        // 4) конверт ASLU v2 с одним файлом: "ASLU" | u8 2 | u8 1 | u16 0 | u8 вид | u8 A | A байт логина | u32 L | L байт
        std::string env;
        env.reserve(8 + 6 + it.account.size() + it.data.size());
        env.append("ASLU", 4);
        env.push_back((char)2);
        env.push_back((char)1);
        env.push_back('\0');
        env.push_back('\0');
        env.push_back((char)it.kind);
        env.push_back((char)it.account.size());
        env.append(it.account);
        PutU32(env, (uint32_t)it.data.size());
        env.append((const char*)it.data.data(), it.data.size());
        // время отправки — до запроса: приёмник считает и запросы, на которые мы не дождались ответа
        sent.push_back(NowFt());
        SaveSent(sentFile, sent);
        ++st.requests;
        // 5) отправка и итог по этому файлу
        std::string what = Fmt("%s (вид %d, %zu КБ%s%s)", Narrow(it.name).c_str(), it.kind, (env.size() + 1023) / 1024,
                               it.account.empty() ? ", без логина" : ", с логином", wasCut ? ", обрезан по границе кадра" : "");
        unsigned long status = 0;
        std::string why;
        if (!Post(o, env, installId, status, why)) {
            log(what + " — не отправлено (" + why + "), повтор при следующем запуске");
            ++st.retry;
            stopRun = true;
            continue;
        }
        std::wstring nn;
        if (status == 204) {
            ++st.uploaded;
            bool moved = MoveChecked(it, it.dir + L"\\Uploaded", nn);
            log(what + Fmt(" — HTTP 204: принято%s", moved ? ", перенесено в Uploaded" : ", перенести не удалось"));
        } else if (status == 400 || status == 413) {
            ++st.rejected;
            bool moved = MoveChecked(it, it.dir + L"\\Uploaded\\rejected", nn);
            log(what + Fmt(" — HTTP %lu: отклонено%s", status, moved ? ", перенесено в Uploaded\\rejected" : ", перенести не удалось"));
        } else {   // 429, 5xx и любой иной код — повтор при следующем запуске; дальше в этот запуск не шлём
            ++st.retry;
            stopRun = true;
            log(what + Fmt(" — HTTP %lu: повтор при следующем запуске", status));
        }
    }
    if (st.rejectedLocal) log(Fmt("не контейнер AsdaLogSeal или старый ключ — в Uploaded\\rejected без отправки: %d", st.rejectedLocal));
    if (busy) log(Fmt("занято другим процессом или не читается (пропуск): %d", busy));
    if (st.throttled) log(Fmt("лимит %u запросов за час исчерпан — остальное при следующем запуске", perHour));
    Disconnect();   // соединение прохода больше не нужно
    for (const std::wstring& d : {sessionsDir, logsDir, crashDir}) {
        if ((d == sessionsDir && !sessionsOk) || (d == logsDir && !logsOk) || (d == crashDir && !crashOk)) continue;
        Prune(d + L"\\Uploaded");
        Prune(d + L"\\Uploaded\\rejected");
    }
    if (stop_ && st.retry) st.result = LogUploadResult::Stopped;
    else if (st.retry) st.result = LogUploadResult::Retry;
    else if (st.uploaded) st.result = LogUploadResult::Uploaded;
    else if (st.rejected || st.rejectedLocal) st.result = LogUploadResult::Rejected;
    else if (st.throttled) st.result = LogUploadResult::Throttled;
    else if (stop_) st.result = LogUploadResult::Stopped;
    return st;
}

#if defined(ASDA_LAUNCHER_TEST_BUILD)
int LogUploadNameSelfTest(const std::function<void(const std::string&)>& out) {
    int fail = 0;
    auto expect = [&](bool c, const std::string& what) {
        out(std::string(c ? "ok   " : "FAIL ") + what);
        fail += c ? 0 : 1;
    };
    // pid сессии из имён файлов журнала: старое имя, повтор «_2», части «_part2…_part9», их сочетание, мусор
    struct Case { const wchar_t* name; unsigned long pid; };
    const Case cases[] = {
        {L"20261031-120000_4242.alog", 4242},          {L"20261031-120000_4242_2.alog", 4242},
        {L"20261031-120000_4242_part2.alog", 4242},    {L"20261031-120000_4242_part9.alog", 4242},
        {L"20261031-120000_4242_2_part3.alog", 4242},  {L"20261031-120000_4242_PART12.alog", 4242},
        {L"20261031-120000_4294967295.alog", 4294967295ul},
        {L"20261031-120000_4294967296.alog", 0},       {L"20261031-120000_4242_part.alog", 0},
        {L"20261031-120000_4242_partx.alog", 0},       {L"20261031-120000_4242_x.alog", 0},
        {L"20261031-120000_.alog", 0},                 {L"2026103a-120000_4242.alog", 0},
        {L"20261031-120000_4242.acrash", 0},           {L"AsdaEac.alog", 0},
        {L"20261031-120000_4242_part2.alog.meta", 0},  {L"20261031-120000_4242_part2_2.alog", 0},
        // ★ ревизия 31.10: префиксы FreeName (Uploaded), регистр расширения, ведущие нули, длина pid и номера части
        {L"20261031-223000_20261031-120000_4242.alog", 4242},           {L"20261031-223000_2_20261031-120000_4242_part3.alog", 4242},
        {L"20261031-223000_17_20261031-120000_4242_2.alog", 4242},      {L"20261031-223000_x_20261031-120000_4242.alog", 0},
        {L"20261031-223000_123_20261031-120000_4242.alog", 0},          {L"20261031-120000_4242.ALOG", 4242},
        {L"20261031-120000_4242_Part2.Alog", 4242},                     {L"20261031-120000_0042.alog", 42},
        {L"20261031-120000_0000.alog", 0},                              {L"20261031-120000_12345678901.alog", 0},
        {L"20261031-120000_1234567890.alog", 1234567890ul},             {L"20261031-120000_4242_part12345.alog", 0},
        {L"20261031-120000_4242_part1234.alog", 4242},                  {L"20261031-223000_20261031-120000_12345678901.alog", 0},
    };
    for (const Case& c : cases) {
        unsigned long got = SessionPidFromName(c.name);
        expect(got == c.pid, Fmt("SessionPidFromName(%s) = %lu (ждали %lu)", Narrow(c.name).c_str(), got, c.pid));
    }
    for (int n = 2; n <= 9; ++n)
        expect(SessionPidFromName(Widen(Fmt("20261031-120000_77_part%d.alog", n))) == 77, Fmt("часть %d — pid 77", n));
    // pid падения (как раньше — по последнему «_»): AsdaCrash_<время>_<pid>.acrash; перенос старых — без pid
    expect(PidFromName(L"AsdaCrash_20261031-121500_4242.acrash") == 4242, "AsdaCrash_<время>_4242.acrash — pid 4242");
    expect(PidFromName(L"UECC-Windows-ABC_20261031-121500.acrash") == 0, "перенесённое падение движка — pid нет");
    // Падение → сессия по pid: части одной сессии (pid 4242, логин a) и чужая сессия ближе по времени (pid 5, логин b)
    const uint64_t T = 133000000000000000ull, M = 60ull * 10000000ull;   // произвольное время и минута (FILETIME)
    std::vector<Session> ss;
    for (int n = 1; n <= 6; ++n) {
        Session s;
        s.pid = SessionPidFromName(Widen(n == 1 ? std::string("20261031-120000_4242.alog") : Fmt("20261031-120000_4242_part%d.alog", n + 3)));
        s.ctime = T + (n - 1) * 40 * M;
        s.mtime = s.ctime + 40 * M;
        s.account = "a";
        ss.push_back(s);
    }
    Session other;
    other.pid = SessionPidFromName(L"20261031-150000_5.alog");
    other.ctime = T + 239 * M;   // открыта за минуту до падения — по времени «ближе»
    other.mtime = T + 300 * M;
    other.account = "b";
    ss.push_back(other);
    Item crash;
    crash.kind = kKindCrash;
    crash.name = L"AsdaCrash_20261031-160000_4242.acrash";
    crash.mtime = T + 240 * M + 30 * 10000000ull;   // через 30 с после конца последней части
    expect(AccountFor(crash, ss) == "a", "падение pid 4242 после части 9 → логин сессии a (по pid, а не ближайшая по времени b)");
    crash.name = L"UECC-Windows-ABC_20261031-160000.acrash";
    expect(AccountFor(crash, ss) == "b", "падение без pid → ближайшая по времени сессия (b)");
    out(Fmt("LOGUPLOAD-NAMES ИТОГ: fail=%d", fail));
    return fail;
}
#endif

}  // namespace asda
