// Copyright (c) 2026 @sitxovski (Discord)
// AsdaInventorySubsystem_Net_Private.h — ★ R3-c 12.11.2026 (разрез AsdaInventorySubsystem_Net.cpp): ЗАКРЫТЫЙ заголовок частей AsdaInventorySubsystem_Net*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaInventorySubsystem_Net.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaInventorySubsystem.h"
#include "AsdaWeightWire.h"   // ★ 29.10.2026 (вес ×2): AsdaWeightFromWire — вес персонажа по проводу u16 (5029, 6097, 5469, 5451, 5007)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "AsdaBytes.h"   // D4: единые читалки сырой записи (little-endian, DataHex, ASCII-поле)
#include "AsdaPacketWriter.h"   // ★ U6 (В1 дедупа №2): 15 байт слотов-приёмников 6096 через общий писатель
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaMiniStateSubsystem.h"   // общий тултип HUD (эталон держит один g_ItemTooltip)
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaWarehouseSubsystem.h"   // свиток возврата склада (5451 подтип 112) → окно 5500
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaItemUpgradeSubsystem.h"  // ★ кнопка 449 — окно улучшения снаряжения 20000 (25.09.2026)
#include "UI/AsdaCraftWindowSubsystem.h"   // ★ кнопка 452 — окно создания снаряжения 76000 (ремесло, волна В2)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: гейты рюкзака и бустера — окно обмена 2008 открыто (`CExchange+0x2C`)
#include "Resources/AsdaSoundSubsystem.h"  // ★ кнопка 449 — звук id 5 ветки 66561 (волна 5 заточки)
// ★ обмен по таблице событий (двойной клик по предмету subType 202 → 6098)   ★ R3: комментарий снятого включения "Npc/AsdaNpcDialogSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaPremiumGachaSubsystem.h"  // ★ гейт `sub_922D5D@0xB6F370` — гейты №15/№15b применения лопат
#include "Items/AsdaItemTableSubsystem.h"
#include "World/AsdaPlayerCharacter.h"     // ★ 09.10.2026 (п. 5): состояние лопаты 40/43 своего персонажа (оружие за спину)
#include "AsdaNetSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/InputSettings.h"   // DoubleClickTime — интервал двойного клика Slate
#include "HAL/IConsoleManager.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "AsdaInventorySubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaInventorySubsystem*.cpp (LogAsdaInven, чистые хелперы)
#include "Quest/AsdaQuestLogSubsystem.h"         // ★ 31.10.2026: ответ 5007 на отказ от задания — журналу заданий (ConsumeItemDump5007)

bool AsdaDuelLoserLocked(const class AAsdaPlayerCharacter* Ch);   // ★ 31.10.2026: жест поражения дуэли
