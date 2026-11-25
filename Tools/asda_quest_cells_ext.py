# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""Таблица расширения ячеек NPC `QuestNpcTableExt.Bin` — чтение и запись (31.10.2026, bug-quest-follow).

ЗАЧЕМ. Запись NPC в `QuestNpcTable.Bin` (1774 б) держит ровно 10 ячеек «ВЫДАЁТ» (+34 + 86·k) и 10 «ПРИНИМАЕТ» (+894 + 88·k),
NpcServer `Start_Quest`/`Process_Quest` отказывают при слоте > 9. Патч AddonNPC N22 (`AstoryServer/Patches/AddonNPC/src/patches/
n22_quest_cells_ext.cpp`) берёт ячейки со слотом ≥ 10 из этого файла; формат записи 1774 б не меняется. Тот же файл лежит у
клиента (`Data/NPC/QuestNpcTableExt.Bin`): импорт `DT_NpcSnq` дописывает ячейки в списки строки NPC по индексу = слот.

ФОРМАТ (описание патча — `Reports/bug_quest_follow/N22_PATCH_DESIGN.md` §4):
  шапка 16 б:  +0 "QNTX", +4 u32 версия 1, +8 u32 число записей, +12 u32 0
  запись 96 б: +0 i16 npcId, +2 u8 вид (0 «ВЫДАЁТ», 1 «ПРИНИМАЕТ»), +3 u8 0, +4 u16 слот (10…127), +6 u16 0,
               +8 ячейка 88 б байт в байт как в записи NPC («ВЫДАЁТ» — 86 б + 2 нуля)
Только функции, без побочных эффектов при импорте.
"""
import struct

MAGIC, VERSION = b"QNTX", 1
HDR, REC = 16, 96
GIVE, TAKE = 0, 1
CELL_LEN = {GIVE: 86, TAKE: 88}
SLOT_MIN, SLOT_MAX = 10, 127
FILE_NAME = "QuestNpcTableExt.Bin"


def parse(b):
    """bytes → [dict(npc, kind, slot, cell)]; ValueError при любом нарушении формата (как отказ патча N22)."""
    if len(b) < HDR or b[:4] != MAGIC:
        raise ValueError("нет шапки QNTX")
    ver, n, _ = struct.unpack_from("<III", b, 4)
    if ver != VERSION:
        raise ValueError("версия %d (ждали %d)" % (ver, VERSION))
    if len(b) != HDR + REC * n:
        raise ValueError("размер %d ≠ 16 + 96·%d" % (len(b), n))
    out, seen = [], set()
    for i in range(n):
        o = HDR + REC * i
        npc, kind, z, slot, z2 = struct.unpack_from("<hBBHH", b, o)
        if npc < 0 or kind not in CELL_LEN or z or z2 or not SLOT_MIN <= slot <= SLOT_MAX:
            raise ValueError("запись %d: npc %d вид %d слот %d" % (i, npc, kind, slot))
        if (npc, kind, slot) in seen:
            raise ValueError("запись %d: повтор (npc %d, вид %d, слот %d)" % (i, npc, kind, slot))
        seen.add((npc, kind, slot))
        out.append(dict(npc=npc, kind=kind, slot=slot, cell=bytes(b[o + 8:o + 8 + CELL_LEN[kind]])))
    return out


def build(rows):
    """[dict(npc, kind, slot, cell)] → bytes (порядок: npc, вид, слот). Ячейка «ВЫДАЁТ» 86 б дополняется двумя нулями."""
    rows = sorted(rows, key=lambda r: (r["npc"], r["kind"], r["slot"]))
    out = bytearray(MAGIC + struct.pack("<III", VERSION, len(rows), 0))
    for r in rows:
        cell = bytes(r["cell"])
        assert len(cell) == CELL_LEN[r["kind"]] and SLOT_MIN <= r["slot"] <= SLOT_MAX
        out += struct.pack("<hBBHH", r["npc"], r["kind"], 0, r["slot"], 0) + cell + bytes(88 - len(cell))
    parse(bytes(out))
    return bytes(out)


def cell_quest(r):
    """Номер задания ячейки (+0 i16)."""
    return struct.unpack_from("<h", r["cell"], 0)[0]


def cell_step(r):
    """Шаг ячейки «ПРИНИМАЕТ» (+66 i16); у «ВЫДАЁТ» шага нет — None."""
    return struct.unpack_from("<h", r["cell"], 66)[0] if r["kind"] == TAKE else None


def cell_keys(r):
    """Пять ключей ConditionTable: «ПРИНИМАЕТ» +68, «ВЫДАЁТ» +66."""
    return list(struct.unpack_from("<5i", r["cell"], 68 if r["kind"] == TAKE else 66))
