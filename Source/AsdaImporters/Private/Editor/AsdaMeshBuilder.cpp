// Copyright (c) 2026 @sitxovski (Discord)
// Реализация сборки StaticMesh из массивов с авторскими нормалями. Разбор причины — в AsdaMeshBuilder.h.
#include "Editor/AsdaMeshBuilder.h"

#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMeshBuild, Log, All);

namespace
{
	/** Санитайз одного числа: NaN/inf/|v|>1e30 → 0 (правило Python `_f`). Счётчик замен — в Stats. */
	FORCEINLINE float SaneF(float V, int32& Counter)
	{
		if (!FMath::IsFinite(V) || FMath::Abs(V) > 1.0e30f)
		{
			++Counter;
			return 0.0f;
		}
		return V;
	}

	/** Конверсия осей игра→UE (закон `asda_coords.to_ue` / `AAsdaMapActor::ToUE`): UE = (-z, x, y). */
	FORCEINLINE FVector3f GameToUE(float X, float Y, float Z)
	{
		return FVector3f(-Z, X, Y);
	}

	/** Конечны ли габариты (защита от мусора «быстрой» ветки: 1e194/1e308/NaN). */
	bool BoundsSane(const FBoxSphereBounds& B)
	{
		auto Ok = [](double V) { return FMath::IsFinite(V) && FMath::Abs(V) < 1.0e30; };
		return Ok(B.Origin.X) && Ok(B.Origin.Y) && Ok(B.Origin.Z)
			&& Ok(B.BoxExtent.X) && Ok(B.BoxExtent.Y) && Ok(B.BoxExtent.Z)
			&& Ok(B.SphereRadius);
	}
}

FAsdaMeshBuildStats UAsdaMeshBuilder::BuildStaticMeshFromArrays(
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
	bool bBuildSimpleCollision,
	float DistanceFieldResolutionScale,
	bool bDistanceFieldTwoSided,
	bool bWeldVertices)
{
	FAsdaMeshBuildStats Stats;

	// ---- проверка входа --------------------------------------------------------------------
	if (Mesh == nullptr)
	{
		Stats.Error = TEXT("Mesh == nullptr");
		return Stats;
	}
	if (Positions.Num() == 0 || (Positions.Num() % 3) != 0)
	{
		Stats.Error = FString::Printf(TEXT("Positions: %d float (не кратно 3 или пусто)"), Positions.Num());
		return Stats;
	}
	const int32 NumSrcVerts = Positions.Num() / 3;
	Stats.SourceVertices = NumSrcVerts;
	const bool bHasNormals = !bRecomputeNormals && Normals.Num() == NumSrcVerts * 3;
	if (!bRecomputeNormals && Normals.Num() != 0 && Normals.Num() != NumSrcVerts * 3)
	{
		Stats.Error = FString::Printf(TEXT("Normals: %d float, ожидалось %d"), Normals.Num(), NumSrcVerts * 3);
		return Stats;
	}
	const bool bHasUVs = UVs.Num() == NumSrcVerts * 2;
	if (UVs.Num() != 0 && !bHasUVs)
	{
		Stats.Error = FString::Printf(TEXT("UVs: %d float, ожидалось %d"), UVs.Num(), NumSrcVerts * 2);
		return Stats;
	}
	const bool bHasColors = Colors.Num() == NumSrcVerts * 4;
	if (Colors.Num() != 0 && !bHasColors)
	{
		Stats.Error = FString::Printf(TEXT("Colors: %d float, ожидалось %d"), Colors.Num(), NumSrcVerts * 4);
		return Stats;
	}
	if (Indices.Num() < 3 || (Indices.Num() % 3) != 0)
	{
		Stats.Error = FString::Printf(TEXT("Indices: %d (не кратно 3 или < 3)"), Indices.Num());
		return Stats;
	}
	const int32 NumSrcTris = Indices.Num() / 3;
	if (TriangleGroups.Num() != 0 && TriangleGroups.Num() != NumSrcTris)
	{
		Stats.Error = FString::Printf(TEXT("TriangleGroups: %d, треугольников %d"), TriangleGroups.Num(), NumSrcTris);
		return Stats;
	}
	if (GroupSlotNames.Num() == 0)
	{
		Stats.Error = TEXT("GroupSlotNames пуст — нужен хотя бы один слот");
		return Stats;
	}
	Stats.Groups = GroupSlotNames.Num();

	// ---- позиции/нормали в пространстве UE + санитайз ---------------------------------------
	TArray<FVector3f> Pos;
	Pos.SetNumUninitialized(NumSrcVerts);
	TArray<FVector3f> Nrm;
	if (bHasNormals)
	{
		Nrm.SetNumUninitialized(NumSrcVerts);
	}
	for (int32 V = 0; V < NumSrcVerts; ++V)
	{
		const float X = SaneF(Positions[V * 3 + 0], Stats.SanitizedValues);
		const float Y = SaneF(Positions[V * 3 + 1], Stats.SanitizedValues);
		const float Z = SaneF(Positions[V * 3 + 2], Stats.SanitizedValues);
		Pos[V] = bGameToUE ? GameToUE(X, Y, Z) : FVector3f(X, Y, Z);
		if (bHasNormals)
		{
			const float NX = SaneF(Normals[V * 3 + 0], Stats.SanitizedValues);
			const float NY = SaneF(Normals[V * 3 + 1], Stats.SanitizedValues);
			const float NZ = SaneF(Normals[V * 3 + 2], Stats.SanitizedValues);
			// Направление: та же перестановка осей, без переноса; нормируем (экспортёр un-bake'ит через
			// матрицу инстанса, у которой масштаб не обязательно 1). Вырожденная → ноль = «нет в источнике».
			FVector3f N = bGameToUE ? GameToUE(NX, NY, NZ) : FVector3f(NX, NY, NZ);
			const float Len = N.Size();
			Nrm[V] = (Len > 1.0e-6f) ? (N / Len) : FVector3f::ZeroVector;
		}
	}

	// ---- отбор валидных треугольников и используемых вершин ---------------------------------
	auto Coincident = [&Pos](int32 A, int32 B)
	{
		const FVector3f D = Pos[A] - Pos[B];
		return (FMath::Abs(D.X) + FMath::Abs(D.Y) + FMath::Abs(D.Z)) < 1.0e-3f;
	};
	TArray<int32> ValidTris;               // индексы треугольников (в Indices/3), прошедших отбор
	ValidTris.Reserve(NumSrcTris);
	TArray<int32> Remap;                   // исходный индекс вершины → индекс в меше (-1 = не используется)
	Remap.Init(-1, NumSrcVerts);
	int32 NumUsed = 0;

	// ---- сшивка вершин по позиции (bWeldVertices) ----------------------------------------------
	// ★ 07.09.2026 (швы террейна, terrain_seams_report.md): патчи террейна .mapraw не делят вершины —
	//   граничная вершина есть в ОБОИХ патчах (7133 совпадающих позиций на карту, 14 линий сетки 6400 юн).
	//   FStaticMeshOperations::ComputeTangentsAndNormals усредняет нормали по FVertexID через
	//   GetVertexConnectedEdgeIDs (StaticMeshOperations.cpp:1470-1476), флаг BlendOverlappingNormals внутри
	//   неё не читается ⇒ две вершины с одной позицией получают нормали по треугольникам только своего патча —
	//   ступень освещения вдоль границы. Лечение: одна FVertexID на позицию (ключ — позиция, квантованная
	//   до 0.01 юн; канон = первая исходная вершина с этой позицией). UV/цвет остаются атрибутами вершинных
	//   экземпляров (у каждого патча свои UV 0..1), группы полигонов (секции) не меняются.
	TArray<int32> Canon;                   // исходная вершина → каноническая исходная вершина той же позиции
	if (bWeldVertices)
	{
		Canon.SetNumUninitialized(NumSrcVerts);
		TMap<FIntVector, int32> ByPos;
		ByPos.Reserve(NumSrcVerts);
		for (int32 V = 0; V < NumSrcVerts; ++V)
		{
			const FIntVector Key(FMath::RoundToInt(Pos[V].X * 100.f), FMath::RoundToInt(Pos[V].Y * 100.f), FMath::RoundToInt(Pos[V].Z * 100.f));
			if (const int32* Found = ByPos.Find(Key))
			{
				Canon[V] = *Found;
			}
			else
			{
				ByPos.Add(Key, V);
				Canon[V] = V;
			}
		}
	}
	for (int32 T = 0; T < NumSrcTris; ++T)
	{
		const int32 A = Indices[T * 3 + 0], B = Indices[T * 3 + 1], C = Indices[T * 3 + 2];
		const bool bRange = A >= 0 && B >= 0 && C >= 0 && A < NumSrcVerts && B < NumSrcVerts && C < NumSrcVerts;
		if (!bRange || A == B || B == C || A == C || Coincident(A, B) || Coincident(B, C) || Coincident(A, C))
		{
			++Stats.SkippedDegenerate;
			continue;
		}
		if (TriangleGroups.Num() != 0 && (TriangleGroups[T] < 0 || TriangleGroups[T] >= GroupSlotNames.Num()))
		{
			++Stats.SkippedDegenerate;
			continue;
		}
		ValidTris.Add(T);
		for (int32 I : { A, B, C })
		{
			const int32 K = bWeldVertices ? Canon[I] : I;   // сшивка: индекс в меше берётся у канона позиции
			if (Remap[K] < 0)
			{
				Remap[K] = NumUsed++;
			}
			if (Remap[I] < 0)
			{
				Remap[I] = Remap[K];
				if (K != I)
				{
					++Stats.WeldedVertices;
				}
			}
		}
	}
	Stats.Triangles = ValidTris.Num();
	Stats.UsedVertices = NumUsed;
	if (ValidTris.Num() == 0)
	{
		Stats.Error = TEXT("0 валидных треугольников");
		return Stats;
	}

	// ---- описание меша ------------------------------------------------------------------------
	FMeshDescription MD;
	FStaticMeshAttributes Attributes(MD);
	Attributes.Register();
	TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> InstNormals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector2f> InstUVs = Attributes.GetVertexInstanceUVs();
	TVertexInstanceAttributesRef<FVector4f> InstColors = Attributes.GetVertexInstanceColors();
	TPolygonGroupAttributesRef<FName> GroupNames = Attributes.GetPolygonGroupMaterialSlotNames();

	MD.ReserveNewVertices(NumUsed);
	MD.ReserveNewVertexInstances(ValidTris.Num() * 3);
	MD.ReserveNewTriangles(ValidTris.Num());
	MD.ReserveNewPolygonGroups(GroupSlotNames.Num());

	TArray<FPolygonGroupID> Groups;
	Groups.Reserve(GroupSlotNames.Num());
	for (const FName& SlotName : GroupSlotNames)
	{
		const FPolygonGroupID G = MD.CreatePolygonGroup();
		GroupNames[G] = SlotName;
		Groups.Add(G);
	}

	// Вершины — только используемые, в порядке первого появления (Remap). При сшивке несколько исходных
	// вершин делят один Dst — вершина меша создаётся по первой из них (канон = первая по индексу).
	TArray<FVertexID> VertexIDs;
	VertexIDs.SetNumUninitialized(NumUsed);
	TBitArray<> Created(false, NumUsed);
	for (int32 Src = 0; Src < NumSrcVerts; ++Src)
	{
		const int32 Dst = Remap[Src];
		if (Dst < 0 || Created[Dst])
		{
			continue;
		}
		Created[Dst] = true;
		const FVertexID VID = MD.CreateVertex();
		VertexPositions[VID] = Pos[Src];
		VertexIDs[Dst] = VID;
		if (bHasNormals)
		{
			if (Nrm[Src].IsNearlyZero())
			{
				++Stats.NormalsMissing;
			}
			else
			{
				++Stats.NormalsAuthored;
			}
		}
		else
		{
			++Stats.NormalsMissing;
		}
	}

	// Треугольники: 3 вершинных экземпляра на каждый (UV/нормаль/цвет — атрибуты экземпляра).
	FVertexInstanceID Corner[3];
	for (int32 T : ValidTris)
	{
		const FPolygonGroupID G = Groups[TriangleGroups.Num() != 0 ? TriangleGroups[T] : 0];
		for (int32 K = 0; K < 3; ++K)
		{
			const int32 Src = Indices[T * 3 + K];
			const FVertexInstanceID VI = MD.CreateVertexInstance(VertexIDs[Remap[Src]]);
			if (bHasUVs)
			{
				InstUVs.Set(VI, 0, FVector2f(SaneF(UVs[Src * 2 + 0], Stats.SanitizedValues),
				                             SaneF(UVs[Src * 2 + 1], Stats.SanitizedValues)));
			}
			if (bHasNormals && !Nrm[Src].IsNearlyZero())
			{
				InstNormals[VI] = Nrm[Src];
			}
			if (bHasColors)
			{
				// Байты источника → linear через FromSRGBColor: билдер закодирует обратно ToFColor(true)
				// (StaticMeshBuilder.cpp:1708) и в буфер ляжет исходный байт.
				auto Byte = [&](int32 Off)
				{
					return (uint8)FMath::Clamp(FMath::RoundToInt(SaneF(Colors[Src * 4 + Off], Stats.SanitizedValues)), 0, 255);
				};
				const FColor C(Byte(0), Byte(1), Byte(2), Byte(3));
				const FLinearColor L = FLinearColor::FromSRGBColor(C);
				InstColors[VI] = FVector4f(L.R, L.G, L.B, C.A / 255.0f);   // альфа — линейно, как в буфере
			}
			Corner[K] = VI;
		}
		MD.CreateTriangle(G, Corner);
	}

	// ---- настройки сборки LOD0 ДО BuildFromMeshDescriptions ---------------------------------
	// Внутри BuildFromMeshDescriptions стоит SetNumSourceModels(NewNumLODs): на уже существующей
	// модели настройки сохраняются (сбрасываются только у ДОБАВЛЯЕМЫХ, StaticMesh.cpp:5975-5990),
	// поэтому создаём модель здесь и выставляем флаги заранее.
	Mesh->SetNumSourceModels(1);
	FMeshBuildSettings& BS = Mesh->GetSourceModel(0).BuildSettings;
	BS.bRecomputeNormals = bRecomputeNormals || !bHasNormals;   // авторские есть → НЕ пересчитывать
	BS.bRecomputeTangents = true;                               // тангенты считаем всегда (MikkTSpace от наших нормалей)
	BS.bUseMikkTSpace = true;
	// Лайтмап-UV не нужны: Lightmass в проекте запрещён (свет — Movable Sun/Sky, см. asda_convert_map_lit),
	// а генерация второго UV-канала на 20 тыс. мешей — самая дорогая часть полного билда.
	BS.bGenerateLightmapUVs = false;
	// ★ Сет E-1 (06.09.2026): настройки Mesh Distance Field — часть ключа DDC меша (StaticMesh.cpp:3662-3663),
	//   задаются ЗДЕСЬ, чтобы перегонка карт не теряла их (см. AsdaMeshBuilder.h). Сам DF строится движком
	//   при r.GenerateMeshDistanceFields=1 (DefaultEngine.ini) или per-mesh bGenerateMeshDistanceField.
	BS.DistanceFieldResolutionScale = FMath::Clamp(DistanceFieldResolutionScale, 0.1f, 8.0f);
	BS.bGenerateDistanceFieldAsIfTwoSided = bDistanceFieldTwoSided;

	// Слоты материалов по именам групп (материал назначит вызывающая сторона): секции получат
	// MaterialIndex по ImportedMaterialSlotName (StaticMeshBuilder.cpp:1065-1079), а не по порядку.
	TArray<FStaticMaterial>& Materials = Mesh->GetStaticMaterials();
	Materials.Reset();
	for (const FName& SlotName : GroupSlotNames)
	{
		Materials.Add(FStaticMaterial(nullptr, SlotName, SlotName));
	}

	// ---- сборка обычным билдером (bFastBuild=false → Build(true), заполняет RenderData->Bounds) --
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bFastBuild = false;
	Params.bCommitMeshDescription = true;
	Params.bMarkPackageDirty = true;
	Params.bUseHashAsGuid = true;              // ключ DDC = хеш содержимого (стабилен, не наследует старых записей)
	Params.bBuildSimpleCollision = bBuildSimpleCollision;
	TArray<const FMeshDescription*> Descs;
	Descs.Add(&MD);
	if (!Mesh->BuildFromMeshDescriptions(Descs, Params))
	{
		Stats.Error = TEXT("BuildFromMeshDescriptions вернул false");
		return Stats;
	}

	// ---- контроль габаритов рендер-данных ----------------------------------------------------
	const FStaticMeshRenderData* RD = Mesh->GetRenderData();
	if (RD == nullptr)
	{
		Stats.Error = TEXT("после сборки нет рендер-данных");
		return Stats;
	}
	if (!BoundsSane(RD->Bounds))
	{
		Stats.Error = FString::Printf(TEXT("габариты рендер-данных не конечны: origin=%s extent=%s r=%g"),
			*RD->Bounds.Origin.ToString(), *RD->Bounds.BoxExtent.ToString(), RD->Bounds.SphereRadius);
		return Stats;
	}
	Stats.RenderSphereRadius = (float)RD->Bounds.SphereRadius;
	Stats.bOk = true;
	return Stats;
}

bool UAsdaMeshBuilder::GetStaticMeshRenderBounds(UStaticMesh* Mesh, FVector& Origin, FVector& BoxExtent, float& SphereRadius)
{
	Origin = FVector::ZeroVector;
	BoxExtent = FVector::ZeroVector;
	SphereRadius = 0.f;
	if (Mesh == nullptr || Mesh->GetRenderData() == nullptr)
	{
		return false;
	}
	const FBoxSphereBounds& B = Mesh->GetRenderData()->Bounds;
	Origin = B.Origin;
	BoxExtent = B.BoxExtent;
	SphereRadius = (float)B.SphereRadius;
	return BoundsSane(B);
}

bool UAsdaMeshBuilder::GetStaticMeshLod0Vertices(UStaticMesh* Mesh, TArray<FVector>& OutPositions, TArray<FVector>& OutNormals, TArray<int32>& OutIndices, TArray<FLinearColor>& OutColors, bool& bOutFromRenderData)
{
	OutPositions.Reset();
	OutNormals.Reset();
	OutIndices.Reset();
	OutColors.Reset();
	bOutFromRenderData = false;
	if (Mesh == nullptr)
	{
		return false;
	}
	const FStaticMeshRenderData* RD = Mesh->GetRenderData();
	if (RD != nullptr && RD->LODResources.Num() > 0)
	{
		const FStaticMeshLODResources& LOD = RD->LODResources[0];
		const FPositionVertexBuffer& PB = LOD.VertexBuffers.PositionVertexBuffer;
		const FStaticMeshVertexBuffer& VB = LOD.VertexBuffers.StaticMeshVertexBuffer;
		const uint32 N = PB.GetNumVertices();
		// CPU-копия буферов есть в редакторе (NeedsCPUAccess=true по умолчанию); иначе — ниже из описания.
		if (N > 0 && VB.GetNumVertices() == N && PB.GetVertexData() != nullptr && VB.GetTangentData() != nullptr)
		{
			OutPositions.Reserve(N);
			OutNormals.Reserve(N);
			for (uint32 I = 0; I < N; ++I)
			{
				OutPositions.Add(FVector(PB.VertexPosition(I)));
				const FVector4f TZ = VB.VertexTangentZ(I);
				OutNormals.Add(FVector(TZ.X, TZ.Y, TZ.Z));
			}
			const FColorVertexBuffer& CB = LOD.VertexBuffers.ColorVertexBuffer;
			if (CB.GetNumVertices() == N && CB.GetVertexData() != nullptr)
			{
				OutColors.Reserve(N);
				for (uint32 I = 0; I < N; ++I)
				{
					OutColors.Add(CB.VertexColor(I).ReinterpretAsLinear());   // байт/255, без sRGB-декодирования
				}
			}
			const FIndexArrayView Idx = LOD.IndexBuffer.GetArrayView();
			OutIndices.Reserve(Idx.Num());
			for (int32 I = 0; I < Idx.Num(); ++I)
			{
				OutIndices.Add((int32)Idx[I]);
			}
			bOutFromRenderData = true;
			return true;
		}
	}
#if WITH_EDITOR
	const FMeshDescription* MD = Mesh->GetMeshDescription(0);
	if (MD == nullptr)
	{
		return false;
	}
	FStaticMeshConstAttributes Attributes(*MD);
	TVertexAttributesConstRef<FVector3f> P = Attributes.GetVertexPositions();
	TVertexInstanceAttributesConstRef<FVector3f> Nn = Attributes.GetVertexInstanceNormals();
	for (const FVertexInstanceID VI : MD->VertexInstances().GetElementIDs())
	{
		OutPositions.Add(FVector(P[MD->GetVertexInstanceVertex(VI)]));
		OutNormals.Add(FVector(Nn[VI]));
	}
	for (const FTriangleID Tri : MD->Triangles().GetElementIDs())
	{
		for (const FVertexInstanceID VI : MD->GetTriangleVertexInstances(Tri))
		{
			OutIndices.Add(VI.GetValue());
		}
	}
	return OutPositions.Num() > 0;
#else
	return false;
#endif
}

bool UAsdaMeshBuilder::GetSkeletalMeshLod0Vertices(USkeletalMesh* Mesh, TArray<FVector>& OutPositions, TArray<FVector>& OutNormals, TArray<int32>& OutIndices, bool& bOutRecomputeNormalsSetting)
{
	OutPositions.Reset();
	OutNormals.Reset();
	OutIndices.Reset();
	bOutRecomputeNormalsSetting = false;
	if (Mesh == nullptr)
	{
		return false;
	}
	if (const FSkeletalMeshLODInfo* Info = Mesh->GetLODInfo(0))
	{
		bOutRecomputeNormalsSetting = Info->BuildSettings.bRecomputeNormals != 0;
	}
	const FSkeletalMeshRenderData* RD = Mesh->GetResourceForRendering();
	if (RD == nullptr || RD->LODRenderData.Num() == 0)
	{
		return false;
	}
	const FSkeletalMeshLODRenderData& LOD = RD->LODRenderData[0];
	const FPositionVertexBuffer& PB = LOD.StaticVertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer& VB = LOD.StaticVertexBuffers.StaticMeshVertexBuffer;
	const uint32 N = PB.GetNumVertices();
	if (N == 0 || VB.GetNumVertices() != N || PB.GetVertexData() == nullptr || VB.GetTangentData() == nullptr)
	{
		return false;
	}
	OutPositions.Reserve(N);
	OutNormals.Reserve(N);
	for (uint32 I = 0; I < N; ++I)
	{
		OutPositions.Add(FVector(PB.VertexPosition(I)));
		const FVector4f TZ = VB.VertexTangentZ(I);
		OutNormals.Add(FVector(TZ.X, TZ.Y, TZ.Z));
	}
	if (LOD.MultiSizeIndexContainer.IsIndexBufferValid())
	{
		const FRawStaticIndexBuffer16or32Interface* IB = LOD.MultiSizeIndexContainer.GetIndexBuffer();
		const int32 Num = IB->Num();
		OutIndices.Reserve(Num);
		for (int32 I = 0; I < Num; ++I)
		{
			OutIndices.Add((int32)IB->Get(I));
		}
	}
	return true;
}
