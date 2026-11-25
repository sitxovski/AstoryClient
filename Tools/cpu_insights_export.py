# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# cpu_insights_export.py — разбор .utrace без графического интерфейса (волна CPU 07.09.2026).
#   Запускает UnrealInsights.exe в режиме анализа файла и просит выгрузить статистику таймеров в CSV.
#   Форма аргументов взята из ШТАТНОГО скрипта движка
#   Engine/Source/Programs/AutomationTool/Scripts/ExportTimerStatisticsFromUtrace.cs:39
#   (-OpenTraceFile=... -unattended -autoquit -noui -nullrhi -ExecOnAnalysisCompleteCmd="...").
#
#   ★ ГРАБЛИ (проверено 07.09.2026): аргументы НЕЛЬЗЯ передавать списком.
#   subprocess сам обернёт весь аргумент в кавычки → командная строка станет
#   "-ExecOnAnalysisCompleteCmd=..." , и FParse::Value движка НЕ найдёт ключ (перед дефисом кавычка).
#   Признак промаха: в логе Insights НЕТ строки «Executing commands on analysis completed...».
#   Поэтому строим командную строку СТРОКОЙ и отдаём её Popen как есть (Windows передаёт дословно).
#
#   За ОДИН запуск Insights выполняет РОВНО ОДНУ команду (InsightsManager.cpp:1045-1057),
#   поэтому на каждый срез (поток × область) — свой запуск.
#
# Использование:
#   python Tools/cpu_insights_export.py <trace.utrace> <выходной_csv> [--region stand] [--threads GameThread] [--max 300]
import argparse, os, subprocess, sys, time

import asda_pie_harness as PH   # ★ волна 3 п.15: командная строка Insights, скрытый запуск, добивание (общий каркас)

INSIGHTS = PH.UNREAL_INSIGHTS   # программа анализа трасс (прежнее имя оставлено)

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass


def export(trace, out_csv, region="", threads="GameThread", maxn="300", timeout=900.0):
    """Один запуск Insights с одной командой экспорта. Возвращает код (0 = файл получен)."""
    trace = trace.replace("\\", "/")
    out_csv = out_csv.replace("\\", "/")
    log_path = os.path.splitext(out_csv)[0] + "_insights.log"
    task = ("TimingInsights.ExportTimerStatistics %s -maxtimercount=%s -threads=%s "
            "-sortby=totalinclusivetime -sortorder=descending" % (out_csv, maxn, threads))
    if region:
        task += " -region=%s" % region
    # ★ строкой, не списком (грабля выше) — форма в PH.insights_export_cmdline
    cmdline = PH.insights_export_cmdline(trace, log_path, task)
    t0 = time.time()
    p = PH.popen_hidden(cmdline)             # закон no-visible-editor-windows: окно не показываем
    while p.poll() is None and time.time() - t0 < timeout:
        time.sleep(2)
    if p.poll() is None:
        PH.kill_tree(p.pid)
        print("  ТАЙМАУТ")
        return 1
    # Успех проверяем ПО ФАЙЛУ и по строке лога, не по коду возврата (закон headless-exit-code-unreliable)
    sz = os.path.getsize(out_csv) if os.path.isfile(out_csv) else 0
    exec_seen = False
    try:
        with open(log_path, encoding="utf-8", errors="replace") as f:
            exec_seen = "Executing commands on analysis completed" in f.read()
    except Exception:
        pass
    print("  %s: %d б, команда %s, %.0f с" % (os.path.basename(out_csv), sz,
                                              "выполнена" if exec_seen else "НЕ ВЫПОЛНЕНА",
                                              time.time() - t0))
    return 0 if (sz > 0 and exec_seen) else 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trace")
    ap.add_argument("out_csv")
    ap.add_argument("--region", default="")
    ap.add_argument("--threads", default="GameThread")
    ap.add_argument("--max", default="300")
    a = ap.parse_args()
    rc = export(a.trace, a.out_csv, a.region, a.threads, a.max)
    print("RC=%d" % rc)
    return rc


if __name__ == "__main__":
    sys.exit(main())
