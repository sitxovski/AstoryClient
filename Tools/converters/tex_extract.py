#!/usr/bin/env python3
# Copyright (c) 2026 @sitxovski (Discord)
# tex_extract — конвертер текстур AsdaStory .TEX -> .dds/.png/.bmp.
# Формат (docs/formats/44_texture_tex.md, ПОДТВЕРЖДЁН): header 0x24 (9×u32: magic 0x20040603,
#   dataSize, width, height, f10, mipCount(f14), d3dformat/FOURCC, f1c, f20) + payload.
# Payload = СТАНДАРТНЫЙ контейнер: 'DDS ' (DXT1/2/3 + мипы) | PNG | BMP. Конвертация = срез header + запись payload.
import struct, os, sys, glob
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import asda_fmt_tex as _FT   # общий разбор .TEX (волна 2 аудита дублей, E9)

MAGIC = _FT.MAGIC

def parse_header(d):
    """Заголовок .TEX → dict или None. Разбор — общий `asda_fmt_tex.read_header` (mipCount = поле +0x14)."""
    h = _FT.read_header(d)
    if h is None: return None
    return dict(magic=h.magic, dataSize=h.size, width=h.width, height=h.height,
                f10=h.f10, mipCount=h.mips, fmt=h.fmt, f1c=h.f1c, f20=h.f20)

def payload_ext(p):
    return _FT.ext_of(p, unknown='.bin')   # неизвестный payload (диагностика)


def convert(src, outdir):
    d = open(src, 'rb').read()
    hdr = parse_header(d)
    if hdr is None: return (src, 'NOT_TEX')
    payload = d[0x24:]
    ext = payload_ext(payload)
    base = os.path.splitext(os.path.basename(src))[0]
    out = os.path.join(outdir, base + ext)
    os.makedirs(outdir, exist_ok=True)
    open(out, 'wb').write(payload)
    return (out, 'w=%d h=%d fmt=0x%X mip=%d %s' % (hdr['width'], hdr['height'], hdr['fmt'], hdr['mipCount'], ext))

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print('usage: tex_extract.py <out_dir> <file.TEX | dir | glob> ...'); sys.exit(1)
    outdir = sys.argv[1]
    files = []
    for a in sys.argv[2:]:
        if os.path.isdir(a): files += glob.glob(os.path.join(a, '**', '*.TEX'), recursive=True) + glob.glob(os.path.join(a, '**', '*.tex'), recursive=True)
        else: files += glob.glob(a)
    ok = bad = 0
    stats = {}
    for f in files:
        out, info = convert(f, outdir)
        if info == 'NOT_TEX': bad += 1
        else:
            ok += 1
            ext = info.split()[-1]; stats[ext] = stats.get(ext, 0) + 1
    print('converted=%d not_tex=%d  by_payload=%s' % (ok, bad, stats))
