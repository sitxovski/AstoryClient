# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_set_login_lighting.py — свет логин-сцены (/Game/Map/Login) в модели UE-света: Sun_Login + Sky_Login.
#
# ★ ДЕФЕКТ, который чинит (Д-17 аудита света 2026-09-03): солнце логина лежало НА ГОРИЗОНТЕ —
#   make_login_level.py ставил `unreal.Rotator(-45, 0, 0)`, а у UE-python аргументы Rotator
#   ПОЗИЦИОННО = (roll, pitch, yaw) ⇒ получился roll=-45, pitch=0: forward (1,0,0), персонаж и
#   шкатулка освещены сбоку, тени бесконечные (sun_shadow_dump.json: pitch 0, roll -45).
#   Замысел был pitch=-45 — ставим его ЯВНО по именам (pitch=…, yaw=…, roll=0).
#   [ПОРТ] Логин — не карта (.mapraw/.SCE у него нет); направление света оригинала на экране логина
#   дампом не снималось (lighting_analysis.md §7), поэтому здесь — замысел порта, не данные эталона.
#
# Ключи (перезапуск скрипта, без пересборки):
#   ASDA_LOGIN_SUN_PITCH (умолч. -45), ASDA_LOGIN_SUN_YAW (0) — направление;
#   ASDA_LOGIN_SUNLUX — сила солнца (если не задана — не трогаем текущую, 2.2 из asda_convert_chars_lit);
#   ASDA_LOGIN_SKYLUX — сила SkyLight (если не задана — не трогаем).
#   ASDA_LOGIN_LEGACY_UNLIT=1 — УСТАРЕВШИЙ проход по MIC unlit-мастера M_AsdaObject (Ambient/LightColor/
#   LightDir); Lit-мастера этих параметров не читают, в Login на M_AsdaObject MIC не осталось — оставлен
#   только как откат, по умолчанию НЕ выполняется.
#
# Запуск (СКРЫТО, редактор закрыт):
#   python Tools/run_headless_safe.py Tools/asda_set_login_lighting.py --marker LOGINLIGHT-FINISHED
import os, unreal
EAL=unreal.EditorAssetLibrary; MEL=unreal.MaterialEditingLibrary
LVL="/Game/Map/Login"; OBJ="M_AsdaObject"

def fl(env,default):
    v=os.environ.get(env)
    return [float(x) for x in v.split(",")] if v else default

SUN_PITCH=float(os.environ.get("ASDA_LOGIN_SUN_PITCH","-45"))   # отрицательный pitch = свет сверху вниз
SUN_YAW=float(os.environ.get("ASDA_LOGIN_SUN_YAW","0"))
SUN_LUX=os.environ.get("ASDA_LOGIN_SUNLUX")                       # None = не трогать
SKY_LUX=os.environ.get("ASDA_LOGIN_SKYLUX")                       # None = не трогать

# --- устаревшая unlit-модель (только по ASDA_LOGIN_LEGACY_UNLIT=1) ---
AMB=float(os.environ.get("ASDA_AMB","0.6"))
LC =fl("ASDA_LC",[0.82,0.80,0.75])
LD =fl("ASDA_LD",[0.2,0.4,-0.85])


def legacy_unlit_params(eas):
    """УСТАРЕЛО: параметры Ambient/LightColor/LightDir у MIC unlit-мастера M_AsdaObject (Lit их не читает)."""
    lcol=unreal.LinearColor(LC[0],LC[1],LC[2],0.0); ldcol=unreal.LinearColor(LD[0],LD[1],LD[2],0.0)
    seen=set(); n=0
    for a in eas.get_all_level_actors():
        for comp in a.get_components_by_class(unreal.MeshComponent):
            for i in range(comp.get_num_materials()):
                m=comp.get_material(i)
                if not isinstance(m,unreal.MaterialInstanceConstant): continue
                par=m.get_editor_property("parent")
                if not par or par.get_name()!=OBJ: continue
                pn=m.get_path_name()
                if pn in seen: continue
                seen.add(pn)
                MEL.set_material_instance_scalar_parameter_value(m,"Ambient",AMB)
                MEL.set_material_instance_vector_parameter_value(m,"LightColor",lcol)
                MEL.set_material_instance_vector_parameter_value(m,"LightDir",ldcol)
                EAL.save_loaded_asset(m); n+=1
    unreal.log("СВЕТ Login (legacy unlit): MIC=%d Ambient=%.2f LightColor=(%.2f,%.2f,%.2f) LightDir=(%.2f,%.2f,%.2f)"
               %(n,AMB,LC[0],LC[1],LC[2],LD[0],LD[1],LD[2]))


def main():
    # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА (13.09.2026): без ASDA_ALLOW_LOGIN_WRITE=1 — стоп до загрузки уровня
    import sys as _sys, os as _os
    _sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
    from asda_headless_tool import forbid_login_write   # ★ волна 3 п.15: каркас headless-инструмента (внутри — asda_login_guard, правило то же)
    forbid_login_write(__file__)   # имя файла = прежний литерал 'asda_set_login_lighting.py'
    les=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not les.load_level(LVL): raise RuntimeError("не загрузился %s"%LVL)
    sun=None; sky=None
    for a in eas.get_all_level_actors():
        if isinstance(a,unreal.DirectionalLight) and (sun is None or a.get_actor_label()=="Sun_Login"): sun=a
        elif isinstance(a,unreal.SkyLight) and (sky is None or a.get_actor_label()=="Sky_Login"): sky=a
    if sun is None: raise RuntimeError("в Login нет DirectionalLight (Sun_Login) — сначала asda_convert_chars_lit.py")
    before=sun.get_actor_rotation()
    # ★ Явно по именам полей — та самая грабля позиционного Rotator(roll,pitch,yaw).
    rot=unreal.Rotator(); rot.pitch=SUN_PITCH; rot.yaw=SUN_YAW; rot.roll=0.0
    sun.set_actor_rotation(rot,False)
    dc=sun.get_component_by_class(unreal.DirectionalLightComponent)
    dc.set_editor_property("mobility",unreal.ComponentMobility.MOVABLE)   # как на всех картах (Lightmass запрещён)
    if SUN_LUX: dc.set_intensity(float(SUN_LUX))
    sun.set_actor_label("Sun_Login")
    if sky is not None:
        sc=sky.get_component_by_class(unreal.SkyLightComponent)
        sc.set_editor_property("mobility",unreal.ComponentMobility.MOVABLE)
        sc.set_editor_property("real_time_capture",False)
        if SKY_LUX: sc.set_intensity(float(SKY_LUX))
        sky.set_actor_label("Sky_Login")
    if os.environ.get("ASDA_LOGIN_LEGACY_UNLIT","0")=="1": legacy_unlit_params(eas)
    les.save_current_level()
    after=sun.get_actor_rotation(); fwd=sun.get_actor_forward_vector()
    unreal.log("СВЕТ Login: Sun_Login rot было (p%.1f y%.1f r%.1f) → стало (p%.1f y%.1f r%.1f), forward=(%.3f,%.3f,%.3f), сила=%.2f | Sky=%s"
               %(before.pitch,before.yaw,before.roll,after.pitch,after.yaw,after.roll,fwd.x,fwd.y,fwd.z,dc.get_editor_property("intensity"),
                 ("%.2f"%sky.get_component_by_class(unreal.SkyLightComponent).get_editor_property("intensity")) if sky else "нет"))
    unreal.log("ЗАВЕРШЕНО (LOGINLIGHT-FINISHED)")   # ASCII-маркер для run_headless_safe.py


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
