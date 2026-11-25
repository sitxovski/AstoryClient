# Copyright (c) 2026 @sitxovski (Discord)
# asda_nb_gen_maps.py — ШАГ 2 превью «нормаль из альбедо»: из альбедо *_A.png сделать *_N.png и *_R.png.
#
# ★ ГЛАВНОЕ МЕСТО — ВЫСОКОЧАСТОТНЫЙ ФИЛЬТР. Наивный приём «яркость = высота» на текстурах 2008 года
#   даёт ложный рельеф: художник рисовал тени прямо в альбедо, и тёмный угол дома превращается
#   в глубокую яму. Поэтому высота = яркость МИНУС её же сильно размытая копия: остаются только
#   мелкие детали (швы кладки, доски, кольчуга), а нарисованная светотень уходит.
#
# Карта нормалей: Sobel по высоте → N = normalize(-dHdx*S, -dHdy*S, 1), сверху подмешан
#   тайлящийся микрорельеф (fbm-шум) — он и даёт «выпуклость» вблизи, где у альбедо просто мыло.
# Шероховатость: тёмное → матовое, светлое → глаже (эвристика, а не данные оригинала: карт
#   шероховатости в игре 2008 года нет физически). Порог правится константами ниже.
#
# Зависимости: numpy + Pillow. Обычный python, БЕЗ unreal. Запуск: python asda_nb_gen_maps.py
import os
import numpy as np
from PIL import Image

SRC_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools/_out/nb_preview"
KEYS = ["stone", "build", "tree", "armor", "mob"]

NORMAL_STRENGTH = 5.0    # сила рельефа из альбедо
DETAIL_STRENGTH = 1.15   # сила микрорельефа (шум)
DETAIL_TILES    = 5.0    # во сколько раз шум мельче текстуры
HIPASS_FRAC     = 0.10   # радиус размытия для high-pass, доля от стороны текстуры
DESAT           = 0.85   # ★ насколько сильно насыщенные (светящиеся) места опускаются вниз, 1 = min(R,G,B)
EMISSIVE_SINK   = 1.30   # ★ насколько глубоко утапливается светящийся шов (лава, кристаллы, лампы)
ROUGH_HI        = 0.94   # шероховатость самых тёмных мест (матовое)
ROUGH_LO        = 0.28   # шероховатость самых светлых мест (с бликом)
FLIP_G          = True   # ★★★ ИНВЕРСИЯ ЗЕЛЁНОГО. Строки изображения растут ВНИЗ, а ось Y тангентного
                         #   пространства — ВВЕРХ, поэтому знак dH/dy надо перевернуть. Без этого при
                         #   свете сверху рельеф читается вывернутым: светлое кажется вдавленным,
                         #   тёмное — выпуклым (замечено на готовом листе).


def _blur1d(a, r, axis):
    """Box-blur по одной оси через кумулятивную сумму — O(n), без scipy."""
    n = a.shape[axis]
    pad = [(0, 0)] * a.ndim
    pad[axis] = (r, r)
    p = np.pad(a, pad, mode="edge")
    c = np.cumsum(p, axis=axis)
    zs = list(c.shape)
    zs[axis] = 1
    c = np.concatenate([np.zeros(zs, c.dtype), c], axis=axis)
    lo = np.take(c, np.arange(0, n), axis=axis)
    hi = np.take(c, np.arange(2 * r + 1, 2 * r + 1 + n), axis=axis)
    return (hi - lo) / float(2 * r + 1)


def box_blur(a, r):
    """Разделимое box-размытие; r<=0 — без изменений."""
    if r <= 0:
        return a
    return _blur1d(_blur1d(a, r, 0), r, 1)


def fbm(h, w, tiles, octaves=4, seed=1337):
    """Тайлящийся fbm-шум [0..1]: несколько решёток случайных чисел, растянутых билинейно."""
    rng = np.random.default_rng(seed)
    acc = np.zeros((h, w), np.float32)
    amp, total = 1.0, 0.0
    base = max(int(round(tiles)), 2)
    for o in range(octaves):
        n = base * (2 ** o)
        g = rng.random((n, n)).astype(np.float32)
        g = np.tile(g, (2, 2))[: n + 1, : n + 1]          # шов: край повторяет начало → тайлится
        layer = np.asarray(Image.fromarray(g, "F").resize((w, h), Image.BILINEAR), np.float32)
        acc += layer * amp
        total += amp
        amp *= 0.5
    return acc / total


def sobel(a):
    """Градиенты Собеля (dx, dy) с краевым продолжением."""
    p = np.pad(a, 1, mode="edge")
    kx = np.array([[-1, 0, 1], [-2, 0, 2], [-1, 0, 1]], np.float32) / 8.0
    ky = kx.T
    gx = np.zeros_like(a)
    gy = np.zeros_like(a)
    for j in range(3):
        for i in range(3):
            win = p[j:j + a.shape[0], i:i + a.shape[1]]
            gx += win * kx[j, i]
            gy += win * ky[j, i]
    return gx, gy


def normal_from_height(hgt, strength):
    """Высота → tangent-space нормаль (значения [-1..1], z вверх)."""
    gx, gy = sobel(hgt)
    nx = -gx * strength
    ny = -gy * strength
    nz = np.ones_like(nx)
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    return nx / ln, ny / ln, nz / ln


def build_one(key):
    src = os.path.join(SRC_DIR, "%s_A.png" % key)
    im = Image.open(src).convert("RGB")
    a = np.asarray(im, np.float32) / 255.0
    h, w = a.shape[:2]
    lum = 0.299 * a[..., 0] + 0.587 * a[..., 1] + 0.114 * a[..., 2]

    # ★★★ ЯРКОСТЬ ≠ ВЫСОТА ТАМ, ГДЕ ТЕКСТУРА СВЕТИТСЯ. У голема потоки лавы — самые яркие пиксели,
    #   но физически они лежат В ТРЕЩИНАХ, ниже окружающих камней. По «светлое = высокое» они
    #   вылезали выпирающими венами (замечено на готовом листе).
    #   Отделяем свечение от освещённости по НАСЫЩЕННОСТИ: свет и тень модулируют яркость, почти не
    #   трогая оттенок, а эмиссия — это узкие пятна высокой насыщенности (оранжевые, жёлтые).
    #   Берём за высоту `V·(1 − S·DESAT)`: при DESAT=1 это ровно min(R,G,B) — насыщенный оранжевый
    #   шов даёт малое значение и становится КАНАВКОЙ, а серый камень сохраняет свою яркость.
    mx = a.max(2)
    mn = a.min(2)
    sat = (mx - mn) / np.maximum(mx, 1e-4)
    base = mx * (1.0 - sat * DESAT)

    # ★ Одного подавления насыщенности МАЛО (проверено числами на големе: лава осталась выше камня,
    #   +0.111 против −0.004). Причина — ЯДРА швов почти белые (255,230,180), их насыщенность низкая,
    #   и они проходят фильтр как «светлый камень». Поэтому строим явную МАСКУ СВЕЧЕНИЯ по
    #   абсолютной цветности (mx−mn) и яркости, а затем РАЗМЫВАЕМ её: размытие затягивает белое ядро,
    #   окружённое насыщенным оранжевым ореолом.
    chroma = mx - mn
    emis = (np.clip((chroma - 0.14) / 0.45, 0.0, 1.0) *
            np.clip((mx - 0.30) / 0.40, 0.0, 1.0))
    emis = np.clip(box_blur(emis, max(int(round(min(h, w) * 0.02)), 1)) * 1.8, 0.0, 1.0)

    # ── высота = мелкие детали альбедо (нарисованная светотень отфильтрована) ──
    r = max(int(round(min(h, w) * HIPASS_FRAC)), 2)
    hp = base - box_blur(base, r)
    s = float(np.std(hp)) or 1e-4
    hp = np.clip(hp / (4.0 * s), -1.0, 1.0)              # нормировка по разбросу, а не по min/max
    # ★★★ Светящееся — ВСЕГДА канавка: раскалённый шов лежит между камнями, а не поверх них.
    hp = np.clip(hp - EMISSIVE_SINK * emis, -1.0, 1.0)

    nx, ny, nz = normal_from_height(hp, NORMAL_STRENGTH)

    # ── микрорельеф: тот же приём, но по шуму, и подмешивается по UDN ──
    d = fbm(h, w, DETAIL_TILES * max(h, w) / 256.0)
    d = d - box_blur(d, max(int(round(min(h, w) * 0.05)), 2))
    ds = float(np.std(d)) or 1e-4
    dx, dy, _dz = normal_from_height(np.clip(d / (4.0 * ds), -1, 1), DETAIL_STRENGTH)
    nx, ny = nx + dx, ny + dy
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    nx, ny, nz = nx / ln, ny / ln, nz / ln

    if FLIP_G:
        ny = -ny
    nrm = np.stack([nx, ny, nz], -1) * 0.5 + 0.5
    Image.fromarray((np.clip(nrm, 0, 1) * 255).astype(np.uint8), "RGB").save(
        os.path.join(SRC_DIR, "%s_N.png" % key))

    # ── шероховатость: светлое глаже, плюс микро-вариация от рельефа ──
    # ★ от той же базы без свечения: иначе лава получала самую низкую шероховатость и блестела как лак
    lo, hi = np.percentile(base, 5), np.percentile(base, 95)
    t = np.clip((base - lo) / max(hi - lo, 1e-4), 0.0, 1.0)
    rough = ROUGH_HI + (ROUGH_LO - ROUGH_HI) * t + 0.06 * hp
    # ★ светящееся не бывает глянцевым — раскалённой поверхности блик не нужен
    rough = np.clip(rough + 0.35 * emis, 0.20, 1.0)
    Image.fromarray((rough * 255).astype(np.uint8), "L").save(
        os.path.join(SRC_DIR, "%s_R.png" % key))
    # ★ маска свечения уже посчитана — сохраняем её как карту Emissive (лава должна светить сама)
    Image.fromarray((emis * 255).astype(np.uint8), "L").save(
        os.path.join(SRC_DIR, "%s_E.png" % key))
    return key, (w, h), float(np.std(hp)), float(rough.mean())


def main():
    print("=== генерация карт нормали/шероховатости ===")
    for k in KEYS:
        key, size, hs, rm = build_one(k)
        print("  %-6s %dx%d  разброс рельефа=%.3f  средняя шероховатость=%.2f" % (key, size[0], size[1], hs, rm))
    print("готово ->", SRC_DIR)


if __name__ == "__main__":
    main()
