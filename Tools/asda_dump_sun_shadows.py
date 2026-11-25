# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Дамп параметров DirectionalLight (солнце) на Login + выборке игровых карт.
# Диагностика полосатых теней (shadow acne). Только чтение, ничего не сохраняет.
import unreal, json, os

MAPS = ["/Game/Map/Login",
        "/Game/Map/alpen/alpen",
        "/Game/Map/rein/rein",
        "/Game/Map/silverion/silverion"]

OUT = r"@@ASTORY_DEV_ROOT@@\Reports\sun_shadow_dump.json"
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
result = {}

PROPS = ["intensity", "light_color", "shadow_bias", "shadow_slope_bias",
         "shadow_resolution_scale", "dynamic_shadow_distance_movable_light",
         "dynamic_shadow_distance_stationary_light", "dynamic_shadow_cascades",
         "cascade_distribution_exponent", "cascade_transition_fraction",
         "contact_shadow_length", "contact_shadow_length_in_ws",
         "shadow_amount", "cast_shadows", "shadow_filter_sharpen",
         "specular_scale", "use_temperature", "atmosphere_sun_light"]


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for path in MAPS:
        name = path.rsplit("/", 1)[-1]
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            result[name] = {"error": "no asset"}
            continue
        if not les.load_level(path):
            result[name] = {"error": "load failed"}
            continue
        entry = {"suns": [], "skylights": []}
        for a in eas.get_all_level_actors():
            cls = a.get_class().get_name()
            if cls == "DirectionalLight" or isinstance(a, unreal.DirectionalLight):
                lc = a.get_component_by_class(unreal.DirectionalLightComponent)
                d = {"label": a.get_actor_label(),
                     "mobility": str(lc.get_editor_property("mobility")),
                     "rotation": str(a.get_actor_rotation())}
                for p in PROPS:
                    try:
                        v = lc.get_editor_property(p)
                        d[p] = str(v)
                    except Exception:
                        pass
                try:
                    lf = lc.get_editor_property("light_function_material")
                    d["light_function"] = lf.get_path_name() if lf else None
                except Exception:
                    pass
                entry["suns"].append(d)
            elif cls == "SkyLight" or isinstance(a, unreal.SkyLight):
                sc = a.get_component_by_class(unreal.SkyLightComponent)
                entry["skylights"].append({
                    "label": a.get_actor_label(),
                    "intensity": str(sc.get_editor_property("intensity")),
                    "mobility": str(sc.get_editor_property("mobility"))})
        result[name] = entry
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(result, f, ensure_ascii=False, indent=1)
    unreal.log("SUN_DUMP_DONE -> " + OUT)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
