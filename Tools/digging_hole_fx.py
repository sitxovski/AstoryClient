# Copyright (c) 2026 @sitxovski (Discord)
# digging_hole_fx.py — ЯМА КОПАНИЯ (10.10.2026, отчёт `Reports/owner_1010/digging_hole.md`): режим смешивания эффектов
#   копания `c-shovel-a_00/a_01/b_01` по эталону. Точечная правка ПО ДАННЫМ трёх BP (только поля Material/MaterialB нужных
#   эмиттеров; прочие поля и настройки владельца не трогаются), плюс два новых мастера. Общие мастера/MIC не меняются.
#
# ЭТАЛОН (render_core.c / effects_particles.c, сверено построчно):
#   * Тип 0x1F (`badakmeshnoalpha` ямы, `badakmesh_part` комьев удара) рисует `CEffectMesh_Render@0x123C0E0`:
#       `rendApplyAlphaFogRenderState@0x125C490` ДО `CMesh_RenderNode(…, 2)` → `rendDisableAlphaTestState` (миснейм:
#       D3DRS_ALPHABLENDENABLE = 0), `CRenderer_ApplyAlphaFogRenderState` (ZENABLE = 1, ZWRITEENABLE = 1),
#       `gfxApplyColorWriteState` (CULLMODE = 2, отсечение задних граней); цвет = текстура × TFACTOR (узел, `d3dSetAlphaBlendArgs`
#       COLORARG2 = TFACTOR). ⇒ НЕПРОЗРАЧНЫЙ меш с записью глубины, бленд контейнера к нему НЕ применяется.
#       Порт строил его как остальные эмиттеры контейнера («mod»: Modulate + ModAdd) — яма выходила полупрозрачной.
#   * Тип 0x11/0x12 (`badakmesh` — тёмный ореол) — `effectNode_updateAndRenderMesh@0x1229810`: `rendApplyAlphaBlendAndDraw`
#       (ALPHABLENDENABLE = 1, CULL_NONE) ⇒ бленд контейнера. Порт ставил ему парную ModAdd-секцию с ТЕКСТУРОЙ `default`
#       (имя текстуры брали из записи эмиттера `default.bmp`, а не из .jem) — исправлено на текстуру меша.
#   * Бленд контейнера этих трёх файлов — диск 3/3 ⇒ D3DBLEND 4/4 = INVSRCCOLOR:INVSRCCOLOR (лоадер «диск+1»,
#       effects_particles.c:113269): Final = srcC×(1−srcC) + dst×(1−srcC). Мастер-пара порта «mod» написана под диск 5/3
#       (INVSRCALPHA: srcC×(1−srcA) + dst×(1−srcC)); Modulate-половина общая, Additive-половина для src = 3 — своя:
#       `M_AsdaEffectModAddC` (Emissive = srcC×(1−srcC)).
#
# Новые ассеты: /Game/Effects/M_AsdaEffectOpaque, /Game/Effects/M_AsdaEffectModAddC,
#   /Game/AsdaAssets/Effects/Mat/MI_<текстура>_opaque|_modaddc. Существующие MIC/мастера НЕ меняются.
# Запуск: MSYS_NO_PATHCONV=1 python Tools/run_headless_safe.py Tools/digging_hole_fx.py --marker "[dighole] ГОТОВО"
#   (сборщик `digging_build_fx.py` зовёт `apply()` сам после постройки BP — пересборка повторяет правку).
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import asda_fx_common as FXC   # общие точки канальных сборщиков (без побочных действий при импорте)

FX_DIR = "/Game/AsdaAssets/Effects"          # BP эффектов копания (имя `BP_Fx_<jeb>`)
MAT_DIR = FX_DIR + "/Mat"                    # MIC канала
TEX_DIR = FX_DIR + "/Tex"                    # текстуры канала
MASTER_DIR = "/Game/Effects"                 # мастера эффектов
M_OPAQUE = "M_AsdaEffectOpaque"              # непрозрачный Unlit: Emissive = Tex.rgb × VC.rgb (эталон 0x1F)
M_MODADDC = "M_AsdaEffectModAddC"            # Additive Unlit: Emissive = srcC × (1 − srcC) (src = INVSRCCOLOR)
# Узлов в графе (контроль `asda_mat_graph.check_graph`): Opaque — tex, vc, mul = 3; ModAddC — tex, vc, mul, 1−x, mul = 5.
EXPECTED = {M_OPAQUE: 3, M_MODADDC: 5}
# Эффекты копания (база .jeb) — ровно те, у которых бленд контейнера 3/3 и/или меш 0x1F (перепись EffectModel.bnz).
JEBS = ["eff_0_char_99_c-shovel-a_00", "eff_0_char_99_c-shovel-a_01", "eff_0_char_99_c-shovel-b_01"]
MESH_TYPES = (0x11, 0x12, 0x1F)              # меш-эмиттеры (.jem)


def log(m):
    """Журнал канала ямы копания."""
    import unreal
    unreal.log_warning("[dighole] %s" % m)


def _tex_stem(name):
    """Имя ассета текстуры, как у `asda_place_effects.get_fx_material`: без расширения, нижний регистр, дефис сохраняется."""
    import re
    return re.sub(r'[^A-Za-z0-9_-]', '_', os.path.splitext(name)[0]).lower() if name else "none"


def ensure_master(name):
    """Создать (или пересобрать на месте) мастер `name`; вернуть ассет. Сохраняет только при верном графе."""
    import unreal
    import asda_mat_graph as MG
    EAL = unreal.EditorAssetLibrary
    MEL = unreal.MaterialEditingLibrary
    path = "%s/%s" % (MASTER_DIR, name)
    if name == M_OPAQUE:
        # ★ 10.10.2026 (`fx_blend_fix.md`, замечание владельца: тени на яму не ложатся, яма светлее грунта): мастер
        #   0x1F теперь ОСВЕЩАЕМЫЙ, его граф ведёт `asda_fx_blend_fix.build_master` — здесь только загрузка/перевод.
        import asda_fx_blend_fix as BF
        mat = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
        return mat if (mat and BF.opaque_is_lit(mat)) else BF.build_master(name)
    if EAL.does_asset_exist(path):
        mat = EAL.load_asset(path)
        MG.clear_expressions(mat, log=log, tag="[dighole]")
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, MASTER_DIR, unreal.Material,
                                                                      unreal.MaterialFactoryNew())
    opaque = (name == M_OPAQUE)   # True — непрозрачный мастер ямы, False — аддитивная половина INVSRCCOLOR
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE if opaque else unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    # Эталон: 0x1F — CULLMODE 2 (задние грани отсекаются) ⇒ односторонний; fx-проход контейнера — CULL_NONE ⇒ двусторонний.
    mat.set_editor_property("two_sided", not opaque)

    def expr(cls, x, y):
        """Создать узел графа в точке (x, y)."""
        return MEL.create_material_expression(mat, cls, x, y)
    tex = expr(unreal.MaterialExpressionTextureSampleParameter2D, -700, 0)
    tex.set_editor_property("parameter_name", "SpriteTexture")   # имя как у прочих мастеров — MIC-путь общий
    vc = expr(unreal.MaterialExpressionVertexColor, -700, 250)
    m1 = expr(unreal.MaterialExpressionMultiply, -450, 60)        # Tex.rgb × VC.rgb (= srcC; у 0x1F VC = TFACTOR узла)
    MEL.connect_material_expressions(tex, "RGB", m1, "A")
    MEL.connect_material_expressions(vc, "", m1, "B")
    if opaque:
        MEL.connect_material_property(m1, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        om = expr(unreal.MaterialExpressionOneMinus, -250, 220)   # 1 − srcC (вход OneMinus безымянный)
        MEL.connect_material_expressions(m1, "", om, "")
        m2 = expr(unreal.MaterialExpressionMultiply, -150, 60)    # srcC × (1 − srcC)
        MEL.connect_material_expressions(m1, "", m2, "A")
        MEL.connect_material_expressions(om, "", m2, "B")
        MEL.connect_material_property(m2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MG.check_graph(mat, expected=EXPECTED[name], log=log, tag="[dighole]")
    # ★ 11.10.2026 (fx_alphatest.md): альфа-тест прохода эффектов эталона (ALPHAREF 8, GE) — цепь поверх графа.
    import asda_fx_alphatest as AT
    AT.apply_in_builder(mat, name, log)
    MEL.recompile_material(mat)
    mat.modify()
    ok = EAL.save_loaded_asset(mat)
    log("мастер %s: %s, сохранён=%s" % (name, "Opaque" if opaque else "Additive (INVSRCCOLOR)", ok))
    if ok:
        print("ASDA_WRITES_ASSET: %s" % path)
    return mat if ok else None


def ensure_mic(tex, suffix, master):
    """MIC `MI_<tex>_<suffix>` от `master` с текстурой канала; вернуть ассет или None (нет текстуры / не сохранён)."""
    import unreal
    EAL = unreal.EditorAssetLibrary
    MEL = unreal.MaterialEditingLibrary
    stem = _tex_stem(tex)
    tp = "%s/%s" % (TEX_DIR, stem)
    if not EAL.does_asset_exist(tp):
        log("ВНИМАНИЕ: текстуры %s нет — MIC %s не создан" % (tp, suffix))
        return None
    nm = "MI_%s_%s" % (stem, suffix)
    mp = "%s/%s" % (MAT_DIR, nm)
    if EAL.does_asset_exist(mp):
        mic = EAL.load_asset(mp)
    else:
        mic = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            nm, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mic, master)
    MEL.set_material_instance_texture_parameter_value(mic, "SpriteTexture", EAL.load_asset(tp))
    ok = EAL.save_loaded_asset(mic)
    if ok:
        print("ASDA_WRITES_ASSET: %s" % mp)
    return mic if ok else None


def apply():
    """Мастера + MIC + точечная правка Material/MaterialB у эмиттеров трёх BP. True — всё записано."""
    import unreal
    import asda_jeb_parse as J
    import asda_jem_parse as JEM
    EAL = unreal.EditorAssetLibrary
    m_op = ensure_master(M_OPAQUE)
    m_ac = ensure_master(M_MODADDC)
    if not m_op or not m_ac:
        log("мастера не сохранены — BP не трогаю")
        return False
    all_ok = True   # итог: все BP записаны
    for jeb in JEBS:
        jb = J.parse_effect(jeb)
        bpp = "%s/%s" % (FX_DIR, FXC.bp_name(jeb))
        if not jb or not EAL.does_asset_exist(bpp):
            log("%s: нет .jeb или BP %s — пропуск" % (jeb, bpp))
            all_ok = False
            continue
        src_c = (jb["blendSrc"] == 3 and jb["blendDst"] == 3)   # INVSRCCOLOR:INVSRCCOLOR
        bp = EAL.load_asset(bpp)
        cdo = unreal.get_default_object(bp.generated_class())
        ems = list(cdo.get_editor_property("emitters"))          # копии структур — правим и пишем массив целиком
        disk = jb["emitters"]
        if len(ems) != len(disk):
            log("%s: эмиттеров в BP %d, на диске %d — сопоставить нельзя, пропуск" % (jeb, len(ems), len(disk)))
            all_ok = False
            continue
        changes = []   # описание правок для журнала
        for i, (e, d) in enumerate(zip(ems, disk)):
            if e.get_editor_property("name") != d["name"] or int(e.get_editor_property("jeb_type")) != d["type"]:
                log("%s: эмиттер %d '%s'/0x%X ≠ диск '%s'/0x%X — пропуск BP" % (jeb, i, e.get_editor_property("name"),
                    e.get_editor_property("jeb_type"), d["name"], d["type"]))
                changes = None
                break
            tex = d.get("tex", "")
            if d["type"] in MESH_TYPES:
                meshes = JEM.load_jem(d.get("jem") or d["name"]) or []
                tex = meshes[0]["tex"] if meshes else tex          # текстура меша — из .jem (как Material при сборке)
            if d["type"] == 0x1F:
                mic = ensure_mic(tex, "opaque", m_op)
                if not mic:
                    changes = None
                    break
                e.set_editor_property("material", mic)
                e.set_editor_property("material_b", None)
                changes.append("%d %s: непрозрачный %s, без ModAdd" % (i, d["name"], mic.get_name()))
            elif src_c and e.get_editor_property("material_b"):
                mic = ensure_mic(tex, "modaddc", m_ac)
                if not mic:
                    changes = None
                    break
                e.set_editor_property("material_b", mic)
                changes.append("%d %s: ModAdd → %s (srcC×(1−srcC))" % (i, d["name"], mic.get_name()))
        if changes is None:
            all_ok = False
            continue
        saved = FXC.finish_effect_bp(bp, cdo, ems)
        log("%s: %s; сохранён=%s" % (jeb, "; ".join(changes) or "правок нет", saved))
        if saved:
            print("ASDA_WRITES_ASSET: %s" % bpp)
        all_ok = all_ok and saved
    return all_ok


def main():
    """Прямой запуск: применить и напечатать маркер."""
    ok = apply()
    log("ГОТОВО: %s" % ("всё записано" if ok else "ЕСТЬ ПРОПУСКИ — см. выше"))


if FXC.is_direct_run(globals()):
    main()
