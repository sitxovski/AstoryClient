# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# setE4_fix_luma.py — доводка E-4 (07.09.2026): средняя люма по кропу кадра для сравнения «свет/без света» и «до/после».
#   Кропы = как в setE4_report.md §5: C2 земля под фонарём (x 34–66 %, y 62–96 %); C4 кристалл (x 34–66 %, y 30–72 %).
#   Люма sRGB = 0.299 R + 0.587 G + 0.114 B (0..255). Печатает среднюю; при двух аргументах — отношение первый/второй.
#   Запуск: python Tools/setE4_fix_luma.py C2 <after.png> [<nolight.png>]
import sys

CROPS = {  # (x0, x1, y0, y1) в долях
    "C2": (0.34, 0.66, 0.62, 0.96),
    "C4": (0.34, 0.66, 0.30, 0.72),
}


def mean_luma(path, crop):
    from PIL import Image
    import numpy as np
    im = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)
    h, w = im.shape[:2]
    x0, x1, y0, y1 = crop
    sub = im[int(y0 * h):int(y1 * h), int(x0 * w):int(x1 * w), :]
    lum = 0.299 * sub[..., 0] + 0.587 * sub[..., 1] + 0.114 * sub[..., 2]
    return float(lum.mean())


def main(argv):
    if len(argv) < 2:
        print("usage: setE4_fix_luma.py C2|C4 <img> [<baseline_nolight>]"); return 2
    key, img = argv[0], argv[1]
    crop = CROPS[key]
    a = mean_luma(img, crop)
    if len(argv) >= 3:
        b = mean_luma(argv[2], crop)
        print("%s: luma=%.3f base=%.3f ratio=x%.4f  (%s)" % (key, a, b, a / b if b else 0, img))
    else:
        print("%s: luma=%.3f  (%s)" % (key, a, img))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
