# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""Полная сверка стадий заданий (06.11.2026, решение владельца: «игрок точно видит, кого убить и где»).

Правило: зачёт сервера (Monster.ai), текст шага и гид (строка «=> Цель - Карта, x, y») говорят одно и то же.
Для каждой стадии каждого задания: типы, которые дают зачёт, их спавны, цели гида, имя и карта в тексте RU и EN.
Запуск: python Tools/asda_quest_stage_audit.py --server-dir <копия сервера> [--out Reports/quest_mobs]
Выход 0 — нарушений нет, 1 — есть. Без побочных эффектов при импорте.
"""
import argparse
import collections
import csv
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as _P
import asda_quest_mob_audit as A

RAW = A.RAW                                                     # RawAssets
# Как карты названы в текстах (основы, нормализованные `A.norm`): номер карты → основы RU и EN.
MAP_STEMS = {0: ("силарис", "silaris", "sillaris"), 1: ("рекадожд", "рекидожд", "рекудожд", "рекедожд", "reinriver"),
             2: ("вечныхвойн", "conquer", "conquest", "darkforest"),
             3: ("альпи", "alpen"), 4: ("долинаноч", "долинуноч", "долиныноч", "долиненоч", "nightvalley"),
             5: ("лощин", "forgottenvalley", "lostvalley"),
             6: ("солнечн", "sunnyside", "sunnycoast", "sunbeach", "sunnybeach"), 7: ("пламени", "flamio", "flammio"),
             8: ("дворец", "дворц", "queen"), 9: ("шахмат", "chess"), 10: ("дворец", "дворц", "queen"), 11: ("шахмат", "chess"),
             12: ("горапламен", "горыпламен", "горепламен", "горупламен", "mtflem", "mountflam", "mtflam", "mountainflam"),
             13: ("флабис", "flabis", "flavis", "desert", "пустын"), 14: ("мертвпустын", "мертвойпустын", "мертваяпустын", "застывш", "stagnant", "desert"), 15: ("логов", "dragon"),
             16: ("огненнчащ", "огненнойчащ", "огненнуючащ", "огненнаячащ", "fireway"), 17: ("хаос", "chaos"), 18: ("инфер", "inferion")}
# Строгие флаги: зачёт, гид и карта в тексте расходятся. Имя моба, не написанное в тексте, — справка: его показывает строка гида.
HARD = ("НЕТ_СПАВНА", "ГИД_НЕТ_МОБА", "ГИД_НЕ_ТОТ_МОБ", "ГИД_НЕ_ТА_КАРТА", "RU_НЕ_ТА_КАРТА", "EN_НЕ_ТА_КАРТА")
# Известные исключения: нужны данные или решение владельца (отчёт QUEST_STRAY_CREDIT.md), гид и текст не гадают.
KNOWN = {(131, 0): "нет старта, цель без спавна", (156, 0): "выдача закрыта (asda.Quest.ClosedStart)", (157, 0): "выдача закрыта (asda.Quest.ClosedStart)",
         (166, 0): "нет старта, цель без спавна", (167, 0): "нет старта, цель без спавна", (130, 0): "условие пусто, сдаётся у NPC",
         (243, 0): "источников больше 4, перечислены в тексте", (398, 0): "19 боссов, текст общий", (20058, 0): "задание без книги",
         (20060, 0): "задание без книги"}
# Слова задачи «убить или добыть»: стадия без цели с таким текстом идёт в отдельный список.
KILL_WORDS = re.compile(r"\b(kill|hunt|defeat|slay|catch|bring|collect|gather|убей|убить|уничтож|добуд|добыт|собер|собрат|поймай)", re.I)


def text_maps(text):
    """Карты, названные в тексте."""
    t = A.norm(text).replace(".", "")
    out = {m for m, stems in MAP_STEMS.items() if any(s in t for s in stems)}
    if 12 in out and "пламени" not in re.sub("гор[аыеу]пламен[ьи]", "", t) and "flam" not in re.sub("(mt|mount|mountain)fl[ae]m", "", t):
        out.discard(7)                                          # «Горы Пламени» — это карта 12, а не Пламения
    return out


def one(text):
    """Текст в одну строку для выгрузки."""
    return " ".join((text or "").split())


def csv_map(path):
    """CSV оверлея с многострочными значениями: {ключ: текст}."""
    return {r[0]: r[1] for r in csv.reader(open(path, encoding="utf-8-sig", newline="")) if len(r) > 1}


def load(sd):
    """Все источники сверки."""
    srv = A.load_server(sd)
    guide = json.load(open(os.path.join(RAW, "Data", "Quest", "QuestGuide.json"), encoding="utf-8"))
    book = {r["Name"]: r for r in json.load(open(os.path.join(RAW, "Data", "Quest", "QuestBook.json"), encoding="utf-8"))}
    ep = {r["QuestKey"]: r for r in json.load(open(os.path.join(RAW, "Data", "NPC", "EpisodeTable.json"), encoding="utf-8"))}
    book_en = csv_map(os.path.join(A.L10N_EN, "DT_Loc_QuestBook_en.csv"))
    raid_en = csv_map(os.path.join(A.L10N_EN, "DT_Loc_Raid_quest_data_en.csv"))
    mob_en = csv_map(os.path.join(A.L10N_EN, "DT_Loc_MobNames_en.csv"))
    mob_ru = A.parse_moblist(os.path.join(_P.DATA, "Mob", "Mob list.BIN"), "cp1251")
    raid_ru = list(csv.reader(open(os.path.join(RAW, "Text", "Quest", "Raid_quest_data.csv"), encoding="utf-8-sig")))
    return srv, guide, book, ep, book_en, raid_en, mob_en, mob_ru, raid_ru


def stages(ep, book, guide, srv):
    """Все стадии: (задание, стадия) из книг, гида и зачётов."""
    out = set(srv["kills"]) | {(g["QuestNo"], g["Stage"]) for g in guide}
    for q, e in ep.items():
        b = book.get(e["QstFile"])
        if b:
            out |= {(q, i) for i, s in enumerate(b["Steps"]) if s.strip()}
    return sorted(k for k in out if k[0] > 0)


def audit(sd):
    """Строки сверки и данные сервера."""
    srv, guide, book, ep, book_en, raid_en, mob_en, mob_ru, raid_ru = load(sd)
    gmap = collections.defaultdict(list)
    for g in guide:
        gmap[(g["QuestNo"], g["Stage"])].append(g)

    def ru(t):
        """Имя RU по серверному типу."""
        return mob_ru[t - 1]["name"] if 0 < t <= len(mob_ru) else ""

    def en(t):
        """Имя EN по серверному типу."""
        return mob_en.get("%d.Name" % (t - 1), "")

    def spawn(t):
        """Спавны типа: {карта: число точек}."""
        return {m: len(p[t]) for m, p in srv["spawns"].items() if p.get(t)}

    raid_txt = {}
    for r in raid_ru:
        if r and r[0].strip().isdigit():
            raid_txt[int(r[0])] = " ".join(r[1:])
    rows = []
    for k in stages(ep, book, guide, srv):
        q, st = k
        e = ep.get(q)
        b = book.get(e["QstFile"]) if e else None
        t_ru = b["Steps"][st] if b and st < len(b["Steps"]) else ""
        t_en = book_en.get("%s.Steps.%d" % (e["QstFile"], st), "") if e else ""
        title = (book_en.get(e["QstFile"] + ".Title", "") if e else "") or raid_en.get("%d.name" % q, "")
        if q >= 20000:
            t_en = raid_en.get("%d.desc" % q, "")
            t_ru = raid_txt.get(q, "")
        kills = srv["kills"].get(k, [])
        types = sorted({t for t, _, _ in kills})
        live = {t: spawn(t) for t in types if spawn(t)}
        maps = sorted({m for s in live.values() for m in s})
        gm = [g for g in gmap.get(k, []) if g["TargetKind"] == 1]
        gn = [g for g in gmap.get(k, []) if g["TargetKind"] == 0]
        flags = []
        if kills:
            if not live:
                flags.append("НЕТ_СПАВНА")
            if not gm:
                flags.append("ГИД_НЕТ_МОБА")
            names = {ru(t) for t in types}
            for g in gm:
                typ = g["TargetId"] + 1
                if typ not in types and ru(typ) not in names:
                    flags.append("ГИД_НЕ_ТОТ_МОБ")
                elif live and g["MapId"] not in maps:
                    flags.append("ГИД_НЕ_ТА_КАРТА")
            if not any(A.name_in_text(ru(t), t_ru) for t in types):
                flags.append("RU_НЕТ_МОБА")
            if not any(A.name_in_text(en(t), t_en) for t in types):
                flags.append("EN_НЕТ_МОБА")
            for lang, txt in (("RU", t_ru), ("EN", t_en)):
                tm = text_maps(txt)
                if not tm:
                    flags.append(lang + "_НЕТ_КАРТЫ")
                elif live and not tm & set(maps):
                    flags.append(lang + "_НЕ_ТА_КАРТА")
        elif gm:
            flags.append("ГИД_МОБ_БЕЗ_ЗАЧЁТА")
        elif not gn and KILL_WORDS.search(t_en + " " + t_ru):
            flags.append("ТЕКСТ_БЕЗ_ЦЕЛИ?")
        rows.append({"q": q, "st": st, "title": title, "kills": kills, "types": types, "live": live, "maps": maps, "gm": gm,
                     "flags": sorted(set(flags)), "ru": t_ru, "en": t_en, "names": [(t, ru(t), en(t)) for t in types],
                     "book": e["QstFile"] if e else ""})
    return rows, srv


def main(argv=None):
    """Точка входа."""
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    ap = argparse.ArgumentParser()
    ap.add_argument("--server-dir", required=True)
    ap.add_argument("--out", default="")
    a = ap.parse_args(argv)
    rows, srv = audit(a.server_dir)
    c = collections.Counter(f for r in rows for f in r["flags"])
    kill = [r for r in rows if r["kills"]]
    print("стадий всего %d; с зачётом за мобов %d; с нарушениями %d" % (len(rows), len(kill), sum(1 for r in rows if r["flags"])))
    for f, n in c.most_common():
        print("  %-22s %d" % (f, n))
    if a.out:
        with open(os.path.join(a.out, "quest_stage_audit.tsv"), "w", encoding="utf-8") as f:
            f.write("квест\tстадия\tзадание\tфлаги\tтипы(спавн)\tгид\tтекст RU\tтекст EN\n")
            for r in rows:
                nm = "; ".join("%d %s/%s %s" % (t, x, y, {srv["map_name"].get(m, m): n for m, n in r["live"].get(t, {}).items()}) for t, x, y in r["names"])
                gd = "; ".join("%d@%s(%d,%d)" % (g["TargetId"] + 1, srv["map_name"].get(g["MapId"], g["MapId"]), g["X"], g["Y"]) for g in r["gm"])
                f.write("\t".join([str(r["q"]), str(r["st"]), r["title"].replace("\n", " "), ",".join(r["flags"]), nm, gd,
                                   r["ru"].replace("\n", " ").replace("\t", " "), r["en"].replace("\n", " ").replace("\t", " ")]) + "\n")
    hard = [(r["q"], r["st"], [f for f in r["flags"] if f in HARD]) for r in rows
            if (r["q"], r["st"]) not in KNOWN and any(f in HARD for f in r["flags"])]
    print("строгих нарушений %d, известных исключений %d" % (len(hard), len(KNOWN)))
    for h in hard:
        print("  ", h)
    return 1 if hard else 0


if __name__ == "__main__":
    sys.exit(main())
