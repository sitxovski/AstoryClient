# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""Серверная таблица сдачи заданий → `RawAssets/Data/Quest/QuestTurnIn.json` → `DT_QuestTurnIn` + сверка всех шагов (31.10.2026, баг 6).

Зачем. Кнопка «Следовать» журнала при выполненной цели шага ведёт к NPC, который ПРИМЕТ сдачу (решение владельца). Кто
принимает — решает СЕРВЕР, и только он: NpcServer `Process_Quest@0x420C10` (`NpcServer.exe.c:22344`, пакет 7103 от GameServer):
  * запись NPC `QuestNpcTable.Bin` (1774 б, +0 i16 npcId) → ячейка «ПРИНИМАЕТ» №k (+894 + 88·k, k ≤ 9): +0 i16 задание,
    +66 i16 ШАГ, +68 5×i32 ключей `ConditionTable.Bin`;
  * в журнале игрока должен быть слот с парой (задание, стадия) == (ячейка +0, ячейка +66) — `__PAIR32__(+66, +0)`, иначе
    отказ (код 2);
  * ключи условий перебираются по порядку, `CheckCondition` (`sub_41FD70`) — достаточно ОДНОГО прошедшего (OR);
  * действие — байт +35 записи условия: 1 — стадия +1, 2 — задание завершено (бит), 0 — состояние не меняется (такие ячейки
    выдают квестовый предмет: условие «предмета НЕТ», `ObjFlagNone`).
Клиентская копия ячеек (`SNQInfo.Bin`, +66) у 14 ячеек отстала от сервера (задания 128, 428–441: клиент 0, сервер 1), а у 7
ключей условия нет проверки задания вовсе — поэтому таблица строится из СЕРВЕРНЫХ файлов, а не из клиентских.

Строка таблицы — (задание, шаг, NPC, ключи, действие, место NPC по серверному спавну `NpcInfo/<карта>.npc`).

Сверка (`--out`): для КАЖДОЙ строки `QuestComplete` (QCT — «цель выполнена», 491) и каждой серверной пары (задание, шаг)
моделируется состояние игрока «цель выполнена» (слот на стадии шага, цели слота = цели QCT) и условие каждого ключа
проверяется тем же правилом, что у клиента (`UAsdaQuestMarkerSubsystem::MatchObjectives` / `IsQuestActiveWithStage`).
Итог — `turnin_all.csv`: NPC, точка, подпись «=> NPC - карта, x, y», число кандидатов, статус.

Запуск:
  python asda_quest_turnin_table.py --server-dir D [--out <папка>]   — JSON + сверка
  python asda_quest_turnin_table.py --fetch [--out <папка>]          — то же, сервер по scp (только чтение)
  Tools\\run_headless_safe.py Tools\\asda_quest_turnin_table.py       — под UE: залить JSON в DT_QuestTurnIn
Код возврата: 0 — у каждого «выполненного» шага ровно один NPC (или законная пара, помечена), 1 — есть неоднозначные/пустые.
"""
import collections
import csv
import json
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as _P

OUT_JSON = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "Quest", "QuestTurnIn.json")
RAW = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data")
PKG = "/Game/AsdaAssets/Data/Quest"
DT_NAME = "DT_QuestTurnIn"
RS = "/Script/AsdaReborn.AsdaQuestTurnInRow"
NPC_REC, TAKE_OFF, CELL, COND_REC = 1774, 894, 88, 146


def log(msg):
    print("[quest-turnin] %s" % msg)


def read_cells(sd):
    """Серверные ячейки «ПРИНИМАЕТ»: [(npcId, запись, ячейка, задание, шаг, [ключи], [действия])].
    ★ [ИСПРАВЛЕНИЕ] 31.10.2026: и ячейки со слотом ≥ 10 из таблицы расширения `Quest/QuestNpcTableExt.Bin` (патч NpcServer N22,
    формат — `asda_quest_cells_ext.py`): сервер принимает их у того же NPC тем же правилом."""
    import asda_quest_cells_ext as QX
    b = open(os.path.join(sd, "Quest", "QuestNpcTable.Bin"), "rb").read()
    ct = open(os.path.join(sd, "Quest", "ConditionTable.Bin"), "rb").read()
    out, seen = [], set()
    for i in range(len(b) // NPC_REC):
        o = i * NPC_REC
        npc = struct.unpack_from("<h", b, o)[0]
        if npc < 0:
            continue
        # NpcServer адресует NPC ИНДЕКСОМ записи; дубли npcId (38, 39, 200, 201) — у клиента первая запись, то же берём здесь.
        if npc in seen:
            continue
        seen.add(npc)
        for k in range(10):
            so = o + TAKE_OFF + CELL * k
            q = struct.unpack_from("<h", b, so)[0]
            if q <= 0:
                continue
            step = struct.unpack_from("<h", b, so + 66)[0]
            keys = [x for x in struct.unpack_from("<5i", b, so + 68) if x > 0]
            acts = [ct[x * COND_REC + 35] if (x + 1) * COND_REC <= len(ct) else -1 for x in keys]
            out.append(dict(npc=npc, rec=i, cell=k, quest=q, step=step, keys=keys, acts=acts))
    xp = os.path.join(sd, "Quest", QX.FILE_NAME)
    if os.path.exists(xp):
        for x in QX.parse(open(xp, "rb").read()):
            if x["kind"] != QX.TAKE or QX.cell_quest(x) <= 0:
                continue
            keys = [k for k in QX.cell_keys(x) if k > 0]
            acts = [ct[k * COND_REC + 35] if (k + 1) * COND_REC <= len(ct) else -1 for k in keys]
            out.append(dict(npc=x["npc"], rec=-1, cell=x["slot"], quest=QX.cell_quest(x), step=QX.cell_step(x), keys=keys, acts=acts))
    return out


def cond_pass(c, quest, stage, slot_objs):
    """Условие ключа в состоянии «цель выполнена»: часть «задание в работе» (rec+28/+36/цели) — 1:1 с клиентом
    (`EvalNsrCond` категория 2 → `IsQuestActiveWithStage` / `MatchObjectives`). Прочие категории (уровень, класс, предметы
    сумки) — свойства персонажа, а не шага: считаются выполненными, их проверяет живой вычислитель клиента."""
    if c is None:
        return True
    qa = c["QuestActive"]
    if qa < 0:
        return True
    if qa != quest:
        return False
    st = c["QuestStage"]
    ids = c["ObjIds"]; cnts = c["ObjCounts"]
    obj0 = ids[0] if ids else -1
    want = sum(1 for x in ids if x > -1)
    if obj0 <= -1:
        return st == -1 or st == stage
    if st > -1 and st != stage:
        return False
    sids = [x for x, _ in slot_objs]
    if c["ObjFlagNone"]:
        return not any(x in sids and x > -1 for x in ids)
    match = sum(1 for (sid, sc) in slot_objs for k in range(5) if sid > -1 and sid == ids[k] and sc == cnts[k])
    return match > 0 if c["ObjFlagAny"] else match == want


def main():
    try:
        import unreal  # noqa: F401
        in_ue = True
    except Exception:
        in_ue = False
    if in_ue:
        from asda_dt_common import fill_dt
        import unreal
        dt = fill_dt(unreal, PKG, DT_NAME, RS, open(OUT_JSON, encoding="utf-8").read(), log, fmt="json",
                     missing_hint="Public/Quest/AsdaQuestTypes.h")
        log("ASDA_WRITES_ASSET: %s/%s" % (PKG, DT_NAME))
        log("ЗАВЕРШЕНО: %s %s" % (DT_NAME, "залит" if dt is not None else "НЕ залит"))
        return 0
    import argparse
    import asda_quest_follow_audit as F
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--fetch", action="store_true")
    ap.add_argument("--server-dir")
    ap.add_argument("--out", default=os.path.join(_P.PORT_DIR, "Reports", "bug_quest_follow"))
    a = ap.parse_args()
    sd = a.server_dir
    if a.fetch:
        import asda_quest_mob_audit as A
        import subprocess
        sd = tempfile.mkdtemp(prefix="asda_qturnin_")
        A.fetch_server(sd)
        for sub, pat in (("Quest", "Quest/QuestNpcTable.Bin"), ("Quest", "Quest/ConditionTable.Bin"), ("NpcInfo", "NpcInfo/*.npc")):
            os.makedirs(os.path.join(sd, sub), exist_ok=True)
            subprocess.run(["scp", "-q", "-i", A.SSH_KEY, "-o", "BatchMode=yes", "%s:%s/%s" % (A.SSH_HOST, A.SRV_ROOT, pat),
                            os.path.join(sd, sub) + os.sep], check=True)
    if not sd:
        log("нет сервера (--fetch или --server-dir)")
        return 2
    cells = read_cells(sd)
    spawn = F.read_server_npcs(sd)
    ict = {r["Key"]: r for r in json.load(open(os.path.join(RAW, "NPC", "SnqIct.json"), encoding="utf-8"))}
    ct = open(os.path.join(sd, "Quest", "ConditionTable.Bin"), "rb").read()
    qct = json.load(open(os.path.join(RAW, "Quest", "QuestComplete.json"), encoding="utf-8"))
    guide = json.load(open(os.path.join(RAW, "Quest", "QuestGuide.json"), encoding="utf-8"))
    names = {}
    # Имена NPC — как их видит игрок: `npc_list.BIN` рабочего клиента, +4 (34 б, cp1251); карта — имя файла серверного спавна.
    nl = open(os.path.join(_P.DATA, "NPC", "npc_list.BIN"), "rb").read()
    for i in range(struct.unpack_from("<i", nl, 4)[0]):
        o = 0x14 + 0x88 * i
        names[struct.unpack_from("<i", nl, o)[0]] = nl[o + 4:o + 0x26].split(b"\0")[0].decode("cp1251", "replace")
    import asda_quest_mob_audit as A
    map_names = A.load_server(sd)["map_name"]

    # ── Таблица для клиента ──
    rows = []
    for c in cells:
        pl = spawn.get(c["npc"], [])
        mp, tx, ty = pl[0] if pl else (-1, -1, -1)
        rows.append(dict(QuestNo=c["quest"], Stage=c["step"], NpcId=c["npc"], Keys=c["keys"] + [-1] * (5 - len(c["keys"])),
                         Action=max(c["acts"]) if c["acts"] else -1, MapId=mp, TileX=tx, TileY=ty))
    out = [dict(r, Name=str(i)) for i, r in enumerate(rows)]
    os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
    with open(OUT_JSON, "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, ensure_ascii=False, indent=0)
    log("строк %d → %s" % (len(out), OUT_JSON))

    # ── Сверка: все состояния «цель выполнена» (QCT) и все серверные пары (задание, шаг) ──
    by_qs = collections.defaultdict(list)
    for c in cells:
        by_qs[(c["quest"], c["step"])].append(c)
    states = []
    for r in qct:
        # Состояние «цель выполнена» по правилу QCT (`MatchObjectives`): обычная запись — цели слота = её цели; `ObjFlagNone`
        #   (20, 28: предметы надо РАЗДАТЬ посредникам) — в слоте нет ни одной из них; `ObjFlagAny` — достаточно первой.
        if r["ObjFlagNone"]:
            objs = []
        elif r["ObjFlagAny"]:
            objs = [(i, n) for i, n in zip(r["ObjIds"], r["ObjCounts"]) if i > -1][:1]
        else:
            objs = [(i, n) for i, n in zip(r["ObjIds"], r["ObjCounts"]) if i > -1]
        states.append(("QCT", r["QuestNo"], r["QuestStage"], objs))
    qct_keys = {(s[1], s[2]) for s in states}
    for (q, st) in sorted(by_qs):
        if (q, st) not in qct_keys:
            states.append(("шаг сервера", q, st, []))
    res = []
    for kind, q, st, objs in states:
        cands = []
        for c in by_qs.get((q, st), []):
            ok = [k for k in c["keys"] if cond_pass(ict.get(k), q, st, objs)]
            if ok:
                cands.append(dict(c, ok=ok, act=max(ct[k * COND_REC + 35] for k in ok)))
        # Действие 0 (выдача предмета) при выполненной цели само не проходит; если модель условий его пропустила
        #   (условие «предмет есть» — сдача предмета посреднику, задания 20/28), посредники — законные кандидаты.
        adv = [c for c in cands if c["act"] in (1, 2)]
        npcs = sorted({c["npc"] for c in cands})
        if not cands:
            status = "НИКТО: сервер на этой стадии сдачу не примет (кнопка — строки гида)"
        elif len(npcs) == 1:
            status = "ОДИН"
        elif len({c["npc"] for c in adv}) == 1 and kind == "QCT":
            status = "ОДИН (завершает; прочие — выдача предмета)"
        else:
            status = "НЕСКОЛЬКО ЗАКОННО (ближайший)"
        for c in cands or [None]:
            npc = c["npc"] if c else -1
            pl = spawn.get(npc, []) if c else []
            mp, tx, ty = pl[0] if pl else (-1, -1, -1)
            label = ("=> %s - %s, x:%d, y: %d" % (names.get(npc, npc), map_names.get(mp, mp), tx, ty)) if c and mp >= 0 else ""
            res.append(dict(kind=kind, quest=q, stage=st, npc=npc, slot=(c["cell"] if c else ""), action=(c["act"] if c else ""), keys=(c["ok"] if c else ""),
                            map=mp, x=tx, y=ty, label=label, candidates=len(npcs), status=status,
                            guide=";".join("%s%d(%d,%d)" % ("NPC" if g["TargetKind"] == 0 else "моб", g["TargetId"], g["X"], g["Y"])
                                           for g in guide if g["QuestNo"] == q and g["Stage"] == st)))
    # Оракул для самотеста `asda.SelfTest.QuestFollow` (раздел V): по каждому состоянию QCT — цели слота, класс персонажа (если
    #   условие ячейки требует класс) и ожидаемые NPC. Самотест ставит это состояние штатным приёмом 5052 и сверяет живой выбор.
    exp = []
    for kind, q, st, objs in states:
        if kind != "QCT":
            continue
        npcs = sorted({r["npc"] for r in res if r["kind"] == kind and r["quest"] == q and r["stage"] == st and r["npc"] != -1})
        cls = -1
        for c in by_qs.get((q, st), []):
            for k in c["keys"]:
                i = ict.get(k)
                if i and i["HasClass"] and c["npc"] in npcs:
                    cls = i["ClassId"]
        places = [[r["npc"], r["map"], r["x"], r["y"]] for r in res if r["kind"] == kind and r["quest"] == q and r["stage"] == st and r["npc"] != -1]
        exp.append(dict(QuestNo=q, Stage=st, ObjIds=[x for x, _ in objs], ObjCounts=[n for _, n in objs], ClassId=cls, Npcs=npcs,
                        Places=places))
    exp_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "quest_turnin_expected.json")
    with open(exp_path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(exp, f, ensure_ascii=False, indent=0)
    log("оракул самотеста: %d состояний → %s" % (len(exp), exp_path))
    os.makedirs(a.out, exist_ok=True)
    cols = ["kind", "quest", "stage", "npc", "slot", "action", "keys", "map", "x", "y", "label", "candidates", "status", "guide"]
    with open(os.path.join(a.out, "turnin_all.csv"), "w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(f, cols)
        w.writeheader()
        w.writerows(res)
    per = collections.OrderedDict()
    for r in res:
        per.setdefault((r["kind"], r["quest"], r["stage"]), r["status"])
    cnt = collections.Counter((k[0], s) for k, s in per.items())
    for (kind, s), n in sorted(cnt.items()):
        log("%-12s %-55s %d" % (kind, s, n))
    nolabel = sum(1 for r in res if r["npc"] != -1 and not r["label"])
    log("строк с NPC без координат серверного спавна: %d" % nolabel)
    for k in [k for k, s in per.items() if k[0] == "QCT" and s.startswith("НЕСКОЛЬКО")]:
        log("НЕСКОЛЬКО ЗАКОННО: задание %d шаг %d → %s (ведём к ближайшему)" % (k[1], k[2],
            sorted({r["npc"] for r in res if (r["kind"], r["quest"], r["stage"]) == k})))
    # Неоднозначности нет: законные пары помечены и решаются «ближайший», «никто» — строки гида оригинала.
    return 1 if nolabel else 0


if __name__ == "__main__":
    sys.exit(main())
