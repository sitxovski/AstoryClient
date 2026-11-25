# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _verify_fx_scale_timing.py — ПРОВЕРКА (ничего не меняет): МАСШТАБЫ и ДЛИТЕЛЬНОСТИ собранных
# эффект-BP против .jeb и против формул ЭТАЛОНА.
#
# Что сверяется по каждому эмиттеру каждого `BP_Fx_*` / `BP_Impact*` / `BP_ClickMarker` / `BP_LootDropFx`:
#   1. `period` — против эталонной формулы:
#        кольцо 0xE/4/0x15/0x1A  → clamp(lifeBase, 0.1, 10.0)   (`fxParticleSpawnRandomize@0x1208F90`)
#        квад 0xB/0xC/0x18/…     → clamp(life,     0.1, 10.0)   (`quadEmitterReset@0x12182D0` → та же)
#        частицы 0xD/3/0x1B/0x17 → period БЕЗ клампа            (`CEmitter_InitReset@0x120F0E0`)
#   2. `end_mode` — против диска (`endMode` квадов/частиц, `loopMode` колец).
#   3. `start_delay` — против rec+0x20.
#   4. РАЗМЕРЫ узлов: минимальный `size_x`/`size_y` (квады) и `ring_scale` (кольца) — ловим
#      порт-подстановки `max(size,1.0)` и `scale or 1.0`, которых у эталона нет.
#   5. СОГЛАСОВАННОСТЬ КАНАЛОВ: один и тот же `.jeb`, собранный разными BP, обязан дать
#      одинаковые period/end_mode/размеры.
#
# Запуск: UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="...\_verify_fx_scale_timing.py"
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal
import asda_jeb_parse as J

PKG = "/Game/AsdaAssets/Effects"
OUT = r"@@ASTORY_DEV_ROOT@@\Reports\fx_scale_timing_dump.tsv"

RING = (0xE, 4, 0x15, 0x1A)
QUAD = (1, 2, 0xB, 0xC, 0x13, 0x14, 0x18, 0x19)
PART = (0xD, 3, 0x1B, 0x17, 0x20)


def log(m): unreal.log_warning("[масштабы-fx] %s" % m)


def clamp(v, lo, hi): return lo if v < lo else (hi if v > hi else v)


EAL = unreal.EditorAssetLibrary
_jeb = {}
rows = []
stat = {"BP": 0, "em": 0, "no_jeb": 0}
bad_period, bad_end, sub_size, sub_scale, cross = [], [], [], [], {}



_seen = set()


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    assets = EAL.list_assets(PKG, False, False)
    for a in assets:
        name = a.split(".")[-1]
        if not (name.startswith("BP_Fx_") or name.startswith("BP_Impact")
                or name in ("BP_ClickMarker", "BP_LootDropFx")):
            continue
        obj = EAL.load_asset(a.split(".")[0])
        if obj is None or not hasattr(obj, "generated_class"): continue
        try: cdo = unreal.get_default_object(obj.generated_class())
        except Exception: continue
        try:
            eff = cdo.get_editor_property("effect_name")
            ems = cdo.get_editor_property("emitters")
        except Exception:
            continue
        if not eff: continue
        if eff not in _jeb:
            r = J.parse_effect(eff)
            _jeb[eff] = r["emitters"] if r else None
        src = _jeb[eff]
        if src is None:
            stat["no_jeb"] += 1; continue
        stat["BP"] += 1
        # ★ СОПОСТАВЛЕНИЕ ПО ИМЕНИ, а не по индексу: сборщики пропускают часть эмиттеров
        #   (`build_emitter` → None), из-за чего индексы BP и .jeb расходятся и по-индексное
        #   сравнение даёт ЛОЖНЫЕ расхождения (проверено на shd_*_ma_03: 6 эмиттеров в .jeb, 4 в BP).
        by_name = {}
        for si, se in enumerate(src): by_name.setdefault(se["name"], []).append(si)
        used = set()
        for i, e in enumerate(ems):
            enm = str(e.get_editor_property("name"))
            cand = [x for x in by_name.get(enm, []) if x not in used]
            if cand: si = cand[0]
            elif i < len(src) and src[i]["name"] == enm: si = i
            else:
                stat["unmatched"] = stat.get("unmatched", 0) + 1
                continue
            used.add(si)
            se = src[si]; t = se["type"]; p = se.get("params") or {}
            stat["em"] += 1
            per = float(e.get_editor_property("period"))
            endm = int(e.get_editor_property("end_mode"))
            sdel = float(e.get_editor_property("start_delay"))
            nodes = e.get_editor_property("nodes")
            # эталонный период
            exp_per = None; exp_end = None
            if t in RING and p.get("ring"):
                exp_per = clamp(p["life"][0], 0.1, 10.0)
                exp_end = {0: 0, 1: 1, 2: 3, 3: 3}.get(p.get("loopMode", 3), 3)
            elif t in QUAD and p.get("growQuad"):
                exp_per = clamp(p.get("life", [1.0])[0], 0.1, 10.0)
                exp_end = int(p.get("endMode", 0))
            elif t in PART:
                exp_per = max(p.get("period", 5.0), 0.05)
                exp_end = int(p.get("endMode", 3))
            if exp_per is not None and abs(per - exp_per) > 1e-3:
                bad_period.append((name, eff, se["name"], t, per, exp_per))
            if exp_end is not None and endm != exp_end:
                bad_end.append((name, eff, se["name"], t, endm, exp_end))
            # размеры узлов
            mn_sx = mn_sy = mn_rs = None
            for nd in nodes:
                sx = float(nd.get_editor_property("size_x")); sy = float(nd.get_editor_property("size_y"))
                rs = float(nd.get_editor_property("ring_scale"))
                mn_sx = sx if mn_sx is None else min(mn_sx, sx)
                mn_sy = sy if mn_sy is None else min(mn_sy, sy)
                mn_rs = rs if mn_rs is None else min(mn_rs, rs)
            # диск-минимумы для тех же каналов
            if t in QUAD and p.get("growQuad"):
                dsk = []
                for k in (0, 1):
                    dsk.append(p.get("scaleX", [0, 0, 0])[k]); dsk.append((p.get("scaleY") or p.get("scaleX", [0, 0, 0]))[k])
                for n40 in (p.get("trackNodes") or []):
                    if len(n40) >= 40: dsk += [n40[15], n40[17]]
                if dsk and min(dsk) < 1.0 and mn_sx is not None and mn_sx >= 0.999:
                    sub_size.append((name, eff, se["name"], t, round(min(dsk), 3), round(mn_sx, 3)))
            if t in RING and p.get("ring"):
                dsk = [p["scale"][0], p["scale"][1]] + [n[15] for n in (p.get("trackNodes") or []) if len(n) >= 45]
                if dsk and min(dsk) == 0.0 and mn_rs is not None and mn_rs > 0.0:
                    sub_scale.append((name, eff, se["name"], round(min(dsk), 3), round(mn_rs, 3)))
            rows.append("\t".join(str(x) for x in
                        (name, eff, si, "0x%X" % t, se["name"], round(per, 4), round(exp_per, 4) if exp_per is not None else "",
                         endm, exp_end if exp_end is not None else "", round(sdel, 4),
                         len(nodes), round(mn_sx, 3) if mn_sx is not None else "",
                         round(mn_sy, 3) if mn_sy is not None else "",
                         round(mn_rs, 3) if mn_rs is not None else "")))
            cross.setdefault((eff, si), {}).setdefault((round(per, 4), endm, round(sdel, 4), len(nodes),
                                                       round(mn_sx or 0, 3), round(mn_rs or 0, 3)), []).append(name)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("BP\tjeb\tidx\tтип\tэмиттер\tperiod\tperiod_эталон\tend_mode\tend_mode_диск\tstart_delay\tузлов\tmin_sx\tmin_sy\tmin_ring_scale\n")
        f.write("\n".join(rows))
    log("BP=%d эмиттеров=%d без .jeb=%d несопоставлено=%d → %s" % (stat["BP"], stat["em"], stat["no_jeb"], stat.get("unmatched",0), OUT))
    log("РАСХОЖДЕНИЯ period: %d" % len(bad_period))
    for x in bad_period[:20]: log("   period %s / %s / %s тип 0x%X: у нас %.4f, эталон %.4f" % x)
    log("РАСХОЖДЕНИЯ end_mode: %d" % len(bad_end))
    for x in bad_end[:40]:
        k = (x[1], x[3], x[4], x[5])
        if k in _seen: continue
        _seen.add(k)
        log("   end_mode %s / %s / %s тип 0x%X: у нас %d, диск %d" % x)
    log("ПОРТ-ПОДСТАНОВКА размера квада (диск<1 → 1.0): %d эмиттеров" % len(sub_size))
    for x in sub_size[:10]: log("   размер %s / %s / %s тип 0x%X: диск min=%.3f, у нас min=%.3f" % x)
    log("ПОРТ-ПОДСТАНОВКА scale кольца (диск 0 → 1.0): %d эмиттеров" % len(sub_scale))
    for x in sub_scale[:10]: log("   scale %s / %s / %s: диск min=%.3f, у нас min=%.3f" % x)
    nc = 0
    for (eff, i), variants in cross.items():
        if len(variants) > 1:
            nc += 1
            if nc <= 15:
                log("РАЗНОБОЙ КАНАЛОВ %s[%d]: %s" % (eff, i, " | ".join(
                    "%s ← %s" % (str(k), ",".join(v[:3])) for k, v in variants.items())))
    log("эмиттеров с РАЗНОБОЕМ между каналами: %d (из %d уникальных .jeb-эмиттеров)" % (nc, len(cross)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
