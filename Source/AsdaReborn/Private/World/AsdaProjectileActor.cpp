// Copyright (c) 2026 @sitxovski (Discord)
// Реализация AAsdaProjectileActor — см. заголовок (эталон Reports/projectile_re.md).
#include "World/AsdaProjectileActor.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"

AAsdaProjectileActor::AAsdaProjectileActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;   // позиция снаряда после движения носителей

	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(RootScene);

	ArrowMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ArrowMesh"));
	ArrowMesh->SetupAttachment(RootScene);
	// Снаряд — чистая визуализация: столкновений нет (эталон ведёт трассу сам, попадание решает сервер).
	ArrowMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ArrowMesh->SetCastShadow(false);
	ArrowMesh->SetRelativeRotation(MeshAlignRot);
	ArrowMesh->SetRelativeScale3D(FVector(MeshScale));
}

void AAsdaProjectileActor::Launch(const FVector& From, const FVector& To, float InSpeedUnitsPerMs)
{
	StartPos = From;
	EndPos   = To;
	if (InSpeedUnitsPerMs > 0.01f) { SpeedUnitsPerMs = InSpeedUnitsPerMs; }

	const FVector Delta = EndPos - StartPos;
	const float Dist = (float)Delta.Size();
	FlightDir   = (Dist > KINDA_SMALL_NUMBER) ? (FVector)(Delta / Dist) : GetActorForwardVector();
	FlightTimeS = FlightTimeSeconds(Dist, SpeedUnitsPerMs);
	ElapsedS    = 0.f;
	bArrived    = false;
	bLaunched   = true;

	SetActorLocation(StartPos);
	// Ориентация 1:1 (cBillboardInitFromAxis: строка2 матрицы = направление полёта): «вперёд» актора
	//   ведём по FlightDir, а доворот/масштаб меша задаём на КОМПОНЕНТЕ (редактируется в UE).
	SetActorRotation(FlightDir.Rotation());
	if (ArrowMesh)
	{
		ArrowMesh->SetRelativeRotation(MeshAlignRot);
		ArrowMesh->SetRelativeScale3D(FVector(MeshScale));
	}
	// Страховка от «зависших» снарядов (потеря тика/паузы): вдвое дольше расчётного полёта, минимум 2 с.
	SetLifeSpan(FMath::Max(2.f, FlightTimeS * 2.f + 1.f));
}

// ★ 05.11.2026 [ОРИГИНАЛ] полёт дугой — разбор в заголовке.
void AAsdaProjectileActor::LaunchArc(const FVector& From, AActor* HomingTarget, const FVector& EndFallback, float DurationS, float ApexUp)
{
	StartPos = From;
	EndPos = HomingTarget ? HomingTarget->GetActorLocation() : EndFallback;
	ArcTarget = HomingTarget;
	ArcApexUp = ApexUp;
	bArc = true;
	FlightTimeS = FMath::Max(0.05f, DurationS);
	ElapsedS = 0.f;
	bArrived = false;
	bLaunched = true;
	const FVector Delta = EndPos - StartPos;
	FlightDir = Delta.IsNearlyZero() ? GetActorForwardVector() : Delta.GetSafeNormal();
	SetActorLocation(StartPos);
	SetLifeSpan(FlightTimeS * 2.f + 1.f);   // страховка от «зависшего» снаряда
}

void AAsdaProjectileActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bLaunched || bArrived) { return; }

	if (bArc)
	{
		ElapsedS += DeltaSeconds;
		if (AActor* T = ArcTarget.Get()) { EndPos = T->GetActorLocation(); }   // `beamSelf[9..11]` — позиция цели каждый кадр
		// `effectBeamUpdate`: вершина = середина по плоскости, высота = конец + 750; точка = Безье(старт, вершина, конец; доля).
		const FVector Apex((StartPos.X + EndPos.X) * 0.5, (StartPos.Y + EndPos.Y) * 0.5, EndPos.Z + ArcApexUp);
		const float U = FMath::Clamp(ElapsedS / FlightTimeS, 0.f, 1.f);
		const FVector P = StartPos * FMath::Square(1.f - U) + Apex * (2.f * U * (1.f - U)) + EndPos * FMath::Square(U);
		SetActorLocation(P);
		if (ElapsedS >= FlightTimeS)
		{
			bArrived = true;
			OnArrive.ExecuteIfBound();
			Destroy();
		}
		return;
	}

	ElapsedS += DeltaSeconds;
	// Эталон effectNodeMotionUpdate mode 2: смещение = dir · speed · elapsed, с клампом на длительность.
	const float T = FMath::Min(ElapsedS, FlightTimeS);
	SetActorLocation(StartPos + FlightDir * (SpeedUnitsPerMs * 1000.f) * T);

	if (ElapsedS >= FlightTimeS)
	{
		bArrived = true;
		OnArrive.ExecuteIfBound();   // цифры урона + импакт + звук попадания (эталон: всё по прилёте)
		Destroy();
	}
}

void AAsdaProjectileActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// ★ Шлейф не должен пережить снаряд (закон: эффект гаснет вместе с носителем; attached-актор
	//   в UE при Destroy() родителя НЕ уничтожается автоматически — только отцепляется).
	if (TrailFx && IsValid(TrailFx))
	{
		TrailFx->Destroy();
	}
	TrailFx = nullptr;
	Super::EndPlay(EndPlayReason);
}
