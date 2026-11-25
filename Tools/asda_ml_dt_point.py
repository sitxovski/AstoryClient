# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ml_dt_point.py — ТОЧЕЧНАЯ перезаливка DataTable из нового CSV с двумя сторожами (мультиязычность, 09.10.2026).
#
# Зачем: сборщики DT порта (asda_build_*_dt.py, import_text_tables) пересоздают таблицу целиком; так нельзя, если в
#   ассете могла быть ручная правка владельца (закон owner-asset-edits-never-lose). Этот шаг:
#   1) СТОРОЖ РУЧНОЙ ПРАВКИ: экспорт DT сравнивается с ПРЕЖНИМ CSV-источником (`csv_old`, обычно бэкап до правки) —
#      значения должны совпасть (с нормализацией формата: bool без регистра, числа по значению, \n ↔ перевод строки);
#      расхождение = ассет правили руками → СТОП, таблица не трогается;
#   2) заливка нового CSV (`fill_data_table_from_csv_string`) и СВЕРКА ПОСЛЕ: изменились ТОЛЬКО разрешённые колонки
#      (`allow_cols`) и/или строки (`allow_rows`), иначе ассет НЕ сохраняется.
# Задания — JSON-файл в переменной ASDA_PT_JOBS: [{"dt": "/Game/...", "csv_new": "...", "csv_old": "...",
#   "allow_cols": [..], "allow_rows": [..]}]. Пустые allow_* = «любые» по этой оси (но хотя бы одна ось задана).
# Сухой прогон: ASDA_PT_DRYRUN=1. Маркер раннера: «ЗАВЕРШЕНО: точечные DT».
# Запуск (редактор закрыт, -game не запущен):
#   python Tools/run_headless_safe.py Tools/asda_ml_dt_point.py --marker "ЗАВЕРШЕНО: точечные DT"
import csv
import io
import json
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ledger as _L  # noqa: E402  ★ дедуп №2 В2 Т8: общая нормализация ячеек ведомостей/точечной заливки


def log(m):
    """Строка в журнал UE с префиксом шага."""
    unreal.log_warning("[dt-point] %s" % m)


# Нормализованное значение ячейки для сравнения источника с экспортом ассета — тело перенесено дословно в
#   `asda_ledger.norm_typed` (★ дедуп №2 В2 Т8).
norm = _L.norm_typed


def table(text, keycol=None):
    """CSV-текст → (колонки без ключа, {ключ: {колонка: значение}}). Ключ — первая колонка."""
    rows = list(csv.reader(io.StringIO(text)))
    head = rows[0]
    out = {}
    for r in rows[1:]:
        if not r:
            continue
        out[r[0]] = dict(zip(head[1:], r[1:]))
    return head[1:], out


def run_job(j, dry):
    """Одно задание. Возврат True — записано (или сухо проверено), False — стоп."""
    dt = unreal.EditorAssetLibrary.load_asset(j["dt"])
    if not dt:
        log("СТОП: нет ассета %s" % j["dt"])
        return False
    lib = unreal.DataTableFunctionLibrary
    before_txt = lib.export_data_table_to_csv_string(dt)
    cols, before = table(before_txt)
    old_cols, old = table(open(j["csv_old"], encoding="utf-8-sig").read())
    new_txt = open(j["csv_new"], encoding="utf-8-sig").read()
    new_cols, new = table(new_txt)
    common = [c for c in old_cols if c in cols]
    # 1) сторож ручной правки: ассет == прежний источник
    # ★ 05.11.2026 (редактор бустер-шопа, Tools/BoosterShopEditor): ASDA_PT_RESUME=1 — повторный прогон после частичной
    #   заливки. Строка из allow_rows, которая в ассете уже РАВНА новому CSV (или удалена в обоих), — уже применена этим же
    #   заданием, а не ручная правка. Без ключа — прежнее поведение.
    resume = bool(os.environ.get("ASDA_PT_RESUME"))
    rows_ok = set(str(x) for x in (j.get("allow_rows") or []))

    def applied(k):
        """Строка k уже приведена к новому CSV (режим повтора)."""
        if not resume or k not in rows_ok:
            return False
        a, n = before.get(k), new.get(k)
        if a is None or n is None:
            return a is None and n is None
        return all(norm(a.get(c)) == norm(n.get(c)) for c in new_cols if c in cols)

    bad = []
    for k in sorted(set(before) | set(old)):
        if applied(k):
            continue
        a, b = before.get(k), old.get(k)
        if a is None or b is None:
            bad.append("%s: строка только в %s" % (k, "ассете" if b is None else "источнике"))
            continue
        for c in common:
            if norm(a.get(c)) != norm(b.get(c)):
                bad.append("%s.%s: ассет «%s» ≠ источник «%s»" % (k, c, a.get(c), b.get(c)))
    if bad:
        log("СТОП %s: ассет расходится с прежним источником (ручная правка?) — %d: %s" % (j["dt"], len(bad), bad[:8]))
        return False
    # 2) что меняет новый CSV — только разрешённое
    acols, arows = set(j.get("allow_cols") or []), set(str(x) for x in (j.get("allow_rows") or []))
    chg = []
    for k in sorted(set(old) | set(new)):
        a, b = old.get(k), new.get(k)
        if a is None or b is None:
            chg.append((k, "*"))
            continue
        for c in new_cols:
            if norm(a.get(c)) != norm(b.get(c)):
                chg.append((k, c))
    out = [(k, c) for k, c in chg if (acols and c not in acols and c != "*") or (arows and k not in arows)
           or (c == "*" and not arows)]
    log("%s: изменений %d %s" % (j["dt"], len(chg), chg[:20]))
    if out:
        log("СТОП %s: изменения вне разрешённого %s" % (j["dt"], out[:10]))
        return False
    if dry:
        log("СУХОЙ ПРОГОН %s: сторожа пройдены, запись пропущена" % j["dt"])
        return True
    if not lib.fill_data_table_from_csv_string(dt, new_txt):
        log("СТОП %s: заливка вернула ошибку" % j["dt"])
        return False
    _, after = table(lib.export_data_table_to_csv_string(dt))
    miss = [k for k in new if k not in after] + [k for k in after if k not in new]
    for k in new:
        if k in after:
            for c in new_cols:
                if c in cols and norm(after[k].get(c)) != norm(new[k].get(c)):
                    miss.append("%s.%s" % (k, c))
    if miss:
        log("СТОП %s: после заливки ассет ≠ новый CSV %s — НЕ сохраняется" % (j["dt"], miss[:10]))
        return False
    unreal.log("ASDA_WRITES_ASSET: %s" % j["dt"])
    if not unreal.EditorAssetLibrary.save_loaded_asset(dt, False):
        log("СТОП %s: не сохранён" % j["dt"])
        return False
    log("СВЕРКА ОК %s: строк %d, изменено ячеек %d" % (j["dt"], len(after), len(chg)))
    # ★ 05.11.2026: копия прошлого импорта — те же ячейки, иначе `import_text_lang.py` примет заливку за ручную правку. Не оверлей — пропуск.
    _L.overlay_snapshot_apply(j["dt"], {k: new[k].get("Text") if k in new else None for k, _ in chg}, log)
    return True


def main():
    """Выполнить все задания из ASDA_PT_JOBS; при первом стопе остальные не выполняются."""
    jobs = json.load(open(os.environ["ASDA_PT_JOBS"], encoding="utf-8"))
    dry = bool(os.environ.get("ASDA_PT_DRYRUN"))
    ok = 0
    for j in jobs:
        if not run_job(j, dry):
            break
        ok += 1
    log("ЗАВЕРШЕНО: точечные DT, заданий %d, выполнено %d%s" % (len(jobs), ok, " [СУХОЙ]" if dry else ""))
    if ok != len(jobs):
        unreal.log_error("[dt-point] НЕ ВСЕ ЗАДАНИЯ ВЫПОЛНЕНЫ")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
