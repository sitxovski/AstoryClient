# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""Пачка 19, баг 4: поиск прозрачной щели окна 6000 по кадрам самотеста `asda.Shop.PremiumGapSelfTest`.
Участок проверки — правее полос вкладок (x от правого края витрины − 140 до правого края − 20), по высоте от низа шапки окна
до верха витрины: там должен быть только фон окна (чёрный). Пиксель «не чёрный» = просвет (мир за окном).
Запуск: python Tools/asda_premium_gap_scan.py <папка кадров> <журнал самотеста>
"""
import glob
import re
import sys

from PIL import Image


def main():
    """Точка входа."""
    shots, log = sys.argv[1], sys.argv[2]
    geo = {}
    for line in open(log, encoding="utf-8", errors="ignore"):
        m = re.search(r"ГЕОМ (\d+)x(\d+) (\w+) win=\S+ base=(\d+),(\d+),(\d+),(\d+) .* list=(\d+),(\d+),(\d+),(\d+)", line)
        if m:
            g = [int(v) if v.isdigit() else v for v in m.groups()]
            geo[(g[0], g[1], g[2])] = g
    bad = 0
    for (w, h, c), g in sorted(geo.items()):
        base_bottom, list_top, list_right = g[6], g[8], g[9]
        files = glob.glob("%s/*_%dx%d_%s.png" % (shots, w, h, c))
        if not files:
            print("%dx%d %s: нет кадра" % (w, h, c))
            continue
        im = Image.open(files[0]).convert("RGB")
        rows = []
        for y in range(base_bottom + 1, list_top - 1):
            n = 0
            for x in range(list_right - 140, list_right - 20):
                r, gg, b = im.getpixel((x, y))
                if r + gg + b > 24:
                    n += 1
            if n:
                rows.append((y, n))
        if rows:
            bad += 1
        print("%dx%d %-4s шапка до %d, витрина с %d: %s" % (w, h, c, base_bottom, list_top, ("ПРОСВЕТ строки %s" % rows[:6]) if rows else "чисто"))
    print("ИТОГ: кадров с просветом %d" % bad)


if __name__ == "__main__":
    main()
