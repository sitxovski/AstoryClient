// Copyright (c) 2026 @sitxovski (Discord)
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AsdaSkyDomeActor.generated.h"

class UMaterialInstanceDynamic;

// ★ [ИСПРАВЛЕНИЕ] 29.10.2026 (баг-репорт игроков «decrease speed of clouds»): множитель скорости облаков НЕБА —
//   скролла купола (`ScrollBoost` материала купола) и теней облаков на земле (`SpeedU/SpeedV` функции света
//   `M_AsdaCloudShadowLF`). Ключ `asda.Clouds.SlowSky`: 1 (умолч.) = вдвое медленнее, 0 = прежняя скорость.
//   3D-облака `AAsdaCloudFieldActor` ключ НЕ трогает — их владелец замедлил сам в уровнях.
float AsdaSkyCloudSpeedFactor();

// Купол неба AsdaStory (реверс CTerrain_SetupMultiTexBlend @0x1136CB0, отчёт Reports/sky_system_reverse.md):
// пер-карточный sky_dome.jbm (сплюснутая полусфера R=1000/H=285), в оригинале приклеен к камере и рисуется
// ПЕРВЫМ в кадре с ZENABLE=0. UE-эквивалент: unlit купол огромного масштаба (углы при равномерном
// масштабе сохраняются) + следование за камерой в Tick — глубина сама кладёт его за всю геометрию.
// ★ 05.09.2026: материал купола TRANSLUCENT с Apply Fogging=off (иначе ExponentialHeightFog заливает небо),
// TranslucencySortPriority −1000 (конструктор) — купол рисуется первым среди транслюцентов.
// Слои (мастер M_AsdaSkyDome): cloude1 база (uv0 планарная) → cloude2 по её альфе (uv0, скролл) →
// sky_dome.TEX горы горизонта по альфе (uv1 цилиндрическая). Скорости скролла — из .TER (MIC-параметры).
UCLASS()
class ASDAREBORN_API AAsdaSkyDomeActor : public AActor
{
	GENERATED_BODY()

public:
	AAsdaSkyDomeActor();

	UPROPERTY(VisibleAnywhere, Category = "Asda")
	TObjectPtr<UStaticMeshComponent> Dome;

	// Следовать за камерой (оригинал: world-трансляция купола = позиция камеры каждый кадр).
	// В редакторе — за вьюпорт-камерой; выкл = статичное размещение.
	UPROPERTY(EditAnywhere, Category = "Asda")
	bool bFollowCamera = true;

	// ★★★ `CloudsSpeedBoost` и `HorizonBoost` СНЯТЫ ОТСЮДА 2026-08-27: это ПОСТОЯННЫЕ множители,
	//   одинаковые на всех 18 картах, и место им в материалах. Теперь они живут в пер-карточных
	//   `MI_skydome.ScrollBoost` (= 9) и `MI_skyhorizon.HorizonBoost` (= 1.725) — там их видно
	//   и правит редактор. Хранитель — `Tools/asda_bake_sky_material_params.py`.
	//   Вместе с ними ушли `OnConstruction` / `BeginPlay` / `PostEditChangeProperty`: их тела
	//   состояли ИСКЛЮЧИТЕЛЬНО из вызова `ApplySkyParams()`.

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

	// Применить множитель скорости облаков (AsdaSkyCloudSpeedFactor) к слотам купола с параметром `ScrollBoost`.
	//   Только в игровом мире (игра/PIE): в окне редактора уровня MID на компоненте ушёл бы в .umap при сохранении.
	//   Вызывается из BeginPlay и при смене ключа asda.Clouds.SlowSky на лету.
	void ApplyCloudSpeed();

private:
	// MID купола по индексу слота материала (nullptr — у слота нет `ScrollBoost` или множитель ещё не нужен).
	//   Либо созданный ApplyCloudSpeed поверх MI_skydome, либо MID, уже сохранённый в уровне (на части карт
	//   слот купола несёт MID «MID_MI_skydome_*» со времён снятого 27.08 ApplySkyParams).
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> CloudSpeedMIDs;

	// Исходное `ScrollBoost` слота (до множителя), снятое при первом применении; повторное применение множитель
	//   не накапливает. Индекс — как у CloudSpeedMIDs.
	TArray<float> CloudSpeedBase;

	// ★ волна 3 п.11 (15.09.2026): своя копия GetViewLocation снята — точка наблюдателя AsdaView::GetViewLocation (AsdaView.h).
};
