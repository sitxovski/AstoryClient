# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_ride_effect_dt.py — DT_RideEffect: code транспорта → шлейф .jeb + звуки езды.
# Источник: RawAssets/Data/Character/Ride_effect_table.bin (магия "RET", хедер 20 б, запись 260 б).
# ★ ФОРМАТ (уточнён этим проходом — в mount_system_re.md §1.3 поле +132 значилось «резерв 128 б», ЭТО ОШИБКА):
#   +0   i32      Code        (== Special_ItemList.Code, здесь всегда i+1)
#   +4   char[64] EffectFile  шлейф .jeb  — item_inventory.c: sprintf(Buffer, "%s", rec+4)
#   +68  char[64] SoundFile   loop-звук движения — item_inventory.c: sprintf(soundPath, "%s", rec+68)
#   +132 char[64] SoundFile2  «..._id2.wav» — charItemSoundChargeTick@0xF27740: rec+132 при toggle==1
#   +196 char[64] SoundFile3  «..._id1.wav» — тот же тик: rec+196 в противофазе (чередующийся мотор)
#   Непустые +132/+196 у 93 записей из 488 (велосипед/метла/мустанг/дракончик/шар/меч/ковёр/огн.колесо).
# RowStruct = FAsdaRideEffectRow (Source/AsdaReborn/Public/World/AsdaPlayerCharacter.h) — ТРЕБУЕТ сборки C++.
# Выход: RawAssets/Data/RideEffect.csv (ВНЕ Content!) + /Game/AsdaAssets/Data/DT_RideEffect
# Запуск: Tools/run_headless.bat Tools\asda_build_ride_effect_dt.py
import os, struct, sys, unreal
import os as _os_dtc  # noqa: E402  ★ дедуп №2 В2 Т4: общий шаг «заливка → сохранение» DataTable
import sys as _sys_dtc  # noqa: E402
_sys_dtc.path.insert(0, _os_dtc.path.dirname(_os_dtc.path.abspath(__file__)))
import asda_dt_common as _DTC  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_bintable as _BT     # noqa: E402  ★ дедуп №2 В1 Т5: общий обход записей и C-строки
import asda_ue_paths as _U      # noqa: E402  ★ дедуп №2 В1 Т20: каталоги проекта и /Game — реестр путей

BIN = os.path.join(_U.DATA_SRC, "Character", "Ride_effect_table.bin")
CSV = os.path.join(_U.DATA_SRC, "RideEffect.csv")
OUT_PKG, OUT_NAME = _U.G_DATA, "DT_RideEffect"
ROW_STRUCT = "/Script/AsdaReborn.AsdaRideEffectRow"
HDR, REC = 20, 260


def log(m): unreal.log_warning("[ride-dt] %s" % m)


def strip_ext(s):
    """Имена в таблице — с расширением и в ВЕРХНЕМ регистре; ассеты UE названы по имени файла в нижнем."""
    s = s.lower()
    for e in (".jeb", ".wav"):
        if s.endswith(e): return s[:-4]
    return s


def read_rows():
    b = open(BIN, "rb").read()
    cnt = struct.unpack_from("<i", b, 4)[0]
    assert b[12:15] == b"RET", "не Ride_effect_table.bin"
    assert len(b) == HDR + REC * cnt, "размер не сходится: %d != %d" % (len(b), HDR + REC * cnt)

    def s(rec, o): return strip_ext(_BT.cstr(rec, o, 64, "latin1"))
    out = []
    # ★ дедуп №2 В1 Т5: обход — общий `asda_bintable.iter_records` (размер файла сверен assert'ом выше).
    for i, rec in _BT.iter_records(b, REC, count=cnt, base=HDR):
        out.append((struct.unpack_from("<i", rec, 0)[0], s(rec, 4), s(rec, 68), s(rec, 132), s(rec, 196)))
    return out


TAIL_CSV = os.path.join(_U.DATA_SRC, "RideEffect_tail.csv")   # ★ 23.10.2026: ведомость дописываемых строк


def tail_rows(rows):
    """★ 23.10.2026 (маунты донора AsdaFableClassic, Reports/fable_mounts/FABLE_MOUNTS.md): строки кодов, которых
    нет в Ride_effect_table.bin основной сборки, — из ведомости RideEffect_tail.csv (строит asda_fable_mounts_data.py;
    значения из Ride_effect_table донора, уже без расширения и в нижнем регистре). Совпавший код — отказ ведомости."""
    if not os.path.isfile(TAIL_CSV):
        return []
    import csv as _csv
    have = {r[0] for r in rows}
    out = []
    for r in _csv.DictReader(open(TAIL_CSV, encoding="utf-8")):
        c = int(r["Code"])
        if c in have:
            log("!! ведомость %s: код %d уже в таблице — хвост НЕ дописан" % (TAIL_CSV, c))
            return []
        have.add(c)
        out.append((c, r["EffectName"], r["SoundName"], r["SoundName2"], r["SoundName3"]))
    return out


def main():
    rows = read_rows()
    tail = tail_rows(rows)
    rows += tail
    log("хвост из %s: +%d строк" % (os.path.basename(TAIL_CSV), len(tail)))
    csv = "_row,Code,EffectName,SoundName,SoundName2,SoundName3\n" + "".join(
        '%d,%d,"%s","%s","%s","%s"\n' % (c, c, e, s1, s2, s3) for (c, e, s1, s2, s3) in rows)
    with open(CSV, "w", encoding="utf-8", newline="") as f:
        f.write(csv)
    filled = sum(1 for r in rows if r[1] or r[2])
    log("CSV: %d строк (с непустым эффектом/звуком %d) → %s" % (len(rows), filled, CSV))

    rs = unreal.load_object(None, ROW_STRUCT)
    if not rs:
        log("!! RowStruct %s НЕ НАЙДЕН — нужна СБОРКА C++ (FAsdaRideEffectRow в "
            "Public/World/AsdaPlayerCharacter.h ещё не скомпилирован). DT не создан; после сборки "
            "прогнать этот скрипт повторно." % ROW_STRUCT)
        return

    full = "%s/%s" % (OUT_PKG, OUT_NAME)
    dt = _DTC.open_or_create_dt(unreal, OUT_PKG, OUT_NAME, rs)  # дедуп №2 В2 Т4: общий «открыть-или-создать»
    if not dt:
        log("!! DataTable не создан"); return
    problems = _DTC.fill_and_save(unreal, dt, csv, log, fmt="csv")  # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т4: провал заливки → DT не сохраняется; маркер ASDA_WRITES_ASSET
    log("DT_RideEffect: строк=%d, заполнение=%s" % (len(dt.get_row_names()), problems))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
