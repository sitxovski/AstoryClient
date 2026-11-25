# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# import_text_lang.py — ★ [ПОРТ] 09.10.2026 ИМПОРТ ЯЗЫКОВЫХ ОВЕРЛЕЕВ В UE (мультиязычность, этап 2 плана
#   `Reports/owner_1009/multilang_plan.md`).
#
# ЧТО ДЕЛАЕТ. Для каждого `RawAssets/Text/L10N/<lang>/DT_Loc_<Таблица>_<lang>.csv` (пишет генератор
#   `Tools/extract_text_lang.py`) заводит/обновляет ассет `/Game/AsdaAssets/Text/L10N/<lang>/DT_Loc_<Таблица>_<lang>`
#   со строкой `FAsdaLocRow { Text }` (C++: `Public/Text/AsdaLang.h`; читает `FAsdaLang::FindOverlay`).
#   RU-ассеты НЕ трогаются вообще — перевод живёт только в этих НОВЫХ ассетах (закон owner-asset-edits-never-lose).
#
# СТОРОЖ РУЧНОЙ ПРАВКИ. После каждого импорта CSV копируется в `RawAssets/Text/L10N/<lang>/_imported/` — это «что мы
#   залили в ассет». Перед перезаливкой ассет сверяется с этой копией: расхождение = ассет правили руками (например,
#   владелец поправил перевод в редакторе) → таблица НЕ перезаливается, в лог — ключи расхождений. Правку тогда надо
#   перенести в ведомость `Tools/data/loc/<lang>/<Таблица>.json` (переживает перегенерацию) и повторить импорт.
#   Нет копии, но ассет есть — тоже стоп (неизвестно, чей он).
#   ★ 05.11.2026: точечные заливки (`asda_ml_dt_point.py`, `asda_ledger.point_fill_dt`, `asda_turnin_en_rows.py`) обновляют эту
#   копию теми же ячейками (`asda_ledger.overlay_snapshot_apply`) — новый инструмент записи в оверлей обязан делать то же.
# ПОСЛЕ ЗАЛИВКИ — обратное чтение: ассет обязан совпасть с CSV построчно, иначе не сохраняется.
#
# Запуск (редактор закрыт, -game не запущен, окон не показывать):
#   python Tools\run_headless_safe.py Tools\import_text_lang.py --marker "ЗАВЕРШЕНО: оверлеи"
#   ASDA_LANG=en (по умолчанию), ASDA_LANG_ONLY=GameText,item_list — только перечисленные таблицы,
#   ASDA_LANG_DRYRUN=1 — только сверки, без записи.
import csv
import io
import os
import shutil
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ledger as _L  # noqa: E402  ★ дедуп №2 В2 Т8: общая нормализация ячеек

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
ROW_STRUCT = "/Script/AsdaReborn.AsdaLocRow"           # FAsdaLocRow
SRC_ROOT = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Text\L10N"
DST_ROOT = "/Game/AsdaAssets/Text/L10N"


def log(m):
    """Строка журнала шага."""
    unreal.log_warning("[оверлеи] %s" % m)


def read_csv_text(text):
    """CSV-текст (Name,Text) → {ключ: текст}. Переводы строк приводятся к виду экспорта ассета (настоящий \\n)."""
    out = {}
    rows = list(csv.reader(io.StringIO(text)))
    for r in rows[1:]:
        if len(r) >= 2:
            out[r[0]] = r[1]
    return out


# Значение для сравнения: экспорт ассета даёт настоящий перевод строки, CSV источника — то же или `\\n`.
#   Тело перенесено дословно в `asda_ledger.norm_text` (★ дедуп №2 В2 Т8).
norm = _L.norm_text


def dt_rows(dt):
    """Строки ассета {ключ: Text} через экспорт в CSV-строку."""
    return read_csv_text(unreal.DataTableFunctionLibrary.export_data_table_to_csv_string(dt))


def import_table(lang, fn, dry):
    """Один CSV → один ассет. Возврат: 'ok' | 'skip' | 'stop'."""
    base = fn[:-4]                                              # DT_Loc_<T>_<lang>
    src = os.path.join(SRC_ROOT, lang, fn)
    snap_dir = os.path.join(SRC_ROOT, lang, "_imported")
    snap = os.path.join(snap_dir, fn)
    pkg = "%s/%s" % (DST_ROOT, lang)
    full = "%s/%s" % (pkg, base)
    text = open(src, encoding="utf-8-sig").read()
    want = read_csv_text(text)
    if not want and not EAL.does_asset_exist(full):
        log("%s: в CSV нет строк — ассет не заводится" % full)     # нечего переводить (нет EN-источника)
        return "skip"
    if EAL.does_asset_exist(full):
        dt = EAL.load_asset(full)
        have = dt_rows(dt)
        if not os.path.isfile(snap):
            log("СТОП %s: ассет есть, а копии прошлого импорта нет — неизвестно, чей ассет" % full)
            return "stop"
        prev = read_csv_text(open(snap, encoding="utf-8-sig").read())
        diff = [k for k in set(have) | set(prev) if norm(have.get(k)) != norm(prev.get(k))]
        if diff:
            log("СТОП %s: ассет правили после прошлого импорта (%d ключей: %s) — перенести правку в ведомость "
                "Tools/data/loc/%s/<таблица>.json" % (full, len(diff), sorted(diff)[:10], lang))
            return "stop"
        if all(norm(have.get(k)) == norm(v) for k, v in want.items()) and len(have) == len(want):
            log("%s: без изменений (%d строк)" % (full, len(want)))
            return "skip"
    else:
        if dry:
            log("[сухой] %s: будет создан (%d строк)" % (full, len(want)))
            return "ok"
        fac = unreal.DataTableFactory()
        fac.set_editor_property("struct", unreal.load_object(None, ROW_STRUCT))
        dt = AT.create_asset(base, pkg, unreal.DataTable, fac)
        if not dt:
            log("СТОП %s: не создан" % full)
            return "stop"
    if dry:
        log("[сухой] %s: будет перезалит (%d строк)" % (full, len(want)))
        return "ok"
    if not unreal.DataTableFunctionLibrary.fill_data_table_from_csv_string(dt, text):
        log("СТОП %s: заливка CSV вернула ошибку" % full)
        return "stop"
    back = dt_rows(dt)
    bad = [k for k in want if norm(back.get(k)) != norm(want[k])] + [k for k in back if k not in want]
    if bad:
        log("СТОП %s: после заливки ассет ≠ CSV (%s) — не сохраняю" % (full, bad[:10]))
        return "stop"
    unreal.log("ASDA_WRITES_ASSET: %s" % full)
    if not EAL.save_loaded_asset(dt, False):
        log("СТОП %s: не сохранён" % full)
        return "stop"
    os.makedirs(snap_dir, exist_ok=True)
    shutil.copyfile(src, snap)
    log("%s: залито %d строк" % (full, len(want)))
    return "ok"


def main():
    """Импорт всех (или перечисленных) оверлеев языка ASDA_LANG."""
    lang = os.environ.get("ASDA_LANG", "en")
    only = {s.strip() for s in os.environ.get("ASDA_LANG_ONLY", "").split(",") if s.strip()}
    dry = bool(os.environ.get("ASDA_LANG_DRYRUN"))
    d = os.path.join(SRC_ROOT, lang)
    files = sorted(f for f in os.listdir(d) if f.startswith("DT_Loc_") and f.endswith("_%s.csv" % lang))
    if only:
        files = [f for f in files if f[len("DT_Loc_"):-len("_%s.csv" % lang)] in only]
    res = {"ok": 0, "skip": 0, "stop": 0}
    for fn in files:
        try:
            res[import_table(lang, fn, dry)] += 1
        except Exception as e:                                      # noqa: BLE001
            log("СТОП %s: исключение %s" % (fn, e))
            res["stop"] += 1
    log("ЗАВЕРШЕНО: оверлеи %s%s — таблиц %d: залито %d, без изменений %d, стоп %d"
        % (lang, " [СУХОЙ]" if dry else "", len(files), res["ok"], res["skip"], res["stop"]))
    if res["stop"]:
        unreal.log_error("[оверлеи] ЕСТЬ ОСТАНОВКИ — см. строки «СТОП»")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
