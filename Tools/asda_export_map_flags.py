# Copyright (c) 2026 @sitxovski (Discord)
# asda_export_map_flags.py — БАЙТЫ +68 / +69 записи РЕЕСТРА КАРТ оригинала в `Reports/map_table.csv`
#   (колонки ItemRestrict / CampwarPkFlag строки FAsdaMapRow).
#
# ИСТОЧНИК (1:1, без выдумок) — статическая таблица клиента `g_aMapEntityTable`.
#   Эталон: `getSlotByIndex@0x966220` возвращает `this + 18*mapId + 3170` (шаг 18 dword = 72 байта,
#   до 101 записи, `rec[0] == -1` = пусто); `insDungeon_GetSlotRecord@0x997D10` копирует 0x20 байт
#   с `rec + 4` — это ОТОБРАЖАЕМОЕ имя. Байт `+69` читают:
#     network.c:43101-43125 (вход на карту) — `bCampWarVisible = (*(g_aMapEntityTable[mapId]+69) == 1)`
#       → `campWarWnd_ToggleVisible` → `uiDlgSetDetailVisible@0xD66E60` → показ кнопки 17520
#         «Выйти из боя» на мини-карте;
#     ui_core.c / world_map.c — снятие фильтра «только своя карта» у маркеров мини-карты.
#   Байт `+68` — ограничение предметов (эталонный item-restrict), выгружаем заодно.
#
# РАСКЛАДКА ЗАПИСИ (проверена по живому дампу, см. ниже): 72 байта,
#     +0  int32  mapId (-1 = запись пуста)
#     +4  char[32] отображаемое имя (cp1251)
#     +36 char[32] имя папки карты
#     +68 uint8  item-restrict
#     +69 uint8  campwar/PK  (1 = карта лагерной войны/поля битвы)
#   ⚠ Прежняя запись в заметках («база 0x2BCD414, +0 dir, +40 display») сдвинута на 36 байт:
#   0x2BCD414 — это поле `dir` ПЕРВОЙ записи, а НАЧАЛО таблицы = 0x2BCD414 − 36 = 0x2BCD3F0.
#   Сдвиг виден сразу: при старом чтении имя карты 0 получалось «Река Дождя» (это имя карты 1).
#
# ЧТЕНИЕ: живой минидамп `@@ASTORY_DEV_ROOT@@\RE\dumps\asda.dmp` через `RE/ida/mdmp.py` (ASLR off → статический VA
#   == рантайм-VA). Дамп — штатный ground-truth проекта.
#
# Запуск:  python asda_export_map_flags.py            (обычный python, unreal НЕ нужен)
#          --dry  — только показать таблицу, CSV не трогать
# Затем:   Tools\run_headless.bat …\asda_setup_zone_assets.py   (пересоберёт DT_MapTable из CSV)
import csv
import io
import os
import struct
import sys

CSV_PATH  = r"@@ASTORY_DEV_ROOT@@\Reports\map_table.csv"
DUMP_PATH = r"@@ASTORY_DEV_ROOT@@\RE\dumps\asda.dmp"
MDMP_DIR  = r"@@ASTORY_DEV_ROOT@@\RE\ida"

TABLE_VA   = 0x2BCD3F0     # начало g_aMapEntityTable (0x2BCD414 — поле dir первой записи)
REC_SIZE   = 72
REC_COUNT  = 101           # getSlotByIndex: a2 >= 0x65 → nullptr
OFF_ID     = 0
OFF_DISP   = 4
OFF_DIR    = 36
OFF_ITEM   = 68
OFF_CAMP   = 69


def _s(b):
    b = b.split(b"\x00")[0]
    try:
        return b.decode("cp1251")
    except Exception:
        return ""


def read_records():
    """[(mapId, display, dir, item68, camp69)] из живого дампа."""
    sys.path.insert(0, MDMP_DIR)
    from mdmp import MiniDump          # локальный импорт: без дампа скрипт бесполезен
    dump = MiniDump(DUMP_PATH)
    blob = dump.read_va(TABLE_VA, REC_SIZE * REC_COUNT)
    out = []
    for i in range(REC_COUNT):
        r = blob[REC_SIZE * i: REC_SIZE * (i + 1)]
        mid = struct.unpack_from("<i", r, OFF_ID)[0]
        d = _s(r[OFF_DIR:OFF_DIR + 32])
        if mid == -1 or not d:
            continue                    # пустой слот реестра
        out.append((i, _s(r[OFF_DISP:OFF_DISP + 32]), d, r[OFF_ITEM], r[OFF_CAMP]))
    return out


def main(dry=False):
    recs = read_records()
    by_id = {r[0]: r for r in recs}
    print("Реестр карт клиента (g_aMapEntityTable @0x%X, %d непустых записей):" % (TABLE_VA, len(recs)))
    for i, disp, d, it, cw in recs:
        mark = "  <<< CAMPWAR" if cw == 1 else ""
        print("  %3d %-20s %-24s +68=%d +69=%d%s" % (i, d, disp, it, cw, mark))

    # ★ 14.09.2026 (реестр карт, волна 2): байты пишутся в ЕДИНЫЙ ИСТОЧНИК `Tools/data/asda_map_registry.json`
    #   (поля item_restrict / campwar_pk), а map_table.csv генерируется из него (`Tools/asda_map_registry.py`).
    import asda_map_registry as _REG    # локально: реестр нужен только при запуске
    reg = _REG.load_registry()
    changed = 0
    missing = []
    updates = {}
    for m in _REG.maps_by_id(reg):
        mid = m["id"]
        rec = by_id.get(mid)
        if rec is None:
            missing.append((mid, m["dir"]))
            continue
        # Сверка: имя папки в реестре порта и в реестре клиента должны совпасть (регистр/пробелы игнорируем).
        a = (m["dir"] or "").strip().lower().replace(" ", "_")
        b = (rec[2] or "").strip().lower().replace(" ", "_")
        if a and b and a != b:
            print("  !! mapId=%d: dir в реестре '%s' != реестра клиента '%s' — поля НЕ трогаю" % (mid, m["dir"], rec[2]))
            continue
        want = {"item_restrict": int(rec[3]), "campwar_pk": int(rec[4])}
        changed += sum(1 for k, v in want.items() if m[k] != v)
        updates[mid] = want

    if missing:
        print("  (в реестре клиента нет записей для mapId: %s)" % ", ".join(str(m) for m, _ in missing))
    if dry:
        print("--dry: реестр не изменён (правок было бы %d)" % changed)
        return
    n = _REG.update_map_fields(updates)
    print("РЕЕСТР обновлён: %s (полей изменено %d)" % (_REG.REGISTRY_JSON, n))
    _REG.generate()
    print("Дальше (если map_table.csv изменился): Tools\\run_headless.bat …\\asda_setup_zone_assets.py — пересобрать DT_MapTable")


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    main(dry="--dry" in sys.argv)
