# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_charinfo_tab_loc_en.py — ★ 08.11.2026 (пачка 16, баг 5; решение владельца «переименовать, чтобы текст был крупнее»):
#   ТОЧЕЧНАЯ правка EN-строки loc 8052 (третья под-вкладка страницы «Статус» окна персонажа C) в DT_Loc_GameText_en и её CSV.
#
# ЗАЧЕМ. «Characteristic Info» при нормальном кегле 8 занимает 89 px, а элемент переключателя 981 — 72 px (место 68), поэтому
#   `UAsdaCharaInfoSubsystem::BuildStatTabCaptions` ужимал подпись до кегля 6. На вкладке — стихии атаки и защиты
#   (loc 12048…12061: Fire/Water/Earth/Climate/Light/Darkness), RU уже «Стихии». Новое значение «Element Info» — 60 px при кегле 8,
#   в ряд с соседними «Basic Info» / «Ability Info». Строка берётся из ведомости `Tools/data/loc/en/GameText.json` (ключ 8052),
#   ассет правится общей точкой `asda_ledger.point_fill_dt`, источник `RawAssets/Text/L10N/en/DT_Loc_GameText_en.csv` — только если
#   там `was` или уже `value`; копия прошлого импорта — `overlay_snapshot_apply` (иначе страж `import_text_lang.py` встанет).
#   Образец — `asda_cashgift_loc_en.py`.
# Сухой прогон: set ASDA_CHARINFO_TAB_LOC_DRYRUN=1. Маркер: «EN 8052: ГОТОВО».
# Запуск: python Tools/run_headless_safe.py Tools/asda_charinfo_tab_loc_en.py --marker "EN 8052: ГОТОВО"
#   --expect-asset /Game/AsdaAssets/Text/L10N/en/DT_Loc_GameText_en
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

LEDGER = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools/data/loc/en/GameText.json"                   # ведомость EN
CSV_EN = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/RawAssets/Text/L10N/en/DT_Loc_GameText_en.csv"     # источник оверлея
DT_EN = "/Game/AsdaAssets/Text/L10N/en/DT_Loc_GameText_en"                                # ассет оверлея
KEY = "8052"                                                                              # подпись третьей под-вкладки
MARK = "EN 8052: ГОТОВО"                                                                  # маркер раннера


def fix_csv(row, dry, log):
    """Строка `8052,...` источника EN: `was` → `value` (уже `value` — 0 правок; иное — СТОП)."""
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
    dry = bool(os.environ.get("ASDA_CHARINFO_TAB_LOC_DRYRUN"))
    rows = [r for r in L.read_rows(LEDGER) if str(r.get("key")) == KEY]
    if len(rows) != 1:
        raise RuntimeError("в ведомости EN строк %s: %d (ждали 1)" % (KEY, len(rows)))
    nc = fix_csv(rows[0], dry, unreal.log)
    na = L.point_fill_dt(unreal, DT_EN, rows, "Text", unreal.log, dry, label="DT_Loc_GameText_en", eq=L.eq_nl)
    if not dry and na:
        L.overlay_snapshot_apply(DT_EN, {KEY: rows[0]["value"]}, unreal.log)
    unreal.log("%s CSV изменено %d, ассет изменено %d%s" % (MARK, nc, na, " [СУХОЙ ПРОГОН]" if dry else ""))


if __name__ == "__main__":
    main()
