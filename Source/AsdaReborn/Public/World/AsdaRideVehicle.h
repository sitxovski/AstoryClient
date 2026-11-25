// Copyright (c) 2026 @sitxovski (Discord)
// AAsdaRideVehicle — машина пасхалки «Транспорт» на ШТАТНОЙ системе UE (Chaos Vehicles).
//
// ★★★ ЗАЧЕМ СВОЙ КЛАСС, А НЕ BLUEPRINT НАБОРА.
//   Vehicle Variety Pack приехал в проект в сборке под UE4: все его блюпринты машин, колёс и
//   конфигов шин ссылаются на `/Script/PhysXVehicles`, а этой системы в UE5 НЕТ ВООБЩЕ (её
//   заменили на Chaos Vehicles). Проверено по самим файлам: упоминаний `PhysXVehicles` — во всех
//   блюпринтах набора, упоминаний `ChaosVehicles` — НОЛЬ. Поэтому у них не находится родительский
//   класс, `generated_class` пуст, и `LoadClass` возвращает null («класс не загрузился» в логе).
//   ⇒ Блюпринты набора непригодны, а вот его АССЕТЫ пригодны полностью: скелетные меши `SK_*`
//   с физ-ассетами на месте, и кости у них стандартные — `Vehicle_ROOT`, `Wheel_Front_Left`,
//   `Wheel_Front_Right`, `Wheel_Rear_Left`, `Wheel_Rear_Right` (снято живой пробой).
//   Поэтому машину собираем сами: меш берём из набора, физику — штатную движковую.
#pragma once

#include "CoreMinimal.h"
#include "WheeledVehiclePawn.h"
#include "ChaosVehicleWheel.h"
#include "AsdaRideVehicle.generated.h"

class USkeletalMesh;

// Переднее колесо: рулит, тормозит, двигателем не ведётся.
UCLASS()
class ASDAREBORN_API UAsdaRideWheelFront : public UChaosVehicleWheel
{
	GENERATED_BODY()
public:
	UAsdaRideWheelFront();
};

// Заднее колесо: ведущее, держит ручной тормоз, не рулит.
UCLASS()
class ASDAREBORN_API UAsdaRideWheelRear : public UChaosVehicleWheel
{
	GENERATED_BODY()
public:
	UAsdaRideWheelRear();
};

UCLASS()
class ASDAREBORN_API AAsdaRideVehicle : public AWheeledVehiclePawn
{
	GENERATED_BODY()

public:
	AAsdaRideVehicle();

	virtual void Tick(float DeltaSeconds) override;

	// Фары: включить/выключить (клавиша `L` во время поездки).
	void ToggleHeadlights();

	// Обзор мышью при зажатой ПКМ: крутит ШТАНГУ камеры, машину не поворачивает.
	void AddCameraInput(float DeltaYaw, float DeltaPitch);

protected:
	// ★★★ ОПОРА ПОД КОЛЁСАМИ. В этом порте у мира НЕТ физической коллизии: персонаж ходит по
	//   высоте, вычисленной из данных террейна (`AsdaGround::GroundZ`, `UAsdaWalkGrid::GetGroundY`),
	//   а не по столкновениям. Поэтому машина, честно симулируемая физикой, просто проваливается
	//   сквозь мир — единственное, обо что она билась, это капсула персонажа.
	//   Лечение: невидимая площадка, которую мы каждый кадр подставляем ПОД машину на высоту земли
	//   и наклоняем по рельефу (высоты берём в трёх точках вокруг). Машина катится по ней как по
	//   настоящей земле, а физика остаётся штатной.
	//   ★★★ ОПОРА — ОТДЕЛЬНЫЙ АКТОР, а не компонент машины. Компонент, прикреплённый к самой
	//   машине, опорой быть НЕ МОЖЕТ: он часть того же физического тела, и машина проваливалась
	//   сквозь него (симптом владельца «тачки падают сквозь пол»). Пока рядом стоял персонаж,
	//   она цеплялась за его капсулу — стоило персонажа скрыть, держать стало нечему.
	UPROPERTY(Transient) TObjectPtr<AActor> GroundPad = nullptr;

	// Фары — свои, у набора они жили в непригодном блюпринте.
	UPROPERTY(Transient) TObjectPtr<class USpotLightComponent> HeadlightL = nullptr;
	UPROPERTY(Transient) TObjectPtr<class USpotLightComponent> HeadlightR = nullptr;
	bool bHeadlightsOn = true;

	// Штанга камеры и её углы (обзор ПКМ).
	UPROPERTY(Transient) TObjectPtr<class USpringArmComponent> CameraArm = nullptr;
	float CamYaw = 0.f;
	float CamPitch = -15.f;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// ★ Меш задаётся В КОНСТРУКТОРЕ наследника. Позже — НЕЛЬЗЯ: штатная система машин строит
	//   колёса по КОСТЯМ уже при создании компонентов, и без меша получает пустой скелет.
	//   Симптомы в логе именно такие: `Ensure condition failed: Mesh->GetSkinnedAsset()` и
	//   `Spring configuration is invalid! ... no two springs are at the same location`
	//   (все четыре колеса встают в одну точку 0,0,0).
	void ApplyMesh(const TCHAR* MeshPath);
};

// ── Пять машин набора: у каждой свой меш, всё остальное общее ───────────────────────────────────
UCLASS() class ASDAREBORN_API AAsdaRideSportsCar : public AAsdaRideVehicle
{ GENERATED_BODY() public: AAsdaRideSportsCar(); };

UCLASS() class ASDAREBORN_API AAsdaRideSuv : public AAsdaRideVehicle
{ GENERATED_BODY() public: AAsdaRideSuv(); };

UCLASS() class ASDAREBORN_API AAsdaRidePickup : public AAsdaRideVehicle
{ GENERATED_BODY() public: AAsdaRidePickup(); };

UCLASS() class ASDAREBORN_API AAsdaRideHatchback : public AAsdaRideVehicle
{ GENERATED_BODY() public: AAsdaRideHatchback(); };

UCLASS() class ASDAREBORN_API AAsdaRideBoxTruck : public AAsdaRideVehicle
{ GENERATED_BODY() public: AAsdaRideBoxTruck(); };
