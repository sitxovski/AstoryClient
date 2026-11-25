# Copyright (c) 2026 @sitxovski (Discord)
# asda_run_mod_smoke_fix.py — одна headless-сессия: мастера Modulate/ModAdd (D3D 6:4 «тёмный дым»)
# + переимпорт эффектов 5 карт с mod-эффектами (eff_flammio-a_09/10, mdel_worker02-a_00/01).
import os, unreal
TOOLS = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools"

def run(script):
    p = os.path.join(TOOLS, script)
    unreal.log("=== RUN %s ===" % script)
    exec(compile(open(p, encoding="utf-8").read(), p, "exec"), {"__name__": "__main__", "__file__": p})

# ★ Список карт с mod-эффектами. Переменную окружения ставит ТОЛЬКО main(): на уровне модуля
#   это тихо меняло бы `ASDA_MAPS` всем скриптам сессии при простом импорте (закон
#   [[tools-no-import-side-effects]]; вызова тут нет, поэтому AST-сканеры этот класс не видят).
MOD_MAPS = "flammio||firewayforest||alpen_dungeon||rein||silalis"


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    os.environ["ASDA_MAPS"] = MOD_MAPS
    run("asda_build_effect_mod_masters.py")
    run("asda_place_effects_all.py")
    unreal.log("=== MOD SMOKE FIX DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
