# Copyright (c) 2026 @sitxovski (Discord)
# asda_extract_name_textures.py — текстуры НАЗВАНИЙ ЛОКАЦИЙ (name_*.TEX) из Data/NPC/Image/Texture.btz
#   → RawAssets/Textures/Data/NPC/Image/*.png (дальше импорт через asda_setup_zone_assets.py).
# Эталон: npcwin_DrawMapNpcImage@0xDA3330 / mapUI_DrawMapEnterBanner@0xDA2EF0 берут имя из
#   Map_SetNameTextureByID@0xDA3700 и грузят TextureCache_GetOrReload("Data/NPC/Image", name).
# .TEX = 24-байтный заголовок 0x20040603 + сырой PNG/TGA/DDS.
# Основной клиент (…_MINE) содержит не все имена — недостающие берём из других сборок клиента
#   (это ТЕ ЖЕ оригинальные ассеты, просто более полный набор). Никакой генерации своих картинок.
# Запуск: python asda_extract_name_textures.py
import os, sys, zipfile
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_fmt_tex as _FT   # общий разбор .TEX (волна 2 аудита дублей, E9)
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass

OUT = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Textures", "Data", "NPC", "Image")
HDR = _FT.HDR
# Порядок = приоритет: сначала основной клиент порта, затем прочие сборки (только недостающие имена).
ARCHIVES = [
    _P.NPC_IMAGE_BTZ,
    os.path.join(_P.ROOT, "Donors", "Fantazium", "data", "NPC", "Image", "Texture.btz"),
    os.path.join(_P.ROOT, "Donors", "ArabicASDAStory", "data", "NPC", "Image", "Texture.btz"),
]


def ext_of(p):
    """Расширение по сигнатуре нагрузки — общий `asda_fmt_tex.ext_of` (неизвестное = .tga)."""
    return _FT.ext_of(p)


def main():
    os.makedirs(OUT, exist_ok=True)
    seen = set()
    total = 0
    for arc in ARCHIVES:
        if not os.path.isfile(arc):
            print("нет архива:", arc); continue
        z = zipfile.ZipFile(arc)
        n_new = 0
        for name in z.namelist():
            base = os.path.basename(name)
            if not base.lower().startswith("name_") or not base.lower().endswith(".tex"):
                continue
            stem = os.path.splitext(base)[0].lower()
            if stem in seen:
                continue
            payload = _FT.payload(z.read(name))
            e = ext_of(payload)
            if e != ".png":
                print("  ! %s: неожиданный формат %s (пропуск)" % (stem, e)); continue
            with open(os.path.join(OUT, stem + ".png"), "wb") as fh:
                fh.write(payload)
            seen.add(stem); n_new += 1; total += 1
        print("%-70s новых: %d" % (os.path.basename(os.path.dirname(os.path.dirname(os.path.dirname(arc)))) + "/" + os.path.basename(arc), n_new))
    print("ИТОГО текстур названий: %d → %s" % (total, OUT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
