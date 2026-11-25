// Copyright (c) 2026 @sitxovski (Discord)
// AsdaShadowPhysicsAssetLib — EDITOR-ONLY утилита для ОТДЕЛЬНОГО физассета ТЕНИ (USkeletalMesh::ShadowPhysicsAsset)
//   у тел героя ch1_*/cre_* (сет C-2 «материалы поверхностей», 05.09.2026; review_setB_fix.md ВАЖНО-3).
//
// ★ ЗАЧЕМ ОТДЕЛЬНЫЙ ФИЗАССЕТ. Капсульная непрямая тень (CapsuleShadowRendering) берёт капсулы ТОЛЬКО из
//   USkeletalMesh::ShadowPhysicsAsset (SkeletalMeshSceneProxy.cpp:318-345). Сет B назначил туда авто-физассет
//   Interchange (`<mesh>_PhysicsAsset`). У мобов его капсулы 24–31 см (mon_s_0_rwolf01 r0=2885 при scale 0.01) и пятно
//   под телом читается; у героя ch1_m_cremmy_preview радиусы 3–16 см (r0 337…1558) — тень под ногами −4…−7 %, глазами
//   не видна (review_setB_fix.md п.3b). Обычный PhysicsAsset трогать нельзя (физика/хит-тест), поэтому копия →
//   капсулы укрупняются → копия назначается ТОЛЬКО в ShadowPhysicsAsset.
//
// ★ ПОЧЕМУ C++. `UPhysicsAsset::SkeletalBodySetups` — UPROPERTY(instanced) без Blueprint-доступа: из Python
//   `get_editor_property("skeletal_body_setups")` → «Failed to find property» (asda_set_shadow_physics_assets.py,
//   ложное «без капсул 235/235» 05.09). Радиусы капсул из Python не читаются и не пишутся вовсе.
//
// Что умеет (всё — БЕЗ сохранения: сохраняет вызывающий Python через EditorAssetLibrary):
//   DescribeBodies   — строки «bone|refIdx|refPoseZ|sphyl|sphere|r|len» по каждому телу (контроль ИЗ АССЕТА до/после);
//   ScaleCapsules    — r' = clamp(r·RadiusScale, MinRadius, MaxRadius) у всех сфер/капсул; тела с исходным r < DropBelow
//                      (вырожденный корень r0=0.505) удаляются вместе с их констрейнтами; возвращает число оставшихся тел;
//   CreateSphylFromMesh — новый физассет из меша штатным FPhysicsAssetUtils::CreateFromSkeletalMesh (GeomType=Sphyl),
//                      без назначения мешу (bSetToMesh=false) — запасной путь, если у меша нет авто-физассета.
// Единицы — единицы МЕША (у тел героя масштаб компонента 0.01 ⇒ 100 единиц = 1 см).
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AsdaShadowPhysicsAssetLib.generated.h"

class UPhysicsAsset;
class USkeletalMesh;

UCLASS()
class ASDAIMPORTERS_API UAsdaShadowPhysicsAssetLib : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Состав тел физассета: «bone|refIdx|refPoseZ|sphyl|sphere|box|convex|r|len» (r/len — первой капсулы/сферы, в единицах меша).
	 *  Mesh может быть nullptr — тогда refIdx=-1, refPoseZ=0. Только чтение. */
	UFUNCTION(BlueprintCallable, Category = "Asda|Тень")
	static TArray<FString> DescribeBodies(UPhysicsAsset* PhysicsAsset, USkeletalMesh* Mesh);

	/** Укрупнить сферы/капсулы: r' = clamp(r·RadiusScale, MinRadius, MaxRadius); тела с исходным max(r) < DropBelowRadius
	 *  удаляются (и констрейнты на их кости). LengthScale — множитель длины капсул (1 = не трогать).
	 *  Ассет помечается Modify(), НЕ сохраняется. Возвращает число тел после правки (-1 при ошибке). */
	UFUNCTION(BlueprintCallable, Category = "Asda|Тень")
	static int32 ScaleCapsules(UPhysicsAsset* PhysicsAsset, float RadiusScale, float MinRadius, float MaxRadius,
	                           float DropBelowRadius, float LengthScale = 1.0f);

	/** Новый физассет PackagePath/AssetName из меша штатной генерацией (капсулы, MinBoneSize в единицах меша),
	 *  мешу НЕ назначается. nullptr при ошибке (текст — в лог LogAsdaShadowPA). Не сохраняется. */
	UFUNCTION(BlueprintCallable, Category = "Asda|Тень")
	static UPhysicsAsset* CreateSphylFromMesh(USkeletalMesh* Mesh, const FString& PackagePath, const FString& AssetName,
	                                          float MinBoneSize);
};
