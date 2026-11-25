# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_fx_master_stats.py — ТОЛЬКО ЗАМЕР инструкций пиксельного шейдера мастеров эффектов.
#   Ничего не меняет и не сохраняет. Нужен, чтобы зафиксировать «до волны Ф1» и сравнить с «после»
#   (доказательство цены дефолта: +1 инструкция на скаляр EmissiveBoost, 0 на выключенное затухание).
# Запуск: python Tools/run_headless_safe.py Tools/_probe_fx_master_stats.py --commandlet-rhi --marker "FX-STATS ЗАВЕРШЕНО"
import unreal
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MASTERS = ("M_AsdaEffectAdd", "M_AsdaEffectAlpha", "M_AsdaEffectModAdd", "M_AsdaEffectMod")


def main():
    """Работа скрипта (закон tools-no-import-side-effects: импорт ничего не делает)."""
    for n in MASTERS:
        m = EAL.load_asset("/Game/Effects/%s" % n)
        if not m:
            continue
        st = MEL.get_statistics(m)
        unreal.log("[FX-STATS] %s: ps=%s vs=%s samplers=%s узлов=%d" % (
            n, st.get_editor_property("num_pixel_shader_instructions"),
            st.get_editor_property("num_vertex_shader_instructions"),
            st.get_editor_property("num_samplers"), len(MEL.get_material_expressions(m))))
    unreal.log("FX-STATS ЗАВЕРШЕНО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
