# Copyright (c) 2026 @sitxovski (Discord)
# ★★★ УСТАРЕЛО (2026-09-03, правка Д-1 аудита света): скрипт относится к СТАРОЙ unlit-модели
#   (per-map LightColor/LightDir/Ambient в MIC) и берёт направление света из .mapraw 'lightDir' —
#   вектор НЕВЕРНОЙ формулы (ошибка 30..90° на всех картах). Lit-мастера этих параметров не читают.
#   НЕ ЗАПУСКАТЬ. Актуальный свет карт: asda_convert_map_lit.py (sunDir из .mapraw ver>=3).
# update_map_lighting.py — ТОЧЕЧНОЕ обновление света существующего уровня карты из .mapraw
#   (без полного ре-импорта). Ставит per-map цвет/силу направленного света + SkyLight + направление.
#   Запуск: ASDA_MAP=alpen run_headless.bat update_map_lighting.py   (по умолч. alpen)
#   См. RE/docs/client/logic/lighting_analysis.md
import os
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_coords as _C
import sys as _sys
import unreal
import asda_mapraw as _MR                                  # ★ единый ридер .mapraw (был свой read_scene)
import asda_maps as _MAPS                                  # ★ единый санитайзер имени карты
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)

MAP_NAME = os.environ.get("ASDA_MAP", "alpen")
RAW_DIR  = os.environ.get("ASDA_RAW", r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps")

SUN_LUX = float(os.environ.get("ASDA_SUNLUX", "5.0"))   # калибровка направленного (подстраивать визуально)
SKY_LUX = float(os.environ.get("ASDA_SKYLUX", "0.6"))   # ambient-заполнение (эталон MatAmbient=0.6)
SCALE   = 100.0

def to_ue(x, y, z): return _C.to_ue(x, y, z, SCALE)   # ★ единая конверсия осей (asda_coords)

def read_scene(path):
    sc = _MR.read_scene(path)                   # strict=True — как прежний assert "bad magic"
    return {"ambient": sc["ambient"], "diffuse": sc["diffuse"], "lightDir": sc["lightDir"],
            "fogStart": sc["fogStart"], "fogEnd": sc["fogEnd"]}

def main():
    raw = os.path.join(RAW_DIR, MAP_NAME + ".mapraw")
    if not os.path.exists(raw):
        unreal.log_error("Нет .mapraw: %s" % raw); return
    scene = read_scene(raw)
    amb = scene["ambient"]; ld = scene["lightDir"]
    mx = max(amb[0], amb[1], amb[2]) or 1.0
    sun_col = unreal.LinearColor(amb[0]/mx, amb[1]/mx, amb[2]/mx, 1.0)
    sun_int = mx * SUN_LUX

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not unreal.EditorAssetLibrary.does_asset_exist(LVL_PATH):
        unreal.log_error("Нет уровня: %s" % LVL_PATH); return
    les.load_level(LVL_PATH)

    dirue = to_ue(ld[0], ld[1], ld[2])
    travel = unreal.Vector(-dirue.x, -dirue.y, -dirue.z)
    rot = unreal.MathLibrary.find_look_at_rotation(unreal.Vector(0, 0, 0), travel)

    nd = ns = 0
    for a in eas.get_all_level_actors():
        if isinstance(a, unreal.DirectionalLight):
            a.set_actor_rotation(rot, False)
            dc = a.get_component_by_class(unreal.DirectionalLightComponent)
            dc.set_light_color(sun_col)
            dc.set_intensity(sun_int)
            nd += 1
        elif isinstance(a, unreal.SkyLight):
            sc = a.get_component_by_class(unreal.SkyLightComponent)
            sc.set_intensity(SKY_LUX)
            ns += 1
    les.save_current_level()
    unreal.log("СВЕТ %s: Sun цвет=(%.2f,%.2f,%.2f) сила=%.2f (dir=%s) + Sky=%.2f | обновлено Dir=%d Sky=%d"
               % (MAP_NAME, amb[0], amb[1], amb[2], sun_int, ld, SKY_LUX, nd, ns))


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global LVL_PATH
    ASSET    = _MAPS.map_asset_name(MAP_NAME)
    LVL_PATH = _U.level_path(MAP_NAME)
    main()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
