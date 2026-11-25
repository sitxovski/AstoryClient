# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_softfade_switch.py — ВРЕМЕННО включить/выключить мягкое врезание во ВСЕХ мастерах эффектов,
#   меняя дефолт static switch `SoftFadeEnabled` прямо в мастере.
#
# ЗАЧЕМ. `SoftFadeEnabled` — STATIC switch: его переключение = НОВАЯ перестановка шейдера, которую
#   надо компилировать. В режиме -game компиляция пошла бы прямо в кадре (и кадр снялся бы с Default
#   Material). Поэтому для съёмочного прогона «с затуханием» свитч переводится ЗАРАНЕЕ, в headless,
#   где шейдер компилируется офлайн; после съёмки — возвращается.
#   ★ Штатный путь раската — НЕ этот скрипт, а MIC-варианты (asda_apply_fx_boost.py): там свитч
#     включается точечно, у выбранных семейств. Здесь — глобальный тумблер только ради кадров.
#
# Использование:
#   ASDA_FX_SOFT=on   python Tools/run_headless_safe.py Tools/asda_fx_softfade_switch.py --commandlet-rhi --marker "FX-SOFT ЗАВЕРШЕНО"
#   ASDA_FX_SOFT=off  ... (обязательный возврат после съёмки)
#   ASDA_FX_SOFT_DIST=48  — заодно поставить дистанцию затухания (юниты UE)
import os
import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
DIR = "/Game/Effects"
MASTERS = ("M_AsdaEffectAdd", "M_AsdaEffectAlpha", "M_AsdaEffectModAdd", "M_AsdaEffectMod")
ON = os.environ.get("ASDA_FX_SOFT", "off").lower() in ("1", "on", "true")
DIST = os.environ.get("ASDA_FX_SOFT_DIST", "")


def main():
    """Работа скрипта (закон tools-no-import-side-effects: импорт ничего не делает)."""
    for name in MASTERS:
        mat = EAL.load_asset("%s/%s" % (DIR, name))
        if not mat:
            unreal.log_warning("[FX-SOFT] нет %s" % name)
            continue
        touched = []
        for e in MEL.get_material_expressions(mat):
            try:
                nm = str(e.get_editor_property("parameter_name"))
            except Exception:
                continue
            if nm == "SoftFadeEnabled":
                e.set_editor_property("default_value", ON)
                touched.append("SoftFadeEnabled=%s" % ON)
            elif nm == "SoftFadeDistance" and DIST:
                e.set_editor_property("default_value", float(DIST))
                touched.append("SoftFadeDistance=%s" % DIST)
        if not touched:
            unreal.log_warning("[FX-SOFT] %s: параметров затухания в графе нет" % name)
            continue
        MEL.recompile_material(mat)
        mat.modify()
        ok = EAL.save_loaded_asset(mat)
        st = MEL.get_statistics(mat)
        unreal.log("[FX-SOFT] %s: %s save=%s ps=%s" % (
            name, ", ".join(touched), ok, st.get_editor_property("num_pixel_shader_instructions")))
    unreal.log("FX-SOFT ЗАВЕРШЕНО (режим=%s)" % ("ВКЛ" if ON else "ВЫКЛ"))


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
