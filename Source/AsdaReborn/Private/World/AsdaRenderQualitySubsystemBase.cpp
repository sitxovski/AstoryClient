// Copyright (c) 2026 @sitxovski (Discord)
#include "World/AsdaRenderQualitySubsystemBase.h"
#include "Misc/ConfigCacheIni.h"              // GConfig / GScalabilityIni — читатель уровневых ключей (★ хвосты волны 3: перенесён из меню)
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Scalability.h"

// ★ Волна 3 п.12 (15.09.2026): тела — дословно прежние копии трёх подсистем (см. шапку класса)

void UAsdaRenderQualitySubsystemBase::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ScalabilityHandle = Scalability::OnScalabilitySettingsChanged.AddLambda([this](const Scalability::FQualityLevels&) { OnQualityLevelsChanged(); });
}

void UAsdaRenderQualitySubsystemBase::Deinitialize()
{
	UnbindQualityHooks();
	Super::Deinitialize();
}

void UAsdaRenderQualitySubsystemBase::UnbindQualityHooks()
{
	if (ScalabilityHandle.IsValid()) { Scalability::OnScalabilitySettingsChanged.Remove(ScalabilityHandle); ScalabilityHandle.Reset(); }
	if (ActorSpawnedHandle.IsValid())
	{
		if (UWorld* W = GetWorld()) { W->RemoveOnActorSpawnedHandler(ActorSpawnedHandle); }
		ActorSpawnedHandle.Reset();
	}
}

void UAsdaRenderQualitySubsystemBase::BindActorSpawned(UWorld& InWorld)
{
	if (!ActorSpawnedHandle.IsValid())
	{
		// Указатель на виртуальный метод базы — вызов уходит в переопределение наследника
		ActorSpawnedHandle = InWorld.AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UAsdaRenderQualitySubsystemBase::OnActorSpawned));
	}
}

int32 UAsdaRenderQualitySubsystemBase::ShadowQualityLevel()
{
	UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	return FMath::Clamp(S ? S->GetShadowQuality() : 3, 0, 4);   // ★ В2-C (хвост F17): зажим 0..4 — в базе
}

int32 UAsdaRenderQualitySubsystemBase::EffectsQualityLevel()
{
	UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	return FMath::Clamp(S ? S->GetVisualEffectQuality() : 3, 0, 4);   // ★ В2-C (хвост F17): зажим 0..4 — в базе (меню своего не держит)
}

// ★ Волна 3, хвосты (15.09.2026): тело дословно из бывшего UAsdaDevMenuSubsystem::ScalabilityLevelValue (E-0b 06.09 / E-4 07.09)
FString UAsdaRenderQualitySubsystemBase::ScalabilityLevelValue(const TCHAR* Group, int32 Level, const TCHAR* Key, const TCHAR* Def)
{
	const FString Section = (Level >= 4) ? FString::Printf(TEXT("%s@Cine"), Group) : FString::Printf(TEXT("%s@%d"), Group, FMath::Max(Level, 0));
	FString V;
	if (GConfig && GConfig->GetString(*Section, Key, V, GScalabilityIni) && !V.IsEmpty())
	{
		// В секции допустим хвост «; комментарий» (BaseScalability так пишет r.MaterialQualityLevel=1 ; High quality)
		FString Left, Right;
		if (V.Split(TEXT(";"), &Left, &Right)) { V = Left; }
		return V.TrimStartAndEnd();
	}
	return Def;
}

FString UAsdaRenderQualitySubsystemBase::LevelValue(const TCHAR* Group, int32 Level, const TCHAR* Key)
{
	return ScalabilityLevelValue(Group, Level, Key, TEXT(""));
}

int32 UAsdaRenderQualitySubsystemBase::LevelInt(const TCHAR* Group, int32 Level, const TCHAR* Key, int32 Fallback)
{
	const FString V = LevelValue(Group, Level, Key);
	return V.IsEmpty() ? Fallback : FCString::Atoi(*V);
}

float UAsdaRenderQualitySubsystemBase::LevelFloat(const TCHAR* Group, int32 Level, const TCHAR* Key, float Fallback)
{
	const FString V = LevelValue(Group, Level, Key);
	return V.IsEmpty() ? Fallback : FCString::Atof(*V);
}

float UAsdaRenderQualitySubsystemBase::LightDrawDistanceScale()
{
	static IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LightMaxDrawDistanceScale"));
	const float V = CV ? CV->GetFloat() : 1.f;
	return V > 0.f ? V : 1.f;
}
