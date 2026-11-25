# Copyright (c) 2026 @sitxovski (Discord)
# extract_jui_textures.py — извлечение UI-атласов логина из juidata/Image/Texture.btz.
# .tex = 8 байт (magic 0x20040603 + size) + 28 байт info + СТАНДАРТНЫЙ TGA с офсета 0x24
# (реверс CTextureFont_LoadAtlas @0x115D920, отчёт login_helper_ui_reverse.md).
# Выход: RawAssets/Textures/JUI/<имя>.png (полные атласы; UV-кроп делает Slate-брашами).
import os, zipfile, io, struct, sys
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_fmt_tex as _FT   # общий декодер .TEX (волна 2 аудита дублей, E9)
import asda_login_guard as _LG   # ★ 16.09.2026 (раздел 17): PNG-исходники JUI-атласов окон логина — только с разрешения владельца
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
from PIL import Image

ARC = _P.JUI_IMAGE_BTZ
OUT = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Textures", "JUI")
NEED = [
    "n_01.tex", "n_02.tex", "n_03.tex", "n_chapter.tex",
    "ne_skillbar3.tex", "ne_wheeltaget2.tex", "ne_booster_center100.tex",
    "ne_godrobot01_praise.tex", "ne_godrobot02_warning.tex", "ne_godrobot03_sation.tex",
]

def crop_uv(img, u0, v0, u1, v1):
    W, H = img.size
    return img.crop((round(u0*W), round(v0*H), round(u1*W), round(v1*H)))

def compose_9slice(atlas, pieces, tw, th):
    # pieces: [(rect(x0,y0,x1,y1 в шаблоне), uv(u0,v0,u1,v1), modulate ARGB)] — сшивка плоскостей ResizeWindow
    # в единый шаблон для UE Box-браша (движок рисует 9 плоскостей; сшивка байт-точна по данным JUS).
    out = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    for rect, uv, mod in pieces:
        p = crop_uv(atlas, *uv).resize((rect[2]-rect[0], rect[3]-rect[1]), Image.NEAREST)
        if mod != 0xFFFFFFFF:
            a = (mod >> 24) & 0xFF; r = (mod >> 16) & 0xFF; g = (mod >> 8) & 0xFF; b = mod & 0xFF
            px = p.load()
            for yy in range(p.height):
                for xx in range(p.width):
                    pr, pg, pb, pa = px[xx, yy]
                    px[xx, yy] = (pr*r//255, pg*g//255, pb*b//255, pa*a//255)
        out.paste(p, (rect[0], rect[1]))
    return out

_LOGIN_SKIPPED = []   # ★ 16.09.2026 (раздел 17): имена PNG, пропущенных защитой логина (итог одной строкой в конце main)


def _login_png_blocked(stem):
    """★ 16.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 17). True — `RawAssets/Textures/JUI/<stem>.png` — исходник пакета сцены
    логина `/Game/AsdaAssets/Textures/JUI/<stem>` (Tools/data/asda_login_packages.json), а ASDA_ALLOW_LOGIN_WRITE != 1: файл не
    перезаписывать (переимпорт взял бы новый). Имя копится для итоговой строки."""
    if _LG.login_package_blocked("/Game/AsdaAssets/Textures/JUI/" + stem, "extract_jui_textures.py", quiet=True):
        _LOGIN_SKIPPED.append(stem)
        return True
    return False

def main():
    os.makedirs(OUT, exist_ok=True)
    z = zipfile.ZipFile(ARC)
    names = {os.path.basename(n).lower(): n for n in z.namelist()}
    atl = {}
    for want in NEED:
        entry = names.get(want)
        if not entry:
            print("НЕТ В АРХИВЕ:", want); continue
        raw = z.read(entry)
        assert _FT.is_tex(raw), hex(struct.unpack_from("<I", raw, 0)[0])
        # стандартный TGA (в т.ч. RLE type 10) с +0x24; декод — общий asda_fmt_tex (волна 2)
        img = _FT.to_pil(raw, min_size=_FT.HDR, tga_fallback=False, raise_errors=True)
        atl[os.path.splitext(want)[0]] = img
        if _login_png_blocked(os.path.splitext(want)[0]):   # ★ 16.09.2026 (раздел 17): атлас нужен шаблонам ниже, файл не пишем
            continue
        out = os.path.join(OUT, os.path.splitext(want)[0] + ".png")
        img.save(out)
        print("OK %-28s %sx%s -> %s" % (want, img.width, img.height, out))

    # ---- 9-slice шаблоны (значения = JuiSkin.ini, отчёт login_helper_ui_reverse.md §8) ----
    # Log_YellowWin: шаблон 46×46 (углы 21, кромка 4), атлас n_02
    yw = compose_9slice(atl["n_02"], [
        ((0,0,21,21),  (0.826,0.188,0.867,0.229), 0xFFFFFFFF),  # LT
        ((21,0,25,21), (0.813,0.297,0.820,0.338), 0xFFFFFFFF),  # T
        ((25,0,46,21), (0.828,0.297,0.869,0.338), 0xFFFFFFFF),  # RT
        ((0,21,21,25), (0.826,0.238,0.867,0.244), 0xFFFFFFFF),  # L
        ((21,21,25,25),(0.813,0.346,0.820,0.350), 0xFFFFFFFF),  # C
        ((25,21,46,25),(0.828,0.346,0.869,0.350), 0xFFFFFFFF),  # R
        ((0,25,21,46), (0.826,0.252,0.867,0.293), 0xFFFFFFFF),  # LB
        ((21,25,25,46),(0.813,0.361,0.820,0.402), 0xFFFFFFFF),  # B
        ((25,25,46,46),(0.828,0.361,0.869,0.402), 0xFFFFFFFF),  # RB
    ], 46, 46)
    if not _login_png_blocked("tpl_yellowwin"):   # ★ 16.09.2026 (раздел 17): шаблон окон логина — только с разрешения
        yw.save(os.path.join(OUT, "tpl_yellowwin.png")); print("OK tpl_yellowwin 46x46")

    # Log_GiantGod_TalkWin: колонки 16/8/8, ряды 8/8/8 → шаблон 32×24, атлас ne_wheeltaget2; центр модулируется FF000000
    tw = compose_9slice(atl["ne_wheeltaget2"], [
        ((0,0,16,8),   (0.828,0.504,0.891,0.535), 0xFFFFFFFF),  # LT
        ((16,0,24,8),  (0.930,0.551,0.992,0.613), 0xFFFFFFFF),  # T
        ((24,0,32,8),  (0.969,0.504,1.000,0.535), 0xFFFFFFFF),  # RT
        ((0,8,16,16),  (0.703,0.695,0.770,0.715), 0xFFFFFFFF),  # L
        ((16,8,24,16), (0.930,0.551,0.992,0.613), 0xFF000000),  # C (чёрный)
        ((24,8,32,16), (0.969,0.504,1.000,0.535), 0xFFFFFFFF),  # R
        ((0,16,16,24), (0.703,0.695,0.770,0.715), 0xFFFFFFFF),  # LB
        ((16,16,24,24),(0.969,0.504,1.000,0.535), 0xFFFFFFFF),  # B
        ((24,16,32,24),(0.969,0.504,1.000,0.535), 0xFFFFFFFF),  # RB
    ], 32, 24)
    # убрать альфа-градиент диагональных сегментов (в игре рамка сплошная, без градиента к прозрачному)
    twp = tw.load()
    for _y in range(tw.height):
        for _x in range(tw.width):
            _r,_g,_b,_a = twp[_x,_y]
            twp[_x,_y] = (_r,_g,_b, 255 if _a >= 128 else 0)
    if not _login_png_blocked("tpl_talkwin"):   # ★ 16.09.2026 (раздел 17): шаблон окна логина — только с разрешения
        tw.save(os.path.join(OUT, "tpl_talkwin.png")); print("OK tpl_talkwin 32x24 (без градиента)")

    # ---- loginhelpertext.bin: байт-точная копия из Data\character\Bin.bbz (для рантайм-чтения порта) ----
    bbz = zipfile.ZipFile(_P.CHAR_BIN_BBZ)
    lht = {os.path.basename(n).lower(): n for n in bbz.namelist()}.get("loginhelpertext.bin")
    if lht:
        data = bbz.read(lht)
        outb = os.path.join(OUT, "..", "..", "GameData")
        os.makedirs(outb, exist_ok=True)
        p = os.path.abspath(os.path.join(outb, "loginhelpertext.bin"))
        open(p, "wb").write(data)
        print("OK loginhelpertext.bin %d байт -> %s" % (len(data), p))
    else:
        print("НЕТ loginhelpertext.bin в Bin.bbz!")
    if _LOGIN_SKIPPED:   # ★ 16.09.2026 (раздел 17): итог защиты логина одной строкой
        print("[LOGIN-GUARD] extract_jui_textures.py: PNG атласов окон логина не записано %d (%s) — только с %s=1"
              % (len(_LOGIN_SKIPPED), ", ".join(_LOGIN_SKIPPED), _LG.ALLOW_ENV))

if __name__ == "__main__":
    main()
