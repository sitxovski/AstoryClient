# Copyright (c) 2026 @sitxovski (Discord)
# asda_extract_archive_textures.py — распаковка ТЕКСТУР из ZIP-архивов игры (.btz) → PNG.
# Текстуры персонажей/мобов лежат в архивах (Character/Texture/Texture.btz, Mob/Texture/Texture.btz),
# а не loose — поэтому базовый asda_extract_textures.py их не взял, и материалы моделей серые.
# .TEX = 0x24 header + DDS/PNG/BMP/TGA. DXT-DDS декодим Pillow → PNG (UE жрёт нативно).
# Пишем в raw-layout RawAssets/Textures/Data/<...>/Texture/, чтобы classify() в импорте направил в Characters/Mobs.
# Запуск: python asda_extract_archive_textures.py
import os, zipfile, struct, io, sys
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_fmt_tex as _TEX      # ★ дедуп №2 В1 Т5: общий патч fourcc DXT2/DXT4 и снятие премультипликации
import asda_alpha_bleed as _BLEED   # ★ 07.11.2026 (summer-hem): цвет под прозрачностью текстур прозрачных частей снаряжения
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
from PIL import Image

GAME = _P.DATA   # ОСНОВНОЙ клиент
OUT  = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Textures")
HDR  = 0x24

# (архив, целевая raw-подпапка под OUT — должна совпасть с путём игры для classify())
ARCHIVES = [
    (os.path.join(GAME, "Character", "Texture", "Texture.btz"), "Data/Character/Texture"),
    (os.path.join(GAME, "Mob", "Texture", "Texture.btz"),       "Data/Mob/Texture"),
    # ★ NPC: МОДЕЛЬНЫЕ атласы (a/b/c) — в NPC\Texture\Texture.btz. РАНЬШЕ ОШИБОЧНО брали NPC\Image\Texture.btz —
    #   а там UI-ПОРТРЕТЫ лиц для диалогов (один на NPC, 256×512 с альфой), из-за чего на моделях оказывался портрет.
    (os.path.join(GAME, "NPC", "Texture", "Texture.btz"),       "Data/NPC/Texture"),
    # UI-портреты NPC (для будущих диалогов) — ОТДЕЛЬНО, чтобы не сталкивались с модельными атласами по имени.
    (os.path.join(GAME, "NPC", "Image", "Texture.btz"),         "Data/NPC/Portrait"),
]

def detect(payload):
    if payload[:4] == b"DDS ":   return "dds"
    if payload[:4] == b"\x89PNG": return "png"
    if payload[:2] == b"BM":     return "bmp"
    if payload[:2] == b"\xff\xd8": return "jpg"
    if len(payload) > 3 and payload[1] in (0,1) and payload[2] in (1,2,3,9,10,11): return "tga"
    return None

def to_rgba(payload):
    # DXT2/DXT4 (premultiplied-alpha BC2/BC3) Pillow не тянет: патчим fourCC на DXT3/DXT5,
    # декодим и un-premultiply (RGB/=alpha) — прямая альфа для UE.
    # ★ дедуп №2 В1 Т5: патч fourcc — общий `asda_fmt_tex.fix_premult_fourcc`.
    # ★ ИСПРАВЛЕНИЕ дедуп №2 В2: деление на alpha — общий `asda_fmt_tex.unpremultiply_rgba` (целочисленный floor,
    #   rgb = min(255, rgb·255 // a)) вместо своей float32-формулы. Доказательство (11.10.2026, все 130 DXT2/DXT4 из
    #   4 архивов): оригинал (D3D9) показывает такую текстуру как есть — цвет rgb_p; UE смешивает прямой цвет как rgb·a/255.
    #   Ошибка |rgb·a/255 − rgb_p| у целочисленной формулы НИ В ОДНОМ канале не больше, чем у float32, а во всех 81 036
    #   различающихся каналах (46 568 пикселей, ±1) — меньше: при rgb_p = a float32 даёт 254.99998 → 254 вместо точного 255 (244 пары rgb_p/a из 65 280).
    #   Суммарная ошибка 539 682 → 523 240. Выходные PNG меняются на ±1 в этих пикселях.
    p, premult = _TEX.fix_premult_fourcc(payload)
    img = Image.open(io.BytesIO(p)).convert("RGBA")
    if not premult:
        return img
    return _TEX.unpremultiply_rgba(img)

def main():
    n_ok = n_skip = n_err = 0
    for arc, subdir in ARCHIVES:
        if not os.path.exists(arc):
            print("нет архива:", arc); continue
        outdir = os.path.join(OUT, *subdir.split("/"))
        os.makedirs(outdir, exist_ok=True)
        z = zipfile.ZipFile(arc)
        tex = [n for n in z.namelist() if n.lower().endswith(".tex")]
        print("%s: .TEX=%d → %s" % (os.path.basename(arc), len(tex), subdir))
        for n in tex:
            try:
                data = z.read(n)
                if len(data) <= HDR: n_skip += 1; continue
                payload = data[HDR:]; kind = detect(payload)
                base = os.path.splitext(os.path.basename(n))[0]
                dst = os.path.join(outdir, base + ".png")
                if kind in ("dds", "png", "bmp", "tga", "jpg"):
                    to_rgba(payload).save(dst, "PNG")
                    _BLEED.bleed_file(dst)   # ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: иначе распаковка вернёт белую кайму вырезанных вещей
                else:
                    n_skip += 1; continue
                n_ok += 1
            except Exception as e:
                n_err += 1
                if n_err <= 10: print("  ERR", n, str(e)[:50])
    print("=" * 60)
    print("Архив-текстуры → PNG: ок=%d, пропущено=%d, ошибок=%d → %s" % (n_ok, n_skip, n_err, OUT))

if __name__ == "__main__":
    main()
