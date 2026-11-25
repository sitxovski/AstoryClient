// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMeshMaintenance — EDITOR-ONLY обслуживание уже импортированных StaticMesh'ей.
// ★ Существует ровно потому, что одно-единственное поле движка недоступно из Python:
//   `UStaticMesh::bDoFastBuild` (UPROPERTY, Engine/Classes/Engine/StaticMesh.h:1393-1394) объявлено
//   без BlueprintReadWrite/EditAnywhere ⇒ PythonScriptPlugin отвечает
//   «Property 'bDoFastBuild' ... is protected and cannot be read». Из C++ поле лежит в public-секции
//   (public: со стр. 1329) и пишется напрямую.
//
// ★★★ ЗАЧЕМ. Флаг взводится при `build_from_static_mesh_descriptions(..., fast_build=True)`
//   (StaticMesh.cpp:8928-8931 — ветка `if (Params.bFastBuild) { bDoFastBuild = true; }`; обратного
//   присваивания в false там НЕТ ни при каких аргументах). Дальше при КАЖДОЙ сборке рендер-данных
//   `FStaticMeshRenderData::Cache` (StaticMesh.cpp:4321) видит взведённый флаг и идёт «быстрой»
//   веткой: AllocateLODResources + BuildFromMeshDescription по LOD'ам, МИМО DDC
//   («just build it each time here instead of using the DDC as a cache») и МИМО присваивания
//   `RenderData->Bounds`. Поле `FBoxSphereBounds Bounds` (StaticMeshResources.h:795) в конструкторе
//   FStaticMeshRenderData не инициализируется ⇒ в кук уходит мусор неинициализированной памяти,
//   у части мешей — NaN («FStaticMeshRenderData::Serialize found NaN in Bounds of ...»).
//   В редакторе не видно: CalculateExtendedBounds берёт CachedMeshDescriptionBounds, а не RenderData->Bounds.
//
// ★★ ПОЧЕМУ НЕ ПОМОГ ОБХОДНОЙ ПУТЬ ЧЕРЕЗ NANITE. Движок сам сбрасывает флаг в
//   UStaticMesh::PostEditChangeProperty (StaticMesh.cpp:5445-5453) при изменении NaniteSettings с
//   bEnabled=true. Но Python-вызов `set_editor_property("nanite_settings", ns)` идёт с
//   EPropertyAccessChangeNotifyMode::Default = «уведомлять, ТОЛЬКО если значение реально изменилось»
//   (PropertyAccessUtil.h:51-59). Структура, полученная через get_editor_property, правится по месту,
//   поэтому к моменту записи новое значение равно старому → PostEditChangeProperty НЕ вызывается →
//   флаг остаётся взведённым. Проверено фактом: после «лечения» 20 152 мешей имя `bDoFastBuild`
//   по-прежнему сериализовано в 20 112 пакетах из 21 065 (тегированная сериализация пишет свойство,
//   только если оно ОТЛИЧАЕТСЯ от CDO, а в CDO флаг = false — StaticMesh.cpp:4741).
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AsdaMeshMaintenance.generated.h"

class UStaticMesh;

/**
 * ★ Сет E-1 (06.09.2026): состояние Mesh Distance Field у StaticMesh — из Python не читается
 * (`FStaticMeshRenderData::LODResources[0].DistanceFieldData` и `SourceModels` — не UPROPERTY/приватные).
 */
USTRUCT(BlueprintType)
struct ASDAIMPORTERS_API FAsdaDistanceFieldInfo
{
	GENERATED_BODY()

	/** Есть ли у LOD0 объект DistanceFieldData вообще (строится только при r.GenerateMeshDistanceFields=1 или per-mesh флаге). */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|DF") bool bHasData = false;
	/** DF валиден (FDistanceFieldVolumeData::IsValid — Mip0 не пуст). */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|DF") bool bValid = false;
	/** Сборка ещё идёт в фоне (bAsyncBuilding). */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|DF") bool bAsyncBuilding = false;
	/** Движок счёл меш «в основном двусторонним» (открытая геометрия). */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|DF") bool bMostlyTwoSided = false;
	/** Размер сетки индирекции Mip0 в бриках (×7 уникальных вокселей = разрешение). */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|DF") FIntVector Mip0Bricks = FIntVector::ZeroValue;
	/** Число занятых бриков Mip0 (разреженность). */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|DF") int32 Mip0NumBricks = 0;
	/** Память DF (все мипы + всегда загруженный мип), байт — FDistanceFieldVolumeData::GetResourceSizeBytes. */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|DF") int64 ResourceBytes = 0;
	/** Настройки сборки LOD0: DistanceFieldResolutionScale / bGenerateDistanceFieldAsIfTwoSided; per-mesh флаг bGenerateMeshDistanceField. */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|DF") float ResolutionScale = 1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Asda|DF") bool bTwoSidedSetting = false;
	UPROPERTY(BlueprintReadOnly, Category = "Asda|DF") bool bPerMeshFlag = false;
	/** Габарит локального бокса меша (для формулы «юн/воксель»). */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|DF") FVector BoundsSize = FVector::ZeroVector;
};

UCLASS()
class ASDAIMPORTERS_API UAsdaMeshMaintenance : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Взведён ли у меша `bDoFastBuild` (диагностика; из Python поле не читается). */
	UFUNCTION(BlueprintCallable, Category = "Asda|Обслуживание")
	static bool IsFastBuildStaticMesh(UStaticMesh* Mesh);

	/**
	 * Снять `bDoFastBuild` у одного меша. Возвращает true, если флаг БЫЛ взведён и снят.
	 * @param bRebuild  дополнительно прогнать честный UStaticMesh::Build() прямо сейчас
	 *                  (нужно не всегда: со снятым флагом Cache сам пойдёт обычным билдером).
	 * Ассет НЕ сохраняется — сохранение делает вызывающая сторона.
	 */
	UFUNCTION(BlueprintCallable, Category = "Asda|Обслуживание")
	static bool ClearFastBuildStaticMesh(UStaticMesh* Mesh, bool bRebuild = false);

	/**
	 * Массовое снятие флага по корням реестра ассетов (например "/Game/Map").
	 * Грузит каждый StaticMesh, снимает флаг и сохраняет пакет.
	 * @param bDryRun  только посчитать, ничего не менять и не сохранять.
	 * @return строка-отчёт «просмотрено N, было взведено M, снято K, сохранено S, ошибок E».
	 */
	UFUNCTION(BlueprintCallable, Category = "Asda|Обслуживание")
	static FString ClearFastBuildInPaths(const TArray<FString>& Roots, bool bDryRun = false, bool bRebuild = false);

	/**
	 * ★★★ ВТОРАЯ ПОЛОВИНА ЛЕЧЕНИЯ — СБРОС ОТРАВЛЕННОЙ ЗАПИСИ DDC.
	 * Снятия `bDoFastBuild` мало: «быстрая» ветка FStaticMeshRenderData::Cache не только не считает
	 * Bounds, но и КЛАДЁТ результат в DDC — блок `GetCache().Put(...)` (StaticMesh.cpp:4448-4457)
	 * находится ПОСЛЕ if/else и общий для обеих веток. Комментарий движка «just build it each time
	 * here instead of using the DDC as a cache» (стр. 4322) относится только к ЧТЕНИЮ. Ключ
	 * (BuildStaticMeshDerivedDataKeySuffix, стр. 3727) флаг не учитывает, поэтому меш со снятым
	 * флагом попадает в ту же самую запись и снова поднимает мусорные габариты.
	 *
	 * Лечение: перекоммитить mesh-description LOD0 с `bUseHashAsGuid=true`. В ключ входит
	 * `GetMeshDescriptionBulkData()->GetUnversionedIdString()` (стр. 3825), а коммит меняет этот
	 * идентификатор на ХЕШ СОДЕРЖИМОГО: ключ становится другим (старая отравленная запись больше не
	 * находится) и одновременно детерминированным — повторный прогон ключ уже не двигает.
	 * Геометрия при этом не меняется вообще: описание записывается то же самое.
	 */
	UFUNCTION(BlueprintCallable, Category = "Asda|Обслуживание")
	static bool RefreshDerivedDataKey(UStaticMesh* Mesh);

	/** Массовый вариант RefreshDerivedDataKey по корням реестра ассетов, с сохранением пакетов. */
	UFUNCTION(BlueprintCallable, Category = "Asda|Обслуживание")
	static FString RefreshDerivedDataKeysInPaths(const TArray<FString>& Roots, bool bDryRun = false);

	// ---------------------------------------------------------------------------------------------
	// ★ Сет E-1 (06.09.2026): Mesh Distance Fields. Настройки DF живут в `FMeshBuildSettings` LOD0
	//   (`SourceModels` — приватный UPROPERTY без геттера, из Python недоступен), состояние DF — в
	//   рендер-данных. Отсюда функции ниже; сохранение пакетов — вызывающая сторона (Python).
	// ---------------------------------------------------------------------------------------------

	/** Состояние DF и настройки сборки LOD0 (см. FAsdaDistanceFieldInfo). */
	UFUNCTION(BlueprintCallable, Category = "Asda|Обслуживание")
	static FAsdaDistanceFieldInfo GetDistanceFieldInfo(UStaticMesh* Mesh);

	/**
	 * Записать `DistanceFieldResolutionScale` / `bGenerateDistanceFieldAsIfTwoSided` в BuildSettings LOD0.
	 * Оба поля входят в ключ DDC меша (StaticMesh.cpp:3662-3663) ⇒ при следующей загрузке рендер-данные
	 * пересоберутся честным билдером (Build → Bounds заполняются, как у ClearFastBuildStaticMesh).
	 * ★ Сет E-2: ResolutionScale 0.0 = DF у меша НЕ строится (пустое поле, вне DF-сцены) — единственный per-mesh выключатель при
	 *   проектном r.GenerateMeshDistanceFields=1 (флаг bGenerateMeshDistanceField тогда игнорируется, StaticMesh.cpp:4564).
	 * @param bRebuild  пересобрать прямо сейчас (UStaticMesh::Build; долго — для проверки на 1 меше).
	 * @return true, если хоть одно значение изменилось (тогда Modify() уже вызван; пакет НЕ сохранён).
	 */
	UFUNCTION(BlueprintCallable, Category = "Asda|Обслуживание")
	static bool SetDistanceFieldBuildSettings(UStaticMesh* Mesh, float ResolutionScale, bool bTwoSided, bool bRebuild = false);

	/**
	 * Дождаться фоновых сборок DF и карточек Lumen (GDistanceFieldAsyncQueue / GCardRepresentationAsyncQueue):
	 * в коммандлете задачи завершаются только через ProcessAsyncTasks на игровом потоке.
	 * @return число незавершённых задач DF+карточек по истечении TimeoutSec (0 = всё собрано).
	 */
	UFUNCTION(BlueprintCallable, Category = "Asda|Обслуживание")
	static int32 WaitForDistanceFieldBuilds(float TimeoutSec = 3600.0f);

	/** Незавершённых задач DF+карточек сейчас (без ожидания). */
	UFUNCTION(BlueprintCallable, Category = "Asda|Обслуживание")
	static int32 GetOutstandingDistanceFieldBuilds();

	/**
	 * Per-mesh флаг `bGenerateMeshDistanceField` БЕЗ PostEditChangeProperty (Э10). Python `set_editor_property` зовёт
	 * PostEditChangeProperty → Build() (StaticMesh.cpp:5457) на каждом меше; в headless-сеансе 06.09 такой Build террейна
	 * mine (270 К верш.) завис в FinishCompilation («Подготовка: статичные сетки 0/1») на 30 мин при ~1 ядре — прогон убит
	 * по простою. Здесь поле пишется напрямую (+Modify), DF соберётся при следующей загрузке ассета (PostLoad → Cache →
	 * StaticMesh.cpp:4564 `CVar!=0 || bGenerateMeshDistanceField`). Возвращает true, если значение изменилось.
	 */
	UFUNCTION(BlueprintCallable, Category = "Asda|Обслуживание")
	static bool SetGenerateMeshDistanceFieldFlag(UStaticMesh* Mesh, bool bEnable);

	/** Незавершённых фоновых сборок StaticMesh (FStaticMeshCompilingManager) — вместе с DF в ожидании WaitForDistanceFieldBuilds. */
	UFUNCTION(BlueprintCallable, Category = "Asda|Обслуживание")
	static int32 GetOutstandingStaticMeshCompilations();
};
