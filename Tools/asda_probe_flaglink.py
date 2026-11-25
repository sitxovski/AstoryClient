# Copyright (c) 2026 @sitxovski (Discord)
# asda_probe_flaglink.py — ДИАГНОСТИКА: component-space ref-поза кости Flag_Link у транспорта.
# Нужна, чтобы ПРОВЕРИТЬ (не угадать) базис конверсии D3D→UE внутри скелетала и вычислить константу
# поворота крепления флага клана (эталон mapobj_updateFlagLinkAnim@0xFB0CB0:
#   D3DXMatrixRotationYawPitchRoll(π, 0, π/2) в осях оригинала).
# Эталонное значение из .jbb (frame 0, D3D, колоночная запись):
#   R = [[0,1,0],[0.9816,0,-0.1908],[-0.1908,0,-0.9816]],  t = (0, 95.5147, 28.9615)
# Запуск: run_headless.bat asda_probe_flaglink.py
import unreal

MESH = "/Game/AsdaAssets/Models/Skeletal/Vehicle/itm1_n_96_vehicle_0000/SkeletalMeshes/itm1_n_96_vehicle_0000"
BONES = ["Flag_Link", "Bone01", "Bone02"]

world = unreal.EditorLevelLibrary.get_editor_world()


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    m = unreal.load_asset(MESH)
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0))
    comp = actor.skeletal_mesh_component
    comp.set_skeletal_mesh(m)
    for bn in BONES:
        t = comp.get_socket_transform(unreal.Name(bn), unreal.RelativeTransformSpace.RTS_COMPONENT)
        tr = t.translation
        q = t.rotation
        # оси кости в component-space
        ax = q.rotate_vector(unreal.Vector(1, 0, 0))
        ay = q.rotate_vector(unreal.Vector(0, 1, 0))
        az = q.rotate_vector(unreal.Vector(0, 0, 1))
        unreal.log("[probe] %s T=(%.4f %.4f %.4f) X=(%.4f %.4f %.4f) Y=(%.4f %.4f %.4f) Z=(%.4f %.4f %.4f) S=(%.3f %.3f %.3f)"
                   % (bn, tr.x, tr.y, tr.z, ax.x, ax.y, ax.z, ay.x, ay.y, ay.z, az.x, az.y, az.z,
                      t.scale3d.x, t.scale3d.y, t.scale3d.z))
    unreal.EditorLevelLibrary.destroy_actor(actor)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
