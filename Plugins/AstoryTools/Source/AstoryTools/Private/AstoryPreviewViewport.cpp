// Copyright (c) 2026 @sitxovski (Discord)
// Реализация общей базы 3D-превью — см. AstoryPreviewViewport.h.
#include "AstoryPreviewViewport.h"

#include "AdvancedPreviewScene.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

// ════════════════════════════════════════════════════════════════════════════
//  Клиент
// ════════════════════════════════════════════════════════════════════════════
FAstoryPreviewViewportClient::FAstoryPreviewViewportClient(FAdvancedPreviewScene* InScene,
	const TSharedRef<SEditorViewport>& InViewport, const FVector& InViewLocation, const FRotator& InViewRotation,
	bool bSeparateTranslucency)
	: FEditorViewportClient(nullptr, InScene, InViewport)
{
	// Настройки как у превью-вьюпортов движка (SMaterialEditorViewport/StaticMeshEditor).
	DrawHelper.bDrawPivot = false;
	DrawHelper.bDrawWorldBox = false;
	DrawHelper.bDrawKillZ = false;
	DrawHelper.bDrawGrid = false;

	SetViewMode(VMI_Lit);
	EngineShowFlags.SetSnap(0);
	if (bSeparateTranslucency)
	{
		EngineShowFlags.SetSeparateTranslucency(true);   // эффекты — прозрачные квады
	}
	EngineShowFlags.SetSelectionOutline(false);
	OverrideNearClipPlane(1.0f);
	bUsingOrbitCamera = true;                            // ЛКМ-таскание = орбита вокруг модели
	bSetListenerPosition = false;
	SetRealtime(true);
	SetViewLocation(InViewLocation);
	SetViewRotation(InViewRotation);
}

void FAstoryPreviewViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);
	PreWorldTick(DeltaSeconds);
	// Мир превью тикаем сами — иначе акторы (эффекты) не симулируются.
	// (В не-игровом мире тик-функции акторов регистрируются сразу, см. AActor::PostRegisterAllComponents.)
	if (!GIntraFrameDebuggingGameThread && PreviewScene && PreviewScene->GetWorld())
	{
		PreviewScene->GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
	}
	PostWorldTick(DeltaSeconds);
}

// ════════════════════════════════════════════════════════════════════════════
//  Виджет
// ════════════════════════════════════════════════════════════════════════════
void SAstoryPreviewViewport::ConstructPreviewScene(bool bFloorVisible)
{
	PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
	PreviewScene->SetFloorVisibility(bFloorVisible, /*bDirect*/true);

	SEditorViewport::Construct(SEditorViewport::FArguments());

	// Компоненты-носители (создаются один раз, дальше только меняем меш/материалы).
	StaticComp = NewObject<UStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	StaticComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewScene->AddComponent(StaticComp, FTransform::Identity);

	SkelComp = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	SkelComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkelComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	PreviewScene->AddComponent(SkelComp, FTransform::Identity);
}

SAstoryPreviewViewport::~SAstoryPreviewViewport()
{
	PreviewScene.Reset();
}
