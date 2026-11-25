# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""Точечная дозапись EN-строк страниц сдачи 16 заданий в `DT_Loc_NpcNsrPage_en` (31.10.2026, bug-quest-follow).

Зачем отдельный шаг: общий импорт оверлея (`import_text_lang.py`) останавливается, потому что в ассете есть строки, внесённые
мимо CSV (защита от потери чужих правок). Здесь ассет выгружается целиком (`export_data_table_to_csv_string`), дописываются
ТОЛЬКО ключи из ведомости `Tools/data/loc/en/NpcNsrPage.json` с пометкой «31.10.2026 bug-quest-follow», которых в ассете ещё нет,
и таблица заливается обратно; прочие строки сверяются байт в байт до/после. Под UE: Tools\\run_headless_safe.py <этот файл>.
"""
import csv
import io
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ledger as _L  # noqa: E402  ★ 05.11.2026: копия прошлого импорта оверлея

LEDGER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "loc", "en", "NpcNsrPage.json")
ASSET = "/Game/AsdaAssets/Text/L10N/en/DT_Loc_NpcNsrPage_en"


def log(m):
    print("[turnin-en] %s" % m)


def main():
    import unreal
    dt = unreal.load_asset(ASSET)
    if dt is None:
        log("ЗАВЕРШЕНО: ассета нет — ничего не сделано")
        return
    lib = unreal.DataTableFunctionLibrary
    src = lib.export_data_table_to_csv_string(dt)
    rows = list(csv.reader(io.StringIO(src)))
    head, body = rows[0], rows[1:]
    before = {r[0]: r for r in body}
    want = [r for r in json.load(open(LEDGER, encoding="utf-8"))["rows"] if r.get("why", "").startswith("31.10.2026 bug-quest-follow")]
    add = [r for r in want if r["key"] not in before]
    log("в ассете строк %d, столбцы %s; к дописыванию %d из %d" % (len(body), head, len(add), len(want)))
    if not add:
        log("ЗАВЕРШЕНО: дописывать нечего")
        return
    ti = head.index("Text")
    for r in add:
        row = [""] * len(head)
        row[0] = r["key"]
        row[ti] = r["value"]
        body.append(row)
    out = io.StringIO()
    w = csv.writer(out, lineterminator="\n")
    w.writerow(head)
    w.writerows(body)
    ok = lib.fill_data_table_from_csv_string(dt, out.getvalue())
    after = {r[0]: r for r in list(csv.reader(io.StringIO(lib.export_data_table_to_csv_string(dt))))[1:]}
    same = all(after.get(k) == v for k, v in before.items())
    have = all(after.get(r["key"], [None] * (ti + 1))[ti] == r["value"] for r in add)
    log("заливка %s; прежних строк %d — %s; новые %d — %s" % (ok, len(before), "не изменились" if same else "ИЗМЕНИЛИСЬ!",
                                                          len(add), "на месте" if have else "НЕ ВСЕ!"))
    if ok and same and have:
        unreal.EditorAssetLibrary.save_loaded_asset(dt, only_if_is_dirty=False)
        log("ASDA_WRITES_ASSET: %s" % ASSET)
        _L.overlay_snapshot_apply(ASSET, {r["key"]: r["value"] for r in add}, log)   # иначе штатный импорт примет дозапись за ручную правку
        log("ЗАВЕРШЕНО: дописано %d" % len(add))
    else:
        log("ЗАВЕРШЕНО: НЕ сохранено (проверка не прошла)")


if __name__ == "__main__":
    main()
