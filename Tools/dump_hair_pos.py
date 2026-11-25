# Copyright (c) 2026 @sitxovski (Discord)
import unreal
def L(m): unreal.log("[HAIRPOS] "+str(m))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    # 1. hair static mesh bounds
    sm=unreal.load_object(None,"/Game/AsdaAssets/Models/Char/ch1_m_00_hair001_00_00_0/StaticMeshes/ch1_m_00_hair001_00_00_0.ch1_m_00_hair001_00_00_0")
    if sm:
        b=sm.get_bounds(); L("HAIR static bounds origin=%s extent=%s"%(b.origin,b.box_extent))
    # 2. cremmy skeletal head bone bind
    sk=unreal.load_object(None,"/Game/AsdaAssets/Models/Char/ch1_m_cremmy_preview/SkeletalMeshes/ch1_m_cremmy_preview.ch1_m_cremmy_preview")
    if not sk:
        sk=unreal.load_object(None,"/Game/AsdaAssets/Models/Skeletal/Characters/ch1_m_cremmy_preview/ch1_m_cremmy_preview")
    if sk:
        skel=sk.skeleton
        names=[]
        try:
            rb=skel.get_editor_property('reference_skeleton') if skel else None
        except Exception: rb=None
        # перечислить кости через bone tree
        try:
            n=skel.get_num_bones() if hasattr(skel,'get_num_bones') else 0
        except Exception: n=0
        L("cremmy skel bones=%s"%n)
        # bounds всего меша
        b2=sk.get_bounds(); L("CREMMY bounds origin=%s extent=%s"%(b2.origin,b2.box_extent))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
