// Copyright (c) 2026 @sitxovski (Discord)
// Http.cpp — WinHTTP и локальный источник (см. Http.h).
#include "Http.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <mutex>
#include <thread>

namespace asda {

namespace {

// Число попыток при сетевом сбое и пауза между ними (рост ×2)
constexpr int kRetries = 4;
constexpr DWORD kRetryDelayMs = 1500;
// ★ 09.11.2026 Размер одного чтения тела ответа (WinHttpReadData) — см. Request
constexpr size_t kReadChunk = 1u << 20;

// Источник «локальная папка»
class LocalSource final : public Source {
public:
    explicit LocalSource(std::wstring root) : root_(std::move(root)) {}

    Bytes Get(const std::string& rel, size_t limit) override {
        HANDLE f = Open(rel);
        LARGE_INTEGER sz{};
        GetFileSizeEx(f, &sz);
        if ((uint64_t)sz.QuadPart > limit) {
            CloseHandle(f);
            throw UpdateError(Fmt("%s больше предела %zu", rel.c_str(), limit));
        }
        Bytes b((size_t)sz.QuadPart);
        DWORD got = 0;
        bool ok = b.empty() || (ReadFile(f, b.data(), (DWORD)b.size(), &got, nullptr) && got == b.size());
        CloseHandle(f);
        if (!ok) throw UpdateError(rel + ": ошибка чтения");
        downloaded += b.size();
        return b;
    }

    Bytes GetRange(const std::string& rel, uint64_t start, uint32_t len) override {
        HANDLE f = Open(rel);
        LARGE_INTEGER pos;
        pos.QuadPart = (LONGLONG)start;
        SetFilePointerEx(f, pos, nullptr, FILE_BEGIN);
        Bytes b(len);
        DWORD got = 0;
        bool ok = ReadFile(f, b.data(), len, &got, nullptr) && got == len;
        CloseHandle(f);
        if (!ok) throw UpdateError(rel + ": короткий диапазон");
        downloaded += len;
        return b;
    }

private:
    // Открыть файл хранилища на чтение
    HANDLE Open(const std::string& rel) {
        std::wstring p = root_ + L"\\" + Widen(rel);
        for (auto& c : p)
            if (c == L'/') c = L'\\';
        HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE) throw UpdateError(rel + ": нет в хранилище");
        return f;
    }

    std::wstring root_;   // корень хранилища
};

// RAII-дескриптор WinHTTP
struct HInternet {
    HINTERNET h = nullptr;
    HInternet() = default;
    explicit HInternet(HINTERNET x) : h(x) {}
    ~HInternet() {
        if (h) WinHttpCloseHandle(h);
    }
    HInternet(const HInternet&) = delete;
    HInternet& operator=(const HInternet&) = delete;
    operator HINTERNET() const { return h; }
};

// Источник HTTPS
class HttpSource final : public Source {
public:
    explicit HttpSource(const std::string& base) {
        std::wstring w = Widen(base);
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        wchar_t host[256]{}, path[1024]{};
        uc.lpszHostName = host;
        uc.dwHostNameLength = 255;
        uc.lpszUrlPath = path;
        uc.dwUrlPathLength = 1023;
        if (!WinHttpCrackUrl(w.c_str(), 0, 0, &uc)) throw UpdateError("источник: неверный адрес");
        host_ = host;
        port_ = uc.nPort;
        secure_ = uc.nScheme == INTERNET_SCHEME_HTTPS;
#if defined(ASDA_LAUNCHER_TEST_BUILD)
        bool localHttp = !secure_ && (host_ == L"127.0.0.1" || host_ == L"localhost");
#else
        bool localHttp = false;
#endif
        if (!secure_ && !localHttp) throw UpdateError("источник должен быть https://");
        prefix_ = path;
        while (!prefix_.empty() && prefix_.back() == L'/') prefix_.pop_back();
        session_.h = WinHttpOpen(L"AsdaLauncher/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                 WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session_) throw UpdateError("WinHTTP: " + LastErrorText(GetLastError()));
        DWORD protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        if (!WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos))) {
            protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;          // TLS 1.3 нет в старых Windows 10
            WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos));
        }
        WinHttpSetTimeouts(session_, 15000, 15000, 30000, 60000);
        connect_.h = WinHttpConnect(session_, host_.c_str(), port_, 0);
        if (!connect_) throw UpdateError("WinHTTP: " + LastErrorText(GetLastError()));
    }

    Bytes Get(const std::string& rel, size_t limit) override {
        return WithRetry(rel, [&] { return Request(rel, nullptr, 0, 0, limit); });
    }

    Bytes GetRange(const std::string& rel, uint64_t start, uint32_t len) override {
        return WithRetry(rel, [&] {
            uint64_t end = start + len - 1;
            std::wstring hdr = Widen(Fmt("Range: bytes=%llu-%llu", (unsigned long long)start, (unsigned long long)end));
            return Request(rel, hdr.c_str(), start, len, len);
        });
    }

private:
    // Сетевой (повторяемый) сбой — отличается от отказа по содержимому
    struct Transient : UpdateError {
        using UpdateError::UpdateError;
    };

    template <class F>
    Bytes WithRetry(const std::string& rel, F f) {
        DWORD delay = kRetryDelayMs;
        for (int i = 0;; ++i) {
            try {
                return f();
            } catch (const Transient& e) {
                if (i + 1 >= kRetries) throw UpdateError(rel + ": сеть: " + e.what());
                Sleep(delay);
                delay *= 2;
            }
        }
    }

    // Один запрос; rangeHdr != null — диапазон (ответ строго 206 с нашим Content-Range)
    Bytes Request(const std::string& rel, const wchar_t* rangeHdr, uint64_t start, uint32_t len, size_t limit) {
        std::wstring path = prefix_ + L"/" + Widen(rel);
        HInternet req(WinHttpOpenRequest(connect_, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, secure_ ? WINHTTP_FLAG_SECURE : 0));
        if (!req) throw Transient(LastErrorText(GetLastError()));
        DWORD noRedirect = WINHTTP_DISABLE_REDIRECTS;                 // переадресации запрещены
        WinHttpSetOption(req, WINHTTP_OPTION_DISABLE_FEATURE, &noRedirect, sizeof(noRedirect));
        std::wstring headers = L"Cache-Control: no-cache\r\n";
        if (rangeHdr) headers += std::wstring(rangeHdr) + L"\r\n";
        if (!WinHttpSendRequest(req, headers.c_str(), (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(req, nullptr))
            throw Transient(LastErrorText(GetLastError()));
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &sz, WINHTTP_NO_HEADER_INDEX);
        if (status >= 500 || status == 429 || status == 408) throw Transient(Fmt("HTTP %lu", status));
        if (rangeHdr) {
            wchar_t cr[128]{};
            DWORD crsz = sizeof(cr);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_RANGE, WINHTTP_HEADER_NAME_BY_INDEX, cr, &crsz,
                                WINHTTP_NO_HEADER_INDEX);
            std::string want = Fmt("bytes %llu-%llu/", (unsigned long long)start, (unsigned long long)(start + len - 1));
            if (status != 206 || Narrow(cr).rfind(want, 0) != 0)
                throw UpdateError(Fmt("%s: сервер не выдал диапазон (код %lu, Content-Range «%s»)", rel.c_str(), status,
                                      Narrow(cr).c_str()));
        } else if (status != 200) {
            throw UpdateError(Fmt("%s: HTTP %lu", rel.c_str(), status));
        }
        // ★ 09.11.2026 Тело — крупными чтениями (kReadChunk) прямо в заранее выделенный буфер. Прежде цикл
        //   WinHttpQueryDataAvailable → WinHttpReadData(avail) читал тем, что уже лежит в буфере WinHTTP (единицы КБ), и
        //   следующий приём из сокета начинался только после нашего вызова: один 16 МиБ диапазон через тот же канал шёл
        //   >5 мин (~0.05 МБ/с), крупными чтениями — 3.5–5 МБ/с, как curl. Игрок видел «0.2 MB/s · ~11 min» при исправной
        //   сети (баг владельца 09.11).
        uint64_t expect = 0;   // ожидаемая длина тела: диапазон — len, файл — Content-Length (если сервер его дал)
        if (rangeHdr) {
            expect = len;
        } else {
            wchar_t cl[32]{};
            DWORD clsz = sizeof(cl);
            if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, cl, &clsz,
                                    WINHTTP_NO_HEADER_INDEX))
                expect = _wcstoui64(cl, nullptr, 10);
            if (expect > limit) throw UpdateError(Fmt("%s больше предела %zu", rel.c_str(), limit));
        }
        Bytes out;
        out.resize((size_t)(expect ? expect : std::min<uint64_t>(limit, kReadChunk)));
        size_t at = 0;
        for (;;) {
            if (at == out.size()) {
                if (rangeHdr) {   // диапазон уже полон: лишнее тело — ошибка длины
                    uint8_t extra = 0;
                    DWORD got = 0;
                    if (!WinHttpReadData(req, &extra, 1, &got)) throw Transient(LastErrorText(GetLastError()));
                    if (got) throw Transient(rel + ": диапазон неверной длины");
                    break;
                }
                if (out.size() >= limit) {   // файл дорос до предела: есть ли ещё тело
                    uint8_t extra = 0;
                    DWORD got = 0;
                    if (!WinHttpReadData(req, &extra, 1, &got)) throw Transient(LastErrorText(GetLastError()));
                    if (got) throw UpdateError(Fmt("%s больше предела %zu", rel.c_str(), limit));
                    break;
                }
                out.resize((size_t)std::min<uint64_t>(limit, (uint64_t)out.size() * 2));
            }
            DWORD want = (DWORD)std::min<size_t>(kReadChunk, out.size() - at), got = 0;
            if (!WinHttpReadData(req, out.data() + at, want, &got)) throw Transient(LastErrorText(GetLastError()));
            if (!got) break;   // конец тела
            at += got;
            downloaded += got;
        }
        out.resize(at);
        if (rangeHdr && out.size() != len) throw Transient(rel + ": диапазон неверной длины");
        if (!rangeHdr && expect && out.size() != expect) throw Transient(rel + ": тело короче Content-Length");
        return out;
    }

    std::wstring host_, prefix_;   // хост и префикс пути (/_selftest в тестах)
    INTERNET_PORT port_ = 443;
    bool secure_ = true;
    HInternet session_, connect_;
};

}  // namespace

std::unique_ptr<Source> MakeHttpSource(const std::string& baseUrl) { return std::make_unique<HttpSource>(baseUrl); }
std::unique_ptr<Source> MakeLocalSource(const std::wstring& root) { return std::make_unique<LocalSource>(root); }

std::unique_ptr<Source> MakeSource(const std::string& s) {
    if (s.find("://") != std::string::npos) return MakeHttpSource(s);
#if defined(ASDA_LAUNCHER_TEST_BUILD)
    return MakeLocalSource(Widen(s));
#else
    throw UpdateError("источник должен быть https://");
#endif
}

}  // namespace asda
