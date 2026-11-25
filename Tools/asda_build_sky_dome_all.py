# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_sky_dome_all.py — раскатка неба (asda_build_sky_dome.py) на все карты с SKY_DOME.JBM.
# Одна UE-сессия; карты без JBM/уровня скрипт сам пропускает с логом.
import os, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_maps as _MAPS
TOOLS = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools"
# ★ Список карт — ЕДИНЫЙ источник asda_maps.py (18 открытых карт (без 4 закрытых/инстансных) — небо/облака).
#   Было: локальный литерал (13 копий по тулам). Состав сверен 1:1.
MAPS = _MAPS.OUTDOOR_MAPS


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global MAPS
    # порционная раскатка (18 карт в одной сессии = OOM-краш на 10-й): ASDA_SKY_MAPS="a||b||c"
    _only = os.environ.get("ASDA_SKY_MAPS", "")
    if _only: MAPS = [m for m in MAPS if m in _only.split("||")]
    _p = os.path.join(TOOLS, "asda_build_sky_dome.py")
    _src = open(_p, encoding="utf-8").read()
    _failed = []
    for mp in MAPS:
        os.environ["ASDA_MAP"] = mp
        unreal.log("=== SKYALL: %s ===" % mp)
        try:
            exec(compile(_src, _p, "exec"), {"__name__": "__main__", "__file__": _p})
        except (Exception, SystemExit) as e:
            _failed.append(mp)
            unreal.log_error("SKYALL %s FAIL: %s" % (mp, e))
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass
    # ★ 16.09.2026 (ревизия savedir12, МЕЛК-3). Строка успеха и строка неуспеха начинаются ПО-РАЗНОМУ:
    #   run_headless_safe.py по умолчанию ищет маркер «ЗАВЕРШЕНО», и безусловная печать означала, что прогон,
    #   где упали ВСЕ карты, считается успешным.
    if _failed:
        unreal.log_error("=== SKYALL ПРЕРВАНО: провалов=%d из %d (%s) ===" % (len(_failed), len(MAPS), ", ".join(_failed[:8])))
        raise SystemExit(3)
    unreal.log("=== SKYALL ЗАВЕРШЕНО: карт=%d ===" % len(MAPS))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
