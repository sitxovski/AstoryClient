# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""ЖИВОЙ ТИК (ничего не меняет): габарит эффекта удара при масштабе цели-МОБА и цели-ПЕРСОНАЖА.

Гоняет консольный зонд `asda.Fx.MotionProbe <jeb> <сек> <strict> <масштаб>` — он спавнит BP,
крутит настоящий `Tick` 60 Гц и печатает РАДИУС построенной секции на 5 отсчётах. Прогон парами
(масштаб ДО / ПОСЛЕ) даёт числа, а не арифметику на бумаге.
"""
import os
import sys

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

import unreal

# (jeb-база, подпись, масштаб ДО, масштаб ПОСЛЕ, длительность прогона)
CASES = [
    ("eff_1_char_07_runfirb04-a_00", "удар навыка 707 по ЦЕЛИ-ПЕРСОНАЖУ", 1.0, 2.0, 0.6),
    ("eff_1_char_07_runfirb04-a_00", "удар навыка 707 по мобу 150% (не меняется)", 1.5, 1.5, 0.6),
    ("BP_ImpactBattk03", "импакт обычной атаки по мобу 200% (медиана архива)", 1.0, 2.0, 0.8),
    ("BP_ImpactBattk01", "импакт обычной атаки по мобу 150% (Степной волк)", 1.0, 1.5, 0.8),
    ("BP_ImpactMob", "моб бьёт игрока — эталон 1.0, НЕ меняем", 1.0, 1.0, 0.8),
]


def log(m):
    unreal.log_warning("[живой-масштаб] %s" % m)


def main():
    try:
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).new_level("/Game/Temp/_live_scale_probe")
    except Exception as e:
        log("новый уровень не создан: %s" % e)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not world:
        log("нет мира редактора — зонд не запустить")
        return
    for jeb, why, s0, s1, secs in CASES:
        for s in (s0, s1):
            log("── %s | %s | масштаб актора %.2f ──" % (jeb, why, s))
            unreal.SystemLibrary.execute_console_command(
                world, "asda.Fx.MotionProbe %s %.2f 1 %.2f" % (jeb, secs, s))
    log("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
