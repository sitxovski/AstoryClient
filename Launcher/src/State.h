// Copyright (c) 2026 @sitxovski (Discord)
// State.h — состояние клиента в <папка игры>\.asdaupd (схема как у эталона asda_update_client.py) + дубль
// счётчиков отката в HKCU (переживает удаление папки состояния).
#pragma once
#include <functional>
#include <map>
#include <optional>
#include <string>

#include "Util.h"

namespace asda {

// Незавершённое обновление (пока есть — запуск игры запрещён)
struct Pending {
    std::string version, manifest;
    int64_t seq = 0;
};

// Запись кэша хэшей: размер, время изменения, SHA-256
struct CacheEntry {
    uint64_t size = 0, mtime = 0;
    std::string sha;
};

// Состояние клиента
class State {
public:
    // root — RootPath папки игры; log — журнал (может быть пустым)
    State(const std::wstring& root, const std::function<void(const std::string&)>& log);
    // Сохранить state.json и hashcache.json (временный → fsync → замена, прежний — в .bak) и дубль в реестре
    void Save();

    // Установленная версия
    std::string channel, version, manifest, minVersion;
    int64_t seq = 0;
    // Максимум виденного seq канала и sha манифеста при нём — по каналам
    std::map<std::string, int64_t> seqMax;
    std::map<std::string, std::string> seenManifest;
    // Максимум виденного keyring seq
    int64_t keyringSeq = 0;
    std::optional<Pending> pending;
    // Кэш хэшей: путь → запись
    std::map<std::string, CacheEntry> cache;

    // Папка .asdaupd и objects
    std::wstring dir, objects;

    // Сохранённый .plst (хэш сверяется) или пусто
    bool Plst(const std::string& sha, Bytes& out) const;
    // Манифест установленной версии (сжатый, как с сервера; хэш = manifest) или пусто
    bool ManifestBlob(Bytes& out) const;

private:
    void Load(const std::function<void(const std::string&)>& log);
    void LoadRegistry();
    void SaveRegistry() const;
    std::wstring regKey_;   // HKCU\Software\AStoryRemake\Updater\<хэш пути>
    std::wstring root_;     // корень папки игры (для проверок путей при записи)
};

// Готова ли установка к запуску игры: версия есть и нет pending
bool IsReady(const std::wstring& gameDir);

}  // namespace asda
