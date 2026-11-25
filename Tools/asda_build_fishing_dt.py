# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_fishing_dt.py — ТРИ ТАБЛИЦЫ РЫБАЛКИ (волна В1, 08.10.2026) из клиентских .BIN:
#   DT_FishData  ← RawAssets/Data/Fish_data.BIN        (рыбы: раунды, мс раунда, текстура, модель)
#   DT_FishNum   ← RawAssets/Data/Fish_num.BIN         (диапазоны {min,max,value}: стрелок в раунде)
#   DT_FishSpots ← RawAssets/Data/Map_info.BIN + Fish_area_data.BIN (места лова, таблица спотов в памяти)
#
# ИСТОЧНИК (вещественное доказательство, скопировано 1:1 из Donors\AsdaChroniclesInterlude_MINE\Data, md5 сверены):
#   Fish_data.BIN       382d31eeb74df07adc8e6ef260cd0b50  15844 б = шапка 20 + 86 × 184
#   Fish_num.BIN        7b219222517cf13b2fa730405cbdc31d    260 б = шапка 20 + 20 × 12
#   Fish_area_data.BIN  03d011f793274b39fbde50bf04ebd6bd   2132 б = шапка 20 + 16 × 132
#   Map_info.BIN        a44b74b9787e5c0fe0f5ebb657bbed32   9036 б = шапка 20 + 23 × 392
#
# ЗАГРУЗЧИКИ ЭТАЛОНА (`RE/ida/export/AsdaStory_game.c`):
#   `CMap_LoadFishingData@0xFCFBC0` (g:608637) → `CMap_LoadMapInfoAndFishAreas@0xFCFC00` (g:608645),
#   `FishData_LoadFromBin@0xFD01E0`, `CFishGame_LoadFishNumTable@0xFD0440`. Шапка 20 б, `+4` = число записей.
#   Поиск рыбы — `fishGameFindRecordById@0xFD03C0`: ПЕРВАЯ запись по порядку файла с `+0 == id` (в файле код
#   69649 встречается ДВАЖДЫ — строки 72 и 80; порядок значим, поэтому колонка Order).
#   Поиск диапазона — `rangeTableLookupValue@0xFD0570`: первая запись с `min <= v <= max`, иначе 1.
#   Таблица спотов — элемент 152 б: `+0 карта, +4 X, +8 Y, +12 радиус, +16 умение, +20 имя (area+4),
#   +84 имя (area+68), +148 номер слота` (стек `areaEntry[5]` + `fishName2[64]` + `fishName[64]` + `slotIdx`).
#   Пустой слот спота = X (`+0xC0 + 4j`) == −1. Индекс области вне [0, count) — эталон оставляет имена от
#   ПРЕДЫДУЩЕЙ итерации (мусор); у текущих данных такого нет, при появлении — пишем пусто и предупреждаем.
#
# ★ ДАННЫЕ НЕ ПРАВИМ: строки Fish_data с −1 (коды 69617…69648 и др.) переносятся как есть (их обработка —
#   задача кода: эталон берёт `+8 < 0 → 1 раунд`); дубль 69649 сохраняется (первая строка выигрывает).
#
# RowStruct'ы = FAsdaFishDataRow / FAsdaFishNumRow / FAsdaFishSpotRow (Public/World/AsdaFishingSubsystem.h)
#   — ТРЕБУЮТ сборки C++ до запуска в редакторе.
# Выход: RawAssets/Data/{FishData,FishNum,FishSpots}.csv (ВНЕ Content!) + /Game/AsdaAssets/Data/DT_Fish*
# Запуск (в редакторе): python Tools/run_headless_safe.py Tools/asda_build_fishing_dt.py --idle 200
# Запуск (только CSV):  python Tools/asda_build_fishing_dt.py
# ★ СВЕРКА: после заливки печатается «записей: N (в .BIN M)» по каждой таблице и общий вердикт
#   «СВЕРКА ОК» / «СВЕРКА РАСХОЖДЕНИЕ» (N ≠ M = провал — закон «потеряно записей: 0»).
# ★ ПРАВИЛО tools-no-import-side-effects: НИКАКИХ действий при импорте модуля — только через main().
import os
import os as _os_dtc  # noqa: E402  ★ дедуп №2 В2 Т4: общий шаг «заливка → сохранение» DataTable
import sys as _sys_dtc  # noqa: E402
_sys_dtc.path.insert(0, _os_dtc.path.dirname(_os_dtc.path.abspath(__file__)))
import asda_dt_common as _DTC  # noqa: E402
import struct

try:
    import unreal
except ImportError:      # штатный режим «только CSV» вне редактора
    unreal = None

import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_bintable as _BT     # noqa: E402  ★ дедуп №2 В1 Т5: общий счётчик/обход записей и C-строки
import asda_ue_paths as _U      # noqa: E402  ★ дедуп №2 В1 Т20: /Game — реестр путей

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "RawAssets", "Data")
BIN_FISH = os.path.join(DATA, "Fish_data.BIN")
BIN_NUM = os.path.join(DATA, "Fish_num.BIN")
BIN_AREA = os.path.join(DATA, "Fish_area_data.BIN")
BIN_MAP = os.path.join(DATA, "Map_info.BIN")
OUT_PKG = _U.G_DATA          # ★ дедуп №2 В1 Т20: /Game/AsdaAssets/Data — реестр путей
HDR = 20             # шапка всех четырёх файлов (`fread(header, 0x14u, 1u, Stream)`)
REC_FISH = 184       # `fread(..., 0xB8u, count, ...)`
REC_NUM = 12         # `fread(..., 0xCu, count, ...)`
REC_AREA = 132       # `fread(..., 0x84u, count, ...)`
REC_MAP = 392        # `fread(..., 0x188u, count, ...)`

# Описание таблиц: имя DT, RowStruct, путь CSV.
TABLES = {
    "fish": ("DT_FishData", "/Script/AsdaReborn.AsdaFishDataRow", os.path.join(DATA, "FishData.csv")),
    "num": ("DT_FishNum", "/Script/AsdaReborn.AsdaFishNumRow", os.path.join(DATA, "FishNum.csv")),
    "spot": ("DT_FishSpots", "/Script/AsdaReborn.AsdaFishSpotRow", os.path.join(DATA, "FishSpots.csv")),
}


def log(m):
    """Печать: в редакторе — warning-лог (виден в headless), иначе stdout с заменой непечатного."""
    if unreal:
        unreal.log_warning("[fishing-dt] %s" % m)
        return
    line = "[fishing-dt] %s" % m
    enc = (getattr(__import__("sys").stdout, "encoding", None) or "utf-8")
    print(line.encode(enc, errors="replace").decode(enc, errors="replace"))


def read_bin(path, rec):
    """Файл → (байты, число записей по шапке, число записей по размеру). None — файла нет."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        log("!! не прочитать %s: %s" % (path, e))
        return None
    if len(data) < HDR:
        log("!! %s короче шапки" % path)
        return None
    count_hdr = _BT.record_count(data)                 # i32@4 (общий `asda_bintable`)
    count_file = (len(data) - HDR) // rec
    if (len(data) - HDR) % rec:
        log("!! %s: хвост %d б не кратен записи %d б" % (os.path.basename(path), (len(data) - HDR) % rec, rec))
    return data, count_hdr, count_file


def cstr(b):
    """ASCII/cp1251-строка до нуля (имена текстур/моделей ASCII); общий `asda_bintable.cstr`."""
    return _BT.cstr(b, 0, None, "cp1251", "replace")


def parse_fish():
    """Fish_data.BIN → список строк в порядке файла. Поля — все 184 байта (ничего не выбрасываем)."""
    r = read_bin(BIN_FISH, REC_FISH)
    if not r:
        return [], 0
    data, ch, cf = r
    n = min(ch, cf)
    rows = []
    # ★ дедуп №2 В1 Т5: обход — общий `asda_bintable.iter_records`; смещения ниже — от начала записи.
    for i, rec in _BT.iter_records(data, REC_FISH, count=n, base=HDR):
        ints = struct.unpack_from("<12i", rec, 0)
        rows.append({
            "Order": i,
            "FishCode": ints[0],     # +0  id предмета рыбы (= FishCode из 6168)
            "FishType": ints[1],     # +4  тип 0..4 (0 золотая, 1/2/3 обычные, 4 мусор)
            "Rounds": ints[2],       # +8  раундов мини-игры (FSM 114)
            "RoundMs": ints[3],      # +12 мс на раунд (ключ-гайд, состояние 3)
            "Raw16": ints[4], "Raw20": ints[5], "Raw24": ints[6], "Raw28": ints[7],
            "Raw32": ints[8], "Raw36": ints[9], "Raw40": ints[10], "Raw44": ints[11],
            "Texture": cstr(rec[48:112]),                   # +48 имя текстуры карточки (64 б)
            "TexCol": struct.unpack_from("<i", rec, 112)[0],        # +112 колонка в атласе
            "TexRow": struct.unpack_from("<i", rec, 116)[0],        # +116 строка в атласе
            "Model": cstr(rec[120:184]),                    # +120 имя модели рыбы (64 б)
        })
    return rows, ch


def parse_num():
    """Fish_num.BIN → диапазоны {min, max, value} в порядке файла."""
    r = read_bin(BIN_NUM, REC_NUM)
    if not r:
        return [], 0
    data, ch, cf = r
    n = min(ch, cf)
    rows = []
    for i, rec in _BT.iter_records(data, REC_NUM, count=n, base=HDR):     # общий обход (дедуп №2 Т5)
        mn, mx, v = struct.unpack_from("<3i", rec, 0)
        rows.append({"Order": i, "Min": mn, "Max": mx, "Value": v})
    return rows, ch


def parse_spots():
    """Map_info.BIN × Fish_area_data.BIN → таблица спотов 1:1 с `CMap_LoadMapInfoAndFishAreas`."""
    rm = read_bin(BIN_MAP, REC_MAP)
    ra = read_bin(BIN_AREA, REC_AREA)
    if not rm or not ra:
        return [], 0, 0
    mdata, mch, mcf = rm
    adata, ach, acf = ra
    maps = min(mch, mcf)
    areas = min(ach, acf)
    rows = []
    expected = 0
    for i, mrec in _BT.iter_records(mdata, REC_MAP, count=maps, base=HDR):   # общий обход (дедуп №2 Т5)
        map_id = struct.unpack_from("<i", mrec, 0)[0]
        xs = struct.unpack_from("<10i", mrec, 0xC0)
        ys = struct.unpack_from("<10i", mrec, 0xE8)
        rs = struct.unpack_from("<10i", mrec, 0x110)
        ar = struct.unpack_from("<10i", mrec, 0x138)
        sk = struct.unpack_from("<10i", mrec, 0x160)
        for j in range(10):
            if xs[j] == -1:          # эталон: `if (*(mapInfo + 98*i + j + 48) != -1)`
                continue
            expected += 1
            a = ar[j]
            n1 = n2 = ""
            if 0 <= a < areas:
                ao = HDR + REC_AREA * a
                n1 = cstr(adata[ao + 4:ao + 68])     # entry+20 ← area+4
                n2 = cstr(adata[ao + 68:ao + 132])   # entry+84 ← area+68
            else:
                log("!! карта %d слот %d: индекс области %d вне [0,%d) — имена пустые (эталон тянул бы мусор)"
                    % (map_id, j, a, areas))
            rows.append({
                "Order": len(rows), "MapId": map_id, "X": xs[j], "Y": ys[j], "Radius": rs[j],
                "Skill": sk[j], "AreaIdx": a, "AreaName1": n1, "AreaName2": n2, "Slot": j,
            })
    return rows, expected, maps


def to_csv(rows, cols):
    """CSV для `fill_data_table_from_csv_string`: первая колонка — ключ строки (= Order)."""
    out = ["_row," + ",".join(cols)]
    for r in rows:
        vals = []
        for c in cols:
            v = r[c]
            vals.append('"%s"' % v if isinstance(v, str) else str(v))
        out.append("%d,%s" % (r["Order"], ",".join(vals)))
    return "\n".join(out) + "\n"


def fill(name, row_struct, text):
    """Создать/открыть DT и залить CSV. Возвращает число строк в DT (−1 — не удалось)."""
    rs = unreal.load_object(None, row_struct)
    if not rs:
        log("!! RowStruct %s НЕ НАЙДЕН — нужна СБОРКА C++ (AsdaFishingSubsystem.h). DT %s не создан." % (row_struct, name))
        return -1
    dt = _DTC.open_or_create_dt(unreal, OUT_PKG, name, rs)  # дедуп №2 В2 Т4: общий «открыть-или-создать»
    if not dt:
        log("!! DataTable %s не создан" % name)
        return -1
    # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т4: заливка не прошла → DT НЕ сохраняется и возвращается −1 (раньше — лог и
    #   сохранение битой таблицы). Маркер ASDA_WRITES_ASSET печатает общий шаг перед сохранением.
    if not _DTC.fill_and_save(unreal, dt, text, log, fmt="csv"):
        return -1
    return len(dt.get_row_names())


def main():
    fish, fish_hdr = parse_fish()
    num, num_hdr = parse_num()
    spots, spot_exp, map_cnt = parse_spots()
    log("Fish_data: %d записей (шапка %d); Fish_num: %d (шапка %d); споты: %d из %d карт (ожидалось %d)"
        % (len(fish), fish_hdr, len(num), num_hdr, len(spots), map_cnt, spot_exp))
    dup = {}
    for r in fish:
        dup.setdefault(r["FishCode"], []).append(r["Order"])
    for code, orders in dup.items():
        if len(orders) > 1:
            log("★ код рыбы %d встречается в строках %s — эталон берёт ПЕРВУЮ (fishGameFindRecordById)" % (code, orders))
    per_map = {}
    for s in spots:
        per_map.setdefault(s["MapId"], 0)
        per_map[s["MapId"]] += 1
    log("споты по картам: %s" % ", ".join("%d:%d" % kv for kv in sorted(per_map.items())))

    texts = {
        "fish": to_csv(fish, ["Order", "FishCode", "FishType", "Rounds", "RoundMs", "Raw16", "Raw20", "Raw24",
                              "Raw28", "Raw32", "Raw36", "Raw40", "Raw44", "Texture", "TexCol", "TexRow", "Model"]),
        "num": to_csv(num, ["Order", "Min", "Max", "Value"]),
        "spot": to_csv(spots, ["Order", "MapId", "X", "Y", "Radius", "Skill", "AreaIdx", "AreaName1", "AreaName2", "Slot"]),
    }
    expect = {"fish": fish_hdr, "num": num_hdr, "spot": spot_exp}
    for k, (name, rs, csv_path) in TABLES.items():
        with open(csv_path, "w", encoding="utf-8-sig", newline="\n") as f:
            f.write(texts[k])
        log("CSV записан: %s" % csv_path)

    if not unreal:
        log("редактора нет — шаг «CSV → DataTable» пропущен (штатный режим «только CSV»)")
        ok = len(fish) == fish_hdr and len(num) == num_hdr and len(spots) == spot_exp
        log("СВЕРКА %s (по CSV)" % ("ОК" if ok else "РАСХОЖДЕНИЕ"))
        print("ЗАВЕРШЕНО")
        return

    ok = True
    for k, (name, rs, csv_path) in TABLES.items():
        n = fill(name, rs, texts[k])
        log("%s: записей: %d (в .BIN %d)" % (name, n, expect[k]))
        if n != expect[k]:
            ok = False
    log("СВЕРКА %s" % ("ОК" if ok else "РАСХОЖДЕНИЕ"))
    print("ЗАВЕРШЕНО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    main()
