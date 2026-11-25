# Copyright (c) 2026 @sitxovski (Discord)
import unreal
AR = unreal.AssetRegistryHelpers.get_asset_registry()


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for ad in AR.get_assets_by_path(unreal.Name("/Game/AsdaAssets/Models/Char/cre_creback-a_00"), recursive=True):
        cls = str(ad.asset_class_path.asset_name)
        if cls in ("SkeletalMesh","AnimSequence"):
            pkg = str(ad.package_name)
            unreal.log("[skel] %s = %s" % (cls, pkg))
            obj = unreal.load_asset(pkg)
            if cls=="SkeletalMesh" and obj:
                b=obj.get_bounds(); e=b.box_extent; o=b.origin
                unreal.log("[skel]   origin=%.1f %.1f %.1f size=%.0f %.0f %.0f" % (o.x,o.y,o.z, e.x*2,e.y*2,e.z*2))
            if cls=="AnimSequence" and obj:
                unreal.log("[skel]   anim length=%.2fs" % obj.get_play_length())


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
