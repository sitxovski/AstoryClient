// Copyright (c) 2026 @sitxovski (Discord)
// FileSys.h — безопасная работа с файлами внутри папки игры (FORMAT_v1 §6 п. 13–14; ревизия review_wave4.md В1/В2).
// Правило: каждый файл открывается с FILE_FLAG_OPEN_REPARSE_POINT (ссылка не разыменовывается), после открытия
// финальный путь дескриптора (GetFinalPathNameByHandle) сверяется с корнем; удаление — через тот же дескриптор.
#pragma once
#include <string>

#include "Util.h"

namespace asda {

// Абсолютный путь «\\?\…» корня папки игры (создаётся при отсутствии; сам корень не может быть ссылкой)
std::wstring RootPath(const std::wstring& gameDir);
// Путь файла манифеста внутри корня: проверка пути, отказ при junction/symlink на любом существующем компоненте,
// ближайшая существующая папка-предок по дескриптору обязана лежать внутри корня.
std::wstring SafePath(const std::wstring& root, const std::string& rel);
// Есть ли обычный файл (не папка, не точка повторного разбора)
bool IsRegularFile(const std::wstring& p);
// Есть ли папка
bool IsDir(const std::wstring& p);
// Создать папки пути файла НИЖЕ корня (сам корень и его родителей не трогает)
void MakeParentDirs(const std::wstring& root, const std::wstring& filePath);
// Открыть существующий обычный файл на чтение с проверкой «не ссылка, внутри корня»; INVALID_HANDLE_VALUE — нельзя
void* OpenReadChecked(const std::wstring& root, const std::wstring& p);
// Создать НОВЫЙ временный файл (прежний — удаляется как имя, ссылка не разыменовывается; затем CREATE_NEW),
// проверить «не ссылка, одна жёсткая ссылка, внутри корня». UpdateError при нарушении.
void* CreateTmpChecked(const std::wstring& root, const std::wstring& p);
// Атомарная замена файла (MoveFileEx REPLACE_EXISTING | WRITE_THROUGH; заменяет саму запись каталога, не цель ссылки);
// перед заменой папка назначения сверяется с корнем
void ReplaceFile(const std::wstring& root, const std::wstring& from, const std::wstring& to);
// Удалить файл (или ссылку как таковую) через проверенный дескриптор; нет файла — не ошибка
void RemoveFileChecked(const std::wstring& root, const std::wstring& p);
// Удалить опустевшие папки вверх от родителя файла до корня (не включая корень)
void RemoveEmptyParents(const std::wstring& root, const std::wstring& filePath);
// Удалить *.asdaupd-tmp во всём дереве, включая подложенные ссылки с таким именем (удаляется сама ссылка);
// в папки-ссылки и в .asdaupd не заходить
void CleanupTmp(const std::wstring& root);
// Размер файла и время изменения (100-нс тики); false, если нет
bool FileStat(const std::wstring& p, uint64_t& size, uint64_t& mtime);
// Прочитать файл целиком (ограничение limit); false, если нет
bool ReadAll(const std::wstring& p, Bytes& out, size_t limit = 512u << 20);
// Записать файл атомарно (служебные файлы в корне/.asdaupd): новый временный → FlushFileBuffers → замена
void WriteAtomic(const std::wstring& root, const std::wstring& p, const void* data, size_t n);

}  // namespace asda
