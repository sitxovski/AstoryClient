// Copyright (c) 2026 @sitxovski (Discord)
// Реализация 3D-превью моба — см. SAsdaMobPreviewViewport.h.
#include "SAsdaMobPreviewViewport.h"

#include "AdvancedPreviewScene.h"
#include "EditorViewportClient.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"
#include "UnrealClient.h"

#include "AsdaMobPreviewClient.h"

// Масштаб компонента = масштаб актора моба (0.01, AsdaWorldSubsystem SpawnNetMobDynamic) × меш (1, −1, 1) (AAsdaMobActor ctor).
static const FVector kMobCompScale(0.01f, -0.01f, 0.01f);
// Поворот модели, при котором она смотрит в камеру (камера смотрит вдоль +X). Подобран по кадрам самопроверки:
//   модели мобов авторены лицом в +Y порта до зеркального фикса; компонент с (1,−1,1) и −90° смотрит в −X.
static constexpr float kFaceCameraYaw = -90.f;
// ★ NPC в игре ставятся тем же поворотом (asda_place_npcs.py: NPC_YAW_OFFSET −90, тот же масштаб), но модели NPC авторены лицом
//   в обратную сторону: при −90 превью показывало спину (кадр f02, Мейо). Для превью — разворот на 180°, чтобы видеть лицо.
static constexpr float kNpcExtraYaw = 180.f;
// Сдвиг камеры от направления лучей: свет чуть сбоку даёт объём, но граница тени уходит к краю модели.
static constexpr float kLightSideOffsetDeg = 30.f;

// ════════════════════════════════════════════════════════════════════════════
//  Клиент
// ════════════════════════════════════════════════════════════════════════════
FAsdaMobPreviewClient::FAsdaMobPreviewClient(FAdvancedPreviewScene* InScene, const TSharedRef<SAsdaMobPreviewViewport>& InViewport)
	: FAstoryPreviewViewportClient(InScene, StaticCastSharedRef<SEditorViewport>(InViewport),
		FVector(-300.f, 0.f, 100.f), FRotator(-12.f, 0.f, 0.f), /*bSeparateTranslucency: у мобов есть прозрачные части*/ true)
	, ViewportWidget(InViewport)
{
	bUsingOrbitCamera = false;   // камера своя: модель крутится, камера смотрит на центр модели
}

bool FAsdaMobPreviewClient::InputKey(const FInputKeyEventArgs& Args)
{
	TSharedPtr<SAsdaMobPreviewViewport> W = ViewportWidget.Pin();
	if (!W.IsValid()) { return FAstoryPreviewViewportClient::InputKey(Args); }
	if (Args.Key == EKeys::LeftMouseButton)
	{
		if (Args.Event == IE_Pressed) { bRotating = true; }
		else if (Args.Event == IE_Released) { bRotating = false; }
		else if (Args.Event == IE_DoubleClick) { W->ResetView(); }
		return true;
	}
	if (Args.Key == EKeys::RightMouseButton)
	{
		if (Args.Event == IE_Pressed) { bTilting = true; }
		else if (Args.Event == IE_Released) { bTilting = false; }
		return true;
	}
	if (Args.Key == EKeys::MouseScrollUp || Args.Key == EKeys::MouseScrollDown)
	{
		if (Args.Event == IE_Pressed)
		{
			const float Step = (Args.Key == EKeys::MouseScrollUp) ? (1.f / 1.12f) : 1.12f;
			W->SetZoom(W->GetZoom() * Step);
		}
		return true;
	}
	if (Args.Key == EKeys::MiddleMouseButton || Args.Key == EKeys::ThumbMouseButton || Args.Key == EKeys::ThumbMouseButton2)
	{
		return true;   // камеры движка (панорама/полёт) нет: всё управление — своё
	}
	return FAstoryPreviewViewportClient::InputKey(Args);
}

bool FAsdaMobPreviewClient::InputAxis(const FInputKeyEventArgs& Args)
{
	TSharedPtr<SAsdaMobPreviewViewport> W = ViewportWidget.Pin();
	if (W.IsValid() && (Args.Key == EKeys::MouseX || Args.Key == EKeys::MouseY))
	{
		if (Args.Key == EKeys::MouseX && bRotating) { W->SetModelYaw(W->GetModelYaw() + Args.AmountDepressed * kDegPerPixel); }
		if (Args.Key == EKeys::MouseY && bTilting)  { W->SetCameraPitch(W->GetCameraPitch() + Args.AmountDepressed * kDegPerPixel); }
		return true;
	}
	return FAstoryPreviewViewportClient::InputAxis(Args);
}

void FAsdaMobPreviewClient::PostWorldTick(float DeltaSeconds)
{
	if (TSharedPtr<SAsdaMobPreviewViewport> W = ViewportWidget.Pin()) { W->ApplyCamera(); }
}

// ════════════════════════════════════════════════════════════════════════════
//  Виджет
// ════════════════════════════════════════════════════════════════════════════
void SAsdaMobPreviewViewport::Construct(const FArguments& InArgs)
{
	ConstructPreviewScene(bShowFloor);   // сцена, пол, SEditorViewport (→ MakeEditorViewportClient), носители
	ApplyModelTransform();
}

SAsdaMobPreviewViewport::~SAsdaMobPreviewViewport()
{
	Client.Reset();
	// Сцену сбрасывает деструктор базы SAstoryPreviewViewport.
}

TSharedRef<FEditorViewportClient> SAsdaMobPreviewViewport::MakeEditorViewportClient()
{
	Client = MakeShared<FAsdaMobPreviewClient>(PreviewScene.Get(), SharedThis(this));
	return Client.ToSharedRef();
}

void SAsdaMobPreviewViewport::ClearModel()
{
	if (SkelComp) { SkelComp->Stop(); SkelComp->EmptyOverrideMaterials(); SkelComp->SetSkeletalMeshAsset(nullptr); }
	if (StaticComp) { StaticComp->EmptyOverrideMaterials(); StaticComp->SetStaticMesh(nullptr); }
	ModelBounds = FBox(ForceInit);
}

void SAsdaMobPreviewViewport::ApplyMaterials(UMeshComponent* MC, const FAsdaMobModelAssets& Assets)
{
	if (!MC) { return; }
	const TArray<FName> SlotNames = MC->GetMaterialSlotNames();
	for (int32 i = 0; i < SlotNames.Num(); ++i)
	{
		for (const TPair<FName, UMaterialInterface*>& O : Assets.MaterialOverrides)
		{
			if (O.Key == SlotNames[i] && O.Value) { MC->SetMaterial(i, O.Value); break; }
		}
	}
}

void SAsdaMobPreviewViewport::SetModel(const FAsdaMobModelAssets& Assets, bool bNpc)
{
	BaseYaw = kFaceCameraYaw + (bNpc ? kNpcExtraYaw : 0.f);
	ClearModel();
	ModelYaw = 0.f;
	if (Assets.Skel && SkelComp)
	{
		SkelComp->SetSkeletalMeshAsset(Assets.Skel);
		ApplyMaterials(SkelComp, Assets);
	}
	else if (Assets.Static && StaticComp)
	{
		StaticComp->SetStaticMesh(Assets.Static);
		ApplyMaterials(StaticComp, Assets);
	}
	ApplyModelTransform();
	EnsureTexturesReady();
	// Габарит — в позе привязки при нулевом повороте пользователя (камера по нему; поворот модели её не двигает).
	//   Берём границы САМОГО меша (импортные), а не компонента: у скелета компонент считает их по физассету, который у
	//   части моделей меньше геометрии (голова Мейо уходила за кадр).
	if (HasSkeletal())
	{
		ModelBounds = SkelComp->GetSkeletalMeshAsset()->GetBounds().GetBox().TransformBy(SkelComp->GetComponentTransform());
	}
	else if (HasStatic())
	{
		ModelBounds = StaticComp->GetStaticMesh()->GetBounds().GetBox().TransformBy(StaticComp->GetComponentTransform());
	}
	Zoom = 1.f;
	CamPitch = -12.f;
	ApplyCamera();
}

// Текстуры материалов модели — собранные и с полными мипами: пока текстура компилируется (первая загрузка в сессии
//   редактора), движок рисует заглушку и модель выходит белой (та же грабля в браузере предметов, item_texture_audit.md §8.4).
void SAsdaMobPreviewViewport::EnsureTexturesReady()
{
	UMeshComponent* MC = HasSkeletal() ? static_cast<UMeshComponent*>(SkelComp) : (HasStatic() ? static_cast<UMeshComponent*>(StaticComp) : nullptr);
	if (!MC) { return; }
	for (int32 i = 0; i < MC->GetNumMaterials(); ++i)
	{
		UMaterialInterface* M = MC->GetMaterial(i);
		if (!M) { continue; }
		TArray<UTexture*> Used;
		M->GetUsedTextures(Used);   // без уровня качества и платформы — все уровни и платформы
		for (UTexture* T : Used)
		{
			if (!T) { continue; }
#if WITH_EDITOR
			T->BlockOnAnyAsyncBuild();
#endif
			if (UTexture2D* T2 = Cast<UTexture2D>(T)) { T2->SetForceMipLevelsToBeResident(30.f); }
		}
	}
}

bool SAsdaMobPreviewViewport::HasSkeletal() const { return SkelComp && SkelComp->GetSkeletalMeshAsset() != nullptr; }
bool SAsdaMobPreviewViewport::HasStatic() const { return StaticComp && StaticComp->GetStaticMesh() != nullptr; }

bool SAsdaMobPreviewViewport::PlayClip(UAnimSequence* Clip, bool bLoop)
{
	if (!HasSkeletal()) { return false; }
	if (!Clip)
	{
		SkelComp->Stop();
		SkelComp->SetAnimation(nullptr);
		return false;
	}
	// Скелет клипа обязан совпадать со скелетом меша, иначе движок молча не играет (и предупреждает в журнал).
	if (Clip->GetSkeleton() != SkelComp->GetSkeletalMeshAsset()->GetSkeleton()) { return false; }
	SkelComp->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	SkelComp->PlayAnimation(Clip, bLoop);
	SkelComp->bPauseAnims = bPaused;
	return true;
}

UAnimSequence* SAsdaMobPreviewViewport::GetCurrentClip() const
{
	if (!HasSkeletal() || SkelComp->GetAnimationMode() != EAnimationMode::AnimationSingleNode) { return nullptr; }
	const UAnimSingleNodeInstance* SNI = SkelComp->GetSingleNodeInstance();
	return SNI ? Cast<UAnimSequence>(SNI->GetAnimationAsset()) : nullptr;
}

void SAsdaMobPreviewViewport::SetPaused(bool bPause)
{
	bPaused = bPause;
	if (SkelComp) { SkelComp->bPauseAnims = bPause; }
}

void SAsdaMobPreviewViewport::ApplyModelTransform()
{
	const FVector Scale = bDebugNoMirror ? FVector(0.01f) : kMobCompScale;
	const FTransform Xf(FRotator(0.f, BaseYaw + CameraYaw() + ModelYaw, 0.f), FVector::ZeroVector, Scale);
	if (SkelComp) { SkelComp->SetWorldTransform(Xf); }
	if (StaticComp) { StaticComp->SetWorldTransform(Xf); }
}

void SAsdaMobPreviewViewport::SetModelYaw(float Deg)
{
	ModelYaw = FRotator::NormalizeAxis(Deg);
	ApplyModelTransform();
}

void SAsdaMobPreviewViewport::SetCameraPitch(float Deg)
{
	CamPitch = FMath::Clamp(Deg, -80.f, 30.f);
	ApplyCamera();
}

void SAsdaMobPreviewViewport::SetZoom(float Factor)
{
	Zoom = FMath::Clamp(Factor, 0.15f, 6.f);
	ApplyCamera();
}

void SAsdaMobPreviewViewport::ResetView()
{
	SetModelYaw(0.f);
	Zoom = 1.f;
	CamPitch = -12.f;
	ApplyCamera();
}

void SAsdaMobPreviewViewport::SetShowFloor(bool bVisible)
{
	bShowFloor = bVisible;
	if (PreviewScene.IsValid()) { PreviewScene->SetFloorVisibility(bVisible, /*bDirect*/true); }
}

void SAsdaMobPreviewViewport::ApplyCamera()
{
	if (!Client.IsValid()) { return; }
	FVector Center(0.f, 0.f, 80.f);
	float Radius = 120.f;
	if (ModelBounds.IsValid)
	{
		Center = ModelBounds.GetCenter();
		Radius = FMath::Max(ModelBounds.GetExtent().Size(), 10.f);
	}
	// ViewFOV — горизонтальный угол; у широкого вьюпорта тесен ВЕРТИКАЛЬНЫЙ: берём меньший из двух (иначе высокие модели режутся).
	const float HalfH = FMath::DegreesToRadians(FMath::Max(Client->ViewFOV, 20.f) * 0.5f);
	const FIntPoint VpSize = Client->Viewport ? Client->Viewport->GetSizeXY() : FIntPoint(16, 9);
	const float Aspect = (VpSize.X > 0 && VpSize.Y > 0) ? (float)VpSize.X / (float)VpSize.Y : 16.f / 9.f;
	const float HalfV = FMath::Atan(FMath::Tan(HalfH) / Aspect);
	const float HalfFov = FMath::Min(HalfH, HalfV);
	const float Dist = Radius / FMath::Sin(HalfFov) * 1.05f * Zoom;   // сфера габарита целиком в кадре при Zoom = 1
	const FRotator Rot(CamPitch, CameraYaw(), 0.f);
	Client->SetViewLocation(Center - Rot.Vector() * Dist);
	Client->SetViewRotation(Rot);
	// Ближняя/дальняя плоскости — по масштабу модели (у мелких мобов габарит ~50 ед., у боссов — тысячи).
	Client->OverrideNearClipPlane(FMath::Max(1.f, Dist * 0.01f));
}

void SAsdaMobPreviewViewport::SetDebugViewMode(int32 Mode)
{
	if (!Client.IsValid()) { return; }
	if (Mode == 3)
	{
		Client->SetViewMode(VMI_VisualizeBuffer);
		Client->CurrentBufferVisualizationMode = FName(TEXT("WorldNormal"));
		return;
	}
	Client->SetViewMode(Mode == 1 ? VMI_LightingOnly : (Mode == 2 ? VMI_Unlit : VMI_Lit));
}

// ★ Свет превью (владелец 09.11: «полоса посередине модели»). Направленный свет сцены превью падал на модель СБОКУ
//   относительно прежней камеры (вдоль +X): граница света и тени шла ровно по середине лица, а материал персонажей
//   рисует её резко. Свет профиля не трогаем (движок пишет его направление в общий профиль превью редактора — в ini),
//   вместо этого ставим КАМЕРУ со стороны света: смотрим почти по направлению лучей (свет из-за плеча, сдвиг 30°),
//   модель разворачивается к камере. Режим «как раньше» — ключ не нужен: это только вид редакторского превью.
float SAsdaMobPreviewViewport::CameraYaw() const
{
	if (!PreviewScene.IsValid()) { return 0.f; }
	const FRotator L = PreviewScene->GetLightDirection();   // направленный свет светит вдоль своей оси X
	return FRotator::NormalizeAxis(L.Yaw + kLightSideOffsetDeg);
}
