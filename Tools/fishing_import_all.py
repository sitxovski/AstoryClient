# Copyright (c) 2026 @sitxovski (Discord)
# fishing_import_all.py — РЫБАЛКА, волна В5а: ОДИН headless-прогон на весь импорт ассетов рыбалки
# (headless держит DLL модуля — соседняя сборка падает, поэтому прогонов как можно меньше и короче).
#   1) fishing_import_char_clips.main()  — клипы персонажа anim_225..232 (оба пола);
#   2) fishing_import_models.main()      — скелеталы удочек/рыб/зоны в Skeletal/Fishing;
#   3) fishing_build_fx.main()           — BP_Fx_fish_end_01/02;
#   4) fishing_dump_assets.main()        — проверочный дамп (JSON в Reports/owner_1008).
# Шаги выбираются переменной FISHING_STEPS="char;models;fx;dump" (по умолчанию все).
# Каждый шаг пишет «ASDA_WRITES_ASSET: <путь>» для проверки даты файлов обёрткой run_headless_safe.
# Запуск: python Tools/run_headless_safe.py Tools/fishing_import_all.py --marker "FISHING_ALL_DONE"
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
import importlib.util
import os
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))   # каталог инструментов
sys.path.insert(0, HERE)
import unreal

# Шаги прогона: (ключ, файл модуля)
STEPS = [("char", "fishing_import_char_clips.py"), ("models", "fishing_import_models.py"),
         ("fx", "fishing_build_fx.py"), ("dump", "fishing_dump_assets.py")]


def _load(fn):
    """Загрузить модуль шага БЕЗ запуска (у модулей main() только при прямом исполнении)."""
    spec = importlib.util.spec_from_file_location(os.path.splitext(fn)[0], os.path.join(HERE, fn))
    m = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = m
    spec.loader.exec_module(m)
    return m


def main():
    """Прогнать выбранные шаги по порядку; итог — маркер FISHING_ALL_DONE."""
    want = os.environ.get("FISHING_STEPS", "").strip()
    keys = [k.strip() for k in want.split(";") if k.strip()] if want else [k for k, _ in STEPS]
    res = []
    for key, fn in STEPS:
        if key not in keys:
            continue
        unreal.log_warning("[fishing-all] === шаг %s (%s) ===" % (key, fn))
        try:
            r = _load(fn).main()
            res.append("%s=%s" % (key, "ok" if r is not False else "FAIL"))
        except Exception:
            unreal.log_warning("[fishing-all] шаг %s упал:\n%s" % (key, traceback.format_exc()))
            res.append("%s=EXC" % key)
    unreal.log_warning("[fishing-all] FISHING_ALL_DONE %s" % " ".join(res))


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
