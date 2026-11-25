# Copyright (c) 2026 @sitxovski (Discord)
# asda_rollout_sky_full.py — ПОЛНАЯ раскатка небесного комплекта на карту (env ASDA_SKY_MAPS порциями):
#   1) небо: asda_build_sky_dome.py (купол+горизонт+звёзды+MapFog 1e-6+снос LayeredClouds)
#   2) CloudField: 38 3D-облаков на Z=35000, цвет = лифт главного цвета неба карты (+тёмный низ) — ТОЛЬКО в
#      пер-карточном MIC M_AsdaCloudMesh_<карта> (asda_percloud_mics.ensure_cloud_mic; поля cloud_color/shade_color
#      с актора сняты — памятка sky-system-complete; до 05.09.2026 скрипт их ставил и падал на alpen),
#      меши cloudblob_a..d ставит asda_percloud_mics.ensure_cloud_meshes (до аудита 05.09 не ставились → сферы),
#      ветер по направлению дрейфа купольного слоя из .TER (узор движется против прироста UV-офсета:
#      UE-вектор = (dv, -du)), скорость по высоте (низ быстрее).
# Тени облаков (CloudShadows) уже раскатаны ранее и не трогаются.
import os, unreal
import sys as _sys, os as _os
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_maps as _MAPS
import asda_ter_height as _TER      # ★ единый разбор .TER (был свой struct по индексу 2912)
import asda_ue_paths as _UP
import asda_percloud_mics as _PMC   # ★ единый носитель цвета облаков: MIC M_AsdaCloudMesh_<карта>
EAL = unreal.EditorAssetLibrary
TOOLS = _UP.TOOLS
_asset = _MAPS.map_asset_name          # ★ единый санитайзер имени карты

SKY_COLORS = _MAPS.SKY_COLORS     # ★ единая таблица цвета неба (было 3 дословные копии), ключи = OUTDOOR_MAPS

def wind_yaw_from_ter(mp):
    p = _TER.ter_path(mp)                       # ★ единый резолв пути (учитывает регистр имени .TER)
    if not p: return -90.0
    spd, _c5 = _TER.TerFile(p).sky_scroll()     # скорости скролла слоёв купола (индексы 2912..2915)
    du, dv = spd[2], spd[3]
    if abs(du) < 1e-12 and abs(dv) < 1e-12: du = dv = spd[0]
    if abs(du) < 1e-12 and abs(dv) < 1e-12: return -90.0
    import math
    return math.degrees(math.atan2(-du, dv))

def place_cloud_field(mp):
    root = "/Game/Map/%s" % _asset(mp); lvl = "%s/%s" % (root, _asset(mp))
    # ★★★ 16.09.2026 (обратный обход ревизии savedir12): гейта не было вовсе, а ниже идут load_level и
    #   save_current_level. Проверяем и сцену логина, и имя карты — ДО первой операции с уровнем.
    import asda_login_guard as _LG
    if _LG.login_map_write_blocked(root, "asda_rollout_sky_full.py") or \
       _LG.map_name_blocked(root, "asda_rollout_sky_full.py"):
        return
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(lvl)
    mn = [1e12, 1e12]; mx = [-1e12, -1e12]
    for a in eas.get_all_level_actors():
        if a.get_class().get_name() != "StaticMeshActor": continue
        o, e = a.get_actor_bounds(False)
        mn[0] = min(mn[0], o.x - e.x); mn[1] = min(mn[1], o.y - e.y)
        mx[0] = max(mx[0], o.x + e.x); mx[1] = max(mx[1], o.y + e.y)
    for a in list(eas.get_all_level_actors()):
        if a.get_class().get_name() == "AsdaCloudFieldActor": eas.destroy_actor(a)
    mic, mic_name = _PMC.ensure_cloud_mic(mp)      # цвет облаков живёт ТОЛЬКО здесь (Tint/ShadeColor/Intensity)
    f = eas.spawn_actor_from_class(unreal.AsdaCloudFieldActor, unreal.Vector((mn[0]+mx[0])/2, (mn[1]+mx[1])/2, 35000.0))
    f.set_actor_label("CloudField")
    f.set_editor_property("material", mic)
    # ★ 05.09.2026 (аудит неба): без этого CloudMeshes пуст → актор рисует блобы из движковых сфер вместо cloudblob_a..d
    _PMC.ensure_cloud_meshes(f)
    f.set_editor_property("area_extent", unreal.Vector2D((mx[0]-mn[0])/2*1.05, (mx[1]-mn[1])/2*1.05))
    f.set_editor_property("count", 38)
    f.set_editor_property("wind_yaw", wind_yaw_from_ter(mp))
    les.save_current_level()
    unreal.log("[ROLL] %s: CloudField 38шт, yaw=%.0f, материал=%s" % (mp, wind_yaw_from_ter(mp), mic_name))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global e, mp
    MAPS = _MAPS.select_maps(_MAPS.SKY_MAPS, env="ASDA_SKY_MAPS")   # фильтр "a||b" — порционная раскатка
    _sky = os.path.join(TOOLS, "asda_build_sky_dome.py")
    _sky_src = open(_sky, encoding="utf-8").read()
    for mp in MAPS:
        os.environ["ASDA_MAP"] = mp
        unreal.log("=== ROLL: %s ===" % mp)
        try:
            exec(compile(_sky_src, _sky, "exec"), {"__name__": "__main__", "__file__": _sky})
        except Exception as e:
            unreal.log_error("ROLL sky %s FAIL: %s" % (mp, e))
        try:
            place_cloud_field(mp)
        except Exception as e:
            unreal.log_error("ROLL clouds %s FAIL: %s" % (mp, e))
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass
    unreal.log("=== ROLL ЗАВЕРШЕНО ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
