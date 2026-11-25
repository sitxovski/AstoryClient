# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_reimport_normals.py — РЕИМПОРТ ТОЛЬКО МЕШЕЙ из перегнанных .glb (нормали, 2026-09-05) + дамп состояния мешей.
#
# ★ ЗАЧЕМ. `Tools/asda_fix_glb_normals.py` переписал аксессор NORMAL в 3643 .glb (RawAssets/Models, отчёт
#   `Reports/render_sets_2026-09-05/normals_export_report.md`). В UE меши надо переимпортировать, НЕ ТРОГАЯ материалы,
#   текстуры, скелеты, физассеты и клипы. Штатные импортёры (`asda_import_models.py`, `asda_import_skinned_models.py`,
#   `asda_import_npc.py`, `asda_import_mob_skel.py`, …) для этого не годятся: `AutomatedAssetImportData` заново создаёт MI
#   из glb (слетают репарент лица → M_AsdaSkinLit, свитч Foliage, оверрайды HasMaps/ClassStrength/TexN), у части
#   импортёров `INCREMENTAL=True` пропускает существующее, у части — БЕЗУСЛОВНЫЙ снос папки назначения.
#
# ★ КАК. Настоящий реимпорт Interchange: `UInterchangeManager::ReimportAsset(mesh, params)` (BlueprintCallable, СИНХРОННЫЙ —
#   InterchangeManager.cpp:1603 `bRunSynchronous=true`), `params.reimport_asset = mesh`, `override_pipelines = [наш
#   InterchangeGenericAssetsPipeline]` с `import_materials=False`, `import_textures=False`, `import_animations=False`,
#   `build_nanite=False`, `create_physics_asset=False`, `recompute_normals=False`. В контексте AssetReimport сам пайплайн
#   подставляет скелет и физассет ТЕКУЩЕГО меша (InterchangeGenericMeshPipeline.cpp:207-216), фабрики в режиме
#   `ReimportObject != nullptr` СОХРАНЯЮТ материалы слотов (InterchangeMeshHelper.cpp:1445: при реимпорте слот меняется
#   только если он пуст/дефолтный) и не трогают свойства ассета (`ReimportStrategy = ApplyNoProperties`,
#   InterchangeImportCommon.cpp:346). ⚠ Путь `AssetImportTask`+`replace_existing` этого НЕ даёт: AssetTools.cpp:3845 ставит
#   `ReimportAsset = nullptr`, и у StaticMesh `bReimport=false` (InterchangeStaticMeshFactory.cpp:307) → слоты перетираются.
#   Настройки сборки живут в ассете и при ApplyNoProperties не меняются ⇒ `bRecomputeNormals` выставляется ЯВНО на ассете
#   (LODInfo[0].BuildSettings у скелетала / LOD0 BuildSettings у статика) ДО реимпорта, иначе авторские нормали снова
#   пересчитались бы у мешей, импортированных свежим пайплайном (C++-дефолт bRecomputeNormals=true).
#
# РЕЖИМЫ (env):
#   ASDA_RN_MODE=dump   — только дамп: слоты (имя→MIC), Nanite, bRecomputeNormals, скелет/PA/ShadowPA, LOD, вершин, sha1 позиций и
#                          нормалей LOD0 рендер-данных, cos(нормаль, геометрическая) и доля <0; MIC: parent (тег реестра), у MIC
#                          лица/листвы/выборки — параметры. Ничего не пишет в ассеты.
#   ASDA_RN_MODE=apply  — реимпорт выбранных мешей (гейт после КАЖДОГО: слоты/скелет/PA/ShadowPA/LOD/пакет/состав папки = «до»,
#                          иначе GATE FAIL и СТОП без отката) + сохранение ТОЛЬКО пакета меша + дамп «после».
#                          Пишет только при ASDA_REIMPORT_NORMALS=1, иначе сухой прогон (список, что было бы сделано).
#   ASDA_RN_SANDBOX=1   — (apply) меш дублируется в /Game/_NormalsTest/… и реимпортируется ТАМ; оригинал не трогается; папка удаляется.
#   ASDA_RN_GROUPS      — группы = каталог glb относительно RawAssets/Models («Char,Items,Skeletal/NPC»), ALL (умолч.).
#   ASDA_RN_FILTER      — regex по пути ассета; ASDA_RN_LIMIT — не больше N мешей; ASDA_RN_OUT — json дампа;
#   ASDA_RN_NORMALS=all|sample|none — для скольких мешей считать cos/хэши (sample = ASDA_RN_SAMPLE на группу, seed 20260905).
#   ASDA_RN_ROOT        — корень поиска мешей (умолч. /Game).
# Запуск (скрыто, успех по логу): python Tools/run_headless_safe.py Tools/asda_reimport_normals.py --marker RN-FINISHED
import os, sys, re, json, math, time, hashlib, struct, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal
import asda_login_guard as _LG   # ★ 15.09.2026: меши сцены логина не реимпортировать без ASDA_ALLOW_LOGIN_WRITE=1

MODE = os.environ.get("ASDA_RN_MODE", "dump").lower()
WRITE = os.environ.get("ASDA_REIMPORT_NORMALS", "") == "1"
SANDBOX = os.environ.get("ASDA_RN_SANDBOX", "") == "1"
GROUPS = [g.strip() for g in os.environ.get("ASDA_RN_GROUPS", "ALL").split(",") if g.strip()]
FILTER = os.environ.get("ASDA_RN_FILTER", "").strip()
LIMIT = int(os.environ.get("ASDA_RN_LIMIT", "0") or 0)
OUT = os.environ.get("ASDA_RN_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05/normals_ue_dump_%s.json" % MODE)
NORMALS = os.environ.get("ASDA_RN_NORMALS", "all").lower()
SAMPLE = int(os.environ.get("ASDA_RN_SAMPLE", "12") or 12)
ROOT = os.environ.get("ASDA_RN_ROOT", "/Game")
RAW_MODELS = os.path.normpath(r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/RawAssets/Models").lower()
PREV_DIR = "_prev_normals_2026-09-05"
SANDBOX_ROOT = "/Game/_NormalsTest"
FOLIAGE_JSON = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05/setD_foliage_report.json"
GC_EVERY = 40

EAL = unreal.EditorAssetLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()
MEL = unreal.MaterialEditingLibrary
MB = unreal.AsdaMeshBuilder


def log(m): unreal.log("[reimport_normals] %s" % m)


# ───────────────────────────── источники: меш → glb ─────────────────────────────
def source_files(asset):
    """Абсолютные пути источников из AssetImportData (обе Python-обёртки — какая есть в сборке)."""
    try:
        aid = asset.get_editor_property("asset_import_data")
    except Exception:
        return []
    if not aid:
        return []
    for fn in ("extract_filenames", "k2_extract_filenames"):
        try:
            r = getattr(aid, fn)()
            return [str(x) for x in r]
        except Exception:
            pass
    try:
        return [str(aid.get_first_filename())]
    except Exception:
        return []


def glb_group(path):
    """Группа = каталог glb относительно RawAssets/Models; None — источник вне Models или бэкап _prev."""
    p = os.path.normpath(path).lower()
    if not p.startswith(RAW_MODELS + os.sep):
        return None
    rel = p[len(RAW_MODELS) + 1:].replace("\\", "/")
    if rel.startswith(PREV_DIR.lower()):
        return None
    d = rel.rsplit("/", 1)[0] if "/" in rel else ""
    return d


def registry_meshes(root):
    out = []
    for ad in AR.get_assets_by_path(unreal.Name(root), recursive=True):
        cn = str(ad.asset_class_path.asset_name)
        if cn in ("SkeletalMesh", "StaticMesh"):
            tag = None
            try:
                tag = str(ad.get_tag_value("AssetImportData"))
            except Exception:
                pass
            out.append((str(ad.package_name), cn, tag))
    out.sort()
    return out


# ───────────────────────────── геометрия LOD0 ─────────────────────────────
def lod0(mesh):
    if isinstance(mesh, unreal.SkeletalMesh):
        r = MB.get_skeletal_mesh_lod0_vertices(mesh)
    else:
        r = MB.get_static_mesh_lod0_vertices(mesh)
    if not isinstance(r, tuple):
        r = (r,)
    arrays = [x for x in r if isinstance(x, unreal.Array)]
    bools = [x for x in r if isinstance(x, bool)]
    pos = arrays[0] if len(arrays) > 0 else []
    nrm = arrays[1] if len(arrays) > 1 else []
    idx = arrays[2] if len(arrays) > 2 else []
    return pos, nrm, idx, bools


def normals_stats(pos, nrm, idx):
    """(sha1 позиций, sha1 нормалей, средний cos(нормаль, геометрическая по треугольникам), доля cos<0, n)."""
    P = [(v.x, v.y, v.z) for v in pos]
    N = [(v.x, v.y, v.z) for v in nrm]
    I = list(idx)
    hp = hashlib.sha1(); hn = hashlib.sha1()
    # хэш позиций — по УНИКАЛЬНЫМ отсортированным позициям: число рендер-вершин зависит от расщепления по нормалям/UV
    #   (песочница: бант 143→145 вершин при той же геометрии), поэтому сырой список сравнивать нельзя
    for p in sorted(set((int(round(p[0] * 1000)), int(round(p[1] * 1000)), int(round(p[2] * 1000))) for p in P)):
        hp.update(struct.pack("<3i", *p))
    for n in N:
        hn.update(struct.pack("<3i", int(round(n[0] * 1000)), int(round(n[1] * 1000)), int(round(n[2] * 1000))))
    acc = [[0.0, 0.0, 0.0] for _ in P]
    for t in range(0, len(I) - 2, 3):
        a, b, c = I[t], I[t + 1], I[t + 2]
        if a >= len(P) or b >= len(P) or c >= len(P):
            continue
        pa, pb, pc = P[a], P[b], P[c]
        u = (pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]); v = (pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2])
        # формула движка (StaticMeshOperations::ComputeTriangleTangentsAndNormals: (P1-P2)^(P0-P2) = -(u^v)),
        #   как в asda_verify_mesh_normals.geometric_normals — иначе на зеркальной конверсии осей знак обратный
        g = (-(u[1] * v[2] - u[2] * v[1]), -(u[2] * v[0] - u[0] * v[2]), -(u[0] * v[1] - u[1] * v[0]))
        for i in (a, b, c):
            acc[i][0] += g[0]; acc[i][1] += g[1]; acc[i][2] += g[2]
    s = 0.0; neg = 0; n = 0
    for k in range(len(P)):
        g = acc[k]; lg = math.sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2])
        nn = N[k]; ln = math.sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2])
        if lg < 1e-9 or ln < 1e-6:
            continue
        c = (g[0] * nn[0] + g[1] * nn[1] + g[2] * nn[2]) / (lg * ln)
        s += c; n += 1
        if c < 0: neg += 1
    return hp.hexdigest()[:16], hn.hexdigest()[:16], (s / n if n else None), (neg / n if n else None), n


# ───────────────────────────── метаданные меша ─────────────────────────────
def obj_path(o):
    return o.get_path_name().split(".")[0] if o else None


def build_recompute(mesh):
    """(bRecomputeNormals LOD0, как читать) — скелетал: LODInfo[0].BuildSettings; статик: StaticMeshEditorSubsystem."""
    try:
        if isinstance(mesh, unreal.SkeletalMesh):
            # LODInfo из Python не виден («Failed to find property lod_info») — статики SkeletalMeshEditorSubsystem
            bs = unreal.SkeletalMeshEditorSubsystem.get_lod_build_settings(mesh, 0)
            return bool(bs.get_editor_property("recompute_normals")), "skm_subsystem"
        sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        bs = sub.get_lod_build_settings(mesh, 0)
        return bool(bs.get_editor_property("recompute_normals")), "sme_subsystem"
    except Exception as e:
        return None, "err:%s" % str(e)[:60]


def set_recompute_false(mesh):
    """bRecomputeNormals=False у LOD0 (все LOD у скелетала). Возвращает описание изменения или None."""
    try:
        if isinstance(mesh, unreal.SkeletalMesh):
            n = unreal.SkeletalMeshEditorSubsystem.get_lod_count(mesh)
            changed = []
            for i in range(n):
                bs = unreal.SkeletalMeshEditorSubsystem.get_lod_build_settings(mesh, i)
                if bs.get_editor_property("recompute_normals"):
                    bs.set_editor_property("recompute_normals", False)
                    unreal.SkeletalMeshEditorSubsystem.set_lod_build_settings(mesh, i, bs)
                    changed.append(i)
            return ("skeletal recompute_normals True→False LOD%s" % changed) if changed else None
        sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        n = mesh.get_num_lods()
        changed = []
        for i in range(n):
            bs = sub.get_lod_build_settings(mesh, i)
            if bs.get_editor_property("recompute_normals"):
                bs.set_editor_property("recompute_normals", False)
                sub.set_lod_build_settings(mesh, i, bs)
                changed.append(i)
        return ("static recompute_normals True→False LOD%s" % changed) if changed else None
    except Exception as e:
        return "ERR set_recompute: %s" % str(e)[:80]


def nanite_enabled(mesh):
    try:
        ns = mesh.get_editor_property("nanite_settings")
        return bool(ns.get_editor_property("enabled")) if ns else None
    except Exception:
        return None


def set_nanite_off(mesh):
    try:
        ns = mesh.get_editor_property("nanite_settings")
        if ns and ns.get_editor_property("enabled"):
            ns.set_editor_property("enabled", False); mesh.set_editor_property("nanite_settings", ns)
            return True
    except Exception:
        pass
    return False


def meta_of(mesh):
    m = {"class": mesh.get_class().get_name(), "package": obj_path(mesh), "name": mesh.get_name()}
    if isinstance(mesh, unreal.SkeletalMesh):
        mats = mesh.get_editor_property("materials")
        m["slots"] = [[str(s.get_editor_property("material_slot_name")), obj_path(s.get_editor_property("material_interface"))] for s in mats]
        m["skeleton"] = obj_path(mesh.get_editor_property("skeleton"))
        m["physics_asset"] = obj_path(mesh.get_editor_property("physics_asset"))
        try:
            m["shadow_physics_asset"] = obj_path(mesh.get_editor_property("shadow_physics_asset"))
        except Exception:
            m["shadow_physics_asset"] = "n/a"
        try:
            m["lods"] = int(unreal.SkeletalMeshEditorSubsystem.get_lod_count(mesh))
        except Exception:
            m["lods"] = None
    else:
        mats = mesh.get_editor_property("static_materials")
        m["slots"] = [[str(s.get_editor_property("material_slot_name")), obj_path(s.get_editor_property("material_interface"))] for s in mats]
        try:
            m["lods"] = mesh.get_num_lods()
        except Exception:
            m["lods"] = None
    m["nanite"] = nanite_enabled(mesh)
    m["recompute_normals"], m["recompute_src"] = build_recompute(mesh)
    return m


GATE_KEYS = ("class", "package", "name", "slots", "skeleton", "physics_asset", "shadow_physics_asset", "lods")


def gate_diff(before, after):
    d = []
    for k in GATE_KEYS:
        if before.get(k) != after.get(k):
            d.append("%s: %r → %r" % (k, before.get(k), after.get(k)))
    if after.get("nanite"):
        d.append("nanite: включён после реимпорта")
    if after.get("recompute_normals"):
        d.append("recompute_normals: True после реимпорта")
    return d


def folder_of(pkg):
    return pkg.rsplit("/", 1)[0]


def list_folder(folder):
    try:
        return sorted(str(x).split(".")[0] for x in EAL.list_assets(folder, recursive=True, include_folder=False))
    except Exception:
        return []


# ───────────────────────────── MIC ─────────────────────────────
def mic_registry(root):
    out = {}
    for ad in AR.get_assets_by_path(unreal.Name(root), recursive=True):
        if str(ad.asset_class_path.asset_name) == "MaterialInstanceConstant":
            par = None
            try:
                par = str(ad.get_tag_value("Parent"))
            except Exception:
                pass
            out[str(ad.package_name)] = par
    return out


def mic_params(path):
    mic = unreal.load_asset(path)
    if not mic:
        return None
    r = {"parent": obj_path(mic.get_editor_property("parent"))}
    for p in ("HasMaps", "ClassStrength", "Ambient", "RoughAlphaMix"):
        try:
            r["s_" + p] = round(float(MEL.get_material_instance_scalar_parameter_value(mic, p)), 4)
        except Exception:
            pass
    for p in ("Tex", "TexN", "TexC"):
        try:
            r["t_" + p] = obj_path(MEL.get_material_instance_texture_parameter_value(mic, p))
        except Exception:
            pass
    try:
        r["sw_Foliage"] = bool(MEL.get_material_instance_static_switch_parameter_value(mic, "Foliage"))
    except Exception:
        pass
    # оверрайды (что реально переопределено в MIC) — надёжнее «значений»: значение может совпасть с дефолтом мастера
    try:
        sp = mic.get_editor_property("scalar_parameter_values")
        r["overrides_scalar"] = sorted(str(v.get_editor_property("parameter_info").get_editor_property("name")) for v in sp)
        tp = mic.get_editor_property("texture_parameter_values")
        r["overrides_texture"] = sorted(str(v.get_editor_property("parameter_info").get_editor_property("name")) for v in tp)
    except Exception:
        pass
    return r


# ───────────────────────────── реимпорт ─────────────────────────────
def make_pipeline():
    pipe = unreal.InterchangeGenericAssetsPipeline()
    mp = pipe.get_editor_property("material_pipeline")
    mp.set_editor_property("import_materials", False)
    try:
        mp.get_editor_property("texture_pipeline").set_editor_property("import_textures", False)
    except Exception as e:
        log("texture_pipeline.import_textures недоступен: %s" % e)
    ap = pipe.get_editor_property("animation_pipeline")
    ap.set_editor_property("import_animations", False)
    mesh_p = pipe.get_editor_property("mesh_pipeline")
    mesh_p.set_editor_property("build_nanite", False)
    mesh_p.set_editor_property("create_physics_asset", False)
    cm = pipe.get_editor_property("common_meshes_properties")
    cm.set_editor_property("recompute_normals", False)
    cm.set_editor_property("recompute_tangents", True)     # тангенсов в glb нет и не было — как при исходном импорте
    return pipe


def pipeline_desc(pipe):
    d = {}
    for sub in ("common_meshes_properties", "mesh_pipeline", "material_pipeline", "animation_pipeline", "common_skeletal_meshes_and_animations_properties"):
        try:
            o = pipe.get_editor_property(sub)
        except Exception:
            continue
        for k in ("recompute_normals", "recompute_tangents", "use_mikk_t_space", "compute_weighted_normals", "remove_degenerates",
                  "build_nanite", "create_physics_asset", "import_materials", "import_animations", "import_static_meshes",
                  "import_skeletal_meshes", "import_morph_targets", "keep_sections_separate", "bake_meshes", "import_lods",
                  "vertex_color_import_option", "use_high_precision_skin_weights", "threshold_position", "threshold_tangent_normal"):
            try:
                d["%s.%s" % (sub, k)] = str(o.get_editor_property(k))
            except Exception:
                pass
    try:
        d["material_pipeline.texture_pipeline.import_textures"] = str(pipe.get_editor_property("material_pipeline").get_editor_property("texture_pipeline").get_editor_property("import_textures"))
    except Exception:
        pass
    try:
        d["reimport_strategy"] = str(pipe.get_editor_property("reimport_strategy"))
    except Exception:
        pass
    return d


def reimport_one(mesh):
    """Синхронный реимпорт меша через InterchangeManager.reimport_asset с нашим пайплайном. (ok, imported_paths, err)."""
    mgr = unreal.InterchangeManager.get_interchange_manager_scripted()
    pipe = make_pipeline()
    params = unreal.ImportAssetParameters()
    params.set_editor_property("reimport_asset", mesh)
    params.set_editor_property("is_automated", True)
    params.set_editor_property("replace_existing", True)
    params.set_editor_property("override_pipelines", [unreal.SoftObjectPath(pipe.get_path_name())])
    try:
        r = mgr.reimport_asset(mesh, params)
    except Exception as e:
        return False, [], "exc: %s" % str(e)[:200]
    # Python-обёртка отдаёт либо (bool, Array) либо только Array out-объектов — разбираем по типу
    if isinstance(r, tuple):
        ok = bool(r[0]); outs = [obj_path(o) for o in (r[1] if len(r) > 1 else [])]
    elif isinstance(r, bool):
        ok = r; outs = []
    else:
        outs = [obj_path(o) for o in r]; ok = len(outs) > 0
    return bool(ok), outs, None


# ───────────────────────────── main ─────────────────────────────
def main():
    t0 = time.time()
    log("режим=%s write=%s sandbox=%s groups=%s filter=%r limit=%d normals=%s root=%s" % (MODE, WRITE, SANDBOX, GROUPS, FILTER, LIMIT, NORMALS, ROOT))
    if MODE == "apply":
        log("пайплайн реимпорта: %s" % json.dumps(pipeline_desc(make_pipeline()), ensure_ascii=False))
    meshes = registry_meshes(ROOT)
    log("в реестре StaticMesh/SkeletalMesh под %s: %d" % (ROOT, len(meshes)))
    rx = re.compile(FILTER, re.I) if FILTER else None
    rows = []; skipped_src = 0; by_group = {}
    # шаг 1: источник каждого меша (загрузка — источник в теге реестра не всегда абсолютный)
    no_tag = 0
    n_login = 0   # ★ 15.09.2026: пропущено мешей сцены логина (только apply)
    for i, (pkg, cn, tag) in enumerate(meshes):
        if rx and not rx.search(pkg):
            continue
        if MODE == "apply" and _LG.login_package_blocked(pkg, "asda_reimport_normals.py", quiet=True):   # ★ 15.09.2026: сцена логина
            n_login += 1
            continue
        # тег реестра AssetImportData (JSON с RelativeFilename) — отсев без загрузки: SM карт (MeshBuilder) источника не имеют,
        #   их 12k+ под /Game/Map; грузим только кандидатов из RawAssets/Models
        if tag is None or "rawassets/models" not in tag.lower().replace("\\", "/"):
            no_tag += 1
            continue
        a = unreal.load_asset(pkg)
        if not a:
            continue
        srcs = source_files(a)
        grp = glb_group(srcs[0]) if srcs else None
        if grp is None:
            skipped_src += 1
            continue
        if "ALL" not in GROUPS and grp not in GROUPS:
            continue
        rows.append({"package": pkg, "class": cn, "source": srcs[0], "group": grp})
        by_group.setdefault(grp, []).append(len(rows) - 1)
        if (i + 1) % 400 == 0:
            log("  индекс %d/%d …" % (i + 1, len(meshes)))
            unreal.SystemLibrary.collect_garbage()
    unreal.SystemLibrary.collect_garbage()
    if n_login:   # ★ 15.09.2026
        log("[LOGIN-GUARD] пропущено мешей сцены логина: %d (реимпорт — только с ASDA_ALLOW_LOGIN_WRITE=1)" % n_login)
    if LIMIT and len(rows) > LIMIT:
        # равномерно по группам: первые LIMIT в стабильном порядке пакетов
        rows = rows[:LIMIT]
        by_group = {}
        for k, r in enumerate(rows):
            by_group.setdefault(r["group"], []).append(k)
    log("мешей из RawAssets/Models: %d (без тега/не из Models: %d, вне Models/_prev после загрузки: %d); группы: %s" % (
        len(rows), no_tag, skipped_src, ", ".join("%s=%d" % (g, len(v)) for g, v in sorted(by_group.items()))))
    # какие меши считать по нормалям
    want_normals = set()
    if NORMALS == "all":
        want_normals = set(range(len(rows)))
    elif NORMALS == "sample":
        rnd = random.Random(20260905)
        for g, ids in sorted(by_group.items()):
            pick = ids if len(ids) <= SAMPLE else rnd.sample(ids, SAMPLE)
            want_normals.update(pick)
    # шаг 2: по мешам
    mgr_ok = True
    result = {"mode": MODE, "write": WRITE, "sandbox": SANDBOX, "groups": GROUPS, "root": ROOT, "started": time.strftime("%Y-%m-%d %H:%M:%S"),
              "meshes": [], "gate_fail": None, "reimported": 0, "reimport_failed": [], "recompute_forced": 0, "nanite_forced": 0}
    if MODE == "apply":
        result["pipeline"] = pipeline_desc(make_pipeline())
    for k, r in enumerate(rows):
        pkg = r["package"]
        mesh = unreal.load_asset(pkg)
        if not mesh:
            r["error"] = "не загрузился"; result["meshes"].append(r); continue
        target = mesh
        sandbox_folder = None
        if MODE == "apply" and SANDBOX:
            # ★ Путь источника в AssetImportData ОТНОСИТЕЛЬНЫЙ (в .uasset лежит «../../../../../../RawAssets/Models/…», от папки
            #   пакета на диске). Дубликат обязан лежать на ТОЙ ЖЕ глубине, иначе реимпорт ищет «D:/../RawAssets/…» и молча
            #   отвечает «nothing imported» (зонд rn_probe 05.09: все 4 варианта пайплайна падали за 10 мс именно из-за этого).
            #   /Game/AsdaAssets/Models/... → /Game/_NormalsTest/Models/... (глубина совпадает).
            if not pkg.startswith("/Game/AsdaAssets/"):
                r["error"] = "sandbox: пакет вне /Game/AsdaAssets — глубину не воспроизвести"; result["meshes"].append(r); continue
            dst = SANDBOX_ROOT + pkg[len("/Game/AsdaAssets"):]
            sandbox_folder = dst.rsplit("/", 1)[0]
            dup = EAL.duplicate_asset(pkg, dst)
            if not dup:
                r["error"] = "sandbox duplicate failed"; result["meshes"].append(r); continue
            target = dup
            r["sandbox_package"] = dst
        before = meta_of(target)
        r["before"] = before
        folder_before = list_folder(folder_of(before["package"]))
        if k in want_normals:
            pos, nrm, idx, _b = lod0(target)
            hp, hn, cs, neg, n = normals_stats(pos, nrm, idx)
            r["before_geom"] = {"pos_sha": hp, "nrm_sha": hn, "cos": cs, "neg": neg, "n": n}
        if MODE == "apply":
            if not WRITE:
                r["would"] = "reimport %s ← %s" % (before["package"], r["source"])
            else:
                chg = set_recompute_false(target)
                if chg:
                    r["recompute_change"] = chg; result["recompute_forced"] += 1
                if set_nanite_off(target):
                    r["nanite_change"] = True; result["nanite_forced"] += 1
                t1 = time.time()
                ok, outs, err = reimport_one(target)
                r["reimport_ok"] = ok; r["reimport_sec"] = round(time.time() - t1, 2); r["imported"] = outs
                if err: r["reimport_err"] = err
                if not ok:
                    result["reimport_failed"].append(pkg)
                    log("  !! реимпорт НЕ удался: %s (%s)" % (pkg, err))
                else:
                    result["reimported"] += 1
                after = meta_of(target)
                r["after"] = after
                folder_after = list_folder(folder_of(before["package"]))
                diff = gate_diff(before, after)
                new_assets = sorted(set(folder_after) - set(folder_before))
                lost_assets = sorted(set(folder_before) - set(folder_after))
                if new_assets: diff.append("новые ассеты в папке: %s" % new_assets)
                if lost_assets: diff.append("пропали ассеты из папки: %s" % lost_assets)
                if diff:
                    r["gate"] = diff
                    result["gate_fail"] = {"package": pkg, "diff": diff}
                    log("★★★ GATE FAIL на %s: %s — СТОП без отката" % (pkg, diff))
                    if not sandbox_folder:
                        result["meshes"].append(r)
                        break
                if ok:
                    saved = EAL.save_loaded_asset(target, only_if_is_dirty=False)
                    r["saved"] = bool(saved)
                if k in want_normals:
                    pos, nrm, idx, _b = lod0(target)
                    hp, hn, cs, neg, n = normals_stats(pos, nrm, idx)
                    r["after_geom"] = {"pos_sha": hp, "nrm_sha": hn, "cos": cs, "neg": neg, "n": n}
        result["meshes"].append(r)
        if sandbox_folder:
            try:
                EAL.delete_directory(sandbox_folder)
            except Exception as e:
                log("sandbox не удалён %s: %s" % (sandbox_folder, e))
            if r.get("gate"):
                break
        if (k + 1) % 25 == 0 or k == len(rows) - 1:
            log("  [%d/%d] %s | реимпорт=%d, провалов=%d, %.0f с" % (k + 1, len(rows), r["group"], result["reimported"], len(result["reimport_failed"]), time.time() - t0))
        if (k + 1) % GC_EVERY == 0:
            unreal.SystemLibrary.collect_garbage()
    if MODE == "apply" and SANDBOX and EAL.does_directory_exist(SANDBOX_ROOT):
        EAL.delete_directory(SANDBOX_ROOT)
    # шаг 3: MIC — паренты из реестра + параметры лица/листвы/выборки
    mics = mic_registry("/Game/AsdaAssets/Models")
    result["mic_parents"] = mics
    face = sorted(p for p in mics if "face" in p.rsplit("/", 1)[-1].lower())
    fol = []
    try:
        fol = [p.split(".")[0] for p in json.load(open(FOLIAGE_JSON, encoding="utf-8")).get("touched_list", [])]
    except Exception as e:
        log("листва: %s не прочитан (%s)" % (FOLIAGE_JSON, e))
    sample_mics = set()
    # MIC слотов выборочных мешей (до SAMPLE на группу, seed 20260905) — параметры
    rnd = random.Random(20260905)
    for g, ids in sorted(by_group.items()):
        pick = ids if len(ids) <= SAMPLE else rnd.sample(ids, SAMPLE)
        for i in pick:
            if i < len(result["meshes"]):
                for _sn, mp in result["meshes"][i].get("before", {}).get("slots", []):
                    if mp: sample_mics.add(mp)
    params = {}
    for p in sorted(set(face) | set(fol) | sample_mics):
        try:
            params[p] = mic_params(p)
        except Exception as e:
            params[p] = {"error": str(e)[:80]}
    result["mic_params"] = params
    result["face_mics"] = face
    result["foliage_mics"] = fol
    result["elapsed_sec"] = round(time.time() - t0, 1)
    # итог по группам
    summ = {}
    for r in result["meshes"]:
        g = r["group"]; s = summ.setdefault(g, {"n": 0, "with_geom": 0, "cos_before": 0.0, "cos_after": 0.0, "n_after": 0, "reimported": 0})
        s["n"] += 1
        if r.get("before_geom") and r["before_geom"].get("cos") is not None:
            s["with_geom"] += 1; s["cos_before"] += r["before_geom"]["cos"]
        if r.get("after_geom") and r["after_geom"].get("cos") is not None:
            s["n_after"] += 1; s["cos_after"] += r["after_geom"]["cos"]
        if r.get("reimport_ok"): s["reimported"] += 1
    for g, s in sorted(summ.items()):
        log("ГРУППА %-28s мешей=%4d cos_до=%s cos_после=%s реимпорт=%d" % (
            g, s["n"], ("%.3f" % (s["cos_before"] / s["with_geom"])) if s["with_geom"] else "—",
            ("%.3f" % (s["cos_after"] / s["n_after"])) if s["n_after"] else "—", s["reimported"]))
    result["summary"] = summ
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        json.dump(result, fh, ensure_ascii=False, indent=1)
    log("дамп → %s (%d мешей, %d MIC, %d параметров MIC)" % (OUT, len(result["meshes"]), len(mics), len(params)))
    if result["gate_fail"]:
        log("RN-GATE-FAIL %s" % result["gate_fail"]["package"])
    log("RN-FINISHED mode=%s reimported=%d failed=%d gate_fail=%s %.0f с" % (
        MODE, result["reimported"], len(result["reimport_failed"]), bool(result["gate_fail"]), time.time() - t0))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
