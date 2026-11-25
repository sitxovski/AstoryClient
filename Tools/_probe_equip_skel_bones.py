# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""ДОКАЗАТЕЛЬСТВО: скелетные компоненты частей экипа (Equip_*, LeaderPose=BaseSkel) имеют СВОЙ
   набор костей — индекс/поза кости 8_a1 у них НЕ совпадает с телом. Если эффект получает такой
   компонент через FindComponentByClass, он садится на ЧУЖУЮ кость в РЕФ-позе → «наискось».
"""
import sys, math
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

BASE = "/Game/AsdaAssets/Models/Skeletal/Characters/ch1_m_cremmy_preview/SkeletalMeshes/ch1_m_cremmy_preview"
WANT = ("8_a1", "_8_a1", "_7_a1", "M")


def log(m): unreal.log_warning("[equip] %s" % m)


def bones_of(comp):
    out = []
    i = 0
    while i < 400:
        n = str(comp.get_bone_name(i))
        if n in ("None", ""): break
        out.append(n); i += 1
    return out


les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

ar = unreal.AssetRegistryHelpers.get_asset_registry()

base_cs = {}


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    try: les.new_level("/Game/Temp/_equip_probe")
    except Exception: pass
    assets = ar.get_assets_by_path("/Game/AsdaAssets/Models/Skeletal/EquipParts", recursive=True)
    sk_assets = [a for a in assets if str(a.asset_class_path.asset_name) == "SkeletalMesh"]
    log("частей экипа (SkeletalMesh) в проекте: %d" % len(sk_assets))
    base = unreal.EditorAssetLibrary.load_asset(BASE)
    a0 = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0))
    c0 = a0.skeletal_mesh_component
    c0.set_skeletal_mesh_asset(base)
    bb = bones_of(c0)
    log("ТЕЛО %s: костей=%d  индексы: %s" % (BASE.rsplit("/", 1)[-1], len(bb),
        ", ".join("%s=%d" % (w, bb.index(w)) if w in bb else "%s=НЕТ" % w for w in WANT)))
    for w in WANT:
        if w in bb:
            t = c0.get_socket_transform(unreal.Name(w), unreal.RelativeTransformSpace.RTS_COMPONENT)
            base_cs[w] = t
    shown = 0
    for a in sk_assets:
        if shown >= 12: break
        sm = a.get_asset()
        if not sm: continue
        act = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0))
        c = act.skeletal_mesh_component
        c.set_skeletal_mesh_asset(sm)
        bn = bones_of(c)
        idx = ["%s=%s" % (w, (bn.index(w) if w in bn else "НЕТ")) for w in WANT]
        diff = []
        for w in WANT:
            if w in bn and w in base_cs:
                t = c.get_socket_transform(unreal.Name(w), unreal.RelativeTransformSpace.RTS_COMPONENT)
                d = (t.translation - base_cs[w].translation).length()
                q1, q2 = t.rotation, base_cs[w].rotation
                dotq = abs(q1.x*q2.x + q1.y*q2.y + q1.z*q2.z + q1.w*q2.w)
                ang = math.degrees(2*math.acos(max(0.0, min(1.0, dotq))))
                diff.append("%s Δпоз=%.1f Δугол=%.1f°" % (w, d, ang))
        log("  %-34s костей=%-3d %s | %s" % (str(a.asset_name), len(bn), " ".join(idx), "; ".join(diff) or "—"))
        eas.destroy_actor(act)
        shown += 1
    # какой компонент вернёт FindComponentByClass, если на акторе несколько скелеталок
    act = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(200, 0, 0))
    log("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
