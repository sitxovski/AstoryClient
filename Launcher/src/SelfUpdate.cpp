// Copyright (c) 2026 @sitxovski (Discord)
// SelfUpdate.cpp — самообновление лаунчера (см. SelfUpdate.h).
#include "SelfUpdate.h"

#include <windows.h>

#include "Config.h"
#include "Crypto.h"
#include "FileSys.h"
#include "Launch.h"

namespace asda {

// Имя exe лаунчера в манифесте канала launcher
static const char* kLauncherExe = "AsdaLauncher.exe";

void CleanupOldLauncher() {
    wchar_t self[4096];
    DWORD n = GetModuleFileNameW(nullptr, self, 4096);
    std::wstring old = std::wstring(self, n) + L".old";
    DeleteFileW(old.c_str());
}

SelfUpdateResult SelfUpdate(Source& src, const UpdateOptions& base, const std::wstring& restartArgs) {
    UpdateOptions o = base;
    o.channel = config::kLauncherChannel;
    o.minSeq = config::kMinLauncherSeq;
    o.repair = false;
    std::wstring stage = LauncherDir() + L"\\.asdaupd-launcher";
    try {
        CheckRemote(src, stage, o);
    } catch (const UpdateError& e) {
        // канала нет на сервере → 404 на channels/launcher.json; прочие ошибки — наружу
        std::string m = e.what();
        if (m.find("HTTP 404") != std::string::npos || m.find("нет в хранилище") != std::string::npos)
            return SelfUpdateResult::NoChannel;
        throw;
    }
    Update(src, stage, o);
    std::wstring staged = SafePath(RootPath(stage), kLauncherExe);
    wchar_t selfBuf[4096];
    DWORD n = GetModuleFileNameW(nullptr, selfBuf, 4096);
    std::wstring self(selfBuf, n);
    if (Sha256FileHex(staged) == Sha256FileHex(self)) return SelfUpdateResult::UpToDate;
    std::wstring old = self + L".old";
    DeleteFileW(old.c_str());
    if (!MoveFileExW(self.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING))
        throw UpdateError("самообновление: не переименован запущенный лаунчер: " + LastErrorText(GetLastError()));
    if (!CopyFileW(staged.c_str(), self.c_str(), FALSE)) {
        DWORD e = GetLastError();
        MoveFileExW(old.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING);   // вернуть как было
        throw UpdateError("самообновление: новый лаунчер не скопирован: " + LastErrorText(e));
    }
    if (Sha256FileHex(self) != Sha256FileHex(staged)) {
        MoveFileExW(old.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING);
        throw UpdateError("самообновление: копия не совпала");
    }
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + self + L"\" " + restartArgs;
    if (!CreateProcessW(self.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, LauncherDir().c_str(), &si, &pi)) {
        DWORD e = GetLastError();
        MoveFileExW(old.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING);   // откат: работаем прежней версией
        throw UpdateError("самообновление: новый лаунчер не запустился: " + LastErrorText(e));
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return SelfUpdateResult::Restarting;
}

}  // namespace asda
