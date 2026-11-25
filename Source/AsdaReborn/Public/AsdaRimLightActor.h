// Copyright (c) 2026 @sitxovski (Discord)
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AsdaRimLightActor.generated.h"

class UMaterialParameterCollection;

// ★ Сет D (05.09.2026; gamedesigner_round2.md §4 «Слой 3 — рим на живых», FINAL_synthesis §4): ПЕР-КАРТОЧНЫЕ значения рима
//   персонажей/NPC/мобов. Сам рим живёт в мастерах семейства персонажа (asda_build_materials_lit.wire_rim:
//   Emissive += RimColor · RimWidth · Fresnel(RimPower) · (1 − saturate(N·L)) · RimEnable) и читает коллекцию MPC_AsdaToon.
//   Коллекция одна на проект, а нужные значения у карт разные (ночные — бирюза/пурпур, дневные — 0), поэтому актор карты
//   на BeginPlay (и в редакторе — OnConstruction/PostEditChangeProperty) пишет их в экземпляр коллекции ЭТОГО мира
//   (UKismetMaterialLibrary::Set*ParameterValue — значения per-world, новый мир стартует с дефолтов коллекции: RimEnable 0).
//   Карта без актора = рим выключен (RimEnable 0 в дефолтах коллекции) = прежний граф численно.
//   Размещает Tools/asda_convert_map_lit.py (ASDA_LIT_MODE=rim, ASDA_RIM=auto|<r,g,b,strength,power>): метка Rim_<map>,
//   цвет = туман карты (ground_colors.json), сила по mx карты (ночные ↑, дневные 0). Правится в деталях актора.
//   ★ 09.11.2026 [ОРИГИНАЛ] ключ asda.Rim.OriginalOff (умолч. 1) глушит рим на ВСЕХ картах (RimEnable 0 в Apply): в оригинале рима нет,
//   а голубой рим давал мобам данжей фиолетовый налёт и контур (dungeon_mob_purple). 0 = прежнее поведение (значения актора).
//   Направление света в материале берётся узлом SkyAtmosphereLightDirection[0] (= −forward первого DirectionalLight сцены,
//   SceneRendering.cpp:1546, SkyAtmosphere не нужен) — актору солнце передавать не надо.
UCLASS()
class ASDAREBORN_API AAsdaRimLightActor : public AActor
{
	GENERATED_BODY()

public:
	AAsdaRimLightActor();

	// Коллекция параметров (по умолчанию /Game/Materials/MPC_AsdaToon — существующая, с RimEnable/RimPower/RimWidth/RimColor)
	UPROPERTY(EditAnywhere, Category = "Asda|Rim")
	TObjectPtr<UMaterialParameterCollection> Collection;

	// Включён ли рим на этой карте (пишет RimEnable 1/0)
	UPROPERTY(EditAnywhere, Category = "Asda|Rim")
	bool bEnable = true;

	// Цвет каймы (линейный RGB); ГД: nightvalley (0.20, 0.85, 0.95), castle_of_chaos (0.85, 0.45, 0.85), castle_of_chess (0.30, 0.70, 0.95)
	UPROPERTY(EditAnywhere, Category = "Asda|Rim")
	FLinearColor RimColor = FLinearColor(0.2f, 0.85f, 0.95f, 1.0f);

	// Сила каймы (пишет RimWidth коллекции); ГД: ночные 0.30–0.35, дневные 0 (0.10–0.15 — отдельный A/B)
	UPROPERTY(EditAnywhere, Category = "Asda|Rim", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float RimStrength = 0.35f;

	// Показатель Френеля (пишет RimPower); ГД: 3.0 — чем больше, тем тоньше кайма
	UPROPERTY(EditAnywhere, Category = "Asda|Rim", meta = (ClampMin = "0.5", ClampMax = "16.0"))
	float RimPower = 3.0f;

	virtual void OnConstruction(const FTransform& Xf) override;
	virtual void BeginPlay() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& E) override;
#endif

	// Записать значения в экземпляр коллекции текущего мира (вызывается сам; доступно из Python/консоли для проверки)
	UFUNCTION(BlueprintCallable, Category = "Asda|Rim")
	void Apply();

};
