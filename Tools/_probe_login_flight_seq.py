# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_login_flight_seq.py — ЗАМЕР секвенции LS_LoginFlight и связанных акторов уровня Login.
#   Ничего не меняет. Нужен для перевода перелёта «сцена летит → камера летит»:
#     • биндинги секвенции, их треки и ВСЕ ключи loc/rot/scale по кадрам;
#     • одинаковы ли треки шкатулки и фона (иначе у шкатулки есть собственное движение);
#     • мировые трансформы LoginCamera / LoginBackdrop_Flight / Anchor / шкатулки в уровне.
# Выход: ASDA_PROBE_OUT (умолч. Reports/login_camflight/flight_seq.json)
import os, io, json
import unreal

OUT = os.environ.get("ASDA_PROBE_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/login_camflight/flight_seq.json")
SEQ = "/Game/AsdaAssets/Cinematics/LS_LoginFlight"
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def log(m):
    unreal.log_warning("[probe-flight] %s" % m)


def xf(a):
    t = a.get_actor_transform()
    r = t.rotation.rotator()
    q = t.rotation
    return {"loc": [t.translation.x, t.translation.y, t.translation.z],
            "rot_pyr": [r.pitch, r.yaw, r.roll], "quat_xyzw": [q.x, q.y, q.z, q.w],
            "scale": [t.scale3d.x, t.scale3d.y, t.scale3d.z]}


def main():
    assert les.load_level("/Game/Map/Login")
    res = {"actors": {}, "bindings": []}
    for a in eas.get_all_level_actors():
        lbl = a.get_actor_label()
        if lbl in ("LoginCamera", "LoginBackdrop_Flight", "LoginBackdrop_Anchor", "LoginCasket_cre_creation01",
                   "LoginBackdropAnimator", "Sun_Login", "Sky_Login", "PlayerStart") or lbl.startswith("CharSelect"):
            d = xf(a)
            par = a.get_attach_parent_actor()
            d["parent"] = par.get_actor_label() if par else None
            if isinstance(a, unreal.CameraActor):
                cc = a.camera_component
                d["fov"] = float(cc.get_editor_property("field_of_view"))
                d["auto_activate"] = str(a.get_editor_property("auto_activate_for_player"))
            res["actors"][lbl] = d
    seq = unreal.load_asset(SEQ)
    assert seq, "нет " + SEQ
    res["display_rate"] = str(seq.get_display_rate())
    res["playback"] = [seq.get_playback_start(), seq.get_playback_end()]
    world = unreal.EditorLevelLibrary.get_editor_world()
    for b in seq.get_bindings():
        bd = {"name": str(b.get_display_name()), "id": str(b.get_id()), "tracks": []}
        try:
            bound = unreal.LevelSequenceEditorBlueprintLibrary.get_bound_objects(b.get_id()) if False else []
        except Exception:
            bound = []
        for tr in b.get_tracks():
            td = {"class": tr.get_class().get_name(), "sections": []}
            for sec in tr.get_sections():
                sd = {"range": [sec.get_start_frame(), sec.get_end_frame()],
                      "completion": str(sec.get_completion_mode()) if hasattr(sec, "get_completion_mode") else "?",
                      "channels": {}}
                for ch in unreal.MovieSceneSectionExtensions.get_all_channels(sec):
                    keys = ch.get_keys()
                    sd["channels"][ch.get_name()] = [[k.get_time().frame_number.value, float(k.get_value())] for k in keys]
                td["sections"].append(sd)
            bd["tracks"].append(td)
        res["bindings"].append(bd)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    io.open(OUT, "w", encoding="utf-8").write(json.dumps(res, ensure_ascii=False, indent=1))
    log("записано %s; биндингов %d" % (OUT, len(res["bindings"])))
    log("PROBE-FLIGHT-FINISHED")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
