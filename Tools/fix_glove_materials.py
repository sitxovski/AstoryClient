# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Проверка и починка материалов «стоковых» перчаток (grov105) в char-select.
# Симптом: у персонажа без экипированных перчаток руки не отображаются.
# Проверяем: у SkeletalMesh части назначен материал и в материале стоит текстура (параметр Tex).
import unreal
import os as _au_os, sys as _au_sys   # ★ волна 2 п. 7 (14.09.2026): реестр путей ассетов (Tools/asda_ue_paths.py)
_au_sys.path.insert(0, _au_os.path.dirname(_au_os.path.abspath(__file__)))
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MASTER = _U.M_OBJECT_LIT  # ★ ПЕРСОНАЖИ = LIT (unlit-мастер давал пересвет лица/тела vs Lit-экип; см. face_overbright_fix.md)
TEX_DIR = _U.G_TEX_CHAR_TEXTURE

def log(m): unreal.log("[fix-gloves] " + str(m))

PARTS = ["ch1_m_00_grov105_00_00_0", "ch1_f_00_grov105_00_00_0"]


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    master = EAL.load_asset(MASTER)
    log("мастер-материал %s: %s" % (MASTER, "есть" if master else "НЕТ"))
    for name in PARTS:
        sk_path = _U.equip_part_mesh(name)
        sk = EAL.load_asset(sk_path)
        if not sk:
            log("%s: SkeletalMesh НЕ найден (%s)" % (name, sk_path)); continue
        mats = sk.get_editor_property("materials")
        log("%s: слотов материала = %d" % (name, len(mats)))
        tex = EAL.load_asset("%s/%s" % (TEX_DIR, name))
        log("   текстура %s: %s" % (name, "есть" if tex else "НЕТ"))
        for i, sm in enumerate(mats):
            mi = sm.material_interface
            log("   слот %d: материал = %s" % (i, mi.get_path_name() if mi else "ПУСТО"))
            need_fix = mi is None
            if mi is not None and tex is not None:
                try:
                    cur = MEL.get_material_instance_texture_parameter_value(mi, "Tex")
                    log("      параметр Tex = %s" % (cur.get_path_name() if cur else "ПУСТО"))
                    need_fix = cur is None
                except Exception as e:
                    log("      Tex прочитать не удалось: %s" % e)
            if need_fix and tex is not None and master is not None:
                mic_path = _U.equip_part_mesh(name) + "_mat"
                mic = EAL.load_asset(mic_path)
                if mic is None:
                    mic = ATOOLS.create_asset("%s_mat" % name,
                                              _U.G_MODELS_EQUIPPARTS + "/" + name,
                                              unreal.MaterialInstanceConstant,
                                              unreal.MaterialInstanceConstantFactoryNew())
                MEL.set_material_instance_parent(mic, master)
                MEL.set_material_instance_texture_parameter_value(mic, "Tex", tex)
                try:
                    MEL.set_material_instance_scalar_parameter_value(mic, "Ambient", 1.0)
                except Exception:
                    pass
                sk.set_material(i, mic)
                EAL.save_asset(mic.get_path_name())
                EAL.save_asset(sk_path)
                log("      ИСПРАВЛЕНО: назначен %s с текстурой" % mic.get_path_name())
    log("готово")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
