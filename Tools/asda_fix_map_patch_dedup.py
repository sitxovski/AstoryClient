# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_map_patch_dedup.py — ТОЧЕЧНАЯ правка уже собранных уровней карт после исправления дедупа .mapraw (28.10.2026).
#
# ★★★ ПРИЧИНА (Reports/map_children_transforms/REPORT.md). Экспортёр AsdaMapViewer --export склеивал слайсы геометрии
#   объектов по ключу (section,pool,slot,geomIdx,startVtx,startIdx,vtxCount) БЕЗ номера патча, а буферы Pos0/1/2 у каждого
#   патча свои. Инстанс из «чужого» патча получал геометрию (и текстуру) первого встреченного патча: шахматные доски
#   castle_of_chess/palace_of_queen несли плоскость и бортики МАЛОЙ доски, на alpen/conquester/silalis — чужие деревья,
#   стены и травы, на quizstage знак «X» стоял вместо «O». Экспортёр исправлен (asda_export.cpp, проход 2): прежние
#   модели остаются на своих индексах и под своими именами, исправленным инстансам назначается верная модель —
#   существующая (та же геометрия) или новая, дописанная в конец списка.
#
# ★ ЧТО ДЕЛАЕТ (уровень НЕ пересобирается — правки владельца в картах: свет, NPC, эффекты, яркость — не трогаются):
#   меняется ТОЛЬКО актор-держатель AsdaMapObjects_<карта> (разрешение владельца 28.10: его можно приводить к данным
#   целиком, прочие акторы не трогать). Каждый его ISM «I_<модель>» приводится ровно к инстансам этой модели по
#   исправленному .mapraw (ASDA_NEW_RAW): совпавшие остаются на месте, лишние убираются (в т. ч. детали досок, которые
#   масштабировали/сдвигали руками, пытаясь исправить), недостающие добавляются; нет компонента — создаётся с
#   настройками компонента прежней модели этих инстансов; нет SM — собирается тем же путём, что asda_build_map_level.
#   Прежний .mapraw (ASDA_OLD_RAW) нужен для сверки: его модели обязаны совпасть с началом нового списка.
#   До и после снимается отпечаток всех акторов/компонентов, кроме держателя: расхождение — уровень НЕ сохраняется.
#
# Запуск (headless, через run_headless_safe.py):
#   ASDA_MAPS=t_castle_of_chess,palace_of_queen | ALL     — список карт (имена .mapraw без расширения)
#   ASDA_OLD_RAW=@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps         — прежний .mapraw (по нему собраны уровни)
#   ASDA_NEW_RAW=<папка исправленного экспорта>
#   ASDA_APPLY=1                                           — записать (иначе сухой прогон: только отчёт)
#   ASDA_BACKUP=@@ASTORY_DEV_ROOT_FWD@@/_backup/<дата>_<сет>           — куда копировать .umap до записи (обязателен при ASDA_APPLY=1)
#   ASDA_REPORT=<путь .json>                               — отчёт по картам
# Маркер конца: «ЗАВЕРШЕНО asda_fix_map_patch_dedup».
import os, sys, json, shutil, math
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_build_map_level as BML     # read_mapraw / inst_transform / _build_sm / get_material (импорт без сайд-эффектов)
import asda_maps as _MAPS
import asda_login_guard as _LG

OLD_RAW = os.environ.get("ASDA_OLD_RAW", r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps")
NEW_RAW = os.environ.get("ASDA_NEW_RAW", "")
APPLY = os.environ.get("ASDA_APPLY", "0") == "1"
BACKUP = os.environ.get("ASDA_BACKUP", "")
REPORT = os.environ.get("ASDA_REPORT", "")
CONTENT = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Content"
EAL = unreal.EditorAssetLibrary
POS_TOL = 1.0          # допуск совпадения позиции инстанса, UE-см (уровень строился из тех же матриц)
ROT_TOL = 1e-3         # допуск 1 − |q1·q2|

# Настройки компонента, переносимые на новый ISM с компонента-источника (у сборщика они одинаковые у всех,
# но владелец мог менять отдельные компоненты руками — берём фактические значения источника).
COPY_PROPS = ("mobility", "cast_shadow", "cast_dynamic_shadow", "cast_static_shadow", "affect_distance_field_lighting",
              "affect_dynamic_indirect_lighting", "visible", "hidden_in_game", "receives_decals",
              "instance_start_cull_distance", "instance_end_cull_distance", "ld_max_draw_distance",
              "generate_overlap_events", "can_character_step_up_on", "lighting_channels", "use_as_occluder",
              "render_custom_depth", "custom_depth_stencil_value", "translucency_sort_priority",
              "cast_hidden_shadow", "cast_shadow_as_two_sided", "visible_in_ray_tracing", "evaluate_world_position_offset")


def log(m):
    unreal.log("[patchdedup] " + str(m))


def _get(o, p):
    try:
        return o.get_editor_property(p)
    except Exception:
        return None


def _ncd(ism):
    """Число custom data float у ISM (NumCustomDataFloats; геттера-функции в Python нет)."""
    return int(_get(ism, "num_custom_data_floats") or 0)


def _set_map_globals(name):
    """Глобалы сборщика на эту карту (как в asda_build_map_level._script_main) + сброс кэшей."""
    BML.MAP_NAME = name
    BML.ASSET = _MAPS.map_asset_name(name)
    BML.DEST_ROOT = "/Game/Map/" + BML.ASSET
    BML.TEX_ROOT = BML.DEST_ROOT + "/Texture"
    BML._tex_cache.clear(); BML._mat_cache.clear(); BML._used_sm_names.clear()
    BML._tex_index = None


def _quat(t):
    q = t.rotation
    return (q.x, q.y, q.z, q.w)


def _same_xf(a, b):
    la, lb = a.translation, b.translation
    if abs(la.x - lb.x) > POS_TOL or abs(la.y - lb.y) > POS_TOL or abs(la.z - lb.z) > POS_TOL:
        return False
    qa, qb = _quat(a), _quat(b)
    return 1.0 - abs(sum(x * y for x, y in zip(qa, qb))) < ROT_TOL and \
        all(abs(s - 1.0) < 1e-3 for s in (b.scale3d.x, b.scale3d.y, b.scale3d.z))


def _fmt(v):
    if isinstance(v, float):
        return round(v, 3)
    if isinstance(v, (int, bool, str)) or v is None:
        return v
    try:
        return str(v.export_text())
    except Exception:
        return str(v)


def fingerprint(holder_label):
    """Отпечаток уровня БЕЗ держателя объектов карты: акторы, их компоненты, трансформы и ключевые свойства
    (свет/туман/постпроцесс/эффекты/NPC). Сверка до/после доказывает, что правка не задела ничего, кроме держателя."""
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    out = {}
    lprops = ("intensity", "light_color", "temperature", "use_temperature", "attenuation_radius", "source_radius",
              "inner_cone_angle", "outer_cone_angle", "indirect_lighting_intensity", "volumetric_scattering_intensity",
              "cast_shadows", "mobility", "visible", "hidden_in_game", "static_mesh", "skeletal_mesh_asset",
              "fog_density", "fog_height_falloff", "fog_inscattering_luminance", "settings", "unbound", "priority",
              "blend_weight", "enabled", "cubemap", "source_type", "lower_hemisphere_color")
    for a in eas.get_all_level_actors():
        lab = a.get_actor_label()
        if lab == holder_label:
            continue
        key = "%s|%s" % (lab, a.get_name())
        t = a.get_actor_transform()
        rec = {"class": a.get_class().get_name(),
               "xf": [round(t.translation.x, 2), round(t.translation.y, 2), round(t.translation.z, 2),
                      round(t.rotation.x, 5), round(t.rotation.y, 5), round(t.rotation.z, 5), round(t.rotation.w, 5),
                      round(t.scale3d.x, 4), round(t.scale3d.y, 4), round(t.scale3d.z, 4)],
               "hidden": a.is_hidden_ed(), "comps": {}}
        for c in a.get_components_by_class(unreal.ActorComponent):
            cr = {"class": c.get_class().get_name()}
            if isinstance(c, unreal.SceneComponent):
                rt = c.get_relative_transform()
                cr["xf"] = [round(rt.translation.x, 2), round(rt.translation.y, 2), round(rt.translation.z, 2),
                            round(rt.rotation.x, 5), round(rt.rotation.y, 5), round(rt.rotation.z, 5),
                            round(rt.rotation.w, 5), round(rt.scale3d.x, 4), round(rt.scale3d.y, 4), round(rt.scale3d.z, 4)]
            for p in lprops:
                v = _get(c, p)
                if v is not None:
                    cr[p] = _fmt(v)
            if isinstance(c, unreal.InstancedStaticMeshComponent):
                cr["n"] = c.get_instance_count()
            cr_key = c.get_name()
            rec["comps"][cr_key] = cr
        out[key] = rec
    return out


def diff_fp(a, b):
    d = []
    for k in sorted(set(a) | set(b)):
        if k not in a: d.append("+ " + k); continue
        if k not in b: d.append("- " + k); continue
        if json.dumps(a[k], sort_keys=True) != json.dumps(b[k], sort_keys=True):
            d.append("~ " + k)
    return d


def holder_and_comps():
    """Держатель AsdaMapObjects_* и его ISM, ключ — путь пакета меша (имя компонента ненадёжно: при совпадении
    имён моделей сборщик даёт мешу суффикс _<idx>, а компонент получает имя по умолчанию «InstancedStaticMesh<N>»)."""
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    holders = [a for a in eas.get_all_level_actors() if a.get_actor_label().startswith("AsdaMapObjects")]
    if len(holders) != 1:
        return None, {}, []
    h = holders[0]
    comps = {}; dup = []
    for c in h.get_components_by_class(unreal.InstancedStaticMeshComponent):
        sm = c.static_mesh
        key = sm.get_outermost().get_name() if sm else "<нет меша>:" + c.get_name()
        if key in comps:
            dup.append([key, c.get_name()])
            continue
        comps[key] = c
    return h, comps, dup


def sm_asset_names(models):
    """Имена SM по порядку моделей — ровно как asda_build_map_level.build_mesh (_used_sm_names): повтор имени → _<idx>."""
    used = {}; out = []
    for idx, m in enumerate(models):
        base = "SM_" + m["name"]
        cnt = used.get(base, 0); used[base] = cnt + 1
        out.append(base if cnt == 0 else "%s_%d" % (base, idx))
    return out


def build_new_sm(model, name):
    """SM для дописанной модели — тем же путём, что asda_build_map_level.build_mesh (свежий ассет)."""
    path = BML.DEST_ROOT + "/Models/" + name
    if EAL.does_asset_exist(path):
        return None, "ассет %s уже есть — не перезаписываю" % path
    sm = BML.ATOOLS.create_asset(name, BML.DEST_ROOT + "/Models", unreal.StaticMesh, None)
    if sm is None:
        return None, "create_asset вернул None (%s)" % path
    st = BML._build_sm(sm, name, model["pos"], model["nrm"], model["uv"], None, model["idx"], None, ["Mat0"])
    if st is None:
        return None, "сборка геометрии не удалась (%s)" % path
    mic = None
    try:
        mic = BML.get_material(model["tex"])
    except Exception as e:
        log("SM %s: материал '%s' не назначен (%s)" % (name, model["tex"], e))
    if mic:
        sm.set_editor_property("static_materials",
                               [unreal.StaticMaterial(material_interface=mic, material_slot_name="Mat0")])
    EAL.save_loaded_asset(sm)
    unreal.log("ASDA_WRITES_ASSET: " + path)
    return sm, None


def add_ism(holder, name, src, sm):
    """Новый ISM «I_<модель>» на держателе (как в asda_build_map_level.main) с настройками компонента-образца."""
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    root = sds.k2_gather_subobject_data_for_instance(holder)[0]
    params = unreal.AddNewSubobjectParams()
    params.set_editor_property("parent_handle", root)
    params.set_editor_property("new_class", unreal.InstancedStaticMeshComponent)
    sub, fail = sds.add_new_subobject(params)
    if fail and str(fail):
        return None, "ISM add FAIL: %s" % fail
    sds.rename_subobject(sub, unreal.Text(name))   # при занятом имени UE оставит своё — как у сборщика; ключ всё равно меш
    comp = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(sds.k2_find_subobject_data_from_handle(sub))
    ism = unreal.InstancedStaticMeshComponent.cast(comp)
    ism.set_static_mesh(sm)
    for p in COPY_PROPS:
        v = _get(src, p)
        if v is not None:
            try:
                ism.set_editor_property(p, v)
            except Exception:
                pass
    try:
        ism.set_collision_profile_name(src.get_collision_profile_name())
        ism.set_collision_enabled(src.get_collision_enabled())
    except Exception:
        pass
    return ism, None


def process_map(name):
    rep = {"map": name, "changed": 0, "removed": 0, "added": 0, "comps_touched": [], "new_sm": [], "new_comp": [],
           "owner_scaled": [], "foreign": [], "dup_mesh": [], "uv_added": 0, "uv_no_scroll": [], "errors": [],
           "fp_diff": None, "saved": False}
    old_p = os.path.join(OLD_RAW, name + ".mapraw"); new_p = os.path.join(NEW_RAW, name + ".mapraw")
    if not (os.path.exists(old_p) and os.path.exists(new_p)):
        rep["errors"].append("нет .mapraw"); return rep
    od = BML.read_mapraw(old_p); nd = BML.read_mapraw(new_p)
    om, nm = od["models"], nd["models"]
    if len(od["instances"]) != len(nd["instances"]) or len(nm) < len(om):
        rep["errors"].append("структура .mapraw не совпала"); return rep
    for i in range(len(om)):
        if om[i]["name"] != nm[i]["name"] or om[i]["pos"] != nm[i]["pos"]:
            rep["errors"].append("прежняя модель %d изменилась — правка невозможна" % i); return rep
    nchg = 0
    for a, b in zip(od["instances"], nd["instances"]):
        if list(a["m"]) != list(b["m"]):
            rep["errors"].append("матрица инстанса разошлась"); return rep
        nchg += (a["model"] != b["model"])
    rep["changed"] = nchg
    if not nchg:
        return rep
    _set_map_globals(name)
    lvl = "%s/%s" % (BML.DEST_ROOT, BML.ASSET)
    if _LG.login_map_write_blocked(BML.DEST_ROOT, "asda_fix_map_patch_dedup.py", quiet=True) or \
            _LG.map_name_blocked(lvl, "asda_fix_map_patch_dedup.py"):
        rep["errors"].append("гейт логина"); return rep
    if not EAL.does_asset_exist(lvl):
        rep["errors"].append("нет уровня " + lvl); return rep
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    les.load_level(lvl)
    holder, comps, dup = holder_and_comps()
    if holder is None:
        rep["errors"].append("держатель AsdaMapObjects не найден/не один"); return rep
    rep["dup_mesh"] = dup
    hlabel = holder.get_actor_label()
    fp_before = fingerprint(hlabel)
    smn = sm_asset_names(nm)
    mdir = BML.DEST_ROOT + "/Models/"

    # 1) СИНХРОНИЗАЦИЯ держателя с исправленным .mapraw (разрешение владельца 28.10: держатель AsdaMapObjects_* можно
    #    приводить к данным целиком, прочие акторы — не трогать). Каждый ISM меша SM_<модель> должен содержать ровно
    #    инстансы этой модели по .mapraw: совпавшие (позиция/поворот, масштаб 1) остаются на месте, лишние убираются
    #    (сюда попадают и детали, сдвинутые/отмасштабированные руками при попытке исправить доски), недостающие
    #    добавляются. Компонент без расхождений не меняется вовсе.
    expect = {}                    # путь SM -> [(FTransform, uvScroll)]
    tmpl = {}                      # путь SM -> путь SM прежней модели этих инстансов (образец настроек компонента)
    involved = set()               # меши прежних и новых моделей инстансов, сменивших модель — только их и сверяем
    for ia, ib in zip(od["instances"], nd["instances"]):
        if ia["model"] != ib["model"]:
            involved.add(mdir + smn[ia["model"]]); involved.add(mdir + smn[ib["model"]])
    for ia, ib in zip(od["instances"], nd["instances"]):
        key = mdir + smn[ib["model"]]
        if key not in involved:
            continue
        expect.setdefault(key, []).append((BML.inst_transform(ib["m"]), list(ib["uvScroll"])))
        tmpl.setdefault(key, mdir + smn[ia["model"]])
    for key in involved:           # модель, у которой не осталось инстансов: компонент нужно опустошить
        expect.setdefault(key, [])
    allkeys = {mdir + n for n in smn}
    for key, ism in comps.items():
        if key not in allkeys and ism.get_instance_count() > 0:
            rep["foreign"].append([key, ism.get_name(), ism.get_instance_count()])
    plan_rm = {}; plan_add = {}
    for key, E in expect.items():
        ism = comps.get(key)
        A = [ism.get_instance_transform(i, False) for i in range(ism.get_instance_count())] if ism else []
        usedA = set(); miss = []
        for w, uv in E:
            hit = -1
            for i, t in enumerate(A):
                if i not in usedA and _same_xf(w, t):
                    hit = i; break
            if hit < 0:
                miss.append((w, uv))
            else:
                usedA.add(hit)
        extra = [i for i in range(len(A)) if i not in usedA]
        if extra:
            plan_rm[key] = extra
            for i in extra:
                t = A[i]
                if any(abs(c - 1.0) > 1e-3 for c in (t.scale3d.x, t.scale3d.y, t.scale3d.z)):
                    rep["owner_scaled"].append([key.split("/")[-1], round(t.translation.x, 1), round(t.translation.y, 1),
                                                round(t.translation.z, 1), round(t.scale3d.x, 3), round(t.scale3d.y, 3),
                                                round(t.scale3d.z, 3)])
        if miss:
            plan_add[key] = miss
            for w, uv in miss:
                if any(abs(x) > 1e-7 for x in uv[:2]) and not (ism and _ncd(ism) >= 2):
                    rep["uv_no_scroll"].append(key.split("/")[-1])
        if ism is None and E:
            rep["new_comp"].append(key)
            if not EAL.does_asset_exist(key):
                rep["new_sm"].append(key.split("/")[-1])
    rep["removed"] = sum(len(v) for v in plan_rm.values())
    rep["added"] = sum(len(v) for v in plan_add.values())
    rep["comps_touched"] = sorted(k.split("/")[-1] for k in set(plan_rm) | set(plan_add))
    if not APPLY or not rep["comps_touched"]:
        return rep
    if any(d[0] in involved for d in rep["dup_mesh"]):
        rep["errors"].append("в держателе есть компоненты вне .mapraw/с одним мешем — запись отменена"); return rep

    # 2) бэкап .umap
    rel = BML.DEST_ROOT.replace("/Game/", "") + "/" + BML.ASSET + ".umap"
    src_file = os.path.join(CONTENT, rel)
    dst_file = os.path.join(BACKUP, "AstoryClient/Content", rel)
    os.makedirs(os.path.dirname(dst_file), exist_ok=True)
    if not os.path.exists(dst_file):
        shutil.copy2(src_file, dst_file)
    if os.path.getsize(dst_file) != os.path.getsize(src_file):
        rep["errors"].append("бэкап не совпал по размеру — запись отменена"); return rep

    # 3) недостающие SM и ISM
    idx_of = {mdir + smn[i]: i for i in range(len(nm))}
    for key in rep["new_comp"]:
        if EAL.does_asset_exist(key):
            sm, err = EAL.load_asset(key), None
        else:
            sm, err = build_new_sm(nm[idx_of[key]], key.split("/")[-1])
        if sm is None:
            rep["errors"].append(err); return rep
        src = comps.get(tmpl.get(key)) or next(iter(comps.values()))
        ism, err = add_ism(holder, "I_" + nm[idx_of[key]]["name"], src, sm)
        if ism is None:
            rep["errors"].append(err); return rep
        comps[key] = ism

    # 4) удалить лишние (по убыванию индексов), затем добавить недостающие; UV-скролл (custom data 0/1, как
    #    asda_add_uvscroll) — из .mapraw, если у компонента он заведён
    for key, idxs in plan_rm.items():
        for i in sorted(idxs, reverse=True):
            comps[key].remove_instance(i)
    for key, ws in plan_add.items():
        dst = comps[key]
        ncd = _ncd(dst)
        for w, uv in ws:
            ni = dst.add_instance(w)
            if ncd >= 2:
                dst.set_custom_data_value(ni, 0, uv[0]); dst.set_custom_data_value(ni, 1, uv[1])
                rep["uv_added"] += 1
    for key, E in expect.items():
        if key in comps and comps[key].get_instance_count() != len(E):
            rep["errors"].append("%s: инстансов %d, ожидалось %d — НЕ сохраняю"
                                 % (key, comps[key].get_instance_count(), len(E)))
            return rep

    # 5) контроль: все инстансы на месте, остальной уровень не изменился
    fp_after = fingerprint(hlabel)
    d = diff_fp(fp_before, fp_after)
    rep["fp_diff"] = d
    if d:
        rep["errors"].append("отпечаток уровня вне держателя изменился — НЕ сохраняю: %s" % d[:10])
        return rep
    les.save_current_level()
    rep["saved"] = True
    unreal.log("ASDA_WRITES_ASSET: " + lvl)
    return rep


def main():
    maps = os.environ.get("ASDA_MAPS", "")
    if maps == "ALL":
        maps = sorted(f[:-7] for f in os.listdir(NEW_RAW) if f.endswith(".mapraw"))
    else:
        maps = [m for m in maps.split(",") if m]
    if APPLY and not BACKUP:
        log("ASDA_APPLY=1 без ASDA_BACKUP — отказ"); return
    reps = []
    for m in maps:
        try:
            r = process_map(m)
        except Exception as e:
            r = {"map": m, "errors": ["исключение: %r" % e]}
        reps.append(r)
        log("%s: инстансов со сменой модели %s, убрано %s, добавлено %s, компонентов %d, новых SM %d, новых ISM %d, "
            "ручных масштабов %d, чужих компонентов %s, дублей меша %s, UV-скролл %s/без скролла %d, сохранено %s, ошибки %s"
            % (m, r.get("changed"), r.get("removed"), r.get("added"), len(r.get("comps_touched", [])),
               len(r.get("new_sm", [])), len(r.get("new_comp", [])), len(r.get("owner_scaled", [])), r.get("foreign"),
               r.get("dup_mesh"), r.get("uv_added"), len(r.get("uv_no_scroll", [])), r.get("saved"), r.get("errors")))
    if REPORT:
        with open(REPORT, "w", encoding="utf-8") as f:
            json.dump(reps, f, ensure_ascii=False, indent=1)
    unreal.log("ЗАВЕРШЕНО asda_fix_map_patch_dedup (%s)" % ("ЗАПИСЬ" if APPLY else "сухой прогон"))


# ★ ЗАКОН [[tools-no-import-side-effects]]
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
