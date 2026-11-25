# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_pet_dt.py — DT_PetModels: petTypeId → модель/клипы/парение (для рантайма AAsdaPetActor).
# Источник: RawAssets/Data/Character/pettable.bin (tag PTB, заголовок 20 б, запись 472 б).
#   +0   i32      Id            = petTypeId в сети (ключ строки)
#   +96  i32      bHover        0 = ходит по земле, 1 = парит
#   +100 i32      HoverHeight   прибавка к высоте при bHover (−20/0/55/110 game-ед. = см)
#   +148 char[32] ModelFile     .jbm → имя SkeletalMesh
#   +180/212/244  AnimFile0/1/2 .jbb — треки 0/1 (покой A/B) и 2 (бег)
# Реверс: Reports/pet_system_re.md §1.2 (CPetData::GetPetResourceName, slot 0/1/2 → +180/212/244, 100 → +148).
# RowStruct = FAsdaPetModelRow (Source/AsdaReborn/Public/World/AsdaPetActor.h) — ТРЕБУЕТ сборки C++.
# Выход: RawAssets/Data/PetModels.csv (ВНЕ Content!) + /Game/AsdaAssets/Data/DT_PetModels
# Запуск: Tools/run_headless.bat Tools\asda_build_pet_dt.py
import os, struct, sys, unreal
import os as _os_dtc  # noqa: E402  ★ дедуп №2 В2 Т4: общий шаг «заливка → сохранение» DataTable
import sys as _sys_dtc  # noqa: E402
_sys_dtc.path.insert(0, _os_dtc.path.dirname(_os_dtc.path.abspath(__file__)))
import asda_dt_common as _DTC  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_bintable as _BT     # noqa: E402  ★ дедуп №2 В1 Т5: общий разбор C-строк
import asda_ue_paths as _U      # noqa: E402  ★ дедуп №2 В1 Т20: каталоги проекта и /Game — реестр путей

BIN = os.path.join(_U.DATA_SRC, "Character", "pettable.bin")
CSV = os.path.join(_U.DATA_SRC, "PetModels.csv")
OUT_PKG, OUT_NAME = _U.G_DATA, "DT_PetModels"
ROW_STRUCT = "/Script/AsdaReborn.AsdaPetModelRow"
REC = 472


def log(m): unreal.log_warning("[pet-dt] %s" % m)


def read_rows():
    b = open(BIN, "rb").read()
    cnt = struct.unpack_from("<i", b, 4)[0]
    assert b[12:15] == b"PTB", "не PetTable.BIN"

    def s(o): return _BT.cstr(b, o, 32, "latin1")        # char[32] до NUL (общий `asda_bintable.cstr`)
    out = []
    for i in range(cnt):
        r = 20 + REC * i
        out.append((struct.unpack_from("<i", b, r)[0],           # Id
                    s(r + 148),                                   # ModelFile
                    s(r + 180), s(r + 212), s(r + 244),           # AnimFile0/1/2
                    struct.unpack_from("<i", b, r + 96)[0],       # bHover
                    struct.unpack_from("<i", b, r + 100)[0]))     # HoverHeight
    return out


def main():
    rows = read_rows()
    csv = "_row,PetTypeId,ModelName,Anim0,Anim1,Anim2,bHover,HoverHeight\n" + "".join(
        '%d,%d,"%s","%s","%s","%s",%s,%d\n' % (i, i, m, a0, a1, a2, "true" if h else "false", hh)
        for (i, m, a0, a1, a2, h, hh) in rows)
    with open(CSV, "w", encoding="utf-8", newline="") as f:
        f.write(csv)
    log("CSV: %d строк → %s" % (len(rows), CSV))

    rs = unreal.load_object(None, ROW_STRUCT)
    if not rs:
        log("!! RowStruct %s НЕ НАЙДЕН — нужна СБОРКА C++ (FAsdaPetModelRow в "
            "Public/World/AsdaPetActor.h ещё не скомпилирован). DT не создан; после сборки "
            "прогнать этот скрипт повторно." % ROW_STRUCT)
        return

    full = "%s/%s" % (OUT_PKG, OUT_NAME)
    dt = _DTC.open_or_create_dt(unreal, OUT_PKG, OUT_NAME, rs)  # дедуп №2 В2 Т4: общий «открыть-или-создать»
    if not dt:
        log("!! DataTable не создан"); return
    problems = _DTC.fill_and_save(unreal, dt, csv, log, fmt="csv")  # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т4: провал заливки → DT не сохраняется; маркер ASDA_WRITES_ASSET
    log("DT_PetModels: строк=%d, заполнение=%s" % (len(dt.get_row_names()), problems))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
