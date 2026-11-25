# Copyright (c) 2026 @sitxovski (Discord)
# asda_tune_map_lighting.py — настройка теней DirectionalLight на картах для ПРОИЗВОДИТЕЛЬНОСТИ без запекания.
#   Несобранное освещение → всё в динамике; главный расход = CSM-тени со всей геометрии (2597 статик +
#   скелетал). Ограничиваем ДИСТАНЦИЮ динамических теней (только близкие объекты) + число каскадов —
#   даёт крупный прирост FPS, тени вблизи остаются. Правится в редакторе (значения на свету карты).
# Запуск: headless. Опц. ASDA_MAPS="alpen||rein".
import os, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_maps as _MAPS
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)

EAL = unreal.EditorAssetLibrary
def log(m): unreal.log("[tune_lighting] " + str(m))

# Карты с уровнями (как в раскате NPC/мобов).
# ★ Список карт — ЕДИНЫЙ источник asda_maps.py (все 22 карты порта).
#   Было: локальный литерал (13 копий по тулам). Состав сверен 1:1.
ALL_MAPS = _MAPS.ALL_MAPS

SHADOW_DIST = 15000.0   # 150 м — дальше динамических теней нет (перф). Вблизи тени сохранены.
CASCADES    = 3

def tune_level(level_pkg):
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not EAL.does_asset_exist(level_pkg): return None
    les.load_level(level_pkg)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    n = 0
    for a in eas.get_all_level_actors():
        if type(a).__name__ != "DirectionalLight": continue
        c = a.get_component_by_class(unreal.DirectionalLightComponent)
        if not c: continue
        # Дистанция динамических теней (стационарный свет) + каскады.
        for prop, val in (("dynamic_shadow_distance_stationary_light", SHADOW_DIST),
                          ("dynamic_shadow_distance_movable_light", SHADOW_DIST),
                          ("num_dynamic_shadow_cascades", CASCADES)):
            try: c.set_editor_property(prop, val)
            except Exception: pass
        n += 1
    if n: les.save_current_level()
    return n

def main():
    only = os.environ.get("ASDA_MAPS", "")
    maps = [m.strip() for m in only.split("||")] if only else ALL_MAPS
    tot = 0
    for mp in maps:
        import re
        asset = _MAPS.map_asset_name(mp)  # ★ единый санитайзер имени карты (asda_maps.map_asset_name)
        pkg = _U.level_path(mp)
        r = tune_level(pkg)
        if r is None: log("пропуск (нет уровня): %s" % mp); continue
        log("%s: DirectionalLight настроено %d (тень-дист=%.0f каскадов=%d)" % (mp, r, SHADOW_DIST, CASCADES))
        tot += r
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass
    log("ГОТОВО: свет настроен на %d уровнях" % tot)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
