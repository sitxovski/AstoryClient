// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaWorldSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaWorldSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaWorldSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "World/AsdaWorldSubsystem.h"

#include "World/AsdaPlayerCharacter.h"
#include "Npc/AsdaMobActor.h"
// тип оружия атакующего (импакт battk01..04)   ★ R3: комментарий снятого включения "Items/AsdaItemTableSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaEffectsSubsystem.h"         // ★ состояния мобов: 4017 бафф-стеки и бафф отражения
#include "ProceduralMeshComponent.h"               // ★ 09.11: площадка города по рельефу
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
// loc-нотисы подбора (1011/1012/1030)   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
// UAsdaRichText::ApplyFormatted (единая точка текста)   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)

// ★ 05.11.2026 (stun-attack): ключ `asda.Combat.ControlStopsAttack` (определение — AsdaNetSubsystem_Combat.cpp).
bool AsdaControlStopsAttackOn();
// ★ 06.11.2026: усиления из серверного списка моба (определение и разбор — AsdaEffectsSubsystem.cpp).
int32 AsdaMobAddListedBuffs(UAsdaEffectsSubsystem* Fx, int32 Rid, const TArray<int32>& Ids, const TArray<int32>& Levels, bool bSpawnRecord);

// GAsdaMobClipBaseModel: определение — в домашней части (R2).
extern int32 GAsdaMobClipBaseModel;

// ★ 29.10.2026: ключ `asda.Soulmate.RingCacheSync` (определение и разбор — AsdaWorldSubsystem_Peers.cpp): кэш колец 6152 — данные сессии.
bool AsdaSoulRingCacheSyncOn();
// ★ 30.10.2026: ключ `asda.Mount.PortalReapply` (определение и разбор — AsdaWorldSubsystem_Peers.cpp): транспорт своего павна после портала.
bool AsdaMountPortalReapplyOn();
// ★ 30.10.2026: ключ `asda.Possess.BodyIdentity` (определение и разбор — AsdaNetSubsystem_Session.cpp): имя и вид тела при одержимости.
bool AsdaPossessBodyIdentityOn();
// ★ 05.11.2026: ключ `asda.Peer.Revive4056` и разбор 4056 о соседе (определения и разбор — AsdaNetSubsystem.cpp).
bool AsdaPeerRevive4056On();
bool AsdaPeerEntry4056(int32 Uidx, int32& OutTileX, int32& OutTileZ, bool& bOutDeadArrival);

// Прототип: шлейф/звук езды из Ride_effect_table (определение ниже, рядом с LookupMount).
static bool LookupRideFx(int32 ItemCode, FString& OutEffect, FString& OutSound,
	FString& OutSound2, FString& OutSound3);
