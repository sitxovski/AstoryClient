# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_glow_handedit.py — ИМИТАЦИЯ РУЧНОЙ ПРАВКИ ВЛАДЕЛЬЦА для доказательства совместимости.
#
#   ЗАЧЕМ. На 10.09.2026 ни у одного из 1274 ассетов BP_Fx_* не стоял замок «переопределено вручную»
#   (дамп: manual=0), то есть взять «ассет с ручной правкой» и показать значения ДО/ПОСЛЕ было НЕ НА ЧЁМ.
#   Скрипт ставит на ОДИН заранее названный ассет нетипичные числа И замок — ровно то, что оставила бы
#   рука владельца в панели деталей, — и сохраняет ассет. Дальше этот ассет служит контрольным образцом.
#
#   ★ НЕ ТРОГАЕТ ПРИНЯТОЕ. Ассет по умолчанию — BP_Fx_0018 (семейство other, ни на одной приёмке
#     не фигурировал). Столбы alpen, фонарь nightvalley, дым и метки прицела не затрагиваются.
#   Запуск: python Tools/run_headless_safe.py Tools/asda_fx_glow_handedit.py --marker "РУЧНАЯ ПРАВКА: ЗАВЕРШЕНО"
import os

PKG = os.environ.get("ASDA_GLOW_ASSET", "/Game/AsdaAssets/Effects/BP_Fx_0018")

# Нетипичные значения: ни одно не совпадает с дефолтом семейства, поэтому подмену видно сразу.
VALUES = {
    "intensity_cd": 33.25,
    "radius_min": 175.0,
    "radius_max": 925.0,
    "color_saturation": 0.6,
    "flicker_hz": 4.5,
    "flicker_amplitude": 0.35,
    "priority": 2.5,
    "alpha_power": 0.5,
}


def main():
    import unreal
    bp = unreal.load_object(None, PKG + "." + PKG.rsplit("/", 1)[1])
    cdo = unreal.get_default_object(bp.generated_class())
    fx = cdo.get_editor_property("fx_light")
    for k, v in VALUES.items():
        fx.set_editor_property(k, v)
    # Цвет — отдельно (структура), и режим «явный», чтобы цвет реально применялся
    fx.set_editor_property("color_srgb", unreal.Color(b=40, g=90, r=230, a=255))
    fx.set_editor_property("color_mode", unreal.AsdaFxLightColorMode.FIXED)
    fx.set_editor_property("offset", unreal.Vector(0.0, 0.0, 65.0))
    # ★ ЗАМОК — последним: он и есть признак «правил руками»
    for nm in ("b_manual", "manual"):
        try:
            fx.set_editor_property(nm, True)
            break
        except Exception:
            continue
    cdo.set_editor_property("fx_light", fx)
    ok = unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    unreal.log("[ручная правка] %s сохранён=%s" % (PKG, ok))
    unreal.log("РУЧНАЯ ПРАВКА: ЗАВЕРШЕНО")


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
