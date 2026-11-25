# Copyright (c) 2026 @sitxovski (Discord)
# Извлечь ВСЕ UI-текстуры .TEX (juidata loose + Texture.btz) → PNG для UMG-импорта.
# .TEX = 8б (magic 0x20040603 + size) + 28б info + СТАНДАРТНЫЙ TGA с офсета 0x24
# (реверс CTextureFont_LoadAtlas@0x115D920). Выход: RawAssets/Textures/JUI/<имя>.png (полные атласы).
import os, io, zipfile, glob, sys
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_fmt_tex as _FT   # общий декодер .TEX (волна 2 аудита дублей, E9)
import asda_login_guard as _LG   # ★ 16.09.2026 (раздел 17): PNG-исходники JUI-атласов окон логина — только с разрешения владельца
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
from PIL import Image

JUIDATA = _P.JUIDATA
OUT = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Textures", "JUI")

_LOGIN_SKIPPED = []   # ★ 16.09.2026 (раздел 17): имена .TEX, чей PNG не записан защитой логина (итог одной строкой в main)

def tex_to_png(data, name):
    """Байты .TEX → PNG. Общий декод `asda_fmt_tex` (волна 2): магия не проверяется, как раньше.
    ★ DXT2 (премультиплицированные иконки gIcon) теперь декодируются вместо FAIL; premultiply не снимается."""
    if len(data) < _FT.HDR: return False
    # ★ 16.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 17): PNG — исходник пакета /Game/AsdaAssets/Textures/JUI/<имя>; атлас окон
    #   логина (Tools/data/asda_login_packages.json) без ASDA_ALLOW_LOGIN_WRITE=1 не перезаписывается.
    if _LG.login_package_blocked("/Game/AsdaAssets/Textures/JUI/" + os.path.splitext(name)[0], "asda_extract_all_jui_tex.py", quiet=True):
        _LOGIN_SKIPPED.append(os.path.splitext(name)[0])
        return False
    try:
        img = _FT.to_pil(data, check_magic=False, min_size=_FT.HDR, tga_fallback=False, raise_errors=True)
    except Exception as e:
        print("  FAIL %s: %s" % (name, e)); return False
    img.save(os.path.join(OUT, os.path.splitext(name)[0] + ".png"))
    return True


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    os.makedirs(OUT, exist_ok=True)
    seen = set(); ok = 0
    # 1) loose .tex по всему juidata
    for p in glob.glob(os.path.join(JUIDATA, "**", "*.tex"), recursive=True) + \
             glob.glob(os.path.join(JUIDATA, "**", "*.TEX"), recursive=True):
        nm = os.path.basename(p).lower()
        if nm in seen: continue
        seen.add(nm)
        if tex_to_png(open(p, "rb").read(), os.path.basename(p)): ok += 1
    # 2) .tex внутри архивов .btz/.btz
    for arc in glob.glob(os.path.join(JUIDATA, "**", "*.btz"), recursive=True):
        try: z = zipfile.ZipFile(arc)
        except Exception: continue
        for n in z.namelist():
            if not n.lower().endswith(".tex"): continue
            bn = os.path.basename(n).lower()
            if bn in seen: continue
            seen.add(bn)
            if tex_to_png(z.read(n), os.path.basename(n)): ok += 1
    print("=== .TEX → PNG: извлечено %d (из %d уник) в %s ===" % (ok, len(seen), OUT))
    if _LOGIN_SKIPPED:   # ★ 16.09.2026 (раздел 17): итог защиты логина одной строкой
        print("[LOGIN-GUARD] asda_extract_all_jui_tex.py: PNG атласов окон логина не записано %d (%s) — только с %s=1"
              % (len(_LOGIN_SKIPPED), ", ".join(_LOGIN_SKIPPED), _LG.ALLOW_ENV))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
