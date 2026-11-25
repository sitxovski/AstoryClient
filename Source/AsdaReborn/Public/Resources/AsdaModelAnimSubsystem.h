// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaModelAnimSubsystem — единый менеджер МОДЕЛЕЙ и АНИМАЦИЙ (по образу оригинала CResMgr, resource_graph.c).
//   Оригинал (ETALON_RESOURCE_MANAGERS.md §3): ОДИН общий кэш мешей и анимаций — CResMgr_LoadOrCacheItem@0x119BA10
//   (find-by-name → load+cache), .jbb=скелет-клип, .jbm=меш; клиенту отдаётся дескриптор, ресурсом владеет кэш.
//   Порт: скелетная оркестрация была размазана по AsdaLoginGameMode (LoadObject<UAnimSequence/USkeletalMesh>
//   на каждое переключение, без кэша). Эта подсистема — ЕДИНЫЙ вход «дай меш/клип по имени», с кэшем+GC-рутом.
//   UEngineSubsystem — как текстуры: ресурсы engine-lifetime, нужны и в редакторных тулзах.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "AsdaModelAnimSubsystem.generated.h"

class USkeletalMesh;
class UAnimSequence;

UCLASS()
class ASDAREBORN_API UAsdaModelAnimSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	// Скелет-клип по объектному пути/ключу (аналог .jbb через CResMgr). Кэш+рут. nullptr если нет.
	UFUNCTION(BlueprintCallable, Category="AsdaModelAnim")
	UAnimSequence* LoadAnimClip(const FString& Key);

	// Скелет-меш по объектному пути/ключу (аналог .jbm). Кэш+рут. nullptr если нет.
	UFUNCTION(BlueprintCallable, Category="AsdaModelAnim")
	USkeletalMesh* LoadModel(const FString& Key);

	// Полная очистка кэша (CResMgr_ClearCache).
	UFUNCTION(BlueprintCallable, Category="AsdaModelAnim")
	void ClearCache();

	// ── Д9: единый доступ к менеджеру (engine-lifetime) для ВСЕХ потребителей ────────────────────────
	// Возвращает подсистему или nullptr (нет движка). Прежде каждый файл поднимал свой GetAnimMgr/обёртку.
	static UAsdaModelAnimSubsystem* Get();
	// Загрузка меша/клипа через менеджер одной строкой (кэш+GC-рут). nullptr, если подсистемы/ассета нет.
	//   Через них проходят все акторы (Д2) — «ресурс только через менеджер» ([[resource-subsystems]]).
	static USkeletalMesh* LoadMeshChecked(const FString& Key);
	static UAnimSequence*  LoadClipChecked(const FString& Key);

	// ── Д8: единый построитель пути скелеталки <Root>/<Name>/SkeletalMeshes/<Name><Suffix> ───────────
	// Suffix дописывается к листовому <Name> БЕЗ разделителя (как в оригинале: "<name>anim_00").
	//   Корень (Pet/Vehicle/SoulGuard/MobSkel/…) — параметр. Объектный суффикс «.<leaf>» дописывает сам
	//   менеджер при загрузке, поэтому здесь его не пишем. НЕ для EquipParts (там нет /SkeletalMeshes/).
	static FString BuildSkelPath(const FString& Root, const FString& Name, const FString& Suffix = FString());
	// Тот же путь с суффиксом анимации anim_%02d.
	static FString BuildSkelAnimPath(const FString& Root, const FString& Name, int32 AnimIdx);

	UFUNCTION(BlueprintCallable, Category="AsdaModelAnim")
	int32 GetCachedCount() const { return Cache.Num(); }

	// Суммарный размер закэшированных ресурсов в байтах (аналог resMgrGetTotalCacheBytes@0x119B470).
	UFUNCTION(BlueprintCallable, Category="AsdaModelAnim")
	int64 GetTotalCacheBytes() const;

private:
	// Общий find-or-load по ключу для любого UObject-ресурса (меш/клип) — единый кэш, как в CResMgr.
	UObject* LoadOrCache(const FString& Key, UClass* Class);

	// Ключ(путь)→ресурс. UPROPERTY = GC-рут (держит меши/клипы резидентно, чтобы не грузить повторно).
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UObject>> Cache;
};
