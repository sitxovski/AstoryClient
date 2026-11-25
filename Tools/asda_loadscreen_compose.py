# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_loadscreen_compose.py — ★ [ПОРТ] 10.11.2026 КАРТИНКИ ЗАГРУЗКИ ДЛЯ КАРТ БЕЗ СВОИХ (решение владельца «взять донорские картинки»).
#
# ЗАЧЕМ. Оригинал (`CMap_LoadLoadingScreen@0x975650`, `RE/etalon/world_map.c:196-218`) берёт картинку из `map\<dir>\load\*`,
#   пусто → `map\load\start_logo.TEX`. В эталоне `Donors/AsdaChroniclesInterlude` у Inferion (18) и battlefield (19) папки `load`
#   нет/пуста — на этих картах всегда логотип (жалоба тестера «у Inferion нет экрана загрузки»). Владелец решил взять донорские.
#
# ПОЧЕМУ НЕ «КАК ЕСТЬ». У КАЖДОЙ донорской картинки в шапке рамки — логотип ЧУЖОГО сервера (Фантазиум, ASDA Global, Fable Classic,
#   World of Dreams, арабский). Все эталонные картинки нарисованы в ОДНОЙ рамке с логотипом «AStory REMAKE» (шаблон тот же, что у
#   доноров, отличается только логотип и цвет). Поэтому собираем: РАМКА+ЛОГОТИП — из эталонных `e_*.png` (медиана 29 картинок —
#   рамка одинакова у всех), ОКНО — сцена донора.
#
# ИСТОЧНИКИ СЦЕН — рабочий клиент владельца `Donors/AsdaChroniclesInterlude_MINE` (оттуда же он сам брал, md5 = Fantazium):
#   map/inferion/load/limg_Inferion00.TEX (md5 75f11abb…), map/battlefield/load/limg_battlefield00.TEX (md5 57f69778…), 1024², DXT2.
#
# МАСКА ОКНА. «Постоянные» пиксели (std по эталонам < 10) = рамка и логотип; окно = связная область НЕпостоянных, содержащая центр
#   картинки (1024,1100). Сцена донора внутри собственной белой каймы донора (область, где две донорские картинки различаются) — а где
#   кайма донора заходит в окно эталона, точки заливаются нормированной свёрткой сцены (плавно, ревизия 10.11). Вне окна, где эталоны
#   расходятся (плашки/блики отдельных картинок), — пиксель одной эталонной `e_Flammio.png`, не медиана.
#
# ВЫХОД: RawAssets/Textures/map/<dir>/load/<имя>.png 2048×2048 RGB (как остальные загрузочные исходники). Импорт в UE —
#   `asda_import_loadscreen_donor.py`. Идемпотентно: перезаписывает только свои два файла.
# Запуск (обычный Python с numpy/scipy/Pillow): python Tools\asda_loadscreen_compose.py [--preview <папка>]
import glob
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RAW = os.path.normpath(os.path.join(HERE, "..", "RawAssets", "Textures", "map"))
import asda_paths as _P   # пути — только через asda_paths (CLAUDE.md); MAP_DIR = рабочий клиент _MINE/map
DONOR = _P.MAP_DIR
# (папка карты, имя картинки) — имя как у донора; папка = dir реестра карт (asda_map_registry.json), регистр как у Content.
JOBS = [("inferion", "limg_Inferion00"), ("battlefield", "limg_battlefield00")]
SIZE = 2048          # размер эталонных исходников загрузки
CENTER = (1100, 1024)  # (y, x) точка заведомо внутри окна картинки


def biggest(nd, np, m):
    """Самая крупная связная область маски."""
    lab, n = nd.label(m)
    s = nd.sum(m, lab, range(1, n + 1))
    return lab == (1 + int(np.argmax(s)))


def main():
    import numpy as np
    from PIL import Image, ImageFilter
    from scipy import ndimage as nd
    sys.path.insert(0, HERE)
    import asda_fmt_tex as ft   # общий декодер .TEX

    preview = None
    if "--preview" in sys.argv:
        preview = sys.argv[sys.argv.index("--preview") + 1]
        os.makedirs(preview, exist_ok=True)

    frames = sorted(f for f in glob.glob(os.path.join(RAW, "*", "load", "e_*.png")))
    if len(frames) < 10:
        raise RuntimeError("мало эталонных рамок e_*.png: %d" % len(frames))
    A = np.stack([np.asarray(Image.open(p).convert("RGB"), dtype=np.float32) for p in frames])
    if A.shape[1:] != (SIZE, SIZE, 3):
        raise RuntimeError("эталонные рамки не %d²: %s" % (SIZE, A.shape))
    sd = A.std(0).max(-1)
    frame = np.median(A, 0)
    flammio = [i for i, f in enumerate(frames) if os.path.basename(f) == "e_Flammio.png"]
    if not flammio:
        raise RuntimeError("нет e_Flammio.png")
    frame = np.where((sd >= 10)[..., None], A[flammio[0]], frame)

    # окно эталона: связная НЕпостоянная область вокруг центра; мелкие дыры (случайно «постоянные» точки сцены) закрываем
    lab, _ = nd.label(~nd.binary_closing(sd < 10, iterations=4))
    win = lab == lab[CENTER]
    holes = nd.binary_fill_holes(win) & ~win
    hl, hn = nd.label(holes)
    hs = nd.sum(holes, hl, range(1, hn + 1))
    for i, s in enumerate(hs):
        if s < 3000:
            win |= hl == (i + 1)
    win = biggest(nd, np, win)
    frac = float(win.mean())
    if not (0.30 < frac < 0.40):
        raise RuntimeError("окно эталона неправдоподобно: доля %.3f" % frac)
    # ★ ревизия-2 10.11: кончики зубцов окна, где эталоны расходятся (sd ≥ 10), маска окна не захватывала — там
    #   просвечивал e_Flammio (жёлтые/фиолетовые точки 1–3 px). Расходящиеся пиксели в 6 px от окна отдаём сцене.
    win = win | (nd.binary_dilation(win, iterations=6) & (sd >= 10))
    alpha = np.asarray(Image.fromarray((win * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2)),
                       dtype=np.float32)[..., None] / 255.0

    scenes = {}
    for d, name in JOBS:
        src = os.path.join(DONOR, d, "load", name + ".TEX")
        img = ft.decode_file(src)
        if img is None:
            raise RuntimeError("не декодирован %s" % src)
        scenes[(d, name)] = np.asarray(img.convert("RGB").resize((SIZE, SIZE), Image.LANCZOS), dtype=np.float32)
    # окно донора: там, где две донорские картинки (одна рамка, разные сцены) различаются
    a, b = list(scenes.values())[:2]
    dvar = np.abs(a - b).max(-1) > 10
    inner_d = nd.binary_fill_holes(nd.binary_closing(biggest(nd, np, nd.binary_opening(dvar, iterations=6)), iterations=12))
    inner_d = nd.binary_erosion(inner_d, iterations=2)
    # ★ ревизия 10.11: где кайма донора заходит в окно эталона — НЕ ближайший пиксель (давал плоские «мозаичные» пятна), а
    #   нормированная свёртка (размытие известных пикселей сцены / размытие маски) с растущей сигмой: плавная заливка.
    known = inner_d.astype(np.float32)

    def fill(sc):
        out = sc.copy()
        todo = ~inner_d
        for sigma in (4, 8, 16, 32, 64):
            wsum = nd.gaussian_filter(known, sigma)
            ok = todo & (wsum > 1e-3)
            for ch in range(3):
                num = nd.gaussian_filter(sc[..., ch] * known, sigma)
                out[..., ch] = np.where(ok, num / np.maximum(wsum, 1e-6), out[..., ch])
            todo &= ~ok
            if not todo.any():
                break
        return out

    for (d, name), sc in scenes.items():
        sc = fill(sc)
        out = (frame * (1 - alpha) + sc * alpha).clip(0, 255).astype(np.uint8)
        dst_dir = os.path.join(RAW, d, "load")
        os.makedirs(dst_dir, exist_ok=True)
        dst = os.path.join(dst_dir, name + ".png")
        Image.fromarray(out).save(dst)
        print("[compose] %s (окно %.3f, рамок %d)" % (dst, frac, len(frames)))
        if preview:
            Image.fromarray(out).resize((768, 768)).save(os.path.join(preview, name + "_preview.png"))
    print("[compose] ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
