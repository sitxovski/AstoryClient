# Copyright (c) 2026 @sitxovski (Discord)
# asda_add_volumetric_clouds.py — ОБЪЁМНЫЕ ОБЛАКА per map (просьба юзера 2026-07-28, современный рендер):
#   UE VolumetricCloud + MIC движкового m_SimpleVolumetricCloud с Cloud_AlbedoColor из ГЛАВНОГО ЦВЕТА
#   НЕБА карты (среднее её SKY_DOME_CLOUDE1, посчитано офлайн). Всё Editor-Friendly: обычные акторы,
#   параметры в MIC (MI_volclouds), высоты/покрытие на компоненте.
#   Купол неба поднимается scale 100→1000 (углы не меняются — вид тот же), чтобы облака (1.2..2.4 км)
#   поместились ПОД непрозрачным куполом (иначе dome на 1 км их полностью загораживал).
#   + SkyAtmosphere (скрыт за куполом, но освещает облака) + atmosphere_sun_light на солнце.
# env: ASDA_SKY_MAPS фильтр "a||b" (как раскатка неба); порциями — 18 уровней в одной сессии = OOM.
import os, unreal
import sys as _sys, os as _os
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_maps as _MAPS
import asda_login_guard as _LG   # ★ 16.09.2026 (обратный обход savedir14): единый гейт сцены логина

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AT  = unreal.AssetToolsHelpers.get_asset_tools()
ENGINE_CLOUD_MAT = "/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud"

# главный цвет неба (среднее RGB SKY_DOME_CLOUDE1.png карты)
SKY_COLORS = _MAPS.SKY_COLORS     # ★ единая таблица цвета неба (было 3 дословные копии), ключи = OUTDOOR_MAPS
DOME_SCALE = 1000.0

_asset = _MAPS.map_asset_name          # ★ единый санитайзер имени карты

def process(mp):
    root = "/Game/Map/%s" % _asset(mp)
    lvl = "%s/%s" % (root, _asset(mp))
    # ★ 16.09.2026 (обратный обход ревизии savedir14). Инструмент грузит уровень, спавнит акторы, пишет MIC
    #   под корнем карты и СОХРАНЯЕТ уровень — гейт сцены логина ДО load_level и до первой записи.
    #   Проверяется КОРЕНЬ карты (у сцены логина он же и есть её уровень со светом владельца).
    if _LG.login_map_write_blocked(root, "asda_add_volumetric_clouds.py", quiet=True):
        return
    if not EAL.does_asset_exist(lvl):
        unreal.log("[VCL] %s: нет уровня — пропуск" % mp); return
    col = SKY_COLORS.get(mp)
    if col is None:
        unreal.log("[VCL] %s: нет цвета неба — пропуск" % mp); return
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(lvl)
    # 1) MIC облаков: альбедо = лифт главного цвета (0.4 + 0.6·c — облака остаются «облачными», но в тоне карты)
    nm = "MI_volclouds"; mp_path = root + "/Materials/" + nm
    mic = EAL.load_asset(mp_path) if EAL.does_asset_exist(mp_path) else \
          AT.create_asset(nm, root + "/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mic, EAL.load_asset(ENGINE_CLOUD_MAT))
    alb = [0.4 + 0.6*c for c in col]
    MEL.set_material_instance_vector_parameter_value(mic, "Cloud_AlbedoColor", unreal.LinearColor(alb[0], alb[1], alb[2], 1.0))
    MEL.set_material_instance_scalar_parameter_value(mic, "Cloud_GlobalCoverage", 0.42)   # реже = отдельные кучевые
    MEL.set_material_instance_scalar_parameter_value(mic, "Cloud_GlobalDensity", 1.3)    # плотнее = тёмные низы, читается на светлом куполе
    EAL.save_loaded_asset(mic)
    # 2) акторы уровня
    sun = None
    for a in list(eas.get_all_level_actors()):
        cls = a.get_class().get_name()
        if cls == "VolumetricCloud":
            eas.destroy_actor(a)
        elif cls == "AsdaSkyDomeActor":
            a.set_actor_scale3d(unreal.Vector(DOME_SCALE, DOME_SCALE, DOME_SCALE))
        elif cls == "DirectionalLight":
            sun = a
    for mi_name in ("MI_skydome", "MI_skyhorizon"):
        p = root + "/Materials/" + mi_name
        if EAL.does_asset_exist(p):
            mi = EAL.load_asset(p)
            MEL.set_material_instance_scalar_parameter_value(mi, "DomeScale", DOME_SCALE)
            EAL.save_loaded_asset(mi)
    vc = eas.spawn_actor_from_class(unreal.VolumetricCloud, unreal.Vector(0, 0, 0))
    vc.set_actor_label("VolumetricClouds")
    comp = vc.get_component_by_class(unreal.VolumetricCloudComponent.static_class())
    comp.set_editor_property("layer_bottom_altitude", 0.9)   # км: под куполом (зенит купола 2.85 км при scale 1000)
    comp.set_editor_property("layer_height", 0.9)
    comp.set_editor_property("material", mic)
    # 3) освещение облаков: солнце как атмосферное + SkyAtmosphere (за куполом не виден, но нужен свету)
    if sun:
        try:
            sun.light_component.set_editor_property("atmosphere_sun_light", True)
        except Exception as e:
            unreal.log_warning("[VCL] %s: atmosphere_sun_light: %s" % (mp, e))
    if not any(a.get_class().get_name() == "SkyAtmosphere" for a in eas.get_all_level_actors()):
        sa = eas.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
        sa.set_actor_label("SkyAtmosphere_ForClouds")
    les.save_current_level()
    unreal.log("[VCL] %s: облака alb=(%.2f,%.2f,%.2f), купол scale=%d" % (mp, alb[0], alb[1], alb[2], DOME_SCALE))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global e, mp
    MAPS = _MAPS.select_maps(_MAPS.SKY_MAPS, env="ASDA_SKY_MAPS")   # фильтр "a||b" — порционная раскатка
    for mp in MAPS:
        try: process(mp)
        except Exception as e: unreal.log_error("[VCL] %s FAIL: %s" % (mp, e))
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass
    unreal.log("=== VCL ЗАВЕРШЕНО ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
