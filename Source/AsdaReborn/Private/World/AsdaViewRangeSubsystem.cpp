// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaViewRangeSubsystem — разбор в шапке заголовка.
#include "World/AsdaViewRangeSubsystem.h"
#include "Npc/AsdaNpcGmOnly.h"   // ★ 09.11.2026: NPC «только для GM_»
#include "AsdaDataTableCache.h"
#include "AsdaDevConsole.h"
#include "AsdaPerf.h"   // ★ R4, доля a: область замера подгонки тумана
#include "AsdaEffectActor.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/ExponentialHeightFog.h"   // ★ R4, доля a: класс актора тумана (подписка на появление актора)
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Data/AsdaMobStatsRow.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Npc/AsdaMobActor.h"
#include "Npc/AsdaNpcActor.h"
#include "UI/AsdaSoulmateSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "World/AsdaCoord.h"
#include "World/AsdaCullDistanceSubsystem.h"
#include "World/AsdaFadeComponent.h"
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaPetActor.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaSoulGuardianActor.h"
#include "World/AsdaWorldKind.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaWorldSubsystemInternal.h"

// Рубильник: 1 — работает, 0 — откат: туман карты, все показаны, отсечка как до правки.
static ASDA_DEV_CVAR(int32, CVarEnable, TEXT("asda.ViewRange.Enable"), 1,
	TEXT("1 — дальность видимости от ползунка; 0 — откат."), ECVF_Default);
// Дальность видимости при ползунке 100 %, юниты; итог = это число × asda.Cull.Scale (0,5…2).
static ASDA_DEV_CVAR(float, CVarBaseUnits, TEXT("asda.ViewRange.BaseUnits"), 24000.f,
	TEXT("Дальность видимости при ползунке 100 %, юниты."), ECVF_Default);
// Во сколько раз игроки видны дальше мобов и NPC.
static ASDA_DEV_CVAR(float, CVarPlayerMul, TEXT("asda.ViewRange.PlayerMul"), 1.5f,
	TEXT("Мобы и NPC гаснут на дальности / это число, игроки на самой дальности."), ECVF_Default);
// Запас на скрытие, тайлы: разные пороги показа и скрытия убирают мигание.
static ASDA_DEV_CVAR(int32, CVarHysteresis, TEXT("asda.ViewRange.Hysteresis"), 10,
	TEXT("Запас на скрытие, тайлы."), ECVF_Default);
// Сколько мобов начинают фейд за шаг: смена ползунка не бьёт по кадру.
static ASDA_DEV_CVAR(int32, CVarFadePerStep, TEXT("asda.ViewRange.FadePerStep"), 6,
	TEXT("Сколько мобов начинают фейд за шаг 0,1 с."), ECVF_Default);
// Боссы не подчиняются радиусу.
static ASDA_DEV_CVAR(int32, CVarKeepBoss, TEXT("asda.ViewRange.KeepBoss"), 1,
	TEXT("1 — босс (ранг таблицы 3) не скрывается."), ECVF_Default);
// Непрозрачность тумана на самой дальности.
static ASDA_DEV_CVAR(float, CVarOpacity, TEXT("asda.ViewRange.FogOpacity"), 0.99f,
	TEXT("Непрозрачность тумана на дальности видимости."), ECVF_Default);
// Самое дальнее начало тумана как доля дальности: перед полным туманом нужен плавный подъём.
static ASDA_DEV_CVAR(float, CVarStartFrac, TEXT("asda.ViewRange.FogStartFrac"), 0.45f,
	TEXT("Начало тумана не дальше этой доли дальности."), ECVF_Default);

// Растворение дальних силуэтов в небе: 1 — купол растворяет, 0 — откат.
static ASDA_DEV_CVAR(int32, CVarSkyFade, TEXT("asda.ViewRange.SkyFade"), 1,
	TEXT("1 — купол растворяет дальние силуэты, 0 — откат."), ECVF_Default);
// Начало растворения как доля дальности: туман здесь уже почти полный.
static ASDA_DEV_CVAR(float, CVarSkyFadeStart, TEXT("asda.ViewRange.SkyFadeStart"), 0.8f,
	TEXT("Начало растворения в небе, доля дальности."), ECVF_Default);
// Конец растворения как доля дальности.
static ASDA_DEV_CVAR(float, CVarSkyFadeEnd, TEXT("asda.ViewRange.SkyFadeEnd"), 1.f,
	TEXT("Конец растворения в небе, доля дальности."), ECVF_Default);

namespace
{
	// Значения MPC_AsdaViewRange, при которых купол ничего не растворяет: дальше любой геометрии карты.
	constexpr float kSkyFadeOffStart = 1.0e9f;
	constexpr float kSkyFadeOffEnd = 1.001e9f;

	// Значение ползунка: ключ живёт в Shipping.
	float SliderScale()
	{
		static IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Cull.Scale"));
		return Var ? FMath::Clamp(Var->GetFloat(), 0.5f, 2.f) : 1.f;
	}

	// Клетка актора.
	FIntPoint TileOf(const AActor* A)
	{
		return AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(A->GetActorLocation()));
	}

	// Расстояние Чебышёва, тайлы.
	int32 TileDist(const FIntPoint& A, const FIntPoint& B)
	{
		return FMath::Max(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y));
	}

	// Подсистема мира игры.
	UAsdaWorldSubsystem* WorldOf(const UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	}
}

bool UAsdaViewRangeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* W = Cast<UWorld>(Outer);
	return W && W->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

UAsdaViewRangeSubsystem* UAsdaViewRangeSubsystem::Get(const UWorld* W)
{
	return W ? W->GetSubsystem<UAsdaViewRangeSubsystem>() : nullptr;
}

bool UAsdaViewRangeSubsystem::IsOn()
{
	return CVarEnable.GetValueOnGameThread() != 0;
}

bool UAsdaViewRangeSubsystem::IsSkyFadeOn()
{
	return CVarSkyFade.GetValueOnGameThread() != 0;
}

void UAsdaViewRangeSubsystem::DebugSkyFade(float& OutStart, float& OutEnd) const
{
	OutStart = LastFadeStart;
	OutEnd = LastFadeEnd;
}

float UAsdaViewRangeSubsystem::RangeUnits(const UWorld* W)
{
	if (!IsOn() || !W || !W->IsGameWorld() || AsdaWorldKind::IsLoginScene(W) || AsdaWorldKind::IsLoadingMap(W)) { return 0.f; }
	return FMath::Max(1000.f, CVarBaseUnits.GetValueOnGameThread()) * SliderScale();
}

int32 UAsdaViewRangeSubsystem::PlayerTiles() const
{
	return FMath::RoundToInt(RangeUnits(GetWorld()) / AsdaCoord::kTileUnits);
}

int32 UAsdaViewRangeSubsystem::MobTiles() const
{
	return FMath::RoundToInt(PlayerTiles() / FMath::Max(1.f, CVarPlayerMul.GetValueOnGameThread()));
}

void UAsdaViewRangeSubsystem::DebugSetClan(int32 UserIdx, int32 ClanId)
{
	if (UAsdaWorldSubsystem* WS = WorldOf(GetWorld()))
	{
		WS->SelfClanId = ClanId;
		if (UAsdaWorldSubsystem::FAsdaNetPlayerEntry* E = WS->NetPlayers.Find(UserIdx)) { E->ClanId = ClanId; }
	}
}

void UAsdaViewRangeSubsystem::DebugAddPet(int32 CharId, AAsdaPetActor* Pet)
{
	if (UAsdaWorldSubsystem* WS = WorldOf(GetWorld())) { WS->PetActors.Add(CharId, Pet); }
}

// ★ R4, доля a (14.11.2026) [ПОРТ, производительность]: компоненты тумана мира — списком, а не обходом всех объектов.
//   Прежний `ApplyFog` 10 раз в секунду шёл `TObjectIterator` по всему массиву объектов процесса (десятки–сотни тысяч) ради
//   одного–трёх компонентов тумана. Теперь список пересобирается тем же обходом только когда набор мог измениться: первый шаг,
//   появление актора тумана (`AExponentialHeightFog`), добавление уровня в мир, и страховочно раз в 20 шагов (≈ 2 с — на случай
//   компонента тумана у актора другого класса или добавленного к живому актору; в коде порта таких мест нет). Подгонка каждого компонента — прежнее тело (`ApplyFogOne`).
//   Запись 11 (15.11.2026): прежний обход всех объектов и ключ убраны.

void UAsdaViewRangeSubsystem::RefreshFogList(bool bCountAge)
{
	UWorld* W = GetWorld();
	if (!bFogHooks && W)
	{
		bFogHooks = true;
		FogSpawnHandle = W->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateWeakLambda(this, [this](AActor* A)
		{
			if (A && A->IsA<AExponentialHeightFog>()) { bFogListDirty = true; }   // только проверка класса: обработчик зовётся на каждый появившийся актор
		}));
		FogLevelHandle = FWorldDelegates::LevelAddedToWorld.AddWeakLambda(this, [this](ULevel*, UWorld* InWorld)
		{
			if (InWorld == GetWorld()) { bFogListDirty = true; }
		});
	}
	if (!bFogListDirty && (!bCountAge || ++FogListAge < 20)) { return; }
	bFogListDirty = false;
	FogListAge = 0;
	FogList.Reset();
	for (TObjectIterator<UExponentialHeightFogComponent> It; It; ++It)
	{
		UExponentialHeightFogComponent* C = *It;
		if (!C || C->IsTemplate() || C->GetWorld() != W) { continue; }
		FogList.Add(C);
	}
}

void UAsdaViewRangeSubsystem::Deinitialize()
{
	if (bFogHooks)
	{
		if (UWorld* W = GetWorld()) { W->RemoveOnActorSpawnedHandler(FogSpawnHandle); }
		FWorldDelegates::LevelAddedToWorld.Remove(FogLevelHandle);
		bFogHooks = false;
	}
	Super::Deinitialize();
}

void UAsdaViewRangeSubsystem::ApplyFog(float Range)
{
	ASDA_PERF_SCOPE("ViewRange.ApplyFog");   // R4, доля a: вся подгонка тумана за один шаг (в Shipping пусто)
	const UWorld* W = GetWorld();
	const float Opacity = FMath::Clamp(CVarOpacity.GetValueOnGameThread(), 0.5f, 0.999f);
	const float StartFrac = FMath::Clamp(CVarStartFrac.GetValueOnGameThread(), 0.05f, 0.9f);
	// Тот же отбор и та же подгонка, что у прежнего обхода всех объектов; компоненты — из списка. Уничтоженный, но ещё не
	//   собранный сборщиком компонент обход всех объектов тоже отдавал — берём и его (`Get(true)`).
	RefreshFogList();
	for (const TWeakObjectPtr<UExponentialHeightFogComponent>& P : FogList)
	{
		UExponentialHeightFogComponent* C = P.Get(/*bEvenIfGarbage=*/true);
		if (!C || C->IsTemplate() || C->GetWorld() != W) { continue; }
		ApplyFogOne(C, Range, Opacity, StartFrac);
	}
}

void UAsdaViewRangeSubsystem::ApplyFogOne(UExponentialHeightFogComponent* C, float Range, float Opacity, float StartFrac)
{
	{
		FFogBase* B = Fog.Find(C);
		if (!B)
		{
			B = &Fog.Add(C);
			B->Density = C->FogDensity; B->Start = C->StartDistance; B->MaxOpacity = C->FogMaxOpacity;
		}
		// Значение не наше — настройки тумана наката́ли новое: это новая база.
		if (B->WroteDensity >= 0.f && !FMath::IsNearlyEqual(C->FogDensity, B->WroteDensity)) { B->Density = C->FogDensity; }
		if (B->WroteStart >= 0.f && !FMath::IsNearlyEqual(C->StartDistance, B->WroteStart)) { B->Start = C->StartDistance; }
		float WantStart = B->Start, WantDensity = B->Density, WantMax = B->MaxOpacity;
		if (Range > 0.f)
		{
			// Непрозрачность = 1 − e^(−(ln2)²·density/1000·(L − Start)), разбор — UAsdaCullDistanceSubsystem::FogOpacityAt.
			WantStart = FMath::Min(B->Start, Range * StartFrac);
			const float Need = FMath::Loge(1.f / (1.f - Opacity)) * 1000.f / (FMath::Square(UE_LN2) * (Range - WantStart));
			WantDensity = FMath::Max(B->Density, Need);
			WantMax = 1.f;
		}
		if (!FMath::IsNearlyEqual(C->StartDistance, WantStart)) { C->SetStartDistance(WantStart); }
		if (!FMath::IsNearlyEqual(C->FogDensity, WantDensity)) { C->SetFogDensity(WantDensity); }
		if (!FMath::IsNearlyEqual(C->FogMaxOpacity, WantMax)) { C->SetFogMaxOpacity(WantMax); }
		B->WroteDensity = Range > 0.f ? C->FogDensity : -1.f;
		B->WroteStart = Range > 0.f ? C->StartDistance : -1.f;
	}
}

void UAsdaViewRangeSubsystem::ApplySkyFade(float Range)
{
	const bool bFade = Range > 0.f && IsSkyFadeOn();
	const float Start = bFade ? Range * FMath::Clamp(CVarSkyFadeStart.GetValueOnGameThread(), 0.3f, 0.98f) : kSkyFadeOffStart;
	const float End = bFade ? FMath::Max(Range * CVarSkyFadeEnd.GetValueOnGameThread(), Start + 100.f) : kSkyFadeOffEnd;
	if (FMath::IsNearlyEqual(Start, LastFadeStart) && FMath::IsNearlyEqual(End, LastFadeEnd)) { return; }
	if (!FadeMpc) { FadeMpc = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/Materials/MPC_AsdaViewRange.MPC_AsdaViewRange")); }
	UWorld* W = GetWorld();
	UMaterialParameterCollectionInstance* Inst = (FadeMpc && W) ? W->GetParameterCollectionInstance(FadeMpc) : nullptr;
	if (!Inst) { return; }
	Inst->SetScalarParameterValue(TEXT("FadeStart"), Start);
	Inst->SetScalarParameterValue(TEXT("FadeEnd"), End);
	LastFadeStart = Start;
	LastFadeEnd = End;
	// Купол растворяет только силуэты на фоне неба: рельеф и вода отмечаются в CustomDepth, там купол не рисуется.
	for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
	{
		UStaticMeshComponent* C = *It;
		const UStaticMesh* M = (C && !C->IsTemplate() && C->GetWorld() == W) ? C->GetStaticMesh() : nullptr;
		if (!M) { continue; }
		const FString N = M->GetName();
		if (N.StartsWith(TEXT("SM_terrain"), ESearchCase::IgnoreCase) || N.StartsWith(TEXT("SM_water"), ESearchCase::IgnoreCase)) { C->SetRenderCustomDepth(bFade); }
	}
}

bool UAsdaViewRangeSubsystem::IsMobProtected(const AAsdaMobActor* M) const
{
	if (M->IsCombatStance() || (CVarKeepBoss.GetValueOnGameThread() != 0 && M->MobAggression == 3)) { return true; }   // поле зовётся агрессией, смысл — ранг, 3 = Boss
	const UWorld* W = GetWorld();
	const UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	const AAsdaPlayerController* PC = GI ? Cast<AAsdaPlayerController>(GI->GetFirstLocalPlayerController(W)) : nullptr;
	return PC && PC->TargetActor.Get() == M;
}

void UAsdaViewRangeSubsystem::ShowMob(AAsdaMobActor* M, EMobHide State)
{
	if (M->Mesh) { M->Mesh->SetComponentTickEnabled(true); }
	if (M->Nameplate) { M->Nameplate->SetComponentTickEnabled(true); }
	if (State != EMobHide::Fading) { M->SetActorHiddenInGame(false); }
	AsdaSetAttachedFxHidden(M, false);
	UAsdaFadeComponent::Begin(M, /*bIn=*/true, AAsdaMobActor::FadeInSeconds, /*bDestroyOwner=*/false);
}

void UAsdaViewRangeSubsystem::StepMobs(const FIntPoint& SelfTile, bool bOn)
{
	UAsdaWorldSubsystem* WS = WorldOf(GetWorld());
	if (!WS) { return; }
	const int32 In = MobTiles();
	const int32 Out = In + CVarHysteresis.GetValueOnGameThread();
	int32 Left = FMath::Max(1, CVarFadePerStep.GetValueOnGameThread());
	for (auto It = Mobs.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
	for (auto It = KnownMobs.CreateIterator(); It; ++It) { if (!It->IsValid()) { It.RemoveCurrent(); } }
	for (const TPair<int32, TWeakObjectPtr<AAsdaMobActor>>& It : WS->NetMobs)
	{
		AAsdaMobActor* M = It.Value.Get();
		if (!M || M->IsDead() || M->IsFadingOut()) { continue; }   // труп и уход из обзора ведёт штатный цикл
		const int32 D = TileDist(TileOf(M), SelfTile);
		const bool bKeep = !bOn || IsMobProtected(M);
		bool bKnown = false;
		KnownMobs.Add(M, &bKnown);
		if (EMobHide* St = Mobs.Find(M))
		{
			if (bKeep || D <= In)
			{
				ShowMob(M, *St);
				Mobs.Remove(M);
			}
			else if (*St == EMobHide::Fading && M->IsHidden())
			{
				*St = EMobHide::Hidden;
				AsdaSetAttachedFxHidden(M, true);
			}
			else if (*St == EMobHide::Hidden)
			{
				// Плашка уже прочла скрытие владельца и убрала виджет: тики меша и плашки больше не нужны.
				if (M->Mesh) { M->Mesh->SetComponentTickEnabled(false); }
				if (M->Nameplate) { M->Nameplate->SetComponentTickEnabled(false); }
				*St = EMobHide::Dormant;
			}
		}
		else if (!bKeep && D > Out && !M->IsHidden())
		{
			if (!bKnown)
			{
				M->SetActorHiddenInGame(true);   // новый моб вдали: в тумане, фейд не нужен
				AsdaSetAttachedFxHidden(M, true);
				Mobs.Add(M, EMobHide::Hidden);
			}
			else if (Left > 0)
			{
				--Left;
				UAsdaFadeComponent::Begin(M, /*bIn=*/false, AAsdaMobActor::AoiFadeSeconds, /*bDestroyOwner=*/false);
				Mobs.Add(M, EMobHide::Fading);
			}
		}
	}
}

void UAsdaViewRangeSubsystem::StepPlayers(const FIntPoint& SelfTile, bool bOn)
{
	UAsdaWorldSubsystem* WS = WorldOf(GetWorld());
	if (!WS) { return; }
	const UWorld* W = GetWorld();
	UGameInstance* GI = W->GetGameInstance();
	const AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GI->GetFirstLocalPlayerController(W));
	const UAsdaSoulmateSubsystem* Soul = GI->GetSubsystem<UAsdaSoulmateSubsystem>();
	const int32 In = PlayerTiles();
	const int32 Out = In + CVarHysteresis.GetValueOnGameThread();
	for (auto It = Players.CreateIterator(); It; ++It) { if (!WS->NetPlayers.Contains(*It)) { It.RemoveCurrent(); } }
	// Скрыть или показать соседа с тем, что скрытие актора не достаёт: эффекты и хранитель. Питомец следует за владельцем сам.
	const auto SetHidden = [WS](AAsdaPlayerCharacter* A, bool bHidden)
	{
		AsdaSetAttachedFxHidden(A, bHidden);
		if (AAsdaSoulGuardianActor* G = WS->FindSoulGuardianByOwner(A))
		{
			G->SetActorHiddenInGame(bHidden);
			AsdaSetAttachedFxHidden(G, bHidden);
		}
		if (bHidden) { UAsdaFadeComponent::Begin(A, /*bIn=*/false, AAsdaMobActor::AoiFadeSeconds, /*bDestroyOwner=*/false); }
		else
		{
			A->SetActorHiddenInGame(false);
			UAsdaFadeComponent::Begin(A, /*bIn=*/true, AAsdaMobActor::FadeInSeconds, /*bDestroyOwner=*/false);
		}
	};
	for (const TPair<int32, UAsdaWorldSubsystem::FAsdaNetPlayerEntry>& It : WS->NetPlayers)
	{
		AAsdaPlayerCharacter* A = It.Value.Actor.Get();
		if (!A) { continue; }
		// GM-скрытие и невидимость навыка главнее: дальность их не проявляет.
		const bool bOtherHide = WS->GmHiddenUserIdx.Contains(It.Key) || !A->IsStealthRendered();
		const bool bSoul = Soul && (Soul->GetMateUserIdx() == It.Key || (!It.Value.Name.IsEmpty() && Soul->GetPartnerName() == It.Value.Name));
		const bool bClan = It.Value.ClanId >= 0 && WS->SelfClanId == It.Value.ClanId;
		const bool bKeep = !bOn || bOtherHide || bSoul || bClan || WS->PartyUserIdxs.Contains(It.Key) || (PC && PC->TargetActor.Get() == A);
		const int32 D = TileDist(TileOf(A), SelfTile);
		if (Players.Contains(It.Key))
		{
			if (bKeep || D <= In)
			{
				Players.Remove(It.Key);
				if (!bOtherHide) { SetHidden(A, false); }
			}
		}
		else if (!bKeep && D > Out && !A->IsHidden())
		{
			Players.Add(It.Key);
			SetHidden(A, true);
		}
	}
}

void UAsdaViewRangeSubsystem::StepNpcs(const FIntPoint& SelfTile, bool bOn)
{
	UAsdaWorldSubsystem* WS = WorldOf(GetWorld());
	if (!WS) { return; }
	const int32 In = MobTiles();
	const int32 Out = In + CVarHysteresis.GetValueOnGameThread();
	for (const TPair<int32, TWeakObjectPtr<AAsdaNpcActor>>& It : WS->NetNpcs)
	{
		AAsdaNpcActor* N = It.Value.Get();
		if (!N) { Npcs.Remove(It.Key); continue; }
		if (AsdaNpcGmOnly::HiddenForSelf(GetWorld() ? GetWorld()->GetGameInstance() : nullptr, N->NpcId)) { Npcs.Remove(It.Key); continue; }   // ★ 09.11.2026: NPC «только для GM_» не показываем
		const bool bQuest = N->QuestMark && N->QuestMark->IsVisible();   // NPC с квестовой меткой виден всегда
		const int32 D = TileDist(TileOf(N), SelfTile);
		if (Npcs.Contains(It.Key))
		{
			if (!bOn || bQuest || D <= In)
			{
				Npcs.Remove(It.Key);
				N->SetActorHiddenInGame(false);
				UAsdaFadeComponent::Begin(N, /*bIn=*/true, AAsdaMobActor::FadeInSeconds, /*bDestroyOwner=*/false);
			}
		}
		else if (bOn && !bQuest && D > Out && !N->IsHidden())
		{
			Npcs.Add(It.Key);
			UAsdaFadeComponent::Begin(N, /*bIn=*/false, AAsdaMobActor::AoiFadeSeconds, /*bDestroyOwner=*/false);
		}
	}
}

void UAsdaViewRangeSubsystem::StepFx(const FVector& SelfPos, float Range)
{
	// Аддитивный эффект светится сквозь туман, поэтому эффекты карты за дальностью скрываются. Навешанные на персонажей ведут их хозяева.
	for (auto It = Fx.CreateIterator(); It; ++It)
	{
		AAsdaEffectActor* E = It->Get();
		if (!E) { It.RemoveCurrent(); continue; }
		if (Range <= 0.f || FVector::Dist2D(E->GetActorLocation(), SelfPos) <= Range)
		{
			E->SetActorHiddenInGame(false);
			It.RemoveCurrent();
		}
	}
	if (Range <= 0.f) { return; }
	const float Out = Range + CVarHysteresis.GetValueOnGameThread() * AsdaCoord::kTileUnits;
	for (TActorIterator<AAsdaEffectActor> It(GetWorld()); It; ++It)
	{
		AAsdaEffectActor* E = *It;
		if (E->GetAttachParentActor() || E->IsHidden() || FVector::Dist2D(E->GetActorLocation(), SelfPos) <= Out) { continue; }
		E->SetActorHiddenInGame(true);
		Fx.Add(E);
	}
}

void UAsdaViewRangeSubsystem::Tick(float DeltaTime)
{
	Accum += DeltaTime;
	if (Accum < 0.1f) { return; }
	Accum = 0.f;
	UWorld* W = GetWorld();
	const float Range = RangeUnits(W);
	const bool bOn = Range > 0.f;
	if (!bOn && LastRange <= 0.f && Fog.Num() == 0) { return; }   // логин и загрузка: ничего не касаемся
	ApplyFog(Range);
	ApplySkyFade(Range);
	if (!FMath::IsNearlyEqual(Range, LastRange))
	{
		LastRange = Range;
		UAsdaCullDistanceSubsystem::MarkAllDirty();
	}
	const UAsdaWorldSubsystem* WS = WorldOf(W);
	const AActor* Self = WS ? WS->LocalPlayerActor.Get() : nullptr;
	if (!Self) { return; }
	const FIntPoint SelfTile = TileOf(Self);
	StepMobs(SelfTile, bOn);
	if (++Steps < 10) { return; }
	Steps = 0;
	StepPlayers(SelfTile, bOn);
	StepNpcs(SelfTile, bOn);
	StepFx(Self->GetActorLocation(), Range);
}

#if ASDA_DEV_CONSOLE
// `asda.ViewRange.Dump` — дальность, туман и скрытые в лог.
static FAutoConsoleCommandWithWorld GAsdaViewRangeDump(
	TEXT("asda.ViewRange.Dump"),
	TEXT("Дальность видимости, туман мира, сколько скрыто."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		const UAsdaViewRangeSubsystem* S = UAsdaViewRangeSubsystem::Get(W);
		if (!S) { return; }
		UE_LOG(LogAsdaWorld, Log, TEXT("[дальность] %.0f юн; мобы и NPC %d тайлов, игроки %d; скрыто мобов %d, игроков %d, NPC %d, эффектов %d"),
			UAsdaViewRangeSubsystem::RangeUnits(W), S->MobTiles(), S->PlayerTiles(), S->HiddenMobCount(), S->HiddenPlayerCount(), S->HiddenNpcCount(), S->HiddenFxCount());
		for (TObjectIterator<UExponentialHeightFogComponent> It; It; ++It)
		{
			if (It->IsTemplate() || It->GetWorld() != W) { continue; }
			UE_LOG(LogAsdaWorld, Log, TEXT("[дальность] туман %s: начало %.0f, плотность %.5f, потолок %.2f, спад %.6f, объёмный %d"),
				*It->GetPathName(), It->StartDistance, It->FogDensity, It->FogMaxOpacity, It->FogHeightFalloff, It->bEnableVolumetricFog ? 1 : 0);
		}
	}));
#endif
