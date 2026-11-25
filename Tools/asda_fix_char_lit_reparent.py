# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# ФИКС пересвета лица персонажей в мире.
# Первопричина: MIC базового превью-скелета (face/hair/chst/pant/shoe) остались/были пересозданы
# с parent = M_AsdaObject (UNLIT, emissive=tex*(Ambient+ndl), Ambient=1.0 → полная яркость,
# свет сцены игнорируется), тогда как экип-части (EquipParts) — M_AsdaObjectLit (DefaultLit,
# затемняются светом карты). Итог: лицо/волосы ярче остального персонажа.
# Регрессию вносили import_char_cremmy.py / import_equip_parts.py / fix_preview_body_materials.py /
# fix_glove_materials.py (MASTER=M_AsdaObject) при перезапуске ПОСЛЕ asda_convert_chars_lit.py —
# теперь у них MASTER=M_AsdaObjectLit.
# Этот скрипт — идемпотентный репарент ВСЕХ персонажных MIC: parent M_AsdaObject → M_AsdaObjectLit.
# Охват = папки персонажей/NPC/мобов (как в asda_convert_chars_lit.py) + Skeletal/EquipParts (recursive).
# Запуск: Tools\run_headless.bat asda_fix_char_lit_reparent.py
import unreal

EAL = unreal.EditorAssetLibrary
AR  = unreal.AssetRegistryHelpers.get_asset_registry()

OBJ_SRC = "M_AsdaObject"
FOLDERS = ["Char", "Mob", "MobSkel", "NPC", "Skeletal", "Items"]   # как asda_convert_chars_lit.py (Skeletal покрывает Characters+EquipParts)

def log(m): unreal.log("[fix-char-lit] " + str(m))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    LIT = EAL.load_asset("/Game/Materials/M_AsdaObjectLit")
    if not LIT:
        raise RuntimeError("M_AsdaObjectLit не найден — сначала run_headless.bat asda_build_materials_lit.py")
    n_fixed, n_ok = 0, 0
    for folder in FOLDERS:
        for ad in AR.get_assets_by_path("/Game/AsdaAssets/Models/%s" % folder, recursive=True):
            if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
                continue
            mic = EAL.load_asset(str(ad.package_name))
            if not mic:
                continue
            par = mic.get_editor_property("parent")
            pn = par.get_name() if par else "None"
            if pn == OBJ_SRC:
                mic.set_editor_property("parent", LIT)
                EAL.save_loaded_asset(mic)
                n_fixed += 1
                log("REPARENT: %s (%s -> M_AsdaObjectLit)" % (str(ad.package_name), pn))
            elif pn == "M_AsdaObjectLit":
                n_ok += 1
    log("итого: репарентнуто=%d, уже Lit=%d" % (n_fixed, n_ok))
    log("DONE")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
