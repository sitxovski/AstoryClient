// Copyright (c) 2026 @sitxovski (Discord)
#include "Resources/AsdaAssetManager.h"

// Лог разбивки на чанки (видно в логе кука)
DEFINE_LOG_CATEGORY_STATIC(LogAsdaChunks, Log, All);

namespace
{
	// Корень карт: пакеты /Game/Map/<папка>/… без правила идут в UnmappedMapChunkId
	const TCHAR* const GAsdaMapRoot = TEXT("/Game/Map/");
}

int32 UAsdaAssetManager::ResolveChunkId(const FString& PackagePath) const
{
	// Самый длинный подходящий префикс: позволяет вложенные правила (папка внутри папки)
	int32 BestLen = -1;
	int32 BestChunk = DefaultChunkId;
	for (const FAsdaChunkRule& Rule : ChunkRules)
	{
		if (!Rule.PathPrefix.IsEmpty() && Rule.PathPrefix.Len() > BestLen
			&& PackagePath.StartsWith(Rule.PathPrefix, ESearchCase::IgnoreCase))
		{
			BestLen = Rule.PathPrefix.Len();
			BestChunk = Rule.ChunkId;
		}
	}
	if (BestLen >= 0)
	{
		return BestChunk;
	}

	// Карта в своей папке, но без правила: отдельный «запасной» чанк, чтобы она не раздувала базовый
	const int32 RootLen = FCString::Strlen(GAsdaMapRoot);
	if (PackagePath.StartsWith(GAsdaMapRoot, ESearchCase::IgnoreCase))
	{
		const int32 Slash = PackagePath.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, RootLen);
		if (Slash != INDEX_NONE)
		{
			const FString Folder = PackagePath.Mid(RootLen, Slash - RootLen);
			FScopeLock Lock(&WarnedLock);
			if (!WarnedMapFolders.Contains(Folder))
			{
				WarnedMapFolders.Add(Folder);
				UE_LOG(LogAsdaChunks, Warning,
					TEXT("[ЧАНКИ] папка карты '%s' без правила в [/Script/AsdaReborn.AsdaAssetManager] -> чанк %d; добавь правило 100+id карты"),
					*Folder, UnmappedMapChunkId);
			}
			return UnmappedMapChunkId;
		}
	}
	return DefaultChunkId;
}

#if WITH_EDITOR
bool UAsdaAssetManager::GetPackageChunkIds(FName PackageName, const ITargetPlatform* TargetPlatform,
	TArrayView<const int32> ExistingChunkList, TArray<int32>& OutChunkList, TArray<int32>* OutOverrideChunkList) const
{
	const int32 ChunkId = ResolveChunkId(PackageName.ToString());
	OutChunkList.Reset();
	OutChunkList.Add(ChunkId);
	if (OutOverrideChunkList)
	{
		OutOverrideChunkList->AddUnique(ChunkId);
	}
	return true;
}
#endif
