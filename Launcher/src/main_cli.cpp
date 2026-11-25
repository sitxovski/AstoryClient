// Copyright (c) 2026 @sitxovski (Discord)
// main_cli.cpp — консольный клиент обновлений ДЛЯ ТЕСТОВ (AsdaLauncherCli.exe; в раздачу не входит).
// Позволяет задать источник (папка или URL) и корневой ключ — поэтому собирается только с ASDA_LAUNCHER_TEST_BUILD.
// Используется сквозным самотестом: python Tools/asda_update_selftest.py --client <путь к AsdaLauncherCli.exe>
//
//   AsdaLauncherCli update|verify --source <папка|https://…> --dir <папка игры> --root-pub <hex>
//                   [--channel alpha] [--min-keyring-seq N] [--min-seq N]
//   AsdaLauncherCli status --dir <папка игры>          — «ready»/«not-ready» (код 0/3)
//   AsdaLauncherCli server-ping [--seconds N]          — ★ 17.10.2026 опрос /status как в окне (Ping.h): строка
//                   «PING <время мс> <online|offline|checking> <пинг мс>» на каждый результат; в середине — пауза 5 с
//                   (как свёрнутое окно), в конце — время прерывания RequestStop. Код 0 — был хоть один «online».
//   AsdaLauncherCli selfupdate --source … --root-pub … --restart-args "<аргументы нового exe>"
//                   — самообновление ЭТОГО exe по каналу launcher (как в окне); итог «SELFUPDATE <результат>».
//                     В тестах новый exe перезапускается с «--render-preview <png> ready» — без окна.
//   AsdaLauncherCli log-upload [--saved <папка Saved>] [--install-id-file <файл>] [--endpoint <https://@@ASTORY_AUTH_HOST@@/…|
//                   http://127.0.0.1:…/…>] — ★ 19.10.2026 один проход выгрузки журналов (LogUpload.h) как в окне;
//                   строки «LOG …» и итог «LOGUPLOAD <nothing|uploaded|rejected|retry|throttled|game-running|stopped> requests=N
//                   uploaded=N rejected=N local=N retry=N expired=N throttled=0|1», код 0; [--max-per-hour N] — свой лимит;
//                   [--pin <hex SPKI>]… — свои пины корня (проверка отказа пиннинга).
// Итог успеха — строка JSON «STATS {…}», отказ — «ОТКАЗ: <текст>» и код 1. Вывод — UTF-8.
#include <windows.h>

#include <cstdio>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>

#include "Format.h"
#include "SelfUpdate.h"
#include "Http.h"
#include "LogUpload.h"
#include "Ping.h"
#include "State.h"
#include "Updater.h"

using namespace asda;

// Вывод строки UTF-8 в stdout
static void Out(const std::string& s) {
    std::string line = s + "\n";
    DWORD w = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line.data(), (DWORD)line.size(), &w, nullptr);
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 2) {
        Out("использование: update|verify|status --source … --dir … --root-pub …");
        return 2;
    }
    std::string cmd = Narrow(argv[1]);
    std::string source, dir, rootHex, channel = "alpha", restartArgs;
    long long minK = 0, minS = 0;
    for (int i = 2; i + 1 < argc; i += 2) {
        std::string k = Narrow(argv[i]), v = Narrow(argv[i + 1]);
        if (k == "--source") source = v;
        else if (k == "--dir") dir = v;
        else if (k == "--root-pub") rootHex = v;
        else if (k == "--channel") channel = v;
        else if (k == "--min-keyring-seq") minK = _atoi64(v.c_str());
        else if (k == "--min-seq") minS = _atoi64(v.c_str());
        else if (k == "--restart-args") restartArgs = v;
    }
    try {
        if (cmd == "server-ping") {
            int seconds = 12;
            for (int i = 2; i + 1 < argc; i += 2)
                if (Narrow(argv[i]) == "--seconds") seconds = std::max(4, _wtoi(argv[i + 1]));
            ULONGLONG start = GetTickCount64();
            std::atomic<bool> any{false};
            ServerProbe probe(
                [&](const ServerStatus& st) {
                    const char* n = st.state == ServerState::Online ? "online" : st.state == ServerState::Offline ? "offline" : "checking";
                    if (st.state == ServerState::Online) any = true;
                    Out(Fmt("PING %llu %s %d", (unsigned long long)(GetTickCount64() - start), n, st.pingMs));
                },
                [](const std::string& l) { Out("LOG " + l); });
            probe.Start();
            int half = seconds * 1000 / 2;
            Sleep(half);
            Out("PAUSE");
            probe.SetPaused(true);
            Sleep(5000);
            Out("RESUME");
            probe.SetPaused(false);
            Sleep(seconds * 1000 - half);
            ULONGLONG t0 = GetTickCount64();
            probe.RequestStop();
            while (!probe.Done() && GetTickCount64() - t0 < 10000) Sleep(5);
            Out(Fmt("STOP %llu ms", (unsigned long long)(GetTickCount64() - t0)));
            probe.JoinIfDone();
            return any ? 0 : 1;
        }
        if (cmd == "log-names-selftest") {   // ★ 31.10.2026: разбор имён частей журнала и привязка падения по pid
            return LogUploadNameSelfTest([](const std::string& l) { Out(l); }) ? 1 : 0;
        }
        if (cmd == "log-upload") {
            LogUploadOptions lo = DefaultLogUploadOptions([](const std::string& l) { Out("LOG " + l); });
            for (int i = 2; i + 1 < argc; i += 2) {
                std::wstring k = argv[i];
                if (k == L"--saved") lo.savedDir = argv[i + 1];
                else if (k == L"--install-id-file") lo.installIdFile = argv[i + 1];
                else if (k == L"--endpoint") lo.endpoint = Narrow(argv[i + 1]);
                else if (k == L"--max-per-hour") lo.maxPerHour = _wtoi(argv[i + 1]);
                else if (k == L"--pin") lo.pinsOverride.push_back(Narrow(argv[i + 1]));
            }
            LogUploader up;
            LogUploadStats st = up.RunNow(lo);
            Out(Fmt("LOGUPLOAD %s requests=%d uploaded=%d rejected=%d local=%d retry=%d expired=%d throttled=%d",
                    LogUploadResultName(st.result), st.requests, st.uploaded, st.rejected, st.rejectedLocal, st.retry,
                    st.expired, st.throttled ? 1 : 0));
            return 0;
        }
        if (cmd == "status") {
            bool ready = IsReady(Widen(dir));
            Out(ready ? "ready" : "not-ready");
            return ready ? 0 : 3;
        }
        if (cmd != "update" && cmd != "verify" && cmd != "selfupdate") throw UpdateError("неизвестная команда " + cmd);
        Bytes root;
        if (!FromHex(rootHex, root) || root.size() != 32) throw UpdateError("корневой ключ — 32 байта");
        UpdateOptions o;
        std::memcpy(o.rootPub.data(), root.data(), 32);
        o.channel = channel;
        o.minKeyringSeq = minK;
        o.minSeq = minS;
        o.repair = cmd == "verify";
        o.log = [](const std::string& s) { Out("LOG " + s); };
        auto src = MakeSource(source);
        if (cmd == "selfupdate") {
            SelfUpdateResult r = SelfUpdate(*src, o, Widen(restartArgs));
            Out(std::string("SELFUPDATE ") + (r == SelfUpdateResult::Restarting ? "restarting" : r == SelfUpdateResult::UpToDate ? "uptodate" : "nochannel"));
            return 0;
        }
        UpdateStats s = Update(*src, Widen(dir), o);
        Out(Fmt("STATS {\"version\":\"%s\",\"seq\":%lld,\"files\":%llu,\"changed\":%llu,\"local\":%llu,\"net_payload\":%llu,"
                "\"deleted\":%llu,\"downloaded\":%llu}",
                s.version.c_str(), (long long)s.seq, (unsigned long long)s.files, (unsigned long long)s.changed,
                (unsigned long long)s.local, (unsigned long long)s.netPayload, (unsigned long long)s.deleted,
                (unsigned long long)s.downloaded));
        return 0;
    } catch (const std::exception& e) {
        Out(std::string("ОТКАЗ: ") + e.what());
        return 1;
    }
}
