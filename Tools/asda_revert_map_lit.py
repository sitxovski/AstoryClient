# Copyright (c) 2026 @sitxovski (Discord)
# ★★★ УСТАРЕЛО (2026-09-03, правка Д-1 аудита света): скрипт относится к СТАРОЙ unlit-модели
#   (per-map LightColor/LightDir/Ambient в MIC) и берёт направление света из .mapraw 'lightDir' —
#   вектор НЕВЕРНОЙ формулы (ошибка 30..90° на всех картах). Lit-мастера этих параметров не читают.
#   НЕ ЗАПУСКАТЬ. Актуальный свет карт: asda_convert_map_lit.py (sunDir из .mapraw ver>=3).
# asda_revert_map_lit.py — откат карты с UE-света обратно на рабочий UNLIT per-map (как остальные карты).
#   1) репарент MIC: M_AsdaObjectLit→M_AsdaObject, M_AsdaTerrainLit→M_AsdaTerrain;
#   2) убрать Lit-актёры (Sun_/Sky_/PP_ = DirectionalLight/SkyLight/PostProcessVolume);
#   3) вернуть per-map значения материалов (Ambient=0.6, LightColor=scene.ambient, LightDir).
#   Запуск: ASDA_MAP=alpen run_headless.bat asda_revert_map_lit.py
import os, unreal
import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_mapraw as _MR                                  # ★ единый ридер .mapraw (был свой read_scene)
import asda_maps as _MAPS                                  # ★ единый санитайзер имени карты
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
EAL=unreal.EditorAssetLibrary; MEL=unreal.MaterialEditingLibrary
ar=unreal.AssetRegistryHelpers.get_asset_registry()
MAP=os.environ.get("ASDA_MAP","alpen"); RAW=os.environ.get("ASDA_RAW",r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps")
OBJ="/Game/Materials/M_AsdaObject"; TER="/Game/Materials/M_AsdaTerrain"

def read_scene(path):
    sc=_MR.read_scene(path)                                # strict=True — как прежний assert на 'AMRW'
    return sc["ambient"], sc["lightDir"]

def main():
    # ★ 16.09.2026 (ревизия раздела 22, СРЕДН-2): имя карты приходит из ASDA_MAP и НИ С ЧЕМ не сверяется
    #   (asda_maps.map_asset_name только чистит символы) — ASDA_MAP=Login дал бы пути сцены логина. Уровень
    #   логина открывает только ASDA_ALLOW_LOGIN_MAP_WRITE=1 (единое правило, asda_login_guard).
    #   Репарент ниже идёт по ВСЕМ MIC под /Game/Map/<ASSET> с сохранением каждого, дальше — load_level и
    #   save_current_level; при ASDA_MAP=Login это были бы пакеты и уровень сцены логина.
    import asda_login_guard as _LG
    # ★ 16.09.2026 (ревизия §7, п. 5): плюс сверка имени карты с реестром (Tools/data/asda_map_registry.json) —
    #   опечатка в ASDA_MAP иначе молча даёт произвольный корень /Game/Map/<мусор>.
    if _LG.login_map_write_blocked(LVL, "asda_revert_map_lit.py") or \
       _LG.login_map_write_blocked("/Game/Map/%s"%ASSET, "asda_revert_map_lit.py", quiet=True) or \
       _LG.map_name_blocked(LVL, "asda_revert_map_lit.py"): return
    amb,ld=read_scene(os.path.join(RAW,MAP+".mapraw"))
    unlit_obj=EAL.load_asset(OBJ); unlit_ter=EAL.load_asset(TER)
    lcol=unreal.LinearColor(amb[0],amb[1],amb[2],0.0); ldcol=unreal.LinearColor(ld[0],ld[1],ld[2],0.0)
    n_o=n_t=0
    for ad in ar.get_assets_by_path("/Game/Map/%s"%ASSET, recursive=True):
        if str(ad.asset_class_path.asset_name)!="MaterialInstanceConstant": continue
        mic=EAL.load_asset(str(ad.package_name)); par=mic.get_editor_property("parent")
        if not par: continue
        pn=par.get_name(); tgt=None
        if pn=="M_AsdaObjectLit": mic.set_editor_property("parent",unlit_obj); tgt="obj"; n_o+=1
        elif pn=="M_AsdaTerrainLit": mic.set_editor_property("parent",unlit_ter); tgt="ter"; n_t+=1
        elif pn in ("M_AsdaObject","M_AsdaTerrain"): tgt="keep"
        else: continue
        MEL.set_material_instance_scalar_parameter_value(mic,"Ambient",0.6)
        MEL.set_material_instance_vector_parameter_value(mic,"LightColor",lcol)
        MEL.set_material_instance_vector_parameter_value(mic,"LightDir",ldcol)
        EAL.save_loaded_asset(mic)
    # убрать Lit-актёры
    les=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(LVL); ndel=0
    for a in list(eas.get_all_level_actors()):
        if isinstance(a,(unreal.DirectionalLight,unreal.SkyLight,unreal.PostProcessVolume)):
            eas.destroy_actor(a); ndel+=1
    les.save_current_level()
    unreal.log("REVERT %s: репарент obj=%d ter=%d, актёров удалено=%d, per-map возвращён (Ambient=0.6, LightColor=%s)"
               %(MAP,n_o,n_t,ndel,tuple(round(x,2) for x in amb)))


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global ASSET, LVL
    ASSET=_MAPS.map_asset_name(MAP); LVL=_U.level_path(MAP)
    main()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
