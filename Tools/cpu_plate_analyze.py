# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# cpu_plate_analyze.py — сводка ABBA по срезам трассы для правки «тело плашки» (07.09.2026).
#   Вход — CSV-выгрузки TimingInsights.ExportTimerStatistics по областям (tpc_<область>.csv),
#   выход — таблица «мс на кадр у таймера Nameplate» и попарные дельты быстрый/старый путь.
#   Число кадров в области берём НЕ из FrameTime (там свой счёт), а из Count таймера Nameplate,
#   делённого на число тикающих плашек: так кадры считаются тем же событием, что и измеряется.
import csv, os, sys

OUTD = os.path.dirname(os.path.abspath(__file__)).replace("\\", "/") if False else \
    r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05"
PLATES = int(sys.argv[1]) if len(sys.argv) > 1 else 426   # тикающих плашек в срезе (перепись прогона)


def read(region):
    p = os.path.join(OUTD, "tpc_%s.csv" % region)
    if not os.path.isfile(p) or os.path.getsize(p) == 0:
        return None
    with open(p, encoding="utf-8", errors="replace") as f:
        for r in csv.DictReader(f):
            if r["Name"] == "Nameplate":
                cnt = float(r["Count"])
                frames = cnt / PLATES
                return {"calls": cnt, "frames": frames,
                        "incl_ms": 1000.0 * float(r["Incl"]) / frames,
                        "excl_ms": 1000.0 * float(r["Excl"]) / frames,
                        "per_call_us": 1e6 * float(r["Incl"]) / cnt}
    return None


def main():
    regs = ["f1a3", "f0a3", "f0b3", "f1b3", "f1a4", "f0a4", "f0b4", "f1b4"]
    data = {r: read(r) for r in regs}
    print("область  вызовов/кадр  инкл.мс/кадр  экскл.мс/кадр  мкс/плашку")
    for r in regs:
        d = data[r]
        if not d:
            print("%-7s  НЕТ ДАННЫХ" % r)
            continue
        print("%-7s  %11.1f  %12.3f  %13.3f  %9.2f" % (r, PLATES, d["incl_ms"], d["excl_ms"], d["per_call_us"]))
    print("\nпары ABBA (быстрый − старый), инкл. мс/кадр:")
    deltas = []
    for a, b in (("f1a3", "f0a3"), ("f1b3", "f0b3"), ("f1a4", "f0a4"), ("f1b4", "f0b4")):
        if data[a] and data[b]:
            d = data[a]["incl_ms"] - data[b]["incl_ms"]
            deltas.append(d)
            print("  %s − %s = %+.3f" % (a, b, d))
    if deltas:
        print("среднее ABBA: %+.3f мс/кадр" % (sum(deltas) / len(deltas)))


if __name__ == "__main__":
    main()
