# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""Пересборка ВСЕХ BP-каналов эффектов после отключения КОНЕЦ-сида трека.

Зачем отдельный драйвер: каждый сборщик — самостоятельный скрипт (закон
tools-no-import-side-effects: при импорте не делает ничего), поэтому запускаем их
`runpy`-ом как `__main__` в ОДНОМ headless-процессе — один прогрев редактора вместо шести.
Ничего, кроме уже существующих BP, не создаётся: правка только в узлах треков.
"""
import os
import runpy
import sys

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import unreal

# Порядок как в раскатке сид-узлов: предметы+персонаж → навыки → баффы → транспорт → заточка → маркер.
SCRIPTS = [
    "asda_build_char_fx.py",
    "asda_build_skill_fx.py",
    "asda_build_buff_fx.py",
    "asda_build_mount_fx.py",
    "asda_build_weapon_glow_fx.py",
    "asda_build_click_marker.py",
]


def main():
    for s in SCRIPTS:
        p = os.path.join(HERE, s)
        if not os.path.isfile(p):
            unreal.log_warning("[rebuild] НЕТ %s" % p)
            continue
        unreal.log_warning("[rebuild] ═══ %s ═══" % s)
        try:
            runpy.run_path(p, run_name="__main__")
        except Exception as e:
            unreal.log_warning("[rebuild] %s: ИСКЛЮЧЕНИЕ %s" % (s, e))
    unreal.log_warning("[rebuild] ВСЕ КАНАЛЫ ПЕРЕСОБРАНЫ")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
