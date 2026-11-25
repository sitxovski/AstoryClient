# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_restore_bmp_alpha.py — вернуть АЛЬФУ, потерянную апскейлом, в апскейленные PNG.
#
# ★ ЗАЧЕМ. Часть текстур игры лежит в 32-битном BMP (BGRA) с настоящей маской прозрачности:
#   фон записан белым цветом с альфой 0. Pillow открывает такой BMP как «RGB» и молча
#   отбрасывает четвёртый байт — поэтому конвейер апскейла (`Tools/NormalStudio/for upscaling`)
#   пометил эти файлы `alpha_used = нет`, канал не попал ни в `alpha/`, ни в результат, и
#   апскейленный PNG получился БЕЗ альфы. В игре это видно как непрозрачный белый
#   прямоугольник вокруг значка (заявка владельца по стрелкам трекера заданий, 2026-08-23).
#
# ★ ЧТО ДЕЛАЕМ. Цвет берём из АПСКЕЙЛА (он лучше), альфу — из ОРИГИНАЛЬНОГО .bmp,
#   увеличенную БЛИЖАЙШИМ СОСЕДОМ. Ближайшим, а не билинейно: маска двоичная (0/255),
#   интерполяция размыла бы край отсечения — то же правило записано в `КАК_АПСКЕЙЛИТЬ.md`
#   про очередь `alpha/`. Ничего не дорисовывается: только перенос существующего канала.
#
# ★ ДВА ДЕРЕВА. Результат пишется в ОБА (закон [[upscale-two-trees]]): `RawAssets/Textures/**`
#   (из него импортирует `asda_import_textures.py`) и `Tools/NormalStudio/for upscaling/upscaled/src/**`
#   (на него ссылается `AssetImportData` уже импортированных ассетов). Иначе повторный импорт
#   вернул бы дефект.
#
# Запуск (без UE):
#   python asda_restore_bmp_alpha.py --subtree map            # только текстуры карт (по умолчанию)
#   python asda_restore_bmp_alpha.py --subtree .              # всё дерево
#   python asda_restore_bmp_alpha.py --subtree map --dry-run  # только отчёт, файлы не трогать
import argparse
import os
import struct
import sys

_PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))          # проект (папка над Tools)
RAW_ROOT = os.path.join(_PROJ, "RawAssets", "Textures")
UPS_ROOT = os.path.join(_PROJ, "Tools", "NormalStudio", "for upscaling", "upscaled", "src")   # необязательная папка, не в git


def log(msg):
    print("[альфа-восстановление] %s" % msg)


def read_bmp_bgra(path):
    """Сырой 32-битный BMP → (w, h, [альфа-байты в порядке слева-направо, сверху-вниз]).
    Своего разбора цвета не делаем — из BMP нужен ТОЛЬКО четвёртый байт."""
    d = open(path, "rb").read()
    if d[:2] != b"BM":
        return None
    off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0]
    if bpp != 32:
        return None
    hh = abs(h)
    if off + w * hh * 4 > len(d):
        return None
    rows = []
    for y in range(hh):
        # положительная высота = строки снизу вверх
        src = (hh - 1 - y) if h > 0 else y
        base = off + src * w * 4
        rows.append(bytes(d[base + 4 * x + 3] for x in range(w)))
    return w, hh, b"".join(rows)


def collect(subtree):
    """Пары «оригинальный .bmp с альфой» + «апскейленный .png без альфы»."""
    from PIL import Image
    root = os.path.normpath(os.path.join(RAW_ROOT, subtree))
    out = []
    for dirpath, _dirs, files in os.walk(root):
        for fn in files:
            if not fn.lower().endswith(".bmp"):
                continue
            bmp = os.path.join(dirpath, fn)
            png = os.path.splitext(bmp)[0] + ".png"
            if not os.path.exists(png):
                continue
            got = read_bmp_bgra(bmp)
            if not got:
                continue
            w, h, a = got
            if set(a) == {255}:
                continue                       # альфы по существу нет — терять было нечего
            if Image.open(png).mode == "RGBA":
                continue                       # уже с альфой — чинить нечего
            out.append((bmp, png, w, h, a))
    return out


def restore(bmp, png, w, h, alpha, dry):
    from PIL import Image
    rgb = Image.open(png).convert("RGB")
    W, H = rgb.size
    if W % w or H % h or (W // w) != (H // h):
        log("!! %s: апскейл %dx%d не кратен оригиналу %dx%d — пропуск" % (png, W, H, w, h))
        return None
    k = W // w
    mask = Image.frombytes("L", (w, h), alpha).resize((W, H), Image.NEAREST)
    rgba = rgb.copy()
    rgba.putalpha(mask)
    opaque = sum(1 for v in alpha if v == 255)
    log("%s: %dx%d x%d, непрозрачных пикселей оригинала %d из %d (%.1f%%)"
        % (os.path.basename(png), w, h, k, opaque, w * h, 100.0 * opaque / (w * h)))
    if dry:
        return None
    rel = os.path.relpath(png, RAW_ROOT)
    targets = [png, os.path.join(UPS_ROOT, rel)]
    written = []
    for t in targets:
        if not os.path.isdir(os.path.dirname(t)):
            log("   пропуск (нет каталога): %s" % t)
            continue
        rgba.save(t)
        written.append(t)
    return written


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--subtree", default="map", help="подкаталог RawAssets/Textures (по умолчанию map)")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args(argv)

    pairs = collect(a.subtree)
    log("подкаталог «%s»: пар «BMP с альфой + PNG без альфы» найдено %d" % (a.subtree, len(pairs)))
    done = 0
    for bmp, png, w, h, alpha in pairs:
        if restore(bmp, png, w, h, alpha, a.dry_run):
            done += 1
    log("переписано файлов: %d (в каждом дереве)" % done)
    if not a.dry_run and done:
        log("дальше: переимпортировать эти текстуры (asda_import_textures.py или точечно)")
    return 0


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" and globals().get("__spec__") is None:
    sys.exit(main())
