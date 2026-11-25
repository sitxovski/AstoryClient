# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_alphatest.py — АЛЬФА-ТЕСТ ПРОХОДА ЭФФЕКТОВ как в эталоне (11.10.2026, отчёт
#   `Reports/owner_1010/fx_alphatest.md`, ревизия `fx_blend_fix_REVIEW.md` СРЕДН-1).
#
# ЭТАЛОН. Очередь эффектов g_RenderQueue@0x3537FA8 (контейнеры .jeb — `effectMgr_BuildDepthSortQueue` →
#   `CRenderQueue_AddNode`, тип 1 → `objListRenderInRange`) рисуется под
#   ALPHATESTENABLE 1, ALPHAREF 8, ALPHAFUNC 7 (GREATEREQUAL):
#   • сцены логина/выбора/создания (`CRenderer_RenderFrame@0x975EF0`, render_core.c:820-827) — всегда;
#   • мир (`CRenderer_RenderWorldFrame@0x9773B0`, render_core.c:1419-1432) — только при пункте свечения 4 «Все»
#     (`sub_928F5F` → g_nCurrentChannelId@0x15CB4E0 = g_hMainWnd+0x80 = пункт комбо 871); на 0..3 — `clanShadowRenderQueue`,
#     тест там не включается (после блоков его гасят в 0, а Reset устройства возвращает ALPHAFUNC = ALWAYS).
#   Альфа пикселя: стадия 0 ALPHAOP MODULATE(TEXTURE, CURRENT) ⇒ srcA = Tex.a × diffuse.a (у 0x1F — Tex.a × TFACTOR.a,
#   `d3dSetAlphaBlendArgs`, render_core.c:40296). Тест отбрасывает пиксель ЦЕЛИКОМ — и аддитивную, и затемняющую часть.
#
# ПОРТ. Скаляр `AlphaTest` (0/1) коллекции /Game/Effects/MPC_AsdaFx ставит C++ (`UAsdaVideoSettingsSubsystem::
#   ApplyFxAlphaTest`: сцена логина или пункт 4 ⇒ 1). Каждый мастер эффектов получает цепь
#     srcA  = Tex.a × VC.a
#     step  = saturate(floor(srcA − 8/255) + 1)   — 1, если srcA ≥ 8/255 РОВНО (GE эталона), иначе 0
#             ★ 11.10.2026 (ревизия fx_alphatest_REVIEW МЕЛК-4): было saturate(ceil(srcA − 7.5/255)) — порог 7.5/255
#               пропускал srcA из (7.5, 8)/255, которые эталон с float-сравнением (ref/255) отбрасывает. floor(x)+1 даёт
#               ровно x ≥ 0 ⇒ ≥ 1, x ∈ [−1, 0) ⇒ 0 без эпсилона. Старые мастера переводит `migrate_threshold`.
#     keep  = lerp(1, step, AlphaTest)             — без теста пиксель остаётся всегда
#   и применяет её к своему выходу:
#     аддитивные (Add, ModAdd, ModAddC/A/Sq/Inv): Emissive × keep;
#     Alpha (полупрозрачный):                     Opacity × keep;
#     Mod (Modulate, dst × E):                    lerp(1, E, keep)  — отброшенный пиксель не затемняет;
#     Opaque (0x1F):                              Masked, OpacityMask = keep, порог 0.5.
#   Цепь только ДОБАВЛЯЕТСЯ к существующему графу (прочие узлы — затухание, буст, FadeIn — не трогаются) и умножается
#   на выход, поэтому порядок с другими доработками не важен. Идемпотентность: узел CollectionParameter MPC_AsdaFx/AlphaTest
#   в графе = уже применено.
#
# Запуск: python Tools/run_headless_safe.py Tools/asda_fx_alphatest.py --commandlet-rhi --marker "FX-ALPHATEST ЗАВЕРШЕНО"
#   ASDA_FX_ALPHATEST_MODE=report (по умолчанию) — только печать; =apply — запись.
#   Ассеты сохранять только без запущенного -game (закон asset-save-blocked-by-running-game).
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
import os

MASTER_DIR = "/Game/Effects"                       # мастера эффектов
MPC_NAME = "MPC_AsdaFx"                            # коллекция параметров эффектов
MPC_PATH = "%s/%s" % (MASTER_DIR, MPC_NAME)        # её путь
PARAM = "AlphaTest"                                # скаляр: 1 — тест включён
PARAM_DEFAULT = 1.0                                # дефолт (редактор; в игре значение пишет C++ под пункт свечения)
THRESHOLD = 8.0 / 255.0                            # порог эталона: ALPHAREF 8, GREATEREQUAL ⇒ srcA ≥ 8/255 (ровно)
OLD_THRESHOLD = 7.5 / 255.0                        # прежний порог (до 11.10.2026) — признак старой цепи для миграции
# Мастер → способ применения keep (см. шапку).
MODES = {
    "M_AsdaEffectAdd": "emissive_mul",
    "M_AsdaEffectAlpha": "opacity_mul",
    "M_AsdaEffectMod": "emissive_lerp1",
    "M_AsdaEffectModAdd": "emissive_mul",
    "M_AsdaEffectModAddC": "emissive_mul",
    "M_AsdaEffectModAddA": "emissive_mul",
    "M_AsdaEffectModAddSq": "emissive_mul",
    "M_AsdaEffectModAddInv": "emissive_mul",
    "M_AsdaEffectOpaque": "masked",
}
# Сколько узлов добавляет цепь: coll, vc, srcA, sub, floor, add, sat, keep (+ итоговый mul/lerp, кроме masked).
ADDED = {"masked": 8}
ADDED_DEFAULT = 9


def log(m):
    """Строка журнала с тегом."""
    import unreal
    unreal.log("[fx-alphatest] " + str(m))


def ensure_mpc(write):
    """Коллекция MPC_AsdaFx со скаляром AlphaTest: вернуть ассет (создать при write=True) или None."""
    import unreal
    EAL = unreal.EditorAssetLibrary
    if EAL.does_asset_exist(MPC_PATH):
        mpc = EAL.load_asset(MPC_PATH)
        names = [str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("scalar_parameters")]
        if PARAM in names:
            return mpc
        if not write:
            log("MPC %s без скаляра %s" % (MPC_PATH, PARAM))
            return None
    else:
        if not write:
            log("MPC %s нет (report)" % MPC_PATH)
            return None
        mpc = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            MPC_NAME, MASTER_DIR, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    sp = unreal.CollectionScalarParameter()          # конструктор структуры сам выдаёт Id (FGuid)
    sp.set_editor_property("parameter_name", PARAM)
    sp.set_editor_property("default_value", PARAM_DEFAULT)
    arr = list(mpc.get_editor_property("scalar_parameters"))
    arr.append(sp)
    mpc.set_editor_property("scalar_parameters", arr)
    mpc.modify()
    ok = EAL.save_loaded_asset(mpc)
    log("MPC %s: скаляр %s=%.1f, сохранён=%s" % (MPC_PATH, PARAM, PARAM_DEFAULT, ok))
    if ok:
        print("ASDA_WRITES_ASSET: %s" % MPC_PATH)
    return mpc if ok else None


def is_applied(mat, mpc):
    """В графе уже есть узел CollectionParameter MPC_AsdaFx/AlphaTest?"""
    import unreal
    for e in unreal.MaterialEditingLibrary.get_material_expressions(mat):
        if isinstance(e, unreal.MaterialExpressionCollectionParameter):
            c = e.get_editor_property("collection")
            if c and c.get_path_name().split(".")[0] == MPC_PATH and str(e.get_editor_property("parameter_name")) == PARAM:
                return True
    return False


def _sprite_tex(mat):
    """Узел SpriteTexture (общий параметр текстуры всех мастеров эффектов) или None."""
    import unreal
    for e in unreal.MaterialEditingLibrary.get_material_expressions(mat):
        if isinstance(e, unreal.MaterialExpressionTextureSampleParameter2D) \
                and str(e.get_editor_property("parameter_name")) == "SpriteTexture":
            return e
    return None


def _input(mat, prop):
    """(узел, имя выхода) входа материала `prop` или (None, '')."""
    import unreal
    MEL = unreal.MaterialEditingLibrary
    node = MEL.get_material_property_input_node(mat, prop)
    if not node:
        return None, ""
    try:
        out = str(MEL.get_material_property_input_node_output_name(mat, prop))
    except Exception:
        out = ""
    return node, out


def apply_to(mat, name, mpc):
    """Добавить цепь альфа-теста в граф мастера `name` (без сохранения). Вернуть True, если граф изменён.

    Бросает RuntimeError при неожиданном графе — вызывающий тогда НЕ сохраняет ассет."""
    import unreal
    import asda_mat_graph as MG
    MEL = unreal.MaterialEditingLibrary
    P = unreal.MaterialProperty
    mode = MODES[name]
    if is_applied(mat, mpc):
        log("%s: уже применено" % name)
        return False
    tex = _sprite_tex(mat)
    if tex is None:
        raise RuntimeError("%s: нет узла SpriteTexture — граф не тот" % name)
    target = {"emissive_mul": P.MP_EMISSIVE_COLOR, "emissive_lerp1": P.MP_EMISSIVE_COLOR,
              "opacity_mul": P.MP_OPACITY, "masked": None}[mode]
    old, old_out = (None, "")
    if target is not None:
        old, old_out = _input(mat, target)
        if old is None:
            raise RuntimeError("%s: вход %s пуст — граф не тот" % (name, target))
    before = MG.expression_count(mat)

    def expr(cls, x, y):
        """Узел графа в точке (x, y) — правее и ниже существующих."""
        return MEL.create_material_expression(mat, cls, x, y)
    coll = expr(unreal.MaterialExpressionCollectionParameter, 200, 900)   # AlphaTest 0/1 (C++ под пункт свечения)
    coll.set_editor_property("collection", mpc)
    coll.set_editor_property("parameter_name", PARAM)
    vc = expr(unreal.MaterialExpressionVertexColor, 200, 700)               # diffuse эталона (VC.a = вершинная альфа)
    sa = expr(unreal.MaterialExpressionMultiply, 400, 700)                  # srcA = Tex.a × VC.a
    MEL.connect_material_expressions(tex, "A", sa, "A")
    MEL.connect_material_expressions(vc, "A", sa, "B")
    sub = expr(unreal.MaterialExpressionSubtract, 550, 700)                 # srcA − 8/255
    sub.set_editor_property("const_b", THRESHOLD)
    MEL.connect_material_expressions(sa, "", sub, "A")
    fl = expr(unreal.MaterialExpressionFloor, 700, 700)                     # ≥0 → 0.., [−1..0) → −1
    MEL.connect_material_expressions(sub, "", fl, "")
    one = expr(unreal.MaterialExpressionAdd, 775, 700)                      # +1: GE → ≥1, иначе 0
    one.set_editor_property("const_b", 1.0)
    MEL.connect_material_expressions(fl, "", one, "A")
    st = expr(unreal.MaterialExpressionSaturate, 850, 700)                  # step 0/1
    MEL.connect_material_expressions(one, "", st, "")
    keep = expr(unreal.MaterialExpressionLinearInterpolate, 1000, 750)      # keep = lerp(1, step, AlphaTest)
    keep.set_editor_property("const_a", 1.0)
    MEL.connect_material_expressions(st, "", keep, "B")
    MEL.connect_material_expressions(coll, "", keep, "Alpha")
    if mode in ("emissive_mul", "opacity_mul"):
        fin = expr(unreal.MaterialExpressionMultiply, 1200, 400)            # выход × keep
        MEL.connect_material_expressions(old, old_out, fin, "A")
        MEL.connect_material_expressions(keep, "", fin, "B")
        MEL.connect_material_property(fin, "", target)
    elif mode == "emissive_lerp1":
        fin = expr(unreal.MaterialExpressionLinearInterpolate, 1200, 400)   # Modulate: lerp(1, E, keep)
        fin.set_editor_property("const_a", 1.0)
        MEL.connect_material_expressions(old, old_out, fin, "B")
        MEL.connect_material_expressions(keep, "", fin, "Alpha")
        MEL.connect_material_property(fin, "", target)
    else:                                                                   # 0x1F: Masked, маска = keep
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        mat.set_editor_property("opacity_mask_clip_value", 0.5)
        MEL.connect_material_property(keep, "", P.MP_OPACITY_MASK)
    MG.check_graph(mat, expected=before + ADDED.get(mode, ADDED_DEFAULT), log=log, tag="[fx-alphatest]")
    # Контроль связности: итоговый вход ведёт в новый узел.
    chk_prop = P.MP_OPACITY_MASK if mode == "masked" else target
    node, _ = _input(mat, chk_prop)
    if node is None or node.get_name() != (keep if mode == "masked" else fin).get_name():
        raise RuntimeError("%s: вход %s не подключён к цепи — ассет НЕ сохранён" % (name, chk_prop))
    return True


def _old_chain(mat):
    """(sub, ceil, saturate) прежней цепи (порог 7.5/255 + Ceil) или None, если её нет."""
    import unreal
    MEL = unreal.MaterialEditingLibrary
    exprs = MEL.get_material_expressions(mat)
    subs = [e for e in exprs if isinstance(e, unreal.MaterialExpressionSubtract)
            and abs(float(e.get_editor_property("const_b")) - OLD_THRESHOLD) < 1e-7]
    for sub in subs:
        for ce in [e for e in exprs if isinstance(e, unreal.MaterialExpressionCeil)]:
            if sub not in list(MEL.get_inputs_for_material_expression(mat, ce)):
                continue
            for st in [e for e in exprs if isinstance(e, unreal.MaterialExpressionSaturate)]:
                if ce in list(MEL.get_inputs_for_material_expression(mat, st)):
                    return sub, ce, st
    return None


def migrate_threshold(mat, name):
    """Перевести прежнюю цепь на ровный порог 8/255: sub.const_b = 8/255, Ceil → Floor + Add(1). Без сохранения.
    True — граф изменён; False — старой цепи нет. RuntimeError — граф не тот (ассет не сохранять)."""
    import unreal
    import asda_mat_graph as MG
    MEL = unreal.MaterialEditingLibrary
    old = _old_chain(mat)
    if old is None:
        return False
    sub, ce, st = old
    before = MG.expression_count(mat)
    x = ce.get_editor_property("material_expression_editor_x")
    y = ce.get_editor_property("material_expression_editor_y")
    sub.set_editor_property("const_b", THRESHOLD)
    fl = MEL.create_material_expression(mat, unreal.MaterialExpressionFloor, x, y)
    MEL.connect_material_expressions(sub, "", fl, "")
    one = MEL.create_material_expression(mat, unreal.MaterialExpressionAdd, x + 75, y)
    one.set_editor_property("const_b", 1.0)
    MEL.connect_material_expressions(fl, "", one, "A")
    MEL.connect_material_expressions(one, "", st, "")
    MEL.delete_material_expression(mat, ce)
    MG.check_graph(mat, expected=before + 1, log=log, tag="[fx-alphatest]")
    ins = list(MEL.get_inputs_for_material_expression(mat, st))
    if not ins or ins[0] is None or ins[0].get_name() != one.get_name():
        raise RuntimeError("%s: Saturate не подключён к новой ступени — ассет НЕ сохранён" % name)
    log("%s: порог 7.5/255 → 8/255 (Ceil → Floor+1)" % name)
    return True


def patch_master(name, write):
    """Применить к мастеру `name` (report — только сказать, что сделал бы). Вернуть 'applied'/'already'/'would'/'fail'."""
    import unreal
    EAL = unreal.EditorAssetLibrary
    MEL = unreal.MaterialEditingLibrary
    path = "%s/%s" % (MASTER_DIR, name)
    mat = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
    if mat is None:
        log("%s: нет ассета" % name)
        return "fail"
    mpc = ensure_mpc(write)
    if mpc is None:
        return "would" if not write else "fail"
    if is_applied(mat, mpc):
        if _old_chain(mat) is None:
            log("%s: уже применено" % name)
            return "already"
        if not write:
            log("%s: перевёл бы порог на 8/255" % name)
            return "would_migrate"
        try:
            migrate_threshold(mat, name)
        except Exception as ex:
            log("%s: ОШИБКА миграции %s" % (name, ex))
            return "fail"
        MEL.recompile_material(mat)
        mat.modify()
        ok = EAL.save_loaded_asset(mat)
        log("%s: порог мигрирован, сохранён=%s" % (name, ok))
        if ok:
            print("ASDA_WRITES_ASSET: %s" % path)
        return "migrated" if ok else "fail"
    if not write:
        log("%s: применил бы (%s)" % (name, MODES[name]))
        return "would"
    try:
        apply_to(mat, name, mpc)
    except Exception as ex:
        log("%s: ОШИБКА %s" % (name, ex))
        return "fail"
    MEL.recompile_material(mat)
    mat.modify()
    ok = EAL.save_loaded_asset(mat)
    st = MEL.get_statistics(mat)
    log("%s: %s, blend=%s, ps=%s, сохранён=%s" % (name, MODES[name], mat.get_editor_property("blend_mode"),
                                                 st.get_editor_property("num_pixel_shader_instructions"), ok))
    if ok:
        print("ASDA_WRITES_ASSET: %s" % path)
    return "applied" if ok else "fail"


def apply_in_builder(mat, name, log_fn=None):
    """Для сборщиков мастеров: после пересборки графа добавить цепь (MPC создаётся при нужде). Без сохранения."""
    mpc = ensure_mpc(True)
    if mpc is None:
        raise RuntimeError("[fx-alphatest] нет %s — мастер %s без альфа-теста НЕ сохраняется" % (MPC_PATH, name))
    changed = apply_to(mat, name, mpc)
    if log_fn:
        log_fn("альфа-тест эффектов (asda_fx_alphatest): %s" % ("добавлен" if changed else "уже был"))
    return changed


def main():
    """Работа скрипта (закон tools-no-import-side-effects: импорт ничего не делает)."""
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    write = os.environ.get("ASDA_FX_ALPHATEST_MODE", "report").lower() == "apply"
    res = {}
    for name in MODES:
        res[name] = patch_master(name, write)
    bad = [n for n, r in res.items() if r == "fail"]
    log("итог (%s): %s" % ("apply" if write else "report", ", ".join("%s=%s" % kv for kv in res.items())))
    log("FX-ALPHATEST ЗАВЕРШЕНО: мастеров %d, ошибок %d" % (len(res), len(bad)))


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
