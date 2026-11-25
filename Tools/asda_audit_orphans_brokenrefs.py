# Copyright (c) 2026 @sitxovski (Discord)
# asda_audit_orphans_brokenrefs.py — аудит битых ссылок и сирот по AssetRegistry (надёжнее байт-скана).
# Источник истины — реестр ссылок движка. Пишет JSON в Reports/audit_orphans_*.json.
# ТОЛЬКО ЧТЕНИЕ: ничего не удаляет и не сохраняет.
import json, os
import unreal

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports"
MARK = "AUDIT_ORPHANS_DONE"


def main():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    ar.wait_for_completion()

    # 1) Все ассеты под /Game -> множество существующих имён пакетов.
    assets = ar.get_assets_by_path(unreal.Name("/Game"), recursive=True, include_only_on_disk_assets=True)
    existing = set()
    pkg_class = {}
    for a in assets:
        pn = str(a.package_name)
        existing.add(pn)
        try:
            pkg_class[pn] = str(a.asset_class_path.asset_name)
        except Exception:
            pkg_class[pn] = str(getattr(a, "asset_class", ""))
    unreal.log_warning("[audit] пакетов под /Game: %d" % len(existing))

    opt = unreal.AssetRegistryDependencyOptions(
        include_soft_package_references=True,
        include_hard_package_references=True,
        include_searchable_names=False,
        include_soft_management_references=False,
        include_hard_management_references=False,
    )

    broken = {}            # referencer -> [missing dep,...]
    missing_targets = {}   # missing dep -> count referencers
    referenced = set()     # any /Game package that is depended-upon

    n = 0
    for pn in existing:
        n += 1
        deps = ar.get_dependencies(unreal.Name(pn), opt) or []
        miss = []
        for d in deps:
            ds = str(d)
            if not ds.startswith("/Game/"):
                continue
            referenced.add(ds)
            if ds not in existing:
                miss.append(ds)
                missing_targets[ds] = missing_targets.get(ds, 0) + 1
        if miss:
            broken[pn] = sorted(set(miss))
        if n % 5000 == 0:
            unreal.log_warning("[audit] обработано %d/%d, битых ссылок пока: %d" % (n, len(existing), len(broken)))

    # 2) Сироты: пакет под /Game, на который никто из /Game не ссылается.
    orphans = sorted(existing - referenced)

    # Сгруппировать сирот по верхней папке для обзора.
    orphan_by_dir = {}
    for pn in orphans:
        parts = pn.split("/")
        top = "/".join(parts[:5]) if len(parts) >= 5 else pn
        orphan_by_dir.setdefault(top, 0)
        orphan_by_dir[top] += 1

    out = {
        "total_packages": len(existing),
        "broken_ref_count_referencers": len(broken),
        "missing_target_count": len(missing_targets),
        "orphan_count": len(orphans),
        "missing_targets": dict(sorted(missing_targets.items(), key=lambda kv: -kv[1])),
        "broken": broken,
        "orphan_by_dir": dict(sorted(orphan_by_dir.items(), key=lambda kv: -kv[1])),
        "orphans": orphans,
    }
    os.makedirs(OUT_DIR, exist_ok=True)
    p1 = os.path.join(OUT_DIR, "audit_orphans_brokenrefs.json")
    with open(p1, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    unreal.log_warning("[audit] ЗАПИСАНО %s" % p1)
    unreal.log_warning("[audit] БИТЫХ целей=%d, ссылающихся=%d, сирот=%d, всего=%d"
                       % (len(missing_targets), len(broken), len(orphans), len(existing)))
    unreal.log_warning(MARK)


if __name__ == "__main__":
    main()
