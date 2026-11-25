# Copyright (c) 2026 @sitxovski (Discord)
# read-only: устройство аним-дирижабля и attach-состояние радуг (alpen)
import sys
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal


def log(m): unreal.log_warning("[air] %s" % m)


les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ships = {}


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    les.load_level("/Game/Map/alpen/alpen")
    for a in list(eas.get_all_level_actors()):
        if not isinstance(a, unreal.AsdaEffectActor): continue
        lbl = a.get_actor_label()
        if "rainbow" not in lbl: continue
        fa = a.get_editor_property("follow_actor")
        root = a.root_component
        par = root.get_attach_parent()
        log("%s: follow_actor=%s bone=%s bFollowPos=%s bFollowRot=%s bindRot=%s authored=%s attachParent=%s socket=%s scale=%s" % (
            lbl, fa.get_actor_label() if fa else None, a.get_editor_property("follow_bone"),
            a.get_editor_property("follow_position"), a.get_editor_property("follow_rotation"),
            a.get_editor_property("follow_bind_rot"), a.get_editor_property("authored_rot"),
            par.get_owner().get_actor_label() if par else None,
            root.get_attach_socket_name() if par else None, a.get_actor_scale3d()))
        if fa: ships[fa.get_actor_label()] = fa
    for nm, s in ships.items():
        comp = s.skeletal_mesh_component
        ad = comp.get_editor_property("animation_data")
        log("SHIP %s: mesh=%s animMode=%s animData=%s compScale=%s" % (
            nm, comp.get_skeletal_mesh_asset().get_name() if comp.get_skeletal_mesh_asset() else None,
            comp.get_editor_property("animation_mode"), ad, comp.get_world_transform().scale3d))
        seq = ad.get_editor_property("anim_to_play")
        if seq:
            log("  seq=%s len=%.3f fps~%s" % (seq.get_name(), seq.get_editor_property("sequence_length"),
                                              seq.get_editor_property("target_frame_rate") if hasattr(seq, "target_frame_rate") else "?"))
            # амплитуда Bone03 по AnimPose
            try:
                sk = comp.get_skeletal_mesh_asset()
                lo = [1e18]*3; hi = [-1e18]*3
                L = float(seq.get_editor_property("sequence_length"))
                for k in range(21):
                    t = L*k/20.0
                    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(seq, t)
                    bt = unreal.AnimPoseExtensions.get_bone_pose(pose, unreal.Name("Bone03"), unreal.AnimPoseSpaces.MESH)
                    p = bt.translation
                    for j, x in enumerate((p.x, p.y, p.z)):
                        lo[j] = min(lo[j], x); hi[j] = max(hi[j], x)
                log("  Bone03 component-space амплитуда: dX=%.1f dY=%.1f dZ=%.1f (lo=%s hi=%s)" %
                    (hi[0]-lo[0], hi[1]-lo[1], hi[2]-lo[2], [round(x,1) for x in lo], [round(x,1) for x in hi]))
            except Exception as ex:
                log("  AnimPose fail: %s" % ex)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
