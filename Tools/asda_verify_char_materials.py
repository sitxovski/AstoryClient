# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_char_materials.py — ИНВЕНТАРИЗАЦИЯ материалов моделей по категориям (только чтение).
# Контроль фикса «частично прозрачное оружие» (2026-07-31) и защита уже починенного (лут/волосы/NPC/мобы):
#   для каждой категории — распределение мастер-материалов, blend-режимов и настроек компрессии текстур.
# Запуск: run_headless.bat asda_verify_char_materials.py
import os, sys, json, collections
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_material_policy as POL

import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
OUT = os.path.join(_P.RE_REPORTS, "char_materials_inventory.json")
WEAP = ("swrd", "spir", "stap", "thnd", "lbow", "bbow", "cbow", "shid", "step", "sbow", "arrs")
ARMOR = ("chst", "pant", "shoe", "grov", "hcap")
ROOTS = ["/Game/AsdaAssets/Models"]

EAL = unreal.EditorAssetLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()


def log(m): unreal.log_warning("[verify] %s" % m)


def category(pkg, name):
    n = name.lower()
    if "/Models/Mob" in pkg or "/MobSkel" in pkg: return "мобы"
    if "/Models/NPC" in pkg: return "NPC"
    if "/Models/Items" in pkg: return "лут(Items)"
    if "/Models/Backdrop" in pkg: return "задники"
    if "/Skeletal/Pet" in pkg: return "петы"
    if "/Skeletal/Vehicle" in pkg: return "транспорт"
    if not n.startswith("ch1_"): return "прочее(не ch1_)"
    if any("_%s" % w in n for w in WEAP): return "оружие"
    if any("_%s" % a in n for a in ARMOR): return "броня/экип"
    if "hair" in n: return "причёски"
    if "face" in n: return "лицо"
    return "персонаж/прочее ch1_"


def main():
    stat = collections.defaultdict(lambda: collections.Counter())
    texstat = collections.defaultdict(lambda: collections.Counter())
    for root in ROOTS:
        for ad in AR.get_assets_by_path(unreal.Name(root), recursive=True):
            cls = str(ad.asset_class_path.asset_name)
            if cls not in ("StaticMesh", "SkeletalMesh"):
                continue
            pkg, name = str(ad.package_name), str(ad.asset_name)
            cat = category(pkg, name)
            m = unreal.load_asset(pkg)
            if not m:
                continue
            slots = (m.get_editor_property("static_materials") if cls == "StaticMesh"
                     else m.get_editor_property("materials"))
            for s in slots:
                mi = (s.material_interface if cls == "StaticMesh"
                      else s.get_editor_property("material_interface"))
                if not mi:
                    stat[cat]["слот пуст"] += 1
                    continue
                base = mi
                if isinstance(mi, unreal.MaterialInstance):
                    p = mi.get_editor_property("parent")
                    stat[cat][p.get_name() if p else "БЕЗ РОДИТЕЛЯ"] += 1
                    base = p
                    try:
                        for tp in (mi.get_editor_property("texture_parameter_values") or []):
                            if str(tp.get_editor_property("parameter_info").get_editor_property("name")) == "Tex":
                                t = tp.get_editor_property("parameter_value")
                                if t:
                                    texstat[cat]["%s/sRGB=%d" % (str(t.get_editor_property("compression_settings")).split(".")[-1],
                                                                 int(bool(t.get_editor_property("srgb"))))] += 1
                    except Exception:
                        pass
                else:
                    stat[cat]["материал(не MIC): " + mi.get_name()] += 1
                if base:
                    try:
                        stat[cat]["blend=" + str(base.get_editor_property("blend_mode")).split(".")[-1]] += 1
                    except Exception:
                        pass
    for cat in sorted(stat):
        log("%-22s %s" % (cat, dict(stat[cat].most_common(8))))
        if texstat[cat]:
            log("%-22s текстуры: %s" % ("", dict(texstat[cat].most_common(4))))
    try:
        os.makedirs(os.path.dirname(OUT), exist_ok=True)
        with open(OUT, "w") as f:
            json.dump({k: dict(v) for k, v in stat.items()}, f, ensure_ascii=False, indent=1)
        log("отчёт: %s" % OUT)
    except Exception as e:
        log("отчёт не записан: %s" % e)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
