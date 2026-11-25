# Copyright (c) 2026 @sitxovski (Discord)
# asda_extract_effect_textures.py — извлечь текстуры эффектов из Data/Effect/Texture/Texture.btz (ZIP .tex).
# .tex = 36-байт заголовок (magic 0x20040603) + payload (DDS/PNG/BMP/JPG/TGA) → Pillow → PNG.
# РАЗДЕЛЕНИЕ (по указанию): текстуры, используемые эффектами КАРТ, кладём в папку каждой карты
#   export/maps/<map>/fxtex/<name>.png (self-contained, дальше импортируются в /Game/Map/<map>/Texture);
#   ОСТАЛЬНЫЕ (персонажные/скилловые эффекты) → export/effects/tex_misc/<name>.png (→ /Game/AsdaAssets/Textures/Effects).
# Использование текстур картами берём из export/maps/<map>/effects.json (поле emitter.tex).
import os, sys, io, zipfile, json, struct
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_fmt_tex as _FT   # общий декодер .TEX (волна 2 аудита дублей, E9)
from PIL import Image

BTZ = os.environ.get("ASDA_EFFECT_BTZ", _P.EFFECT_TEX_BTZ)
OUT = os.environ.get("ASDA_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps")
MISC = r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/effects/tex_misc"

def decode_tex(payload_raw):
    """Байты .tex → RGBA PIL.Image или None. Общий декод `asda_fmt_tex` с unpremultiply=True:
    у DXT2/DXT4 rgb делится на alpha (аудит §4: материал эффектов множит на alpha ещё раз) — как было.
    ★ Оригинал (D3D9) и C++ AsdaTexLoader premultiply НЕ снимают; режим оставлен ради неизменности PNG."""
    return _FT.to_pil(payload_raw, unpremultiply=True)


def stem(n): return os.path.splitext(os.path.basename(n))[0].lower()

def main():
    # 1) карта→множество имён текстур (basename, lower) из effects.json
    map_tex = {}          # mapname → set(stem)
    tex_to_maps = {}      # stem → set(mapname)
    for d in os.listdir(OUT):
        j = os.path.join(OUT, d, "effects.json")
        if not os.path.isfile(j): continue
        data = json.load(open(j, encoding="utf-8"))
        s = set()
        for jb in data.get("jeb", {}).values():
            for em in jb.get("emitters", []):
                t = em.get("tex", "")
                if t: s.add(stem(t))
        if s:
            map_tex[d] = s
            for t in s: tex_to_maps.setdefault(t, set()).add(d)
    print("карт с эффект-текстурами: %d; уникальных имён (карты): %d" % (len(map_tex), len(tex_to_maps)))

    z = zipfile.ZipFile(BTZ)
    os.makedirs(MISC, exist_ok=True)
    map_ok = misc_ok = err = 0
    for n in z.namelist():
        if not n.lower().endswith(".tex"): continue
        st = stem(n)
        im = decode_tex(z.read(n))
        if im is None: err += 1; continue
        maps = tex_to_maps.get(st)
        if maps:
            for mp in maps:                                   # в папку КАЖДОЙ использующей карты (self-contained)
                fxdir = os.path.join(OUT, mp, "fxtex"); os.makedirs(fxdir, exist_ok=True)
                im.save(os.path.join(fxdir, st + ".png"), "PNG")
            map_ok += 1
        else:
            im.save(os.path.join(MISC, st + ".png"), "PNG"); misc_ok += 1
    print("ИЗВЛЕЧЕНО: карт-эффект-текстур=%d (по папкам карт), прочих=%d (%s), ошибок=%d" % (map_ok, misc_ok, MISC, err))
    # какие карт-текстуры НЕ нашлись в btz (для контроля)
    have = set(stem(n) for n in z.namelist() if n.lower().endswith(".tex"))
    miss = sorted(set(tex_to_maps) - have)
    if miss: print("НЕ найдено в btz (%d): %s" % (len(miss), ", ".join(miss[:15])))

if __name__ == "__main__":
    main()
