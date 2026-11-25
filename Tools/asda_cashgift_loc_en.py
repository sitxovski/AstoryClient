# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_cashgift_loc_en.py — ★ P101_CashGift (01.11.2026): ТОЧЕЧНАЯ правка EN-строки loc 20070 (Д-К4) в DT_Loc_GameText_en и её CSV.
#
# ЗАЧЕМ. Код 1 ответа 5073 (получатель в сети, маршрут сохранён — `netHandleCraftResultPacket@0xC337E0`, network.c:13028-13040)
#   показывает loc 20070, а перевод говорил «You cannot send gifts to [ss1].» — смысл перевёрнут (дефект Д-К4, `re/client.md` §5).
#   Решение владельца — PROJECT_P101.md §8 п.6. RU-строка правится штатной ведомостью (`asda_gametext_overrides.py`);
#   для EN точечного инструмента не было — этот шаг применяет ТОЛЬКО строку 20070 ведомости `Tools/data/loc/en/GameText.json`
#   общей точкой `asda_ledger.point_fill_dt` (экспорт → замена ячейки → заливка → сверка «изменилась ровно эта строка» → сохранение)
#   и ту же строку источника `RawAssets/Text/L10N/en/DT_Loc_GameText_en.csv` (пишет, только если там `was` или уже `value`).
# Сухой прогон: set ASDA_CASHGIFT_LOC_DRYRUN=1. Маркер: «EN 20070: ГОТОВО».
# Запуск: python Tools/run_headless_safe.py Tools/asda_cashgift_loc_en.py --marker "EN 20070: ГОТОВО"
#   --expect-asset /Game/AsdaAssets/Text/L10N/en/DT_Loc_GameText_en
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

LEDGER = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools/data/loc/en/GameText.json"
CSV_EN = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/RawAssets/Text/L10N/en/DT_Loc_GameText_en.csv"
DT_EN = "/Game/AsdaAssets/Text/L10N/en/DT_Loc_GameText_en"
KEY = "20070"
MARK = "EN 20070: ГОТОВО"


def fix_csv(row, dry, log):
    """Строка `20070,...` источника EN: `was` → `value` (уже `value` — 0 правок; иное — СТОП)."""
    raw = open(CSV_EN, "rb").read()
    bom = raw[:3] == b"\xef\xbb\xbf"
    text = raw.decode("utf-8-sig")
    nl = "\r\n" if "\r\n" in text else "\n"
    lines = text.split(nl)
    for i, l in enumerate(lines):
        if l.startswith(KEY + ","):
            cur = l[len(KEY) + 1:]
            if cur == row["value"]:
                return 0
            if cur != row["was"]:
                raise RuntimeError("CSV EN: строка %s = %r, ждали %r — чужая правка, СТОП" % (KEY, cur, row["was"]))
            lines[i] = KEY + "," + row["value"]
            if not dry:
                open(CSV_EN, "wb").write((b"\xef\xbb\xbf" if bom else b"") + nl.join(lines).encode("utf-8"))
            log("CSV EN: %s %r -> %r" % (KEY, cur, row["value"]))
            return 1
    raise RuntimeError("CSV EN: строки %s нет" % KEY)


def main():
    """Точка входа (только под __main__ — закон tools-no-import-side-effects)."""
    import unreal
    import asda_ledger as L
    dry = bool(os.environ.get("ASDA_CASHGIFT_LOC_DRYRUN"))
    rows = [r for r in L.read_rows(LEDGER) if str(r.get("key")) == KEY]
    if len(rows) != 1:
        raise RuntimeError("в ведомости EN строк %s: %d (ждали 1)" % (KEY, len(rows)))
    nc = fix_csv(rows[0], dry, unreal.log)
    na = L.point_fill_dt(unreal, DT_EN, rows, "Text", unreal.log, dry, label="DT_Loc_GameText_en", eq=L.eq_nl)
    unreal.log("%s CSV изменено %d, ассет изменено %d%s" % (MARK, nc, na, " [СУХОЙ ПРОГОН]" if dry else ""))


if __name__ == "__main__":
    main()
