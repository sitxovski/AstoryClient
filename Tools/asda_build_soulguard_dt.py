# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_soulguard_dt.py — DT_SoulGuarder (27) + DT_SkillCoin (1255): клиентские таблицы
#   системы «хранителя» (Soul Guardian / Soul Boost), ЭТАП 3 окна MINISTATE_WIN.
#
# Источники (скопированы из дистрибутива клиента пользователя Donors\AsdaChroniclesInterlude_MINE\Data):
#   RawAssets/Data/Character/SoulGuarderTable.BIN  — магия "SGT", хедер 20 б, запись 1956 б
#   RawAssets/Data/Character/SkillCoinTable.bin    — магия "SGP", хедер 20 б, запись 20 б
# Полная раскладка и чем она подтверждена — в шапке Public/Data/AsdaSoulGuardRows.h.
#
# RowStruct = FAsdaSoulGuarderRow / FAsdaSkillCoinRow — ТРЕБУЮТ сборки C++ до запуска скрипта.
# Выход: RawAssets/Data/SoulGuarder.csv + SkillCoin.csv (ВНЕ Content!) и /Game/AsdaAssets/Data/DT_*
# Запуск: Tools/run_headless.bat Tools\asda_build_soulguard_dt.py
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
import asda_bintable as _BT     # noqa: E402  ★ дедуп №2 В1 Т5: общий разбор C-строк
import asda_ue_paths as _U      # noqa: E402  ★ дедуп №2 В1 Т20: каталоги проекта и /Game — реестр путей

# ROOT — клиентские .bin внутри Content (не приманка авто-импорта, фабрики для .bin нет).
ROOT = _U.DATA_SRC
# OUT — промежуточные CSV ВНЕ Content: любой .csv внутри Content редактор авто-импортирует
#       и заводит ассет-двойник (см. Reports/content_intermediate_cleanup.md).
OUT = _U.DATA_SRC
SG_BIN = ROOT + r"\Character\SoulGuarderTable.BIN"
SC_BIN = ROOT + r"\Character\SkillCoinTable.bin"
SG_CSV = OUT + r"\SoulGuarder.csv"
SC_CSV = OUT + r"\SkillCoin.csv"
OUT_PKG = _U.G_DATA
SG_STRUCT = "/Script/AsdaReborn.AsdaSoulGuarderRow"
SC_STRUCT = "/Script/AsdaReborn.AsdaSkillCoinRow"
HDR = 20
SG_REC, SC_REC = 1956, 20

# Имена 27 строковых полей с +228 (по содержимому строк; порядок = порядок в файле).
SG_STR_FIELDS = [
    "Model", "AniCall", "AniOff", "AniIdle0", "AniIdle1",
    "AniBattle0", "AniBattle1", "AniBattle2", "AniBattle3", "AniMove",
    "AniSlot10", "AniSlot11", "EffSkill1", "EffSkill2", "EffSkill3",
    "SndCall", "SndOff", "SndIdle0", "SndIdle1", "SndBattleIdle",
    "SndAttack0", "SndAttack1", "SndAttack2", "SndAttack3", "SndAttack4",
    "SndMove", "Portrait",
]


def log(m):
    unreal.log_warning("[soulguard-dt] %s" % m)


def cstr(b, o, n=64):
    """Строка char[n] с нулевым терминатором. Текст в RU-сборке — cp1251 (имена хранителей);
    тело — общий `asda_bintable.cstr`."""
    return _BT.cstr(b, o, n, "cp1251", "replace")


def csv_cell(s):
    """Экранирование для CSV DataTable: кавычки удваиваются."""
    return '"%s"' % s.replace('"', '""')


def read_soulguarder():
    b = open(SG_BIN, "rb").read()
    ver, cnt, fields = struct.unpack_from("<3i", b, 0)
    assert b[12:15] == b"SGT", "не SoulGuarderTable.BIN (нет магии SGT)"
    assert len(b) == HDR + SG_REC * cnt, "размер не сходится: %d != %d" % (len(b), HDR + SG_REC * cnt)
    assert fields == 39, "неожиданное число колонок: %d (ждали 39)" % fields
    rows = []
    for i in range(cnt):
        r = HDR + SG_REC * i
        idx, stype, rowi, buffid = struct.unpack_from("<4i", b, r)
        u144, u148, gskill = struct.unpack_from("<3i", b, r + 144)
        u220, u224 = struct.unpack_from("<2i", b, r + 220)
        strs = [cstr(b, r + 228 + 64 * k) for k in range(27)]
        rows.append(dict(SoulGuardIndex=idx, SkillType=stype, RowIndex=rowi, BuffSkillId=buffid,
                         Name=cstr(b, r + 16), IconBig=cstr(b, r + 80),
                         Unk144=u144, Unk148=u148, GuardianSkillId=gskill,
                         IconSmall=cstr(b, r + 156), Unk220=u220, Unk224=u224,
                         **dict(zip(SG_STR_FIELDS, strs))))
    return rows


def read_skillcoin():
    b = open(SC_BIN, "rb").read()
    ver, cnt, fields = struct.unpack_from("<3i", b, 0)
    assert b[12:15] == b"SGP", "не SkillCoinTable.bin (нет магии SGP)"
    assert len(b) == HDR + SC_REC * cnt, "размер не сходится: %d != %d" % (len(b), HDR + SC_REC * cnt)
    assert fields == 5, "неожиданное число колонок: %d (ждали 5)" % fields
    rows = []
    for i in range(cnt):
        sid, c0, c1, c2, f16 = struct.unpack_from("<5i", b, HDR + SC_REC * i)
        rows.append(dict(SkillId=sid, Coin0=c0, Coin1=c1, Coin2=c2, Field16=f16))
    return rows


def sg_csv(rows):
    cols = ["SoulGuardIndex", "SkillType", "RowIndex", "BuffSkillId", "Name", "IconBig",
            "Unk144", "Unk148", "GuardianSkillId", "IconSmall", "Unk220", "Unk224"] + SG_STR_FIELDS
    out = ["_row," + ",".join(cols)]
    for r in rows:
        cells = [str(r["SoulGuardIndex"])]
        for c in cols:
            v = r[c]
            cells.append(str(v) if isinstance(v, int) else csv_cell(v))
        out.append(",".join(cells))
    return "\n".join(out) + "\n"


def sc_csv(rows):
    cols = ["SkillId", "Coin0", "Coin1", "Coin2", "Field16"]
    out = ["_row," + ",".join(cols)]
    for r in rows:
        out.append(",".join([str(r["SkillId"])] + [str(r[c]) for c in cols]))
    return "\n".join(out) + "\n"


def build(csv_text, csv_path, row_struct, name):
    with open(csv_path, "w", encoding="utf-8", newline="") as f:
        f.write(csv_text)
    log("CSV → %s (%d строк)" % (csv_path, csv_text.count("\n") - 1))
    rs = unreal.load_object(None, row_struct)
    if not rs:
        log("!! RowStruct %s НЕ НАЙДЕН — нужна СБОРКА C++ (Public/Data/AsdaSoulGuardRows.h). DT не создан." % row_struct)
        return
    full = "%s/%s" % (OUT_PKG, name)
    dt = _DTC.open_or_create_dt(unreal, OUT_PKG, name, rs)  # дедуп №2 В2 Т4: общий «открыть-или-создать»
    if not dt:
        log("!! DataTable %s не создан" % name)
        return
    problems = _DTC.fill_and_save(unreal, dt, csv_text, log, fmt="csv")  # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т4: провал заливки → DT не сохраняется; маркер ASDA_WRITES_ASSET
    log("%s: строк=%d, заполнение=%s" % (name, len(dt.get_row_names()), problems))


def main():
    build(sg_csv(read_soulguarder()), SG_CSV, SG_STRUCT, "DT_SoulGuarder")
    build(sc_csv(read_skillcoin()), SC_CSV, SC_STRUCT, "DT_SkillCoin")


if __name__ == "__main__":
    main()
