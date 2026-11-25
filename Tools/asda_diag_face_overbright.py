# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# ДИАГНОСТИКА пересвета лица: дамп материалов превью-скелетов + SRGB-флагов текстур.
# Запуск: Tools\run_headless.bat asda_diag_face_overbright.py
import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AR  = unreal.AssetRegistryHelpers.get_asset_registry()

def log(m): unreal.log("[diag-face] " + str(m))

MESHES = [
    "/Game/AsdaAssets/Models/Skeletal/Characters/ch1_m_cremmy_preview/SkeletalMeshes/ch1_m_cremmy_preview",
    "/Game/AsdaAssets/Models/Skeletal/Characters/ch1_f_cremmy_preview/SkeletalMeshes/ch1_f_cremmy_preview",
]

def parent_chain(mi):
    names = []
    cur = mi
    guard = 0
    while cur is not None and guard < 8:
        names.append("%s(%s)" % (cur.get_name(), type(cur).__name__))
        if isinstance(cur, unreal.MaterialInstance) or isinstance(cur, unreal.MaterialInstanceConstant):
            try:
                cur = cur.get_editor_property("parent")
            except Exception:
                cur = None
        else:
            cur = None
        guard += 1
    return " <- ".join(names)

def dump_mi_params(mi):
    try:
        tex = MEL.get_material_instance_texture_parameter_value(mi, "Tex")
    except Exception:
        tex = None
    vals = {}
    for sp in ("Ambient", "Roughness", "Metallic"):
        try:
            vals[sp] = MEL.get_material_instance_scalar_parameter_value(mi, sp)
        except Exception:
            vals[sp] = None
    return tex, vals


TD = "/Game/AsdaAssets/Textures/Characters/Texture"
import re
pat = re.compile(r"face001|chst001|pant001|shoe001|hair00", re.I)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global mi, names, tex, vals
    for mp in MESHES:
        sk = EAL.load_asset(mp)
        if not sk:
            log("НЕ ЗАГРУЗИЛСЯ: " + mp); continue
        log("=== " + mp.split("/")[-1] + " ===")
        for i, sm in enumerate(sk.get_editor_property("materials")):
            mi = sm.material_interface
            slot = str(sm.material_slot_name)
            if mi is None:
                log("  слот %d '%s': ПУСТО" % (i, slot)); continue
            chain = parent_chain(mi)
            line = "  слот %d '%s': %s" % (i, slot, chain)
            if isinstance(mi, unreal.MaterialInstanceConstant):
                tex, vals = dump_mi_params(mi)
                line += " | Tex=%s | scalars=%s" % (tex.get_path_name() if tex else "None", vals)
            log(line)
    # SRGB текстур: лицо + части тела + волосы
    log("=== ТЕКСТУРЫ (srgb / compression) ===")
    names = EAL.list_assets(TD, recursive=True, include_folder=False)
    cnt = 0
    for p in sorted(names):
        base = p.split("/")[-1].split(".")[0]
        if not pat.search(base):
            continue
        t = EAL.load_asset(p.split(".")[0])
        if not isinstance(t, unreal.Texture2D):
            continue
        log("  %-40s srgb=%s comp=%s" % (base, t.get_editor_property("srgb"),
                                         str(t.get_editor_property("compression_settings"))))
        cnt += 1
        if cnt > 60: break
    log("=== DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
