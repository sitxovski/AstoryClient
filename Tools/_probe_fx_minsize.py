# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_fx_minsize.py — ЖИВОЙ ТИК до/после порога размера (`asda.Fx.MinQuadSize` / `MinParticleSize`).
# Ничего не меняет: гоняет штатный зонд `asda.Fx.MotionProbe` дважды — со СТАРЫМИ жёсткими полами
# (1.0 / 0.5) и с эталонными (0/0) — и печатает радиус секции каждого эмиттера в обоих режимах.
# A/B на ОДНОЙ сборке и ОДНИХ ассетах: разница = ровно вклад порога.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal

EFFECTS = [e for e in os.environ.get("ASDA_PROBE_FX", "").split(";") if e] or [
    "ch1_a_4th_ac_04_1",              # квады Bil_001/Bil_002/S_002: концы рампы = 0
    "eff_1_char_04_draine02-a_00",    # 54 квада с нулевыми концами
    "ch1_a_4th_ac_04_2a",             # частицы Par_001 с нулевым стартом
]
SECS = os.environ.get("ASDA_PROBE_SECS", "1.5")


def log(m): unreal.log_warning("[порог-размера] %s" % m)


w = unreal.EditorLevelLibrary.get_editor_world()


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for mode, q, p in (("СТАРЫЙ ПОЛ (квад 1.0 / частица 0.5)", "1.0", "0.5"),
                       ("ЭТАЛОН (порог 0/0)", "0", "0")):
        log("═══ %s ═══" % mode)
        unreal.SystemLibrary.execute_console_command(w, "asda.Fx.MinQuadSize %s" % q)
        unreal.SystemLibrary.execute_console_command(w, "asda.Fx.MinParticleSize %s" % p)
        for eff in EFFECTS:
            log("--- %s" % eff)
            unreal.SystemLibrary.execute_console_command(w, "asda.Fx.MotionProbe %s %s 1" % (eff, SECS))
    # вернуть дефолт
    unreal.SystemLibrary.execute_console_command(w, "asda.Fx.MinQuadSize 0")
    unreal.SystemLibrary.execute_console_command(w, "asda.Fx.MinParticleSize 0")
    log("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
