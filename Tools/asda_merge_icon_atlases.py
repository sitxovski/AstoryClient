# Copyright (c) 2026 @sitxovski (Discord)
# asda_merge_icon_atlases.py — слияние атласов иконок предметов: база = сборка MINE (RawAssets),
# пустые (сплошные/прозрачные) ячейки долиты из клиентов с ПОБАЙТОВО ТЕМ ЖЕ item_img_table.bin
# (AsdaChroniclesInterlude). Причина: у MINE часть атласов недорисована → «белые иконки».
# Отчёт: Reports/item_icons_missing/merged_cells.csv. Запуск: python asda_merge_icon_atlases.py
# Затем переимпорт в UE: run_headless.bat asda_reimport_icon_atlases.py
# Слить атласы иконок: база = MINE (текущий вид порта), пустые ячейки заполнить из клиентов
# с ПОБАЙТОВО ТЕМ ЖЕ item_img_table.bin (Interlude → Interlude_copy). Результат → RawAssets/Textures/JUI.
import os, io, sys, csv, glob, zipfile, shutil, json
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from PIL import Image

DB   = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "ItemDB")
RAW  = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Textures", "JUI")
BAK  = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Textures", "JUI", "_bak_icons_2026-08-01")
OUT  = os.path.join(_P.RE_REPORTS, "item_icons_missing")
DONORS = [("Interlude", _P.JUIDATA_ORIG),
          ("Interlude_copy", _P.JUIDATA_ORIG_COPY)]
BASE = ("MINE", _P.JUIDATA)

def main():
    names = set()
    with open(os.path.join(DB, "item_img_table.csv"), encoding="utf-8") as f:
        for r in csv.DictReader(f):
            for k in ("IconAtlasMale", "IconAtlasFemale"):
                if r[k]: names.add(r[k].lower())

    def collect(jd):
        out = {}
        for p in glob.glob(os.path.join(jd, "**", "*.tex"), recursive=True):
            b = os.path.basename(p).lower()[:-4]
            if b in names and b not in out: out[b] = open(p, "rb").read()
        for arc in glob.glob(os.path.join(jd, "**", "*.btz"), recursive=True):
            try: z = zipfile.ZipFile(arc)
            except Exception: continue
            for n in z.namelist():
                if not n.lower().endswith(".tex"): continue
                b = os.path.basename(n).lower()[:-4]
                if b in names and b not in out: out[b] = z.read(n)
        return out

    def img_of(d):
        try: return Image.open(io.BytesIO(d[0x24:])).convert("RGBA")
        except Exception: return None

    def empty(im, x, y, s):
        r = im.crop((x, y, x + s, y + s))
        if r.getchannel("A").getextrema()[1] == 0: return True
        return all(lo == hi for lo, hi in r.convert("RGB").getextrema())

    base = collect(BASE[1])
    don  = [(n, collect(p)) for n, p in DONORS]
    print("база %d атласов, доноры: %s" % (len(base), [(n, len(d)) for n, d in don]))

    log = []; changed = []
    for a in sorted(names):
        if a not in base: continue
        im = img_of(base[a])
        if im is None: continue
        s = 32 if a.startswith("s_item_s_") else 64
        W, H = im.size
        filled = 0; src_used = {}
        for gy in range(H // s):
            for gx in range(W // s):
                x, y = gx * s, gy * s
                if not empty(im, x, y, s): continue
                for dn, dmap in don:
                    dd = dmap.get(a)
                    if dd is None: continue
                    dim = img_of(dd)
                    if dim is None or dim.size != (W, H): continue
                    if empty(dim, x, y, s): continue
                    im.paste(dim.crop((x, y, x + s, y + s)), (x, y))
                    filled += 1; src_used[dn] = src_used.get(dn, 0) + 1
                    log.append((a, gy * (W // s) + gx, x, y, s, dn))
                    break
        if filled:
            png = os.path.join(RAW, a + ".png")
            if os.path.exists(png) and not os.path.exists(os.path.join(BAK, a + ".png")):
                shutil.copy2(png, os.path.join(BAK, a + ".png"))
            im.save(png)
            changed.append((a, filled, src_used))
            print("MERGE %-16s +%d ячеек %s" % (a, filled, src_used))

    print("=== изменено атласов: %d, добавлено ячеек: %d" % (len(changed), sum(c[1] for c in changed)))
    with open(os.path.join(OUT, "merged_cells.csv"), "w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f); w.writerow(["Atlas", "GridIndex", "X", "Y", "CellSize", "Donor"]); w.writerows(log)
    json.dump([[a, n, s] for a, n, s in changed], open(os.path.join(os.path.dirname(__file__), "merged.json"), "w"))


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    os.makedirs(BAK, exist_ok=True); os.makedirs(OUT, exist_ok=True)
    main()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
