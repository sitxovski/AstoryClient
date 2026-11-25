# Copyright (c) 2026 @sitxovski (Discord)
# Фикс glTF-дефолтных материалов мобов/NPC: репарент на M_AsdaObjectLit (появляется поле Tex — юзер укажет
#   текстуры сам). Причина: часть MIC осталась на MI_Default_Opaque (glTF Interchange) → белые, без Tex-параметра.
#   Прежние фиксеры репарентили только с родителем M_AsdaObject — эти пропускались. Текстуру НЕ трогаем.
#   Запуск: run_headless.bat asda_fix_gltf_default_mats.py
import unreal
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
ar = unreal.AssetRegistryHelpers.get_asset_registry()


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    LIT = EAL.load_asset("/Game/Materials/M_AsdaObjectLit")
    n = 0; skip = 0
    for folder in ("Mob", "MobSkel", "NPC", "Char", "Skeletal", "Items"):
        for ad in ar.get_assets_by_path("/Game/AsdaAssets/Models/%s" % folder, recursive=True):
            if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
                continue
            m = EAL.load_asset(str(ad.package_name))
            if not m:
                continue
            par = m.get_editor_property("parent")
            pn = par.get_name() if par else "None"
            if pn == "M_AsdaObjectLit":
                skip += 1
                continue
            # Любой НЕ-Lit родитель (MI_Default_Opaque, glTF-цепочки, M_AsdaObject) → на Lit-мастер.
            #   set_editor_property('parent') + modify() персистит (MEL.set_material_instance_parent не метил dirty).
            m.set_editor_property("parent", LIT)
            m.modify()
            EAL.save_loaded_asset(m)
            n += 1
    unreal.log("[FIXGLTF] переведено на M_AsdaObjectLit: %d (уже Lit: %d)" % (n, skip))
    unreal.log("[FIXGLTF] done — поле Tex теперь доступно, текстуры выставит пользователь")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
