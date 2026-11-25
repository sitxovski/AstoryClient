// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ИМЕНА ЭФФЕКТОВ ПОРТА — ТЕГИ ЭФФЕКТОВ ПЕРСОНАЖА И ИМЕНА ОДИНОЧНЫХ ЭФФЕКТ-BP (дедуп №2, В1-C, 11.10.2026). [ПОРТ]
//   План — `Reports/dedup_audit2/00_PLAN.md` §5 В1, находки F5/F6 — `03_fx_render.md`, отчёт — `wave1_C.md`.
//
//   ЗАЧЕМ. Тег эффекта, прикреплённого к персонажу («WeaponGlowFx», «ShieldGlowFx»…), жил строковым литералом в 3 файлах
//   (7 копий) — переименование в одном месте молча оставляло свечение заточки над трупом и на рыбалке (C9 аудита 14.09).
//   Одиночные эффект-BP вне таблиц (`BP_LootDropFx`, `BP_ClickMarker`, стенд `BP_Fx_test_000`) спавнились по полному пути
//   литералом, путь лута — двумя копиями (игра + конфигуратор свечения). Здесь — одно имя на всё; путь класса строит
//   общее правило `AsdaAssetPaths::FxClassPathFromBp`, спавн — `AAsdaEffectActor::SpawnByBPName`.
//   Строки побайтно прежние (сверка — самотест `asda.Dedup.W1CSelfTest`). Оригинала здесь нет: теги и BP — устройство порта.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

// Теги эффект-акторов, прикреплённых к персонажу (`AActor::Tags`): по ним эффект находят, гасят и прячут.
namespace AsdaCharFxTags
{
	inline constexpr const TCHAR* kWeaponGlow = TEXT("WeaponGlowFx");   // свечение заточки оружия
	inline constexpr const TCHAR* kShieldGlow = TEXT("ShieldGlowFx");   // свечение заточки щита
	inline constexpr const TCHAR* kWing       = TEXT("WingFx");         // крылья Plus Package (6585)
	inline constexpr const TCHAR* kSoulRing   = TEXT("SoulRingFx");     // кольцо соулмейта (5220)
	inline constexpr const TCHAR* kEntry      = TEXT("EntryFx");        // столб света входа в мир
	inline constexpr const TCHAR* kTargetRing = TEXT("TargetRingFx");   // кольцо выбранной цели

	// Уничтожить прикреплённые к `Owner` акторы с тегом `TagA` или `TagB` (один проход по списку, порядок — как у
	//   `GetAttachedActors`). `TagB = NAME_None` — только первый тег (`ActorHasTag(NAME_None)` всегда false).
	//   Общая точка двух бывших копий: `DestroyCharFxByTag` мира (смена состояния/повтор) и гашение свечения на смерти.
	inline void DestroyAttached(AActor* Owner, FName TagA, FName TagB = NAME_None)
	{
		if (!Owner) { return; }
		TArray<AActor*> Attached;
		Owner->GetAttachedActors(Attached);
		for (AActor* A : Attached)
		{
			if (A && (A->ActorHasTag(TagA) || A->ActorHasTag(TagB))) { A->Destroy(); }
		}
	}
}

// Имена одиночных эффект-BP в `/Game/AsdaAssets/Effects` (не из таблиц .jeb): аргумент `AAsdaEffectActor::SpawnByBPName`
//   и `AsdaAssetPaths::FxClassPathFromBp`. Родитель всех трёх — `AAsdaEffectActor` (проверено по .uasset 11.10.2026).
namespace AsdaFxBp
{
	inline constexpr const TCHAR* kLootDrop    = TEXT("BP_LootDropFx");    // блёстки лута (`eff_0_item_99_drop01-a`)
	inline constexpr const TCHAR* kClickMarker = TEXT("BP_ClickMarker");   // метка клика по земле
	inline constexpr const TCHAR* kLagTest     = TEXT("BP_Fx_test_000");   // стенд `asda.FxFollowLagTest` (диагностика)
}
