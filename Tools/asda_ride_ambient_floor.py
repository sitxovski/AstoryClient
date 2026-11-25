# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ride_ambient_floor.py — ПОДПОЛ ОСВЕЩЕНИЯ материалам машин пасхалки «Транспорт».
#
# СИМПТОМ (владелец, живой тест): «тачки тёмные».
# ПРИЧИНА. В этом проекте действует аниме-правило «тень НИКОГДА не чёрная»: всем игровым
#   Lit-мастерам добавлен ambient-floor — `Emissive = BaseColor × AmbientFloor` (см.
#   `Tools/asda_add_ambient_floor.py` и разбор регрессии в `Tools/asda_fix_charlit_lighting.py`:
#   именно отсутствие этого подпола давало «персонаж слишком легко затемняется»).
#   Материалы набора Vehicle Variety Pack собраны сторонним автором и подпола НЕ имеют, поэтому
#   на нашей карте машины уходят в черноту.
#   ⚠ Флаги использования у них в порядке (`used_with_skeletal_mesh = True` у всех кузовных,
#   проверено пробой) — дело именно в освещении, а не в usage-флагах.
#
# ЧТО ДЕЛАЕТ (идемпотентно): в каждый Lit-мастер машин добавляет скаляр-параметр `AmbientFloor`
#   и связку `Multiply(BaseColor, AmbientFloor) → Emissive`. Существующие материалы
#   МОДИФИЦИРУЮТСЯ, а не пересоздаются (пересоздание сбросило бы usage-флаги и порвало ссылки
#   инстансов) — тот же приём, что у `asda_add_ambient_floor.py`.
#
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_ride_ambient_floor.py
import traceback
import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

ROOT  = "/Game/VehicleVarietyPack/Materials"
FLOOR = 0.25          # тот же уровень, что у игровых мастеров проекта
PARAM = "AmbientFloor"


def log(m):
    unreal.log("[подпол-машин] %s" % m)


def main():
    done = skipped = 0
    for a in EAL.list_assets(ROOT, True, False):
        path = a.split(".")[0]
        m = EAL.load_asset(path)
        if not isinstance(m, unreal.Material):
            continue                      # инстансы наследуют подпол от мастера
        name = path.rsplit("/", 1)[-1]

        # Уже есть? (идемпотентность) — признак: наш параметр уже в списке скаляров материала,
        #   либо Emissive уже чем-то занят (второй раз подпол не вешаем).
        try:
            names = [str(n) for n in MEL.get_scalar_parameter_names(m)]
        except Exception:
            names = []
        if PARAM in names:
            skipped += 1
            continue
        # ★ Emissive может быть УЖЕ ЗАНЯТ — у кузовов набора там фары/подсветка. Такие материалы
        #   пропускать НЕЛЬЗЯ: кузов и есть главное, что владелец видит тёмным. Поэтому подпол не
        #   заменяет чужую цепь, а ПРИБАВЛЯЕТСЯ к ней через Add.
        emissive_prev = MEL.get_material_property_input_node(m, unreal.MaterialProperty.MP_EMISSIVE_COLOR)

        bc = MEL.get_material_property_input_node(m, unreal.MaterialProperty.MP_BASE_COLOR)
        if not bc:
            skipped += 1
            log("%s: вход BaseColor пуст — пропуск" % name)
            continue

        sp = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -400, 400)
        sp.set_editor_property("parameter_name", PARAM)
        sp.set_editor_property("default_value", FLOOR)
        mul = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -200, 400)
        # Множим ТУ ЖЕ цепь, что идёт в BaseColor — цвет кузова сохраняется, меняется только
        #   гарантированный минимум яркости в тени.
        MEL.connect_material_expressions(bc, "", mul, "A")
        MEL.connect_material_expressions(sp, "", mul, "B")
        if emissive_prev:
            # Складываем: своё свечение материала + наш гарантированный минимум.
            add = MEL.create_material_expression(m, unreal.MaterialExpressionAdd, -80, 400)
            MEL.connect_material_expressions(emissive_prev, "", add, "A")
            MEL.connect_material_expressions(mul, "", add, "B")
            MEL.connect_material_property(add, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        else:
            MEL.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        MEL.recompile_material(m)
        if not EAL.save_loaded_asset(m, False):
            raise RuntimeError("не сохранён %s" % path)
        done += 1
        log("%s: подпол %.2f добавлен" % (name, FLOOR))

    log("ГОТОВО: изменено %d, пропущено %d" % (done, skipped))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[подпол-машин] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
