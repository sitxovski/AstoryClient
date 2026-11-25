// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPetEvolutionSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaPetEvolutionSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaPetEvolutionSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaPetEvolutionSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaPetEvolutionSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "UI/AsdaJuiWindowHandle.h"
#include "Net/NetOpcodes.h"

// Категория журнала частей AsdaPetEvolutionSubsystem*.cpp: определение — AsdaPetEvolutionSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaPetEvo, Log, All );

// ── Идентификаторы и пути трёх окон ──
static const FName  kEvoWindowId(TEXT("Pet_Evolution_Win"));
static const FName  kGridWindowId(TEXT("Pet_Inven_Win"));
static const FName  kTipWindowId(TEXT("Pet_Inven_Tooltip"));

namespace PetEvoLoc {
// ══════════════════════════════════════════════════════════════════════════════════════════════
//  LOC-ИДЕНТИФИКАТОРЫ. Каждый снят по СВОЕМУ обработчику эталона (закон
//  `loc-per-handler-not-by-family`), файл:строка — рядом с каждой строкой.
//  ★ Ни один id из списка «несуществующих» брифинга здесь не используется.
// ══════════════════════════════════════════════════════════════════════════════════════════════
	// ── Подписи окна — `uiPetDialogInit@0xEDC7C0` (ui_core.c:89789-89907) ──
	constexpr int32 kTitle      = 109960;  // :89813  43501 «Карта развития питомца»
	constexpr int32 kMatTitle0  = 109964;  // :89851  43743+m, цикл `m + 109964`: 109964/109965/109966
	constexpr int32 kBtnOk      = 109933;  // :89869  43876 «Развитие»
	constexpr int32 kBtnEnd     = 109934;  // :89872  43879 «Закрыть»
	constexpr int32 kGridTitle  = 109962;  // :89880  43431 «Вещи питомца»
	constexpr int32 kTipGrowth  = 109963;  // :89891  43453 «Рост»

	// ── Открытие — `uiPetDialog_Open@0xEDD450` (ui_core.c:89955-89992) ──
	constexpr int32 kGateSummoned = 109974; // :89964  ГЕЙТ: питомец призван, окно НЕ открывается
	constexpr int32 kHintOpened   = 109967; // :89979  43877 «Питомцы, готовые к развитию, ярко…»

	// ── Выбор стадии — `skillTreeOnTabSelect@0xEE00D0` (skill_buff.c:37953-37978) ──
	constexpr int32 kHintPickMats = 109968; // skill_buff.c:37965  43877 «В окне справа подтверди…»

	// ── Кнопка «Развитие» — `uiCraftCheckSlotsAndShowTooltip@0xEDFE00` (ui_core.c:90297-90320) ──
	constexpr int32 kConfirm70   = 109972;  // :90318  msgbox 70 «Будут использованы все материалы…»
	constexpr int32 kNoMats      = 109971;  // :90322  43877 «Не выбраны необходимые материалы»
	constexpr int32 kMsg70Ok     = 108510;  // item_inventory.c:51110 кнопка «ОК» msgbox 70
	constexpr int32 kMsg70No     = 108511;  // item_inventory.c:51113 кнопка «Отмена»

	// ── Три слота материалов — `itemPanel3SlotRefresh@0xEDE050` (item_inventory.c:65057-65188) ──
	constexpr int32 kMatReady    = 109970;  // :65165  43749+m «Питомец готов» (слот занят)
	constexpr int32 kMatEmpty    = 109969;  // :65177  43749+m «Не выбрано»    (слот пуст)

	// ── Приём 6113 — `petUI_OnPetDataPacket@0xEE0920` (npc_mob_pet.c:7645-7689) ──
	constexpr int32 kUnionDone   = 109973;  // :7684  «Развитие питомца завершено…» — ТОЛЬКО код 1

	// Кнопка однокнопочного нотиса (канал A) — loc 2288 «ОК» (item_inventory.c:50880).
	constexpr int32 kBtnOkNotice = 2288;
}
