# Copyright (c) 2026 @sitxovski (Discord)
# Конвертация ПЕРСОНАЖЕЙ/NPC/МОБОВ на Lit + контактные тени + свет для Login.
#   1) Репарент MIC под /Game/AsdaAssets/Models/{Char,Mob,MobSkel,NPC,Skeletal,Items}: M_AsdaObject → M_AsdaObjectLit.
#      Backdrop НЕ трогаем (задник логина остаётся unlit — выверенный вид).
#   2) Contact shadows на всех Sun_<map> (screen-space стык у подножий — «плоские стыки» юзера).
#   3) Login-уровень: Movable Sun/Sky (части персонажа теперь Lit → без света чёрные в превью).
#   Запуск: run_headless.bat asda_convert_chars_lit.py
import unreal
import os   # ★ 15.09.2026: исправлен давний NameError: os — шаг 3 (Login) звал os.path до проверки разрешения, модуль импортировал только unreal
EAL = unreal.EditorAssetLibrary
ar = unreal.AssetRegistryHelpers.get_asset_registry()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ML = unreal.MathLibrary

OBJ_SRC = "M_AsdaObject"
FOLDERS = ["Char", "Mob", "MobSkel", "NPC", "Skeletal", "Items"]


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    LIT = EAL.load_asset("/Game/Materials/M_AsdaObjectLit")
    # ★★★ 15.09.2026: MIC сцены логина (Models/Char/cre_creback-a_00, cre_creation01*) — только с ASDA_ALLOW_LOGIN_WRITE=1
    import os as _os_lg, sys as _sys_lg
    _sys_lg.path.insert(0, _os_lg.path.dirname(_os_lg.path.abspath(__file__)))
    from asda_login_guard import login_package_blocked
    n_login = 0
    # 1) репарент
    n = 0
    for folder in FOLDERS:
        for ad in ar.get_assets_by_path("/Game/AsdaAssets/Models/%s" % folder, recursive=True):
            if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant": continue
            if login_package_blocked(str(ad.package_name), "asda_convert_chars_lit.py", quiet=True):
                n_login += 1; continue
            mic = EAL.load_asset(str(ad.package_name))
            if not mic: continue
            par = mic.get_editor_property("parent")
            if par and par.get_name() == OBJ_SRC:
                mic.set_editor_property("parent", LIT)
                EAL.save_loaded_asset(mic); n += 1
    unreal.log("[CHARS] MIC на Lit: %d" % n)
    if n_login:
        unreal.log_warning("[LOGIN-GUARD] asda_convert_chars_lit.py: пропущено MIC сцены логина: %d" % n_login)
    # 2) contact shadows на солнцах всех карт
    nc = 0
    for ad in ar.get_assets_by_path("/Game/Map", recursive=True):
        if str(ad.asset_class_path.asset_name) != "World": continue
        lvl = str(ad.package_name)
        if lvl.endswith(("/Login", "/L_Loading")): continue
        les.load_level(lvl)
        changed = False
        for a in eas.get_all_level_actors():
            if isinstance(a, unreal.DirectionalLight) and a.get_actor_label().startswith("Sun_"):
                dc = a.get_component_by_class(unreal.DirectionalLightComponent)
                dc.set_editor_property("contact_shadow_length", 0.06)   # screen-space контакт-тень у подножий
                changed = True
        if changed: les.save_current_level(); nc += 1
    unreal.log("[CHARS] contact shadows на картах: %d" % nc)
    # 3) Login: Movable Sun/Sky (тёплые значения выверенного логин-света)
    # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА (13.09.2026): блок логина — только с ASDA_ALLOW_LOGIN_WRITE=1
    import sys as _sys
    _sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from asda_login_guard import login_write_allowed
    if not login_write_allowed():
        unreal.log_warning("[LOGIN-GUARD] asda_convert_chars_lit.py: блок Login пропущен (свет владельца; ASDA_ALLOW_LOGIN_WRITE=1; в автоматике для сохранения самого уровня нужна ещё ASDA_ALLOW_LOGIN_MAP_WRITE=1 — C++-сторож)")
        return
    les.load_level("/Game/Map/Login")
    has_sun = any(isinstance(a, unreal.DirectionalLight) for a in eas.get_all_level_actors())
    if not has_sun:
        rot = ML.find_look_at_rotation(unreal.Vector(0, 0, 0), unreal.Vector(0.2 * 100, 0.4 * 100, -0.85 * 100))
        sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 30000), rot)
        dc = sun.get_component_by_class(unreal.DirectionalLightComponent)
        dc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        dc.set_light_color(unreal.LinearColor(0.82, 0.80, 0.75, 1.0))   # тёплый логин-свет (asda_set_login_lighting)
        dc.set_intensity(2.2)
        dc.set_editor_property("light_source_angle", 2.0)
        dc.set_editor_property("contact_shadow_length", 0.06)
        sun.set_actor_label("Sun_Login")
        sky = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 30000))
        sc = sky.get_component_by_class(unreal.SkyLightComponent)
        sc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        sc.set_editor_property("real_time_capture", False)
        sc.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_CAPTURED_SCENE)
        sc.set_intensity(0.35)
        sky.set_actor_label("Sky_Login")
        les.save_current_level()
        unreal.log("[CHARS] Login: Sun+Sky добавлены (Movable)")
    else:
        unreal.log("[CHARS] Login: свет уже есть — не трогаю")
    unreal.log("[CHARS] done")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
