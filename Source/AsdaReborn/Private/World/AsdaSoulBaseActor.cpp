// Copyright (c) 2026 @sitxovski (Discord)
// Реализация AAsdaSoulBaseActor — разбор в заголовке.
#include "World/AsdaSoulBaseActor.h"
#include "AsdaEffectActor.h"
#include "AsdaNetSubsystem.h"
#include "Animation/AnimSequence.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Resources/AsdaModelAnimSubsystem.h"
#include "World/AsdaPlayerCharacter.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoulBase, Log, All);

namespace
{
	// Корень модели духа (`Tools/asda_import_soulbase_model.py`).
	const TCHAR* const kSkelRootSoulBase = TEXT("/Game/AsdaAssets/Models/Skeletal/SoulBase");
	// Имя модели (`soulBase_LoadIdleMesh@0xFB66E0`).
	const TCHAR* const kSoulModel = TEXT("soul_base_idle01");
	// Кость эффектов (`soulEffAttachToNeckBone@0xFB6B80`, «Bip01 Neck»; импорт заменяет пробелы дефисом).
	const FName kNeckBone(TEXT("Bip01-Neck"));
}

AAsdaSoulBaseActor::AAsdaSoulBaseActor()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	MeshRoot = CreateDefaultSubobject<USceneComponent>(TEXT("MeshRoot"));
	MeshRoot->SetupAttachment(Root);
	MeshRoot->SetRelativeScale3D(FVector(0.01f, -0.01f, 0.01f));
	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(MeshRoot);
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);   // эталон ставит духа только в очередь рисования `renderQueueAddListObjects`
}

void AAsdaSoulBaseActor::InitSoul(AActor* InOwnerChar, int32 InUserIdx, bool bBirthFlash)
{
	OwnerChar = InOwnerChar;
	UserIdx = InUserIdx;
	USkeletalMesh* Model = UAsdaModelAnimSubsystem::LoadMeshChecked(UAsdaModelAnimSubsystem::BuildSkelPath(kSkelRootSoulBase, kSoulModel));
	if (!Model)
	{
		UE_LOG(LogAsdaSoulBase, Warning, TEXT("[дух] модель не импортирована (Tools/asda_import_soulbase_model.py)"));
		return;
	}
	Mesh->SetSkeletalMesh(Model);
	PlayTrack(0);
	bBirthPending = bBirthFlash;
}

bool AAsdaSoulBaseActor::DebugHasMesh() const { return Mesh && Mesh->GetSkeletalMeshAsset() != nullptr; }

bool AAsdaSoulBaseActor::DebugHasNeck() const { return Mesh && Mesh->DoesSocketExist(kNeckBone); }

FString AAsdaSoulBaseActor::DebugDescribe() const
{
	const AAsdaEffectActor* Fx = IdleFx.IsValid() ? IdleFx.Get() : HpFx.Get();
	return FString::Printf(TEXT("актор %s меш %s размер %s шея %s эффект %s"), *GetActorLocation().ToCompactString(), *Mesh->Bounds.Origin.ToCompactString(),
		*Mesh->Bounds.BoxExtent.ToCompactString(), *Mesh->GetSocketLocation(kNeckBone).ToCompactString(), Fx ? *Fx->GetActorLocation().ToCompactString() : TEXT("нет"));
}

void AAsdaSoulBaseActor::PlayTrack(int32 InTrack)
{
	UAnimSequence* Clip = UAsdaModelAnimSubsystem::LoadClipChecked(UAsdaModelAnimSubsystem::BuildSkelAnimPath(kSkelRootSoulBase, kSoulModel, InTrack));
	if (!Clip) { return; }
	Mesh->PlayAnimation(Clip, /*bLooping=*/false);
	Track = InTrack;
	ClipLeftSec = Clip->GetPlayLength();
}

void AAsdaSoulBaseActor::UpdateFollow(AActor* Own)
{
	// Поворот и тело — как у питомца: меш владельца и «хвост» сетевой поправки.
	FRotator OwnRot = Own->GetActorRotation();
	if (const USceneComponent* OwnMesh = Own->FindComponentByClass<USkeletalMeshComponent>()) { OwnRot = OwnMesh->GetComponentRotation(); }
	OwnRot.Pitch = 0.f;
	OwnRot.Roll = 0.f;
	FVector Body = Own->GetActorLocation();
	if (const AAsdaPlayerCharacter* OwnCh = Cast<AAsdaPlayerCharacter>(Own)) { Body += OwnCh->GetVisualTail(); }
	FTrailPoint Cur;
	Cur.Loc = Body + OwnRot.RotateVector(FollowOffset);
	Cur.Yaw = OwnRot.Yaw;

	FTrailPoint Show = Cur;
	const bool bTrail = Queue.Num() >= TrailFrames;
	Queue.Add(Cur);
	if (bTrail)
	{
		Show = Queue[0];
		const FTrailPoint Oldest = Show;
		Queue.RemoveAll([&Oldest](const FTrailPoint& P) { return P.Same(Oldest); });
	}
	SetActorLocation(Show.Loc);
	MeshRoot->SetWorldRotation(FRotator(0.f, Show.Yaw, 0.f));
}

void AAsdaSoulBaseActor::UpdateTrack(float DeltaSeconds)
{
	ClipLeftSec -= DeltaSeconds;
	if (ClipLeftSec > 0.f) { return; }
	if (Queue.Num() >= TrailFrames) { PlayTrack(2); return; }
	PlayTrack(((FMath::Rand() + FMath::Rand()) % 10 >= 2) ? 1 : 0);
}

AAsdaEffectActor* AAsdaSoulBaseActor::SpawnNeckFx(const TCHAR* Jeb, bool bOneShot)
{
	UWorld* W = GetWorld();
	if (!W) { return nullptr; }
	const bool bNeck = DebugHasNeck();
	const FVector At = bNeck ? Mesh->GetSocketLocation(kNeckBone) : GetActorLocation();
	AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnByJebName(W, Jeb, At, FRotator::ZeroRotator, this);
	if (!Fx)
	{
		UE_LOG(LogAsdaSoulBase, Warning, TEXT("[дух] эффект %s не собран (Tools/asda_build_skill_fx.py)"), Jeb);
		return nullptr;
	}
	Fx->ConfigureBoneAttach(this, Mesh, bNeck ? kNeckBone : FName(NAME_None), bNeck ? EAsdaFxAttachMode::BonePos : EAsdaFxAttachMode::CharPos);
	if (bOneShot) { Fx->SetLifeSpan(Fx->NaturalLifeSpan(0.5f, 1.f, 30.f)); }
	return Fx;
}

void AAsdaSoulBaseActor::UpdateHpFx()
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const UAsdaNetSubsystem::FAsdaVitals* V = Net ? Net->GetVitals(UserIdx) : nullptr;
	// HP соседа может быть ещё неизвестен: тогда покой, как при полном здоровье.
	const bool bLow = V && V->HpCur >= 0 && V->HpMax > 0 && (double)V->HpCur < (double)V->HpMax * (double)LowHpFraction;
	TWeakObjectPtr<AAsdaEffectActor>& On = bLow ? HpFx : IdleFx;
	TWeakObjectPtr<AAsdaEffectActor>& Off = bLow ? IdleFx : HpFx;
	if (On.IsValid()) { return; }
	if (AAsdaEffectActor* Old = Off.Get()) { Old->Destroy(); }
	Off.Reset();
	On = SpawnNeckFx(bLow ? TEXT("souleff_base_hp01") : TEXT("souleff_base_idle01"), /*bOneShot=*/false);
}

void AAsdaSoulBaseActor::SetFxHidden(bool bHide)
{
	if (AAsdaEffectActor* Fx = IdleFx.Get()) { Fx->SetActorHiddenInGame(bHide); }
	if (AAsdaEffectActor* Fx = HpFx.Get()) { Fx->SetActorHiddenInGame(bHide); }
	if (AAsdaEffectActor* Fx = BirthFx.Get()) { Fx->SetActorHiddenInGame(bHide); }
}

void AAsdaSoulBaseActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AActor* Own = OwnerChar.Get();
	if (!Own) { Destroy(); return; }
	const bool bHide = Own->IsHidden();
	if (bHide != IsHidden())
	{
		SetActorHiddenInGame(bHide);
		SetFxHidden(bHide);
	}
	if (bHide)
	{
		Queue.Reset();
		return;
	}
	UpdateFollow(Own);
	if (!DebugHasMesh()) { return; }
	UpdateTrack(DeltaSeconds);
	// Вспышка рождается на шее уже стоящего духа: эмиттеры `soulritec-a_03` заданы от точки эффекта, а актор создан в позиции владельца.
	if (bBirthPending)
	{
		bBirthPending = false;
		BirthFx = SpawnNeckFx(TEXT("eff_1_char_99_soulritec-a_03"), /*bOneShot=*/true);
	}
	UpdateHpFx();
}

void AAsdaSoulBaseActor::EndPlay(const EEndPlayReason::Type Reason)
{
	if (AAsdaEffectActor* Fx = IdleFx.Get()) { Fx->Destroy(); }
	if (AAsdaEffectActor* Fx = HpFx.Get()) { Fx->Destroy(); }
	if (AAsdaEffectActor* Fx = BirthFx.Get()) { Fx->Destroy(); }
	Super::EndPlay(Reason);
}
