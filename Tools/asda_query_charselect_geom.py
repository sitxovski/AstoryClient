# Copyright (c) 2026 @sitxovski (Discord)
# Дамп геометрии char-select: камера + кости пьедестала шкатулки + один глиф-актор.
# Цель: узнать ТОЧНУЮ ориентацию (forward камеры vs нормаль квада глифа), чтобы развернуть
# сияние-квад лицом к камере БЕЗ догадок про оси.
import unreal, math

LEVEL = "/Game/Map/Login"
CASKET = "cre_creation01"
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

def fmt_v(v): return "(%.1f, %.1f, %.1f)" % (v.x, v.y, v.z)
def fmt_r(r): return "P%.1f Y%.1f R%.1f" % (r.pitch, r.yaw, r.roll)

def fwd(rot):
    v = rot.get_forward_vector(); return v
def basis(xf):
    r = xf.rotation.rotator()
    return ("fwd(X)="+fmt_v(r.get_forward_vector()),
            "right(Y)="+fmt_v(r.get_right_vector()),
            "up(Z)="+fmt_v(r.get_up_vector()))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global xf
    les.load_level(LEVEL)
    print("=== CAMERAS / PLAYER START ===")
    for a in eas.get_all_level_actors():
        cn = a.get_class().get_name()
        if "Camera" in cn or "PlayerStart" in cn or "ViewTarget" in cn:
            xf = a.get_actor_transform()
            print("  %-22s label=%s loc=%s rot=%s" % (cn, a.get_actor_label(), fmt_v(xf.translation), fmt_r(xf.rotation.rotator())))
            for b in basis(xf): print("       ", b)
    print("=== CASKET + PEDESTAL BONES ===")
    casket = None
    for a in eas.get_all_level_actors():
        if a.get_class().get_name() != "SkeletalMeshActor": continue
        comp = a.skeletal_mesh_component; sm = comp.get_skeletal_mesh_asset() if comp else None
        if sm and CASKET in sm.get_name().lower(): casket = a; break
    if not casket:
        print("  ШКАТУЛКА НЕ НАЙДЕНА");
    else:
        comp = casket.skeletal_mesh_component
        xf = casket.get_actor_transform()
        print("  casket loc=%s rot=%s scale=%s" % (fmt_v(xf.translation), fmt_r(xf.rotation.rotator()), fmt_v(xf.scale3d)))
        for bn in ["CH_001","CH_002","CH_003"]:
            if comp.does_socket_exist(unreal.Name(bn)):
                st = comp.get_socket_transform(unreal.Name(bn), unreal.RelativeTransformSpace.RTS_WORLD)
                print("  bone %-7s loc=%s rot=%s" % (bn, fmt_v(st.translation), fmt_r(st.rotation.rotator())))
                for b in basis(st): print("        ", b)
            else:
                print("  bone %-7s ОТСУТСТВУЕТ" % bn)
    print("=== GLYPH ACTORS ===")
    for a in eas.get_all_level_actors():
        if a.get_actor_label().startswith("CharSelectGlyph"):
            xf = a.get_actor_transform()
            print("  %s loc=%s rot=%s scale=%s" % (a.get_actor_label(), fmt_v(xf.translation), fmt_r(xf.rotation.rotator()), fmt_v(xf.scale3d)))
            for b in basis(xf): print("        ", b)
    print("=== DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
