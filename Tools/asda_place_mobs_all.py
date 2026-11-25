# Copyright (c) 2026 @sitxovski (Discord)
# asda_place_mobs_all.py — раскат моб-акторов на ВСЕ карты с .svr (одна UE-сессия).
import os, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_maps as _MAPS
TOOLS = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools"
# ★ Список карт — ЕДИНЫЙ источник asda_maps.py (20 карт, у которых есть .svr в GameServer\MonsterData_Info).
#   Было: локальный литерал (13 копий по тулам). Состав сверен 1:1.
MAPS = _MAPS.MAPS_WITH_MOB_SVR


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    _failed = []
    for mp in MAPS:
        unreal.log("=== MOBALL: %s ===" % mp)
        os.environ["ASDA_MAP"] = mp
        try:
            _pf = os.path.join(TOOLS, "asda_place_mobs.py")
            exec(compile(open(_pf, encoding="utf-8").read(), _pf, "exec"),
                 {"__name__": "__main__", "__file__": _pf})
        except (Exception, SystemExit) as e:
            _failed.append(mp)
            unreal.log_error("MOBALL %s FAIL: %s" % (mp, e))
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass
    # ★ 16.09.2026 (ревизия savedir12, МЕЛК-3). Строка успеха и строка неуспеха начинаются ПО-РАЗНОМУ:
    #   run_headless_safe.py по умолчанию ищет маркер «ЗАВЕРШЕНО», и безусловная печать означала, что прогон,
    #   где упали ВСЕ карты, считается успешным.
    if _failed:
        unreal.log_error("=== MOBALL ПРЕРВАНО: провалов=%d из %d (%s) ===" % (len(_failed), len(MAPS), ", ".join(_failed[:8])))
        raise SystemExit(3)
    unreal.log("=== MOBALL ЗАВЕРШЕНО: карт=%d ===" % len(MAPS))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
