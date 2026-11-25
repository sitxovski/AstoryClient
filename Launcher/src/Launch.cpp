// Copyright (c) 2026 @sitxovski (Discord)
// Launch.cpp — папки, служба EAC, запуск игры (см. Launch.h).
#include "Launch.h"

#undef WIN32_LEAN_AND_MEAN  // exdisp.h/shldisp.h (запуск через проводник) требуют OLE-заголовков windows.h

#include <windows.h>
#include <exdisp.h>
#include <shellapi.h>
#include <shldisp.h>
#include <shlobj.h>
#include <softpub.h>
#include <tlhelp32.h>
#include <wintrust.h>

#include <rapidjson/document.h>

#include "Config.h"
#include "FileSys.h"
#include "Format.h"
#include "Updater.h"

#pragma comment(lib, "wintrust.lib")

namespace asda {

std::wstring LauncherDir() {
    wchar_t buf[4096];
    DWORD n = GetModuleFileNameW(nullptr, buf, 4096);
    std::wstring p(buf, n);
    return p.substr(0, p.rfind(L'\\'));
}

std::wstring GameDir() { return LauncherDir() + L"\\" + config::kGameSubdir; }

bool IsWritableDir(const std::wstring& dir) {
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring probe = dir + L"\\.asdaupd-write-probe";
    HANDLE f = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    CloseHandle(f);
    return true;
}

bool EacServiceInstalled() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, config::kEacService, SERVICE_QUERY_STATUS);
    bool ok = svc != nullptr;
    if (svc) CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

// Подпись Authenticode файла действительна (WinVerifyTrust, без UI, с проверкой отзыва по цепочке)
static bool AuthenticodeOk(const std::wstring& file) {
    WINTRUST_FILE_INFO fi{sizeof(fi)};
    fi.pcwszFilePath = file.c_str();
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA wd{sizeof(wd)};
    wd.dwUIChoice = WTD_UI_NONE;
    wd.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    wd.dwUnionChoice = WTD_CHOICE_FILE;
    wd.pFile = &fi;
    wd.dwStateAction = WTD_STATEACTION_VERIFY;
    LONG r = WinVerifyTrust(nullptr, &action, &wd);
    wd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &wd);
    return r == ERROR_SUCCESS;
}

bool GameRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{sizeof(pe)};
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
        found = _wcsicmp(pe.szExeFile, L"AsdaReborn-Win64-Shipping.exe") == 0 || _wcsicmp(pe.szExeFile, L"Astory.exe") == 0;
    CloseHandle(snap);
    return found;
}

bool IsElevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION e{};
    DWORD sz = 0;
    BOOL ok = GetTokenInformation(tok, TokenElevation, &e, sizeof(e), &sz);
    CloseHandle(tok);
    return ok && e.TokenIsElevated;
}

// Запуск от имени обычного пользователя из повышенного процесса: через проводник (IShellDispatch2::ShellExecute
// в процессе explorer.exe — тот работает без повышения). Ревизия В4: игра не должна наследовать права администратора.
static void ShellExecuteAsUser(const std::wstring& exe, const std::wstring& dir) {
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellWindows* sw = nullptr;
    IDispatch* disp = nullptr;
    IServiceProvider* sp = nullptr;
    IShellBrowser* sb = nullptr;
    IShellView* sv = nullptr;
    IDispatch* bg = nullptr;
    IShellFolderViewDual* fv = nullptr;
    IDispatch* app = nullptr;
    IShellDispatch2* sd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&sw));
    VARIANT loc{}, empty{};
    loc.vt = VT_I4;
    loc.lVal = CSIDL_DESKTOP;
    long hwnd = 0;
    if (SUCCEEDED(hr)) hr = sw->FindWindowSW(&loc, &empty, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &disp);
    if (SUCCEEDED(hr) && disp) hr = disp->QueryInterface(IID_PPV_ARGS(&sp)); else if (SUCCEEDED(hr)) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = sp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&sb));
    if (SUCCEEDED(hr)) hr = sb->QueryActiveShellView(&sv);
    if (SUCCEEDED(hr)) hr = sv->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&bg));
    if (SUCCEEDED(hr)) hr = bg->QueryInterface(IID_PPV_ARGS(&fv));
    if (SUCCEEDED(hr)) hr = fv->get_Application(&app);
    if (SUCCEEDED(hr)) hr = app->QueryInterface(IID_PPV_ARGS(&sd));
    if (SUCCEEDED(hr)) {
        BSTR file = SysAllocString(exe.c_str()), wd = SysAllocString(dir.c_str());
        VARIANT vArgs{}, vDir{}, vOp{}, vShow{};
        vDir.vt = VT_BSTR;
        vDir.bstrVal = wd;
        vShow.vt = VT_I4;
        vShow.lVal = SW_SHOWNORMAL;
        hr = sd->ShellExecute(file, vArgs, vDir, vOp, vShow);
        SysFreeString(file);
        SysFreeString(wd);
    }
    for (IUnknown* u : {(IUnknown*)sd, (IUnknown*)app, (IUnknown*)fv, (IUnknown*)bg, (IUnknown*)sv, (IUnknown*)sb,
                        (IUnknown*)sp, (IUnknown*)disp, (IUnknown*)sw})
        if (u) u->Release();
    if (SUCCEEDED(hrInit)) CoUninitialize();
    if (FAILED(hr)) throw UpdateError(Fmt("игра не запустилась от имени пользователя (0x%08lx) — запустите лаунчер без прав администратора", (unsigned long)hr));
}

void InstallEacService(const std::wstring& gameDir) {
    Bytes js;
    if (!ReadAll(gameDir + L"\\EasyAntiCheat\\Settings.json", js, 1 << 20))
        throw UpdateError("нет EasyAntiCheat\\Settings.json — проверьте файлы игры");
    rapidjson::Document d;
    d.Parse((const char*)js.data(), js.size());
    std::string pid = d.HasParseError() ? "" : JStr(d, "productid");
    if (pid.size() != 32) throw UpdateError("Settings.json: нет productid");
    for (char c : pid)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) throw UpdateError("Settings.json: неверный productid");
    std::wstring exe = gameDir + L"\\EasyAntiCheat\\EasyAntiCheat_EOS_Setup.exe";
    // ревизия В4: перед повышением прав — файл ровно тот, что в подписанном манифесте, и подписан издателем
    std::string want;
    if (!InstalledFileSha(gameDir, "EasyAntiCheat/EasyAntiCheat_EOS_Setup.exe", want) || Sha256FileHex(exe) != want)
        throw UpdateError("установщик Easy Anti-Cheat не совпадает с подписанным манифестом — «Проверить файлы»");
    if (!AuthenticodeOk(exe)) throw UpdateError("установщик Easy Anti-Cheat: недействительная цифровая подпись");
    std::wstring args = L"install " + Widen(pid);
    std::wstring setupDir = gameDir + L"\\EasyAntiCheat";
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = args.c_str();
    sei.lpDirectory = setupDir.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        DWORD e = GetLastError();
        if (e == ERROR_CANCELLED) throw UpdateError("установка Easy Anti-Cheat отменена (нужны права администратора)");
        throw UpdateError("установщик Easy Anti-Cheat не запустился: " + LastErrorText(e));
    }
    WaitForSingleObject(sei.hProcess, 5 * 60 * 1000);
    DWORD code = 1;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    if (code != 0 || !EacServiceInstalled())
        throw UpdateError(Fmt("установка Easy Anti-Cheat не удалась (код %lu); журнал — %%AppData%%\\EasyAntiCheat", code));
}

void StartGame(const std::wstring& gameDir) {
    std::wstring exe = gameDir + L"\\" + config::kBootstrapper;
    if (!IsRegularFile(exe)) throw UpdateError("нет Astory.exe — запустите «Проверить файлы»");
    if (IsElevated()) {                                     // ревизия В4: игра — без прав администратора
        ShellExecuteAsUser(exe, gameDir);
        return;
    }
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\"";
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, gameDir.c_str(), &si, &pi))
        throw UpdateError("игра не запустилась: " + LastErrorText(GetLastError()));
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

bool RelaunchElevated(const std::wstring& args) {
    wchar_t self[4096];
    GetModuleFileNameW(nullptr, self, 4096);
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.lpVerb = L"runas";
    sei.lpFile = self;
    sei.lpParameters = args.c_str();
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

}  // namespace asda
