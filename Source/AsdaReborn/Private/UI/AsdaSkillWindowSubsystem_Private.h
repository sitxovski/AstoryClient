// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillWindowSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaSkillWindowSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaSkillWindowSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaSkillWindowSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaSkillWindowSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaHotbarSubsystem.h"   // ★ 06.11.2026: «Друг в игре» для подсказки навыка Дружбы
#include "UI/AsdaSoulmateSkillSubsystem.h"   // ★ 07.11.2026: AsdaSoulTrayFlowOn
#include "UI/AsdaSoulmateSubsystem.h"        // ★ 07.11.2026: уровень Дружбы для подсказки
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "AsdaSelf.h"   // ★ В2-A дедупа №2: общая точка «это я?» (класс/уровень/charId/userNum)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 30.09.2026 (волна Л5 торговли): гейт «лавка открыта» 22000/23000 → 107022

#include "UI/AsdaSkillDatabase.h"
#include "UI/AsdaSkillUseSubsystem.h"     // ★ боевой диспетчер применения навыков (мост к панели)
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaAnimatedPlane.h"            // вспышка CED — кадровый план
#include "UI/AsdaSoulGuardSubsystem.h"       // таблица SoulGuarderTable для левой панели 56300
#include "Data/AsdaSoulGuardRows.h"
#include "UI/AsdaMessageSubsystem.h"
#include "AsdaBytes.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaLocIds.h"                 // ★ Аудит №3 (A1): нотисы кнопок ОБЩИХ окон (2288 «ОК»)
#include "UI/AsdaScrollBarNomal.h"         // ★ Аудит №3 (A1): общая полоса прокрутки скина `Nomal_Scr`
#include "AsdaNetSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#if PLATFORM_WINDOWS
// ★ Системный интервал двойного клика: оригинал получает событие 0xD от самой Windows
//   (`WM_LBUTTONDBLCLK`), то есть по значению «Панель управления → Мышь». В Slate такого запроса нет
//   (`FSlateApplication` умеет только маршрутизировать уже готовое событие), поэтому берём его у ОС
//   тем же вызовом, каким его берёт оконная процедура оригинала.
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/MinWindows.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include "Components/CanvasPanel.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "UI/AsdaCraftSubsystem.h"   // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Text/AsdaLangRows.h"               // [ПОРТ] 09.10.2026 (мультиязычность): подсказка навыка соулмейта на языке сессии
#include "UI/AsdaCloseGear.h"   // ★ 08.11.2026: шестерёнка у крестика — общая точка (asda.UI.CloseGearHover)

// Категория журнала частей AsdaSkillWindowSubsystem*.cpp: определение — AsdaSkillWindowSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaSkillWin, Log, All );

static const FName  kSkillWinId = FName(TEXT("SKILL_WIN"));

static constexpr int32 kLocJob4Button       = 111500;  // текст кнопки 4-й профессии
static constexpr int32 kLocJob4TipTitle     = 111800;  // подсказка кнопки 4-й профессии (заголовок)

// GAsdaSkillLearnedRev: определение — в домашней части (★ R3-c 12.11.2026).
extern uint32 GAsdaSkillLearnedRev;

// ── Откаты правок ──
// ★ ДЕФЕКТ ОРИГИНАЛА (разбор `RE/docs/ОЧКИ_УМЕНИЙ.md`): при уровне ≥ 60 эталон ПЕРЕТИРАЕТ присланное
//   сервером число очков выдуманной формулой «110 − потрачено» (`skillwin_OpenSkillWindow@0xE530CC` и
//   `skillUiUpdatePointText@0xE56790`). Порт по умолчанию показывает значение СЕРВЕРА.
// ★ 25.10.2026: часы и ключи реестра откатов — определены в AsdaHotbarSubsystem.cpp (шапка «ОТКАТЫ: ЧАСЫ, ПРОГОН ШТОРКИ,
//   СНИМОК 5465»); здесь только аксессоры (чужой ключ — функцией, не поиском по имени).
double AsdaCooldownNowS(const UObject* Ctx);
bool AsdaCooldownRealClockOn();

// Ключ CVarCooldownArrivalOrder: определение — в домашней части (★ R3-c 12.11.2026, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarCooldownArrivalOrder;
#else
extern TAsdaConstCVar<int32> CVarCooldownArrivalOrder;
#endif

// Лимит основных деревьев — все очки до 59-го уровня (2·59−8); константа эталона `skillUiUpdatePointText@0xE56720` / `Skill_BuildTreeUI`.
static constexpr int32 kMainTreePointCap = 110;
