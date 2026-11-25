// Copyright (c) 2026 @sitxovski (Discord)
// AsdaLoginGameMode_Private.h — ★ R3-c 12.11.2026 (разрез AsdaLoginGameMode.cpp): ЗАКРЫТЫЙ заголовок частей AsdaLoginGameMode*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaLoginGameMode.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "Login/AsdaLoginGameMode.h"
// ★ Аудит №4 (A8): `Login/AsdaGameText.h` больше не нужен — строка помощника теперь
//   `FAsdaLoginhelpertextRow` из `Items/AsdaItemDbRows.h` (тянется через AsdaItemTableSubsystem.h).
#include "Blueprint/UserWidget.h"
// ── Ctrl+N-пикер разрешения/качества (dev) ──

// (Ctrl+N-пикер УЕХАЛ в общий UAsdaDevMenuSubsystem — dev-меню настроек работает на всех картах.)
// Цель фокуса `FInputModeUIOnly` для корня окна (определение — AsdaWindowSubsystem_Dialogs.cpp, ключ `asda.UI.ModalFocusNoRoot`).
TSharedPtr<SWidget> AsdaUiOnlyFocusTarget(UUserWidget* Root);
