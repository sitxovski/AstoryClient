# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_skillclips_overlay.py — РЕЦЕПТ точечных правок `RawAssets/Data/SkillClips.csv` (источник DT_SkillClips), 10.11.2026.

ЗАЧЕМ. `SkillClips.csv` выпускает `asda_dump_skill_dispatch.py`, но живая таблица дорабатывалась после последнего полного прогона
  (1705 строк против 1689 у текущего разборщика, имена навыков), поэтому целиком её не перегенерируют. Правки, найденные сверкой
  с эталоном, живут ЗДЕСЬ — в `Tools/data/skillclips_overlay.csv` (key, column, old_value, new_value, reason, note); BuffFx — `Tools/data/bufffx_overlay.csv`, ключ Name; запуск с `--table bufffx` — и
  накладываются после любой перегенерации этим скриптом.

ИДЕМПОТЕНТНОСТЬ (как `asda_apply_name_overlay.py`): текущее == new → уже применено; текущее == old → применяем; иначе — КОНФЛИКТ,
  ячейка не трогается, код 2. После правки сверяются ВСЕ ячейки: измениться имеют право только перечисленные в рецепте.

Запуск:  python AstoryClient\Tools\asda_skillclips_overlay.py [--dry-run]
Затем точечная заливка DT: Tools/asda_ml_dt_point.py (задание: dt=/Game/AsdaAssets/Data/DT_SkillClips, csv_old = бэкап до правки).
Модуль без побочных эффектов при импорте (закон tools-no-import-side-effects).
"""
import argparse
import csv
import io
import os
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))                                   # каталог инструментов
DATA = os.path.join(os.path.dirname(TOOLS), "RawAssets", "Data")                     # исходники DT (вне Content)
# Таблица → (живой CSV, колонка-ключ строки = RowName DT, рецепт). ★ 10.11.2026: + BuffFx (эффекты состояний 4-й профессии из
#   `system/abSkillInfo.inl` — `asda_dump_buff_fx.py` их не знает, а живой BuffFx.csv после прогона генератора правился вручную).
TABLES = {
    "skillclips": (os.path.join(DATA, "SkillClips.csv"), "_row", os.path.join(TOOLS, "data", "skillclips_overlay.csv")),
    "bufffx": (os.path.join(DATA, "BuffFx.csv"), "Name", os.path.join(TOOLS, "data", "bufffx_overlay.csv")),
}


def load(path):
    """CSV → (шапка, строки как списки, перевод строки файла)."""
    raw = io.open(path, encoding="utf-8-sig", newline="").read()
    nl = "\r\n" if "\r\n" in raw else "\n"
    rows = list(csv.reader(io.StringIO(raw)))
    return rows[0], rows[1:], nl


def main():
    """Наложить рецепт на SkillClips.csv. Код: 0 — готово, 2 — конфликт/ошибка (файл не тронут)."""
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true", help="ничего не писать")
    ap.add_argument("--table", default="skillclips", choices=sorted(TABLES), help="какую таблицу править (по умолчанию skillclips)")
    a = ap.parse_args()
    CLIPS, KEY, RECIPE = TABLES[a.table]
    head, body, nl = load(CLIPS)
    col = {c: i for i, c in enumerate(head)}
    ik = col[KEY]
    by_key = {r[ik]: i for i, r in enumerate(body)}
    recipe = list(csv.DictReader(io.open(RECIPE, encoding="utf-8-sig", newline="")))
    new_body = [list(r) for r in body]
    allowed = set()
    st = {"уже": 0, "применено": 0, "конфликт": 0}
    for e in recipe:
        k, c = e["key"].strip(), e["column"].strip()
        if k not in by_key or c not in col:
            print("ОШИБКА: нет строки %s или колонки %s" % (k, c))
            return 2
        i, j = by_key[k], col[c]
        cur = new_body[i][j]
        if cur == e["new_value"]:
            st["уже"] += 1
        elif cur == e["old_value"]:
            new_body[i][j] = e["new_value"]
            st["применено"] += 1
        else:
            st["конфликт"] += 1
            print("КОНФЛИКТ %s.%s: в таблице «%s», рецепт ждёт «%s»" % (k, c, cur[:80], e["old_value"][:80]))
        allowed.add((i, j))
    if st["конфликт"]:
        print("СТОП: конфликты — файл не тронут", st)
        return 2
    diff = [(i, j) for i in range(len(body)) for j in range(len(head)) if body[i][j] != new_body[i][j]]
    if any(d not in allowed for d in diff):
        print("СТОП: изменились ячейки вне рецепта", [d for d in diff if d not in allowed][:5])
        return 2
    print("рецепт %d строк: %s; изменено ячеек %d" % (len(recipe), st, len(diff)))
    if a.dry_run or not diff:
        return 0
    # Формат живого файла (выпускает разборщик): шапка без кавычек, строки данных — все поля в кавычках.
    out = io.StringIO()
    out.write(",".join(head) + nl)
    csv.writer(out, lineterminator=nl, quoting=csv.QUOTE_ALL).writerows(new_body)
    io.open(CLIPS, "w", encoding="utf-8-sig", newline="").write(out.getvalue())
    print("записано %s" % CLIPS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
