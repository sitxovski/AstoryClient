# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
find_tooltip_loaders.py — ищет загрузчики тултип-таблиц и точку, где берётся длина строки
перед циклом дешифровки «байт-1».

Ход: строка имени файла ('Item_tooltip.BIN' и т.п.) -> ссылка на неё в коде -> вперёд по функции
ищем цикл дешифровки строки (movsx/mov байт + sub 1 + запись байта) и ближайший перед ним
источник длины (call strlen 0x93BB78 либо иная конструкция).
"""
import struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

EXE = r"@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE\system\AsdaStory.exe"
STRLEN = 0x93BB78
NAMES = [b"Item_tooltip.BIN", b"sowell_tooltip.BIN", b"SpecialItem_TooltipList.BIN",
         b"SpecialItem_BufTooltipList.BIN", b"stateInfotip.BIN", b"SoulstateInfotip.BIN"]

def pe_map(data):
    off = struct.unpack_from('<I', data, 0x3C)[0]
    nsec = struct.unpack_from('<H', data, off + 6)[0]
    optsz = struct.unpack_from('<H', data, off + 20)[0]
    base = struct.unpack_from('<I', data, off + 24 + 28)[0]
    secs = []
    for i in range(nsec):
        o = off + 24 + optsz + i * 40
        nm = data[o:o+8].rstrip(b'\0').decode('latin1')
        vsz, va, rsz, ptr = struct.unpack_from('<IIII', data, o + 8)
        secs.append((nm, va, vsz, ptr, rsz))
    def va2off(va):
        r = va - base
        for _, sva, vsz, ptr, rsz in secs:
            if sva <= r < sva + max(vsz, rsz): return ptr + (r - sva)
    def off2va(o):
        for _, sva, vsz, ptr, rsz in secs:
            if ptr <= o < ptr + rsz: return base + sva + (o - ptr)
    return base, secs, va2off, off2va

def main():
    d = open(EXE, 'rb').read()
    base, secs, va2off, off2va = pe_map(d)
    text = [s for s in secs if s[0] == '.text'][0]
    tbeg, tend = text[3], text[3] + text[4]
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    for nm in NAMES:
        i = d.find(nm)
        if i < 0:
            print("%-32s строка не найдена" % nm.decode()); continue
        sva = off2va(i)
        refs = []
        pat = struct.pack('<I', sva)
        j = tbeg
        while True:
            j = d.find(pat, j, tend)
            if j < 0: break
            refs.append(j); j += 1
        print("\n%-32s VA 0x%X, ссылок в .text: %d" % (nm.decode(), sva, len(refs)))
        for r in refs:
            rva = off2va(r)
            # дизасм вперёд от ссылки: ищем цикл дешифровки и источник длины
            beg = r - 8
            ins = list(md.disasm(d[beg:beg + 0x1400], off2va(beg)))
            loop_at = None
            for k, x in enumerate(ins):
                if x.mnemonic == 'sub' and x.op_str.endswith(', 1') and x.op_str.split(',')[0] in ('eax','ecx','edx','ebx'):
                    # проверяем, что рядом идёт запись байта
                    nxt = " ".join(y.mnemonic + y.op_str for y in ins[k+1:k+4])
                    prv = " ".join(y.mnemonic + y.op_str for y in ins[max(0,k-3):k])
                    if 'byte' in nxt or 'byte' in prv:
                        loop_at = k; break
            if loop_at is None:
                print("   ref 0x%08X: цикл дешифровки не найден в окне" % rva); continue
            # ищем назад источник длины
            src = None
            for k in range(loop_at, max(0, loop_at - 40), -1):
                x = ins[k]
                if x.mnemonic == 'call':
                    try: tgt = int(x.op_str, 16)
                    except ValueError: continue
                    if tgt == STRLEN:
                        src = ("strlen", x.address); break
            print("   ref 0x%08X -> цикл @0x%X, длина: %s" % (
                rva, ins[loop_at].address, ("call strlen @0x%X" % src[1]) if src else "НЕ strlen (иная схема)"))
            lo = max(0, loop_at - 16)
            for x in ins[lo:loop_at + 4]:
                print("        0x%08X %-7s %s" % (x.address, x.mnemonic, x.op_str))

if __name__ == "__main__":
    main()
