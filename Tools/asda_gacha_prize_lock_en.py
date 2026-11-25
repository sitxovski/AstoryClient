# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_gacha_prize_lock_en.py — ★ 07.11.2026 (решение владельца, премиум-гача): ТОЧЕЧНОЕ добавление EN-строки loc 991202
#   «сначала забери приз» в DT_Loc_GameText_en.
#
# ЗАЧЕМ. Новая строка порта для окна-сообщения при запрете закрыть сцену гачи (ключ `asda.Gacha.PrizeCloseLock`). RU-строку
#   заливает штатная ведомость `asda_gametext_overrides.py`; для EN шаг применяет ТОЛЬКО строку 991202 ведомости
#   `Tools/data/loc/en/GameText.json` общей точкой `asda_ledger.point_fill_dt`, дописывает её в источник
#   `RawAssets/Text/L10N/en/DT_Loc_GameText_en.csv` и обновляет копию прошлого импорта (`overlay_snapshot_apply`), чтобы
#   страж `import_text_lang.py` не принял заливку за ручную правку. Образец — `asda_cashgift_loc_en.py`.
# Сухой прогон: set ASDA_GACHA_LOC_DRYRUN=1. Маркер: «EN 991202: ГОТОВО».
# Запуск: python Tools/run_headless_safe.py Tools/asda_gacha_prize_lock_en.py --marker "EN 991202: ГОТОВО"
#   --expect-asset /Game/AsdaAssets/Text/L10N/en/DT_Loc_GameText_en
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

LEDGER = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools/data/loc/en/GameText.json"                   # ведомость EN
CSV_EN = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/RawAssets/Text/L10N/en/DT_Loc_GameText_en.csv"     # источник оверлея
DT_EN = "/Game/AsdaAssets/Text/L10N/en/DT_Loc_GameText_en"                                # ассет оверлея
KEY = "991202"                                                                            # строка порта
MARK = "EN 991202: ГОТОВО"                                                                # маркер раннера


def csv_cell(value):
    """Значение ячейки CSV: в кавычках, если есть запятая или кавычка (как пишет генератор)."""
    if "," in value or '"' in value:
        return '"' + value.replace('"', '""') + '"'
    return value


def fix_csv(row, dry, log):
    """Дописать строку KEY в источник EN; уже есть с тем же значением — 0 правок; с другим — СТОП."""
    raw = open(CSV_EN, "rb").read()
    bom = raw[:3] == b"\xef\xbb\xbf"
    text = raw.decode("utf-8-sig")
    nl = "\r\n" if "\r\n" in text else "\n"
    want = KEY + "," + csv_cell(row["value"])
    lines = text.split(nl)
    for l in lines:
        if l.startswith(KEY + ","):
            if l == want:
                return 0
            raise RuntimeError("CSV EN: строка %s = %r, ждали %r — чужая правка, СТОП" % (KEY, l, want))
    tail_empty = lines and lines[-1] == ""
    if tail_empty:
        lines[-1] = want
        lines.append("")
    else:
        lines.append(want)
    if not dry:
        open(CSV_EN, "wb").write((b"\xef\xbb\xbf" if bom else b"") + nl.join(lines).encode("utf-8"))
    log("CSV EN: дописана строка %s" % KEY)
    return 1


def main():
    """Точка входа (только под __main__ — закон tools-no-import-side-effects)."""
    import unreal
    import asda_ledger as L
    dry = bool(os.environ.get("ASDA_GACHA_LOC_DRYRUN"))
    rows = [r for r in L.read_rows(LEDGER) if str(r.get("key")) == KEY]
    if len(rows) != 1:
        raise RuntimeError("в ведомости EN строк %s: %d (ждали 1)" % (KEY, len(rows)))
    nc = fix_csv(rows[0], dry, unreal.log)
    na = L.point_fill_dt(unreal, DT_EN, rows, "Text", unreal.log, dry, label="DT_Loc_GameText_en", allow_new=True, eq=L.eq_nl)
    if not dry and na:
        L.overlay_snapshot_apply(DT_EN, {KEY: rows[0]["value"]}, unreal.log)
    unreal.log("%s CSV изменено %d, ассет изменено %d%s" % (MARK, nc, na, " [СУХОЙ ПРОГОН]" if dry else ""))


if __name__ == "__main__":
    main()
