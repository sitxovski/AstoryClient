// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSetupBoot.cpp — обёртка установщика AStory Remake и помощник удаления (★ 25.10.2026 [ИСПРАВЛЕНИЕ] «Error 5» на
// машинах, где запуск программ из %TEMP% запрещён: AppLocker/SRP или NTFS «запретить выполнение»).
//
// Почему: загрузчик Inno Setup (UseSetupLdr=yes) распаковывал setup в %TEMP%\is-XXXXX.tmp и запускал его оттуда —
// на таких машинах «Невозможно выполнить файл во временном каталоге. Ошибка 5: Отказано в доступе»; деинсталлятор Inno
// так же копирует себя в %TEMP% (iu-*.tmp\_unins.tmp) и запускает копию. Разбор: Reports\launcher_temp\REPORT.md,
// ревизия REVIEW.md (вторая волна 25.10 — H1, M2–M4, L1–L3 закрыты здесь).
//
// Один exe, режим выбирается ЯВНО (ревизия М3):
//   * УСТАНОВКА — в хвосте есть нагрузка (AStoryRemake_Setup_<версия>.exe = этот exe + файлы Inno без загрузчика,
//     UseSetupLdr=no, приклеивает deploy.py pack_setup_boot; хвост ищется ПЕРЕД подписью Authenticode). Файлы
//     разворачиваются в папку, где запуск РАЗРЕШЁН (пробный запуск в приостановленном состоянии), держатся открытыми
//     без права записи и удаления до конца установки, setup.exe запускается оттуда; код возврата — его. Повышение прав
//     делает сам Setup (перезапуск себя из той же папки), поэтому «от имени исходного пользователя» работает как раньше.
//   * УДАЛЕНИЕ — exe называется AStoryUninstall.exe и нагрузки нет (лежит в {app}\Setup, на него указывает
//     UninstallString). Стадии:
//       0) запуск без прав в админской установке (папка {app}\Setup закрыта на запись) → сам себя повышает (runas,
//          /ASDA-ELEV <pid>) и ждёт; через runas окружение не передаётся (ревизия Н1), поэтому TEMP ставим уже после;
//       E) повышенный (или установка без прав администратора): закрытая рабочая папка W (только Администраторы и SYSTEM),
//          копия себя туда, TEMP/TMP → W, запуск копии (стадия 1), ждёт её код по каналу и выходит с ним;
//       1) копия в W: запускает unins000.exe (Inno видит права администратора — перезапуска нет, TEMP наследуется,
//          _unins.tmp появляется в W), ждёт его и отдаёт код стадии E; дожидается выхода стадий 0/E (их образ лежит в
//          {app}\Setup — пока они живы, его не удалить), удаляет AStoryUninstall.exe, ждёт конца второй фазы Inno,
//          чистит W, а пустые {app}\Setup и {app} и саму W убирает отложенный cmd.exe (System32) после своего выхода.
//   * иначе (нагрузки нет, имя другое: недокачанный/обрезанный установщик) — «скачайте заново».
// Если разрешённой папки нет — сообщение RU/EN по фактической причине (запрет запуска или иная ошибка).
// Сборка: build_boot.bat (MSVC, статический CRT — рантайма VC++ на машине игрока ещё нет; задержанная загрузка всех DLL
// кроме kernel32 и поиск DLL только в System32 — подмена DLL рядом с exe не работает, ревизия М4).

#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace {

// Признак полезной нагрузки в конце файла (16 байт)
const char kMagic[16] = {'A', 'S', 'D', 'A', 'S', 'E', 'T', 'U', 'P', 'B', 'O', 'O', 'T', '0', '1', '\0'};
// Имя помощника удаления в {app}\Setup (по нему включается режим удаления)
const wchar_t kUninstallerName[] = L"AStoryUninstall.exe";
// Служебные ключи стадий удаления (всегда первыми в командной строке, дальше не передаются)
const wchar_t kElevFlag[] = L"/ASDA-ELEV";
const wchar_t kStage1Flag[] = L"/ASDA-U1";
// Код выхода «не удалось» (как Inno: 1 — Setup failed to initialize; 5 у Inno — «отменено пользователем»)
const int kExitFail = 1;

// Один файл нагрузки: имя и место в exe
struct PayloadFile {
    std::wstring name;   // имя файла (без пути)
    uint64_t offset;     // смещение от начала exe
    uint64_t size;       // размер в байтах
};

// Ошибки CreateProcess, означающие «запуск из этой папки запрещён» (права NTFS, SRP, AppLocker, WDAC, SmartScreen-политики)
bool IsExecBlocked(DWORD e) {
    return e == ERROR_ACCESS_DENIED || e == ERROR_ACCESS_DISABLED_BY_POLICY || e == ERROR_ACCESS_DISABLED_NO_SAFER_UI_BY_POLICY ||
           e == ERROR_APPHELP_BLOCK || e == ERROR_VIRUS_INFECTED || e == ERROR_VIRUS_DELETED ||
           e == 4551 /* ERROR_SYSTEM_INTEGRITY_POLICY_VIOLATION */ || e == 4580 /* ERROR_SYSTEM_INTEGRITY_REPUTATION_MALICIOUS */;
}

// Русский ли интерфейс Windows (иначе сообщения — по-английски)
bool RuUi() { return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN; }

// Путь exe этого процесса
std::wstring SelfPath() {
    std::wstring buf(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
    buf.resize(n);
    return buf;
}

// Папка файла (без завершающей «\»)
std::wstring DirOf(const std::wstring& p) {
    size_t i = p.find_last_of(L"\\/");
    return i == std::wstring::npos ? L"." : p.substr(0, i);
}

// Имя файла без папки
std::wstring NameOf(const std::wstring& p) {
    size_t i = p.find_last_of(L"\\/");
    return i == std::wstring::npos ? p : p.substr(i + 1);
}

// Известная папка оболочки (LocalAppData, ProgramData); пусто, если нет
std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p)) && p) r = p;
    CoTaskMemFree(p);
    return r;
}

// Пропустить в командной строке n слов (правила кавычек как у CRT для простых случаев); остаток — как есть
const wchar_t* SkipWords(const wchar_t* c, int n) {
    for (int i = 0; i < n && *c; ++i) {
        while (*c == L' ' || *c == L'\t') ++c;
        bool q = false;
        while (*c && (q || (*c != L' ' && *c != L'\t'))) {
            if (*c == L'"') q = !q;
            ++c;
        }
    }
    while (*c == L' ' || *c == L'\t') ++c;
    return c;
}

// Слова командной строки (без имени exe)
std::vector<std::wstring> ArgWords() {
    std::vector<std::wstring> r;
    int n = 0;
    LPWSTR* a = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 1; a && i < n; ++i) r.push_back(a[i]);
    if (a) LocalFree(a);
    return r;
}

// Есть ли в аргументах ключ (без учёта регистра, целым словом: /SILENT не путается с /VERYSILENT)
bool HasSwitch(const std::wstring& args, const wchar_t* sw) {
    size_t n = wcslen(sw);
    for (size_t i = 0; i + n <= args.size(); ++i) {
        if ((i == 0 || args[i - 1] == L' ' || args[i - 1] == L'\t' || args[i - 1] == L'"') &&
            _wcsnicmp(args.c_str() + i, sw, n) == 0 &&
            (i + n == args.size() || args[i + n] == L' ' || args[i + n] == L'\t' || args[i + n] == L'"' || args[i + n] == L'='))
            return true;
    }
    return false;
}

// Тихий режим: сообщений не показывать (как Inno при /SILENT /VERYSILENT /SUPPRESSMSGBOXES)
bool Quiet(const std::wstring& args) {
    return HasSwitch(args, L"/SILENT") || HasSwitch(args, L"/VERYSILENT") || HasSwitch(args, L"/SUPPRESSMSGBOXES");
}

// Сообщение об ошибке (в тихом режиме — нет)
void ErrorBox(const std::wstring& args, const std::wstring& ru, const std::wstring& en) {
    if (Quiet(args)) return;
    MessageBoxW(nullptr, (RuUi() ? ru : en).c_str(), L"AStory Remake", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

// Текст системной ошибки на языке интерфейса: «Отказано в доступе (код 5)»
std::wstring ErrText(DWORD e) {
    wchar_t* m = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e, 0,
                   (LPWSTR)&m, 0, nullptr);
    std::wstring r = m ? m : L"";
    LocalFree(m);
    while (!r.empty() && (r.back() == L'\n' || r.back() == L'\r' || r.back() == L' ' || r.back() == L'.')) r.pop_back();
    if (r.empty()) r = RuUi() ? L"неизвестная ошибка" : L"unknown error";
    wchar_t code[32];
    swprintf_s(code, RuUi() ? L" (код %lu)" : L" (code %lu)", e);
    return r + code;
}

// Процесс запущен с повышенными правами
bool IsElevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION e{};
    DWORD sz = 0;
    BOOL ok = GetTokenInformation(tok, TokenElevation, &e, sizeof(e), &sz);
    CloseHandle(tok);
    return ok && e.TokenIsElevated;
}

// Можно ли этому процессу создавать файлы в папке (админская установка: {app}\Setup закрыта для обычных прав)
bool IsWritableDir(const std::wstring& dir) {
    wchar_t name[64];
    swprintf_s(name, L"\\.asda-write-probe-%lu", GetCurrentProcessId());
    HANDLE f = CreateFileW((dir + name).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    CloseHandle(f);
    return true;
}

// Случайный суффикс имени (8 байт из системного ГСЧ) — имя рабочей папки нельзя угадать заранее
std::wstring RandomHex() {
    unsigned char b[8] = {};
    BCryptGenRandom(nullptr, b, sizeof(b), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    wchar_t s[17];
    for (int i = 0; i < 8; ++i) swprintf_s(s + i * 2, 3, L"%02x", b[i]);
    return s;
}

// Жив ли процесс с таким pid (рабочие папки называются <s|u>-<pid>-…; папку живого экземпляра не трогаем — ревизия L1)
bool PidAlive(DWORD pid) {
    if (pid == 0 || pid == GetCurrentProcessId()) return pid != 0;
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h) return GetLastError() == ERROR_ACCESS_DENIED;   // чужой защищённый процесс — считаем живым
    bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}

// Удалить папку со всем содержимым (лучшее усилие: занятые файлы остаются). Ссылки (reparse) удаляются сами, внутрь не идём.
void DeleteTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            std::wstring p = dir + L"\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(p.c_str()); else DeleteFileW(p.c_str());
            } else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                DeleteTree(p);
            } else {
                SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(p.c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

// Убрать остатки прошлых запусков: папки <prefix>-<pid>-… в корне, чей процесс уже завершился
void CleanupStale(const std::wstring& base, wchar_t prefix) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((base + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = fd.cFileName;
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
            n.size() > 2 && n[0] == prefix && n[1] == L'-' && !PidAlive((DWORD)wcstoul(n.c_str() + 2, nullptr, 10)))
            DeleteTree(base + L"\\" + n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// Создать путь целиком (как mkdir -p); код ошибки — в err
bool MakeDirs(const std::wstring& dir, DWORD& err) {
    if (CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) return true;
    err = GetLastError();
    std::wstring parent = DirOf(dir);
    if (parent.size() >= dir.size() || parent.size() < 3) return false;
    if (!MakeDirs(parent, err)) return false;
    if (CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) return true;
    err = GetLastError();
    return false;
}

// Новая рабочая папка <base>\<prefix>-<pid>-<случайное>. closed=true — с закрытым DACL: только Администраторы и SYSTEM,
// без наследования (для повышенных стадий; ревизия М1/М2). Пусто — не создана (err — причина).
std::wstring NewWorkDir(const std::wstring& base, const std::wstring& prefix, bool closed, DWORD& err) {
    std::wstring d = base + L"\\" + prefix + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-" + RandomHex();
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, FALSE};
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (closed) {
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;BA)(A;OICI;FA;;;SY)", SDDL_REVISION_1, &sd, nullptr)) {
            err = GetLastError();
            return L"";
        }
        sa.lpSecurityDescriptor = sd;
    }
    BOOL ok = CreateDirectoryW(d.c_str(), closed ? &sa : nullptr);   // новая — обязана создаться (не «уже есть»)
    err = ok ? 0 : GetLastError();
    if (sd) LocalFree(sd);
    return ok ? d : L"";
}

// Пробный запуск exe в приостановленном состоянии (код не выполняется) — разрешён ли запуск из этой папки.
// 0 — разрешён; иначе код ошибки CreateProcess.
DWORD ProbeExec(const std::wstring& exe, const std::wstring& dir) {
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\"";
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, dir.c_str(), &si, &pi))
        return GetLastError();
    TerminateProcess(pi.hProcess, 0);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

// Запустить и дождаться; код выхода процесса или ~0u, если не запустился (ошибка — в err)
DWORD RunWait(const std::wstring& exe, const std::wstring& args, const std::wstring& dir, DWORD& err, BOOL inherit = FALSE) {
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\"" + (args.empty() ? L"" : L" " + args);
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, inherit, 0, nullptr, dir.c_str(), &si, &pi)) {
        err = GetLastError();
        return ~0u;
    }
    err = 0;
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return code;
}

// Конец полезных данных файла: начало каталога сертификатов Authenticode, если он в конце (подпись дописывается ПОСЛЕ
// хвоста — ревизия М3), иначе размер файла
uint64_t DataEnd(HANDLE f, uint64_t size) {
    auto readAt = [&](uint64_t off, void* dst, DWORD n) {
        LARGE_INTEGER p;
        p.QuadPart = (LONGLONG)off;
        DWORD got = 0;
        return SetFilePointerEx(f, p, nullptr, FILE_BEGIN) && ReadFile(f, dst, n, &got, nullptr) && got == n;
    };
    uint32_t peOff = 0, sig = 0;
    uint16_t magic = 0;
    if (!readAt(0x3C, &peOff, 4) || peOff > size - 0x100 || !readAt(peOff, &sig, 4) || sig != 0x00004550) return size;
    uint64_t opt = peOff + 4 + 20;                                   // после IMAGE_FILE_HEADER
    if (!readAt(opt, &magic, 2)) return size;
    uint64_t dirs = opt + (magic == 0x20B ? 112 : magic == 0x10B ? 96 : 0);
    if (dirs == opt) return size;
    uint32_t sec[2] = {};                                            // IMAGE_DIRECTORY_ENTRY_SECURITY: смещение в ФАЙЛЕ, размер
    if (!readAt(dirs + 4 * 8, sec, 8) || sec[0] == 0 || sec[1] == 0) return size;
    return (uint64_t)sec[0] + sec[1] <= size && sec[0] > peOff ? sec[0] : size;
}

// Прочитать таблицу нагрузки из хвоста своего exe. Формат (пишет deploy.py, pack_setup_boot):
//   [файлы подряд][записи: u16 длина имени, имя UTF-16LE, u64 смещение, u64 размер]×N[u32 N][u32 размер таблицы][kMagic]
// Хвост ищется перед подписью (с учётом выравнивания подписи до 8 байт). true — нагрузка найдена и цела.
bool ReadPayload(const std::wstring& self, std::vector<PayloadFile>& files) {
    HANDLE f = CreateFileW(self.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    auto readAt = [&](uint64_t off, void* dst, DWORD n) {
        LARGE_INTEGER p;
        p.QuadPart = (LONGLONG)off;
        DWORD got = 0;
        return SetFilePointerEx(f, p, nullptr, FILE_BEGIN) && ReadFile(f, dst, n, &got, nullptr) && got == n;
    };
    bool ok = false;
    uint64_t end = DataEnd(f, (uint64_t)sz.QuadPart);
    char tail[24];
    for (uint64_t pad = 0; pad < 8 && !ok && end > sizeof(tail) + pad; ++pad) {
        uint64_t tailOff = end - pad - sizeof(tail);
        if (!readAt(tailOff, tail, sizeof(tail)) || memcmp(tail + 8, kMagic, sizeof(kMagic)) != 0) continue;
        uint32_t count, tableSize;
        memcpy(&count, tail, 4);
        memcpy(&tableSize, tail + 4, 4);
        if (tableSize >= (1u << 20) || count == 0 || count >= 64 || tailOff < tableSize) break;
        std::vector<char> t(tableSize);
        uint64_t tableOff = tailOff - tableSize;
        if (!readAt(tableOff, t.data(), tableSize)) break;
        size_t p = 0;
        bool good = true;
        files.clear();
        for (uint32_t i = 0; i < count && good; ++i) {
            uint16_t nl;
            if (p + 2 > t.size()) { good = false; break; }
            memcpy(&nl, &t[p], 2);
            p += 2;
            if (nl == 0 || nl > 200 || p + nl * 2u + 16 > t.size()) { good = false; break; }
            PayloadFile pf;
            pf.name.assign((const wchar_t*)&t[p], nl);
            p += nl * 2u;
            memcpy(&pf.offset, &t[p], 8);
            memcpy(&pf.size, &t[p + 8], 8);
            p += 16;
            good = pf.name.find_first_of(L"\\/:") == std::wstring::npos && pf.name != L"." && pf.name != L".." &&
                   pf.offset + pf.size <= tableOff;
            files.push_back(pf);
        }
        ok = good && p == t.size();
        break;
    }
    CloseHandle(f);
    if (!ok) files.clear();
    return ok;
}

// Скопировать файлы нагрузки в папку и открыть их заново только на чтение без права записи/удаления для других
// (держатся до конца установки: второй экземпляр и посторонние процессы их не подменят и не удалят — ревизия L1/М1).
// false — не удалось (err — причина); открытые ручки — в held.
bool ExtractPayload(const std::wstring& self, const std::vector<PayloadFile>& files, const std::wstring& dir,
                    std::vector<HANDLE>& held, DWORD& err) {
    HANDLE src = CreateFileW(self.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                             FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (src == INVALID_HANDLE_VALUE) { err = GetLastError(); return false; }
    std::vector<char> buf(1 << 20);
    bool ok = true;
    err = 0;
    for (const PayloadFile& pf : files) {
        std::wstring path = dir + L"\\" + pf.name;
        HANDLE dst = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
        if (dst == INVALID_HANDLE_VALUE) { err = GetLastError(); ok = false; break; }
        LARGE_INTEGER p;
        p.QuadPart = (LONGLONG)pf.offset;
        SetFilePointerEx(src, p, nullptr, FILE_BEGIN);
        uint64_t left = pf.size;
        while (ok && left) {
            DWORD n = (DWORD)(left < buf.size() ? left : buf.size()), got = 0, put = 0;
            ok = ReadFile(src, buf.data(), n, &got, nullptr) && got == n && WriteFile(dst, buf.data(), n, &put, nullptr) && put == n;
            if (!ok) err = GetLastError();
            left -= n;
        }
        if (!CloseHandle(dst) && ok) { err = GetLastError(); ok = false; }
        if (!ok) break;
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) { err = GetLastError(); ok = false; break; }
        held.push_back(h);
    }
    CloseHandle(src);
    return ok;
}

// Закрыть удерживаемые файлы
void Release(std::vector<HANDLE>& held) {
    for (HANDLE h : held) CloseHandle(h);
    held.clear();
}

// Убрать пустой корень рабочих папок: рядом с установщиком в «Загрузках» не должна оставаться пустая
// AStoryRemake_Setup.files. Непустые папки RemoveDirectory не трогает.
void RemoveBaseIfEmpty(const std::wstring& base) { RemoveDirectoryW(base.c_str()); }

// Строка списка «Проверенные папки»: путь и что с ним
std::wstring TriedLine(const std::wstring& base, DWORD e) { return L"\n  " + base + L" — " + ErrText(e); }

// Итоговое сообщение «не удалось подготовить запуск» по фактической причине (ревизия L3)
void FailBox(const std::wstring& args, bool uninstall, DWORD lastErr, const std::wstring& tried) {
    if (IsExecBlocked(lastErr)) {
        ErrorBox(args,
                 std::wstring(uninstall ? L"Не удалось запустить удаление" : L"Не удалось запустить установку") +
                     L": политика безопасности этого компьютера запрещает запуск программ из рабочих папок.\n\nПроверенные папки:" +
                     tried +
                     L"\n\nОбратитесь к администратору компьютера: нужно разрешить запуск программ хотя бы из одной из этих папок." +
                     (uninstall ? L"" : L" Можно также перенести установщик в другую папку на этом компьютере (не во временную) и запустить его оттуда."),
                 std::wstring(uninstall ? L"Uninstall could not be started" : L"Setup could not be started") +
                     L": this computer's security policy blocks running programs from the working folders.\n\nFolders tried:" + tried +
                     L"\n\nPlease contact your computer administrator: running programs must be allowed in at least one of these folders." +
                     (uninstall ? L"" : L" You can also move the installer to another folder on this computer (not a temporary one) and run it from there."));
    } else {
        ErrorBox(args,
                 std::wstring(uninstall ? L"Не удалось подготовить удаление" : L"Не удалось подготовить установку") + L": " +
                     ErrText(lastErr) + L".\n\nПроверенные папки:" + tried,
                 std::wstring(uninstall ? L"Could not prepare uninstall" : L"Could not prepare setup") + L": " + ErrText(lastErr) +
                     L".\n\nFolders tried:" + tried);
    }
}

// ───────────────────────────── установка ─────────────────────────────

// Режим установки. Рабочие папки: свой профиль → папка самого exe (раз он запустился, запуск оттуда разрешён).
// %TEMP% не берём никогда; ProgramData — тоже (открыта на запись всем пользователям, ревизия М2).
int RunInstall(const std::wstring& self, const std::vector<PayloadFile>& files, const std::wstring& args) {
    const std::wstring& setupName = files.front().name;   // первым deploy.py кладёт setup.exe
    std::vector<std::wstring> bases;
    std::wstring la = KnownFolder(FOLDERID_LocalAppData);
    if (!la.empty()) bases.push_back(la + L"\\AsdaReborn\\Setup");
    bases.push_back(DirOf(self) + L"\\AStoryRemake_Setup.files");
    DWORD lastErr = 0;
    std::wstring tried;
    for (const std::wstring& base : bases) {
        DWORD err = 0;
        if (!MakeDirs(base, err)) { lastErr = err; tried += TriedLine(base, err); continue; }
        CleanupStale(base, L's');
        std::wstring dir = NewWorkDir(base, L"s", false, err);
        if (dir.empty()) { lastErr = err; tried += TriedLine(base, err); RemoveBaseIfEmpty(base); continue; }
        std::vector<HANDLE> held;
        if (!ExtractPayload(self, files, dir, held, err)) {
            Release(held);
            lastErr = err;
            tried += TriedLine(base, err);
            DeleteTree(dir);
            RemoveBaseIfEmpty(base);
            continue;
        }
        std::wstring exe = dir + L"\\" + setupName;
        DWORD pe = ProbeExec(exe, dir);
        if (pe != 0) {                                   // запуск отсюда запрещён (или иная ошибка) — следующая папка
            Release(held);
            lastErr = pe;
            tried += TriedLine(base, pe);
            DeleteTree(dir);
            RemoveBaseIfEmpty(base);
            continue;
        }
        DWORD code = RunWait(exe, args, dir, err);
        Release(held);
        DeleteTree(dir);
        RemoveBaseIfEmpty(base);
        if (code == ~0u) {
            lastErr = err;
            tried += TriedLine(base, err);
            if (IsExecBlocked(err)) continue;
            ErrorBox(args, L"Не удалось запустить установку: " + ErrText(err) + L".", L"Setup could not be started: " + ErrText(err) + L".");
            return kExitFail;
        }
        return (int)code;
    }
    FailBox(args, false, lastErr, tried);
    return kExitFail;
}

// ───────────────────────────── удаление ─────────────────────────────

// Самый новый деинсталлятор Inno в папке (unins000.exe, unins001.exe …)
std::wstring FindUninstaller(const std::wstring& dir) {
    std::wstring best;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\unins???.exe").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return best;
    do {
        std::wstring n = fd.cFileName;
        if (n.size() == 12 && iswdigit(n[5]) && iswdigit(n[6]) && iswdigit(n[7]) && (best.empty() || _wcsicmp(n.c_str(), best.c_str()) > 0))
            best = n;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return best.empty() ? best : dir + L"\\" + best;
}

// Стадия 0: запуск из «Приложений» без прав. Админская установка ({app}\Setup закрыта на запись) — повышаем себя и ждём;
// иначе (установка без прав администратора) сразу стадия E.
int UninstallStage0(const std::wstring& self, const std::wstring& args);
// Стадия E: закрытая рабочая папка, копия себя, TEMP/TMP, стадия 1; выход с её кодом
int UninstallStageE(const std::wstring& self, DWORD parentPid, const std::wstring& args);
// Стадия 1 (копия в рабочей папке): unins000.exe, код — стадии E, затем уборка
int UninstallStage1(const std::wstring& self, const std::vector<std::wstring>& w, const std::wstring& args);

int UninstallStage0(const std::wstring& self, const std::wstring& args) {
    std::wstring setupDir = DirOf(self);
    if (FindUninstaller(setupDir).empty()) {
        ErrorBox(args, L"Не найден деинсталлятор (unins000.exe) в папке " + setupDir + L".",
                 L"The uninstaller (unins000.exe) was not found in " + setupDir + L".");
        return kExitFail;
    }
    if (IsElevated() || IsWritableDir(setupDir)) return UninstallStageE(self, 0, args);
    std::wstring params = std::wstring(kElevFlag) + L" " + std::to_wstring(GetCurrentProcessId()) + (args.empty() ? L"" : L" " + args);
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas";
    sei.lpFile = self.c_str();
    sei.lpParameters = params.c_str();
    sei.lpDirectory = setupDir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        DWORD e = GetLastError();
        if (e == ERROR_CANCELLED) return kExitFail;      // отказ в окне UAC — без сообщения, как у Inno
        ErrorBox(args, L"Не удалось получить права администратора для удаления: " + ErrText(e) + L".",
                 L"Could not obtain administrator rights for uninstall: " + ErrText(e) + L".");
        return kExitFail;
    }
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD code = kExitFail;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return (int)code;
}

int UninstallStageE(const std::wstring& self, DWORD parentPid, const std::wstring& args) {
    std::wstring setupDir = DirOf(self);
    bool elevated = IsElevated();
    // Рабочие папки по порядку: сама {app}\Setup (закрыта для пользователей, Program Files разрешён и стандартными
    // правилами AppLocker) → [повышенный] прямо в ProgramData (имя случайное, DACL закрыт; удалять/переименовывать её
    // пользователи не могут — у них нет права удаления в корне ProgramData) → свой %LOCALAPPDATA%.
    struct Base { std::wstring dir, prefix; bool cleanup; };
    std::vector<Base> bases{{setupDir, L"u", true}};
    std::wstring pd = KnownFolder(FOLDERID_ProgramData), la = KnownFolder(FOLDERID_LocalAppData);
    if (elevated && !pd.empty()) bases.push_back({pd, L"AStoryRemake.u", false});
    if (!la.empty()) bases.push_back({la + L"\\AsdaReborn\\Setup", L"u", !elevated});
    DWORD lastErr = 0;
    std::wstring tried;
    for (const Base& b : bases) {
        DWORD err = 0;
        if (!MakeDirs(b.dir, err)) { lastErr = err; tried += TriedLine(b.dir, err); continue; }
        if (b.cleanup) CleanupStale(b.dir, L'u');
        std::wstring work = NewWorkDir(b.dir, b.prefix, elevated, err);
        if (work.empty()) { lastErr = err; tried += TriedLine(b.dir, err); continue; }
        std::wstring copy = work + L"\\" + kUninstallerName;
        DWORD pe = CopyFileW(self.c_str(), copy.c_str(), TRUE) ? ProbeExec(copy, work) : GetLastError();
        if (pe != 0) {
            lastErr = pe;
            tried += TriedLine(b.dir, pe);
            DeleteTree(work);
            continue;
        }
        // Канал «код удаления» от стадии 1 и ручки для ожидания выхода стадий E и 0 (наследуются только они)
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE rd = nullptr, wr = nullptr, me = nullptr, parent = nullptr;
        CreatePipe(&rd, &wr, &sa, 0);
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &me, SYNCHRONIZE, TRUE, 0);
        if (parentPid) parent = OpenProcess(SYNCHRONIZE, TRUE, parentPid);
        wchar_t hs[96];
        swprintf_s(hs, L"%s %llu %llu %llu ", kStage1Flag, (unsigned long long)(uintptr_t)wr, (unsigned long long)(uintptr_t)me,
                   (unsigned long long)(uintptr_t)parent);
        std::wstring a1 = hs + (L"\"" + setupDir + L"\" \"" + work + L"\"") + (args.empty() ? L"" : L" " + args);
        SetEnvironmentVariableW(L"TEMP", work.c_str());
        SetEnvironmentVariableW(L"TMP", work.c_str());
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"\"" + copy + L"\" " + a1;
        BOOL started = CreateProcessW(copy.c_str(), cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, work.c_str(), &si, &pi);
        DWORD se = started ? 0 : GetLastError();
        CloseHandle(wr);
        if (me) CloseHandle(me);
        if (parent) CloseHandle(parent);
        if (!started) {
            CloseHandle(rd);
            DeleteTree(work);
            lastErr = se;
            tried += TriedLine(b.dir, se);
            continue;
        }
        CloseHandle(pi.hThread);
        DWORD code = kExitFail, got = 0;
        if (!ReadFile(rd, &code, sizeof(code), &got, nullptr) || got != sizeof(code)) {   // стадия 1 умерла, не ответив
            WaitForSingleObject(pi.hProcess, INFINITE);
            GetExitCodeProcess(pi.hProcess, &code);
        }
        CloseHandle(rd);
        CloseHandle(pi.hProcess);
        return (int)code;
    }
    FailBox(args, true, lastErr, tried);
    return kExitFail;
}

int UninstallStage1(const std::wstring& self, const std::vector<std::wstring>& w, const std::wstring& args) {
    // w: [0]=/ASDA-U1 [1]=канал [2]=ручка стадии E [3]=ручка стадии 0 (0 — нет) [4]=папка {app}\Setup [5]=рабочая папка
    HANDLE wr = (HANDLE)(uintptr_t)_wcstoui64(w[1].c_str(), nullptr, 10);
    HANDLE hE = (HANDLE)(uintptr_t)_wcstoui64(w[2].c_str(), nullptr, 10);
    HANDLE h0 = (HANDLE)(uintptr_t)_wcstoui64(w[3].c_str(), nullptr, 10);
    const std::wstring& setupDir = w[4];
    const std::wstring& work = w[5];
    std::wstring unins = FindUninstaller(setupDir);
    DWORD code = kExitFail, err = 0;
    if (unins.empty()) {
        ErrorBox(args, L"Не найден деинсталлятор (unins000.exe) в папке " + setupDir + L".",
                 L"The uninstaller (unins000.exe) was not found in " + setupDir + L".");
    } else {
        code = RunWait(unins, args, setupDir, err);
        if (code == ~0u) {
            code = kExitFail;
            ErrorBox(args, L"Не удалось запустить удаление: " + ErrText(err) + L".", L"Uninstall could not be started: " + ErrText(err) + L".");
        }
    }
    DWORD put = 0;
    WriteFile(wr, &code, sizeof(code), &put, nullptr);    // стадии E (и через неё 0) — код для «Приложений», winget, SCCM
    CloseHandle(wr);
    for (HANDLE h : {hE, h0})                             // их образ — {app}\Setup\AStoryUninstall.exe
        if (h) { WaitForSingleObject(h, 30000); CloseHandle(h); }
    if (code == 0) {                                      // удаление прошло (не отменено) — убрать помощник
        std::wstring orig = setupDir + L"\\" + kUninstallerName;
        for (int i = 0; i < 50 && GetFileAttributesW(orig.c_str()) != INVALID_FILE_ATTRIBUTES && !DeleteFileW(orig.c_str()); ++i) Sleep(200);
    }
    // Вторая фаза Inno работает из work\iu-*.tmp\_unins.tmp и удаляет unins000.exe после выхода первой — ждём, пока
    // её папки в work не удалятся (не дольше 60 с), чтобы не оставлять 3,8 МБ (ревизия L2)
    std::wstring selfName = NameOf(self);
    for (int i = 0; i < 300; ++i) {
        bool left = false;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((work + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::wstring n = fd.cFileName;
                if (n == L"." || n == L".." || _wcsicmp(n.c_str(), selfName.c_str()) == 0) continue;
                std::wstring p = work + L"\\" + n;
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) DeleteTree(p);
                else DeleteFileW(p.c_str());
                left = left || GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        if (!left) break;
        Sleep(200);
    }
    // Себя (и, если удаление прошло, пустые {app}\Setup и {app}) убирает cmd.exe из System32 после нашего выхода
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring shell = std::wstring(sys) + L"\\cmd.exe";
    std::wstring line = L"/d /c ping -n 3 127.0.0.1 >nul & rd /s /q \"" + work + L"\"";
    if (code == 0) line += L" & rd \"" + setupDir + L"\" & rd \"" + DirOf(setupDir) + L"\"";
    line += L" >nul 2>&1";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + shell + L"\" " + line;
    if (CreateProcessW(shell.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, sys, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return (int)code;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Первым делом: DLL — только из System32 (рядом с exe в «Загрузках» могут лежать подложенные; ревизия М4).
    // Все DLL, кроме kernel32, загружаются с задержкой (build_boot.bat) — уже после этой строки.
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    std::wstring self = SelfPath();
    std::vector<PayloadFile> files;
    if (ReadPayload(self, files)) return RunInstall(self, files, SkipWords(GetCommandLineW(), 1));
    if (_wcsicmp(NameOf(self).c_str(), kUninstallerName) == 0) {
        std::vector<std::wstring> w = ArgWords();
        if (w.size() >= 6 && _wcsicmp(w[0].c_str(), kStage1Flag) == 0)
            return UninstallStage1(self, w, SkipWords(GetCommandLineW(), 7));
        if (w.size() >= 2 && _wcsicmp(w[0].c_str(), kElevFlag) == 0)
            return UninstallStageE(self, (DWORD)wcstoul(w[1].c_str(), nullptr, 10), SkipWords(GetCommandLineW(), 3));
        return UninstallStage0(self, SkipWords(GetCommandLineW(), 1));
    }
    std::wstring args = SkipWords(GetCommandLineW(), 1);
    ErrorBox(args, L"Файл установщика повреждён или скачан не полностью — скачайте его заново.",
             L"The installer file is damaged or incomplete — please download it again.");
    return kExitFail;
}
