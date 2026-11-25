# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_skillfx_dt.py — DT_SkillFx: «скилл → СОБСТВЕННЫЙ .jeb-эффект» из skill_info.sif (+0xD1).
#
# Источник: RawAssets/Data/Character/skill_info.sif (копия из Data\character\Bin.bbz).
#   Запись 273 б (0x111), индекс = id скилла; +0x02 — название, +0xD1 — имя .jeb («-1» = нет).
#   Полное обоснование раскладки и «ссылок в срезе нет» — в шапке Public/Data/AsdaSkillFxRows.h.
#
# RowStruct = FAsdaSkillFxRow — ТРЕБУЕТ сборки C++ до запуска скрипта.
# Выход: RawAssets/Data/SkillFx.csv (ВНЕ Content!) + /Game/AsdaAssets/Data/DT_SkillFx
# Запуск: Tools/run_headless.bat Tools\asda_build_skillfx_dt.py
# ★ ПРАВИЛО tools-no-import-side-effects: НИКАКИХ действий при импорте модуля — только через main().
import os
import struct
import sys as _sys

import unreal
import os as _os_dtc  # noqa: E402  ★ дедуп №2 В2 Т4: общий шаг «заливка → сохранение» DataTable
import sys as _sys_dtc  # noqa: E402
_sys_dtc.path.insert(0, _os_dtc.path.dirname(_os_dtc.path.abspath(__file__)))
import asda_dt_common as _DTC  # noqa: E402

_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_bintable as _BT     # noqa: E402  ★ дедуп №2 В1 Т5: общий разбор C-строк
import asda_ue_paths as _U      # noqa: E402  ★ дедуп №2 В1 Т20: каталоги проекта и /Game — реестр путей

# ROOT — клиентские данные внутри Content (.sif читаем оттуда, это не приманка авто-импорта).
ROOT = _U.DATA_SRC
# OUT — промежуточный CSV ВНЕ Content: любой .csv внутри Content редактор авто-импортирует
#       и заводит ассет-двойник (см. Reports/content_intermediate_cleanup.md).
OUT = _U.DATA_SRC
SIF = ROOT + r"\Character\skill_info.sif"
CSV = OUT + r"\SkillFx.csv"
OUT_PKG = _U.G_DATA
STRUCT_PATH = "/Script/AsdaReborn.AsdaSkillFxRow"
REC = 0x111          # 273 б — stride записи (CSkillMgr_LoadSkillData@0xA79F30: count = size / 0x111)
OFF_NAME = 0x02      # название скилла (буфер 39 б)
OFF_FX = 0xD1        # имя .jeb (буфер 39 б); «-1» = эффекта нет
NO_FX = "-1"


def log(m):
    unreal.log_warning("[skillfx-dt] %s" % m)


def cstr(b, o):
    """Строка с нулевым терминатором внутри записи (в буфере остаётся мусор от прошлых записей).
    Поле без фиксированной длины — до NUL или конца записи (общий `asda_bintable.cstr`, maxlen=None)."""
    return _BT.cstr(b, o, None, "cp1251", "replace")


def csv_cell(s):
    return '"%s"' % s.replace('"', '""')


def read_rows():
    b = open(SIF, "rb").read()
    assert len(b) % REC == 0, "размер skill_info.sif %d не кратен 273" % len(b)
    total = len(b) // REC
    rows = []
    for i in range(total):
        rec = b[i * REC:(i + 1) * REC]
        if struct.unpack_from("<h", rec, 0)[0] == -1:   # пустая запись (init LOWORD = -1)
            continue
        fx = cstr(rec, OFF_FX)
        if not fx or fx == NO_FX:                        # у 1228 из 1255 собственного .jeb нет
            continue
        rows.append(dict(SkillId=i, SkillName=cstr(rec, OFF_NAME), HitFx=fx))
    return total, rows


def build_csv(rows):
    cols = ["SkillId", "SkillName", "HitFx"]
    out = ["_row," + ",".join(cols)]
    for r in rows:
        out.append(",".join([str(r["SkillId"]), str(r["SkillId"]),
                             csv_cell(r["SkillName"]), csv_cell(r["HitFx"])]))
    return "\n".join(out) + "\n"


def main():
    if not os.path.isfile(SIF):
        log("НЕТ файла %s" % SIF)
        return
    total, rows = read_rows()
    log("skill_info.sif: записей %d, с собственным .jeb — %d" % (total, len(rows)))
    if not rows:
        log("ни одной записи с эффектом — таблица НЕ перезаписана")
        return
    csv_text = build_csv(rows)
    with open(CSV, "w", encoding="utf-8", newline="") as f:
        f.write(csv_text)
    log("CSV → %s" % CSV)

    rs = unreal.load_object(None, STRUCT_PATH)
    if not rs:
        log("!! RowStruct %s НЕ НАЙДЕН — нужна СБОРКА C++ (Public/Data/AsdaSkillFxRows.h). DT не создан."
            % STRUCT_PATH)
        return
    full = "%s/DT_SkillFx" % OUT_PKG
    dt = _DTC.open_or_create_dt(unreal, OUT_PKG, "DT_SkillFx", rs)  # дедуп №2 В2 Т4: общий «открыть-или-создать»
    if not dt:
        log("!! DataTable DT_SkillFx не создан")
        return
    problems = _DTC.fill_and_save(unreal, dt, csv_text, log, fmt="csv")  # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т4: провал заливки → DT не сохраняется; маркер ASDA_WRITES_ASSET
    log("ГОТОВО: DT_SkillFx строк=%d (ожидалось %d), заполнение=%s"
        % (len(dt.get_row_names()), len(rows), problems))
    for r in rows[:3] + rows[-3:]:
        log("   %d «%s» → %s" % (r["SkillId"], r["SkillName"], r["HitFx"]))


# ★★★ Закон tools-no-import-side-effects: main() только при ПРЯМОМ запуске.
#   Прежняя проверка `__name__ != "asda_build_skillfx_dt"` ловила лишь импорт ровно под этим именем:
#   `import Tools.asda_build_skillfx_dt` или `importlib.util.spec_from_file_location("X", ...)` дают другое
#   имя — и main() выстреливал бы. Надёжный признак импорта — `__spec__`: его выставляет ЛЮБАЯ
#   машинерия импорта (import / importlib / пакет / runpy.run_module) и НЕ выставляет прямое
#   исполнение файла (python file.py, UE -ExecutePythonScript). `python -m` даёт "__main__".
def _is_direct_run():
    """True — файл запущен НАПРЯМУЮ, а не импортирован."""
    return __name__ == "__main__" or globals().get("__spec__") is None


if _is_direct_run():
    main()
