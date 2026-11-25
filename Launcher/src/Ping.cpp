// Copyright (c) 2026 @sitxovski (Discord)
// Ping.cpp — фоновый опрос GET /status (см. Ping.h).
#include "Ping.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <deque>

#include "Config.h"

namespace asda {

namespace {

// Соединение считается «остывшим» (Caddy закрывает простаивающее через 30 с), если удачного ответа не было дольше
constexpr ULONGLONG kWarmMs = 20000;
// Сколько неудач подряд нужно, чтобы сменить «Онлайн» на «Офлайн» (одиночная потеря пакета не мигает статусом)
constexpr int kFailsToOffline = 2;
// Сколько последних замеров сглаживается (медиана): одиночный всплеск не красит пинг красным
constexpr size_t kPingWindow = 3;
// Предел тела ответа /status, байт
constexpr size_t kBodyLimit = 512;

// Имя состояния для журнала
const char* StateName(ServerState s) {
    return s == ServerState::Online ? "онлайн" : s == ServerState::Offline ? "офлайн" : "проверка";
}

}  // namespace

ServerProbe::ServerProbe(std::function<void(const ServerStatus&)> update, std::function<void(const std::string&)> log)
    : update_(std::move(update)), log_(std::move(log)) {}

ServerProbe::~ServerProbe() {
    RequestStop();
    if (thread_.joinable()) thread_.join();   // идущий запрос уже прерван — поток выходит сразу
}

void ServerProbe::Start() {
    if (thread_.joinable()) return;
    stop_ = false;
    done_ = false;
    thread_ = std::thread([this] {
        Loop();
        done_ = true;
    });
}

void ServerProbe::SetPaused(bool paused) {
    {
        std::lock_guard<std::mutex> lk(m_);
        paused_ = paused;
    }
    cv_.notify_all();   // снятие паузы будит поток — опрос сразу, без ожидания периода
}

void ServerProbe::RequestStop() {
    {
        std::lock_guard<std::mutex> lk(m_);
        stop_ = true;
        if (req_) {   // синхронный запрос WinHTTP прерывается закрытием его дескриптора
            WinHttpCloseHandle((HINTERNET)req_);
            req_ = nullptr;
        }
    }
    cv_.notify_all();
}

void ServerProbe::JoinIfDone() {
    if (thread_.joinable() && done_) thread_.join();
}

bool ServerProbe::Wait(unsigned ms) {
    std::unique_lock<std::mutex> lk(m_);
    bool wasPaused = paused_;
    cv_.wait_for(lk, std::chrono::milliseconds(ms), [&] { return stop_.load() || paused_.load() != wasPaused; });
    return !stop_;
}

bool ServerProbe::Query(int& code, bool& online, int& ms) {
    code = 0;
    online = false;
    ms = -1;
    if (!session_) {
        HINTERNET s = WinHttpOpen(L"AsdaLauncher/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
        if (!s) return false;
        DWORD protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        if (!WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos))) {
            protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;   // TLS 1.3 нет в старых Windows 10
            WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos));
        }
        DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(s, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));
        const int t = (int)config::kStatusTimeoutMs;
        WinHttpSetTimeouts(s, t, t, t, t);
        HINTERNET c = WinHttpConnect(s, config::kStatusHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!c) {
            WinHttpCloseHandle(s);
            return false;
        }
        session_ = s;
        connect_ = c;
    }
    HINTERNET r = WinHttpOpenRequest((HINTERNET)connect_, L"GET", config::kStatusPath, nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH);
    if (!r) return false;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (stop_) {
            WinHttpCloseHandle(r);
            return false;
        }
        req_ = r;
    }
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    bool ok = WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(r, nullptr);
    QueryPerformanceCounter(&t1);
    if (ok) {
        DWORD st = 0, sz = sizeof(st);
        ok = WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &st,
                                 &sz, WINHTTP_NO_HEADER_INDEX);
        code = (int)st;
    }
    std::string body;
    // тело дочитывается целиком: только тогда WinHTTP вернёт соединение в пул (keep-alive)
    while (ok) {
        char buf[256];
        DWORD got = 0;
        if (!WinHttpReadData(r, buf, sizeof(buf), &got)) {
            ok = false;
            break;
        }
        if (!got) break;
        if (body.size() < kBodyLimit) body.append(buf, std::min<size_t>(got, kBodyLimit - body.size()));
    }
    {
        std::lock_guard<std::mutex> lk(m_);
        if (req_ == r) {   // иначе дескриптор уже закрыл RequestStop
            WinHttpCloseHandle(r);
            req_ = nullptr;
        }
    }
    if (!ok) return false;
    ms = (int)((t1.QuadPart - t0.QuadPart) * 1000 / f.QuadPart);
    online = code == 200 && body.find("\"online\":true") != std::string::npos;
    return true;
}

void ServerProbe::Loop() {
    ServerState shown = ServerState::Checking;   // последнее показанное состояние
    int shownPing = -1;                          // последний показанный пинг
    int fails = 0;                               // неудач подряд
    ULONGLONG lastOk = 0;                        // время последнего ответа (тики), 0 — соединение «холодное»
    std::deque<int> samples;                     // последние замеры для медианы
    while (!stop_) {
        if (paused_) {   // окно свёрнуто: не опрашиваем вовсе
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [&] { return stop_.load() || !paused_.load(); });
            continue;
        }
        ULONGLONG t0 = GetTickCount64();
        bool cold = !lastOk || t0 - lastOk > kWarmMs;
        int code = 0, ms = -1;
        bool online = false;
        bool ok = Query(code, online, ms);
        if (stop_) break;
        ServerState st;
        int ping = shownPing;
        if (ok) {
            fails = 0;
            lastOk = GetTickCount64();
            st = online ? ServerState::Online : ServerState::Offline;
            if (cold) {
                samples.clear();   // замер с DNS/TCP/TLS — не пинг; следующий запрос пойдёт по открытому соединению
            } else {
                samples.push_back(ms);
                if (samples.size() > kPingWindow) samples.pop_front();
                std::deque<int> sorted = samples;
                std::sort(sorted.begin(), sorted.end());
                ping = sorted[sorted.size() / 2];
            }
        } else {
            lastOk = 0;
            samples.clear();
            ++fails;
            if (shown == ServerState::Online && fails < kFailsToOffline) {
                if (!Wait(config::kStatusIntervalMs)) break;   // одна потеря — не повод мигать «Офлайн»
                continue;
            }
            st = ServerState::Offline;
            ping = -1;
        }
        if (st != shown && log_)
            log_(std::string("сервер: ") + StateName(st) + (ok ? " (HTTP " + std::to_string(code) + ")" : " (нет ответа)"));
        shown = st;
        shownPing = ping;
        if (update_) update_(ServerStatus{st, ping});
        if (ok && cold) {   // сразу второй запрос — уже по тёплому соединению, ради настоящего пинга
            if (!Wait(150)) break;
            continue;
        }
        ULONGLONG spent = GetTickCount64() - t0;
        if (!Wait(spent >= config::kStatusIntervalMs ? 50 : (unsigned)(config::kStatusIntervalMs - spent))) break;
    }
    if (connect_) WinHttpCloseHandle((HINTERNET)connect_);
    if (session_) WinHttpCloseHandle((HINTERNET)session_);
    connect_ = session_ = nullptr;
}

}  // namespace asda
