# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
corpus.py — параллельный корпус «английский оригинал → русский перевод» по item_list.

ВАЖНО о чтении строк: способ зависит от того, пропатчен ли файл.
  * ОРИГИНАЛ клиента (tables_orig) — не пропатчен: заполнитель поля равен 0x00, поэтому длина
    строки определяется ПЕРВЫМ нулём В СЫРЫХ байтах, и только потом снимается шифр «-1».
    Если сначала дешифровать всё поле, нули превратятся в 0xFF, то есть в букву «я», и к
    каждой строке прилипнет хвост «яяяя…».
  * КЛИЕНТ после патча — заполнитель 0x01, дешифруется всё поле, строка режется по первому 0x00.
"""
import struct, zipfile

STRIDE, MD5, NAME_OFF, NAME_CAP = 264, 16, 4, 64

def _records(path, entry=None):
    d = zipfile.ZipFile(path).read(entry) if entry else open(path, "rb").read()
    cnt = struct.unpack_from("<i", d, 4)[0]
    for i in range(cnt):
        o = 20 + i * STRIDE
        if o + STRIDE > len(d): break
        yield d[o + MD5:o + STRIDE]

def read_name(body, patched):
    raw = bytes(body[NAME_OFF:NAME_OFF + NAME_CAP])
    if patched:
        plain = bytes(((c - 1) & 0xFF) for c in raw)
        z = plain.find(b"\x00")
        return plain[:z if z >= 0 else NAME_CAP]
    z = raw.find(b"\x00")                      # длина по сырым байтам
    return bytes(((c - 1) & 0xFF) for c in (raw[:z] if z >= 0 else raw))

def load(path, entry=None, patched=True):
    out = {}
    for body in _records(path, entry):
        code = (struct.unpack_from("<I", body, 0)[0] - 35) & 0xFFFFFFFF
        out[code] = read_name(body, patched).strip()
    return out

ORIG = r"@@ASTORY_DEV_ROOT@@\Donors\TRANSLATE_unpacked\tables_orig\Data\Character\item_list.bin"
CLIENT_BBZ = r"@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE\Data\Character\Bin.bbz"

def load_pair():
    en = load(ORIG, patched=False)
    ru = load(CLIENT_BBZ, "item_list.bin", patched=True)
    return en, ru
