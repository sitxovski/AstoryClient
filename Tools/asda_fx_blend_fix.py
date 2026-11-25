# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_blend_fix.py — РЕЖИМ СМЕШИВАНИЯ ЭФФЕКТОВ ПО ЭТАЛОНУ (10.10.2026, отчёт `Reports/owner_1010/fx_blend_fix.md`).
#   Продолжение `digging_hole_fx.py` (яма копания) на ВСЕ собранные BP_Fx_*: точечная правка ТОЛЬКО полей
#   Material / MaterialB эмиттеров. Прочие поля эмиттеров, настройки свечения владельца, общие мастера
#   M_AsdaEffect{Add,Alpha,Mod,ModAdd} и чужие MIC не меняются.
#
# ЭТАЛОН (сверено построчно):
#   * Бленд контейнера — заголовок .jeb +0x44/+0x48, лоадер «диск+1» = D3DBLEND (effects_particles.c:113266 switch),
#     ставится `objListRenderInRange@0x114E470` → `d3dSetBlendFactors` (render_core.c:44183).
#   * srcC = Tex.rgb × diffuse.rgb, srcA = Tex.a × diffuse.a (стадия 0: COLOROP/ALPHAOP = MODULATE, ARG1 = TEXTURE —
#     инициализация устройства render_core.c:40782/40850; ARG2 = CURRENT, `gfxRestoreRenderState2000` render_core.c:1907).
#   * Бэкбуфер без альфы (формат режима дисплея X8R8G8B8/R5G6B5, render_core.c:41172/41194) ⇒ по D3D9 Ad = 1:
#     DESTALPHA = 1, INVDESTALPHA = 0, SRCALPHASAT = min(As, 1−Ad) = 0.
#   * Тип 0x1F — `CEffectMesh_Render@0x123C0E0`: `rendApplyAlphaFogRenderState@0x125C490` (ALPHABLENDENABLE 0,
#     ZWRITE 1, CULL 2) и `CMesh_RenderNode(…, 2)@0x125C4C0` (model_anim.c:41215): в режиме 2 НЕТ SetMaterial
#     (vtbl+196 — только в режимах 0/1), COLORARG2 = TFACTOR (`d3dSetAlphaBlendArgs` render_core.c:40296) ⇒
#     пиксель = текстура × цвет узла, вершинное освещение не участвует (НЕОСВЕЩЁННЫЙ непрозрачный меш), бленд
#     контейнера к нему не применяется.
#
# ЧТО СТАВИТСЯ (эмиттеры BP, сопоставленные с .jeb по имени и типу):
#   * 0x1F → Material = MI_<текстура .jem>_opaque (M_AsdaEffectOpaque), MaterialB = нет.
#     ★ [ПОРТ] M_AsdaEffectOpaque — ОСВЕЩАЕМЫЙ (DefaultLit, BaseColor = Tex×VC, Specular 0, Roughness 1), а не
#     Unlit-эмиссия, как у эталона. Обоснование (замечание владельца 10.10 по кадрам ямы: тени не ложатся, яма
#     светлее грунта 135 против 114): у эталона И грунт, И яма выводились без тонмаппера — неосвещённая яма
#     совпадала с грунтом по яркости «как нарисовано». В порту грунт (M_AsdaTerrainLit) освещён светом карты,
#     тенями (в т. ч. тенями облаков — функция света солнца) и проходит тонмаппер, а Unlit-эмиссия — только
#     тонмаппер ⇒ яма светлее грунта и без теней. Освещаемый материал ставит яму в те же условия, что и землю
#     вокруг: тот же свет, те же тени, та же экспозиция. Свою тень меш не отбрасывает (как у эталона:
#     `Mesh->SetCastShadow(false)` в AsdaEffectActor.cpp).
#   * Контейнер dst = 3 (INVSRCCOLOR): Modulate-половина (dst×(1−srcC)) общая; аддитивная половина MaterialB — по src:
#       src 5 INVSRCALPHA → MI_<tex>_modadd     (M_AsdaEffectModAdd,  srcC×(1−srcA)) — у мешей с текстурой .jem, не default;
#       src 3 INVSRCCOLOR → MI_<tex>_modaddc    (M_AsdaEffectModAddC, srcC×(1−srcC));
#       src 4 SRCALPHA    → MI_<tex>_modadda    (M_AsdaEffectModAddA, srcC×srcA);
#       src 2 SRCCOLOR    → MI_<tex>_modaddsq   (M_AsdaEffectModAddSq, srcC×srcC);
#       src 9 INVDESTCOLOR→ MI_<tex>_modaddinv  (M_AsdaEffectModAddInv, srcC×(1−dstC); dstC — SceneColor) [ПОРТ: dst — сцена
#                           без полупрозрачных слоёв, в линейном HDR];
#       src 0 ZERO, 7 INVDESTALPHA → вклад источника 0 ⇒ MaterialB = нет (только затемнение);
#       src 10 SRCALPHASAT (6 `sb_*_oh_*`) — отступление владельца BLEND_CANON_FIX («add»), НЕ трогается.
#   * Буст свечения (вариант `_fxbNNN` старого MIC, волна Ф1) переносится на новый MIC того же варианта.
#
# ЗАЩИТА ПРАВОК: эмиттер меняется, только если его текущий материал — ровно то, что ставит сборщик
#   (`MI_<tex>_<режим>[_fxb…]`), или уже целевой. Иное — ручная/чужая правка: эмиттер пропускается, строка в отчёте.
#   Существующий MIC с чужим родителем/текстурой не перезаписывается (эмиттер пропускается).
#
# Режимы (переменная ASDA_FXBLEND_MODE): report (дефолт) — только план в JSON; apply — запись.
# Запуск: MSYS_NO_PATHCONV=1 python Tools/run_headless_safe.py Tools/asda_fx_blend_fix.py --marker "[fxblend] ГОТОВО"
#   (с --env нельзя: переменную ставить в окружение вызова).
# Сборщики: `asda_fx_common.finish_effect_bp` зовёт `retarget_for_build()` — пересборка канала повторяет правку
#   (откат — ASDA_FX_NO_ETALON_BLEND=1 при пересборке).
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

FX_DIR = "/Game/AsdaAssets/Effects"          # BP_Fx_<jeb>
MAT_DIR = FX_DIR + "/Mat"                    # MIC канала
TEX_DIR = FX_DIR + "/Tex"                    # текстуры канала
MASTER_DIR = "/Game/Effects"                 # мастера эффектов
REPORT_JSON = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/owner_1010/fx_blend_fix/plan_%s.json"   # план/итог прогона (%s = режим)

# суффикс MIC → мастер
MASTERS = {
    "opaque": "M_AsdaEffectOpaque",       # 0x1F: непрозрачный освещаемый
    "modadd": "M_AsdaEffectModAdd",       # src 5 (общий мастер, не меняется)
    "modaddc": "M_AsdaEffectModAddC",     # src 3 (мастер ямы копания, не меняется)
    "modadda": "M_AsdaEffectModAddA",     # src 4 (новый)
    "modaddsq": "M_AsdaEffectModAddSq",   # src 2 (новый)
    "modaddinv": "M_AsdaEffectModAddInv",  # src 9 (новый)
}
NEW_MASTERS = ("M_AsdaEffectModAddA", "M_AsdaEffectModAddSq", "M_AsdaEffectModAddInv")   # создаются этим модулем
# src диска (dst = 3) → суффикс аддитивной половины; None — вклад источника 0 (MaterialB снимается)
SRC_TO_ADD = {5: "modadd", 3: "modaddc", 4: "modadda", 2: "modaddsq", 9: "modaddinv", 0: None, 7: None}
# Узлов в графах (контроль asda_mat_graph.check_graph)
EXPECTED = {
    "M_AsdaEffectOpaque": 5,      # tex, vc, mul, spec, rough
    "M_AsdaEffectModAddA": 7,     # tex, vc, m1, ma, m2, boost, m3
    "M_AsdaEffectModAddSq": 6,    # tex, vc, m1, m2, boost, m3
    "M_AsdaEffectModAddInv": 10,  # tex, vc, m1, sc, mask, om, sat, m2, boost, m3
}
MESH_TYPES = (0x11, 0x12, 0x1F)              # меш-эмиттеры (.jem)
CANON_FIX_SKIP = re.compile(r"^s[bd]_md_eff_oh_")   # BLEND_CANON_FIX (asda_build_char_fx.py) — не трогать
FXB_RX = re.compile(r"^(MI_.+?)(_fxb\d{3}(?:s\d+)?)$")   # вариант буста волны Ф1


def log(m):
    """Журнал правки бленда."""
    import unreal
    unreal.log_warning("[fxblend] %s" % m)


def tex_stem(name):
    """Имя ассета текстуры как у `asda_place_effects.get_fx_material` (без расширения, нижний регистр, дефис сохраняется)."""
    return re.sub(r'[^A-Za-z0-9_-]', '_', os.path.splitext(name)[0]).lower() if name else "none"


def split_variant(mic_name):
    """('MI_<tex>_<режим>', '_fxbNNN…' или '') для имени MIC."""
    m = FXB_RX.match(mic_name or "")
    return (m.group(1), m.group(2)) if m else (mic_name or "", "")


def read_jeb(jeb):
    """Разобранный .jeb из каталога оригинала (как у канальных сборщиков) или None."""
    import asda_paths as P
    import asda_jeb_parse as J
    p = os.path.join(P.EFFECT_MODEL_ORIG_DIR, jeb.lower() + ".jeb")
    if not os.path.isfile(p):
        return None
    try:
        return J.parse_jeb(open(p, "rb").read())
    except Exception:
        return None


def mesh_tex(d):
    """Текстура меш-эмиттера — из .jem (как Material при сборке), иначе из записи эмиттера."""
    import asda_jem_parse as JEM
    meshes = JEM.load_jem(d.get("jem") or d["name"]) or []
    return meshes[0]["tex"] if meshes else d.get("tex", "")


def built_mode(jb):
    """Режим материала, который ставит сборщик для контейнера ("add"/"alpha"/"mod")."""
    import asda_fx_common as FXC
    return FXC.blend_mode(jb)


def needs_fix(jb):
    """Есть ли в эффекте, что править: 0x1F или контейнер dst = 3 (меш-пара / src ≠ 5)."""
    if any(d["type"] == 0x1F for d in jb["emitters"]):
        return True
    return jb.get("blendDst") == 3


# ── мастера ───────────────────────────────────────────────────────────────────────────────────
def _boost(MEL, mat, x, y):
    """Скаляр EmissiveBoost (дефолт 1.0) — как у мастеров волны Ф1: множитель готовой эмиссии."""
    import unreal
    b = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    b.set_editor_property("parameter_name", "EmissiveBoost")
    b.set_editor_property("default_value", 1.0)
    return b


def build_master(name):
    """Собрать мастер `name` (новые — создать; Opaque — пересобрать на месте в освещаемый). Вернуть ассет или None."""
    import unreal
    import asda_mat_graph as MG
    EAL = unreal.EditorAssetLibrary
    MEL = unreal.MaterialEditingLibrary
    path = "%s/%s" % (MASTER_DIR, name)
    if EAL.does_asset_exist(path):
        mat = EAL.load_asset(path)
        MG.clear_expressions(mat, log=log, tag="[fxblend]")   # на месте: MIC сохраняют родителя
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, MASTER_DIR, unreal.Material,
                                                                      unreal.MaterialFactoryNew())
    opaque = (name == "M_AsdaEffectOpaque")   # True — меш 0x1F, False — аддитивная половина пары
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE if opaque else unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT if opaque
                            else unreal.MaterialShadingModel.MSM_UNLIT)
    # 0x1F — CULLMODE 2 (односторонний); fx-проход контейнера — CULL_NONE (двусторонний)
    mat.set_editor_property("two_sided", not opaque)

    def expr(cls, x, y):
        """Узел графа в точке (x, y)."""
        return MEL.create_material_expression(mat, cls, x, y)
    tex = expr(unreal.MaterialExpressionTextureSampleParameter2D, -900, 0)
    tex.set_editor_property("parameter_name", "SpriteTexture")   # имя как у прочих мастеров — MIC-путь общий
    vc = expr(unreal.MaterialExpressionVertexColor, -900, 300)
    m1 = expr(unreal.MaterialExpressionMultiply, -650, 60)        # srcC = Tex.rgb × VC.rgb
    MEL.connect_material_expressions(tex, "RGB", m1, "A")
    MEL.connect_material_expressions(vc, "", m1, "B")
    P = unreal.MaterialProperty
    if opaque:
        MEL.connect_material_property(m1, "", P.MP_BASE_COLOR)
        sp = expr(unreal.MaterialExpressionConstant, -300, 200)   # Specular 0 — без бликов (у эталона их нет)
        sp.set_editor_property("r", 0.0)
        MEL.connect_material_property(sp, "", P.MP_SPECULAR)
        ro = expr(unreal.MaterialExpressionConstant, -300, 300)   # Roughness 1 — матовая земля
        ro.set_editor_property("r", 1.0)
        MEL.connect_material_property(ro, "", P.MP_ROUGHNESS)
    else:
        if name == "M_AsdaEffectModAddA":         # srcC × srcA, srcA = Tex.a × VC.a
            ma = expr(unreal.MaterialExpressionMultiply, -650, 300)
            MEL.connect_material_expressions(tex, "A", ma, "A")
            MEL.connect_material_expressions(vc, "A", ma, "B")
            m2 = expr(unreal.MaterialExpressionMultiply, -400, 120)
            MEL.connect_material_expressions(m1, "", m2, "A")
            MEL.connect_material_expressions(ma, "", m2, "B")
        elif name == "M_AsdaEffectModAddSq":      # srcC × srcC
            m2 = expr(unreal.MaterialExpressionMultiply, -400, 120)
            MEL.connect_material_expressions(m1, "", m2, "A")
            MEL.connect_material_expressions(m1, "", m2, "B")
        else:                                     # srcC × (1 − dstC), dstC = цвет сцены за эффектом
            sc = expr(unreal.MaterialExpressionSceneColor, -900, 500)
            mk = expr(unreal.MaterialExpressionComponentMask, -700, 500)
            for ch, on in (("r", True), ("g", True), ("b", True), ("a", False)):
                mk.set_editor_property(ch, on)
            MEL.connect_material_expressions(sc, "", mk, "")
            om = expr(unreal.MaterialExpressionOneMinus, -550, 500)
            MEL.connect_material_expressions(mk, "", om, "")
            sat = expr(unreal.MaterialExpressionSaturate, -450, 500)   # HDR сцены > 1 не даёт отрицательного вклада
            MEL.connect_material_expressions(om, "", sat, "")
            m2 = expr(unreal.MaterialExpressionMultiply, -400, 120)
            MEL.connect_material_expressions(m1, "", m2, "A")
            MEL.connect_material_expressions(sat, "", m2, "B")
        bo = _boost(MEL, mat, -400, 350)
        m3 = expr(unreal.MaterialExpressionMultiply, -200, 120)   # × EmissiveBoost (волна Ф1)
        MEL.connect_material_expressions(m2, "", m3, "A")
        MEL.connect_material_expressions(bo, "", m3, "B")
        MEL.connect_material_property(m3, "", P.MP_EMISSIVE_COLOR)
    MG.check_graph(mat, expected=EXPECTED[name], log=log, tag="[fxblend]")
    # ★ 11.10.2026 (fx_alphatest.md): альфа-тест прохода эффектов эталона (ALPHAREF 8, GE) — цепь поверх графа.
    import asda_fx_alphatest as AT
    AT.apply_in_builder(mat, name, log)
    MEL.recompile_material(mat)
    mat.modify()
    ok = EAL.save_loaded_asset(mat)
    log("мастер %s: сохранён=%s" % (name, ok))
    if ok:
        print("ASDA_WRITES_ASSET: %s" % path)
    return mat if ok else None


def opaque_is_lit(mat):
    """Мастер Opaque уже освещаемый (DefaultLit)?"""
    import unreal
    return mat.get_editor_property("shading_model") == unreal.MaterialShadingModel.MSM_DEFAULT_LIT


def ensure_masters(write):
    """Мастера: новые — создать, Opaque — перевести в освещаемый. write=False — только загрузить существующие."""
    import unreal
    EAL = unreal.EditorAssetLibrary
    out = {}
    for suf, nm in MASTERS.items():
        path = "%s/%s" % (MASTER_DIR, nm)
        mat = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
        if write and (nm in NEW_MASTERS and mat is None or nm == "M_AsdaEffectOpaque" and mat and not opaque_is_lit(mat)):
            mat = build_master(nm)
        out[suf] = mat
    return out


# ── MIC ───────────────────────────────────────────────────────────────────────────────────────
def mic_state(stem, suffix, variant, masters):
    """(путь, статус) MIC `MI_<stem>_<suffix><variant>`: 'ok' — есть и верен, 'new' — создать, 'conflict' — чужой."""
    import unreal
    EAL = unreal.EditorAssetLibrary
    MEL = unreal.MaterialEditingLibrary
    base = "%s/MI_%s_%s" % (MAT_DIR, stem, suffix)
    path = base + variant
    if not EAL.does_asset_exist(path):
        return path, "new"
    mic = EAL.load_asset(path)
    par = mic.get_editor_property("parent")   # у варианта буста (дубликат базового) родитель тот же мастер
    tv = MEL.get_material_instance_texture_parameter_value(mic, "SpriteTexture")
    ok_par = par is not None and masters.get(suffix) is not None and par.get_path_name() == masters[suffix].get_path_name()
    ok_tex = tv is not None and tv.get_name().lower() == stem.lower()
    return path, ("ok" if (ok_par and ok_tex) else "conflict")


def make_mic(stem, suffix, variant, masters):
    """Создать MIC (и базовый, если нужен вариант буста). Вернуть ассет или None. Существующие не перезаписываются."""
    import unreal
    EAL = unreal.EditorAssetLibrary
    MEL = unreal.MaterialEditingLibrary
    base_nm = "MI_%s_%s" % (stem, suffix)
    base = "%s/%s" % (MAT_DIR, base_nm)
    tp = "%s/%s" % (TEX_DIR, stem)
    if not EAL.does_asset_exist(tp):
        log("ВНИМАНИЕ: текстуры %s нет" % tp)
        return None
    if not EAL.does_asset_exist(base):
        mic = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            base_nm, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mic, masters[suffix])
        MEL.set_material_instance_texture_parameter_value(mic, "SpriteTexture", EAL.load_asset(tp))
        if not EAL.save_loaded_asset(mic):
            return None
        print("ASDA_WRITES_ASSET: %s" % base)
    if not variant:
        return EAL.load_asset(base)
    vp = base + variant
    if not EAL.does_asset_exist(vp):
        if not EAL.duplicate_asset(base, vp):
            return None
        mic = EAL.load_asset(vp)
        m = re.match(r"_fxb(\d{3})(?:s(\d+))?$", variant)
        MEL.set_material_instance_scalar_parameter_value(mic, "EmissiveBoost", int(m.group(1)) / 100.0)
        MEL.update_material_instance(mic)
        if not EAL.save_loaded_asset(mic):
            return None
        print("ASDA_WRITES_ASSET: %s" % vp)
    return EAL.load_asset(vp)


# ── план по одному BP ─────────────────────────────────────────────────────────────────────────
def _name(o):
    """Имя ассета или ''."""
    return o.get_name() if o else ""


def align(ems, disk):
    """Сопоставить эмиттеры BP с диском по (имя, тип) по порядку. [(i_bp, d) | (i_bp, None)]."""
    out, j = [], 0
    for i, e in enumerate(ems):
        nm, tp = str(e.get_editor_property("name")), int(e.get_editor_property("jeb_type"))
        k = j
        while k < len(disk) and not (disk[k]["name"] == nm and disk[k]["type"] == tp):
            k += 1
        if k < len(disk):
            out.append((i, disk[k]))
            j = k + 1
        else:
            out.append((i, None))
    return out


def plan_bp(jeb, jb, ems, masters):
    """Список правок [(i, поле, старое имя, (суффикс, stem, вариант) | None, причина)] и пропусков [(i, текст)]."""
    edits, skips = [], []
    if CANON_FIX_SKIP.match(jeb.lower()):
        return edits, [(-1, "BLEND_CANON_FIX (отступление владельца, «add») — не трогается")]
    bm = built_mode(jb)
    src, dst = jb.get("blendSrc"), jb.get("blendDst")
    for i, d in align(ems, jb["emitters"]):
        e = ems[i]
        if d is None:
            if int(e.get_editor_property("jeb_type")) == 0x1F or dst == 3:
                skips.append((i, "эмиттер '%s' не сопоставлен с .jeb" % e.get_editor_property("name")))
            continue
        is_mesh = d["type"] in MESH_TYPES
        tex = mesh_tex(d) if is_mesh else d.get("tex", "")
        stem = tex_stem(tex)
        m_old, mb_old = _name(e.get_editor_property("material")), _name(e.get_editor_property("material_b"))
        m_base, m_var = split_variant(m_old)
        mb_base, mb_var = split_variant(mb_old)
        if d["type"] == 0x1F:
            tgt = "MI_%s_opaque" % stem
            ok_old = m_base in ("MI_%s_%s" % (stem, bm), tgt)
            ok_b = (mb_base in ("", "MI_%s_modadd" % stem, "MI_default_modadd"))
            if not (ok_old and ok_b):
                skips.append((i, "0x1F '%s': материал %s/%s не от сборщика — ручная/чужая правка" % (d["name"], m_old, mb_old or "-")))
                continue
            if m_old != tgt:
                edits.append((i, "material", m_old, ("opaque", stem, ""), "0x1F непрозрачный (CEffectMesh_Render)"))
            if mb_old:
                edits.append((i, "material_b", mb_old, None, "0x1F без пары ModAdd"))
            continue
        if dst != 3 or bm != "mod":
            continue
        if src not in SRC_TO_ADD:
            skips.append((i, "src %s без правила" % src))
            continue
        suf = SRC_TO_ADD[src]
        exp_old = ("MI_%s_modadd" % stem, "MI_default_modadd") if is_mesh else ("MI_%s_modadd" % stem,)
        tgt_base = ("MI_%s_%s" % (stem, suf)) if suf else ""
        if mb_base == tgt_base:
            continue
        if mb_base not in exp_old:
            skips.append((i, "'%s': MaterialB %s не от сборщика — ручная/чужая правка" % (d["name"], mb_old or "-")))
            continue
        why = "src %d/dst 3 → %s" % (src, suf or "без аддитивной половины")
        if is_mesh and mb_base == "MI_default_modadd":
            why += "; меш: текстура .jem вместо default"
        var = mb_var                          # вариант буста, переносимый на эталонную половину
        if suf and suf != "modadd" and var:
            # ★ 11.10.2026 (решение владельца, `Reports/owner_1011/fx_unboost_meshes.md`): если прежняя половина
            #   srcC×(1−srcA) давала 0, буст на ней был невидим — на эталонную половину он не переносится.
            import asda_fx_modadda_unboost as UB
            if UB.prior_half_zero(d, mb_base[len("MI_"):-len("_modadd")]):
                why += "; буст %s не переносится: прежняя половина давала 0" % var
                var = ""
        edits.append((i, "material_b", mb_old, (suf, stem, var) if suf else None, why))
    return edits, skips


def retarget(jeb, jb, ems, masters, write):
    """Применить план к списку эмиттеров `ems` (копии структур). Вернуть (ems, строки_правок, строки_пропусков)."""
    edits, skips = plan_bp(jeb, jb, ems, masters)
    rows, srows = [], [{"i": i, "почему": t} for i, t in skips]
    for i, field, old, tgt, why in edits:
        new_name = "" if tgt is None else ("MI_%s_%s%s" % (tgt[1], tgt[0], tgt[2]))
        row = {"i": i, "эмиттер": str(ems[i].get_editor_property("name")), "тип": "0x%X" % int(ems[i].get_editor_property("jeb_type")),
               "поле": field, "было": old or "-", "стало": new_name or "-", "почему": why}
        if tgt is not None:
            path, stt = mic_state(tgt[1], tgt[0], tgt[2], masters)
            row["mic"] = stt
            if stt == "conflict":
                srows.append({"i": i, "почему": "MIC %s существует с чужим родителем/текстурой — не трогаю" % path})
                continue
            if write:
                mic = make_mic(tgt[1], tgt[0], tgt[2], masters) if stt == "new" else __import__("unreal").EditorAssetLibrary.load_asset(path)
                if not mic:
                    srows.append({"i": i, "почему": "MIC %s не создан" % path})
                    continue
                e = ems[i]
                e.set_editor_property(field, mic)
                ems[i] = e
        elif write:
            e = ems[i]
            e.set_editor_property(field, None)
            ems[i] = e
        rows.append(row)
    return ems, rows, srows


def retarget_for_build(cdo, emitters):
    """Точка сборщиков (`asda_fx_common.finish_effect_bp`): привести материалы к эталону. Без изменений — тот же список."""
    if os.environ.get("ASDA_FX_NO_ETALON_BLEND", "") == "1":
        return emitters
    jeb = str(cdo.get_editor_property("effect_name") or "")
    jb = read_jeb(jeb) if jeb else None
    if not jb or not needs_fix(jb):
        return emitters
    masters = ensure_masters(write=True)
    ems, rows, srows = retarget(jeb, jb, list(emitters), masters, True)
    for r in rows:
        log("%s: %s %s %s → %s (%s)" % (jeb, r["эмиттер"], r["поле"], r["было"], r["стало"], r["почему"]))
    for r in srows:
        log("%s: ПРОПУСК %s" % (jeb, r["почему"]))
    return ems


# ── обход всех BP ─────────────────────────────────────────────────────────────────────────────
def run(write):
    """Обход BP_Fx_* с эффектами, где есть что править. write=False — только план."""
    import unreal
    import asda_paths as P
    import asda_fx_common as FXC
    EAL = unreal.EditorAssetLibrary
    masters = ensure_masters(write)
    res = {"режим": "apply" if write else "report", "эффекты": [], "не_собраны": [], "ошибки": []}
    for f in sorted(os.listdir(P.EFFECT_MODEL_ORIG_DIR)):
        if not f.lower().endswith(".jeb"):
            continue
        jeb = f[:-4]
        jb = read_jeb(jeb)
        if not jb or not needs_fix(jb):
            continue
        bpp = "%s/%s" % (FX_DIR, FXC.bp_name(jeb))
        if not EAL.does_asset_exist(bpp):
            res["не_собраны"].append(jeb)
            continue
        bp = EAL.load_asset(bpp)
        cdo = unreal.get_default_object(bp.generated_class())
        ems = list(cdo.get_editor_property("emitters"))
        ems, rows, srows = retarget(jeb, jb, ems, masters, write)
        ent = {"jeb": jeb, "src": jb.get("blendSrc"), "dst": jb.get("blendDst"), "правки": rows, "пропуски": srows}
        if write and rows:
            saved = FXC.finish_effect_bp(bp, cdo, ems)
            ent["сохранён"] = bool(saved)
            if saved:
                print("ASDA_WRITES_ASSET: %s" % bpp)
            else:
                res["ошибки"].append("%s не сохранён" % bpp)
        res["эффекты"].append(ent)
        log("%s (src %s/dst %s): правок %d, пропусков %d" % (jeb, jb.get("blendSrc"), jb.get("blendDst"), len(rows), len(srows)))
    out = REPORT_JSON % res["режим"]
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as fh:
        json.dump(res, fh, ensure_ascii=False, indent=1)
    n = sum(len(e["правки"]) for e in res["эффекты"])
    log("ГОТОВО: режим %s, эффектов %d, правок %d, не собраны %d, ошибок %d; план %s"
        % (res["режим"], len(res["эффекты"]), n, len(res["не_собраны"]), len(res["ошибки"]), out))
    return res


def main():
    """Прямой запуск: ASDA_FXBLEND_MODE=apply — запись, иначе только план."""
    run(os.environ.get("ASDA_FXBLEND_MODE", "report") == "apply")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
