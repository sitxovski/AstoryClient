# Copyright (c) 2026 @sitxovski (Discord)
# _dump_wing_bp.py — ДИАГНОСТИКА (круг 2 блёсток крыльев): что РЕАЛЬНО лежит в wing-BP CDO.
# Печатает по каждому эмиттеру: jeb_type, ветки (mesh/ring/single_quad), nodes (time/size/color/rate),
# pos_base/pos_range, life, atlas, spawn_mode. Запуск: run_headless.bat <этот файл>.
import unreal

def log(m): unreal.log_warning("[wingdump] %s" % m)

if __name__ in ("__main__", "builtins"):   # main-guard: закон tools-no-import-side-effects
    for bp in ("BP_Fx_eff_wing_m_ss_00", "BP_Fx_eff_wing_f_ss_00", "BP_Fx_eff_wing_m_big_00"):
        path = "/Game/AsdaAssets/Effects/%s.%s_C" % (bp, bp)
        cls = unreal.load_class(None, path)
        if not cls:
            log("НЕТ класса %s" % path); continue
        cdo = unreal.get_default_object(cls)
        ems = cdo.get_editor_property("emitters")
        log("=== %s: emitters=%d effect_name=%s" % (bp, len(ems), cdo.get_editor_property("effect_name")))
        for i, em in enumerate(ems):
            jt = em.get_editor_property("jeb_type")
            nodes = em.get_editor_property("nodes")
            log(" [%d] name=%s jeb_type=0x%X mesh=%s ring=%s squad=%s spawn_mode=%s life=%.2f atlas=%dx%d" % (
                i, em.get_editor_property("name"), jt,
                em.get_editor_property("mesh_emitter"), em.get_editor_property("ring"),
                em.get_editor_property("single_quad"), em.get_editor_property("spawn_mode"),
                em.get_editor_property("life_base"),
                em.get_editor_property("atlas_cols"), em.get_editor_property("atlas_rows")))
            pb = em.get_editor_property("pos_base"); pr = em.get_editor_property("pos_range")
            log("      pos_base=(%.1f,%.1f,%.1f) pos_range=(%.1f,%.1f,%.1f) period=%.2f" % (
                pb.x, pb.y, pb.z, pr.x, pr.y, pr.z, em.get_editor_property("period")))
            for nd in nodes:
                c = nd.get_editor_property("color"); ax = nd.get_editor_property("cone_axis")
                log("      node t=%.2f rate=%.1f size=(%.1f,%.1f) speed=%.1f cone=%.1f alpha=%.2f rgb=(%.2f,%.2f,%.2f) grav=%.1f axis=(%.1f,%.1f,%.1f)" % (
                    nd.get_editor_property("time"), nd.get_editor_property("rate"),
                    nd.get_editor_property("size_x"), nd.get_editor_property("size_y"),
                    nd.get_editor_property("speed"), nd.get_editor_property("cone_angle"),
                    nd.get_editor_property("alpha"), c.r, c.g, c.b,
                    nd.get_editor_property("gravity"), ax.x, ax.y, ax.z))
    log("КОНЕЦ ДИАГНОСТИКИ")
