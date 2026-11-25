// Copyright (c) 2026 @sitxovski (Discord)
#include "AsdaLayeredCloudsActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AAsdaLayeredCloudsActor::AAsdaLayeredCloudsActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AAsdaLayeredCloudsActor::OnConstruction(const FTransform&)
{
	Rebuild();
}

#if WITH_EDITOR
void AAsdaLayeredCloudsActor::PostEditChangeProperty(FPropertyChangedEvent& E)
{
	Super::PostEditChangeProperty(E);
	Rebuild();
}
#endif

void AAsdaLayeredCloudsActor::Rebuild()
{
	for (UStaticMeshComponent* C : CloudLayers)
	{
		if (C) { C->DestroyComponent(); }
	}
	CloudLayers.Reset();
	if (!Master) { return; }
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (!Plane) { return; }
	const int32 N = FMath::Clamp(NumLayers, 1, 12);
	for (int32 i = 0; i < N; ++i)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(RootComponent);
		C->SetStaticMesh(Plane);
		C->SetMobility(EComponentMobility::Movable);
		C->SetCastShadow(false);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetRelativeLocation(FVector(0, 0, BottomZ + LayerStep * i));
		C->SetRelativeScale3D(FVector(PlaneScale, PlaneScale, 1.f));
		C->SetTranslucentSortPriority(4);          // под эффектами (5), поверх воды
		const float T = (N > 1) ? (float)i / (float)(N - 1) : 1.f;
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Master, this);
		if (CloudTexture) { MID->SetTextureParameterValue(TEXT("CloudTex"), CloudTexture); }
		MID->SetScalarParameterValue(TEXT("UVScale"), UVTiles * (1.f + 0.08f * i));
		// псевдослучайный сдвиг слоя — стек не выглядит «копиями»
		MID->SetScalarParameterValue(TEXT("OffU"), FMath::Frac(i * 0.377f));
		MID->SetScalarParameterValue(TEXT("OffV"), FMath::Frac(i * 0.611f));
		MID->SetScalarParameterValue(TEXT("SpeedU"), SpeedU * (1.f + 0.18f * i));
		MID->SetScalarParameterValue(TEXT("SpeedV"), SpeedV * (1.f + 0.18f * i));
		MID->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		MID->SetVectorParameterValue(TEXT("Tint"), FMath::Lerp(TintBottom, TintTop, T));
		C->SetMaterial(0, MID);
		C->RegisterComponent();
		CloudLayers.Add(C);
	}
}
