// Copyright (c) 2026 @sitxovski (Discord)
// Реализация проигрывателя анимации login-задника. См. комментарий-шапку в AsdaBackdropAnimator.h.
#include "Login/AsdaBackdropAnimator.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Login/AsdaBackdropAnimData.h"
#include "Components/SceneComponent.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"

// ★ КЛЮЧ ОТКАТА: `asda.LoginBackdropAnim 0` — мгновенно возвращает статичный задник (авторские
//   трансформы элементов). Работает и в упакованной игре, состояние проверяется каждый тик.
static ASDA_DEV_CVAR(int32, CVarLoginBackdropAnim,
	TEXT("asda.LoginBackdropAnim"),
	1,
	TEXT("Анимация растительности на экране входа: 1 = играть (оригинал), 0 = статика (откат)."),
	ECVF_Default);

AAsdaBackdropAnimator::AAsdaBackdropAnimator()
{
	PrimaryActorTick.bCanEverTick = true;
	// Анимация — чистая работа с трансформами сцены, ставим её ПОСЛЕ обычной игровой логики,
	// чтобы перелёт (`LS_LoginFlight` двигает родителя `LoginBackdrop_Flight`) успел отработать.
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	// В редакторе НЕ тикаем принципиально: дирижёр меняет трансформы акторов, а изменение их в
	// редакторе пометило бы уровень грязным и могло затереть ручные правки дизайнера при сохранении.
	PrimaryActorTick.bStartWithTickEnabled = true;
	SetActorTickEnabled(true);

	// Пустой корень: дирижёр ничего не рисует, ему нужна только точка в уровне.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AAsdaBackdropAnimator::BeginPlay()
{
	Super::BeginPlay();
	PlayTime = StartTime;
	BindElements();
}

void AAsdaBackdropAnimator::BindElements()
{
	Bound.Reset();
	if (!AnimData)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AsdaBackdrop] Не задан AnimData — задник останется статичным."));
		return;
	}

	// Имя трека → его индекс (акторов ищем по тегам, а тег может быть любым из списка).
	TMap<FName, int32> TrackByName;
	TrackByName.Reserve(AnimData->Tracks.Num());
	for (int32 i = 0; i < AnimData->Tracks.Num(); ++i)
	{
		TrackByName.Add(AnimData->Tracks[i].Element, i);
	}

	TArray<AActor*> Elements;
	UGameplayStatics::GetAllActorsWithTag(this, ElementTag, Elements);
	if (Elements.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
		       TEXT("[AsdaBackdrop] В уровне нет акторов с тегом '%s' — прогоните Tools/asda_build_backdrop_anim.py."),
		       *ElementTag.ToString());
		return;
	}

	int32 NumStaticMobility = 0;
	for (AActor* Actor : Elements)
	{
		if (!IsValid(Actor))
		{
			continue;
		}
		// Второй тег актора = имя элемента; ищем тот, для которого есть трек.
		const int32* FoundIndex = nullptr;
		FName ElementName = NAME_None;
		for (const FName& Tag : Actor->Tags)
		{
			if (Tag == ElementTag)
			{
				continue;
			}
			if (const int32* Idx = TrackByName.Find(Tag))
			{
				FoundIndex = Idx;
				ElementName = Tag;
				break;
			}
		}
		if (!FoundIndex || ExcludedElements.Contains(ElementName))
		{
			continue;   // элемент без трека (неподвижен в оригинале) либо исключён дизайнером
		}

		USceneComponent* Root = Actor->GetRootComponent();
		if (!Root)
		{
			continue;
		}
		// Статичный компонент нельзя двигать в рантайме. Билдер выставляет Movable; здесь — страховка
		// на случай, если актор добавили руками (иначе UE молча проигнорировал бы наши трансформы).
		if (Root->Mobility != EComponentMobility::Movable)
		{
			Root->SetMobility(EComponentMobility::Movable);
			++NumStaticMobility;
		}

		FBoundElement Entry;
		Entry.Actor = Actor;
		Entry.TrackIndex = *FoundIndex;
		Entry.Base = Root->GetRelativeTransform();   // АВТОРСКИЙ трансформ = база, дельта идёт поверх
		Bound.Add(MoveTemp(Entry));
	}

	UE_LOG(LogTemp, Log,
	       TEXT("[AsdaBackdrop] Привязано элементов: %d из %d помеченных (треков в данных: %d, ")
	       TEXT("неподвижных по оригиналу: %d, без кости: %d). Переведено в Movable на лету: %d."),
	       Bound.Num(), Elements.Num(), AnimData->Tracks.Num(),
	       AnimData->StaticElements.Num(), AnimData->UnboundElements.Num(), NumStaticMobility);
}

void AAsdaBackdropAnimator::RestoreBaseTransforms()
{
	for (const FBoundElement& Entry : Bound)
	{
		if (AActor* Actor = Entry.Actor.Get())
		{
			if (USceneComponent* Root = Actor->GetRootComponent())
			{
				Root->SetRelativeTransform(Entry.Base, false, nullptr, ETeleportType::TeleportPhysics);
			}
		}
	}
	bDeltaApplied = false;
}

void AAsdaBackdropAnimator::SetAnimationEnabled(bool bInEnabled)
{
	bEnabled = bInEnabled;
	if (!bEnabled)
	{
		RestoreBaseTransforms();
	}
}

void AAsdaBackdropAnimator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const bool bActive = bEnabled && (CVarLoginBackdropAnim.GetValueOnGameThread() != 0)
	                     && AnimData && Bound.Num() > 0 && AnimData->Duration > KINDA_SMALL_NUMBER;
	if (!bActive)
	{
		if (bDeltaApplied)
		{
			RestoreBaseTransforms();   // откат к статике ровно один раз
		}
		return;
	}

	PlayTime += DeltaSeconds * PlayRate;
	if (PlayTime >= AnimData->Duration)
	{
		// Оригинал: `Game_IsPausedOrMenu` (time >= duration) → `CObject_SetField13(...,0)` — время
		// клипа сбрасывается в ноль, то есть чистый цикл без склейки (ключ 0 == последний ключ).
		PlayTime = bLoop ? FMath::Fmod(PlayTime, AnimData->Duration) : AnimData->Duration;
	}

	const float ClampedAmplitude = FMath::Max(Amplitude, 0.f);
	for (const FBoundElement& Entry : Bound)
	{
		AActor* Actor = Entry.Actor.Get();
		if (!Actor)
		{
			continue;
		}
		USceneComponent* Root = Actor->GetRootComponent();
		if (!Root)
		{
			continue;
		}
		FTransform Delta;
		if (!AnimData->SampleTrack(Entry.TrackIndex, PlayTime, Delta))
		{
			continue;
		}
		if (!FMath::IsNearlyEqual(ClampedAmplitude, 1.f))
		{
			// Смешение с тождественным трансформом = ослабление/усиление качания (не оригинал,
			// дефолт Amplitude=1.0 этой ветки не касается).
			Delta.BlendWith(FTransform::Identity, 1.f - FMath::Min(ClampedAmplitude, 1.f));
			if (ClampedAmplitude > 1.f)
			{
				Delta.SetTranslation(Delta.GetTranslation() * ClampedAmplitude);
			}
		}
		// Композиция: сначала локальная дельта, затем авторская расстановка элемента.
		// (В UE `A * B` = «применить A, потом B», B — родительский.)
		Root->SetRelativeTransform(Delta * Entry.Base, false, nullptr, ETeleportType::TeleportPhysics);
	}
	bDeltaApplied = true;
}
