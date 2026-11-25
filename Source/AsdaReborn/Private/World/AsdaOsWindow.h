// Copyright (c) 2026 @sitxovski (Discord)
// AsdaOsWindow.h — ★ R5-fix6 (13.11.2026): есть ли у игры НАСТОЯЩЕЕ окно операционной системы.
//   ЗАЧЕМ. Две точки порта читают ФИЗИЧЕСКОЕ состояние устройств мимо событий ввода — кнопки мыши (`KbRawMouseDown`, эталон
//   `GetAsyncKeyState(1/2)` в ходе клавишами) и PrintScreen (`IsBindKeyDown(44)`), — и обе закрыты условием «окно игры на переднем
//   плане». При запуске без окна (`-RenderOffscreen`: самотесты, общая проверка собранной игры) движок создаёт Null-приложение
//   (WindowsPlatformApplicationMisc.cpp:173), а его окно на вопрос «на переднем плане?» ВСЕГДА отвечает «да»
//   (`FNullWindow::IsForegroundWindow`, ApplicationCore/Private/Null/NullWindow.cpp:194). Скрытая игра читала мышь машины: любой
//   настоящий щелчок ЛКМ гасил зажатые самотестом клавиши хода («ЛКМ гасит все четыре флага»), ПКМ включала стрейф — клавиатурные
//   самотесты краснели случайно (журнал rx_r5fix3, `SwimBack.try2`: зажатая S «отпущена» на 92 мс и вернулась сама).
//   ПРАВИЛО. Физическое состояние устройств читается, только когда у окна игры есть хэндл ОС. У игры с окном ничего не меняется.
#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GenericPlatform/GenericWindow.h"
#include "Widgets/SWindow.h"

// true — у окна игры есть хэндл операционной системы (обычный запуск); false — окна нет (`-RenderOffscreen`) или вьюпорт не создан.
inline bool AsdaGameHasOsWindow()
{
	const UGameViewportClient* VP = GEngine ? GEngine->GameViewport.Get() : nullptr;
	const TSharedPtr<SWindow> W = VP ? VP->GetWindow() : nullptr;
	const TSharedPtr<FGenericWindow> Native = W.IsValid() ? W->GetNativeWindow() : nullptr;
	return Native.IsValid() && Native->GetOSWindowHandle() != nullptr;
}
