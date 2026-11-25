// Copyright (c) 2026 @sitxovski (Discord)
// Категория игрового сообщения (`EAsdaMsgCategory`) — единственное, что сообщает вызывающий код: смысл события, а не окно.
//   ★ R1 рефакторинга (10.11.2026): дословно вынесена из `UI/AsdaMessageSubsystem.h` (он включает этот файл). Заголовкам,
//   которым нужна только категория (`AsdaWindowSubsystem.h`, `AsdaNotice.h`, `AsdaPartySubsystem.h`, `AsdaPlayerMenuSubsystem.h`),
//   больше не нужен класс подсистемы сообщений и её таблица маршрутов. Отражение UE (`/Script/AsdaReborn.EAsdaMsgCategory`)
//   не менялось.
#pragma once
#include "CoreMinimal.h"
#include "AsdaMsgCategory.generated.h"

// ── Категория события = ЕДИНСТВЕННОЕ, что сообщает вызывающий код. Смысл, а не окно. ──
UENUM(BlueprintType)
enum class EAsdaMsgCategory : uint8
{
	War            = 0  UMETA(DisplayName = "War — военные/кампвар (битва, захват, зоны)"),
	AdminNotice    = 1  UMETA(DisplayName = "AdminNotice — объявление администрации (3113)"),
	HeroLoot       = 2  UMETA(DisplayName = "HeroLoot — редкая добыча другого игрока (6131)"),
	System         = 3  UMETA(DisplayName = "System — общесистемное сообщение"),
	MoveRefused    = 4  UMETA(DisplayName = "MoveRefused — отказ движения / недоступная область"),
	CombatRefused  = 5  UMETA(DisplayName = "CombatRefused — отказ атаки/замаха, нет стрел"),
	SkillRefused   = 6  UMETA(DisplayName = "SkillRefused — отказ навыка/каста"),
	ItemRefused    = 7  UMETA(DisplayName = "ItemRefused — отказ предмета, прочность, вес"),
	Loot           = 8  UMETA(DisplayName = "Loot — подбор предметов и золота"),
	Trade          = 9  UMETA(DisplayName = "Trade — торговля, магазин, транзакции"),
	Quest          = 10 UMETA(DisplayName = "Quest — квесты"),
	Progress       = 11 UMETA(DisplayName = "Progress — уровень, опыт, награды"),
	Social         = 12 UMETA(DisplayName = "Social — клан/пати/друзья/соулмейт"),
	Connection     = 13 UMETA(DisplayName = "Connection — соединение, авторизация, варп"),
	Duel           = 14 UMETA(DisplayName = "Duel — PvP-дуэль"),
	Tip            = 15 UMETA(DisplayName = "Tip — подсказки, скриншот, служебное"),
	MAX            = 16 UMETA(Hidden)
};
