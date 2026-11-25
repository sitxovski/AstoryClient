# Copyright (c) 2026 @sitxovski (Discord)
# asda_extract_terrain_tiles.py — извлечь per-patch splat-плитки террейна в PNG (системный Python).
# TerrainColor.TEX / TerrainAlpha.TEX = конкатенация вложенных .TEX-плиток (36Б header + DDS DXT3).
# Офсеты плиток — в TerrainColor.SEQ / TerrainAlpha.SEQ (u32 на патч). Режем по офсетам, декодим DDS
# через Pillow, сохраняем <out>/<map>/terrain/{color,alpha}_<offset>.png (имя по офсету = авто-дедуп).
# Запуск: python asda_extract_terrain_tiles.py <mapDir> <outRoot> <mapName>
import os, sys, struct, io
from PIL import Image

def read_u32(path):
    if not os.path.exists(path): return []
    b = open(path, "rb").read()
    return list(struct.unpack("<%dI" % (len(b)//4), b[:len(b)//4*4]))

def extract(tex_path, seq_path, out_dir, prefix):
    if not (os.path.exists(tex_path) and os.path.exists(seq_path)):
        print("  нет %s / %s" % (os.path.basename(tex_path), os.path.basename(seq_path))); return 0
    data = open(tex_path, "rb").read()
    offs = read_u32(seq_path)
    uniq = sorted(set(o for o in offs if 0 <= o < len(data)))
    bounds = uniq + [len(data)]
    n = 0
    for i, off in enumerate(uniq):
        end = bounds[i+1]
        tile = data[off:end]
        if len(tile) <= 36: continue
        dds = tile[36:]                      # снять 36Б .TEX-заголовок -> сырой DDS
        if dds[:4] != b"DDS ": continue
        try:
            with Image.open(io.BytesIO(dds)) as im:
                im.convert("RGBA").save(os.path.join(out_dir, "%s_%d.png" % (prefix, off)), "PNG")
            n += 1
        except Exception as e:
            print("  ERR off=%d: %s" % (off, e))
    return n

def main():
    if len(sys.argv) < 4:
        print("usage: asda_extract_terrain_tiles.py <mapDir> <outRoot> <mapName>"); return
    map_dir, out_root, name = sys.argv[1], sys.argv[2], sys.argv[3]
    out_dir = os.path.join(out_root, name, "terrain")
    os.makedirs(out_dir, exist_ok=True)
    c = extract(os.path.join(map_dir, "TerrainColor.TEX"), os.path.join(map_dir, "TerrainColor.SEQ"), out_dir, "color")
    a = extract(os.path.join(map_dir, "TerrainAlpha.TEX"), os.path.join(map_dir, "TerrainAlpha.SEQ"), out_dir, "alpha")
    print("OK %s: color-плиток=%d, alpha-плиток=%d -> %s" % (name, c, a, out_dir))

if __name__ == "__main__":
    main()
