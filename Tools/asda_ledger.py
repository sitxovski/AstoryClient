# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_ledger.py — ВЕДОМОСТИ ПРАВОК «было → стало»: одно чтение всех форматов, одна семантика ячейки, одна точечная
заливка DataTable.

★ Дедуп №2, волна В2, пункт Т8 (`Reports/dedup_audit2/04_tools_python.md` §Т8). До него в проекте жили 6 форматов
ведомостей и 6 применителей с РАЗНЫМИ правилами для одного и того же случая:
  | ведомость (формат)                                         | применитель                         | «уже стоит» | prev | новая строка | чужое значение |
  | Tools/data/gametext_overrides.json {key,value,was,prev,why}| asda_gametext_overrides             | да          | да   | да (was=null)| СТОП           |
  | Tools/data/warrank_ru_overrides.json {key,value,was,why,source} | asda_warrank_ru_overrides      | да          | НЕТ  | нет          | СТОП           |
  | Tools/data/loc/<lang>/<Т>.json {key,value,was,why,source}  | extract_text_lang.apply_ledgers     | НЕТ (стоп)  | НЕТ  | да (was=null)| СТОП           |
  | Tools/data/loc/<lang>/UiLiteral.json {asset,node,ru,en,…}  | asda_ui_literals_lang.load_ledger   | —           | —    | —            | — (словарь)    |
  | RawAssets/ItemDB/_value_overlay/<т>.csv {row,column,was,now,source,reason} | decode_item_db  | НЕТ (предупр.)| —  | —            | предупреждение |
  | Tools/_out/itemdb_overlay/item_name_overlay.csv {table,key,column,old_value,new_value,…} | asda_apply_name_overlay | да | — | — | КОНФЛИКТ (код 1) |
  плюс 2 копии `load_ledger` (gametext/warrank — точные клоны) и 2 копии точечной заливки DT (gametext/warrank) с разной
  строгостью сверки после заливки.

ТЕПЕРЬ:
  • ФОРМАТЫ ФАЙЛОВ НЕ МЕНЯЮТСЯ (это данные владельца). Читатели ниже возвращают строки как есть (словари со ВСЕМИ полями
    файла, порядок строк сохранён) — ни одно поле не теряется; `audit()` проверяет это на всех ведомостях проекта.
  • ОДНА семантика ячейки — `classify()`:
        текущее == value (стало)                → ALREADY  (уже стоит, без записи);
        текущее == was   или ∈ prev             → APPLY    (применяем);
        строки/ключа нет и was = null           → NEW      (новая строка);
        иначе                                   → CONFLICT (чужая правка).
    Что делать с CONFLICT, решает применитель по своей роли (точечная запись в ассет/источник — СТОП; декодер ItemDB —
    предупреждение и статус CHECK; рецепт имён — счётчик и код 1) — эти политики прежние.
  • ОДНА точечная заливка DataTable — `point_fill_dt()` (строгая сверка «изменилось ровно то, что в ведомости, только в
    своей колонке, ни одной строки не потеряно»), сохранение только после сверки, маркер ASDA_WRITES_ASSET.

Побочных эффектов при импорте нет (закон tools-no-import-side-effects): на уровне модуля — константы и функции.
"""
import csv
import io
import json
import os
import re

ALREADY = "already"     # в данных уже значение ведомости
APPLY = "apply"         # в данных «было» (или прежнее значение самой ведомости) — применяем
NEW = "new"             # строки нет, ведомость её заводит (was = null)
CONFLICT = "conflict"   # в данных чужое значение — ручная правка или сменился источник

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)


# ───────────────────────────── сравнение значений ─────────────────────────────

def eq_plain(cur, x):
    """Строгое равенство (ведомости званий, рецепт имён, ItemDB, оверлеи языков)."""
    return cur == x


def nl_variants(cur):
    """Виды одного значения DT_GameText: экспорт ассета даёт перевод строки и настоящим символом, и парой «\\n»."""
    cur = cur or ""
    return {cur, cur.replace("\n", "\\n"), cur.replace("\r\n", "\\n")}


def eq_nl(cur, x):
    """Равенство «с точностью до вида перевода строки» (прежние `variants` asda_gametext_overrides)."""
    return x in nl_variants(cur)


def norm_text(v):
    """Нормализация текстовой ячейки: только CRLF → LF (прежняя `import_text_lang.norm`)."""
    return (v or "").replace("\r\n", "\n")


def norm_typed(v):
    """Нормализация ячейки произвольной таблицы (прежняя `asda_ml_dt_point.norm`): CRLF и «\\n» → перевод строки,
    обрезка пробелов, bool без регистра, числа — по значению (round 5)."""
    s = (v or "").replace("\r\n", "\n").replace("\\n", "\n").strip()
    low = s.lower()
    if low in ("true", "false"):
        return low
    try:
        f = float(s)
        return repr(round(f, 5))
    except ValueError:
        return s


# ───────────────────────────── семантика ячейки ─────────────────────────────

def classify(cur, value, was=None, prev=None, exists=True, eq=eq_plain):
    """Решение по одной ячейке ведомости (см. шапку): ALREADY / APPLY / NEW / CONFLICT.

    cur    — текущее значение в данных (игнорируется при exists=False);
    value  — «стало» ведомости; was — «было» (None = строка заводится ведомостью); prev — список прежних значений
             САМОЙ ведомости (строку переписали новым решением);
    exists — есть ли строка/ключ в данных; eq — функция равенства (`eq_plain`, `eq_nl`).
    """
    if not exists:
        return NEW if was is None else CONFLICT
    if eq(cur, value):
        return ALREADY
    if was is not None and eq(cur, was):
        return APPLY
    if any(eq(cur, p) for p in (prev or ())):
        return APPLY
    return CONFLICT


def classify_row(r, cur, exists=True, eq=eq_plain):
    """`classify` для строки JSON-ведомости {key, value, was, prev, …}."""
    return classify(cur, r.get("value"), r.get("was"), r.get("prev"), exists, eq)


# ───────────────────────────── чтение форматов (без потерь) ─────────────────────────────

def read_json_ledger(path):
    """JSON-ведомость {"_comment": …, "rows": [...]} → (rows, meta). rows — словари со ВСЕМИ полями файла, порядок
    сохранён; meta — все ключи верхнего уровня, кроме rows. Форматы: gametext_overrides, warrank_ru_overrides,
    loc/<lang>/<Таблица>.json ({key,value,was,prev?,why,source}) и loc/<lang>/UiLiteral.json ({asset?,node?,ru,en,…})."""
    with open(path, encoding="utf-8") as fh:
        data = json.load(fh)
    rows = data.get("rows", [])
    meta = {k: v for k, v in data.items() if k != "rows"}
    return rows, meta


def read_rows(path):
    """Только строки JSON-ведомости (прежние `load_ledger` gametext/warrank — точные клоны)."""
    return read_json_ledger(path)[0]


def read_csv_ledger(path, encoding="utf-8"):
    """CSV-ведомость (с заголовком) → (заголовок, строки-словари со всеми колонками). Форматы: `_value_overlay`
    {row,column,was,now,source,reason}, `_tail_append` (колонки таблицы), рецепт имён {table,key,column,old_value,
    new_value,…}. Длинные поля (DataHex) допускаются."""
    csv.field_size_limit(1 << 30)
    with open(path, "r", encoding=encoding, newline="") as fh:
        rdr = csv.DictReader(fh)
        rows = list(rdr)
        return list(rdr.fieldnames or []), rows


def value_overlay_entries(path):
    """`_value_overlay/<т>.csv` → строки {row, column, was, now, source, reason} (строки как в файле)."""
    return read_csv_ledger(path, "utf-8")[1]


def name_overlay_entries(path):
    """Рецепт имён `item_name_overlay.csv` → строки (utf-8-sig, как читал `asda_apply_name_overlay.load_overlay`)."""
    return read_csv_ledger(path, "utf-8-sig")[1]


# ───────────────────────────── точечная заливка DataTable ─────────────────────────────

def _key_order(k):
    """Порядок ключей в сообщениях: числовые — по значению, прочие — строкой после них."""
    s = str(k)
    return (0, int(s), "") if s.lstrip("-").isdigit() else (1, 0, s)


# Путь ассета языкового оверлея: /Game/AsdaAssets/Text/L10N/<язык>/DT_Loc_<Таблица>_<язык>[.<объект>].
_OVERLAY_DT = re.compile(r"^/Game/AsdaAssets/Text/L10N/([^/]+)/(DT_Loc_[^/.]+)(?:\.[^/]*)?$")


def overlay_snapshot_apply(dt_path, cells, log, root=None):
    """★ 05.11.2026 Копия прошлого импорта оверлея (`RawAssets/Text/L10N/<язык>/_imported/<таблица>.csv`) — те же ячейки, что
    точечная заливка только что записала в ассет. Страж `import_text_lang.py` сверяет ассет с этой копией: без неё любая
    точечная заливка выглядела как ручная правка и штатный импорт вставал на СТОП (16 таблиц из 56 к 05.11).

    Звать ПОСЛЕ успешного сохранения ассета. dt_path не оверлей — ничего не делает (возврат 0).
    cells — {ключ: текст} (None = строка удалена). Правятся только эти строки, остальные байты файла не меняются; запись
      атомарная. Копии нет — предупреждение: копию заводит только штатный импорт. Возврат: число изменённых строк.
    root — каталог L10N (по умолчанию боевой; другой нужен только проверке).
    """
    m = _OVERLAY_DT.match(dt_path)
    if not m or not cells:
        return 0
    snap = os.path.join(root or os.path.join(PORT, "RawAssets", "Text", "L10N"), m.group(1), "_imported", m.group(2) + ".csv")
    if not os.path.isfile(snap):
        log("копии прошлого импорта нет (%s) — не обновлена, штатный импорт по этой таблице встанет" % snap)
        return 0
    raw = open(snap, "rb").read()
    bom = raw[:3] == b"\xef\xbb\xbf"
    text = raw.decode("utf-8-sig")
    eol = "\r\n" if "\r\n" in text else "\n"
    rows = list(csv.reader(io.StringIO(text, newline="")))
    at = {r[0]: n for n, r in enumerate(rows) if r and n}          # строка 0 — заголовок
    changed, drop = 0, set()
    for k, v in cells.items():
        n = at.get(str(k))
        if v is None:
            if n is not None:
                drop.add(n)
                changed += 1
        elif n is None:
            rows.append([str(k), v])
            changed += 1
        elif rows[n][1] != v:
            rows[n][1] = v
            changed += 1
    if not changed:
        return 0
    buf = io.StringIO(newline="")
    csv.writer(buf, lineterminator=eol).writerows(r for n, r in enumerate(rows) if n not in drop)
    tmp = snap + ".tmp"
    with open(tmp, "wb") as fh:
        fh.write((b"\xef\xbb\xbf" if bom else b"") + buf.getvalue().encode("utf-8"))
    os.replace(tmp, snap)
    log("копия прошлого импорта %s: строк обновлено %d" % (m.group(2), changed))
    return changed


def point_fill_dt(unreal, dt_path, rows, value_col, log, dry=False, label=None, allow_new=False, eq=eq_plain,
                  key_col=0, on_noop=None):
    """Точечная правка ОДНОЙ колонки DataTable по ведомости: экспорт CSV-строкой → замена ячеек → заливка → сверка →
    сохранение. Возврат: число изменённых строк. Любое отклонение — RuntimeError, ассет НЕ сохраняется.

    rows      — строки ведомости {key, value, was, prev?}; ключ сравнивается со столбцом `key_col` экспорта (RowName);
    value_col — имя колонки, которую правит ведомость (SourceString у DT_GameText, Title у DT_WarRank);
    allow_new — разрешить NEW (was = null и строки нет: строка дописывается в конец, прочие колонки пустые);
    eq        — равенство ячейки (`eq_nl` для текстов с переводами строк);
    on_noop   — вызывается (head, rows_экспорта, key_col, value_idx) при «нечего писать» или сухом прогоне.
    Сверка после заливки (★ строже обеих прежних копий): число строк = прежнее + новые; изменились ровно ключи, где
      ведомость меняет значение (+ новые); у изменившихся — только колонка `value_col`, и в ней значение ведомости.
    """
    label = label or dt_path.rsplit("/", 1)[-1]
    dt = unreal.EditorAssetLibrary.load_asset(dt_path)
    if not dt:
        raise RuntimeError("нет ассета %s" % dt_path)
    lib = unreal.DataTableFunctionLibrary
    before = lib.export_data_table_to_csv_string(dt)
    rdr = list(csv.reader(io.StringIO(before)))
    head = rdr[0]
    body = [list(r) for r in rdr[1:]]                          # КОПИЯ строк: сверка ниже сравнивает с нетронутым экспортом
    ki, vi = key_col, head.index(value_col)
    by_key = {row[ki]: n for n, row in enumerate(body)}
    changed, added, want = 0, [], set()
    for r in rows:
        k = str(r["key"])
        n = by_key.get(k)
        cur = body[n][vi] if n is not None else None
        cls = classify_row(r, cur, exists=n is not None, eq=eq)
        if cls == NEW and allow_new:
            nr = [""] * len(head)
            nr[ki], nr[vi] = k, r["value"]
            body.append(nr)
            by_key[k] = len(body) - 1
            added.append(k)
            want.add(k)
            log("DT %s: НОВАЯ строка «%s»%s" % (k, r["value"], " [СУХОЙ]" if dry else ""))
            changed += 1
            continue
        if n is None:
            raise RuntimeError("%s: строки %s нет — стоп" % (label, k))
        if cls == ALREADY:
            log("DT %s: уже «%s» — без записи" % (k, cur))
            continue
        if cls != APPLY:
            raise RuntimeError("%s %s: текущее «%s» ≠ ведомостному прежнему «%s» — правка в ассете, стоп"
                               % (label, k, cur, r.get("was")))
        body[n][vi] = r["value"]
        want.add(k)
        log("DT %s: «%s» → «%s»%s" % (k, cur, r["value"], " [СУХОЙ]" if dry else ""))
        changed += 1
    if not changed or dry:
        if on_noop:
            on_noop(head, rdr[1:], ki, vi)
        return changed
    buf = io.StringIO()
    w = csv.writer(buf, quoting=csv.QUOTE_ALL, lineterminator="\n")
    w.writerow(head)
    w.writerows(body)
    if not lib.fill_data_table_from_csv_string(dt, buf.getvalue()):
        raise RuntimeError("fill_data_table_from_csv_string вернул False — %s НЕ сохраняется" % label)
    after = list(csv.reader(io.StringIO(lib.export_data_table_to_csv_string(dt))))
    old = {row[ki]: row for row in rdr[1:]}
    new = {row[ki]: row for row in after[1:]}
    diff = {k for k in set(old) | set(new) if old.get(k) != new.get(k)}
    value_of = {str(r["key"]): r["value"] for r in rows}
    other = sorted((k for k in diff if k in old and k in new
                    and [v for i, v in enumerate(old[k]) if i != vi] != [v for i, v in enumerate(new[k]) if i != vi]),
                   key=_key_order)
    bad_val = sorted((k for k in diff if k in new and not eq(new[k][vi], value_of.get(k))), key=_key_order)
    lost = sorted((k for k in old if k not in new), key=_key_order)
    log("сверка после заливки: строк было %d, стало %d, отличаются %s; прочие поля тронуты: %s"
        % (len(old), len(new), sorted(diff, key=_key_order), other or "нет"))
    if len(old) + len(added) != len(new) or diff != want or other or bad_val or lost:
        raise RuntimeError("сверка не прошла (отличаются %s, ждали %s, прочие поля %s, неверные значения %s, пропали %s)"
                           " — ассет НЕ сохраняется" % (sorted(diff, key=_key_order), sorted(want, key=_key_order),
                                                         other, bad_val, lost))
    unreal.log("ASDA_WRITES_ASSET: %s" % dt_path)
    if not unreal.EditorAssetLibrary.save_loaded_asset(dt, False):
        raise RuntimeError("не сохранён %s" % dt_path)
    overlay_snapshot_apply(dt_path, {k: value_of[k] for k in want}, log)   # ★ 05.11.2026: иначе штатный импорт примет заливку за ручную правку
    return changed


# ───────────────────────────── проверка «все ведомости читаются без потерь» ─────────────────────────────

def all_ledgers():
    """Все ведомости проекта: [(вид, путь)]."""
    out = []
    data = os.path.join(HERE, "data")
    for fn in ("gametext_overrides.json", "warrank_ru_overrides.json"):
        out.append(("json", os.path.join(data, fn)))
    loc = os.path.join(data, "loc")
    if os.path.isdir(loc):
        for lang in sorted(os.listdir(loc)):
            d = os.path.join(loc, lang)
            for fn in sorted(os.listdir(d)) if os.path.isdir(d) else []:
                if fn.endswith(".json"):
                    out.append(("json", os.path.join(d, fn)))
    for sub, kind in (("_value_overlay", "value_overlay"), ("_tail_append", "tail_append")):
        d = os.path.join(PORT, "RawAssets", "ItemDB", sub)
        for fn in sorted(os.listdir(d)) if os.path.isdir(d) else []:
            if fn.endswith(".csv"):
                out.append((kind, os.path.join(d, fn)))
    nov = os.path.join(HERE, "_out", "itemdb_overlay", "item_name_overlay.csv")
    if os.path.isfile(nov):
        out.append(("name_overlay", nov))
    return out


def audit(log=print):
    """Прочитать КАЖДУЮ ведомость общим читателем и сверить с независимым разбором (json.load / csv.reader):
    строк и полей столько же, значения те же. Возврат: число расхождений (0 — без потерь)."""
    bad = 0
    for kind, p in all_ledgers():
        if kind == "json":
            rows, meta = read_json_ledger(p)
            with open(p, "rb") as fh:
                ref = json.loads(fh.read().decode("utf-8"))
            same = rows == ref.get("rows", []) and meta == {k: v for k, v in ref.items() if k != "rows"}
            n, nf = len(rows), sum(len(r) for r in rows)
        else:
            enc = "utf-8-sig" if kind == "name_overlay" else "utf-8"
            head, rows = read_csv_ledger(p, enc)
            with open(p, "rb") as fh:
                raw = fh.read().decode(enc)
            ref = list(csv.reader(io.StringIO(raw, newline="")))
            same = (head == ref[0] and len(rows) == len(ref) - 1
                    and all([r[c] for c in head] == list(x) for r, x in zip(rows, ref[1:])))
            n, nf = len(rows), len(rows) * len(head)
        if not same:
            bad += 1
        log("%-3s %-14s строк %6d полей %8d  %s" % ("OK" if same else "!!", kind, n, nf, os.path.relpath(p, PORT)))
    log("ведомостей %d, расхождений %d" % (len(all_ledgers()), bad))
    return bad


if __name__ == "__main__":
    import sys
    sys.exit(1 if audit() else 0)
