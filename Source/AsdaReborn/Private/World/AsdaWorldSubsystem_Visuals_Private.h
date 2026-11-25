// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_Visuals_Private.h — ★ R3-c 12.11.2026 (разрез AsdaWorldSubsystem_Visuals.cpp): ЗАКРЫТЫЙ заголовок частей AsdaWorldSubsystem_Visuals*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaWorldSubsystem_Visuals.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта

// ★ 05.11.2026: обводка самоцвета своего оружия/щита (определение и разбор — AsdaSowelGlow.cpp).
void AsdaSowelGlowRefresh(AAsdaPlayerCharacter* Ch);
