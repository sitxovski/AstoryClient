# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_verify_masters.py — проверка мастеров эффектов ПОСЛЕ правки волны Ф1 (asda_add_fx_glow_params.py).
#   Отвечает на два вопроса, которые нельзя закрыть чтением графа:
#     1) ветка ВЫКЛЮЧЕНА (дефолт): сколько инструкций у пиксельного шейдера — это «прежний» материал;
#     2) ветка ВКЛЮЧЕНА: компилируется ли она вообще (ps>0) и во сколько инструкций обходится.
#   Механика: временно ставим default_value=True у static switch, recompile, замер, возвращаем False,
#   recompile, замер, СОХРАНЯЕМ (в ассете гарантированно остаётся False).
# ★ Запускать только с настоящим RHI, иначе ps=0 у всех:
#   python Tools/run_headless_safe.py Tools/asda_fx_verify_masters.py --commandlet-rhi --marker "FX-VERIFY ЗАВЕРШЕНО"
import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

DIR = "/Game/Effects"
MASTERS = ("M_AsdaEffectAdd", "M_AsdaEffectAlpha", "M_AsdaEffectModAdd", "M_AsdaEffectMod")
SWITCHES = ("SoftFadeEnabled",)   # ★ буст — обычный скаляр, static switch остался только у затухания


def switch_nodes(mat):
    """Узлы static switch волны Ф1 в графе мастера (у M_AsdaEffectMod буста нет — вернётся один)."""
    out = []
    for e in MEL.get_material_expressions(mat):
        if isinstance(e, unreal.MaterialExpressionStaticSwitchParameter):
            try:
                if str(e.get_editor_property("parameter_name")) in SWITCHES:
                    out.append(e)
            except Exception:
                pass
    return out


def stats(mat, tag):
    """Печать статистики шейдера. ps=0 ⇒ материал НЕ скомпилировался (закон «ps=0 = провал»)."""
    st = MEL.get_statistics(mat)
    ps = vs = smp = -1
    try:
        ps = st.get_editor_property("num_pixel_shader_instructions")
        vs = st.get_editor_property("num_vertex_shader_instructions")
        smp = st.get_editor_property("num_samplers")
    except Exception as ex:
        unreal.log_warning("[FX-VERIFY] статистика недоступна: %s" % ex)
    unreal.log("[FX-VERIFY] %s: ps=%s vs=%s samplers=%s" % (tag, ps, vs, smp))
    return ps


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон tools-no-import-side-effects)."""
    for name in MASTERS:
        mat = EAL.load_asset("%s/%s" % (DIR, name))
        if not mat:
            unreal.log_warning("[FX-VERIFY] нет %s" % name)
            continue
        sw = switch_nodes(mat)
        MEL.recompile_material(mat)
        off = stats(mat, "%s ВЫКЛ (дефолт ассета)" % name)
        for e in sw:
            e.set_editor_property("default_value", True)
        MEL.recompile_material(mat)
        on = stats(mat, "%s ВКЛ (обе ветки Ф1)" % name)
        for e in sw:
            e.set_editor_property("default_value", False)      # возврат: в ассет уходит только ВЫКЛ
        MEL.recompile_material(mat)
        back = stats(mat, "%s возврат ВЫКЛ" % name)
        mat.modify()
        EAL.save_loaded_asset(mat)
        unreal.log("[FX-VERIFY] %s: свитчей=%d, дельта инструкций ВКЛ−ВЫКЛ=%s, возврат совпал=%s"
                   % (name, len(sw), (on - off) if (on >= 0 and off >= 0) else "?", back == off))
    unreal.log("FX-VERIFY ЗАВЕРШЕНО")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
