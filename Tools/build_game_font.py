# Copyright (c) 2026 @sitxovski (Discord)
# Собирает НАСТОЯЩИЙ шрифт игры (битмап FONT0.BIN + DATA0.TEX) в пиксель-TTF для UE.
# Источник (эталон): LoadEngineFont@0x12C3090 — этот клиент = язык 25 (кириллица cp1251 + латиница),
#   FONT0.BIN = 223 записи{float u0,v0,u1,v1; int32 page,advW,glyphH,0}, все глифы на DATA0.TEX (256x256, 1-бит).
# Маппинг кодпоинт->запись (подтв. агентами):
#   ASCII 0x20-0x7E -> rec 128+(cp-0x20);  high cp1251 0x80-0xFF -> rec (cp-0x80) = кириллица.
# Каждый вкл-пиксель -> прямоугольный контур (пиксель-точный рендер в UE при 14px, POINT-подобно).
import struct, os, sys
from PIL import Image
import io
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

FONT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Donors/client_system/juidata/font"
OUT_TTF  = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/RawAssets/Fonts/AsdaUIFont.ttf"
CELL = 14                 # высота глифа (px) — language 25
UPM_PX = 128              # font-units на 1 пиксель (целые контуры)
UPM = CELL * UPM_PX       # unitsPerEm = 1792 -> при size=14 px 1 пиксель = 1 px
ASCENT = UPM              # базовая линия = низ ячейки

def load_page(n):
    d = open(os.path.join(FONT_DIR, "DATA%d.TEX" % n), "rb").read()
    return Image.open(io.BytesIO(d[d.find(b"DDS "):])).convert("L")

def parse_font0():
    f = open(os.path.join(FONT_DIR, "FONT0.BIN"), "rb").read()
    return [struct.unpack_from("<4f4i", f, i * 32) for i in range(len(f) // 32)]

EPS = 8   # верт-перекрытие рядов (0.06px — убирает шов-стык, не тянет высоту; non-zero заливка сплошная)
def glyph_from_bitmap(px, x0, y0, x1, y1, advW):
    """Глиф = пер-строчные run-length прямоугольники вкл-пикселей, с ВЕРТИКАЛЬНЫМ ПЕРЕКРЫТИЕМ рядов
    на EPS. Иначе соседние ряды делят ровно одно ребро → антиалиасинг UE рисует хайрлайн-полосы.
    Перекрытие → нет совпадающих рёбер, non-zero заливка сплошная. y-флип. 1 пиксель = UPM_PX ед."""
    pen = TTGlyphPen(None)
    W = x1 - x0; H = y1 - y0
    for row in range(H):
        y = y0 + row
        run = None
        for col in range(W + 1):
            onp = col < W and 0 <= x0 + col < 256 and 0 <= y < 256 and px[x0 + col, y] > 127
            if onp and run is None:
                run = col
            elif not onp and run is not None:
                px0 = run * UPM_PX; px1 = col * UPM_PX
                fy1 = (CELL - row) * UPM_PX + EPS          # верх ряда (+перекрытие)
                fy0 = (CELL - row - 1) * UPM_PX - EPS      # низ ряда (−перекрытие)
                pen.moveTo((px0, fy0)); pen.lineTo((px0, fy1)); pen.lineTo((px1, fy1)); pen.lineTo((px1, fy0)); pen.closePath()
                run = None
    return pen.glyph()

def main():
    os.makedirs(os.path.dirname(OUT_TTF), exist_ok=True)
    page0 = load_page(0); px = page0.load()
    recs = parse_font0()
    # кодпоинт -> запись
    cp2rec = {}
    for r in range(128):                              # high half = cp1251 0x80..0xFF
        b = r + 0x80
        try:
            uni = bytes([b]).decode("cp1251")
        except Exception:
            continue
        if uni and uni != "�":
            cp2rec[ord(uni)] = r
    for r in range(128, len(recs)):                   # low half = ASCII 0x20..
        cp = 0x20 + (r - 128)
        cp2rec[cp] = r

    glyph_order = [".notdef"]
    glyphs = {".notdef": TTGlyphPen(None).glyph()}
    advances = {".notdef": CELL * UPM_PX}
    cmap = {}
    used = set()
    for cp, r in sorted(cp2rec.items()):
        U0, V0, U1, V1, page, advW, glyphH, _ = recs[r]
        x0, y0 = round(U0 * 256), round(V0 * 256)
        x1, y1 = round(U1 * 256), round(V1 * 256)
        if x1 <= x0: x1 = x0 + max(advW, 1)
        if y1 <= y0: y1 = y0 + CELL
        name = "g%04X" % cp
        if name in glyphs: continue
        glyphs[name] = glyph_from_bitmap(px, x0, y0, x1, y1, advW)
        advances[name] = max(advW, 1) * UPM_PX
        glyph_order.append(name)
        cmap[cp] = name
    # пробел гарантированно есть
    if 0x20 not in cmap:
        glyphs["g0020"] = TTGlyphPen(None).glyph(); advances["g0020"] = 4 * UPM_PX
        glyph_order.append("g0020"); cmap[0x20] = "g0020"

    fb = FontBuilder(UPM, isTTF=True)
    fb.setupGlyphOrder(glyph_order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    metrics = {n: (advances.get(n, CELL * UPM_PX), glyphs[n].xMin if hasattr(glyphs[n], "xMin") and glyphs[n].numberOfContours > 0 else 0) for n in glyph_order}
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=ASCENT, descent=0)
    fb.setupNameTable({"familyName": "AsdaUIFont", "styleName": "Regular",
                       "fullName": "AsdaUIFont", "psName": "AsdaUIFont-Regular"})
    fb.setupOS2(sTypoAscender=ASCENT, sTypoDescender=0, usWinAscent=ASCENT, usWinDescent=0)
    fb.setupPost()
    fb.save(OUT_TTF)
    print("OK шрифт: %s | глифов=%d (EN+RU), UPM=%d, cell=%dpx" % (OUT_TTF, len(glyph_order) - 1, UPM, CELL))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
