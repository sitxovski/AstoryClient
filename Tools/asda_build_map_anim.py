# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_map_anim.py — импорт анимированных объектов карты (skeletal glb) в уровень.
# Источник: <export>/<map>/anim/clip_*.glb + <export>/<map>/anim_manifest.json (asda_mapanim_to_gltf.py).
# Делает: импорт glb (SkeletalMesh+Skeleton+AnimSequence) в /Game/Map/<Name>/Anims/clip_<c>/, материалы
# (MIC→текстуры карты), расстановка SkeletalMeshActor по objPlace с проигрыванием анимации (loop).
# Запуск headless ПОСЛЕ asda_build_map_level.py: env ASDA_MAP (по умолч alpen).
import os, json, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_coords as _C
import asda_maps as _MAPS
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)

import re as _re
MAP_NAME = os.environ.get("ASDA_MAP", "alpen")
RAW_DIR  = os.environ.get("ASDA_RAW", r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps")
OBJ_MASTER = _U.M_OBJECT_LIT   # ★ Lit-мастер (курс «современно» 2026-07-23): пересборка НЕ должна откатывать MIC на unlit
SCALE = 1.0

EAL  = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
AR   = unreal.AssetRegistryHelpers.get_asset_registry()
MEL  = unreal.MaterialEditingLibrary
ML   = unreal.MathLibrary
import re
def _san(t): return re.sub(r'[^A-Za-z0-9_]', '_', os.path.splitext(t)[0])

C  = [0,0,-1, 1,0,0, 0,1,0]
CT = [0,1,0, 0,0,1, -1,0,0]
matmul = _C.matmul3   # 3×3 row-major (общая реализация asda_coords; дедуп №2 Т19)
# glTF→UE Interchange импортирует ×100 (метры glTF → см UE), а статику строю напрямую (×1).
# Компенсируем масштабом актора GLTF_SCALE=0.01 (проверено: clip_0 bounds 153206 vs ожид. ~1534 = ×100).
GLTF_SCALE = float(os.environ.get("ASDA_ANIM_SCALE", "0.01"))
# ★ ТОЧНАЯ формула (доказано: bone-correspondence vs истина MapViewer, err=0.0000 на clip0/clip1, G постоянна).
# T = C · objPlace_col · G_full. C — та же game→UE конверсия что у статики (det=-1). G_full = 0.01·Rx(-90°) —
# ПОСТОЯННАЯ поправка glTF-кругорейса (z-negate→Interchange→×100). Отражение (det C=-1) выносим в scale.X<0:
# у статики два C (C·Aᵀ·Cᵀ, det+1) гасят отражение, у скелетных один C → отражение остаётся (это и есть «зеркало»).
G3 = [0.01, 0.0, 0.0,   0.0, 0.0, 0.01,   0.0, -0.01, 0.0]   # G_full 3x3 row-major = 0.01·Rx(-90°)
import math
def _finite16(m):
    return all(x == x and -1e30 < x < 1e30 for x in m)
def place_xform(m):
    # T = C·Aᵀ·G_full. Возвращает None для МУСОРНЫХ objPlace (NaN/огромные/вырожденные) — как matrix_ok у
    # статики (иначе невалидный поворот → краш ассерта кватерниона в UE).
    if not _finite16(m):
        return None
    AT = [m[0],m[4],m[8],  m[1],m[5],m[9],  m[2],m[6],m[10]]   # objPlace_col rotation = Aᵀ
    M = matmul(matmul(C, AT), G3)                              # C·Aᵀ·G_full (rotation*scale, row-major)
    c0=[M[0],M[3],M[6]]; c1=[M[1],M[4],M[7]]; c2=[M[2],M[5],M[8]]   # столбцы = базис трансформа
    def dot(a,b): return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]
    def nrm(a): return math.sqrt(dot(a,a))
    # ★ Грам-Шмидт: гарантирует ОРТОНОРМАЛЬНЫЙ базис (валидный кватернион) даже при неортогональном
    # objPlace (неравном scale/скосе). Для ортонормальных (rotation-only) даёт тот же результат (точно).
    n0=nrm(c0)
    if n0 < 1e-6: return None
    e0=[c/n0 for c in c0]
    u1=[c1[i]-e0[i]*dot(c1,e0) for i in range(3)]; n1=nrm(u1)
    if n1 < 1e-6: return None
    e1=[c/n1 for c in u1]
    u2=[c2[i]-e0[i]*dot(c2,e0)-e1[i]*dot(c2,e1) for i in range(3)]; n2=nrm(u2)
    if n2 < 1e-6: return None
    e2=[c/n2 for c in u2]
    det=(e0[0]*(e1[1]*e2[2]-e1[2]*e2[1]) - e0[1]*(e1[0]*e2[2]-e1[2]*e2[0]) + e0[2]*(e1[0]*e2[1]-e1[1]*e2[0]))
    # РАВНОМЕРНЫЙ масштаб (геом. среднее норм): неравномерный scale на скелетном компоненте → ensure
    # кватерниона при анимации. Для rotation-only/uniform objPlace n0=n1=n2 → точно; для редких
    # неравномерных — валидная аппроксимация (UE всё равно не представит шир/анизотропию точно).
    s = (n0 * n1 * n2) ** (1.0 / 3.0)
    sx = -s if det < 0 else s            # отражение (det C=-1) → scale.X<0, e0 флипаем (R→det+1)
    sy = s; sz = s
    if det < 0:
        e0 = [-v for v in e0]
    rot=ML.make_rotation_from_axes(unreal.Vector(*e0), unreal.Vector(*e1), unreal.Vector(*e2))
    loc=_C.to_ue_mat_translation(m, SCALE)   # ★ единая конверсия осей (asda_coords)
    return unreal.Transform(loc, rot, unreal.Vector(sx, sy, sz))

_tex_idx = None
# ★ ВОЛНА 2 (14.09.2026, дефект E11): индекс/поиск текстур, подвязка MIC, импорт и Nanite — из ЕДИНОЙ
#   библиотеки asda_model_import_ue. Пресеты повторяют прежние копии этого файла (правило дублей имён,
#   нормализация ключа) — найденная текстура прежняя; добавлено только снятие меток `_msk`/`_blend` там, где
#   раньше текстура не находилась (решение координатора, `Reports/dedup_audit/wave2_import.md` §2, §8).
import asda_model_import_ue as MI

def find_tex(texbase):
    # РЕГИСТРОНЕЗАВИСИМЫЙ поиск (имена анимаций в нижнем регистре 'pig', ассеты в верхнем 'PIG';
    # does_asset_exist регистрозависим). Индекс lower(имя)→путь по всей папке текстур карты
    # (asda_model_import_ue, пресет поиска «map_anim»: точное → без хвостовых цифр разом).
    global _tex_idx
    if _tex_idx is None:
        _tex_idx = MI.tex_index("all_last", root=TEX_ROOT)
    p = MI.find_tex(_san(texbase), _tex_idx, "map_anim")
    return EAL.load_asset(p) if p else None

_amat_cache = {}
def get_anim_material(texbase, master):
    # свежий MIC parent M_AsdaObject + Tex (как статика get_material). Дедуп по текстуре.
    base = _san(texbase) or "none"
    if base in _amat_cache:
        return _amat_cache[base]
    mp = DEST_ROOT + "/Anims/_mats/MI_" + base
    if EAL.does_asset_exist(mp):
        mic = EAL.load_asset(mp)
    else:
        mic = ATOOLS.create_asset("MI_" + base, DEST_ROOT + "/Anims/_mats",
                                  unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mic, master)
        tx = find_tex(texbase)
        if tx: MEL.set_material_instance_texture_parameter_value(mic, "Tex", tx)
        MEL.set_material_instance_scalar_parameter_value(mic, "Ambient", 1.0)
        EAL.save_loaded_asset(mic)
    _amat_cache[base] = mic
    return mic

def assets_in(folder, cls):
    out=[]
    for ad in AR.get_assets_by_path(unreal.Name(folder), recursive=True):
        if str(ad.asset_class_path.asset_name) == cls:
            out.append(str(ad.package_name))
    return out

def main():
    man_path = os.path.join(RAW_DIR, MAP_NAME, "anim_manifest.json")
    if not os.path.exists(man_path):
        unreal.log_error("Нет манифеста анимаций: %s" % man_path); return
    man = json.load(open(man_path, encoding="utf-8"))
    master = EAL.load_asset(OBJ_MASTER)
    # ★ M_AsdaObject создан для статики — без флага "Used with Skeletal Mesh" UE на скелетных мешах
    # подставляет ДЕФОЛТНЫЙ материал (без текстуры). Включаем флаг на мастере.
    try:
        if master and not master.get_editor_property("used_with_skeletal_mesh"):
            master.set_editor_property("used_with_skeletal_mesh", True)
            unreal.MaterialEditingLibrary.recompile_material(master)
            EAL.save_loaded_asset(master)
            unreal.log("M_AsdaObject: включён used_with_skeletal_mesh")
    except Exception as e:
        unreal.log_warning("skeletal usage flag: %s" % e)
    anims_root = DEST_ROOT + "/Anims"
    skip_import = os.environ.get("ASDA_SKIP_IMPORT", "0") == "1" and EAL.does_directory_exist(anims_root)
    if not skip_import and EAL.does_directory_exist(anims_root):
        EAL.delete_directory(anims_root)   # чистый импорт анимаций

    # 1) БАТЧ-импорт всех glb за один вызов (38 отдельных = медленно) → Anims/clip_<c>/
    glbs = []
    for c in man["clips"]:
        g = os.path.join(RAW_DIR, MAP_NAME, c["glb"].replace("/", os.sep))
        if os.path.exists(g): glbs.append(g)
    if glbs and not skip_import:
        d = unreal.AutomatedAssetImportData()
        d.set_editor_property("destination_path", anims_root)
        d.set_editor_property("filenames", glbs)
        d.set_editor_property("replace_existing", True)
        ATOOLS.import_assets_automated(d)
        EAL.save_directory(anims_root, only_if_is_dirty=True, recursive=True)
    unreal.log("Импорт skeletal glb: %s (%d)" % ("ПРОПУЩЕН" if skip_import else "завершён", len(glbs)))

    # ★ NO_PLACE: только обновить материалы на ассетах SkeletalMesh, НЕ трогая уровень/акторов/повороты.
    # Материалы лежат на ассете skm (skm.materials) — расстановка акторов для этого не нужна. Защищает
    # ручные правки трансформов от затирания при перепривязке текстур.
    no_place = os.environ.get("ASDA_NO_PLACE", "0") == "1"
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not no_place:
        les.load_level(DEST_ROOT + "/" + ASSET)
        # снести старые анимированные акторы (повторный прогон — иначе дубли/старые сломанные)
        for a in list(eas.get_all_level_actors()):
            if a.get_class().get_name() == "SkeletalMeshActor":
                try: eas.destroy_actor(a)
                except Exception: pass

    def set_anim(comp, anim):
        comp.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)
        ad = unreal.SingleAnimationPlayData()
        ad.set_editor_property("anim_to_play", anim)
        ad.set_editor_property("saved_looping", True)
        ad.set_editor_property("saved_playing", True)
        comp.set_editor_property("animation_data", ad)

    placed = 0; clips_ok = 0; skipped_bad = 0
    for c in man["clips"]:
        dest = "%s/clip_%d" % (anims_root, c["clip"])
        sk = assets_in(dest, "SkeletalMesh")
        if not sk:
            continue
        skm = EAL.load_asset(sk[0])
        an = assets_in(dest, "AnimSequence")
        anim = EAL.load_asset(an[0]) if an else None
        # материалы ПО ПОРЯДКУ слотов SkeletalMesh == порядок примитивов == манифест textures (имя MIC
        # ненадёжно из-за Interchange-суффиксов). Текстуры — из манифеста (реальные имена .TEX).
        texlist = c.get("textures") or []
        skmats = skm.get_editor_property("materials")
        # СВЕЖИЕ MIC (как статика, которая работает): parent M_AsdaObject + Tex + save, назначаем в слоты
        # SkeletalMesh. НЕ трогаем хрупкие Interchange-MIC.
        newmats = []
        for i, smat in enumerate(skmats):
            # ИМЯ СЛОТА = имя glb-материала = имя текстуры (надёжно, 1 слот на уник-текстуру после дедупа
            # Interchange). texlist[i] был ПЕР-ПРИМИТИВНЫМ (21 запись) → при дедупе в N слотов индекс сбивался
            # и текстуры с дальних индексов (gi_004@13, gi_006@20) терялись, слот получал чужую. texlist — фолбэк.
            sn = str(smat.material_slot_name) if (smat and smat.material_slot_name) else ""
            tb = sn or (texlist[i] if i < len(texlist) else (texlist[-1] if texlist else "none"))
            mic = get_anim_material(tb, master)
            slot = smat.material_slot_name if smat else unreal.Name("Mat%d" % i)
            newmats.append(unreal.SkeletalMaterial(material_interface=mic, material_slot_name=slot))
        skm.set_editor_property("materials", newmats)
        EAL.save_loaded_asset(skm)
        clips_ok += 1
        if no_place:
            continue
        for m in c["placements"]:
            xf = place_xform(m)
            if xf is None:                       # мусорная objPlace — пропуск (как matrix_ok у статики)
                skipped_bad += 1; continue
            act = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0))
            comp = act.skeletal_mesh_component
            comp.set_skeletal_mesh_asset(skm) if hasattr(comp, "set_skeletal_mesh_asset") else comp.set_skeletal_mesh(skm)
            if anim:
                try: set_anim(comp, anim)
                except Exception as e: unreal.log_warning("anim set clip %d: %s" % (c["clip"], e))
            act.set_actor_transform(xf, False, False)
            placed += 1
    if not no_place:
        les.save_current_level()
    unreal.log("АНИМАЦИИ ГОТОВО: клипов=%d, акторов размещено=%d, пропущено мусорных objPlace=%d%s" %
               (clips_ok, placed, skipped_bad, " (NO_PLACE: только материалы)" if no_place else ""))


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global ASSET, DEST_ROOT, TEX_ROOT
    ASSET    = _MAPS.map_asset_name(MAP_NAME)  # ★ единый санитайзер имени карты (asda_maps.map_asset_name)
    DEST_ROOT= "/Game/Map/" + ASSET
    TEX_ROOT = DEST_ROOT + "/Texture"
    # ★★★ 16.09.2026 (раздел 22, Reports/dedup_audit/savedir_guard_2026-09-16.md): ASDA_MAP со списком карт
    #   не сверяется, поэтому ASDA_MAP=Login даёт DEST_ROOT=/Game/Map/Login — дерево сцены логина, а ниже идут
    #   delete_directory(anims_root) и save_directory(anims_root) по /Game/Map/<карта>/Anims.
    #   Без явного разрешения — стоп до первой операции.
    import asda_login_guard as _LG_map
    if _LG_map.is_login_owned_package(DEST_ROOT):
        _LG_map.forbid_login_write("asda_build_map_anim.py", "карта " + str(MAP_NAME) + " -> " + DEST_ROOT + " (сцена логина)")
    # ★★★ 16.09.2026 (ревизия savedir3, СРЕДН-2): плюс сверка имени карты с реестром (Tools/data/asda_map_registry.json).
    #   Этот инструмент создаёт и СНОСИТ целые деревья под /Game/Map/<ASSET>, поэтому опечатка в ASDA_MAP здесь
    #   дороже всего: delete_directory(anims_root) и save_directory(anims_root) работали бы
    #   по /Game/Map/<опечатка>/Anims. Реестр — единый источник имён [[map-registry-single-source]].
    #   ★ 16.09.2026 (ревизия savedir7, СРЕДН-1): гейту отдаётся ПОСТРОЕННЫЙ путь, а не сырое имя из ASDA_MAP.
    #   Инструмент делает папку из сырого значения (map_asset_name: всё, кроме [A-Za-z0-9_], → «_»), поэтому
    #   «/Game/Map/alpen» в переменной давало папку «/Game/Map/_Game_Map_alpen» — а гейт, судивший имя после
    #   своей нормализации, такой вход пропускал. Судим ровно ту строку, в которую пойдёт запись.
    if _LG_map.map_name_blocked(DEST_ROOT, "asda_build_map_anim.py"):
        raise SystemExit(3)
    main()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
