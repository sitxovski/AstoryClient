// Copyright (c) 2026 @sitxovski (Discord)
// Реализация вьюпорта превью — см. SAsdaItemPreviewViewport.h.
#include "SAsdaItemPreviewViewport.h"

#include "AdvancedPreviewScene.h"
#include "EditorViewportClient.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"
#include "Math/ScaleMatrix.h"

#include "AsdaEffectActor.h"   // AAsdaEffectActor — флаги посадки эффектов на кость

// ── Пространства (1:1 с рантаймом порта) ────────────────────────────────────
// Компонент модели персонажа: AAsdaPlayerCharacter::MeshRoot = (0.01, -0.01, 0.01)
//   (glTF-скелеталки метрами → см + компенсация лишнего флипа конвертера). Дроп-модели
//   AAsdaLootActor::Mesh — тот же масштаб. Значит ЛЮБАЯ модель предмета живёт в этом масштабе.
static const FVector kCharCompScale(0.01f, -0.01f, 0.01f);
// Смещение меша оружия относительно кости хвата (UAsdaCharAssembly::WeaponAttachInfo, «в руке»).
static const FVector kWeaponBoneOffset(0.f, 46.3f, 0.f);

// Модель относительно «кости» (в превью роль кости играет единичный трансформ).
FTransform SAsdaItemPreviewViewport::MakeMeshLocalTransform(bool bWeaponOffset)
{
	// Относительное смещение компонента задаётся в пространстве кости и масштабируется родителем.
	const FVector Loc = bWeaponOffset ? (kWeaponBoneOffset * kCharCompScale) : FVector::ZeroVector;
	return FTransform(FQuat::Identity, Loc, kCharCompScale);
}

// Эффект относительно той же «кости»: A = FxToBone · BoneCS · Comp (AAsdaEffectActor::Tick,
// ветка bFollowBoneFull = attach-10 эталона). В превью BoneCS = единица, Comp = масштаб персонажа;
// масштаб из A убирается (100·0.01 = 1), остаётся чистый поворот — эффект и меш совпадают 1:1.
static FTransform MakeFxLocal()
{
	static const FMatrix FxToBone(
		FPlane(0, 100, 0, 0),
		FPlane(100, 0, 0, 0),
		FPlane(0, 0, 100, 0),
		FPlane(0, 0, 0, 1));
	FMatrix A = FxToBone * FScaleMatrix(kCharCompScale);
	A.RemoveScaling();
	return FTransform(A.ToQuat(), FVector::ZeroVector, FVector::OneVector);
}

// ════════════════════════════════════════════════════════════════════════════
//  Вьюпорт-клиент: тик мира превью (иначе эффект не анимируется) + автовращение
// ════════════════════════════════════════════════════════════════════════════
// ★ Волна 3 п.15: общие настройки клиента (орбита, VMI_Lit, без сетки/пивота, ближняя плоскость 1, realtime)
//   и тик мира превью — в базе FAstoryPreviewViewportClient; здесь только своё: камера эффекта и автовращение.
class FAsdaItemPreviewClient : public FAstoryPreviewViewportClient
{
public:
	FAsdaItemPreviewClient(FAdvancedPreviewScene* InScene, const TSharedRef<SAsdaItemPreviewViewport>& InViewport)
		: FAstoryPreviewViewportClient(InScene, StaticCastSharedRef<SEditorViewport>(InViewport),
			FVector(0.f, -160.f, 60.f), FRotator(-10.f, 90.f, 0.f),
			/*bSeparateTranslucency: эффекты — прозрачные квады*/ true)
		, ViewportWidget(InViewport)
	{
	}

	bool  bAutoRotate = true;
	float RotSpeed = 30.f;      // град/с
	float Yaw = 0.f;

protected:
	virtual void PreWorldTick(float DeltaSeconds) override
	{
		// ★ Камера НАШЕЙ сцены — эффекту ДО тика мира (иначе он возьмёт камеру вьюпорта уровня:
		//   дистанц-кулл убивал эффект целиком, см. SetFxViewPoint).
		if (TSharedPtr<SAsdaItemPreviewViewport> W = ViewportWidget.Pin())
		{
			W->SetFxViewPoint(GetViewLocation(), GetViewRotation());
		}
	}

	virtual void PostWorldTick(float DeltaSeconds) override
	{
		if (bAutoRotate)
		{
			Yaw = FMath::Fmod(Yaw + RotSpeed * DeltaSeconds, 360.f);
			if (TSharedPtr<SAsdaItemPreviewViewport> W = ViewportWidget.Pin()) { W->ApplySpin(Yaw); }
		}
	}

private:
	TWeakPtr<SAsdaItemPreviewViewport> ViewportWidget;
};

// ════════════════════════════════════════════════════════════════════════════
void SAsdaItemPreviewViewport::Construct(const FArguments& InArgs)
{
	// Сцена, пол, SEditorViewport и компоненты-носители — общая база (тот же порядок, что был здесь).
	ConstructPreviewScene(bShowFloor);

	MeshLocal = MakeMeshLocalTransform(false);
	FxLocal = MakeFxLocal();
	ApplySpin(0.f);
}

SAsdaItemPreviewViewport::~SAsdaItemPreviewViewport()
{
	DestroyFxActor();
	Client.Reset();
	// PreviewScene сбрасывает деструктор базы SAstoryPreviewViewport — сразу после этого.
}

TSharedRef<FEditorViewportClient> SAsdaItemPreviewViewport::MakeEditorViewportClient()
{
	Client = MakeShared<FAsdaItemPreviewClient>(PreviewScene.Get(), SharedThis(this));
	return Client.ToSharedRef();
}

void SAsdaItemPreviewViewport::DestroyFxActor()
{
	if (AActor* A = FxActor.Get())
	{
		A->Destroy();
	}
	FxActor = nullptr;
}

void SAsdaItemPreviewViewport::ClearPreview()
{
	DestroyFxActor();
	if (StaticComp) { StaticComp->SetStaticMesh(nullptr); }
	if (SkelComp) { SkelComp->SetSkeletalMeshAsset(nullptr); }
}

// Свап эталонной текстуры предмета на все слоты (как UAsdaCharAssembly::ApplyItemTexture /
// AAsdaLootActor: параметр "Tex" в MID; строка материала внутри .jbm оригиналом не используется).
static void ApplyTexture(UMeshComponent* Comp, UTexture2D* Tex)
{
	if (!Comp || !Tex) { return; }
#if WITH_EDITOR
	// Пока текстура компилируется, движок отдаёт дефолтную серую 32×32 (грабли из item_texture_audit.md §8.4).
	Tex->BlockOnAnyAsyncBuild();
#endif
	for (int32 mi = 0; mi < Comp->GetNumMaterials(); ++mi)
	{
		if (UMaterialInterface* M = Comp->GetMaterial(mi))
		{
			if (UMaterialInstanceDynamic* MID = Comp->CreateDynamicMaterialInstance(mi, M))
			{
				MID->SetTextureParameterValue(TEXT("Tex"), Tex);
			}
		}
	}
}

void SAsdaItemPreviewViewport::SetPreview(const FAsdaPreviewRequest& Req)
{
	DestroyFxActor();

	MeshLocal = MakeMeshLocalTransform(Req.bWeaponBoneOffset);
	bOrientAllowed = Req.bAllowAutoOrient;

	if (StaticComp)
	{
		StaticComp->SetStaticMesh(Req.StaticMesh);
		StaticComp->EmptyOverrideMaterials();
		ApplyTexture(StaticComp, Req.Texture);
	}
	if (SkelComp)
	{
		SkelComp->SetSkeletalMeshAsset(Req.SkeletalMesh);
		SkelComp->EmptyOverrideMaterials();
		ApplyTexture(SkelComp, Req.Texture);
	}

	// Эффект: BP_Fx_* (Blueprint на AAsdaEffectActor). Флаг посадки на кость выставляем ДО
	// перезапуска конструкции, как это делает UAsdaWorldSubsystem::ApplyWeaponGlowToActor.
	if (Req.FxClass && PreviewScene.IsValid())
	{
		if (UWorld* W = PreviewScene->GetWorld())
		{
			FActorSpawnParameters SP;
			SP.ObjectFlags = RF_Transient;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			const FTransform FxWorld = FxLocal * FTransform(FRotator(0.f, CurrentYaw, 0.f));
			AActor* Fx = W->SpawnActor<AActor>(Req.FxClass, FxWorld, SP);
			if (Fx)
			{
				if (AAsdaEffectActor* E = Cast<AAsdaEffectActor>(Fx))
				{
					// ★ Камера = НАША превью-сцена (иначе эффект берёт камеру вьюпорта уровня и
					//   попадает под дистанц-кулл — «заточка не накладывает эффект»).
					E->bUseViewOverride = true;
					if (Client.IsValid())
					{
						E->ViewLocationOverride = Client->GetViewLocation();
						E->ViewRotationOverride = Client->GetViewRotation();
					}
					// Эффект НА КОСТИ (свечение оружия/щита): координаты .jeb = сырые координаты меша.
					E->bStrictQuadAttach = true;
					E->bFollowPosition = false;
					E->FollowActor = nullptr;
					E->AuthoredRot = FRotator::ZeroRotator;
					E->FollowOffset = FVector::ZeroVector;
					Fx->RerunConstructionScripts();   // прогрев OnConstruction уже с флагом
				}
				FxActor = Fx;
			}
		}
	}

	UpdateAlignRotation();
	ApplySpin(CurrentYaw);
	FocusOnPreview();
}

// ── АВТООРИЕНТАЦИЯ ──────────────────────────────────────────────────────────
// Модели предметов авторены в осях ИГРЫ: в мире их разворачивает матрица кости персонажа
// (меч/копьё/посох длинные по одной оси, арбалет/баллиста — по другой). В превью кости нет,
// поэтому часть оружия лежит боком. Главную (продольную) ось берём ИЗ ГЕОМЕТРИИ: самый длинный
// габарит бокса; знак — по смещению центра бокса относительно начала координат (= точки хвата),
// чтобы «рабочая часть» смотрела вверх. Доворот кладём на ВСЮ связку (модель + эффект),
// значит взаимная посадка свечения не меняется — оно так же сидит на клинке.
FQuat SAsdaItemPreviewViewport::ComputeAlignRotation(const FBox& LocalBounds, const FTransform& InMeshLocal)
{
	if (!LocalBounds.IsValid) { return FQuat::Identity; }

	const FVector Ext = LocalBounds.GetExtent();
	const FVector Center = LocalBounds.GetCenter();
	const int32 Axis = (Ext.X >= Ext.Y && Ext.X >= Ext.Z) ? 0 : ((Ext.Y >= Ext.Z) ? 1 : 2);
	// Второй по величине габарит — если предмет «кубический», выраженной продольной оси нет.
	double Second = 0.0;
	for (int32 i = 0; i < 3; ++i) { if (i != Axis) { Second = FMath::Max(Second, Ext[i]); } }
	if (Ext[Axis] <= Second * 1.15) { return FQuat::Identity; }

	FVector LocalDir = FVector::ZeroVector;
	LocalDir[Axis] = (Center[Axis] < 0.0) ? -1.0 : 1.0;   // «рабочая часть» относительно точки хвата (0)
	// Направление в мире с учётом масштаба-зеркала компонента, затем ставим его вертикально.
	const FVector WorldDir = InMeshLocal.TransformVector(LocalDir).GetSafeNormal();
	if (WorldDir.IsNearlyZero()) { return FQuat::Identity; }
	if (FMath::Abs(WorldDir.Z) > 0.98) { return FQuat::Identity; }   // уже вертикально — не трогаем
	return FQuat::FindBetweenNormals(WorldDir, FVector::UpVector);
}

void SAsdaItemPreviewViewport::UpdateAlignRotation()
{
	AlignQuat = FQuat::Identity;
	if (!bAutoOrient || !bOrientAllowed) { return; }

	FBox Local(ForceInit);
	if (StaticComp && StaticComp->GetStaticMesh())
	{
		Local = StaticComp->GetStaticMesh()->GetBoundingBox();
	}
	else if (SkelComp && SkelComp->GetSkeletalMeshAsset())
	{
		Local = SkelComp->GetSkeletalMeshAsset()->GetBounds().GetBox();
	}
	AlignQuat = ComputeAlignRotation(Local, MeshLocal);
}

void SAsdaItemPreviewViewport::SetAutoOrient(bool bEnable)
{
	if (bAutoOrient == bEnable) { return; }
	bAutoOrient = bEnable;
	UpdateAlignRotation();
	ApplySpin(CurrentYaw);
	FocusOnPreview();
}

void SAsdaItemPreviewViewport::ApplySpin(float YawDegrees)
{
	CurrentYaw = YawDegrees;
	// Общий «стенд»: автоориентация + вращение. Одинаков для модели и эффекта.
	const FTransform Stand = FTransform(AlignQuat) * FTransform(FRotator(0.f, YawDegrees, 0.f));
	if (StaticComp) { StaticComp->SetWorldTransform(MeshLocal * Stand); }
	if (SkelComp)   { SkelComp->SetWorldTransform(MeshLocal * Stand); }
	if (AActor* A = FxActor.Get()) { A->SetActorTransform(FxLocal * Stand); }
}

void SAsdaItemPreviewViewport::SetFxViewPoint(const FVector& ViewLoc, const FRotator& ViewRot)
{
	if (AAsdaEffectActor* E = Cast<AAsdaEffectActor>(FxActor.Get()))
	{
		E->bUseViewOverride = true;
		E->ViewLocationOverride = ViewLoc;
		E->ViewRotationOverride = ViewRot;
	}
}

void SAsdaItemPreviewViewport::SetAutoRotate(bool bEnable)
{
	if (Client.IsValid()) { Client->bAutoRotate = bEnable; }
}

bool SAsdaItemPreviewViewport::IsAutoRotate() const
{
	return Client.IsValid() ? Client->bAutoRotate : false;
}

void SAsdaItemPreviewViewport::SetRotateSpeed(float DegPerSec)
{
	if (Client.IsValid()) { Client->RotSpeed = DegPerSec; }
}

float SAsdaItemPreviewViewport::GetRotateSpeed() const
{
	return Client.IsValid() ? Client->RotSpeed : 0.f;
}

void SAsdaItemPreviewViewport::SetShowFloor(bool bVisible)
{
	bShowFloor = bVisible;
	if (PreviewScene.IsValid())
	{
		PreviewScene->SetFloorVisibility(bVisible, /*bDirect*/true);
		PreviewScene->SetEnvironmentVisibility(bVisible, /*bDirect*/true);
	}
}

void SAsdaItemPreviewViewport::FocusOnPreview()
{
	if (!Client.IsValid()) { return; }
	FBox Box(ForceInit);
	if (StaticComp && StaticComp->GetStaticMesh()) { Box += StaticComp->Bounds.GetBox(); }
	if (SkelComp && SkelComp->GetSkeletalMeshAsset()) { Box += SkelComp->Bounds.GetBox(); }
	if (AActor* A = FxActor.Get())
	{
		FVector Origin, Extent;
		A->GetActorBounds(/*bOnlyCollidingComponents*/false, Origin, Extent);
		// Эффекты бывают огромными (лучи/шлейфы) — берём их в кадр умеренно, чтобы модель не стала точкой.
		Box += FBox(Origin - Extent.GetClampedToMaxSize(150.f), Origin + Extent.GetClampedToMaxSize(150.f));
	}
	if (!Box.IsValid || Box.GetSize().IsNearlyZero())
	{
		Box = FBox(FVector(-50.f), FVector(50.f));
	}
	Client->FocusViewportOnBox(Box, /*bInstant*/true);
}
