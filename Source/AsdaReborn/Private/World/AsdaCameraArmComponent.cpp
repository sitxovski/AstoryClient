// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaCameraArmComponent — правило камеры оригинала (25.10.2026). Разбор и доказательства — в шапке заголовка.
#include "World/AsdaCameraArmComponent.h"
#include "AsdaDevConsole.h"          // ASDA_DEV_CVAR — дев-ключ вне Shipping
#include "World/AsdaGroundTrace.h"   // AsdaGround::GroundZ — высота логической сетки .TER (как у ног персонажа)
#include "Engine/World.h"
#include "GameFramework/Pawn.h"       // IsLocallyControlled — правило только для стрелы своего персонажа

// ★ [ОРИГИНАЛ] 25.10.2026: камера без столкновений с объектами, глаз не ниже `.TER + 60` (cameraUpdate@0x959880).
static ASDA_DEV_CVAR(int32, CVarCameraOriginalNoCollide, TEXT("asda.Camera.OriginalNoCollide"), 1,
	TEXT("1 = камера как в оригинале (cameraUpdate@0x959880): игроки, мобы, NPC и объекты карты стрелу НЕ укорачивают, ")
	TEXT("глаз поднимается не ниже высоты земли .TER + 60 в своей точке и смотрит на цель; ")
	TEXT("0 = прежнее поведение порта (сфера по каналу Camera укорачивает стрелу при любом блокирующем попадании)."),
	ECVF_Default);

bool AsdaCameraOriginalNoCollideOn()
{
	return CVarCameraOriginalNoCollide.GetValueOnGameThread() != 0;
}

UAsdaCameraArmComponent::UAsdaCameraArmComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UAsdaCameraArmComponent::UpdateDesiredArmLocation(bool bDoTrace, bool bDoLocationLag, bool bDoRotationLag, float DeltaTime)
{
	const bool bOriginal = AsdaCameraOriginalNoCollideOn();
	// Ключ 0 — штатный расчёт без изменений (проверка по `bDoCollisionTest` владельца).
	// Ключ 1 — у оригинала проверки препятствий нет вовсе: расчёт глаза тот же, но без сферы по каналу Camera.
	Super::UpdateDesiredArmLocation(bOriginal ? false : bDoTrace, bDoLocationLag, bDoRotationLag, DeltaTime);

	bGroundLifted = false;
	LastGroundLift = 0.f;
	LastGroundZ = NAN;
	if (!bOriginal) { return; }

	// Стрела соседа не рисует кадр — высоту под её глазом не считаем (и не пишем журнал высоты каждый кадр).
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled()) { return; }

	UWorld* W = GetWorld();
	if (!W) { return; }

	// Глаз камеры, рассчитанный штатно (мировые координаты сокета стрелы).
	const FTransform CompTM = GetComponentTransform();
	const FVector Eye = CompTM.TransformPosition(RelativeSocketLocation);

	// Высота логической сетки .TER под глазом — `terrainGetCellByPos_A840@0x95A840` → `terrainInterpHeightAtXY@0x1130F90`
	//   (тот же источник, что у ног персонажа; объекты карты в высоте не участвуют). Нет данных — правило не применяется.
	const float Ground = AsdaGround::GroundZ(W, (float)Eye.X, (float)Eye.Y, NAN, GetOwner());
	if (!FMath::IsFinite(Ground)) { return; }
	LastGroundZ = Ground;

	// Эталон: `if (земля + g_fViewHeightOffset <= глаз.y)` — кадр как есть; иначе глаз на высоту `земля + 60`.
	const float MinZ = Ground + kGroundClearance;
	if ((float)Eye.Z >= MinZ) { return; }

	// Подъём в той же точке XZ и взгляд на цель стрелы (`cameraSetViewLookAtTarget`; цель = игрок + 120 = начало стрелы).
	// [ИСПРАВЛЕНИЕ] За краем сетки (X/Z вне 0..51100) оригинал переносит глаз в ЗАЖАТУЮ точку края карты (скачок кадра,
	//   `terrainGetCellByPos_A840@0x95A840` зажимает пробную точку на месте); порт оставляет глаз в его точке XZ.
	const FVector NewEye(Eye.X, Eye.Y, MinZ);
	const FVector Target = PreviousArmOrigin;   // начало стрелы этого кадра (Super только что записал его)
	FVector Look = Target - NewEye;
	if (Look.IsNearlyZero()) { return; }
	FRotator LookRot = Look.Rotation();
	LookRot.Roll = 0.f;

	const FTransform WorldCamTM(LookRot, NewEye);
	const FTransform RelCamTM = WorldCamTM.GetRelativeTransform(CompTM);
	RelativeSocketLocation = RelCamTM.GetLocation();
	RelativeSocketRotation = RelCamTM.GetRotation();
	bGroundLifted = true;
	LastGroundLift = MinZ - (float)Eye.Z;
	UpdateChildTransforms();
}
