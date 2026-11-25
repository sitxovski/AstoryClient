# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_glow_unlock.py — СНЯТЬ замок «переопределено вручную» с контрольного ассета и сохранить.
#   Это первая половина проверки «возврат к значениям семейства работает»: вторая — прогон
#   Tools/asda_apply_fx_light_defaults.py, который обязан вернуть числа семейства ИМЕННО ПОТОМУ,
#   что замок снят (пока он стоял, генератор ассет пропускал).
#   Запуск: python Tools/run_headless_safe.py Tools/asda_fx_glow_unlock.py --marker "ЗАМОК СНЯТ"
import os

PKG = os.environ.get("ASDA_GLOW_ASSET", "/Game/AsdaAssets/Effects/BP_Fx_0018")


def main():
    import unreal
    bp = unreal.load_object(None, PKG + "." + PKG.rsplit("/", 1)[1])
    cdo = unreal.get_default_object(bp.generated_class())
    fx = cdo.get_editor_property("fx_light")
    for nm in ("b_manual", "manual"):
        try:
            fx.set_editor_property(nm, False)
            break
        except Exception:
            continue
    cdo.set_editor_property("fx_light", fx)
    ok = unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    unreal.log("[замок] %s снят, сохранён=%s" % (PKG, ok))
    unreal.log("ЗАМОК СНЯТ")


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
