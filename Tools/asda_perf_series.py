# Copyright (c) 2026 @sitxovski (Discord)
# asda_perf_series.py — ЗАМЕР ПРАВКИ ВОЛНЫ R4: серии «ключ 0/1 в одном процессе» (R4, шаг M0, доля c; 14.11.2026).
#
# ЗАЧЕМ. Правка производительности живёт под ключом `asda.Perf.<Имя>` (1 — новое, 0 — старый код). Этот сценарий поднимает
#   ОДНУ игру (редактор -game, -AsdaNoNet -RenderOffscreen, через `asda_selftest_batch.py` — тот сам берёт слот очереди UE) и
#   даёт ей команду `asda.Perf.Series` (Source/AsdaReborn/Private/Test/AsdaPerfSeries.cpp): серии с чередованием ключа в одной
#   сцене, итог — файл `<out>/series.tsv` (строки серий, сводка, приговор «измеримо / не измеримо», строка адаптера и число игр).
#   Своих циклов ожидания нет: серии идут по кадрам игры. Описание: Reports/refactor_2026-11/r4/c/M0.md.
#
# ЗАПУСК (обычный python):
#   python Tools/asda_perf_series.py --key asda.Perf.Foo --scopes Foo.Tick,Foo.Build --out <папка> [ключи]
#     --key <ключ>        консольный ключ правки (обязателен)
#     --scopes <a,b>      точные имена областей замера (ASDA_PERF_SCOPE); пусто — все, где были вызовы
#     --pairs N           пар серий (4 → 8 серий 0,1,1,0,0,1,1,0)        --order abba|abab
#     --frames N          кадров замера в серии (240)                     --warm N   кадров прогрева после смены ключа (30)
#     --cmd "<команда>"   консольная команда в начале каждой серии        --tick "<команда>"  команда каждый кадр (нагрузчик)
#     --reps N            повторов --tick за кадр (1)
#     --before "<тест>"   самотест/команда до серий в той же игре (повторяемый; например расстановка сцены)
#     --pre "<команда>"   консольная команда до пакета (повторяемый; например "t.MaxFPS 60")
#     --map world|login|<путь>   --lang ru|en   --timeout <с> (900)   --delay <с> пауза перед сериями (3)
#     --out <папка>       куда класть series.tsv и журналы игры (обязателен; пробелов в пути быть не должно)
# КОД ВОЗВРАТА: 0 — файл серий записан и по каждой области есть приговор; иначе код пакетного прогона либо 4 (нет файла).
#
# ★ Модуль БЕЗ побочных эффектов при импорте (закон tools-no-import-side-effects): работа — только в main() под __main__.
import argparse
import io
import os
import subprocess
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))   # папка Tools


def series_command(args, out_file):
    """Строка команды `asda.Perf.Series …` для списка пакета (пробелы внутри --cmd/--tick — знаком `+`)."""
    parts = ["asda.Perf.Series", "key=" + args.key, "pairs=%d" % args.pairs, "frames=%d" % args.frames, "warm=%d" % args.warm,
             "order=" + args.order, "reps=%d" % args.reps, "delay=%g" % args.delay, "file=" + out_file.replace("\\", "/")]
    if args.scopes:
        parts.append("scopes=" + args.scopes)
    if args.cmd:
        parts.append("cmd=" + "+".join(args.cmd.split()))
    if args.tick:
        parts.append("tick=" + "+".join(args.tick.split()))
    parts.append("@timeout=%d" % args.timeout)
    return " ".join(parts)


def main():
    """Точка входа: один прогон серий, печать сводки и приговоров."""
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser(description="Серии замера правки R4: ключ 0/1 в одной игре (asda.Perf.Series)")
    ap.add_argument("--key", required=True)
    ap.add_argument("--scopes", default="")
    ap.add_argument("--pairs", type=int, default=4)
    ap.add_argument("--order", default="abba", choices=["abba", "abab"])
    ap.add_argument("--frames", type=int, default=240)
    ap.add_argument("--warm", type=int, default=30)
    ap.add_argument("--cmd", default="")
    ap.add_argument("--tick", default="")
    ap.add_argument("--reps", type=int, default=1)
    ap.add_argument("--before", action="append", default=[])
    ap.add_argument("--pre", action="append", default=[])
    ap.add_argument("--map", default="world")
    ap.add_argument("--lang", default="ru")
    ap.add_argument("--timeout", type=int, default=900)
    ap.add_argument("--delay", type=float, default=3.0)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    out = os.path.abspath(args.out)
    if " " in out:
        print("ОШИБКА: в пути --out не должно быть пробелов:", out)
        return 2
    os.makedirs(out, exist_ok=True)
    out_file = os.path.join(out, "series.tsv")
    if os.path.exists(out_file):
        os.remove(out_file)
    cmd = [sys.executable, os.path.join(TOOLS_DIR, "asda_selftest_batch.py"), "--map", args.map, "--lang", args.lang,
           "--timeout", str(args.timeout), "--out", out, "--suffix", "perf_%d" % os.getpid()]
    for p in args.pre:
        cmd += ["--pre", p]
    cmd += list(args.before) + [series_command(args, out_file)]
    rc = subprocess.call(cmd, cwd=os.path.dirname(TOOLS_DIR), env=dict(os.environ, PYTHONIOENCODING="utf-8"),
                         stdout=io.open(os.path.join(out, "batch.out"), "w", encoding="utf-8"), stderr=subprocess.STDOUT)
    if not os.path.exists(out_file):
        print("ФАЙЛА СЕРИЙ НЕТ (код пакетного прогона %d): %s — смотреть %s" % (rc, out_file, os.path.join(out, "batch.log")))
        return rc or 4
    for ln in io.open(out_file, encoding="utf-8"):
        if ln.startswith(("#", "сводка", "приговор")):
            print(ln.rstrip("\n"))
    print("файл серий: %s (код пакетного прогона %d)" % (out_file, rc))
    return rc


if __name__ == "__main__":
    sys.exit(main())
