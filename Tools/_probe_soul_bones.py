# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Проба (ТОЛЬКО ЧТЕНИЕ, headless -NullRHI): из каких костей `AAsdaLoginGameMode::RayHitsActor`
#   строит коробку пика по позе — и нет ли среди них костей-«пустышек» (dummy/якорей оружия и т. п.),
#   лежащих ДАЛЕКО от тела. Такая кость расширяет хулл в сторону, куда не тянется ни один вершинный
#   вес ⇒ хит-зона «сползает» относительно видимого силуэта (жалоба владельца 02.09.2026: клик и
#   наведение срабатывают правее силуэта). Для каждого меша печатаем:
#     • импортные границы меша (bounds) — они же примерно габарит ВИДИМОЙ геометрии в ref-позе;
#     • каждую кость ref-скелета: имя, родитель, позиция в компонентном пространстве (ref-поза),
#       и пометку ДАЛЕКО, если кость вылезает за bounds больше чем на 10 % высоты.
#   Метод: временный SkeletalMeshActor в editor-мире + `get_bone_location(..., COMPONENT)`;
#   актор удаляется, уровень не сохраняется (ничего не пишем).
# Запуск: python Tools/run_headless_safe.py Tools/_probe_soul_bones.py --marker "ПРОБА-КОСТИ ЗАВЕРШЕНА"
import unreal

DEST = "/Game/AsdaAssets/Models/Skeletal/Characters"
MESHES = ("cre_cresoulmwwoman_preview", "cre_cresoulwmman_preview",
          "cre_cresoulman_preview", "cre_cresoulwoman_preview",
          "ch1_m_cremmy_preview", "ch1_f_cremmy_preview")


def log(m):
    unreal.log("[проба-кости] " + str(m))


def probe(name):
    path = "%s/%s/SkeletalMeshes/%s" % (DEST, name, name)
    m = unreal.load_asset(path)
    if not m:
        log("%-28s МЕША НЕТ (%s)" % (name, path))
        return
    b = m.get_bounds()
    org, ext = b.origin, b.box_extent
    lo = unreal.Vector(org.x - ext.x, org.y - ext.y, org.z - ext.z)
    hi = unreal.Vector(org.x + ext.x, org.y + ext.y, org.z + ext.z)
    height = max(1.0, ext.z * 2.0)
    tol = 0.10 * height
    log("=== %s  bounds min=(%.0f %.0f %.0f) max=(%.0f %.0f %.0f) высота=%.0f допуск=%.0f"
        % (name, lo.x, lo.y, lo.z, hi.x, hi.y, hi.z, height, tol))
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0))
    if not actor:
        log("  актор не создан")
        return
    try:
        comp = actor.skeletal_mesh_component
        comp.set_skeletal_mesh_asset(m)
        n = comp.get_num_bones()
        far = 0
        # Хулл по костям (ровно то, что делает RayHitsActor: минимум/максимум по позициям костей).
        hmin = [1e18, 1e18, 1e18]
        hmax = [-1e18, -1e18, -1e18]
        for i in range(n):
            bn = comp.get_bone_name(i)
            parent = comp.get_parent_bone(bn)
            # get_bone_location в Python не экспонирован; сокет-трансформ кости в компонентном
            #   пространстве — то же самое (кость = сокет для USkinnedMeshComponent).
            p = comp.get_socket_transform(bn, unreal.RelativeTransformSpace.RTS_COMPONENT).translation
            for k, v in enumerate((p.x, p.y, p.z)):
                hmin[k] = min(hmin[k], v)
                hmax[k] = max(hmax[k], v)
            out = (p.x < lo.x - tol or p.x > hi.x + tol or
                   p.y < lo.y - tol or p.y > hi.y + tol or
                   p.z < lo.z - tol or p.z > hi.z + tol)
            mark = "  ★ ДАЛЕКО ЗА ГРАНИЦАМИ" if out else ""
            if out:
                far += 1
            log("  [%2d] %-24s <- %-16s (%9.1f %9.1f %9.1f)%s" % (i, str(bn), str(parent), p.x, p.y, p.z, mark))
        log("  ХУЛЛ ПО КОСТЯМ: min=(%.0f %.0f %.0f) max=(%.0f %.0f %.0f); костей=%d, далёких=%d"
            % (hmin[0], hmin[1], hmin[2], hmax[0], hmax[1], hmax[2], n, far))
        # Сдвиг центра хулла относительно центра границ — знак «куда сползает» коробка.
        cx = (hmin[0] + hmax[0]) * 0.5 - org.x
        cy = (hmin[1] + hmax[1]) * 0.5 - org.y
        log("  сдвиг центра хулла к центру bounds: dX=%.0f dY=%.0f (в долях высоты %.2f / %.2f)"
            % (cx, cy, cx / height, cy / height))
    finally:
        unreal.EditorLevelLibrary.destroy_actor(actor)


def main():
    for n in MESHES:
        probe(n)
    log("ПРОБА-КОСТИ ЗАВЕРШЕНА")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
