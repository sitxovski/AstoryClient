// Copyright (c) 2026 @sitxovski (Discord)
// AsdaHotbarSubsystemInternal.h — ★ R2 10.11.2026 (разрез AsdaHotbarSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaHotbarSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaHotbarSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaHotbarSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaSoulmateSubsystem.h"    // ★ 06.11.2026: пара и «Друг в игре» — из карточки (`asda.Soul.SlotTruth`)
#include "UI/AsdaCharaInfoSubsystem.h"   // ★ 06.11.2026: перерисовка лотков действий при смене пары
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaItemCountStyle.h"   // ★ 26.10.2026: обводка числа стопки — общая точка (ключ asda.UI.ItemCountOutline)
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "AsdaBytes.h"   // D4: единые читалки сырой записи (little-endian, DataHex, ASCII-поле)
#include "UI/AsdaUiHelpers.h"   // ★ D2/D4: единые UI-хелперы (дедуп 2026-08-08)

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMiniStateSubsystem.h"        // один общий тултип на HUD (эталон держит один g_ItemTooltip)
// владелец общей плашки (порт g_ItemTooltip)   ★ R3: комментарий снятого включения "UI/AsdaItemTooltipSubsystem.h" (файл им не пользовался) — сохранён
// ★ ТОЛЬКО ЗАГОЛОВОК, поведение не меняется: ниже (мост к окну навыков) уже используется
//   `UAsdaSkillWindowSubsystem`, но #include отсутствовал — тип «доезжал» через unity-сборку.
//   Как только этот файл попадает в отдельную единицу трансляции, компиляция падала.
#include "UI/AsdaSkillWindowSubsystem.h"
// сводная сверка шторок отката (`asda.Cooldown.Dump`)   ★ R3: комментарий снятого включения "UI/AsdaInventorySubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaBoosterPanelSubsystem.h"  // ★ гейт `CObject_GetField254(g_pInventoryWnd)` = ОКНО ГАЧИ 10050
#include "UI/AsdaChatWindowSubsystem.h"    // ★ гейт `Input_IsTextInputFocused(g_pChatInputWnd)`
#include "UI/AsdaSkillDatabase.h"          // ★ Аудит №3 (A13): имя иконки навыка — только отсюда
#include "UI/AsdaAnimatedPlane.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Login/AsdaText.h"   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): FAsdaText::Loc — заголовки подсказок по id
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaKeybindConfig.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Engine.h"   // ★ 17.10.2026 GEngine->Exec(quit) в самотесте иконок эмоций
#include "Components/CanvasPanel.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Engine/DataTable.h"
#include "Items/AsdaItemDbRows.h"
#include "HAL/IConsoleManager.h"
#include "UObject/ObjectKey.h"       // ★ 26.10.2026: TObjectKey — ревизия списка навыков по экземпляру панели
#include "UI/AsdaCraftSubsystem.h"   // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: окно обмена 2008 открыто — гейт слотов и значков заявки 4/5
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "World/AsdaFishingSubsystem.h"     // ★ 08.10.2026 (рыбалка В4): гейт «идёт рыбалка» в `IsActivationBlocked`
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`
#include "UI/AsdaHelpTopics.h"             // ★ 30.10.2026: справочные темы кнопок листания и пустого слота оружия
#include "UI/AsdaDeathInputLock.h"          // ★ 05.11.2026: меню смерти запирает слоты (эталон CUIManager+0x44, ui_core.c:67351)

// Категория журнала частей AsdaHotbarSubsystem*.cpp: определение — AsdaHotbarSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaQuick, Log, All );

static const FName  kQuickWindowId(TEXT("QUICK_NEW_WIN"));

static constexpr int32 kFlashFrames   = 16;

// AsdaCooldownNowS: определение — в домашней части (R2).
double AsdaCooldownNowS(const UObject* Ctx);

// [ОРИГИНАЛ] Эталон `ui_core.c:66420-66431` (useFullSize): эти три иконки эмоций — текстуры 64×64 и рисуются целиком,
//   остальные `act_*` — 32×32. Правило по ИМЕНИ, поэтому оно действует при любом источнике имени (таблица или мост окна эмоций).
// ★ 17.10.2026 одна функция для таблицы и моста: раньше при поднятом мосте `Misc.GetEmotionIconName` признак терялся и
//   у трёх иконок рисовалась левая верхняя четверть ×2 (регресс 11.10, разбор `Reports/emote_icons_2026-10-17/REPORT.md`).
static bool IsFullSizeEmotionIcon(const FString& Tex)
{
	return Tex == TEXT("act_invite") || Tex == TEXT("act_itemchange") || Tex == TEXT("act_itemchange2");
}

// AsdaSoulSlotTruthOn: определение — в домашней части (R2).
bool AsdaSoulSlotTruthOn();
