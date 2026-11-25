# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_set_foliage_switch.py — СВИТЧ ЛИСТВЫ Foliage=true у MIC мастера M_AsdaObjectLit (сет D, 05.09.2026;
#   FINAL_synthesis §4 «Сет D», developer_round2 §5/§7: «Foliage=true на MIC листвы по тому же списку имён, что и класс D»).
#
# ЧТО: мастер M_AsdaObjectLit после сета D несёт StaticSwitchParameter «Foliage» (asda_build_materials_lit.wire_foliage):
#   False (дефолт) → ShadingModel DefaultLit (граф = прежний), True → TwoSidedFoliage, SubsurfaceColor = BaseColor × FoliageSSSTint × SSSScale.
#   Этот скрипт ставит оверрайд свитча у MIC листвы по правилам Tools/setD_foliage_rules.json (include/exclude regex по имени MIC,
#   parent по тегу реестра — без загрузки чужих MIC). Билдер/меши/нормали НЕ трогаются.
#
# ГРАБЛИ (из истории проекта): set_material_instance_static_switch_parameter_value не помечает пакет dirty → mic.modify() +
#   save_loaded_asset(mic, False); файлы MIC могут быть read-only от Perforce → chmod + путь в список для reconcile
#   (память p4v-revert-wiped-assets). Результат печатается ИЗ АССЕТА (get_material_instance_static_switch_parameter_value) и пишется в JSON.
#
# Режимы (env ASDA_FOLIAGE_MODE): apply (умолч.) — Foliage=true у совпавших; dry — только список; revert — Foliage=false (оверрайд
#   остаётся, значение дефолтное) у всех MIC мастера, у которых он стоит True. ASDA_FOLIAGE_REPORT — путь JSON-отчёта.
# Запуск (СКРЫТО, редактор закрыт, ПОСЛЕ asda_fix_objectlit_graph.py):
#   python Tools/run_headless_safe.py Tools/asda_set_foliage_switch.py --marker FOLIAGE-FINISHED
import os, sys, re, json, stat
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_login_guard as _LG   # ★ 15.09.2026: MIC сцены логина (Models/Backdrop, Char/cre_creback-a_00, cre_creation01*) — только с разрешением

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()
RULES = os.environ.get("ASDA_FOLIAGE_JSON", os.path.join(os.path.dirname(os.path.abspath(__file__)), "setD_foliage_rules.json"))
MODE = os.environ.get("ASDA_FOLIAGE_MODE", "apply").lower()
REPORT = os.environ.get("ASDA_FOLIAGE_REPORT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05/setD_foliage_report.json")
READONLY_LIST = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/_backup/2026-09-05_setD/_readonly_list_mics.txt"
CONTENT = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Content"
SWITCH = "Foliage"


def log(m): unreal.log_warning("[foliage] %s" % m)


def parent_path_of(ad):
    try:
        v = str(ad.get_tag_value("Parent"))
    except Exception:
        return ""
    if "'" in v:
        v = v.split("'")[1]
    return v.split(".")[0] if "." in v else v


def pkg_file(pkg):
    return os.path.join(CONTENT, pkg[len("/Game/"):].replace("/", os.sep) + ".uasset")


def make_writable(pkg, lst):
    f = pkg_file(pkg)
    if os.path.isfile(f) and not os.access(f, os.W_OK):
        os.chmod(f, stat.S_IWRITE | stat.S_IREAD)
        lst.append(pkg)


def main():
    """Тело скрипта под функцией — закон [[tools-no-import-side-effects]]."""
    rules = json.load(open(RULES, encoding="utf-8"))
    inc = re.compile(rules["include"], re.I); exc = re.compile(rules["exclude"], re.I)
    master = "/Game/Materials/%s" % rules["parent"]
    m = EAL.load_asset(master)
    if not m:
        raise RuntimeError("нет мастера %s" % master)
    if SWITCH not in [str(n) for n in MEL.get_static_switch_parameter_names(m)]:
        raise RuntimeError("у %s нет StaticSwitchParameter %s — сначала asda_fix_objectlit_graph.py" % (master, SWITCH))
    log("режим %s; правила %s; мастер %s (свитч %s, дефолт %s)" % (MODE, RULES, master, SWITCH, MEL.get_material_default_static_switch_parameter_value(m, SWITCH)))
    kids = [ad for ad in AR.get_assets_by_path("/Game", recursive=True)
            if str(ad.asset_class_path.asset_name) == "MaterialInstanceConstant" and parent_path_of(ad) == master]
    log("MIC мастера по реестру: %d" % len(kids))
    matched, excluded, touched, readonly, verify_bad, unset = [], [], [], [], [], []
    per_map = {}
    n_login = 0   # ★ 15.09.2026: пропущено MIC сцены логина
    for ad in sorted(kids, key=lambda a: str(a.package_name)):
        pkg = str(ad.package_name); nm = str(ad.asset_name)
        if _LG.login_package_blocked(pkg, "asda_set_foliage_switch.py", quiet=True):   # ★ 15.09.2026: сцена логина
            n_login += 1
            continue
        if MODE == "revert":
            mic = EAL.load_asset(pkg)
            if not mic or not MEL.get_material_instance_static_switch_parameter_value(mic, SWITCH):
                continue
            make_writable(pkg, readonly)
            MEL.set_material_instance_static_switch_parameter_value(mic, SWITCH, False)
            mic.modify()
            if not EAL.save_loaded_asset(mic, False):
                raise RuntimeError("не сохранился %s" % pkg)
            touched.append(pkg)
            continue
        hit = bool(inc.search(nm)) and not exc.search(nm)
        if inc.search(nm) and exc.search(nm):
            excluded.append(pkg)
        if not hit:
            # ★ синхронизация (05.09): у не совпавшего MIC свитч может стоять True от прежнего прогона правил (NPC ludivine ← «vine») — снять
            if MODE == "apply":
                mic = EAL.load_asset(pkg)
                if mic and MEL.get_material_instance_static_switch_parameter_value(mic, SWITCH):
                    make_writable(pkg, readonly)
                    MEL.set_material_instance_static_switch_parameter_value(mic, SWITCH, False)
                    mic.modify()
                    if not EAL.save_loaded_asset(mic, False):
                        raise RuntimeError("не сохранился %s" % pkg)
                    unset.append(pkg)
            continue
        matched.append(pkg)
        seg = pkg.split("/")
        key = seg[3] if len(seg) > 3 and seg[2] == "Map" else "/".join(seg[2:4])
        per_map[key] = per_map.get(key, 0) + 1
        if MODE == "dry":
            continue
        mic = EAL.load_asset(pkg)
        if not mic:
            raise RuntimeError("не загрузился %s" % pkg)
        make_writable(pkg, readonly)
        MEL.set_material_instance_static_switch_parameter_value(mic, SWITCH, True)
        mic.modify()
        if not EAL.save_loaded_asset(mic, False):
            raise RuntimeError("не сохранился %s (read-only?)" % pkg)
        if not MEL.get_material_instance_static_switch_parameter_value(mic, SWITCH):   # контроль ИЗ АССЕТА
            verify_bad.append(pkg)
        touched.append(pkg)
    if n_login:   # ★ 15.09.2026
        log("[LOGIN-GUARD] пропущено MIC сцены логина: %d (ASDA_ALLOW_LOGIN_WRITE=1)" % n_login)
    if readonly:
        os.makedirs(os.path.dirname(READONLY_LIST), exist_ok=True)
        with open(READONLY_LIST, "a", encoding="utf-8") as fh:
            for p in readonly:
                fh.write("Content/%s.uasset\n" % p[len("/Game/"):])
    rep = {"mode": MODE, "master": master, "rules": RULES, "mics_of_master": len(kids), "matched": len(matched), "excluded_by_rule": len(excluded),
           "written": len(touched), "readonly_unset": len(readonly), "verify_bad": verify_bad, "per_map": per_map,
           "switch_unset": len(unset), "unset_list": unset,
           "matched_list": matched, "excluded_list": excluded, "touched_list": touched}
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with open(REPORT, "w", encoding="utf-8") as fh:
        json.dump(rep, fh, ensure_ascii=False, indent=1)
    log("совпало %d (исключено правилом %d), записано %d, снят у не совпавших %d, read-only снят %d, не подтвердилось %d; per_map=%s" % (
        len(matched), len(excluded), len(touched), len(unset), len(readonly), len(verify_bad), json.dumps(per_map, ensure_ascii=False)))
    for p in unset[:40]:
        log("   ~ снят Foliage: %s" % p)
    for p in matched[:60]:
        log("   + %s" % p)
    if len(matched) > 60:
        log("   … ещё %d (см. %s)" % (len(matched) - 60, REPORT))
    for p in excluded[:30]:
        log("   - исключено: %s" % p)
    if verify_bad:
        raise RuntimeError("свитч не подтвердился из ассета у %d MIC: %s" % (len(verify_bad), verify_bad[:5]))
    log("ЗАВЕРШЕНО (FOLIAGE-FINISHED)")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
