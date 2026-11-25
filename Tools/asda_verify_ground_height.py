# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_ground_height.py — ДОКАЗАТЕЛЬСТВО правильности высоты земли.
# Сверяет три величины в одних и тех же точках маршрута:
#   ОРИГИНАЛ  — эталонная формула terrainInterpHeightAtXY@0x1130F90, считанная прямо из <map>.TER;
#   ПОРТ ДО   — высота трейса по РЕНДЕР-мешу террейна (что возвращал прежний AsdaGround::GroundZ):
#               та же треугольная интерполяция, но по вершинам .TVB из export/maps/<map>.mapraw
#               (диагональ меша совпадает с эталонной — проверено по индексному буферу);
#   ПОРТ ПОСЛЕ— UAsdaWalkGrid::GetGroundY (боевой C++ порта) через UFUNCTION.
# Запуск: headless (UnrealEditor-Cmd -ExecutePythonScript). Опц. ASDA_VERIFY_MAP=alpen.
# Отчёт: Reports/ground_height_table.md
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import os, math, unreal
import asda_ter_height as _TER    # ★ единый разбор .TER (был свой ter_vertgrid + свой путь к файлу)
import asda_paths as _P

MAP      = os.environ.get("ASDA_VERIFY_MAP", "alpen")
OUT      = os.path.join(_P.RE_REPORTS, "ground_height_table.md")
def log(m): unreal.log("[ground-verify] " + str(m))

# ── .TER: вершинная сетка высот (как экспортёр) ────────────────────────────────────────────────
def ter_vertgrid(path):
    t = _TER.TerFile(path)         # magic-проверка внутри (был assert)
    vw, v = t.vertex_grid()
    return t.width, t.height, t.cell, v

# ── .mapraw: вершины РЕНДЕР-меша террейна (та же решётка 513×513) ──────────────────────────────
# ★ Ридер .mapraw — ЕДИНЫЙ модуль asda_mapraw.py.
#   Здесь строки всегда читались СЫРЫМИ байтами (без decode) — это метод sb()
#   единого ридера; локальный подкласс-переопределение больше не нужен.
from asda_mapraw import R


def mapraw_vertgrid(path, W, H, cell):
    r = R(open(path, "rb").read()); r.o = 4
    r.u32(); r.sb(); r.u32(); r.f32(); r.f32(); r.farr(3); r.farr(3); r.farr(3)
    pc = r.u32(); ic = r.u32(); r.skip(2 * ic)
    VW = W + 1
    v = [None] * (VW * (H + 1))
    for _ in range(pc):
        n = r.u32()
        if n:
            pos = r.farr(n * 3); r.farr(n * 2); r.farr(n * 2); r.skip(n * 4)
        else:
            pos = ()
        r.u32(); r.u32(); r.f32(); r.f32(); r.sb(); r.f32(); r.sb(); r.f32()
        for i in range(n):
            x, y, z = pos[i*3], pos[i*3+1], pos[i*3+2]
            vx = int(round(x / cell)); vz = int(round(z / cell))
            if 0 <= vx <= W and 0 <= vz <= H:
                v[vz * VW + vx] = y
    return v

# ── Эталонная интерполяция (terrainInterpHeightAtXY): диагональ (minX,minZ)—(maxX,maxZ), v<=u ──
def sample(v, W, H, cell, x, z):
    if x < 0.0: x = cell
    if x > (W - 1) * cell: x = (W - 1) * cell
    if z < 0.0: z = cell
    if z > (H - 1) * cell: z = (H - 1) * cell
    tx = min(max(int(x / cell), 0), W - 1); tz = min(max(int(z / cell), 0), H - 1)
    VW = W + 1
    h00 = v[tz*VW + tx]; h10 = v[tz*VW + tx + 1]
    h01 = v[(tz+1)*VW + tx]; h11 = v[(tz+1)*VW + tx + 1]
    if h00 is None or h10 is None or h01 is None or h11 is None: return None
    u = (x - tx * cell) / cell; w = (z - tz * cell) / cell
    return (h00 + (h10-h00)*u + (h11-h10)*w) if w <= u else (h00 + (h01-h00)*w + (h11-h01)*u)

# UFUNCTION с bool-возвратом и out-параметром: UE-Python может вернуть кортеж или одно число.
def gy(da, gx, gz):
    r = da.get_ground_y(gx, gz)
    return float(r[-1]) if isinstance(r, tuple) else float(r)

def main():
    W, H, cell, ter = ter_vertgrid(TER)
    ren = mapraw_vertgrid(MAPRAW, W, H, cell)
    asset = "/Game/Map/%s/DA_WalkGrid.DA_WalkGrid" % MAP.replace(" ", "_")
    da = unreal.load_asset(asset)
    if not da: log("НЕТ ассета %s" % asset); return
    log("сетка %dx%d шаг %.0f; ассет: вершин %d" % (W, H, cell, len(da.get_editor_property("vert_heights"))))

    # ── Маршруты ──
    routes = []
    # 1) через ПРОБЛЕМНУЮ зону: диагональ через вершину максимального расхождения (11000, 39100)
    routes.append(("Проблемный склон/платформа: (9800,38200) → (12200,40000)", (9800.0, 38200.0), (12200.0, 40000.0), 240))
    # 2) РОВНЫЙ/обычный участок для контроля регрессии
    routes.append(("Контроль (обычный рельеф): (25000,25000) → (28000,27500)", (25000.0, 25000.0), (28000.0, 27500.0), 240))

    lines = []
    lines.append("# Высота земли: оригинал vs порт (карта %s)\n" % MAP)
    lines.append("Источники: ОРИГИНАЛ = `terrainInterpHeightAtXY@0x1130F90` по `%s.TER`; " % MAP)
    lines.append("ДО = трейс по рендер-мешу террейна (.TVB через mapraw); ")
    lines.append("ПОСЛЕ = `UAsdaWalkGrid::GetGroundY` (боевой код порта).\n")
    summary = []
    for title, a, b, npts in routes:
        maxbefore = maxafter = 0.0; rows = []
        for i in range(npts + 1):
            t = i / float(npts)
            gx = a[0] + (b[0] - a[0]) * t
            gz = a[1] + (b[1] - a[1]) * t
            orig = sample(ter, W, H, cell, gx, gz)
            before = sample(ren, W, H, cell, gx, gz)
            after = gy(da, gx, gz)
            db = (before - orig) if before is not None else float('nan')
            dafter = after - orig
            maxbefore = max(maxbefore, abs(db)); maxafter = max(maxafter, abs(dafter))
            rows.append((gx, gz, orig, before, after, db, dafter))
        summary.append((title, npts + 1, maxbefore, maxafter))
        lines.append("\n## %s\n" % title)
        lines.append("Точек: %d (шаг %.1f ед., границы тайлов пересекаются)\n" % (npts + 1, math.hypot(b[0]-a[0], b[1]-a[1]) / npts))
        lines.append("\n| # | тайл | game X | game Z | ОРИГИНАЛ | ПОРТ ДО | ПОРТ ПОСЛЕ | Δ до | Δ после |")
        lines.append("|--:|:--|--:|--:|--:|--:|--:|--:|--:|")
        for i, (gx, gz, o, bf, af, db, da_) in enumerate(rows):
            lines.append("| %d | %d,%d | %.1f | %.1f | %.2f | %.2f | %.2f | %+.2f | %+.2f |"
                         % (i, int(gx/cell), int(gz/cell), gx, gz, o, bf, af, db, da_))
        lines.append("\n**Максимум |Δ|: ДО %.2f, ПОСЛЕ %.4f**\n" % (maxbefore, maxafter))
        log("%s: max|Δ| до %.2f, после %.4f" % (title, maxbefore, maxafter))

    # ── Массовая сверка порта с эталоном по всей карте ──
    import random
    rnd = random.Random(1234)
    worst = 0.0
    for _ in range(20000):
        gx = rnd.uniform(0.0, (W - 1) * cell); gz = rnd.uniform(0.0, (H - 1) * cell)
        o = sample(ter, W, H, cell, gx, gz)
        worst = max(worst, abs(gy(da, gx, gz) - o))
    lines.append("\n## Массовая сверка порта с эталоном\n")
    lines.append("20000 случайных точек по всей карте: **максимум |ПОСЛЕ − ОРИГИНАЛ| = %.6f**\n" % worst)
    log("массовая сверка 20000 точек: max|Δ| = %.6f" % worst)

    lines.append("\n## Итог\n")
    lines.append("| маршрут | точек | max &#124;Δ&#124; ДО | max &#124;Δ&#124; ПОСЛЕ |")
    lines.append("|:--|--:|--:|--:|")
    for t, n, mb, ma in summary:
        lines.append("| %s | %d | %.2f | %.4f |" % (t, n, mb, ma))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "w", encoding="utf-8").write("\n".join(lines) + "\n")
    log("отчёт: %s" % OUT)


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global MAPRAW, TER
    TER      = _TER.ter_path(MAP) or os.path.join(_TER.TER_DIR, MAP, "%s.TER" % MAP)
    MAPRAW   = _P.mapraw(MAP)
    main()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
