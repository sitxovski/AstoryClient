# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_soulmate_glow_sheet.py — лист сравнения «до/после» яркости показа навыков Дружбы (01.11.2026, soulmate-glow).
#   Кадры снимает самотест `asda.SelfTest.SoulmateGlow shot=<папка>` (AsdaSoulmateGlowSelfTest.cpp):
#   `soul_<код>_k<100·ключ>_t<мс>.png`, ключ `asda.Soul.SkillGlowScale` 1 (до) и 0.5 (после).
#   Мера — в окне вокруг персонажа (кадр после тонемаппера, поэтому линейные ×0.5 дают меньше, чем вдвое):
#   средняя яркость (Rec.709) и доля пересвеченных пикселей (яркость > 0.9 — «белое» свечение).
#   Пик — кадр с наибольшей средней яркостью окна при ключе 1; на нём и сравниваются «до» и «после».
#   Запуск: python Tools/asda_soulmate_glow_sheet.py <папка кадров> [<выходной png>]
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw

NAME_RX = re.compile(r"^soul_(\d+)_k(\d{3})_t(\d{4})\.png$")   # имя кадра самотеста
WIN = (0.25, 0.05, 0.75, 0.95)                                  # окно вокруг персонажа (доли кадра: x0, y0, x1, y1)


def load(path):
    """Кадр как float32 RGB 0..1."""
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0


def luma(a):
    """Яркость Rec.709."""
    return a[..., 0] * 0.2126 + a[..., 1] * 0.7152 + a[..., 2] * 0.0722


def main():
    if len(sys.argv) < 2:
        print("использование: asda_soulmate_glow_sheet.py <папка кадров> [<выходной png>]")
        return 2
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(src)), "soulmate_glow_sheet.png")
    frames = {}   # код → {(k, t): путь}
    for n in sorted(os.listdir(src)):
        m = NAME_RX.match(n)
        if m:
            frames.setdefault(int(m.group(1)), {})[(int(m.group(2)), int(m.group(3)))] = os.path.join(src, n)
    if not frames:
        print("кадров soul_*.png нет")
        return 1

    rows = []      # (код, t пика, путь до, путь после, яркость до/после, пересвет до/после)
    report = []    # строки отчёта
    for code, fr in sorted(frames.items()):
        times = sorted({t for (_, t) in fr})
        imgs = {key: load(p) for key, p in fr.items()}
        h, w = next(iter(imgs.values())).shape[:2]
        x0, y0, x1, y1 = int(WIN[0] * w), int(WIN[1] * h), int(WIN[2] * w), int(WIN[3] * h)
        lum = {key: luma(a)[y0:y1, x0:x1] for key, a in imgs.items()}
        mean = {key: float(v.mean()) for key, v in lum.items()}               # средняя яркость окна
        over = {key: float((v > 0.9).mean()) for key, v in lum.items()}       # доля пересвета
        tk = [t for t in times if (100, t) in mean and (50, t) in mean]
        if not tk:
            continue
        peak = max(tk, key=lambda t: mean[(100, t)])
        rows.append((code, peak, fr[(100, peak)], fr[(50, peak)], mean[(100, peak)], mean[(50, peak)],
                     over[(100, peak)], over[(50, peak)]))
        report.append(f"{code}: пик t={peak} мс — яркость окна {mean[(100, peak)]:.3f} → {mean[(50, peak)]:.3f}, "
                      f"пересвет {over[(100, peak)] * 100:.1f}% → {over[(50, peak)] * 100:.1f}%")

    tw, th = 512, 384   # клетка листа
    sheet = Image.new("RGB", (tw * 2, (th + 24) * len(rows) + 30), (20, 20, 20))
    d = ImageDraw.Draw(sheet)
    d.text((8, 8), "ДО: asda.Soul.SkillGlowScale 1   |   ПОСЛЕ: 0.5 (умолчание)", fill=(255, 255, 255))
    y = 30
    for code, peak, p1, p05, m1, m05, o1, o05 in rows:
        d.text((8, y + 4), f"показ {code}, пик {peak} мс: яркость окна {m1:.3f} -> {m05:.3f}, пересвет {o1 * 100:.1f}% -> {o05 * 100:.1f}%",
               fill=(255, 230, 120))
        sheet.paste(Image.open(p1).convert("RGB").resize((tw, th)), (0, y + 24))
        sheet.paste(Image.open(p05).convert("RGB").resize((tw, th)), (tw, y + 24))
        y += th + 24
    sheet.save(out)
    for r in report:
        print(r)
    print("лист:", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
