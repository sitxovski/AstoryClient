# Copyright (c) 2026 @sitxovski (Discord)
# asda_place_npcs.py — запекание NPC карты в UE-уровень как полноценных AAsdaNpcActor (Editor-Friendly).
# Источники (авторские данные, тот же файл читает СЕРВЕР — позиции 1:1 с онлайном):
#   export/maps/<map>/npc_manifest.json — из серверного NpcData_Info\<Map>.npc (asda_build_npc_manifest.py):
#     type-id, модель (резолв npc_list.BIN), мир gx/gz=тайл*100+50, gy=снап террейна, face°.
#   Data\NPC\npc_list.BIN — отображаемое имя (+4) и тип-функции (+0x7C) по строке id-1 (stride 0x88).
# Поворот: game-матрица rotY(face°) → UE через ТУ ЖЕ конверсию, что у map-объектов/эффектов
#   (place_transform из asda_place_effects.py: C·Aᵀ·Cᵀ; конверсия осей (x,y,z)→(-z,x,y)).
# Сетевой флоу НЕ ломается: при входе в мир спавн 0xFAD «усыновит» этих акторов по NpcId (см. AsdaNpcActor.h).
# Запуск: run_headless.bat asda_place_npcs.py   (ASDA_MAP=alpen по умолчанию)
import os, re, json, math, struct, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_coords as _C
import asda_ue_paths as _U
import asda_paths as _P
import asda_maps as _MAPS

MAP_NAME = os.environ.get("ASDA_MAP", "alpen")
MANIFEST = r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps/%s/npc_manifest.json" % MAP_NAME
NPC_LIST = _P.NPC_LIST_BIN
NPC_ROOT = _U.G_MODELS_NPC

EAL = unreal.EditorAssetLibrary
ML  = unreal.MathLibrary
def log(m): unreal.log("[place_npcs] " + str(m))

# ── конверсия трансформа: 1:1 asda_place_effects.place_transform ──
C  = [0,0,-1, 1,0,0, 0,1,0]
CT = [0,1,0, 0,0,1, -1,0,0]
matmul = _C.matmul3   # 3×3 row-major (общая реализация asda_coords; дедуп №2 Т19)
to_ue_v = _C.to_ue_v   # ★ единая конверсия осей (asda_coords)

# ★ Масштаб NPC: glb авторски в game-units (=см, рост LuaLua 155.5), но UE glTF-импорт считает
#   единицы МЕТРАМИ (×100) — так же были запечены меши login-сцены → 0.01.
#   (★ 12.09.2026: САМА login-сцена больше не ×100 — переведена в ×1, как карты; речь только о том,
#    что glTF-импорт запекает ×100 в ВЕРШИНЫ меша, и это компенсируется масштабом актора.)
NPC_SCALE = 0.01

# Базовый доворот модели NPC вокруг вертикали (UE yaw): импортированная glTF-модель «смотрит» на 90°
#   правее, чем ориентация face° оригинала (подтверждено визуально на alpen) — доворачиваем влево.
NPC_YAW_OFFSET = -90.0

def place_transform(pos, m):
    AT=[m[0],m[4],m[8], m[1],m[5],m[9], m[2],m[6],m[10]]
    R=matmul(matmul(C,AT),CT)
    c0=(R[0],R[3],R[6]); c1=(R[1],R[4],R[7]); c2=(R[2],R[5],R[8])
    fwd=unreal.Vector(*c0); rgt=unreal.Vector(*c1); up=unreal.Vector(*c2)
    rot=ML.make_rotation_from_axes(fwd,rgt,up)
    rot=ML.compose_rotators(rot, unreal.Rotator(0.0, 0.0, NPC_YAW_OFFSET))   # (roll,pitch,yaw)
    return unreal.Transform(to_ue_v(pos), rot, unreal.Vector(NPC_SCALE, NPC_SCALE, NPC_SCALE))

def roty_matrix(deg):
    """game D3D RotationY(θ), row-major 4x4 (row-vectors): оси X=(c,0,-s) Y=(0,1,0) Z=(s,0,c)."""
    r=math.radians(deg); c=math.cos(r); s=math.sin(r)
    return [c,0,-s,0,  0,1,0,0,  s,0,c,0,  0,0,0,1]

# ── npc_list.BIN: имя (+4, cp1251) и тип-функции (+0x7C) по id ──
def load_npc_list():
    b=open(NPC_LIST,"rb").read()
    # Магия: у ряда клиентов "GCM=" (0x3D4D4347), у Interlude — 0x3DCCCCCD (float 0.1, версия).
    #   Валидируем структурой: (size-0x14) кратен 0x88 и равен count.
    count=struct.unpack_from("<I", b, 4)[0]
    assert (len(b)-0x14) % 0x88 == 0 and (len(b)-0x14)//0x88 == count, \
        "npc_list.BIN: структура не 0x14+N*0x88 (count=%d size=%d)" % (count, len(b))
    base=0x14; stride=0x88
    rows={}
    for i in range(count):
        r=base+i*stride
        if r+stride>len(b): break
        rid=struct.unpack_from("<H", b, r)[0]
        raw=b[r+4:r+0x46].split(b"\0")[0]
        name=raw.decode("cp1251","replace")
        typefunc=struct.unpack_from("<I", b, r+0x7C)[0]
        rows[rid]={"name":name,"typefunc":typefunc}
    return rows

def _xyz(v):
    """Вектор UE → список из трёх чисел (для отчёта)."""
    return [round(float(v.x), 3), round(float(v.y), 3), round(float(v.z), 3)]

def patch_positions(data, eas, les):
    """ПАТЧ-РЕЖИМ: сдвинуть существующих NPC-акторов в позиции манифеста, не пересоздавая их.
    Отчёт (все акторы ДО/ПОСЛЕ + список сдвинутых) дописывается в JSON из ASDA_NPC_PATCH_REPORT."""
    acts = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.AsdaNpcActor)]
    by_idx = {}
    for a in acts:
        m = re.match(r"NPC_(\d+)_", a.get_actor_label())
        if m: by_idx.setdefault(int(m.group(1)), []).append(a)
    before = {a.get_actor_label(): _xyz(a.get_actor_location()) for a in acts}
    moved = []; same = 0; lost = []
    for n in data["npc"]:
        cand = [a for a in by_idx.get(n["idx"], []) if int(a.get_editor_property("npc_id")) == int(n["type"])]
        if len(cand) != 1:
            lost.append(n["idx"]); unreal.log_warning("[place_npcs] патч: нет однозначного актора idx=%d type=%d (%d)"
                                                      % (n["idx"], n["type"], len(cand))); continue
        a = cand[0]
        new = place_transform((n["gx"], n["gy"], n["gz"]), roty_matrix(n["face"])).translation
        old = a.get_actor_location()
        d = ((new.x-old.x)**2 + (new.y-old.y)**2 + (new.z-old.z)**2) ** 0.5
        if d > 0.01:
            a.set_actor_location(new, False, False)
            moved.append({"idx": n["idx"], "type": n["type"], "model": n["model"], "label": a.get_actor_label(),
                          "old": _xyz(old), "new": _xyz(a.get_actor_location()), "dist": round(d, 3)})
        else:
            same += 1
    after = {a.get_actor_label(): _xyz(a.get_actor_location()) for a in acts}
    if moved: les.save_current_level()
    log("ПАТЧ %s: акторов %d, сдвинуто %d, без изменений %d, не найдено %d"
        % (MAP_NAME, len(acts), len(moved), same, len(lost)))
    rp = os.environ.get("ASDA_NPC_PATCH_REPORT", "")
    if rp:
        rep = json.load(open(rp, encoding="utf-8")) if os.path.isfile(rp) else {}
        rep[MAP_NAME] = {"actors": len(acts), "moved": moved, "same": same, "lost": lost,
                         "before": before, "after": after}
        json.dump(rep, open(rp, "w", encoding="utf-8"), ensure_ascii=False, indent=1)

def main():
    # ★ 16.09.2026 (ревизия раздела 22, СРЕДН-2): имя карты приходит из ASDA_MAP и НИ С ЧЕМ не сверяется
    #   (asda_maps.map_asset_name только чистит символы) — ASDA_MAP=Login дал бы пути сцены логина. Уровень
    #   логина открывает только ASDA_ALLOW_LOGIN_MAP_WRITE=1 (единое правило, asda_login_guard).
    import asda_login_guard as _LG
    # ★ 16.09.2026 (ревизия §7, п. 5): плюс сверка имени карты с реестром (Tools/data/asda_map_registry.json) —
    #   опечатка в ASDA_MAP иначе молча даёт произвольный корень /Game/Map/<мусор>.
    if _LG.login_map_write_blocked(LEVEL, "asda_place_npcs.py") or \
       _LG.map_name_blocked(LEVEL, "asda_place_npcs.py"): return
    if not EAL.does_asset_exist(LEVEL):
        unreal.log_error("Нет уровня %s" % LEVEL); return
    data=json.load(open(MANIFEST, encoding="utf-8"))
    rows=load_npc_list()
    log("карта %s: NPC=%d (из %s), npc_list=%d строк" % (MAP_NAME, data["count"], data["npcFile"], len(rows)))

    les=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(LEVEL)

    # ★★★ 2026-09-11 ПАТЧ-РЕЖИМ (ASDA_MAP_PATCH_ONLY=1): акторы НЕ пересоздаются (ручные правки,
    #   ссылки и метки уровня сохраняются), у существующих AsdaNpcActor переписывается ТОЛЬКО позиция,
    #   и только если она разошлась с манифестом. Сопоставление: метка NPC_<idx>_ + npc_id == type.
    #   Нужен после правки манифестов (XZ типов 1/3/5 из npc_list +0x70/+0x74, клампы [100, 51100]).
    if os.environ.get("ASDA_MAP_PATCH_ONLY", "") == "1":
        patch_positions(data, eas, les); return

    # идемпотентность: снести прежних NPC-акторов уровня
    old=[a for a in eas.get_all_level_actors() if isinstance(a, unreal.AsdaNpcActor)]
    for a in old: eas.destroy_actor(a)
    if old: log("удалено прежних NPC-акторов: %d" % len(old))

    placed=missing=0
    for n in data["npc"]:
        model=n["model"]
        mesh_path="%s/%s/SkeletalMeshes/%s" % (NPC_ROOT, model, model)
        # Идл = клип _0.jbb (эталон effectListUpdateType14): многоклиповые модели → "<model>anim_00",
        #   одноклиповые (тип 1/3, один jbb) → "<model>_Anim".
        anim_path=None
        for cand in (mesh_path+"anim_00", mesh_path+"_Anim"):
            if EAL.does_asset_exist(cand): anim_path=cand; break
        if not EAL.does_asset_exist(mesh_path):
            unreal.log_warning("нет меша: %s (type=%d idx=%d)" % (mesh_path, n["type"], n["idx"]))
            missing+=1; continue
        xf=place_transform((n["gx"], n["gy"], n["gz"]), roty_matrix(n["face"]))
        act=eas.spawn_actor_from_class(unreal.AsdaNpcActor, xf.translation)
        act.set_actor_transform(xf, False, False)
        act.set_editor_property("npc_id", n["type"])
        act.set_editor_property("tile_x", n["tileX"])
        act.set_editor_property("tile_z", n["tileZ"])
        act.set_editor_property("facing_deg", n["face"])
        row=rows.get(n["type"], {})
        act.set_editor_property("npc_name", row.get("name",""))
        act.set_editor_property("type_func", row.get("typefunc",0))
        mesh=unreal.load_asset(mesh_path)
        act.mesh.set_skeletal_mesh(mesh)
        if anim_path:
            anim=unreal.load_asset(anim_path)
            act.set_editor_property("idle_anim", anim)
            # Идл виден и В РЕДАКТОРЕ (не Т-поза): назначаем single-node анимацию самому компоненту.
            #   BeginPlay в PIE переиграет её лупом (тот же клип).
            act.mesh.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)
            ad=act.mesh.get_editor_property("animation_data")   # FSingleAnimationPlayData
            ad.set_editor_property("anim_to_play", anim)
            for prop,val in (("saved_looping",True),("saved_playing",True)):
                try: ad.set_editor_property(prop, val)
                except Exception: pass
            act.mesh.set_editor_property("animation_data", ad)
        else:
            unreal.log_warning("нет идл-анима: %sanim_00 / _Anim" % mesh_path)
        # ★ Отключённый NPC (отрицательный тайл в .npc — сервер пропускает): скрыт в игре, виден в редакторе.
        dis = bool(n.get("disabled", 0))
        act.set_editor_property("disabled", dis)
        short=re.sub(r'^npc_[fm]_[a-z]_', '', model).replace('-a_00','')
        act.set_actor_label("NPC_%02d_%s%s" % (n["idx"], short, "_OFF" if dis else ""))
        act.set_folder_path("NPC/Disabled" if dis else "NPC")
        placed+=1
    les.save_current_level()
    log("ГОТОВО: размещено %d, без модели %d" % (placed, missing))


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global LEVEL
    ASSET    = _MAPS.map_asset_name(MAP_NAME)  # ★ единый санитайзер имени карты (asda_maps.map_asset_name)
    LEVEL    = _U.level_path(MAP_NAME)   # ★ волна 2 п. 7: путь уровня — реестр путей
    main()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
