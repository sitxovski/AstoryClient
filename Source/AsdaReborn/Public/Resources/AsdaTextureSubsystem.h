// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaTextureSubsystem — единый менеджер ТЕКСТУР (по образу оригинала CTextureManager, font_text.c).
//   Оригинал (ETALON_RESOURCE_MANAGERS.md §1): CTextureManager_LoadTextureCached@0x116D750 — find-by-name
//   в массиве слотов (имя+refCount+D3D-handle) → при промахе грузит из Texture.btz → отдаёт слот.
//   Порт: низкоуровневый парсер .TEX уже есть (namespace AsdaTex — аналог gfxLoadTextureBtz). Эта подсистема
//   — ЕДИНЫЙ фасад «дай текстуру по имени/пути»: кэш по ключу (регистронезав.) + refCount + GC-рут внутри.
//
//   ПОЧЕМУ UEngineSubsystem, а не GameInstance: текстур-кэш нужен и в РЕДАКТОРЕ (AAsdaMapActor строит карту
//   в OnConstruction, вне PIE), и глобальный engine-lifetime кэш ближе к оригиналу-синглтону. Доступ:
//   GEngine->GetEngineSubsystem<UAsdaTextureSubsystem>().
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "Engine/Engine.h"                 // ★ GEngine + GetEngineSubsystem<> для inline-хелпера AsdaTexMgr()
#include "UObject/ObjectKey.h"             // ★ charsel_preload: TObjectKey — ключ реестра владения флагом полных мипов
#include "AsdaTextureSubsystem.generated.h"

class UTexture2D;

// Слот кэша текстуры (аналог слота CTextureManager: текстура + refCount по имени).
USTRUCT()
struct FAsdaTexCacheEntry
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Tex = nullptr;

	// refCount (эталон slot+4): ++ при повторном запросе, ReleaseTexture декрементит, при 0 слот освобождается.
	int32 RefCount = 0;
};

UCLASS()
class ASDAREBORN_API UAsdaTextureSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	// ── Универсальный вход (аналог CTextureManager_LoadTextureCached) ──
	// Key = объектный путь "/Game/..."(→LoadObject, .Leaf добавляется автоматически) ИЛИ дисковый путь к
	// .TEX (→AsdaTex::LoadTexFile). Дедуп по ключу (регистронезав.) + ++refCount; результат кэшируется/рутится.
	UTexture2D* LoadTextureCached(const FString& Key);

	// Нормализовать имя-источник (срезать любое расширение) и загрузить как UI-текстуру (аналог
	//   CTextureManager_BuildPathAndLoad@0x116D6A0 — замена чужого расширения на паковое). "FOO.TGA" → JUI FOO.
	UFUNCTION(BlueprintCallable, Category="AsdaTexture")
	UTexture2D* BuildPathAndLoad(const FString& Src);

	// UI-атлас JUI по короткому имени: "n_01" → /Game/AsdaAssets/Textures/JUI/n_01.n_01 (расширение срезается).
	UFUNCTION(BlueprintCallable, Category="AsdaTexture")
	UTexture2D* LoadUITexture(FName Name);

	// Текстура объекта/тайла карты: project-first (/Game/AsdaAssets/Textures/Maps/<map>/[Texture/]<name>)
	// + фолбэк рантайм-декодом .TEX с диска (Dir/[Texture/]<base>.TEX). Логика перенесена из AAsdaMapActor.
	UTexture2D* LoadMapTexture(const FString& MapDir, const FString& TexName);

	// Тайл террейна из памяти (вложенный .TEX по offset). CacheKey — уникальный ключ (пусто = без кэша).
	UTexture2D* LoadTexFromMemory(const uint8* Data, int32 Size, const FString& CacheKey = FString());

	// ── refCount API (аналог CTextureManager slot+4) ──
	// Ещё раз сослаться на закэшированную текстуру по ключу (++refCount). true если ключ найден.
	UFUNCTION(BlueprintCallable, Category="AsdaTexture")
	bool AddRefTexture(const FString& Key);

	// Освободить ссылку (--refCount). При 0 — убрать из кэша (GC освободит, если больше нет ссылок).
	UFUNCTION(BlueprintCallable, Category="AsdaTexture")
	void ReleaseTexture(const FString& Key);

	// Полная очистка кэша (напр. при смене карты). Сами transient-текстуры освободит GC, если больше нет ссылок.
	UFUNCTION(BlueprintCallable, Category="AsdaTexture")
	void ClearCache();

	UFUNCTION(BlueprintCallable, Category="AsdaTexture")
	int32 GetCachedCount() const { return Cache.Num(); }

	// ★ 10.10.2026 (язык на лету): отпустить из кэша текстуры, у которых есть языковой вариант
	//   `/Game/L10N/<культура>/…` (атласы с надписями). Движок выбирает вариант при ЗАГРУЗКЕ пакета, поэтому после
	//   смены культуры такой пакет должен выгрузиться (перезагрузка уровня + сборка мусора) и загрузиться заново.
	//   Возврат — сколько слотов отпущено.
	int32 ReleaseLocalizedVariants();
	// Есть ли у пакета ассета языковой вариант хотя бы одной культуры порта (en / ar / ru).
	static bool HasLocalizedVariant(const FString& PackageName);

	// Суммарный размер закэшированных текстур в байтах (аналог учёта памяти менеджера).
	UFUNCTION(BlueprintCallable, Category="AsdaTexture")
	int64 GetTotalCacheBytes() const;

	// ★★★ ЕДИНЫЙ РЕЕСТР ВЛАДЕНИЯ ФЛАГОМ ПОЛНЫХ МИПОВ (`bForceMiplevelsToBeResident`, ревизия charsel_preload
	//   13.09.2026). Флаг один на ассет, а ставят его несколько независимых владельцев: предзагрузка экрана
	//   выбора, превью на нём, персонажи в мире. Прямая запись «поставил/снял» зависела от порядка событий:
	//   кто пришёл вторым, видел флаг уже стоящим и не считал его своим — и не снимал никогда.
	//   Здесь: у ассета счётчик владельцев; первый владелец запоминает, стоял ли флаг ДО него (чужой постоянный
	//   форс, напр. картинка загрузочного экрана — его не снимаем), последний отпустивший возвращает прежнее.
	//   Владелец = набор ключей (один ассет входит в набор один раз). Только игровой поток.
	using FFullMipsOwnerSet = TSet<TObjectKey<class UStreamableRenderAsset>>;
	// Взять флаг для Asset от имени Owner (повтор тем же владельцем — ничего).
	static void AcquireFullMips(class UStreamableRenderAsset* Asset, FFullMipsOwnerSet& Owner);
	// Привести набор владельца к Want: отпустить лишнее, взять новое.
	static void SyncFullMips(FFullMipsOwnerSet& Owner, const TSet<class UStreamableRenderAsset*>& Want);
	// Отпустить всё, что держит владелец (набор очищается).
	static void ReleaseAllFullMips(FFullMipsOwnerSet& Owner);

private:
	// Резолв «объектный путь vs дисковый .TEX» + кэш. Возвращает уже закэшированное (++refCount) либо грузит.
	UTexture2D* ResolveAndCache(const FString& Key);

	// Записать/обновить слот кэша (refCount=1 при первом добавлении). CacheKey уже нормализован (lowercase).
	UTexture2D* StoreCached(const FString& CacheKey, UTexture2D* Tex);

	// ★ Довести загруженный АССЕТ до пригодного к немедленному использованию: дождаться async-компиляции
	//   (иначе UE отдаёт ДЕФОЛТНУЮ серую 32×32 — модель выглядит «без текстуры») и закрепить мипы
	//   резидентными (текстуры, назначаемые в MID в рантайме, не входят в стриминг-данные меша).
	//   Подробности и доказательство — в теле функции (AsdaTextureSubsystem.cpp).
	static void MakeReadyForRuntimeUse(UTexture2D* Tex);

	// Сколько секунд держать мипы рантайм-текстур резидентными (предметы/карта/UI — мелкие, дёшево).
	static constexpr float kForceMipResidentSeconds = 600.0f;

	// Ключ(lowercase)→слот. UPROPERTY = GC-рут (держит импортированные ассеты и transient .TEX).
	UPROPERTY(Transient)
	TMap<FString, FAsdaTexCacheEntry> Cache;
};

// ★ Короткий доступ к менеджеру (аудит №3, B8): строка `GEngine ? GEngine->GetEngineSubsystem<…>() : nullptr`
//   стояла 19 раз в 14 файлах, и именно её длина была причиной, по которой в чате и на мини-карте
//   писали прямой `LoadObject` мимо кэша и refCount (закон `resource-subsystems`).
FORCEINLINE UAsdaTextureSubsystem* AsdaTexMgr()
{
	return GEngine ? GEngine->GetEngineSubsystem<UAsdaTextureSubsystem>() : nullptr;
}
