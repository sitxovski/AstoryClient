// Copyright (c) 2026 @sitxovski (Discord)
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AsdaCloudShadowsActor.generated.h"

class ADirectionalLight;
class UMaterialInstanceDynamic;

// Тени облаков карты (Editor-Friendly): Light Function на солнце из облачной текстуры карты (CLOUDE2).
// В деталях актора живёт то, что ПЕР-КАРТОЧНОЕ (солнце, маска облаков, мастер) и применяется
// немедленно (OnConstruction → MID на свет); постоянные числа — в дефолтах мастера.
// Ядро облака при Strength=1 гасит солнечный свет полностью — плотность как у обычных теней объектов.
UCLASS()
class ASDAREBORN_API AAsdaCloudShadowsActor : public AActor
{
	GENERATED_BODY()

public:
	AAsdaCloudShadowsActor();

	// Солнце карты, на которое вешается функция теней
	UPROPERTY(EditAnywhere, Category = "Asda|CloudShadows")
	TObjectPtr<ADirectionalLight> Sun;

	// Маска облаков (альфа = облако); по умолчанию SKY_DOME_CLOUDE2 карты — тени соответствуют небу
	UPROPERTY(EditAnywhere, Category = "Asda|CloudShadows")
	TObjectPtr<UTexture> CloudTexture;

	// Мастер M_AsdaCloudShadowLF (домен Light Function)
	UPROPERTY(EditAnywhere, Category = "Asda|CloudShadows")
	TObjectPtr<UMaterialInterface> Master;

	// ★★★ `TileSize / SpeedU / SpeedV / Strength / Contrast` СНЯТЫ ОТСЮДА 2026-08-27: значения
	//   постоянные и одинаковые на всех 18 картах, поэтому живут в ДЕФОЛТАХ мастера
	//   `M_AsdaCloudShadowLF` (масштаб пятен 60000 юн, скорость 0.01/0, глубина 1, контраст 5).
	//   Правятся прямо в материале; шаг-хранитель — `Tools/asda_bake_sky_material_params.py`.

	virtual void OnConstruction(const FTransform& Xf) override;
	virtual void BeginPlay() override;
#if WITH_EDITOR
	// гарантия живого применения при правке в панели деталей
	virtual void PostEditChangeProperty(FPropertyChangedEvent& E) override;
#endif

	// Повесить/снять Light Function на солнце. Публичный с 05.09.2026: ключ `asda.Clouds.Shadows` (0/1, вкладка
	//   «Небо и туман» dev-меню Ctrl+N) переприменяет его на всех акторах карты живьём; при 0 функция света снимается.
	void Apply();

private:
	// MID функции света, созданный Apply. Переиспользуется, пока Master тот же (ревизия 05.09 М-3: раньше новый MID на
	//   каждое применение, в т.ч. на каждый щелчок asda.Clouds.Shadows 0→1 — мусор до GC).
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LightFunctionMID;
};
