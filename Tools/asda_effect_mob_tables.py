# Copyright (c) 2026 @sitxovski (Discord)
# asda_effect_mob_tables.py — ОБЩИЙ парсер трёх «досок данных» оригинала (чистый python, без unreal).
#   Data/Effect/effect list.bin      → EFF  (реестр имён эффектов; загрузчика в клиенте НЕТ)
#   Data/Mob/CallMobTable.BIN        → CMT  (предмет-призывалка → моб)
#   Data/Mob/Monster_SkillEffect.BIN → MSE  (эффекты скиллов монстров)
# Раскладки — см. Source/AsdaReborn/Public/Data/AsdaEffectMobTableRows.h (там же адреса эталона).
# ★ ПРАВИЛО tools-no-import-side-effects: при импорте модуля НИЧЕГО не выполняется.
# ★ Исходные .bin в Content НЕ кладём — читаем прямо из клиента (SRC_ROOT).
import os
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import struct

SRC_ROOT = _P.DATA_WOS
# ★ ПРОМЕЖУТОЧНЫЙ CSV НЕ КЛАДЁМ В Content. Редактор следит за каталогом Content и АВТО-ИМПОРТИРУЕТ
#   любой .csv/.json оттуда, заводя ассет-двойник рядом с настоящим DT_* (так появились
#   `/Game/AsdaAssets/Data/CallMob`, `…/EffectList`, `…/MonsterSkillEffect` — копии, которые никто
#   не использует, но которые редактор перечитывает и ругается на них при каждом расхождении
#   данных со структурой). CSV — это ВХОД импортёра, а не ассет: его место вне Content.
OUT_DATA = os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "_out")

BIN_EFFECT_LIST = os.path.join(SRC_ROOT, "Effect", "effect list.bin")
BIN_CALL_MOB = os.path.join(SRC_ROOT, "Mob", "CallMobTable.BIN")
BIN_MONSTER_SKILL_FX = os.path.join(SRC_ROOT, "Mob", "Monster_SkillEffect.BIN")

import asda_bintable as _BT   # общий заголовок/C-строки .bin (волна 2 аудита дублей)

HDR = 20  # общий заголовок семейства: float@0, count i32@4, columns i32@8, fourcc@12, i32@16


def read_header(blob, magic):
    """Проверяет магию и возвращает (count, columns). Магия — 3 буквы + \\0 на +12.
    Заголовок и C-строка — общий `asda_bintable` (волна 2)."""
    assert len(blob) >= HDR, "файл короче заголовка"
    assert _BT.tag_is(blob, magic), "не %s: магия %r" % (magic.decode(), blob[12:16])
    h = _BT.read_header(blob)
    return h.count, h.extra


def cstr(blob, off, size):
    """ASCIIZ из фиксированного поля: только до первого \\0 (хвост поля — мусор старых записей)."""
    return _BT.cstr(blob, off, size, "latin1")


def hexs(blob, off, size):
    return blob[off:off + size].hex()


def csv_escape(s):
    return '"%s"' % s.replace('"', '""')


# --------------------------------------------------------------------------- EFF
def parse_effect_list(path=BIN_EFFECT_LIST):
    """452 б = 20 хедер + КОЛОНОЧНО: char Name[12][32] (384) + int32 Flag[12] (48).
    Раскладка выведена по размеру файла и по Columns=2 — загрузчика в клиенте нет (см. отчёт)."""
    blob = open(path, "rb").read()
    count, columns = read_header(blob, b"EFF")
    assert columns == 2, "ожидалось 2 колонки, получено %d" % columns
    assert len(blob) == HDR + count * 32 + count * 4, \
        "размер не сходится: %d != %d" % (len(blob), HDR + count * 36)
    names_at, flags_at = HDR, HDR + count * 32
    rows = []
    for i in range(count):
        name = cstr(blob, names_at + 32 * i, 32)
        flag = struct.unpack_from("<i", blob, flags_at + 4 * i)[0]
        raw = hexs(blob, names_at + 32 * i, 32) + hexs(blob, flags_at + 4 * i, 4)
        rows.append(dict(Index=i, EffectName=name, Flag=flag, DataHex=raw))
    return rows


EFFECT_LIST_CSV_HEADER = "_row,Index,EffectName,Flag,DataHex"


def effect_list_csv(rows):
    out = [EFFECT_LIST_CSV_HEADER]
    for r in rows:
        out.append("%d,%d,%s,%d,%s" % (r["Index"], r["Index"], csv_escape(r["EffectName"]),
                                       r["Flag"], csv_escape(r["DataHex"])))
    return "\n".join(out) + "\n"


# --------------------------------------------------------------------------- CMT
CALL_MOB_REC = 32
# ★ Имена колонок = имена UPROPERTY в FAsdaCallMobRow (Public/Data/AsdaEffectMobTableRows.h).
#   Порядок = порядок байтов записи. Смысл подтверждён ДВАЖДЫ: клиентом (objListFindByTwoKeys
#   @0xF77000 + game_main.c:10211) и сервером (CallMobTable.txt, Recv_RaidMonsterCallReq @0x428C50).
#   Расхождение этого списка со структурой = ошибки импорта «Property … cannot be found in struct».
CALL_MOB_FIELDS = ["Index", "NpcId", "MaterialId", "NeedCount",
                   "SpawnArgByte", "MapId", "SpawnArgWord", "BossSlotIndex"]


def parse_call_mob(path=BIN_CALL_MOB):
    """Запись 32 б = 8×int32 (mobLoadCallMobTable @0xF76D10: new[](32*count), fread(rows,0x20,count))."""
    blob = open(path, "rb").read()
    count, columns = read_header(blob, b"CMT")
    assert columns == 8, "ожидалось 8 колонок, получено %d" % columns
    assert len(blob) == HDR + CALL_MOB_REC * count, \
        "размер не сходится: %d != %d" % (len(blob), HDR + CALL_MOB_REC * count)
    rows = []
    for i in range(count):
        o = HDR + CALL_MOB_REC * i
        vals = struct.unpack_from("<8i", blob, o)
        r = dict(zip(CALL_MOB_FIELDS, vals))
        r["DataHex"] = hexs(blob, o, CALL_MOB_REC)
        rows.append(r)
    return rows


CALL_MOB_CSV_HEADER = "_row," + ",".join(CALL_MOB_FIELDS) + ",DataHex"


def call_mob_csv(rows):
    out = [CALL_MOB_CSV_HEADER]
    for i, r in enumerate(rows):
        out.append("%d,%s,%s" % (i, ",".join(str(r[f]) for f in CALL_MOB_FIELDS),
                                 csv_escape(r["DataHex"])))
    return "\n".join(out) + "\n"


# --------------------------------------------------------------------------- MSE
MSE_REC = 0x2D8            # 728 — Mob_LoadSkillEffects @0xF76770: fread(rows, 0x2D8, count)
MSE_JEB = 0x004            # +0x004 + 64*i  (skillCloneEntryByIndex: 728*a2 + 64*i + 8, база на 4 больше)
MSE_BONE = 0x14E           # +0x14E + 64*i  (…+ 64*i + 338)
MSE_SLOT_TYPE = 0x144      # +0x144 + i     (…+ i + 328)
MSE_SLOT_PARAM = 0x149     # +0x149 + i     (…+ i + 333)
MSE_RANGE_A = 0x2D0        # float (дефолт 500.0 в sub_F76BA0)
MSE_RANGE_B = 0x2D4        # float (дефолт 800.0)
MSE_ZERO_ZONE = (0x28E, 0x2D0)  # 66 байт: нули во всех записях (проверено)
MSE_SLOTS = 5


def parse_monster_skill_effect(path=BIN_MONSTER_SKILL_FX):
    blob = open(path, "rb").read()
    count, columns = read_header(blob, b"MSE")
    assert len(blob) == HDR + MSE_REC * count, \
        "размер не сходится: %d != %d" % (len(blob), HDR + MSE_REC * count)
    rows = []
    for i in range(count):
        o = HDR + MSE_REC * i
        r = dict(EffectId=struct.unpack_from("<H", blob, o)[0],
                 Reserved2=struct.unpack_from("<H", blob, o + 2)[0],
                 RangeA=struct.unpack_from("<f", blob, o + MSE_RANGE_A)[0],
                 RangeB=struct.unpack_from("<f", blob, o + MSE_RANGE_B)[0],
                 DataHex=hexs(blob, o, MSE_REC))
        for k in range(MSE_SLOTS):
            r["Jeb%d" % k] = cstr(blob, o + MSE_JEB + 64 * k, 64)
            r["Bone%d" % k] = cstr(blob, o + MSE_BONE + 64 * k, 64)
            r["SlotType%d" % k] = blob[o + MSE_SLOT_TYPE + k]
            r["SlotParam%d" % k] = blob[o + MSE_SLOT_PARAM + k]
        rows.append(r)
    return rows


MSE_FIELDS = (["EffectId", "Reserved2"]
              + ["Jeb%d" % k for k in range(MSE_SLOTS)]
              + ["SlotType%d" % k for k in range(MSE_SLOTS)]
              + ["SlotParam%d" % k for k in range(MSE_SLOTS)]
              + ["Bone%d" % k for k in range(MSE_SLOTS)]
              + ["RangeA", "RangeB"])
MSE_CSV_HEADER = "_row," + ",".join(MSE_FIELDS) + ",DataHex"
_MSE_STR_FIELDS = set(["Jeb%d" % k for k in range(MSE_SLOTS)] + ["Bone%d" % k for k in range(MSE_SLOTS)])


def _mse_cell(r, f):
    if f in _MSE_STR_FIELDS:
        return csv_escape(r[f])
    if f in ("RangeA", "RangeB"):
        return repr(float(r[f]))
    return str(r[f])


def monster_skill_effect_csv(rows):
    out = [MSE_CSV_HEADER]
    for i, r in enumerate(rows):
        out.append("%d,%s,%s" % (i, ",".join(_mse_cell(r, f) for f in MSE_FIELDS),
                                 csv_escape(r["DataHex"])))
    return "\n".join(out) + "\n"


# --------------------------------------------------------------------------- общее
TABLES = {
    "EffectList": dict(bin=BIN_EFFECT_LIST, parse=parse_effect_list, csv=effect_list_csv,
                       csv_path=os.path.join(OUT_DATA, "EffectList.csv"),
                       row_struct="/Script/AsdaReborn.AsdaEffectListRow",
                       pkg="/Game/AsdaAssets/Data", name="DT_EffectList"),
    "CallMob": dict(bin=BIN_CALL_MOB, parse=parse_call_mob, csv=call_mob_csv,
                    csv_path=os.path.join(OUT_DATA, "CallMob.csv"),
                    row_struct="/Script/AsdaReborn.AsdaCallMobRow",
                    pkg="/Game/AsdaAssets/Data", name="DT_CallMob"),
    "MonsterSkillEffect": dict(bin=BIN_MONSTER_SKILL_FX, parse=parse_monster_skill_effect,
                               csv=monster_skill_effect_csv,
                               csv_path=os.path.join(OUT_DATA, "MonsterSkillEffect.csv"),
                               row_struct="/Script/AsdaReborn.AsdaMonsterSkillEffectRow",
                               pkg="/Game/AsdaAssets/Data", name="DT_MonsterSkillEffect"),
}
