# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""Дамп ВСЕХ актор-уровневых свойств BP свечения оружия + размеров/каналов эмиттеров."""
import sys
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

CASES = ["ohs_a_co_ma_03", "ths_a_co_ma_03", "spr_a_co_ma_03", "bow_a_co_ma_03", "shd_a_co_ma_03"]
ACTOR_PROPS = ["effect_name", "effect_emitter_scale", "particle_alpha_scale", "holder_yaw_start",
               "holder_yaw_speed", "holder_pos_vel", "chiral_normal", "base_already_ue",
               "flip_quad_v", "invert_node_rot", "follow_bone_full", "strict_quad_attach",
               "follow_char_world", "follow_position", "follow_rotation", "follow_actor_yaw",
               "authored_rot", "follow_offset", "time_sync_gate", "cull_distance"]


def log(m): unreal.log_warning("[cdo] %s" % m)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for eff in CASES:
        bp = unreal.EditorAssetLibrary.load_asset("/Game/AsdaAssets/Effects/BP_Fx_%s" % eff)
        if not bp:
            log("НЕТ BP %s" % eff); continue
        cdo = unreal.get_default_object(bp.generated_class())
        log("=" * 90)
        vals = []
        for p in ACTOR_PROPS:
            try: vals.append("%s=%s" % (p, cdo.get_editor_property(p)))
            except Exception: pass
        log("%s  %s" % (eff, "  ".join(vals)))
        for em in cdo.get_editor_property("emitters"):
            nm = str(em.get_editor_property("name"))
            t = em.get_editor_property("jeb_type")
            if not em.get_editor_property("single_quad"): continue
            nds = em.get_editor_property("nodes")
            if not nds: continue
            s = []
            for i, n in enumerate(nds):
                po = n.get_editor_property("ring_pos_ofs"); bpz = n.get_editor_property("ring_base_pos")
                rt = n.get_editor_property("ring_rot")
                s.append("узел%d t=%.2f размер=(%.0f,%.0f) roll=%.0f rot=(%.0f,%.0f,%.0f) pos=(%.1f,%.1f,%.1f) base=(%.1f,%.1f,%.1f)"
                         % (i, n.get_editor_property("time"), n.get_editor_property("size_x"), n.get_editor_property("size_y"),
                            n.get_editor_property("ring_roll"), rt.x, rt.y, rt.z, po.x, po.y, po.z, bpz.x, bpz.y, bpz.z))
            log("  %-16s тип=0x%X bb=%d период=%.2f | %s" % (nm, t, em.get_editor_property("billboard_mode"),
                                                            em.get_editor_property("period"), " || ".join(s)))
    log("[cdo] ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
