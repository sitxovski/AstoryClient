// Copyright (c) 2026 @sitxovski (Discord)
// AsdaInventorySubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaInventorySubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaInventorySubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaInventorySubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaInventorySubsystem.h"
#include "Misc/ConfigCacheIni.h"   // ★ 07.11.2026: `asda.Inven.RememberTab` — GConfig
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "AsdaBytes.h"   // D4: единые читалки сырой записи (little-endian, DataHex, ASCII-поле)
#include "AsdaPacketWriter.h"   // ★ U6 (В1 дедупа №2): 15 байт слотов-приёмников 6096 через общий писатель
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaScrollBarNomal.h"   // D3: общая полоса прокрутки скина `Nomal_Scr`            // ★ D1/D2/D5: единые UI-хелперы (дедуп 2026-08-08)

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaMiniStateSubsystem.h"   // общий тултип HUD (эталон держит один g_ItemTooltip)
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaWarehouseSubsystem.h"   // свиток возврата склада (5451 подтип 112) → окно 5500
#include "UI/AsdaSelfStoreSubsystem.h"   // билет премиум-лавки (5451 подтип 65) → 6300 (волна Л1 торговли)
// ★ Волна 1 «применение предметов»: эффект/звук по `linkCode` и состояния 265/266 из 5029 —
//   через подсистему эффектов (своих копий спавна и поиска актора не заводим, [[port-shared-points]]).
#include "UI/AsdaEffectsSubsystem.h"
// ★ Только ради ОДНОЙ статической таблицы цветов заточки (`itemGetGradeColor@0xD29940`) — второй
//   копии палитры не заводим (закон «аудит дублей»). Сам файл тултипа отсюда не правится.
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaPetCageSubsystem.h"      // ★ кнопка 454 — окно питомца 43000 (волна 2)
#include "UI/AsdaItemUpgradeSubsystem.h"  // ★ кнопка 449 — окно улучшения снаряжения 20000 (25.09.2026)
#include "UI/AsdaCraftWindowSubsystem.h"   // ★ кнопка 452 — окно создания снаряжения 76000 (ремесло, волна В2)
#include "UI/AsdaDisassemblySubsystem.h"    // ★ кнопка 453 — окно разборки снаряжения 32000 (волна 2-А, 06.10.2026)
#include "UI/AsdaUpgradeStoneSubsystem.h"   // ★ 31.10.2026: открытие сумки снимает взвод камня переноса улучшения
#include "UI/AsdaCraftSubsystem.h"         // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: гейты рюкзака и бустера — окно обмена 2008 открыто (`CExchange+0x2C`)
#include "UI/AsdaFishingToolSubsystem.h"   // ★ окно «Взять рыболовную снасть» 15900 (точки открытия/закрытия)
#include "UI/AsdaSowelSubsystem.h"         // ★ ветка САМОЦВЕТОВ правой панели (430…437 / 441…446 / 495 / 499)
#include "UI/AsdaPetHatcherySubsystem.h"   // ★ окно ИНКУБАТОРА 42000 (двойной клик по предмету subType 107)
#include "UI/AsdaPremiumGachaSubsystem.h"  // ★ гейт `sub_922D5D@0xB6F370` — гейты №15/№15b применения лопат
#include "Items/AsdaItemTableSubsystem.h"
#include "AsdaNetSubsystem.h"
#include "World/AsdaPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/Button.h"
#include "Engine/Engine.h"
// ★ Явно, а не «через unity»: файл зовёт `GI->GetSubsystem<>()` больше сотни раз. Без этого include
//   одиночная сборка файла (`Build.bat … -SingleFile=…`) не проходит — а она единственный способ
//   проверить компиляцию, пока в редакторе активен Live Coding и полная сборка запрещена.
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/InputSettings.h"   // DoubleClickTime — интервал двойного клика Slate
#include "HAL/IConsoleManager.h"
#include "AsdaInventorySubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaInventorySubsystem*.cpp (LogAsdaInven, чистые хелперы)

// Ключ CVarLegacyNoAutoArrange: определение — в домашней части (★ R3-c 12.11.2026, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarLegacyNoAutoArrange;
#else
extern TAsdaConstCVar<int32> CVarLegacyNoAutoArrange;
#endif
