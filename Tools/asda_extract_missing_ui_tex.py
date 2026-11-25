# Copyright (c) 2026 @sitxovski (Discord)
# Извлечь ТОЛЬКО недостающие скин-текстуры окон (.TEX→PNG). Проект уже содержит большинство текстур;
# докидываем UI-атласы окон, которых нет. Список = скины JuiSkin.ini, которых нет в Content.
import os, io, zipfile, glob, sys, importlib.util
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
JUIDATA = _P.JUIDATA
OUT = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Textures", "JUI")
CONTENT = os.path.join(_P.ROOT, "AstoryClient", "Content")

spec = importlib.util.spec_from_file_location("J", os.path.join(HERE, "asda_jui_parse.py"))
J = importlib.util.module_from_spec(spec); sys.modules["J"] = J; spec.loader.exec_module(J)

# индекс всех .tex (loose + архивы)
tex_src = {}

def tex_bytes(src):
    if src[0] == "file": return open(src[1], "rb").read()
    return zipfile.ZipFile(src[1]).read(src[2])


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global src
    os.makedirs(OUT, exist_ok=True)
    skins = J.load_skin()
    need = set(os.path.splitext(str(v["tex_name"]))[0].lower() for v in skins.values() if v.get("tex_name"))
    have = set(os.path.splitext(os.path.basename(f))[0].lower() for f in glob.glob(CONTENT + r"\**\*.uasset", recursive=True))
    missing = {n for n in need if n not in have}
    print("нужно скин-текстур:", len(need), "| нет в проекте:", len(missing))
    for p in glob.glob(os.path.join(JUIDATA, "**", "*.tex"), recursive=True) + glob.glob(os.path.join(JUIDATA, "**", "*.TEX"), recursive=True):
        tex_src.setdefault(os.path.splitext(os.path.basename(p))[0].lower(), ("file", p))
    for arc in glob.glob(os.path.join(JUIDATA, "**", "*.btz"), recursive=True):
        try: z = zipfile.ZipFile(arc)
        except Exception: continue
        for n in z.namelist():
            if n.lower().endswith(".tex"):
                tex_src.setdefault(os.path.splitext(os.path.basename(n))[0].lower(), ("zip", arc, n))
    ok = 0; dxt = []; nofile = []
    for nm in sorted(missing):
        src = tex_src.get(nm)
        if not src: nofile.append(nm); continue
        data = tex_bytes(src)
        if len(data) < 0x24: nofile.append(nm); continue
        try:
            Image.open(io.BytesIO(data[0x24:])).convert("RGBA").save(os.path.join(OUT, nm + ".png")); ok += 1
        except Exception:
            dxt.append(nm)
    print("=== извлечено %d PNG | DXT/непонятных %d | нет файла %d ===" % (ok, len(dxt), len(nofile)))
    if dxt: print("DXT (нужен спец-декод):", dxt)
    if nofile: print("нет .tex файла:", nofile)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
