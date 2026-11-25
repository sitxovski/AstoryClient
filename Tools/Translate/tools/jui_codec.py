# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
jui_codec.py — чтение/запись .JUI/.JUS (декомпил JUI_LoadDecryptFile@0x13150C0 + LZSS_Decompress@0x1314D40).

Формат: [байт0 пропускается] [8 Б заголовок: compSize,decompSize — каждый байт бит-реверс]
        [compData — каждый байт бит-реверс].
Ветвление загрузчика (ui_core.c:123961): `if (sizes[0] >= sizes[1]) memcpy(out, comp, sizes[1])`
— то есть при compSize >= decompSize данные берутся КАК ЕСТЬ, без LZSS. Поэтому запись
делаем в этом режиме (compSize = decompSize): легально по коду клиента, LZSS-компрессор не нужен.
"""
_REV = [int('{:08b}'.format(b)[::-1], 2) for b in range(256)]

def lzss_decompress(src, out_size):
    out = bytearray(); ring = bytearray(4096); ringpos = 1; bitpos = 32
    def readbits(n):
        nonlocal bitpos
        v = 0
        for _ in range(n):
            bi = bitpos >> 3; sh = 7 - (bitpos & 7)
            v = (v << 1) | ((src[bi] >> sh) & 1); bitpos += 1
        return v
    while len(out) < out_size:
        while readbits(1):
            lit = readbits(8)
            out.append(lit); ring[ringpos] = lit; ringpos = (ringpos + 1) & 0xFFF
            if len(out) >= out_size: return bytes(out[:out_size])
        off = readbits(12)
        if off == 0: break
        mlen = readbits(4) + 1
        for k in range(mlen + 1):
            b = ring[(k + off) & 0xFFF]
            out.append(b); ring[ringpos] = b; ringpos = (ringpos + 1) & 0xFFF
    return bytes(out[:out_size])

def decode(data):
    """.JUI (bytes) -> распакованный текст (bytes)."""
    hdr = bytes(_REV[b] for b in data[1:9])
    comp_size = int.from_bytes(hdr[0:4], 'little')
    decomp_size = int.from_bytes(hdr[4:8], 'little')
    comp = bytes(_REV[b] for b in data[9:9 + comp_size])
    if comp_size >= decomp_size:
        return comp[:decomp_size]
    return lzss_decompress(comp, decomp_size)

def encode(plain, first_byte=0):
    """текст (bytes) -> .JUI (bytes) в raw-режиме (compSize == decompSize)."""
    n = len(plain)
    hdr = n.to_bytes(4, 'little') + n.to_bytes(4, 'little')
    out = bytearray([first_byte])
    out += bytes(_REV[b] for b in hdr)
    out += bytes(_REV[b] for b in plain)
    return bytes(out)
