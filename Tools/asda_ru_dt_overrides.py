# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_ru_dt_overrides.py - ВЕДОМОСТЬ РУССКИХ ПРАВОК ВЛОЖЕННЫХ DT и её применение в импортёрах (names11, этап 3 «ревизия», 14.10.2026).

ЗАЧЕМ
  Русская сборка клиента не содержит часть корейских реплик NPC и квестов (диалог NPC 144npc, 15 квестов Реки Дождя / смены класса,
  имена NPC питомцев и рыбалки, подпись кнопки и шапка NPC праздника). Импортёры (`asda_import_nsr_dialog.py`,
  `asda_import_quest_tables.py`, `asda_import_npc_tables.py`) каждый раз заново разбирают .BIN/.NSR/.QST и перезаписывают
  `NpcNsrHeader/Button/Page.json`, `QuestBook.json`, `NpcSnq.json` и DT - без этой ведомости любой переимпорт вернул бы корейский текст.

ВЕДОМОСТЬ  Tools/data/ru_dt_overrides.json : {"_comment": ..., "rows": [{dt, row, field, index, sub, was, now}, ...]}
  dt    - имя DataTable (DT_NpcNsrPage, DT_NpcNsrHeader, DT_NpcNsrButton, DT_QuestBook, DT_NpcSnq);
  row   - ключ строки DT (поле "Name": «<nsr>_<talkId>», «<nsr>», «<nsr>_<кнопка>», имя файла .QST, NpcId);
  field - поле строки; index - номер элемента массива или null; sub - поле структуры в элементе массива (Steps[i].Caption) или null;
  was   - значение, которое даёт разбор источника (корейское), now - русский текст.

КАК ПРИМЕНЯЕТСЯ  `apply_rows(dt, rows, key_fn)` правит СПИСОК СЛОВАРЕЙ-СТРОК после разбора источника и до сериализации в JSON/DT:
    значение == was -> ставится now (applied); значение == now -> уже (already); иное или строки/поля нет -> предупреждение «!!» в журнал
    и правка НЕ накладывается (источник изменился - решение за человеком, как у ведомостей рецепта).
Точечная правка уже залитого DT без переимпорта - `asda_dt_json_point.py` (режим ASDA_JP_LEDGER=1 берёт задание отсюда).

Модуль БЕЗ побочных эффектов при импорте (закон tools-no-import-side-effects): на уровне модуля только константы и функции.
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
LEDGER = os.path.join(HERE, "data", "ru_dt_overrides.json")


def load(dt=None, path=None):
    """Строки ведомости (все или только для DT `dt`, имя без пути, например «DT_QuestBook»)."""
    p = path or LEDGER
    if not os.path.isfile(p):
        return []
    with open(p, "rb") as f:
        rows = json.loads(f.read().decode("utf-8-sig")).get("rows", [])
    if dt:
        rows = [r for r in rows if r["dt"].rsplit("/", 1)[-1] == dt]
    return rows


def _cell(row, e):
    """(контейнер, ключ) ячейки правки внутри словаря-строки; None, если поля или элемента нет."""
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
        if not isinstance(el, dict) or e["sub"] not in el:
            return None
        return el, e["sub"]
    return arr, e["index"]


def apply_rows(dt, rows, key_fn, log=None, path=None):
    """Накладывает ведомость на список словарей-строк `rows` (на месте). Возврат: {applied, already, missing, mismatch}."""
    log = log or (lambda m: None)
    edits = load(dt, path)
    st = {"applied": 0, "already": 0, "missing": 0, "mismatch": 0}
    if not edits:
        return st
    by = {}
    for r in rows:
        by.setdefault(str(key_fn(r)), r)
    for e in edits:
        r = by.get(str(e["row"]))
        c = _cell(r, e) if r is not None else None
        if c is None:
            st["missing"] += 1
            log("!! РУС-ведомость %s: нет строки/поля %s.%s[%s] - правка пропущена" % (dt, e["row"], e["field"], e.get("index")))
            continue
        box, k = c
        if box[k] == e["now"]:
            st["already"] += 1
        elif box[k] == e["was"]:
            box[k] = e["now"]
            st["applied"] += 1
        else:
            st["mismatch"] += 1
            log("!! РУС-ведомость %s %s.%s[%s]: в источнике %r, ведомость ждёт %r - правка НЕ наложена"
                % (dt, e["row"], e["field"], e.get("index"), str(box[k])[:50], str(e["was"])[:50]))
    log("РУС-ведомость %s: наложено %d, уже %d, нет строки %d, расхождений %d" % (dt, st["applied"], st["already"], st["missing"], st["mismatch"]))
    return st
