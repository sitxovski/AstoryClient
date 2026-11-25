# Copyright (c) 2026 @sitxovski (Discord)
# Тюнер PostProcess по всем картам: усилить AO (env ASDA_AO_INT/ASDA_AO_RAD), не трогая остальное.
#   AO режет только ambient-компоненту (SkyLight 0.35) → intensity 0.4 давал ~14% затемнения (невидимо).
#   Запуск: run_headless.bat asda_tune_pp.py   (env ASDA_AO_INT=0.9 ASDA_AO_RAD=220)
import os, unreal
EAL = unreal.EditorAssetLibrary
ar = unreal.AssetRegistryHelpers.get_asset_registry()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
AO_INT = float(os.environ.get("ASDA_AO_INT", "0.9"))
AO_RAD = float(os.environ.get("ASDA_AO_RAD", "220"))
n = 0


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global n
    for ad in ar.get_assets_by_path("/Game/Map", recursive=True):
        if str(ad.asset_class_path.asset_name) != "World": continue
        lvl = str(ad.package_name)
        # ★★★ 16.09.2026 (обратный обход ревизии savedir12). БЫЛ пропуск ЛИТЕРАЛОМ — он не узнаёт иных форм
        #   пути ([[false-negatives-method]]) и ничего не знает о прочих пакетах сцены логина. Спрашиваем модуль:
        #   он читает единый список и отвечает по существу, а с разрешением владельца пропускает работу.
        import asda_login_guard as _LG
        if _LG.login_map_write_blocked(lvl, "asda_tune_pp.py", quiet=True): continue
        les.load_level(lvl)
        changed = False
        for a in eas.get_all_level_actors():
            if isinstance(a, unreal.PostProcessVolume) and a.get_actor_label().startswith("PP_"):
                s = a.get_editor_property("settings")
                s.set_editor_property("override_ambient_occlusion_intensity", True); s.set_editor_property("ambient_occlusion_intensity", AO_INT)
                s.set_editor_property("override_ambient_occlusion_radius", True);    s.set_editor_property("ambient_occlusion_radius", AO_RAD)
                a.set_editor_property("settings", s)
                changed = True
        if changed:
            les.save_current_level(); n += 1
            unreal.log("[PP] %s: AO int=%.2f rad=%.0f" % (lvl.split("/")[-1], AO_INT, AO_RAD))
    unreal.log("[PP] карт обновлено: %d" % n)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
