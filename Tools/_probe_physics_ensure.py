# Copyright (c) 2026 @sitxovski (Discord)
# _probe_physics_ensure.py — ЛОКАЛИЗАЦИЯ handled ensure в FQuat(FMatrix) при куке.
#
# ★ Символизация стека (без PDB, по таблице экспорта DLL + база модуля из живого процесса) дала цепочку:
#   UEditorEngine::InitializePhysicsSceneForSaveIfNecessary → UWorld::UpdateWorldComponents →
#   ULevel::UpdateLevelComponents → AActor::IncrementalRegisterComponents →
#   UActorComponent::RegisterComponentWithWorld → CreatePhysicsState →
#   USkeletalMeshComponent::OnCreatePhysicsState → InitArticulated →
#   InstantiatePhysicsAsset_Internal → InstantiatePhysicsAssetBodies_Internal →
#   FBodyInstance::InitBody → UBodySetup::AddShapesToRigidActor_AssumesLocked →
#   FPhysInterface_Chaos::AddGeometry → ChaosInterface::CreateGeometry → FKSphylElem::GetFinalScaled.
#   То есть матрицу со скейлом в FQuat отдаёт КАПСУЛА физ-ассета скелетного меша, а не уровень и не
#   статичная геометрия. Воспроизводится простой загрузкой уровня в редакторе.
#
# Скрипт перечисляет по уровню все SkeletalMeshComponent: масштаб, скелетный меш, физ-ассет и состав
# его тел (сколько сфер/капсул/боксов/выпуклых). Это и есть кандидаты.
# ASDA_LEVEL — пакет уровня (по умолчанию Mountain_Flamion, на нём ensure и всплывает первым).
import os
import unreal


def main():
    lvl = os.environ.get("ASDA_LEVEL", "/Game/Map/Mountain_Flamion/Mountain_Flamion")
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(lvl)
    seen = {}
    for actor in eas.get_all_level_actors():
        for comp in actor.get_components_by_class(unreal.SkeletalMeshComponent):
            sk = comp.get_editor_property("skeletal_mesh_asset")
            if sk is None:
                continue
            pa = sk.get_editor_property("physics_asset")
            s = comp.get_editor_property("relative_scale3d")
            key = (sk.get_path_name(), None if pa is None else pa.get_path_name(),
                   round(s.x, 4), round(s.y, 4), round(s.z, 4))
            seen[key] = seen.get(key, 0) + 1
    unreal.log("[phys] уровень %s: различных сочетаний меш/физ-ассет/масштаб — %d" % (lvl, len(seen)))
    for k, n in sorted(seen.items(), key=lambda kv: -kv[1]):
        unreal.log("[phys] x%-5d масштаб=(%s,%s,%s) физ-ассет=%s меш=%s" % (n, k[2], k[3], k[4], k[1], k[0]))


if __name__ == "__main__":
    main()
