# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_gametext_overrides.py — ТОЧЕЧНОЕ применение ведомости правок строк DT_GameText (08.10.2026).
#
# Ведомость: Tools/data/gametext_overrides.json (решения владельца по текстам). Для каждой записи:
#   1) источник RawAssets/Text/GameText.csv — меняется ТОЛЬКО строка с этим ключом (кодировка, кавычки и концы
#      строк файла сохраняются); пишем лишь если текущее значение = `was` или уже = `value`, иначе СТОП (чужая правка);
#   2) ассет /Game/AsdaAssets/Text/DT_GameText — экспорт в CSV-строку, замена одной строки, заливка обратно
#      (`fill_data_table_from_csv_string`), затем сверка: число строк то же, отличается ровно эта строка. Без полного
#      переимпорта — правки, сделанные в ассете руками, не теряются (закон owner-asset-edits-never-lose).
# Поле `prev` (★ 09.10.2026, необязательное): список прежних значений САМОЙ ведомости — запись разрешена и тогда, когда текущее
#   совпадает с одним из них (строку ведомости переписали новым решением; `was` остаётся значением до ведомости / null).
# Сухой прогон: ASDA_GAMETEXT_DRYRUN=1 — только сверка и печать, без записи.
# Маркер для run_headless_safe.py: «ЗАВЕРШЕНО: ведомость GameText».
# Запуск: python Tools/run_headless_safe.py Tools/asda_gametext_overrides.py --marker "ЗАВЕРШЕНО: ведомость GameText"
#   --expect-asset /Game/AsdaAssets/Text/DT_GameText
import csv
import io
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ledger as _L  # noqa: E402  ★ дедуп №2 В2 Т8: одно чтение ведомостей, одна семантика ячейки, одна точечная заливка DT

LEDGER = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools/data/gametext_overrides.json"
CSV_PATH = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/RawAssets/Text/GameText.csv"
DT_PATH = "/Game/AsdaAssets/Text/DT_GameText"


def load_ledger():
    """Строки ведомости [{key, value, was, prev?, why}] — общий читатель `asda_ledger.read_rows` (★ дедуп №2 В2 Т8)."""
    return _L.read_rows(LEDGER)


def csv_escape(s):
    """Значение в кавычках CSV (удвоение кавычек) — формат GameText.csv."""
    return '"' + s.replace('"', '""') + '"'


def apply_to_source_csv(rows, dry, log):
    """Точечная правка GameText.csv по ключу. Возврат: число изменённых строк. Чужое значение — исключение."""
    raw = open(CSV_PATH, "rb").read()
    bom = raw.startswith(b"\xef\xbb\xbf")
    text = raw.decode("utf-8-sig")
    eol = "\r\n" if "\r\n" in text else "\n"
    lines = text.split(eol)
    changed = 0
    for r in rows:
        prefix = csv_escape(r["key"]) + ","
        idx = [i for i, l in enumerate(lines) if l.startswith(prefix)]
        # ★ 09.10.2026 (мультиязычность, этап 0): `was` = null — НОВАЯ строка (id есть у EN-оригинала, у RU нет).
        #   Ключа нет → вставка перед первой строкой с бо́льшим числовым ключом; ключ есть → только «уже = value».
        # ★ дедуп №2 В2 Т8: решение по ячейке — общее `asda_ledger.classify` (уже / применить / новая / чужая правка).
        if not idx and _L.classify_row(r, None, exists=False) == _L.NEW:
            pos = len(lines)
            while pos > 1 and not lines[pos - 1].strip():
                pos -= 1                                          # хвостовые пустые строки файла остаются в конце
            for i in range(1, pos):
                k = next(csv.reader([lines[i]]))[0] if lines[i].strip() else ""
                if k.lstrip("-").isdigit() and int(k) > int(r["key"]):
                    pos = i
                    break
            lines.insert(pos, prefix + csv_escape(r["value"]))
            log("CSV %s: НОВАЯ строка «%s»%s" % (r["key"], r["value"], " [СУХОЙ]" if dry else ""))
            changed += 1
            continue
        if len(idx) != 1:
            raise RuntimeError("GameText.csv: ключ %s найден %d раз — стоп" % (r["key"], len(idx)))
        i = idx[0]
        cur = next(csv.reader([lines[i]]))[1]
        cls = _L.classify_row(r, cur)
        if cls == _L.ALREADY:
            log("CSV %s: уже «%s» — без записи" % (r["key"], cur))
            continue
        # ★ 09.10.2026 (ревизия мультиязычности МЕЛК-1): `prev` — список ПРЕЖНИХ значений самой ведомости (строку заменили
        #   новым решением). Текущее = одному из них — это наша же прежняя запись, а не чужая правка: пишем новое.
        if cls != _L.APPLY:
            raise RuntimeError("GameText.csv %s: текущее «%s» ≠ ведомостному прежнему «%s» — чужая правка, стоп" % (r["key"], cur, r["was"]))
        lines[i] = prefix + csv_escape(r["value"])
        log("CSV %s: «%s» → «%s»%s" % (r["key"], cur, r["value"], " [СУХОЙ]" if dry else ""))
        changed += 1
    if changed and not dry:
        out = eol.join(lines).encode("utf-8")
        open(CSV_PATH, "wb").write((b"\xef\xbb\xbf" if bom else b"") + out)
    return changed


def compare_with_csv(head, rows, ki, vi, log):
    """Сверка ассета с источником GameText.csv (только печать): сколько строк расходится и первые из них."""
    src = {}
    with open(CSV_PATH, encoding="utf-8-sig", newline="") as fh:
        for r in list(csv.reader(fh))[1:]:
            if len(r) >= 2:
                src[r[0]] = r[1]
    dt = {r[ki]: r[vi] for r in rows}
    bad = [k for k in sorted(set(src) | set(dt)) if src.get(k) != dt.get(k)
           and (dt.get(k) or "").replace(chr(10), chr(92) + "n") != src.get(k)]   # перевод строки ассета = «\n» источника
    log("сверка ассет ↔ GameText.csv: строк ассета %d, источника %d, расходится %d%s"
        % (len(dt), len(src), len(bad), (": " + ", ".join(bad[:10])) if bad else ""))


def apply_to_asset(rows, dry, log):
    """Точечная правка DT_GameText через экспорт/заливку CSV-строки. Возврат: число изменённых строк.

    ★ дедуп №2 В2 Т8: тело — общая `asda_ledger.point_fill_dt` (то же, что у ведомости званий). Новые строки (was = null)
      дописываются в конец; перевод строки сравнивается в обоих видах (`eq_nl`). ★ ИСПРАВЛЕНИЕ: сверка после заливки
      строже прежней — кроме «изменились ровно ключи ведомости» проверяется, что у этих строк тронута только колонка
      SourceString и в ней стоит значение ведомости, и что ни одна строка не пропала (раньше `want` считался из самого
      `diff` и пропускал лишние изменения внутри ключей ведомости)."""
    import unreal
    return _L.point_fill_dt(unreal, DT_PATH, rows, "SourceString", log, dry, label="DT_GameText", allow_new=True,
                            eq=_L.eq_nl, on_noop=lambda head, body, ki, vi: compare_with_csv(head, body, ki, vi, log))


def main(source_csv=True):
    """Применить ведомость. source_csv=False — только ассет (вызов из import_gametext.py после импорта из CSV)."""
    try:
        import unreal
        log = unreal.log
    except ImportError:                                          # вне редактора — только CSV
        unreal = None
        log = print
    dry = bool(os.environ.get("ASDA_GAMETEXT_DRYRUN"))
    rows = load_ledger()
    nc = apply_to_source_csv(rows, dry, log) if source_csv else 0
    na = apply_to_asset(rows, dry, log) if unreal else 0
    # Маркер «ЗАВЕРШЕНО» — только у самостоятельного запуска: внутри import_gametext.py он сработал бы у раннера раньше
    #   собственного маркера импорта.
    log("%s ведомость GameText%s, записей %d, CSV изменено %d, ассет изменено %d"
        % ("ЗАВЕРШЕНО:" if source_csv else "применена:", " [СУХОЙ ПРОГОН]" if dry else "", len(rows), nc, na))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
