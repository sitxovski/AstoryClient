# Copyright (c) 2026 @sitxovski (Discord)
# asda_export_swimbits.py — запись маски ОТКРЫТОЙ ВОДЫ (плавание, 28.10.2026) в `DA_WalkGrid.SwimBits` карт.
#   Источник — `Tools/data/water_swim/<карта>.bits` (генератор `asda_water_swim.py --write`; тот же список тайлов снят с бита 0
#   `.sim` GameServer). Пишется ТОЛЬКО поле `swim_bits` (точечная запись, прочие поля ассета не трогаются); карты без открытой
#   воды пропускаются (поле остаётся пустым). Проверка: после сохранения поле перечитывается и сверяется с файлом побайтно.
#   ★ 31.10.2026: маска БЕЗ открытых тайлов при НЕПУСТОМ поле ассета (карта исключена генератором — `EXCLUDE`, поле битвы 19)
#   — поле ОЧИЩАЕТСЯ (пустой массив = «вода блок», как до 28.10); пустое поле и пустая маска — пропуск, как прежде.
# Запуск: headless (run_headless_safe.py). Опц. ASDA_MAPS="alpen||rein". Маркер успеха — «ЗАВЕРШЕНО: swimbits».
import os, sys, unreal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_maps as _MAPS
import asda_login_guard as _LG      # единый гейт сцены логина (карта из ASDA_MAPS ни с чем не сверяется)

EAL = unreal.EditorAssetLibrary
DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "water_swim")
def log(m): unreal.log("[swimbits] " + str(m))


def main():
    only = os.environ.get("ASDA_MAPS", "")
    maps = [m.strip() for m in only.split("||")] if only else _MAPS.ALL_MAPS
    ok = skip = bad = 0
    for mp in maps:
        p = os.path.join(DATA, mp + ".bits")
        if not os.path.isfile(p):
            log("%s: нет %s — пропуск" % (mp, p)); skip += 1; continue
        data = open(p, "rb").read()
        n = sum(bin(b).count("1") for b in data)
        pkg = "/Game/Map/%s/DA_WalkGrid" % _MAPS.map_asset_name(mp)
        if n == 0 and not EAL.does_asset_exist(pkg):
            skip += 1; continue
        if _LG.login_map_write_blocked("/Game/Map/%s" % _MAPS.map_asset_name(mp), "asda_export_swimbits.py", quiet=True):
            skip += 1; continue
        da = unreal.load_asset(pkg) if EAL.does_asset_exist(pkg) else None
        if not da:
            log("%s: НЕТ ассета %s" % (mp, pkg)); bad += 1; continue
        if n == 0:
            # ★ 31.10.2026: карта исключена — снять прежнюю маску (только если она есть; иначе ассет не трогаем).
            if len(da.get_editor_property("swim_bits")) == 0:
                skip += 1; continue
            da.set_editor_property("swim_bits", [])
            if not EAL.save_loaded_asset(da, only_if_is_dirty=False) or len(da.get_editor_property("swim_bits")) != 0:
                log("%s: очистка SwimBits НЕ прошла" % mp); bad += 1; continue
            log("%s: SwimBits ОЧИЩЕН — вода закрыта (%s)" % (mp, pkg)); ok += 1
            continue
        w, h = da.get_editor_property("width"), da.get_editor_property("height")
        if (w * h + 7) // 8 != len(data):
            log("%s: размер сетки %dx%d не сходится с маской %d б" % (mp, w, h, len(data))); bad += 1; continue
        da.set_editor_property("swim_bits", list(data))
        if not EAL.save_loaded_asset(da, only_if_is_dirty=False):
            log("%s: сохранение НЕ прошло" % mp); bad += 1; continue
        back = bytes(da.get_editor_property("swim_bits"))
        if back != data:
            log("%s: перечитанное поле не совпало с файлом" % mp); bad += 1; continue
        log("%s: SwimBits записан, открыто %d тайлов (%s)" % (mp, n, pkg)); ok += 1
    log("итог: записано %d, пропущено %d, ошибок %d" % (ok, skip, bad))
    wok, wbad = write_walls(maps)
    if bad == 0 and wbad == 0:
        log("ЗАВЕРШЕНО: swimbits")


def write_walls(maps):
    """★ 08.11.2026 [ИСПРАВЛЕНИЕ] (пачка 18, баг 9): «стена» маски плавания → `DA_WalkGrid.swim_wall_bits`.

    Источник — `Tools/data/water_swim/<карта>.walls.bits` (`asda_water_swim.py --write`, правило `wall_tiles`). Нет файла —
    поле очищается, если было непустым (иначе ассет не трогается). Точечная запись одного поля, перечитка и сверка с файлом.
    """
    ok = bad = 0
    for mp in maps:
        pkg = "/Game/Map/%s/DA_WalkGrid" % _MAPS.map_asset_name(mp)
        if not EAL.does_asset_exist(pkg):
            continue
        if _LG.login_map_write_blocked("/Game/Map/%s" % _MAPS.map_asset_name(mp), "asda_export_swimbits.py", quiet=True):
            continue
        p = os.path.join(DATA, mp + ".walls.bits")
        data = open(p, "rb").read() if os.path.isfile(p) else b""
        da = unreal.load_asset(pkg)
        if not da:
            log("%s: НЕТ ассета %s (стена)" % (mp, pkg)); bad += 1; continue
        cur = bytes(da.get_editor_property("swim_wall_bits"))
        if cur == data:
            if data:
                log("%s: SwimWallBits уже совпадает (%d тайлов)" % (mp, sum(bin(b).count("1") for b in data)))
            continue
        if data:
            w, h = da.get_editor_property("width"), da.get_editor_property("height")
            if (w * h + 7) // 8 != len(data):
                log("%s: размер сетки %dx%d не сходится со стеной %d б" % (mp, w, h, len(data))); bad += 1; continue
        da.set_editor_property("swim_wall_bits", list(data))
        if not EAL.save_loaded_asset(da, only_if_is_dirty=False) or bytes(da.get_editor_property("swim_wall_bits")) != data:
            log("%s: запись SwimWallBits НЕ прошла" % mp); bad += 1; continue
        log("%s: SwimWallBits записан, стена %d тайлов (%s)" % (mp, sum(bin(b).count("1") for b in data), pkg)); ok += 1
    log("стена: записано %d, ошибок %d" % (ok, bad))
    return ok, bad


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
