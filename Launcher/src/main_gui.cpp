// Copyright (c) 2026 @sitxovski (Discord)
// main_gui.cpp — точка входа оконного лаунчера AsdaLauncher.exe.
//   AsdaLauncher.exe                                  — обычный запуск
//   AsdaLauncher.exe --after-selfupdate               — после самообновления (не проверять себя повторно)
//   AsdaLauncher.exe --render-preview <out.png> <starting|updating|ready|error> [ru|en] [online:<мс>|offline|checking]
//                                                     — нарисовать окно в PNG без сети (проверка вида)
#include <windows.h>
#include <shellapi.h>

#include <string>

#include "SelfUpdate.h"
#include "Ui.h"

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 4 && std::wstring(argv[1]) == L"--render-preview")
        return asda::RenderPreview(inst, argv[2], argv[3], argc >= 5 ? argv[4] : L"ru", argc >= 6 ? argv[5] : L"");
    // один экземпляр: второй запуск поднимает окно первого
    // после самообновления / перезапуска от администратора прежний экземпляр ещё закрывается — ждём его до 5 с
    bool after = std::wstring(cmdLine).find(L"--after-selfupdate") != std::wstring::npos ||
                 std::wstring(cmdLine).find(L"--elevated-restart") != std::wstring::npos;
    HANDLE mtx = nullptr;
    DWORD me = 0;
    const int tries = after ? 50 : 1;
    for (int i = 0; i < tries; ++i) {
        if (mtx) CloseHandle(mtx);
        mtx = CreateMutexW(nullptr, TRUE, L"Local\\AStoryRemake.Launcher");
        me = GetLastError();                       // ACCESS_DENIED — мьютекс экземпляра с другими правами
        if (mtx && me != ERROR_ALREADY_EXISTS) break;
        if (i + 1 < tries) Sleep(100);
    }
    if (!mtx || me == ERROR_ALREADY_EXISTS || me == ERROR_ACCESS_DENIED) {
        HWND w = FindWindowW(L"AsdaLauncherWindow", nullptr);
        if (w) {
            ShowWindow(w, SW_RESTORE);
            SetForegroundWindow(w);
        }
        return 0;
    }
    asda::CleanupOldLauncher();
    int rc = asda::RunLauncherWindow(inst, cmdLine);
    if (mtx) {
        ReleaseMutex(mtx);
        CloseHandle(mtx);
    }
    LocalFree(argv);
    return rc;
}
