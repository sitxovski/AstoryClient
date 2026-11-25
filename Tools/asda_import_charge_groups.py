# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_charge_groups.py — DT_ChargeGroup: ключи слияния зарядов спецпредметов P100_ChargeStack (клиент 5.1.4, 01.11.2026).
# [ИСПРАВЛЕНИЕ] Подбор ячеек коробки до 6096 (`AsdaPackageReserve::Compute`, ключ asda.Item.ChargeStackReserve) повторяет
#   правило сервера P100 один в один, поэтому таблица ОБЩАЯ с сервером:
#   источник — AstoryServer/Patches/AsdaPatch/tools/p100_data/charge_groups_client.csv (генератор gen_charge_groups.py;
#   здесь НЕ пересчитывается и не правится), формат «Name,Code,GroupKey,Charges», 173 кода (кода нет = не складывать).
# RowStruct = FAsdaChargeGroupRow (Source/AsdaReborn/Public/Items/AsdaChargeStack.h) — требует сборки C++.
# Выход: RawAssets/Data/ChargeGroups.csv (копия источника байт в байт, для аудита импорта) + /Game/AsdaAssets/Data/DT_ChargeGroup.
# Запуск (headless, без окна): python Tools/run_headless_safe.py Tools/asda_import_charge_groups.py --marker "CHARGE_GROUPS ГОТОВО"
#   (из git-bash не передавать `--expect-asset /Game/...` без MSYS_NO_PATHCONV=1 — MSYS переписывает путь в C:/Program Files/Git/Game;
#    запись ассета и так проверяется по строке ASDA_WRITES_ASSET).
# ★ ПРАВИЛО tools-no-import-side-effects: при импорте модуля ничего не делается — только main() под __main__.
import csv
import io
import os
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))   # папка Tools проекта
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import asda_paths as _P      # noqa: E402  корень port (реестр путей)
import asda_ue_paths as _U   # noqa: E402  каталоги проекта и /Game-префиксы

SRC = os.path.join(_P.PORT_DIR, "AstoryServer", "Patches", "AsdaPatch", "tools", "p100_data", "charge_groups_client.csv")   # общий источник с сервером
CSV_COPY = os.path.join(_U.DATA_SRC, "ChargeGroups.csv")   # копия в RawAssets (вне Content)
OUT_PKG, OUT_NAME = _U.G_DATA, "DT_ChargeGroup"            # /Game/AsdaAssets/Data/DT_ChargeGroup
ROW_STRUCT = "/Script/AsdaReborn.AsdaChargeGroupRow"        # FAsdaChargeGroupRow
HEADER = ["Name", "Code", "GroupKey", "Charges"]            # шапка источника
EXPECT_ROWS = 173                                           # кодов «складывать» (P100_PROJECT.md §1)
MARKER = "CHARGE_GROUPS ГОТОВО"                             # строка успеха для run_headless_safe


def read_source(path=SRC):
    """(текст источника, строки-словари) с проверкой формата: шапка, число строк, Name == Code, ключ/заряды > 0, коды уникальны."""
    raw = io.open(path, "r", encoding="utf-8", newline="").read()
    rows = list(csv.DictReader(io.StringIO(raw)))
    head = raw.splitlines()[0].split(",") if raw else []
    if head != HEADER:
        raise ValueError("шапка %s, ждали %s" % (head, HEADER))
    if len(rows) != EXPECT_ROWS:
        raise ValueError("строк %d, ждали %d" % (len(rows), EXPECT_ROWS))
    seen = set()
    for r in rows:
        code, key, ch = int(r["Code"]), int(r["GroupKey"]), int(r["Charges"])
        if r["Name"] != r["Code"] or code <= 0 or key <= 0 or ch <= 0 or code in seen:
            raise ValueError("плохая строка %r" % (r,))
        seen.add(code)
    return raw, rows


def main():
    import unreal
    import asda_dt_common as _DTC

    def log(m):
        unreal.log_warning("[charge-dt] %s" % m)

    raw, rows = read_source()
    os.makedirs(os.path.dirname(CSV_COPY), exist_ok=True)
    with io.open(CSV_COPY, "w", encoding="utf-8", newline="") as f:
        f.write(raw)
    keys = len({int(r["GroupKey"]) for r in rows})
    log("источник %s: кодов %d, ключей %d → копия %s" % (SRC, len(rows), keys, CSV_COPY))

    rs = unreal.load_object(None, ROW_STRUCT)
    if not rs:
        log("!! RowStruct %s НЕ НАЙДЕН — нужна сборка C++ (Public/Items/AsdaChargeStack.h). DT не создан." % ROW_STRUCT)
        return
    dt = _DTC.open_or_create_dt(unreal, OUT_PKG, OUT_NAME, rs)
    if not dt:
        log("!! DataTable %s не создан" % OUT_NAME)
        return
    ok = _DTC.fill_and_save(unreal, dt, raw, log, fmt="csv")   # провал заливки → DT не сохраняется; маркер ASDA_WRITES_ASSET
    n = len(dt.get_row_names())
    log("%s: строк=%d (ждали %d), заливка=%s" % (OUT_NAME, n, len(rows), ok))
    if ok and n == len(rows):
        unreal.log("%s: %s строк=%d ключей=%d" % (MARKER, OUT_NAME, n, keys))


if __name__ == "__main__":
    main()
