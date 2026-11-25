// Copyright (c) 2026 @sitxovski (Discord)
#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManager.h"
#include "AsdaAssetManager.generated.h"

// ★★★ РАЗБИВКА КОНТЕНТА НА ЧАНКИ ДЛЯ ДЕЛЬТА-ОБНОВЛЕНИЙ (16.10.2026, Reports/updater/00_OPTIONS.md §3, волна 1).
//   ЗАЧЕМ. Без разбивки весь контент лежит в одном AsdaReborn-Windows.ucas (5,4 ГБ). Любая пересборка перетасовывает
//   его блоки, и лаунчер качал бы весь файл. С разбивкой правка карты или таблиц меняет только свой контейнер
//   pakchunk<N>-Windows.utoc/ucas, остальные остаются байт-в-байт (ключи pak статические — решение владельца 16.10).
//
//   КАК. Движок спрашивает у менеджера ассетов чанк каждого пакета (UAssetManager::GetPackageChunkIds, зовёт
//   FAssetRegistryGenerator при куке). Мы отвечаем ровно ОДНИМ чанком по префиксу пути пакета — без PrimaryAssetLabel-
//   ассетов, без зависимости от ссылок, без дублей пакета в нескольких контейнерах. Сгенерированные пакеты (ячейки
//   World Partition) движок сам относит к чанку своей карты.
//   Правила — Config/DefaultGame.ini секция [/Script/AsdaReborn.AsdaAssetManager]; побеждает самый длинный префикс.
//   Включение: DefaultEngine.ini [/Script/Engine.Engine] AssetManagerClassName + DefaultGame.ini bGenerateChunks=True.
//
//   НОМЕРА ЧАНКОВ МЕНЯТЬ НЕЛЬЗЯ: номер = имя файла у игрока (pakchunk<N>). Новая карта — новый номер (100 + id карты
//   из Tools/data/asda_map_registry.json), старые не трогать. Шифрование и подпись — те же, что у прежнего единого
//   контейнера (флаги Config/DefaultCrypto.ini действуют на каждый контейнер).
//
//   ПОЧЕМУ НЕ PrimaryAssetLabel: это новые .uasset в Content (лишние ассеты и правки через редактор), а назначение по
//   ссылкам при общих ассетах кладёт пакет в несколько чанков. Префикс пути предсказуем и проверяется глазами.

// Одно правило разбивки: все пакеты, путь которых начинается с PathPrefix, идут в контейнер ChunkId.
USTRUCT()
struct ASDAREBORN_API FAsdaChunkRule
{
	GENERATED_BODY()

	// Префикс пути пакета, например "/Game/Map/alpen/" (регистр не важен; завершающий "/" обязателен для папки)
	UPROPERTY(config)
	FString PathPrefix;

	// Номер чанка = номер контейнера pakchunk<N>-Windows; после выпуска не менять
	UPROPERTY(config)
	int32 ChunkId = 0;
};

// Менеджер ассетов проекта. Отличие от UAssetManager одно — назначение чанков по правилам ини (см. шапку).
UCLASS(config = Game)
class ASDAREBORN_API UAsdaAssetManager : public UAssetManager
{
	GENERATED_BODY()

public:
	// Чанк для пакетов, не попавших ни под одно правило (движок, плагины, общий контент, логин)
	UPROPERTY(config)
	int32 DefaultChunkId = 0;

	// Чанк для папки /Game/Map/<папка>/, которой нет в правилах (новая карта без номера); в лог кука — предупреждение
	UPROPERTY(config)
	int32 UnmappedMapChunkId = 99;

	// Правила «префикс пути → чанк»
	UPROPERTY(config)
	TArray<FAsdaChunkRule> ChunkRules;

#if WITH_EDITOR
	// Ровно один чанк на пакет по правилам; ExistingChunkList (стартовые пакеты и пр.) не учитывается намеренно.
	// Только редактор/кук: в движке метод под WITH_EDITOR (AssetManager.h), в игре чанки уже разложены по контейнерам.
	virtual bool GetPackageChunkIds(FName PackageName, const ITargetPlatform* TargetPlatform,
		TArrayView<const int32> ExistingChunkList, TArray<int32>& OutChunkList,
		TArray<int32>* OutOverrideChunkList = nullptr) const override;
#endif

	// Номер чанка для пути пакета по правилам (общий для кука и самопроверки)
	int32 ResolveChunkId(const FString& PackagePath) const;

private:
	// Папки карт без правила, о которых уже предупредили (чтобы не засорять лог кука на каждом пакете)
	mutable TSet<FString> WarnedMapFolders;

	// Защита WarnedMapFolders: генератор реестра может звать из разных потоков
	mutable FCriticalSection WarnedLock;
};
