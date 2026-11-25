# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_npc_quest_data.py — ТАБЛИЦЫ КВЕСТОВЫХ МЕТОК NPC («!» над головой + точки на карте).
#
# ══════════════════════════════════ ЧТО ИМПОРТИРУЕМ ══════════════════════════════════════════════
#  1) DT_NpcQuestScript  ← ./Data/NPC/Episode/NPC/Nsr.bsz  (ZIP-контейнер CPackFile, 187 файлов *.NSR)
#     Эталон CNpcData_LoadNSR@0xD9C1E0:
#         fread(header, 4);  fread(buffer, 14352);          // буфер = байты файла [4 .. 4+14352)
#     Записи сценария лежат в буфере со СМЕЩЕНИЯ 48, шаг 144 байта; сканируется РОВНО 64 штуки
#     (questFindMatchingActionMode1@0xD8AB90: `for (i=0; i<64; ++i) rec = &a14 + 36*i`, где a14 —
#     14-й аргумент = v7[12] = buffer+48). ⇒ смещение записи i в ФАЙЛЕ = 4 + 48 + 144*i = 52 + 144*i.
#     Поля записи (выведены из itemTooltip_BuildStatFlagsTable1@0xD84810 и
#     charEquip_VerifyStatChecksum@0xD850D0 — см. Source/AsdaReborn/Public/Quest/AsdaQuestTypes.h):
#         +0   i32  opcode  (121 крафт, 122/123 спец, прочее — обычная запись)
#         +4   i16  pageId  (страница диалога)
#         +6   i16  МЕТКА   (1 жёлтый «!», 2 красный «!», 3 пурпурный «?»)
#         +8/+10   u16 уровень min/max
#         +12/+16  i32 класс / режим сравнения
#         +20/+24/+28/+32  i32 квесты: «не взят», «завершён», «в работе», «не взят»
#         +36      i32 стадия активного квеста
#         +40/+48/+56/+64/+72 i32 цели, +44/+52/+60/+68/+76 u16 их количества
#         +78/+80  u16 два флага режима сверки целей
#         +84…+116 i32 предметы, +88…+120 u16 их количества
#         +124 i32 «иметь предмет», +128/+132 i32 вкладка сумки, +136 u16 слот, +140 i32 прочее
#     В таблицу НЕ кладутся записи, которые НИКОГДА не срабатывают и не влияют на цикл:
#     opcode не 121/122/123 И ни одного заданного условия (Verify требует cnt>0).
#
#  2) DT_NpcQuestNpc ← ./Data/NPC/npc_list.BIN (заголовок 20 б, запись 136 б; npcListLoadFromBin@0xD9A3C0)
#         +0    i32   npcId (1-based)
#         +0x26 char  имя NSR-сценария  (npcSlotLoadModelAndSetup: sprintf("%s.NSR", rec+38))
#         +0x68 i32   ВЫСОТА сущности (worldEntityInitByType@0xF98B30: heightParam; -1 => 150) —
#                     из неё строится AABB (полу-размер 50,H/2,50) и на его ВЕРХ садится плашка имени
#         +0x6C i32   mapId,  +0x70 float мир X,  +0x74 float мир Y   (npcUi_BuildNpcActionList@0xD97650)
#         +0x7C i32   ТИП NPC — в список карты идут только 0, 4 и 5
#
# СВЕРКА: после записи CSV скрипт заново разбирает исходники и сравнивает поле-в-поле с тем, что
#   попало в CSV, печатая «N/N расхождений 0».
#
# Запуск: Tools\run_headless.bat Tools\asda_import_npc_quest_data.py
#   (без UE — python asda_import_npc_quest_data.py --csv-only — только CSV + сверка)
import os
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_bintable as _BT     # ★ дедуп №2 В1 Т5: общий разбор C-строк
import struct
import sys
import zipfile

GAME     = _P.DATA
NSR_ZIP  = os.path.join(GAME, r"NPC\Episode\NPC\Nsr.bsz")
NPC_LIST = os.path.join(GAME, r"NPC\npc_list.BIN")

OUT_DIR  = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "NPC")
CSV_SCRIPT = os.path.join(OUT_DIR, "NpcQuestScript.csv")
CSV_NPC    = os.path.join(OUT_DIR, "NpcQuestNpc.csv")

PKG = "/Game/AsdaAssets/Data/NPC"
DT_SCRIPT_NAME, DT_NPC_NAME = "DT_NpcQuestScript", "DT_NpcQuestNpc"
RS_SCRIPT = "/Script/AsdaReborn.AsdaNpcQuestScriptRow"
RS_NPC    = "/Script/AsdaReborn.AsdaNpcQuestNpcRow"

REC_BASE, REC_STRIDE, REC_COUNT = 52, 144, 64     # эталонные константы (см. шапку)
NPC_HDR, NPC_STRIDE = 0x14, 0x88


def log(msg):
    try:
        import unreal
        unreal.log_warning("[quest-dt] %s" % msg)
    except Exception:
        print("[quest-dt] %s" % msg)


# ── Разбор .NSR ─────────────────────────────────────────────────────────────────────────────────
def parse_nsr(blob):
    """Возвращает список словарей-записей (индекс в файле сохраняется в RecIndex)."""
    buf = blob[:4 + 14352]
    out = []
    for i in range(REC_COUNT):
        o = REC_BASE + REC_STRIDE * i
        if o + REC_STRIDE > len(buf):
            break
        I = lambda k: struct.unpack_from("<i", buf, o + k)[0]
        U = lambda k: struct.unpack_from("<H", buf, o + k)[0]
        S = lambda k: struct.unpack_from("<h", buf, o + k)[0]
        rec = dict(
            RecIndex=i, Opcode=I(0), PageId=S(4), Mark=S(6),
            LevelMin=U(8), LevelMax=U(10),
            ClassId=I(12), ClassMode=I(16),
            QuestFreeA=I(20), QuestDone=I(24), QuestActive=I(28), QuestFreeB=I(32), QuestStage=I(36),
            ObjIds=[I(40), I(48), I(56), I(64), I(72)],
            ObjCounts=[U(44), U(52), U(60), U(68), U(76)],
            ObjFlagNone=U(78), ObjFlagAny=U(80),
            ReqItemIds=[I(84), I(92), I(100), I(108), I(116)],
            ReqItemCounts=[U(88), U(96), U(104), U(112), U(120)],
            HaveItemId=I(124), BagTabA=I(128), BagTabB=I(132), BagSlot=U(136), MiscCond=I(140),
        )
        out.append(rec)
    return out


def rec_is_useful(r):
    """Запись влияет на выбор метки? (иначе цикл эталона её просто пропускает)"""
    if r["Opcode"] in (121, 122, 123):
        return True
    if r["LevelMin"] or r["LevelMax"]:
        return True
    if r["ClassId"] > -1 or r["ClassMode"] > -1:
        return True
    if r["QuestDone"] > -1 or r["QuestFreeA"] > -1 or r["QuestActive"] > -1 or r["QuestFreeB"] > -1:
        return True
    if any(v > -1 for v in r["ReqItemIds"]):
        return True
    if r["HaveItemId"] > -1 or r["BagTabA"] > 0 or r["BagTabB"] > 0 or r["BagSlot"] or r["MiscCond"] > 0:
        return True
    return False


def read_scripts():
    z = zipfile.ZipFile(NSR_ZIP)
    names = sorted(n for n in z.namelist() if n.lower().endswith(".nsr"))
    rows = []
    for n in names:
        nsr = os.path.basename(n)[:-4].lower()
        for r in parse_nsr(z.read(n)):
            if rec_is_useful(r):
                r = dict(r); r["NsrName"] = nsr
                rows.append(r)
    # ★ Часть ЧТЕНИЯ, а не отдельный проход: тогда сверка `verify()` (она зовёт `read_scripts` повторно) сравнивает
    #   одинаково подготовленные данные, а повторный импорт правки не теряет.
    rows = apply_donor_scripts(rows)
    rows = apply_script_fixes(rows)
    return names, rows


# ★ 04.11.2026 (questmark-chmella): сценарии, которые в нашей сборке — заглушка, а у доноров есть целиком (tomb1, NPC 64:
#   задания 127/128/129). Ведомость строит ТОЛЬКО `Tools/asda_nsr_donor_restore.py` (разбор — в его шапке); записи донора
#   ЗАМЕНЯЮТ записи сценария целиком (`replace_records`), уровни уже приведены к серверной ConditionTable.
DONOR_SCRIPTS = os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "data", "nsr_donor_scripts.json")


def apply_donor_scripts(rows):
    if not os.path.isfile(DONOR_SCRIPTS):
        return rows
    import json
    led = json.load(open(DONOR_SCRIPTS, encoding="utf-8"))["rows"]
    out = rows
    for d in led:
        if not d.get("replace_records"):
            continue
        nsr = d["nsr"].lower()
        out = [r for r in out if r["NsrName"] != nsr]
        for r in d["records"]:
            r = dict(r); r["NsrName"] = nsr
            if rec_is_useful(r):
                out.append(r)
        log("донорский сценарий %s (%s): записей %d вместо записей заглушки" % (nsr, d["donor"], len(d["records"])))
    return out


# ★★★ 29.10.2026 (решение владельца «выровняй»): условия записей сценария — метка «!» над NPC и вход в ветку квеста —
#   приводятся к СЕРВЕРНЫМ условиям квестов. Ведомость `Tools/data/nsr_script_fixes.json` строит только генератор
#   `Tools/asda_nsr_script_align.py` (разбор — в его шапке); здесь она применяется. Правка записи (`set`) или удаление
#   записи (`disable`: квест, которого нет ни в эпизодах клиента, ни в выдаче сервера). Каждая правка несёт `was` —
#   значения ДО правки: если источник `.NSR` с тех пор изменился, правка НЕ применяется (строка «!!» в журнал).
SCRIPT_FIXES = os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "data", "nsr_script_fixes.json")
# Перенаправление сценариев на страницы сдачи 16 заданий (31.10.2026, bug-quest-follow).
SCRIPT_TURNIN_FIXES = os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "data", "nsr_script_turnin_fixes.json")


def apply_script_fixes(rows):
    if not os.path.isfile(SCRIPT_FIXES):
        log("ведомость записей сценария %s не найдена — записи как в .NSR" % SCRIPT_FIXES)
        return rows
    import json
    fixes = {(f["nsr"], f["rec"]): f for f in json.load(open(SCRIPT_FIXES, encoding="utf-8"))["rows"]}
    # ★ 31.10.2026 (bug-quest-follow): записи сценария, перенаправленные на новые страницы сдачи (75 → ella_19, 85 → ella_18;
    #   прежние страницы 3/4 заняты другим содержимым). Ведомость строит `Tools/asda_quest_turnin_restore.py`.
    if os.path.isfile(SCRIPT_TURNIN_FIXES):
        for f in json.load(open(SCRIPT_TURNIN_FIXES, encoding="utf-8"))["rows"]:
            if (f["nsr"], f["rec"]) in fixes:
                raise RuntimeError("запись сценария %s#%d есть в обеих ведомостях" % (f["nsr"], f["rec"]))
            fixes[(f["nsr"], f["rec"])] = f
    out, applied, dropped, stale = [], 0, 0, 0
    for r in rows:
        f = fixes.get((r["NsrName"], r["RecIndex"]))
        if not f:
            out.append(r)
            continue
        if any(r.get(k) != v for k, v in f["was"].items()):
            stale += 1
            log("!! запись сценария %s#%d изменилась в .NSR после построения ведомости — правка не применена"
                % (r["NsrName"], r["RecIndex"]))
            out.append(r)
            continue
        if f.get("disable"):
            dropped += 1
            continue
        r = dict(r)
        r.update(f["set"])
        applied += 1
        out.append(r)
    log("записи сценария по серверу: выровнено %d, убрано %d, устарело %d (ведомость %d)" % (applied, dropped, stale, len(fixes)))
    return out


# ★★★ 31.10.2026 [ИСПРАВЛЕНИЕ] (решение владельца «у сервера верные значения — синхронизируй позиции»): места NPC класса
#   0/2/4 — по серверному спавну `NpcInfo/<карта>.npc` (эталон ставит их в клетку сервера, а метки карты брал из npc_list —
#   у 36/49/104/139/182 «!» стоял не у NPC, у 37 — на другой карте, 38/39/55/74 без места). Ведомость `Tools/data/npc_list_fixes.json`
#   строит только `Tools/asda_npc_list_sync.py` (разбор — в его шапке). Правка несёт `was`: если npc_list с тех пор изменился,
#   она НЕ применяется («!!» в журнал). `extra` — второе и далее места NPC на сервере: строки `<NpcId>_<n>` с тем же NpcId.
#   Откат: ASDA_NPCLIST_FIXES=0 при импорте.
NPC_FIXES = os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "data", "npc_list_fixes.json")


def apply_npc_fixes(rows):
    if os.environ.get("ASDA_NPCLIST_FIXES", "1") == "0":
        log("места NPC: ведомость отключена (ASDA_NPCLIST_FIXES=0) — как в npc_list.BIN")
        return rows
    if not os.path.isfile(NPC_FIXES):
        log("ведомость мест NPC %s не найдена — места как в npc_list.BIN" % NPC_FIXES)
        return rows
    import json
    fixes = {f["npc"]: f for f in json.load(open(NPC_FIXES, encoding="utf-8"))["rows"]}
    out, applied, extra, stale, used = [], 0, 0, 0, set()
    for r in rows:
        out.append(r)
        f = fixes.get(r["NpcId"])
        if not f:
            continue
        used.add(r["NpcId"])
        if any(r[k] != v for k, v in f["was"].items()):
            stale += 1
            log("!! место NPC %d изменилось в npc_list после построения ведомости — правка не применена" % r["NpcId"])
            continue
        r.update(f["set"])
        applied += 1
        for n, e in enumerate(f.get("extra", [])):
            x = dict(r)
            x.update(e)
            x["_row"] = "%d_%d" % (r["NpcId"], n + 2)
            out.append(x)
            extra += 1
    for nid in sorted(set(fixes) - used):
        stale += 1
        log("!! NPC %d из ведомости мест нет в npc_list" % nid)
    log("места NPC по серверу: исправлено %d, вторых мест %d, устарело %d (ведомость %d)" % (applied, extra, stale, len(fixes)))
    return out


# ── Разбор npc_list.BIN ─────────────────────────────────────────────────────────────────────────
def read_npcs(apply_fixes=True):
    b = open(NPC_LIST, "rb").read()
    cnt = struct.unpack_from("<i", b, 4)[0]
    assert len(b) == NPC_HDR + NPC_STRIDE * cnt, "npc_list.BIN: размер не сходится"

    def cstr(o, m=48):
        """C-строка поля m байт (до первого NUL), latin1 — общий `asda_bintable.cstr` (дедуп №2 В1 Т5)."""
        return _BT.cstr(b, o, m, "latin1")

    out = []
    for i in range(cnt):
        r = NPC_HDR + NPC_STRIDE * i
        out.append(dict(
            NpcId=struct.unpack_from("<i", b, r)[0],
            NsrName=cstr(r + 0x26).lower(),
            # ★ +0x46 — базовое имя модели; оно же имя ПОРТРЕТА окна диалога
            #   (NPC_OpenWindow@0xD99D80: "\Data\NPC\Image\\" + rec[+0x46] + ".TEX").
            ModelName=cstr(r + 0x46),
            PlateHeight=struct.unpack_from("<i", b, r + 0x68)[0],
            MapId=struct.unpack_from("<i", b, r + 0x6C)[0],
            WorldX=struct.unpack_from("<f", b, r + 0x70)[0],
            WorldY=struct.unpack_from("<f", b, r + 0x74)[0],
            NpcType=struct.unpack_from("<i", b, r + 0x7C)[0],
        ))
    return apply_npc_fixes(out) if apply_fixes else out


# ── CSV ─────────────────────────────────────────────────────────────────────────────────────────
SCRIPT_COLS = ["NsrName", "RecIndex", "Opcode", "PageId", "Mark", "LevelMin", "LevelMax",
               "ClassId", "ClassMode", "QuestDone", "QuestFreeA", "QuestFreeB", "QuestActive",
               "QuestStage", "ObjIds", "ObjCounts", "ObjFlagAny", "ObjFlagNone",
               "ReqItemIds", "ReqItemCounts", "HaveItemId", "BagTabA", "BagTabB", "BagSlot", "MiscCond"]
NPC_COLS = ["NpcId", "NsrName", "ModelName", "PlateHeight", "MapId", "WorldX", "WorldY", "NpcType"]


def arr(v):
    return '"(%s)"' % ",".join(str(x) for x in v)


def script_csv(rows):
    out = ["_row," + ",".join(SCRIPT_COLS)]
    for r in rows:
        key = "%s_%02d" % (r["NsrName"], r["RecIndex"])
        cells = [key, '"%s"' % r["NsrName"]]
        for c in SCRIPT_COLS[1:]:
            v = r[c]
            cells.append(arr(v) if isinstance(v, list) else str(v))
        out.append(",".join(cells))
    return "\n".join(out) + "\n"


def npc_csv(rows):
    out = ["_row," + ",".join(NPC_COLS)]
    for r in rows:
        # Имя строки = NpcId (поиск `AsdaDt::RowByInt`); второе и далее места NPC — `<NpcId>_<n>` (ведомость мест).
        out.append("%s,%d,\"%s\",\"%s\",%d,%d,%.6f,%.6f,%d" % (
            r.get("_row", str(r["NpcId"])), r["NpcId"], r["NsrName"], r["ModelName"], r["PlateHeight"], r["MapId"],
            r["WorldX"], r["WorldY"], r["NpcType"]))
    return "\n".join(out) + "\n"


# ── Сверка: CSV ↔ повторный разбор источника ────────────────────────────────────────────────────
def verify(script_rows, npc_rows):
    bad = 0
    _, again_s = read_scripts()
    if len(again_s) != len(script_rows):
        log("!! сценарии: число строк разошлось %d != %d" % (len(again_s), len(script_rows)))
        bad += 1
    else:
        for a, b in zip(again_s, script_rows):
            for c in SCRIPT_COLS:
                if a[c] != b[c]:
                    bad += 1
                    if bad < 10:
                        log("!! %s#%d поле %s: %r != %r" % (a["NsrName"], a["RecIndex"], c, a[c], b[c]))
    log("сценарии NSR: %d/%d, расхождений %d" % (len(script_rows), len(again_s), bad))

    bad2 = 0
    again_n = read_npcs()
    if len(again_n) != len(npc_rows):
        bad2 += 1
    else:
        for a, b in zip(again_n, npc_rows):
            for c in NPC_COLS:
                if a[c] != b[c]:
                    bad2 += 1
    log("npc_list: %d/%d, расхождений %d" % (len(npc_rows), len(again_n), bad2))
    return bad + bad2 + verify_server_places(npc_rows)


# ★ 31.10.2026: места строк — против слепка серверного спавна `Tools/data/npc_server_spawn.json` (строит
#   `asda_npc_list_sync.py` по всем копиям стойки; та же сверка, что его `--check`, но без сервера). Класс 0/2/4: набор клеток
#   строк NPC = набор мест сервера; класс 1/3/5: каждая строка ≤ 100 ед. от места сервера (игра берёт float npc_list).
#   Расхождение — «!!» с пометкой «вне ведомости», если NPC в ведомости нет (ведомость устарела: перестроить генератором).
NPC_SPAWN = os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "data", "npc_server_spawn.json")


def verify_server_places(npc_rows):
    if os.environ.get("ASDA_NPCLIST_FIXES", "1") == "0":
        log("места NPC: ведомость отключена — сверка с сервером пропущена")
        return 0
    if not os.path.isfile(NPC_SPAWN):
        log("!! слепок серверного спавна %s не найден — места NPC не сверены" % NPC_SPAWN)
        return 1
    import json
    spawn = {int(k): [tuple(p) for p in v] for k, v in json.load(open(NPC_SPAWN, encoding="utf-8"))["spawn"].items()}
    fixed = {f["npc"] for f in json.load(open(NPC_FIXES, encoding="utf-8"))["rows"]} if os.path.isfile(NPC_FIXES) else set()
    by = {}
    for r in npc_rows:
        by.setdefault(r["NpcId"], []).append(r)
    bad = 0
    for nid, rows in sorted(by.items()):
        places = spawn.get(nid)
        if not places:
            continue                                   # сервер NPC не ставит — сверять не с чем
        if rows[0]["NpcType"] in (1, 3, 5):
            ok = all(any(p[0] == r["MapId"] and abs(r["WorldX"] - (p[1] * 100 + 50)) <= 100
                         and abs(r["WorldY"] - (p[2] * 100 + 50)) <= 100 for p in places) for r in rows)
        else:
            ok = sorted((r["MapId"], int(r["WorldX"] // 100), int(r["WorldY"] // 100)) for r in rows) == sorted(places)
        if not ok:
            bad += 1
            log("!! NPC %d%s: строки %s, сервер %s" % (nid, "" if nid in fixed else " (вне ведомости)",
                [(r["MapId"], r["WorldX"], r["WorldY"]) for r in rows], places))
    log("места NPC против сервера (%d NPC со спавном): расхождений %d" % (len(spawn), bad))
    return bad


# ── Создание/заливка DataTable (только под UE) ──────────────────────────────────────────────────
def fill_dt(name, row_struct_path, csv_text):
    # * DEDUP D10: sam shag "RowStruct -> DataTable -> zalivka -> sohranenie" na port ODIN
    #   (`asda_dt_common.fill_dt`). Zdes ostayotsya tolko svoyo: paket, logger, podskazka pro
    #   zagolovok C++ i priznak uspeha.
    import unreal
    from asda_dt_common import fill_dt as _fill
    dt = _fill(unreal, PKG, name, row_struct_path, csv_text, log,
               fmt="csv", missing_hint="Public/Quest/AsdaQuestTypes.h")
    return dt is not None


def main(csv_only=False):
    os.makedirs(OUT_DIR, exist_ok=True)
    files, srows = read_scripts()
    nrows = read_npcs()

    s_csv, n_csv = script_csv(srows), npc_csv(nrows)
    with open(CSV_SCRIPT, "w", encoding="utf-8", newline="") as f:
        f.write(s_csv)
    with open(CSV_NPC, "w", encoding="utf-8", newline="") as f:
        f.write(n_csv)

    marks = {}
    for r in srows:
        if r["Mark"] in (1, 2, 3):
            marks[r["Mark"]] = marks.get(r["Mark"], 0) + 1
    log("NSR-файлов %d → полезных записей %d (с меткой: жёлтый!=%d красный!=%d ?=%d)" % (
        len(files), len(srows), marks.get(1, 0), marks.get(2, 0), marks.get(3, 0)))
    log("npc_list: %d записей, из них квестовых (тип 0/4/5): %d" % (
        len(nrows), sum(1 for r in nrows if r["NpcType"] in (0, 4, 5))))

    if verify(srows, nrows) == 0:
        log("СВЕРКА: расхождений 0")

    if csv_only:
        return
    try:
        import unreal  # noqa: F401
    except Exception:
        log("UE недоступен — записаны только CSV (%s, %s)" % (CSV_SCRIPT, CSV_NPC))
        return
    # ★ 29.10.2026: точечный переимпорт — ASDA_NPCQUEST_ONLY=DT_NpcQuestScript[,DT_NpcQuestNpc]; пусто — обе таблицы.
    only = {x.strip() for x in os.environ.get("ASDA_NPCQUEST_ONLY", "").split(",") if x.strip()}
    if not only or DT_SCRIPT_NAME in only:
        fill_dt(DT_SCRIPT_NAME, RS_SCRIPT, s_csv)
    if not only or DT_NPC_NAME in only:
        fill_dt(DT_NPC_NAME, RS_NPC, n_csv)
    log("ЗАВЕРШЕНО: импорт %s" % (", ".join(sorted(only)) if only else "обеих таблиц"))   # маркер для run_headless_safe


if __name__ == "__main__":
    main(csv_only=("--csv-only" in sys.argv))
