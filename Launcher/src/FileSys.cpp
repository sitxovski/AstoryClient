// Copyright (c) 2026 @sitxovski (Discord)
// FileSys.cpp — безопасная работа с файлами (см. FileSys.h).
#include "FileSys.h"

#include <windows.h>

#include "Format.h"

namespace asda {

namespace {

// Финальный путь дескриптора (DOS-вид «\\?\D:\…»), нижний регистр — для сравнения с корнем
std::wstring FinalLower(HANDLE h) {
    wchar_t buf[4096];
    DWORD n = GetFinalPathNameByHandleW(h, buf, 4096, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!n || n >= 4096) return {};
    std::wstring s(buf, n);
    CharLowerBuffW(s.data(), (DWORD)s.size());
    return s;
}

// Финальный путь корня (кэш по строке корня)
std::wstring RootFinal(const std::wstring& root) {
    static thread_local std::wstring lastRoot, lastFinal;
    if (root == lastRoot && !lastFinal.empty()) return lastFinal;
    HANDLE h = CreateFileW(root.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) throw UpdateError("папка игры недоступна: " + LastErrorText(GetLastError()));
    std::wstring f = FinalLower(h);
    CloseHandle(h);
    if (f.empty()) throw UpdateError("папка игры: не определён путь");
    lastRoot = root;
    lastFinal = f;
    return f;
}

// Финальный путь дескриптора лежит строго внутри корня
bool InsideRoot(HANDLE h, const std::wstring& root) {
    std::wstring f = FinalLower(h), r = RootFinal(root);
    return !f.empty() && f.size() > r.size() && f.compare(0, r.size(), r) == 0 && f[r.size()] == L'\\';
}

// Сведения о файле по дескриптору: не точка повторного разбора и (опционально) ровно одна жёсткая ссылка
bool PlainFile(HANDLE h, bool singleLink) {
    BY_HANDLE_FILE_INFORMATION bi{};
    if (!GetFileInformationByHandle(h, &bi)) return false;
    if (bi.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) return false;
    return !singleLink || bi.nNumberOfLinks == 1;
}

// Папка файла
std::wstring ParentOf(const std::wstring& p) { return p.substr(0, p.rfind(L'\\')); }

// Папка внутри корня (или сам корень) — по дескриптору
bool DirInsideOrRoot(const std::wstring& root, const std::wstring& dir) {
    HANDLE h = CreateFileW(dir.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION bi{};
    bool ok = GetFileInformationByHandle(h, &bi) && !(bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
    std::wstring f = FinalLower(h), r = RootFinal(root);
    CloseHandle(h);
    return ok && (f == r || (f.size() > r.size() && f.compare(0, r.size(), r) == 0 && f[r.size()] == L'\\'));
}

}  // namespace

std::wstring RootPath(const std::wstring& gameDir) {
    wchar_t buf[4096];
    DWORD n = GetFullPathNameW(gameDir.c_str(), 4096, buf, nullptr);
    if (!n || n >= 4096) throw UpdateError("папка игры: неверный путь");
    std::wstring full(buf, n);
    while (full.size() > 3 && (full.back() == L'\\' || full.back() == L'/')) full.pop_back();
    if (full.rfind(L"\\\\?\\", 0) != 0)
        full = full.rfind(L"\\\\", 0) == 0 ? L"\\\\?\\UNC\\" + full.substr(2) : L"\\\\?\\" + full;
    CreateDirectoryW(full.c_str(), nullptr);
    DWORD a = GetFileAttributesW(full.c_str());
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) throw UpdateError("папка игры не создаётся");
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) throw UpdateError("папка игры — junction/symlink: отказ");
    RootFinal(full);
    return full;
}

std::wstring SafePath(const std::wstring& root, const std::string& rel) {
    CheckRelPath(rel);
    std::wstring cur = root, deepestDir = root;
    size_t a = 0;
    for (;;) {
        size_t b = rel.find('/', a);
        cur += L"\\" + Widen(rel.substr(a, b == std::string::npos ? std::string::npos : b - a));
        DWORD attr = GetFileAttributesW(cur.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) break;           // дальше компонентов ещё нет
        if (attr & FILE_ATTRIBUTE_REPARSE_POINT) throw UpdateError("путь " + rel + " проходит через junction/symlink — отказ");
        if (b == std::string::npos) break;
        if (attr & FILE_ATTRIBUTE_DIRECTORY) deepestDir = cur;
        a = b + 1;
    }
    // ближайшая существующая папка-предок — по дескриптору внутри корня (подмена на junction после проверки имени)
    if (!DirInsideOrRoot(root, deepestDir)) throw UpdateError("путь " + rel + " выходит за папку игры");
    std::wstring full = root + L"\\" + Widen(rel);
    for (auto& c : full)
        if (c == L'/') c = L'\\';
    return full;
}

bool IsRegularFile(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
}

bool IsDir(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
}

void MakeParentDirs(const std::wstring& root, const std::wstring& filePath) {
    if (filePath.compare(0, root.size(), root) != 0) throw UpdateError("папка вне корня игры");
    for (size_t i = filePath.find(L'\\', root.size() + 1); i != std::wstring::npos; i = filePath.find(L'\\', i + 1)) {
        std::wstring d = filePath.substr(0, i);
        if (!CreateDirectoryW(d.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            throw UpdateError("не создана папка: " + LastErrorText(GetLastError()));
        if (!DirInsideOrRoot(root, d)) throw UpdateError("папка игры подменена ссылкой — отказ");
    }
}

void* OpenReadChecked(const std::wstring& root, const std::wstring& p) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
    if (!PlainFile(h, false) || !InsideRoot(h, root)) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

void* CreateTmpChecked(const std::wstring& root, const std::wstring& p) {
    if (!DeleteFileW(p.c_str())) {                        // удаляется ИМЯ (ссылка — как ссылка), цель не трогается
        DWORD e = GetLastError();
        if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) throw UpdateError("временный файл занят: " + LastErrorText(e));
    }
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) throw UpdateError("временный файл: " + LastErrorText(GetLastError()));
    if (!PlainFile(h, true) || !InsideRoot(h, root)) {
        CloseHandle(h);
        throw UpdateError("временный файл подменён ссылкой — отказ");
    }
    return h;
}

void ReplaceFile(const std::wstring& root, const std::wstring& from, const std::wstring& to) {
    if (!DirInsideOrRoot(root, ParentOf(to))) throw UpdateError("папка назначения подменена ссылкой — отказ");
    for (int i = 0; i < 20; ++i) {
        if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return;
        DWORD e = GetLastError();
        if (e != ERROR_ACCESS_DENIED && e != ERROR_SHARING_VIOLATION) throw UpdateError("замена файла: " + LastErrorText(e));
        Sleep(250);                                   // антивирус/индексатор держит файл — короткие повторы
    }
    throw UpdateError("файл занят (закройте игру): " + Narrow(to.substr(to.rfind(L'\\') + 1)));
}

void RemoveFileChecked(const std::wstring& root, const std::wstring& p) {
    HANDLE h = CreateFileW(p.c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return;
        throw UpdateError("удаление файла: " + LastErrorText(e));
    }
    BY_HANDLE_FILE_INFORMATION bi{};
    bool isLink = GetFileInformationByHandle(h, &bi) && (bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
    // ссылку удаляем как ссылку (цель не трогается); обычный файл — только если он внутри корня
    if (!isLink && !InsideRoot(h, root)) {
        CloseHandle(h);
        throw UpdateError("удаляемый файл вне папки игры — отказ");
    }
    FILE_DISPOSITION_INFO di{TRUE};
    BOOL ok = SetFileInformationByHandle(h, FileDispositionInfo, &di, sizeof(di));
    DWORD e = GetLastError();
    CloseHandle(h);
    if (!ok) throw UpdateError("удаление файла: " + LastErrorText(e));
}

void RemoveEmptyParents(const std::wstring& root, const std::wstring& filePath) {
    std::wstring d = ParentOf(filePath);
    while (d.size() > root.size() && d.compare(0, root.size(), root) == 0 && d[root.size()] == L'\\') {
        if (!RemoveDirectoryW(d.c_str())) break;            // непустая/ссылка → стоп (RemoveDirectory не идёт по ссылке)
        d = ParentOf(d);
    }
}

// Обход для CleanupTmp
static void CleanupDir(const std::wstring& dir, bool top) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    static const std::wstring suf = Widen(kTmpSuffix), stateDir = Widen(kStateDirName);
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring p = dir + L"\\" + name;
        bool reparse = fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT;
        bool isDir = fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
        bool tmp = name.size() >= suf.size() && _wcsicmp(name.c_str() + name.size() - suf.size(), suf.c_str()) == 0;
        if (isDir && !reparse) {
            if (!(top && _wcsicmp(name.c_str(), stateDir.c_str()) == 0)) CleanupDir(p, false);
        } else if (tmp) {
            if (isDir) RemoveDirectoryW(p.c_str());         // папка-ссылка с именем *.asdaupd-tmp — удалить ссылку
            else DeleteFileW(p.c_str());                    // файл или файловая ссылка — удаляется само имя
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

void CleanupTmp(const std::wstring& root) { CleanupDir(root, true); }

bool FileStat(const std::wstring& p, uint64_t& size, uint64_t& mtime) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d) ||
        (d.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        return false;
    size = (uint64_t)d.nFileSizeHigh << 32 | d.nFileSizeLow;
    mtime = (uint64_t)d.ftLastWriteTime.dwHighDateTime << 32 | d.ftLastWriteTime.dwLowDateTime;
    return true;
}

bool ReadAll(const std::wstring& p, Bytes& out, size_t limit) {
    HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!PlainFile(f, false) || !GetFileSizeEx(f, &sz) || (uint64_t)sz.QuadPart > limit) {
        CloseHandle(f);
        return false;
    }
    out.resize((size_t)sz.QuadPart);
    DWORD got = 0;
    bool ok = out.empty() || (ReadFile(f, out.data(), (DWORD)out.size(), &got, nullptr) && got == out.size());
    CloseHandle(f);
    return ok;
}

void WriteAtomic(const std::wstring& root, const std::wstring& p, const void* data, size_t n) {
    std::wstring tmp = p + L".tmp";
    HANDLE f = CreateTmpChecked(root, tmp);
    DWORD put = 0;
    bool ok = (!n || WriteFile(f, data, (DWORD)n, &put, nullptr)) && put == n && FlushFileBuffers(f);
    DWORD e = GetLastError();
    CloseHandle(f);
    if (!ok) throw UpdateError("запись файла: " + LastErrorText(e));
    ReplaceFile(root, tmp, p);
}

}  // namespace asda
