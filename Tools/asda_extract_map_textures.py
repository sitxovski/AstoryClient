# Copyright (c) 2026 @sitxovski (Discord)
# asda_extract_map_textures.py — извлечь ВСЕ текстуры карты (.TEX) в PNG для нормального импорта в UE.
# .TEX = 36-байт заголовок (magic 0x20040603) + payload (DDS DXT1/3/5 | PNG | BMP | JPG | TGA).
# Берём <gamemap>/Texture/*.TEX (объекты/вода) и <gamemap>/*.TEX (террейн-тайлы/небо), декодим payload
# через Pillow → <out>/<map>/textures/<basename>.png. Имя = basename исходного .TEX (для поиска материалом).
# Запуск: python asda_extract_map_textures.py <gameMapDir> <outRoot> <mapName>
import os, sys, io, struct
from PIL import Image
import asda_fmt_tex as _FT   # общий декодер .TEX (волна 2 аудита дублей, E9)

def decode_tex(path):
    """.TEX → RGBA PIL.Image или None. Декод — общий `asda_fmt_tex` (волна 2, E9).
    ★ Прежняя копия не подменяла fourcc DXT2 → Pillow падал и текстура молча терялась (59 файлов в зоне
      обхода: миникарты и Texture/SILHOUTTE_TEX_WHITE.TEX). Теперь DXT2 извлекается; premultiply НЕ
      снимается (как оригинал и C++ AsdaTexLoader). Остальные файлы — побайтно как раньше."""
    return _FT.decode_file(path)


def main():
    if len(sys.argv) < 4:
        print("usage: asda_extract_map_textures.py <gameMapDir> <outRoot> <mapName>"); return
    map_dir, out_root, name = sys.argv[1], sys.argv[2], sys.argv[3]
    out_dir = os.path.join(out_root, name, "textures")
    os.makedirs(out_dir, exist_ok=True)
    # источники: подпапка Texture/ (объекты/вода) + корень карты (террейн-тайлы/небо). НЕ массивы
    # TerrainColor/Alpha.TEX (их режет asda_extract_terrain_tiles) — пропускаем по имени.
    skip = {"terraincolor.tex", "terrainalpha.tex"}
    ok = err = 0
    for base in (os.path.join(map_dir, "Texture"), map_dir):
        if not os.path.isdir(base):
            continue
        for fn in os.listdir(base):
            if not fn.lower().endswith(".tex") or fn.lower() in skip:
                continue
            src = os.path.join(base, fn)
            if not os.path.isfile(src):
                continue
            stem = os.path.splitext(fn)[0]
            dst = os.path.join(out_dir, stem + ".png")
            if os.path.exists(dst):
                continue  # из Texture/ имеет приоритет (объектные); не перетираем корневыми
            im = decode_tex(src)
            if im is None:
                err += 1; continue
            im.save(dst, "PNG"); ok += 1
    print("OK %s: текстур извлечено=%d, ошибок=%d -> %s" % (name, ok, err, out_dir))

if __name__ == "__main__":
    main()
