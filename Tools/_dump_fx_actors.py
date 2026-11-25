# Copyright (c) 2026 @sitxovski (Discord)
# _dump_fx_actors.py — ПОЛНЫЙ дамп MapEffect-акторов уровня в JSON (только чтение, БЕЗ save).
# Назначение: снять состояние уровня для диффа «до/после» (регрессия эффектов 2026-07-30).
# ASDA_MAP=<карта> (дефолт alpen), ASDA_DUMP_OUT=<путь json>.
import os, sys, json
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

MAP_NAME = os.environ.get("ASDA_MAP", "alpen")
OUT = os.environ.get("ASDA_DUMP_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/fx_dump_%s.json" % MAP_NAME)


def v3(v): return [round(v.x, 4), round(v.y, 4), round(v.z, 4)]
def v2(v): return [round(v.x, 4), round(v.y, 4)]


def dump_node(nd):
    c = nd.get_editor_property("color")
    return {
        "t": round(float(nd.get_editor_property("time")), 4),
        "rate": round(float(nd.get_editor_property("rate")), 3),
        "sx": round(float(nd.get_editor_property("size_x")), 3),
        "sy": round(float(nd.get_editor_property("size_y")), 3),
        "spd": round(float(nd.get_editor_property("speed")), 3),
        "cone": round(float(nd.get_editor_property("cone_angle")), 3),
        "alpha": round(float(nd.get_editor_property("alpha")), 4),
        "rgb": [round(c.r, 4), round(c.g, 4), round(c.b, 4)],
        "grav": round(float(nd.get_editor_property("gravity")), 4),
        "axis": v3(nd.get_editor_property("cone_axis")),
        "rScale": round(float(nd.get_editor_property("ring_scale")), 4),
        "rRoll": round(float(nd.get_editor_property("ring_roll")), 4),
        "rRot": v3(nd.get_editor_property("ring_rot")),
        "rH": round(float(nd.get_editor_property("ring_h")), 3),
        "rTop": round(float(nd.get_editor_property("ring_top_r")), 3),
        "rBot": round(float(nd.get_editor_property("ring_bot_r")), 3),
        "rU": round(float(nd.get_editor_property("ring_u")), 4),
        "rV": round(float(nd.get_editor_property("ring_v")), 4),
        "rBase": v3(nd.get_editor_property("ring_base_pos")),
        "rPos": v3(nd.get_editor_property("ring_pos_ofs")),
    }


def dump_emitter(em):
    d = {
        "name": str(em.get_editor_property("name")),
        "type": "0x%X" % em.get_editor_property("jeb_type"),
        "mat": str(em.get_editor_property("material").get_path_name()) if em.get_editor_property("material") else None,
        "matB": str(em.get_editor_property("material_b").get_path_name()) if em.get_editor_property("material_b") else None,
        "period": round(float(em.get_editor_property("period")), 4),
        "endMode": int(em.get_editor_property("end_mode")),
        "life": [round(float(em.get_editor_property("life_base")), 4), round(float(em.get_editor_property("life_var")), 4)],
        "posBase": v3(em.get_editor_property("pos_base")),
        "posRange": v3(em.get_editor_property("pos_range")),
        "spawnMode": int(em.get_editor_property("spawn_mode")),
        "singleQuad": bool(em.get_editor_property("single_quad")),
        "billboard": int(em.get_editor_property("billboard_mode")),
        "ring": bool(em.get_editor_property("ring")),
        "ringUTile": round(float(em.get_editor_property("ring_u_tile")), 4),
        "ringVA": round(float(em.get_editor_property("ring_va")), 4),
        "meshEmitter": bool(em.get_editor_property("mesh_emitter")),
        "meshVerts": len(em.get_editor_property("mesh_verts")),
        "startDelay": round(float(em.get_editor_property("start_delay")), 4),
        "nodes": [dump_node(n) for n in em.get_editor_property("nodes")],
    }
    # новые флаги могут отсутствовать в старом уровне/классе — читаем через try
    for pn in ("strict_channel_rot", "strict_quad_orbit", "strict_mesh_pos", "atlas_cols", "atlas_rows",
               "skin_bones", "flicker_alt"):
        try: d[pn] = em.get_editor_property(pn)
        except Exception: pass
    if d.get("meshVerts"):
        mv = em.get_editor_property("mesh_verts")
        d["meshV0"] = v3(mv[0]); d["meshVn"] = v3(mv[-1])   # реперные вершины (оси .jem-конверсии)
    return d


def _map_asset_name(name):
    """Имя карты → имя ассета уровня (★ ревизия 15.09: единое правило реестра карт; было сырое имя — «Mountain Flamion» промахивался)."""
    here = os.path.dirname(os.path.abspath(__file__))
    if here not in sys.path:
        sys.path.insert(0, here)
    import asda_maps
    return asda_maps.map_asset_name(name)


def main():
    _mn = _map_asset_name(MAP_NAME)
    lvl = "/Game/Map/%s/%s" % (_mn, _mn)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(lvl)
    out = {}
    for a in eas.get_all_level_actors():
        if not isinstance(a, unreal.AsdaEffectActor): continue
        lbl = a.get_actor_label()
        if not lbl.startswith("MapEffect_"): continue
        xf = a.get_actor_transform()
        r = xf.rotation.rotator()
        rec = {
            "loc": v3(xf.translation),
            "rot": [round(r.pitch, 4), round(r.yaw, 4), round(r.roll, 4)],
            "scale": v3(xf.scale3d),
            "emitters": [dump_emitter(em) for em in a.get_editor_property("emitters")],
        }
        for pn in ("chiral_normal", "follow_offset", "holder_pos_vel"):
            try: rec[pn] = v3(a.get_editor_property(pn))
            except Exception: pass
        for pn in ("holder_yaw_start", "holder_yaw_speed", "time_sync_gate", "particle_alpha_scale",
                   "effect_emitter_scale", "b_base_already_ue", "b_invert_node_rot", "b_flip_quad_v",
                   "b_follow_position", "b_follow_rotation", "b_follow_bone_full", "b_follow_char_world",
                   "b_follow_actor_yaw", "cull_distance"):
            try: rec[pn] = a.get_editor_property(pn)
            except Exception: pass
        try:
            fa = a.get_editor_property("follow_actor")
            rec["follow_actor"] = fa.get_actor_label() if fa else None
            rec["follow_bone"] = str(a.get_editor_property("follow_bone"))
            fb = a.get_editor_property("follow_bind_rot"); ar = a.get_editor_property("authored_rot")
            rec["follow_bind_rot"] = [round(fb.pitch, 4), round(fb.yaw, 4), round(fb.roll, 4)]
            rec["authored_rot"] = [round(ar.pitch, 4), round(ar.yaw, 4), round(ar.roll, 4)]
        except Exception: pass
        ap = a.root_component.get_attach_parent()
        rec["attach_parent"] = str(ap.get_owner().get_actor_label()) + ":" + str(a.root_component.get_attach_socket_name()) if ap else None
        out[lbl] = rec
    json.dump(out, open(OUT, "w", encoding="utf-8"), ensure_ascii=False, indent=1, default=str)
    unreal.log_warning("[fxdump] %s: акторов %d -> %s" % (MAP_NAME, len(out), OUT))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
