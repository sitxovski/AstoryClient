// Copyright (c) 2026 @sitxovski (Discord)
// AsdaAlphaSplitLib — отделение бит-подсеток .jbm в собственный слот материала (31.10.2026, баг 8). Разбор — в заголовке.
#include "Editor/AsdaAlphaSplitLib.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "StaticMeshResources.h"
#include "Algo/Sort.h"
#include "AssetCompilingManager.h"
#include "DistanceFieldAtlas.h"
#include "Editor/AsdaMeshMaintenance.h"

namespace AsdaAlphaSplit
{
	// Квант UV: 1e-4 (как округление данных .jbm).
	constexpr double kQ = 10000.0;

	// Квант координаты: основной и (если координата у середины кванта) соседний — защита от расхождения float/double.
	static void Cands(double X, TArray<int32, TInlineAllocator<2>>& Out)
	{
		const double S = X * kQ;
		const int32 A = FMath::FloorToInt32(S + 0.5);
		Out.Add(A);
		const double F = S - FMath::FloorToDouble(S);
		if (FMath::Abs(F - 0.5) < 0.05) { Out.Add(F < 0.5 ? A + 1 : A - 1); }
	}

	// Ключ треугольника из трёх квантованных углов (углы сортируются).
	static FString Key(FIntPoint C0, FIntPoint C1, FIntPoint C2)
	{
		FIntPoint C[3] = { C0, C1, C2 };
		Algo::Sort(C, [](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
		return FString::Printf(TEXT("%d,%d;%d,%d;%d,%d"), C[0].X, C[0].Y, C[1].X, C[1].Y, C[2].X, C[2].Y);
	}

	// Ключ треугольника .jbm (без вариантов).
	static FString JbmKey(const float* UV)
	{
		auto Q = [](float X) { return FMath::FloorToInt32((double)X * kQ + 0.5); };
		return Key(FIntPoint(Q(UV[0]), Q(UV[1])), FIntPoint(Q(UV[2]), Q(UV[3])), FIntPoint(Q(UV[4]), Q(UV[5])));
	}

	// Все варианты ключа треугольника ассета (UV углов уже с учётом оси V).
	static void AssetKeys(const FVector2D UV[3], TArray<FString>& Out)
	{
		TArray<int32, TInlineAllocator<2>> CU[3], CV[3];
		for (int32 i = 0; i < 3; ++i) { Cands(UV[i].X, CU[i]); Cands(UV[i].Y, CV[i]); }
		for (int32 a : CU[0]) for (int32 b : CV[0]) for (int32 c : CU[1]) for (int32 d : CV[1]) for (int32 e : CU[2]) for (int32 f : CV[2])
		{
			Out.AddUnique(Key(FIntPoint(a, b), FIntPoint(c, d), FIntPoint(e, f)));
		}
	}

	// Запись .jbm под ключом: группа (1 — бит) и X центра треугольника (модель .jbm).
	struct FJbmTri { uint8 Bit = 0; float X = 0.f; bool bUsed = false; };

	// Итог сопоставления при одной ориентации оси V.
	struct FMatch
	{
		TMap<FTriangleID, uint8> Group;   // треугольник ассета → 1 (бит) / 0 (без бита)
		int32 Unmatched = 0;              // треугольников ассета без пары в .jbm
		int32 LeftJbm = 0;                // треугольников .jbm без пары в ассете (вырожденные, выброшенные сборкой)
		int32 Bit = 0, NoBit = 0;         // сопоставлено по группам
		int32 Ambiguous = 0;              // разобрано по позиции (ключ UV у треугольников обеих групп)
		double FitR = 0.0, FitK = 0.0;    // регрессия оси ассета к X .jbm: корреляция и масштаб
		int32 FitAxis = 0;                // ось ассета (0 X, 1 Y, 2 Z)
		FString AxisNote;                 // ось ассета, сопоставленная с X .jbm (для отчёта)
	};

	// Центр треугольника ассета.
	static FVector3f TriCenter(const FMeshDescription& MD, FTriangleID T)
	{
		TVertexAttributesConstRef<FVector3f> Pos = MD.VertexAttributes().GetAttributesRef<FVector3f>(MeshAttribute::Vertex::Position);
		TArrayView<const FVertexInstanceID> VI = MD.GetTriangleVertexInstances(T);
		FVector3f C = FVector3f::ZeroVector;
		for (int32 i = 0; i < 3; ++i) { C += Pos.Get(MD.GetVertexInstanceVertex(VI[i])); }
		return C / 3.f;
	}

	// Сопоставить треугольники ассета с треугольниками .jbm.
	//   1) Ключ, у всех записей которого одна группа, — группа известна сразу.
	//   2) Ключ с записями ОБЕИХ групп (зеркальные треугольники с общей UV) — по позиции: ось ассета и масштаб к X .jbm
	//      находятся линейной регрессией по однозначным треугольникам (ключ с единственной записью), затем каждому такому
	//      треугольнику ассета — свободная запись с ближайшим X.
	//   Groups (★ 01.11.2026, mob_fx_audit) — если задан, сопоставляются только треугольники этих групп полигонов (один слот
	//   многослотового меша); nullptr — все треугольники (прежнее поведение).
	static FMatch Match(const FMeshDescription& MD, const TArray<float>& TriUV, const TArray<uint8>& TriBit, const TArray<float>& TriX,
		bool bFlipV, const TSet<FPolygonGroupID>* Groups = nullptr)
	{
		FMatch R;
		TMap<FString, TArray<FJbmTri>> Queue;
		const int32 NT = TriBit.Num();
		for (int32 t = 0; t < NT; ++t)
		{
			FJbmTri E; E.Bit = TriBit[t]; E.X = TriX.IsValidIndex(t) ? TriX[t] : 0.f;
			Queue.FindOrAdd(JbmKey(TriUV.GetData() + t * 6)).Add(E);
		}
		TVertexInstanceAttributesConstRef<FVector2f> UVs =
			MD.VertexInstanceAttributes().GetAttributesRef<FVector2f>(MeshAttribute::VertexInstance::TextureCoordinate);
		TArray<FString> Keys;
		TArray<TPair<FTriangleID, FString>> Deferred;   // треугольники с ключом обеих групп
		TArray<FVector3f> FitUe;                        // центры однозначных треугольников ассета
		TArray<float> FitX;                             // X .jbm тех же треугольников
		for (const FTriangleID T : MD.Triangles().GetElementIDs())
		{
			if (Groups && !Groups->Contains(MD.GetTrianglePolygonGroup(T))) { continue; }   // чужой слот
			TArrayView<const FVertexInstanceID> VI = MD.GetTriangleVertexInstances(T);
			FVector2D UV[3];
			for (int32 i = 0; i < 3; ++i)
			{
				const FVector2f P = UVs.Get(VI[i], 0);
				UV[i] = FVector2D(P.X, bFlipV ? 1.0 - (double)P.Y : (double)P.Y);
			}
			Keys.Reset();
			AssetKeys(UV, Keys);
			bool bHit = false;
			for (const FString& K : Keys)
			{
				TArray<FJbmTri>* Q = Queue.Find(K);
				if (!Q) { continue; }
				int32 Free = INDEX_NONE;   // первая свободная запись
				bool bMixed = false;       // у ключа записи обеих групп
				for (int32 j = 0; j < Q->Num(); ++j)
				{
					if (!(*Q)[j].bUsed && Free == INDEX_NONE) { Free = j; }
					bMixed |= (*Q)[j].Bit != (*Q)[0].Bit;
				}
				if (Free == INDEX_NONE) { continue; }
				bHit = true;
				if (bMixed) { Deferred.Add(TPair<FTriangleID, FString>(T, K)); break; }
				(*Q)[Free].bUsed = true;
				R.Group.Add(T, (*Q)[Free].Bit);
				if (Q->Num() == 1) { FitUe.Add(TriCenter(MD, T)); FitX.Add((*Q)[Free].X); }
				break;
			}
			if (!bHit) { ++R.Unmatched; }
		}
		{
			// Ось ассета с наибольшей |корреляцией| к X .jbm и прямая X = (ue − B) / A (считается всегда — для отчёта).
			int32 BestAx = 0; double BestR = 0.0, A = 1.0, B = 0.0;
			const int32 N = FitX.Num();
			for (int32 ax = 0; ax < 3 && N > 2; ++ax)
			{
				double Sx = 0, Sy = 0, Sxx = 0, Syy = 0, Sxy = 0;
				for (int32 i = 0; i < N; ++i)
				{
					const double x = FitX[i], y = FitUe[i][ax];
					Sx += x; Sy += y; Sxx += x * x; Syy += y * y; Sxy += x * y;
				}
				const double Cxy = Sxy - Sx * Sy / N, Cxx = Sxx - Sx * Sx / N, Cyy = Syy - Sy * Sy / N;
				const double Rr = (Cxx > 0 && Cyy > 0) ? Cxy / FMath::Sqrt(Cxx * Cyy) : 0.0;
				if (FMath::Abs(Rr) > FMath::Abs(BestR)) { BestR = Rr; BestAx = ax; A = Cxy / Cxx; B = (Sy - A * Sx) / N; }
			}
			R.AxisNote = FString::Printf(TEXT("ось ассета %c ~ X .jbm: r=%.4f, k=%.4f, однозначных %d"), TEXT("XYZ")[BestAx], BestR, A, N);
			R.FitR = BestR; R.FitAxis = BestAx; R.FitK = A;
			if (FMath::Abs(BestR) < 0.99)
			{
				// Зеркальная модель: почти все ключи UV парные, регрессии не на чем строиться. Соглашение экспорта частей
				//   (у всех 70 мешей с надёжной регрессией |r| ≥ 0.99 — 64 скелетных и 6 статических, сверка 01.11.2026,
				//   Reports/bug_chest_invisible/split_dry.json): ось ассета X = 100 × X .jbm.
				BestAx = 0; A = 100.0; B = 0.0;
				R.AxisNote += TEXT(" → соглашение экспорта X×100");
			}
			for (const TPair<FTriangleID, FString>& D : Deferred)
			{
				TArray<FJbmTri>& Q = Queue[D.Value];
				const double Xp = FMath::IsNearlyZero(A) ? 0.0 : (TriCenter(MD, D.Key)[BestAx] - B) / A;   // X в модели .jbm
				int32 Best = INDEX_NONE;
				for (int32 j = 0; j < Q.Num(); ++j)
				{
					if (!Q[j].bUsed && (Best == INDEX_NONE || FMath::Abs(Q[j].X - Xp) < FMath::Abs(Q[Best].X - Xp))) { Best = j; }
				}
				if (Best == INDEX_NONE) { ++R.Unmatched; continue; }
				Q[Best].bUsed = true;
				R.Group.Add(D.Key, Q[Best].Bit);
				++R.Ambiguous;
			}
		}
		int32 Used = 0;
		for (const TPair<FString, TArray<FJbmTri>>& Q : Queue) { for (const FJbmTri& E : Q.Value) { Used += E.bUsed ? 1 : 0; } }
		for (const TPair<FTriangleID, uint8>& G : R.Group) { (G.Value ? R.Bit : R.NoBit) += 1; }
		R.LeftJbm = NT - Used;
		return R;
	}

	// Строка секций рендер-данных LOD0 «[слот i, треуг. n]…».
	static FString SectionsOf(USkeletalMesh* SK, UStaticMesh* SM)
	{
		FString Sec;
		if (SK)
		{
			if (const FSkeletalMeshRenderData* RD = SK->GetResourceForRendering(); RD && RD->LODRenderData.Num() > 0)
			{
				for (const FSkelMeshRenderSection& S : RD->LODRenderData[0].RenderSections)
				{
					Sec += FString::Printf(TEXT(" [слот %d, треуг. %d]"), S.MaterialIndex, S.NumTriangles);
				}
			}
		}
		else if (const FStaticMeshRenderData* RD = SM->GetRenderData(); RD && RD->LODResources.Num() > 0)
		{
			for (const FStaticMeshSection& S : RD->LODResources[0].Sections)
			{
				Sec += FString::Printf(TEXT(" [слот %d, треуг. %d]"), S.MaterialIndex, S.NumTriangles);
			}
		}
		return Sec;
	}
}

FString UAsdaAlphaSplitLib::SplitBitSubsets(UObject* MeshAsset, const TArray<float>& TriUV, const TArray<uint8>& TriBit,
	const TArray<float>& TriX, const FString& Suffix, bool bApply)
{
	using namespace AsdaAlphaSplit;
	USkeletalMesh* SK = Cast<USkeletalMesh>(MeshAsset);
	UStaticMesh* SM = Cast<UStaticMesh>(MeshAsset);
	if (!SK && !SM) { return TEXT("FAIL не меш"); }
	if (TriUV.Num() != TriBit.Num() * 6 || TriBit.Num() == 0 || TriX.Num() != TriBit.Num())
	{
		return TEXT("FAIL данные .jbm: длины не сходятся");
	}
	if (Suffix.IsEmpty()) { return TEXT("FAIL пустой хвост слота"); }
	const int32 NumMats = SK ? SK->GetMaterials().Num() : SM->GetStaticMaterials().Num();
	// Уже разделён (повторный прогон): слот с хвостом есть — сверка текущего разделения с .jbm (и правка при bApply).
	int32 BitMat = INDEX_NONE;   // индекс слота бит-подсеток
	for (int32 m = 0; m < NumMats; ++m)
	{
		const FName N = SK ? SK->GetMaterials()[m].MaterialSlotName : SM->GetStaticMaterials()[m].MaterialSlotName;
		if (N.ToString().EndsWith(Suffix)) { BitMat = m; }
	}
	const bool bAlready = BitMat != INDEX_NONE;
	if (!bAlready && NumMats != 1) { return FString::Printf(TEXT("FAIL слотов материала %d (ждём 1)"), NumMats); }
	FMeshDescription* MD = SK ? SK->GetMeshDescription(0) : SM->GetMeshDescription(0);
	if (!MD) { return TEXT("FAIL нет MeshDescription LOD0"); }
	// Статический меш (оружие) с несколькими группами: подсетки уже лежат отдельными секциями — меняется только слот секций.
	const bool bSectionsOnly = SM && MD->PolygonGroups().Num() > 1;
	const int32 WantGroups = bAlready ? 2 : 1;
	if (!bSectionsOnly && MD->PolygonGroups().Num() != WantGroups)
	{
		return FString::Printf(TEXT("FAIL групп полигонов %d (ждём %d)"), MD->PolygonGroups().Num(), WantGroups);
	}

	// Сопоставление при прямой и перевёрнутой оси V — берём полную.
	FMatch M0 = Match(*MD, TriUV, TriBit, TriX, false);
	FMatch M1 = Match(*MD, TriUV, TriBit, TriX, true);
	const bool bFlip = M1.Unmatched < M0.Unmatched;
	FMatch& M = bFlip ? M1 : M0;
	const int32 NumTri = MD->Triangles().Num();
	FString Rep = FString::Printf(TEXT("ось V %s, треуг. ассета %d: бит %d, без бита %d, без пары %d; .jbm без пары %d; по позиции %d%s%s"),
		bFlip ? TEXT("перевёрнута") : TEXT("прямая"), NumTri, M.Bit, M.NoBit, M.Unmatched, M.LeftJbm, M.Ambiguous,
		M.AxisNote.IsEmpty() ? TEXT("") : TEXT(" ("), M.AxisNote.IsEmpty() ? TEXT("") : *(M.AxisNote + TEXT(")")));
	if (M.Unmatched != 0) { return TEXT("FAIL ") + Rep; }
	if (M.Bit == 0 || M.NoBit == 0) { return TEXT("FAIL нечего делить: ") + Rep; }
	// Полигон из нескольких треугольников не должен попасть в обе группы.
	for (const FPolygonID P : MD->Polygons().GetElementIDs())
	{
		TArrayView<const FTriangleID> Tris = MD->GetPolygonTriangles(P);
		for (const FTriangleID T : Tris)
		{
			if (M.Group[T] != M.Group[Tris[0]]) { return TEXT("FAIL полигон с треугольниками обеих групп: ") + Rep; }
		}
	}
	if (bSectionsOnly)
	{
		// Секция = непустая группа полигонов в порядке групп (StaticMeshBuilder.cpp:1619). Каждая группа — целиком одной
		//   стороны; число и размер секций сверяются с рендер-данными.
		TArray<uint8> SecBit;     // секция → 1 (бит) / 0
		TArray<int32> SecTris;    // секция → треугольников
		for (const FPolygonGroupID G : MD->PolygonGroups().GetElementIDs())
		{
			TArrayView<const FTriangleID> GT = MD->GetPolygonGroupTriangles(G);
			if (GT.Num() == 0) { continue; }
			const uint8 B = M.Group[GT[0]];
			for (const FTriangleID T : GT)
			{
				if (M.Group[T] != B) { return TEXT("FAIL группа полигонов с треугольниками обеих сторон: ") + Rep; }
			}
			SecBit.Add(B);
			SecTris.Add(GT.Num());
		}
		const FStaticMeshRenderData* RD0 = SM->GetRenderData();
		if (!RD0 || RD0->LODResources.Num() == 0 || RD0->LODResources[0].Sections.Num() != SecBit.Num())
		{
			return FString::Printf(TEXT("FAIL секций в рендер-данных не %d: "), SecBit.Num()) + Rep;
		}
		FString Map;
		int32 Diff = 0;   // секций, чей слот расходится с .jbm (для уже разделённого)
		for (int32 i = 0; i < SecBit.Num(); ++i)
		{
			if ((int32)RD0->LODResources[0].Sections[i].NumTriangles != SecTris[i])
			{
				return FString::Printf(TEXT("FAIL секция %d: треуг. %d, у группы %d: "), i, RD0->LODResources[0].Sections[i].NumTriangles, SecTris[i]) + Rep;
			}
			Map += FString::Printf(TEXT(" %d:%s"), i, SecBit[i] ? TEXT("бит") : TEXT("опак"));
			if (bAlready) { Diff += (SM->GetSectionInfoMap().Get(0, i).MaterialIndex == BitMat) != (SecBit[i] != 0) ? 1 : 0; }
		}
		Rep += TEXT("; секции") + Map;
		if (bAlready && (Diff == 0 || !bApply))
		{
			return FString::Printf(TEXT("OK уже разделён: расхождений секций с .jbm %d; %s; секции:%s"), Diff, *Rep, *SectionsOf(SK, SM));
		}
		if (!bApply) { return TEXT("OK сверка: ") + Rep; }
		SM->Modify();
		if (!bAlready)
		{
			const FStaticMaterial Slot0 = SM->GetStaticMaterials()[0];
			SM->GetStaticMaterials().Add(FStaticMaterial(Slot0.MaterialInterface, FName(*(Slot0.MaterialSlotName.ToString() + Suffix)),
				FName(*(Slot0.ImportedMaterialSlotName.ToString() + Suffix))));
			BitMat = 1;
		}
		for (int32 i = 0; i < SecBit.Num(); ++i)
		{
			FMeshSectionInfo Info = SM->GetSectionInfoMap().Get(0, i);
			Info.MaterialIndex = SecBit[i] ? BitMat : 0;
			SM->GetSectionInfoMap().Set(0, i, Info);
		}
		SM->Build(/*bInSilent*/true);
		SM->PostEditChange();
		const int64 DF = FinishDistanceField(SM);   // поле расстояний достроено до сохранения
		return FString::Printf(TEXT("OK применено (секции): %s; секции:%s; поле расстояний %lld б"), *Rep, *SectionsOf(SK, SM), DF);
	}

	// ═══ Одна группа (или две у уже разделённого): группа бит-подсеток со слотом «<слот0><Suffix>» ═══
	TPolygonGroupAttributesRef<FName> SlotNames =
		MD->PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
	FPolygonGroupID G0 = FPolygonGroupID(INDEX_NONE), GB = FPolygonGroupID(INDEX_NONE);   // группа непрозрачных и бит-подсеток
	for (const FPolygonGroupID G : MD->PolygonGroups().GetElementIDs())
	{
		const bool bBitGroup = bAlready && SlotNames.IsValid() && SlotNames.Get(G).ToString().EndsWith(Suffix);
		(bBitGroup ? GB : G0) = G;
	}
	if (bAlready && (G0 == FPolygonGroupID(INDEX_NONE) || GB == FPolygonGroupID(INDEX_NONE))) { return TEXT("FAIL уже разделён, но группы бит-подсеток нет: ") + Rep; }
	// Полигоны не в своей группе (у неразделённого — все бит-полигоны).
	TArray<FPolygonID> Move;
	for (const FPolygonID P : MD->Polygons().GetElementIDs())
	{
		const bool bBit = M.Group[MD->GetPolygonTriangles(P)[0]] != 0;
		const FPolygonGroupID Cur = MD->GetPolygonPolygonGroup(P);
		if (bAlready ? (Cur != (bBit ? GB : G0)) : bBit) { Move.Add(P); }
	}
	if (bAlready && (Move.Num() == 0 || !bApply))
	{
		return FString::Printf(TEXT("OK уже разделён: полигонов не в своей группе %d; %s; секции:%s"), Move.Num(), *Rep, *SectionsOf(SK, SM));
	}
	if (!bApply) { return TEXT("OK сверка: ") + Rep; }

	const FName ImpSlot0 = SlotNames.IsValid() ? SlotNames.Get(G0) : NAME_None;
	const FName ImpSlotBit(*(ImpSlot0.ToString() + Suffix));
	MeshAsset->Modify();
	if (SK) { SK->ModifyMeshDescription(0); }
	if (!bAlready)
	{
		GB = MD->CreatePolygonGroup();
		if (SlotNames.IsValid()) { SlotNames.Set(GB, ImpSlotBit); }
	}
	for (const FPolygonID P : Move)
	{
		MD->SetPolygonPolygonGroup(P, MD->GetPolygonPolygonGroup(P) == GB ? G0 : GB);
	}
	if (SK)
	{
		if (!bAlready)
		{
			const FSkeletalMaterial Slot0 = SK->GetMaterials()[0];
			SK->GetMaterials().Add(FSkeletalMaterial(Slot0.MaterialInterface, FName(*(Slot0.MaterialSlotName.ToString() + Suffix)), ImpSlotBit));
			if (FSkeletalMeshLODInfo* LI = SK->GetLODInfo(0))
			{
				if (LI->LODMaterialMap.Num() > 0) { LI->LODMaterialMap.SetNum(2); LI->LODMaterialMap[1] = INDEX_NONE; }
			}
		}
		SK->CommitMeshDescription(0);
		SK->Build();
		SK->PostEditChange();
	}
	else
	{
		if (!bAlready)
		{
			const FStaticMaterial Slot0 = SM->GetStaticMaterials()[0];
			SM->GetStaticMaterials().Add(FStaticMaterial(Slot0.MaterialInterface, FName(*(Slot0.MaterialSlotName.ToString() + Suffix)), ImpSlotBit));
			SM->GetSectionInfoMap().Set(0, 0, FMeshSectionInfo(0));
			SM->GetSectionInfoMap().Set(0, 1, FMeshSectionInfo(1));
		}
		SM->CommitMeshDescription(0);
		SM->Build(/*bInSilent*/true);
		SM->PostEditChange();
		FinishDistanceField(SM);   // поле расстояний достроено до сохранения
	}
	return FString::Printf(TEXT("OK применено%s: перенесено полигонов %d; %s; секции:%s"), bAlready ? TEXT(" (исправление разделения)") : TEXT(""),
		Move.Num(), *Rep, *SectionsOf(SK, SM));
}

// ★ 01.11.2026 (mob_fx_audit) — разделение ВНУТРИ одного слота многослотового меша моба/NPC. Разбор — в заголовке.
FString UAsdaAlphaSplitLib::SplitSlotSubsets(UObject* MeshAsset, FName SlotName, const TArray<float>& TriUV, const TArray<uint8>& TriBit,
	const TArray<float>& TriX, const FString& Suffix, bool bApply)
{
	using namespace AsdaAlphaSplit;
	USkeletalMesh* SK = Cast<USkeletalMesh>(MeshAsset);
	UStaticMesh* SM = Cast<UStaticMesh>(MeshAsset);
	if (!SK && !SM) { return TEXT("FAIL не меш"); }
	if (TriUV.Num() != TriBit.Num() * 6 || TriBit.Num() == 0 || TriX.Num() != TriBit.Num())
	{
		return TEXT("FAIL данные .jbm: длины не сходятся");
	}
	if (Suffix.IsEmpty()) { return TEXT("FAIL пустой хвост слота"); }
	// Слот-источник и (у уже разделённого) слот эффектов: индекс и импортное имя (по нему связаны группы полигонов).
	const FName FxSlotName(*(SlotName.ToString() + Suffix));
	const int32 NumMats = SK ? SK->GetMaterials().Num() : SM->GetStaticMaterials().Num();
	int32 SrcMat = INDEX_NONE, FxMat = INDEX_NONE;   // индексы слотов источника и эффектов
	FName SrcImp, FxImp;                             // их импортные имена
	for (int32 m = 0; m < NumMats; ++m)
	{
		const FName N = SK ? SK->GetMaterials()[m].MaterialSlotName : SM->GetStaticMaterials()[m].MaterialSlotName;
		const FName I = SK ? SK->GetMaterials()[m].ImportedMaterialSlotName : SM->GetStaticMaterials()[m].ImportedMaterialSlotName;
		if (N == SlotName) { SrcMat = m; SrcImp = I; }
		if (N == FxSlotName) { FxMat = m; FxImp = I; }
	}
	if (SrcMat == INDEX_NONE) { return FString::Printf(TEXT("FAIL нет слота %s"), *SlotName.ToString()); }
	const bool bAlready = FxMat != INDEX_NONE;
	if (!bAlready) { FxImp = FName(*(SrcImp.ToString() + Suffix)); }
	FMeshDescription* MD = SK ? SK->GetMeshDescription(0) : SM->GetMeshDescription(0);
	if (!MD) { return TEXT("FAIL нет MeshDescription LOD0"); }
	TPolygonGroupAttributesRef<FName> GroupSlot =
		MD->PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
	if (!GroupSlot.IsValid()) { return TEXT("FAIL у групп полигонов нет имён слотов"); }
	// Группы полигонов слота-источника (импортное имя совпадает) и группа эффектов (если уже есть).
	TSet<FPolygonGroupID> Groups;
	FPolygonGroupID GS = FPolygonGroupID(INDEX_NONE), GF = FPolygonGroupID(INDEX_NONE);
	for (const FPolygonGroupID G : MD->PolygonGroups().GetElementIDs())
	{
		const FName N = GroupSlot.Get(G);
		if (N == SrcImp) { Groups.Add(G); if (GS == FPolygonGroupID(INDEX_NONE)) { GS = G; } }
		else if (N == FxImp) { Groups.Add(G); GF = G; }
	}
	if (GS == FPolygonGroupID(INDEX_NONE)) { return FString::Printf(TEXT("FAIL нет группы полигонов слота %s (импорт %s)"), *SlotName.ToString(), *SrcImp.ToString()); }
	if (bAlready && GF == FPolygonGroupID(INDEX_NONE)) { return TEXT("FAIL слот эффектов есть, а группы полигонов нет"); }

	FMatch M0 = Match(*MD, TriUV, TriBit, TriX, false, &Groups);
	FMatch M1 = Match(*MD, TriUV, TriBit, TriX, true, &Groups);
	const bool bFlip = M1.Unmatched < M0.Unmatched;
	FMatch& M = bFlip ? M1 : M0;
	FString Rep = FString::Printf(TEXT("слот %s: ось V %s, эффект %d, остаётся %d, без пары %d; .jbm без пары %d; по позиции %d (%s)"),
		*SlotName.ToString(), bFlip ? TEXT("перевёрнута") : TEXT("прямая"), M.Bit, M.NoBit, M.Unmatched, M.LeftJbm, M.Ambiguous, *M.AxisNote);
	if (M.Unmatched != 0) { return TEXT("FAIL ") + Rep; }
	if (M.Bit == 0 || M.NoBit == 0) { return TEXT("FAIL нечего делить: ") + Rep; }
	// Полигоны не в своей группе (у неразделённого — все полигоны эффектов).
	TArray<FPolygonID> Move;
	for (const FPolygonID P : MD->Polygons().GetElementIDs())
	{
		const FPolygonGroupID Cur = MD->GetPolygonPolygonGroup(P);
		if (!Groups.Contains(Cur)) { continue; }
		TArrayView<const FTriangleID> Tris = MD->GetPolygonTriangles(P);
		for (const FTriangleID T : Tris)
		{
			if (M.Group[T] != M.Group[Tris[0]]) { return TEXT("FAIL полигон с треугольниками обеих групп: ") + Rep; }
		}
		const bool bFx = M.Group[Tris[0]] != 0;
		// ★ У статического меша на один слот бывает НЕСКОЛЬКО групп полигонов (группа на подсетку): «своя» для остающегося
		//   полигона — любая группа слота-источника, а не только первая (иначе повтор переносил остающиеся в слот эффектов).
		if (bAlready ? ((Cur == GF) != bFx) : bFx) { Move.Add(P); }
	}
	if (bAlready && (Move.Num() == 0 || !bApply))
	{
		return FString::Printf(TEXT("OK уже разделён: полигонов не в своей группе %d; %s; секции:%s"), Move.Num(), *Rep, *SectionsOf(SK, SM));
	}
	if (!bApply) { return TEXT("OK сверка: ") + Rep; }

	MeshAsset->Modify();
	if (SK) { SK->ModifyMeshDescription(0); }
	if (!bAlready)
	{
		GF = MD->CreatePolygonGroup();
		GroupSlot.Set(GF, FxImp);
	}
	for (const FPolygonID P : Move)
	{
		MD->SetPolygonPolygonGroup(P, MD->GetPolygonPolygonGroup(P) == GF ? GS : GF);
	}
	if (SK)
	{
		if (!bAlready)
		{
			const FSkeletalMaterial Src = SK->GetMaterials()[SrcMat];
			SK->GetMaterials().Add(FSkeletalMaterial(Src.MaterialInterface, FxSlotName, FxImp));
		}
		SK->CommitMeshDescription(0);
		SK->Build();
		SK->PostEditChange();
	}
	else
	{
		if (!bAlready)
		{
			const FStaticMaterial Src = SM->GetStaticMaterials()[SrcMat];
			SM->GetStaticMaterials().Add(FStaticMaterial(Src.MaterialInterface, FxSlotName, FxImp));
		}
		SM->CommitMeshDescription(0);
		// Секция = непустая группа полигонов в порядке групп (StaticMeshBuilder.cpp:1619): слот каждой секции — по импортному
		//   имени её группы (новая группа последняя, но пересчитываем все — порядок прежних секций не меняется).
		int32 Sec = 0;
		for (const FPolygonGroupID G : MD->PolygonGroups().GetElementIDs())
		{
			if (MD->GetPolygonGroupTriangles(G).Num() == 0) { continue; }
			const int32 Mi = SM->GetMaterialIndexFromImportedMaterialSlotName(GroupSlot.Get(G));
			if (Mi != INDEX_NONE)
			{
				FMeshSectionInfo Info = SM->GetSectionInfoMap().Get(0, Sec);
				Info.MaterialIndex = Mi;
				SM->GetSectionInfoMap().Set(0, Sec, Info);
			}
			++Sec;
		}
		SM->Build(/*bInSilent*/true);
		SM->PostEditChange();
		FinishDistanceField(SM);   // поле расстояний достроено до сохранения
	}
	return FString::Printf(TEXT("OK применено%s: перенесено полигонов %d; %s; секции:%s"), bAlready ? TEXT(" (исправление разделения)") : TEXT(""),
		Move.Num(), *Rep, *SectionsOf(SK, SM));
}

int64 UAsdaAlphaSplitLib::FinishDistanceField(UStaticMesh* Mesh)
{
	if (!Mesh) { return -1; }
	FAssetCompilingManager::Get().FinishAllCompilation();   // сборка рендер-данных (асинхронная компиляция меша)
	// В коммандлете задачи DF завершаются только через ProcessAsyncTasks на игровом потоке — штатное ожидание проекта (сет E-1).
	UAsdaMeshMaintenance::WaitForDistanceFieldBuilds(600.f);
	FAssetCompilingManager::Get().FinishAllCompilation();
	// ★ Размер 528 б = пустое поле: у мешей оружия персонажа (Models/Char) DistanceFieldResolutionScale = 0 (сет E-2 — DF
	//   у меша не строится), в т. ч. у нетронутых; прежний тег 2–3 МБ — устаревший (сохранение 06.09 до выключателя).
	const FStaticMeshRenderData* RD = Mesh->GetRenderData();
	if (!RD || RD->LODResources.Num() == 0 || !RD->LODResources[0].DistanceFieldData) { return -1; }
	const FDistanceFieldVolumeData& V = *RD->LODResources[0].DistanceFieldData;
	return (int64)V.GetResourceSizeBytes() + (int64)V.StreamableMips.GetBulkDataSize();   // как тег DistanceFieldSize (StaticMesh.cpp:6300)
}
