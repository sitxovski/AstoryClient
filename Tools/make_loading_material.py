# Copyright (c) 2026 @sitxovski (Discord)
# M_LoadingBG — UI-материал фона загрузчика: текстура-параметр Tex → Final Color (UV 0..1 = вся геометрия,
#   гарантированно заполняет виджет без aspect-логики Slate-brush). + дамп реальных размеров загрузочных текстур.
#   Запуск: run_headless.bat make_loading_material.py
import unreal


# --- материал ---
ui_dir = "/Game/AsdaAssets/UI/Dev"
mat_path = ui_dir + "/M_LoadingBG"

at = unreal.AssetToolsHelpers.get_asset_tools()

MEL = unreal.MaterialEditingLibrary


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    # --- дамп размеров (диагностика) ---
    for p in ["/Game/AsdaAssets/Textures/Maps/load/ad_img000",
              "/Game/AsdaAssets/Textures/Maps/alpen/load/e_Alpen",
              "/Game/AsdaAssets/Textures/Maps/load/Start_logo"]:
        t = unreal.load_asset(p)
        if t:
            try:
                isz = t.get_editor_property("imported_size")
                unreal.log(f"[texdump] {p}: runtime={t.blueprint_get_size_x()}x{t.blueprint_get_size_y()} imported={isz.x}x{isz.y}")
            except Exception as e:
                unreal.log(f"[texdump] {p}: {t.blueprint_get_size_x()}x{t.blueprint_get_size_y()} (imported n/a: {e})")
        else:
            unreal.log_warning(f"[texdump] НЕ загружено: {p}")
    if unreal.EditorAssetLibrary.does_asset_exist(mat_path):
        unreal.EditorAssetLibrary.delete_asset(mat_path)
    mat = at.create_asset("M_LoadingBG", ui_dir, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    ts = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -400, 0)
    ts.set_editor_property("parameter_name", "Tex")
    # дефолтная текстура, чтобы материал компилился
    deftex = unreal.load_asset("/Game/AsdaAssets/Textures/Maps/load/ad_img000")
    if deftex:
        ts.set_editor_property("texture", deftex)
    # РЕВЕРС uiRenderLoadingBar@0x97A6B0 (ui_core.c:1496-1513): quad UV V идёт 0..0.75 — верхние 768/1024
    #   текстуры (контент под 4:3) на весь экран, нижние 25% (белый паддинг) обрезаются. Задаём VTiling=0.75.
    uv = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -700, 0)
    uv.set_editor_property("u_tiling", 1.0)
    uv.set_editor_property("v_tiling", 0.75)
    MEL.connect_material_expressions(uv, "", ts, "UVs")
    MEL.connect_material_property(ts, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(mat_path)
    unreal.log("[loading_material] M_LoadingBG создан")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
