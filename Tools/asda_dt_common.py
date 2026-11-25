# Copyright (c) 2026 @sitxovski (Discord)
# asda_dt_common.py — общий шаг «CSV → DataTable» для импортёров трёх таблиц эффектов/мобов.
# Повторяет конвенцию Tools/asda_build_plus_package_dt.py (DataTableFactory + fill_from_csv_string),
# вынесен отдельно, чтобы три импортёра не дублировали один и тот же код.
# ★ ПРАВИЛО tools-no-import-side-effects: модуль ничего не делает при импорте.
# unreal передаётся аргументом, чтобы модуль оставался импортируемым и вне редактора (для тестов).


def build_table(unreal, spec, tag):
    """spec — запись из asda_effect_mob_tables.TABLES: bin/parse/csv/csv_path/row_struct/pkg/name."""
    def log(m):
        unreal.log_warning("[%s] %s" % (tag, m))

    rows = spec["parse"](spec["bin"])
    csv_text = spec["csv"](rows)
    with open(spec["csv_path"], "w", encoding="utf-8", newline="") as f:
        f.write(csv_text)
    log("CSV: %d строк → %s" % (len(rows), spec["csv_path"]))

    rs = unreal.load_object(None, spec["row_struct"])
    if not rs:
        log("!! RowStruct %s НЕ НАЙДЕН — нужна СБОРКА C++ "
            "(Public/Data/AsdaEffectMobTableRows.h ещё не скомпилирован). DT не создан."
            % spec["row_struct"])
        return None

    full = "%s/%s" % (spec["pkg"], spec["name"])
    dt = unreal.EditorAssetLibrary.load_asset(full) if unreal.EditorAssetLibrary.does_asset_exist(full) else None
    if not dt:
        factory = unreal.DataTableFactory()
        factory.set_editor_property("struct", rs)
        dt = unreal.AssetToolsHelpers.get_asset_tools().create_asset(spec["name"], spec["pkg"],
                                                                     unreal.DataTable, factory)
    if not dt:
        log("!! DataTable %s не создан" % spec["name"])
        return None
    # ★ ИСПРАВЛЕНИЕ (дедуп №2 В2 Т4): провал заливки → DT НЕ сохраняется (раньше сохранялась молча).
    problems = fill_and_save(unreal, dt, csv_text, log, fmt="csv")
    log("%s: строк=%d (в .bin %d), заполнение=%s"
        % (spec["name"], len(dt.get_row_names()), len(rows), problems))
    return dt if problems else None


# ════════════════════════════════════════════════════════════════════════════════════════════════
#  ★ ДЕДУП Д10 (аудит `Reports/quest_npc_dedup_audit.md`) — ОБЩИЙ JSON-ПУТЬ «строки → DataTable».
#  Эти два тела были СКОПИРОВАНЫ в шесть импортёров таблиц NPC и квестов слово в слово; отличались
#  только `PKG`, функция `log` и текст подсказки «RowStruct не найден». Теперь тело ОДНО, а всё
#  различающееся приходит аргументами.
#  ★ ПРАВИЛО `tools-no-import-side-effects` соблюдено: модуль по-прежнему ничего не делает при
#    импорте, `unreal` передаётся аргументом (значит модуль импортируется и вне редактора).
# ════════════════════════════════════════════════════════════════════════════════════════════════


def json_rows(rows, key_fn):
    """Список словарей-строк → JSON для `fill_data_table_from_json_string`.

    Ключ строки DataTable кладётся в поле "Name" — так его читает UE. `key_fn(r)` возвращает
    этот ключ (у одних таблиц это имя `.NSR`, у других — пара «имя_индекс», у третьих — номер).
    `indent=0` — как было во всех шести копиях: перенос строк есть, отступов нет (файлы крупные).
    """
    import json
    out = []
    for r in rows:
        d = dict(r)
        d["Name"] = key_fn(r)
        out.append(d)
    return json.dumps(out, ensure_ascii=False, indent=0)


def open_or_create_dt(unreal, pkg, name, row_struct):
    """Открыть DataTable `pkg/name` или создать новую по RowStruct `row_struct` (объект UScriptStruct).

    Общий «открыть-или-создать» (дедуп №2 В2 Т4): тело 1:1 с прежними копиями в сборщиках таблиц —
    `load_asset`, если ассет есть; иначе `DataTableFactory` + `create_asset`. Возвращает DT или None.
    """
    full = "%s/%s" % (pkg, name)
    dt = unreal.EditorAssetLibrary.load_asset(full) if unreal.EditorAssetLibrary.does_asset_exist(full) else None
    if not dt:
        factory = unreal.DataTableFactory()
        factory.set_editor_property("struct", row_struct)
        dt = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, pkg, unreal.DataTable, factory)
    return dt


def dt_package_path(dt):
    """'/Game/…/DT_X.DT_X' → '/Game/…/DT_X' (так путь понимает `run_headless_safe` в строке ASDA_WRITES_ASSET)."""
    return str(dt.get_path_name()).split(".", 1)[0]


def fill_and_save(unreal, dt, text, log, fmt="csv", save_on_fail=False):
    """Залить `text` в DataTable `dt` и сохранить ТОЛЬКО при успешной заливке. Возвращает bool «залито и записано».

    ★ ИСПРАВЛЕНИЕ (дедуп №2 В2 Т4, находка Т4 аудита `Reports/dedup_audit2/04_tools_python.md`): прежде
      `fill_dt`/`build_table` и ~15 своих копий в сборщиках сохраняли DT при ЛЮБОМ исходе заливки — таблица, у
      которой разбор CSV/JSON провалился (`fill_data_table_from_*_string` вернул False), молча писалась на диск.
      Теперь провал = СТОП до сохранения (как уже делали `asda_build_fishbook_dt`, `import_text_lang`,
      `import_text_tables.import_one`).
    fmt: "csv" → `fill_data_table_from_csv_string`, "json" → `fill_data_table_from_json_string`.
    save_on_fail: True — прежнее поведение (сохранять и при провале); оставлено только как явный откат.
    Перед сохранением печатается «ASDA_WRITES_ASSET: <путь>» — по ней `run_headless_safe.py` проверяет, что файл
      ассета действительно обновился за прогон (закон asset-save-blocked-by-running-game). Сохранение —
      `save_loaded_asset(dt, False)` (без условия «грязный»): объявленный ассет обязан получить свежую дату.
    """
    lib = unreal.DataTableFunctionLibrary
    if fmt == "csv":
        ok = lib.fill_data_table_from_csv_string(dt, text)
    elif fmt == "json":
        ok = lib.fill_data_table_from_json_string(dt, text)
    else:
        raise ValueError("fill_and_save: fmt=%r — допустимо 'csv' или 'json'" % (fmt,))
    path = dt_package_path(dt)
    if not ok and not save_on_fail:
        log("!! %s: заливка %s НЕ прошла (подробности — LogDataTable выше) — DT НЕ сохранена" % (path, fmt.upper()))
        return False
    unreal.log("ASDA_WRITES_ASSET: %s" % path)
    if not unreal.EditorAssetLibrary.save_loaded_asset(dt, False):
        log("!! %s: сохранение НЕ прошло" % path)
        return False
    return bool(ok)


def fill_dt(unreal, pkg, name, row_struct_path, text, log, fmt="json", missing_hint="", save_on_fail=False):
    """Создать (или открыть) DataTable `pkg/name` по RowStruct и залить в него `text`.

    fmt: "json" → `fill_data_table_from_json_string`, "csv" → `fill_data_table_from_csv_string`.
    missing_hint: чем дополнить сообщение, если RowStruct ещё не скомпилирован (у каждого
        импортёра свой заголовок C++ — это единственное, что в сообщении различалось).
    save_on_fail: см. `fill_and_save` (по умолчанию провал заливки = не сохранять).
    Возвращает объект DataTable либо None (нет RowStruct, DT не создан, ★ заливка/сохранение не прошли).
        Вызывающий сам решает, во что это превратить (часть импортёров отдаёт True/False — их тонкие
        обёртки это и делают).
    """
    rs = unreal.load_object(None, row_struct_path)
    if not rs:
        log("!! RowStruct %s НЕ НАЙДЕН — нужна СБОРКА C++%s. Текст записан на диск; "
            "прогнать скрипт повторно." % (row_struct_path, (" (%s)" % missing_hint) if missing_hint else ""))
        return None
    dt = open_or_create_dt(unreal, pkg, name, rs)
    if not dt:
        log("!! DataTable %s не создан" % name)
        return None
    # ★ ИСПРАВЛЕНИЕ (дедуп №2 В2 Т4): провал заливки → DT НЕ сохраняется, вызывающему — None.
    problems = fill_and_save(unreal, dt, text, log, fmt="csv" if fmt == "csv" else "json", save_on_fail=save_on_fail)
    log("%s: строк=%d, замечания=%s" % (name, len(dt.get_row_names()), problems))
    return dt if (problems or save_on_fail) else None


def rewrite_header_hex(src_csv, dst_csv):
    """Копия CSV item-БД с заменой в ПЕРВОЙ строке колонки `_data_hex` → `DataHex` (имя UPROPERTY; иначе колонка
    не смапится), остальное — байт в байт (потоково: item_list ~33 МБ). Колонка-ключ `_row` не трогается — UE берёт
    первую колонку как RowName по позиции.

    ★ Дедуп №2 В2 Т9: одна реализация вместо пяти (4 точные копии в `_import_one_itemdb_table`,
    `asda_reimport_item_list`, `asda_reimport_item_tables`, `asda_reimport_itemdb_union` и вариант
    `gen_itemdb_datatables` с тем же итогом)."""
    import io
    import os
    os.makedirs(os.path.dirname(dst_csv), exist_ok=True)
    with io.open(src_csv, 'r', encoding='utf-8', newline='') as fi, \
         io.open(dst_csv, 'w', encoding='utf-8', newline='') as fo:
        fo.write(fi.readline().replace('_data_hex', 'DataHex'))
        while True:
            chunk = fi.read(1 << 20)
            if not chunk:
                break
            fo.write(chunk)
