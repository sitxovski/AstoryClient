# Copyright (c) 2026 @sitxovski (Discord)
# Фикс usage-флагов мастер-материалов: HISM-объекты карт требуют used_with_instanced_static_meshes,
#   скелеталы — used_with_skeletal_mesh. Пересборка мастера (delete+recreate) флаги СБРАСЫВАЕТ:
#   в редакторе авто-usage маскирует (компилит на лету), в -game/standalone → ЧЁРНЫЙ фолбэк.
#   Симптом: «через логин объекты чёрные, напрямую в редакторе норм». Запуск: run_headless.bat asda_fix_material_usages.py
import unreal
EAL = unreal.EditorAssetLibrary
MASTERS = ["M_AsdaObject", "M_AsdaObjectLit", "M_AsdaTerrain", "M_AsdaTerrainLit",
           "M_AsdaSky", "M_AsdaWater", "M_AsdaObjectScroll", "M_AsdaObjectScrollLit"]   # ★ 03.09.2026: + Lit-скролл


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for name in MASTERS:
        p = "/Game/Materials/" + name
        if not EAL.does_asset_exist(p):
            unreal.log_warning("[USAGE] нет %s" % name); continue
        m = EAL.load_asset(p)
        before_i = m.get_editor_property("used_with_instanced_static_meshes")
        before_s = m.get_editor_property("used_with_skeletal_mesh")
        m.set_editor_property("used_with_instanced_static_meshes", True)
        m.set_editor_property("used_with_skeletal_mesh", True)
        unreal.MaterialEditingLibrary.recompile_material(m)
        EAL.save_loaded_asset(m)
        unreal.log("[USAGE] %s: ism %s->True, skel %s->True" % (name, before_i, before_s))
    unreal.log("[USAGE] done")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
