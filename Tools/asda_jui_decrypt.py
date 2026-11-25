# Copyright (c) 2026 @sitxovski (Discord)
# Декодер бинарного .JUI → текст-INI (реверс JUI_LoadDecryptFile@0x13150C0 + LZSS_Decompress@0x1314D40).
# Формат: skip 1 байт; 8-байт заголовок (бит-реверс каждого байта)=compSize,decompSize; compData (бит-реверс
#   каждого байта); если comp>=decomp → raw, иначе LZSS (окно 4096, первые 4 байта=CRC32, флаг-бит 1=литерал(8),
#   0→offset(12,=0 конец)+len(4)+2 байта копии из кольца). Валидировано побайтово на Login.JUI==login_jui.ini.
import sys, os
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P

_REV = [int('{:08b}'.format(b)[::-1], 2) for b in range(256)]   # таблица бит-реверса байта

def lzss_decompress(src, out_size):
    out = bytearray()
    ring = bytearray(4096)
    ringpos = 1
    bitpos = 4 * 8                       # битстрим после 4-байтного CRC
    def readbits(n):
        nonlocal bitpos
        v = 0
        for _ in range(n):
            bi = bitpos >> 3; sh = 7 - (bitpos & 7)
            v = (v << 1) | ((src[bi] >> sh) & 1)
            bitpos += 1
        return v
    while len(out) < out_size:
        while readbits(1):               # флаг=1 → литерал
            lit = readbits(8)
            out.append(lit); ring[ringpos] = lit; ringpos = (ringpos + 1) & 0xFFF
            if len(out) >= out_size: return bytes(out[:out_size])
        off = readbits(12)               # смещение матча
        if off == 0: break               # конец
        mlen = readbits(4) + 1
        for k in range(mlen + 1):        # len+1 байт из кольца
            b = ring[(k + off) & 0xFFF]
            out.append(b); ring[ringpos] = b; ringpos = (ringpos + 1) & 0xFFF
    return bytes(out[:out_size])

def decrypt_jui(data):
    hdr = bytes(_REV[b] for b in data[1:9])          # skip byte0, 8-байт заголовок бит-реверс
    comp_size = int.from_bytes(hdr[0:4], 'little')
    decomp_size = int.from_bytes(hdr[4:8], 'little')
    comp = bytes(_REV[b] for b in data[9:9 + comp_size])
    if comp_size >= decomp_size:
        return comp[:decomp_size]
    return lzss_decompress(comp, decomp_size)

if __name__ == "__main__":
    if len(sys.argv) >= 2:               # decrypt <in.JUI> [out.ini]
        data = open(sys.argv[1], "rb").read()
        out = decrypt_jui(data)
        dst = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(sys.argv[1])[0] + ".ini"
        open(dst, "wb").write(out)
        print("OK %s -> %s (%d bytes)" % (os.path.basename(sys.argv[1]), dst, len(out)))
    else:
        # ВАЛИДАЦИЯ на Login.JUI vs login_jui.ini
        j = decrypt_jui(open(_os.path.join(_P.JUIDATA, "Login.JUI"), "rb").read())
        ref = open(r"@@ASTORY_DEV_ROOT_FWD@@/Data/Decoded/jui/login_jui.ini", "rb").read()
        print("декод Login.JUI: %d байт | эталон login_jui.ini: %d байт | СОВПАДАЕТ: %s" % (
            len(j), len(ref), j == ref))
        if j != ref:
            for i in range(min(len(j), len(ref))):
                if j[i] != ref[i]:
                    print("первое расхождение @%d: got %02x exp %02x ...%r vs %r" % (
                        i, j[i], ref[i], j[max(0,i-20):i+20], ref[max(0,i-20):i+20])); break
