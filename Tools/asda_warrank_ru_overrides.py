# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_warrank_ru_overrides.py — ТОЧЕЧНОЕ применение ведомости RU-названий званий к DT_WarRank (10.10.2026,
#   отчёт `Reports/owner_1010/esc_ranks.md`).
#
# Ведомость: Tools/data/warrank_ru_overrides.json (ключ = RankIndex, поле Title). Для каждой записи:
#   1) источник RawAssets/Data/Character/WarRank.json — меняется ТОЛЬКО поле Title записи с этим RankIndex; пишем лишь
#      если текущее = `was` или уже = `value`, иначе СТОП (чужая правка). Формат файла (отступы, концы строк) сохраняется;
#   2) ассет /Game/AsdaAssets/Data/Character/DT_WarRank — экспорт в CSV-строку, замена Title нужных строк, заливка обратно
#      (`fill_data_table_from_csv_string`), затем сверка: число строк то же, отличаются ровно строки ведомости и только
#      полем Title. Без полного переимпорта (`asda_import_war_shop.py` перезаливает и DT_WarShopList) — закон
#      owner-asset-edits-never-lose.
# Сухой прогон: ASDA_WARRANK_DRYRUN=1 — только сверка и печать, без записи.
# Маркер для run_headless_safe.py: «ЗАВЕРШЕНО: ведомость званий».
# Запуск: python Tools/run_headless_safe.py Tools/asda_warrank_ru_overrides.py --marker "ЗАВЕРШЕНО: ведомость званий"
#   --expect-asset /Game/AsdaAssets/Data/Character/DT_WarRank
import csv
import io
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ledger as _L  # noqa: E402  ★ дедуп №2 В2 Т8: одно чтение ведомостей, одна семантика ячейки, одна точечная заливка DT

LEDGER = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools/data/warrank_ru_overrides.json"           # ведомость
JSON_PATH = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/RawAssets/Data/Character/WarRank.json"       # источник таблицы
DT_PATH = "/Game/AsdaAssets/Data/Character/DT_WarRank"                                 # ассет


def load_ledger():
    """Строки ведомости [{key, value, was, why, source}] — общий читатель `asda_ledger.read_rows` (★ дедуп №2 В2 Т8)."""
    return _L.read_rows(LEDGER)


def apply_to_source_json(rows, dry, log):
    """Точечная правка WarRank.json: только строки `"Title": …` записей ведомости. Возврат: число изменённых."""
    raw = open(JSON_PATH, "rb").read()
    text = raw.decode("utf-8")
    data = json.loads(text)
    by_idx = {int(r["RankIndex"]): r for r in data}
    eol = "\r\n" if "\r\n" in text else "\n"
    lines = text.split(eol)
    changed = 0
    for r in rows:
        k = int(r["key"])
        rec = by_idx.get(k)
        if rec is None:
            raise RuntimeError("WarRank.json: записи %d нет — стоп" % k)
        cur = rec["Title"]
        cls = _L.classify_row(r, cur)        # ★ дедуп №2 В2 Т8: общая семантика ячейки (+ поле prev, как у GameText)
        if cls == _L.ALREADY:
            log("JSON %d: уже «%s» — без записи" % (k, cur))
            continue
        if cls != _L.APPLY:
            raise RuntimeError("WarRank.json %d: текущее «%s» ≠ ведомостному прежнему «%s» — чужая правка, стоп" % (k, cur, r["was"]))
        # строка Title этой записи: ищем блок с `"RankIndex": k,` и первую строку `"Title":` после него
        start = [i for i, l in enumerate(lines) if l.strip() == '"RankIndex": %d,' % k]
        if len(start) != 1:
            raise RuntimeError("WarRank.json: блок записи %d найден %d раз — стоп" % (k, len(start)))
        ti = next(i for i in range(start[0], len(lines)) if lines[i].lstrip().startswith('"Title":'))
        old_line = lines[ti]
        if old_line.strip() != '"Title": %s,' % json.dumps(cur, ensure_ascii=False):
            raise RuntimeError("WarRank.json %d: строка Title не та «%s» — стоп" % (k, old_line))
        lines[ti] = old_line[:len(old_line) - len(old_line.lstrip())] + '"Title": %s,' % json.dumps(r["value"], ensure_ascii=False)
        log("JSON %d: «%s» → «%s»%s" % (k, cur, r["value"], " [СУХОЙ]" if dry else ""))
        changed += 1
    if changed and not dry:
        out = eol.join(lines)
        if json.loads(out) is None:
            raise RuntimeError("WarRank.json после правки не разбирается — стоп")
        open(JSON_PATH, "wb").write(out.encode("utf-8"))
    return changed


def apply_to_asset(rows, dry, log):
    """Точечная правка DT_WarRank через экспорт/заливку CSV-строки. Возврат: число изменённых строк.

    ★ дедуп №2 В2 Т8: тело — общая `asda_ledger.point_fill_dt` (то же, что у DT_GameText): прежняя строгая сверка этой
      копии (ровно строки ведомости, только поле Title, значение ведомости) сохранена, добавлено «ни одна строка не
      пропала»; новые строки ведомость званий не заводит (allow_new=False — как было)."""
    import unreal
    return _L.point_fill_dt(unreal, DT_PATH, rows, "Title", log, dry, label="DT_WarRank")


def main():
    """Применить ведомость к источнику JSON и ассету (вне редактора — только JSON)."""
    try:
        import unreal
        log = unreal.log
    except ImportError:
        unreal = None
        log = print
    dry = bool(os.environ.get("ASDA_WARRANK_DRYRUN"))
    rows = load_ledger()
    nj = apply_to_source_json(rows, dry, log)
    na = apply_to_asset(rows, dry, log) if unreal else 0
    log("ЗАВЕРШЕНО: ведомость званий%s, записей %d, JSON изменено %d, ассет изменено %d"
        % (" [СУХОЙ ПРОГОН]" if dry else "", len(rows), nj, na))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
