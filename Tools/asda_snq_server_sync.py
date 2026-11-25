# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""Сверка и дописывание клиентских `SNQInfo.Bin` / `SNQICT.Bin` по серверным `QuestNpcTable.Bin` / `ConditionTable.Bin`.

Зачем (29.10.2026, баг игроков «квест не берётся»). Клиент шлёт взятие квеста 5043 с НОМЕРОМ ЯЧЕЙКИ ВЫДАЧИ NPC из своего
`SNQInfo.Bin` (эталон `sendStartQuestBookProcess@0xD9D9D0`: ячейка с questId и хоть одним ключом > 0; порт — тот же поиск в
`UAsdaNpcDialogSubsystem::SendQuestAccept`), а NpcServer `Start_Quest()` `sub_421020@0x421020` берёт квест по этому номеру из
СВОЕЙ `QuestNpcTable.Bin`. Клиентская таблица у части NPC отстала от серверной: ячеек выдачи/сдачи нет вовсе, и шаг
«Принять задание» молча не отправлял ничего (у оригинала — уходил номер 0, то есть ЧУЖОЙ квест). Условия ячеек (облачко
выбора 2406) лежат в `SNQICT.Bin` по ключам ячеек; недостающие ключи дописываются из серверной `ConditionTable.Bin`.

Форматы (сверено байтами, 29.10.2026):
  * сервер `QuestNpcTable.Bin` — 200 записей по 1774 б без шапки: `+0 i16 npcId`, `+2` имя, `+34` 10×86 «ВЫДАЁТ»
    (`+0 i16 questId`, `+2` имя, `+66` 5×i32 ключей условий), `+894` 10×88 «ПРИНИМАЕТ» (ключи с `+68`);
    раскладка выдачи — из `sub_421020` (`&v27[43*слот + 17]`, `FirstConditionTableIndex` = `+66`).
  * клиент `SNQInfo.Bin` — шапка 24 б (тег «NPC» @0x0C, count @0x14), записи 1796 б: `+0 i16 npcId`, `+2` имя (34 б),
    `+36` 10×88 «ВЫДАЁТ», `+916` 10×88 «ПРИНИМАЕТ»; под-запись: `+0 i16 questId`, `+2` имя, `+68` 5×i32 ключей
    (`Tools/asda_import_npc_tables.py`, `read_snq`).
  * сервер `ConditionTable.Bin` — 1148 записей по 146 б без выравнивания, индекс = номер условия.
  * клиент `SNQICT.Bin` — шапка 24 б (тег «CON», count @0x14), записи 156 б — та же структура С ВЫРАВНИВАНИЕМ (заполнитель
    0xCC). Отображение полей `FIELD_MAP` проверено на всех 839 общих ключах: совпадение байт-в-байт, кроме уровня (+38/+39):
    на сервере потолок поднят до 150 (решение владельца), у клиентских записей стоит 147 — клиент снимает потолок кодом
    (`Asda.Quest.LevelCapEnforce 0`). Новые записи пишутся по тому же правилу (max 150 → 147), как в JOB4_CHAIN_FIX 11.09.

Что делает:
  без ключей   — только сверка, печать расхождений; выход 0 — клиент совпадает с сервером, 1 — есть что дописать, 2 — нет данных;
  --apply      — дописывает ТОЛЬКО пустые клиентские ячейки (questId 0 и все ключи 0) копией серверных и недостающие ключи
                 SNQICT в конец файла; непустые расходящиеся ячейки НЕ трогает (печатает «!!»). Бэкап каждого файла до записи
                 (`asda_headless_tool.backup_file`, набор `--set`).
Источник сервера: `--fetch` (scp с dev по SSH, только чтение; шару \\\\@@ASTORY_SERVER_HOSTNAME@@ не трогаем) или `--server-dir <папка с двумя .Bin>`.
После `--apply` — переимпорт `DT_NpcSnq`/`DT_SnqIct`: `Tools/run_headless_safe.py Tools/asda_import_npc_tables.py`.

Запуск:
  python asda_snq_server_sync.py --fetch
  python asda_snq_server_sync.py --fetch --apply --set bugs7
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as _P

SSH_KEY = r"C:\Users\@@ASTORY_DEV_USER@@\.ssh\@@ASTORY_SSH_KEY_NAME@@"
SSH_HOST = "Administrator@@@ASTORY_SERVER_VPN_IP@@"
SERVER_QUEST_DIR = "@@ASTORY_SERVER_ROOT_FWD@@/Working_ASDA/v2/NpcServer/Quest"   # копии Ch2_/Indun_ — те же файлы (сверка md5 в --fetch)
SERVER_COPIES = ("NpcServer", "Ch2_NpcServer", "Indun_NpcServer")

CLI_SNQ = os.path.join(_P.DATA, r"NPC\SNQInfo.Bin")
CLI_ICT = os.path.join(_P.DATA, r"NPC\SNQICT.Bin")

SRV_NPC_REC, CLI_NPC_REC, CLI_SUB = 1774, 1796, 88
SRV_GIVE, SRV_GIVE_STRIDE, SRV_GIVE_KEYS = 34, 86, 66
SRV_TAKE, SRV_TAKE_STRIDE, SRV_TAKE_KEYS = 894, 88, 68
CLI_GIVE, CLI_TAKE, CLI_KEYS = 36, 916, 68
HDR = 0x18
SRV_ICT, CLI_ICT_REC = 146, 156
# (смещение в серверной записи, смещение в клиентской, длина) — сверено на 839 общих ключах (см. шапку).
FIELD_MAP = [(0, 0, 11), (11, 12, 2), (13, 14, 23), (36, 37, 3), (39, 40, 1), (40, 44, 4), (44, 48, 4), (48, 52, 1),
             (49, 56, 16), (65, 72, 4), (69, 76, 20), (89, 96, 10), (99, 106, 1), (100, 108, 20), (120, 128, 10),
             (130, 138, 1), (131, 140, 4), (135, 144, 1), (136, 146, 6), (142, 152, 4)]
CLI_LEVEL_MAX = 147   # клиентская запись: потолок 147, снимает его код (см. шапку)


def fetch_server(dst):
    """Снять QuestNpcTable.Bin и ConditionTable.Bin с dev (scp, только чтение) и сверить три копии по md5."""
    for name in ("QuestNpcTable.Bin", "ConditionTable.Bin"):
        cmd = ["scp", "-q", "-i", SSH_KEY, "-o", "BatchMode=yes", "-o", "ConnectTimeout=15",
               "%s:%s/%s" % (SSH_HOST, SERVER_QUEST_DIR, name), os.path.join(dst, name)]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        if r.returncode != 0:
            raise SystemExit("scp %s: %s" % (name, r.stderr.strip()))
    ps = "; ".join("(Get-FileHash -Algorithm MD5 @@ASTORY_SERVER_ROOT_ESC@@\\Working_ASDA\\v2\\%s\\Quest\\%s).Hash" % (c, n)
                   for c in SERVER_COPIES for n in ("QuestNpcTable.Bin", "ConditionTable.Bin"))
    r = subprocess.run(["ssh", "-i", SSH_KEY, "-o", "BatchMode=yes", "-o", "ConnectTimeout=15", SSH_HOST,
                        "powershell -NoProfile -Command \"%s\"" % ps], capture_output=True, text=True, timeout=120)
    hashes = [h.strip() for h in r.stdout.splitlines() if h.strip()]
    if len(hashes) == 6:
        same = hashes[0] == hashes[2] == hashes[4] and hashes[1] == hashes[3] == hashes[5]
        print("сервер: копии %s %s" % ("/".join(SERVER_COPIES), "ОДИНАКОВЫ" if same else "!! РАЗНЫЕ — сверять каждую"))
    else:
        print("сервер: md5 копий не получены (%s)" % r.stderr.strip()[:200])


def read_server_npcs(b):
    """npcId → (выдача, сдача): списки (questId, (5 ключей), имя-байты) по 10."""
    out = {}
    for i in range(len(b) // SRV_NPC_REC):
        o = i * SRV_NPC_REC
        npc = struct.unpack_from("<h", b, o)[0]
        if npc < 0:
            continue
        sides = []
        for base, st, ko in ((SRV_GIVE, SRV_GIVE_STRIDE, SRV_GIVE_KEYS), (SRV_TAKE, SRV_TAKE_STRIDE, SRV_TAKE_KEYS)):
            lst = []
            for k in range(10):
                so = o + base + st * k
                q = struct.unpack_from("<h", b, so)[0]
                keys = struct.unpack_from("<5i", b, so + ko)
                name = b[so + 2:so + ko].split(b"\0")[0]
                lst.append((q, keys, name))
            sides.append(lst)
        out.setdefault(npc, sides)   # дубли npcId: первая запись, как у клиента
    return out


def read_client_npcs(b):
    """npcId → (смещение записи, выдача, сдача) — первая запись npcId (эталон `findCopyRecord1796ById16`)."""
    n = struct.unpack_from("<i", b, 0x14)[0]
    out = {}
    for i in range(n):
        o = HDR + CLI_NPC_REC * i
        npc = struct.unpack_from("<h", b, o)[0]
        if npc in out:
            continue
        sides = []
        for base in (CLI_GIVE, CLI_TAKE):
            lst = []
            for k in range(10):
                so = o + base + CLI_SUB * k
                lst.append((struct.unpack_from("<h", b, so)[0], struct.unpack_from("<5i", b, so + CLI_KEYS)))
            sides.append(lst)
        out[npc] = (o, sides[0], sides[1])
    return out


def conv_condition(srec):
    """Серверная запись условия 146 б → клиентская 156 б (заполнитель 0xCC, потолок уровня клиента)."""
    out = bytearray(b"\xcc" * CLI_ICT_REC)
    for so, co, sz in FIELD_MAP:
        out[co:co + sz] = srec[so:so + sz]
    if out[37] == 1 and out[39] > CLI_LEVEL_MAX:
        out[39] = CLI_LEVEL_MAX
    return bytes(out)


def verify_map(srv_ict, cli_ict):
    """Сверка отображения полей на всех общих ключах (кроме уровня) — без неё дописывать нельзя."""
    n = struct.unpack_from("<i", cli_ict, 0x14)[0]
    bad = 0
    for i in range(n):
        crec = cli_ict[HDR + CLI_ICT_REC * i:HDR + CLI_ICT_REC * (i + 1)]
        key = struct.unpack_from("<i", crec, 0)[0]
        if not (0 <= key < len(srv_ict) // SRV_ICT):
            continue
        v = conv_condition(srv_ict[SRV_ICT * key:SRV_ICT * (key + 1)])
        mapped = [co + j for so, co, sz in FIELD_MAP for j in range(sz) if co + j not in (38, 39)]
        if any(v[x] != crec[x] for x in mapped):
            bad += 1
    return n, bad


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--fetch", action="store_true", help="снять серверные таблицы по scp (только чтение)")
    ap.add_argument("--server-dir", help="папка с QuestNpcTable.Bin и ConditionTable.Bin")
    ap.add_argument("--apply", action="store_true", help="дописать пустые ячейки и недостающие условия в клиент")
    ap.add_argument("--set", default="snq_server_sync", help="имя набора бэкапа (@@ASTORY_DEV_ROOT_ESC@@\\_backup\\<дата>_<набор>)")
    a = ap.parse_args(argv)

    tmp = None
    sdir = a.server_dir
    if a.fetch:
        tmp = tempfile.mkdtemp(prefix="asda_snq_")
        fetch_server(tmp)
        sdir = tmp
    if not sdir or not os.path.isfile(os.path.join(sdir, "QuestNpcTable.Bin")):
        print("нет серверных таблиц (--fetch или --server-dir)")
        return 2
    try:
        srv_npc = open(os.path.join(sdir, "QuestNpcTable.Bin"), "rb").read()
        srv_ict = open(os.path.join(sdir, "ConditionTable.Bin"), "rb").read()
        cli_snq = bytearray(open(CLI_SNQ, "rb").read())
        cli_ict = bytearray(open(CLI_ICT, "rb").read())
        if cli_snq[0x0C:0x0F] != b"NPC" or cli_ict[0x0C:0x0F] != b"CON":
            print("!! тег клиентских таблиц не тот — файлы не трогаю")
            return 2
        n_common, n_bad = verify_map(srv_ict, cli_ict)
        print("SNQICT: отображение полей сервер→клиент на %d общих ключах — расхождений %d" % (n_common, n_bad))
        if n_bad:
            print("!! отображение полей не сходится — дописывать нельзя")
            return 2

        S = read_server_npcs(srv_npc)
        C = read_client_npcs(cli_snq)
        ict_keys = {struct.unpack_from("<i", cli_ict, HDR + CLI_ICT_REC * i)[0]
                    for i in range(struct.unpack_from("<i", cli_ict, 0x14)[0])}
        fills, conflicts, need_keys = [], [], set()
        for npc, (sg, st) in sorted(S.items()):
            if npc not in C:
                if any(q > 0 for q, _, _ in sg + st):
                    conflicts.append("npc %d: у клиента нет записи NPC (серверных ячеек %d)" % (npc, sum(q > 0 for q, _, _ in sg + st)))
                continue
            off, cg, ct = C[npc]
            for side, srv, cli, base in (("выдача", sg, cg, CLI_GIVE), ("сдача", st, ct, CLI_TAKE)):
                for k in range(10):
                    sq, sk, sname = srv[k]
                    cq, ck = cli[k]
                    if (sq, tuple(sk)) == (cq, tuple(ck)) or (sq <= 0 and cq <= 0):
                        continue
                    if cq == 0 and not any(ck):
                        fills.append((npc, side, k, off + base + CLI_SUB * k, sq, sk, sname))
                        need_keys.update(x for x in sk if x > 0 and x not in ict_keys)
                    else:
                        conflicts.append("npc %d %s[%d]: сервер q%d %s, клиент q%d %s — НЕ трогаю"
                                         % (npc, side, k, sq, list(sk), cq, list(ck)))
        # Ключи, на которые уже ссылаются клиентские ячейки, но которых нет в SNQICT, — тоже с сервера.
        for npc, (off, cg, ct) in C.items():
            for q, ks in cg + ct:
                if q > 0:
                    need_keys.update(x for x in ks if x > 0 and x not in ict_keys)
        need_keys = sorted(k for k in need_keys if k < len(srv_ict) // SRV_ICT
                           and struct.unpack_from("<i", srv_ict, SRV_ICT * k)[0] == k)
        for f in fills:
            print("дописать npc %-4d %s[%d] ← q%d ключи %s" % (f[0], f[1], f[2], f[4], list(f[5])))
        for c in conflicts:
            print("!! " + c)
        print("ячеек дописать %d, расхождений непустых ячеек %d, условий SNQICT дописать %d %s"
              % (len(fills), len(conflicts), len(need_keys), need_keys))
        if not a.apply:
            return 1 if (fills or need_keys) else 0
        if not fills and not need_keys:
            print("дописывать нечего")
            return 0

        from asda_headless_tool import backup_file
        for p in (CLI_SNQ, CLI_ICT):
            print("бэкап: %s" % backup_file(p, a.set, verify="md5"))
        for npc, side, k, so, sq, sk, sname in fills:
            sub = bytearray(cli_snq[so:so + CLI_SUB])
            struct.pack_into("<h", sub, 0, sq)
            name = sname.replace(b'"', b"")[:CLI_KEYS - 3]     # у клиента имя без кавычек (JOB4_CHAIN_FIX §5)
            sub[2:CLI_KEYS] = name + b"\0" * (CLI_KEYS - 2 - len(name))
            struct.pack_into("<5i", sub, CLI_KEYS, *sk)
            cli_snq[so:so + CLI_SUB] = sub
        n_old = struct.unpack_from("<i", cli_ict, 0x14)[0]
        add = b"".join(conv_condition(srv_ict[SRV_ICT * k:SRV_ICT * (k + 1)]) for k in need_keys)
        cli_ict = cli_ict[:HDR + CLI_ICT_REC * n_old] + add + cli_ict[HDR + CLI_ICT_REC * n_old:]
        struct.pack_into("<i", cli_ict, 0x14, n_old + len(need_keys))
        open(CLI_SNQ, "wb").write(cli_snq)
        open(CLI_ICT, "wb").write(cli_ict)
        print("записано: SNQInfo %d ячеек (%d б), SNQICT %d → %d записей (%d б)"
              % (len(fills), len(cli_snq), n_old, n_old + len(need_keys), len(cli_ict)))
        # Повторная сверка тем же разбором — расхождений по дописанному быть не должно.
        C2 = read_client_npcs(bytes(cli_snq))
        left = sum(1 for npc, side, k, *_ in fills
                   if (C2[npc][1] if side == "выдача" else C2[npc][2])[k][0] != S[npc][0 if side == "выдача" else 1][k][0])
        print("контроль после записи: недописанных ячеек %d" % left)
        return 0 if left == 0 else 1
    finally:
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
