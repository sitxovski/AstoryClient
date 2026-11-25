# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_pathfinding.py — ПРОВЕРКА поиска пути (UAsdaPathfindLibrary::FindPath) на РЕАЛЬНОМ
#   игровом коде: маршруты по alpen с заведомыми препятствиями + замер производительности.
# Каждый маршрут печатает: число узлов, длину ломаной, отношение к прямой, стоимость, развёрнутые узлы,
#   время поиска и КОНТРОЛЬ ОБХОДА — «узлов в стене» и «перекрытых отрезков» обязаны быть 0.
# Запуск: Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_verify_pathfinding.py
import unreal, time

MAP = "alpen"
# (start_x, start_z, goal_x, goal_z, комментарий) — подобраны так, что ПРЯМАЯ линия перекрыта.
ROUTES = [
    (56, 242, 116, 368, "длинный обход массива скал (105/140 клеток прямой перекрыто)"),
    (266, 323, 166, 316, "обход застройки/воды (73/101)"),
    (100, 309, 197, 258, "диагональ через перешеек (71/110)"),
    (163, 365, 173, 246, "объезд озера с юга на север (67/120)"),
    (324, 184, 384, 166, "короткий обход здания (37/63)"),
    (91, 425, 152, 373, "обход скалы, короткая дистанция (33/81)"),
]

def log(m): unreal.log("[путь-проверка] " + str(m))

def main():
    pkg = "/Game/Map/%s/DA_WalkGrid" % MAP
    grid = unreal.load_asset(pkg)
    if not grid:
        log("НЕТ ассета %s" % pkg); return
    log("сетка %s: %dx%d, cell=%.0f" % (MAP, grid.get_editor_property("width"),
        grid.get_editor_property("height"), grid.get_editor_property("cell_size")))
    lib = unreal.AsdaPathfindLibrary
    def run(sx, sz, gx, gz):
        # Соглашение UE-Python: UFUNCTION с bool-возвратом и out-параметрами отдаёт кортеж out'ов
        #   при true и None при false. None здесь = «маршрута нет».
        r = lib.find_path(grid, unreal.IntPoint(sx, sz), unreal.IntPoint(gx, gz))
        return r[-1] if isinstance(r, tuple) else "МАРШРУТА НЕТ (цель в замкнутой/непроходимой области)"

    # Прогрев: первый вызов строит сетку весов (кэш на карту) — его время меряем отдельно.
    t0 = time.perf_counter()
    run(56, 242, 57, 242)
    log("построение сетки весов (один раз на карту): %.1f мс" % ((time.perf_counter() - t0) * 1000.0))

    bad = 0
    for (sx, sz, gx, gz, note) in ROUTES:
        rep = ""
        for _ in range(5):                     # 5 прогонов подряд (прогретый кэш весов)
            rep = run(sx, sz, gx, gz)
        log("%s  # %s" % (rep, note))
        if ("узлов в стене=0" not in rep) or ("перекрытых отрезков=0" not in rep):
            bad += 1
    log("МАРШРУТОВ С ДЕФЕКТОМ ОБХОДА: %d из %d" % (bad, len(ROUTES)))

    # Цель В СТЕНЕ — маршрут должен строиться к ближайшему проходимому тайлу («цель сдвинута»);
    #   цель в наглухо закрытом углу карты — честный отказ.
    for (gx, gz) in [(70, 250), (0, 0), (300, 300)]:
        log("цель (%d,%d) [проходима=%s]: %s" % (gx, gz, grid.is_tile_walkable(gx, gz), run(56, 242, gx, gz)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
