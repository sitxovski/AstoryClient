# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_dt_json_point.py - ТОЧЕЧНАЯ правка ячеек DataTable со вложенными структурами через JSON (names11, 14.10.2026; в Tools/ с этапа 3).
#
# Зачем: у DT_NpcNsrPage / DT_QuestBook поля-массивы и вложенные структуры; CSV-заливка (asda_ml_dt_point.py) их не гарантирует, родная
#   заливка этих таблиц - fill_data_table_from_json_string (см. asda_import_nsr_dialog.py). Здесь тот же путь, но ТОЧЕЧНО:
#   1) экспорт DT в JSON (export_data_table_to_json_string);
#   2) сторож «было»: значение ячейки (или элемента массива / поля структуры) обязано равняться `was`; уже равно `now` - пропуск;
#      иное - СТОП, ассет не трогается;
#   3) правка ТОЛЬКО перечисленных ячеек, заливка fill_data_table_from_json_string;
#   4) сверка после: повторный экспорт отличается от исходного ровно в перечисленных ячейках, иначе ассет НЕ сохраняется;
#   5) сохранение и маркер ASDA_WRITES_ASSET для run_headless_safe --expect-asset.
# Задание: JSON-файл в ASDA_JP_JOBS: [{"dt": "/Game/...", "edits": [{"row","field","index"|null,"sub"|null,"was","now"}]}],
#   либо ASDA_JP_LEDGER=1 - задание строится из ведомости Tools/data/ru_dt_overrides.json (asda_ru_dt_overrides.py), все её DT.
# Сухой прогон: ASDA_JP_DRYRUN=1 (заливка в память, сохранение пропущено). Маркер раннера: «ЗАВЕРШЕНО: JSON-точечные DT».
# Запуск (редактор закрыт): python Tools/run_headless_safe.py Tools/asda_dt_json_point.py --marker "ЗАВЕРШЕНО: JSON-точечные DT"
import json
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def log(m):
    """строка в журнал UE"""
    unreal.log_warning("[dt-json-point] %s" % m)


def rows_of(js):
    """JSON экспорта -> список строк-словарей (ключ строки DataTable = поле Name)"""
    d = json.loads(js)
    return d if isinstance(d, list) else d.get("rows", d)


def cell(row, e):
    """(контейнер, ключ) ячейки правки или None"""
    f = e["field"]
    if f not in row:
        return None
    if e.get("index") is None:
        return row, f
    arr = row[f]
    if not isinstance(arr, list) or len(arr) <= e["index"]:
        return None
    if e.get("sub"):
        el = arr[e["index"]]
        return (el, e["sub"]) if isinstance(el, dict) and e["sub"] in el else None
    return arr, e["index"]


def load_jobs():
    """задание: файл ASDA_JP_JOBS или ведомость (ASDA_JP_LEDGER=1)"""
    if os.environ.get("ASDA_JP_LEDGER"):
        import asda_ru_dt_overrides as RO
        by = {}
        for e in RO.load():
            by.setdefault(e["dt"], []).append({k: e.get(k) for k in ("row", "field", "index", "sub", "was", "now")})
        return [{"dt": dt, "edits": es} for dt, es in by.items()]
    return json.load(open(os.environ["ASDA_JP_JOBS"], encoding="utf-8"))


def main():
    jobs = load_jobs()
    dry = bool(os.environ.get("ASDA_JP_DRYRUN"))
    ok_all = True
    for job in jobs:
        path = job["dt"]
        dt = unreal.EditorAssetLibrary.load_asset(path)
        js0 = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt)
        rows0 = rows_of(js0)
        by0 = {str(r["Name"]): r for r in rows0}
        todo = []
        bad = False
        for e in job["edits"]:
            r = by0.get(str(e["row"]))
            c = cell(r, e) if r is not None else None
            if c is None:
                log("СТОП %s: нет строки/поля %s.%s[%s]" % (path, e["row"], e["field"], e.get("index"))); bad = True; continue
            cur = c[0][c[1]]
            if cur == e["now"]:
                continue                                    # уже стоит
            if cur != e["was"]:
                log("СТОП %s %s.%s[%s]: в ассете %r, ждали %r" % (path, e["row"], e["field"], e.get("index"), cur, e["was"])); bad = True; continue
            todo.append(e)
        if bad:
            ok_all = False; continue
        if not todo:
            log("%s: правок нет (всё уже стоит), ассет не трогаю" % path)
            continue
        rows1 = json.loads(js0)
        by1 = {str(r["Name"]): r for r in rows1}
        for e in todo:
            box, k = cell(by1[str(e["row"])], e)
            box[k] = e["now"]
        js1 = json.dumps(rows1, ensure_ascii=False)
        res = unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(dt, js1)
        log("%s: fill_from_json -> %s, правок %d" % (path, res, len(todo)))
        rows2 = rows_of(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt))
        if len(rows2) != len(rows0):
            log("СТОП %s: число строк %d -> %d" % (path, len(rows0), len(rows2))); ok_all = False; continue
        want = {(str(e["row"]), e["field"], e.get("index")): (e["now"] if not e.get("sub") else {e["sub"]: e["now"]}) for e in todo}
        diffs = []
        for a, b in zip(rows0, rows2):
            for k in set(a) | set(b):
                if a.get(k) == b.get(k):
                    continue
                if isinstance(a.get(k), list) and isinstance(b.get(k), list) and len(a[k]) == len(b[k]):
                    for i, (x, y) in enumerate(zip(a[k], b[k])):
                        if x == y:
                            continue
                        if isinstance(x, dict) and isinstance(y, dict):
                            diffs.append((str(a["Name"]), k, i, {kk: y[kk] for kk in y if x.get(kk) != y[kk]}))
                        else:
                            diffs.append((str(a["Name"]), k, i, y))
                else:
                    diffs.append((str(a["Name"]), k, None, b.get(k)))
        unexpected = [d for d in diffs if want.get((d[0], d[1], d[2])) != d[3]]
        missing = [k for k in want if not any((d[0], d[1], d[2]) == k for d in diffs)]
        if unexpected or missing:
            log("СТОП %s: сверка не прошла: неожиданных %d, не применённых %d, пример %r" % (path, len(unexpected), len(missing), (unexpected or missing)[:2]))
            ok_all = False; continue
        log("СВЕРКА ОК %s: строк %d, изменено ячеек %d" % (path, len(rows2), len(diffs)))
        if dry:
            log("СУХОЙ ПРОГОН %s: сохранение пропущено" % path)
            continue
        unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        unreal.log_warning("ASDA_WRITES_ASSET: %s" % path)
    log("ЗАВЕРШЕНО: JSON-точечные DT, ok=%s" % ok_all)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
