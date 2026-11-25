# Copyright (c) 2026 @sitxovski (Discord)
# asda_extract_textures.py — РАСПАКОВКА текстур AsdaStory (.TEX) в нативные DDS/PNG/BMP.
# .TEX = 0x24(36)-байт заголовок + стандартный контейнер (DDS/PNG/BMP) с offset 0x24.
# Срезаем заголовок → файл, который UE импортирует нативно. Структура папок игры сохраняется.
# Запуск (обычный Python 3): python asda_extract_textures.py [GAME_ROOT] [OUT_ROOT]
import os, sys, struct
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P

GAME_ROOT = sys.argv[1] if len(sys.argv) > 1 else _P.CLIENT_ORIG
OUT_ROOT  = sys.argv[2] if len(sys.argv) > 2 else os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Textures")

HDR = 0x24  # размер заголовка .TEX

def detect_ext(payload):
    if payload[:4] == b"DDS ":          return ".dds"
    if payload[:4] == b"\x89PNG":       return ".png"
    if payload[:2] == b"BM":            return ".bmp"
    if payload[:2] == b"\xff\xd8":      return ".jpg"
    # TGA не имеет магии: byte1=colormaptype(0/1), byte2=imagetype(1,2,3,9,10,11)
    if len(payload) > 3 and payload[1] in (0, 1) and payload[2] in (1, 2, 3, 9, 10, 11):
        return ".tga"
    return None

def main():
    n_ok = n_skip = n_err = 0
    by_ext = {}
    for root, _dirs, files in os.walk(GAME_ROOT):
        for fn in files:
            if not fn.lower().endswith(".tex"):
                continue
            src = os.path.join(root, fn)
            try:
                with open(src, "rb") as f:
                    data = f.read()
                if len(data) <= HDR:
                    n_skip += 1; continue
                payload = data[HDR:]
                ext = detect_ext(payload)
                if ext is None:
                    n_skip += 1
                    if n_skip <= 10:
                        print("SKIP (неизв. контейнер):", os.path.relpath(src, GAME_ROOT), payload[:4])
                    continue
                rel = os.path.relpath(src, GAME_ROOT)
                base = os.path.splitext(rel)[0] + ext
                dst = os.path.join(OUT_ROOT, base)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, "wb") as f:
                    f.write(payload)
                n_ok += 1
                by_ext[ext] = by_ext.get(ext, 0) + 1
            except Exception as e:
                n_err += 1
                if n_err <= 10:
                    print("ERR", src, e)
    print("=" * 60)
    print("Распаковано:", n_ok, "| пропущено:", n_skip, "| ошибок:", n_err)
    print("По форматам:", by_ext)
    print("Выход:", OUT_ROOT)

if __name__ == "__main__":
    main()
