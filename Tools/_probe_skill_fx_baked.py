# Copyright (c) 2026 @sitxovski (Discord)
# _probe_skill_fx_baked.py - ЧИТАЕТ ЗАПЕЧЁННЫЕ BP_Fx_* (второй метод против оффлайн-разбора .jeb).
#
# Печатает для каждого эффекта: флаги эмиттера (strict_*, ring_no_chiral, strict_quad_pivot) и
#   офсеты колец/квадов по узлам - ровно те числа, которые получит рантайм.
# Запуск: Tools\run_headless.bat Tools\_probe_skill_fx_baked.py
#   ASDA_FXPROBE_ONLY="имя1;имя2" - только перечисленные (иначе список ниже).
# ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal

PKG = "/Game/AsdaAssets/Effects"
DEFAULT = [
    "eff_1_char_01_fdrile01-a_00",
    "eff_1_char_03_aerob02-a_00",
    "eff_1_char_04_vshotb02-a_00",
    "eff_1_char_05_sspee01-a_00",
    "eff_1_char_06_bigmeob03-a_00",
]


def log(m):
    unreal.log_warning("[fxprobe] %s" % m)


def v3(v):
    return "(%.0f,%.0f,%.0f)" % (v.x, v.y, v.z)


def dump(eff):
    name = "BP_Fx_" + eff.replace("-", "_")
    full = "%s/%s" % (PKG, name)
    if not unreal.EditorAssetLibrary.does_asset_exist(full):
        log("НЕТ АССЕТА %s" % full)
        return
    bp = unreal.EditorAssetLibrary.load_asset(full)
    cdo = unreal.get_default_object(bp.generated_class())
    ems = cdo.get_editor_property("emitters")
    log("=== %s: эмиттеров %d ===" % (eff, len(ems)))
    for i, e in enumerate(ems):
        t = e.get_editor_property("jeb_type")
        nm = e.get_editor_property("name")
        ring = e.get_editor_property("ring")
        sq = e.get_editor_property("single_quad")
        fl = "chanRot=%d meshPos=%d quadOrbit=%d ringBase=%d quadPivot=%d noChiral=%d" % (
            e.get_editor_property("strict_channel_rot"),
            e.get_editor_property("strict_mesh_pos"),
            e.get_editor_property("strict_quad_orbit"),
            e.get_editor_property("strict_ring_base_pos"),
            e.get_editor_property("strict_quad_pivot"),
            e.get_editor_property("ring_no_chiral"))
        nodes = e.get_editor_property("nodes")
        kind = "КОЛЬЦО" if ring else ("КВАД" if sq else "частицы/меш")
        head = "  [%02d] %-16s 0x%02X %-11s узлов %2d | %s" % (i, nm, t, kind, len(nodes), fl)
        log(head)
        if not (ring or sq):
            continue
        for n in nodes:
            log("        t=%.2f base=%s pos=%s scale=%.1f botR=%.1f topR=%.1f h=%.1f "
                "rot=(%.0f,%.0f,%.0f) roll=%.0f sx=%.1f sy=%.1f"
                % (n.get_editor_property("time"),
                   v3(n.get_editor_property("ring_base_pos")),
                   v3(n.get_editor_property("ring_pos_ofs")),
                   n.get_editor_property("ring_scale"),
                   n.get_editor_property("ring_bot_r"),
                   n.get_editor_property("ring_top_r"),
                   n.get_editor_property("ring_h"),
                   n.get_editor_property("ring_rot").x,
                   n.get_editor_property("ring_rot").y,
                   n.get_editor_property("ring_rot").z,
                   n.get_editor_property("ring_roll"),
                   n.get_editor_property("size_x"),
                   n.get_editor_property("size_y")))


def main():
    only = os.environ.get("ASDA_FXPROBE_ONLY", "").strip()
    names = [x.strip() for x in only.split(";") if x.strip()] if only else DEFAULT
    for n in names:
        dump(n)
    log("ГОТОВО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
