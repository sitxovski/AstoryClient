# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_effect_mob_dt.py — сверка ГОТОВЫХ ассетов DataTable против исходных .bin.
# В отличие от Tools/asda_verify_effect_mob_tables.py (сверяет CSV), этот скрипт читает уже
# импортированные /Game/AsdaAssets/Data/DT_EffectList | DT_CallMob | DT_MonsterSkillEffect
# и сравнивает КАЖДОЕ поле каждой строки с распарсенным .bin — то есть проверяет сам ассет.
# Запуск: Tools/run_headless.bat Tools\asda_verify_effect_mob_dt.py
# ★ ПРАВИЛО tools-no-import-side-effects: работа только через main().
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_effect_mob_tables as T  # noqa: E402

EPS = 1e-6


def log(m):
    unreal.log_warning("[dt-verify] %s" % m)


def same_value(dt_text, src):
    """Сравнение значения из DT (строка) с распарсенным из .bin."""
    if isinstance(src, float):
        try:
            return abs(float(dt_text) - src) < EPS
        except ValueError:
            return False
    return dt_text == str(src)


def verify(key, fields):
    spec = T.TABLES[key]
    full = "%s/%s" % (spec["pkg"], spec["name"])
    dt = unreal.EditorAssetLibrary.load_asset(full)
    if not dt:
        log("!! %s не найден" % full)
        return False
    rows = spec["parse"](spec["bin"])
    names = list(dt.get_row_names())
    ok = len(names) == len(rows)
    log("%s: строк в DT=%d, в .bin=%d — %s" % (spec["name"], len(names), len(rows),
                                               "OK" if ok else "РАСХОЖДЕНИЕ"))
    bad = 0
    for f in fields:
        col = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, f)
        if len(col) != len(rows):
            log("!! колонка %s: длина %d != %d" % (f, len(col), len(rows)))
            bad += 1
            continue
        for i, src in enumerate(rows):
            if not same_value(col[i], src[f]):
                bad += 1
                if bad < 6:
                    log("!! строка %d поле %s: DT=%r .bin=%r" % (i, f, col[i], src[f]))
    log("%s: сверено полей %d × строк %d, расхождений %d" % (spec["name"], len(fields), len(rows), bad))
    return ok and bad == 0


def main():
    all_ok = True
    all_ok &= verify("EffectList", ["Index", "EffectName", "Flag", "DataHex"])
    all_ok &= verify("CallMob", T.CALL_MOB_FIELDS + ["DataHex"])
    all_ok &= verify("MonsterSkillEffect", T.MSE_FIELDS + ["DataHex"])
    log("ИТОГ: %s" % ("ВСЕ ТРИ DT СОВПАДАЮТ С .bin, расхождений 0" if all_ok else "ЕСТЬ РАСХОЖДЕНИЯ"))


if __name__ == "__main__":
    main()
