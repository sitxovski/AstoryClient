# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""РЕШАЮЩИЙ ЭКСПЕРИМЕНТ: FindComponentByClass<USkeletalMeshComponent> на персонаже порта.
   У персонажа НЕСКОЛЬКО скелетных компонентов: BaseSkel (анимируется) + Equip_* (LeaderPose=BaseSkel).
   AsdaEffectActor::Tick берёт ПЕРВЫЙ попавшийся (TSet OwnedComponents — порядок не определён) и читает
   GetComponentSpaceTransforms(). Меш оружия сидит на BaseSkel. Если найден Equip_* — поза может
   отличаться ⇒ эффект и меш разъезжаются.
   Мерим: (1) какой компонент возвращает FindComponentByClass, (2) поза кости 8_a1 у лидера и у follower'а
   при АНИМИРОВАННОЙ позе."""
import sys, math
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

BASE = "/Game/AsdaAssets/Models/Skeletal/Characters/ch1_m_cremmy_preview/SkeletalMeshes/ch1_m_cremmy_preview"
PART = "/Game/AsdaAssets/Models/Skeletal/EquipParts/ch1_m_00_chst001_00_00_0/ch1_m_00_chst001_00_00_0"
BONE = "8_a1"


def log(m): unreal.log_warning("[fc] %s" % m)


def dq(t1, t2):
    d = (t1.translation - t2.translation).length()
    a, b = t1.rotation, t2.rotation
    dp = abs(a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w)
    return d, math.degrees(2*math.acos(max(0.0, min(1.0, dp))))


les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


followers = []


# анимируем лидера: любой клип персонажа
ar = unreal.AssetRegistryHelpers.get_asset_registry()


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global a, d
    try: les.new_level("/Game/Temp/_fc_probe")
    except Exception: pass
    base = unreal.EditorAssetLibrary.load_asset(BASE)
    part = unreal.EditorAssetLibrary.load_asset(PART)
    log("база=%s часть=%s" % (bool(base), bool(part)))
    a = eas.spawn_actor_from_class(unreal.Actor, unreal.Vector(0, 0, 0))
    root = a.root_component
    leader = unreal.SkeletalMeshComponent(outer=a, name="BaseSkel")
    leader.attach_to_component(root, "", unreal.AttachmentRule.KEEP_RELATIVE, unreal.AttachmentRule.KEEP_RELATIVE,
                               unreal.AttachmentRule.KEEP_RELATIVE, False)
    leader.set_skeletal_mesh_asset(base)
    leader.register_component()
    for i in range(4):
        f = unreal.SkeletalMeshComponent(outer=a, name="Equip_%d" % i)
        f.attach_to_component(root, "", unreal.AttachmentRule.KEEP_RELATIVE, unreal.AttachmentRule.KEEP_RELATIVE,
                              unreal.AttachmentRule.KEEP_RELATIVE, False)
        f.set_skeletal_mesh_asset(part or base)
        f.register_component()
        try: f.set_leader_pose_component(leader)
        except Exception as e: log("set_leader_pose_component: %s" % e)
        followers.append(f)
    found = a.get_component_by_class(unreal.SkeletalMeshComponent)
    log("FindComponentByClass вернул: %s  (BaseSkel=%s)" % (found.get_name() if found else "None", leader.get_name()))
    log("это BaseSkel? %s" % (found == leader))
    anims = [x for x in ar.get_assets_by_path("/Game/AsdaAssets/Models/Skeletal/Characters", recursive=True)
             if str(x.asset_class_path.asset_name) == "AnimSequence"]
    log("клипов найдено: %d" % len(anims))
    if anims:
        seq = anims[0].get_asset()
        leader.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
        leader.set_animation(seq)
        leader.set_position(0.35, False)
        try: leader.refresh_bone_transforms()
        except Exception: pass
        for f in followers:
            try: f.refresh_bone_transforms()
            except Exception: pass
        tl = leader.get_socket_transform(unreal.Name(BONE), unreal.RelativeTransformSpace.RTS_COMPONENT)
        log("клип=%s  лидер '%s' CS: T=(%.2f,%.2f,%.2f)" % (str(anims[0].asset_name), BONE,
            tl.translation.x, tl.translation.y, tl.translation.z))
        for f in followers[:2]:
            tf = f.get_socket_transform(unreal.Name(BONE), unreal.RelativeTransformSpace.RTS_COMPONENT)
            d, an = dq(tf, tl)
            log("  follower %-9s CS: T=(%.2f,%.2f,%.2f)  Δпоз=%.2f Δугол=%.2f°  %s"
                % (f.get_name(), tf.translation.x, tf.translation.y, tf.translation.z, d, an,
                   "РАСХОЖДЕНИЕ!" if (d > 0.5 or an > 1.0) else "совпало"))
    log("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
