// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPetSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaPetSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaPetSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaPetSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "Pet/AsdaPetSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ В2-A дедупа №2: общая точка «это я?» (класс/уровень/charId/userNum)

#include "Net/NetOpcodes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Items/AsdaItemDbRows.h"           // ★ волна 8: FAsdaPetRow / FAsdaPetexptableRow
#include "Text/AsdaLangRows.h"               // [ПОРТ] 09.10.2026 (мультиязычность): имя стадии питомца на языке сессии
#include "UI/AsdaNotice.h"            // ★ волна 8: общая точка нотиса AsdaPetUi::NoticeMsgBox               // ★ волна 8: обход `petexptable` в AsdaPetUi::ExpRow
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)

// Категория журнала частей AsdaPetSubsystem*.cpp: определение — AsdaPetSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaPet, Log, All );

// Ключ CVarPetLimitBreakFixTable: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarPetLimitBreakFixTable;
#else
extern TAsdaConstCVar<int32> CVarPetLimitBreakFixTable;
#endif

namespace AsdaPetUi {
// PetRow: определение — в домашней части (R2).
bool PetRow(const UGameInstance* GI, int32 PetIndex, FAsdaPetRow& Out);

// ExpRow: определение — в домашней части (R2).
bool ExpRow(const UGameInstance* GI, int32 Pos, FAsdaPetexptableRow& Out);

// ExpFraction: определение — в домашней части (R2).
bool ExpFraction(const UGameInstance* GI, const FAsdaPetSlot& S, float& OutFrac);

// DrainEvents: определение — в домашней части (R2).
void DrainEvents(TArray<TTuple<int32, int32, int32>>& Queue,
		TFunctionRef<void(int32, int32, int32)> Handler);

// IsSendLinkUp: определение — в домашней части (R2).
bool IsSendLinkUp(const UGameInstance* GI);

// SendTimeoutSec: определение — в домашней части (R2).
float SendTimeoutSec();

// NoticeMsgBox: определение — в домашней части (R2).
void NoticeMsgBox(const UGameInstance* GI, int32 LocId, int32 EtalonNoticeId);

// StandI32: определение — в домашней части (R2).
int32 StandI32(const TArray<uint8>& B, int32 Ofs);
// StandI16: определение — в домашней части (R2).
int32 StandI16(const TArray<uint8>& B, int32 Ofs);
// StandU8: определение — в домашней части (R2).
int32 StandU8(const TArray<uint8>& B, int32 Ofs);
}
