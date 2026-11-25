// Copyright (c) 2026 @sitxovski (Discord)
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AsdaLayeredCloudsActor.generated.h"

// «Объёмные» облака из облачной текстуры карты (CLOUDE2): стек полупрозрачных плоскостей на разных
// высотах — параллакс слоёв даёт глубину/толщину. Обычная транслюцентная геометрия (никакой зависимости
// от волюметрик-пайплайна). Все параметры редактируются в деталях и применяются мгновенно.
UCLASS()
class ASDAREBORN_API AAsdaLayeredCloudsActor : public AActor
{
	GENERATED_BODY()

public:
	AAsdaLayeredCloudsActor();

	// Маска/цвет облаков (обычно SKY_DOME_CLOUDE2 карты)
	UPROPERTY(EditAnywhere, Category = "Asda|Clouds")
	TObjectPtr<UTexture> CloudTexture;

	// Мастер M_AsdaLayerClouds (translucent unlit)
	UPROPERTY(EditAnywhere, Category = "Asda|Clouds")
	TObjectPtr<UMaterialInterface> Master;

	// Число слоёв стека
	UPROPERTY(EditAnywhere, Category = "Asda|Clouds", meta = (ClampMin = "1", ClampMax = "12"))
	int32 NumLayers = 5;

	// Высота нижнего слоя (юниты) и шаг между слоями
	UPROPERTY(EditAnywhere, Category = "Asda|Clouds")
	float BottomZ = 60000.f;
	UPROPERTY(EditAnywhere, Category = "Asda|Clouds")
	float LayerStep = 9000.f;

	// Масштаб плоскости (базовый quad 100×100 юнитов; 6000 → 600k юнитов = 6 км)
	UPROPERTY(EditAnywhere, Category = "Asda|Clouds")
	float PlaneScale = 6000.f;

	// Повторов текстуры на плоскость
	UPROPERTY(EditAnywhere, Category = "Asda|Clouds", meta = (ClampMin = "0.1"))
	float UVTiles = 2.5f;

	// Непрозрачность слоя (итог по стеку выше)
	UPROPERTY(EditAnywhere, Category = "Asda|Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float Opacity = 0.75f;

	// Базовая скорость дрейфа (UV/сек) — каждый слой чуть быстрее (параллакс ветра)
	UPROPERTY(EditAnywhere, Category = "Asda|Clouds")
	float SpeedU = 0.003f;
	UPROPERTY(EditAnywhere, Category = "Asda|Clouds")
	float SpeedV = 0.0007f;

	// Тонировка: низ стека темнее (подсветка «толщины»), верх — светлый
	UPROPERTY(EditAnywhere, Category = "Asda|Clouds")
	FLinearColor TintBottom = FLinearColor(0.72f, 0.76f, 0.82f, 1.f);
	UPROPERTY(EditAnywhere, Category = "Asda|Clouds")
	FLinearColor TintTop = FLinearColor(1.f, 1.f, 1.f, 1.f);

	virtual void OnConstruction(const FTransform& Xf) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& E) override;
#endif

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> CloudLayers;

	void Rebuild();
};
