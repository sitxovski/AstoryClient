// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPartySubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaPartySubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaPartySubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaPartySubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaPartySubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI

#include "HAL/IConsoleManager.h"

// Категория журнала частей AsdaPartySubsystem*.cpp: определение — AsdaPartySubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaParty, Log, All );

namespace {
	// ★ Панель отряда в HUD — окно `PARTY_WIN` 2200 и окно подтверждения `PARTYDISPERSION` 2256.
	const FName  kRosterWinId(TEXT("PARTY_WIN"));
	const FName  kDisbandWinId(TEXT("PARTYDISPERSION"));

	constexpr int32 kRosterSlots = 6;    // строки `PARTY_WIN00…05` (2201…2206)
}

namespace {
namespace PartyLoc {
// ── Идентификаторы строк. Все — из тел эталонных функций, см. §5 заголовка. ──
		constexpr int32 kInviteAsk      = 20020;  // «Пригласить игрока [ss1] в отряд?»    → 2251
		constexpr int32 kJoinAskFree    = 20021;  // «[ss1] приглашает… (кто успел)»        → 2246
		constexpr int32 kJoinAskAuto    = 20022;  // «[ss1] приглашает… (автораспределение)»→ 2246
		constexpr int32 kRuleFree       = 1167;   // «Распределение вещей: кто успел»       → 2255
		constexpr int32 kRuleAuto       = 1168;   // «Распределение вещей: автоматически»   → 2255
		constexpr int32 kComboFree      = 12153;  // пункт 0 списка 2254
		constexpr int32 kComboAuto      = 12154;  // пункт 1 списка 2254

		constexpr int32 kSelfInvite     = 1093;   // гейт «цель — я сам»
		constexpr int32 kAlreadyInParty = 1047;   // гейт «уже в моём отряде», и коды 8/4 ответов
		constexpr int32 kOneAtATime     = 1056;   // гейт «моя заявка в полёте»
		constexpr int32 kHasIncoming    = 1045;   // гейт «висит входящая заявка»
		constexpr int32 kNotLeader      = 1089;   // гейт «я не глава отряда»
		constexpr int32 kIncomingSound  = 5057;   // «Тебя приглашают в отряд.»
		constexpr int32 kBtnBanish      = 12001;  // подпись 2262 «Выгнать» (social.c:1927)
		constexpr int32 kBtnLeave       = 12002;  // подпись 2260 «Выход»   (social.c:1925)
		constexpr int32 kNobodyToBanish = 1090;   // «Некого выгонять из отряда»
		constexpr int32 kMenuMarkTarget  = 9488;  // «Указать цель» — подпись 8102
		constexpr int32 kMenuFollow      = 6958;  // «Следовать»    — подпись 8101 (та же, что у 2062)
		constexpr int32 kMenuClose       = 5035;  // «Закрыть»      — подпись 8103 (та же, что у 2052)
		constexpr int32 kMarkTargetSet   = 20307; // «общая цель указана» (msgId 8102, слот-имя)
		constexpr int32 kTargetNotFound  = 20308; // «цель не найдена»
		// ★ 25.10.2026: нотисы «общей цели» кат. 2/6 (ui_core.c:32208-32285; RU GameText.csv, EN DT_Loc_GameText_en.csv).
		constexpr int32 kShareNoTarget   = 20300; // «Не с кем делиться» / «No target to share» — у соотрядника нет цели
		constexpr int32 kShareMateDead   = 20301; // «В настоящий момент член отряда мертв»
		constexpr int32 kShareSelfDead   = 20302; // «Состояние: труп»
		constexpr int32 kShareNotMarked  = 20304; // «Выбери члена отряда, которому хочешь указать цель»
		constexpr int32 kShareOnVehicle  = 20305; // «Сначала покинь ТС»
		constexpr int32 kShareNotInParty = 20306; // «Позволено только членам отряда»
		constexpr int32 kDisbandAsk      = 1170;  // «Ты действительно хочешь разделить отряд?» → 2257 (0xB99C30, push 0x492)
}
}

// Ключ CVarFixContextMenuInvite: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarFixContextMenuInvite;
#else
extern TAsdaConstCVar<int32> CVarFixContextMenuInvite;
#endif

// Ключ CVarShowLootRuleForLeader: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarShowLootRuleForLeader;
#else
extern TAsdaConstCVar<int32> CVarShowLootRuleForLeader;
#endif

// Ключ CVarTrackLootRule: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarTrackLootRule;
#else
extern TAsdaConstCVar<int32> CVarTrackLootRule;
#endif

// Ключ CVarSwapRowHighlights: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarSwapRowHighlights;
#else
extern TAsdaConstCVar<int32> CVarSwapRowHighlights;
#endif

// Ключ CVarAutoOpenJoinBox: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarAutoOpenJoinBox;
#else
extern TAsdaConstCVar<int32> CVarAutoOpenJoinBox;
#endif

// Ключ CVarFixDisbandConfirm: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarFixDisbandConfirm;
#else
extern TAsdaConstCVar<int32> CVarFixDisbandConfirm;
#endif

// Ключ CVarMapMarkNeedPos: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarMapMarkNeedPos;
#else
extern TAsdaConstCVar<int32> CVarMapMarkNeedPos;
#endif

namespace {
	// ── Таймауты эталона (`partyInviteTimeoutTick@0xB9B070`, social.c:3108) ──
	constexpr double kIncomingTimeoutSec = 30.0;   // 0x7530 мс — и ЖЕ диапазон шкалы 2247
	constexpr double kMyRequestTimeoutSec = 32.0;  // 0x7D00 мс — снятие «моя заявка в полёте»
}
