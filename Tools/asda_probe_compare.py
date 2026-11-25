# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_probe_compare.py — числовое сравнение кадров пробника (вне UE, numpy+PIL).
#   Для каждой пары «мастер A / мастер B» при одинаковых меше+текстуре+свете считает:
#     покрытие (доля непустых пикселей), среднюю/медианную яркость по ПОКРЫТЫМ пикселям,
#     среднюю яркость по ТЁМНОЙ половине (теневая сторона — там и жалоба «легко затемняется»),
#     гистограмму (8 корзин) и max|Δ|.
#   Запуск: python asda_probe_compare.py [tagA] [tagB]      (по умолчанию сравнивает *_before.png)
import os, sys, glob
import numpy as np
from PIL import Image

DIR = r"@@ASTORY_DEV_ROOT@@\RE\reports\matprobe"
TAGS = sys.argv[1:] or ["before"]
BG_EPS = 2          # порог «фон» (чёрный фон сцены)
BASE = "ObjectLit"                       # эталон «как было до правки прозрачности»
VARIANTS = ["Regressed", "CharLit"]      # с чем сравниваем: регрессия и текущий (после фикса)


def load(path):
    a = np.asarray(Image.open(path).convert("RGB")).astype(np.float64)
    # яркость sRGB (Rec.709 по гамма-кодированным значениям — то, что видит глаз на экране)
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def stats(lum, mask):
    v = lum[mask]
    if v.size == 0:
        return dict(n=0, mean=0, med=0, dark=0, p10=0, p90=0)
    thr = np.median(v)
    dark = v[v <= thr]
    return dict(n=int(v.size), mean=float(v.mean()), med=float(np.median(v)),
                dark=float(dark.mean()) if dark.size else 0.0,
                p10=float(np.percentile(v, 10)), p90=float(np.percentile(v, 90)))


def hist(lum, mask, bins=8):
    v = lum[mask]
    if v.size == 0:
        return [0] * bins
    h, _ = np.histogram(v, bins=bins, range=(0, 256))
    return (h / v.size * 100.0).round(1).tolist()


rows = []

hdr = "%-8s %-22s | %-24s | %-24s | %-13s" % (
    "tag", "кадр/вариант", "A: сред/медиана/тень/чёрн%", "B: сред/медиана/тень/чёрн%", "Δсред / Δmax")


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for tag in TAGS:
        keys = sorted(set(os.path.basename(p).rsplit("_", 2)[0]
                          for p in glob.glob(os.path.join(DIR, "*_%s.png" % tag))))
        for key in keys:
          for variant in VARIANTS:
            pa = os.path.join(DIR, "%s_%s_%s.png" % (key, BASE, tag))
            pb = os.path.join(DIR, "%s_%s_%s.png" % (key, variant, tag))
            if not (os.path.exists(pa) and os.path.exists(pb)):
                continue
            key_v = "%s/%s" % (key, variant)
            la, lb = load(pa), load(pb)
            ma, mb = la > BG_EPS, lb > BG_EPS
            # ★ силуэт = ОБЪЕДИНЕНИЕ: при ambient=0 теневая сторона у материала БЕЗ ambient-floor
            #   уходит в чистый чёрный и по пересечению масок просто выпадала бы из статистики
            #   (именно это и маскировало масштаб регрессии в первом прогоне).
            both = ma | mb
            sa, sb = stats(la, ma), stats(lb, mb)
            sab, sbb = stats(la, both), stats(lb, both)
            d = np.abs(la - lb)[both]
            rows.append((tag, key_v, sa, sb, sab, sbb,
                         float(d.mean()) if d.size else 0.0, float(d.max()) if d.size else 0.0,
                         hist(la, both), hist(lb, both),
                         float(ma.mean() * 100), float(mb.mean() * 100),
                         float((la[both] < 8).mean() * 100), float((lb[both] < 8).mean() * 100)))
    print("=== ПРОБНИК СВЕТОТЕНИ: база A = M_AsdaObjectLit («как было»), B = вариант ===")
    print("яркость 0..255 (sRGB, Rec.709), по пикселям меша; 'тень' = средняя по тёмной половине\n")
    print(hdr); print("-" * len(hdr))
    for (tag, key, sa, sb, sab, sbb, dm, dx, ha, hb, ca, cb, ba, bb) in rows:
        print("%-8s %-22s | %6.2f /%6.2f /%6.2f /%5.1f | %6.2f /%6.2f /%6.2f /%5.1f | %6.2f /%6.2f"
              % (tag, key, sab["mean"], sab["med"], sab["dark"], ba,
                 sbb["mean"], sbb["med"], sbb["dark"], bb, dm, dx))
    print()
    print("Гистограммы (доля %% пикселей меша по 8 корзинам яркости 0..255):")
    for (tag, key, sa, sb, sab, sbb, dm, dx, ha, hb, ca, cb, ba, bb) in rows:
        print("  %-14s A: %s" % (key + "_" + tag, ha))
        print("  %-14s B: %s" % ("", hb))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
