# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_e5_char_alpha.py — Э5(а) круглого стола (FINAL_synthesis.md §5): ГИСТОГРАММА АЛЬФЫ персонажных текстур.
#   Вопрос: есть ли в альфе текстур персонажей/экипа ПЕРЕМЕННАЯ маска (для roughness = lerp(0.85, 0.5, Tex.A)
#   / SpecAlphaBoost), или альфа константна и параметр надо делать константой.
#   СИСТЕМНЫЙ Python (PIL+numpy), только чтение. Два источника — оба обязательны (закон false-negatives-method):
#     1) ОРИГИНАЛ: `Donors/TRANSLATE_unpacked/textures_orig/Data/Character/Texture/*.tex` — заголовок 36 Б:
#        u32 magic 0x20040603 | u32 size | u32 w | u32 h | u32 f10 (=1, не мипы) | u32 MipLevels | u32 D3DFORMAT | u32 | u32, затем
#        payload PNG / DDS / BMP как есть (тот же разбор, что asda_gacha_fix_texture_mips.py:50).
#        D3DFORMAT 22 = X8R8G8B8 (альфа ОТБРАСЫВАЕТСЯ при создании текстуры — формат без альфы),
#        21 = A8R8G8B8, fourcc DXT1 (1-бит альфа), DXT2/3 (явная 4-бит, DXT2 — премультиплицированная).
#     2) ПОРТ: `RawAssets/Textures/Data/Character/Texture/*.png` — что реально импортировано в UE
#        (/Game/AsdaAssets/Textures/Characters/Texture); режим RGB ⇒ UE даёт альфу = 1 (константа).
#   Классы альфы: no_alpha (формат/режим без альфы) | constant (одно значение) | near_constant (≥99 % пикселей
#   одно значение) | binary (только 2 значения, «вырез») | variable (плавная маска; гистограмма 8 корзин).
# Запуск: python Tools/asda_e5_char_alpha.py [--out <md>]   (пути по умолчанию — координаты проекта)
import os, sys, io, struct, argparse, collections
import asda_fmt_tex as _FT   # общий декодер .TEX (волна 2 аудита дублей, E9)

ORIG_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Donors/TRANSLATE_unpacked/textures_orig/Data/Character/Texture"
PORT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/RawAssets/Textures/Data/Character/Texture"
OUT_DEFAULT = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05/e5a_char_alpha.md"
TEX_MAGIC = 0x20040603
FMT_NAMES = {22: "X8R8G8B8", 21: "A8R8G8B8", 20: "R8G8B8", 23: "R5G6B5", 25: "A1R5G5B5", 26: "A4R4G4B4"}


def fourcc_name(v):
    b = struct.pack("<I", v)
    return b.decode("latin1") if all(32 <= c < 127 for c in b) else str(v)


def alpha_stats(img):
    """Статистика альфы PIL-изображения → dict (или None, если альфы нет в режиме)."""
    import numpy as np
    if img.mode not in ("RGBA", "LA", "PA", "P"):
        if img.mode == "P" and "transparency" in img.info:
            pass
        else:
            return None
    if img.mode == "P":
        if "transparency" not in img.info:
            return None
        img = img.convert("RGBA")
    a = np.asarray(img.convert("RGBA"))[..., 3]
    vals, counts = np.unique(a, return_counts=True)
    n = a.size
    top = counts.max() / n
    hist = np.histogram(a, bins=8, range=(0, 256))[0]
    if len(vals) == 1:
        cls = "constant"
    elif top >= 0.99:
        cls = "near_constant"
    elif len(vals) == 2:
        cls = "binary"
    else:
        cls = "variable"
    return {"cls": cls, "min": int(a.min()), "max": int(a.max()), "mean": float(a.mean()),
            "uniq": int(len(vals)), "top_frac": float(top), "mode": int(vals[counts.argmax()]),
            "hist": [int(x) for x in hist], "n": int(n)}


def read_tex(path):
    """Оригинальный .tex → (fmt_name, payload_kind, PIL image | None, w, h, mips).
    Разбор — общий `asda_fmt_tex` (волна 2). ★ mips = поле +0x14 (MipLevels): прежняя копия брала +0x10,
    которое на данных ВСЕГДА 1 (сверено с DDS dwMipMapCount у 1915 файлов, 14.09.2026)."""
    b = open(path, "rb").read()
    h = _FT.read_header(b)
    if h is None:
        return None
    payload = _FT.payload(b)
    kind = _FT.payload_kind(payload, unknown="?")
    if kind == "JPG":
        kind = "?"                                  # прежняя классификация JPG не выделяла
    fmt_name = FMT_NAMES.get(h.fmt) or fourcc_name(h.fmt)
    img = _FT.to_pil(b, mode=None, min_size=_FT.HDR, tga_fallback=False)
    return fmt_name, kind, img, h.width, h.height, h.mips


def group_of(name):
    """ch1_f_00_chst001_00_00_0 → ('ch1', 'chst'); прочие → (префикс, '')."""
    parts = name.split("_")
    pre = parts[0]
    slot = ""
    if pre in ("ch1", "ch2", "ch4") and len(parts) >= 4:
        slot = "".join(ch for ch in parts[3] if ch.isalpha())
    return pre, slot


def scan_orig(d):
    rows = {}
    for f in sorted(os.listdir(d)):
        if not f.lower().endswith(".tex"):
            continue
        r = read_tex(os.path.join(d, f))
        if r is None:
            continue
        fmt_name, kind, img, w, h, mips = r
        st = alpha_stats(img) if img is not None else None
        payload_has_alpha = st is not None and st["cls"] != "constant"
        # Что видит D3D: формат без альфы → альфа отброшена независимо от payload
        if fmt_name in ("X8R8G8B8", "R8G8B8", "R5G6B5"):
            eff = "no_alpha_format"
        elif st is None:
            eff = "no_alpha_payload" if img is not None else "undecoded"
        else:
            eff = st["cls"]
        rows[os.path.splitext(f)[0]] = {"fmt": fmt_name, "kind": kind, "w": w, "h": h, "mips": mips,
                                       "stats": st, "eff": eff, "payload_alpha_cls": (st["cls"] if st else None)}
    return rows


def scan_port(d):
    from PIL import Image
    rows = {}
    for f in sorted(os.listdir(d)):
        if not f.lower().endswith(".png"):
            continue
        try:
            img = Image.open(os.path.join(d, f)); img.load()
        except Exception:
            continue
        st = alpha_stats(img)
        rows[os.path.splitext(f)[0]] = {"mode": img.mode, "stats": st,
                                       "eff": (st["cls"] if st else "no_alpha_mode")}
    return rows


def pct(a, b):
    return "%.1f %%" % (100.0 * a / b) if b else "—"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--orig", default=ORIG_DIR)
    ap.add_argument("--port", default=PORT_DIR)
    ap.add_argument("--out", default=OUT_DEFAULT)
    a = ap.parse_args()
    orig = scan_orig(a.orig) if os.path.isdir(a.orig) else {}
    port = scan_port(a.port) if os.path.isdir(a.port) else {}
    L = []
    L.append("# Э5(а) — гистограмма альфы персонажных текстур (офлайн, `Tools/asda_e5_char_alpha.py`)\n")
    L.append("Источники: оригинал `%s` (%d .tex), порт `%s` (%d .png).\n" % (a.orig, len(orig), a.port, len(port)))
    L.append("Классы: `no_alpha_format` — D3DFORMAT без альфы (X8R8G8B8): альфа отброшена загрузчиком, что бы ни лежало в payload; "
             "`constant`/`near_constant` (≥99 % пикселей одно значение)/`binary` (2 значения, вырез)/`variable` (плавная маска).\n")

    def summarize(title, rows, key="eff", subset=None):
        sel = {k: v for k, v in rows.items() if (subset is None or subset(k))}
        c = collections.Counter(v[key] for v in sel.values())
        n = len(sel)
        L.append("### %s — %d файлов\n" % (title, n))
        L.append("| класс | файлов | доля |\n|---|---|---|")
        for k, v in c.most_common():
            L.append("| %s | %d | %s |" % (k, v, pct(v, n)))
        L.append("")
        return sel, c

    # --- оригинал
    L.append("## 1. Оригинал (.tex, заголовок + декод payload)\n")
    fmtc = collections.Counter((v["fmt"], v["kind"]) for v in orig.values())
    L.append("| D3DFORMAT | payload | файлов |\n|---|---|---|")
    for (f, k), n in fmtc.most_common():
        L.append("| %s | %s | %d |" % (f, k, n))
    L.append("")
    all_o, c_all = summarize("Все персонажные текстуры (эффективная альфа, как её видит D3D)", orig)
    ch1, c_ch1 = summarize("Только `ch1_*` (тела/экип игровых персонажей)", orig, subset=lambda k: k.startswith("ch1_"))
    # payload alpha regardless of format (честность: что лежит в файле)
    pc = collections.Counter(v["payload_alpha_cls"] or "none" for v in orig.values())
    L.append("Альфа В PAYLOAD безотносительно формата (что физически записано): " +
             ", ".join("%s×%d" % kv for kv in pc.most_common()) + ".\n")
    fmt_x_payload = collections.Counter((v["fmt"], v["payload_alpha_cls"] or "none") for v in orig.values())
    L.append("| D3DFORMAT | альфа payload | файлов |\n|---|---|---|")
    for (f, k), n in fmt_x_payload.most_common():
        L.append("| %s | %s | %d |" % (f, k, n))
    L.append("")
    # по слотам ch1
    slots = collections.defaultdict(collections.Counter)
    for k, v in ch1.items():
        slots[group_of(k)[1]][v["eff"]] += 1
    L.append("### `ch1_*` по слотам экипа (эффективная альфа)\n")
    L.append("| слот | всего | variable | binary | near_constant | constant | no_alpha_format |\n|---|---|---|---|---|---|---|")
    for s, c in sorted(slots.items(), key=lambda kv: -sum(kv[1].values())):
        t = sum(c.values())
        L.append("| %s | %d | %d | %d | %d | %d | %d |" % (s or "—", t, c["variable"], c["binary"], c["near_constant"], c["constant"], c["no_alpha_format"]))
    L.append("")
    # примеры variable
    var = [(k, v) for k, v in orig.items() if v["eff"] == "variable"]
    var.sort(key=lambda kv: -(kv[1]["stats"]["uniq"]))
    L.append("### Примеры `variable` в оригинале (uniq значений, min..max, среднее, гистограмма 8 корзин 0..255)\n")
    L.append("| файл | fmt | uniq | min..max | mean | mode (доля) | гистограмма |\n|---|---|---|---|---|---|---|")
    for k, v in var[:40]:
        s = v["stats"]
        L.append("| %s | %s | %d | %d..%d | %.1f | %d (%.0f %%) | %s |" % (
            k, v["fmt"], s["uniq"], s["min"], s["max"], s["mean"], s["mode"], 100 * s["top_frac"], " ".join(str(x) for x in s["hist"])))
    if len(var) > 40:
        L.append("| … ещё %d | | | | | | |" % (len(var) - 40))
    L.append("")
    binr = [k for k, v in orig.items() if v["eff"] == "binary"][:20]
    L.append("Примеры `binary` (вырез): " + (", ".join(binr) if binr else "нет") + ("…" if len(binr) == 20 else "") + "\n")

    # --- порт
    L.append("## 2. Порт (RawAssets PNG — то, что импортировано в UE)\n")
    modes = collections.Counter(v["mode"] for v in port.values())
    L.append("Режимы PNG: " + ", ".join("%s×%d" % kv for kv in modes.most_common()) + ". Режим RGB ⇒ в UE альфа = 1 (константа).\n")
    summarize("Все PNG порта", port)
    summarize("Только `ch1_*` порта", port, subset=lambda k: k.startswith("ch1_"))
    # --- расхождение оригинал↔порт
    L.append("## 3. Сверка оригинал ↔ порт (по имени файла)\n")
    both = set(orig) & set(port)
    lost = [k for k in both if orig[k]["eff"] in ("variable", "binary", "near_constant") and port[k]["eff"] in ("no_alpha_mode", "constant")]
    gained = [k for k in both if orig[k]["eff"] == "no_alpha_format" and port[k]["eff"] in ("variable", "binary", "near_constant")]
    L.append("Общих имён: %d; только в оригинале: %d; только в порте: %d.\n" % (len(both), len(set(orig) - set(port)), len(set(port) - set(orig))))
    L.append("Маска ЕСТЬ в оригинале, но ПОТЕРЯНА в PNG порта: **%d** (%s%s)\n" % (len(lost), ", ".join(sorted(lost)[:25]), "…" if len(lost) > 25 else ""))
    L.append("Формат оригинала БЕЗ альфы (X8R8G8B8), но PNG порта несёт переменную альфу (в UE она станет Tex.A, хотя оригинал её отбрасывал): **%d** (%s%s)\n" % (
        len(gained), ", ".join(sorted(gained)[:25]), "…" if len(gained) > 25 else ""))
    # --- вывод по критерию Э5
    n_ch1 = len(ch1); v_ch1 = c_ch1["variable"]; b_ch1 = c_ch1["binary"]
    L.append("## 4. Итог по критерию Э5(а)\n")
    L.append("- Оригинал, `ch1_*`: неконстантная эффективная альфа (variable+binary+near_constant) у **%d из %d (%s)**; из них плавная (`variable`) — %d (%s), вырез (`binary`) — %d.\n" % (
        v_ch1 + b_ch1 + c_ch1["near_constant"], n_ch1, pct(v_ch1 + b_ch1 + c_ch1["near_constant"], n_ch1), v_ch1, pct(v_ch1, n_ch1), b_ch1))
    L.append("- Оригинал, все персонажные: variable — %d из %d (%s).\n" % (c_all["variable"], len(orig), pct(c_all["variable"], len(orig))))
    pv = sum(1 for k, v in port.items() if k.startswith("ch1_") and v["eff"] == "variable")
    pn = sum(1 for k in port if k.startswith("ch1_"))
    L.append("- Порт (что получит `Tex.A` в UE), `ch1_*`: variable — %d из %d (%s).\n" % (pv, pn, pct(pv, pn)))
    L.append("- Критерий стола: «0 % неконстантных → константа». Решение по цифрам выше; если variable > 0 — маска есть, но проверить, ЧТО она кодирует (вырез волос/плащей vs спекуляр) — по примерам §1.\n")
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")
    print("-> %s | orig=%d port=%d | orig eff: %s | port eff: %s" % (
        a.out, len(orig), len(port), dict(c_all), dict(collections.Counter(v["eff"] for v in port.values()))))


if __name__ == "__main__":
    main()
