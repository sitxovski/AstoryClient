# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_modadda_unboost.py — СНЯТЬ буст свечения ×N с аддитивных половин пары dst = 3 ЛЮБОГО src
#   (`MI_<tex>_{modadda,modaddsq,modaddinv,modaddc}_fxbNNN`), где ПРЕЖНЯЯ половина давала 0.
#   10/11.10.2026: первая версия — только src 4 (`_modadda`, 70 половин, отчёт `Reports/owner_1010/fx_alphatest.md`);
#   11.10.2026 обобщено по решению владельца на все src (movdw01 src 9, defdwe01 src 2 и полный скан всех BP;
#   отчёт `Reports/owner_1011/fx_unboost_meshes.md`).
#
# ПОЧЕМУ. Волна Ф1 ставила буст на прежнюю (неверную для src ≠ 5) половину `M_AsdaEffectModAdd` = srcC×(1−srcA)×boost.
#   При srcA ≡ 1 она давала 0, и ×N был невидим. Правка fx_blend_fix перенесла вариант на эталонную половину
#   (src 4 srcC×srcA, src 2 srcC×srcC, src 9 srcC×(1−dstC), src 3 srcC×(1−srcC)) — и такие эмиттеры стали в N раз
#   ярче эталона (×1). Владелец: буст убрать.
#   srcA ≡ 1 ⇔ альфа ПРЕЖНЕЙ текстуры ≡ 255 (таблица альф из Texture.btz, см. `build_tex_alpha`) И вершинная альфа 1
#   на всей жизни (0xC/0xB/0x19 — ключи трека [13]; без ключей — поле alpha эмиттера; effect_loaders_pertype.md §5).
#   Прежняя текстура — из «было» плана fx_blend_fix (`plan_apply.json`; у мешей там бывает `default`), иначе текущая.
#   Половины с srcA ≡ 0 не трогаются (у src 4 новая половина = 0; у src 2/9/3 прежняя была srcC×boost ≠ 0 — не наш случай).
#   Смешанные (альфа меняется) и текстуры с переменной альфой — только в отчёт, без правки.
#
# ЗАЩИТА ПРАВОК ВЛАДЕЛЬЦА: меняется только MaterialB, равный ровно `MI_<текстура эмиттера>_<половина>_fxbNNN`
#   (имя, которое ставят инструменты), цель — базовый `MI_<текстура>_<половина>` с верным родителем и текстурой.
#   Повторный прогон — 0 правок. Прочие поля и Material не меняются.
#
# Запуск:
#   1) таблица альф текстур (системный Python с Pillow, вне UE):  python Tools/asda_fx_modadda_unboost.py --tex-alpha
#   2) python Tools/run_headless_safe.py Tools/asda_fx_modadda_unboost.py --marker "FX-UNBOOST ЗАВЕРШЕНО"
#      ASDA_FXUB_MODE=report (по умолчанию) | apply. Ассеты сохранять только без запущенного -game.
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
import json
import os
import re

FX_DIR = "/Game/AsdaAssets/Effects"                     # каталог BP_Fx_*
HALVES = ("modadda", "modaddsq", "modaddinv", "modaddc")   # эталонные аддитивные половины src 4 / 2 / 9 / 3
VAR_RX = re.compile(r"^(MI_(.+)_(%s))(_fxb\d{3}(?:s\d+)?)$" % "|".join(HALVES))   # вариант буста на такой половине
OLD_RX = re.compile(r"^MI_(.+)_modadd(?:_fxb\d{3}(?:s\d+)?)?$")   # прежняя половина M_AsdaEffectModAdd (имя «было»)
OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/owner_1011/fx_unboost"      # отчёты прогона
REPORT_JSON = OUT_DIR + "/unboost_%s.json"                       # итог прогона (%s = режим)
TEX_ALPHA_JSON = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "asda_fx_tex_alpha.json")   # таблица
#   альф текстур эффектов (stem → [min, max, утечка]); читают ещё asda_fx_blend_fix и asda_apply_fx_boost (через prior_half_zero)
_TEX_ALPHA_CACHE = {}                                          # кэш таблицы альф на процесс (ключ — путь)
PLAN_JSON = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/owner_1010/fx_blend_fix/plan_apply.json"   # «было» правки fx_blend_fix


def log(m):
    """Журнал."""
    import unreal
    unreal.log_warning("[fx-unboost] %s" % m)


def build_tex_alpha():
    """Вне UE: min/max альфы каждой текстуры Texture.btz (без снятия премультипликации) → TEX_ALPHA_JSON."""
    import sys
    import zipfile
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_paths as P
    import asda_fmt_tex as FT
    z = zipfile.ZipFile(P.EFFECT_TEX_BTZ)
    out, bad = {}, []
    for n in z.namelist():
        if not n.lower().endswith(".tex"):
            continue
        im = FT.to_pil(z.read(n), unpremultiply=False)
        if im is None:
            bad.append(n)
            continue
        rgba = im.convert("RGBA")
        lo, hi = rgba.getchannel("A").getextrema()
        # «утечка» прежней половины по текселям: доля текселей с a < 255 И rgb > 0 (srcC×(1−Tex.a) ≠ 0). У DXT1 с 1-битной
        #   альфой прозрачный тексель — чёрный (0,0,0,0), поэтому утечка 0: прежняя половина давала 0 по текселям,
        #   ненулевой остаётся только кайма билинейной фильтрации (c·t·(1−t), как у D3D).
        px = rgba.getdata()
        leak = sum(1 for r, g, bb, al in px if al < 255 and (r or g or bb))
        out[os.path.splitext(os.path.basename(n))[0].lower()] = [lo, hi, round(leak / float(len(px)), 6)]
    os.makedirs(os.path.dirname(TEX_ALPHA_JSON), exist_ok=True)
    with open(TEX_ALPHA_JSON, "w", encoding="utf-8") as fh:
        json.dump({"текстур": len(out), "не_декодированы": bad, "alpha": out}, fh, ensure_ascii=False, indent=0)
    print("tex-alpha: текстур %d, не декодированы %d -> %s" % (len(out), len(bad), TEX_ALPHA_JSON))


def alpha_values(d):
    """Значения вершинной альфы эмиттера .jeb за жизнь: ключи трека [13], без ключей — поле alpha."""
    pr = d.get("params", {}) or {}
    al = [k[13] for k in (pr.get("trackNodes") or []) if len(k) > 13]
    if not al and "alpha" in pr:
        al = list(pr["alpha"][:2])
    return al


def classify(d, stem, tex_alpha):
    """'one' (srcA ≡ 1), 'one_punch' (вершинная альфа ≡ 1, текстура DXT1-«дырявая»: где a < 255, там rgb = 0 ⇒
    srcC×(1−srcA) ≡ 0 по текселям), 'zero' (≡ 0), 'mixed', 'tex_alpha' (альфа текстуры с утечкой), 'notex', 'nodata'."""
    ta = tex_alpha.get(stem.lower())
    if ta is None:
        return "notex"
    al = alpha_values(d)
    if not al:
        return "nodata"
    if all(abs(a - 1.0) < 1e-6 for a in al):
        if ta[0] == 255:
            return "one"
        if len(ta) > 2 and ta[2] == 0:
            return "one_punch"
    if ta[1] == 0 or all(abs(a) < 1e-6 for a in al):
        return "zero"
    if ta[0] != 255:
        return "tex_alpha"
    return "mixed"


def load_tex_alpha():
    """Таблица альф текстур {stem: [min, max, утечка]} или {} без файла (кэш на процесс)."""
    if TEX_ALPHA_JSON not in _TEX_ALPHA_CACHE:
        try:
            _TEX_ALPHA_CACHE[TEX_ALPHA_JSON] = json.load(open(TEX_ALPHA_JSON, encoding="utf-8"))["alpha"]
        except Exception:
            _TEX_ALPHA_CACHE[TEX_ALPHA_JSON] = {}
    return _TEX_ALPHA_CACHE[TEX_ALPHA_JSON]


def prior_half_zero(d, old_stem):
    """Давала ли ПРЕЖНЯЯ половина M_AsdaEffectModAdd (srcC×(1−srcA)) ноль у эмиттера `d` с текстурой `old_stem`.
    Точка для сборщиков: на такой половине буст волны Ф1 был невидим и на эталонную половину не переносится
    (решение владельца 11.10.2026). Без таблицы альф — False (буст не трогается)."""
    ta = load_tex_alpha()
    return bool(ta) and d is not None and classify(d, old_stem, ta) in ("one", "one_punch")


def old_halves():
    """{(jeb, i): имя прежней MaterialB} из плана fx_blend_fix; {} без файла."""
    if not os.path.isfile(PLAN_JSON):
        return {}
    pl = json.load(open(PLAN_JSON, encoding="utf-8"))
    out = {}
    for e in pl.get("эффекты", []):
        for r in e.get("правки", []):
            if r.get("поле") == "material_b":
                out[(e["jeb"], int(r["i"]))] = r.get("было", "")
    return out


def run(write):
    """Обход ВСЕХ BP_Fx_* с половинами `_<половина>_fxb…`. write=False — только план."""
    import unreal
    import asda_fx_blend_fix as BF
    import asda_fx_common as FXC
    EAL = unreal.EditorAssetLibrary
    masters = BF.ensure_masters(False)
    tex_alpha = load_tex_alpha()
    if not tex_alpha:
        raise RuntimeError("нет таблицы альф %s — сначала --tex-alpha" % TEX_ALPHA_JSON)
    olds = old_halves()
    res = {"режим": "apply" if write else "report", "bp_всего": 0, "bp_с_вариантом": 0,
           "правки": [], "оставлены": [], "ошибки": []}
    for p in sorted(EAL.list_assets(FX_DIR, False, False)):
        if "/BP_Fx_" not in p:
            continue
        res["bp_всего"] += 1
        bp = EAL.load_asset(p.split(".")[0])
        cdo = unreal.get_default_object(bp.generated_class())
        ems = list(cdo.get_editor_property("emitters"))
        if not any(VAR_RX.match(BF._name(e.get_editor_property("material_b"))) for e in ems):
            continue
        res["bp_с_вариантом"] += 1
        jeb = str(cdo.get_editor_property("effect_name") or "")
        jb = BF.read_jeb(jeb) if jeb else None
        if not jb:
            res["ошибки"].append("%s: нет .jeb '%s'" % (p, jeb))
            continue
        changed = False
        for i, d in BF.align(ems, jb["emitters"]):
            mb = BF._name(ems[i].get_editor_property("material_b"))
            m = VAR_RX.match(mb)
            if not m:
                continue
            row = {"jeb": jeb, "src": jb.get("blendSrc"), "i": i, "эмиттер": str(ems[i].get_editor_property("name")),
                   "было": mb, "половина": m.group(3)}
            if d is None:
                row["почему"] = "эмиттер не сопоставлен с .jeb"
                res["оставлены"].append(row)
                continue
            tex = BF.mesh_tex(d) if d["type"] in BF.MESH_TYPES else d.get("tex", "")   # как у сборщика
            stem = BF.tex_stem(tex)
            old = olds.get((jeb, i), "")
            mo = OLD_RX.match(old)
            old_stem = mo.group(1) if mo else stem          # прежняя текстура (у мешей бывала `default`)
            cls = classify(d, old_stem, tex_alpha)
            row.update({"тип": "0x%X" % d["type"], "текстура": tex, "прежняя_половина": old or "(нет в плане — как текущая)",
                        "альфа_прежней_текстуры": tex_alpha.get(old_stem.lower()), "srcA": cls, "альфа": alpha_values(d)[:6]})
            if m.group(2).lower() != stem.lower():
                row["почему"] = "MaterialB не от текстуры эмиттера — ручная/чужая правка"
                res["оставлены"].append(row)
                continue
            if cls not in ("one", "one_punch"):
                row["почему"] = "srcA прежней половины не ≡ 1 — прежняя половина не давала 0"
                res["оставлены"].append(row)
                continue
            path, st = BF.mic_state(stem, m.group(3), "", masters)
            if st != "ok":
                row["почему"] = "базовый %s: %s" % (path, st)
                res["оставлены"].append(row)
                continue
            row["стало"] = m.group(1)
            res["правки"].append(row)
            if write:
                e = ems[i]
                e.set_editor_property("material_b", EAL.load_asset(path))
                ems[i] = e                     # копию структуры — обратно (грабля Python-UE, см. asda_apply_fx_boost)
                changed = True
        if write and changed:
            if FXC.finish_effect_bp(bp, cdo, ems):
                print("ASDA_WRITES_ASSET: %s" % p.split(".")[0])
            else:
                res["ошибки"].append("%s не сохранён" % p)
    os.makedirs(OUT_DIR, exist_ok=True)
    with open(REPORT_JSON % res["режим"], "w", encoding="utf-8") as fh:
        json.dump(res, fh, ensure_ascii=False, indent=1)
    by, kept = {}, {}
    for r in res["правки"]:
        k = "%s/%s" % (r["jeb"], r["половина"])
        by[k] = by.get(k, 0) + 1
    for r in res["оставлены"]:
        k = "%s/%s/%s" % (r["jeb"], r["половина"], r.get("srcA", "-"))
        kept[k] = kept.get(k, 0) + 1
    log("эффекты с правкой: %s" % ", ".join("%s=%d" % kv for kv in sorted(by.items())))
    log("оставлены: %s" % ", ".join("%s=%d" % kv for kv in sorted(kept.items())))
    log("FX-UNBOOST ЗАВЕРШЕНО: режим %s, BP %d (с вариантом %d), правок %d, оставлено %d, ошибок %d"
        % (res["режим"], res["bp_всего"], res["bp_с_вариантом"], len(res["правки"]), len(res["оставлены"]),
           len(res["ошибки"])))
    return res


def main():
    """Прямой запуск: `--tex-alpha` — таблица альф (вне UE); иначе ASDA_FXUB_MODE=apply — запись, по умолчанию план."""
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    if "--tex-alpha" in sys.argv:
        build_tex_alpha()
        return
    run(os.environ.get("ASDA_FXUB_MODE", "report").lower() == "apply")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
