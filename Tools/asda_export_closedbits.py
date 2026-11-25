# Copyright (c) 2026 @sitxovski (Discord)
# asda_export_closedbits.py — запись ЗАКРЫТЫХ ТАЙЛОВ (04.11.2026, баг «портал Fireway Forest → Flabis») в `DA_WalkGrid.ClosedBits`.
#   Источник — `Tools/data/walk_closed/<карта>.json` (генератор `asda_portal_pockets.py --write`): `pocket_tiles` (карман за зоной
#   портала под геометрией объектов) + `parity_tiles` (открыт в .TER, закрыт битом 0 .sim GameServer). Пишется ТОЛЬКО поле
#   `closed_bits` (точечная запись, прочие поля ассета не трогаются). Карта без json: поле непустое — ОЧИЩАЕТСЯ (закрытий больше
#   нет), пустое — ассет не трогается. Проверка: после сохранения поле перечитывается и сверяется с ожидаемым побайтно.
#   Бэкап `.uasset` до записи — `@@ASTORY_DEV_ROOT@@\_backup\<ASDA_BACKUP_TAG или дата_closedbits>\AstoryClient\Content\Map\<карта>\`.
# Запуск: headless (run_headless_safe.py). Опц. ASDA_MAPS="firewayforest||silalis". Маркер успеха — «ЗАВЕРШЕНО: closedbits».
import os, sys, json, shutil, datetime, unreal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_maps as _MAPS
import asda_paths as _P
import asda_login_guard as _LG      # единый гейт сцены логина (карта из ASDA_MAPS ни с чем не сверяется)

EAL = unreal.EditorAssetLibrary
DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "walk_closed")   # json генератора
CONTENT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Content")   # Content проекта
def log(m): unreal.log("[closedbits] " + str(m))


def expected_bits(path, w, h):
    """Бит-пак закрытых тайлов из json генератора (бит tz*W+tx = 1), число тайлов."""
    d = json.load(open(path, encoding="utf-8"))
    out = bytearray((w * h + 7) // 8)
    n = 0
    for x, z in d.get("pocket_tiles", []) + d.get("parity_tiles", []):
        if 0 <= x < w and 0 <= z < h:
            i = z * w + x
            if not (out[i >> 3] >> (i & 7)) & 1:
                out[i >> 3] |= 1 << (i & 7); n += 1
    return bytes(out), n


def backup(mp):
    """Копия .uasset DA_WalkGrid карты до записи; True — скопировано и размер сошёлся."""
    tag = os.environ.get("ASDA_BACKUP_TAG") or (datetime.date.today().isoformat() + "_closedbits")
    src = os.path.join(CONTENT, "Map", _MAPS.map_asset_name(mp), "DA_WalkGrid.uasset")
    if not os.path.isfile(src):
        log("%s: нет файла %s для бэкапа" % (mp, src)); return False
    rel = os.path.relpath(src, os.path.join(_P.ROOT))
    dst = os.path.join(_P.ROOT, "_backup", tag, rel)
    if not os.path.isfile(dst):                  # первый бэкап не перезаписывается повторным прогоном
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
    ok = os.path.getsize(dst) > 0
    log("%s: бэкап %s (%s)" % (mp, dst, "ok" if ok else "ПУСТОЙ"))
    return ok


def main():
    only = os.environ.get("ASDA_MAPS", "")
    maps = [m.strip() for m in only.split("||")] if only else _MAPS.ALL_MAPS
    ok = skip = bad = 0
    for mp in maps:
        pkg = "/Game/Map/%s/DA_WalkGrid" % _MAPS.map_asset_name(mp)
        if not EAL.does_asset_exist(pkg):
            skip += 1; continue
        if _LG.login_map_write_blocked("/Game/Map/%s" % _MAPS.map_asset_name(mp), "asda_export_closedbits.py", quiet=True):
            skip += 1; continue
        da = unreal.load_asset(pkg)
        if not da:
            log("%s: НЕТ ассета %s" % (mp, pkg)); bad += 1; continue
        cur = bytes(da.get_editor_property("closed_bits"))
        p = os.path.join(DATA, mp + ".json")
        if not os.path.isfile(p):
            if not cur:
                skip += 1; continue
            want, n = b"", 0
        else:
            w, h = da.get_editor_property("width"), da.get_editor_property("height")
            want, n = expected_bits(p, w, h)
        if cur == want:
            log("%s: ClosedBits уже как в данных (%d тайлов) — пропуск" % (mp, n)); skip += 1; continue
        if not backup(mp):
            bad += 1; continue
        da.set_editor_property("closed_bits", list(want))
        if not EAL.save_loaded_asset(da, only_if_is_dirty=False):
            log("%s: сохранение НЕ прошло" % mp); bad += 1; continue
        back = bytes(da.get_editor_property("closed_bits"))
        if back != want:
            log("%s: перечитанное поле не совпало с данными" % mp); bad += 1; continue
        log("%s: ClosedBits записан, закрыто %d тайлов (%s)" % (mp, n, pkg)); ok += 1
    log("итог: записано %d, пропущено %d, ошибок %d" % (ok, skip, bad))
    if bad == 0:
        log("ЗАВЕРШЕНО: closedbits")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
