# Copyright (c) 2026 @sitxovski (Discord)
# asda_diag_mob_materials.py — диагностика материалов скелетал-мобов: слоты уток + все MIC без текстуры.
import unreal
import os as _au_os, sys as _au_sys   # ★ волна 2 п. 7 (14.09.2026): реестр путей ассетов (Tools/asda_ue_paths.py)
_au_sys.path.insert(0, _au_os.path.dirname(_au_os.path.abspath(__file__)))
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
EAL = unreal.EditorAssetLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()
MEL = unreal.MaterialEditingLibrary


# массовый скан: MIC в MobSkel без Tex
miss = []


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for nm in ("mon_s_0_kduck01-a_00", "mon_m_0_kduck01-a_00"):
        sk = EAL.load_asset(_U.skel_mesh(_U.G_MODELS_MOBSKEL, nm))
        if not sk: unreal.log_warning("нет скелетала %s" % nm); continue
        mats = sk.get_editor_property("materials")
        unreal.log("SKEL %s: слотов=%d" % (nm, len(mats)))
        for i, m in enumerate(mats):
            mi = m.get_editor_property("material_interface")
            tex = None
            if mi and isinstance(mi, unreal.MaterialInstanceConstant):
                try: tex = MEL.get_material_instance_texture_parameter_value(mi, "Tex")
                except Exception: pass
            unreal.log("  слот %d: mat=%s tex=%s" % (i, mi.get_name() if mi else None, tex.get_name() if tex else "НЕТ"))
    for ad in AR.get_assets_by_path(unreal.Name(_U.G_MODELS_MOBSKEL), recursive=True):
        if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant": continue
        a = unreal.load_asset(str(ad.package_name))
        tex = None
        try: tex = MEL.get_material_instance_texture_parameter_value(a, "Tex")
        except Exception: pass
        if not tex or tex.get_name() == "DefaultTexture":
            miss.append(str(ad.asset_name))
    unreal.log("MIC БЕЗ ТЕКСТУРЫ: %d" % len(miss))
    for m in sorted(miss): unreal.log("   " + m)
    unreal.log("=== MOB MAT DIAG DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
