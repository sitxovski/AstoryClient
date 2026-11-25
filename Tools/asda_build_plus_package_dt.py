# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_plus_package_dt.py — DT_PlusPackage: WingId → база .jeb крыльев (муж/жен).
# Источник: RawAssets/Data/Character/PlusPackageTable.BIN (магия "PPT", хедер 20 б, запись 140 б).
# ★ ФОРМАТ (сверен байтами + эталон CPlusPackage_LoadTable@0xEE3440):
#   header 20: ver i32@0 (=0), count i32@4, extra i32@8 (=5), magic "PPT\0"@12, pad@16
#   запись 140:
#     +0   i32      Id             (== WingId из пакета крыльев; в файле 0..count-1)
#     +4   i32      NoticeIdMale   id нотиса (муж) — 110900..110904
#     +8   i32      NoticeIdFemale id нотиса (жен) — 110920..110924
#     +12  char[64] JebMale        база .jeb крыла (муж), напр. "eff_wing_m_ss_00"
#     +76  char[64] JebFemale      база .jeb крыла (жен), напр. "eff_wing_f_ss_00"
# RowStruct = FAsdaPlusPackageRow (Source/AsdaReborn/Public/World/AsdaWorldSubsystem.h) — ТРЕБУЕТ сборки C++.
# Выход: RawAssets/Data/PlusPackage.csv (ВНЕ Content!) + /Game/AsdaAssets/Data/DT_PlusPackage
# Запуск: Tools/run_headless.bat Tools\asda_build_plus_package_dt.py
# ★ ПРАВИЛО tools-no-import-side-effects: НИКАКИХ действий при импорте модуля — только через main().
import os as _os
import struct
import sys as _sys

import unreal
import os as _os_dtc  # noqa: E402  ★ дедуп №2 В2 Т4: общий шаг «заливка → сохранение» DataTable
import sys as _sys_dtc  # noqa: E402
_sys_dtc.path.insert(0, _os_dtc.path.dirname(_os_dtc.path.abspath(__file__)))
import asda_dt_common as _DTC  # noqa: E402

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_bintable as _BT     # noqa: E402  ★ дедуп №2 В1 Т5: общий обход записей и C-строки
import asda_ue_paths as _U      # noqa: E402  ★ дедуп №2 В1 Т20: каталоги проекта и /Game — реестр путей

BIN = _os.path.join(_U.DATA_SRC, "Character", "PlusPackageTable.BIN")
CSV = _os.path.join(_U.DATA_SRC, "PlusPackage.csv")
OUT_PKG, OUT_NAME = _U.G_DATA, "DT_PlusPackage"
ROW_STRUCT = "/Script/AsdaReborn.AsdaPlusPackageRow"
HDR, REC = 20, 140


def log(m):
    unreal.log_warning("[plus-dt] %s" % m)


def strip_ext(s):
    """Имена в таблице бывают с расширением/в верхнем регистре; ассеты UE названы по имени файла в нижнем."""
    s = s.lower()
    if s.endswith(".jeb"):
        return s[:-4]
    return s


def read_rows():
    b = open(BIN, "rb").read()
    cnt = struct.unpack_from("<i", b, 4)[0]
    assert b[12:15] == b"PPT", "не PlusPackageTable.BIN (нет магии PPT)"
    assert len(b) == HDR + REC * cnt, "размер не сходится: %d != %d" % (len(b), HDR + REC * cnt)

    def s(rec, o):
        return strip_ext(_BT.cstr(rec, o, 64, "latin1"))

    out = []
    # ★ дедуп №2 В1 Т5: обход — общий `asda_bintable.iter_records` (размер файла сверен assert'ом выше).
    for i, rec in _BT.iter_records(b, REC, count=cnt, base=HDR):
        wid, nm, nf = struct.unpack_from("<iii", rec, 0)
        out.append((wid, nm, nf, s(rec, 12), s(rec, 76)))
    return out


def main():
    rows = read_rows()
    csv = "_row,Id,NoticeIdMale,NoticeIdFemale,JebMale,JebFemale\n" + "".join(
        '%d,%d,%d,%d,"%s","%s"\n' % (i, i, nm, nf, jm, jf) for (i, nm, nf, jm, jf) in rows)
    with open(CSV, "w", encoding="utf-8", newline="") as f:
        f.write(csv)
    log("CSV: %d строк → %s" % (len(rows), CSV))

    rs = unreal.load_object(None, ROW_STRUCT)
    if not rs:
        log("!! RowStruct %s НЕ НАЙДЕН — нужна СБОРКА C++ (FAsdaPlusPackageRow в "
            "Public/World/AsdaWorldSubsystem.h ещё не скомпилирован). DT не создан." % ROW_STRUCT)
        return

    full = "%s/%s" % (OUT_PKG, OUT_NAME)
    dt = _DTC.open_or_create_dt(unreal, OUT_PKG, OUT_NAME, rs)  # дедуп №2 В2 Т4: общий «открыть-или-создать»
    if not dt:
        log("!! DataTable не создан")
        return
    problems = _DTC.fill_and_save(unreal, dt, csv, log, fmt="csv")  # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т4: провал заливки → DT не сохраняется; маркер ASDA_WRITES_ASSET
    log("DT_PlusPackage: строк=%d, заполнение=%s" % (len(dt.get_row_names()), problems))


if __name__ == "__main__":
    main()
