// Copyright (c) 2026 @sitxovski (Discord)
// Launch.h — папки установки, служба Easy Anti-Cheat, запуск игры через загрузчик EAC (Astory.exe).
#pragma once
#include <string>

#include "Util.h"

namespace asda {

// Папка, где лежит exe лаунчера
std::wstring LauncherDir();
// Папка игры: <папка лаунчера>\Game
std::wstring GameDir();
// Можно ли писать в папку (иначе — нужны права администратора)
bool IsWritableDir(const std::wstring& dir);
// Установлена ли служба EAC
bool EacServiceInstalled();
// Установить службу EAC: EasyAntiCheat\EasyAntiCheat_EOS_Setup.exe install <productid из Settings.json>,
// с запросом прав (UAC). Ждёт завершения. UpdateError при отказе/ошибке.
void InstallEacService(const std::wstring& gameDir);
// Запустить игру (Astory.exe в папке игры). Перед этим вызывающий проверяет IsReady.
void StartGame(const std::wstring& gameDir);
// Запущена ли игра (AsdaReborn-Win64-Shipping.exe или загрузчик Astory.exe): файлы игры нельзя заменять, а её журналы —
// трогать (★ 19.10.2026 перенесено сюда из Ui.cpp — общая проверка окна и выгрузки журналов, LogUpload.cpp)
bool GameRunning();
// Запущен ли процесс с повышенными правами
bool IsElevated();
// Перезапустить себя с правами администратора (args — командная строка); false, если пользователь отказал
bool RelaunchElevated(const std::wstring& args);

}  // namespace asda
