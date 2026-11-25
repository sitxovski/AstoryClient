# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""ПРОВЕРКА (ничего не меняет): в собранных BP не осталось КОНЕЦ-узла трека.

Ожидание эталона (`skill_fx_scale.md` §3): у эмиттера С keyframe-ключами трек =
`[СТАРТ-сид @ t=0] + [ключи файла]` и БОЛЬШЕ НИЧЕГО — после последнего ключа канал ЗАМИРАЕТ
(`effectUpdateKeyframeTrack@0x11F7B10`: нет узла `курсор+1` → `return 0`).
Значит: узлов = 1 + len(ключей), а максимум размера по узлам = максимум по (СТАРТ-сид, ключи).
Эмиттеры БЕЗ ключей не трогаются (у них две точки СТАРТ→КОНЕЦ — это другая машина,
`effectTransformTick@0x12158B0`, и она верна).
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
OUT = os.path.join(r"@@ASTORY_DEV_ROOT@@", "Reports", "fx_kf_end_seed_verify.tsv")


def log(m):
    unreal.log_warning("[kf-end] %s" % m)


def main():
    srcmap = {}
    for p in glob.glob(os.path.join(SRC, "*.jeb")):
        srcmap[os.path.splitext(os.path.basename(p))[0].lower().replace("-", "_")] = p
    rows = ["BP\tэмиттер\tтип\tключей\tузлов_ожид\tузлов_факт\tmax_ожид\tmax_факт\tвердикт"]
    checked = kf = qr = bad = 0
    for p in sorted(glob.glob(os.path.join(BPDIR, "BP_Fx_*.uasset"))):
        base = os.path.splitext(os.path.basename(p))[0][len("BP_Fx_"):]
        src = srcmap.get(base.lower())
        if not src:
            continue
        try:
            jb = J.parse_jeb(open(src, "rb").read())
        except Exception:
            jb = None
        if not jb:
            continue
        bp = unreal.EditorAssetLibrary.load_asset("/Game/AsdaAssets/Effects/BP_Fx_%s" % base)
        if not bp:
            continue
        cdo = unreal.get_default_object(bp.generated_class())
        ems = cdo.get_editor_property("emitters")
        by_name = {str(e.get("name", "?")): e for e in jb.get("emitters", [])}
        for i in range(len(ems)):
            em = ems[i]
            nm = str(em.get_editor_property("name"))
            src_em = by_name.get(nm)
            if not src_em:
                continue
            pr = src_em.get("params") or {}
            ring = bool(pr.get("ring"))
            tn = [n for n in (pr.get("trackNodes") or []) if len(n) >= (45 if ring else 40)]
            checked += 1
            if not tn:
                continue
            kf += 1
            # ожидание: СТАРТ-сид + ключи
            n_exp = 1 + len(tn)
            if ring:
                exp_max = max([abs(pr["scale"][0]), abs(pr["topR"][0]), abs(pr["botR"][0]),
                               abs(pr["height"][0])]
                              + [max(abs(n[15]), abs(n[42]), abs(n[44]), abs(n[40])) for n in tn])
            else:
                sx = pr.get("scaleX", [0, 0, 0])
                sy = pr.get("scaleY") or sx
                exp_max = max([abs(sx[0]), abs(sy[0])]
                              + [max(abs(n[15]), abs(n[17])) for n in tn])
            nodes = em.get_editor_property("nodes")
            got_max = 0.0
            for n in nodes:
                for k in ("size_x", "size_y", "ring_top_r", "ring_bot_r", "ring_h", "ring_scale"):
                    try:
                        got_max = max(got_max, abs(float(n.get_editor_property(k))))
                    except Exception:
                        pass
            # ★ Критерий — только СОСТАВ трека (число узлов). Максимум размера печатаем справочно:
            #   у колец он складывается из нескольких полей узла и как признак не годится.
            QUAD = (0xB, 0xC, 0x13, 0x14, 0x18, 0x19)
            RING = (0xE, 0x15, 0x1A)
            t = int(em.get_editor_property("jeb_type"))
            if t not in QUAD and t not in RING:
                continue                      # меши/частицы строятся другой веткой — не наш критерий
            qr += 1
            ok = (len(nodes) == n_exp)
            if not ok:
                bad += 1
                rows.append("BP_Fx_%s\t%s\t0x%X\t%d\t%d\t%d\t%.1f\t%.1f\t%s"
                            % (base, nm, int(em.get_editor_property("jeb_type")), len(tn),
                               n_exp, len(nodes), exp_max, got_max, "РАСХОЖДЕНИЕ"))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(rows) + "\n")
    log("эмиттеров сверено %d, с keyframe-ключами %d, из них квадов/колец %d, РАСХОЖДЕНИЙ %d"
        % (checked, kf, qr, bad))
    log("подробности: %s" % OUT)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
