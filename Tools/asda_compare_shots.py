# Copyright (c) 2026 @sitxovski (Discord)
# asda_compare_shots.py — числовое сравнение кадров «до/после» (вне UE).
#
# Считает по каждой паре `before_X.png` / `after_X.png`:
#   • долю пикселей, где кадр изменился заметно (|Δ| > 8 хотя бы в одном канале);
#   • среднюю и максимальную разницу яркости;
#   • ПЛОЩАДЬ СИЛУЭТА — долю пикселей, НЕ совпадающих с фоном-подложкой (пурпур 0.85/0.10/0.65).
#     Рост силуэта = вернулись вырезанные masked-мастером участки; падение = стало прозрачнее.
# Кладёт рядом склейку `cmp_X.png` (до | после | карта различий).
#
# Запуск: python asda_compare_shots.py [каталог]
import os
import sys

import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
DEFAULT_DIR = os.path.join(_P.RE_REPORTS, "blend_rollout")
# Цвет подложки в sRGB (материал M_BlendShotBG, Unlit Emissive = 0.85, 0.10, 0.65 в линейном).
BG_LINEAR = (0.85, 0.10, 0.65)


def _srgb(x):
    return 12.92 * x if x <= 0.0031308 else 1.055 * (x ** (1 / 2.4)) - 0.055


def main():
    import numpy as np
    from PIL import Image
    d = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DIR
    bg = np.array([_srgb(c) * 255.0 for c in BG_LINEAR])
    tags = sorted(f[len("before_"):-4] for f in os.listdir(d)
                  if f.startswith("before_") and f.endswith(".png"))
    print("%-18s %8s %8s %8s %8s %8s" % ("модель", "изм.%", "Δср", "Δmax", "силуэт до", "силуэт после"))
    rows = []
    for t in tags:
        pb = os.path.join(d, "before_%s.png" % t)
        pa = os.path.join(d, "after_%s.png" % t)
        if not os.path.exists(pa):
            print("%-18s  нет кадра «после»" % t)
            continue
        A = np.asarray(Image.open(pb).convert("RGB")).astype(np.float32)
        B = np.asarray(Image.open(pa).convert("RGB")).astype(np.float32)
        if A.shape != B.shape:
            print("%-18s  размеры не совпали: %s vs %s" % (t, A.shape, B.shape))
            continue
        dif = np.abs(A - B)
        changed = float((dif.max(axis=2) > 8).mean())
        # силуэт = пиксели, отличающиеся от цвета подложки
        sa = float((np.abs(A - bg).max(axis=2) > 24).mean())
        sb = float((np.abs(B - bg).max(axis=2) > 24).mean())
        print("%-18s %7.2f%% %8.2f %8.0f %8.3f %12.3f"
              % (t, changed * 100, float(dif.mean()), float(dif.max()), sa, sb))
        rows.append((t, changed, sa, sb))
        # склейка: до | после | карта различий (усиленная)
        m = np.clip(dif.max(axis=2) * 6, 0, 255).astype(np.uint8)
        vis = np.concatenate([A.astype(np.uint8), B.astype(np.uint8),
                              np.dstack([m, m, m])], axis=1)
        Image.fromarray(vis).save(os.path.join(d, "cmp_%s.png" % t))
    if rows:
        print("\nпар сравнено: %d; из них с видимым изменением (>0.1%% пикселей): %d"
              % (len(rows), sum(1 for r in rows if r[1] > 0.001)))


if __name__ == "__main__":
    main()
