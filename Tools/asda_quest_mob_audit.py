# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""Сверка целей-мобов заданий: клиент (QDT) <-> порт <-> сервер (Monster.ai) <-> спаун (.svr) <-> имя RU/EN.

Зачем (25.10.2026, баг игроков «квест засчитывает не тех мобов»). Журнал заданий 700 и трекер 1800 пишут строку
«=> Цель - Карта, x:…, y:…» из `Data\\QDT.BIN` (36 б на запись). У цели-моба (`+5 == 1`) поле `+8` — это ИНДЕКС
строки `Mob list.BIN` (эталон `QuestGuide_BuildObjectiveText@0xC68E00` → `sub_939242` → `sub_97D820`:
`база + 272*id + 100`, без сдвига), а серверный тип моба = id записи `Mob list.BIN` (`mobTableFindIndexById@0xF707D0`)
= строка + 1. Порт до правки искал имя по строке `TargetId - 1` — на одну строку выше.

Что проверяет скрипт (для каждой записи QDT с целью-мобом):
  * имя цели по эталону (строка TargetId) и по старому порту (строка TargetId-1), RU и EN;
  * засчитывает ли сервер это задание за тип TargetId+1 (`Monster.ai` +403, 5 записей: квест, шаг, предмет, шанс);
  * есть ли спаун типа TargetId+1 на карте записи (`<карта>.svr`, X @+4, Y @+8, тип @+16) и как далеко
    ближайшая точка спауна от координат метки;
  * упоминает ли текст стадии задания (QuestBook RU / EN-оверлей) имя цели.
И отдельно — задания, которые сервер засчитывает за убийства, но у которых в QDT нет цели-моба.

Источники (только чтение): клиент эталона `Donors\\AsdaChroniclesInterlude\\Data`, рабочий клиент `_MINE\\Data`
(из него импортирован порт), данные порта `RawAssets`, сервер — копия `NpcServer\\Monster` и `\\Quest`, которую
скрипт сам снимает по SSH/scp (`--fetch`, шару \\\\@@ASTORY_SERVER_HOSTNAME@@ НЕ трогаем) или берёт из `--server-dir`.

Запуск:
  python asda_quest_mob_audit.py --fetch --out @@ASTORY_DEV_ROOT_ESC@@\\Reports\\quest_mobs
  python asda_quest_mob_audit.py --server-dir <копия NpcServer> --out <папка>
Код возврата: 0 — все цели-мобы QDT по правилу эталона совпадают с сервером/спауном (кроме перечисленных
в KNOWN_ORIG_DEFECTS); 1 — есть новые расхождения; 2 — нет данных.
"""
import argparse
import collections
import csv
import json
import math
import os
import re
import struct
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as _P  # noqa: E402  (пути — только через asda_paths)

# Реквизиты dev-сервера (правило: только SSH, правка/чтение в @@ASTORY_SERVER_ROOT@@\Working_ASDA\v2).
SSH_KEY = r"C:\Users\@@ASTORY_DEV_USER@@\.ssh\@@ASTORY_SSH_KEY_NAME@@"
SSH_HOST = "Administrator@@@ASTORY_SERVER_VPN_IP@@"
SRV_ROOT = "@@ASTORY_SERVER_ROOT_FWD@@/Working_ASDA/v2/NpcServer"

# Размеры записей.
QDT_REC, MOB_REC, MAI_REC, SVR_REC = 36, 272, 479, 74
# Пара координат в тексте стадии задания.
TXT_XY = re.compile(r"x\s*:\s*(\d+)\s*,?\s*y\s*:\s*(\d+)", re.I)
# Порог «метка рядом со спауном», клеток (координаты QDT и .svr — клетки карты).
NEAR_CELLS = 40

# Известные расхождения ДАННЫХ оригинала, которые скрипт не считает новой ошибкой (заполняется по аудиту;
#   ключ — (квест, стадия, TargetId)). Пусто = известных нет.
KNOWN_ORIG_DEFECTS = {
    # Квест 338 «Подтверждение мутации 2»: в QDT эталона цель-моб 565 продублирована на стадии 1 (сдача ученику Рону);
    #   сервер засчитывает того же моба на шаге 0 — строка гида на стадии 1 лишняя, но верная по мобу. Оставлено как оригинал.
    (338, 1, 565): "дубль строки оригинала на стадии сдачи; моб верный",
}

RAW = os.path.join(_P.PORT_DIR, "AstoryClient", "RawAssets")
L10N_EN = os.path.join(RAW, "Text", "L10N", "en")
# Ведомость исправлений гида (её применяет `asda_import_quest_tables.read_qdt`).
FIXES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "quest_guide_fixes.json")


def parse_qdt(path):
    """QDT.BIN: заголовок 20 б (count @+4, тег 'QDT' @+12), записи по 36 б."""
    b = open(path, "rb").read()
    n = struct.unpack_from("<i", b, 4)[0]
    if b[12:15] != b"QDT" or 20 + QDT_REC * n != len(b):
        raise SystemExit("QDT.BIN: неожиданный формат %s (n=%d, len=%d)" % (path, n, len(b)))
    out = []
    for i in range(n):
        o = 20 + QDT_REC * i
        q, = struct.unpack_from("<i", b, o)
        out.append({"idx": i, "quest": q, "stage": b[o + 4], "kind": b[o + 5],
                    "tid": struct.unpack_from("<i", b, o + 8)[0], "map": struct.unpack_from("<i", b, o + 12)[0],
                    "cont": b[o + 16], "x": struct.unpack_from("<i", b, o + 20)[0],
                    "y": struct.unpack_from("<i", b, o + 24)[0]})
    return out


def parse_moblist(path, enc):
    """`Mob list.BIN`: заголовок 20 б (float 0.1, count), записи 272 б: +0 id, +4 модель, +100 имя."""
    b = open(path, "rb").read()
    n = struct.unpack_from("<I", b, 4)[0]
    rows = []
    for i in range(n):
        r = b[20 + MOB_REC * i:20 + MOB_REC * (i + 1)]
        rows.append({"id": struct.unpack_from("<i", r, 0)[0],
                     "model": r[4:36].split(b"\0")[0].decode("latin-1"),
                     "name": r[100:132].split(b"\0")[0].decode(enc, "replace").strip()})
    return rows


def parse_moblist_hp(path):
    """HP строк `Mob list.BIN` (+0x94, эталон `Recv_MobSpawn`: MaxHp = запись + 0x94)."""
    b = open(path, "rb").read()
    n = struct.unpack_from("<I", b, 4)[0]
    return [struct.unpack_from("<i", b, 20 + MOB_REC * i + 0x94)[0] for i in range(n)]


def load_csv_map(path):
    """CSV оверлея L10N (`key,Text`) -> dict."""
    out = {}
    if not os.path.isfile(path):
        return out
    with open(path, encoding="utf-8-sig", newline="") as f:
        for row in csv.reader(f):
            if len(row) >= 2:
                out[row[0]] = row[1]
    return out


def fetch_server(dst):
    """Снять копию NpcServer\\Monster и \\Quest + ini\\MapInfo.ini по scp (только чтение)."""
    for sub in ("Monster", "Quest", "ini"):
        os.makedirs(os.path.join(dst, sub), exist_ok=True)
    jobs = [("Monster/Monster.ai", "Monster"), ("Monster/*.svr", "Monster"),
            ("Quest/MonsterConditionTable.Bin", "Quest"), ("Quest/LimitationQuest.txt", "Quest"),
            ("ini/MapInfo.ini", "ini")]
    for src, sub in jobs:
        cmd = ["scp", "-q", "-i", SSH_KEY, "-o", "BatchMode=yes", "-o", "ConnectTimeout=15",
               "%s:%s/%s" % (SSH_HOST, SRV_ROOT, src), os.path.join(dst, sub) + os.sep]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit("scp %s: %s" % (src, r.stderr.strip()))


def load_server(sd):
    """Monster.ai (квест-записи) + спауны .svr по номеру карты MapInfo.ini."""
    mai = open(os.path.join(sd, "Monster", "Monster.ai"), "rb").read()
    kills = collections.defaultdict(list)      # (квест, шаг) -> [(тип, предмет, шанс)]
    by_quest = collections.defaultdict(set)    # квест -> {типы}
    srv_name, srv_hp = {}, {}
    for i in range(len(mai) // MAI_REC):
        r = mai[i * MAI_REC:(i + 1) * MAI_REC]
        mid = struct.unpack_from("<i", r, 0)[0]
        srv_name[mid] = r[5:45].split(b"\0")[0].decode("cp949", "replace")
        srv_hp[mid] = struct.unpack_from("<i", r, 45)[0]       # NpcServer: лог-строка «HP : %07d»
        for k in range(5):                      # NpcServer :41805-41836 — стоп на первой -1
            o = 403 + 12 * k
            q = struct.unpack_from("<i", r, o)[0]
            if q == -1:
                break
            item = struct.unpack_from("<i", r, o + 7)[0]
            if q == 0 and item == 0:
                continue
            kills[(q, r[o + 4])].append((mid, item, struct.unpack_from("<b", r, o + 11)[0]))
            by_quest[q].add(mid)
    # MapInfo.ini: MonsterInfo_NNN="<файл>.svr"
    ini = open(os.path.join(sd, "ini", "MapInfo.ini"), "rb").read().decode("cp1251", "replace")
    spawns = {}                                 # номер карты -> {тип: [(x, y)]}
    map_name = {}
    for m in re.finditer(r'MonsterInfo_(\d+)\s*=\s*"([^"]+)"', ini):
        mi, fn = int(m.group(1)), m.group(2)
        map_name[mi] = fn[:-4]
        p = os.path.join(sd, "Monster", fn)
        if not os.path.isfile(p):
            alt = [f for f in os.listdir(os.path.join(sd, "Monster")) if f.lower() == fn.lower()]
            if not alt:
                continue
            p = os.path.join(sd, "Monster", alt[0])
        d = open(p, "rb").read()
        n = struct.unpack_from("<i", d, 0)[0]   # NpcServer :55903 — count i16 в 4-байтовом поле
        n &= 0xFFFF
        per = collections.defaultdict(list)
        for k in range(n):
            o = 4 + SVR_REC * k
            if o + SVR_REC > len(d):
                break
            per[struct.unpack_from("<i", d, o + 16)[0]].append(
                (struct.unpack_from("<h", d, o + 4)[0], struct.unpack_from("<h", d, o + 8)[0]))
        spawns[mi] = per
    return {"kills": kills, "by_quest": by_quest, "spawns": spawns, "map_name": map_name, "srv_name": srv_name,
            "hp": srv_hp}


def nearest(points, x, y):
    """Расстояние (клетки) до ближайшей точки; None — точек нет / у метки нет координат."""
    if not points or x == -1 or y == -1:
        return None
    return min(math.hypot(px - x, py - y) for px, py in points)


def norm(s):
    """Для поиска имени в тексте: нижний регистр, ё→е, без пробелов/дефисов/кавычек."""
    return re.sub(r"[\s\-'’`\"«»]", "", (s or "").lower().replace("ё", "е"))


def name_in_text(name, text):
    """Имя цели упомянуто в тексте стадии (25.10.2026): КАЖДОЕ слово имени длиннее 4 букв (слова — по пробелам и дефисам)
    находится в тексте своей основой — без 2 последних букв, не короче 4 (падежи RU: «железношлем» → «железношлемов»,
    множественное EN: «Junkman» → «Junkmen»). Короткие слова («Злой», «Bad», «Gang») не проверяются."""
    t = norm(text)
    words = [w for w in re.split(r"[\s\-]+", (name or "").lower().replace("ё", "е")) if len(w) > 4]
    if not t or not words:
        return False
    return all(w[:max(4, len(w) - 2)] in t for w in words)


# Задания переноса Флэммио: текст стадии ОБЯЗАН называть моба цели (решение владельца 25.10.2026).
STRICT_TEXT_QUESTS = {566, 568, 579, 582, 584, 589, 590, 591, 592, 598, 604, 606, 607, 623, 624, 626}
# Их описания (Desc), в которых моб не назван вовсе (только предметы/NPC) — проверяются лишь на отсутствие мобов Global.
DESC_NO_MOB = {584, 589, 591, 604, 606, 624, 626}
# Имена мобов Global из прежних текстов Флэммио: после замены их не должно быть ни в стадиях, ни в описаниях
#   (регулярные выражения по нормализованному тексту — без пробелов/дефисов, нижний регистр).
GLOBAL_MOB_RE = re.compile("|".join((
    r"слиз(ен|н)",                 # «стальные/разбойные/твёрдые/Декроновы слизни, слизень» — мобы Global
    r"царапун", r"огненн\w{0,4}голем", r"лавов\w{0,4}голем", r"железяк\w{0,2}(воин|часов)", r"декронов",
    r"steelslime", r"firegolem", r"flamegolems", r"lavagolem", r"junkmanwarrior", r"junkmansentr", r"violonger",
    r"deckron(slime|junk)",
)))


def global_mob_in(text):
    """Основа имени моба Global в тексте (первая найденная) или пусто."""
    m = GLOBAL_MOB_RE.search(norm(text))
    return m.group(0) if m else ""


def edge_space_titles(raw_dir, l10n_dir):
    """Названия заданий с пробелом в начале/конце (в т.ч. внутри кавычек RU): [(таблица, ключ, значение)]."""
    def bad(v):
        s = v.strip()
        inner = s[1:-1] if len(s) >= 2 and s[0] == '"' and s[-1] == '"' else s
        return v != s or inner != inner.strip()
    out = []
    for b in json.load(open(os.path.join(raw_dir, "Data", "Quest", "QuestBook.json"), encoding="utf-8")):
        if bad(b["Title"]):
            out.append(("RU QuestBook", b["Name"], b["Title"]))
    for e in json.load(open(os.path.join(raw_dir, "Data", "NPC", "EpisodeTable.json"), encoding="utf-8")):
        if bad(e["Title"]):
            out.append(("RU EpisodeTable", str(e["EpisodeId"]), e["Title"]))
    for t in ("QuestBook", "EpisodeTable", "Raid_quest_data"):
        for k, v in load_csv_map(os.path.join(l10n_dir, "DT_Loc_%s_en.csv" % t)).items():
            if re.search(r"\.(Title|name)$", k) and bad(v):
                out.append(("EN " + t, k, v))
    return out


def build(args):
    sd = args.server_dir
    if args.fetch:
        sd = tempfile.mkdtemp(prefix="asda_qmob_")
        fetch_server(sd)
    if not sd or not os.path.isdir(sd):
        print("нет копии сервера: --fetch или --server-dir")
        return None
    srv = load_server(sd)
    if args.fetch:
        shutil.rmtree(sd, ignore_errors=True)     # временная копия сервера больше не нужна

    qdt_orig = parse_qdt(os.path.join(_P.DATA_ORIG, "QDT.BIN"))
    qdt_mine = parse_qdt(os.path.join(_P.DATA, "QDT.BIN"))
    mob_orig = parse_moblist(os.path.join(_P.DATA_ORIG, "Mob", "Mob list.BIN"), "cp949")   # эталон: EN-имена
    mob_mine = parse_moblist(os.path.join(_P.DATA, "Mob", "Mob list.BIN"), "cp1251")      # рабочий клиент: RU
    guide = json.load(open(os.path.join(RAW, "Data", "Quest", "QuestGuide.json"), encoding="utf-8"))
    book = {r["Name"]: r for r in json.load(open(os.path.join(RAW, "Data", "Quest", "QuestBook.json"), encoding="utf-8"))}
    ep = {r["QuestKey"]: r for r in json.load(open(os.path.join(RAW, "Data", "NPC", "EpisodeTable.json"), encoding="utf-8"))}
    mob_en = load_csv_map(os.path.join(L10N_EN, "DT_Loc_MobNames_en.csv"))
    book_en = load_csv_map(os.path.join(L10N_EN, "DT_Loc_QuestBook_en.csv"))
    ep_en = load_csv_map(os.path.join(L10N_EN, "DT_Loc_EpisodeTable_en.csv"))

    # Порт = MINE + ведомость `Tools/data/quest_guide_fixes.json`: сверка импорта (иначе аудит порта бессмыслен).
    fixes = {f["index"]: f for f in json.load(open(FIXES, encoding="utf-8"))["fixes"]} if os.path.isfile(FIXES) else {}
    exp = []
    for r in qdt_mine:
        e = {"QuestNo": r["quest"], "Stage": r["stage"], "TargetKind": r["kind"], "TargetId": r["tid"], "MapId": r["map"],
             "ContinueFlag": r["cont"], "X": r["x"], "Y": r["y"]}
        f = fixes.get(r["idx"])
        if f and all(e.get(k) == v for k, v in f["expect"].items()):
            e.update(f["set"])
        exp.append(e)
    # ★ 06.11.2026: строки гида, добавленные ведомостью (`adds`), идут за концом таблицы.
    exp += json.load(open(FIXES, encoding="utf-8")).get("adds", []) if os.path.isfile(FIXES) else []
    flds = ("QuestNo", "Stage", "TargetKind", "TargetId", "MapId", "ContinueFlag", "X", "Y")
    import_ok = [tuple(g[k] for k in flds) for g in guide] == [tuple(e[k] for k in flds) for e in exp]
    okey = {(r["quest"], r["stage"], r["kind"], r["tid"]) for r in qdt_orig}
    mob_ids_ok = all(r["id"] == i + 1 for i, r in enumerate(mob_mine)) and all(r["id"] == i + 1 for i, r in enumerate(mob_orig))
    # Нумерация мобов клиент <-> сервер (вариант «перенумерация»): HP строки `type-1` (+0x94) = HP типа в Monster.ai (+45);
    #   сравнение со сдвигом ±1 — контроль метода (при верной нумерации совпадений со сдвигом почти нет).
    mob_hp = parse_moblist_hp(os.path.join(_P.DATA, "Mob", "Mob list.BIN"))
    hp_shift = {d: sum(1 for i, hp in enumerate(mob_hp) if srv["hp"].get(i + 1 + d) == hp) for d in (-1, 0, 1)}
    # Полнота EN-имён мобов (оверлей `MobNames`): строк без перевода.
    en_missing = [i for i in range(len(mob_mine)) if "%d.Name" % i not in mob_en]

    def ru(row):
        return mob_mine[row]["name"] if 0 <= row < len(mob_mine) else ""

    def en(row):
        if not 0 <= row < len(mob_mine):
            return ""
        return mob_en.get("%d.Name" % row) or (mob_orig[row]["name"] if row < len(mob_orig) else "")

    def title(q):
        e = ep.get(q)
        if not e:
            return "", ""
        b = book.get(e["QstFile"])
        t_ru = (b["Title"] if b else e["Title"]).strip('"')
        t_en = book_en.get(e["QstFile"] + ".Title") or ep_en.get("%d.Title" % e["EpisodeId"], "")
        return t_ru, t_en

    def step_text(q, st):
        e = ep.get(q)
        if not e:
            return "", ""
        b = book.get(e["QstFile"])
        t_ru = b["Steps"][st] if b and st < len(b["Steps"]) else ""
        return t_ru, book_en.get("%s.Steps.%d" % (e["QstFile"], st), "")

    def desc_text(q):
        """Описание задания (RU из книги, EN из оверлея)."""
        e = ep.get(q)
        if not e:
            return "", ""
        b = book.get(e["QstFile"])
        return (b["Desc"] if b else ""), book_en.get("%s.Desc" % e["QstFile"], "")

    rows = []
    raw_by_idx = {r["idx"]: r for r in qdt_mine}
    for g in guide:                                    # то, что видит игрок: данные порта
        if g["TargetKind"] != 1:
            continue
        raw = raw_by_idx.get(g["Index"], {})
        r = {"idx": g["Index"], "quest": g["QuestNo"], "stage": g["Stage"], "tid": g["TargetId"], "map": g["MapId"],
             "x": g["X"], "y": g["Y"]}
        q, st, tid = r["quest"], r["stage"], r["tid"]
        typ = tid + 1                                   # серверный тип по правилу эталона
        srv_types = sorted(t for t, _, _ in srv["kills"].get((q, st), []))   # типы, засчитанные за этот шаг
        srv_any = sorted(srv["by_quest"].get(q, set()))                       # за любой шаг этого квеста
        sp = srv["spawns"].get(r["map"], {})
        d_t = nearest(sp.get(typ), r["x"], r["y"])
        d_srv = min([d for d in (nearest(sp.get(t), r["x"], r["y"]) for t in srv_types) if d is not None], default=None)
        name_new_ru, name_new_en = ru(tid), en(tid)
        name_old_ru, name_old_en = ru(tid - 1), en(tid - 1)
        srt_ru, srt_en = step_text(q, st)
        # Координаты из текста стадии («x:120 y:160», «x:265, y:312») — ближайшая пара к метке гида.
        txy = [(int(a), int(b)) for a, b in TXT_XY.findall(srt_en + " " + srt_ru)]
        d_txt = min((math.hypot(a - r["x"], b - r["y"]) for a, b in txy), default=None) if r["x"] != -1 else None
        # Вердикт по серверу: тип засчитывается за этот шаг / за другой шаг этого квеста / не засчитывается.
        srv_names = {ru(t - 1) for t in srv_types}
        if typ in srv_types:
            v_srv = "OK"
        elif srv_types and name_new_ru in srv_names:
            v_srv = "OK-имя"                            # другой вариант того же моба (пары _F/_M)
        elif typ in srv_any:
            v_srv = "другой шаг"
        elif srv_any or srv_types:
            v_srv = "НЕ ТОТ МОБ"
        else:
            v_srv = "не убийство"                       # сервер этот квест за мобов не засчитывает
        n_sp = len(sp.get(typ, []))
        if d_t is None:
            v_sp = "нет спауна на карте" if r["x"] != -1 else ("спаун есть" if n_sp else "нет спауна на карте")
        elif d_t <= NEAR_CELLS:
            v_sp = "рядом"
        else:
            v_sp = "далеко"
        rows.append({
            "quest": q, "stage": st, "title_ru": title(q)[0], "title_en": title(q)[1], "tid": tid, "type": typ,
            "map": r["map"], "map_name": srv["map_name"].get(r["map"], "?"), "x": r["x"], "y": r["y"],
            "in_orig_qdt": (raw.get("quest"), raw.get("stage"), raw.get("kind"), raw.get("tid")) in okey,
            "fixed": "%d→%d" % (raw["tid"], tid) if g["Index"] in fixes and raw.get("tid") != tid else
                     ("да" if g["Index"] in fixes else ""),
            "name_ru": name_new_ru, "name_en": name_new_en,
            # Что показывал порт до 25.10: строка (сырой TargetId − 1).
            "old_ru": ru(raw.get("tid", tid) - 1), "old_en": en(raw.get("tid", tid) - 1),
            "port_bug": ru(raw.get("tid", tid) - 1) != name_new_ru or en(raw.get("tid", tid) - 1) != name_new_en,
            "en_from_overlay": "%d.Name" % tid in mob_en,
            "srv_types": srv_types, "srv_any": srv_any, "v_srv": v_srv,
            "n_spawn": n_sp, "d_spawn": d_t, "d_srv_spawn": d_srv, "v_spawn": v_sp,
            "txt_ru": name_in_text(name_new_ru, srt_ru), "txt_en": name_in_text(name_new_en, srt_en),
            "txt_old_ru": name_in_text(name_old_ru, srt_ru), "txt_old_en": name_in_text(name_old_en, srt_en),
            "d_text_xy": d_txt,
            # ★ 25.10.2026 (решение владельца): описание задания — моб цели назван (кроме описаний без моба), мобов Global нет.
            "desc_ru": name_in_text(name_new_ru, desc_text(q)[0]), "desc_en": name_in_text(name_new_en, desc_text(q)[1]),
            "global_left": "; ".join(x for x in (global_mob_in(srt_ru), global_mob_in(srt_en),
                                                 global_mob_in(desc_text(q)[0]), global_mob_in(desc_text(q)[1])) if x),
        })

    # Задания, которые сервер засчитывает за убийства, но без цели-моба в QDT.
    qdt_mob_q = {(g["QuestNo"], g["Stage"]) for g in guide if g["TargetKind"] == 1}
    qdt_any = collections.defaultdict(list)
    for g in guide:
        qdt_any[(g["QuestNo"], g["Stage"])].append({"kind": g["TargetKind"], "tid": g["TargetId"]})
    orphans = []
    for (q, st), lst in sorted(srv["kills"].items()):
        if (q, st) in qdt_mob_q:
            continue
        orphans.append({"quest": q, "stage": st, "title_ru": title(q)[0], "title_en": title(q)[1],
                        "types": sorted({t for t, _, _ in lst}),
                        "qdt": ["%s %d" % ("моб" if g["kind"] == 1 else "NPC", g["tid"]) for g in qdt_any.get((q, st), [])],
                        "in_book": q in ep})
    return {"rows": rows, "orphans": orphans, "import_ok": import_ok, "mob_ids_ok": mob_ids_ok,
            "n_qdt_orig": len(qdt_orig), "n_qdt_mine": len(qdt_mine), "srv": srv, "ru": ru,
            "hp_shift": hp_shift, "n_mob": len(mob_hp), "en_missing": en_missing, "n_fixes": len(fixes),
            "edge_titles": edge_space_titles(RAW, L10N_EN)}


def fmt_d(d):
    return "—" if d is None else "%.0f" % d


def write_report(res, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    rows, ru = res["rows"], res["ru"]
    tsv = os.path.join(out_dir, "quest_mob_audit.tsv")
    cols = ["quest", "stage", "title_ru", "title_en", "tid", "type", "map", "map_name", "x", "y", "in_orig_qdt", "fixed",
            "name_ru", "name_en", "en_from_overlay", "old_ru", "old_en", "port_bug", "srv_types", "srv_any", "v_srv",
            "n_spawn", "d_spawn", "d_srv_spawn", "v_spawn", "txt_ru", "txt_en", "txt_old_ru", "txt_old_en",
            "d_text_xy"]
    with open(tsv, "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(cols)
        for r in rows:
            w.writerow([" ".join(map(str, r[c])) if isinstance(r[c], list) else
                        (fmt_d(r[c]) if c.startswith("d_") else r[c]) for c in cols])
    with open(os.path.join(out_dir, "quest_mob_orphans.tsv"), "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["quest", "stage", "title_ru", "title_en", "srv_types", "srv_names_ru", "qdt", "in_book"])
        for o in res["orphans"]:
            w.writerow([o["quest"], o["stage"], o["title_ru"], o["title_en"], " ".join(map(str, o["types"])),
                        "; ".join(sorted({ru(t - 1) for t in o["types"]})), "; ".join(o["qdt"]), o["in_book"]])
    return tsv


def summarize(res):
    rows = res["rows"]
    c = collections.Counter
    s = {
        "qdt_mob_rows": len(rows),
        "port_bug_rows": sum(r["port_bug"] for r in rows),
        "v_srv": dict(c(r["v_srv"] for r in rows)),
        "v_spawn": dict(c(r["v_spawn"] for r in rows)),
        "txt_ru_new": sum(r["txt_ru"] for r in rows), "txt_ru_old": sum(r["txt_old_ru"] for r in rows),
        "txt_en_new": sum(r["txt_en"] for r in rows), "txt_en_old": sum(r["txt_old_en"] for r in rows),
        "orphans": len(res["orphans"]), "import_ok": res["import_ok"], "mob_ids_ok": res["mob_ids_ok"],
        "n_qdt_orig": res["n_qdt_orig"], "n_qdt_mine": res["n_qdt_mine"], "n_fixes": res["n_fixes"],
        "hp_match_by_shift": res["hp_shift"], "n_mob_rows": res["n_mob"],
        "text_xy_far": sum(1 for r in rows if r["d_text_xy"] is not None and r["d_text_xy"] > NEAR_CELLS),
        "text_xy_known": sum(1 for r in rows if r["d_text_xy"] is not None),
        "en_missing_rows": len(res["en_missing"]), "en_missing_in_targets": sum(not r["en_from_overlay"] for r in rows),
    }
    bad = [r for r in rows if r["v_srv"] in ("НЕ ТОТ МОБ", "другой шаг")
           and (r["quest"], r["stage"], r["tid"]) not in KNOWN_ORIG_DEFECTS]
    # ★ 25.10.2026: тексты стадий Флэммио обязаны называть моба цели (= моба сервера) — RU и EN.
    strict = [r for r in rows if r["quest"] in STRICT_TEXT_QUESTS and r["v_srv"] in ("OK", "OK-имя")]
    s["strict_text_rows"] = len(strict)
    s["strict_desc_rows"] = sum(1 for r in strict if r["quest"] not in DESC_NO_MOB)
    for r in strict:
        if not (r["txt_ru"] and r["txt_en"]):
            bad.append(dict(r, title_ru="ТЕКСТ СТАДИИ ≠ ЦЕЛЬ (RU %s, EN %s) — %s" % (r["txt_ru"], r["txt_en"], r["title_ru"])))
        if r["quest"] not in DESC_NO_MOB and not (r["desc_ru"] and r["desc_en"]):
            bad.append(dict(r, title_ru="ОПИСАНИЕ ≠ ЦЕЛЬ (RU %s, EN %s) — %s" % (r["desc_ru"], r["desc_en"], r["title_ru"])))
        if r["global_left"]:
            bad.append(dict(r, title_ru="ОСТАЛОСЬ ИМЯ МОБА GLOBAL «%s» — %s" % (r["global_left"], r["title_ru"])))
    # ★ 25.10.2026: пробел в начале/конце названия задания (RU/EN).
    s["edge_space_titles"] = len(res["edge_titles"])
    for t, k, v in res["edge_titles"]:
        bad.append({"quest": -1, "stage": -1, "title_ru": "ПРОБЕЛ В НАЗВАНИИ %s %s" % (t, k), "tid": -1, "name_ru": repr(v),
                    "srv_types": []})
    s["new_mismatch"] = len(bad)
    # Нумерация: при сдвиге 0 совпадений HP должно быть больше, чем при ±1 (иначе клиент и сервер перенумерованы).
    if not (res["hp_shift"][0] > 2 * max(res["hp_shift"][-1], res["hp_shift"][1])):
        bad.append({"quest": -1, "stage": -1, "title_ru": "НУМЕРАЦИЯ МОБОВ", "tid": -1, "name_ru": str(res["hp_shift"]),
                    "srv_types": []})
    if not res["import_ok"]:
        bad.append({"quest": -1, "stage": -1, "title_ru": "ИМПОРТ ≠ MINE+ведомость", "tid": -1, "name_ru": "", "srv_types": []})
    return s, bad


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--fetch", action="store_true", help="снять копию NpcServer по scp (только чтение)")
    ap.add_argument("--server-dir", default="", help="локальная копия NpcServer (Monster/, Quest/, ini/)")
    ap.add_argument("--out", default=os.path.join(_P.RE_REPORTS, "quest_mobs"))
    a = ap.parse_args(argv)
    res = build(a)
    if res is None:
        return 2
    tsv = write_report(res, a.out)
    s, bad = summarize(res)
    print(json.dumps(s, ensure_ascii=False, indent=1))
    for r in bad:
        print("РАСХОЖДЕНИЕ: квест %d ст.%d «%s» цель %d (%s) сервер=%s" % (
            r["quest"], r["stage"], r["title_ru"], r["tid"], r["name_ru"], r["srv_types"]))
    print("выгрузка:", tsv)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
