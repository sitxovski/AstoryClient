# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_mount_assets.py — ПРОВЕРКА импорта ассетов транспорта (маунтов).
# Проверяет: скелеталы транспорта + их клипы, сидячие клипы персонажа обоих полов, длины > 0,
# материалы (мастер + параметр Tex).  Запуск: run_headless.bat asda_verify_mount_assets.py
import re, unreal
import os as _au_os, sys as _au_sys   # ★ волна 2 п. 7 (14.09.2026): реестр путей ассетов (Tools/asda_ue_paths.py)
_au_sys.path.insert(0, _au_os.path.dirname(_au_os.path.abspath(__file__)))
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)

VEH  = _U.G_MODELS_VEHICLE
CHAR = _U.G_MODELS_CHARACTERS + "/ch1_%s_cremmy_preview/SkeletalMeshes"
AR  = unreal.AssetRegistryHelpers.get_asset_registry()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
def log(m): unreal.log("[verify_mount] " + str(m))

def main():
    # ── 1. транспорт ──
    meshes, anims, mics = {}, {}, []
    for ad in AR.get_assets_by_path(unreal.Name(VEH), recursive=True):
        cn = str(ad.asset_class_path.asset_name); an = str(ad.asset_name)
        pkg = str(ad.package_name)
        parts = pkg.split("/")          # ['', Game, AsdaAssets, Models, Skeletal, Vehicle, <model>, ...]
        model = parts[6] if len(parts) > 6 else "?"
        if cn == "SkeletalMesh": meshes[model] = pkg
        elif cn == "AnimSequence": anims.setdefault(model, []).append(pkg)
        elif cn == "MaterialInstanceConstant": mics.append((model, pkg))
    log("ТРАНСПОРТ: SkeletalMesh=%d, AnimSequence=%d, MIC=%d" %
        (len(meshes), sum(len(v) for v in anims.values()), len(mics)))
    noclip = sorted(m for m in meshes if not anims.get(m))
    log("  моделей без клипов: %d %s" % (len(noclip), noclip))
    cnt = {}
    for m, v in anims.items(): cnt[len(v)] = cnt.get(len(v), 0) + 1
    log("  распределение клипов на модель: %s" % cnt)
    zero, badskel = [], []
    for m, lst in anims.items():
        for p in lst:
            a = unreal.load_asset(p)
            if not a or float(a.get_editor_property("sequence_length")) <= 0: zero.append(p)
            sk = a.get_editor_property("skeleton") if a else None
            if not sk or not sk.get_name().startswith(m): badskel.append(p)
    log("  клипов нулевой длины: %d %s" % (len(zero), zero[:5]))
    log("  клипов с чужим скелетом: %d %s" % (len(badskel), badskel[:5]))
    notex = []
    for m, p in mics:
        a = unreal.load_asset(p)
        try:
            t = MEL.get_material_instance_texture_parameter_value(a, "Tex")
        except Exception:
            t = None
        par = a.get_editor_property("parent")
        if not t or not par or par.get_name() != "M_AsdaObjectLit": notex.append((m, p))
    log("  MIC без Tex/мастера: %d %s" % (len(notex), notex[:5]))

    # ── 2. сидячие клипы персонажа ──
    rx = re.compile(r"sit_(\d{4})_(\d{3})$", re.I)
    for g in ("m", "f"):
        path = CHAR % g
        got = {}
        for ad in AR.get_assets_by_path(unreal.Name(path), recursive=True):
            if str(ad.asset_class_path.asset_name) != "AnimSequence": continue
            mm = rx.search(str(ad.asset_name))
            if mm: got[(mm.group(1), mm.group(2))] = str(ad.package_name)
        sets = sorted({k[0] for k in got}); variants = sorted({k[1] for k in got})
        zl = []
        for k, p in got.items():
            a = unreal.load_asset(p)
            if not a or float(a.get_editor_property("sequence_length")) <= 0: zl.append(p)
        log("СИДЯЧИЕ [%s]: клипов=%d, наборов=%d (%s..%s), вариантов=%s, нулевой длины=%d"
            % (g, len(got), len(sets), sets[0] if sets else "-", sets[-1] if sets else "-",
               variants, len(zl)))
        # старые клипы персонажа не пострадали?
        old = [str(ad.asset_name) for ad in AR.get_assets_by_path(unreal.Name(path), recursive=True)
               if str(ad.asset_class_path.asset_name) == "AnimSequence" and "anim_" in str(ad.asset_name)]
        log("  прежних anim_* на месте: %d" % len(old))

    for t in ("/Game/Tmp_Sit_m", "/Game/Tmp_Sit_f", "/Game/Tmp_Move35_m", "/Game/Tmp_Move35_f"):
        if EAL.does_directory_exist(t): log("!! осталась временная папка %s" % t)
    log("ПРОВЕРКА ЗАВЕРШЕНА")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
