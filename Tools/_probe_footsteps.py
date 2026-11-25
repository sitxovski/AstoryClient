# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""_probe_footsteps.py — проверка данных для следов и звука шагов.

Смотрит НЕ код, а то, чем код кормится: сохранились ли в сетках карт материал поверхности
(байт 0 флага `.TER`) и маска воды (растеризованная геометрия `.WAT`), лежат ли на месте
четыре звука шагов и два эффекта следа. Живого персонажа для этого не нужно.

Запуск: headless через Tools/run_headless_safe.py.
"""
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import unreal
import asda_maps as _MAPS

EAL = unreal.EditorAssetLibrary


def log(m):
    unreal.log("[шаги] " + str(m))


def main():
    log("═══ данные для следов и звука шагов ═══")

    # ── 1. Звуки: четыре варианта из эталонной таблицы материалов ──
    ok_snd = 0
    for n in ("S_c_run0", "S_c_run1", "S_c_run2", "S_c_run3"):
        p = "/Game/AsdaAssets/Sound/%s.%s" % (n, n)
        exists = EAL.does_asset_exist(p)
        ok_snd += 1 if exists else 0
        log("  звук %-10s %s" % (n, "есть" if exists else "НЕТ"))

    # ── 2. Эффекты следа ──
    ok_fx = 0
    for n, what in (("BP_Fx_eff_1_char_99_move01_a_00", "пыль (материал 0)"),
                    ("BP_Fx_eff_1_char_99_move02_a_00", "всплеск (материал 1)")):
        p = "/Game/AsdaAssets/Effects/%s.%s" % (n, n)
        exists = EAL.does_asset_exist(p)
        ok_fx += 1 if exists else 0
        log("  эффект %-34s %-22s %s" % (n, what, "есть" if exists else "НЕТ"))

    # ── 3. Сетки карт: материал поверхности и вода ──
    log("  ── сетки карт ──")
    nmaps = nmat = nwat = 0
    for mp in _MAPS.ALL_MAPS:
        p = _MAPS.map_asset_name(mp)
        path = "/Game/Map/%s/DA_WalkGrid.DA_WalkGrid" % p if not p.startswith("/") else p
        if not EAL.does_asset_exist(path):
            # раскладка проекта могла отличаться — пробуем как в экспортёре
            continue
        da = EAL.load_asset(path)
        if not da:
            continue
        nmaps += 1
        steps = da.get_editor_property("step_materials")
        water = da.get_editor_property("water_bits")
        w = da.get_editor_property("width")
        h = da.get_editor_property("height")
        n_mat = sum(1 for v in steps if v) if steps else 0
        n_wat = 0
        if water:
            for b in water:
                n_wat += bin(b).count("1")
        if n_mat:
            nmat += 1
        if n_wat:
            nwat += 1
        if n_mat or n_wat:
            kinds = sorted(set(v for v in steps if v)) if steps else []
            log("    %-18s материал у %6d тайлов %-6s вода у %6d (%.1f%%)"
                % (mp, n_mat, str(kinds), n_wat, 100.0 * n_wat / max(1, w * h)))

    log("─── ИТОГ: звуков %d/4, эффектов %d/2, сеток прочитано %d, "
        "с материалом %d, с водой %d ───" % (ok_snd, ok_fx, nmaps, nmat, nwat))
    log("ЗАВЕРШЕНО")


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
