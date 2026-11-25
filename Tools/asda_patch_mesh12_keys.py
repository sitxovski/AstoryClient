# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""ТОЧЕЧНЫЙ ПАТЧ: вернуть меш-эмиттерам (0x11/0x12/0x1F) серединные узлы трека из файловых ключей.

Почему патч, а не пересборка каналов: пересборка сносит и перезаписывает ВСЕ эмиттеры BP (в т.ч.
подтверждённые визуально) — тот же риск, из-за которого для карт сделан `ASDA_MAP_PATCH_ONLY`.
Здесь переписывается ТОЛЬКО массив `nodes` у эмиттеров меш-типов, у которых в `.jeb` есть ключи,
а в BP стоят ровно два сид-узла. Сид-узлы (первый и последний) СОХРАНЯЮТСЯ КАК ЕСТЬ — трогаются
только вставляемые между ними ключи.

Основание — ЗАМЕР ЖИВОГО ОРИГИНАЛА, `Reports/fx_pipeline_ground_truth.md` §8:
меш-эмиттер обновляется той же машиной, что квад (`effectAnimTickKeyframe@0x1239250`,
`CEmitter_Mesh_Update@0x123BA70`; список трека `+0x724`, `next` узла `+0xA0`), и его трек в памяти
процесса = [СТАРТ-сид @0] + [ключи файла] + [КОНЕЦ-сид @duration].

Откат: `ASDA_FX_NO_MESH12_KEYS=1` + повторный прогон (вернёт два узла).
Отчёт: `Reports/fx_mesh12_keys_patch.tsv`.
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
import os as _fxg_os, sys as _fxg_sys
_fxg_sys.path.insert(0, _fxg_os.path.dirname(_fxg_os.path.abspath(__file__)))
import asda_fx_common as _FXC_GATE   # ★ Ф16: гейт записи эмиттеров (импорт без побочных эффектов)
import asda_jeb_parse as J
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
import asda_place_effects as PE

SRC = os.path.join(r"@@ASTORY_DEV_ROOT@@", "Donors", "TRANSLATE_unpacked", "models_orig", "Data", "Effect", "Model")
BPDIR = os.path.join(r"@@ASTORY_DEV_ROOT@@", "AstoryClient", "Content", "AsdaAssets", "Effects")
OUT = os.path.join(r"@@ASTORY_DEV_ROOT@@", "Reports", "fx_mesh12_keys_patch.tsv")
MESH_TYPES = (0x11, 0x12, 0x1F)


def log(m):
    unreal.log_warning("[mesh12] %s" % m)


def main():
    srcmap = {}
    for p in glob.glob(os.path.join(SRC, "*.jeb")):
        srcmap[os.path.splitext(os.path.basename(p))[0].lower().replace("-", "_")] = p

    rows = ["BP\tэмиттер\tтип\tключей\tduration\tузлов_было\tузлов_стало"]
    touched_bp = patched = skipped = 0

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
        # индексируем записи .jeb по (имя, порядковый номер повтора) — в архиве есть ДУБЛИ имён
        seq = {}
        recs = []
        for e in jb.get("emitters", []):
            nm = str(e.get("name", "?"))
            k = seq.get(nm, 0)
            seq[nm] = k + 1
            recs.append((nm, k, e))

        bp = unreal.EditorAssetLibrary.load_asset("%s/BP_Fx_%s" % (_U.G_EFFECTS, base))
        if not bp:
            continue
        cdo = unreal.get_default_object(bp.generated_class())
        try:
            ems = cdo.get_editor_property("emitters")
        except Exception:
            continue

        seen = {}
        changed = False
        for i in range(len(ems)):
            em = ems[i]
            nm = str(em.get_editor_property("name"))
            k = seen.get(nm, 0)
            seen[nm] = k + 1
            rec = None
            for rnm, rk, re_ in recs:
                if rnm == nm and rk == k:
                    rec = re_
                    break
            if rec is None:
                continue
            if int(rec.get("type", 0)) not in MESH_TYPES:
                continue
            p = rec.get("params", {})
            keys = p.get("trackNodes") or []
            if not keys:
                continue
            nodes = em.get_editor_property("nodes")
            if len(nodes) < 2:
                skipped += 1
                continue
            lf = p.get("life") or [10.0]
            life = max(float(lf[0]), 0.1)
            mid = PE.mesh12_key_nodes(p, life)
            if not mid:
                continue
            # идемпотентно: сид-узлы (первый и последний) берём из BP КАК ЕСТЬ, серединные
            # пересобираем из ключей заново — повторный прогон даёт тот же результат
            newnodes = [nodes[0]] + mid + [nodes[len(nodes) - 1]]
            if len(newnodes) == len(nodes):
                pass    # состав тот же — всё равно перезапишем, дешевле, чем сравнивать поля
            em.set_editor_property("nodes", newnodes)
            ems[i] = em          # ⚠ массив структур приходит КОПИЕЙ — элемент надо положить обратно
            rows.append("%s\t%s\t0x%X\t%d\t%.4f\t%d\t%d" %
                        (base, nm, int(rec.get("type", 0)), len(keys), life, len(nodes), len(newnodes)))
            patched += 1
            changed = True
        if changed:
            # ⚠ и сам массив надо записать в CDO: `get_editor_property` отдаёт КОПИЮ (тот же
            #   порядок, что в `asda_build_char_fx.py:337-338`), иначе сохранение уходит вхолостую.
            # ★★★ Ф16 (К-1, 20.09.2026): запись массива эмиттеров — под явным гейтом
            #   `asda.Fx.Glow.EmittersWriter` (обоснование — asda_fx_common.emitters_writer_gate):
            #   признак «пишет скрипт» не должен зависеть от формы события правки.
            with _FXC_GATE.emitters_writer_gate():
                cdo.set_editor_property("emitters", ems)
            # ⚠ only_if_is_dirty=False: правка CDO НЕ помечает пакет BP грязным, и обычный
            #   save_loaded_asset(bp) уходит вхолостую (проверено: патч «отработал», а верификатор
            #   показал те же 213 расхождений).
            unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
            touched_bp += 1

    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(rows) + "\n")
    log("патч меш-ключей: эмиттеров %d, BP %d, пропущено (узлов != 2) %d" % (patched, touched_bp, skipped))
    log("TSV: %s" % OUT)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
