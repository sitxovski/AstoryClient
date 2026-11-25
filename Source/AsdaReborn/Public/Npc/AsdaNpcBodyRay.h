// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ 08.11.2026 [ИСПРАВЛЕНИЕ] ЛУЧ ПРОТИВ ТЕЛА NPC в ТЕКУЩЕЙ позе — для спорного щелчка (разбор и ключ — `AsdaClickResolveNpcOverlap`
//   в AsdaPlayerController.cpp, ключ `asda.Npc.ClickByBody`): побеждает NPC, чьё тело луч задевает первым.
//   ТЕЛО = плотные коробки КОСТЕЙ модели (`RayBones`): для каждой кости — коробка в её системе привязки по вершинам, у которых эта
//     кость с наибольшим весом; в игре коробка едет с костью (`GetBoneTransform`). Таблица — `AsdaNpcPickBones.inl`, её пишет
//     генератор `asda.Npc.GenPickBones` (редактор, данные импорта моделей; новая модель NPC → перегенерировать). Работает в любой
//     сборке, коллизия меша не нужна.
//   Тела ФИЗ-АССЕТА (`RayBodies`) — только запасной путь для модели без строки в таблице: автосозданные капсулы физ-ассета у
//     моделей ASDA толще геометрии на десятки единиц (замер `asda.SelfTest.NpcClickPick`: капсула кости Object14 робота PrePre-v19, NPC 146,
//     задета в 180 ед. перед телом Бумибуми там, где геометрии робота нет).
#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/BodySetup.h"

namespace AsdaNpcBodyRay
{
	// Ближайшее попадание луча (O, единичное D) в коробки костей модели меша (таблица `AsdaNpcPickBones.inl`): true и дальность;
	//   OutBone — кость. Модели нет в таблице — `bOutKnown` = false и false.
	ASDAREBORN_API bool RayBones(const USkeletalMeshComponent* Mesh, const FVector& O, const FVector& D, double& OutT, bool& bOutKnown, FName* OutBone = nullptr);

	// Луч (O, единичное D) против коробки в локальных осях трансформа T с полуразмерами Half: true и дальность входа (≥ 0).
	inline bool RayBox(const FTransform& T, const FVector& Half, const FVector& O, const FVector& D, double& OutT)
	{
		const FVector Lo = T.InverseTransformPosition(O);
		const FVector Le = T.InverseTransformPosition(O + D * 100000.0);
		const FVector Ld = Le - Lo;
		double T0 = 0.0, T1 = 1.0;
		for (int32 a = 0; a < 3; ++a)
		{
			if (FMath::Abs(Ld[a]) < 1e-9) { if (FMath::Abs(Lo[a]) > Half[a]) { return false; } continue; }
			double Ta = (-Half[a] - Lo[a]) / Ld[a], Tb = (Half[a] - Lo[a]) / Ld[a];
			if (Ta > Tb) { Swap(Ta, Tb); }
			T0 = FMath::Max(T0, Ta); T1 = FMath::Min(T1, Tb);
			if (T0 > T1) { return false; }
		}
		OutT = T0 * 100000.0;
		return true;
	}
	// Луч против капсулы (отрезок A–B, радиус R): true и дальность ближайшего сближения (для порядка достаточно).
	inline bool RaySegment(const FVector& A, const FVector& B, double R, const FVector& O, const FVector& D, double& OutT)
	{
		FVector Pr, Ps;
		FMath::SegmentDistToSegmentSafe(O, O + D * 100000.0, A, B, Pr, Ps);
		if (FVector::DistSquared(Pr, Ps) > R * R) { return false; }
		OutT = FMath::Max(0.0, FVector::DotProduct(Pr - O, D) - R);
		return true;
	}

	// Ближайшее попадание луча (O, единичное D) в тела физ-ассета меша: true и дальность. Нет ассета/попадания — false.
	//   OutBone/OutKind (необязательно) — кость и вид формы ближайшего попадания (1 сфера, 2 капсула, 3 конус, 4 коробка, 5 выпуклая).
	inline bool RayBodies(const USkeletalMeshComponent* Mesh, const FVector& O, const FVector& D, double& OutT, FName* OutBone = nullptr, int32* OutKind = nullptr)
	{
		const UPhysicsAsset* PA = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
		if (!PA) { return false; }
		bool bAny = false;
		double Best = 0.0;
		FName CurBone;
		auto Take = [&](double T, int32 Kind) { if (!bAny || T < Best) { Best = T; bAny = true; if (OutBone) { *OutBone = CurBone; } if (OutKind) { *OutKind = Kind; } } };
		for (const TObjectPtr<USkeletalBodySetup>& BS : PA->SkeletalBodySetups)
		{
			if (!BS) { continue; }
			const int32 Bone = Mesh->GetBoneIndex(BS->BoneName);
			CurBone = BS->BoneName;
			if (Bone == INDEX_NONE) { continue; }
			const FTransform BoneTm = Mesh->GetBoneTransform(Bone);
			const double S = BoneTm.GetScale3D().GetAbsMax();
			const FKAggregateGeom& G = BS->AggGeom;
			double T = 0.0;
			for (const FKSphereElem& E : G.SphereElems)
			{
				const FVector C = BoneTm.TransformPosition(E.Center);
				if (RaySegment(C, C, E.Radius * S, O, D, T)) { Take(T, 1); }
			}
			for (const FKSphylElem& E : G.SphylElems)
			{
				const FTransform Et = E.GetTransform() * BoneTm;
				const FVector A = Et.TransformPosition(FVector(0, 0, -E.Length * 0.5)), B = Et.TransformPosition(FVector(0, 0, E.Length * 0.5));
				if (RaySegment(A, B, E.Radius * S, O, D, T)) { Take(T, 2); }
			}
			for (const FKTaperedCapsuleElem& E : G.TaperedCapsuleElems)
			{
				const FTransform Et = E.GetTransform() * BoneTm;
				const FVector A = Et.TransformPosition(FVector(0, 0, -E.Length * 0.5)), B = Et.TransformPosition(FVector(0, 0, E.Length * 0.5));
				if (RaySegment(A, B, FMath::Max(E.Radius0, E.Radius1) * S, O, D, T)) { Take(T, 3); }
			}
			for (const FKBoxElem& E : G.BoxElems)
			{
				if (RayBox(E.GetTransform() * BoneTm, FVector(E.X, E.Y, E.Z) * 0.5, O, D, T)) { Take(T, 4); }
			}
			for (const FKConvexElem& E : G.ConvexElems)
			{
				const FBox Bx = E.ElemBox;
				if (!Bx.IsValid) { continue; }
				FTransform Bt = E.GetTransform();
				Bt.SetTranslation(Bt.TransformPosition(Bx.GetCenter()));
				if (RayBox(Bt * BoneTm, Bx.GetExtent(), O, D, T)) { Take(T, 5); }
			}
		}
		OutT = Best;
		return bAny;
	}

	// Тело NPC: коробки костей, а для модели без таблицы — тела физ-ассета.
	inline bool RayBody(const USkeletalMeshComponent* Mesh, const FVector& O, const FVector& D, double& OutT, FName* OutBone = nullptr)
	{
		bool bKnown = false;
		if (RayBones(Mesh, O, D, OutT, bKnown, OutBone)) { return true; }
		return !bKnown && RayBodies(Mesh, O, D, OutT, OutBone);
	}
}
