# Copyright (c) 2026 @sitxovski (Discord)
# asda_audit_material_phantoms.py — ищем фантомные узлы в мастер-материалах (баг UE 5.8
# DeleteAllMaterialExpressions): дубли одноимённых параметров и узлы без связей.
# ТОЛЬКО ЧТЕНИЕ. Пишет JSON. Память ue-delete-all-material-expressions-bug.
import json, os
import unreal

OUT = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/audit_material_phantoms.json"
MARK = "AUDIT_MATPHANTOM_DONE"

FOLDERS = ["/Game/Materials", "/Game/Effects", "/Game/AsdaAssets/Effects", "/Game/AsdaAssets/Materials"]


def get_exprs(mat):
    # Пробуем разные API-пути (5.x меняли расположение выражений).
    try:
        return list(unreal.MaterialEditingLibrary.get_material_expressions(mat) or [])
    except Exception:
        pass
    for prop in ("expressions",):
        try:
            v = mat.get_editor_property(prop)
            if v:
                return list(v)
        except Exception:
            pass
    try:
        eod = mat.get_editor_property("editor_only_data")
        if eod:
            v = eod.get_editor_property("expression_collection")
            exprs = v.get_editor_property("expressions")
            return list(exprs or [])
    except Exception:
        pass
    return None


def main():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    ar.wait_for_completion()
    results = {}
    for folder in FOLDERS:
        assets = ar.get_assets_by_path(unreal.Name(folder), recursive=True)
        for a in assets:
            cls = ""
            try:
                cls = str(a.asset_class_path.asset_name)
            except Exception:
                cls = str(getattr(a, "asset_class", ""))
            if cls != "Material":
                continue
            pn = str(a.package_name)
            name = pn.split("/")[-1]
            if not name.startswith("M_"):
                continue
            mat = unreal.load_asset(pn)
            if mat is None:
                results[pn] = {"error": "load_failed"}
                continue
            exprs = get_exprs(mat)
            if exprs is None:
                results[pn] = {"error": "no_expr_api"}
                continue
            total = len(exprs)
            param_names = {}
            unconnected = 0
            for e in exprs:
                try:
                    pnm = e.get_editor_property("parameter_name")
                    if pnm:
                        s = str(pnm)
                        param_names[s] = param_names.get(s, 0) + 1
                except Exception:
                    pass
            dup_params = {k: v for k, v in param_names.items() if v > 1}
            results[pn] = {"total_expr": total, "dup_params": dup_params}
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(results, f, ensure_ascii=False, indent=1)
    flagged = {k: v for k, v in results.items() if v.get("dup_params") or v.get("error")}
    unreal.log_warning("[matphantom] мастеров=%d, с дублями/ошибкой=%d" % (len(results), len(flagged)))
    for k, v in flagged.items():
        unreal.log_warning("[matphantom] %s -> %s" % (k, v))
    unreal.log_warning("[matphantom] ЗАПИСАНО %s" % OUT)
    unreal.log_warning(MARK)


if __name__ == "__main__":
    main()
