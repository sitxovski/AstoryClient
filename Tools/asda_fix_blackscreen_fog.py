# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_blackscreen_fog.py — ★ ЗАТЕМНЕНИЕ СОЗДАНИЯ ПЕРСОНАЖА БЕЗ ТУМАНА (08.11.2026, пачка 16, баг 10). [ИСПРАВЛЕНИЕ]
#   M_AsdaBlackScreen (квад blackscreen за создаваемым персонажем, translucent unlit, чёрный, альфа 0.85 — бленд эталона)
#   получал туман по вершинам («Apply Fogging» = use_translucency_vertex_fog): вершины квада лежат в 100 000+ ед. от камеры,
#   поэтому весь квад окрашивался в полный цвет тумана логина — вместо затемнения жёлтая пелена (кадр
#   Reports/batch16/login/after_create/ru/create_dim_mode1.png до этой правки). В эталоне квад — чистый чёрный с альфой.
#   Правка: use_translucency_vertex_fog = False (и compute_fog_per_pixel = False), перекомпиляция, сохранение.
#   Пакет сцены логина — нужен ASDA_ALLOW_LOGIN_WRITE=1. Сухой: ASDA_BLACKSCREEN_FOG_DRYRUN=1.
#   Запуск: python Tools/run_headless_safe.py Tools/asda_fix_blackscreen_fog.py --marker ЗАВЕРШЕНО
# ★ Закон tools-no-import-side-effects: при импорте модуль ничего не делает.
import os

MAT = "/Game/Effects/M_AsdaBlackScreen"   # материал квада затемнения


def main():
    import sys
    import unreal
    here = os.path.dirname(os.path.abspath(__file__))
    if here not in sys.path:
        sys.path.insert(0, here)
    from asda_login_guard import package_blocked_for
    log = lambda m: unreal.log("[blackscreen-туман] " + m)
    if package_blocked_for(__file__, MAT):
        log("ЗАВЕРШЕНО (пропуск: нужен ASDA_ALLOW_LOGIN_WRITE=1)")
        return
    m = unreal.load_asset(MAT)
    if not m:
        log("ЗАВЕРШЕНО (ошибка: нет материала)")
        return
    was = (m.get_editor_property("use_translucency_vertex_fog"), m.get_editor_property("compute_fog_per_pixel"))
    log("было: use_translucency_vertex_fog=%s compute_fog_per_pixel=%s" % was)
    if os.environ.get("ASDA_BLACKSCREEN_FOG_DRYRUN", "") not in ("", "0"):
        log("ЗАВЕРШЕНО (сухой)")
        return
    m.set_editor_property("use_translucency_vertex_fog", False)
    m.set_editor_property("compute_fog_per_pixel", False)
    unreal.MaterialEditingLibrary.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    log("стало: use_translucency_vertex_fog=%s compute_fog_per_pixel=%s; сохранено %s"
        % (m.get_editor_property("use_translucency_vertex_fog"), m.get_editor_property("compute_fog_per_pixel"), MAT))
    log("ЗАВЕРШЕНО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
