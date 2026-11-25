# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _shot_backdrop_shadows.py — КОНТРОЛЬНЫЕ КАДРЫ ПУТЁМ ИГРОКА (-game): отбрасывают ли элементы задника логина
#   (актор-тег AsdaBackdropElem) тени. Меняет ТОЛЬКО рантайм-свойства компонентов текущего прогона —
#   ничего не сохраняет (★★★ Login.umap — ручной свет владельца, запись запрещена).
#   Кадры: A/A2 (шум), B/B2 (все элементы SetCastShadow(false)), C (возврат), D (позитивный контроль:
#   тень шкатулки выкл), затем изоляция по источникам: только Sun / только SpotLight / только HolyLight —
#   у каждого пара «тени элементов вкл/выкл». Плюс визуализация ShowFlag.ShadowFrustums.
# Запуск (СКРЫТО):
#   python Tools/set0_run_game.py Tools/_shot_backdrop_shadows.py --map /Game/Map/Login --res 1600x900 \
#          --log Reports/backdrop_shadows/game.log --env ASDA_BDS_TAG=run1
import os, glob, json, shutil

OUTD = os.environ.get("ASDA_BDS_DIR", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/backdrop_shadows")
TAG = os.environ.get("ASDA_BDS_TAG", "run1")
SSDIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
SETTLE = int(os.environ.get("ASDA_BDS_SETTLE", "300"))
CVARS_I = ["r.Shadow.Virtual.Enable", "r.Shadow.CSM.MaxCascades", "sg.ShadowQuality", "r.ShadowQuality",
           "r.Nanite", "r.DynamicGlobalIlluminationMethod", "r.DistanceFieldShadowing", "r.ContactShadows",
           "r.Shadow.Virtual.NonNanite.ProjectEnabled"]
CVARS_F = ["r.Shadow.DistanceScale", "r.Shadow.RadiusThreshold", "r.Shadow.CSM.TransitionScale",
           "r.Shadow.MaxResolution", "r.Shadow.PerObject"]


def main():
    import unreal
    st = {"n": 0, "w": None, "q": [], "seen": set(), "lights": {}, "elems": [], "casket": []}
    R = {"tag": TAG, "shots": [], "runtime": {}, "notes": []}

    def log(m):
        unreal.log("[bd-shadow] " + str(m))

    def note(m):
        R["notes"].append(str(m)); log("NOTE: " + str(m))

    def cc(c):
        unreal.SystemLibrary.execute_console_command(st["w"], c); log("cmd: " + c)

    def after(frames, fn):
        st["q"].append([frames, fn])

    def run_queue():
        if st["q"]:
            it = st["q"][0]; it[0] -= 1
            if it[0] <= 0:
                st["q"].pop(0); it[1]()

    def find_world():
        for path in ("/Game/Map/Login.Login", "/Game/Map/Login"):
            for fn in (unreal.find_object, unreal.load_object):
                try:
                    o = fn(None, path)
                    if o and isinstance(o, unreal.World):
                        return o
                except Exception:
                    pass
        return None

    def take(name):
        cur = set(glob.glob(os.path.join(SSDIR, "**", "*.png"), recursive=True))
        new = sorted(cur - st["seen"], key=os.path.getmtime); st["seen"] |= cur
        if not new:
            note("нет нового PNG для %s" % name); return
        dst = os.path.join(OUTD, "bds_%s_%s.png" % (TAG, name))
        shutil.copy(new[-1], dst)
        R["shots"].append({"name": name, "png": dst})
        log("СОХРАНЁН -> %s" % dst)

    def shot(name):
        after(90, lambda: cc("HighResShot 1600x900"))
        after(30, lambda: take(name))

    def collect():
        w = st["w"]
        for a in unreal.GameplayStatics.get_all_actors_with_tag(w, "AsdaBackdropElem"):
            st["elems"] += list(a.get_components_by_class(unreal.PrimitiveComponent))
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Actor):
            try:
                lbl = a.get_actor_label()
            except Exception:
                lbl = a.get_name()
            if lbl.startswith("LoginCasket"):
                st["casket"] += list(a.get_components_by_class(unreal.PrimitiveComponent))
            for c in a.get_components_by_class(unreal.LightComponentBase):
                st["lights"][lbl] = [c, float(c.get_editor_property("intensity"))]

    def dump_runtime(key):
        d = {"cvars": {}}
        for n in CVARS_I:
            try:
                d["cvars"][n] = unreal.SystemLibrary.get_console_variable_int_value(n)
            except Exception:
                pass
        for n in CVARS_F:
            try:
                d["cvars"][n] = unreal.SystemLibrary.get_console_variable_float_value(n)
            except Exception:
                pass
        h = {}
        for c in st["elems"]:
            lc = c.get_editor_property("lighting_channels")
            for k, v in (("cast_shadow", c.get_editor_property("cast_shadow")),
                         ("cast_dynamic_shadow", c.get_editor_property("cast_dynamic_shadow")),
                         ("visible", c.is_visible()), ("hidden_in_game", c.get_editor_property("hidden_in_game")),
                         ("render_in_depth_pass", c.get_editor_property("render_in_depth_pass")),
                         ("mobility", str(c.get_editor_property("mobility"))),
                         ("was_recently_rendered", c.was_recently_rendered(0.5)),
                         ("lc", "%d%d%d" % (lc.channel0, lc.channel1, lc.channel2))):
                kk = "%s=%s" % (k, v); h[kk] = h.get(kk, 0) + 1
        d["n_elem_comps"] = len(st["elems"]); d["elem_hist"] = h
        d["casket"] = [{"name": c.get_name(), "cast_shadow": c.get_editor_property("cast_shadow"),
                        "visible": c.is_visible()} for c in st["casket"]]
        d["lights"] = {}
        for lbl, (c, i0) in st["lights"].items():
            e = {"class": c.get_class().get_name(), "intensity": float(c.get_editor_property("intensity")),
                 "cast_shadows": c.get_editor_property("cast_shadows"), "visible": c.is_visible()}
            try:
                e["world_loc"] = [round(x, 1) for x in (c.get_world_location().x, c.get_world_location().y, c.get_world_location().z)]
                fv = c.get_forward_vector(); e["fwd"] = [round(fv.x, 3), round(fv.y, 3), round(fv.z, 3)]
            except Exception:
                pass
            d["lights"][lbl] = e
        for a in unreal.GameplayStatics.get_all_actors_with_tag(st["w"], "AsdaBackdropElem"):
            if a.get_actor_label() == "BackdropElem_bg_102_e00_00":
                org, ext = a.get_actor_bounds(False)
                l = a.get_actor_location()
                d["bg_102"] = {"loc": [round(l.x, 1), round(l.y, 1), round(l.z, 1)],
                               "bounds_o": [round(org.x, 1), round(org.y, 1), round(org.z, 1)],
                               "bounds_e": [round(ext.x, 1), round(ext.y, 1), round(ext.z, 1)]}
        pc = unreal.GameplayStatics.get_player_controller(st["w"], 0)
        if pc:
            cm = pc.player_camera_manager
            l = cm.get_camera_location(); r = cm.get_camera_rotation()
            d["cam"] = {"loc": [round(l.x, 1), round(l.y, 1), round(l.z, 1)],
                        "rot_pyr": [round(r.pitch, 2), round(r.yaw, 2), round(r.roll, 2)], "fov": cm.get_fov_angle()}
        R["runtime"][key] = d
        log("RUNTIME %s: %s" % (key, json.dumps(d, ensure_ascii=False)))

    def set_elem_shadow(on):
        for c in st["elems"]:
            c.set_cast_shadow(bool(on))
        log("элементы задника cast_shadow = %s (%d комп.)" % (bool(on), len(st["elems"])))

    def set_casket_shadow(on):
        for c in st["casket"]:
            c.set_cast_shadow(bool(on))
        log("шкатулка cast_shadow = %s" % bool(on))

    def solo(label):
        for lbl, (c, i0) in st["lights"].items():
            c.set_intensity(i0 if (label is None or lbl == label) else 0.0)
        log("свет: solo=%s" % label)

    def finish():
        with open(os.path.join(OUTD, "bds_%s_shots.json" % TAG), "w", encoding="utf-8") as f:
            json.dump(R, f, ensure_ascii=False, indent=1)
        log("ЗАВЕРШЕНО")
        cc("quit")

    def phase_init():
        st["w"] = find_world()
        if st["w"] is None:
            unreal.log_error("[bd-shadow] МИР НЕ НАЙДЕН"); unreal.SystemLibrary.execute_console_command(None, "quit"); return
        os.makedirs(OUTD, exist_ok=True)
        st["seen"] = set(glob.glob(os.path.join(SSDIR, "**", "*.png"), recursive=True))
        cc("r.SetRes 1600x900w"); cc("r.ScreenPercentage 100")
        cc("asda.LoginBackdropAnim 0")          # заморозка качания фона (шум кадров)
        mode = os.environ.get("ASDA_BDS_MODE")
        after(SETTLE, {"exp": phase_exp, "exp2": phase_exp2, "exp3": phase_exp3, "exp4": phase_exp4,
                       "exp5": phase_exp5, "fix": phase_fix}.get(mode, phase_shots))

    # ★ Режим fix (14.09.2026): проверка ЗАПИСАННОГО исправления (слот Mat0 в SM_m*), авторский свет.
    #   Для уровней GI Epic(3) и High(2): «после» (как на диске) — пара + тени элементов выкл + Lumen выкл;
    #   «до» — слоты убраны В ПАМЯТИ (ассеты не сохраняются) — пара + Lumen выкл; затем слоты возвращаются.
    def phase_fix():
        collect()
        NEVER = unreal.PropertyAccessChangeNotifyMode.NEVER
        meshes = {}
        for c in st["elems"]:
            sm = c.static_mesh
            if sm:
                meshes.setdefault(sm.get_path_name(), [sm, list(sm.get_editor_property("static_materials"))])
        R["runtime"]["fix_meshes"] = len(meshes)
        R["runtime"]["fix_slots_hist"] = {}
        for p, (sm, sl) in meshes.items():
            k = str(len(sl)); R["runtime"]["fix_slots_hist"][k] = R["runtime"]["fix_slots_hist"].get(k, 0) + 1
        R["runtime"]["fix_num_mats_hist"] = {}
        for c in st["elems"]:
            k = str(c.get_num_materials()); R["runtime"]["fix_num_mats_hist"][k] = R["runtime"]["fix_num_mats_hist"].get(k, 0) + 1
        log("fix: мешей %d, слоты %s, GetNumMaterials %s" % (len(meshes), R["runtime"]["fix_slots_hist"], R["runtime"]["fix_num_mats_hist"]))

        def refresh():
            for c in st["elems"]:
                c.set_cast_shadow(False); c.set_cast_shadow(True)

        def slots(on):
            for p, (sm, sl) in meshes.items():
                sm.set_editor_property("static_materials", sl if on else [], NEVER)
            refresh()
            log("слоты мешей в памяти: %s" % ("как на диске" if on else "УБРАНЫ (состояние до правки)"))

        def cv(key):
            d = {}
            for n in ("sg.GlobalIlluminationQuality", "sg.ShadowQuality", "r.DynamicGlobalIlluminationMethod",
                      "r.Lumen.DiffuseIndirect.Allow", "r.Shadow.Virtual.Enable", "r.ReflectionMethod"):
                try:
                    d[n] = unreal.SystemLibrary.get_console_variable_int_value(n)
                except Exception:
                    pass
            R["runtime"]["cvars_" + key] = d
            log("CVARS %s: %s" % (key, d))

        def wshot(name, wait=150):
            after(wait, lambda: cc("HighResShot 1600x900"))
            after(30, lambda: take(name))
        for gi in (3, 2):
            g = "G%d" % gi
            after(1, lambda gi=gi: cc("sg.GlobalIlluminationQuality %d" % gi))
            after(2, lambda g=g: cv(g))
            wshot(g + "_after_on", 240); wshot(g + "_after_on2")
            after(1, lambda: set_elem_shadow(False)); wshot(g + "_after_off")
            after(1, lambda: set_elem_shadow(True))
            after(1, lambda: cc("r.DynamicGlobalIlluminationMethod 0")); wshot(g + "_after_nolumen", 240)
            after(1, lambda: cc("r.DynamicGlobalIlluminationMethod 1"))
            after(1, lambda: slots(False)); wshot(g + "_before_on", 240); wshot(g + "_before_on2")
            after(1, lambda: cc("r.DynamicGlobalIlluminationMethod 0")); wshot(g + "_before_nolumen", 240)
            after(1, lambda: cc("r.DynamicGlobalIlluminationMethod 1"))
            after(1, lambda: slots(True))
        after(1, lambda: cc("sg.GlobalIlluminationQuality 3"))
        after(5, finish)

    # ★ Режим exp5: МЕШ или МАТЕРИАЛ + ПРОВЕРКА ИСПРАВЛЕНИЯ (в памяти, без сохранения ассетов).
    #   Та же доказанная точка песка (exp4), солнце вниз. Y6 SM_m102+DefaultMaterial, Y7 Cube+MIC,
    #   Y8 SM_m102+MIC после добавления в ПАМЯТИ слота StaticMaterials (у SM_m* их 0 → GetNumMaterials()=0).
    #   Z: слот в памяти у ВСЕХ мешей элементов, авторский свет — кадры «тени элементов вкл/выкл» (вид «после»).
    def phase_exp5():
        collect()
        w = st["w"]
        sun_lbl = next((l for l, (c, i) in st["lights"].items() if c.get_class().get_name() == "DirectionalLightComponent"), None)
        sun_a = st["lights"][sun_lbl][0].get_owner()
        rot0 = sun_a.get_actor_rotation()
        el = next(a for a in unreal.GameplayStatics.get_all_actors_with_tag(w, "AsdaBackdropElem")
                  if a.get_actor_label() == "BackdropElem_bg_102_e00_00")
        comp = el.static_mesh_component
        loc0 = el.get_actor_location(); mesh0 = comp.static_mesh; mat0 = comp.get_material(0)
        cube = unreal.load_object(None, "/Engine/BasicShapes/Cube.Cube")
        defmat = unreal.load_object(None, "/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")
        PT = unreal.Vector(-681.16, -743.98, -194.84 + 400.0)   # точка песка из exp4 (+400 над землёй)
        NEVER = unreal.PropertyAccessChangeNotifyMode.NEVER

        def place(mesh, mat, scale):
            comp.set_static_mesh(mesh); comp.set_material(0, mat)
            el.set_actor_scale3d(unreal.Vector(scale, scale, scale))
            el.set_actor_location(PT, False, True)
            R["runtime"].setdefault("exp5", []).append({"mesh": mesh.get_path_name(), "mat": mat.get_path_name(),
                                                        "num_mats": comp.get_num_materials()})
            log("Y mesh=%s mat=%s num_mats=%d" % (mesh.get_name(), mat.get_name(), comp.get_num_materials()))

        def add_slot(sm, mat):
            sl = list(sm.get_editor_property("static_materials"))
            if not sl:
                sm.set_editor_property("static_materials",
                                       [unreal.StaticMaterial(material_interface=mat, material_slot_name="Mat0")], NEVER)
                return True
            return False

        def cell(tag, pre):
            after(1, pre)
            shot(tag + "_on")
            after(1, lambda: comp.set_cast_shadow(False)); shot(tag + "_off")
            after(1, lambda: comp.set_cast_shadow(True))
        after(1, lambda: solo(sun_lbl))
        after(1, lambda: sun_a.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-89.0, yaw=0.0), False))
        after(1, lambda: el.set_actor_location(loc0, False, True))
        shot("Y0_on"); shot("Y0_on2")
        cell("Y4_own_mic", lambda: place(mesh0, mat0, 1.0))
        cell("Y6_own_def", lambda: place(mesh0, defmat, 1.0))
        cell("Y7_cube_mic", lambda: place(cube, mat0, 2.0))

        def fix_one():
            add_slot(mesh0, mat0)
            place(mesh0, mat0, 1.0)
            comp.set_cast_shadow(False); comp.set_cast_shadow(True)   # пересоздать прокси
        cell("Y8_own_mic_slot", fix_one)

        def home():
            el.set_actor_scale3d(unreal.Vector(1, 1, 1)); el.set_actor_location(loc0, False, True)
            sun_a.set_actor_rotation(rot0, False); solo(None)
        after(1, home)
        shot("Z0_before_on"); shot("Z0_before_on2")

        def fix_all():
            n = 0
            for c in st["elems"]:
                sm = c.static_mesh
                if sm and add_slot(sm, c.get_material(0)):
                    n += 1
            for c in st["elems"]:
                c.set_cast_shadow(False); c.set_cast_shadow(True)
            R["runtime"]["exp5_slots_added"] = n
            log("слот добавлен в памяти у %d мешей" % n)
        after(1, fix_all)
        shot("Z1_fixed_on"); shot("Z1_fixed_on2")
        after(1, lambda: set_elem_shadow(False)); shot("Z1_fixed_off")
        after(1, lambda: set_elem_shadow(True))
        after(5, finish)

    # ★ Режим exp4: ДОКАЗАННЫЙ ПРИЁМНИК. Точка освещённого песка (экран 1300,650) → мир (трасса/плоскость),
    #   солнце строго вниз. Над точкой по очереди: Y1 шкатулка (позитивный контроль — её тень обязана лечь),
    #   Y2 куб на компоненте bg_102, Y3 тот же куб ОТЦЕПЛЕННЫЙ от LoginBackdrop_Anchor, Y4 родной SM_m102+MIC.
    def phase_exp4():
        collect()
        w = st["w"]
        sun_lbl = next((l for l, (c, i) in st["lights"].items() if c.get_class().get_name() == "DirectionalLightComponent"), None)
        sun_a = st["lights"][sun_lbl][0].get_owner()
        rot0 = sun_a.get_actor_rotation()
        el = next(a for a in unreal.GameplayStatics.get_all_actors_with_tag(w, "AsdaBackdropElem")
                  if a.get_actor_label() == "BackdropElem_bg_102_e00_00")
        comp = el.static_mesh_component
        loc0 = el.get_actor_location(); mesh0 = comp.static_mesh; mat0 = comp.get_material(0)
        cas = st["casket"][0].get_owner(); cas_loc0 = cas.get_actor_location()
        cube = unreal.load_object(None, "/Engine/BasicShapes/Cube.Cube")
        defmat = unreal.load_object(None, "/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")
        P = {"pt": None}

        def find_point():
            pc = unreal.GameplayStatics.get_player_controller(w, 0)
            res = unreal.GameplayStatics.deproject_screen_to_world(pc, unreal.Vector2D(1300.0, 650.0))
            vals = [v for v in (res if isinstance(res, (tuple, list)) else [res]) if isinstance(v, unreal.Vector)]
            o, d = vals[0], vals[1]
            info = {"ray_o": [o.x, o.y, o.z], "ray_d": [d.x, d.y, d.z]}
            hitp = None
            try:
                hit = unreal.SystemLibrary.line_trace_single(w, o, o + d * 20000.0, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
                                                             True, [], unreal.DrawDebugTrace.NONE, True)
                t = hit.to_tuple() if hit else None
                if t and t[0]:
                    hitp = t[5]
                    info["hit_actor"] = t[9].get_actor_label() if t[9] else None
            except Exception as ex:
                info["trace_err"] = str(ex)
            if hitp is None:
                co, ce = cas.get_actor_bounds(False)
                gz = co.z - ce.z
                k = (gz - o.z) / d.z
                hitp = o + d * k
                info["plane_z"] = gz
            P["pt"] = hitp
            info["point"] = [hitp.x, hitp.y, hitp.z]
            R["runtime"]["exp4_point"] = info
            log("ТОЧКА: %s" % json.dumps(info, ensure_ascii=False, default=str))

        def above(h):
            p = P["pt"]; return unreal.Vector(p.x, p.y, p.z + h)

        def place_el(mesh, mat, scale, detach=False):
            if detach:
                el.detach_from_actor(location_rule=unreal.DetachmentRule.KEEP_WORLD,
                                     rotation_rule=unreal.DetachmentRule.KEEP_WORLD, scale_rule=unreal.DetachmentRule.KEEP_WORLD)
            comp.set_static_mesh(mesh); comp.set_material(0, mat)
            el.set_actor_scale3d(unreal.Vector(scale, scale, scale))
            el.set_actor_location(above(400.0), False, True)
            par = el.get_attach_parent_actor()
            log("Y el mesh=%s mat=%s parent=%s at %s" % (mesh.get_name(), mat.get_name(), par.get_actor_label() if par else None, el.get_actor_location()))

        def home_el():
            el.set_actor_location(loc0, False, True)
        after(1, lambda: solo(sun_lbl))
        after(1, lambda: sun_a.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-89.0, yaw=0.0), False))
        after(3, find_point)
        shot("Y0_on"); shot("Y0_on2")
        after(1, lambda: cas.set_actor_location(above(400.0), False, True)); shot("Y1_casket_on")
        after(1, lambda: set_casket_shadow(False)); shot("Y1_casket_off")
        after(1, lambda: set_casket_shadow(True)); after(1, lambda: cas.set_actor_location(cas_loc0, False, True))
        after(1, lambda: place_el(cube, defmat, 2.0)); shot("Y2_cube_on")
        after(1, lambda: comp.set_cast_shadow(False)); shot("Y2_cube_off"); after(1, lambda: comp.set_cast_shadow(True))
        after(1, lambda: place_el(mesh0, mat0, 1.0)); shot("Y4_own_on")
        after(1, lambda: comp.set_cast_shadow(False)); shot("Y4_own_off"); after(1, lambda: comp.set_cast_shadow(True))
        after(1, lambda: place_el(cube, defmat, 2.0, detach=True)); shot("Y3_cube_detached_on")
        after(1, lambda: comp.set_cast_shadow(False)); shot("Y3_cube_detached_off"); after(1, lambda: comp.set_cast_shadow(True))
        after(1, lambda: place_el(mesh0, mat0, 1.0)); shot("Y5_own_detached_on")
        after(1, lambda: comp.set_cast_shadow(False)); shot("Y5_own_detached_off"); after(1, lambda: comp.set_cast_shadow(True))
        after(1, home_el)
        after(1, lambda: sun_a.set_actor_rotation(rot0, False)); after(1, lambda: solo(None))
        after(5, finish)

    # ★ Режим exp3: разделить МЕШ и КОМПОНЕНТ/МАТЕРИАЛ. Один элемент (bg_102, тот же компонент, те же флаги и тег)
    #   переносится над крышкой шкатулки, солнце строго вниз (только оно) — тень обязана лечь на крышку.
    #   X1 свой меш+MIC, X2 Cube+DefaultMaterial, X3 Cube+MIC, X4 SM_m102+DefaultMaterial.
    def phase_exp3():
        collect()
        sun_lbl = next((l for l, (c, i) in st["lights"].items() if c.get_class().get_name() == "DirectionalLightComponent"), None)
        sun_a = st["lights"][sun_lbl][0].get_owner()
        rot0 = sun_a.get_actor_rotation()
        el = next(a for a in unreal.GameplayStatics.get_all_actors_with_tag(st["w"], "AsdaBackdropElem")
                  if a.get_actor_label() == "BackdropElem_bg_102_e00_00")
        comp = el.static_mesh_component
        loc0 = el.get_actor_location()
        mesh0 = comp.static_mesh; mat0 = comp.get_material(0)
        cube = unreal.load_object(None, "/Engine/BasicShapes/Cube.Cube")
        defmat = unreal.load_object(None, "/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")
        after(1, lambda: solo(sun_lbl))
        after(1, lambda: sun_a.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-89.0, yaw=0.0), False))

        def cfg(mesh, mat, scale):
            comp.set_static_mesh(mesh); comp.set_material(0, mat)
            el.set_actor_scale3d(unreal.Vector(scale, scale, scale))
            el.set_actor_location(unreal.Vector(-50.0, 245.0, 900.0), False, True)
            org, ext = el.get_actor_bounds(False)
            R["runtime"].setdefault("exp3_cfg", []).append({"mesh": mesh.get_path_name(), "mat": mat.get_path_name(),
                                                            "bounds_o": [org.x, org.y, org.z], "bounds_e": [ext.x, ext.y, ext.z]})
            log("X cfg mesh=%s mat=%s bounds o=%s e=%s" % (mesh.get_name(), mat.get_name(), org, ext))

        def me_shadow(on):
            comp.set_cast_shadow(bool(on)); log("bg_102 cast_shadow=%s" % bool(on))

        def cell(tag, mesh, mat, scale):
            after(1, lambda: cfg(mesh, mat, scale))
            shot(tag + "_on"); shot(tag + "_on2")
            after(1, lambda: me_shadow(False)); shot(tag + "_off")
            after(1, lambda: me_shadow(True))
        cell("X1_own", mesh0, mat0, 1.0)
        cell("X2_cube_def", cube, defmat, 3.0)
        cell("X3_cube_mic", cube, mat0, 3.0)
        cell("X4_own_def", mesh0, defmat, 1.0)

        def restore():
            comp.set_static_mesh(mesh0); comp.set_material(0, mat0)
            el.set_actor_scale3d(unreal.Vector(1, 1, 1)); el.set_actor_location(loc0, False, True)
            sun_a.set_actor_rotation(rot0, False); solo(None)
        after(1, restore)
        after(5, finish)

    # ★ Режим exp2: матрица причин при ОДНОМ солнце. Каждая ячейка — пара «тени элементов вкл / выкл»
    #   (+ повтор «вкл» для шума). Позитивный контроль — тень шкатулки в тех же условиях.
    def phase_exp2():
        collect()
        dump_runtime("start")
        sun_lbl = next((l for l, (c, i) in st["lights"].items() if c.get_class().get_name() == "DirectionalLightComponent"), None)
        sun_c = st["lights"][sun_lbl][0]
        sun_a = sun_c.get_owner()
        rot0 = sun_a.get_actor_rotation()
        after(1, lambda: solo(sun_lbl))

        def sun_down(on):
            sun_a.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-89.0, yaw=0.0) if on else rot0, False)
            log("солнце: %s" % sun_a.get_actor_rotation())

        def two_sided(on):
            for c in st["elems"]:
                c.set_editor_property("cast_shadow_as_two_sided", bool(on))
                c.set_cast_shadow(False); c.set_cast_shadow(True)
            log("элементы cast_shadow_as_two_sided = %s" % bool(on))

        def cell(tag):
            shot(tag + "_on"); shot(tag + "_on2")
            after(1, lambda: set_elem_shadow(False)); shot(tag + "_off")
            after(1, lambda: set_elem_shadow(True))
        # T1 солнце вниз
        after(1, lambda: sun_down(True)); cell("T1_down")
        after(1, lambda: set_casket_shadow(False)); shot("T1_down_casketoff"); after(1, lambda: set_casket_shadow(True))
        # T2 солнце вниз + тень двусторонняя
        after(1, lambda: two_sided(True)); cell("T2_down_2s")
        # T5 авторское солнце + тень двусторонняя
        after(1, lambda: sun_down(False)); cell("T5_orig_2s")
        after(1, lambda: two_sided(False))
        # T3 солнце вниз + VSM
        after(1, lambda: sun_down(True)); after(1, lambda: cc("r.Shadow.Virtual.Enable 1")); cell("T3_down_vsm")
        after(1, lambda: cc("r.Shadow.Virtual.Enable 0"))
        # T4 солнце вниз + без отсечения мелких теневых кастеров
        after(1, lambda: cc("r.Shadow.RadiusThreshold 0")); cell("T4_down_rad0")
        after(1, lambda: cc("r.Shadow.RadiusThreshold 0.03"))
        after(1, lambda: sun_down(False)); after(1, lambda: solo(None))
        after(1, lambda: dump_runtime("end"))
        after(5, finish)

    # ★ Режим exp (ASDA_BDS_MODE=exp): что именно гасит тень — материал или меш/компонент.
    #   Всё при ОДНОМ солнце (остальные источники в 0) — сигнал тени чище.
    #   M_*: всем элементам материал слота 0 → /Engine/EngineMaterials/DefaultMaterial (непрозрачный).
    #   K_*: заспавненный в рантайме куб (Movable) с DefaultMaterial и с MIC элемента.
    def phase_exp():
        collect()
        dump_runtime("start")
        sun_lbl = next((l for l, (c, i) in st["lights"].items() if c.get_class().get_name() == "DirectionalLightComponent"), None)
        after(1, lambda: solo(sun_lbl))
        shot("P_on"); shot("P_on2")
        after(1, lambda: set_elem_shadow(False)); shot("P_off")
        after(1, lambda: set_elem_shadow(True))
        defmat = unreal.load_object(None, "/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")
        orig = [(c, c.get_material(0)) for c in st["elems"]]
        R["runtime"]["elem_mat0_sample"] = [str(m.get_path_name()) if m else None for (_, m) in orig[:3]]

        def swap(on):
            for c, m in orig:
                c.set_material(0, defmat if on else m)
            log("материал элементов: %s" % ("DefaultMaterial" if on else "исходный"))
        after(1, lambda: swap(True)); shot("M_on"); shot("M_on2")
        after(1, lambda: set_elem_shadow(False)); shot("M_off")
        after(1, lambda: set_elem_shadow(True)); after(1, lambda: swap(False))
        cube = {"a": None}

        def spawn():
            w = st["w"]
            xf = unreal.Transform(location=unreal.Vector(-77.0, -415.0, 250.0), rotation=unreal.Rotator(0, 0, 0),
                                  scale=unreal.Vector(1.5, 1.5, 1.5))
            a = unreal.GameplayStatics.begin_deferred_actor_spawn_from_class(w, unreal.StaticMeshActor, xf)
            a = unreal.GameplayStatics.finish_spawning_actor(a, xf)
            c = a.static_mesh_component
            c.set_mobility(unreal.ComponentMobility.MOVABLE)
            c.set_static_mesh(unreal.load_object(None, "/Engine/BasicShapes/Cube.Cube"))
            c.set_material(0, defmat)
            cube["a"] = a
            log("куб заспавнен: %s" % a.get_actor_location())

        def cube_cfg(mat=None, shadow=True, hidden=False):
            c = cube["a"].static_mesh_component
            if mat is not None:
                c.set_material(0, mat)
            c.set_cast_shadow(bool(shadow))
            cube["a"].set_actor_hidden_in_game(bool(hidden))
            log("куб: mat=%s shadow=%s hidden=%s" % (c.get_material(0).get_path_name(), shadow, hidden))
        after(1, spawn)
        shot("K_def_on"); shot("K_def_on2")
        after(1, lambda: cube_cfg(shadow=False)); shot("K_def_off")
        mic = orig[0][1]
        after(1, lambda: cube_cfg(mat=mic, shadow=True)); shot("K_mic_on")
        after(1, lambda: cube_cfg(shadow=False)); shot("K_mic_off")
        after(1, lambda: cube_cfg(shadow=False, hidden=True)); shot("K_none")
        after(1, lambda: solo(None))
        after(1, lambda: dump_runtime("end"))
        after(5, finish)

    def phase_shots():
        collect()
        dump_runtime("start")
        shot("A_base"); shot("A_base2")
        after(1, lambda: set_elem_shadow(False)); shot("B_elem_off"); shot("B_elem_off2")
        after(1, lambda: set_elem_shadow(True)); shot("C_elem_on")
        after(1, lambda: set_casket_shadow(False)); shot("D_casket_off")
        after(1, lambda: set_casket_shadow(True))
        for lbl in list(st["lights"].keys()):
            if st["lights"][lbl][0].get_class().get_name() == "SkyLightComponent":
                continue
            safe = "".join(ch if ch.isalnum() else "_" for ch in lbl)
            after(1, lambda l=lbl: solo(l))
            shot("S_%s_on" % safe)
            after(1, lambda: set_elem_shadow(False)); shot("S_%s_off" % safe)
            after(1, lambda: set_elem_shadow(True)); shot("S_%s_on2" % safe)
        after(1, lambda: solo(None))
        after(1, lambda: cc("ShowFlag.ShadowFrustums 1")); shot("V_frustums")
        after(1, lambda: cc("ShowFlag.ShadowFrustums 0"))
        after(1, lambda: dump_runtime("end"))
        after(5, finish)

    def tick(_dt):
        st["n"] += 1
        if st["n"] == 1:
            phase_init(); return
        if st["w"] is None:
            return
        try:
            run_queue()
        except Exception as ex:
            import traceback
            unreal.log_error("[bd-shadow] ошибка шага: %s\n%s" % (ex, traceback.format_exc()))

    unreal.register_slate_post_tick_callback(tick)
    log("старт, tag=%s" % TAG)


# ★ ЗАКОН [[tools-no-import-side-effects]]
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
