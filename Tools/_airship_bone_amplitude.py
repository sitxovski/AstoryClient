# Copyright (c) 2026 @sitxovski (Discord)
# амплитуда Bone03 дирижаблей (component-space, шкала мира = CS*0.01) + мировые сокет-позиции
import sys
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal


def log(m): unreal.log_warning("[amp] %s" % m)


les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
seen = set()


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    les.load_level("/Game/Map/alpen/alpen")
    for a in list(eas.get_all_level_actors()):
        if not isinstance(a, unreal.AsdaEffectActor): continue
        if "rainbow" not in a.get_actor_label(): continue
        fa = a.get_editor_property("follow_actor")
        if not fa: continue
        comp = fa.skeletal_mesh_component
        seq = comp.get_editor_property("animation_data").get_editor_property("anim_to_play")
        key = fa.get_actor_label()
        sock = comp.get_socket_transform(unreal.Name("Bone03"), unreal.RelativeTransformSpace.RTS_WORLD)
        log("%s <- %s: actorLoc=%s socketLoc(t=editor)=%s" % (a.get_actor_label(), key,
            a.get_actor_location(), sock.translation))
        if key in seen or not seq: continue
        seen.add(key)
        L = float(seq.get_editor_property("sequence_length"))
        opt = unreal.AnimPoseEvaluationOptions()
        lo = [1e18]*3; hi = [-1e18]*3; z_series = []
        for k in range(41):
            t = L*k/40.0
            pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(seq, t, opt)
            bt = unreal.AnimPoseExtensions.get_bone_pose(pose, unreal.Name("Bone03"), unreal.AnimPoseSpaces.WORLD)
            p = bt.translation
            z_series.append(round(p.z*0.01, 1))
            for j, x in enumerate((p.x, p.y, p.z)):
                lo[j] = min(lo[j], x); hi[j] = max(hi[j], x)
        log("SHIP %s seq=%s len=%.0fs Bone03 мировая амплитуда: dX=%.1f dY=%.1f dZ=%.1f юн" %
            (key, seq.get_name(), L, (hi[0]-lo[0])*0.01, (hi[1]-lo[1])*0.01, (hi[2]-lo[2])*0.01))
        log("  Z-серия (мир, шаг %.1fс): %s" % (L/40.0, z_series))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
