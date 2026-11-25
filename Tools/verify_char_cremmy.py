# Copyright (c) 2026 @sitxovski (Discord)
import unreal
P="/Game/AsdaAssets/Models/Skeletal/Characters/ch1_m_cremmy_preview/SkeletalMeshes/ch1_m_cremmy_preview"


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    sk=unreal.load_asset(P)
    unreal.log("[verify] SkeletalMesh loaded: %s" % (sk is not None))
    mats=sk.get_editor_property("materials")
    for i,m in enumerate(mats):
        mi=m.get_editor_property("material_interface")
        unreal.log("[verify] slot %d -> %s" % (i, mi.get_name() if mi else "NONE"))
    skel=sk.get_editor_property("skeleton")
    unreal.log("[verify] skeleton=%s bones=%d" % (skel.get_name(), len(skel.get_editor_property("bone_tree")) if hasattr(skel,'get_editor_property') else -1))
    anim=unreal.load_asset("/Game/AsdaAssets/Models/Skeletal/Characters/ch1_m_cremmy_preview/SkeletalMeshes/ch1_m_cremmy_previewanim_00")
    unreal.log("[verify] idle anim=%s len=%.2fs" % (anim.get_name(), anim.get_editor_property("sequence_length") if anim else -1))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
