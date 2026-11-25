# Copyright (c) 2026 @sitxovski (Discord)
import unreal, os, csv, io
import os as _os_dtc  # noqa: E402  ★ дедуп №2 В2 Т4: общий шаг «заливка → сохранение» DataTable
import sys as _sys_dtc  # noqa: E402
_sys_dtc.path.insert(0, _os_dtc.path.dirname(_os_dtc.path.abspath(__file__)))
import asda_dt_common as _DTC  # noqa: E402
AT = unreal.AssetToolsHelpers.get_asset_tools(); EAL = unreal.EditorAssetLibrary
SRC = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/RawAssets/Text"
DEST = "/Game/AsdaAssets/Text"


def _dt_cells(dt, cols):
    """Ячейки DT: {RowName: {колонка: строка}} (чтение `get_data_table_column_as_string`)."""
    lib = unreal.DataTableFunctionLibrary
    names = [str(n) for n in lib.get_data_table_row_names(dt)]
    out = {n: {} for n in names}
    for c in cols:
        vals = list(lib.get_data_table_column_as_string(dt, c))
        for n, v in zip(names, vals):
            out[n][c] = str(v)
    return out


def import_one(rel, allow_rows):
    """★ six_items МЕЛК-3 (09.10.2026): ТОЧЕЧНЫЙ импорт одной таблицы `rel` («Категория/Имя» без DT_ и .csv).
    Гейт ДО записи (закон owner-asset-edits-never-lose): DT читается и сравнивается с новым CSV — расхождения допустимы
    ТОЛЬКО в строках `allow_rows` (ровно те, что меняет ведомость); любое другое отличие = ручная правка в ассете →
    СТОП, DT не трогается. После заливки — сверка обратным чтением со всем CSV, только потом сохранение."""
    cat, base = rel.split("/", 1)
    csvp = os.path.join(SRC, cat, base + ".csv")
    full = "%s/%s/DT_%s" % (DEST, cat, base)
    text = open(csvp, encoding="utf-8-sig").read()
    rows = list(csv.reader(io.StringIO(text)))
    hdr, body = rows[0], rows[1:]
    cols = hdr[1:]
    want = {r[0]: dict(zip(cols, r[1:])) for r in body}
    dt = unreal.load_object(None, full) if EAL.does_asset_exist(full) else None
    if not dt:
        unreal.log_warning("СТОП: нет %s — точечный импорт только по существующему ассету" % full); return
    have = _dt_cells(dt, cols)
    diff = sorted(set(want) ^ set(have)) + sorted(k for k in want if k in have and want[k] != have[k])
    extra = [k for k in diff if k not in allow_rows]
    unreal.log_warning("ДО записи %s: строк DT %d, CSV %d, отличаются %s (разрешены %s)" % (
        full, len(have), len(want), diff, sorted(allow_rows)))
    if extra:
        unreal.log_warning("СТОП: отличия вне ведомости %s — похоже на ручную правку ассета, DT НЕ записан" % extra); return
    if not unreal.DataTableFunctionLibrary.fill_data_table_from_csv_string(dt, text):
        unreal.log_warning("СТОП: заливка CSV вернула ошибку — DT НЕ сохранён"); return
    back = _dt_cells(dt, cols)
    bad = [k for k in want if back.get(k) != want[k]] + [k for k in back if k not in want]
    if bad:
        unreal.log_warning("СВЕРКА РАСХОЖДЕНИЕ после заливки: %s — DT НЕ сохранён" % bad); return
    if not EAL.save_loaded_asset(dt):
        unreal.log_warning("СТОП: %s не сохранён" % full); return
    unreal.log_warning("ASDA_WRITES_ASSET: %s" % full)
    unreal.log_warning("СВЕРКА ОК %s: строк %d, изменено %d (%s)" % (full, len(back), len(diff), diff))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    struct = unreal.load_object(None, "/Script/AsdaReborn.AsdaTextRow")
    if not struct: unreal.log_error("FAsdaTextRow НЕ найден")
    # ★ six_items (09.10.2026): ASDA_TEXT_ONLY="Fishing/Fish_collection_data" + ASDA_TEXT_ALLOW_ROWS="5,6,7,8" —
    #   точечный импорт одной таблицы с гейтом ручных правок (см. `import_one`); массовый проход ниже не выполняется.
    only = os.environ.get("ASDA_TEXT_ONLY", "")
    if only:
        allow = {s.strip() for s in os.environ.get("ASDA_TEXT_ALLOW_ROWS", "").split(",") if s.strip()}
        import_one(only, allow)
        print("ЗАВЕРШЕНО")
        return
    # Орфаны: таблицы, ставшие ПУСТЫМИ в этой сборке (Fpremium: файл 20б/count=0) — удалить ассет, если остался.
    # ★ 2026-09-02: `Craft/DT_making_recipe` — таблица рецептов уехала в item-БД
    #   (`/Game/AsdaAssets/Data/ItemDB/DT_making_recipe`, реестр Tools/decode_item_db.py), из
    #   TABLES экстрактора запись удалена, CSV `RawAssets/Text/Craft/making_recipe.csv` убран.
    #   Здесь дописан старый ассет, чтобы у одного игрового файла не осталось двух таблиц.
    for orphan in ("Shop/DT_Fpremium", "Craft/DT_making_recipe"):
        full = "%s/%s" % (DEST, orphan)
        if EAL.does_asset_exist(full):
            EAL.delete_asset(full); unreal.log("ORPHAN удалён %s" % full)
    n = 0
    for cat in sorted(os.listdir(SRC)):
        catdir = os.path.join(SRC, cat)
        if not os.path.isdir(catdir): continue
        for fn in sorted(os.listdir(catdir)):
            if not fn.lower().endswith(".csv"): continue
            name = "DT_" + os.path.splitext(fn)[0]
            pkg = "%s/%s" % (DEST, cat)
            full = "%s/%s" % (pkg, name)
            # Существующий ассет НЕ удаляем (в headless delete_asset ненадёжен) — загружаем и перезаполняем.
            dt = None
            if EAL.does_asset_exist(full):
                dt = unreal.load_object(None, full)
            if not dt:
                fac = unreal.DataTableFactory(); fac.set_editor_property("struct", struct)
                dt = AT.create_asset(name, pkg, unreal.DataTable, fac)
            if not dt: unreal.log_warning("не создан/загружен %s" % full); continue
            csv_str = open(os.path.join(catdir, fn), encoding="utf-8-sig").read()
            _filled = _DTC.fill_and_save(unreal, dt, csv_str, unreal.log_warning, fmt="csv")  # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т4: провал заливки → DT не сохраняется; маркер ASDA_WRITES_ASSET | заменяет все строки
            rows = len(unreal.DataTableFunctionLibrary.get_data_table_row_names(dt))
            unreal.log("IMPORTED %s/%s строк=%d" % (cat, name, rows))
            n += 1
    unreal.log("=== ТЕКСТ-АССЕТОВ импортировано=%d" % n)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
