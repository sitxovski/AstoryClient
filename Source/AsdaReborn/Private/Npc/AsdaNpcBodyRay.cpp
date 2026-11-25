// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ 08.11.2026 [ИСПРАВЛЕНИЕ] Луч против коробок тела модели NPC (разбор — в Public/Npc/AsdaNpcBodyRay.h).
//   Коробка листа хранится в пространстве привязки модели; в текущей позе её ставит смешанная матрица трёх костей листа
//   Σ wᵢ · (RefBasesInvᵢ · КостьВКомпонентеᵢ) — тот же линейный скиннинг, что у вершин, с весами, усреднёнными по листу.
#include "Npc/AsdaNpcBodyRay.h"
#include "Engine/SkeletalMesh.h"

namespace AsdaNpcPickBones
{
	// Коробка листа: три кости (индексы в kBones от FModel.FirstBone, −1 — нет), их веса ×255, min/max в пространстве привязки модели
	//   (единицы ассета).
	struct FBoneBox { int16 B0, B1, B2; uint8 W0, W1, W2; int32 MinX, MinY, MinZ, MaxX, MaxY, MaxZ; };
	// Треугольник рыхлого листа (крупные наклонные грани): кости и веса листа, три вершины в пространстве привязки модели.
	struct FBoneTri { int16 B0, B1, B2; uint8 W0, W1, W2; int32 AX, AY, AZ, BX, BY, BZ, CX, CY, CZ; };
	// Модель: имя ассета, коробки (первая, число) в kBoxes, кости в kBones, треугольники в kTris.
	struct FModel { const TCHAR* Model; int32 First; int32 Num; int32 FirstBone; int32 NumBones; int32 FirstTri; int32 NumTri; };
}
#include "AsdaNpcPickBones.inl"   // ★ генератор `asda.Npc.GenPickBones` (AsdaNpcClickPickSelfTest.cpp) — руками не править

namespace AsdaNpcBodyRayLocal
{
	// Кэш модели: строка таблицы и индексы костей меша (−1 — кости в скелете нет).
	struct FCache { const AsdaNpcPickBones::FModel* Row = nullptr; TArray<int32> BoneIdx; };
	static TMap<TWeakObjectPtr<const USkeletalMesh>, FCache> GCache;   // игровой поток

	static const FCache* Find(const USkeletalMeshComponent* Mesh)
	{
		const USkeletalMesh* SK = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
		if (!SK) { return nullptr; }
		if (const FCache* C = GCache.Find(SK)) { return C; }
		FCache& C = GCache.Add(SK);
		const FString Name = SK->GetName();
		for (const AsdaNpcPickBones::FModel& M : AsdaNpcPickBones::kModels)
		{
			if (M.Model && Name.Equals(M.Model, ESearchCase::IgnoreCase)) { C.Row = &M; break; }
		}
		if (C.Row)
		{
			for (int32 i = 0; i < C.Row->NumBones; ++i)
			{
				C.BoneIdx.Add(SK->GetRefSkeleton().FindBoneIndex(FName(AsdaNpcPickBones::kBones[C.Row->FirstBone + i])));
			}
		}
		return &C;
	}
}

bool AsdaNpcBodyRay::RayBones(const USkeletalMeshComponent* Mesh, const FVector& O, const FVector& D, double& OutT, bool& bOutKnown, FName* OutBone)
{
	bOutKnown = false;
	const AsdaNpcBodyRayLocal::FCache* C = AsdaNpcBodyRayLocal::Find(Mesh);
	if (!C || !C->Row) { return false; }
	bOutKnown = true;
	USkeletalMesh* SK = Mesh->GetSkeletalMeshAsset();
	const TArray<FMatrix44f>& Inv = SK->GetRefBasesInvMatrix();
	const TArray<FTransform>& Cs = Mesh->GetComponentSpaceTransforms();
	// Матрица «привязка → мир» каждой кости модели: RefBasesInv · кость в компоненте · компонент в мире.
	const FMatrix ToWorld = Mesh->GetComponentTransform().ToMatrixWithScale();
	TArray<FMatrix, TInlineAllocator<64>> Skin;
	Skin.SetNumUninitialized(C->BoneIdx.Num());
	for (int32 i = 0; i < C->BoneIdx.Num(); ++i)
	{
		const int32 B = C->BoneIdx[i];
		Skin[i] = (Inv.IsValidIndex(B) && Cs.IsValidIndex(B)) ? FMatrix(Inv[B]) * Cs[B].ToMatrixWithScale() * ToWorld : FMatrix(EForceInit::ForceInitToZero);
	}
	const FVector End = O + D * 100000.0;
	// Смешанная матрица листа: сумма wi * Skin[bi].
	auto Blend = [&Skin](int32 B0, int32 B1, int32 B2, uint8 W0, uint8 W1, uint8 W2)
	{
		FMatrix M(EForceInit::ForceInitToZero);
		const int32 Bi[3] = { B0, B1, B2 };
		const float Ws = FMath::Max(1.f, (float)W0 + W1 + W2);
		const float Wi[3] = { W0 / Ws, W1 / Ws, W2 / Ws };
		for (int32 k = 0; k < 3; ++k)
		{
			if (Bi[k] < 0 || !Skin.IsValidIndex(Bi[k]) || Wi[k] <= 0.f) { continue; }
			const FMatrix& S = Skin[Bi[k]];
			for (int32 r = 0; r < 4; ++r) { for (int32 c = 0; c < 4; ++c) { M.M[r][c] += S.M[r][c] * Wi[k]; } }
		}
		return M;
	};
	bool bAny = false;
	// Треугольники рыхлых листьев: вершины смешанной матрицей в мир, точное пересечение (Мёллер–Трумбор, обе стороны).
	for (int32 i = 0; i < C->Row->NumTri; ++i)
	{
		const AsdaNpcPickBones::FBoneTri& R = AsdaNpcPickBones::kTris[C->Row->FirstTri + i];
		const FMatrix M = Blend(R.B0, R.B1, R.B2, R.W0, R.W1, R.W2);
		const FVector A = M.TransformPosition(FVector(R.AX, R.AY, R.AZ)), Bv = M.TransformPosition(FVector(R.BX, R.BY, R.BZ)), Cv = M.TransformPosition(FVector(R.CX, R.CY, R.CZ));
		const FVector E1 = Bv - A, E2 = Cv - A, Pv = FVector::CrossProduct(D, E2);
		const double Det = FVector::DotProduct(E1, Pv);
		if (FMath::Abs(Det) < 1e-12) { continue; }
		const double InvDet = 1.0 / Det;
		const FVector Tv = O - A;
		const double U = FVector::DotProduct(Tv, Pv) * InvDet;
		if (U < 0.0 || U > 1.0) { continue; }
		const FVector Qv = FVector::CrossProduct(Tv, E1);
		const double V = FVector::DotProduct(D, Qv) * InvDet;
		if (V < 0.0 || U + V > 1.0) { continue; }
		const double T = FVector::DotProduct(E2, Qv) * InvDet;
		if (T > 0.0 && (!bAny || T < OutT))
		{
			OutT = T;
			bAny = true;
			if (OutBone && R.B0 >= 0) { *OutBone = FName(AsdaNpcPickBones::kBones[C->Row->FirstBone + R.B0]); }
		}
	}
	for (int32 i = 0; i < C->Row->Num; ++i)
	{
		const AsdaNpcPickBones::FBoneBox& B = AsdaNpcPickBones::kBoxes[C->Row->First + i];
		const FMatrix M = Blend(B.B0, B.B1, B.B2, B.W0, B.W1, B.W2);
		if (FMath::Abs(M.Determinant()) < 1e-18) { continue; }
		const FMatrix MI = M.Inverse();
		const FVector Lo = MI.TransformPosition(O), Le = MI.TransformPosition(End), Ld = Le - Lo;
		const FVector Mn(B.MinX, B.MinY, B.MinZ), Mx(B.MaxX, B.MaxY, B.MaxZ);
		double T0 = 0.0, T1 = 1.0;
		bool bHit = true;
		for (int32 a = 0; a < 3 && bHit; ++a)
		{
			if (FMath::Abs(Ld[a]) < 1e-12) { bHit = Lo[a] >= Mn[a] && Lo[a] <= Mx[a]; continue; }
			double Ta = (Mn[a] - Lo[a]) / Ld[a], Tb = (Mx[a] - Lo[a]) / Ld[a];
			if (Ta > Tb) { Swap(Ta, Tb); }
			T0 = FMath::Max(T0, Ta); T1 = FMath::Min(T1, Tb);
			bHit = T0 <= T1;
		}
		if (!bHit) { continue; }
		const double T = T0 * 100000.0;
		if (!bAny || T < OutT)
		{
			OutT = T;
			bAny = true;
			if (OutBone && B.B0 >= 0) { *OutBone = FName(AsdaNpcPickBones::kBones[C->Row->FirstBone + B.B0]); }
		}
	}
	return bAny;
}
