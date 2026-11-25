// Copyright (c) 2026 @sitxovski (Discord)
// Updater.h — обновление папки игры до версии канала (FORMAT_v1 §6–§7). Порт Tools/asda_update_client.py.
#pragma once
#include <array>
#include <atomic>
#include <functional>
#include <string>

#include "Http.h"
#include "Util.h"

namespace asda {

// Фаза обновления (для окна)
enum class Phase { Connect, Check, Download, Finish };

// Прогресс и отмена (вызывается из рабочего потока)
struct Progress {
    std::function<void(Phase, const std::string&)> phase;        // смена фазы + текст
    // Ход фазы. Check — байты проверенных файлов; ★ 19.10.2026 Download — байты СЕТИ: total = план загрузки (сумма
    //   диапазонов недостающих кусков, считается до первой загрузки), done — уже скачанные диапазоны. Раньше в Download
    //   total был суммой размеров изменённых файлов целиком (игрок видел 2.7 ГБ при загрузке 90 МБ).
    std::function<void(uint64_t done, uint64_t total)> bytes;
    // ★ 19.10.2026 Download: сборка из СВОИХ кусков старой версии (копирование с диска, не сеть): done/total — байты.
    std::function<void(uint64_t done, uint64_t total)> assemble;
    std::atomic<bool>* cancel = nullptr;                         // запрос отмены
};

// Параметры обновления
struct UpdateOptions {
    std::string channel = "alpha";
    std::array<uint8_t, 32> rootPub{};      // вшитый корневой ключ
    int64_t minKeyringSeq = 0, minSeq = 0;  // вшитые минимумы (защита первой установки)
    bool repair = false;                    // «Проверить файлы»: без кэша хэшей
    int threads = 4;                        // параллельных диапазонов на файл
    std::function<void(const std::string&)> log;
    Progress progress;
};

// Итог
struct UpdateStats {
    std::string version;
    int64_t seq = 0;
    uint64_t files = 0, changed = 0, local = 0, netPayload = 0, deleted = 0, downloaded = 0;
    uint64_t changedBytes = 0;   // ★ 19.10.2026: размер изменённых файлов целиком (сколько обновляется на диске)
    uint64_t planNet = 0;        // ★ 19.10.2026: план загрузки — байты диапазонов недостающих кусков (до загрузки)
};

// Обновить/восстановить папку игры. UpdateError — отказ, Cancelled — отмена.
UpdateStats Update(Source& src, const std::wstring& gameDir, const UpdateOptions& opt);

// Сведения о доступной версии без изменения диска (для окна: «есть обновление?»)
struct RemoteInfo {
    std::string version, minVersion;
    int64_t seq = 0;
    bool upToDate = false;   // установлена ровно эта версия и нет pending
};
RemoteInfo CheckRemote(Source& src, const std::wstring& gameDir, const UpdateOptions& opt);

// SHA-256 файла rel по УСТАНОВЛЕННОМУ манифесту (хэш манифеста сверен с состоянием); false — нет записи/установки
bool InstalledFileSha(const std::wstring& gameDir, const std::string& rel, std::string& sha);

}  // namespace asda
