# Copyright (c) 2026 @sitxovski (Discord)
# asda_clear_decor_physics_assets.py — СНЯТИЕ АВТО-ФИЗАССЕТОВ У ДЕКОРАТИВНЫХ АНИМИРОВАННЫХ
#   СКЕЛЕТНЫХ МЕШЕЙ КАРТ (`/Game/Map/<карта>/Anims/clip_*/SkeletalMeshes/clip_*`).
#
# ★★ ЗАЧЕМ. Кук валит один handled ensure в FQuat(FMatrix) (Quat.h:789). Стек, символизированный по
#   таблице экспорта DLL (PDB в поставке нет), даёт цепочку:
#     UEditorEngine::InitializePhysicsSceneForSaveIfNecessary → UWorld::UpdateWorldComponents →
#     ULevel::UpdateLevelComponents → AActor::IncrementalRegisterComponents →
#     UActorComponent::RegisterComponentWithWorld → CreatePhysicsState →
#     USkeletalMeshComponent::OnCreatePhysicsState → InitArticulated →
#     InstantiatePhysicsAsset_Internal → InstantiatePhysicsAssetBodies_Internal →
#     FBodyInstance::InitBody → UBodySetup::AddShapesToRigidActor_AssumesLocked →
#     FPhysInterface_Chaos::AddGeometry → ChaosInterface::CreateGeometry → FKSphylElem::GetFinalScaled
#   То есть матрицу со скейлом в FQuat отдаёт КАПСУЛА физ-ассета скелетного меша при создании физ-тел,
#   когда редактор синтезирует физ-сцену на время сохранения мира (кук). FKSphylElem::GetFinalScaled
#   (BodySetup.cpp:2144) перемножает два FTransform, у которых Scale3D = масштаб компонента; при
#   ОТРИЦАТЕЛЬНОМ масштабе FTransform уходит в матричную ветку (MultiplyUsingMatrixWithScale →
#   ConstructTransformFromMatrixWithDesiredScale), где и вызывается FQuat(FMatrix).
#
# ★ Почему трогаем именно clip_*: у мобов/NPC масштаб (0.01,0.01,0.01) — положительный, матричная
#   ветка не включается. Отрицательный масштаб (-0.01,0.01,0.01) только у анимированной декорации
#   карт (зеркальная конвенция скелетных импортов, см. asda_build_map_anim.py). Физ-ассет у них
#   авто-сгенерирован импортом и не используется: коллизия карт в оригинале серверная (.SIM),
#   декорация не является целью клика.
#
# Скрипт запоминает снятые ссылки в лог (обратимо) и сохраняет ассеты.
# ASDA_MESHES="/Game/..." — конкретные меши через ||; иначе все clip_* всех карт.
# ASDA_DRYRUN=1 — только показать.
import os
import unreal

EAL = unreal.EditorAssetLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()


def collect():
    explicit = os.environ.get("ASDA_MESHES", "").strip()
    if explicit:
        return [p.strip() for p in explicit.split("||") if p.strip()]
    out = []
    for ad in AR.get_assets_by_path(unreal.Name("/Game/Map"), recursive=True):
        if str(ad.asset_class_path.asset_name) != "SkeletalMesh":
            continue
        pkg = str(ad.package_name)
        if "/Anims/" in pkg and "/SkeletalMeshes/" in pkg:
            out.append(pkg)
    return sorted(out)


def main():
    dry = os.environ.get("ASDA_DRYRUN", "0") == "1"
    pkgs = collect()
    unreal.log("[decorphys] кандидатов: %d, dry_run=%s" % (len(pkgs), dry))
    cleared = 0
    for p in pkgs:
        sk = unreal.load_asset(p)
        if not isinstance(sk, unreal.SkeletalMesh):
            continue
        pa = sk.get_editor_property("physics_asset")
        if pa is None:
            continue
        unreal.log("[decorphys] %s: снят физ-ассет %s" % (p, pa.get_path_name()))
        if dry:
            continue
        sk.set_editor_property("physics_asset", None)
        EAL.save_loaded_asset(sk)
        cleared += 1
        if cleared % 200 == 0:
            unreal.SystemLibrary.collect_garbage()
    unreal.log("[decorphys] ИТОГ: снято %d из %d" % (cleared, len(pkgs)))


if __name__ == "__main__":
    main()
