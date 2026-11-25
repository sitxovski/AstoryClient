# Copyright (c) 2026 @sitxovski (Discord)
# asda_audit_import_sources.py — АУДИТ источников импорта у всех ассетов проекта + поиск двойников.
#
# ЗАЧЕМ. Редактор авто-импортирует ЛЮБОЙ .csv/.json, положенный внутрь `Content`, и заводит из него
# НОВЫЙ ассет, если ни один существующий ассет не объявил этот файл своим источником
# (`AssetImportData`). Так рядом с настоящим `DT_<Имя>` появляется двойник `<Имя>` с другим составом
# полей — он и даёт ошибки «Property 'X' … cannot be found in struct» при переимпорте.
# Разбор механизма: `Reports/callmob_table_fix.md`, `Reports/content_intermediate_cleanup.md`.
#
# ЧТО ДЕЛАЕТ:
#   1. по всем ассетам достаёт `AssetImportData` (тег реестра `AssetImportData`, JSON-массив) и
#      печатает те, чей источник — файл `.csv`/`.json`, ЛЕЖАЩИЙ ВНУТРИ `Content`;
#   2. ищет пары «<Имя>» + «DT_<Имя>» в одном каталоге — это признак двойника;
#   3. для каждого найденного двойника печатает число ссылающихся ассетов (AssetRegistry —
#      единственный авторитет: греп по .uasset даёт ложное «ссылок нет», см. правило
#      false-negatives-method) и число строк, чтобы сразу видеть расхождение с настоящей таблицей.
#
# ★ ПРАВИЛО tools-no-import-side-effects: при импорте модуля НИЧЕГО не выполняется.
# Запуск: Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_audit_import_sources.py
import json
import os
import unreal


def L(m):
    unreal.log_warning("[import-audit] %s" % m)


def content_dir():
    """Абсолютный путь к Content проекта с прямыми слэшами."""
    return os.path.abspath(unreal.Paths.project_content_dir()).replace("\\", "/")


def import_sources(asset_data):
    """Список исходных файлов ассета (абсолютные пути) из тега реестра AssetImportData."""
    tag = asset_data.get_tag_value("AssetImportData")
    if not tag:
        return []
    try:
        recs = json.loads(str(tag))
    except Exception:
        return []
    # Путь в AssetImportData ОТНОСИТЕЛЬНЫЙ — считается от каталога пакета на диске.
    out = []
    for r in recs or []:
        rel = (r or {}).get("RelativeFilename") or ""
        if not rel:
            continue
        out.append(rel)
    return out


def rows_of(path):
    dt = unreal.EditorAssetLibrary.load_asset(path)
    if not dt or not isinstance(dt, unreal.DataTable):
        return -1
    return len(unreal.DataTableFunctionLibrary.get_data_table_row_names(dt))


def main():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    ar.wait_for_completion()
    assets = ar.get_assets_by_path(unreal.Name("/Game"), True)
    L("ассетов в /Game: %d" % len(assets))

    # ---- 1. источники импорта, ведущие ВНУТРЬ Content -----------------------------------------
    bad_src = []
    for a in assets:
        pkg = str(a.package_name)
        for rel in import_sources(a):
            low = rel.lower()
            if not (low.endswith(".csv") or low.endswith(".json")):
                continue
            # rel считается от каталога пакета; «..» выводит наружу Content
            pkg_dir_on_disk = os.path.join(
                unreal.Paths.project_content_dir(), pkg.replace("/Game/", "", 1))
            full = os.path.abspath(os.path.join(os.path.dirname(pkg_dir_on_disk), rel))
            full = full.replace("\\", "/")
            inside = full.lower().startswith(content_dir().lower())
            bad_src.append((pkg, rel, full, inside))
    L("--- ассеты с источником импорта .csv/.json ---")
    n_inside = 0
    for pkg, rel, full, inside in sorted(bad_src):
        mark = "ВНУТРИ Content !!" if inside else "вне Content (ок)"
        if inside:
            n_inside += 1
        L("  %-58s %-18s %s" % (pkg.replace("/Game/AsdaAssets/", ""), mark, rel))
    L("всего таких ассетов: %d, из них источник ВНУТРИ Content: %d" % (len(bad_src), n_inside))

    # ---- 2. пары «<Имя>» и «DT_<Имя>» в одном каталоге --------------------------------------
    by_dir = {}
    for a in assets:
        pkg = str(a.package_name)
        d, n = pkg.rsplit("/", 1)
        by_dir.setdefault(d, set()).add(n)
    L("--- пары двойников «<Имя>» рядом с «DT_<Имя>» ---")
    dups = []
    for d, names in sorted(by_dir.items()):
        for n in sorted(names):
            if n.startswith("DT_") and n[3:] in names:
                dups.append("%s/%s" % (d, n[3:]))
    for p in dups:
        refs = ar.get_referencers(unreal.Name(p), unreal.AssetRegistryDependencyOptions())
        refs = [str(r) for r in (refs or [])]
        real = p.rsplit("/", 1)[0] + "/DT_" + p.rsplit("/", 1)[1]
        L("  ДВОЙНИК %-52s строк=%-7d ссылающихся=%-3d | настоящий строк=%d %s"
          % (p, rows_of(p), len(refs), rows_of(real), refs[:5]))
    L("двойников найдено: %d" % len(dups))

    # ---- 3. .csv/.json, физически лежащие внутри Content ------------------------------------
    cd = unreal.Paths.project_content_dir()
    files = []
    for dp, dn, fn in os.walk(cd):
        for f in fn:
            if f.lower().endswith((".csv", ".json")):
                files.append(os.path.join(dp, f).replace("\\", "/"))
    L("--- файлов .csv/.json физически внутри Content: %d ---" % len(files))
    for f in sorted(files):
        L("  %s" % f[len(cd):])


if __name__ == "__main__":
    main()
