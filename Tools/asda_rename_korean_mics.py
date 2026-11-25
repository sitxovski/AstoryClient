# Copyright (c) 2026 @sitxovski (Discord)
# asda_rename_korean_mics.py — переименование 62 MIC с корейскими (cp949-мусорными) именами в ASCII.
#   Имена пришли из glTF-материалов («»óÀÇ3» = cp949 «상의3» = верх). Perforce с P4CHARSET=none калечит
#   такие имена, поэтому в депо кладём ТОЛЬКО ASCII: MI_<папка-меша>_<роль><№>.
#   Карта переименования — JSON (env ASDA_MAP), собранный снаружи: old_obj / new_obj / ko.
#   Режимы (env ASDA_MODE):
#     audit  — только читатели (AssetRegistry referencers) + проверка существования, ничего не пишет;
#     rename — EditorAssetLibrary.rename_asset (штатно, с редиректорами) → fixup_referencers →
#              повторный аудит → удаление редиректоров ТОЛЬКО при 0 ссылок → проверка материалов у владельцев.
#   Результат — JSON (env ASDA_OUT). Маркер завершения для run_headless_safe.py: «ЗАВЕРШЕНО».
import json, os, sys
import unreal
# ★ 16.09.2026 (раздел 22, Reports/dedup_audit/savedir_guard_2026-09-16.md): три save_directory и запасной
#   fixup_referencers_in_folders шли по /Game/AsdaAssets/Models ЦЕЛИКОМ. Под этой папкой — задник сцены логина
#   (Models/Backdrop, 491 меш), шкатулка cre_creation01* и cre_creback-a_00: переименование корейских MIC их
#   не касается, но сохранение папки целиком пересохраняло их, а fixup по папке переписал бы в них ссылки.
#   ★ 16.09.2026 (ревизия раздела 22, МЕЛК-3): sys.path.insert и импорт защиты были ЗДЕСЬ, на уровне модуля —
#   единственный такой случай из 12 инструментов; перенесены в main() (закон tools-no-import-side-effects).

EAL = unreal.EditorAssetLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MODE = os.environ.get("ASDA_MODE", "audit")
MAP = os.environ.get("ASDA_MAP", r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools/rename_korean_mics_map.json")
OUT = os.environ.get("ASDA_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05/rename_korean_mic_%s.json" % MODE)


def pkg_of(obj_path):
    """/Game/A/B/Name → /Game/A/B/Name (имя пакета = путь объекта без суффикса .Name)."""
    return obj_path.split(".")[0]


def referencers(obj_path):
    """Пакеты-читатели (hard + soft) из реестра ассетов."""
    pkg = pkg_of(obj_path)
    opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,
                                                 include_hard_package_references=True,
                                                 include_searchable_names=False,
                                                 include_soft_management_references=False,
                                                 include_hard_management_references=False)
    refs = AR.get_referencers(unreal.Name(pkg), opts) or []
    return sorted(set(str(r) for r in refs))


def asset_class(obj_path):
    d = EAL.find_asset_data(obj_path)
    if not d or not d.is_valid():
        return None
    try:
        return str(d.asset_class_path.asset_name)
    except Exception:
        return str(d.asset_class)


def owner_materials(pkg):
    """Список путей материалов у меша-владельца (StaticMesh / SkeletalMesh)."""
    a = EAL.load_asset(pkg)
    if a is None:
        return ["<не загрузился>"]
    out = []
    if isinstance(a, unreal.StaticMesh):
        for sm in a.get_editor_property("static_materials"):
            mi = sm.get_editor_property("material_interface")
            out.append(mi.get_path_name() if mi else "None")
    elif isinstance(a, unreal.SkeletalMesh):
        for sm in a.get_editor_property("materials"):
            mi = sm.get_editor_property("material_interface")
            out.append(mi.get_path_name() if mi else "None")
    else:
        out.append("<%s>" % a.get_class().get_name())
    return out


def main():
    # ★ 16.09.2026 (ревизия раздела 22, МЕЛК-3): защита подключается ВНУТРИ функции, как в остальных 11 инструментах.
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_login_guard as _LG
    rows = json.load(open(MAP, encoding="utf-8"))
    AR.scan_paths_synchronous(["/Game/AsdaAssets/Models"], True)
    res = []
    unreal.log("=== режим %s, записей %d ===" % (MODE, len(rows)))
    # ---- аудит ДО
    for r in rows:
        old, new = r["old_obj"], r["new_obj"]
        e_old, e_new = EAL.does_asset_exist(old), EAL.does_asset_exist(new)
        refs = referencers(old) if e_old else []
        rec = dict(r, exists_old=e_old, exists_new_before=e_new, class_old=asset_class(old) if e_old else None,
                   refs_before=refs)
        res.append(rec)
        unreal.log("[до] %s (%s) cls=%s new_exists=%s refs=%s" % (old, r["ko"], rec["class_old"], e_new, refs))
    if MODE == "audit":
        json.dump(res, open(OUT, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
        unreal.log("=== ЗАВЕРШЕНО (audit) → %s ===" % OUT)
        return

    # ---- переименование штатно (rename_asset → редиректор на старом пути)
    for rec in res:
        old, new = rec["old_obj"], rec["new_obj"]
        if not rec["exists_old"] or rec["exists_new_before"]:
            rec["renamed"] = False; rec["why"] = "нет старого или уже есть новый"; continue
        ok = EAL.rename_asset(old, new)
        rec["renamed"] = bool(ok)
        unreal.log("[rename] %s -> %s : %s" % (old, new, ok))
    _LG.save_dir_login_safe("/Game/AsdaAssets/Models", "asda_rename_korean_mics.py")   # ★ раздел 22: без пакетов логина

    # ---- fixup редиректоров (перепривязать читателей на новые пути)
    AR.scan_paths_synchronous(["/Game/AsdaAssets/Models"], True)
    redirs = []
    for rec in res:
        if not rec.get("renamed"):
            continue
        d = EAL.find_asset_data(rec["old_obj"])
        rec["redirector_after_rename"] = bool(d and d.is_valid())
        if rec["redirector_after_rename"]:
            a = d.get_asset()
            if isinstance(a, unreal.ObjectRedirector):
                redirs.append(a)
    unreal.log("[fixup] редиректоров: %d" % len(redirs))
    if redirs:
        try:
            TOOLS.fixup_referencers(redirs, checkout_and_prompt=False,
                                    checkout_dialog_mode=unreal.RedirectorFixupMode.NO_CHECKOUT)
        except Exception as e:
            unreal.log_warning("fixup_referencers: %s" % e)
            # ★ раздел 22: в fixup отдаём только папки, под которыми пакетов логина нет; с ASDA_ALLOW_LOGIN_WRITE=1 — корень как был
            _folders_lg = _LG.login_filter_roots(["/Game/AsdaAssets/Models"], "asda_rename_korean_mics.py")
            if _folders_lg:
                TOOLS.fixup_referencers_in_folders(_folders_lg)
    _LG.save_dir_login_safe("/Game/AsdaAssets/Models", "asda_rename_korean_mics.py")   # ★ раздел 22: без пакетов логина

    # ---- аудит ПОСЛЕ + удаление редиректоров только при 0 ссылок
    AR.scan_paths_synchronous(["/Game/AsdaAssets/Models"], True)
    for rec in res:
        if not rec.get("renamed"):
            continue
        old, new = rec["old_obj"], rec["new_obj"]
        still = EAL.does_asset_exist(old)
        rec["redirector_exists_after_fixup"] = still
        rec["refs_old_after"] = referencers(old) if still else []
        rec["refs_new_after"] = referencers(new)
        rec["class_new"] = asset_class(new)
        if still and not rec["refs_old_after"]:
            rec["redirector_deleted"] = bool(EAL.delete_asset(old))
        else:
            rec["redirector_deleted"] = False
        # материалы владельцев — по новым путям?
        owners = {}
        for pkg in set(rec["refs_before"]) | set(rec["refs_new_after"]):
            owners[pkg] = owner_materials(pkg)
        rec["owner_materials_after"] = owners
        unreal.log("[после] %s refs_new=%s refs_old=%s redir_del=%s owners=%s"
                   % (new, rec["refs_new_after"], rec["refs_old_after"], rec["redirector_deleted"], owners))
    _LG.save_dir_login_safe("/Game/AsdaAssets/Models", "asda_rename_korean_mics.py")   # ★ раздел 22: без пакетов логина
    json.dump(res, open(OUT, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    n_ok = sum(1 for r in res if r.get("renamed"))
    unreal.log("=== ЗАВЕРШЕНО (rename): переименовано %d / %d → %s ===" % (n_ok, len(res), OUT))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
