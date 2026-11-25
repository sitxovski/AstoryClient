# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_cloud_mesh_mat.py — ГЕНЕРАТОР мастера 3D-облаков `/Game/Materials/M_AsdaCloudMesh`
#   (CloudField: `AAsdaCloudFieldActor` создаёт MID от пер-карточного MIC `M_AsdaCloudMesh_<карта>`,
#   `asda_percloud_mics.py`; актор в рантайме пишет только `Opacity`).
#
# ★ ПОЧЕМУ ОТДЕЛЬНЫЙ ФАЙЛ. Мастер собран 2026-07-28 скриптом из scratchpad-каталога другой сессии
#   (`cloud_mat_v3.py`, лог `Reports/headless_cm3.log`) — файл не сохранился в Tools. Граф ниже
#   ВОССТАНОВЛЕН ПО ЖИВОМУ АССЕТУ (дамп `Reports/audit_2026-09-02/_sky_dump_before.txt`,
#   03.09.2026): те же классы узлов, имена/дефолты параметров, канал маски, координаты узлов и
#   входы/выходы — ровно 12 ПОДКЛЮЧЁННЫХ узлов. Три узла того дампа (ShadeColor (0.62,0.67,0.78),
#   Tint (0.96,0.97,1) и Multiply с пустыми входами) — ФАНТОМЫ более ранней сборки, оставшиеся
#   из-за бага UE 5.8 `DeleteAllMaterialExpressions` (см. `asda_mat_graph.py`); они ни к чему не были
#   подключены, но КЭШ параметров отдавал их дефолты (ShadeColor/Tint «по умолчанию» врали).
#
# ГРАФ (unlit, translucent):
#   Emissive = lerp(ShadeColor, Tint, clamp(VertexNormalWS.z·0.5 + 0.5)) × Intensity   («AO» по нормали:
#              верх облака = Tint, низ = ShadeColor)
#   Opacity  = Opacity (скаляр 0.85; актор перебивает MID-ом в диапазоне OpacityMin..OpacityMax)
#
# Запуск (СКРЫТО, редактор закрыт):
#   python Tools/run_headless_safe.py Tools/asda_build_cloud_mesh_mat.py --marker CLOUDMESH-FINISHED
import os
import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal
import asda_mat_graph as _MG   # ★ чистка графа до пустого + контроль после сборки (баг UE 5.8 DeleteAllMaterialExpressions)

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
MASTER = "/Game/Materials/M_AsdaCloudMesh"

# Ровно столько узлов строит build_master(): Tint, ShadeColor, VertexNormalWS, ComponentMask, Constant,
#   Multiply, Add, Clamp, LinearInterpolate, Intensity, Multiply, Opacity = 12. При изменении графа — обновить.
MASTER_EXPRESSIONS = 12

# Дефолты параметров — значения ПОДКЛЮЧЁННЫХ узлов живого ассета (не фантомов).
TINT_DEFAULT = (0.97, 0.98, 1.0, 1.0)
SHADE_DEFAULT = (0.55, 0.62, 0.75, 1.0)
INTENSITY_DEFAULT = 1.0
OPACITY_DEFAULT = 0.85


def _connect(a, ao, b, bi):
    """connect_material_expressions с проверкой: тихое False здесь недопустимо (обрыв цепочки)."""
    if not MEL.connect_material_expressions(a, ao, b, bi):
        raise RuntimeError("[CLOUDMESH] не соединилось %s.%s -> %s.%s" % (a.get_name(), ao, b.get_name(), bi))


def build_master(log=None):
    """Пересобрать M_AsdaCloudMesh «на месте» (без delete_asset — иначе 18 MIC потеряют parent)."""
    log = log or unreal.log
    name = MASTER.rsplit("/", 1)[-1]
    if EAL.does_asset_exist(MASTER):
        m = EAL.load_asset(MASTER)
        _MG.clear_expressions(m, log=log, tag="[CLOUDMESH]")   # ★ в цикле до пустого
    else:
        m = AT.create_asset(name, MASTER.rsplit("/", 1)[0], unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    # ★ Доводка 05.09 (setA_fix_report.md §6 находка, setB_fix_report.md): «Apply Fogging» = OFF. После снятия cutoff тумана (сет A)
    #   3D-облака CloudField (z≈35 000, дальше fogEnd) красились туманом целиком (на chess еле видны, на nightvalley — цвет тумана).
    #   Транслюцент с bUseTranslucencyVertexFog=false сценовый туман не получает (MaterialShared.cpp ShouldApplyFogging) — как купол.
    m.set_editor_property("use_translucency_vertex_fog", False)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", False)

    def expr(cls, x, y):
        return MEL.create_material_expression(m, cls, x, y)

    tint = expr(unreal.MaterialExpressionVectorParameter, -800, 0)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(*TINT_DEFAULT))
    shade = expr(unreal.MaterialExpressionVectorParameter, -800, 200)
    shade.set_editor_property("parameter_name", "ShadeColor")
    shade.set_editor_property("default_value", unreal.LinearColor(*SHADE_DEFAULT))
    nrm = expr(unreal.MaterialExpressionVertexNormalWS, -800, 430)
    mz = expr(unreal.MaterialExpressionComponentMask, -650, 430)          # канал Z (верх/низ)
    mz.set_editor_property("r", False); mz.set_editor_property("g", False)
    mz.set_editor_property("b", True);  mz.set_editor_property("a", False)
    _connect(nrm, "", mz, "")
    half = expr(unreal.MaterialExpressionConstant, -650, 560)
    half.set_editor_property("r", 0.5)
    mul = expr(unreal.MaterialExpressionMultiply, -500, 450)              # nz·0.5
    _connect(mz, "", mul, "A"); _connect(half, "", mul, "B")
    add = expr(unreal.MaterialExpressionAdd, -350, 450)                   # +0.5
    _connect(mul, "", add, "A"); _connect(half, "", add, "B")
    cl = expr(unreal.MaterialExpressionClamp, -200, 450)
    _connect(add, "", cl, "")
    lp = expr(unreal.MaterialExpressionLinearInterpolate, -200, 150)      # низ→верх
    _connect(shade, "", lp, "A"); _connect(tint, "", lp, "B"); _connect(cl, "", lp, "Alpha")
    inten = expr(unreal.MaterialExpressionScalarParameter, -200, 320)
    inten.set_editor_property("parameter_name", "Intensity")
    inten.set_editor_property("default_value", INTENSITY_DEFAULT)
    em = expr(unreal.MaterialExpressionMultiply, -50, 200)
    _connect(lp, "", em, "A"); _connect(inten, "", em, "B")
    MEL.connect_material_property(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    op = expr(unreal.MaterialExpressionScalarParameter, -200, 650)
    op.set_editor_property("parameter_name", "Opacity")
    op.set_editor_property("default_value", OPACITY_DEFAULT)
    MEL.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)

    # ★ Контроль ДО сохранения: ровно MASTER_EXPRESSIONS узлов и без дублей имён параметров,
    #   иначе RuntimeError и ассет не сохраняется.
    _MG.check_graph(m, expected=MASTER_EXPRESSIONS, log=log, tag="[CLOUDMESH]")
    MEL.recompile_material(m); m.modify(); EAL.save_loaded_asset(m)
    node = MEL.get_material_property_input_node(m, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    opn = MEL.get_material_property_input_node(m, unreal.MaterialProperty.MP_OPACITY)
    log("[CLOUDMESH] мастер %s: Emissive=%s Opacity=%s" % (
        name, node.get_class().get_name() if node else "ОБРЫВ!", opn.get_class().get_name() if opn else "ОБРЫВ!"))
    return m


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    build_master()
    unreal.log("=== ЗАВЕРШЕНО (CLOUDMESH-FINISHED) ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
