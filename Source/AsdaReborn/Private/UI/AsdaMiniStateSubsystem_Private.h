// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMiniStateSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaMiniStateSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaMiniStateSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaMiniStateSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaMiniStateSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ U13 (В1 дедупа №2): AsdaNotice::ConsoleEcho — эхо консольных команд
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaUiHelpers.h"   // ★ D4: единый замер высоты строки (дедуп 2026-08-08)
#include "UI/AsdaItemTooltipSubsystem.h"       // ★ ОДНА плашка-тултип на игру (порт g_ItemTooltip)
#include "UI/AsdaTipPanel.h"

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"           // ЭТАП 2: модель активных эффектов + ивент-бонусы
#include "AsdaNetSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaPlayerCharacter.h"   // ★ 06.10.2026 (ревизия МЕЛК-3): точный признак смерти — AnimState == Dead
#include "World/AsdaNameplateComponent.h"   // ★ 06.10.2026: данные клана своего персонажа (id, флаг, дизайн)
#include "World/AsdaClanFlag.h"             // ★ 06.10.2026: UAsdaClanFlag::MakePlateBrush — общая кисть эмблемы
#include "UI/AsdaPartySubsystem.h"          // ★ 06.10.2026: состав отряда (5102 ищет главу в составе)
#include "UI/AsdaTargetFrameSubsystem.h"    // ★ 06.10.2026: двойной клик — полная пересборка панели цели
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя — своё HP/MP в HUD, «призрачный» слой, сброс при смерти

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
// inline-теги <FC=…>…</F> в тексте тултипа 110922   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"

// Категория журнала частей AsdaMiniStateSubsystem*.cpp: определение — AsdaMiniStateSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaMini, Log, All );
