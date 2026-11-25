# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_gacha_composite_check.py — ЗАМЕР КОМПОЗИТА кадра гачи по сохранённым кадрам.
#
# ★ Зачем отдельный счётчик. Кадр (`sb_*_color.png`, зонд `asda_gacha_shadow_check.py`) — это
#   выход капчера: RGB ПРЕДУМНОЖЕН на непрозрачность, A = ОБРАТНАЯ непрозрачность (доказано
#   калибровкой, `Reports/gacha_alpha_composite.md` §1). Как этот кадр ляжет в окно, решает
#   мастер `M_AsdaGachaScene3D` + бленд Slate `BF_SourceAlpha / BF_InverseSourceAlpha`
#   (`SlateRHIRenderingPolicy.cpp:566`), причём Slate смешивает уже ГАММА-скорректированные
#   значения (`SlateElementPixelShader.usf:653` — `GammaCorrect` применяется ДО вывода).
#   Здесь обе формулы считаются по-честному и сравниваются с ЭТАЛОНОМ оригинала.
#
#   формула СТАРАЯ (цвет как есть)     : out = RGB·op + F·(1−op)      ⇒ лишнее умножение на op
#   формула НОВАЯ (отмена предумнож.)  : out = sRGB(lin(RGB)/op)·op + F·(1−op)
#   ЭТАЛОН оригинала (D3D9, гамма)     : out = C·op + F·(1−op),  C = байт текстуры (у тени 31)
#
# Запуск: python Tools/asda_gacha_composite_check.py [имя_кадра ...]   (без unreal, чистый python)
import os
import sys

import numpy as np
from PIL import Image

FRAME_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/gacha_3d"
FLOOR = 190.0            # светлый фон окна, на котором владелец видит «серую» тень
SHADOW_TEX_COLOR = 31.0  # байт текстуры `gachya_shadow` в ядре пятна (α = 204/255)


def srgb_to_lin(b):
    c = b / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(x):
    x = np.clip(x, 0.0, 1.0)
    return 255.0 * np.where(x <= 0.0031308, x * 12.92, 1.055 * x ** (1.0 / 2.4) - 0.055)


def shadow_mask(rgb, a):
    """Пиксели ТЕНИ: полупрозрачные, тёмные и серые (у корпуса насыщенность 0.5+, у тени ~0)."""
    mx = rgb.max(2)
    mn = rgb.min(2)
    sat = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1.0), 0.0)
    return (a > 0) & (a < 250) & (mx < 80) & (sat < 0.25)


def report(name):
    p = os.path.join(FRAME_DIR, "%s_color.png" % name)
    if not os.path.exists(p):
        print("нет кадра %s" % p)
        return
    im = np.array(Image.open(p).convert("RGBA")).astype(np.float32)
    rgb, a = im[..., :3], im[..., 3]
    op = 1.0 - a / 255.0
    sel = shadow_mask(rgb, a)
    g = rgb[sel].mean(1)
    o = op[sel]

    old = g * o + FLOOR * (1.0 - o)
    straight = lin_to_srgb(srgb_to_lin(g) / np.maximum(o, 1.0 / 255.0))
    new = straight * o + FLOOR * (1.0 - o)
    ref = SHADOW_TEX_COLOR * o + FLOOR * (1.0 - o)

    core = o >= 0.75            # ядро пятна — там альфа текстуры 204/255 = 0.800
    def q(v, m=None):
        v = v if m is None else v[m]
        return "медиана %.1f / p10 %.1f / min %.1f" % (np.median(v), np.percentile(v, 10), v.min())

    print("── %s ── пикселей тени %d, из них ядро (op ≥ 0.75) %d" % (name, sel.sum(), core.sum()))
    print("   непрозрачность: медиана %.3f  p90 %.3f  max %.3f" % (np.median(o), np.percentile(o, 90), o.max()))
    print("   цвет в кадре (предумнож.): медиана %.1f | восстановленный прямой: медиана %.1f"
          % (np.median(g), np.median(straight)))
    print("   композит на фоне %d:" % FLOOR)
    print("     СТАРАЯ формула : %s" % q(old))
    print("     НОВАЯ формула  : %s" % q(new))
    print("     ЭТАЛОН (C=31)  : %s" % q(ref))
    if core.any():
        print("   ЯДРО тени: старая %.1f · новая %.1f · эталон %.1f (непрозр. %.3f, прямой цвет %.1f)"
              % (np.median(old[core]), np.median(new[core]), np.median(ref[core]),
                 np.median(o[core]), np.median(straight[core])))

    # превью «как ляжет в окно» — обе формулы, для глаза владельца
    full_op = op[..., None]
    full_straight = lin_to_srgb(srgb_to_lin(rgb) / np.maximum(full_op, 1.0 / 255.0))
    for tag, img in (("old", rgb * full_op + FLOOR * (1 - full_op)),
                     ("new", full_straight * full_op + FLOOR * (1 - full_op))):
        Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).save(
            os.path.join(FRAME_DIR, "gcomp_%s_%s.png" % (name, tag)))


if __name__ == "__main__":
    frames = sys.argv[1:] or ["sb_m1", "sb_m1_nobowl", "sb_m5", "sb_m5_nobowl"]
    for f in frames:
        report(f)
