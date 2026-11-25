// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEffectsSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaEffectsSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaEffectsSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaEffectsSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaEffectsSubsystem.h"
#include "Data/AsdaFxLoopRows.h"   // ★ дедуп №2 В2-C (F3): AsdaFxLoop::kTablePath

#include "AsdaEffectActor.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"

// Категория журнала частей AsdaEffectsSubsystem*.cpp: определение — AsdaEffectsSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaBuff, Log, All );

// ★ 30.10.2026: ключ `asda.Mount.PortalReapply` (определение и разбор — World/AsdaWorldSubsystem_Peers.cpp): транспорт своего павна
//   повторно вешается из своего списка состояний на каждой своей 4003 (портал, смена канала, вход).
bool AsdaMountPortalReapplyOn();

static int64 AsdaNowMs()
{
	return (int64)(FPlatformTime::Seconds() * 1000.0);
}

// Ключ ожидания отложенного спавна эффекта состояния: (носитель — сущность?, userIdx/renderId, id состояния).
static FORCEINLINE int64 AsdaBuffFxKey(int32 Key, bool bMob, int32 StateId)
{
	return ((int64)(bMob ? 1 : 0) << 48) | ((int64)(uint32)Key << 16) | (int64)(uint16)StateId;
}

// GAsdaItemFxStrictQuad: определение — в домашней части (R2).
extern bool GAsdaItemFxStrictQuad;
