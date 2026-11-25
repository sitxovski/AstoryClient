# Copyright (c) 2026 @sitxovski (Discord)
# M_AsdaTargetHighlight — оверлей-материал ПОДСВЕТКИ ВЫБРАННОЙ ЦЕЛИ.
#
# ЭТАЛОН (см. Reports/target_selection.md): у мешей выбранной цели клиент ставит
#   NoSkinTexScroll.z = 3.0 (у остальных 1.0), а пиксельный шейдер system/EmptyProject.fx
#   (PShadeNoSkin / PShadeNoSkinNoLight) под комментарием «//Change Brightness» делает
#       Output.RGBColor += NoSkinTexScroll.z * 0.1f;
#   → выделение = АДДИТИВНАЯ добавка +0.2 к RGB. Модель остаётся УЗНАВАЕМОЙ, просто чуть светлее.
#
# ★ ФИКС 2026-08-04 («сплошной белый силуэт»): причина — краевой канал давал добавку до 1.8
#   (FillAdd 0.2 + Fresnel·1.6), что аддитивно ВЫБЕЛИВАЕТ модель в силуэт. Меры:
#     1. край считается ЯВНО: pow(1 − clamp(dot(VertexNormalWS, CameraVectorWS),0,1), RimPower)
#        — без ноды Fresnel и без опоры на Parameters.WorldNormal (у Unlit-материала он ненадёжен);
#     2. добавлен ЖЁСТКИЙ КЛАМП MaxAdd (дефолт 0.6): amount = min(FillAdd + Rim, MaxAdd)
#        — материал ФИЗИЧЕСКИ не может выбелить меш;
#     3. RimIntensity снижен 1.6 → 0.8;
#     4. каждое соединение графа логируется (тихое несоединение — источник прошлых граблей:
#        у VectorParameter НЕТ выхода «RGB», он есть только у TextureSample).
#
# Параметры (имена ДОЛЖНЫ совпадать с AsdaTargetHighlightComponent.cpp):
#   Color (vector) · FillAdd · RimIntensity · RimPower · MaxAdd
#
# Запуск:  Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_build_target_highlight_mat.py
#          (путь к .py — ТОЛЬКО абсолютный: относительный резолвится от Engine/Binaries/Win64)
import unreal

mel = unreal.MaterialEditingLibrary
atools = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary

PKG, NAME = "/Game/Materials", "M_AsdaTargetHighlight"
path = "%s/%s" % (PKG, NAME)


def E(cls, x, y):
    return mel.create_material_expression(m, cls, x, y)

BAD = []
def C(a, ao, b, bi):
    ok = mel.connect_material_expressions(a, ao, b, bi)
    unreal.log("[TGT] connect %s[%s] -> %s.%s : %s" % (a.get_name(), ao or "<default>", b.get_name(), bi, ok))
    if not ok: BAD.append("%s[%s]->%s.%s" % (a.get_name(), ao, b.get_name(), bi))
def P(e, eo, prop):
    ok = mel.connect_material_property(e, eo, prop)
    unreal.log("[TGT] property %s[%s] -> %s : %s" % (e.get_name(), eo or "<default>", prop, ok))
    if not ok: BAD.append("%s->%s" % (e.get_name(), prop))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global m, ok
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    m = atools.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
    # Аддитивный unlit-оверлей: не пишет глубину, не ловит свет, только ДОБАВЛЯЕТ цвет поверх модели.
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", False)
    m.set_editor_property("used_with_skeletal_mesh", True)        # без этого движок ВЫКИДЫВАЕТ оверлей
    m.set_editor_property("used_with_morph_targets", True)
    m.set_editor_property("used_with_static_lighting", True)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    # ── Параметры ──
    col = E(unreal.MaterialExpressionVectorParameter, -1250, -320)
    col.set_editor_property("parameter_name", "Color")
    col.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    fill = E(unreal.MaterialExpressionScalarParameter, -1250, -120)
    fill.set_editor_property("parameter_name", "FillAdd")
    fill.set_editor_property("default_value", 0.2)      # ДЕЛЬТА оригинала: 3.0*0.1 − 1.0*0.1
    rimI = E(unreal.MaterialExpressionScalarParameter, -1250, 60)
    rimI.set_editor_property("parameter_name", "RimIntensity")
    rimI.set_editor_property("default_value", 0.8)
    rimP = E(unreal.MaterialExpressionScalarParameter, -1250, 240)
    rimP.set_editor_property("parameter_name", "RimPower")
    rimP.set_editor_property("default_value", 3.0)
    maxA = E(unreal.MaterialExpressionScalarParameter, -1250, 420)
    maxA.set_editor_property("parameter_name", "MaxAdd")
    maxA.set_editor_property("default_value", 0.6)      # ПОТОЛОК добавки — защита от выбеливания
    # ── Край (обводка) считается явно, без ноды Fresnel ──
    vn = E(unreal.MaterialExpressionVertexNormalWS, -1050, 560)
    cv = E(unreal.MaterialExpressionCameraVectorWS, -1050, 700)
    dot = E(unreal.MaterialExpressionDotProduct, -880, 620)
    C(vn, "", dot, "A")
    C(cv, "", dot, "B")
    clamp01 = E(unreal.MaterialExpressionClamp, -720, 620)
    clamp01.set_editor_property("min_default", 0.0)
    clamp01.set_editor_property("max_default", 1.0)
    C(dot, "", clamp01, "")        # ★ у Clamp имя входа — ПУСТАЯ строка (проверено пробником)
    inv = E(unreal.MaterialExpressionOneMinus, -580, 620)
    C(clamp01, "", inv, "")        # ★ у OneMinus имя входа — ПУСТАЯ строка
    powr = E(unreal.MaterialExpressionPower, -440, 560)
    C(inv,  "", powr, "Base")
    C(rimP, "", powr, "Exp")       # ★ у Power вход называется «Exp», НЕ «Exponent»
    rim = E(unreal.MaterialExpressionMultiply, -300, 480)
    C(powr, "", rim, "A")
    C(rimI, "", rim, "B")
    # ── Сумма каналов с потолком: amount = min(FillAdd + Rim, MaxAdd) ──
    summ = E(unreal.MaterialExpressionAdd, -160, 200)
    C(fill, "", summ, "A")
    C(rim,  "", summ, "B")
    amt = E(unreal.MaterialExpressionMin, -20, 260)
    C(summ, "", amt, "A")
    C(maxA, "", amt, "B")
    emis = E(unreal.MaterialExpressionMultiply, 140, -100)
    C(col, "", emis, "A")      # выход по умолчанию = RGBA (у VectorParameter выхода «RGB» НЕТ)
    C(amt, "", emis, "B")
    one = E(unreal.MaterialExpressionConstant, 140, 200)
    one.set_editor_property("r", 1.0)
    P(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    P(one,  "", unreal.MaterialProperty.MP_OPACITY)
    m.modify()
    mel.recompile_material(m)
    ok = EAL.save_loaded_asset(m)
    if BAD:
        unreal.log_error("[TGT] НЕ ПОДКЛЮЧЕНО: %s" % BAD)
    unreal.log("[TGT] %s собран, save=%s, битых соединений=%d" % (path, ok, len(BAD)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
