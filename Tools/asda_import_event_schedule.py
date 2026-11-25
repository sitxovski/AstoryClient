# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_event_schedule.py — РАСПИСАНИЕ ИВЕНТ-БОНУСОВ (иконки EXP/Drop в HUD):
#   `system\Texture\act_secession3.TEX` → /Game/AsdaAssets/Data/DT_EventSchedule.
#
# ══════════════════════ ФОРМАТ (реверс загрузчика, НЕ догадка) ══════════════════════════════════
# `SecessionSchedule_LoadFromTex@0xE81800` (ui_core.c:77793):
#     fopen("<CurrentDir>\System\Texture\act_secession3.TEX", "rt");
#     while (fscanf(f, "%4d %2d %2d %2d %2d %f", &год,&месяц,&день,&часНачала,&часКонца,&бонус),
#            год != 0)
#         push(запись);
# То есть ТЕКСТ, шесть колонок через табуляцию, по строке на запись. Расширение `.TEX` врёт —
#   это не текстура, а обычный текстовый файл (fopen с режимом "rt").
#
# ★ ОБРЫВ ПО «ГОД == 0» ПРИМЕНЯЕТ ИМПОРТЁР. Эталон перестаёт читать на первой строке с нулевым
#   годом, и всё, что после неё, он не видит НИКОГДА. Значит и в таблицу это попадать не должно:
#   иначе порт показал бы бонусы, которых у оригинала нет.
#
# ★ КЛЮЧ СТРОКИ = ПОРЯДКОВЫЙ НОМЕР В ФАЙЛЕ. Естественного ключа у записи нет: одна и та же дата
#   встречается по многу раз (разные часовые окна одного дня). Порядок значим — эталон обходит
#   массив сверху вниз и берёт ПЕРВОЕ совпадение по месяцу/дню/часу.
#
# ⚠ ИСТОЧНИК — СБОРКА `AsdaChroniclesInterlude` (CLIENT_ORIG), А НЕ `_MINE`. В `_MINE` этот файл
#   обрезан до двух строк августа 2010 (40 байт), а в ORIG лежит полное расписание июля 2011
#   (5842 байта, 265 строк) — именно оно и было положено в Content как
#   `Content/AsdaAssets/Data/act_secession3.txt` (md5 98ac29d1a0c5ea40ff0b830d69a64b43).
#   Берём тот же файл, чтобы данные порта не изменились от перевода в таблицу.
#   ⚠ Даты в нём прошлогодние и сегодня не совпадут никогда — это ДЕФЕКТ ДАННЫХ ОРИГИНАЛА,
#     он разобран и обойдён флагом `bEventFallbackToPacketRate` (см. AsdaEffectsSubsystem.h).
#
# `--verify` печатает разобранные строки и сверяет их число с числом непустых строк файла
#   до первого нулевого года.
#
# Запуск:  Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_import_event_schedule.py
#   без UE: python asda_import_event_schedule.py --json-only
#   сверка: python asda_import_event_schedule.py --verify
import os as _os
import sys as _sys

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_dt_common as _DT

SRC = _os.path.join(_P.CLIENT_ORIG, "system", "Texture", "act_secession3.TEX")
OUT_DIR = _os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data")
OUT_JSON = _os.path.join(OUT_DIR, "EventSchedule.json")

PKG = "/Game/AsdaAssets/Data"
NAME = "DT_EventSchedule"
ROW_STRUCT = "/Script/AsdaReborn.AsdaEventScheduleRow"


def parse_schedule(path):
    """Текст расписания → список словарей-строк. Обрыв на первой строке с годом 0 (как эталон)."""
    with open(path, "rb") as f:
        text = f.read().decode("cp1251", "replace")
    rows = []
    for line in text.splitlines():
        tok = line.split()
        if len(tok) < 6:                     # `fscanf` шести полей не набрал — строка пропущена
            continue
        year = int(tok[0])
        if year == 0:                        # эталон: год 0 ОБРЫВАЕТ чтение
            break
        rows.append({
            "Year": year,
            "Month": int(tok[1]),
            "Day": int(tok[2]),
            "StartHour": int(tok[3]),
            "EndHour": int(tok[4]),
            "Bonus": float(tok[5]),
        })
    return rows


def verify(log):
    rows = parse_schedule(SRC)
    with open(SRC, "rb") as f:
        raw = f.read()
    total = sum(1 for l in raw.decode("cp1251", "replace").splitlines() if len(l.split()) >= 6)
    log("файл: %s (%d б)" % (SRC, len(raw)))
    log("строк с шестью полями в файле: %d, взято до первого «год == 0»: %d" % (total, len(rows)))
    if rows:
        log("первая: %s" % rows[0])
        log("последняя: %s" % rows[-1])
    dates = sorted({(r["Year"], r["Month"], r["Day"]) for r in rows})
    log("различных дат: %d (%s … %s)" % (len(dates), dates[0], dates[-1]) if dates else "дат нет")
    return len(rows) > 0


def main(unreal=None):
    def log(m):
        if unreal:
            unreal.log_warning("[ивент-расписание] %s" % m)
        else:
            print("[ивент-расписание] %s" % m)

    rows = parse_schedule(SRC)
    if not rows:
        log("!! расписание пустое: %s" % SRC)
        return False
    log("строк: %d" % len(rows))

    # Ключ строки = ПОРЯДКОВЫЙ НОМЕР в файле. Поля-счётчика в данных нет, поэтому номер выдаёт
    #   отдельный счётчик: `json_rows` зовёт key_fn ровно один раз на строку и строго по порядку.
    ordinal = iter(range(len(rows)))
    text = _DT.json_rows(rows, lambda r: str(next(ordinal)))

    _os.makedirs(OUT_DIR, exist_ok=True)
    with open(OUT_JSON, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    log("JSON: %s" % OUT_JSON)
    if unreal is None:
        return True
    return _DT.fill_dt(unreal, PKG, NAME, ROW_STRUCT, text, log, fmt="json",
                       missing_hint="Public/UI/AsdaEffectsSubsystem.h, FAsdaEventScheduleRow") is not None


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    if "--verify" in _sys.argv:
        verify(lambda m: print("[ивент-расписание] %s" % m))
    elif "--json-only" in _sys.argv:
        main(None)
    else:
        import unreal as _u
        main(_u)
