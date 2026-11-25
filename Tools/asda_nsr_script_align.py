# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""Выравнивание условий записей сценария NSR (метка «!» над NPC и вход в ветку диалога) и шагов «Принять задание»
по СЕРВЕРНЫМ условиям квестов.

Зачем (29.10.2026, решение владельца «Да, выровняй»). Метку «!» и вход на вводную страницу квеста даёт ЗАПИСЬ СЦЕНАРИЯ
`.NSR` (144 б, блок 64 записей; порт — `UAsdaQuestMarkerSubsystem::EvalMarkForNsr` и `UAsdaNpcDialogSubsystem::RunScriptRecordScan`),
а выдаёт квест NpcServer `Start_Quest()` `sub_421020@0x421020` по условию `ConditionTable.Bin[номер]`, где номер = поле
`State` под-записи шага «Принять» (проверено: у 400 из 403 шагов `State` — валидный ключ серверного условия, и условие
шага с ним совпадает у 392). У части записей сценария условие МЯГЧЕ (или строже) серверного: метка висит, игрок заходит
в ветку, жмёт «Принять» — и квест не выдаётся (уровень 4 при квесте с 5, класс-новичок, «квест 183 сдан» при выдаче 183…).
Инструмент приводит условие записи сценария РОВНО к серверному: метка есть ⇔ сервер квест выдаст.

Что строит (только данные, бинарники клиента НЕ меняются):
  Tools/data/nsr_script_fixes.json — правки записей сценария: `set` (поля записи) или `disable` (квест, которого нет ни в
      эпизодах клиента, ни в выдаче сервера: метку и вход убрать). Применяет `asda_import_npc_quest_data.py` (`read_scripts`).
  Tools/data/nsr_step_cond_fixes.json — правки условий шагов «Принять» там, где шаг расходится с сервером и раскладка
      шага позволяет выразить серверное условие. Применяет `asda_import_nsr_dialog.py` (`read_all`).
  Каждая правка несёт `was` — значения ДО правки; импортёр применяет её, только если источник не изменился.
Запуск:
  python asda_nsr_script_align.py --fetch            — только отчёт (выход 1 — есть что выравнивать)
  python asda_nsr_script_align.py --fetch --write    — записать обе ведомости (бэкап старых)
Сервер: `--fetch` (scp, только чтение, через `asda_snq_server_sync`) или `--server-dir`.
"""
import argparse
import collections
import json
import os
import shutil
import struct
import sys
import tempfile
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as _P
import asda_snq_server_sync as _SNQ

HERE = os.path.dirname(os.path.abspath(__file__))
FIX_SCRIPT = os.path.join(HERE, "data", "nsr_script_fixes.json")
FIX_STEP = os.path.join(HERE, "data", "nsr_step_cond_fixes.json")
NSR_ZIP = os.path.join(_P.DATA, r"NPC\Episode\NPC\Nsr.bsz")
EPISODE_JSON = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "NPC", "EpisodeTable.json")
HEADER_JSON = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "NPC", "NpcNsrHeader.json")
SRV_REC = 146
SPECIAL = (121, 122, 123)   # спец-записи: крафт / реплика Talk / облачко — условием не гейтятся

# Поля условия в общем виде (одинаковые имена у записи сценария и у под-записи шага; у шага слот/прочее — BagSlot142/Misc144).
FIELDS = ["LevelMin", "LevelMax", "ClassId", "ClassMode", "QuestDone", "QuestFreeA", "QuestFreeB", "QuestActive", "QuestStage",
          "ObjIds", "ObjCounts", "ObjFlagNone", "ObjFlagAny", "ReqItemIds", "ReqItemCounts", "HaveItemId", "BagTabA", "BagTabB"]


def server_cond(srv, k):
    """Серверная запись условия `k` (146 б, раскладка — `asda_snq_server_sync.FIELD_MAP`) → поля условия; None — нет записи."""
    if not (0 < k < len(srv) // SRV_REC):
        return None
    r = srv[SRV_REC * k:SRV_REC * (k + 1)]
    I = lambda o: struct.unpack_from("<i", r, o)[0]
    U = lambda o: struct.unpack_from("<H", r, o)[0]
    if I(0) != k:
        return None
    c = dict(LevelMin=0, LevelMax=0, ClassId=-1, ClassMode=-1, QuestDone=-1, QuestFreeA=-1, QuestFreeB=-1, QuestActive=-1,
             QuestStage=-1, ObjIds=[-1] * 5, ObjCounts=[0] * 5, ObjFlagNone=0, ObjFlagAny=0, ReqItemIds=[-1] * 5,
             ReqItemCounts=[0] * 5, HaveItemId=-1, BagTabA=-1, BagTabB=-1, BagSlot=0, Misc=0)
    if r[36] == 1:
        c["LevelMin"], c["LevelMax"] = r[37], r[38]
    c["_cls_set"] = (r[39] == 1)   # у сервера «класс задан» решает флаг, а не значение (0/0 = только новичок)
    if r[39] == 1:
        c["ClassId"], c["ClassMode"] = I(40), I(44)
    if r[48] == 1:
        c.update(QuestFreeA=I(49), QuestDone=I(53), QuestActive=I(57), QuestFreeB=I(61),
                 QuestStage=-1 if r[65] == 255 else r[65], ObjFlagNone=r[67], ObjFlagAny=r[68],
                 ObjIds=[I(69 + 4 * j) for j in range(5)], ObjCounts=[U(89 + 2 * j) for j in range(5)])
    if r[99] == 1:
        c["ReqItemIds"], c["ReqItemCounts"] = [I(100 + 4 * j) for j in range(5)], [U(120 + 2 * j) for j in range(5)]
    if r[130] == 1:
        c["HaveItemId"] = I(131)
    if r[135] == 1:
        c["BagTabA"], c["BagTabB"] = U(136), U(138)
    c["BagSlot"], c["Misc"] = U(140), I(142)
    return c


GROUPS = {   # смысловые группы условия: правится группа целиком и только та, что по смыслу расходится с сервером
    "level": ["LevelMin", "LevelMax"],
    "class": ["ClassId", "ClassMode"],
    "quest": ["QuestDone", "QuestFreeA", "QuestFreeB", "QuestActive", "QuestStage", "ObjIds", "ObjCounts", "ObjFlagNone", "ObjFlagAny"],
    "items": ["ReqItemIds", "ReqItemCounts"],
    "equip": ["HaveItemId"],
    "tab": ["BagTabA", "BagTabB"],
}


def group_sig(c, g, owner_step):
    """Смысл группы условия — то, что реально проверяет `EvalNsrCond` (без безвредных различий записи)."""
    if g == "level":
        lmin, lmax = c["LevelMin"], c["LevelMax"]
        # Потолок уровня клиент снимает кодом (`Asda.Quest.LevelCapEnforce 0`: максимум 1…149 → 150), сервер — правкой таблицы;
        #   значим только минимум и то, задан ли максимум вообще (ветка «минимум без максимума» у эталона особая).
        return None if (lmin == 0 and lmax == 0) else (lmin, lmax > 0)
    if g == "class":
        cid, cm = c["ClassId"], c["ClassMode"]
        cls_set = c["_cls_set"] if "_cls_set" in c else ((cid > 0 or cm > 0) if owner_step else (cid > -1 or cm > -1))
        return (cid, cm) if cls_set else None
    if g == "quest":
        act = c["QuestActive"]
        return (c["QuestDone"], frozenset(x for x in (c["QuestFreeA"], c["QuestFreeB"]) if x >= 0), act,
                (c["QuestStage"], tuple(c["ObjIds"]), tuple(c["ObjCounts"]), c["ObjFlagNone"], c["ObjFlagAny"]) if act >= 0 else None)
    if g == "items":
        return tuple((i, n) for i, n in zip(c["ReqItemIds"], c["ReqItemCounts"]) if i > -1)
    if g == "equip":
        return c["HaveItemId"] if c["HaveItemId"] > -1 else None
    return tuple(x for x in (c["BagTabA"], c["BagTabB"]) if x > 0)


def aligned(cur, srv, key, owner_step):
    """Поля `cur`, приведённые к серверному условию `srv` по смысловым группам; None — правок нет.
    ★ «Самозапрет»: если сервер вовсе не проверяет квесты, а запись проверяет «этот же квест ещё не взят и не сдан», запрет
      остаётся — иначе метка «!» висела бы и после сдачи (сервер повторную выдачу отсекает журналом, а клиенту метку гасить нечем)."""
    new, changed = {}, []
    for g, fields in GROUPS.items():
        if group_sig(cur, g, owner_step) == group_sig(srv, g, owner_step):
            continue
        if g == "quest" and group_sig(srv, g, owner_step) == (-1, frozenset(), -1, None):
            self_only = (cur["QuestDone"] == -1 and cur["QuestActive"] == -1 and
                         {x for x in (cur["QuestFreeA"], cur["QuestFreeB"]) if x >= 0} == {key})
            if self_only or key < 0:
                continue
            new.update(QuestDone=-1, QuestFreeA=key, QuestFreeB=-1, QuestActive=-1, QuestStage=-1)
            changed.append(g)
            continue
        for f in fields:
            new[f] = srv[f]
        if g == "class" and not srv.get("_cls_set", True):
            new["ClassId"], new["ClassMode"] = -1, -1
        changed.append(g)
    return (new, changed) if new else (None, [])


def load_pages():
    import asda_import_nsr_dialog as N
    z = zipfile.ZipFile(NSR_ZIP)
    P = {}
    for n in sorted(x for x in z.namelist() if x.lower().endswith(".nsr")):
        nsr = os.path.basename(n)[:-4].lower()
        _h, _b, pages = N.parse_nsr(nsr, z.read(n))
        for p in pages:
            P[(nsr, p["TalkId"])] = p
    return P


def load_scripts():
    import asda_import_npc_quest_data as Q
    z = zipfile.ZipFile(NSR_ZIP)
    out = []
    for n in sorted(x for x in z.namelist() if x.lower().endswith(".nsr")):
        nsr = os.path.basename(n)[:-4].lower()
        for r in Q.parse_nsr(z.read(n)):
            if Q.rec_is_useful(r):
                r = dict(r); r["NsrName"] = nsr
                out.append(r)
    return out


def chain(P, nsr, start, seen=None):
    """Страница и всё, куда с неё ведут простые переходы (под-запись State 1 → Res2): ветка, которую открывает запись."""
    seen = seen if seen is not None else set()
    if (nsr, start) in seen:
        return
    seen.add((nsr, start))
    p = P.get((nsr, start))
    if not p:
        return
    yield p
    for st in p["Steps"]:
        for s in st["Subs"]:
            if s["State"] == 1 and s["Res2"] > 0:
                yield from chain(P, nsr, s["Res2"], seen)


def self_test_states(c, groups):
    """Состояния для самотеста `asda.SelfTest.QuestDialogReward` (поле `check`, импортёр его не читает):
    `pos` — условие выровненной записи выполнено (метка обязана быть, «Принять» обязан отправить 5043);
    `neg` — по каждой изменённой группе одно нарушение (метки от этой записи быть не должно).
    Состояние: уровень, класс, активный квест [номер, стадия] (−1 — нет), список сданных."""
    lvl = c["LevelMin"] if c["LevelMin"] > 0 else 50
    cid = c["ClassId"]
    cls = 1 if cid in (-1, 99) else cid            # без требования / «любая профессия» — профессия 1
    act = [c["QuestActive"], max(c["QuestStage"], 0)] if c["QuestActive"] >= 0 else [-1, 0]
    done = [c["QuestDone"]] if c["QuestDone"] >= 0 else []
    pos = dict(level=lvl, cls=cls, active=act, done=done)
    neg = []
    if "level" in groups and c["LevelMin"] > 1:
        neg.append(dict(pos, level=c["LevelMin"] - 1, what="уровень %d" % (c["LevelMin"] - 1)))
    if "class" in groups and cid >= 0:
        neg.append(dict(pos, cls=0 if cid == 99 else (2 if cid == 1 else 1), what="класс не тот"))
    if "quest" in groups:
        if c["QuestDone"] >= 0:
            neg.append(dict(pos, done=[], what="квест %d не сдан" % c["QuestDone"]))
        if c["QuestFreeA"] >= 0:
            neg.append(dict(pos, done=done + [c["QuestFreeA"]], what="квест %d уже сдан" % c["QuestFreeA"]))
    return dict(pos=pos, neg=neg)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--fetch", action="store_true")
    ap.add_argument("--server-dir")
    ap.add_argument("--write", action="store_true", help="записать ведомости Tools/data/nsr_*_fixes.json")
    a = ap.parse_args(argv)
    tmp = None
    sdir = a.server_dir
    if a.fetch:
        tmp = tempfile.mkdtemp(prefix="asda_nsr_align_")
        _SNQ.fetch_server(tmp)
        sdir = tmp
    if not sdir or not os.path.isfile(os.path.join(sdir, "ConditionTable.Bin")):
        print("нет серверных таблиц (--fetch или --server-dir)")
        return 2
    try:
        srv = open(os.path.join(sdir, "ConditionTable.Bin"), "rb").read()
        srv_npcs = _SNQ.read_server_npcs(open(os.path.join(sdir, "QuestNpcTable.Bin"), "rb").read())
        give = {npc: {q for q, _k, _n in sides[0] if q > 0} for npc, sides in srv_npcs.items()}
        # Ключ условия ячейки выдачи (questId → первый ключ > 0) — запасной источник серверного условия, когда номер
        #   условия в под-записи шага испорчен (у 2 шагов из 403 `State` вне таблицы сервера).
        give_key = {npc: {q: next((k for k in ks if k > 0), -1) for q, ks, _n in sides[0] if q > 0} for npc, sides in srv_npcs.items()}
        ep = {r["EpisodeId"]: r["QuestKey"] for r in json.load(open(EPISODE_JSON, encoding="utf-8"))}
        P = load_pages()
        recs = load_scripts()
        npc_of = {h["NsrName"]: h["NpcId"] for h in json.load(open(HEADER_JSON, encoding="utf-8"))}   # npcId сценария (шапка +0)

        script_fixes, step_fixes, notes = [], [], []
        # ── 1. Шаги «Принять» против сервера ──
        step_target = {}   # (nsr, talk, step, sub) → серверное условие (или None)
        cond_of = {}       # (nsr, talk, step, sub) → номер серверного условия
        for (nsr, talk), p in sorted(P.items()):
            for si, st in enumerate(p["Steps"]):
                for m, s in enumerate(st["Subs"]):
                    if s["AcceptQuestId"] <= 0:
                        continue
                    c = server_cond(srv, s["State"])
                    cond_no = s["State"]
                    if c is None:   # номер условия в шаге испорчен — условие ячейки выдачи этого NPC на сервере
                        cond_no = give_key.get(npc_of.get(nsr, -1), {}).get(ep.get(s["AcceptQuestId"], -1), -1)
                        c = server_cond(srv, cond_no) if cond_no > 0 else None
                        if c is not None:
                            notes.append("шаг %s/%d: номер условия %d вне таблицы сервера — беру условие ячейки выдачи %d"
                                         % (nsr, talk, s["State"], cond_no))
                    step_target[(nsr, talk, si, m)] = c
                    cond_of[(nsr, talk, si, m)] = cond_no
                    if c is None:
                        continue
                    C = s["Cond"]
                    key = ep.get(s["AcceptQuestId"], -1)
                    new, groups = aligned(C, c, key, True)
                    if not new:
                        continue
                    if "class" in groups and c.get("_cls_set") and c["ClassId"] == 0 and c["ClassMode"] == 0:
                        notes.append("шаг %s/%d: сервер пускает только НОВИЧКА (класс 0/0) — в раскладке шага это не выражается "
                                     "(класс «задан» при > 0); сервер сам откажет остальным, шаг не правлю" % (nsr, talk))
                        continue
                    if c["BagSlot"] or c["Misc"] > 0:
                        notes.append("шаг %s/%d: у серверного условия слот/прочее (%d/%d) — не правлю" % (nsr, talk, c["BagSlot"], c["Misc"]))
                        continue
                    after = dict(C); after.update(new)
                    snpc = npc_of.get(nsr, -1)
                    sslot = next((i for i, (q, ks, _n) in enumerate(srv_npcs.get(snpc, [[]])[0]) if q == key and any(k > 0 for k in ks)), -1)
                    step_fixes.append(dict(nsr=nsr, talk=talk, step=si, sub=m, episode=s["AcceptQuestId"], quest=key, npc=snpc,
                                           give_slot=sslot, server_cond=cond_no, groups=groups, was={f: C[f] for f in new}, set=new,
                                           check=self_test_states(after, groups)))
        # ── 2. Записи сценария против сервера ──
        for r in recs:
            if r["Opcode"] in SPECIAL:
                continue
            targets = {}
            for p in chain(P, r["NsrName"], r["PageId"]):
                for si, st in enumerate(p["Steps"]):
                    for m, s in enumerate(st["Subs"]):
                        if s["AcceptQuestId"] > 0:
                            targets[(p["TalkId"], si, m)] = (s, step_target.get((r["NsrName"], p["TalkId"], si, m)))
            if not targets:
                continue
            conds = {tuple(str(group_sig(c, g, False)) for g in GROUPS) if c else None for _s, c in targets.values()}
            if len(conds) > 1:
                notes.append("запись %s#%d ведёт к %d шагам «Принять» с разными условиями — не правлю: %s"
                             % (r["NsrName"], r["RecIndex"], len(targets), sorted(targets)))
                continue
            (talk, si, m), (s, c) = sorted(targets.items())[0]
            key = ep.get(s["AcceptQuestId"], -1)
            npc = npc_of.get(r["NsrName"], -1)
            slot = next((i for i, (q, ks, _n) in enumerate(srv_npcs.get(npc, [[]])[0]) if q == key and any(k > 0 for k in ks)), -1)
            base = dict(nsr=r["NsrName"], rec=r["RecIndex"], page=r["PageId"], accept_page=talk, accept_step=si, episode=s["AcceptQuestId"],
                        quest=key, mark=r["Mark"], npc=npc, give_slot=slot)
            if c is None:
                if key >= 0 and key in give.get(npc_of.get(r["NsrName"], -1), set()):
                    notes.append("запись %s#%d: у шага нет серверного условия, но сервер выдаёт квест %d — не трогаю" % (r["NsrName"], r["RecIndex"], key))
                    continue
                script_fixes.append(dict(base, why="квест не выдаётся ни клиентом, ни сервером (эпизода %d нет в таблице эпизодов "
                                         "клиента%s) — метку и вход убрать" % (s["AcceptQuestId"],
                                         "" if key < 0 else ", ячейки выдачи на сервере нет"), disable=True,
                                         was={f: r[f] for f in FIELDS}))
                continue
            new, groups = aligned(r, c, key, False)
            # ★ Метка: запись открывает ветку квеста, который сервер ВЫДАЁТ этим NPC, а поле `Mark` 0 — метки «!» нет совсем
            #   (6 записей оригинала). Владелец: «метка ровно тогда, когда квест берётся» ⇒ Mark 1.
            if r["Mark"] != 1 and key >= 0 and key in give.get(npc_of.get(r["NsrName"], -1), set()):
                new = dict(new or {}); new["Mark"] = 1
                groups = groups + ["mark"]
            if not new:
                continue
            if c["BagSlot"] or c["Misc"] > 0:
                notes.append("запись %s#%d: у серверного условия слот/прочее — не правлю" % (r["NsrName"], r["RecIndex"]))
                continue
            cno = cond_of.get((r["NsrName"], talk, si, m), s["State"])
            after = dict(r); after.update(new)
            new_was = {f: r[f] for f in new}
            script_fixes.append(dict(base, server_cond=cno, groups=groups,
                                     why="условие записи ≠ серверному условию %d (%s)" % (cno, ", ".join(groups)),
                                     was=new_was, set=new, check=self_test_states(after, groups)))

        for f in script_fixes:
            diff = [k for k in (f.get("set") or {}) if f["set"][k] != f["was"][k]]
            print("запись %-9s #%-2d стр.%-3d → «Принять» стр.%-3d эп.%d кв.%d: %s%s" % (
                f["nsr"], f["rec"], f["page"], f["accept_page"], f["episode"], f["quest"],
                "УБРАТЬ" if f.get("disable") else "", "; ".join("%s %s→%s" % (k, f["was"][k], f["set"][k]) for k in diff)))
        for f in step_fixes:
            diff = [k for k in f["set"] if f["set"][k] != f["was"][k]]
            print("шаг    %-9s стр.%-3d эп.%d усл.%d: %s" % (f["nsr"], f["talk"], f["episode"], f["server_cond"],
                  "; ".join("%s %s→%s" % (k, f["was"][k], f["set"][k]) for k in diff)))
        for n in notes:
            print("·· " + n)
        print("записей сценария править %d (убрать %d), шагов %d, замечаний %d"
              % (len(script_fixes), sum(1 for f in script_fixes if f.get("disable")), len(step_fixes), len(notes)))
        if a.write:
            from asda_headless_tool import backup_file
            for path, rows, what in ((FIX_SCRIPT, script_fixes, "записи сценария NSR"), (FIX_STEP, step_fixes, "условия шагов «Принять»")):
                if os.path.isfile(path):
                    print("бэкап: %s" % backup_file(path, "nsr_script_align", verify="md5"))
                with open(path, "w", encoding="utf-8", newline="\n") as fo:
                    json.dump({"_comment": "Ведомость Tools/asda_nsr_script_align.py (%s по серверному ConditionTable.Bin). "
                                           "Правится только генератором; применяется импортёром при каждом прогоне." % what,
                               "rows": rows}, fo, ensure_ascii=False, indent=1)
                print("записано: %s (%d)" % (path, len(rows)))
        return 1 if (script_fixes or step_fixes) and not a.write else 0
    finally:
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
