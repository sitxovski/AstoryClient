# Copyright (c) 2026 @sitxovski (Discord)
# asda_diag_materials.py — ДИАГНОСТИКА материалов (без импорта). Почему модели серые?
import unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_ue_paths as _U
MODELS_ROOT = _U.G_MODELS
TEX_ROOT    = _U.G_TEXTURES
EAL = unreal.EditorAssetLibrary
AR  = unreal.AssetRegistryHelpers.get_asset_registry()

# ★ ВОЛНА 2 (14.09.2026, дефект E11): индекс/поиск текстур, подвязка MIC, импорт и Nanite — из ЕДИНОЙ
#   библиотеки asda_model_import_ue. Пресеты повторяют прежние копии этого файла (правило дублей имён,
#   нормализация ключа) — найденная текстура прежняя; добавлено только снятие меток `_msk`/`_blend` там, где
#   раньше текстура не находилась (решение координатора, `Reports/dedup_audit/wave2_import.md` §2, §8).
import asda_model_import_ue as MI

def tex_index():
    """Индекс Texture2D (пресет «all_last» библиотеки = прежняя копия этого файла)."""
    return MI.tex_index("all_last", root=TEX_ROOT)

def find_tex(name, tex):
    """Текстура для слота/MIC (пресет «plain» библиотеки = прежняя копия этого файла)."""
    return MI.find_tex(name, tex, "plain")

def main():
    tex = tex_index()
    unreal.log("DIAG: текстур=%d | примеры ключей: %s" % (len(tex), list(tex.keys())[:5]))
    unreal.log("DIAG: 'mon_b_3_scorpion-a_00' в индексе? %s" % ("mon_b_3_scorpion-a_00" in tex))

    # типы материал-ассетов под Models/
    types = {}
    sample = []
    for ad in AR.get_assets_by_path(unreal.Name(MODELS_ROOT), recursive=True):
        cls = str(ad.asset_class_path.asset_name)
        if cls in ("Material", "MaterialInstanceConstant"):
            types[cls] = types.get(cls, 0) + 1
            if len(sample) < 15:
                nm = str(ad.asset_name)
                sample.append((cls, nm, find_tex(nm, tex) is not None))
    unreal.log("DIAG: типы материалов под Models: %s" % types)
    for cls, nm, hit in sample:
        unreal.log("   %-26s '%s' find_tex=%s" % (cls, nm, hit))

    # слоты StaticMesh — реальные имена слотов
    nstat = 0
    for ad in AR.get_assets_by_path(unreal.Name(MODELS_ROOT + "/Mob"), recursive=True):
        if str(ad.asset_class_path.asset_name) == "StaticMesh":
            sm = unreal.load_asset(str(ad.package_name))
            mats = sm.get_editor_property("static_materials")
            slots = [(str(s.get_editor_property("material_slot_name")),
                      s.get_editor_property("material_interface").get_class().get_name() if s.get_editor_property("material_interface") else None)
                     for s in mats]
            unreal.log("DIAG StaticMesh '%s' слоты: %s" % (str(ad.asset_name), slots[:4]))
            nstat += 1
            if nstat >= 4: break


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
