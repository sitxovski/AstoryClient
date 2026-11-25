# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_fxb_variant.py — контроль раската волны Ф1: на что реально указывают эмиттеры BP и какие
#   параметры лежат в MIC-варианте. Ничего не меняет.
#   Переменная окружения ASDA_FXB_PROBE="BP_Fx_имя1,BP_Fx_имя2" (по умолчанию — один соулмейт-BP).
# Запуск: python Tools/run_headless_safe.py Tools/_probe_fxb_variant.py --marker "FXB-PROBE ЗАВЕРШЕНО"
import os
import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
DIR = "/Game/AsdaAssets/Effects"
NAMES = [x.strip() for x in os.environ.get(
    "ASDA_FXB_PROBE", "BP_Fx_eff_1_char_99_soullink01_a_00").split(",") if x.strip()]


def main():
    """Работа скрипта (закон tools-no-import-side-effects: импорт ничего не делает)."""
    for n in NAMES:
        bp = EAL.load_asset("%s/%s" % (DIR, n))
        if not bp:
            unreal.log_warning("[FXB-PROBE] нет %s" % n)
            continue
        cdo = unreal.get_default_object(bp.generated_class())
        unreal.log("[FXB-PROBE] %s (.jeb «%s»)" % (n, cdo.get_editor_property("effect_name")))
        for i, e in enumerate(cdo.get_editor_property("emitters")):
            for field in ("material", "material_b"):
                m = e.get_editor_property(field)
                if not m:
                    continue
                bits = ""
                try:
                    bits = " EmissiveBoost=%.3f SoftFade=%s" % (
                        MEL.get_material_instance_scalar_parameter_value(m, "EmissiveBoost"),
                        MEL.get_material_instance_static_switch_parameter_value(m, "SoftFadeEnabled"))
                except Exception as ex:
                    bits = " (параметры не прочитались: %s)" % ex
                unreal.log("[FXB-PROBE]   эм.%02d %s = %s%s" % (i, field, m.get_name(), bits))
    unreal.log("FXB-PROBE ЗАВЕРШЕНО")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
