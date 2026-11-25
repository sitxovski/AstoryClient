# Copyright (c) 2026 @sitxovski (Discord)
# asda_extract_minimap_textures.py — КАРТИНКИ КАРТ для миникарты / большой карты.
#   Оригинал (world_map.c):
#     CMap_LoadTownTexture@0xB4AF40   → "./Map/<dir>/<dir>.TEX"           → CMap+496  (картинка МИНИКАРТЫ)
#     CMap_LoadBigMapTexture@0xB4B060 → "./Map/<dir>/<dir>_big.TEX"       → CMap+500  (БОЛЬШАЯ карта, поле)
#                                       "./Map/<dir>/<dir>_town_big.TEX"  → CMap+512  (БОЛЬШАЯ карта, ГОРОД)
#   Формат .TEX: 8 байт (magic 0x20040603 + размер) + 28 байт заголовка (w,h,…) + payload.
#   У карт payload = DDS **DXT2** (premultiplied DXT3). Pillow DXT2 не знает — патчим fourCC DXT2→DXT3
#   (раскладка блоков идентична; альфа во всех этих текстурах = 255, премультипликация ничего не меняет).
#   → RawAssets/Textures/MapImage/<basename>.png. Дальше импорт asda_import_minimap_textures.py.
import os, sys, io, struct
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_fmt_tex as _FT   # общий декодер .TEX (волна 2 аудита дублей, E9)
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
from PIL import Image

SRC = _P.MAP_DIR
OUT = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Textures", "MapImage")
HDR = 36


def decode_tex(path):
    """.TEX → RGBA PIL.Image или None. Общий `asda_fmt_tex`: fourcc DXT2→DXT3 / DXT4→DXT5, TGA-фолбэк."""
    return _FT.decode_file(path)


def main():
    os.makedirs(OUT, exist_ok=True)
    ok = err = 0
    for d in sorted(os.listdir(SRC)):
        sub = os.path.join(SRC, d)
        if not os.path.isdir(sub):
            continue
        for f in sorted(os.listdir(sub)):
            low = f.lower()
            if not low.endswith(".tex"):
                continue
            base = f[:-4]
            # интересуют ровно три: <dir>.TEX, <dir>_big.TEX, <dir>_town_big.TEX (регистр в клиенте пляшет)
            bl = base.lower()
            dl = d.lower()
            if bl not in (dl, dl + "_big", dl + "_town_big") and not (
                    bl.endswith("_big") or bl.endswith("_town_big")):
                continue
            if bl.endswith("_big") or bl.endswith("_town_big") or bl == dl:
                im = decode_tex(os.path.join(sub, f))
                if im is None:
                    print("  ОШИБКА декода:", d + "/" + f); err += 1; continue
                dst = os.path.join(OUT, base.replace(" ", "_") + ".png")
                im.save(dst)  # имя без пробелов — UE их не допускает в имени ассета
                print("  %-34s %s" % (d + "/" + f, "%dx%d" % im.size))
                ok += 1
    print("Готово: %d, ошибок: %d → %s" % (ok, err, OUT))


if __name__ == "__main__":
    main()
