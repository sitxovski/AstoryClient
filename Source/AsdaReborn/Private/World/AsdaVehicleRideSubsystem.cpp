// Copyright (c) 2026 @sitxovski (Discord)
// Реализация UAsdaVehicleRideSubsystem — см. AsdaVehicleRideSubsystem.h (пасхалка «покататься»).
#include "World/AsdaVehicleRideSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Resources/AsdaGameUserSettings.h"   // ★ 19.10.2026: GetCameraTurnScale — чувствительность поворота камеры
#include "World/AsdaPlayerController.h"
#include "World/AsdaAdminSubsystem.h"
#include "Login/AsdaText.h"   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): подписи пунктов GM-меню — строки DT_GameText 990624…990632

#include "ChaosVehicleMovementComponent.h"
#include "World/AsdaRideVehicle.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Kismet/GameplayStatics.h"
#include "World/AsdaGroundTrace.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "World/AsdaOrbitCursor.h"   // ★ 07.11.2026: курсор при зажатой ПКМ скрыт и стоит на месте
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaRide, Log, All);

// Диагностика поездки (ввод, физика, колёса на земле). 1 = включена.
static ASDA_DEV_CVAR(int32, CVarRideDebug,
	TEXT("asda.Ride.Debug"), 1,
	TEXT("1 = раз в полсекунды писать в лог состояние поездки; 0 = молчать."),
	ECVF_Default);

// ★★★ МАШИНЫ: берём СКЕЛЕТНЫЕ МЕШИ набора Vehicle Variety Pack, а физику собираем сами
//   (`AAsdaRideVehicle`, штатная система Chaos Vehicles).
//   ПОЧЕМУ НЕ БЛЮПРИНТЫ НАБОРА: он приехал в сборке под UE4 — все его блюпринты ссылаются на
//   `/Script/PhysXVehicles`, которой в UE5 нет вовсе (проверено по самим файлам: `PhysXVehicles`
//   встречается во всех блюпринтах набора, `ChaosVehicles` — НОЛЬ). Родительский класс не
//   находится, `generated_class` пуст, `LoadClass` возвращает null — отсюда «класс не загрузился».
//   А меши и физ-ассеты набора полностью годны, кости стандартные (`Wheel_Front_Left` и т.д.).
// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): TitleLoc — id строки DT_GameText с подписью пункта (язык сессии).
struct FAsdaRideCar { int32 TitleLoc; TSubclassOf<AAsdaRideVehicle> Class; };
static TArray<FAsdaRideCar> MakeCarList()
{
	return {
		{ 990624, AAsdaRideSportsCar::StaticClass() },   // «Транспорт: спорткар»
		{ 990625, AAsdaRideSuv::StaticClass()       },   // «Транспорт: внедорожник»
		{ 990626, AAsdaRidePickup::StaticClass()    },   // «Транспорт: пикап»
		{ 990627, AAsdaRideHatchback::StaticClass() },   // «Транспорт: хэтчбек»
		{ 990628, AAsdaRideBoxTruck::StaticClass()  },   // «Транспорт: фургон»
	};
}

// Насколько «ускоряет» Shift.
//   1.55 подобрано так, чтобы `0.65 × 1.55 ≈ 1.0`: без Shift машина едет спокойно и слушается
//   руля, с Shift — газ в пол. Штатный компонент всё равно ограничивает вход диапазоном 0…1.
static ASDA_DEV_CVAR(float, CVarRideBoost,
	TEXT("asda.Ride.Boost"), 1.55f,
	TEXT("Множитель газа при зажатом Shift (штатный компонент машины ограничивает вход 0..1)."),
	ECVF_Default);

// Газ без Shift — чтобы спокойная езда была управляемой, а Shift давал разницу.
static ASDA_DEV_CVAR(float, CVarRideThrottle,
	TEXT("asda.Ride.Throttle"), 0.65f,
	TEXT("Газ при W без Shift (0..1)."),
	ECVF_Default);

void UAsdaVehicleRideSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// ★ Пункты админ-меню — через ОБЩУЮ точку реестра (закон `port-shared-points`):
	//   своего меню не заводим, новый пункт = одна регистрация.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaAdminSubsystem* Admin = GI->GetSubsystem<UAsdaAdminSubsystem>())
		{
			// По строке на каждую машину набора — это и есть «выбор тачек».
			const TArray<FAsdaRideCar> Cars = MakeCarList();
			for (int32 i = 0; i < Cars.Num(); ++i)
			{
				TSubclassOf<AAsdaRideVehicle> Cls = Cars[i].Class;
				Admin->RegisterFunction(FName(*FString::Printf(TEXT("RIDE%d"), i)),
					FText::FromString(FAsdaText::Loc(Cars[i].TitleLoc)),
					FText::FromString(FAsdaText::Loc(990629)),   // «WASD — ехать и рулить, …, F12 — вернуть управление персонажу»
					[this, Cls](APlayerController* PC) { EnterVehicle(PC, Cls); });
				Admin->SetFunctionLoc(FName(*FString::Printf(TEXT("RIDE%d"), i)), Cars[i].TitleLoc, 990629);   // ★ 10.10.2026: язык на лету
			}
			// ★ Отдельный пункт: возврат управления транспорт НЕ убирает — убирает только этот.
			Admin->RegisterFunction(TEXT("RIDE_DESPAWN"),
				FText::FromString(FAsdaText::Loc(990631)),   // «Деспаун транспорта»
				FText::FromString(FAsdaText::Loc(990632)),   // «Убрать со сцены весь заспавненный транспорт»
				[this](APlayerController*) { DespawnAll(); });
			Admin->SetFunctionLoc(TEXT("RIDE_DESPAWN"), 990631, 990632);   // ★ 10.10.2026: язык на лету
		}
	}
}

void UAsdaVehicleRideSubsystem::Deinitialize()
{
	ReleaseControl();
	DespawnAll();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaVehicleRideSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Транспорт — актор мира: при травеле он гибнет, при разрыве сессия кончается.
	ReleaseControl();
	DespawnAll();
	bReleaseKeyWasDown = false;
	bLightKeyWasDown = false;
	DebugAccum = 0.f;
	(void)Reason;
}

void UAsdaVehicleRideSubsystem::EnterVehicle(APlayerController* PC, TSubclassOf<AAsdaRideVehicle> VehicleClass)
{
	if (!PC || !VehicleClass) { return; }
	UWorld* W = PC->GetWorld();
	if (!W) { return; }

	// Уже за рулём — сначала вернём управление персонажу, машину при этом ОСТАВЛЯЕМ стоять.
	if (Vehicle) { ReleaseControl(); }

	APawn* Player = PC->GetPawn();
	if (!Player) { return; }

	// ★ СПРАВА ОТ ПЕРСОНАЖА И НА ЗЕМЛЕ (правка по замечанию владельца: раньше машина спавнилась
	//   ровно в точке персонажа — то есть «на голове», и физика тут же расшвыривала обоих).
	//   Вбок отходим на 250 см, высоту берём ТОЙ ЖЕ общей точкой, что и персонаж
	//   (`AsdaGround::GroundZ`), и добавляем небольшой зазор под подвеску.
	const FRotator Rot(0.f, Player->GetActorRotation().Yaw, 0.f);   // крен и тангаж — дело физики
	const FVector  Side = FRotationMatrix(Rot).GetUnitAxis(EAxis::Y);   // «направо» от персонажа
	FVector Loc = Player->GetActorLocation() + Side * 250.f;
	Loc.Z = AsdaGround::GroundZ(W, Loc.X, Loc.Y, Player->GetActorLocation().Z, Player) + 60.f;

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SP.Owner = PC;
	// ★ Обычный спавн: меш у машины задан В КОНСТРУКТОРЕ класса, поэтому система машин находит
	//   кости колёс сразу (отложенный спавн тут не помогал — колёса строятся раньше).
	AAsdaRideVehicle* Car = W->SpawnActor<AAsdaRideVehicle>(VehicleClass, Loc, Rot, SP);
	if (!Car)
	{
		UE_LOG(LogAsdaRide, Warning, TEXT("[транспорт] не заспавнился"));
		return;
	}
#if WITH_EDITOR
	Car->SetActorLabel(TEXT("AsdaRideVehicle"));
#endif
	Spawned.Add(Car);          // ★ помним ВЕСЬ транспорт: он переживёт возврат управления

	Vehicle  = Car;
	Movement = Car->FindComponentByClass<UChaosVehicleMovementComponent>();
	if (!Movement)
	{
		UE_LOG(LogAsdaRide, Warning,
			TEXT("[транспорт] у «%s» нет компонента движения Chaos Vehicles — это не машина; "
			     "управления не будет"), *VehicleClass->GetName());
	}

	PrevPawn = Player;
	// ★ Персонажа ПРЯЧЕМ на время поездки (замечание владельца): иначе он стоит столбом рядом
	//   с машиной. Прячем вместе с детьми — оружие и накидка висят отдельными компонентами.
	Player->SetActorHiddenInGame(true);
	// ★ Клавиши сбрасываем ДО передачи управления: иначе зажатый W уедет вместе с персонажем
	//   (та же предосторожность, что у свободной камеры админа).
	PC->FlushPressedKeys();
	PC->Possess(Vehicle);

	UE_LOG(LogAsdaRide, Log, TEXT("[транспорт] поехали: %s (F12 — вернуть управление). Всего в сцене: %d"),
		*VehicleClass->GetName(), Spawned.Num());
}

void UAsdaVehicleRideSubsystem::ReleaseControl()
{
	if (!Vehicle) { return; }

	// ★ ТРАНСПОРТ НЕ УДАЛЯЕМ (требование владельца): возвращаем только управление и камеру,
	//   машина остаётся стоять там, где её бросили. Убирает её пункт «Деспаун транспорта».
	if (APlayerController* PC = Cast<APlayerController>(Vehicle->GetController()))
	{
		PC->FlushPressedKeys();
		PC->UnPossess();
		if (PrevPawn)
		{
			PrevPawn->SetActorHiddenInGame(false);   // ★ вернуть персонажа в кадр
			PC->Possess(PrevPawn);
		}
	}
	// Отпускаем газ и руль, иначе брошенная машина продолжит ехать сама.
	if (Movement)
	{
		Movement->SetThrottleInput(0.f);
		Movement->SetSteeringInput(0.f);
		Movement->SetBrakeInput(1.f);
		Movement->SetHandbrakeInput(true);
	}

	Vehicle = nullptr;
	Movement = nullptr;
	PrevPawn = nullptr;
	bReleaseKeyWasDown = false;

	UE_LOG(LogAsdaRide, Log, TEXT("[транспорт] управление вернулось персонажу; в сцене машин: %d"),
		Spawned.Num());
}

void UAsdaVehicleRideSubsystem::DespawnAll()
{
	// Если сидим за рулём — сперва вернём управление, иначе игрок останется без пешки.
	if (Vehicle) { ReleaseControl(); }

	int32 Killed = 0;
	for (const TObjectPtr<APawn>& P : Spawned)
	{
		if (IsValid(P)) { P->Destroy(); ++Killed; }
	}
	Spawned.Reset();
	UE_LOG(LogAsdaRide, Log, TEXT("[транспорт] деспаун: убрано машин %d"), Killed);
}

void UAsdaVehicleRideSubsystem::TickRide(APlayerController* PC, float DeltaSeconds)
{
	if (!Vehicle || !PC) { return; }

	// ── F12: вернуть управление персонажу, по ФРОНТУ нажатия ────────────────────────────────────
	//   ★ Транспорт при этом ОСТАЁТСЯ в сцене (требование владельца) — убирает его отдельный
	//   пункт меню «Деспаун транспорта».
	//   ⚠ F12 в обычном режиме открывает GM-меню (`UAsdaAdminSubsystem::PollHotkeys`); пока мы за
	//   рулём, приоритет у выхода — там стоит проверка `IsRiding()`, иначе одно нажатие и вернуло бы
	//   управление, и открыло меню.
	// ★ Из конфига: «EXT RIDE EXIT:» (cmd 92), дефолт прежний F12.
	const AAsdaPlayerController* AsdaPC = Cast<AAsdaPlayerController>(PC);
	const bool bRelease = AsdaPC && AsdaPC->IsCmdKeyDown(92);
	if (bRelease && !bReleaseKeyWasDown) { bReleaseKeyWasDown = true; ReleaseControl(); return; }
	if (!bRelease) { bReleaseKeyWasDown = false; }

	// ── L: включить/выключить фары (по фронту нажатия) ──────────────────────────────────────────
	// ★ Из конфига: «EXT RIDE LIGHT:» (cmd 93), дефолт прежний L.
	const bool bLight = AsdaPC && AsdaPC->IsCmdKeyDown(93);
	if (bLight && !bLightKeyWasDown)
	{
		bLightKeyWasDown = true;
		if (AAsdaRideVehicle* Car = Cast<AAsdaRideVehicle>(Vehicle)) { Car->ToggleHeadlights(); }
	}
	if (!bLight) { bLightKeyWasDown = false; }

	if (!Movement) { return; }

	// ── ПКМ + мышь: свободный обзор вокруг машины ───────────────────────────────────────────────
	//   Машину при этом не поворачиваем — крутится только камера (как у обычного обзора в игре).
	FAsdaOrbitCursor::ReportOrbit(PC->IsInputKeyDown(EKeys::RightMouseButton));   // ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: обзор из машины тоже не упирается в край
	if (PC->IsInputKeyDown(EKeys::RightMouseButton))
	{
		float MdX = 0.f, MdY = 0.f;
		PC->GetInputMouseDelta(MdX, MdY);
		if (AAsdaRideVehicle* Car = Cast<AAsdaRideVehicle>(Vehicle))
		{
			// Знак Y инвертирован: движение мыши вниз опускает камеру, как везде в игре.
			// ★ 19.10.2026: та же «Чувствительность поворота камеры» окна настроек (строка I3), что у обзора пешком.
			const float Turn = 2.5f * UAsdaGameUserSettings::GetCameraTurnScale() * FAsdaOrbitCursor::DeltaScale();
			Car->AddCameraInput(MdX * Turn, -MdY * Turn);
		}
	}

	// ── Руль ────────────────────────────────────────────────────────────────────────────────────
	float Steer = 0.f;
	if (PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::Left))  { Steer -= 1.f; }
	if (PC->IsInputKeyDown(EKeys::D) || PC->IsInputKeyDown(EKeys::Right)) { Steer += 1.f; }
	Movement->SetSteeringInput(Steer);

	// ── Газ, тормоз, задний ход ─────────────────────────────────────────────────────────────────
	//   `S` при движении ВПЕРЁД — это тормоз, и только на почти остановленной машине — задний ход.
	//   Так ведёт себя и оригинальный шаблон машины UE; иначе на полном ходу мгновенно втыкается
	//   реверс, что выглядит поломкой.
	const bool  bFwd   = PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::Up);
	const bool  bBack  = PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::Down);
	const bool  bBoost = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);
	const float Speed  = Movement->GetForwardSpeed();          // см/с, знак = направление

	const float Base  = FMath::Clamp(CVarRideThrottle.GetValueOnGameThread(), 0.f, 1.f);
	const float Boost = FMath::Max(1.f, CVarRideBoost.GetValueOnGameThread());
	const float Gas   = FMath::Clamp(bBoost ? Base * Boost : Base, 0.f, 1.f);

	float Throttle = 0.f, Brake = 0.f;
	if (bFwd && !bBack)
	{
		if (Speed < -50.f) { Brake = 1.f; }                    // катимся назад — сначала тормозим
		else               { Throttle = Gas; }
	}
	else if (bBack && !bFwd)
	{
		if (Speed > 50.f) { Brake = 1.f; }                     // едем вперёд — `S` тормозит
		else              { Throttle = -Gas; }                 // почти встали — задний ход
	}
	Movement->SetThrottleInput(Throttle);
	Movement->SetBrakeInput(Brake);

	// ── ДИАГНОСТИКА (раз в полсекунды): видно ли ввод, включена ли физика, стоят ли колёса на
	//    опоре. Гасится ключом `asda.Ride.Debug 0`.
	if (CVarRideDebug.GetValueOnGameThread() != 0)
	{
		DebugAccum += DeltaSeconds;
		if (DebugAccum >= 0.5f)
		{
			DebugAccum = 0.f;
			const USkeletalMeshComponent* MC = Vehicle->FindComponentByClass<USkeletalMeshComponent>();
			int32 OnGround = 0;
			if (const UChaosWheeledVehicleMovementComponent* WM =
				Cast<UChaosWheeledVehicleMovementComponent>(Movement))
			{
				for (int32 i = 0; i < WM->Wheels.Num(); ++i)
				{
					if (WM->Wheels[i] && WM->Wheels[i]->IsInAir() == false) { ++OnGround; }
				}
			}
			UE_LOG(LogAsdaRide, Warning,
				TEXT("[транспорт/диаг] W=%d S=%d throttle=%.2f brake=%.2f скорость=%.0f "
				     "физика=%d колёс=%d наЗемле=%d Z=%.0f"),
				bFwd ? 1 : 0, bBack ? 1 : 0, Throttle, Brake, Speed,
				(MC && MC->IsSimulatingPhysics()) ? 1 : 0,
				Cast<UChaosWheeledVehicleMovementComponent>(Movement)
					? Cast<UChaosWheeledVehicleMovementComponent>(Movement)->Wheels.Num() : -1,
				OnGround, Vehicle->GetActorLocation().Z);
		}
	}

	// ── Ручной тормоз ───────────────────────────────────────────────────────────────────────────
	// ★ Из конфига: «EXT RIDE HANDBRAKE:» (cmd 94), дефолт прежний SPACE.
	{
		const AAsdaPlayerController* AsdaPC2 = Cast<AAsdaPlayerController>(PC);
		Movement->SetHandbrakeInput(AsdaPC2 && AsdaPC2->IsCmdKeyDown(94));
	}
}
