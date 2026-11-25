# Copyright (c) 2026 @sitxovski (Discord)
# asda_gacha_probe_models.py — ЗАМЕР импортированных автоматов премиум-гачи (только чтение).
#
# Зачем: расстановку сцены берём из эталона в ОРИГИНАЛЬНЫХ единицах
# (`uiBuildPortraitCameraMatrices@0xB6EB50`: радиус 150, углы 0/±27/±54°;
#  `PetWindow_RenderToTarget@0xB6D5B0`: глаз (0,250,150) при 2 автоматах, цель (0,100,-200)).
# Чтобы числа сошлись, надо знать (а) во сколько раз ассет крупнее оригинальных единиц,
# (б) существует ли в скелете кость «Bone19», по проекции которой оригинал ставит контролы
# (`deathRespawnUI_LayoutResurrectPanel@0xB6E480`, ui_core.c:7151-7175).
#
# Скрипт НИЧЕГО не сохраняет: создаёт временный уровень в памяти, печатает габариты,
# позиции всех костей в компонент-пространстве и длительности клипов.
# Запуск: run_headless.bat asda_gacha_probe_models.py
import unreal

ROOT = "/Game/AsdaAssets/Models/Skeletal/Gachya"
MODELS = ["md_gachya_%s_00" % c for c in "abcde"]

EAL = unreal.EditorAssetLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()


def log(m):
    unreal.log("[гача-замер] " + str(m))


def main():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.new_level("/Temp/L_GachaProbe")

    for name in MODELS:
        path = "%s/%s/SkeletalMeshes/%s" % (ROOT, name, name)
        mesh = EAL.load_asset(path)
        if not mesh:
            log("НЕТ меша %s" % path)
            continue

        act = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0))
        comp = act.skeletal_mesh_component
        comp.set_skinned_asset_and_update(mesh)

        b = mesh.get_bounds()
        org, ext = b.origin, b.box_extent
        log("%s: bounds origin=(%.1f, %.1f, %.1f) extent=(%.1f, %.1f, %.1f) => высота %.1f (scale 1)" % (
            name, org.x, org.y, org.z, ext.x, ext.y, ext.z, ext.z * 2.0))

        num = comp.get_num_bones()
        names = [str(comp.get_bone_name(i)) for i in range(num)]
        log("%s: костей=%d :: %s" % (name, num, ", ".join(names)))
        for bn in names:
            t = comp.get_socket_transform(unreal.Name(bn), unreal.RelativeTransformSpace.RTS_COMPONENT)
            p = t.translation
            log("%s:   кость %-28s = (%10.1f, %10.1f, %10.1f)" % (name, bn, p.x, p.y, p.z))

        clips = []
        for ad in AR.get_assets_by_path(unreal.Name("%s/%s/SkeletalMeshes" % (ROOT, name)), recursive=True):
            if str(ad.asset_class_path.asset_name) == "AnimSequence":
                a = EAL.load_asset(str(ad.package_name))
                clips.append("%s=%.3fс" % (str(ad.asset_name), a.get_play_length() if a else -1.0))
        log("%s: клипы: %s" % (name, ", ".join(sorted(clips))))
        eas.destroy_actor(act)


if __name__ == "__main__":
    main()
