# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""Восстановление сдачи 16 непроходимых заданий — [ИСПРАВЛЕНИЕ] дефекта данных оригинала (31.10.2026, bug-quest-follow).

ЗАЧЕМ. Задания 70, 72, 75, 85, 86, 96, 113, 125, 126, 127, 129, 151, 154, 156, 157, 241 сервер выдаёт, но завершить их нельзя:
  у нужной стадии нет ячейки «ПРИНИМАЕТ» в `QuestNpcTable.Bin`, и у NPC нет страницы диалога сдачи. Ни в одной версии данных
  (серверы Arabic/XYT, клиенты эталон/MINE/orig_copy/Fantazium/Arabic/XYT/WoD/Global/Fable, бэкапы стойки) этих ячеек и страниц
  нет. Награды при этом есть: клиентский `RewardTable.BIN` (= GameServer `ItemTable_Info\\RepayTable.txt`) хранит (задание,
  стадия) каждой награды, и на них не ссылается ни одно условие. Разбор — `Reports/bug_quest_follow/turnin_undetermined.txt`.

ЧТО ДЕЛАЕТ (решение владельца «Б»: ячейки + страницы с новыми репликами, тексты утверждены 31.10):
  * СЕРВЕР: дописывает в `ConditionTable.Bin` новые ключи (146 б, ключ = номер записи, загрузчик NpcServer `sub_421360` кладёт
    запись по полю +0) по образцу ключа 41 (сдача задания 2): +4 страница после сдачи, +8 страница облачка (TalkId), +10 = 1 и
    +11+4·k — номера наград (вариант выбора k), +35 действие (1 — стадия +1, 2 — завершение), +37 мин. уровень (как у ключа
    выдачи задания), +57 задание, +65 стадия; и ставит ячейку «ПРИНИМАЕТ» в свободный слот NPC (+894 + 88·k: +0 задание, +2 имя —
    как у ячейки выдачи этого задания, +66 шаг, +68 ключ). Правило приёма — NpcServer `Process_Quest@0x420C10`.
  * КЛИЕНТ: страницы NSR сдачи (`data/nsr_turnin_pages.json` — их дописывает `asda_import_nsr_dialog.py`), EN-реплики (ведомость
    `data/loc/en/NpcNsrPage.json`), перенаправление сценариев Эллы на новые страницы (`data/nsr_script_turnin_fixes.json`,
    `asda_import_npc_quest_data.py`). Ячейки и ключи в клиентские SNQInfo/SNQICT — тем же путём, что у сервера:
    `asda_snq_server_sync.py --server-dir <выход> --apply` (слот ячейки клиента = слот сервера: клиент шлёт его в 5041).
  * ПРОВЕРКА: модель сервера проходит каждое задание от взятия до завершения (Start_Quest → мобы MonsterConditionTable →
    ячейки сдачи), плюс регресс: ни одна прежняя пара (задание, шаг) не получила нового или чужого NPC.

Запуск:  python asda_quest_turnin_restore.py --server-dir <копия NpcServer (Quest\\…)> [--out <папка выхода>]
Выход по умолчанию — AstoryServer/Patches/AsdaPatch/tools/data_quest_turnin/ (QuestNpcTable.Bin, ConditionTable.Bin, spec.json).
"""
import argparse
import collections
import copy
import hashlib
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as _P
import asda_quest_cells_ext as QX

HERE = os.path.dirname(os.path.abspath(__file__))
RAW = os.path.join(_P.PORT_DIR, "AstoryClient", "RawAssets", "Data")
OUT_DEF = os.path.join(_P.PORT_DIR, "AstoryServer", "Patches", "AsdaPatch", "tools", "data_quest_turnin")
PAGES_LEDGER = os.path.join(HERE, "data", "nsr_turnin_pages.json")
SCRIPT_LEDGER = os.path.join(HERE, "data", "nsr_script_turnin_fixes.json")
EN_LEDGER = os.path.join(HERE, "data", "loc", "en", "NpcNsrPage.json")
NPC_REC, TAKE, GIVE, CELL, GCELL, COND = 1774, 894, 34, 88, 86, 146
TEMPLATE_KEY = 41          # сдача задания 2 у Сюрена (91): без целей, награда, действие 2
TEMPLATE_PAGE = ("shuren", 7)
CAPTION_RU, CAPTION_EN = "Получение награды", "Receive Reward"   # существующие подписи (shuren_7, ellosa_23 / melve_45)

# (задание, шаг, NPC, nsr, TalkId страницы, действие, [награды], RU, EN). Тексты утверждены владельцем 31.10.2026.
SPEC = [
    (70, 1, 92, "yazark", 7, 2, [95],
     "Ты нашел меч! Значит, это и вправду были клыкастые люди-грибы.\nСпасибо. Я знал, что на тебя можно положиться. Ха-ха-ха.",
     "You found the sword! So it really was the Fang Mushroom Men.\nThank you. I knew I could count on you. Ha, ha, ha."),
    (72, 0, 92, "yazark", 6, 1, [119],
     "О, это тот самый меч? Как новенький!\nХе-хе. Он принадлежит господину Моргану — отнеси его ему.",
     "Oh, is this the sword? It looks brand new!\nHa, ha. It belongs to Mr. Mogan — take it to him."),
    (72, 1, 27, "mogan", 11, 2, [98],
     "Это же мой меч! Совсем как новый.\nХа-ха-ха! Спасибо тебе.",
     "That's my sword! It's as good as new.\nHa, ha, ha! Thank you."),
    (75, 0, 107, "ella", 19, 2, [102],
     "Тебя прислал Ротон?\nНеведомая злая сила управляет чудовищами Страны Вечных Войн... Это очень важные сведения. Спасибо, [NAME].",
     "Roton sent you?\nAn unknown evil power controlling the monsters of Conquest Land... This is very important news. Thank you, [NAME]."),
    (85, 0, 107, "ella", 18, 2, [129],
     "Это дневник?\nСведения о чудовищах Долины Ночи... Он очень поможет королевству. Спасибо, [NAME].",
     "Is this the diary?\nInformation about the Night Valley monsters... It will be a great help to the kingdom. Thank you, [NAME]."),
    (86, 0, 27, "mogan", 10, 2, [130],
     "Приказ командира рыцарей Эллы?\nЧудовища в Долине Ночи... Ясно. Передай ей, что приказ будет выполнен.",
     "An order from Knight Captain Ella?\nMonsters in the Night Valley... I see. Tell her the order will be carried out."),
    (96, 0, 56, "ellosa", 14, 2, [191],
     "Письмо для меня?\nХм... Подозрительное письмо. Спасибо, что принес его. Я постараюсь узнать о нем правду.",
     "A letter for me?\nHmm... It's a suspicious letter. Thank you for bringing it. I'll try to find out the truth about it."),
    (113, 0, 26, "melve", 10, 2, [154],
     "Опять этот приказ?\nТьфу. Ладно, раз его прислал Гладио, давай сюда.",
     "This order again?\nUgh. Fine, if Gllidio sent it, hand it over."),
    (125, 1, 56, "ellosa", 15, 2, [199],
     "Ты принес листья с головодревов?\nСпасибо. Я изучу их и попробую найти источник заражения.",
     "Did you bring the leaves from the ScornPickles?\nThank you. I'll study them and try to find the source of the contamination."),
    (126, 1, 56, "ellosa", 7, 2, [200],
     "Ты расправился с жуткогрибами и крысами-боксерами?\nОтличная работа. Теперь заражение не будет распространяться так быстро.",
     "You took care of the PanicMushrooms and PunchRats?\nGreat job. Now the contamination won't spread so fast."),
    (127, 1, 45, "tid", 7, 2, [202],
     "Всхлипы на Дозорном Холме?\nХм-м... Спасибо, что рассказал мне об этом.",
     "Sobbing on the Watch Hill?\nHmmm... Thank you for telling me about it."),
    (129, 0, 45, "tid", 6, 2, [204],
     "Это письмо... мне?\n«Моей любимой дочери Тиде»...\nСпасибо, что принес его.",
     "This letter... is for me?\n'To my loving daughter Tid'...\nThank you for bringing it."),
    (151, 0, 19, "kurian", 5, 2, [228],
     "Кольцо? Хм... Дай-ка взглянуть.\nДа, мне кое-что о нем известно.",
     "A ring? Hmm... Let me take a look.\nYes, I know a thing or two about it."),
    (154, 0, 131, "131npc", 23, 2, [231],
     "Букет? От Трими?\nКакой милый. Передай ему спасибо.",
     "A bouquet? From Trimi?\nHow sweet. Tell him thank you."),
    # 156/157: награда на выбор — по одной строке RewardTable на предмет (25244…25247 / 25252…25255; у 157 строки 239, 242,
    #   245, 246 — повторы предметов 237/240/241, в варианты не идут).
    (156, 1, 131, "131npc", 24, 2, [233, 238, 243, 244],
     "Золотой мех? Как красиво!\nПередай Трими мою благодарность.",
     "Golden fur? It's beautiful!\nTell Trimi I'm grateful."),
    (157, 1, 131, "131npc", 25, 2, [234, 237, 240, 241],
     "Столько золотого меха? И это от Бритца?\nОх, передай ему спасибо.",
     "So much golden fur? And it's from Britz?\nOh, tell him thank you."),
    (241, 0, 131, "131npc", 10, 2, [338],
     "Эшли хочет узнать про особый подарок? Ха-ха.\nХорошо, я расскажу тебе, а ты передай ей.",
     "Ashley wants to know about the special gift? Ha, ha.\nAll right, I'll tell you, and you pass it on to her."),
]
# ★ Задания, отложенные решением (пусто: 96 сдаётся у Эллосы ячейкой слота 10 — таблица расширения, патч NpcServer N22).
DEFERRED = set()
# ★ [ИСПРАВЛЕНИЕ] Ячейки сверх 10 (решение владельца 31.10: патч NpcServer N22 вместо «обхода 44»). ПРИЧИНА: запись NPC в
#   `QuestNpcTable.Bin` — 1774 б, ячеек «ПРИНИМАЕТ» в ней ровно 10 (+894 + 88·k), `Process_Quest@0x420C10` отказывает при слоте > 9
#   (NpcServer.exe.c:22361). У Эллосы (56) после освобождения мёртвой 104/2 занято 10 из 10 — 11-я ячейка (задание 96) идёт в
#   таблицу расширения `Quest/QuestNpcTableExt.Bin` слотом 10 (формат — `asda_quest_cells_ext.py`); формат записи не меняется.
#   Тот же файл кладётся клиенту (`Data/NPC/QuestNpcTableExt.Bin`) — из него импорт `DT_NpcSnq` дописывает ячейку 10.
EXT_SLOTS = 128          # слоты 0…127 (патч N22: 10…127 — таблица расширения)
# Задания, добавленные после первой раскладки ключей (96): их ключи — в конце, чтобы ключи ConditionTable остальных не сдвигались
#   (клиентский SNQICT и страницы ссылаются на номера ключей).
LATE = (96,)
# Мёртвые ячейки, которые освобождаются ([ИСПРАВЛЕНИЕ]): 104/шаг 2 у Эллосы — задание 104 завершается раньше, у NPC 25 на
#   стадии 1 (ключ 266, действие 2), стадия 2 недостижима (такая же мёртвая ячейка у NPC 42 остаётся как есть).
FREE_CELLS = [(56, 104, 2)]
# Сценарии, указывающие на занятые страницы (Элла 3 — без блока сдачи, 4 — страница Флэммио 566): перенаправить на новые.
SCRIPT_FIX = [("ella", 2, 85, 3, 18), ("ella", 3, 75, 4, 19)]


def md5(b):
    return hashlib.md5(b).hexdigest()


def log(m):
    print("[turnin-restore] %s" % m)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--server-dir", required=True)
    ap.add_argument("--out", default=OUT_DEF)
    ap.add_argument("--write-ledgers", action="store_true", help="записать клиентские ведомости (страницы, сценарии, EN)")
    ap.add_argument("--apply-client", action="store_true", help="записать ячейки и условия в клиентские SNQInfo/SNQICT (бэкап)")
    a = ap.parse_args(argv)
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    qp = os.path.join(a.server_dir, "Quest", "QuestNpcTable.Bin")
    cp = os.path.join(a.server_dir, "Quest", "ConditionTable.Bin")
    qnt0, ct0 = open(qp, "rb").read(), open(cp, "rb").read()
    qnt, ct = bytearray(qnt0), bytearray(ct0)
    nkey0 = len(ct0) // COND
    rewards = {r["RecordIndex"]: r for r in json.load(open(os.path.join(RAW, "NPC", "QuestRewardTable.json"), encoding="utf-8"))}
    # Клиентский SNQInfo — слот ячейки должен быть свободен и у клиента (клиент шлёт номер слота в 5041).
    cli = open(os.path.join(_P.DATA, "NPC", "SNQInfo.Bin"), "rb").read()
    cli_take = {}
    for i in range(struct.unpack_from("<i", cli, 0x14)[0]):
        o = 0x18 + 1796 * i
        npc = struct.unpack_from("<h", cli, o)[0]
        if npc not in cli_take:
            cli_take[npc] = [struct.unpack_from("<h", cli, o + 916 + 88 * k)[0] for k in range(10)]

    ext = []                                   # ячейки таблицы расширения (слот ≥ 10): dict(npc, kind, slot, cell)

    def rec_of(npc):
        """Запись NPC в QuestNpcTable — по полю +0 (загрузчик кладёт по npcId; дублей у этих NPC нет)."""
        hits = [i for i in range(len(qnt) // NPC_REC) if struct.unpack_from("<h", qnt, i * NPC_REC)[0] == npc]
        if len(hits) != 1:
            raise SystemExit("NPC %d: записей %d (нужна ровно одна)" % (npc, len(hits)))
        return hits[0]

    def give_cell(q):
        for i in range(len(qnt) // NPC_REC):
            for k in range(10):
                so = i * NPC_REC + GIVE + GCELL * k
                if struct.unpack_from("<h", qnt, so)[0] == q:
                    keys = [x for x in struct.unpack_from("<5i", qnt, so + 66) if x > 0]
                    return so, keys
        raise SystemExit("задание %d: ячейки выдачи нет" % q)

    def after_page(npc):
        """+4 (страница после сдачи) — самая частая у существующих ключей сдачи этого NPC; нет — 1 (корневая)."""
        r = rec_of(npc)
        c = collections.Counter()
        for k in range(10):
            so = r * NPC_REC + TAKE + CELL * k
            if struct.unpack_from("<h", qnt, so)[0] > 0:
                for key in struct.unpack_from("<5i", qnt, so + 68):
                    if key > 0:
                        c[struct.unpack_from("<h", ct, key * COND + 4)[0]] += 1
        return c.most_common(1)[0][0] if c else 1

    tmpl = bytes(ct0[TEMPLATE_KEY * COND:(TEMPLATE_KEY + 1) * COND])
    # Освобождение мёртвых ячеек — копией пустой ячейки той же записи (байт в байт как пустой слот).
    for npc, q, st in FREE_CELLS:
        r = rec_of(npc)
        empty = next(bytes(qnt[r * NPC_REC + TAKE + CELL * k:r * NPC_REC + TAKE + CELL * (k + 1)]) for k in range(10)
                     if struct.unpack_from("<h", qnt, r * NPC_REC + TAKE + CELL * k)[0] <= 0)
        hit = [k for k in range(10) if struct.unpack_from("<hh", qnt, r * NPC_REC + TAKE + CELL * k)[0] == q
               and struct.unpack_from("<h", qnt, r * NPC_REC + TAKE + CELL * k + 66)[0] == st]
        if len(hit) != 1:
            raise SystemExit("освобождение %s: ячейка не найдена однозначно (%s)" % ((npc, q, st), hit))
        so = r * NPC_REC + TAKE + CELL * hit[0]
        qnt[so:so + CELL] = empty
        if cli_take.get(npc, [0] * 10)[hit[0]] == q:
            cli_take[npc][hit[0]] = 0           # у клиента тот же слот будет очищен в --apply-client
        log("освобождена мёртвая ячейка: NPC %d слот %d (задание %d шаг %d)" % (npc, hit[0], q, st))
    r91 = rec_of(91)
    tcell = None
    for k in range(10):
        so = r91 * NPC_REC + TAKE + CELL * k
        if struct.unpack_from("<h", qnt0, so)[0] == 2:
            tcell = bytes(qnt0[so:so + CELL])
    assert tcell is not None
    spec_out = []
    for q, st, npc, nsr, talk, act, rew, ru, en in sorted(SPEC, key=lambda e: e[0] in LATE):
        if q in DEFERRED:
            log("задание %d шаг %d: ОТЛОЖЕНО (ждёт решения: у NPC %d нет свободной ячейки)" % (q, st, npc))
            continue
        key = len(ct) // COND
        rec = bytearray(tmpl)
        gso, gkeys = give_cell(q)
        lvl = ct0[gkeys[0] * COND + 37]
        struct.pack_into("<i", rec, 0, key)
        aft = after_page(npc)
        struct.pack_into("<h", rec, 4, aft)
        struct.pack_into("<h", rec, 8, talk)
        rec[10] = 1
        for k in range(6):                        # +11 + 4·k: номер награды варианта k, +13 + 4·k: 0
            struct.pack_into("<hh", rec, 11 + 4 * k, rew[k] if k < len(rew) else 0, 0)
        rec[35] = act
        rec[36], rec[37], rec[38] = 1, lvl, 150
        struct.pack_into("<i", rec, 57, q)
        rec[65] = st
        ct += rec
        # Ячейка сдачи: свободный слот 0…9 и у сервера, и у клиента (у клиента — пусто или уже это же задание: повторный
        #   прогон); свободных нет — первый свободный слот ≥ 10 таблицы расширения этого NPC.
        r = rec_of(npc)
        free = [k for k in range(10) if struct.unpack_from("<h", qnt, r * NPC_REC + TAKE + CELL * k)[0] <= 0
                and cli_take.get(npc, [0] * 10)[k] in (0, q)]
        cell = bytearray(tcell)
        struct.pack_into("<h", cell, 0, q)
        cell[2:34] = qnt0[gso + 2:gso + 34]        # имя — как у ячейки выдачи этого задания
        struct.pack_into("<h", cell, 66, st)
        struct.pack_into("<5i", cell, 68, key, 0, 0, 0, 0)
        if free:
            slot = free[0]
            so = r * NPC_REC + TAKE + CELL * slot
            qnt[so:so + CELL] = cell
        else:
            used = {x["slot"] for x in ext if x["npc"] == npc and x["kind"] == QX.TAKE}
            slot = next(k for k in range(QX.SLOT_MIN, EXT_SLOTS) if k not in used)
            ext.append(dict(npc=npc, kind=QX.TAKE, slot=slot, cell=bytes(cell)))
        spec_out.append(dict(quest=q, stage=st, npc=npc, ext=slot >= 10, nsr=nsr, talk=talk, action=act, rewards=rew, key=key,
                             slot=slot, after_page=aft, level=lvl, ru=ru, en=en))
        log("задание %d шаг %d: NPC %d слот %d%s, ключ %d (действие %d, награды %s, уровень %d, страница %d, после %d)"
            % (q, st, npc, slot, " (таблица расширения)" if slot >= 10 else "", key, act, rew, lvl, talk, aft))

    # ── Модель сервера: каждое задание от взятия до завершения ──
    mai = open(os.path.join(a.server_dir, "Monster", "Monster.ai"), "rb").read()
    mct = open(os.path.join(a.server_dir, "Quest", "MonsterConditionTable.Bin"), "rb").read()
    kills = collections.defaultdict(list)      # (задание, шаг) → [(запись MCT, завершает?)]
    for i in range(len(mai) // 479):
        rr = mai[i * 479:(i + 1) * 479]
        for k in range(5):
            o = 403 + 12 * k
            qq = struct.unpack_from("<i", rr, o)[0]
            if qq == -1:
                break
            mi = struct.unpack_from("<h", rr, o + 5)[0]
            m = mct[59 * mi:59 * mi + 59]
            if len(m) == 59 and m[19] == 1:
                kills[(qq, rr[o + 4])].append((mi, struct.unpack_from("<i", m, 51)[0] == 1))

    def take_cells(buf, cbuf, xrows=()):
        """(задание, шаг) → [(NPC, действие)]: ячейки записей и таблицы расширения (патч N22)."""
        out = collections.defaultdict(list)
        for x in xrows:
            if x["kind"] != QX.TAKE or QX.cell_quest(x) <= 0:
                continue
            acts = [cbuf[k * COND + 35] for k in QX.cell_keys(x) if 0 < k < len(cbuf) // COND]
            out[(QX.cell_quest(x), QX.cell_step(x))].append((x["npc"], max(acts) if acts else -1))
        for i in range(len(buf) // NPC_REC):
            npc = struct.unpack_from("<h", buf, i * NPC_REC)[0]
            for k in range(10):
                so = i * NPC_REC + TAKE + CELL * k
                qq = struct.unpack_from("<h", buf, so)[0]
                if qq <= 0:
                    continue
                acts = [cbuf[x * COND + 35] for x in struct.unpack_from("<5i", buf, so + 68) if 0 < x < len(cbuf) // COND]
                out[(qq, struct.unpack_from("<h", buf, so + 66)[0])].append((npc, max(acts) if acts else -1))
        return out

    before, after = take_cells(qnt0, ct0), take_cells(qnt, ct, ext)
    walk_ok = 0
    walks = []
    for q in sorted({s[0] for s in SPEC} - DEFERRED):
        st, path, done = 0, [], False
        for _ in range(10):
            cells = [c for c in after.get((q, st), []) if c[1] in (1, 2)]
            ks = kills.get((q, st), [])
            if cells:
                npc, act = cells[0]
                path.append("шаг %d: сдача NPC %d (%s)" % (st, npc, "завершение" if act == 2 else "стадия +1"))
                if act == 2:
                    done = True
                    break
                st += 1
            elif ks:
                mi, fin = ks[0]
                path.append("шаг %d: мобы (MCT %d)%s" % (st, mi, " → завершение" if fin else " → стадия +1"))
                if fin:
                    done = True
                    break
                st += 1
            else:
                path.append("шаг %d: ТУПИК" % st)
                break
        walk_ok += done
        walks.append((q, done, path))
        log("ПРОХОЖДЕНИЕ %d: %s — %s" % (q, "ЗАВЕРШАЕТСЯ" if done else "НЕ ЗАВЕРШАЕТСЯ", "; ".join(path)))
    freed = {(q, st) for npc, q, st in FREE_CELLS}
    changed = [k for k in before if before[k] != after.get(k) and k not in freed]
    added = sorted(set(after) - set(before))
    log("регресс: прежних пар (задание, шаг) %d, изменилось %d; новых пар %d %s" % (len(before), len(changed), len(added), added))

    os.makedirs(a.out, exist_ok=True)
    open(os.path.join(a.out, "QuestNpcTable.Bin"), "wb").write(bytes(qnt))
    open(os.path.join(a.out, "ConditionTable.Bin"), "wb").write(bytes(ct))
    xb = QX.build(ext)
    open(os.path.join(a.out, QX.FILE_NAME), "wb").write(xb)
    meta = dict(QuestNpcTable=dict(before=md5(qnt0), after=md5(bytes(qnt)), size=len(qnt)),
                ConditionTable=dict(before=md5(ct0), after=md5(bytes(ct)), records_before=nkey0, records_after=len(ct) // COND),
                QuestNpcTableExt=dict(md5=md5(xb), size=len(xb), cells=[[x["npc"], x["kind"], x["slot"], QX.cell_quest(x)] for x in ext]),
                cells=spec_out, walks=[dict(quest=q, completes=d, path=p) for q, d, p in walks],
                regression=dict(changed=len(changed), added=[list(x) for x in added]))
    json.dump(meta, open(os.path.join(a.out, "spec.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    log("QuestNpcTable %s → %s; ConditionTable %s → %s (%d → %d записей); QuestNpcTableExt %s (%d ячеек); выход %s"
        % (meta["QuestNpcTable"]["before"], meta["QuestNpcTable"]["after"], meta["ConditionTable"]["before"],
           meta["ConditionTable"]["after"], nkey0, len(ct) // COND, md5(xb), len(ext), a.out))

    if a.write_ledgers:
        write_client_ledgers(spec_out, rewards)
    if a.apply_client:
        apply_client(spec_out, bytes(ct), xb)
    return 0 if walk_ok == len(walks) and not changed else 1


def apply_client(spec_out, srv_ct, ext_bytes):
    """Клиентские SNQInfo.Bin / SNQICT.Bin рабочего клиента: освободить те же мёртвые ячейки, поставить новые ячейки в ТЕ ЖЕ
    слоты, что у сервера (номер слота уходит в 5041), дописать условия (`asda_snq_server_sync.conv_condition`: раскладка
    сервер → клиент, проверенная на 839 общих ключах)."""
    import asda_snq_server_sync as SY
    from asda_headless_tool import backup_file
    snq_p, ict_p = SY.CLI_SNQ, SY.CLI_ICT
    snq, ict = bytearray(open(snq_p, "rb").read()), bytearray(open(ict_p, "rb").read())
    n_c, bad = SY.verify_map(srv_ct, ict)
    if bad:
        raise SystemExit("SNQICT: отображение полей не сходится (%d) — клиент не трогаю" % bad)
    first = {}
    for i in range(struct.unpack_from("<i", snq, 0x14)[0]):
        o = 0x18 + 1796 * i
        first.setdefault(struct.unpack_from("<h", snq, o)[0], o)
    for npc, q, st in FREE_CELLS:
        o = first[npc]
        for k in range(10):
            so = o + 916 + 88 * k
            if struct.unpack_from("<h", snq, so)[0] == q:
                snq[so:so + 88] = bytes(1) * 88
                log("клиент: освобождена ячейка NPC %d слот %d (задание %d)" % (npc, k, q))
    have = {struct.unpack_from("<i", ict, 0x18 + 156 * i)[0]: i for i in range(struct.unpack_from("<i", ict, 0x14)[0])}
    add = b""
    for s in spec_out:
        if not s["ext"]:                          # слот ≥ 10 — только в таблице расширения (её копия ниже)
            so = first[s["npc"]] + 916 + 88 * s["slot"]
            cur = struct.unpack_from("<h", snq, so)[0]
            if cur not in (0, s["quest"]):
                raise SystemExit("клиент: NPC %d слот %d занят заданием %d" % (s["npc"], s["slot"], cur))
            sub = bytearray(88)
            struct.pack_into("<h", sub, 0, s["quest"])
            struct.pack_into("<h", sub, 66, s["stage"])
            struct.pack_into("<5i", sub, 68, s["key"], 0, 0, 0, 0)
            snq[so:so + 88] = sub
        want = SY.conv_condition(srv_ct[COND * s["key"]:COND * (s["key"] + 1)])
        if s["key"] not in have:
            add += want
        else:
            i = have[s["key"]]
            if bytes(ict[0x18 + 156 * i:0x18 + 156 * (i + 1)]) != want:
                ict[0x18 + 156 * i:0x18 + 156 * (i + 1)] = want
                log("клиент: условие %d (задание %d) переписано по серверу" % (s["key"], s["quest"]))
    n_old = struct.unpack_from("<i", ict, 0x14)[0]
    n_new = n_old + len(add) // 156
    ict = ict[:0x18 + 156 * n_old] + add + ict[0x18 + 156 * n_old:]
    struct.pack_into("<i", ict, 0x14, n_new)
    for p in (snq_p, ict_p):
        log("бэкап: %s" % backup_file(p, "bug-quest-follow", verify="md5"))
    open(snq_p, "wb").write(bytes(snq))
    # Таблица расширения — тот же файл, что у NpcServer (импорт `DT_NpcSnq` дописывает её ячейки в строку NPC).
    xp = os.path.join(os.path.dirname(snq_p), QX.FILE_NAME)
    if os.path.exists(xp):
        log("бэкап: %s" % backup_file(xp, "bug-quest-follow", verify="md5"))
    open(xp, "wb").write(ext_bytes)
    log("клиент: %s %s (%d б)" % (QX.FILE_NAME, md5(ext_bytes), len(ext_bytes)))
    open(ict_p, "wb").write(bytes(ict))
    log("клиент: SNQInfo %s, SNQICT %d → %d записей" % (md5(bytes(snq)), n_old, n_new))


def write_client_ledgers(spec_out, rewards):
    """Страницы NSR сдачи (RU), EN-реплики и перенаправление сценариев Эллы."""
    pages_all = json.load(open(os.path.join(RAW, "NPC", "NpcNsrPage.json"), encoding="utf-8"))
    tpl = next(p for p in pages_all if p["NsrName"].lower() == TEMPLATE_PAGE[0] and p["TalkId"] == TEMPLATE_PAGE[1])
    empty_obj = {k: 0 for k in tpl["Steps"][0]["Subs"][0]["Objs"][2]}
    rows = []
    # Страницы прежнего прогона этой ведомости (импорт уже дописал их в RawAssets) — не коллизия: повторный прогон.
    mine = set()
    if os.path.exists(PAGES_LEDGER):
        mine = {(r["page"]["NsrName"].lower(), r["page"]["TalkId"]) for r in json.load(open(PAGES_LEDGER, encoding="utf-8"))["rows"]}
    for s in spec_out:
        if (s["nsr"], s["talk"]) not in mine and any(p["NsrName"].lower() == s["nsr"] and p["TalkId"] == s["talk"] for p in pages_all):
            raise SystemExit("страница %s_%d уже есть — коллизия" % (s["nsr"], s["talk"]))
        pg = copy.deepcopy(tpl)
        pg["NsrName"], pg["TalkId"], pg["Text"] = s["nsr"], s["talk"], s["ru"]
        pg.pop("Name", None)
        pg["PageIndex"] = -1                          # номер страницы проставит импортёр (в конец файла NPC)
        st = pg["Steps"][0]
        st["Caption"] = CAPTION_RU
        sub = st["Subs"][0]
        sub["State"] = s["key"]
        sub["Res2"] = s["after_page"]
        c = sub["Cond"]
        c.update(Opcode=s["key"], LevelMin=s["level"], LevelMax=150, QuestActive=s["quest"], QuestStage=s["stage"])
        sub["ObjActive"] = (s["rewards"] + [0] * 6)[:6]
        objs = []
        for rid in s["rewards"]:
            r = rewards[rid]
            o = dict(empty_obj)
            o.update(QuestId=s["quest"], Kind24=1, Flag90=1 if r["ChoiceFlag"] else 0, Need136=-1, A16=r["Money"], A20=r["Exp"],
                     A28=0, A32=-1, A36=-1, A40=-1, A44=-1, A52=-1, A60=-1, A68=r["ItemCode1"], A76=r["ItemCode2"], A84=-1,
                     C72=r["ItemCount1"] if r["ItemCode1"] > 0 else 0, C80=r["ItemCount2"] if r["ItemCode2"] > 0 else 0,
                     S92=-1, S96=-1, S100=-1, S104=-1, S112=r["ChoiceItemCode"] if r["ChoiceFlag"] else -1,
                     S116=r["ChoiceItemCount"] if r["ChoiceFlag"] else 0, S120=-1, S128=-1)
            objs.append(o)
        sub["Objs"] = (objs + [dict(empty_obj) for _ in range(6)])[:len(tpl["Steps"][0]["Subs"][0]["Objs"])]
        rows.append(dict(quest=s["quest"], stage=s["stage"], page=pg))
    # ★ 01.11.2026 (item_dedup, этап 2): награды-дубли страниц сдачи -> основной код (общий модуль asda_item_dedup; награды
    #   берутся из QuestRewardTable.json, который импортёр уже перекодирует, — здесь страховка для старого JSON).
    import asda_item_dedup as _DD
    log("item_dedup: перекодировано наград-дублей в страницах сдачи: %d" % _DD.remap_nsr(rows))
    json.dump(dict(_comment="Страницы NSR сдачи 16 заданий ([ИСПРАВЛЕНИЕ] дефекта оригинала, тексты утверждены владельцем 31.10.2026). "
                            "Строит Tools/asda_quest_turnin_restore.py --write-ledgers; дописывает asda_import_nsr_dialog.py.",
                   rows=rows), open(PAGES_LEDGER, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    log("страниц NSR: %d → %s" % (len(rows), PAGES_LEDGER))
    json.dump(dict(_comment="Перенаправление сценариев на новые страницы сдачи (75 → ella_19, 85 → ella_18): прежние страницы 3/4 "
                            "заняты другим содержимым. Строит asda_quest_turnin_restore.py; применяет asda_import_npc_quest_data.py.",
                   rows=[dict(nsr=n, rec=rc, quest=q, was={"PageId": old}, set={"PageId": new}) for n, rc, q, old, new in SCRIPT_FIX]),
              open(SCRIPT_LEDGER, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    log("сценарии: %d → %s" % (len(SCRIPT_FIX), SCRIPT_LEDGER))
    led = json.load(open(EN_LEDGER, encoding="utf-8"))
    keep = [r for r in led["rows"] if not r.get("why", "").startswith("31.10.2026 bug-quest-follow")]
    for s in spec_out:
        for k, v in (("%s_%d.Text" % (s["nsr"], s["talk"]), s["en"]), ("%s_%d.Steps.0.Caption" % (s["nsr"], s["talk"]), CAPTION_EN)):
            keep.append(dict(key=k, value=v, was=None, why="31.10.2026 bug-quest-follow: страница сдачи задания %d (утверждено владельцем)" % s["quest"],
                             source="manual"))
    led["rows"] = keep
    json.dump(led, open(EN_LEDGER, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    log("EN-ведомость: %d строк → %s" % (len(keep), EN_LEDGER))


if __name__ == "__main__":
    sys.exit(main())
