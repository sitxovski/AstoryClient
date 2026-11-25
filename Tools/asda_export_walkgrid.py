# Copyright (c) 2026 @sitxovski (Discord)
# asda_export_walkgrid.py — экспорт сетки ПРОХОДИМОСТИ и ВЫСОТ каждой карты из <map>.TER в UE-ассет
#   UAsdaWalkGrid (/Game/Map/<map>/DA_WalkGrid). Editor-Friendly, игра ссылается по имени карты.
# Формат .TER (walkable_grid_re.md / CMap_LoadTER@0x1133580): magic 0x20050802, blobSize@4, tableSize@8,
#   header 0x2F6C@12 (tileCountX@12, tileCountY@16, cellSize@20, nodeCount@12+0x2a0c), blob tableSize,
#   затем ТЕЛО из 7 массивов длины N (N = число УЗЛОВ квадродерева, 349504 = 512·512 листьев + 87360
#   внутренних; листья идут ПЕРВЫМИ в row-major, поэтому индекс тайла = tz·512+tx):
#     [0] u32 флаги (4·N)   — проходимость = БАЙТ1; БЛОК при {1,2,129,130,65,193}; бит 0x80 = город
#     [1] f32×4 высоты углов (16·N) — порядок (minX,maxZ),(minX,minZ),(maxX,minZ),(maxX,maxZ)
#     [2] f32 центр Y (4·N), [3] f32 (4·N)
#   Инвариант: 12 + 0x2F6C + tableSize + 7·N·4 == размер файла (сверено на 22 картах).
# ВЫСОТА: углы соседних тайлов совпадают бит-в-бит (шов = 0 на всех картах) → храним компактную
#   вершинную сетку (W+1)×(H+1). Эталон интерполяции — terrainInterpHeightAtXY@0x1130F90 (см. AsdaWalkGrid.h).
# Запуск: headless. Опц. ASDA_MAPS="alpen||rein".
import os, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_maps as _MAPS
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
import asda_ter_height as _TER      # ★ единый разбор .TER (были свои TER_DIR/MAP_TER/ter_path + шапка)
import asda_wat as _WAT             # ★ вода .WAT → маска по тайлам (эталонный вопрос «точка над водой»)

import asda_login_guard as _LG      # ★ 16.09.2026 (обратный обход savedir14): единый гейт сцены логина

TER_DIR = _TER.TER_DIR
BLOCK = _TER.BLOCK_VALUES           # {1,2,129,130,65,193} — значения байта1, означающие БЛОК
EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
def log(m): unreal.log("[walkgrid] " + str(m))

MAP_TER = _TER.MAP_TER              # карты с другим регистром имени файла .TER
# ★ Список карт — ЕДИНЫЙ источник asda_maps.py (все 22 карты порта).
ALL_MAPS = _MAPS.ALL_MAPS
ter_path = _TER.ter_path

def parse_walk_bits(path):
    # ★ Шапка/офсеты/инвариант/вершинная сетка — asda_ter_height.TerFile (было своё дословное тело).
    #   Поведение на плохом файле сохранено: не исключение, а лог + None.
    try:
        t = _TER.TerFile(path)
    except ValueError as e:
        log("  %s" % e); return None
    err = t.body_error()
    if err:
        log("  %s" % err); return None
    tcx, tcy, cellSize = t.width, t.height, t.cell
    tileCountReal = t.tile_count
    # бит-пак: бит (tz*W+tx) = 1 если проходимо
    # ★ ВТОРОЙ бит-пак — БЕЗОПАСНАЯ (ГОРОДСКАЯ) ЗОНА: бит 0x80 ТОГО ЖЕ байта1 (эталон
    #   controlBar_SlotIsActive & 0x80 → миникарта, городской BGM, безоружные анимации idle/бега).
    nbytes = (tileCountReal + 7) // 8
    bits = bytearray(nbytes)
    peace = bytearray(nbytes)
    walk = 0; npeace = 0
    for i, byte1 in enumerate(t.tile_attr_byte1()):
        if byte1 not in BLOCK:
            bits[i >> 3] |= (1 << (i & 7)); walk += 1
        if byte1 & _TER.PEACE_BIT:
            peace[i >> 3] |= (1 << (i & 7)); npeace += 1
    # ★ МАТЕРИАЛ ПОВЕРХНОСТИ — БАЙТ 0 того же флага: следы и звук шагов
    #   (0 пыль, 1 вода, 2/3 свои звуки, 4 как вода без всплеска). Байт на тайл, как есть.
    steps = bytearray(t.tile_step_material())
    nsteps = sum(1 for v in steps if v)
    # ── ВЫСОТЫ: 4 f32 на тайл → вершинная сетка (tcx+1)×(tcy+1) ──
    _VW, vh = t.vertex_grid()
    return tcx, tcy, bits, walk, tileCountReal, peace, npeace, cellSize, vh, steps, nsteps

def make_asset(mp, tcx, tcy, bits, walk, total, peace, npeace, cellSize, vh, steps, nsteps, water, nwater, nbodies):
    asset = _MAPS.map_asset_name(mp)
    pkg_dir = "/Game/Map/%s" % asset
    pkg = pkg_dir + "/DA_WalkGrid"
    # Ассет уже есть — ОБНОВЛЯЕМ поля на месте (пересоздание ломается на живых ссылках из уровней).
    da = unreal.load_asset(pkg) if EAL.does_asset_exist(pkg) else None
    if not da:
        fac = unreal.DataAssetFactory()
        fac.set_editor_property("data_asset_class", unreal.AsdaWalkGrid)
        da = ATOOLS.create_asset("DA_WalkGrid", pkg_dir, unreal.AsdaWalkGrid, fac)
    if not da:
        log("  создать ассет НЕ удалось: %s" % pkg); return False
    da.set_editor_property("width", tcx)
    da.set_editor_property("height", tcy)
    da.set_editor_property("bits", list(bits))
    da.set_editor_property("peace_bits", list(peace))
    da.set_editor_property("step_materials", list(steps))
    da.set_editor_property("water_bits", list(water))
    da.set_editor_property("cell_size", float(cellSize))
    da.set_editor_property("vert_heights", vh)
    da.set_editor_property("height_min", float(min(vh)))
    da.set_editor_property("height_max", float(max(vh)))
    EAL.save_loaded_asset(da)
    log("  %s: DA_WalkGrid %dx%d, проходимо %d/%d (%.1f%%), безопасная зона %d (%.1f%%), "
        "высоты %d вершин (шаг %.0f, Y %.0f..%.0f), материал шага задан у %d тайлов %s, "
        "вода: %d тел, накрыто %d тайлов (%.1f%%)"
        % (mp, tcx, tcy, walk, total, 100.0*walk/total, npeace, 100.0*npeace/total,
           len(vh), cellSize, min(vh), max(vh), nsteps,
           sorted(set(v for v in steps if v)) if nsteps else "",
           nbodies, nwater, 100.0*nwater/total))
    return True

def main():
    only = os.environ.get("ASDA_MAPS", "")
    maps = [m.strip() for m in only.split("||")] if only else ALL_MAPS
    ok = miss = 0
    for mp in maps:
        p = ter_path(mp)
        if not p:
            log("нет .TER: %s" % mp); miss += 1; continue
        asset = _MAPS.map_asset_name(mp)
        # ★ 16.09.2026 (обратный обход ревизии savedir14): make_asset создаёт и СОХРАНЯЕТ ассет
        #   /Game/Map/<карта>/DA_WalkGrid — гейт сцены логина по КОРНЮ карты, ДО создания ассета.
        #   Имя карты приходит из ASDA_MAPS и списком карт не ограничено.
        if _LG.login_map_write_blocked("/Game/Map/%s" % asset, "asda_export_walkgrid.py", quiet=True):
            miss += 1; continue
        if not EAL.does_asset_exist(_U.level_path(mp)):
            log("нет уровня карты: %s (пропуск)" % mp); miss += 1; continue
        r = parse_walk_bits(p)
        if not r: miss += 1; continue
        tcx, tcy, bits, walk, total, peace, npeace, cellSize, vh, steps, nsteps = r
        # ★ ВОДА: та же карта, файл .WAT рядом с .TER. Маска «тайл накрыт водной геометрией» —
        #   заранее посчитанный ответ на эталонный вопрос sub_926A07 (важен горизонтальный охват).
        # Имя папки и файла у части карт различаются (см. _TER.MAP_TER: значение — пара).
        _folder, _terfile = _TER.MAP_TER.get(mp, (mp, mp + ".TER"))
        water, nwater, nbodies = _WAT.water_bits(TER_DIR, _folder, tcx, tcy, cellSize,
                                                 _terfile[:-4])
        if make_asset(mp, tcx, tcy, bits, walk, total, peace, npeace, cellSize, vh, steps, nsteps,
                      water, nwater, nbodies): ok += 1
        else: miss += 1
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass
    log("ГОТОВО: экспортировано %d, пропущено %d" % (ok, miss))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
