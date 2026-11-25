# Copyright (c) 2026 @sitxovski (Discord)
# asda_dds_to_png.py — конвертация распакованных .dds (вкл. DXT1/3/5) в .png через Pillow.
# Причина: UE5.8 Interchange-DDS НЕ импортирует BC1/2/3, а отключение Interchange CVar'ом не
# сработало. PNG импортируется UE нативно (UE сам пережмёт в BC при импорте — look-2008 сохраняется).
# Запуск (обычный Python 3 с Pillow): python asda_dds_to_png.py [ROOT]
import os, sys
from PIL import Image

ROOT = sys.argv[1] if len(sys.argv) > 1 else r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Textures"

def main():
    n_ok = n_err = 0
    errs = []
    for root, _dirs, files in os.walk(ROOT):
        for fn in files:
            if not fn.lower().endswith(".dds"):
                continue
            src = os.path.join(root, fn)
            dst = os.path.splitext(src)[0] + ".png"
            try:
                with Image.open(src) as im:
                    im.convert("RGBA").save(dst, "PNG")
                os.remove(src)              # .dds больше не нужен (import берёт .png)
                n_ok += 1
            except Exception as e:
                n_err += 1
                if len(errs) < 15:
                    errs.append("%s: %s" % (os.path.relpath(src, ROOT), e))
    print("=" * 60)
    print("DDS->PNG: ок=%d, ошибок=%d" % (n_ok, n_err))
    for e in errs:
        print("ERR", e)

if __name__ == "__main__":
    main()
