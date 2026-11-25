// Copyright (c) 2026 @sitxovski (Discord)
// Реализация AAsdaRideVehicle — см. AsdaRideVehicle.h (машина пасхалки на Chaos Vehicles).
#include "World/AsdaRideVehicle.h"

#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "World/AsdaGroundTrace.h"
#include "Components/SpotLightComponent.h"

// ★ Размеры колёс подобраны под меши набора (легковая машина): радиус 35 см, ширина 25 см.
//   Точные значения набора лежали в его `DA_*_TireConfig`, но те ассеты — PhysX-овские и не
//   читаются; на глаз машина ведёт себя ровно, а править можно прямо здесь.
static constexpr float kWheelRadius = 35.f;
static constexpr float kWheelWidth  = 25.f;

UAsdaRideWheelFront::UAsdaRideWheelFront()
{
	WheelRadius        = kWheelRadius;
	WheelWidth         = kWheelWidth;
	bAffectedBySteering = true;    // рулевая ось
	bAffectedByEngine   = false;
	bAffectedByHandbrake = false;
	MaxSteerAngle       = 40.f;
	AxleType            = EAxleType::Front;
	SuspensionMaxRaise  = 8.f;
	SuspensionMaxDrop   = 12.f;
}

UAsdaRideWheelRear::UAsdaRideWheelRear()
{
	WheelRadius        = kWheelRadius;
	WheelWidth         = kWheelWidth;
	bAffectedBySteering = false;
	bAffectedByEngine   = true;    // ведущая ось
	bAffectedByHandbrake = true;   // ручник держит задние
	MaxSteerAngle       = 0.f;
	AxleType            = EAxleType::Rear;
	SuspensionMaxRaise  = 8.f;
	SuspensionMaxDrop   = 12.f;
}

AAsdaRideVehicle::AAsdaRideVehicle()
{
	// Колёса по костям скелета набора (имена сняты живой пробой, см. заголовок).
	if (UChaosWheeledVehicleMovementComponent* Mv =
		Cast<UChaosWheeledVehicleMovementComponent>(GetVehicleMovementComponent()))
	{
		Mv->WheelSetups.SetNum(4);
		Mv->WheelSetups[0].WheelClass = UAsdaRideWheelFront::StaticClass();
		Mv->WheelSetups[0].BoneName   = FName(TEXT("Wheel_Front_Left"));
		Mv->WheelSetups[1].WheelClass = UAsdaRideWheelFront::StaticClass();
		Mv->WheelSetups[1].BoneName   = FName(TEXT("Wheel_Front_Right"));
		Mv->WheelSetups[2].WheelClass = UAsdaRideWheelRear::StaticClass();
		Mv->WheelSetups[2].BoneName   = FName(TEXT("Wheel_Rear_Left"));
		Mv->WheelSetups[3].WheelClass = UAsdaRideWheelRear::StaticClass();
		Mv->WheelSetups[3].BoneName   = FName(TEXT("Wheel_Rear_Right"));

		// ★★★ ДВИГАТЕЛЬ, КОРОБКА, ПРИВОД И МАССА — ЗАДАЁМ ЯВНО.
		//   Диагностика показала ровно эту дыру: газ доходит (throttle 0.65), физика включена,
		//   все четыре колеса на земле — а скорость ноль. То есть момент не доходил до колёс:
		//   у набора эти настройки лежали в его блюпринтах (непригодных, сборка под UE4), а
		//   дефолты компонента их не заменяют.
		Mv->Mass = 1500.f;                                   // легковая машина, кг
		Mv->ChassisWidth  = 180.f;
		Mv->ChassisHeight = 140.f;

		// Момент: ровная полка почти по всем оборотам — пасхалке важна предсказуемость, а не кривая.
		Mv->EngineSetup.MaxTorque = 700.f;
		Mv->EngineSetup.MaxRPM    = 6000.f;
		Mv->EngineSetup.EngineIdleRPM = 800.f;
		Mv->EngineSetup.TorqueCurve.GetRichCurve()->Reset();
		Mv->EngineSetup.TorqueCurve.GetRichCurve()->AddKey(0.f,    0.9f);
		Mv->EngineSetup.TorqueCurve.GetRichCurve()->AddKey(3000.f, 1.0f);
		Mv->EngineSetup.TorqueCurve.GetRichCurve()->AddKey(6000.f, 0.8f);

		// Коробка: автомат, иначе машина стоит на нейтрали и газ уходит в никуда.
		Mv->TransmissionSetup.bUseAutomaticGears  = true;
		Mv->TransmissionSetup.bUseAutoReverse     = true;
		Mv->TransmissionSetup.FinalRatio          = 3.5f;

		// ★ Привод — ЗАДНИЙ, ровно как размечены колёса (`bAffectedByEngine` у задней пары).
		//   Рассогласование привода и разметки колёс — вторая классическая причина «газ есть,
		//   движения нет».
		Mv->DifferentialSetup.DifferentialType = EVehicleDifferential::RearWheelDrive;
	}

	// ★★★ ФИЗИКА КУЗОВА. Без неё машина стоит колом: штатная система машин считает силы, но
	//   прикладывать их не к чему — тело не симулируется. В шаблонах UE это включено в блюпринте,
	//   а у нас блюпринта нет (набор приехал под UE4), поэтому включаем в коде.
	//   Симптом при выключенной: колёса построены, ввод идёт, но на WASD машина не двигается.
	if (USkeletalMeshComponent* MC = GetMesh())
	{
		MC->BodyInstance.bSimulatePhysics = true;
		MC->BodyInstance.bNotifyRigidBodyCollision = true;
		MC->BodyInstance.bUseCCD = true;                     // на скорости не проскакивает опору
		MC->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		// ⚠ Профиля `Vehicle` в этом проекте НЕТ (в `DefaultEngine.ini` он не заведён), а ссылка на
		//   несуществующий профиль молча оставляет коллизию дефолтной. Берём штатный `PhysicsActor`.
		MC->SetCollisionProfileName(TEXT("PhysicsActor"));
		MC->SetGenerateOverlapEvents(false);
	}

	// ★ ФАРЫ. У набора они жили в блюпринте (непригодном), поэтому заводим свои: две узкие
	//   лампы вперёд. Гаснуть/зажигаться им незачем — пасхалка, свет всегда включён.
	auto MakeHeadlight = [this](const TCHAR* Name, float Y) -> USpotLightComponent*
	{
		USpotLightComponent* L = CreateDefaultSubobject<USpotLightComponent>(Name);
		L->SetupAttachment(GetMesh());
		L->SetRelativeLocation(FVector(220.f, Y, 60.f));     // перед капотом, на высоте фар
		L->SetRelativeRotation(FRotator(-8.f, 0.f, 0.f));    // чуть вниз, чтобы бил в дорогу
		L->SetIntensity(60000.f);
		L->SetAttenuationRadius(3000.f);
		L->SetInnerConeAngle(18.f);
		L->SetOuterConeAngle(34.f);
		L->SetLightColor(FLinearColor(1.f, 0.96f, 0.85f));   // тёплый автомобильный свет
		L->SetCastShadows(false);                            // пасхалка: тени от фар не нужны
		return L;
	};
	HeadlightL = MakeHeadlight(TEXT("RideHeadlightL"), -70.f);
	HeadlightR = MakeHeadlight(TEXT("RideHeadlightR"),  70.f);

	// Камера «с погони»: у пешки набора она была в блюпринте, здесь заводим свою — иначе после
	//   передачи управления игрок смотрел бы из центра машины.
	USpringArmComponent* Arm = CreateDefaultSubobject<USpringArmComponent>(TEXT("RideSpringArm"));
	Arm->SetupAttachment(GetMesh());
	Arm->TargetArmLength = 750.f;
	Arm->SetRelativeLocation(FVector(0.f, 0.f, 180.f));
	Arm->SetRelativeRotation(FRotator(-15.f, 0.f, 0.f));
	Arm->bDoCollisionTest      = true;
	Arm->bEnableCameraLag      = true;
	Arm->CameraLagSpeed        = 8.f;
	Arm->bInheritPitch         = false;   // на кочках камера не кувыркается вместе с кузовом
	Arm->bInheritRoll          = false;

	CameraArm = Arm;
	UCameraComponent* Cam = CreateDefaultSubobject<UCameraComponent>(TEXT("RideCamera"));
	Cam->SetupAttachment(Arm, USpringArmComponent::SocketName);
	Cam->SetConstraintAspectRatio(false);   // как игровая камера — без чёрных полей

	PrimaryActorTick.bCanEverTick = true;
}

void AAsdaRideVehicle::BeginPlay()
{
	Super::BeginPlay();

	// ★★★ ФИЗИКУ ВКЛЮЧАЕМ ЯВНО. Одного `BodyInstance.bSimulatePhysics` в конструкторе мало:
	//   тело создаётся при регистрации компонента, и флаг из конструктора до него не всегда
	//   доезжает. Симптом ровно тот, на который жаловался владелец: колёса построены, ввод идёт,
	//   а машина стоит колом и на WASD не реагирует.
	if (USkeletalMeshComponent* MC = GetMesh())
	{
		MC->SetSimulatePhysics(true);
		MC->WakeAllRigidBodies();          // и не даём телу уснуть сразу после спавна
	}

	// ★★★ Спавним ОПОРУ отдельным актором (почему не компонентом — см. заголовок).
	//   Это невидимая плита, которую мы каждый кадр подставляем под машину на высоту земли.
	UWorld* W = GetWorld();
	if (!W) { return; }
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SP.Owner = this;
	GroundPad = W->SpawnActor<AActor>(AActor::StaticClass(), GetActorLocation(), FRotator::ZeroRotator, SP);
	if (!GroundPad) { return; }
#if WITH_EDITOR
	GroundPad->SetActorLabel(TEXT("AsdaRideGroundPad"));
#endif
	UStaticMeshComponent* Pad = NewObject<UStaticMeshComponent>(GroundPad, TEXT("Pad"));
	GroundPad->SetRootComponent(Pad);
	Pad->RegisterComponent();
	Pad->SetMobility(EComponentMobility::Movable);
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		Pad->SetStaticMesh(Cube);
	}
	Pad->SetWorldScale3D(FVector(40.f, 40.f, 0.2f));      // 4000×4000×20 см
	Pad->SetHiddenInGame(true);                            // опора невидима
	Pad->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Pad->SetCollisionObjectType(ECC_WorldStatic);
	Pad->SetCollisionResponseToAllChannels(ECR_Block);
	Pad->SetGenerateOverlapEvents(false);
}

void AAsdaRideVehicle::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GroundPad) { GroundPad->Destroy(); GroundPad = nullptr; }
	Super::EndPlay(Reason);
}

void AAsdaRideVehicle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!GroundPad) { return; }

	// Высоту земли берём ТОЙ ЖЕ общей точкой, что и персонаж (`AsdaGround::GroundZ`), — иначе
	//   машина ездила бы по другой поверхности, чем ходит игрок.
	const FVector C = GetActorLocation();
	const float R = 300.f;                                  // плечо для замера наклона
	const float Zc = AsdaGround::GroundZ(GetWorld(), C.X,     C.Y,     C.Z, this);
	const float Zx = AsdaGround::GroundZ(GetWorld(), C.X + R, C.Y,     C.Z, this);
	const float Zy = AsdaGround::GroundZ(GetWorld(), C.X,     C.Y + R, C.Z, this);

	// Нормаль рельефа по трём точкам — чтобы на склоне опора была наклонена, а не ступенькой.
	const FVector Nx(R, 0.f, Zx - Zc);
	const FVector Ny(0.f, R, Zy - Zc);
	FVector N = FVector::CrossProduct(Nx, Ny).GetSafeNormal();
	if (N.IsNearlyZero() || N.Z < 0.f) { N = FVector::UpVector; }

	// Половина толщины плиты (масштаб 0.2 у куба 100 см) — чтобы ВЕРХ плиты лёг ровно на землю.
	const float HalfThickness = 10.f;
	GroundPad->SetActorLocationAndRotation(
		FVector(C.X, C.Y, Zc - HalfThickness),
		FRotationMatrix::MakeFromZ(N).Rotator(),
		/*bSweep*/false, nullptr, ETeleportType::TeleportPhysics);
}

void AAsdaRideVehicle::ApplyMesh(const TCHAR* MeshPath)
{
	// ConstructorHelpers работает ТОЛЬКО в конструкторе — отсюда и пять классов-наследников.
	ConstructorHelpers::FObjectFinder<USkeletalMesh> Found(MeshPath);
	if (Found.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(Found.Object);
	}
}

// ★ Пути к мешам набора Vehicle Variety Pack. Блюпринты набора непригодны (сборка под UE4,
//   ссылки на `/Script/PhysXVehicles`), а меши и физ-ассеты — полностью годны.
AAsdaRideSportsCar::AAsdaRideSportsCar()
{ ApplyMesh(TEXT("/Game/VehicleVarietyPack/Skeletons/SK_SportsCar.SK_SportsCar")); }

AAsdaRideSuv::AAsdaRideSuv()
{ ApplyMesh(TEXT("/Game/VehicleVarietyPack/Skeletons/SK_SUV.SK_SUV")); }

AAsdaRidePickup::AAsdaRidePickup()
{ ApplyMesh(TEXT("/Game/VehicleVarietyPack/Skeletons/SK_Pickup.SK_Pickup")); }

AAsdaRideHatchback::AAsdaRideHatchback()
{ ApplyMesh(TEXT("/Game/VehicleVarietyPack/Skeletons/SK_Hatchback.SK_Hatchback")); }

AAsdaRideBoxTruck::AAsdaRideBoxTruck()
{ ApplyMesh(TEXT("/Game/VehicleVarietyPack/Skeletons/SK_Truck_Box.SK_Truck_Box")); }

void AAsdaRideVehicle::ToggleHeadlights()
{
	bHeadlightsOn = !bHeadlightsOn;
	if (HeadlightL) { HeadlightL->SetVisibility(bHeadlightsOn); }
	if (HeadlightR) { HeadlightR->SetVisibility(bHeadlightsOn); }
}

void AAsdaRideVehicle::AddCameraInput(float DeltaYaw, float DeltaPitch)
{
	// Свободный обзор при зажатой ПКМ: крутим ШТАНГУ камеры, сама машина не поворачивается.
	CamYaw   = FMath::UnwindDegrees(CamYaw + DeltaYaw);
	CamPitch = FMath::Clamp(CamPitch + DeltaPitch, -70.f, 25.f);   // не заглядываем под днище
	if (CameraArm)
	{
		CameraArm->SetRelativeRotation(FRotator(CamPitch, CamYaw, 0.f));
	}
}
