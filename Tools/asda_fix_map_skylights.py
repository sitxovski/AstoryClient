# Copyright (c) 2026 @sitxovski (Discord)
# Фикс всех карт: (1) SkyLight Real-Time Capture ВЫКЛ (RTC без SkyAtmosphere = чёрный ambient «иначе оно
#   будет чёрным» + пере-капчер каждый кадр = fps-жор); (2) удалить ЗАПЕЧЁННЫЙ свет (<map>_BuiltData от
#   «Собрать свет» — наши материалы UNLIT, лайтмапы им не нужны, только память/fps).
#   Запуск: run_headless.bat asda_fix_map_skylights.py
import unreal
import os   # ★ 16.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 18): БЫЛО не импортирован — строка защиты логина (13.09) падала NameError
            #   на первом же уровне обхода, инструмент не работал ни с разрешением, ни без
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_login_guard as _LG   # ★ 16.09.2026 (обратный обход savedir14): единый гейт сцены логина
EAL = unreal.EditorAssetLibrary
ar = unreal.AssetRegistryHelpers.get_asset_registry()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# все уровни в /Game/Map/*
levels = []


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for ad in ar.get_assets_by_path("/Game/Map", recursive=True):
        if str(ad.asset_class_path.asset_name) == "World":
            levels.append(str(ad.package_name))
    unreal.log("[FIXSKY] уровней: %d" % len(levels))
    for lvl in sorted(levels):
        # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА. ★ 16.09.2026 (обратный обход savedir14): БЫЛ пропуск ЛИТЕРАЛОМ
        #   (rsplit(...) == "Login" + ASDA_ALLOW_LOGIN_WRITE). Литерал не узнаёт ни /Game/Map/Login/<что
        #   угодно>, ни прочие пакеты сцены логина, и спрашивал НЕ ТУ переменную: сам уровень открывает
        #   только ASDA_ALLOW_LOGIN_MAP_WRITE=1. Теперь — общий гейт по единому списку пакетов логина, ДО
        #   load_level и ДО удаления <уровень>_BuiltData. Без quiet: строку [LOGIN-GUARD] печатает сам гейт.
        if _LG.login_map_write_blocked(lvl, "asda_fix_map_skylights.py"):
            continue
        fixed = 0; built = 0
        les.load_level(lvl)
        for a in eas.get_all_level_actors():
            if isinstance(a, unreal.SkyLight):
                c = a.get_component_by_class(unreal.SkyLightComponent)
                if c and c.get_editor_property("real_time_capture"):
                    c.set_editor_property("real_time_capture", False)
                    c.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_CAPTURED_SCENE)
                    fixed += 1
        # запечённый свет: ассет <level>_BuiltData рядом с уровнем
        bd = lvl + "_BuiltData"
        if EAL.does_asset_exist(bd):
            EAL.delete_asset(bd); built = 1
        if fixed or built:
            les.save_current_level()
            unreal.log("[FIXSKY] %s: RTC-off=%d BuiltData-удалён=%d" % (lvl, fixed, built))
    unreal.log("[FIXSKY] done")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
