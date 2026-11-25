// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMeshBuilder — EDITOR-ONLY сборка StaticMesh из плоских массивов с АВТОРСКИМИ НОРМАЛЯМИ.
//
// ★★★ ЗАЧЕМ (дефект Д-2 аудита света 2026-09-03, `Reports/lighting_audit_2026-09-03.md` §4.1).
//   Объекты карт несут авторские нормали (`.jbm`/PBB FVF 0x152/0x252/0x256/0x60252 → `.mapraw`),
//   но Python-путь `asda_build_map_level.build_mesh` их ТЕРЯЛ: у `UStaticMeshDescription`
//   (Engine/Classes/Engine/StaticMeshDescription.h:45-60) есть только `SetVertexInstanceUV`,
//   сеттера нормалей НЕТ, а `BuildFromMeshDescriptions` при `bFastBuild=false` идёт в `Build()`
//   с дефолтными `FMeshBuildSettings` (`bRecomputeNormals=true`, EngineTypes.h:3038) — нормали
//   пересчитываются по геометрии. Итог на alpen: 38.9 % вершин отклоняются от авторских >30°,
//   35 моделей инвертированы, карточки листвы (`tree_gru_021_*`, 88°) светятся как плоскости.
//
// ★ ЧТО ДЕЛАЕТ. Из Python приходят позиции/нормали/UV/цвета/индексы; здесь строится
//   `FMeshDescription` через `FStaticMeshAttributes` (нормали → `GetVertexInstanceNormals()`,
//   цвета → `GetVertexInstanceColors()`), у LOD0 выставляется `BuildSettings.bRecomputeNormals=false`
//   ДО `BuildFromMeshDescriptions` (внутри тот делает `SetNumSourceModels(1)` — на уже созданной
//   модели настройки НЕ сбрасываются, StaticMesh.cpp:5944-5990), и меш собирается ОБЫЧНЫМ
//   билдером (`bFastBuild=false` → `Build(true)`, StaticMesh.cpp:9057) — тот, что заполняет
//   `FStaticMeshRenderData::Bounds` (см. AsdaMeshMaintenance.h про «быструю» ветку).
//   Семантика нулевой нормали: `FStaticMeshOperations::ComputeTangentsAndNormals`
//   (StaticMeshOperations.cpp:1639) при снятом флаге Normals заполняет ТОЛЬКО нулевые нормали
//   средней по группе сглаживания ⇒ модели без нормалей в источнике (FVF без D3DFVF_NORMAL,
//   в `.mapraw` нули) получают честный пересчёт UE, а авторские остаются 1:1.
//
// ★ ЗАЛОЧЕННЫЕ ПРАВИЛА (память map-ue-export-pipeline), перенесены сюда из Python 1:1:
//   • ремап ТОЛЬКО используемых вершин (вне индексов лежит 0xCD-мусор → NaN bounds);
//   • санитайз NaN/inf → 0 (счётчик в статистике);
//   • пропуск вырожденных треугольников (равные индексы И совпадающие позиции, L1 < 1e-3);
//   • вызывающая сторона обязана давать СВЕЖИЙ ассет (пересборка поверх существующего портила bounds);
//   • после сборки габариты рендер-данных проверяются на NaN/бесконечность — иначе bOk=false.
//
// ★ ВЕРШИННЫЙ ЦВЕТ. Билдер кладёт цвет через `FLinearColor::ToFColor(bSRGB=true)`
//   (StaticMeshBuilder.cpp:1708), поэтому байты источника здесь раскодируются
//   `FLinearColor::FromSRGBColor` — после кодирования билдером в буфер попадает ИСХОДНЫЙ байт,
//   и узел VertexColor в материале отдаёт ровно значение источника (0..1).
//
// Конверсия осей (bGameToUE) = закон проекта `asda_coords.to_ue` / `AAsdaMapActor::ToUE`:
//   UE = (-z, x, y); нормаль — та же перестановка без переноса (`AAsdaMapActor::ToUENormal`).
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AsdaMeshBuilder.generated.h"

class UStaticMesh;
class USkeletalMesh;

/** Итог сборки одного StaticMesh — числа для журнала/проверки из Python. */
USTRUCT(BlueprintType)
struct ASDAIMPORTERS_API FAsdaMeshBuildStats
{
	GENERATED_BODY()

	/** Сборка удалась и габариты рендер-данных конечны. */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Меши") bool bOk = false;
	/** Вершин во входных массивах. */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Меши") int32 SourceVertices = 0;
	/** Вершин, реально попавших в меш (только те, на которые ссылаются валидные треугольники). */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Меши") int32 UsedVertices = 0;
	/** Валидных треугольников. */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Меши") int32 Triangles = 0;
	/** Пропущенных вырожденных треугольников (равные индексы / совпадающие позиции / индекс вне массива). */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Меши") int32 SkippedDegenerate = 0;
	/** Используемых вершин с ненулевой авторской нормалью (записана в меш 1:1). */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Меши") int32 NormalsAuthored = 0;
	/** Используемых вершин с нулевой нормалью в источнике (UE досчитает по сглаживанию). */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Меши") int32 NormalsMissing = 0;
	/** Компонент (позиций/нормалей/UV/цветов), заменённых с NaN/inf на 0. */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Меши") int32 SanitizedValues = 0;
	/** Полигон-групп (секций/слотов материалов). */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Меши") int32 Groups = 0;
	/** ★ 07.09.2026 (швы террейна): исходных вершин, сшитых с другой вершиной той же позиции (bWeldVertices). */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Меши") int32 WeldedVertices = 0;
	/** Радиус сферы габаритов рендер-данных после сборки (контроль NaN-габаритов). */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Меши") float RenderSphereRadius = 0.f;
	/** Текст ошибки (пусто при успехе). */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Меши") FString Error;
};

UCLASS()
class ASDAIMPORTERS_API UAsdaMeshBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Собрать StaticMesh (LOD0) из плоских массивов с сохранением авторских нормалей.
	 * @param Mesh            СВЕЖИЙ ассет (create_asset), без source-моделей и рендер-данных.
	 * @param Positions       x,y,z на вершину (SourceVertices*3).
	 * @param Normals         x,y,z на вершину или ПУСТО; нулевая/вырожденная нормаль = «нет в источнике».
	 * @param UVs             u,v на вершину или ПУСТО (тогда 0,0).
	 * @param Colors          r,g,b,a на вершину в БАЙТАХ 0..255 (как в источнике) или ПУСТО (белый).
	 * @param Indices         3 индекса на треугольник.
	 * @param TriangleGroups  индекс полигон-группы на треугольник или ПУСТО (все в группу 0).
	 * @param GroupSlotNames  имена слотов материалов по группам (минимум одно; порядок = порядок секций).
	 * @param bGameToUE       true = входные позиции/нормали в игровом пространстве, конвертировать (-z,x,y);
	 *                        false = уже в пространстве UE (небо, вода в мир-координатах и т.п.).
	 * @param bRecomputeNormals true = игнорировать Normals и считать всё в UE (для мешей без авторских нормалей).
	 * @param bBuildSimpleCollision как одноимённый параметр build_from_static_mesh_descriptions.
	 * @param DistanceFieldResolutionScale ★ Сет E-1 (06.09.2026): `FMeshBuildSettings::DistanceFieldResolutionScale`
	 *                        (EngineTypes.h:3016) — масштаб сетки Mesh Distance Field. Потолок движка:
	 *                        при scale≤1 — DivideAndRoundNearest(256/2, 7) = 18 бриков × 7 уникальных вокселей = 126 вокселей на измерение,
	 *                        при scale>1 — DivideAndRoundNearest(256, 7) = 37 × 7 = 259 (MeshDistanceFieldUtilities.cpp:308-317,
	 *                        r.DistanceFields.MaxPerMeshResolution=256; замер Э10: террейн mine 126³). Террейн карты 512 тайлов =
	 *                        51 200 юн: 1.0 → ≈406 юн/воксель, 2.0 → ≈198 (больше 2.0 не даёт ничего — потолок 37 бриков). Объекты — 1.0.
	 * @param bDistanceFieldTwoSided ★ Сет E-1: `bGenerateDistanceFieldAsIfTwoSided` (EngineTypes.h:2990) —
	 *                        листва/карточки/кресты (открытая геометрия) строят DF «как двусторонние»,
	 *                        иначе DF открытого меша получается односторонним/дырявым; у прочих false.
	 *                        Оба поля входят в ключ DDC меша (StaticMesh.cpp:3662-3663) — пересборка
	 *                        уровней билдером карт СОХРАНЯЕТ настройку DF (без этого перегонка её теряла бы).
	 * @param bWeldVertices   ★ 07.09.2026 (швы террейна, terrain_seams_report.md): сшивать вершины с одинаковой позицией
	 *                        (квантование 0.01 юн) в ОДНУ FVertexID до расчёта нормалей. Патчи террейна .mapraw не делят
	 *                        вершины (граничная вершина есть в обоих патчах, 7133 совпадений на карту), а
	 *                        FStaticMeshOperations::ComputeTangentsAndNormals усредняет нормали по FVertexID через
	 *                        GetVertexConnectedEdgeIDs (StaticMeshOperations.cpp:1470-1476; флаг BlendOverlappingNormals
	 *                        в ней не читается) — без сшивки нормали на границе считаются по треугольникам только своего
	 *                        патча → ступень освещения вдоль 14 линий сетки 6400 юн. UV/цвет — атрибуты вершинных
	 *                        экземпляров, у каждого патча остаются свои; полигон-группы (секции) не меняются.
	 *                        Для мешей с авторскими нормалями не нужно (false).
	 * Материалы ассета НЕ назначаются и ассет НЕ сохраняется — это делает вызывающая сторона.
	 */
	UFUNCTION(BlueprintCallable, Category = "Asda|Меши")
	static FAsdaMeshBuildStats BuildStaticMeshFromArrays(
		UStaticMesh* Mesh,
		const TArray<float>& Positions,
		const TArray<float>& Normals,
		const TArray<float>& UVs,
		const TArray<float>& Colors,
		const TArray<int32>& Indices,
		const TArray<int32>& TriangleGroups,
		const TArray<FName>& GroupSlotNames,
		bool bGameToUE,
		bool bRecomputeNormals,
		bool bBuildSimpleCollision = false,
		float DistanceFieldResolutionScale = 1.0f,
		bool bDistanceFieldTwoSided = false,
		bool bWeldVertices = false);

	/**
	 * Габариты РЕНДЕР-ДАННЫХ (`FStaticMeshRenderData::Bounds`) — именно те, что уходят в кук и
	 * которые Python через `get_bounds()` не видит (тот отдаёт ExtendedBounds из описания меша).
	 * @return false, если рендер-данных нет или габариты содержат NaN/бесконечность/|v|>1e30.
	 */
	UFUNCTION(BlueprintCallable, Category = "Asda|Меши")
	static bool GetStaticMeshRenderBounds(UStaticMesh* Mesh, FVector& Origin, FVector& BoxExtent, float& SphereRadius);

	/**
	 * Позиции, нормали и индексы треугольников LOD0 из РЕНДЕР-ДАННЫХ StaticMesh (то, что реально
	 * рисуется) — для численной сверки с источником (по позициям — с .mapraw; по индексам — угол
	 * «нормаль vs геометрическая», второй, независимый от трансформа метод). При отсутствии CPU-копии
	 * буферов — из описания меша (bOutFromRenderData=false). OutColors — вершинный цвет буфера как байт/255
	 * (ReinterpretAsLinear, т.е. ровно то, что отдаёт узел VertexColor); пусто, если цветов в буфере нет.
	 */
	UFUNCTION(BlueprintCallable, Category = "Asda|Меши")
	static bool GetStaticMeshLod0Vertices(UStaticMesh* Mesh, TArray<FVector>& OutPositions, TArray<FVector>& OutNormals, TArray<int32>& OutIndices, TArray<FLinearColor>& OutColors, bool& bOutFromRenderData);

	/**
	 * То же для SkeletalMesh (LOD0 рендер-данных) + фактическая настройка сборки
	 * `FSkeletalMeshLODInfo::BuildSettings.bRecomputeNormals` (из Python не читается).
	 */
	UFUNCTION(BlueprintCallable, Category = "Asda|Меши")
	static bool GetSkeletalMeshLod0Vertices(USkeletalMesh* Mesh, TArray<FVector>& OutPositions, TArray<FVector>& OutNormals, TArray<int32>& OutIndices, bool& bOutRecomputeNormalsSetting);
};
