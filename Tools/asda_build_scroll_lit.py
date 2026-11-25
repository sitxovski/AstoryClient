# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_scroll_lit.py — Lit-мастер для UV-скроллящихся объектов карт + репарент их MIC.
#   Правка Д-7 аудита света 2026-09-03 (Reports/audit_2026-09-02/fix_scroll_objects_lit.md).
#
# ЧТО БЫЛО: 19 MIC `Content/Map/<map>/ScrollMat/MI_scroll_*` (кольца порталов, магические круги,
#   механизм silalis, бегущие плитки/поток flammio/inferion) сидели на UNLIT-мастере
#   /Game/Materials/M_AsdaObjectScroll (Emissive = Tex·Ambient) среди Lit-мира ⇒ горели полной
#   яркостью ночью и в тени.
# ЭТАЛОН (system/EmptyProject.fx, техника tNoSkin = ВСЕ объекты карты, скроллящиеся в том числе):
#   VShadeNoSkin:  o.Diffuse = MaterialAmbient + max(0, N·L)·MaterialDiffuse   (:591)   ← освещение
#                  o.Tex0    = i.Tex0 + NoSkinTexScroll.xy                       (:620)   ← скролл UV
#   PShadeNoSkin:  RGB = Tex·Diffuse (+туман, +кубмап·Tex.w), A = Tex.w·fog·2·(NoSkinTexScroll.w·0.5)
#   Скролл и свет — ОДНА техника, скроллящийся объект освещён как любой другой ⇒ unlit = дефект порта.
#   NoSkinTexScroll.xy = pVtxConst93[0..1] узла (накопленное смещение UV), .z = 1/3 (подсветка цели),
#   .w = pTexScroll[0] (глобальный фейд). Скорость — 4 float с записи узла +0x260 (в .mapraw —
#   uvScroll инстанса; в .jbm-подмеше это же поле «%s %f %f %f %f» /1000 → +624).
#
# ЧТО ДЕЛАЕМ: новый мастер /Game/Materials/M_AsdaObjectScrollLit =
#   граф M_AsdaObjectLit (DefaultLit; BaseColor=Tex.RGB; Roughness/Metallic — параметры; Emissive =
#   Tex.RGB·AmbientFloor(0.0 — рычаг, как у всех Lit-мастеров после Д-3); OpacityMask = Tex.A·FadeIn;
#   БЕЗ VertexColor — у M_AsdaObjectLit он всё равно не подключён, см. ue-delete-all-material-expressions-bug)
#   + узел скролла 1:1 из unlit-мастера: Panner(Coordinate=TexCoord, Speed = float2(PerInstanceCustomData0,
#   PerInstanceCustomData1)·SpeedScale) → Tex.UVs. Имена параметров Tex / SpeedScale СОХРАНЕНЫ —
#   MIC-оверрайды переживают репарент (asda_add_uvscroll.py пишет скорость в custom-data инстанса, а
#   SpeedScale=300 и Tex — в MIC). Параметр `Ambient` unlit-мастера (множитель Emissive) в Lit смысла
#   не имеет и в новый граф НЕ переносится — его оверрайд на MIC становится мёртвым (как LightColor/
#   LightDir у 2 500 MIC карт — Д-12), это ожидаемо и вписано в отчёт.
#   Бленд/клип НЕ «делаем Opaque потому что Lit»: blend_mode, two_sided и opacity_mask_clip_value
#   читаются С УНЛИТ-МАСТЕРА и копируются 1:1 (Masked, clip 0.3333, two-sided); usage-флаги
#   (used_with_instanced_static_meshes / _skeletal_mesh / _static_lighting) читаются с M_AsdaObjectLit
#   (память lighting-per-map: без ISM-флага HISM-объекты в -game чёрные).
#   Затем все MIC из Content/Map/*/ScrollMat/ с parent == M_AsdaObjectScroll репарентятся на новый
#   мастер; ДО/ПОСЛЕ сверяются Tex и SpeedScale (эффективные значения + список оверрайдов), при
#   расхождении — RuntimeError и MIC не сохраняется. Старый M_AsdaObjectScroll НЕ удаляется.
#
# КОНТРОЛЬ (asda_mat_graph): при повторном прогоне граф существующего M_AsdaObjectScrollLit чистится
#   ЦИКЛОМ до нуля (баг UE 5.8 DeleteAllMaterialExpressions), каждый connect_* проверяется на False,
#   после сборки check_graph(expected=EXPECTED_NODES) + дубли имён, значения печатаются ИЗ АССЕТА
#   (кэш параметров/свойства) после сохранения. Бэкапы 19 MIC + старого мастера — в
#   AstoryClient/_backup/scroll_lit_2026-09-03/ (делает вызывающий, md5 в MD5SUMS.txt).
#
# Запуск (СКРЫТО, редактор закрыт):
#   python Tools/run_headless_safe.py Tools/asda_build_scroll_lit.py --marker SCROLL-LIT-FINISHED
#   ASDA_SCROLL_VERIFY_ONLY=1 — только чтение: дамп нового мастера + parent/параметры каждого MIC
#   (ничего не сохраняется) — второй, независимый от прогона правки, метод проверки.
import os, sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_mat_graph as MG

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()

PKG = "/Game/Materials"
OLD_MASTER = PKG + "/M_AsdaObjectScroll"        # unlit-мастер скролла (НЕ удаляется, 0 инстансов после)
LIT_MASTER = PKG + "/M_AsdaObjectLit"           # образец Lit-графа и usage-флагов (только чтение)
NEW_NAME = "M_AsdaObjectScrollLit"
NEW_MASTER = PKG + "/" + NEW_NAME
MIC_ROOT = "/Game/Map"                          # MIC лежат в /Game/Map/<map>/ScrollMat/
MIC_DIR_TAG = "/ScrollMat/"
# Узлы нового графа: TexCoord, PICD0, PICD1, AppendVector, SpeedScale, Multiply(speed), Panner, Tex,
#   Roughness, Metallic, AmbientFloor, Multiply(emissive), FadeIn, Multiply(mask) = 14.
EXPECTED_NODES = 14
# Свойства, копируемые с unlit-мастера 1:1 (бленд/клип/двусторонность — не «Opaque потому что Lit»).
BLEND_PROPS = ["blend_mode", "two_sided", "dither_opacity_mask"]
# ★ 03.09.2026 (ревизия `review_scroll_objects_lit.md`): порог отсечения НЕ копируем с унлит-мастера —
#   там жило унаследованное 0.3333, которого в эталоне нет. Проход объектов клиента
#   (`AsdaStory_game.c:18456-18461`) ставит `D3DRS_ALPHAREF = 8` при `ALPHAFUNC = GREATEREQUAL`,
#   то есть порог 8/255. Завышенный в 10 раз клип выкусывал мягкий полупрозрачный край порталов
#   и магических кругов. (Оригинал вдобавок СМЕШИВАЕТ по SrcAlpha/InvSrcAlpha — полная точность
#   потребовала бы Translucent; решение по этому за владельцем, см. §3 ревизии.)
ALPHA_REF_CLIP = 8.0 / 255.0   # = 0.031373
# Usage-флаги — как у M_AsdaObjectLit.
USAGE_PROPS = ["used_with_instanced_static_meshes", "used_with_skeletal_mesh", "used_with_static_lighting"]
# Параметры, которые ОБЯЗАНЫ совпасть у каждого MIC ДО/ПОСЛЕ репарента.
KEEP_SCALARS = ["SpeedScale"]
KEEP_TEXTURES = ["Tex"]
VERIFY_ONLY = os.environ.get("ASDA_SCROLL_VERIFY_ONLY", "") not in ("", "0")


def log(m):
    unreal.log_warning("[scroll-lit] %s" % m)


def C(a, ao, b, bi):
    """connect_material_expressions с ПРОВЕРКОЙ возврата (при неверном имени входа UE молча даёт False)."""
    if not MEL.connect_material_expressions(a, ao, b, bi):
        raise RuntimeError("connect %s.%s -> %s.%s вернул False" % (a.get_name(), ao, b.get_name(), bi))


def P(e, eo, prop):
    """connect_material_property с проверкой возврата."""
    if not MEL.connect_material_property(e, eo, prop):
        raise RuntimeError("connect_property %s.%s -> %s вернул False" % (e.get_name(), eo, prop))


# ------------------------------------------------------------------ дамп (печать ИЗ АССЕТА)
OUTS = [("BaseColor", MP.MP_BASE_COLOR), ("Metallic", MP.MP_METALLIC), ("Specular", MP.MP_SPECULAR),
        ("Roughness", MP.MP_ROUGHNESS), ("Emissive", MP.MP_EMISSIVE_COLOR), ("Opacity", MP.MP_OPACITY),
        ("OpacityMask", MP.MP_OPACITY_MASK), ("Normal", MP.MP_NORMAL), ("AO", MP.MP_AMBIENT_OCCLUSION)]
PROPS = ["material_domain", "blend_mode", "shading_model", "two_sided", "opacity_mask_clip_value",
         "dither_opacity_mask", "used_with_skeletal_mesh", "used_with_instanced_static_meshes",
         "used_with_static_lighting"]


def node_label(e, idx_of):
    cls = e.get_class().get_name().replace("MaterialExpression", "")
    extra = ""
    try:
        pn = str(e.get_editor_property("parameter_name"))
        if pn and pn != "None":
            extra += "(%s)" % pn
    except Exception:
        pass
    try:
        if cls == "ScalarParameter":
            extra += "=%.4g" % e.get_editor_property("default_value")
        elif cls == "PerInstanceCustomData":
            extra += "[idx=%d]" % e.get_editor_property("data_index")
        elif cls == "Panner":
            extra += "[sx=%.4g sy=%.4g]" % (e.get_editor_property("speed_x"), e.get_editor_property("speed_y"))
        elif cls == "TextureSampleParameter2D":
            t = e.get_editor_property("texture")
            extra += "[%s]" % (t.get_name() if t else "None")
    except Exception:
        pass
    return "#%d %s%s" % (idx_of[e], cls, extra)


def dump_master(m, tag):
    """Печать графа/свойств/кэша параметров мастера — ровно то, что лежит в ассете."""
    log("===== %s %s" % (tag, m.get_path_name()))
    for k in PROPS:
        try:
            log("  prop %s = %s" % (k, m.get_editor_property(k)))
        except Exception as ex:
            log("  prop %s = ?(%s)" % (k, ex))
    exprs = list(MEL.get_material_expressions(m))
    idx_of = {e: i for i, e in enumerate(exprs)}
    log("  nodes: %d, дубли параметров: %s" % (len(exprs), MG.parameter_duplicates(m) or "нет"))
    for e in exprs:
        try:
            names = list(MEL.get_material_expression_input_names(e))
            srcs = list(MEL.get_inputs_for_material_expression(m, e))
        except Exception:
            names, srcs = [], []
        ins = []
        for i, nm in enumerate(names):
            s = srcs[i] if i < len(srcs) else None
            ins.append("%s<-%s" % (nm or ("in%d" % i), ("#%d" % idx_of[s]) if (s is not None and s in idx_of) else "EMPTY"))
        log("  %s   inputs[%s]" % (node_label(e, idx_of), ", ".join(ins)))
    for nm, prop in OUTS:
        try:
            n = MEL.get_material_property_input_node(m, prop)
            on = MEL.get_material_property_input_node_output_name(m, prop) if n else ""
            log("  OUT %-11s <- %s%s" % (nm, node_label(n, idx_of) if n else "EMPTY", (" out=%s" % on) if on else ""))
        except Exception as ex:
            log("  OUT %s ?(%s)" % (nm, ex))
    sc = [(str(n), MEL.get_material_default_scalar_parameter_value(m, n)) for n in MEL.get_scalar_parameter_names(m)]
    log("  кэш скаляров: %s" % sc)
    log("  кэш текстур: %s ; векторов: %s" % ([str(n) for n in MEL.get_texture_parameter_names(m)],
                                             [str(n) for n in MEL.get_vector_parameter_names(m)]))


# ------------------------------------------------------------------ сборка мастера
scal = MG.scalar_param   # ScalarParameter nm=v в (x, y) графа m (общая реализация asda_mat_graph; дедуп №2 Т19)


def wire(m, old, lit):
    """Граф M_AsdaObjectScrollLit. `old` — unlit M_AsdaObjectScroll (бленд/клип, дефолты Tex/SpeedScale),
    `lit` — M_AsdaObjectLit (usage-флаги, дефолты Roughness/Metallic/AmbientFloor/FadeIn)."""
    E = lambda cls, x, y: MEL.create_material_expression(m, cls, x, y)
    # --- свойства: бленд 1:1 с unlit-мастером, шейдинг Lit, usage как у ObjectLit
    for k in BLEND_PROPS:
        m.set_editor_property(k, old.get_editor_property(k))
    # ★ Порог отсечения — ЭТАЛОННЫЙ (8/255), а не унаследованные 0.3333 с унлит-мастера (см. ALPHA_REF_CLIP).
    m.set_editor_property("opacity_mask_clip_value", ALPHA_REF_CLIP)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    for k in USAGE_PROPS:
        m.set_editor_property(k, lit.get_editor_property(k))
    # --- дефолты параметров из кэшей образцов (не константы скрипта)
    def dflt(src, name):
        if name not in [str(n) for n in MEL.get_scalar_parameter_names(src)]:
            raise RuntimeError("у %s нет скаляра %s — образец не тот" % (src.get_name(), name))
        return MEL.get_material_default_scalar_parameter_value(src, name)
    d_speed = dflt(old, "SpeedScale")
    d_rough, d_metal, d_floor, d_fade = (dflt(lit, "Roughness"), dflt(lit, "Metallic"),
                                         dflt(lit, "AmbientFloor"), dflt(lit, "FadeIn"))
    old_tex = None
    for e in MEL.get_material_expressions(old):
        if isinstance(e, unreal.MaterialExpressionTextureSampleParameter2D) and str(e.get_editor_property("parameter_name")) == "Tex":
            old_tex = e.get_editor_property("texture")
    # --- скролл UV (1:1 с unlit-мастером): Speed = float2(cd0, cd1)·SpeedScale
    uv = E(unreal.MaterialExpressionTextureCoordinate, -1500, -100)
    cd0 = E(unreal.MaterialExpressionPerInstanceCustomData, -1500, 100); cd0.set_editor_property("data_index", 0)
    cd1 = E(unreal.MaterialExpressionPerInstanceCustomData, -1500, 220); cd1.set_editor_property("data_index", 1)
    ap = E(unreal.MaterialExpressionAppendVector, -1300, 150); C(cd0, "", ap, "A"); C(cd1, "", ap, "B")
    spd = scal(m, "SpeedScale", d_speed, -1300, 300)
    mul = E(unreal.MaterialExpressionMultiply, -1100, 200); C(ap, "", mul, "A"); C(spd, "", mul, "B")
    pan = E(unreal.MaterialExpressionPanner, -900, -50); C(uv, "", pan, "Coordinate"); C(mul, "", pan, "Speed")
    # --- Lit-часть = M_AsdaObjectLit
    tex = E(unreal.MaterialExpressionTextureSampleParameter2D, -700, -100)
    tex.set_editor_property("parameter_name", "Tex")
    if old_tex:
        tex.set_editor_property("texture", old_tex)
    C(pan, "", tex, "UVs")
    rough = scal(m, "Roughness", d_rough, -300, 200)
    metal = scal(m, "Metallic", d_metal, -300, 320)
    floor = scal(m, "AmbientFloor", d_floor, 550, 400)
    em = E(unreal.MaterialExpressionMultiply, 700, 300); C(tex, "RGB", em, "A"); C(floor, "", em, "B")
    fade = scal(m, "FadeIn", d_fade, -300, 450)
    msk = E(unreal.MaterialExpressionMultiply, -100, 450); C(tex, "A", msk, "A"); C(fade, "", msk, "B")
    P(tex, "RGB", MP.MP_BASE_COLOR)
    P(rough, "", MP.MP_ROUGHNESS)
    P(metal, "", MP.MP_METALLIC)
    P(em, "", MP.MP_EMISSIVE_COLOR)
    P(msk, "", MP.MP_OPACITY_MASK)
    MEL.recompile_material(m)


def build_master():
    old = EAL.load_asset(OLD_MASTER)
    lit = EAL.load_asset(LIT_MASTER)
    if not old or not lit:
        raise RuntimeError("нет образца: old=%s lit=%s" % (old, lit))
    dump_master(old, "ДО unlit-мастер")
    dump_master(lit, "ОБРАЗЕЦ Lit")
    if EAL.does_asset_exist(NEW_MASTER):
        m = EAL.load_asset(NEW_MASTER)
        dump_master(m, "ДО существующий новый мастер")
        MG.clear_expressions(m, log=log, tag="[scroll-lit]")   # цикл до нуля — баг DeleteAllMaterialExpressions
    else:
        m = ATOOLS.create_asset(NEW_NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
        if not m:
            raise RuntimeError("create_asset %s не удался" % NEW_MASTER)
    wire(m, old, lit)
    MG.check_graph(m, expected=EXPECTED_NODES, log=log, tag="[scroll-lit]")
    m.modify()
    if not EAL.save_loaded_asset(m, False):
        raise RuntimeError("save_loaded_asset %s вернул False" % NEW_MASTER)
    dump_master(m, "ПОСЛЕ новый мастер (сохранён)")
    # контроль из ассета: шейдинг Lit, бленд как у unlit, usage как у ObjectLit, параметры на месте
    if m.get_editor_property("shading_model") != unreal.MaterialShadingModel.MSM_DEFAULT_LIT:
        raise RuntimeError("shading_model не DefaultLit")
    for k in BLEND_PROPS:
        if m.get_editor_property(k) != old.get_editor_property(k):
            raise RuntimeError("свойство %s ≠ unlit-мастеру" % k)
    for k in USAGE_PROPS:
        if m.get_editor_property(k) != lit.get_editor_property(k):
            raise RuntimeError("usage %s ≠ M_AsdaObjectLit" % k)
    names = [str(n) for n in MEL.get_scalar_parameter_names(m)]
    for need in ("SpeedScale", "Roughness", "Metallic", "AmbientFloor", "FadeIn"):
        if need not in names:
            raise RuntimeError("в кэше нового мастера нет скаляра %s" % need)
    if "Tex" not in [str(n) for n in MEL.get_texture_parameter_names(m)]:
        raise RuntimeError("в кэше нового мастера нет текстуры Tex")
    return m


# ------------------------------------------------------------------ MIC
def find_scroll_mics():
    out = []
    # ★ 16.09.2026 (раздел 22, Reports/dedup_audit/savedir_guard_2026-09-16.md): обход идёт по /Game/Map
    #   ЦЕЛИКОМ, а там же сцена логина (/Game/Map/Login — дерево, плюс Login_Shine, HolyLight_SpotLight,
    #   Shine_Dust). Найденным MIC ниже меняют parent и сохраняют (save_loaded_asset) — это запись в пакет.
    #   MIC скролла под Login сегодня нет, правка ПРЕДУПРЕДИТЕЛЬНАЯ: корень обхода общий со сценой логина.
    import os as _os_lg, sys as _sys_lg
    _sys_lg.path.insert(0, _os_lg.path.dirname(_os_lg.path.abspath(__file__)))
    from asda_login_guard import login_package_blocked as _lg_blocked
    for p in EAL.list_assets(MIC_ROOT, True, False):
        if MIC_DIR_TAG not in p:
            continue
        if _lg_blocked(p, "asda_build_scroll_lit.py"):   # ★ раздел 22: пакет сцены логина под /Game/Map
            continue
        a = EAL.load_asset(p)
        if isinstance(a, unreal.MaterialInstanceConstant):
            out.append(a)
    out.sort(key=lambda a: a.get_path_name())
    return out


def mic_snapshot(mic):
    par = mic.get_editor_property("parent")
    # ★ get_path_name() у объекта даёт ОБЪЕКТНЫЙ путь «/Game/Materials/X.X»; сравниваем с путём ПАКЕТА
    #   «/Game/Materials/X» (первый прогон 03.09 из-за этого пропустил все 19 MIC как «parent не тот»).
    par_pkg = par.get_path_name().split(".")[0] if par else None
    ov_s = {}
    for sp in (mic.get_editor_property("scalar_parameter_values") or []):
        ov_s[str(sp.get_editor_property("parameter_info").get_editor_property("name"))] = round(float(sp.get_editor_property("parameter_value")), 6)
    ov_t = {}
    for tp in (mic.get_editor_property("texture_parameter_values") or []):
        t = tp.get_editor_property("parameter_value")
        ov_t[str(tp.get_editor_property("parameter_info").get_editor_property("name"))] = t.get_path_name() if t else None
    eff_s = {n: round(float(MEL.get_material_instance_scalar_parameter_value(mic, n)), 6) for n in KEEP_SCALARS}
    eff_t = {}
    for n in KEEP_TEXTURES:
        t = MEL.get_material_instance_texture_parameter_value(mic, n)
        eff_t[n] = t.get_path_name() if t else None
    return {"parent": par_pkg, "ov_scalar": ov_s, "ov_texture": ov_t,
            "eff_scalar": eff_s, "eff_texture": eff_t}


def fmt(s):
    return "parent=%s | overrides scalar=%s texture=%s | эффективно %s %s" % (
        (s["parent"] or "None").split("/")[-1], s["ov_scalar"],
        {k: (v.split("/")[-1] if v else None) for k, v in s["ov_texture"].items()},
        s["eff_scalar"], {k: (v.split("/")[-1] if v else None) for k, v in s["eff_texture"].items()})


def reparent_all(new):
    mics = find_scroll_mics()
    log("MIC в %s*%s: %d" % (MIC_ROOT, MIC_DIR_TAG, len(mics)))
    done, skipped, still_old = 0, 0, 0
    for mic in mics:
        before = mic_snapshot(mic)
        log("ДО    %s: %s" % (mic.get_path_name(), fmt(before)))
        if before["parent"] == NEW_MASTER:
            skipped += 1
            log("      уже на новом мастере — пропуск")
            continue
        if before["parent"] != OLD_MASTER:
            skipped += 1
            log("      parent не %s — НЕ трогаю" % OLD_MASTER)
            continue
        MEL.set_material_instance_parent(mic, new)
        after = mic_snapshot(mic)
        if after["parent"] != NEW_MASTER:
            raise RuntimeError("%s: parent после репарента %s" % (mic.get_name(), after["parent"]))
        for k in KEEP_SCALARS:
            if abs(after["eff_scalar"][k] - before["eff_scalar"][k]) > 1e-6:
                raise RuntimeError("%s: %s ДО %r ≠ ПОСЛЕ %r — MIC НЕ сохранён" % (mic.get_name(), k, before["eff_scalar"][k], after["eff_scalar"][k]))
        for k in KEEP_TEXTURES:
            if after["eff_texture"][k] != before["eff_texture"][k]:
                raise RuntimeError("%s: %s ДО %r ≠ ПОСЛЕ %r — MIC НЕ сохранён" % (mic.get_name(), k, before["eff_texture"][k], after["eff_texture"][k]))
        mic.modify()
        if not EAL.save_loaded_asset(mic, False):
            raise RuntimeError("%s: save_loaded_asset вернул False" % mic.get_name())
        log("ПОСЛЕ %s: %s" % (mic.get_path_name(), fmt(after)))
        done += 1
    for mic in mics:
        if mic_snapshot(mic)["parent"] == OLD_MASTER:
            still_old += 1
    log("репарент: сделано %d, пропущено %d, осталось на старом мастере %d" % (done, skipped, still_old))
    if still_old:
        raise RuntimeError("остались MIC на старом мастере: %d" % still_old)
    return len(mics), done


def verify_only():
    m = EAL.load_asset(NEW_MASTER)
    if not m:
        raise RuntimeError("нет %s" % NEW_MASTER)
    dump_master(m, "ПРОВЕРКА новый мастер (с диска)")
    MG.check_graph(m, expected=EXPECTED_NODES, log=log, tag="[scroll-lit]")
    mics = find_scroll_mics()
    bad = 0
    for mic in mics:
        s = mic_snapshot(mic)
        ok = (s["parent"] == NEW_MASTER)
        bad += 0 if ok else 1
        log("%s %s: %s" % ("OK " if ok else "BAD", mic.get_path_name(), fmt(s)))
    log("ПРОВЕРКА: MIC %d, не на новом мастере %d" % (len(mics), bad))
    if bad:
        raise RuntimeError("проверка не прошла")
    return len(mics)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    if VERIFY_ONLY:
        n = verify_only()
        log("ЗАВЕРШЕНО (SCROLL-LIT-FINISHED): проверка, MIC=%d" % n)
        return
    new = build_master()
    total, done = reparent_all(new)
    log("ЗАВЕРШЕНО (SCROLL-LIT-FINISHED): мастер %s, MIC всего %d, репарент %d" % (NEW_MASTER, total, done))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
