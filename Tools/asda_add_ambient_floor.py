# Copyright (c) 2026 @sitxovski (Discord)
# Ambient-floor в Lit-мастера: Emissive = BaseColor·AmbientFloor (скаляр-параметр).
#   ★★★ С 2026-09-03 (правка Д-3 аудита света) FLOOR = 0.0: пол 0.25 был «двойным светом» поверх UE-света
#   (тень никогда темнее ~0.5·tex, ночные карты не тёмные). Узел остаётся РЫЧАГОМ (дефолт правится на
#   мастере/MIC), честный ambient — per-map SkyLight (asda_convert_map_lit.py). Живые ассеты чинит
#   asda_fix_lit_ambient_floor.py (дефолт в существующем узле, без повторного добавления узлов).
#   ⚠ Повторный запуск ЭТОГО скрипта ДОБАВИТ второй узел AmbientFloor (дубль параметра) — не запускать
#   на мастере, где узел уже есть; он нужен только при сборке мастера с нуля.
#   Прежнее обоснование (0.25): аниме-правило «тень никогда не чёрная» (оригинал-unlit имел пол 0.37·tex).
#   Floor даёт гарантированный минимум ЦВЕТА поверхности независимо от NdotL/skylight-капчера.
#   МОДИФИЦИРУЕТ существующие мастера (НЕ пересоздаёт: 6600+ MIC ссылаются; delete+recreate сбрасывает usage-флаги).
#   Запуск: run_headless.bat asda_add_ambient_floor.py
import unreal
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
FLOOR = 0.0   # = asda_build_materials_lit.AMBIENT_FLOOR (Д-3)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for name in ("M_AsdaObjectLit", "M_AsdaTerrainLit"):
        p = "/Game/Materials/" + name
        m = EAL.load_asset(p)
        if not m: unreal.log_warning("[FLOOR] нет %s" % name); continue
        # вход BaseColor (существующая цепь bc = tex·vtxColor / террейн-микс)
        bc_node = MEL.get_material_property_input_node(m, unreal.MaterialProperty.MP_BASE_COLOR)
        if not bc_node:
            unreal.log_warning("[FLOOR] %s: вход BaseColor пуст" % name); continue
        # AmbientFloor-параметр + Multiply(BaseColor, Floor) → Emissive
        sp = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, 550, 400)
        sp.set_editor_property("parameter_name", "AmbientFloor")
        sp.set_editor_property("default_value", FLOOR)
        mul = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, 700, 300)
        MEL.connect_material_expressions(bc_node, "", mul, "A")   # bc-цепь (Multiply tex·vtxColor) — дефолт-выход
        MEL.connect_material_expressions(sp, "", mul, "B")
        MEL.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        m.modify()
        ok = EAL.save_loaded_asset(m)
        unreal.log("[FLOOR] %s: AmbientFloor=%.2f save=%s" % (name, FLOOR, ok))
    unreal.log("[FLOOR] done")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
