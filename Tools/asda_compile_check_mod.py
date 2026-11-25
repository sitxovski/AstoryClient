# Copyright (c) 2026 @sitxovski (Discord)
# компиляция мод-мастеров с реальным RHI: ошибки транслятора всплывут в логе как Failed to compile
import unreal
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for nm in ("M_AsdaEffectMod","M_AsdaEffectModAdd","M_AsdaEffectAdd","M_AsdaEffectAlpha"):
        m = EAL.load_asset("/Game/Effects/%s" % nm)
        if m: MEL.recompile_material(m)
        unreal.log("[COMPILE-CHECK] %s loaded=%s" % (nm, m is not None))
    mic = EAL.load_asset("/Game/Map/flammio/EffectMat/MI_eff_0_mon_99_smoke01-a_00_mod")
    unreal.log("[COMPILE-CHECK] MIC mod parent=%s" % (mic.get_editor_property("parent").get_name() if mic else "НЕТ"))
    unreal.log("=== COMPILE CHECK DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
