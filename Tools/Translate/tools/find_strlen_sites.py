# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
find_strlen_sites.py — поиск ВСЕХ мест, где загрузчик таблицы берёт длину строки через strlen
перед циклом дешифровки «байт-1».

Ищем сигнатуру цикла дешифровки:
    movsx ecx, byte ptr [..]      0F BE 4C ..
    sub   ecx, 1                  83 E9 01
    mov   byte ptr [..], cl       88 4C ..
и поднимаемся вверх до ближайшего `push reg / call strlen / add esp,4 / mov [ebp-X], eax`.
"""
import struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

EXE = r"@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE\system\AsdaStory.exe"
STRLEN = 0x93BB78

def pe_map(data):
    off = struct.unpack_from('<I', data, 0x3C)[0]
    nsec = struct.unpack_from('<H', data, off + 6)[0]
    optsz = struct.unpack_from('<H', data, off + 20)[0]
    base = struct.unpack_from('<I', data, off + 24 + 28)[0]
    secs = []
    for i in range(nsec):
        o = off + 24 + optsz + i * 40
        name = data[o:o+8].rstrip(b'\0').decode('latin1')
        vsz, va, rsz, ptr = struct.unpack_from('<IIII', data, o + 8)
        secs.append((name, va, vsz, ptr, rsz))
    def va2off(va):
        r = va - base
        for _, sva, vsz, ptr, rsz in secs:
            if sva <= r < sva + max(vsz, rsz): return ptr + (r - sva)
    def off2va(o):
        for _, sva, vsz, ptr, rsz in secs:
            if ptr <= o < ptr + rsz: return base + sva + (o - ptr)
    return base, secs, va2off, off2va

def find_sites(data):
    base, secs, va2off, off2va = pe_map(data)
    text = [s for s in secs if s[0] == '.text'][0]
    _, sva, vsz, ptr, rsz = text
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    sites = []
    start = ptr
    end = ptr + rsz
    i = start
    while True:
        i = data.find(b"\x83\xE9\x01", i, end)          # sub ecx, 1
        if i < 0: break
        win = data[max(start, i - 12):i]
        if b"\x0f\xbe" not in win:                       # рядом должен быть movsx byte
            i += 1; continue
        if data[i+3:i+4] not in (b"\x8b", b"\x88", b"\x89"):
            pass
        # ищем назад ближайший call strlen (в пределах 0x60 байт)
        back_beg = max(start, i - 0x60)
        seg = data[back_beg:i]
        callsite = None
        for k in range(len(seg) - 5, -1, -1):
            if seg[k] == 0xE8:
                rel = struct.unpack_from('<i', seg, k + 1)[0]
                tgt = off2va(back_beg + k) + 5 + rel
                if tgt == STRLEN:
                    callsite = back_beg + k
                    break
        if callsite is not None:
            sites.append((callsite, i))
        i += 1
    return base, va2off, off2va, sites

def main():
    d = open(EXE, 'rb').read()
    base, va2off, off2va, sites = find_sites(d)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    print("найдено мест 'strlen + цикл дешифровки': %d\n" % len(sites))
    for callo, loopo in sites:
        cva = off2va(callo)
        # ожидаемая раскладка: push reg (1 б) | call (5 б) | add esp,4 (3 б) | mov [ebp-X],eax
        pre = d[callo-1:callo]
        post = d[callo+5:callo+8]
        ok = (0x50 <= pre[0] <= 0x57) and post == b"\x83\xC4\x04"
        ins = list(md.disasm(d[callo-0x30:callo+0x18], cva - 0x30))
        ctx = " | ".join("%s %s" % (i.mnemonic, i.op_str) for i in ins[-7:])
        print("call strlen @0x%08X (file 0x%X)  шаблон push/call/add=%s" % (cva, callo, "ДА" if ok else "НЕТ"))
        print("   контекст: %s" % ctx)
        print()

if __name__ == "__main__":
    main()
