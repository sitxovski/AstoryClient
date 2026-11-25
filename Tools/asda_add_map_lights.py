# Copyright (c) 2026 @sitxovski (Discord)
# ★★★ УСТАРЕЛО (2026-09-03, правка Д-1 аудита света): скрипт относится к СТАРОЙ unlit-модели
#   (per-map LightColor/LightDir/Ambient в MIC) и берёт направление света из .mapraw 'lightDir' —
#   вектор НЕВЕРНОЙ формулы (ошибка 30..90° на всех картах). Lit-мастера этих параметров не читают.
#   НЕ ЗАПУСКАТЬ. Актуальный свет карт: asda_convert_map_lit.py (sunDir из .mapraw ver>=3).
# Вернуть DirectionalLight+SkyLight на карту (для LIT glTF-дефолт объектов; unlit их игнорят).
import os, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_coords as _C
import asda_maps as _MAPS
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
import asda_mapraw as _MR   # ★ единый ридер .mapraw (была инлайн-копия чтения сцены на лямбдах)
ML=unreal.MathLibrary
MAP=os.environ.get("ASDA_MAP","alpen"); RAW=os.environ.get("ASDA_RAW",r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps")
def to_ue(x,y,z): return _C.to_ue(x,y,z,SCALE)   # ★ единая конверсия осей (asda_coords)
les=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem); eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global SCALE
    ASSET=_MAPS.map_asset_name(MAP); LVL=_U.level_path(MAP); SCALE=100.0
    # ★ 16.09.2026 (ревизия раздела 22, СРЕДН-2): имя карты приходит из ASDA_MAP и НИ С ЧЕМ не сверяется
    #   (asda_maps.map_asset_name только чистит символы) — ASDA_MAP=Login дал бы пути сцены логина. Уровень
    #   логина открывает только ASDA_ALLOW_LOGIN_MAP_WRITE=1 (единое правило, asda_login_guard).
    #   Здесь дыра только предупредительная: /Game/Map/Login/Login не существует, load_level на него не пройдёт, —
    #   но проверка стоит ДО load_level, а не надеется на отсутствие уровня.
    import asda_login_guard as _LG
    # ★ 16.09.2026 (ревизия §7, п. 5): плюс сверка имени карты с реестром (Tools/data/asda_map_registry.json) —
    #   опечатка в ASDA_MAP иначе молча даёт произвольный корень /Game/Map/<мусор>.
    if _LG.login_map_write_blocked(LVL, "asda_add_map_lights.py") or \
       _LG.login_map_write_blocked("/Game/Map/%s"%ASSET, "asda_add_map_lights.py", quiet=True) or \
       _LG.map_name_blocked(LVL, "asda_add_map_lights.py"): return
    _sc=_MR.read_scene(os.path.join(RAW,MAP+".mapraw"))   # ambient/diffuse/lightDir из шапки сцены
    amb=_sc["ambient"]; ld=_sc["lightDir"]
    les.load_level(LVL)
    # не плодить дубли
    for a in list(eas.get_all_level_actors()):
        if isinstance(a,(unreal.DirectionalLight,unreal.SkyLight)) and a.get_actor_label().startswith(("Sun_","Sky_")): eas.destroy_actor(a)
    mx=max(amb) or 1.0
    dirue=to_ue(ld[0],ld[1],ld[2]); rot=ML.find_look_at_rotation(unreal.Vector(0,0,0),unreal.Vector(-dirue.x,-dirue.y,-dirue.z))
    sun=eas.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,30000),rot)
    dc=sun.get_component_by_class(unreal.DirectionalLightComponent)
    dc.set_light_color(unreal.LinearColor(amb[0]/mx,amb[1]/mx,amb[2]/mx,1.0)); dc.set_intensity(mx*4.0); sun.set_actor_label("Sun_%s"%MAP)
    sky=eas.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,30000))
    # ★ НЕ real_time_capture: RTC без SkyAtmosphere/VolumetricCloud даёт ЧЁРНЫЙ ambient (красный ворнинг
    #   «иначе оно будет чёрным») + пере-капчер каждый кадр = fps-жор. Статик-капчер сцены достаточно.
    skc=sky.get_component_by_class(unreal.SkyLightComponent)
    skc.set_editor_property("real_time_capture",False)
    skc.set_editor_property("source_type",unreal.SkyLightSourceType.SLS_CAPTURED_SCENE)
    sky.set_actor_label("Sky_%s"%MAP)
    les.save_current_level()
    unreal.log("LIGHTS %s: Sun(цвет %.2f,%.2f,%.2f, сила %.2f)+Sky добавлены"%(MAP,amb[0],amb[1],amb[2],mx*4.0))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
