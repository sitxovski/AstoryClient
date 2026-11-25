# Copyright (c) 2026 @sitxovski (Discord)
# _probe_quest_turnin.py — ЗОНД: почему шаг СДАЧИ квеста не срабатывает.
#
# Отвечает на вопросы, которые нельзя решать «по документации»:
#   1) сколько под-записей шага являются «сдачей» (есть блок цели, нет `AcceptQuestId`);
#   2) у скольких из них блок условия ПУСТ (задано = 0 категорий) — такой шаг у эталона
#      `netSerializeCharStatPacketB@0xD88F00` возвращает FALSE ВСЕГДА (`a3 == v7 && a3 > 0`),
#      то есть кнопка не делает ничего;
#   3) какое `quest_GetEntryProgressState` даёт такой шаг (4 = сдать, 2 = попап наград);
#   4) для конкретного квеста — у какого NPC он принимается (`SNQInfo.Bin`) и что стоит в его
#      сценарии.
# Запуск: python _probe_quest_turnin.py [номер квеста ...]   (UE не нужен)
import os
import struct
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as _P

NSR_ZIP = os.path.join(_P.DATA, r"NPC\Episode\NPC\Nsr.bsz")
SNQ_BIN = os.path.join(_P.DATA, r"NPC\SNQInfo.Bin")
EPI_BIN = os.path.join(_P.DATA, r"NPC\Episode\Episode_Table.BIN")

BLK_OFF, BLK_LEN = 4, 14352
OFF_NPCID, OFF_ENTRY = 0x00, 0x28
PAGE_STRIDE = 31452
STEP_BASE, STEP_STRIDE, STEP_COUNT = 16, 5228, 6
STEP_SUB_BASE, STEP_SUB_STRIDE, STEP_SUB_COUNT = 68, 1032, 5
OBJ_BASE, OBJ_STRIDE, OBJ_COUNT, OBJ_ACTIVE, OBJ_ACCEPT = 188, 140, 6, 164, 1028

I = lambda b, o: struct.unpack_from("<i", b, o)[0]
U = lambda b, o: struct.unpack_from("<H", b, o)[0]
S = lambda b, o: struct.unpack_from("<h", b, o)[0]


def cats_step(b, c):
    """`questEvalConditionFlags@0xD88990` в смещениях ОТ НАЧАЛА БЛОКА УСЛОВИЯ (эталонные
       смещения минус 76). Категория 4 у ШАГА проверяется «> 0», а не «> -1»."""
    e = []
    if U(b, c + 8) or U(b, c + 6):                             e.append(0)
    if I(b, c + 12) > 0 or I(b, c + 16) > 0:                   e.append(1)
    if I(b, c + 24) > -1 or I(b, c + 20) > -1 or I(b, c + 28) > -1: e.append(2)
    if any(I(b, c + k) > -1 for k in (84, 92, 100, 108, 116)): e.append(3)
    if I(b, c + 124) > 0:                                      e.append(4)
    if I(b, c + 128) > 0 or I(b, c + 132) > 0:                 e.append(5)
    if U(b, c + 142):                                          e.append(6)
    if I(b, c + 144) > 0:                                      e.append(7)
    return e


def target_available(b, o):
    """`questNpc_IsTargetAvailable@0xD894C0`."""
    if U(b, o + 24) == 0 and U(b, o + 90) == 0 and I(b, o + 136) >= 0:
        return True
    return U(b, o + 24) == 1 and (I(b, o + 52) > 0 or I(b, o + 60) > 0) and I(b, o + 136) > 0


def progress_state(b, sb_step):
    """`quest_GetEntryProgressState@0xD892D0` для ШАГА (sb_step — база шага)."""
    for i in range(STEP_SUB_COUNT):
        sb = sb_step + STEP_SUB_BASE + STEP_SUB_STRIDE * i
        if I(b, sb + OBJ_ACCEPT) > 0:
            return 1
        for j in range(OBJ_COUNT):
            if I(b, sb + OBJ_ACTIVE + 4 * j) > 0:
                return 4 if target_available(b, sb + OBJ_BASE + OBJ_STRIDE * j) else 2
        if I(b, sb + 4) == 5:
            return 3
    return 0


def read_snq():
    b = open(SNQ_BIN, "rb").read()
    n = I(b, 0x14)
    base = 0x18
    # шапка: тег "NPC" + count по 0x14 (см. asda_import_npc_tables.check)
    while base + 1796 * n > len(b):
        base -= 4
    rows = []
    for i in range(n):
        r = base + 1796 * i
        rows.append(dict(
            NpcId=S(b, r),
            Give=[S(b, r + 36 + 88 * k) for k in range(10)],
            Take=[S(b, r + 916 + 88 * k) for k in range(10)],
            GiveKeys=[[I(b, r + 36 + 88 * k + 68 + 4 * j) for j in range(5)] for k in range(10)],
            TakeKeys=[[I(b, r + 916 + 88 * k + 68 + 4 * j) for j in range(5)] for k in range(10)],
        ))
    return rows


def main():
    want = [int(a) for a in sys.argv[1:]]
    z = zipfile.ZipFile(NSR_ZIP)
    names = sorted(n for n in z.namelist() if n.lower().endswith(".nsr"))

    total_sub = 0
    turnin_sub = 0
    turnin_empty_cond = 0
    turnin_by_prog = {}
    accept_sub = 0
    accept_empty_cond = 0
    empty_examples = []

    for nm in names:
        blob = z.read(nm)
        blk = blob[BLK_OFF:BLK_OFF + BLK_LEN]
        npc_id = S(blk, OFF_NPCID)
        pages = I(blk, OFF_ENTRY)
        pbase = BLK_OFF + BLK_LEN
        for p in range(pages):
            po = pbase + PAGE_STRIDE * p
            if po + PAGE_STRIDE > len(blob):
                break
            talk_id = S(blob, po + 2)
            nsteps = S(blob, po + 14)
            for s in range(min(nsteps, STEP_COUNT)):
                so = po + STEP_BASE + STEP_STRIDE * s
                prog = progress_state(blob, so)
                for m in range(STEP_SUB_COUNT):
                    sb = so + STEP_SUB_BASE + STEP_SUB_STRIDE * m
                    state = I(blob, sb + 4)
                    accept = I(blob, sb + OBJ_ACCEPT)
                    active = [I(blob, sb + OBJ_ACTIVE + 4 * j) for j in range(OBJ_COUNT)]
                    if state == 0 and accept <= 0 and not any(v > 0 for v in active):
                        continue
                    total_sub += 1
                    cs = cats_step(blob, sb + 8)
                    if accept > 0:
                        accept_sub += 1
                        if not cs:
                            accept_empty_cond += 1
                        continue
                    if any(v > 0 for v in active):
                        turnin_sub += 1
                        turnin_by_prog[prog] = turnin_by_prog.get(prog, 0) + 1
                        if not cs and state != 1:
                            turnin_empty_cond += 1
                            if len(empty_examples) < 25:
                                qid = I(blob, sb + OBJ_BASE + 4)
                                empty_examples.append(
                                    "%s npc=%d talk=%d шаг=%d под=%d state=%d prog=%d questId=%d"
                                    % (nm, npc_id, talk_id, s, m, state, prog, qid))

    print("── ВСЕГО по 187 сценариям ─────────────────────────────────────────")
    print("  значащих под-записей шага      : %d" % total_sub)
    print("  под-записей ВЫДАЧИ  (accept>0) : %d, из них с ПУСТЫМ условием: %d" % (accept_sub, accept_empty_cond))
    print("  под-записей СДАЧИ   (цель>0)   : %d, из них с ПУСТЫМ условием: %d" % (turnin_sub, turnin_empty_cond))
    print("  сдача по quest_GetEntryProgressState: %s" % sorted(turnin_by_prog.items()))
    if empty_examples:
        print("── примеры СДАЧИ с пустым условием (кнопка не сработает никогда) ──")
        for e in empty_examples:
            print("   " + e)

    if want:
        snq = read_snq()
        for q in want:
            print("\n── КВЕСТ %d ───────────────────────────────────────────────────" % q)
            for r in snq:
                if q in r["Give"]:
                    k = r["Give"].index(q)
                    print("  ВЫДАЁТ npc %d, слот выдачи %d, ключи %s" % (r["NpcId"], k, r["GiveKeys"][k]))
                if q in r["Take"]:
                    k = r["Take"].index(q)
                    print("  ПРИНИМАЕТ npc %d, слот приёма %d, ключи %s" % (r["NpcId"], k, r["TakeKeys"][k]))
            # где этот квест упоминается в блоках цели сценариев
            for nm in names:
                blob = z.read(nm)
                blk = blob[BLK_OFF:BLK_OFF + BLK_LEN]
                npc_id = S(blk, OFF_NPCID)
                pages = I(blk, OFF_ENTRY)
                pbase = BLK_OFF + BLK_LEN
                for p in range(pages):
                    po = pbase + PAGE_STRIDE * p
                    if po + PAGE_STRIDE > len(blob):
                        break
                    talk_id = S(blob, po + 2)
                    nsteps = S(blob, po + 14)
                    for s in range(min(nsteps, STEP_COUNT)):
                        so = po + STEP_BASE + STEP_STRIDE * s
                        for m in range(STEP_SUB_COUNT):
                            sb = so + STEP_SUB_BASE + STEP_SUB_STRIDE * m
                            hits = []
                            for j in range(OBJ_COUNT):
                                ob = sb + OBJ_BASE + OBJ_STRIDE * j
                                if I(blob, ob + 4) == q and I(blob, sb + OBJ_ACTIVE + 4 * j) > 0:
                                    hits.append((j, U(blob, ob + 24), U(blob, ob + 90), I(blob, ob + 136),
                                                 I(blob, ob + 52), I(blob, ob + 60)))
                            if not hits:
                                continue
                            cs = cats_step(blob, sb + 8)
                            print("  СДАЧА в %s (npc %d) talk=%d шаг=%d под=%d state=%d prog=%d "
                                  "условие=%s цели=%s res=%d/%d/%d"
                                  % (nm, npc_id, talk_id, s, m, I(blob, sb + 4),
                                     progress_state(blob, so), cs or "ПУСТО", hits,
                                     U(blob, sb + 156), U(blob, sb + 158), U(blob, sb + 160)))


if __name__ == "__main__":
    main()
