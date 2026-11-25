# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_ring_live.py — ДИАГНОСТИКА: живой тик кольцевых эффектов канала предметов.
# Гоняет консольный зонд `asda.Fx.MotionProbe`, который печатает по каждому эмиттеру точку эмиссии,
# центроид секции и (новое) РАДИУС секции на пяти отсчётах. Радиус — единственное число, которым
# видно «юбка растёт или стоит»: центроид радиально-симметричного кольца при росте радиуса не движется.
import os, unreal

EFFS = [e.strip() for e in os.environ.get(
    "ASDA_RINGLIVE",
    "eff_0_char_99_pmbuff_a_01;eff_0_char_99_pmbuff_a_00;eff_0_char_99_pmbuff_a_05;"
    "eff_0_char_99_pmbuff_a_06;eff_1_char_99_expmovg-a_01"
).split(";") if e.strip()]
SECS = os.environ.get("ASDA_RINGLIVE_SECS", "1.2")

try:
    w = unreal.EditorLevelLibrary.get_editor_world()
except Exception:
    w = None


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    if not w:
        unreal.log_warning("[зонд-кольца-live] редакторского мира нет — зонд пропущен")
    else:
        for e in EFFS:
            unreal.log_warning("── живой тик %s ──" % e)
            unreal.SystemLibrary.execute_console_command(w, "asda.Fx.MotionProbe %s %s 1" % (e, SECS))
    unreal.log_warning("[зонд-кольца-live] ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
