// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_Entities_Private.h — ★ R3-c 12.11.2026 (разрез AsdaWorldSubsystem_Entities.cpp): ЗАКРЫТЫЙ заголовок частей AsdaWorldSubsystem_Entities*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaWorldSubsystem_Entities.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "World/AsdaAttackExit.h"   // ★ 01.11.2026: 4024/400 — выход своего из действия атаки (AsdaLeaveAttackAction)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "HAL/IConsoleManager.h"        // ключ `asda.Move.RefusePlank`
#include "World/AsdaPlayerCharacter.h"
#include "Npc/AsdaMobActor.h"
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "AsdaEffectActor.h"                 // эффекты персонажей: крылья/кольца soulmate (follow-кость)
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
// loc-нотисы подбора (1011/1012/1030)   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
// UAsdaRichText::ApplyFormatted (единая точка текста)   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)

// Ключ CVarAsdaAoiSectorTiles: определение — в домашней части (R2, без static).
//   ★ R3-c чистка: в Shipping ключ — константа-умолчание, своя копия в каждой части (как до разреза: значение
//   сворачивается компилятором, внешнего объекта нет). Умолчание — одно число на обе ветки.
static constexpr int32 kAsdaAoiSectorTilesDefault = 0;
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarAsdaAoiSectorTiles;
#else
static TAsdaConstCVar<int32> CVarAsdaAoiSectorTiles(kAsdaAoiSectorTilesDefault);
#endif
