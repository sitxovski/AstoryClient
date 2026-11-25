# Copyright (c) 2026 @sitxovski (Discord)
# asda_add_uvscroll.py — UV-скролл поверхностей (вода/лава/водопады) на существующих уровнях БЕЗ ребилда.
# Инстансы со скоростью mesh+0x260 (mapraw) матчатся по МИРОВОЙ позиции в ISM-компонентах уровня; на
# найденный компонент ставится M_AsdaObjectScroll (Panner от PerInstanceCustomData) + скорость в custom-data
# инстанса. Non-scroll инстансы того же компонента получают custom-data 0 → не скроллятся (вид не меняется).
import os, math, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_coords as _C
import sys as _sys
import asda_mapraw as _MR                       # ★ единый ридер .mapraw (был свой class R)
import asda_maps as _MAPS                       # ★ единый санитайзер имени карты
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
import asda_paths as _P

RAW_DIR = _P.EXPORT_MAPS
SCROLL_MASTER = "/Game/Materials/M_AsdaObjectScrollLit"   # ★ 03.09.2026: Lit-мастер (fix_scroll_objects_lit.md); unlit M_AsdaObjectScroll оставлен, 0 инстансов
SPEED_SCALE_DEFAULT = 300.0                     # магнитуда приблизит. (направление 1:1 с диска; скролл-скорость движка тюнится)
EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
ML = unreal.MathLibrary
_asset = _MAPS.map_asset_name                   # ★ единый санитайзер имени карты
to_ue = _C.to_ue   # ★ единая конверсия осей (asda_coords)

C = [0,0,-1, 1,0,0, 0,1,0]; CT = [0,1,0, 0,0,1, -1,0,0]
matmul = _C.matmul3   # 3×3 row-major (общая реализация asda_coords; дедуп №2 Т19)
def inst_loc(m):
    return to_ue(m[12], m[13], m[14])

# Свой class R жил здесь и отличался от прочих копий одним: s_ возвращал СЫРЫЕ БАЙТЫ (без decode).
# В едином модуле это метод `sb`; строки тут всё равно только пропускаются, но поведение сохранено 1:1.
# Магию 'AMRW' этот тул исторически НЕ проверял (только размер файла в main) — оставлено как было.
R = _MR.R

def read_scroll_instances(path):
    r=R(open(path,"rb").read()); r.o=4
    r.u32(); r.sb(); r.u32(); r.f32(); r.f32(); r.fa(3); r.fa(3); r.fa(3)
    pc=r.u32(); ic=r.u32(); r.o+=2*ic
    for _ in range(pc):
        vtx=r.u32()
        if vtx>0: r.o+=vtx*(3*4+2*4+2*4+4)
        r.u32(); r.u32(); r.f32(); r.f32(); r.sb(); r.f32(); r.sb(); r.f32()
    mc=r.u32()
    for _ in range(mc):
        r.sb(); r.sb(); v=r.u32(); r.o+=v*8*4; icx=r.u32(); r.o+=icx*4
    ni=r.u32(); out=[]
    for _ in range(ni):
        r.u32(); m=r.fa(16); r.i32(); r.i32(); uv=r.fa(4)
        if any(abs(x)>1e-7 for x in uv[:2]):
            out.append((inst_loc(m), uv))
    return out

def main():
    import asda_login_guard as _LG   # ★ 16.09.2026 (ревизия раздела 22, СРЕДН-2): гейт уровня логина в цикле ниже
    mp = os.environ.get("ASDA_MAP")
    maps = [mp] if mp else None
    lvls = []
    for m in (maps or [d for d in os.listdir(RAW_DIR) if os.path.isfile(os.path.join(RAW_DIR, d+".mapraw"))]):
        raw = os.path.join(RAW_DIR, m+".mapraw")
        if not os.path.isfile(raw) or os.path.getsize(raw) < 1000: continue
        sc = read_scroll_instances(raw)
        if sc: lvls.append((m, sc))
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    master = EAL.load_asset(SCROLL_MASTER)
    for m, targets in lvls:
        lvl = _U.level_path(m)
        # ★ 16.09.2026 (ревизия раздела 22, СРЕДН-2): имя карты приходит из ASDA_MAP и НИ С ЧЕМ не сверяется
        #   (asda_maps.map_asset_name только чистит символы) — ASDA_MAP=Login дал бы пути сцены логина. Уровень
        #   логина открывает только ASDA_ALLOW_LOGIN_MAP_WRITE=1 (единое правило, asda_login_guard).
        #   Здесь важно ещё и то, что MIC создаются в /Game/Map/<карта>/ScrollMat — под деревом логина это его пакеты.
        # ★ 16.09.2026 (ревизия §7, п. 5): плюс сверка имени карты с реестром (Tools/data/asda_map_registry.json) —
        #   опечатка в ASDA_MAP иначе молча даёт произвольный корень /Game/Map/<мусор> (здесь — ещё и MIC в ScrollMat).
        if _LG.login_map_write_blocked(lvl, "asda_add_uvscroll.py") or \
           _LG.login_map_write_blocked("/Game/Map/%s" % _asset(m), "asda_add_uvscroll.py", quiet=True) or \
           _LG.map_name_blocked(lvl, "asda_add_uvscroll.py"): continue
        if not EAL.does_asset_exist(lvl): continue
        les.load_level(lvl)
        # индекс целей по округлённой позиции
        key = lambda v: (round(v.x,0), round(v.y,0), round(v.z,0))
        tgt = {key(loc): uv for loc, uv in targets}
        matdir = "/Game/Map/%s/ScrollMat" % _asset(m)
        _miccache = {}
        applied = 0; comps_done = set()
        for act in eas.get_all_level_actors():
            for comp in act.get_components_by_class(unreal.InstancedStaticMeshComponent):
                n = comp.get_instance_count()
                hits = []
                for i in range(n):
                    t = comp.get_instance_transform(i, True)
                    uv = tgt.get(key(t.translation))
                    if uv: hits.append((i, uv))
                if not hits: continue
                # компонент содержит скролл-инстансы → custom-data + scroll-материал
                comp.set_num_custom_data_floats(2)
                for i in range(n): comp.set_custom_data_value(i, 0, 0.0); comp.set_custom_data_value(i, 1, 0.0)
                for i, uv in hits:
                    comp.set_custom_data_value(i, 0, uv[0]); comp.set_custom_data_value(i, 1, uv[1])
                # текстура из текущего материала слота 0
                cur = comp.get_material(0)
                texname = "obj"
                if isinstance(cur, unreal.MaterialInstanceConstant):
                    tv = MEL.get_material_instance_texture_parameter_value(cur, "Tex")
                    if tv: texname = tv.get_name()
                micp = "%s/MI_scroll_%s" % (matdir, _asset(texname))
                if micp in _miccache: mic = _miccache[micp]
                elif EAL.does_asset_exist(micp): mic = EAL.load_asset(micp)
                else:
                    mic = ATOOLS.create_asset(os.path.basename(micp), matdir, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
                    MEL.set_material_instance_parent(mic, master)
                    if isinstance(cur, unreal.MaterialInstanceConstant):
                        tv = MEL.get_material_instance_texture_parameter_value(cur, "Tex")
                        if tv: MEL.set_material_instance_texture_parameter_value(mic, "Tex", tv)
                    MEL.set_material_instance_scalar_parameter_value(mic, "SpeedScale", SPEED_SCALE_DEFAULT)
                    MEL.set_material_instance_scalar_parameter_value(mic, "Ambient", 1.0)
                    EAL.save_loaded_asset(mic)
                _miccache[micp] = mic
                comp.set_material(0, mic)
                applied += len(hits); comps_done.add(comp.get_name())
        les.save_current_level()
        unreal.log("UVSCROLL %-18s инстансов=%d, компонентов=%d" % (m, applied, len(comps_done)))
    unreal.log("=== UVSCROLL ГОТОВО ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
