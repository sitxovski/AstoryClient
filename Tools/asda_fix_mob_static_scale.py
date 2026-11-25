# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_mob_static_scale.py — исправить масштаб УЖЕ ЗАПЕЧЁННЫХ моб-акторов со статик-фолбэком.
#
# Причина: прежний asda_place_mobs.py ставил статик-мобам scale 1 («статики в см»), а замер
# показал, что статик-меши мобов импортированы в МЕТРАХ, как и скелеталки:
#   статик  mon_s_0_pawn02-a_00  габарит 21244 ед. при scale 1
#   скелетал mon_s_0_mosrd01-a_00 габарит 20782 ед. при scale 0.01
# ⇒ статик-мобы выходили в 100 раз больше (жалоба «огромные мобы почти на всех картах»).
#
# Скрипт НЕ перезапекает мобов (это дороже и трогает текстуры/анимации) — только доводит масштаб
# у акторов, чей меш статический. Скелетал-мобы не трогаются вообще.
#
# Запуск (headless):
#   UnrealEditor-Cmd.exe <проект> -ExecutePythonScript=".../asda_fix_mob_static_scale.py"
#     -unattended -nopause -nosplash -nosound -NullRHI
MOB_SCALE = 0.01


def main():
    import unreal
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    f = unreal.ARFilter(recursive_paths=True, package_paths=[unreal.Name("/Game/Map")],
                        class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "World")])
    levels = sorted(str(ad.package_name) for ad in ar.get_assets(f))
    # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА В Login.umap (15.09.2026, Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 16): /Game/Map/Login без
    #   ASDA_ALLOW_LOGIN_WRITE=1 не открывается и не пересохраняется (обход сохранял уровень при найденном). Остальные — как раньше.
    import os as _os_lg, sys as _sys_lg
    _sys_lg.path.insert(0, _os_lg.path.dirname(_os_lg.path.abspath(__file__)))   # Tools — для asda_login_guard
    import asda_login_guard as _LG
    login_skipped = [l for l in levels if _LG.login_package_blocked(l, "asda_fix_mob_static_scale.py", quiet=True)]
    if login_skipped:
        levels = [l for l in levels if l not in login_skipped]
        unreal.log_warning("[LOGIN-GUARD] [моб-масштаб] уровней сцены логина пропущено: %d (%s) — не открывались и не сохранялись (только с явного разрешения владельца и ASDA_ALLOW_LOGIN_WRITE=1)"
            % (len(login_skipped), ", ".join(login_skipped)))
    total = 0
    for lv in levels:
        try:
            les.load_level(lv)
        except Exception as e:
            unreal.log_warning("  %s: не открылся (%s)" % (lv, e))
            continue
        fixed = 0
        for a in eas.get_all_level_actors():
            if not isinstance(a, unreal.AsdaMobActor):
                continue
            # статик-фолбэк = меша скелета нет, стоит StaticMesh
            has_skel = a.mesh is not None and a.mesh.get_skinned_asset() is not None
            has_stat = a.static_mesh_comp is not None and a.static_mesh_comp.static_mesh is not None
            if has_skel or not has_stat:
                continue
            if abs(a.get_actor_scale3d().x - MOB_SCALE) < 1e-6:
                continue
            a.mesh.set_world_scale3d(unreal.Vector(MOB_SCALE, MOB_SCALE, MOB_SCALE))
            fixed += 1
        if fixed:
            les.save_current_level()
            total += fixed
            unreal.log("  %-24s исправлено %d" % (lv.split("/")[-1], fixed))
    unreal.log("=== ГОТОВО: масштаб исправлен у %d статик-мобов ===" % total)


if __name__ == "__main__":
    main()
