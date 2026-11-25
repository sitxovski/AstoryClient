# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_fmt_tex.py — ЕДИНЫЙ декодер клиентского контейнера текстур `.TEX` (AsdaStory) для инструментов.

Волна 2 аудита дублей (`Reports/dedup_audit/00_PLAN.md` E9, отчёт `wave2_formats.md`). Раньше та же
логика «магия → полезная нагрузка с +0x24 → Pillow» жила в 8 файлах, и одна копия
(`asda_extract_map_textures`) не умела DXT2 — Pillow 12.2 на нём падает
(`NotImplementedError: Unimplemented pixel format 844388420`), текстура молча терялась.

ФОРМАТ (сверено на данных клиента 14.09.2026: 1915 DDS-файлов, DDS-заголовок против полей .TEX):
    +0x00 u32 magic 0x20040603
    +0x04 u32 size       — размер полезной нагрузки (файл = size + 8)
    +0x08 u32 width      +0x0C u32 height
    +0x10 u32 f10        — ★ ВСЕГДА 1 на данных; это НЕ число мипов
    +0x14 u32 mips       — MipLevels (== dwMipMapCount DDS у 1915 из 1915; эталон font_text.c:3869
                           передаёт imageHdr[3] в D3DXCreateTextureFromFileInMemoryEx)
    +0x18 u32 fmt        — D3DFORMAT / FOURCC (21 = A8R8G8B8, 0x32545844 = 'DXT2', …)
    +0x1C u32 f1c        +0x20 u32 f20
    +0x24 …   полезная нагрузка: стандартный контейнер DDS | PNG | BMP | JPG | TGA (TGA без сигнатуры)

★★★ PREMULTIPLY (DXT2/DXT4). Блоки DXT2 раскладкой идентичны DXT3 (DXT4 — DXT5), отличие только в
смысле цвета: rgb уже умножен на alpha. Оригинал (D3D9) отдаёт такую текстуру видеокарте как есть,
без деления на alpha; C++ порта (`AsdaTexLoader.cpp:89-91`) делает так же — DXT2 → PF_DXT3 без
un-premultiply. Поэтому умолчание `unpremultiply=False` = 1:1 с оригиналом и C++. Единственный
потребитель с `unpremultiply=True` — `asda_extract_effect_textures.py` (его материал эффектов умножает
на alpha ещё раз; режим оставлен, чтобы его выходные PNG не изменились).

API:
    read_header(b)      -> TexHeader | None
    payload(b)          -> bytes (срез с +0x24, без проверок)
    payload_kind(p)     -> 'DDS' | 'PNG' | 'BMP' | 'JPG' | unknown
    ext_of(p)           -> '.dds' | '.png' | '.bmp' | unknown   (JPG считается unknown — как в прежних копиях)
    dds_fourcc(p)       -> bytes | None
    fix_premult_fourcc(p) -> (bytes, premult: bool)   DXT2→DXT3, DXT4→DXT5
    unpremultiply_rgba(im) -> PIL.Image   rgb = min(255, rgb*255 // a) при 0 < a < 255 (целочисленно, как прежняя копия)
    to_pil(b, …)        -> PIL.Image | None
    decode_file(path, …)

★ Модуль БЕЗ побочных эффектов при импорте (закон tools-no-import-side-effects): только определения;
  Pillow и numpy импортируются внутри функций.
"""
import io
import struct
from collections import namedtuple

MAGIC = 0x20040603          # сигнатура контейнера .TEX
HDR = 0x24                  # длина заголовка .TEX; полезная нагрузка начинается здесь
MIN_SIZE = HDR + 8          # минимальный файл, который прежние экстракторы вообще пытались декодировать (44)

# Поля 36-байтного заголовка (9 × u32). Имена f10/f1c/f20 — смысл не подтверждён.
TexHeader = namedtuple("TexHeader", "magic size width height f10 mips fmt f1c f20")

# Премультиплицированные fourcc и их «прямые» пары с той же раскладкой блоков.
PREMULT_FOURCC = {b"DXT2": b"DXT3", b"DXT4": b"DXT5"}

DDS_FOURCC_AT = 84          # смещение fourcc внутри DDS-заголовка (0x54)


def is_tex(b):
    """True, если буфер начинается с заголовка .TEX (длина ≥ 0x24 и верная магия)."""
    return b is not None and len(b) >= HDR and struct.unpack_from("<I", b, 0)[0] == MAGIC


def read_header(b):
    """Разобрать 36-байтный заголовок. None — не .TEX или буфер короче заголовка."""
    if not is_tex(b):
        return None
    return TexHeader(*struct.unpack_from("<9I", b, 0))


def payload(b):
    """Полезная нагрузка (всё после заголовка). Без проверок — вызывающий сам решает про магию."""
    return b[HDR:]


def payload_kind(p, unknown="TGA"):
    """Тип контейнера нагрузки по сигнатуре. TGA сигнатуры не имеет, поэтому он — `unknown`."""
    if p[:4] == b"DDS ":
        return "DDS"
    if p[:4] == b"\x89PNG":
        return "PNG"
    if p[:2] == b"BM":
        return "BMP"
    if p[:3] == b"\xff\xd8\xff":
        return "JPG"
    return unknown


def ext_of(p, unknown=".tga"):
    """Расширение файла для сырой нагрузки. JPG отдельно НЕ распознаётся (как у прежних копий
    `asda_extract_name_textures.ext_of` / `converters/tex_extract.payload_ext`) и уходит в `unknown`."""
    if p[:4] == b"\x89PNG":
        return ".png"
    if p[:4] == b"DDS ":
        return ".dds"
    if p[:2] == b"BM":
        return ".bmp"
    return unknown


def dds_fourcc(p):
    """fourcc DDS-нагрузки (4 байта) или None, если это не DDS или заголовок обрезан."""
    if p[:4] != b"DDS " or len(p) < DDS_FOURCC_AT + 4:
        return None
    return bytes(p[DDS_FOURCC_AT:DDS_FOURCC_AT + 4])


def fix_premult_fourcc(p):
    """DXT2→DXT3 / DXT4→DXT5 в DDS-заголовке (Pillow премультиплицированные не знает).
    Возвращает (нагрузка, была_премультипликация). Блоки не трогаются."""
    fcc = dds_fourcc(p)
    new = PREMULT_FOURCC.get(fcc) if fcc is not None else None
    if new is None:
        return p, False
    p = bytes(p)
    return p[:DDS_FOURCC_AT] + new + p[DDS_FOURCC_AT + 4:], True


def unpremultiply_rgba(im):
    """Снять премультипликацию: rgb = min(255, rgb·255 // a) при 0 < a < 255; a = 0 и a = 255 не трогаются.
    Целочисленная формула побайтно совпадает с прежним попиксельным циклом `asda_extract_effect_textures`."""
    import numpy as np
    from PIL import Image
    if im.mode != "RGBA":
        im = im.convert("RGBA")
    arr = np.array(im, dtype=np.uint32)             # H×W×4, запас разрядности под ×255
    a = arr[..., 3]
    m = (a > 0) & (a < 255)
    if m.any():
        am = a[m]
        for c in range(3):
            ch = arr[..., c]
            ch[m] = np.minimum(255, ch[m] * 255 // am)
    return Image.fromarray(arr.astype(np.uint8), "RGBA")


def to_pil(b, *, unpremultiply=False, patch_premult=True, mode="RGBA", check_magic=True,
           min_size=MIN_SIZE, tga_fallback=True, raise_errors=False):
    """Файл .TEX (байты целиком, с заголовком) → PIL.Image или None.

    unpremultiply — делить rgb на alpha у DXT2/DXT4 (умолчание False = как оригинал и C++, см. шапку).
    patch_premult — подменять fourcc DXT2/DXT4 для Pillow (False = прежнее поведение map-экстрактора,
                    на DXT2 возвращает None; оставлено только для сверки).
    mode          — конвертировать в этот режим (None — отдать как открыл Pillow, как `asda_e5_char_alpha`).
    check_magic   — False: не проверять магию (UI-экстракторы режут +0x24 без проверки).
    min_size      — файл короче — None (44 у экстракторов карт/эффектов/миникарт, 0x24 у UI).
    tga_fallback  — вторая попытка явным форматом TGA.
    raise_errors  — вместо None пробросить последнее исключение Pillow (для экстракторов, печатающих причину).
    """
    from PIL import Image
    if b is None or len(b) < min_size:
        if raise_errors:
            raise ValueError("буфер .TEX короче %d байт" % min_size)
        return None
    if check_magic and struct.unpack_from("<I", b, 0)[0] != MAGIC:
        if raise_errors:
            raise ValueError("неверная магия .TEX 0x%08X" % struct.unpack_from("<I", b, 0)[0])
        return None
    p = bytes(b[HDR:])
    premult = False
    if patch_premult:
        p, premult = fix_premult_fourcc(p)
    err = None
    im = None
    try:
        im = Image.open(io.BytesIO(p))
        im.load()
        if mode:
            im = im.convert(mode)
    except Exception as e:          # Pillow бросает разные типы; причину отдаём при raise_errors
        im = None
        err = e
    if im is not None:
        # ★ Ревизия 14.09: снятие premultiply ВНЕ try — сбой деления (например, нет numpy) должен всплыть, а не
        #   молча увести DXT2-текстуру эффекта в TGA-фолбэк и вернуть None.
        if premult and unpremultiply:
            im = unpremultiply_rgba(im)
        return im
    if tga_fallback:
        try:
            im = Image.open(io.BytesIO(p), formats=["TGA"])
            im.load()
            return im.convert(mode) if mode else im
        except Exception as e:
            err = e
    if raise_errors:
        raise err
    return None


def decode_file(path, **kw):
    """Прочитать .TEX с диска и декодировать (`to_pil` с теми же параметрами)."""
    with open(path, "rb") as f:
        return to_pil(f.read(), **kw)
