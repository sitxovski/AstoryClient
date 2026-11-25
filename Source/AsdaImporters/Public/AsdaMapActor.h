// Copyright (c) 2026 @sitxovski (Discord)
// AAsdaMapActor — загрузка геометрии карты AsdaStory в UE через AsdaFormats.
// Слой 2 (первый видимый результат): строит террейн (PatchTerrain → ProceduralMesh).
// Поставь актор в уровень, задай MapDir (папка карты), жми "Build Map" в деталях.
// ★ EDITOR-ONLY ИНСТРУМЕНТ (модуль AsdaImporters). Актор читает ОРИГИНАЛЬНЫЕ файлы карты с диска
//   (.TER/.BIN/Pos.POS/.WAT/.SCE/.TEX) — в игровую сборку такой код не попадает по закону проекта:
//   рантайм берёт данные ТОЛЬКО из ассетов UE. Готовые карты запечены в уровни Content/Map/<map>,
//   экземпляров этого актора в них нет (проверено grep по .umap).
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AsdaMapActor.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UTexture2D;
class ADirectionalLight;
class AExponentialHeightFog;

namespace asda { struct TerrainPatch; struct Scene; }

UCLASS()
class ASDAIMPORTERS_API AAsdaMapActor : public AActor
{
	GENERATED_BODY()
public:
	AAsdaMapActor();

	// Авто-построение при загрузке уровня / изменении свойств (ProcMesh не сериализуется в .umap).
	virtual void OnConstruction(const FTransform& Transform) override;

	// Рантайм: заспавнить 3D-эмиттеры окружающего звука карты (section E, MapObjects.sounds).
	virtual void BeginPlay() override;

	// Папка карты, напр. "@@ASTORY_DEV_ROOT_FWD@@/Donors/AsdaChroniclesInterlude/map/Mountain Flamion".
	UPROPERTY(EditAnywhere, Category="Asda Map")
	FString MapDir;

	// Масштаб игровых единиц → UE (см). Подбирается визуально.
	UPROPERTY(EditAnywhere, Category="Asda Map")
	float WorldScale = 1.0f;

	// Зеркалить ось Y (исправление RH→LH handedness). Если карта зеркальна — переключи.
	UPROPERTY(EditAnywhere, Category="Asda Map")
	bool bMirrorY = true;

	// Построить/перестроить геометрию карты. Кнопка в панели Details.
	UFUNCTION(CallInEditor, Category="Asda Map")
	void BuildMap();

	UPROPERTY(VisibleAnywhere, Category="Asda Map")
	TObjectPtr<UProceduralMeshComponent> Mesh;

	// Статические объекты карты (Pos.POS + Pos0.PBB) — отдельный ProcMesh, секция на instance.
	UPROPERTY(VisibleAnywhere, Category="Asda Map")
	TObjectPtr<UProceduralMeshComponent> ObjMesh;

	// Вода (.WAT) — отдельный ProcMesh, секция на body.
	UPROPERTY(VisibleAnywhere, Category="Asda Map")
	TObjectPtr<UProceduralMeshComponent> WaterMesh;

	// Небесный купол (SKY_DOME.JBM) — отдельный ProcMesh.
	UPROPERTY(VisibleAnywhere, Category="Asda Map")
	TObjectPtr<UProceduralMeshComponent> SkyMesh;

	// Мастер-материал splat-террейна (/Game/Materials/M_AsdaTerrain). Грузится в
	// BuildMap; если null — секции остаются с дефолтным материалом.
	UPROPERTY(EditAnywhere, Category="Asda Map")
	TObjectPtr<UMaterialInterface> TerrainMaster;

	// Мастер-материал объектов (/Game/Materials/M_AsdaObject). Грузится лениво.
	UPROPERTY(EditAnywhere, Category="Asda Map")
	TObjectPtr<UMaterialInterface> ObjectMaster;

	// Мастер-материал воды (/Game/Materials/M_AsdaWater).
	UPROPERTY(EditAnywhere, Category="Asda Map")
	TObjectPtr<UMaterialInterface> WaterMaster;

	// Мастер-материал неба (/Game/Materials/M_AsdaSky).
	UPROPERTY(EditAnywhere, Category="Asda Map")
	TObjectPtr<UMaterialInterface> SkyMaster;

private:
	// Карта, для которой геометрия уже построена (анти-ребилд в OnConstruction). Не сериализуется.
	UPROPERTY(Transient)
	FString BuiltMapDir;

	// raw (x,y,z) игры [y=высота] → UE (Z-up) с масштабом/зеркалом.
	FVector ToUE(float x, float y, float z) const;

	// Нормаль игры → UE: та же перестановка осей БЕЗ translation/масштаба, нормализована.
	FVector ToUENormal(float nx, float ny, float nz) const;

	// Создать UMaterialInstanceDynamic для патча и назначить на секцию mesh'а.
	void ApplyPatchMaterial(int32 SectionIndex, const asda::TerrainPatch& Patch,
		const TArray<uint8>& ColorArr, const TArray<uint8>& AlphaArr, const FString& Dir);

	// Загрузить текстуру объекта/воды/неба по имени из папки карты (несколько фолбэков).
	UTexture2D* LoadObjTexture(const FString& Dir, const FString& TexName) const;

	// Заспавнить окружающие 3D-звуки карты через единый звук-менеджер (MapObjects.sounds → PlaySound3D).
	void SpawnAmbientSounds();

	// Построить статические объекты карты.
	void BuildObjects(const FString& Dir, const asda::Scene& InScene);
	// Построить воду.
	void BuildWater(const FString& Dir);

	// Построить небо.
	void BuildSky(const FString& Dir);
	// Применить сцену (свет + туман). Возвращает распарсенную сцену через out-параметр.
	void ApplyScene(const FString& Dir, asda::Scene& OutScene);

	// Указатели на заспавненные акторы (чтобы не плодить при ребилде).
	UPROPERTY(Transient)
	TObjectPtr<ADirectionalLight> SunLight;
	UPROPERTY(Transient)
	TObjectPtr<AExponentialHeightFog> HeightFog;
};
