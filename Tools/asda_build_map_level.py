# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_map_level.py — сборка полноценного UE-уровня карты AsdaStory из <map>.mapraw.
#
# Источник: @@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps/<map>.mapraw (его пишет AsdaMapViewer --export, переиспользуя
# проверенный C++-парсер). Этот скрипт НЕ парсит игровые форматы — только читает уже извлечённые
# данные и строит UE-ассеты/акторов.
#
# Что делает (ФАЗА 1 — статические объекты): per-model StaticMesh в /Game/Map/<Name>/Models,
# материалы MIC(M_AsdaObject) в /Game/Map/<Name>/Materials (текстуры — ссылки на уже импортированные
# /Game/AsdaAssets/Textures/Maps/<map>/...), расстановка через Instanced Static Mesh (HISM) по
# мировым матрицам. Террейн/вода/небо/анимация — следующие фазы.
#
# Конверсия осей (== AsdaMapActor::ToUE, проверенный рендер карт): UE=(-z, x, y)*SCALE, bMirrorY=true.
#   Геометрия меша строится в UE-пространстве (C·local). Инстанс-трансформ: UE_R = C·Aᵀ·Cᵀ,
#   UE_T = C·T, где A=3x3 игр-матрицы (row-major), T=трансляция. Это даёт world = ToUE(Aᵀ·local + T)
#   == то, что рисует эталонный AsdaMapActor (но как персистентные ассеты+инстансы).
#
# ★★★ 2026-09-03 (аудит света Д-2/Д-4/Д-6, fix_mesh_normals_and_builder.md):
#   • геометрия ВСЕХ мешей (объекты/террейн/вода/небо) строится C++-библиотекой
#     `unreal.AsdaMeshBuilder.build_static_mesh_from_arrays` (Source/AsdaImporters/.../AsdaMeshBuilder.*):
#     авторские нормали из `.mapraw` попадают в меш 1:1 (`BuildSettings.bRecomputeNormals=false`),
#     у моделей без нормалей в источнике (FVF без D3DFVF_NORMAL → в `.mapraw` нули) UE досчитывает
#     их сам; полный билд (fast_build=False) — габариты рендер-данных проверяются на NaN;
#   • террейн несёт вершинный цвет источника (`col` .mapraw: запечка по уклону × тинт карты,
#     `Reports/vertex_color_re.md` §5) — его разложение «тинт→BaseColor / яркость→AO» делает
#     мастер M_AsdaTerrainLit (Tools/asda_fix_terrainlit_vertexcolor.py), меш хранит байты как есть;
#   • Lit-состояние карты (Lit-мастера, Movable Sun/Sky captured, PPV) ставится СРАЗУ при сборке —
#     единой функцией asda_convert_map_lit.main(), переимпорт больше не откатывает свет на unlit;
#   • режим ASDA_MESH_ONLY=1: пересобрать ТОЛЬКО ассеты мешей (Models + Terrain) на месте,
#     уровень (эффекты/NPC/звуки/акторы) не трогается — путь для массовой перегонки нормалей.
#
# Запуск headless: run_headless.bat asda_build_map_level.py   (имя карты через env ASDA_MAP, по умолч. alpen)
import os, struct, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_coords as _C
import asda_foliage as _FOL   # ★ Сет E-1: признак листвы (DF «как двусторонний»)

import sys as _sys
import asda_mapraw as _MR                                  # ★ единый ридер .mapraw (был свой class Reader)
import asda_maps as _MAPS                                  # ★ единый санитайзер имени карты
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
MAP_NAME   = os.environ.get("ASDA_MAP", "alpen")
RAW_DIR    = os.environ.get("ASDA_RAW", r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps")
EXPORT_TEX = os.path.join(RAW_DIR, MAP_NAME, "textures")   # PNG объектов/воды/неба (asda_extract_map_textures)
EXPORT_TERR= os.path.join(RAW_DIR, MAP_NAME, "terrain")    # PNG splat-плиток (asda_extract_terrain_tiles)
OBJ_MASTER = _U.M_OBJECT_LIT   # ★ Lit-мастер (курс «современно» 2026-07-23): пересборка НЕ должна откатывать MIC на unlit
TER_MASTER = "/Game/Materials/M_AsdaTerrainLit"  # ★ Lit-мастер террейна (Д-6: было M_AsdaTerrain — unlit, переимпорт откатывал свет)
SCALE      = 1.0                                            # WorldScale (игр. ед. = UE см)
SKIP_EXISTING_MESH = (os.environ.get("ASDA_FORCE", "0") != "1")  # ASDA_FORCE=1 = пересобрать SM
LIMIT      = int(os.environ.get("ASDA_LIMIT", "0"))        # >0 = собрать только первые N моделей (смоук-тест)
# ★ ASDA_MESH_ONLY=1 — пересобрать только ассеты мешей (объекты + террейн) СВЕЖИМИ, уровень не грузить и
#   не трогать: ISM-компоненты уровня ссылаются на меши по пути (/Game/Map/<Name>/Models/SM_<имя>), имена
#   детерминированы порядком моделей в .mapraw. Подразумевает ASDA_FORCE=1.
MESH_ONLY  = (os.environ.get("ASDA_MESH_ONLY", "0") == "1")
# ★ ASDA_TERRAIN_ONLY=1 (07.09.2026, швы террейна) — пересобрать ТОЛЬКО SM_terrain карты (Models/уровень не трогать);
#   подразумевает ASDA_MESH_ONLY=1. Нужен, когда меняется только сборка террейна (сшивка вершин патчей).
TERRAIN_ONLY = (os.environ.get("ASDA_TERRAIN_ONLY", "0") == "1")
if TERRAIN_ONLY:
    MESH_ONLY = True
# ★ ASDA_TERRAIN_PATCHES=1 (08.09.2026, terrain_lod_report.md) — террейн ПАТЧАМИ: свой меш
#   SM_terrain_P<i> и свой AStaticMeshActor на каждый патч .mapraw вместо одного склеенного примитива
#   на всю карту. Даёт отсечение по пирамиде видимости, запросы перекрытия и отбор патчей каскадами
#   теней (73 % draw calls кадра — проход теней). Реализация — Tools/asda_terrain_patches.py.
TERRAIN_PATCHES = (os.environ.get("ASDA_TERRAIN_PATCHES", "0") == "1")
if MESH_ONLY:
    SKIP_EXISTING_MESH = False
MB = unreal.AsdaMeshBuilder                                # ★ C++-сборка мешей с авторскими нормалями (AsdaImporters)

def _f(v):  # санитайз: NaN/inf → 0
    return v if (v == v and -1e30 < v < 1e30) else 0.0

EAL  = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
ML   = unreal.MathLibrary

# ------------------------------------------------------------------ чтение .mapraw
# class Reader жил здесь; теперь это asda_mapraw.R (алиасы farr/u16arr/u32arr/u8arr/s сохранены 1:1).
Reader = _MR.Reader

def read_mapraw(path):
    r, ver, name = _MR.open_mapraw(path)                   # проверка магии 'AMRW' (был свой assert)
    # ver≥3: ключ sunDir (готовый вектор К солнцу из .SCE @0xB4); scene['lightDir'] — неверная формула ver≤2, НЕ
    # использовать. Свет ставит asda_convert_map_lit.main() (конец main), здесь поля сцены нужны только для чтения.
    scene = _MR.scene_fields(r, ver=ver)                   # as_list=False → векторы tuple, как было (farr)
    # terrain
    pc = r.u32(); idxCount = r.u32(); indices = list(r.u16arr(idxCount))
    patches = []
    for _ in range(pc):
        vtx = r.u32()
        pos = uv0 = col = ()
        if vtx > 0:
            pos = r.farr(vtx*3); uv0 = r.farr(vtx*2); r.farr(vtx*2); col = r.u8arr(vtx*4)
        colorOff = r.u32(); alphaOff = r.u32(); minX = r.f32(); minZ = r.f32()
        d0 = r.s(); s0 = r.f32(); d1 = r.s(); s1 = r.f32()
        patches.append({"vtx": vtx, "pos": pos, "uv0": uv0, "col": col,
                        "colorOff": colorOff, "alphaOff": alphaOff,
                        "detail0": d0, "scale0": s0, "detail1": d1, "scale1": s1})
    # models
    mc = r.u32(); models = []
    for _ in range(mc):
        nm = r.s(); tex = r.s(); vtx = r.u32()
        pos = r.farr(vtx*3); nrm = r.farr(vtx*3); uv = r.farr(vtx*2)
        ic = r.u32(); idx = r.u32arr(ic)
        models.append({"name": nm, "tex": tex, "vtx": vtx, "pos": pos, "nrm": nrm, "uv": uv, "idx": idx})
    # instances
    ic = r.u32(); inst = []
    for _ in range(ic):
        mi = r.u32(); m = r.farr(16); sec = r.i32(); pat = r.i32(); uvs = r.farr(4)
        inst.append({"model": mi, "m": m, "section": sec, "patch": pat, "uvScroll": uvs})
    # water
    bc = r.u32(); watTex = r.s(); watLevel = r.f32(); water = []
    for _ in range(bc):
        wv = r.u32(); wpos = r.farr(wv*3); wuv = r.farr(wv*2); wic = r.u32(); widx = r.u16arr(wic)
        water.append({"vtx": wv, "pos": wpos, "uv": wuv, "idx": widx})
    return {"ver": ver, "name": name, "scene": scene, "models": models, "instances": inst,
            "patches": patches, "indices": indices,
            "water": water, "waterTex": watTex, "waterLevel": watLevel}

# ------------------------------------------------------------------ конверсия осей
def to_ue(x, y, z): return _C.to_ue(x, y, z, SCALE)   # ★ единая конверсия осей (asda_coords)

# 3x3 mat helpers (row-major lists of 9)
mat_mul = _C.matmul3   # 3×3 row-major (общая реализация asda_coords; дедуп №2 Т19)

C  = [0,0,-1, 1,0,0, 0,1,0]      # C·v = (-vz, vx, vy)
CT = [0,1,0, 0,0,1, -1,0,0]      # transpose of C

def _finite(v): return v == v and -1e30 < v < 1e30
def matrix_ok(m):
    # отсев МУСОРНЫХ (runtime-filled) матриц Pos.POS: R должна быть ~ортонормальной, T в пределах карты.
    # Иначе un-bake/инстанс дают гигантские bounds (1e182, inf) → EnsureFailed + объект выкидывается из рендера.
    if not all(_finite(x) for x in m):
        return False
    for col in ((m[0], m[4], m[8]), (m[1], m[5], m[9]), (m[2], m[6], m[10])):
        L = (col[0]*col[0] + col[1]*col[1] + col[2]*col[2]) ** 0.5
        if not (0.5 <= L <= 2.0):
            return False
    tx, ty, tz = m[12], m[13], m[14]
    return (-2000 < tx < 70000) and (-2000 < tz < 70000) and (-20000 < ty < 20000)

def inst_transform(m):
    # m = 16 floats row-major game world matrix. A = top-left 3x3, AT = transpose.
    AT = [m[0], m[4], m[8],
          m[1], m[5], m[9],
          m[2], m[6], m[10]]
    R = mat_mul(mat_mul(C, AT), CT)   # UE_R = C·Aᵀ·Cᵀ
    fwd = unreal.Vector(R[0], R[3], R[6])   # col0
    rgt = unreal.Vector(R[1], R[4], R[7])   # col1
    up  = unreal.Vector(R[2], R[5], R[8])   # col2
    rot = ML.make_rotation_from_axes(fwd, rgt, up)
    loc = to_ue(m[12], m[13], m[14])
    return unreal.Transform(loc, rot, unreal.Vector(1, 1, 1))

# ------------------------------------------------------------------ текстуры/материалы
import re
def _san(texbase):
    # имя текстуры → как UE санитайзит имя ассета при импорте: пробел/невалидное → '_', НО ДЕФИС
    # и подчёркивание UE СОХРАНЯЕТ (напр. "MDEL_SOULIDOL-A_00" остаётся с дефисом; "tree box"→"tree_box").
    # Если конвертить дефис в '_', find_texture не найдёт dash-текстуру → материал без текстуры (жёлто-зелёный дефолт).
    return re.sub(r'[^A-Za-z0-9_-]', '_', os.path.splitext(texbase)[0])

def import_map_textures():
    """НОРМАЛЬНЫЙ импорт текстур карты: УДАЛИТЬ папку /Game/Map/<Name>/Texture, затем импортировать ВСЕ
    PNG карты (объекты/вода/небо + splat-плитки террейна) с ЗАМЕНОЙ. Self-contained, без ссылок на AsdaAssets."""
    files = []
    for d in (EXPORT_TEX, EXPORT_TERR):
        if os.path.isdir(d):
            files += [os.path.join(d, f) for f in os.listdir(d) if f.lower().endswith(".png")]
    if not files:
        unreal.log_warning("Нет извлечённых текстур (%s). Запусти asda_extract_map_textures.py + asda_extract_terrain_tiles.py" % EXPORT_TEX)
        return
    if EAL.does_directory_exist(TEX_ROOT):
        EAL.delete_directory(TEX_ROOT)                       # УДАЛЕНИЕ старых
    import asda_model_import_ue as _MIU   # ★ дедуп №2 Т17: общий импорт пачкой (AutomatedAssetImportData)
    imported = _MIU.import_batch(files, TEX_ROOT, True, skip_read_only=True)   # ЗАМЕНА существующих
    for a in imported:
        try: EAL.save_loaded_asset(a)
        except Exception: pass
    unreal.log("Текстуры карты импортированы: %d -> %s" % (len(imported), TEX_ROOT))

_tex_cache = {}
_tex_index = None   # {имя ассета в нижнем регистре: путь пакета} по TEX_ROOT — строится один раз
def find_texture(texbase):
    """Текстура карты по базовому имени из .mapraw. ★ РЕГИСТРОНЕЗАВИСИМО через реестр ассетов: имена в
    .mapraw строчные ('mdel_alpenportal-c_00.TEX'), импортированные ассеты — заглавные
    (MDEL_ALPENPORTAL-C_00); прежний load_asset по пути в другом регистре отдавал объект, который
    set_material_instance_texture_parameter_value не принимал («Failed to convert parameter 'value'»)
    → 9 моделей alpen без MIC (часть Д-5). Возвращает только unreal.Texture, иначе None.
    ★ 07.09.2026 (долг ревизии швов review_terrain_seams.md ВАЖНО-3): под TEX_ROOT после переноса текстур
      карт 17.08.2026 лежат 1 006 ObjectRedirector на /Game/AsdaAssets/Textures/Maps/<карта>/Texture.
      Редиректор — НЕ Texture, поэтому прежний код отдавал None, и фолбэк `Detail1 = d1 if d1 else d0`
      в make_terrain_mic терял второй слой детали на 44 патчах (sunnycoast 17 × TILE3, inferion 27 × TILE9).
      Теперь: (1) редиректор РАЗЫМЕНОВЫВАЕТСЯ (destination_object); (2) если под TEX_ROOT имени нет вовсе
      (редиректор мог быть удалён `fixup redirectors`), имя ищется во ВТОРОМ корне — AsdaAssets."""
    global _tex_index
    if texbase in _tex_cache:
        return _tex_cache[texbase]
    if _tex_index is None:
        _tex_index = {}
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
        roots = (TEX_ROOT, "/Game/AsdaAssets/Textures/Maps/" + ASSET + "/Texture")
        # 28.10.2026: синхронный скан — в headless-сессии реестр заполняется асинхронно, индекс не должен быть неполным.
        ar.scan_paths_synchronous(list(roots), True)
        # порядок важен: собственная папка карты имеет приоритет, AsdaAssets — запасной корень
        for root in roots:
            for ad in ar.get_assets_by_path(root, recursive=True):
                _tex_index.setdefault(str(ad.asset_name).lower(), str(ad.package_name))
    base = _san(texbase)
    p = _tex_index.get(base.lower())
    t = EAL.load_asset(p) if p else None
    # ★ ИСПРАВЛЕНО 07.09.2026: питон-обёртки `unreal.ObjectRedirector` в UE 5.8 НЕТ (AttributeError валил бы
    #   весь билдер уровня) — класс определяем по ИМЕНИ. Замечание: EAL.load_asset по пути редиректора обычно
    #   сам разыменовывает его и отдаёт цель; ветка ниже страхует случай, когда объект пришёл редиректором.
    if t is not None and t.get_class().get_name() == "ObjectRedirector":
        try:
            d = t.get_editor_property("destination_object")
        except Exception:
            d = None
        unreal.log("текстура %s (%s) — редиректор на %s" % (texbase, p, d.get_path_name() if d else None))
        t = d
        # ★★ 28.10.2026 (Reports/map_children_transforms/REPORT.md, «пустые MI»): в UE 5.8 у ObjectRedirector
        #   из Python НЕТ свойства destination_object (исключение → d = None), а индекс отдаёт приоритет папке карты,
        #   где после переноса 17.08 лежат редиректоры. find_texture возвращал None, и новый MIC оставался с текстурой
        #   мастера по умолчанию (DefaultTexture: светлая плита вместо эмблемы unicon03 на silalis, 64 меша на 3 картах).
        #   Цель редиректора — одноимённая текстура второго корня (AsdaAssets), берём её.
        if t is None:
            alt = "/Game/AsdaAssets/Textures/Maps/%s/Texture/%s" % (ASSET, p.split("/")[-1])
            t = EAL.load_asset(alt) if EAL.does_asset_exist(alt) else None
            unreal.log("текстура %s — цель редиректора взята по второму корню: %s" % (texbase, alt if t else None))
    if t is not None and not isinstance(t, unreal.Texture):
        unreal.log_warning("текстура %s (%s) — не Texture (%s), пропуск" % (texbase, p, t.get_class().get_name()))
        t = None
    _tex_cache[texbase] = t
    return t

_mat_cache = {}
def get_material(texbase):
    base = _san(texbase) or "none"
    if base in _mat_cache:
        return _mat_cache[base]
    mpath = DEST_ROOT + "/Materials/MI_" + base
    if EAL.does_asset_exist(mpath):
        mic = EAL.load_asset(mpath)
    else:
        master = EAL.load_asset(OBJ_MASTER)
        mic = ATOOLS.create_asset("MI_" + base, DEST_ROOT + "/Materials",
                                  unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        unreal.MaterialEditingLibrary.set_material_instance_parent(mic, master)
        tex = find_texture(texbase)
        if tex:
            unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(mic, "Tex", tex)
        # (оверрайд скаляра "Ambient" снят: у M_AsdaObjectLit такого параметра нет — мёртвый оверрайд, Д-12)
        EAL.save_loaded_asset(mic)
    _mat_cache[base] = mic
    return mic

# ------------------------------------------------------------------ StaticMesh per-model
# Сводная статистика нормалей по карте (в лог в конце): сколько вершин с авторскими нормалями,
# сколько досчитано UE, сколько моделей без нормалей в источнике, отброшенных вырожденных треугольников.
_NSTAT = {"models": 0, "authored": 0, "missing": 0, "models_no_nrm": 0, "degenerate": 0, "nan": 0, "fail": 0,
          "authored_obj": 0, "missing_obj": 0, "other_verts": 0}   # ★ obj — только объекты карты

DF_TERRAIN_SCALE = float(os.environ.get("ASDA_DF_TERRAIN_SCALE", "2.0"))   # ★ Сет E-1: масштаб сетки DF террейна
# ★ 08.09.2026: масштаб сетки DF ПАТЧА террейна (обоснование арифметики — в df_settings_for)
DF_TERRAIN_PATCH_SCALE = float(os.environ.get("ASDA_DF_TERRAIN_PATCH_SCALE", "0.05"))

def df_settings_for(name, cat):
    """★ Сет E-1: (DistanceFieldResolutionScale, bGenerateDistanceFieldAsIfTwoSided) по категории/имени модели."""
    if cat == "ter":
        return DF_TERRAIN_SCALE, False
    if cat == "terpatch":
        # ★ 08.09.2026: патч террейна в 8 раз меньше карты, и при масштабе 1.0 движок упирается в
        #   потолок 126³ (замер: 126×126×126, 1.3…2.3 Мб НА ПАТЧ, до 7 с сборки — на карту ≈118 Мб
        #   против 21 Мб у склеенного меша). Плотность линейна по масштабу
        #   (MeshDistanceFieldUtilities.cpp:316 NumVoxelsPerLocalSpaceUnit = VoxelDensity × Scale),
        #   поэтому 0.05 даёт 6400×0.2×0.05/7 ≈ 9 бриков = 63 вокселя ≈ 101 юн/воксель — ВДВОЕ
        #   мельче, чем 198 юн/воксель у склеенного, при меньшей суммарной памяти.
        return DF_TERRAIN_PATCH_SCALE, False
    if cat == "obj" and _FOL.is_foliage_asset(name):
        return 1.0, True
    return 1.0, False

def _build_sm(sm, name, pos, nrm, uv, col, idx, groups, slots, game_to_ue=True, recompute=False, cat="obj", weld=False):
    """Единая точка сборки геометрии: C++ UAsdaMeshBuilder (авторские нормали, ремап used-вершин, санитайз
    NaN, пропуск вырожденных, полный билд, контроль габаритов рендер-данных). Возвращает stats или None.
    ★ Ассет ОБЯЗАН быть свежим (create_asset) — пересборка поверх существующего портила bounds."""
    # ★ Сет E-1 (06.09.2026): настройки Mesh Distance Field — часть ключа DDC меша, задаются при сборке, чтобы перегонка
    #   уровней их не теряла: террейн — DistanceFieldResolutionScale 2.0 (259 вокселей на 51 200 юн ≈ 198 юн/воксель; при 1.0
    #   только 126 ≈ 406 юн/воксель — потолок движка MeshDistanceFieldUtilities.cpp:308-317, замер Э10), листва/карточки — DF «как
    #   двусторонний» (открытая геометрия), прочие объекты/вода/небо — 1.0/false. Те же правила у asda_df_build_settings.py.
    df_scale, df_two = df_settings_for(name, cat)
    # ★ weld (07.09.2026, швы террейна): сшить вершины одной позиции в одну FVertexID ДО расчёта нормалей —
    #   патчи террейна/блоки воды не делят граничные вершины, иначе нормали на границе считаются по одному патчу
    #   (ступень освещения вдоль сетки 6400 юн). Объекты с авторскими нормалями — без сшивки.
    st = MB.build_static_mesh_from_arrays(sm, pos, nrm or [], uv or [], col or [], idx, groups or [], slots,
                                          game_to_ue, recompute, False, df_scale, df_two, weld)
    if not st.ok:
        unreal.log_warning("SM %s: сборка не удалась — %s" % (name, st.error))
        _NSTAT["fail"] += 1
        return None
    _NSTAT["models"] += 1
    _NSTAT["authored"] += st.normals_authored
    _NSTAT["missing"] += st.normals_missing
    # ★ 03.09.2026: доля «авторских» считается ТОЛЬКО по объектам карты. У террейна/воды/неба нормалей
    #   в источнике нет по устройству формата (их считает движок), а вершин там в разы больше — общий
    #   процент из-за них выглядел как 14.9 % при фактических ~95 % по объектам.
    if cat == "obj":
        _NSTAT["authored_obj"] += st.normals_authored
        _NSTAT["missing_obj"] += st.normals_missing
    else:
        _NSTAT["other_verts"] += st.normals_authored + st.normals_missing
    _NSTAT["degenerate"] += st.skipped_degenerate
    _NSTAT["nan"] += st.sanitized_values
    if st.normals_authored == 0 and not recompute:
        _NSTAT["models_no_nrm"] += 1
    return st

_used_sm_names = {}
def build_mesh(model, idx):
    # имя ассета УНИКАЛЬНОЕ: в карте бывают РАЗНЫЕ модели с одинаковым именем (напр. mine: две
    # 'crystaltree02'). При ASDA_FORCE вторая сборка удалила бы ассет первой → stale-ref → set_static_mesh
    # падает "Failed to convert new_mesh". При коллизии добавляем _idx.
    base = "SM_" + model["name"]
    cnt = _used_sm_names.get(base, 0); _used_sm_names[base] = cnt + 1
    name = base if cnt == 0 else "%s_%d" % (base, idx)
    path = DEST_ROOT + "/Models/" + name
    exists = EAL.does_asset_exist(path)
    # ★★ КЭШ: существующий меш НЕ ТРОГАЕМ (ни материала, ни save). ЛЮБОЙ load+save существующего
    # StaticMesh оставляет bounds мусором (1e194/1e308 → EnsureFailed + объект выкидывается из рендера).
    # Материал ставится ТОЛЬКО при свежей сборке (ниже). Чинить материалы/геометрию = только ASDA_FORCE.
    if exists and SKIP_EXISTING_MESH:
        return EAL.load_asset(path)
    if exists:
        EAL.delete_asset(path)   # свежий ассет — иначе bounds портятся
    sm = ATOOLS.create_asset(name, DEST_ROOT + "/Models", unreal.StaticMesh, None)
    if sm is None:
        # ★ Д-2 (03.09.2026): на alpen так провалились 181 меша двух семейств деревьев —
        #   `LogAssetTools: Error: The asset 'SM_tree_gru_012' already exists in package …`
        #   → `create_asset` вернул None → C++ получил `Mesh == nullptr` → меш остался СТАРЫМ,
        #   без авторских нормалей. Причина: `does_asset_exist` дал False (ассета нет в реестре
        #   этого прогона), а пакет всё ещё лежит на диске/в памяти после чужой перегенерации,
        #   и `CanCreateAsset` отказывает. Лечение — снести безусловно, собрать мусор, повторить.
        #   Память: `map-ue-export-pipeline` («create_asset на существующем → None»).
        try:
            EAL.delete_asset(path)
        except Exception as e:
            unreal.log_warning("SM %s: повторное удаление не удалось: %s" % (name, e))
        try:
            unreal.SystemLibrary.collect_garbage()
        except Exception:
            pass
        sm = ATOOLS.create_asset(name, DEST_ROOT + "/Models", unreal.StaticMesh, None)
        if sm is None:
            unreal.log_error("SM %s: ассет не создать даже после сноса и сборки мусора — меш останется СТАРЫМ (%s)" % (name, path))
            return None
        unreal.log_warning("SM %s: создан со второй попытки (пакет висел на диске/в памяти)" % name)
    # ★ Д-2: геометрия + АВТОРСКИЕ НОРМАЛИ из .mapraw (game→UE (-z,x,y) делает C++ по закону asda_coords).
    #   Нули в nrm (FVF без D3DFVF_NORMAL) = «нет в источнике» → UE досчитает по сглаживанию только их.
    #   Внутри: ремап used-вершин, санитайз NaN, пропуск вырожденных, fast_build=False, контроль bounds.
    st = _build_sm(sm, name, model["pos"], model["nrm"], model["uv"], None, model["idx"], None, ["Mat0"])
    if st is None:
        return None
    # ★ Материал — отдельно от геометрии: сбой MIC (напр. текстура-омоним не Texture2D, дефект Д-5) НЕ должен
    #   терять сам ассет меша — уровень ссылается на SM по пути, отсутствующий меш = невидимый объект.
    try:
        mic = get_material(model["tex"])
    except Exception as e:
        unreal.log_warning("SM %s: материал '%s' не назначен (%s) — меш сохранён без материала" % (name, model["tex"], e))
        mic = None
    if mic:
        sm.set_editor_property("static_materials",
            [unreal.StaticMaterial(material_interface=mic, material_slot_name="Mat0")])
    EAL.save_loaded_asset(sm)
    return sm

# ------------------------------------------------------------------ террейн (splat) + небо
TERR_DIR = os.path.join(RAW_DIR, MAP_NAME, "terrain")   # PNG-плитки color_/alpha_ (asda_extract_terrain_tiles)

def import_terrain_tiles():
    """Импорт извлечённых splat-плиток PNG в /Game/Map/<Name>/Texture."""
    if not os.path.isdir(TERR_DIR):
        unreal.log_warning("Нет папки плиток террейна: %s (запусти asda_extract_terrain_tiles.py)" % TERR_DIR)
        return
    files = [os.path.join(TERR_DIR, f) for f in os.listdir(TERR_DIR) if f.lower().endswith(".png")]
    files = [f for f in files if not EAL.does_asset_exist(DEST_ROOT + "/Texture/" + os.path.splitext(os.path.basename(f))[0])]
    if not files:
        return
    d = unreal.AutomatedAssetImportData()
    d.set_editor_property("destination_path", DEST_ROOT + "/Texture")
    d.set_editor_property("filenames", files)
    d.set_editor_property("replace_existing", True)
    for a in ATOOLS.import_assets_automated(d):
        try: EAL.save_loaded_asset(a)
        except Exception: pass
    unreal.log("Импортировано splat-плиток: %d" % len(files))

def _terr_tex(prefix, off):
    p = "%s/Texture/%s_%d" % (DEST_ROOT, prefix, off)
    return EAL.load_asset(p) if EAL.does_asset_exist(p) else None

#   ★★★ Detail-тайлы патчей берём из СПРАВКИ по <map>/<map>.TER, а не из .mapraw. Причина: прежний
#   экспортёр (Tools/MapViewer, asda_formats.cpp) выбирал .TER как «первый .ter по итератору
#   каталога», а в трёх папках лежит лишний .TER карты-донора, алфавитно раньше своего
#   (castle_of_chess/alpen.TER, nightvalley/conquester.TER, quizstage/OX_Quiz.TER) ⇒ в .mapraw
#   попадали ЧУЖИЕ имена тайлов. Экспортёр исправлен, но уже собранные .mapraw содержат старые
#   имена, поэтому справка (Tools/asda_dump_ter_details.py) имеет приоритет. Нет справки —
#   работаем по .mapraw, как раньше.
_TER_REF = None
def _ter_patch(pi):
    global _TER_REF
    if _TER_REF is None:
        import json
        path = os.path.join(RAW_DIR, "ter_details.json")
        try:
            _TER_REF = json.load(open(path, encoding="utf-8")).get(MAP_NAME, {}).get("patches", [])
            unreal.log("справка .TER: %s → патчей %d" % (path, len(_TER_REF)))
        except Exception as e:
            _TER_REF = []
            unreal.log_warning("справка .TER не прочитана (%s) — detail-тайлы из .mapraw" % e)
    return _TER_REF[pi] if pi < len(_TER_REF) else None

def make_terrain_mic(pi, p, master):
    mp = DEST_ROOT + "/Materials/MIT_patch_%d" % pi
    mic = EAL.load_asset(mp) if EAL.does_asset_exist(mp) else \
          ATOOLS.create_asset("MIT_patch_%d" % pi, DEST_ROOT + "/Materials",
                              unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL = unreal.MaterialEditingLibrary
    MEL.set_material_instance_parent(mic, master)
    ref = _ter_patch(pi)
    if ref:
        p = dict(p)
        p["detail0"], p["scale0"] = ref["detail0"], ref["scale0"]
        p["detail1"], p["scale1"] = ref["detail1"], ref["scale1"]
    d0 = find_texture(p["detail0"]); d1 = find_texture(p["detail1"])
    cm = _terr_tex("color", p["colorOff"]); mk = _terr_tex("alpha", p["alphaOff"])
    if d0: MEL.set_material_instance_texture_parameter_value(mic, "Detail0", d0)
    MEL.set_material_instance_texture_parameter_value(mic, "Detail1", d1 if d1 else d0)
    if cm: MEL.set_material_instance_texture_parameter_value(mic, "Colormap", cm)
    if mk: MEL.set_material_instance_texture_parameter_value(mic, "Mask", mk)
    MEL.set_material_instance_scalar_parameter_value(mic, "Scale0", p["scale0"] or 1.0)
    MEL.set_material_instance_scalar_parameter_value(mic, "Scale1", p["scale1"] or 1.0)
    MEL.set_material_instance_scalar_parameter_value(mic, "HasD1", 0.0 if not p["detail1"] else 1.0)
    EAL.save_loaded_asset(mic)
    return mic

def build_terrain(data, eas):
    """Террейн: ОДИН StaticMesh SM_terrain (полигон-группа = патч = секция/слот P<i>) через C++-билдер.
    ★ Д-4: вершинный цвет `col` .mapraw (BUFFER.TVB diffuse: запечка по уклону × тинт карты, альфа 255)
      переносится в меш БАЙТ-В-БАЙТ (VertexColor в шейдере = байт/255 — C++ учитывает sRGB-кодирование
      билдера). Разложение «тинт → BaseColor, яркость → AO» живёт в мастере M_AsdaTerrainLit
      (asda_fix_terrainlit_vertexcolor.py), чтобы данные ассета оставались данными источника 1:1.
    ★ Д-6: MIC патчей — сразу на Lit-мастере (было M_AsdaTerrain: unlit, переимпорт откатывал свет).
    Нормали источник не несёт (FVF 0x242) → пересчёт UE по общим вершинам (сетка 65×65 гладкая).
    eas=None (ASDA_MESH_ONLY) — только ассет, актор на уровне не трогаем."""
    master = EAL.load_asset(TER_MASTER)
    if not master:
        unreal.log_warning("%s не найден — террейн без материала" % TER_MASTER)
    # (текстуры карты, включая splat-плитки, импортированы один раз в main через import_map_textures)
    # удалить старые per-patch SM_terrain_N (заменены одним SM_terrain)
    for i in range(len(data["patches"])):
        op = DEST_ROOT + "/Terrain/SM_terrain_%d" % i
        if EAL.does_asset_exist(op):
            try: EAL.delete_asset(op)
            except Exception: pass
    indices = data["indices"]
    name = "SM_terrain"; path = DEST_ROOT + "/Terrain/" + name
    exists = EAL.does_asset_exist(path)
    slots = []  # (slotName, mic) в порядке полигон-групп == порядок секций
    if exists and SKIP_EXISTING_MESH:
        # КЭШ: НЕ трогаем SM (re-save портит bounds). Материалы уже на нём с свежей сборки.
        sm = EAL.load_asset(path)
    else:
        if exists:
            EAL.delete_asset(path); exists = False   # свежий ассет — иначе bounds портятся (см. build_mesh)
        sm = ATOOLS.create_asset(name, DEST_ROOT + "/Terrain", unreal.StaticMesh, None)
        # Склейка патчей в один набор массивов: вершины патча получают смещение base, треугольники —
        # номер группы (= порядковый номер слота среди НЕПУСТЫХ патчей). Общий список индексов патча
        # фильтруется по vtx (как раньше: индексы ≥ vtx пропускаются).
        pos_all = []; uv_all = []; col_all = []; idx_all = []; grp_all = []
        base = 0; tri_src = [(indices[t], indices[t+1], indices[t+2]) for t in range(0, len(indices) - 2, 3)]
        for pi, p in enumerate(data["patches"]):
            vtx = p["vtx"]
            if vtx <= 0:
                continue
            gi = len(slots)
            slot = "P%d" % pi
            pos_all.extend(p["pos"]); uv_all.extend(p["uv0"])
            col = p["col"]
            if len(col) >= vtx * 4:
                col_all.extend(col[:vtx * 4])            # байты RGBA как в источнике (bytes → int 0..255)
            else:
                col_all.extend([255] * (vtx * 4))        # неполный массив цветов → белый (нейтрально)
            for a, b, c in tri_src:
                if a >= vtx or b >= vtx or c >= vtx or a == b or b == c or a == c:
                    continue
                idx_all.extend((a + base, b + base, c + base)); grp_all.append(gi)
            slots.append((slot, make_terrain_mic(pi, p, master)))
            base += vtx
        # ★ weld=True (07.09.2026): граничные вершины соседних патчей сшиваются в одну — нормали UE усредняются
        #   через границу (без этого — швы освещения по сетке патчей, terrain_seams_report.md).
        st = _build_sm(sm, name, pos_all, None, uv_all, col_all, idx_all, grp_all, [sl for sl, _ in slots], cat="ter",
                       game_to_ue=True, recompute=True, weld=True)
        if st is None:
            unreal.log_error("Террейн: сборка SM_terrain не удалась")
            return
        unreal.log("Террейн: вершин %d (сшито %d), треугольников %d, секций %d, вершинный цвет перенесён (байты 1:1)"
                   % (st.used_vertices, st.welded_vertices, st.triangles, st.groups))
        sm.set_editor_property("static_materials",
            [unreal.StaticMaterial(material_interface=mic, material_slot_name=slot) for slot, mic in slots])
        EAL.save_loaded_asset(sm)
    # ★ 08.09.2026 (terrain_lod_report.md): режим ПАТЧЕЙ — свой меш и свой актор на каждый патч
    #   .mapraw. Склеенный SM_terrain при этом всё равно собирается выше: он источник СШИТЫХ нормалей
    #   (иначе на границах патчей вернулись бы швы освещения, terrain_seams_report.md).
    if TERRAIN_PATCHES:
        import sys as _s
        import asda_terrain_patches as _TP
        _TP.build_patches(data, eas, _s.modules[__name__])
        return
    if eas is None:
        return
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
    a.set_mobility(unreal.ComponentMobility.STATIC)
    a.static_mesh_component.set_static_mesh(sm)
    unreal.log("Террейн: 1 меш, секций=%d" % len(slots))

def build_water(data, eas):
    """Вода (.WAT): объединённый StaticMesh всех тел (мир-координаты) + M_AsdaWater. Геометрия — C++-билдер
    (санитайз/ремап/полный билд); нормалей источник не несёт → пересчёт UE. eas=None — только ассет."""
    bodies = data.get("water") or []
    if not bodies:
        return
    master = EAL.load_asset("/Game/Materials/M_AsdaWater")
    name = "SM_water"; path = DEST_ROOT + "/Models/" + name
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)   # свежий ассет (иначе bounds портятся)
    sm = ATOOLS.create_asset(name, DEST_ROOT + "/Models", unreal.StaticMesh, None)
    pos_all = []; uv_all = []; idx_all = []; base = 0
    for b in bodies:
        vtx = b["vtx"]; pos = b["pos"]; uv = b["uv"]; ix = b["idx"]
        if vtx <= 0 or len(pos) < vtx * 3:
            continue
        pos_all.extend(pos[:vtx * 3])
        # UV может быть короче числа вершин (как и раньше: недостающие → 0,0)
        uv_all.extend(uv[:vtx * 2]); uv_all.extend([0.0] * (vtx * 2 - min(len(uv), vtx * 2)))
        for t in range(0, len(ix) - 2, 3):
            a, b2, c = ix[t], ix[t+1], ix[t+2]
            if a >= vtx or b2 >= vtx or c >= vtx or a == b2 or b2 == c or a == c:
                continue
            idx_all.extend((a + base, b2 + base, c + base))
        base += vtx
    if not idx_all:
        return
    st = _build_sm(sm, name, pos_all, None, uv_all, None, idx_all, None, ["Mat0"], game_to_ue=True, recompute=True, cat="water",
                   weld=True)   # ★ 07.09.2026: блоки воды тоже не делят граничные вершины — сшивка как у террейна
    if st is None:
        return
    mp = DEST_ROOT + "/Materials/MI_water"
    mic = EAL.load_asset(mp) if EAL.does_asset_exist(mp) else \
          ATOOLS.create_asset("MI_water", DEST_ROOT + "/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL = unreal.MaterialEditingLibrary
    MEL.set_material_instance_parent(mic, master)
    wt = find_texture(data.get("waterTex") or "")
    if wt:
        MEL.set_material_instance_texture_parameter_value(mic, "BaseTex", wt)
    MEL.set_material_instance_scalar_parameter_value(mic, "HasBase", 1.0 if wt else 0.0)
    EAL.save_loaded_asset(mic)
    sm.set_editor_property("static_materials", [unreal.StaticMaterial(material_interface=mic, material_slot_name="Mat0")])
    EAL.save_loaded_asset(sm)
    if eas is None:
        return
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
    a.set_mobility(unreal.ComponentMobility.STATIC)
    a.static_mesh_component.set_static_mesh(sm)
    unreal.log("Вода: тел=%d, tex=%s" % (len(bodies), data.get("waterTex")))

def build_sky(data, eas):
    """Игровой купол неба: большая сфера + M_AsdaSky (как AsdaMapActor::BuildSky). eas=None — только ассет."""
    master = EAL.load_asset("/Game/Materials/M_AsdaSky")
    cloud = find_texture("SKY_DOME_CLOUDE1"); mtn = find_texture("SKY_DOME")
    if not (cloud or mtn):
        unreal.log("Небо: текстур SKY_DOME нет — пропуск"); return
    name = "SM_sky"; path = DEST_ROOT + "/Models/" + name
    # небо ВСЕГДА пересобираем (1 дешёвый меш; старый мог быть с NaN bounds → краш рендера)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)   # свежий ассет — иначе bounds портятся (см. build_mesh)
    sm = ATOOLS.create_asset(name, DEST_ROOT + "/Models", unreal.StaticMesh, None)
    import math
    R = 400000.0; rings = 24; sectors = 48
    pos_all = []; uv_all = []; idx_all = []
    for r in range(rings + 1):
        lat = math.pi * (r / rings) - math.pi * 0.5
        cl = math.cos(lat); sl = math.sin(lat)
        for sct in range(sectors + 1):
            lon = 2 * math.pi * (sct / sectors)
            pos_all.extend((R*cl*math.cos(lon), R*cl*math.sin(lon), R*sl))   # уже в UE-пространстве
            uv_all.extend((sct/sectors, r/rings))
    W = sectors + 1
    for r in range(rings):
        for sct in range(sectors):
            q = (r*W + sct, (r+1)*W + sct, r*W + sct + 1, (r+1)*W + sct + 1)
            for (i0, i1, i2) in ((0,1,2), (2,1,3)):
                # вырожденные тр-ки на полюсах (совпадающие позиции при разных vid) отсеет C++-билдер
                idx_all.extend((q[i0], q[i1], q[i2]))
    st = _build_sm(sm, name, pos_all, None, uv_all, None, idx_all, None, ["Mat0"], game_to_ue=False, recompute=True, cat="sky")
    if st is None:
        return
    # материал неба
    mp = DEST_ROOT + "/Materials/MI_sky"
    mic = EAL.load_asset(mp) if EAL.does_asset_exist(mp) else \
          ATOOLS.create_asset("MI_sky", DEST_ROOT + "/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL = unreal.MaterialEditingLibrary
    MEL.set_material_instance_parent(mic, master)
    if cloud: MEL.set_material_instance_texture_parameter_value(mic, "CloudTex", cloud)
    if mtn: MEL.set_material_instance_texture_parameter_value(mic, "MountainTex", mtn)
    MEL.set_material_instance_scalar_parameter_value(mic, "SkyScale", 1.0)
    MEL.set_material_instance_scalar_parameter_value(mic, "Raise", -0.05)
    MEL.set_material_instance_scalar_parameter_value(mic, "Stretch", 0.35)
    EAL.save_loaded_asset(mic)
    sm.set_editor_property("static_materials", [unreal.StaticMaterial(material_interface=mic, material_slot_name="Mat0")])
    EAL.save_loaded_asset(sm)
    if eas is None:
        return
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
    a.set_mobility(unreal.ComponentMobility.STATIC)
    smc = a.static_mesh_component
    smc.set_static_mesh(sm)
    smc.set_cast_shadow(False)
    unreal.log("Небо: купол SKY_DOME построен")

# ------------------------------------------------------------------ main
def main():
    raw = os.path.join(RAW_DIR, MAP_NAME + ".mapraw")
    if not os.path.exists(raw):
        unreal.log_error("Нет .mapraw: %s" % raw); return
    data = read_mapraw(raw)
    n_models_all = len(data["models"])
    if LIMIT > 0:
        keep = set(range(min(LIMIT, n_models_all)))
        data["instances"] = [i for i in data["instances"] if i["model"] in keep]
        unreal.log("СМОУК-ТЕСТ: лимит моделей=%d" % LIMIT)
    unreal.log("Карта %s: моделей=%d (всего %d), инстансов=%d" %
               (MAP_NAME, min(LIMIT, n_models_all) if LIMIT else n_models_all, n_models_all, len(data["instances"])))

    if MESH_ONLY:
        rebuild_mesh_assets_only(data)
        return

    # При пересборке (ASDA_FORCE) — ОДИН bulk-delete папок мешей (поштучный delete_asset ×2207 = таймаут).
    # Свежие ассеты обязательны: пересборка поверх существующих портит bounds (1e194/1e308 → краш/невидимость).
    if not SKIP_EXISTING_MESH and not LIMIT:
        for sub in ("/Models", "/Terrain", "/Materials"):
            if EAL.does_directory_exist(DEST_ROOT + sub):
                EAL.delete_directory(DEST_ROOT + sub)
        unreal.log("FORCE: папки мешей/материалов очищены (свежая пересборка)")
        # НОРМАЛЬНЫЙ импорт текстур карты (удаление папки + импорт с заменой) — только при force.
        import_map_textures()

    # 1) StaticMesh на каждую уникальную модель
    sm_assets = [None] * len(data["models"])
    for i, m in enumerate(data["models"]):
        if LIMIT > 0 and i >= LIMIT:
            break
        try:
            sm_assets[i] = build_mesh(m, i)
        except Exception as e:
            unreal.log_warning("SM[%d] %s FAIL: %s" % (i, m["name"], e))
        if (i + 1) % 200 == 0:
            unreal.log("  моделей собрано: %d/%d" % (i + 1, len(data["models"])))

    # 2) уровень + один актор-холдер с HISM на каждую модель
    # ВАЖНО: new_level на СУЩЕСТВУЮЩЕМ уровне возвращает False и НЕ переключает мир (а delete_asset
    # для UWorld не срабатывает) → акторы уходили в стартовый LOAD. Поэтому: если уровень есть —
    # load_level + очистить акторов; иначе new_level. Затем СВЕРЯЕМ текущий мир (защита от засора чужого).
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    lvl_path = DEST_ROOT + "/" + ASSET
    if EAL.does_asset_exist(lvl_path):
        les.load_level(lvl_path)
        for a in list(eas.get_all_level_actors()):
            try: eas.destroy_actor(a)
            except Exception: pass
    else:
        if not les.new_level(lvl_path):
            unreal.log_error("new_level FAILED: %s — прерывание" % lvl_path); return
    w = unreal.EditorLevelLibrary.get_editor_world()
    wp = w.get_path_name() if w else ""
    if lvl_path not in wp:
        unreal.log_error("ТЕКУЩИЙ МИР '%s' != цель '%s' — прерывание (иначе засорим чужой уровень)" % (wp, lvl_path))
        return
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    holder = eas.spawn_actor_from_class(unreal.Actor, unreal.Vector(0, 0, 0))
    holder.set_actor_label("AsdaMapObjects_" + MAP_NAME)
    root = sds.k2_gather_subobject_data_for_instance(holder)[0]

    # сгруппировать инстансы по модели
    by_model = {}
    for inst in data["instances"]:
        by_model.setdefault(inst["model"], []).append(inst)

    total_inst = 0; total_hism = 0
    for mi, insts in by_model.items():
        sm = sm_assets[mi] if mi < len(sm_assets) else None
        if not sm or not isinstance(sm, unreal.StaticMesh):
            continue
        params = unreal.AddNewSubobjectParams()
        params.set_editor_property("parent_handle", root)
        # ★ ISM (не HISM): HISM строит внутренний cluster-tree для иерархического отсечения, и при
        # add_instance из Python это дерево НЕ строится корректно → инстансы «всплывают» только вблизи /
        # невидимы. Обычный ISM рисует инстансы простым пер-инстанс отсечением (наши счётчики умеренные).
        params.set_editor_property("new_class", unreal.InstancedStaticMeshComponent)
        sub, fail = sds.add_new_subobject(params)
        if fail and str(fail):
            unreal.log_warning("ISM add FAIL model=%d: %s" % (mi, fail)); continue
        sds.rename_subobject(sub, unreal.Text("I_" + data["models"][mi]["name"]))
        comp = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(sds.k2_find_subobject_data_from_handle(sub))
        ism = unreal.InstancedStaticMeshComponent.cast(comp)
        try:
            ism.set_static_mesh(sm)
            for inst in insts:
                ism.add_instance(inst_transform(inst["m"]))
            total_inst += len(insts); total_hism += 1
        except Exception as e:
            unreal.log_warning("расстановка model=%d (%s) FAIL: %s" % (mi, data["models"][mi]["name"], e))

    # 3) террейн (земля со splat-материалом) + игровой купол неба
    try:
        build_terrain(data, eas)
    except Exception as e:
        unreal.log_warning("build_terrain FAIL: %s" % e)
    try:
        build_water(data, eas)
    except Exception as e:
        unreal.log_warning("build_water FAIL: %s" % e)
    try:
        build_sky(data, eas)
    except Exception as e:
        unreal.log_warning("build_sky FAIL: %s" % e)

    les.save_current_level()
    _log_normal_stats()

    # 4) ★ Д-6: Lit-состояние карты СРАЗУ — той же функцией, что и ручная конвертация
    #    (asda_convert_map_lit.main: репарент MIC на Lit-мастера, Movable Sun (цвет/направление из .mapraw),
    #    Movable SkyLight captured-scene, PostProcessVolume с фикс-экспозицией; сама грузит и сохраняет
    #    уровень). Прежний add_lighting (Stationary Sun 5.0 лк, SkyLight RTC, без PPV) откатывал свет
    #    при каждом переимпорте. Направление солнца берётся из .mapraw ЧЕРЕЗ asda_convert_map_lit —
    #    когда экспортёр начнёт писать готовый вектор .SCE @0xB4, править нужно только там.
    import asda_convert_map_lit as _LIT
    # ★★★ 16.09.2026 (ревизия savedir12, СРЕДН-4). Статус конвертера ЧИТАЕМ. Раньше он выбрасывался, а строка
    #   «ГОТОВО» печаталась ещё ДО вызова — карта с неверным светом (нет LIT-мастеров, солнце не совпало с
    #   эталоном, провал самопроверки) отчитывалась готовой, и батч писал «КАРТА ГОТОВА». Теперь «ГОТОВО»
    #   печатается только после успеха конвертера, а неуспех останавливает сборку кодом 3 — его ловят драйверы.
    _lit_st = _LIT.main(MAP_NAME)
    if _lit_st != "ok":
        unreal.log_error("СВЕТ НЕ РАСКАТАН %s: asda_convert_map_lit вернул %r — карта НЕ готова (см. строки LIT выше)"
                         % (MAP_NAME, _lit_st))
        raise SystemExit(3)
    unreal.log("ГОТОВО %s: уровень %s | HISM=%d, инстансов=%d" %
               (MAP_NAME, DEST_ROOT + "/" + ASSET, total_hism, total_inst))

# ── СВЕТ: прежние SUN_LUX/SKY_LUX/add_lighting (Stationary 5.0 лк + SkyLight RTC) удалены (Д-6) —
#   единственный источник Lit-состояния карты = asda_convert_map_lit.main() (см. конец main()).

def _log_normal_stats():
    """Сводка по нормалям и санитайзу за прогон (числа для отчёта/проверки, см. Д-2)."""
    n = _NSTAT
    tot = n["authored"] + n["missing"]
    ob = n["authored_obj"] + n["missing_obj"]
    unreal.log("НОРМАЛИ (объекты): мешей %d, из них без нормалей в источнике %d, сборка не удалась %d | "
               "вершин с авторскими %d из %d (%.1f%%), досчитано движком %d | террейн/вода/небо: %d вершин "
               "(нормалей в источнике нет по устройству формата) | вырожденных тр-ков %d | NaN→0 %d"
               % (n["models"], n["models_no_nrm"], n["fail"], n["authored_obj"], ob,
                  (100.0 * n["authored_obj"] / ob) if ob else 0.0, n["missing_obj"], n["other_verts"],
                  n["degenerate"], n["nan"]))


def _asset_names(folder):
    """Имена ассетов (без пути/суффикса) в папке /Game/... (не рекурсивно)."""
    if not EAL.does_directory_exist(folder):
        return set()
    return {a.rsplit("/", 1)[-1].split(".")[0] for a in EAL.list_assets(folder, recursive=False)}


def _restore_skydome():
    """Воссоздать SM_skydome (ассет asda_build_sky_dome.py, снесённый bulk-delete'ом /Models): тот же
    build_dome_mesh из SKY_DOME.JBM карты + существующие MI_skydome/MI_skyhorizon. Уровень не трогается —
    актор SkyDome ссылается на меш по пути."""
    import asda_build_sky_dome as SKY
    SKY.MAP_NAME = MAP_NAME
    SKY.DEST_ROOT = DEST_ROOT
    jbm = os.path.join(SKY.CLIENT_MAP, MAP_NAME, "SKY_DOME.JBM")
    if not os.path.isfile(jbm):
        unreal.log_warning("SM_skydome: нет %s — не восстановлен" % jbm); return
    idx, verts, vtx, tri = SKY.parse_jbm(jbm)
    sm = SKY.build_dome_mesh(idx, verts)
    mic = EAL.load_asset(DEST_ROOT + "/Materials/MI_skydome") if EAL.does_asset_exist(DEST_ROOT + "/Materials/MI_skydome") else None
    micH = EAL.load_asset(DEST_ROOT + "/Materials/MI_skyhorizon") if EAL.does_asset_exist(DEST_ROOT + "/Materials/MI_skyhorizon") else None
    sm.set_editor_property("static_materials", [
        unreal.StaticMaterial(material_interface=mic, material_slot_name="Clouds"),
        unreal.StaticMaterial(material_interface=micH, material_slot_name="Horizon")])
    EAL.save_loaded_asset(sm)
    unreal.log("SM_skydome восстановлен (vtx=%d tri=%d, MIC %s/%s)" % (vtx, tri, bool(mic), bool(micH)))


def rebuild_mesh_assets_only(data):
    """★ ASDA_MESH_ONLY=1: пересобрать СВЕЖИМИ только ассеты мешей карты (Models: объекты+вода+небо,
    Terrain: SM_terrain) — уровень не грузится и не меняется; его ISM/StaticMeshActor ссылаются на меши
    по пути, имена детерминированы порядком моделей .mapraw. Материалы (MIC/MIT) переиспользуются,
    MIT патчей репарентятся на Lit-мастер в make_terrain_mic. Контроль: набор имён SM_* до/после."""
    if TERRAIN_ONLY:
        # ★ ASDA_TERRAIN_ONLY=1 (07.09.2026): только SM_terrain. Существующий ассет удаляет сам build_terrain
        #   (SKIP_EXISTING_MESH=False → delete_asset + свежий create_asset), Models и уровень не трогаются;
        #   MIT патчей переиспользуются (make_terrain_mic грузит существующие).
        build_terrain(data, None)
        _log_normal_stats()
        unreal.log("ГОТОВО %s (TERRAIN_ONLY): SM_terrain пересобран, Models/уровень не тронуты" % MAP_NAME)
        return
    before = _asset_names(DEST_ROOT + "/Models")
    # ★ Удаляем ТОЛЬКО свои ассеты (SM_<модель>, SM_water, SM_sky, SM_terrain): в /Models живут и чужие —
    #   SM_skydome (asda_build_sky_dome.py). Bulk-delete папки (как при ASDA_FORCE) снёс бы их (так и
    #   случилось при первом прогоне 03.09 — восстановлено). Поштучно: ~2200 delete_asset.
    _used_sm_names.clear()
    mine = set()
    for i, m in enumerate(data["models"]):
        base = "SM_" + m["name"]; cnt = _used_sm_names.get(base, 0); _used_sm_names[base] = cnt + 1
        mine.add(base if cnt == 0 else "%s_%d" % (base, i))
    _used_sm_names.clear()
    mine |= {"SM_water", "SM_sky"}
    foreign = sorted(before - mine)
    # ★ Удаление — ТОЛЬКО bulk delete_directory (память map-ue-export-pipeline: поштучный delete_asset = отдельный
    #   ForceDelete со сканом ссылок, ~0.85 с/ассет → alpen 31 мин; пакетный delete_loaded_assets тоже грузит все
    #   2207). Чужой ассет в /Models ровно один — SM_skydome (asda_build_sky_dome.py), он детерминирован из
    #   SKY_DOME.JBM и восстанавливается той же build_dome_mesh (MIC остаются). Любой ДРУГОЙ чужой ассет = стоп.
    unknown = [f for f in foreign if f != "SM_skydome"]
    if unknown:
        unreal.log_error("MESH_ONLY: в /Models чужие ассеты %s — bulk-удаление снесло бы их; прерывание" % unknown)
        return
    for sub in ("/Models", "/Terrain"):
        if EAL.does_directory_exist(DEST_ROOT + sub):
            EAL.delete_directory(DEST_ROOT + sub)         # ОДИН bulk-delete (секунды)
    unreal.log("MESH_ONLY: папки Models/Terrain очищены (ассетов было %d; SM_skydome %s)"
               % (len(before), "будет восстановлен" if "SM_skydome" in before else "не было"))
    if "SM_skydome" in before:
        _restore_skydome()
    before = before & mine
    built = 0
    for i, m in enumerate(data["models"]):
        try:
            if build_mesh(m, i):
                built += 1
        except Exception as e:
            unreal.log_warning("SM[%d] %s FAIL: %s" % (i, m["name"], e))
        if (i + 1) % 200 == 0:
            unreal.log("  моделей собрано: %d/%d" % (i + 1, len(data["models"])))
    # Небо-сфера SM_sky пересобирается только если она БЫЛА (asda_build_sky_dome.py заменяет её куполом
    # SM_skydome и удаляет SM_sky/MI_sky — тогда воссоздавать её нечего).
    fns = [build_terrain, build_water] + ([build_sky] if "SM_sky" in before else [])
    if "SM_sky" not in before:
        for stray in (DEST_ROOT + "/Models/SM_sky", DEST_ROOT + "/Materials/MI_sky"):
            if EAL.does_asset_exist(stray):
                EAL.delete_asset(stray)
    for fn in fns:
        try:
            fn(data, None)
        except Exception as e:
            unreal.log_warning("%s FAIL: %s" % (fn.__name__, e))
    after = _asset_names(DEST_ROOT + "/Models")
    missing = sorted(before - after)
    if missing:
        unreal.log_error("MESH_ONLY: %d ассетов из прежнего набора НЕ пересобраны (уровень будет ссылаться "
                         "на отсутствующие меши): %s" % (len(missing), ", ".join(missing[:20])))
    else:
        unreal.log("MESH_ONLY: набор имён сохранён (%d прежних, %d теперь)" % (len(before), len(after)))
    _log_normal_stats()
    unreal.log("ГОТОВО %s (MESH_ONLY): мешей %d, террейн/вода/небо пересобраны, уровень не тронут"
               % (MAP_NAME, built))


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global ASSET, DEST_ROOT, TEX_ROOT
    ASSET      = _MAPS.map_asset_name(MAP_NAME)                # имя для UE-путей (пробелы недопустимы: "Mountain Flamion"→"Mountain_Flamion")
    DEST_ROOT  = "/Game/Map/" + ASSET
    TEX_ROOT   = DEST_ROOT + "/Texture"                        # ★ ТЕКСТУРЫ КАРТЫ — в её собственной папке
    # ★★★ 16.09.2026 (раздел 22, Reports/dedup_audit/savedir_guard_2026-09-16.md): имя карты приходит из
    #   ASDA_MAP и со списком карт НЕ сверяется (map_asset_name только чистит символы), поэтому ASDA_MAP=Login
    #   даёт DEST_ROOT=/Game/Map/Login — дерево сцены логина. Тогда delete_directory(TEX_ROOT) и
    #   delete_directory(DEST_ROOT + sub) снесли бы её папки, а пересборка уровня — свет владельца.
    #   Без явного разрешения останавливаемся ДО первой операции.
    import asda_login_guard as _LG_map
    if _LG_map.is_login_owned_package(DEST_ROOT):
        _LG_map.forbid_login_write("asda_build_map_level.py", "карта " + str(MAP_NAME) + " -> " + DEST_ROOT + " (сцена логина)")
    # ★★★ 16.09.2026 (ревизия savedir3, СРЕДН-2): плюс сверка имени карты с реестром (Tools/data/asda_map_registry.json).
    #   Этот инструмент создаёт и СНОСИТ целые деревья под /Game/Map/<ASSET>, поэтому опечатка в ASDA_MAP здесь
    #   дороже всего: delete_directory(TEX_ROOT) и delete_directory(DEST_ROOT + sub) снесли бы мусорное дерево,
    #   а собранный уровень остался бы под /Game/Map/<опечатка>.
    #   Реестр — единый источник имён [[map-registry-single-source]].
    #   ★ 16.09.2026 (ревизия savedir7, СРЕДН-1): гейту отдаётся ПОСТРОЕННЫЙ путь, а не сырое имя из ASDA_MAP.
    #   Инструмент делает папку из сырого значения (map_asset_name: всё, кроме [A-Za-z0-9_], → «_»), поэтому
    #   «/Game/Map/alpen» в переменной давало папку «/Game/Map/_Game_Map_alpen» — а гейт, судивший имя после
    #   своей нормализации, такой вход пропускал. Судим ровно ту строку, в которую пойдёт запись.
    if _LG_map.map_name_blocked(DEST_ROOT, "asda_build_map_level.py"):
        raise SystemExit(3)
    main()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
