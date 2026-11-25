# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_fx_rollout_scope.py — ОХВАТ ПО ЗАПЕЧЁННЫМ BP_Fx_* (не по .jeb): сколько эффектов реально
#   страдает от расхождения контракта офсетов, и у скольких взведён новый признак
#   `ring_etalon_layout` (регресс-проверка «дефолт = как было»).
#
#   Печатает сводку и список эффектов с ненулевым офсетом кольца/квада.
#   Запуск: Tools\run_headless.bat Tools\_probe_fx_rollout_scope.py
# ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal

PKG = "/Game/AsdaAssets/Effects"
RING_TYPES = (0xE, 4, 0x15, 0x1A)


def log(m):
    unreal.log_warning("[охват] %s" % m)


def nz(v):
    return abs(v.x) + abs(v.y) + abs(v.z) > 1e-3


def main():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = ar.get_assets_by_path(PKG, recursive=False)
    names = sorted(str(a.asset_name) for a in assets if str(a.asset_name).startswith("BP_Fx_"))
    log("BP_Fx_* найдено: %d" % len(names))

    tot_em = 0
    rings = 0
    rings_ofs = 0          # кольцо с НЕнулевым офсетом в ассете
    rings_zero_flagless = 0  # кольцо с НУЛЕВЫМ офсетом и без нового признака (кандидат «потерян офсет»)
    quads = 0
    quads_ofs = 0
    flagged_eff = set()
    flagged_em = 0
    eff_with_ring_ofs = []
    for n in names:
        full = "%s/%s" % (PKG, n)
        try:
            bp = unreal.EditorAssetLibrary.load_asset(full)
            cdo = unreal.get_default_object(bp.generated_class())
            ems = cdo.get_editor_property("emitters")
        except Exception as ex:
            log("НЕ ПРОЧИТАН %s: %s" % (n, ex))
            continue
        has_ring_ofs = False
        for e in ems:
            tot_em += 1
            t = e.get_editor_property("jeb_type")
            try:
                fl = bool(e.get_editor_property("ring_etalon_layout"))
            except Exception:
                fl = False
            if fl:
                flagged_em += 1
                flagged_eff.add(n)
            nodes = e.get_editor_property("nodes")
            ofs = any(nz(nd.get_editor_property("ring_base_pos")) or
                      nz(nd.get_editor_property("ring_pos_ofs")) for nd in nodes)
            if e.get_editor_property("ring"):
                rings += 1
                if ofs:
                    rings_ofs += 1
                    has_ring_ofs = True
                elif not fl:
                    rings_zero_flagless += 1
            elif e.get_editor_property("single_quad"):
                quads += 1
                if ofs:
                    quads_ofs += 1
        if has_ring_ofs:
            eff_with_ring_ofs.append(n)

    log("эмиттеров всего: %d | колец %d (с офсетом в ассете %d) | квадов %d (с офсетом %d)"
        % (tot_em, rings, rings_ofs, quads, quads_ofs))
    log("колец с НУЛЕВЫМ офсетом и БЕЗ признака ring_etalon_layout: %d" % rings_zero_flagless)
    log("признак ring_etalon_layout взведён: эффектов %d, эмиттеров %d"
        % (len(flagged_eff), flagged_em))
    log("с признаком: %s" % ", ".join(sorted(flagged_eff)))
    log("эффектов с ненулевым офсетом кольца: %d" % len(eff_with_ring_ofs))
    log("ГОТОВО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
