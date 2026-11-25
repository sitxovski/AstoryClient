// Copyright (c) 2026 @sitxovski (Discord)
// SelfUpdate.h — самообновление лаунчера через канал «launcher» того же хранилища (те же подписи и проверки).
// Схема: Update() собирает новую версию в <папка лаунчера>\.asdaupd-launcher (полное состояние там же); если
// AsdaLauncher.exe там отличается от запущенного — запущенный переименовывается в .old (Windows разрешает для
// работающего образа), новый копируется на его место, лаунчер перезапускается; .old удаляется при следующем старте.
#pragma once
#include <string>

#include "Http.h"
#include "Updater.h"

namespace asda {

// Итог проверки самообновления
enum class SelfUpdateResult {
    UpToDate,       // уже последняя версия
    Restarting,     // новая версия поставлена, процесс должен завершиться (новый уже запущен)
    NoChannel,      // канала лаунчера на сервере нет (не выпускался) — работаем как есть
};

// Проверить и при необходимости поставить новую версию лаунчера. UpdateError — отказ (лаунчер работает дальше
// старой версией, ошибка показывается в журнале).
SelfUpdateResult SelfUpdate(Source& src, const UpdateOptions& base, const std::wstring& restartArgs);
// Удалить .old от прошлого самообновления
void CleanupOldLauncher();

}  // namespace asda
