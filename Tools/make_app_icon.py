# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# make_app_icon.py — многоразмерная иконка игры из @@ASTORY_DEV_ROOT@@\Data\OwnerArt\icon.png (решение владельца 16.10.2026, agent-retail-branding).
#
# Что делает:
#   • icon.png (RGBA, квадрат) → AstoryClient/Build/Windows/Application.ico: размеры 16, 20, 24, 32, 40, 48, 64, 96, 128, 256;
#     256 — PNG внутри ICO, остальные — 32-битный BMP (DIB BGRA + маска AND), даунскейл Pillow LANCZOS по
#     предумноженной альфе (RGBa) — без тёмных ореолов по краю.
#   • UBT встраивает именно этот файл в exe игры: UEBuildWindows.cs GetWindowsApplicationIcon → <проект>/Build/Windows/
#     Application.ico; VCToolChain.cs — BUILD_ICON_FILE_NAME для Default.rc2 и пререквизит компиляции ресурса (смена файла =
#     перекомпиляция .rc и перелинковка). Настройка проекта «Game Icon» редактора пишет в тот же файл — ini не нужен.
#   • Пересоздаёт ico, только если png новее ico, ico нет, или --force; байты пишет, только если они изменились (лишняя
#     перелинковка не нужна). Вывод детерминирован (без дат внутри).
#   • Самопроверка после записи: разбор ICO — все 10 размеров, 256 = PNG, остальные = BMP 32 бит.
#   • ★ 17.10.2026 Белый фон арта вырезается (cut_background) — одна точка для игры и лаунчера; ico без прозрачности
#     (прежний вид) пересоздаётся при ensure_icon() даже без --force.
# Запуск: python Tools/make_app_icon.py [--force] [--check]   (вызывается из Tools/eac_package.py — ensure_icon())
# При импорте ничего не делает (закон tools-no-import-side-effects).
import argparse
import io
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))                         # …\AsdaReborn\Tools
PROJECT_DIR = os.path.dirname(HERE)                                        # …\AsdaReborn
SRC_PNG = os.path.join(os.path.dirname(PROJECT_DIR), "Data", "OwnerArt", "icon.png")           # @@ASTORY_DEV_ROOT@@\Data\OwnerArt\icon.png — исходник владельца
APP_ICO = os.path.join(PROJECT_DIR, "Build", "Windows", "Application.ico")  # куда смотрит UBT
SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)                         # размеры внутри ico
PNG_SIZES = (256,)                                                         # эти — PNG-сжатые, прочие — BMP


def _resample(img, size):
    """Даунскейл LANCZOS по предумноженной альфе (RGBa) → RGBA size×size."""
    from PIL import Image
    return img.convert("RGBa").resize((size, size), Image.Resampling.LANCZOS).convert("RGBA")


def _bmp_entry(im):
    """Картинка RGBA → данные ICO-элемента BMP: BITMAPINFOHEADER (высота×2) + BGRA снизу вверх + маска AND (1 бит, по 32)."""
    w, h = im.size
    hdr = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, w * h * 4, 0, 0, 0, 0)
    px = im.tobytes("raw", "BGRA")
    rows = [px[y * w * 4:(y + 1) * w * 4] for y in range(h)]
    xor = b"".join(reversed(rows))
    stride = ((w + 31) // 32) * 4
    alpha = im.getchannel("A").tobytes()
    mask = bytearray()
    for y in reversed(range(h)):
        row = bytearray(stride)
        for x in range(w):
            if alpha[y * w + x] == 0:
                row[x // 8] |= 0x80 >> (x % 8)                         # полностью прозрачный пиксель — бит маски
        mask += row
    return hdr + xor + bytes(mask)


def _png_entry(im):
    """Картинка RGBA → PNG (без метаданных времени)."""
    b = io.BytesIO()
    im.save(b, format="PNG", optimize=True)
    return b.getvalue()


# ★ 17.10.2026 (владелец: «белый фон на иконке — аккуратно вырезать», лаунчер и игра): арт icon.png — круглый значок с золотым
#   ободком на почти белом квадрате БЕЗ альфы. Вырезка — здесь, одна точка для игры (Application.ico, BootstrapperIcon EAC) и
#   лаунчера (AstoryClient/Launcher/tools/make_launcher_icon.py): иначе ensure_icon() при сборке вернул бы белый фон.
BG_TOL = 40.0      # отличие от цвета фона (евклид RGB), ниже которого пиксель считается фоном для заливки от рамки
BG_BAND = 2        # полуширина полосы кромки, px исходника
RIM_B = 50.0       # синий канал золотого ободка (замер по арту: 41…56)


def cut_background(img):
    """RGBA без прозрачности → RGBA с вырезанным фоном; картинка, где альфа уже есть, возвращается как есть.
    → (картинка, сводка-словарь). Фон = область «похоже на цвет рамки», СВЯЗАННАЯ с краями (блики внутри значка не трогаются);
    значок не идеальный круг (МНК ±3,4 px) — режем по реальному контуру. Кромка ±BG_BAND px: альфа по смешению с фоном по синему
    каналу, цвет разбавлен обратно (F = (P − (1−a)·фон) / a) — светлой каймы нет. Снаружи — (0,0,0,0)."""
    import numpy as np
    from PIL import Image
    from scipy import ndimage

    rgba0 = img.convert("RGBA")
    if rgba0.getchannel("A").getextrema()[0] < 255:
        return rgba0, {"cut": False, "why": "в исходнике уже есть прозрачность"}
    rgb = np.asarray(rgba0.convert("RGB")).astype(np.float32)
    h, w, _ = rgb.shape
    border = np.concatenate([rgb[0], rgb[-1], rgb[:, 0], rgb[:, -1]])           # пиксели рамки
    bg = np.median(border, axis=0)                                               # цвет фона (≈252,252,251)
    near_bg = np.sqrt(((rgb - bg) ** 2).sum(axis=2)) < BG_TOL                    # «похоже на фон»
    lab, _ = ndimage.label(near_bg)                                              # связные области «фона»
    edge = np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))
    inside = ndimage.binary_fill_holes(~np.isin(lab, edge[edge > 0]))            # значок = всё, что не фон от рамки
    band = (ndimage.binary_dilation(inside, iterations=BG_BAND) &
            ndimage.binary_dilation(~inside, iterations=BG_BAND))                # полоса кромки с обеих сторон границы
    alpha = inside.astype(np.float32)
    a_band = np.clip((bg[2] - rgb[..., 2]) / max(1.0, bg[2] - RIM_B), 0.0, 1.0)
    alpha[band] = a_band[band]
    out = rgb.copy()
    m = band & (alpha > 0.02)
    a3 = alpha[m][:, None]
    out[m] = np.clip((rgb[m] - (1.0 - a3) * bg) / a3, 0, 255)
    arr = np.dstack([out, alpha * 255.0]).round().astype(np.uint8)
    arr[alpha <= 0.02] = 0
    info = {"cut": True, "size": (w, h), "bg": tuple(int(v) for v in bg), "inside_px": int(inside.sum()),
            "band_px": int(band.sum()), "corner_alpha": int(arr[0, 0, 3]), "center_alpha": int(arr[h // 2, w // 2, 3])}
    return Image.fromarray(arr, "RGBA"), info


def build_ico_bytes(src_png=SRC_PNG, cut=True):
    """icon.png → байты ICO со всеми SIZES (cut — вырезать белый фон, см. cut_background)."""
    from PIL import Image
    img = Image.open(src_png)
    img.load()
    if img.mode != "RGBA":
        img = img.convert("RGBA")
    if img.size[0] != img.size[1]:
        raise SystemExit("ОТКАЗ: %s не квадратная (%dx%d)" % (src_png, img.size[0], img.size[1]))
    if cut:
        img, _ = cut_background(img)
    datas = []
    for s in SIZES:
        im = _resample(img, s)
        datas.append(_png_entry(im) if s in PNG_SIZES else _bmp_entry(im))
    out = io.BytesIO()
    out.write(struct.pack("<HHH", 0, 1, len(SIZES)))
    off = 6 + 16 * len(SIZES)
    for s, d in zip(SIZES, datas):
        out.write(struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(d), off))
        off += len(d)
    for d in datas:
        out.write(d)
    return out.getvalue()


def parse_ico(data):
    """ICO → [(размер, 'PNG'|'BMP32', байты элемента)] для самопроверки и сверки с ресурсами exe."""
    zero, typ, n = struct.unpack_from("<HHH", data, 0)
    if zero != 0 or typ != 1:
        raise ValueError("не ICO")
    out = []
    for i in range(n):
        w, h, _, _, _, bpp, size, off = struct.unpack_from("<BBBBHHII", data, 6 + 16 * i)
        blob = data[off:off + size]
        kind = "PNG" if blob[:8] == b"\x89PNG\r\n\x1a\n" else ("BMP%d" % struct.unpack_from("<H", blob, 14)[0])
        out.append((w or 256, kind, blob))
    return out


def check_ico(path=APP_ICO):
    """Самопроверка ico: все SIZES, 256 — PNG, прочие — BMP32. → (ok, описание)."""
    if not os.path.isfile(path):
        return False, "нет %s" % path
    items = parse_ico(open(path, "rb").read())
    got = [(s, k) for s, k, _ in items]
    want = [(s, "PNG" if s in PNG_SIZES else "BMP32") for s in SIZES]
    if got != want:
        return False, "состав %s ≠ ожидаемому %s" % (got, want)
    return True, "%d размеров (%s), %d б" % (len(items), ", ".join("%d%s" % (s, "png" if k == "PNG" else "") for s, k in got),
                                           os.path.getsize(path))


def _ico_opaque(path):
    """Элемент 256 (PNG) в ico целиком непрозрачен — значит, ico собран до вырезки фона (17.10.2026)."""
    from PIL import Image
    try:
        for s, kind, blob in parse_ico(open(path, "rb").read()):
            if s == 256 and kind == "PNG":
                return Image.open(io.BytesIO(blob)).convert("RGBA").getchannel("A").getextrema()[0] == 255
    except (OSError, ValueError):
        return True
    return False


def ensure_icon(force=False, log=print):
    """Пересоздать Application.ico, если png новее (или нет ico / force). → путь ico. Отказ — SystemExit."""
    if not os.path.isfile(SRC_PNG):
        # Исходника нет (публичный клон: Data\OwnerArt не входит в репо) — годится закоммиченный Application.ico,
        # если он проходит самопроверку; иначе отказ с подсказкой.
        ok, why = check_ico()
        if ok:
            log("иконка: исходника %s нет — используется %s из репозитория (%s)" % (SRC_PNG, APP_ICO, why))
            return APP_ICO
        raise SystemExit("ОТКАЗ: нет исходника иконки %s, а %s не годится (%s) — положите квадратный RGBA icon.png "
                         "в <ROOT>\\Data\\OwnerArt\\ или верните Build\\Windows\\Application.ico из репозитория"
                         % (SRC_PNG, APP_ICO, why))
    need = force or not os.path.isfile(APP_ICO) or os.path.getmtime(SRC_PNG) > os.path.getmtime(APP_ICO)
    need = need or _ico_opaque(APP_ICO)                                # ★ 17.10.2026: ico прежнего вида (белый фон) — пересоздать
    if need:
        data = build_ico_bytes()
        old = open(APP_ICO, "rb").read() if os.path.isfile(APP_ICO) else None
        if old != data:
            os.makedirs(os.path.dirname(APP_ICO), exist_ok=True)
            with open(APP_ICO, "wb") as f:
                f.write(data)
            log("иконка: %s пересоздана из %s" % (APP_ICO, SRC_PNG))
        else:
            os.utime(APP_ICO, None)                                    # содержимое то же — только отметка «новее png»
            log("иконка: %s — содержимое не изменилось" % APP_ICO)
    ok, why = check_ico()
    if not ok:
        raise SystemExit("ОТКАЗ: иконка %s: %s" % (APP_ICO, why))
    log("иконка: %s — %s" % (APP_ICO, why))
    return APP_ICO


def main(argv=None):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser(description="Application.ico из Data/OwnerArt/icon.png")
    ap.add_argument("--force", action="store_true", help="пересоздать, даже если ico новее png")
    ap.add_argument("--check", action="store_true", help="только проверить готовый ico")
    a = ap.parse_args(argv)
    if a.check:
        ok, why = check_ico()
        print(("OK: " if ok else "ОШИБКА: ") + why)
        return 0 if ok else 1
    ensure_icon(force=a.force)
    return 0


if __name__ == "__main__":
    sys.exit(main())
