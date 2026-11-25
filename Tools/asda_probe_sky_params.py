# Copyright (c) 2026 @sitxovski (Discord)
# asda_probe_sky_params.py — ТОЛЬКО ЧТЕНИЕ. Дамп фактических значений параметров неба/облаков:
#   1) акторы AsdaSkyDomeActor / AsdaCloudShadowsActor / AsdaLayeredCloudsActor на каждом уровне;
#   2) оверрайды скаляров в MI_skydome / MI_skyhorizon карты;
#   3) дефолты мастеров M_AsdaSkyDome / M_AsdaSkyHorizon / M_AsdaCloudShadowLF.
# Нужен, чтобы перенос «постоянных» параметров в Material Instance не изменил КАРТИНКУ.
# Закон [[tools-no-import-side-effects]]: при импорте модуль ничего не делает.
import os, sys as _sys, os as _os
import unreal
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_maps as _MAPS

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
_asset = _MAPS.map_asset_name

# ★ 2026-08-27: `clouds_speed_boost` / `horizon_boost` у актора БОЛЬШЕ НЕТ — они переехали
#   в `MI_skydome.ScrollBoost` / `MI_skyhorizon.HorizonBoost` (см. asda_bake_sky_material_params).
SKY_PROPS = ("follow_camera",)
# ★ 2026-08-27: пять постоянных чисел переехали в дефолты мастера `M_AsdaCloudShadowLF`;
#   у актора осталось только пер-карточное (солнце, маска облаков, мастер).
CSA_PROPS = ()


def dump_mi(path, names):
    """Оверрайды скаляров MIC: печатаем и ЗАДАННОЕ значение, и признак наличия оверрайда."""
    if not EAL.does_asset_exist(path):
        unreal.log("[PROBE]   %s: НЕТ" % path); return
    mi = EAL.load_asset(path)
    out = []
    for n in names:
        try: v = MEL.get_material_instance_scalar_parameter_value(mi, n)
        except Exception as e: v = "ERR(%s)" % e
        out.append("%s=%s" % (n, v))
    unreal.log("[PROBE]   %s: %s" % (path.split("/")[-1], " ".join(out)))


def main():
    for m in ("/Game/Materials/M_AsdaSkyDome", "/Game/Materials/M_AsdaSkyHorizon",
              "/Game/Materials/M_AsdaCloudShadowLF", "/Game/Materials/M_AsdaLayerClouds"):
        if not EAL.does_asset_exist(m):
            unreal.log("[PROBE] МАСТЕР %s: НЕТ" % m); continue
        mat = EAL.load_asset(m)
        try:
            names = [str(x) for x in MEL.get_scalar_parameter_names(mat)]
            vals = []
            for n in names:
                try: vals.append("%s=%s" % (n, MEL.get_material_default_scalar_parameter_value(mat, n)))
                except Exception: vals.append("%s=?" % n)
            unreal.log("[PROBE] МАСТЕР %s скаляры: %s" % (m.split("/")[-1], " ".join(vals)))
            vn = [str(x) for x in MEL.get_vector_parameter_names(mat)]
            unreal.log("[PROBE] МАСТЕР %s векторы: %s" % (m.split("/")[-1], " ".join(vn)))
            tn = [str(x) for x in MEL.get_texture_parameter_names(mat)]
            unreal.log("[PROBE] МАСТЕР %s текстуры: %s" % (m.split("/")[-1], " ".join(tn)))
        except Exception as e:
            unreal.log_warning("[PROBE] МАСТЕР %s: %s" % (m, e))

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    only = os.environ.get("ASDA_SKY_MAPS", "")
    maps = _MAPS.OUTDOOR_MAPS
    if only: maps = [x for x in maps if x in only.split("||")]
    for mp in maps:
        root = "/Game/Map/%s" % _asset(mp); lvl = "%s/%s" % (root, _asset(mp))
        if not EAL.does_asset_exist(lvl):
            unreal.log("[PROBE] %s: нет уровня" % mp); continue
        les.load_level(lvl)
        n_sky = n_csa = n_lay = 0
        for a in list(eas.get_all_level_actors()):
            cls = a.get_class().get_name()
            if cls == "AsdaSkyDomeActor":
                n_sky += 1
                unreal.log("[PROBE] %s SkyDome: %s" % (mp, " ".join(
                    "%s=%s" % (p, a.get_editor_property(p)) for p in SKY_PROPS)))
            elif cls == "AsdaCloudShadowsActor":
                n_csa += 1
                tex = a.get_editor_property("cloud_texture")
                unreal.log("[PROBE] %s CloudShadows: %s tex=%s" % (mp, " ".join(
                    "%s=%s" % (p, a.get_editor_property(p)) for p in CSA_PROPS),
                    tex.get_name() if tex else None))
            elif cls == "AsdaLayeredCloudsActor":
                n_lay += 1
                unreal.log("[PROBE] %s LayeredClouds: ЕСТЬ" % mp)
        unreal.log("[PROBE] %s ИТОГ акторов: sky=%d csa=%d layered=%d" % (mp, n_sky, n_csa, n_lay))
        dump_mi(root + "/Materials/MI_skydome", ("ScrollBoost", "DomeScale"))
        dump_mi(root + "/Materials/MI_skyhorizon", ("HorizonBoost", "DomeScale"))
        unreal.log("[PROBE] %s MI_cloudshadow есть=%s" % (mp, EAL.does_asset_exist(root + "/Materials/MI_cloudshadow")))
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass
    unreal.log("=== PROBE ЗАВЕРШЕНО ===")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
