# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_npc_spawn.py — расстановка NPC в UE-уровне карты.
# Источник: <export>/maps/<map>/npc_manifest.json (build_npc_manifest.py: резолв модели npc_list.BIN +
#   серверные позиции NpcData_Info\<Map>.npc (тайл×100+50) + высота Y сэмплом террейна mapraw).
# Модели NPC уже импортированы в /Game/AsdaAssets/Models/NPC/<model>/SkeletalMeshes/<model>
#   (asda_fix_npc_materials.py, glb через Interchange = ×100 → актор scale 0.01).
# Делает: грузит уровень /Game/Map/<Name>/<Name>, спавнит SkeletalMeshActor на каждый NPC с
#   трансформом game→UE (та же place_xform что у анимаций карты) + поворот по facing, idle-анимация (loop),
#   тег "AsdaNPC" для идемпотентности. Сохраняет уровень.
# Запуск headless ПОСЛЕ asda_build_map_level.py: env ASDA_MAP (по умолч alpen), ASDA_RAW.
import os, json, math, re, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_coords as _C
import asda_ue_paths as _U
import asda_maps as _MAPS

MAP_NAME = os.environ.get("ASDA_MAP", "alpen")
RAW_DIR  = os.environ.get("ASDA_RAW", r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps")
NPC_ROOT = _U.G_MODELS_NPC
NPC_TAG  = "AsdaNPC"

EAL = unreal.EditorAssetLibrary
AR  = unreal.AssetRegistryHelpers.get_asset_registry()
ML  = unreal.MathLibrary

# ----- конверсия game→UE (идентична asda_build_map_anim.place_xform) -----
SCALE = 1.0
C  = [0,0,-1, 1,0,0, 0,1,0]
G3 = [0.01, 0.0, 0.0,  0.0, 0.0, 0.01,  0.0, -0.01, 0.0]   # 0.01·Rx(-90°): glTF-Interchange round-trip
matmul = _C.matmul3   # 3×3 row-major (общая реализация asda_coords; дедуп №2 Т19)
def _finite16(m): return all(x==x and -1e30<x<1e30 for x in m)
def place_xform(m):
    if not _finite16(m): return None
    AT=[m[0],m[4],m[8], m[1],m[5],m[9], m[2],m[6],m[10]]
    M=matmul(matmul(C, AT), G3)
    c0=[M[0],M[3],M[6]]; c1=[M[1],M[4],M[7]]; c2=[M[2],M[5],M[8]]
    def dot(a,b): return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]
    def nrm(a): return math.sqrt(dot(a,a))
    n0=nrm(c0)
    if n0<1e-6: return None
    e0=[c/n0 for c in c0]
    u1=[c1[i]-e0[i]*dot(c1,e0) for i in range(3)]; n1=nrm(u1)
    if n1<1e-6: return None
    e1=[c/n1 for c in u1]
    u2=[c2[i]-e0[i]*dot(c2,e0)-e1[i]*dot(c2,e1) for i in range(3)]; n2=nrm(u2)
    if n2<1e-6: return None
    e2=[c/n2 for c in u2]
    det=(e0[0]*(e1[1]*e2[2]-e1[2]*e2[1]) - e0[1]*(e1[0]*e2[2]-e1[2]*e2[0]) + e0[2]*(e1[0]*e2[1]-e1[1]*e2[0]))
    s=(n0*n1*n2)**(1.0/3.0)
    sx=-s if det<0 else s; sy=s; sz=s
    if det<0: e0=[-v for v in e0]
    rot=ML.make_rotation_from_axes(unreal.Vector(*e0), unreal.Vector(*e1), unreal.Vector(*e2))
    loc=_C.to_ue_mat_translation(m, SCALE)   # ★ единая конверсия осей (asda_coords)
    return unreal.Transform(loc, rot, unreal.Vector(sx, sy, sz))

def game_xform(gx, gy, gz, face_deg):
    # game-матрица: поворот вокруг вертикали (game Y) на face, трансляция = мир-позиция. Прогон через place_xform.
    # +180°: NPC модели в bind-позе смотрят в противоположную сторону (подтв. визуально юзером на alpen).
    th=math.radians(face_deg + 180.0); cs=math.cos(th); sn=math.sin(th)
    m=[cs,0.0, sn, 0.0,  0.0,1.0,0.0,0.0,  -sn,0.0,cs,0.0,  gx,gy,gz,1.0]
    return place_xform(m)

# ----- индекс ассетов NPC (SkeletalMesh + idle AnimSequence) по имени модели -----
def build_npc_index():
    skel={}; anim={}
    for ad in AR.get_assets_by_path(unreal.Name(NPC_ROOT), recursive=True):
        cls=str(ad.asset_class_path.asset_name); nm=str(ad.asset_name); pkg=str(ad.package_name)
        if cls=="SkeletalMesh":
            skel[nm.lower()]=pkg
        elif cls=="AnimSequence":
            # модель = имя ассета до 'anim_NN'/'_Anim' суффикса
            key=re.sub(r'(anim_\d+|_Anim)$','',nm,flags=re.I).lower()
            anim.setdefault(key,[]).append((nm,pkg))
    return skel, anim

def pick_idle(model_l, anim_idx):
    cands=anim_idx.get(model_l) or []
    if not cands: return None
    # предпочесть idle: '...anim_00' → иначе '..._Anim' → иначе первый. Возвращает package_path.
    for nm,pkg in sorted(cands):
        if nm.lower().endswith("anim_00"): return pkg
    for nm,pkg in sorted(cands):
        if nm.lower().endswith("_anim"): return pkg
    return sorted(cands)[0][1]

def main():
    man_path=os.path.join(RAW_DIR, MAP_NAME, "npc_manifest.json")
    if not os.path.exists(man_path):
        unreal.log_warning("Нет npc_manifest.json: %s (запусти build_npc_manifest.py)" % man_path); return
    man=json.load(open(man_path, encoding="utf-8"))
    npcs=man.get("npc") or []
    if not npcs:
        unreal.log("Карта %s: NPC нет." % MAP_NAME); return

    skel_idx, anim_idx = build_npc_index()
    les=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    # ★ 16.09.2026 (ревизия раздела 22, СРЕДН-2): имя карты приходит из ASDA_MAP и НИ С ЧЕМ не сверяется
    #   (asda_maps.map_asset_name только чистит символы) — ASDA_MAP=Login дал бы пути сцены логина. Уровень
    #   логина открывает только ASDA_ALLOW_LOGIN_MAP_WRITE=1 (единое правило, asda_login_guard).
    import asda_login_guard as _LG
    # ★ 16.09.2026 (ревизия §7, п. 5): плюс сверка имени карты с реестром (Tools/data/asda_map_registry.json) —
    #   опечатка в ASDA_MAP иначе молча даёт произвольный корень /Game/Map/<мусор>.
    if _LG.login_map_write_blocked(DEST_ROOT, "asda_build_npc_spawn.py") or \
       _LG.map_name_blocked(DEST_ROOT, "asda_build_npc_spawn.py"): return
    lvl=DEST_ROOT + "/" + ASSET
    if not EAL.does_asset_exist(lvl):
        unreal.log_error("Уровень не найден: %s (сначала asda_build_map_level.py)" % lvl); return
    les.load_level(lvl)

    # снести старые NPC-акторы (идемпотентность; только наши, по тегу — не трогаем аним-объекты карты)
    removed=0
    for a in list(eas.get_all_level_actors()):
        try:
            if a.actor_has_tag(NPC_TAG):
                eas.destroy_actor(a); removed+=1
        except Exception: pass

    def set_anim(comp, anim):
        comp.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)
        ad=unreal.SingleAnimationPlayData()
        ad.set_editor_property("anim_to_play", anim)
        ad.set_editor_property("saved_looping", True)
        ad.set_editor_property("saved_playing", True)
        comp.set_editor_property("animation_data", ad)

    placed=0; no_asset=0; bad_xf=0; no_anim=0
    for o in npcs:
        model=o.get("model")
        if not model:
            no_asset+=1; continue
        sk=skel_idx.get(model.lower())
        if not sk:
            no_asset+=1; unreal.log_warning("NPC модель без ассета: %s" % model); continue
        xf=game_xform(o["gx"], o["gy"], o["gz"], o.get("face",0))
        if xf is None:
            bad_xf+=1; continue
        skm=EAL.load_asset(sk)
        act=eas.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0,0,0))
        comp=act.skeletal_mesh_component
        (comp.set_skeletal_mesh_asset(skm) if hasattr(comp,"set_skeletal_mesh_asset")
         else comp.set_skeletal_mesh(skm))
        an_pkg=pick_idle(model.lower(), anim_idx)
        anim_asset=EAL.load_asset(an_pkg) if an_pkg else None
        if anim_asset:
            try: set_anim(comp, anim_asset)
            except Exception as e: unreal.log_warning("anim %s: %s" % (model, e)); no_anim+=1
        else:
            no_anim+=1
        act.set_actor_transform(xf, False, False)
        act.tags=[unreal.Name(NPC_TAG)]
        try: act.set_actor_label("NPC_%s_%d" % (model, o.get("idx",placed)))
        except Exception: pass
        placed+=1

    les.save_current_level()
    unreal.log("NPC ГОТОВО [%s]: размещено=%d, снесено старых=%d, без ассета=%d, без анима=%d, мусор-xform=%d" %
               (MAP_NAME, placed, removed, no_asset, no_anim, bad_xf))


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global ASSET, DEST_ROOT
    ASSET    = _MAPS.map_asset_name(MAP_NAME)  # ★ единый санитайзер имени карты (asda_maps.map_asset_name)
    DEST_ROOT= "/Game/Map/" + ASSET
    main()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
