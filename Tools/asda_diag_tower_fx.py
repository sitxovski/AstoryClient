# Copyright (c) 2026 @sitxovski (Discord)
# asda_diag_tower_fx.py — дамп tower-эффекта на alpen: трансформ актора, follow-поля, эмиттеры.
import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    ok = les.load_level("/Game/Map/alpen/alpen")
    unreal.log("load_level=%s" % ok)
    alleff = [a for a in eas.get_all_level_actors() if a.get_actor_label().startswith("MapEffect")]
    unreal.log("MapEffect-акторов: %d" % len(alleff))
    for a in alleff[:5]: unreal.log("  например: %s" % a.get_actor_label())
    for a in alleff:
        if "tower" not in a.get_actor_label().lower(): continue
        unreal.log("ACTOR %s cls=%s" % (a.get_actor_label(), a.get_class().get_name()))
        unreal.log("  loc=%s rot=%s scale=%s" % (a.get_actor_location(), a.get_actor_rotation(), a.get_actor_scale3d()))
        for p in ("follow_actor","follow_bone","follow_bind_rot","authored_rot","follow_position","follow_rotation","base_already_ue","effect_emitter_scale","holder_yaw_start","holder_yaw_speed","follow_offset"):
            try: unreal.log("  %s = %s" % (p, a.get_editor_property(p)))
            except Exception as e: unreal.log("  %s = ERR %s" % (p, e))
        # мировая ось конуса: локальная +Y (game) → RX(89)/RY(31) → UE(-z,x,y) → поворот актора
        import math
        def rx(v,a): c,s=math.cos(a),math.sin(a); return (v[0], v[1]*c+v[2]*s, -v[1]*s+v[2]*c)
        def ry(v,a): c,s=math.cos(a),math.sin(a); return (v[0]*c-v[2]*s, v[1], v[0]*s+v[2]*c)
        ax = ry(rx((0,1,0), math.radians(89)), math.radians(31))
        ue = unreal.Vector(-ax[2], ax[0], ax[1])
        q = a.get_actor_rotation().quaternion()
        world = q.rotate_vector(ue)
        unreal.log("  ОСЬ конуса: game=%s ue=%s WORLD=%s" % (ax, ue, world))
        try:
            for i, em in enumerate(a.get_editor_property("emitters")):
                unreal.log("  em%d %s type=0x%X ring=%s quad=%s nodes=%d" % (
                    i, em.get_editor_property("name"), em.get_editor_property("jeb_type"),
                    em.get_editor_property("ring"), em.get_editor_property("single_quad"),
                    len(em.get_editor_property("nodes"))))
                for n in em.get_editor_property("nodes")[:3]:
                    unreal.log("     node t=%.2f rot=%s posOfs=%s basePos=%s sizeX=%.1f sizeY=%.1f scale=%.2f h=%.1f alpha=%.2f" % (
                        n.get_editor_property("time"), n.get_editor_property("ring_rot"),
                        n.get_editor_property("ring_pos_ofs"), n.get_editor_property("ring_base_pos"),
                        n.get_editor_property("size_x"), n.get_editor_property("size_y"),
                        n.get_editor_property("ring_scale"), n.get_editor_property("ring_h"),
                        n.get_editor_property("alpha")))
        except Exception as e:
            unreal.log_warning("emitters err: %s" % e)
    # зеркальность носителя: scale актора башни + мировой scale сокета ring
    for a in eas.get_all_level_actors():
        if a.get_actor_label() == "" or a.get_class().get_name() != "SkeletalMeshActor": continue
        comp = a.skeletal_mesh_component
        if comp and comp.does_socket_exist(unreal.Name("ring")):
            xf = comp.get_socket_transform(unreal.Name("ring"), unreal.RelativeTransformSpace.RTS_WORLD)
            unreal.log("НОСИТЕЛЬ %s: actorScale=%s | socket 'ring' worldScale=%s loc=%s" % (
                a.get_actor_label(), a.get_actor_scale3d(), xf.scale3d, xf.translation))
    unreal.log("=== TOWER DIAG DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
