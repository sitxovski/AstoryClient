# Copyright (c) 2026 @sitxovski (Discord)
import unreal
def L(m): unreal.log("[VERIFY] " + str(m))
AR = unreal.AssetRegistryHelpers.get_asset_registry()
DEST = "/Game/AsdaAssets/Models/Skeletal/EquipParts"
part = "ch1_m_00_chst001_00_00_0"; sub = DEST + "/" + part
sm = None; anim = None


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global anim, sm
    for ad in AR.get_assets_by_path(unreal.Name(sub), recursive=True):
        cn = str(ad.asset_class_path.asset_name)
        if cn == "SkeletalMesh": sm = unreal.load_asset(str(ad.package_name))
        elif cn == "AnimSequence": anim = unreal.load_asset(str(ad.package_name))
    if sm:
        for i, ms in enumerate(sm.get_editor_property("materials")):
            mi = ms.get_editor_property("material_interface")
            L("slot%d = %s (%s)" % (i, mi.get_name() if mi else None, mi.get_class().get_name() if mi else "-"))
            if mi and mi.get_class().get_name() == "MaterialInstanceConstant":
                p = mi.get_editor_property("parent")
                found, t = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(mi, "Tex"), None
                L("  parent=%s Tex=%s" % (p.get_name() if p else None, found.get_name() if found else None))
    # M_DUMMY через треки анимации (anim привязана к общему скелету -> трек существует => кость в скелете)
    if anim:
        try:
            tn = [str(x) for x in unreal.AnimationLibrary.get_animation_track_names(anim)]
            L("anim tracks=%d M_DUMMY=%s" % (len(tn), any(x.lower()=='m_dummy' for x in tn)))
        except Exception as e: L("anim tracks FAIL %s" % e)
    # bone count скелета через SkeletonModifier.set_skeleton
    sk = sm.get_editor_property("skeleton") if sm else None
    if sk:
        try:
            mod = unreal.SkeletonModifier()
            r = mod.set_skeleton(sk)
            names = [str(x) for x in mod.get_all_bone_names()]
            L("SkeletonModifier set=%s bones=%d M_DUMMY=%s" % (r, len(names), any(x.lower()=='m_dummy' for x in names)))
        except Exception as e: L("SkeletonModifier FAIL %s" % e)
    L("VERIFY DONE")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
