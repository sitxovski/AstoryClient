# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Проба (ТОЛЬКО ЧТЕНИЕ): почему наведение на силуэт «души» работает у одного пола и молчит у другого.
#   `AAsdaLoginGameMode::RayHitsActor` бьёт лучом по ОБЪЕДИНЁННОЙ КОРОБКЕ `UMeshComponent::Bounds`
#   актора. У скелетала эта коробка считается либо из ФИЗ-АССЕТА (если он есть), либо из импортных
#   границ меша. Значит, асимметрия может сидеть ровно здесь — в разных границах/физ-ассетах у
#   мужского и женского силуэта. Плюс печатаем корень кадра 0 у клипов: он задаёт, НАСКОЛЬКО силуэт
#   отходит от моего персонажа (перекрытие коробок = луч попадает в МОЕГО, а он в приоритете).
# Запуск: python Tools/run_headless_safe.py Tools/_probe_soul_hitbox.py --marker "ПРОБА ЗАВЕРШЕНА"
import unreal

DEST = "/Game/AsdaAssets/Models/Skeletal/Characters"


def log(m):
    unreal.log("[проба-хитбокс] " + str(m))


def mesh_info(name):
    path = "%s/%s/SkeletalMeshes/%s" % (DEST, name, name)
    m = unreal.load_asset(path)
    if not m:
        log("%-26s МЕША НЕТ (%s)" % (name, path))
        return
    try:
        b = m.get_bounds()          # FBoxSphereBounds импортных границ
        org, ext, rad = b.origin, b.box_extent, b.sphere_radius
        log("%-26s bounds origin=(%.1f %.1f %.1f) extent=(%.1f %.1f %.1f) r=%.1f"
            % (name, org.x, org.y, org.z, ext.x, ext.y, ext.z, rad))
    except Exception as e:
        log("%-26s bounds: ОШИБКА %s" % (name, e))
    try:
        pa = m.get_editor_property("physics_asset")
        if pa:
            bodies = pa.get_editor_property("skeletal_body_setups")
            log("%-26s физ-ассет=%s тел=%d" % (name, pa.get_name(), len(bodies)))
        else:
            log("%-26s физ-ассет: НЕТ (коробка = импортные границы)" % name)
    except Exception as e:
        log("%-26s физ-ассет: ОШИБКА %s" % (name, e))


def clip_root(name, idx):
    path = "%s/%s/SkeletalMeshes/%sanim_%02d" % (DEST, name, name, idx)
    a = unreal.load_asset(path)
    if not a:
        log("  %-46s КЛИПА НЕТ" % path.rsplit("/", 1)[-1])
        return
    sk = a.get_editor_property("skeleton")
    bone = None
    try:
        names = sk.get_editor_property("bone_tree")
        bone = None
    except Exception:
        bone = None
    # Корень берём по первому имени из ref-скелета меша, поэтому пробуем известные варианты.
    for cand in ("M", "G", "Bip01", "root"):
        try:
            t = unreal.AnimationLibrary.get_bone_pose_for_frame(a, cand, 0, True)
            loc = t.translation
            log("  %-46s корень '%s' = (%.2f %.2f %.2f)  длина %.2f с"
                % (path.rsplit("/", 1)[-1], cand, loc.x, loc.y, loc.z,
                   float(a.get_editor_property("sequence_length"))))
            return
        except Exception:
            continue
    log("  %-46s корень не прочитан (скелет %s)" % (path.rsplit("/", 1)[-1], sk.get_name() if sk else "?"))


def main():
    log("=== ГРАНИЦЫ И ФИЗ-АССЕТЫ (из них считается коробка RayHitsActor) ===")
    for n in ("ch1_m_cremmy_preview", "ch1_f_cremmy_preview",
              "cre_cresoulman_preview", "cre_cresoulwoman_preview",
              "cre_cresoulmwwoman_preview", "cre_cresoulwmman_preview"):
        mesh_info(n)
    log("=== КОРНИ КАДРА 0: мой персонаж (anim_00) и силуэт (anim_00/02 — стартовое состояние 2) ===")
    for n, idxs in (("ch1_m_cremmy_preview", (0, 217)),
                    ("ch1_f_cremmy_preview", (0, 212)),
                    ("cre_cresoulman_preview", (0, 2)),
                    ("cre_cresoulwoman_preview", (0, 2)),
                    ("cre_cresoulmwwoman_preview", (0, 2)),
                    ("cre_cresoulwmman_preview", (0, 2))):
        log("--- %s" % n)
        for i in idxs:
            clip_root(n, i)
    log("ПРОБА ЗАВЕРШЕНА")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
