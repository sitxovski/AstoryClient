// Copyright (c) 2026 @sitxovski (Discord)
// SAsdaNormalStudioViewport — 3D-вьюпорт «Студии нормалей» (редакторский, в игру не попадает).
//
// Показывает ЛЮБУЮ модель проекта с рельефом, полученным из её же плоской текстуры: карта нормалей
// и упакованная маска (R = шероховатость, G = свечение) лежат в /Game/_NormalStudio/Maps и в игре
// НЕ применяются — материалы игры не трогаются вовсе.
//
// Сравнение «до/после» делается одним материалом: сила нормали 0 и шероховатость-константа 0.9 дают
// ровно нынешний вид проекта (мастера M_AsdaObjectLit/M_AsdaCharLit), поэтому разница в кадре —
// это разница ровно двух чисел, а не двух разных материалов.
#pragma once

#include "CoreMinimal.h"
#include "AstoryPreviewViewport.h"   // ★ волна 3 п.15: общая база превью (сцена, пол, носители, тик мира)

class FAdvancedPreviewScene;
class FAsdaNormalStudioClient;
class UStaticMesh;
class USkeletalMesh;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UTexture2D;
class UMaterialInstanceDynamic;

// Настройки показа, которыми управляет панель.
struct FAsdaNsSettings
{
	float NormalStrength = 1.0f;    // 0 = плоско («как сейчас»), 1 = карта как есть, выше — усиление
	float RoughFromMap = 1.0f;      // 0 = константа 0.9 («как сейчас»), 1 = карта шероховатости
	float RoughConst = 0.9f;        // константа мастеров проекта
	float EmissiveGain = 1.0f;      // яркость свечения (лава, кристаллы, лампы)
	float Delight = 1.0f;           // 0 = исходная текстура, 1 = базовый цвет без нарисованного света
	float AoAmount = 1.0f;          // затенение щелей (канал B маски)
	float SpecScale = 0.5f;         // для «альфа = блик» (персонаж/экип по эталону EmptyProject.fx)
	bool  bAlphaIsSpecular = false; // иначе альфа — маска прозрачности (листва, решётки)
};

class SAsdaNormalStudioViewport : public SAstoryPreviewViewport
{
public:
	SLATE_BEGIN_ARGS(SAsdaNormalStudioViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SAsdaNormalStudioViewport() override;

	// Показать модель. Карты ищутся по имени альбедо в /Game/_NormalStudio/Maps.
	void SetPreview(UStaticMesh* StaticMesh, USkeletalMesh* SkeletalMesh, bool bAlphaIsSpecular);
	void ClearPreview();

	void ApplySettings(const FAsdaNsSettings& InSettings);
	const FAsdaNsSettings& GetSettings() const { return Settings; }

	// ── свет превью-сцены ──
	void SetLightAngles(float Yaw, float Pitch);
	void SetLightBrightness(float Value);
	void SetSkyBrightness(float Value);
	void SetShowFloor(bool bVisible);
	void SetShowEnvironment(bool bVisible);
	void SetAutoRotate(bool bEnable);
	void SetLightSpin(bool bEnable);

	void FocusOnPreview();
	void ApplySpin(float YawDegrees);
	void StepLight(float DeltaDegrees);

	// Сколько материалов текущей модели получили карты (для строки состояния панели).
	int32 GetSlotsWithMaps() const { return SlotsWithMaps; }
	int32 GetSlotsTotal() const { return SlotsTotal; }

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	// Собрать динамические материалы для всех слотов компонента и подставить карты.
	void BuildMaterials(class UMeshComponent* Comp, bool bAlphaIsSpecular);
	void PushSettingsToMaterials();

	// Сцена превью и компоненты-носители (StaticComp/SkelComp) — в базе SAstoryPreviewViewport.
	TSharedPtr<FAsdaNormalStudioClient> Client;

	TArray<UMaterialInstanceDynamic*> Mids;   // держатся превью-сценой (GC), см. .cpp

	FAsdaNsSettings Settings;
	float LightYaw = -45.f;
	float LightPitch = -35.f;
	int32 SlotsWithMaps = 0;
	int32 SlotsTotal = 0;
};
