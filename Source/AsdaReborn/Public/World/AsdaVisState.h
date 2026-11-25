// Copyright (c) 2026 @sitxovski (Discord)
// Уровень и состояние значимости объекта (`EAsdaVisLevel`, `FAsdaVisState`) — поле потребителя; разбор системы — в шапке `World/AsdaVisibilitySubsystem.h`.
//   ★ R3 рефакторинга (11.11.2026): дословно вынесено из `World/AsdaVisibilitySubsystem.h` (он включает этот файл — прежние потребители ничего
//   не теряют). Причина: заголовки, которым нужно только поле-состояние (`AsdaEffectActor.h`, `World/AsdaNameplateComponent.h` — переведены на этот файл), включали всю подсистему значимости.
//   Отражение UE (имя типа, модуль `/Script/AsdaReborn`) не менялось.
#pragma once
#include "CoreMinimal.h"
#include "AsdaVisState.generated.h"

// Уровень значимости (см. выше). Хранится в FAsdaVisState как uint8.
UENUM()
enum class EAsdaVisLevel : uint8
{
	Off  = 0,   // не видно и не рядом — заморозить
	Far  = 1,   // в кадре с запасом, далеко — половинная частота
	Near = 2    // рядом/в кадре близко — полная частота
};

// Состояние значимости ОДНОГО объекта. Живёт у потребителя (компонент/актор), подсистема реестра не держит.
USTRUCT()
struct FAsdaVisState
{
	GENERATED_BODY()

	// Последний посчитанный уровень. Стартуем с Near: до первого пересчёта объект работает как раньше
	// (иначе на кадре появления была бы вспышка «выключен → включён»).
	UPROPERTY() uint8 Level = (uint8)EAsdaVisLevel::Near;
	// Фаза разноса по кадрам назначена (0 — объект ещё ни разу не считался).
	UPROPERTY() uint8 bHasPhase = 0;
	// Слот разноса 0..Period−1: объект пересчитывается на кадрах, где (FrameCounter % Period) == Phase.
	UPROPERTY() uint8 Phase = 0;
	// Уровень изменился на последнем ПЕРЕСЧЁТЕ (на кадрах-пропусках всегда 0). Потребитель ловит переход 0→≥1.
	UPROPERTY() uint8 bChanged = 0;
};
