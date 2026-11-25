# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""make_branding.py — картинки оформления установщика AStory Remake (Inno Setup) из арта игры.

Источники (пути от расположения файла, корень = папка клона): RawAssets\\Textures\\map\\load\\Start_logo.png (заставка с эмблемой),
Launcher\\res\\app.ico (иконка игры). Нет RawAssets (публичный клон) — остаётся оформление из репозитория. Результат — рядом с этим файлом:
  wizard_<w>x<h>.bmp  боковая картинка страниц приветствия и завершения (все размеры DPI из справки Inno Setup)
  small_<w>x<h>.bmp   эмблема в шапке остальных страниц, на фирменном тёмном фоне
  AStory.ico          иконка установщика и деинсталлятора (= иконка игры)
Запуск: python make_branding.py (без побочных эффектов при импорте).
"""
import os
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))     # корень клона (DEPLOY\Installer\Branding → ..×3; имя папки любое)
SRC_ART = os.path.join(PROJECT, "RawAssets", "Textures", "map", "load", "Start_logo.png")   # необязателен: RawAssets не в git
SRC_ICO = os.path.join(PROJECT, "Launcher", "res", "app.ico")
HEADER_BG = (24, 18, 12)          # фирменный тёмный фон шапки (= цвет $000C1218 в AStoryRemake.iss)
GOLD = (242, 193, 78)             # золото логотипа — обводка эмблемы
# Размеры Inno Setup 6 для 100/120/150/175/200/225/250 % DPI
WIZARD_SIZES = [(164, 314), (192, 386), (246, 459), (273, 556), (328, 604), (355, 700), (410, 797)]
SMALL_SIZES = [(55, 55), (64, 68), (83, 80), (92, 97), (110, 106), (119, 123), (138, 140)]
EMBLEM_CENTER = (0.5, 0.492)      # центр эмблемы в Start_logo.png (доли ширины/высоты)
EMBLEM_RADIUS = 0.232             # радиус эмблемы (доля ширины)


def wizard_image(art, w, h):
    """Вертикальный срез заставки с эмблемой по центру + мягкое затемнение снизу и сверху."""
    from PIL import Image, ImageDraw
    W, H = art.size
    crop_w = int(H * w / h)
    left = max(0, min(W - crop_w, int(W * EMBLEM_CENTER[0] - crop_w / 2)))
    im = art.crop((left, 0, left + crop_w, H)).resize((w, h), Image.LANCZOS).convert("RGB")
    shade = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(shade)
    for y in range(h):
        t = y / (h - 1)
        a = int(150 * max(0.0, (t - 0.72) / 0.28) ** 1.4 + 70 * max(0.0, (0.18 - t) / 0.18) ** 1.6)
        d.line([(0, y), (w, y)], fill=a)
    dark = Image.new("RGB", (w, h), HEADER_BG)
    return Image.composite(dark, im, shade)


def small_image(art, w, h):
    """Эмблема (круг) с золотой обводкой на тёмном фоне шапки."""
    from PIL import Image, ImageDraw
    W, H = art.size
    cx, cy, r = int(W * EMBLEM_CENTER[0]), int(H * EMBLEM_CENTER[1]), int(W * EMBLEM_RADIUS)
    side = min(w, h)
    em = art.crop((cx - r, cy - r, cx + r, cy + r)).resize((side * 4, side * 4), Image.LANCZOS).convert("RGB")
    mask = Image.new("L", em.size, 0)
    ImageDraw.Draw(mask).ellipse((2, 2, em.size[0] - 3, em.size[1] - 3), fill=255)
    ring = Image.new("RGB", em.size, GOLD)
    ring_mask = Image.new("L", em.size, 0)
    ImageDraw.Draw(ring_mask).ellipse((0, 0, em.size[0] - 1, em.size[1] - 1), fill=255)
    out = Image.new("RGB", em.size, HEADER_BG)
    out.paste(ring, (0, 0), ring_mask)
    out.paste(em, (0, 0), mask)
    out = out.resize((side, side), Image.LANCZOS)
    canvas = Image.new("RGB", (w, h), HEADER_BG)
    canvas.paste(out, ((w - side) // 2, (h - side) // 2))
    return canvas


def main():
    if not os.path.isfile(SRC_ART):
        # Публичный клон: RawAssets нет — остаются закоммиченные wizard_*.bmp, small_*.bmp и AStory.ico.
        print("оформление: исходника %s нет — используется оформление из репозитория (%s)" % (SRC_ART, HERE))
        return 0
    from PIL import Image
    art = Image.open(SRC_ART).convert("RGB")
    made = []
    for w, h in WIZARD_SIZES:
        p = os.path.join(HERE, "wizard_%dx%d.bmp" % (w, h))
        wizard_image(art, w, h).save(p, "BMP")
        made.append(p)
    for w, h in SMALL_SIZES:
        p = os.path.join(HERE, "small_%dx%d.bmp" % (w, h))
        small_image(art, w, h).save(p, "BMP")
        made.append(p)
    shutil.copyfile(SRC_ICO, os.path.join(HERE, "AStory.ico"))
    made.append(os.path.join(HERE, "AStory.ico"))
    print("оформление: %d файлов в %s" % (len(made), HERE))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
