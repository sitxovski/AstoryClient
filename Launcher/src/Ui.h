// Copyright (c) 2026 @sitxovski (Discord)
// Ui.h — окно лаунчера: проверка/загрузка обновлений, «Играть», «Проверить файлы». GDI+, без рамки, под DPI.
#pragma once
#include <windows.h>

#include <string>

namespace asda {

// Запуск окна (цикл сообщений). args — командная строка (например, «--after-selfupdate»). → код выхода.
int RunLauncherWindow(HINSTANCE inst, const std::wstring& args);
// Отрисовка окна в PNG без сети (проверка вида): state — starting|updating|ready|error; lang — ru|en;
// net — плашка сервера: online:<мс> | offline | checking (пусто — online:42).
int RenderPreview(HINSTANCE inst, const std::wstring& outPng, const std::wstring& state, const std::wstring& lang,
                  const std::wstring& net = L"");

}  // namespace asda
