# Copyright (c) 2026 @sitxovski (Discord)
# asda_place_npcs_all.py — раскат NPC-акторов на ВСЕ карты (одна UE-сессия).
# Карты с NPC по серверным NpcData_Info (asda_build_npc_manifest.py). alpen включён (идемпотентно).
import os, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_maps as _MAPS
TOOLS = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools"
# ★ Список карт — ЕДИНЫЙ источник asda_maps.py (17 карт с НЕПУСТЫМ списком NPC (у inferion/t_castle_of_chess .npc пуст)).
#   Было: локальный литерал (13 копий по тулам). Состав сверен 1:1.
MAPS = _MAPS.MAPS_WITH_NPC


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    _failed = []
    for mp in MAPS:
        unreal.log("=== NPCALL: %s ===" % mp)
        os.environ["ASDA_MAP"] = mp
        try:
            _pf = os.path.join(TOOLS, "asda_place_npcs.py")
            exec(compile(open(_pf, encoding="utf-8").read(), _pf, "exec"),
                 {"__name__": "__main__", "__file__": _pf})
        except (Exception, SystemExit) as e:
            _failed.append(mp)
            unreal.log_error("NPCALL %s FAIL: %s" % (mp, e))
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass
    # ★ 16.09.2026 (ревизия savedir12, МЕЛК-3). Строка успеха и строка неуспеха начинаются ПО-РАЗНОМУ:
    #   run_headless_safe.py по умолчанию ищет маркер «ЗАВЕРШЕНО», и безусловная печать означала, что прогон,
    #   где упали ВСЕ карты, считается успешным.
    if _failed:
        unreal.log_error("=== NPCALL ПРЕРВАНО: провалов=%d из %d (%s) ===" % (len(_failed), len(MAPS), ", ".join(_failed[:8])))
        raise SystemExit(3)
    unreal.log("=== NPCALL ЗАВЕРШЕНО: карт=%d ===" % len(MAPS))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
