// Copyright (c) 2026 @sitxovski (Discord)
#include "Resources/AsdaModelAnimSubsystem.h"
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"   // GEngine (Д9: единый доступ к менеджеру)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaModelAnim, Log, All);

// ── Д9: единый доступ к менеджеру + тонкие обёртки Load* (фундамент Д2) ──────────────────────────────
UAsdaModelAnimSubsystem* UAsdaModelAnimSubsystem::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<UAsdaModelAnimSubsystem>() : nullptr;
}

USkeletalMesh* UAsdaModelAnimSubsystem::LoadMeshChecked(const FString& Key)
{
	UAsdaModelAnimSubsystem* M = Get();
	return M ? M->LoadModel(Key) : nullptr;
}

UAnimSequence* UAsdaModelAnimSubsystem::LoadClipChecked(const FString& Key)
{
	UAsdaModelAnimSubsystem* M = Get();
	return M ? M->LoadAnimClip(Key) : nullptr;
}

// ── Д8: единый построитель пути скелеталки ──────────────────────────────────────────────────────────
FString UAsdaModelAnimSubsystem::BuildSkelPath(const FString& Root, const FString& Name, const FString& Suffix)
{
	return AsdaAssetPaths::SkelMesh(Root, Name, Suffix);   // ★ волна 2 п. 7: правило в реестре путей
}

FString UAsdaModelAnimSubsystem::BuildSkelAnimPath(const FString& Root, const FString& Name, int32 AnimIdx)
{
	return AsdaAssetPaths::SkelAnim(Root, Name, AnimIdx);   // ★ волна 2 п. 7
}

void UAsdaModelAnimSubsystem::Deinitialize()
{
	ClearCache();
	Super::Deinitialize();
}

void UAsdaModelAnimSubsystem::ClearCache()
{
	Cache.Empty();
}

UObject* UAsdaModelAnimSubsystem::LoadOrCache(const FString& Key, UClass* Class)
{
	if (Key.IsEmpty() || !Class)
	{
		return nullptr;
	}
	// Ключ кэша: РЕГИСТРОНЕЗАВИСИМО (эталон resMgrFindCachedResourceByName: strlwr+_stricmp) + КЛАСС в ключе
	//   (эталон различает .jbm/.jbb расширением; у нас класс задаётся аргументом → добавляем, чтобы меш и
	//   анимация по одному имени не столкнулись в общем кэше).
	const FString CacheKey = Key.ToLower() + TEXT("|") + Class->GetName();
	if (TObjectPtr<UObject>* Found = Cache.Find(CacheKey))
	{
		return Found->Get();   // дедуп по имени (аналог resMgrFindCachedResourceByName)
	}

	// Объектный путь UE. Если нет '.LeafName' — дописать (пакет "/A/B/N" → "/A/B/N.N"). ★ волна 2 п. 7: одна точка правила.
	const FString ObjPath = AsdaAssetPaths::ObjectPathFromPackage(Key);

	UObject* Res = StaticLoadObject(Class, nullptr, *ObjPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Res)   // не кэшируем промах (эталон: провал загрузки не создаёт слот → ретрай при появлении ассета)
	{
		Cache.Add(CacheKey, Res);
	}
	return Res;
}

UAnimSequence* UAsdaModelAnimSubsystem::LoadAnimClip(const FString& Key)
{
	return Cast<UAnimSequence>(LoadOrCache(Key, UAnimSequence::StaticClass()));
}

USkeletalMesh* UAsdaModelAnimSubsystem::LoadModel(const FString& Key)
{
	return Cast<USkeletalMesh>(LoadOrCache(Key, USkeletalMesh::StaticClass()));
}

int64 UAsdaModelAnimSubsystem::GetTotalCacheBytes() const
{
	int64 Total = 0;
	for (const TPair<FString, TObjectPtr<UObject>>& Kv : Cache)
	{
		if (Kv.Value)
		{
			Total += (int64)Kv.Value->GetResourceSizeBytes(EResourceSizeMode::EstimatedTotal);
		}
	}
	return Total;
}
