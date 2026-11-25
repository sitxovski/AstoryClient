# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# make_eac_splash.py — заставка окна Easy Anti-Cheat (EasyAntiCheat/SplashScreen.png, 800×450) в стиле игры
# (★ 17.10.2026, просьба владельца «поставь наше изображение в сплэш EAC красиво»).
#
# Что делает (детерминированно, без дат внутри PNG):
#   • фон — сцена загрузки игры RawAssets/Textures/map/load/Start_logo.png (тот же город, что у лаунчера и установщика):
#     запечённый в неё круглый значок закрашивается гладкой заливкой по окружению (гармоническое заполнение, без OpenCV),
#     затем фон размывается и притемняется — «призрака» значка нет;
#   • поверх — чёткий круглый значок AStory Remake из арта владельца Data/OwnerArt/icon.png (белый фон вырезается той же
#     функцией, что у иконки игры: make_app_icon.cut_background), с мягким золотым ореолом и тенью;
#   • низ кадра затемнён и отделён тонкой золотой линией: там бутстрапер EAC рисует свою строку состояния и полосу загрузки.
#   Результат — AstoryClient/Build/EAC/SplashScreen.png; eac_package.py (шаг 5, step_dist) кладёт его в
#   <игра>/EasyAntiCheat/SplashScreen.png (путь из Settings.json `requested_splash`) ДО каталога целостности (шаг 7).
# Пересоздаётся, если PNG нет, исходники или этот скрипт новее, или --force.
# Запуск: python Tools/make_eac_splash.py [--force] [--preview <png>]
# При импорте ничего не делает (закон tools-no-import-side-effects).
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))                                         # …\AsdaReborn\Tools
PROJECT_DIR = os.path.dirname(HERE)                                                        # …\AsdaReborn
PORT_DIR = os.path.dirname(PROJECT_DIR)                                                    # …\port
BG_PNG = os.path.join(PROJECT_DIR, "RawAssets", "Textures", "map", "load", "Start_logo.png")   # сцена загрузки (2048²)
ICON_PNG = os.path.join(PORT_DIR, "Data", "OwnerArt", "icon.png")                          # арт значка владельца (1254²)
OUT_PNG = os.path.join(PROJECT_DIR, "Build", "EAC", "SplashScreen.png")                    # готовая заставка
W, H = 800, 450                                                                            # размер заставки тулкита EAC 2.3.0

# Запечённый значок в Start_logo.png (замер 17.10.2026: МНК по золотому ободку в кольце 420…540 px от (1024,1014),
# внешний край — медиана по 360 лучам): центр и радиус в пикселях исходника 2048×2048.
BAKED_CX, BAKED_CY, BAKED_R = 1022.7, 1018.7, 477.0
BAKED_MARGIN = 10          # запас закраски за ободок, px рабочего масштаба (сглаженный край ободка и тень)
BG_BLUR = 7.0              # размытие фона, px (σ) в кадре 800×450
BG_TOP = 236               # верх кадра в рабочем масштабе (фон 800×800): небо сверху, город снизу
BADGE_D = 280              # диаметр значка в кадре, px
BADGE_CY = 188             # центр значка по вертикали, px
STATUS_H = 92              # высота нижней зоны под строку состояния EAC, px


def _harmonic_fill(arr, mask, iters=(400, 160, 80, 40)):
    """Закрасить mask (True — заполнить) гладко по границе: пирамида + Якоби (уравнение Лапласа). arr — float32 H×W×3."""
    import numpy as np
    from scipy import ndimage

    levels = [(arr, mask)]
    while min(levels[-1][0].shape[:2]) > 64:
        a, m = levels[-1]
        a2 = a[: a.shape[0] // 2 * 2, : a.shape[1] // 2 * 2].reshape(a.shape[0] // 2, 2, a.shape[1] // 2, 2, 3).mean(axis=(1, 3))
        m2 = m[: m.shape[0] // 2 * 2, : m.shape[1] // 2 * 2].reshape(m.shape[0] // 2, 2, m.shape[1] // 2, 2).any(axis=(1, 3))
        levels.append((a2, m2))
    kernel = np.array([[0, 1, 0], [1, 0, 1], [0, 1, 0]], np.float32) / 4.0
    guess = None
    for li in range(len(levels) - 1, -1, -1):
        a, m = levels[li]
        cur = a.copy()
        if guess is None:
            cur[m] = a[~m].mean(axis=0)
        else:
            up = np.repeat(np.repeat(guess, 2, axis=0), 2, axis=1)
            up = np.pad(up, ((0, cur.shape[0] - up.shape[0]), (0, cur.shape[1] - up.shape[1]), (0, 0)), mode="edge")
            cur[m] = up[m]
        n = iters[min(len(levels) - 1 - li, len(iters) - 1)] if li else iters[-1]
        for _ in range(n):
            for c in range(3):
                sm = ndimage.convolve(cur[..., c], kernel, mode="nearest")
                cur[..., c][m] = sm[m]
        guess = cur
    return guess


def render():
    """Собрать заставку → PIL.Image RGB 800×450."""
    import numpy as np
    from PIL import Image, ImageDraw, ImageFilter

    if HERE not in sys.path:
        sys.path.insert(0, HERE)
    import make_app_icon as MI

    src = Image.open(BG_PNG).convert("RGB")
    s = W / src.width                                                                       # фон по ширине кадра
    work = src.resize((W, round(src.height * s)), Image.Resampling.LANCZOS)
    arr = np.asarray(work).astype(np.float32)
    yy, xx = np.mgrid[0:arr.shape[0], 0:arr.shape[1]]
    hole = np.hypot(xx - BAKED_CX * s, yy - BAKED_CY * s) <= BAKED_R * s + BAKED_MARGIN     # запечённый значок
    arr = _harmonic_fill(arr, hole)
    bg = Image.fromarray(np.clip(arr, 0, 255).round().astype(np.uint8), "RGB")
    bg = bg.crop((0, BG_TOP, W, BG_TOP + H)).filter(ImageFilter.GaussianBlur(BG_BLUR)).convert("RGBA")

    # притемнение: ровный тон + низ под строку состояния + виньетка
    shade = Image.new("RGBA", (W, H))
    d = ImageDraw.Draw(shade)
    for y in range(H):
        t = y / (H - 1)
        a = 95 + int(150 * max(0.0, (t - 0.5) / 0.5) ** 1.5)
        d.line([(0, y), (W, y)], fill=(14, 10, 6, min(235, a)))
    bg.alpha_composite(shade)
    vig = Image.new("L", (W, H), 0)
    ImageDraw.Draw(vig).ellipse((-W * 0.12, -H * 0.3, W * 1.12, H * 1.2), fill=255)
    vig = vig.filter(ImageFilter.GaussianBlur(70))
    dark = Image.new("RGBA", (W, H), (6, 4, 2, 0))
    dark.putalpha(vig.point(lambda v: int((255 - v) * 0.7)))
    bg.alpha_composite(dark)

    # значок: вырезанный арт владельца → кадр
    badge, _ = MI.cut_background(Image.open(ICON_PNG))
    badge = badge.crop(badge.getbbox())
    badge = badge.convert("RGBa").resize((BADGE_D, BADGE_D), Image.Resampling.LANCZOS).convert("RGBA")
    bx, by = (W - BADGE_D) // 2, BADGE_CY - BADGE_D // 2
    # золотой ореол за значком
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gr = BADGE_D // 2 + 30
    ImageDraw.Draw(glow).ellipse((W // 2 - gr, BADGE_CY - gr, W // 2 + gr, BADGE_CY + gr), fill=(246, 190, 70, 150))
    bg.alpha_composite(glow.filter(ImageFilter.GaussianBlur(24)))
    # тень
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sa = Image.new("RGBA", badge.size, (0, 0, 0, 0))
    sa.putalpha(badge.getchannel("A").point(lambda v: int(v * 0.7)))
    sh.paste(sa, (bx + 2, by + 8), sa)
    bg.alpha_composite(sh.filter(ImageFilter.GaussianBlur(10)))
    bg.alpha_composite(badge, (bx, by))

    # золотая линия над зоной строки состояния EAC
    line = Image.new("RGBA", (W, 2))
    px = line.load()
    for x in range(W):
        t = (1 - abs(x - W / 2) / (W / 2)) ** 1.5
        px[x, 0] = (246, 201, 92, int(210 * t))
        px[x, 1] = (201, 150, 43, int(110 * t))
    bg.alpha_composite(line, (0, H - STATUS_H))
    return bg.convert("RGB")


def ensure_splash(force=False, log=print):
    """Пересоздать SplashScreen.png при необходимости. → путь. Нет исходника — SystemExit."""
    for p in (BG_PNG, ICON_PNG):
        if not os.path.isfile(p):
            raise SystemExit("ОТКАЗ: нет исходника заставки EAC %s" % p)
    newest = max(os.path.getmtime(p) for p in (BG_PNG, ICON_PNG, os.path.abspath(__file__)))
    if force or not os.path.isfile(OUT_PNG) or os.path.getmtime(OUT_PNG) < newest:
        img = render()
        os.makedirs(os.path.dirname(OUT_PNG), exist_ok=True)
        img.save(OUT_PNG, format="PNG", optimize=True)
        log("заставка EAC: %s пересоздана (%dx%d)" % (OUT_PNG, img.width, img.height))
    from PIL import Image
    with Image.open(OUT_PNG) as im:
        if im.size != (W, H):
            raise SystemExit("ОТКАЗ: заставка EAC %s — %dx%d вместо %dx%d" % (OUT_PNG, im.width, im.height, W, H))
    log("заставка EAC: %s — %dx%d, %d б" % (OUT_PNG, W, H, os.path.getsize(OUT_PNG)))
    return OUT_PNG


def main(argv=None):
    """Точка входа командной строки."""
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser(description="заставка окна EAC (SplashScreen.png 800×450) в стиле игры")
    ap.add_argument("--force", action="store_true", help="пересоздать, даже если готовая новее исходников")
    ap.add_argument("--preview", help="только нарисовать в указанный PNG (готовую заставку не трогать)")
    a = ap.parse_args(argv)
    if a.preview:
        render().save(a.preview, format="PNG")
        print("превью: %s" % a.preview)
        return 0
    ensure_splash(force=a.force)
    return 0


if __name__ == "__main__":
    sys.exit(main())
