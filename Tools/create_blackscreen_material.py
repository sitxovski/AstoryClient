# Copyright (c) 2026 @sitxovski (Discord)
# create_blackscreen_material.py — материал blackscreen с ТОЧНЫМ блендом эталона (RE render_core.c:820):
#   ALPHABLEND: SRCBLEND=SRCALPHA, DESTBLEND=INVSRCALPHA, srcAlpha 0.85, ZWRITE OFF, unlit, чёрный.
#   = UE Translucent + Unlit + двусторонний + Emissive(0,0,0) + Opacity 0.85. Наш импорт потерял этот бленд;
#   параметры НЕ выдуманы — подтверждены реверсом. Запуск: run_headless.bat create_blackscreen_material.py
import unreal

PKG, NAME = "/Game/Effects", "M_AsdaBlackScreen"
path = "%s/%s" % (PKG, NAME)
EAL = unreal.EditorAssetLibrary


MEL = unreal.MaterialEditingLibrary


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    # ★★★ ЗАЩИТА СЦЕНЫ ЛОГИНА (15.09.2026, Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 13): удаляет, пересоздаёт и сохраняет ТОЛЬКО /Game/Effects/M_AsdaBlackScreen —
    #   материал затемнения, его грузит лишь AsdaLoginGameMode.cpp.
    #   Запуск только с явного разрешения владельца и ASDA_ALLOW_LOGIN_WRITE=1 (иначе стоп, код 3).
    import os as _os_lg, sys as _sys_lg
    _sys_lg.path.insert(0, _os_lg.path.dirname(_os_lg.path.abspath(__file__)))   # Tools — для asda_login_guard
    from asda_login_guard import forbid_login_write
    forbid_login_write("create_blackscreen_material.py", "Effects/M_AsdaBlackScreen (удаление, пересоздание, сохранение)")
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)     # ZWRITE off + без освещения
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)           # SRCALPHA/INVSRCALPHA
    mat.set_editor_property("two_sided", True)
    col = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, -50)
    col.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 0.0, 1.0))         # чёрный (dst = backdrop·(1-a))
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    op = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 120)
    op.set_editor_property("r", 0.85)                                                    # srcAlpha 0.85 (эталон)
    MEL.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(mat)
    EAL.save_asset(path)
    unreal.log("[blackscreen] создан %s (translucent/unlit/two-sided, opacity 0.85 = бленд эталона)" % path)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
