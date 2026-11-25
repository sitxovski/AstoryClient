# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""ПРОВЕРКА (ничего не меняет): собранные BP против ЗАМЕРА ЖИВОГО ОРИГИНАЛА.

Эталон (замерен в процессе оригинального клиента 2026-08-15, отчёт
`Reports/fx_pipeline_ground_truth.md`): трек keyframe-эмиттера =
    [СТАРТ-сид @ t=0 из «начальных» сидов записи]
  + [ключи файла, время клампится до duration]
  + [КОНЕЦ-сид @ t=duration из «конечных» сидов записи]
Сид-узлы строит сборщик `quadTrack_BuildSeedNodes@0x11F5E80` (0x0B), его близнецы для 0x0C/0x0E
и `animComputeKeyframeDeltas@0x1202BE0` (0x0D/0x03/0x1B) — ДО чтения ключей и заново на каждом
рестарте (первый и последний элемент списка перезаписываются, серединные ключи не трогаются).

Скрипт печатает:
  1) СТРУКТУРНУЮ сводку по всем BP: узлов ожидается 2+len(ключей), последний узел t == duration;
  2) ПОИМЕННЫЙ дамп контрольных эмиттеров (значения узлов) для сверки с замером.
Ничего не пишет в ассеты.
"""
import glob
import os
import sys

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import unreal
import asda_jeb_parse as J

SRC = os.path.join(r"@@ASTORY_DEV_ROOT@@", "Donors", "TRANSLATE_unpacked", "models_orig", "Data", "Effect", "Model")
BPDIR = os.path.join(r"@@ASTORY_DEV_ROOT@@", "AstoryClient", "Content", "AsdaAssets", "Effects")
OUT = os.path.join(r"@@ASTORY_DEV_ROOT@@", "Reports", "fx_ground_truth_verify.tsv")

# контрольные случаи: (basename BP, имя эмиттера)
CONTROL = [
    ("bali_e_ra_ma_03", "ccyl1"),
    ("bali_e_ra_ma_03", "ccyl2"),
    ("bali_e_ra_ma_03", "ccyl3"),
    ("bali_e_ra_ma_03", "ccyl4"),
    ("eff_1_char_07_runfirb04_a_00", "sprite14"),
    ("eff_1_char_07_runfirb04_a_00", "bilboard_dome12"),
]

KF_TYPES = (0x0B, 0x0C, 0x0E, 0x12, 0x18, 0x19, 0x1A, 0x1F)


def log(m):
    unreal.log_warning("[gt] %s" % m)


def em_life(p):
    lf = p.get("life")
    if isinstance(lf, (list, tuple)) and lf:
        return float(lf[0])
    return None


def main():
    srcmap = {}
    for p in glob.glob(os.path.join(SRC, "*.jeb")):
        srcmap[os.path.splitext(os.path.basename(p))[0].lower().replace("-", "_")] = p

    rows = ["BP\tэмиттер\tтип\tключей\tduration\tузлов_ожид\tузлов_факт\tt_последн\tвердикт"]
    tot = ok = bad_cnt = bad_end = 0

    for path in sorted(glob.glob(os.path.join(BPDIR, "BP_Fx_*.uasset"))):
        base = os.path.splitext(os.path.basename(path))[0][len("BP_Fx_"):]
        src = srcmap.get(base.lower())
        if not src:
            continue
        try:
            jb = J.parse_jeb(open(src, "rb").read())
        except Exception:
            continue
        if not jb:
            continue
        bp = unreal.EditorAssetLibrary.load_asset("/Game/AsdaAssets/Effects/BP_Fx_%s" % base)
        if not bp:
            continue
        cdo = unreal.get_default_object(bp.generated_class())
        try:
            ems = cdo.get_editor_property("emitters")
        except Exception:
            continue
        by_name = {}
        for e in jb.get("emitters", []):
            by_name[str(e.get("name", "?"))] = e

        for i in range(len(ems)):
            em = ems[i]
            nm = str(em.get_editor_property("name"))
            se = by_name.get(nm)
            if se is None:
                continue
            typ = int(se.get("type", 0))
            if typ not in KF_TYPES:
                continue
            p = se.get("params", {})
            keys = p.get("trackNodes") or []
            if not keys:
                continue
            life = em_life(p)
            if life is None or life <= 0:
                continue
            nodes = em.get_editor_property("nodes")
            n_exp = 2 + len(keys)
            n_act = len(nodes)
            t_last = float(nodes[n_act - 1].get_editor_property("time")) if n_act else -1.0
            verdict = "OK"
            if n_act != n_exp:
                verdict = "СЧЁТ"
                bad_cnt += 1
            elif abs(t_last - life) > 1e-3:
                verdict = "КОНЕЦ!=duration"
                bad_end += 1
            else:
                ok += 1
            tot += 1
            rows.append("%s\t%s\t0x%X\t%d\t%.4f\t%d\t%d\t%.4f\t%s" %
                        (base, nm, typ, len(keys), life, n_exp, n_act, t_last, verdict))

    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(rows) + "\n")
    log("эмиттеров с ключами проверено: %d, OK %d, неверный счёт узлов %d, последний узел != duration %d"
        % (tot, ok, bad_cnt, bad_end))
    log("TSV: %s" % OUT)

    # ---- поимённый дамп контрольных эмиттеров ----
    for bpbase, emname in CONTROL:
        bp = unreal.EditorAssetLibrary.load_asset("/Game/AsdaAssets/Effects/BP_Fx_%s" % bpbase)
        if not bp:
            log("КОНТРОЛЬ %s/%s: BP не найден" % (bpbase, emname))
            continue
        cdo = unreal.get_default_object(bp.generated_class())
        ems = cdo.get_editor_property("emitters")
        found = False
        for i in range(len(ems)):
            em = ems[i]
            if str(em.get_editor_property("name")) != emname:
                continue
            found = True
            log("КОНТРОЛЬ %s / %s : Period=%.4f EndMode=%d JebType=0x%X узлов=%d"
                % (bpbase, emname, float(em.get_editor_property("period")),
                   int(em.get_editor_property("end_mode")), int(em.get_editor_property("jeb_type")),
                   len(em.get_editor_property("nodes"))))
            for n in em.get_editor_property("nodes"):
                log("    t=%-8.4f SizeX=%-9.3f SizeY=%-9.3f Alpha=%-7.4f RingScale=%-7.3f "
                    "RingH=%-8.3f RingTopR=%-7.3f RingBotR=%-7.3f RingRoll=%-9.3f"
                    % (float(n.get_editor_property("time")), float(n.get_editor_property("size_x")),
                       float(n.get_editor_property("size_y")), float(n.get_editor_property("alpha")),
                       float(n.get_editor_property("ring_scale")), float(n.get_editor_property("ring_h")),
                       float(n.get_editor_property("ring_top_r")), float(n.get_editor_property("ring_bot_r")),
                       float(n.get_editor_property("ring_roll"))))
        if not found:
            log("КОНТРОЛЬ %s/%s: эмиттер в BP не найден" % (bpbase, emname))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
