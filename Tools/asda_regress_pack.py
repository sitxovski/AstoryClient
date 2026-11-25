# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_regress_pack.py — РЕГРЕССИОННЫЙ НАБОР САМОТЕСТОВ КЛИЕНТА В СОБРАННОЙ ИГРЕ: прогон по ведомости и сверка с эталоном.
#   (10.11.2026, рефакторинг архитектуры C++, задача R0-a; план — Docs/Port/REFACTOR_PLAN.md.)
#
# ЗАЧЕМ. Рефакторинг обязан не менять поведение. Доказательство — итоги ВСЕХ самотестов до и после. Инструмент гоняет
#   ведомость самотестов (Tools/data/asda_regress_pack.tsv) в СОБРАННОЙ игре (Build/Dev/<штамп>/Windows, exe без EAC,
#   -AsdaNoNet -RenderOffscreen — сети и окна нет), пачками через `asda.SelfTest.Batch file=…` (Source/…/AsdaSelfTestBatch.cpp;
#   `-ExecCmds` режет аргументы по запятым, поэтому список команд — файлом), и пишет по каждому самотесту вердикт,
#   числа ok/fail, длительность, код выхода игры. `--compare <эталон>` печатает расхождения и возвращает код ≠ 0.
#
# ЗАПУСК (обычный python; редактор и исходники не нужны):
#   python Tools/asda_regress_pack.py --out <папка> [--build <папка Windows сборки>] [--lang ru|en|both] [--filter <regex>]
#          [--compare <папка эталона>] [--recheck N] [--jobs 2] [--chunk 25] [--manifest <tsv>] [--keep-saved]
#   python Tools/asda_regress_pack.py --scan <папка Source> [--dump <журнал DumpConsoleCommands>]   — перечень команд самотестов
#          в исходниках против ведомости: чего нет в ведомости, что в ведомости лишнее (ничего не запускает).
#   --build     папка, где лежит AsdaReborn/Binaries/Win64/AsdaReborn.exe; по умолчанию game_root из Build/Dev/latest.json
#   --lang      both (по умолчанию): RU — все строки ведомости, EN — строки, у которых в столбце «языки» есть en
#   --filter    regex по id строки ведомости (частичный прогон)
#   --resume [папка] продолжить прерванный прогон: итоги читаются из журналов игр <out>/<язык>/games, готовые самотесты не
#     перезапускаются; пока прогон идёт, готовое лежит в <out>/summary.partial.tsv (после каждой пачки), в конце — summary.tsv;
#   --recheck N повторить (в свежей игре) до N раз самотесты с вердиктом не ok, а при --compare — разошедшиеся с эталоном;
#               разный итог на повторах = «нестабильный» (стабилен=0), в сверке нестабильный не считается расхождением,
#               если хоть одна попытка совпала с эталоном. Больше 3 не задавать (правило проекта).
#   --jobs      игр одновременно (по умолчанию 2: игра занимает до 8 ГБ); новая игра ждёт, пока свободной памяти < --min-free ГБ
#   --chunk     самотестов на одну игру (по умолчанию 25) — падение/зависание портит не больше одной пачки
#
# ВЕДОМОСТЬ (TSV, UTF-8, `#` — комментарий; столбцы через TAB):
#   id  группа  команда  потолок_с  языки  вкл  примечание  файл
#   id        — уникальное имя строки (имя команды; вариант с аргументами — `имя#метка`)
#   группа    — карта запуска: world (alpen со своим персонажем) | login | map:<имя> (/Game/Map/<имя>/<имя>, режим мира) |
#               путь уровня целиком; суффикс `!` (world!) — самотест идёт ОДИН в своей игре (портит состояние соседям)
#   команда   — консольная команда с аргументами БЕЗ `quit` (его добавляет пакет); `{out}` — папка самотеста для кадров
#   потолок_с — потолок времени самотеста (пусто — --timeout)
#   языки     — ru | ru,en (en — самотест сверяет текст/вёрстку, гонять и на английском)
#   вкл       — 1 гонять; 0 не гонять (причина — в примечании; строка остаётся в ведомости как учёт «не вошло»)
#
# РЕЗУЛЬТАТ в --out: summary.tsv (строка на самотест и язык), <язык>/logs/<id>.log (отрезок журнала самотеста),
#   <язык>/games/<пачка>_<запуск>.log (полный журнал игры), run.json (сборка, время, параметры), при --compare — compare.txt.
#   Вердикты: ok | fail | timeout (потолок; пакет встаёт, остальные идут в новой игре) | crash (игра умерла на самотесте) |
#   notrun (игра не смогла стартовать). Подпись самотеста для сверки: вердикт + ok + fail (если чисел в строке ИТОГ нет —
#   сама строка ИТОГ с маской дробных чисел) + хэш набора строк FAIL (числа — маской).
#
# ОЧЕРЕДЬ UE. Каждая запускаемая игра идёт под заявкой «shared» общей очереди (AstoryClient/DEPLOY/_tools/ue_queue.py), заявка —
#   на ОДНУ игру (пачку), а не на весь прогон: между пачками сборка редактора (заявка «exclusive») получает свою очередь, а
#   игра не стартует посреди сборки и сверх потолка игр машины. Причина (10.11.2026): две игры вне очереди вместе со сборкой
#   в 16 компиляторов упёрлись в порог памяти UBA — 402 файла собирались 28,8 мин вместо ≈8. Сверх очереди: не больше
#   --jobs игр и старт новой только при свободной памяти ≥ --min-free ГБ (ue_queue.free_ram_gb).
#
# КОД ВОЗВРАТА: 0 — прогон дошёл до конца (без --compare красные самотесты кода не меняют: красный эталон — тоже эталон),
#   с --compare — расхождений нет; 1 — есть расхождения с эталоном; 2 — ошибка аргументов/ведомости; 3 — часть самотестов
#   не запускалась (notrun).
#
# ★ Модуль БЕЗ побочных эффектов при импорте (закон tools-no-import-side-effects): работа — только в main() под __main__.
import argparse
import hashlib
import io
import json
import os
import queue
import re
import shutil
import subprocess
import sys
import threading
import time

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))                       # папка Tools
PORT_DIR = os.path.dirname(os.path.dirname(TOOLS_DIR))                       # <ROOT> (Tools → проект → ROOT)
MANIFEST = os.path.join(TOOLS_DIR, "data", "asda_regress_pack.tsv")          # ведомость набора по умолчанию
LATEST = os.path.join(PORT_DIR, "Build", "Dev", "latest.json")               # указатель на последнюю Dev-сборку
EXE_REL = os.path.join("AsdaReborn", "Binaries", "Win64", "AsdaReborn.exe")  # exe игры внутри папки сборки (без EAC)
WORLD_OPT = "?game=/Script/AsdaReborn.AsdaWorldGameMode"                     # режим мира для карты (свой персонаж офлайн)
MAPS = {"login": "/Game/Map/Login", "world": "/Game/Map/alpen/alpen" + WORLD_OPT}   # короткие имена групп → уровень
ENV_CRASH_RE = re.compile(r"D3D12Util\.cpp|D3D12Residency")   # падение исчерпания D3D12 — известная нестабильность (см. compare)
COLS = ("id", "группа", "команда", "потолок_с", "языки", "вкл", "примечание", "файл")   # столбцы ведомости
SUM_COLS = ("id", "язык", "вердикт", "ok", "fail", "с", "код_выхода", "попыток", "стабилен", "fail_строк", "fail_хэш",
            "подпись", "повторы", "утечки", "группа", "пачка", "ИТОГ")                 # столбцы summary.tsv
TS_RE = re.compile(r"^\[\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3}\]\[\s*\d+\]")   # метка времени и кадра строки журнала
NUM_RE = re.compile(r"\d+(\.\d+)?")                                          # любые числа — маска строк FAIL
FLOAT_RE = re.compile(r"\d+\.\d+")                                           # дробные числа (тайминги) — маска строки ИТОГ
BEGIN_RE = re.compile(r"SELFTEST_BATCH НАЧАЛО (\d+)/(\d+) (\S+):")           # начало самотеста в журнале игры
END_RE = re.compile(r"SELFTEST_BATCH КОНЕЦ (\d+)/(\d+) (\S+): (\w+) \(([-\d.]+) с\) итог=«(.*)» fail-строк=(\d+)(.*)$")   # конец самотеста
TOTAL_RE = re.compile(r"SELFTEST_BATCH ИТОГ: ")                              # общий итог пакета (пакет дошёл до конца)
OK_RE = re.compile(r"(?:\bok|\bOK|PASS|пройдено|успешно)\s*[=:]?\s*(\d+)")   # число успешных проверок в строке ИТОГ
FAIL_RE = re.compile(r"(?:fail=|FAIL\s*[=:]?\s*|провалов\s*[=:]?\s*|провалено\s*[=:]?\s*)(\d+)")   # число провалов в строке ИТОГ
REG_RE = re.compile(r"\b(FAutoConsoleCommand\w*|RegisterConsoleCommand)\b")  # регистрация консольной команды в исходниках
LIT_RE = re.compile(r'TEXT\(\s*"((?:[^"\\]|\\.)*)"\s*\)')                    # строковый литерал TEXT("…")
START_CEIL_S = 300.0                                                         # потолок загрузки игры до первого самотеста, с
PRINT_LOCK = threading.Lock()                                                # замок печати (игры идут в потоках)


def say(msg):
    """Печать строки с временем (из любого потока)."""
    with PRINT_LOCK:
        print("[%s] %s" % (time.strftime("%H:%M:%S"), msg), flush=True)


def read_text(path):
    """Текст файла (пусто, если файла нет)."""
    try:
        return io.open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        return ""


def queue_module():
    """Модуль общей очереди UE (AstoryClient/DEPLOY/_tools/ue_queue.py); путь добавляется в sys.path при первом обращении."""
    d = os.path.join(PORT_DIR, "AstoryClient", "DEPLOY", "_tools")
    if d not in sys.path:
        sys.path.insert(0, d)
    import ue_queue
    return ue_queue


def free_ram_gb():
    """Свободная физическая память, ГБ (общая функция очереди UE; -1 — узнать не удалось)."""
    try:
        return queue_module().free_ram_gb()
    except Exception:
        return -1.0


def load_manifest(path):
    """Строки ведомости → список словарей по COLS (с проверкой уникальности id). Комментарии и пустые строки пропускаются."""
    rows, seen = [], set()
    for n, ln in enumerate(io.open(path, encoding="utf-8-sig"), 1):
        ln = ln.rstrip("\r\n")
        if not ln.strip() or ln.startswith("#"):
            continue
        f = ln.split("\t")
        f += [""] * (len(COLS) - len(f))
        r = dict(zip(COLS, (x.strip() for x in f[:len(COLS)])))
        if r["id"] == "id":
            continue   # строка заголовка
        if not r["id"] or not r["команда"]:
            raise SystemExit("ведомость %s, строка %d: пустой id или команда" % (path, n))
        if r["id"] in seen:
            raise SystemExit("ведомость %s, строка %d: повтор id %s" % (path, n, r["id"]))
        if not re.match(r"^[A-Za-z0-9_.#\-]+$", r["id"]):
            raise SystemExit("ведомость %s, строка %d: id %r — только [A-Za-z0-9_.#-]" % (path, n, r["id"]))
        seen.add(r["id"])
        rows.append(r)
    return rows


def map_url(group):
    """Группа ведомости → уровень для командной строки игры."""
    g = group.rstrip("!")
    if g in MAPS:
        return MAPS[g]
    if g.startswith("map:"):
        name = g[4:]
        return "/Game/Map/%s/%s%s" % (name, name, WORLD_OPT)
    return g


def map_id(group):
    """Номер карты для группы `map:<папка>` по реестру карт (Tools/data/asda_map_registry.json); -1 — не карта или нет в реестре."""
    g = group.rstrip("!")
    if not g.startswith("map:"):
        return -1
    try:
        reg = json.load(io.open(os.path.join(TOOLS_DIR, "data", "asda_map_registry.json"), encoding="utf-8"))
        for m in reg.get("maps", []):
            if str(m.get("folder", "")).lower() == g[4:].lower():
                return int(m.get("id", -1))
    except (OSError, ValueError):
        pass
    return -1


def make_chunks(rows, lang, chunk_size):
    """Строки одного языка → пачки [(имя пачки, группа, [строки])]: по группе (карте), не больше chunk_size; группа с `!` —
    по одному самотесту на игру. Порядок — как в ведомости (состав пачек полного прогона повторяем)."""
    by, order = {}, []
    for r in rows:
        if r["группа"] not in by:
            by[r["группа"]] = []
            order.append(r["группа"])
        by[r["группа"]].append(r)
    chunks = []
    for g in order:
        size = 1 if g.endswith("!") else chunk_size
        lst = by[g]
        for i in range(0, len(lst), size):
            # «!» → «_solo»: имена одиночных пачек не совпадают с обычными той же карты (иначе общий журнал и список)
            tag = re.sub(r"[^A-Za-z0-9]+", "_", g.replace("!", "_solo")).strip("_") or "x"
            chunks.append(("%s_%s_%02d" % (lang, tag, i // size), g, lst[i:i + size]))
    return chunks


def parse_counts(itog):
    """(ok, fail) из строки ИТОГ; '' — числа нет. При нескольких вхождениях берётся последнее ok и наибольший fail."""
    oks = OK_RE.findall(itog)
    fails = FAIL_RE.findall(itog)
    return (oks[-1] if oks else "", str(max(int(x) for x in fails)) if fails else "")


def fail_lines(seg):
    """Множество строк FAIL отрезка журнала самотеста (без метки времени, числа — маской; строки пакета не считаются)."""
    out = set()
    for ln in seg.splitlines():
        ln = TS_RE.sub("", ln).strip()
        if "SELFTEST_BATCH" in ln or "CVAR_RESTORE" in ln or "ИТОГ" in ln:
            continue
        if "FAIL" in ln:
            out.add(NUM_RE.sub("#", ln))
    return out


def signature(res):
    """Подпись результата самотеста для сверки: вердикт|ok|fail (или строка ИТОГ с маской дробных)|хэш строк FAIL."""
    core = "%s/%s" % (res["ok"], res["fail"]) if (res["ok"] != "" or res["fail"] != "") else FLOAT_RE.sub("#", TS_RE.sub("", res["ИТОГ"]).strip())
    return norm_sig("%s|%s|%s" % (res["вердикт"], core, res["fail_хэш"]))


def norm_sig(sig):
    """Подпись без хэша строк FAIL у зелёного самотеста с fail=0: такие строки там — попутный шум журнала (отказ отправки
    офлайн «-> FAIL» и т.п.), от прогона к прогону их набор плавает (эталон 11.11.2026: Craft.SelfTestMake [en]).
    Применяется и к подписям, сохранённым в summary.tsv эталона до этой правки."""
    parts = sig.split("|")
    if len(parts) == 3 and parts[0] == "ok" and re.fullmatch(r"\d*/0?", parts[1]):
        return "ok|%s|" % parts[1]
    return sig


def parse_game_log(text, tests):
    """Журнал одного запуска игры → ({номер в списке (с 0): результат}, номер самотеста, на котором игра оборвалась, или None,
    дошёл ли пакет до общего итога). Результат: вердикт/ok/fail/с/ИТОГ/fail_строк/fail_хэш/утечки/отрезок."""
    res, cur, buf, open_idx = {}, None, [], None
    # ★ 11.11.2026: строка КОНЕЦ бывает многострочной — у самотеста без строки ИТОГ пакет берёт в «итог» многострочный отчёт
    #   (Upgrade.SelfTest, HighRefine.SelfTest, SelfTest.Toggles). Без склейки конец не находился и самотест считался
    #   оборванным («crash, код 0») при том, что пакет записал ему ok/timeout. Строки записи склеиваются через « ⏎ ».
    lines, pend = [], None
    for ln in text.splitlines():
        if pend is not None:
            pend += " ⏎ " + ln.strip()
            if "» fail-строк=" in ln:
                lines.append(pend)
                pend = None
            continue
        if "SELFTEST_BATCH КОНЕЦ" in ln and "» fail-строк=" not in ln:
            pend = ln
            continue
        lines.append(ln)
    if pend is not None:
        lines.append(pend)
    for ln in lines:
        b = BEGIN_RE.search(ln)
        if b:
            cur, buf, open_idx = int(b.group(1)) - 1, [ln], int(b.group(1)) - 1
            continue
        if cur is None:
            continue
        buf.append(ln)
        e = END_RE.search(ln)
        if e and int(e.group(1)) - 1 == cur:
            seg = "\n".join(buf) + "\n"
            fl = fail_lines(seg)
            ok, fail = parse_counts(e.group(6))
            res[cur] = {"вердикт": e.group(4), "ok": ok, "fail": fail, "с": e.group(5), "ИТОГ": TS_RE.sub("", e.group(6)).strip()[:300],
                        "fail_строк": str(len(fl)), "fail_хэш": hashlib.md5("\n".join(sorted(fl)).encode("utf-8")).hexdigest()[:8] if fl else "",
                        "утечки": e.group(8).strip(), "отрезок": seg}
            cur, buf, open_idx = None, [], None
    if open_idx is not None:   # игра оборвалась посреди самотеста — отрезок сохраняется как есть
        seg = "\n".join(buf) + "\n"
        fl = fail_lines(seg)
        res[open_idx] = {"вердикт": "crash", "ok": "", "fail": "", "с": "", "ИТОГ": "", "fail_строк": str(len(fl)), "fail_хэш": "",
                         "утечки": "", "отрезок": seg}
    return res, open_idx, bool(TOTAL_RE.search(text))


def crash_reason(text):
    """Причина падения из хвоста журнала игры (строка Fatal/Assertion/исключения) или ''."""
    for ln in reversed(text.splitlines()[-400:]):
        if re.search(r"Fatal error|Assertion failed|Unhandled Exception|EXCEPTION_ACCESS_VIOLATION|Ensure condition failed", ln):
            return TS_RE.sub("", ln).strip()[:240]
    return ""


def kill_tree(pid):
    """Снять процесс с дочерними по PID."""
    subprocess.call(["taskkill", "/PID", str(pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def wait_memory(min_free, who):
    """Перед стартом игры: ждать, пока свободной памяти меньше min_free ГБ (не дольше 15 мин — дальше старт с предупреждением)."""
    t0, last = time.time(), 0.0
    while True:
        fr = free_ram_gb()
        if fr < 0 or fr >= min_free:
            return
        if time.time() - t0 > 900:
            say("!! %s: свободной памяти %.1f ГБ < %.0f ГБ уже 15 мин — стартую всё равно" % (who, fr, min_free))
            return
        if time.time() - last >= 60:
            say("%s: жду память — свободно %.1f ГБ < %.0f ГБ" % (who, fr, min_free))
            last = time.time()
        time.sleep(10)


def run_game(exe, group, tests, lang, work_dir, tag, args):
    """Один запуск игры со списком самотестов. → (текст журнала, код выхода или None если снят, причина снятия или '').
    tests — [(командная строка, потолок с)]. Журнал — work_dir/<tag>.log, список — work_dir/<tag>.list.txt."""
    os.makedirs(work_dir, exist_ok=True)
    list_file = os.path.join(work_dir, tag + ".list.txt")
    log = os.path.join(work_dir, tag + ".log")
    if re.search(r"[\s,]", list_file):
        raise SystemExit("путь списка %r: без пробелов и запятых (аргументы консоли делятся пробелами, -ExecCmds — запятыми)" % list_file)
    with io.open(list_file, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("# asda_regress_pack %s\n" % tag + "".join("%s @timeout=%g\n" % (c, t) for c, t in tests))
    if os.path.exists(log):
        os.remove(log)
    suffix = "regress_%d_%s" % (os.getpid(), re.sub(r"[^A-Za-z0-9_]", "_", tag))
    saved = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(exe))), "Saved_" + suffix)   # <сборка>/AsdaReborn/Saved_<суффикс>
    keys_ini = os.path.join(saved, "AsdaSelfTest", "Shortcutkey.ini")   # песочница раскладки клавиш (как asda_selftest_batch.py)
    batch = "asda.SelfTest.Batch file=%s quit timeout=%g settle=%g start=%g" % (list_file.replace("\\", "/"), args.timeout, args.settle, args.start)
    # ★ 11.11.2026 (D4): на карте не-Альпене игре до пакета сообщается номер текущей карты (`Asda.Load.CurrentMap <id>`), как делали
    #   прежние прогоны (`--pre "Asda.Load.CurrentMap 0"` у QuestMarkMelve): офлайн-персонаж самотестов по умолчанию считает
    #   себя на карте 3, и метки заданий/NPC другой карты не строятся (эталон: QuestMarkMelve 0/8 и на своей карте — 2/6).
    mid = map_id(group)
    pre_map = ("Asda.Load.CurrentMap %d," % mid) if mid >= 0 else ""
    cmd = [exe, map_url(group), "-AsdaNoNet", "-RenderOffscreen", "-unattended", "-nopause", "-nosplash", "-nosound",
           "-AsdaLang=" + lang, "-saveddirsuffix=" + suffix, "-abslog=" + log, "-AsdaShortcutkeyIni=" + keys_ini,
           "-ExecCmds=r.SetRes 1024x768w,%s%s" % (pre_map, batch)]
    shutil.rmtree(saved, ignore_errors=True)
    # заявка общей очереди на эту игру: ждёт конца идущей сборки и места под потолком игр; снимается в finally
    slot = queue_module().Slot("shared", "regress %s (%d самотестов)" % (tag, len(tests)), timeout_min=args.queue_wait, log=say)
    slot.__enter__()
    try:
        wait_memory(args.min_free, tag)
        si = subprocess.STARTUPINFO()
        si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        si.wShowWindow = 0
        # ★ 11.11.2026 (D4): ASDA_ROOT — корень раскладки. Самотесты с ведомостями-оракулами из дерева (Tools/data, RawAssets,
        #   tests, Releases/keys, файлы доноров) в собранной игре ищут их через эту переменную (Test/AsdaSelfTestTree.h,
        #   asda.Fish.ZoneSelfTest); без неё они красные не из-за игры.
        env = dict(os.environ)
        env.setdefault("ASDA_ROOT", os.path.dirname(PORT_DIR))
        p = subprocess.Popen(cmd, startupinfo=si, creationflags=subprocess.CREATE_NO_WINDOW, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
        slot.add_pid(p.pid)   # игра под очередью: сборка редактора не сочтёт её «чужим» процессом UE
    except BaseException:
        slot.__exit__(None, None, None)
        raise
    t0, why, cur, cur_t0, n_end = time.time(), "", -1, time.time(), 0
    try:
        while True:
            time.sleep(2)
            txt = read_text(log)
            begins = BEGIN_RE.findall(txt)
            if begins and int(begins[-1][0]) - 1 != cur:
                cur, cur_t0 = int(begins[-1][0]) - 1, time.time()
            n_end = txt.count("SELFTEST_BATCH КОНЕЦ")
            if TOTAL_RE.search(txt):
                deadline = time.time() + 30   # итог есть — дать игре выйти самой
                while p.poll() is None and time.time() < deadline:
                    time.sleep(1)
                if p.poll() is None:
                    why = "не вышла после итога"
                break
            if p.poll() is not None:
                break
            if cur < 0 and time.time() - t0 > START_CEIL_S:
                why = "не дошла до первого самотеста за %.0f с" % START_CEIL_S
                break
            if cur >= 0 and n_end <= cur and time.time() - cur_t0 > tests[min(cur, len(tests) - 1)][1] + 120:
                why = "зависла на самотесте %d (потолок пакета не сработал)" % (cur + 1)
                break
    finally:
        rc = p.poll()
        if rc is None:
            kill_tree(p.pid)
            time.sleep(2)
        slot.__exit__(None, None, None)
        if not args.keep_saved:
            shutil.rmtree(saved, ignore_errors=True)
    return read_text(log), rc, why


def run_chunk(exe, name, group, rows, lang, out_dir, args):
    """Пачка самотестов одной карты: запуски игры до исчерпания списка (после потолка/падения остаток идёт в новой игре).
    → {id: результат} (в результате добавлены код_выхода, пачка, группа)."""
    games = os.path.join(out_dir, lang, "games")
    results, rest, launch, start_fail = {}, list(rows), 0, 0
    while rest:
        launch += 1
        tests = []
        for r in rest:
            shots = os.path.join(out_dir, lang, "shots", r["id"]).replace("\\", "/")
            tests.append((expand_cmd(r, lang, out_dir), float(r["потолок_с"] or args.timeout)))
        t0 = time.time()
        text, rc, why = run_game(exe, group, tests, lang, games, "%s_%d" % (name, launch), args)
        res, open_idx, done = parse_game_log(text, tests)
        got = 0
        for i, r in enumerate(rest):
            if i not in res:
                continue
            x = res[i]
            if i == open_idx:   # игра оборвалась на этом самотесте: падение или зависание
                x["вердикт"] = "timeout" if why else "crash"
                x["ИТОГ"] = why or crash_reason(text) or "игра завершилась посреди самотеста (код %s)" % rc
            x.update({"код_выхода": "" if rc is None else str(rc), "пачка": "%s_%d" % (name, launch), "группа": group})
            results[r["id"]] = x
            got += 1
        say("%s запуск %d: самотестов %d из %d, %.0f с, код %s%s" % (name, launch, got, len(rest), time.time() - t0, rc,
                                                                    ("; " + why) if why else ("" if done else "; пакет не дошёл до итога")))
        if got == 0:   # игра не дошла до первого самотеста
            start_fail += 1
            if start_fail >= 2:
                for r in rest:
                    results[r["id"]] = {"вердикт": "notrun", "ok": "", "fail": "", "с": "", "ИТОГ": why or crash_reason(text) or "игра не дошла до самотестов (код %s)" % rc,
                                        "fail_строк": "0", "fail_хэш": "", "утечки": "", "отрезок": "", "код_выхода": "" if rc is None else str(rc),
                                        "пачка": "%s_%d" % (name, launch), "группа": group}
                break
            continue
        start_fail = 0
        rest = [r for r in rest if r["id"] not in results]
    return results


def expand_cmd(row, lang, out_dir):
    """Команда строки ведомости с подставленной папкой снимков {out} — ровно то, что уходит в список игры."""
    return row["команда"].replace("{out}", os.path.join(out_dir, lang, "shots", row["id"]).replace("\\", "/"))


def load_done(out_dir, rows, langs):
    """Продолжение (--resume): готовые итоги из журналов игр прошлых запусков <out>/<язык>/games/*.log (+ *.list.txt).
    → {(id, язык): результат}. Берутся самотесты со строкой КОНЕЦ; оборванный посреди — только если журнал закрыт
    (игра умерла сама: падение или собственный выход), иначе он пойдёт заново. Повторы (re<N>_*) не считаются."""
    done = {}
    for lang in langs:
        games = os.path.join(out_dir, lang, "games")
        by_cmd = {expand_cmd(r, lang, out_dir): r for r in rows}
        try:
            lists = sorted((f for f in os.listdir(games) if f.endswith(".list.txt") and not re.match(r"re\d+_", f)),
                           key=lambda f: os.path.getmtime(os.path.join(games, f)))
        except OSError:
            continue
        for lf in lists:
            tag = lf[:-len(".list.txt")]
            tests = []
            for ln in read_text(os.path.join(games, lf)).splitlines():
                m = re.match(r"(.*) @timeout=([\d.]+)\s*$", ln)
                if m and not ln.startswith("#"):
                    tests.append((m.group(1), float(m.group(2))))
            text = read_text(os.path.join(games, tag + ".log"))
            if not text:
                continue
            res, open_idx, _total = parse_game_log(text, tests)
            closed = "Log file closed" in text[-4000:] or bool(crash_reason(text))
            for i, x in res.items():
                if i >= len(tests) or tests[i][0] not in by_cmd:
                    continue
                if i == open_idx:
                    if not closed:
                        continue
                    x["ИТОГ"] = crash_reason(text) or "игра завершилась посреди самотеста"
                r = by_cmd[tests[i][0]]
                x.update({"код_выхода": "", "пачка": tag, "группа": r["группа"]})
                done[(r["id"], lang)] = x
    return done


def run_all(exe, plan, out_dir, args, on_chunk=None):
    """Прогон плана [(язык, имя пачки, группа, строки)] в --jobs потоков. → {(id, язык): результат}.
    on_chunk(итоги на сейчас) зовётся после каждой пачки (частичная сводка)."""
    q, results, lock = queue.Queue(), {}, threading.Lock()
    for item in plan:
        q.put(item)

    def worker():
        """Поток одной игры: берёт пачки, пока есть."""
        while True:
            try:
                lang, name, group, rows = q.get_nowait()
            except queue.Empty:
                return
            try:
                res = run_chunk(exe, name, group, rows, lang, out_dir, args)
            except Exception as e:   # поток не должен молча умереть — пачка помечается notrun
                say("!! %s: %s" % (name, e))
                res = {r["id"]: {"вердикт": "notrun", "ok": "", "fail": "", "с": "", "ИТОГ": "ошибка инструмента: %s" % e, "fail_строк": "0",
                                 "fail_хэш": "", "утечки": "", "отрезок": "", "код_выхода": "", "пачка": name, "группа": group} for r in rows}
            with lock:
                for k, v in res.items():
                    results[(k, lang)] = v
                if on_chunk:
                    try:
                        on_chunk(dict(results))
                    except Exception as e:
                        say("!! частичная сводка: %s" % e)

    threads = [threading.Thread(target=worker, daemon=True) for _ in range(max(1, args.jobs))]
    for t in threads:
        t.start()
        time.sleep(3)   # игры стартуют не в одну секунду
    for t in threads:
        t.join()
    return results


def write_logs(results, out_dir):
    """Отрезки журналов самотестов → <out>/<язык>/logs/<id>.log (повторные попытки — <id>.try<N>.log пишет вызывающий)."""
    for (tid, lang), x in results.items():
        d = os.path.join(out_dir, lang, "logs")
        os.makedirs(d, exist_ok=True)
        if x.get("отрезок"):
            with io.open(os.path.join(d, tid + ".log"), "w", encoding="utf-8", newline="\n") as fh:
                fh.write(x["отрезок"])


def write_summary(results, order, path):
    """summary.tsv: строка на (самотест, язык) в порядке ведомости."""
    with io.open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\t".join(SUM_COLS) + "\n")
        for key in order:
            x = results.get(key)
            if not x:
                continue
            row = dict(x, id=key[0], язык=key[1], подпись=x.get("подпись") or signature(x), повторы=" ;; ".join(x.get("повторы", [])))
            fh.write("\t".join(str(row.get(c, "")).replace("\t", " ").replace("\n", " ") for c in SUM_COLS) + "\n")


def load_summary(path):
    """summary.tsv → {(id, язык): строка-словарь}."""
    out = {}
    lines = read_text(path).splitlines()
    if not lines:
        return out
    head = lines[0].split("\t")
    for ln in lines[1:]:
        f = ln.split("\t")
        f += [""] * (len(head) - len(f))
        r = dict(zip(head, f))
        out[(r["id"], r["язык"])] = r
    return out


def recheck(exe, results, keys, rows_by_id, out_dir, args, tag):
    """Повтор самотестов keys в свежих играх до args.recheck раз. → {ключ: [подписи повторов]}; повторы пишутся в логи .tryN."""
    tries = {k: [] for k in keys}
    for n in range(1, args.recheck + 1):
        plan = []
        for lang in sorted({k[1] for k in keys}):
            rows = [rows_by_id[k[0]] for k in keys if k[1] == lang and k[0] in rows_by_id]
            plan += [(lang, "%s%d_%s" % (tag, n, name), g, rs) for name, g, rs in make_chunks(rows, lang, args.chunk)]
        say("повтор %d/%d: самотестов %d, пачек %d" % (n, args.recheck, len(keys), len(plan)))
        again = run_all(exe, plan, out_dir, args)
        for k in keys:
            x = again.get(k)
            if not x:
                continue
            tries[k].append(signature(x))
            d = os.path.join(out_dir, k[1], "logs")
            os.makedirs(d, exist_ok=True)
            if x.get("отрезок"):
                with io.open(os.path.join(d, "%s.try%d.log" % (k[0], n + 1)), "w", encoding="utf-8", newline="\n") as fh:
                    fh.write(x["отрезок"])
    return tries


def compare(results, base, order):
    """Сверка прогона с эталоном (только по ключам прогона; пропавшие из прогона ключи эталона считаются, если прогон полный —
    это решает вызывающий через order). → (расхождения [строки], нестабильные [строки])."""
    diffs, flaky = [], []
    for key in order:
        x, b = results.get(key), base.get(key)
        name = "%s [%s]" % key
        if x is None and b is None:
            continue
        if b is None:
            diffs.append("ПОЯВИЛСЯ   %s: в эталоне нет; сейчас %s" % (name, signature(x)))
            continue
        if x is None:
            diffs.append("ПРОПАЛ     %s: в эталоне %s; сейчас не запускался" % (name, b.get("подпись", "")))
            continue
        sig = signature(x)
        bsig = norm_sig(b.get("подпись", ""))
        if sig == bsig:
            continue
        seen = [sig] + list(x.get("повторы", []))
        base_sigs = {bsig} | set(norm_sig(z) for z in filter(None, b.get("повторы", "").split(" ;; ")))   # все подписи, виденные в эталоне
        if b.get("стабилен", "1") == "0" or any(s in base_sigs for s in seen):
            flaky.append("НЕСТАБИЛЕН %s: эталон %s%s; сейчас %s" % (name, b.get("подпись", ""), " (в эталоне помечен нестабильным)" if b.get("стабилен", "1") == "0" else "",
                                                               " → ".join(seen)))
            continue
        # оба зелёные, fail тот же, разное только число ok: у части самотестов число проверок плавает от прогона к прогону
        # (эталон 10.11.2026: Soul.SelfTestWave1A 79↔80, UI.SelfTestBodyPress 144↔139, UI.SelfTestOpenOnTop 486↔509) — замечание
        if x["вердикт"] == "ok" and b.get("вердикт") == "ok" and x["fail"] == b.get("fail", "") and x["fail"] in ("", "0"):
            flaky.append("ЧИСЛО OK   %s: эталон ok=%s → сейчас ok=%s (оба зелёные; не расхождение)" % (name, b.get("ok", ""), x["ok"]))
            continue
        # падение исчерпания D3D12 (D3D12Util.cpp, «Too many residency sets», память процесса 10–16 ГБ) — известная нестабильность
        # сборки эталона 7.6.0: бьёт по разным самотестам (HighRefine, UpgradeScrollKinds, Craft.SelfTestW5/Recipe, Fish.WinSelfTest),
        # чаще на EN, в том числе в одиночной игре. С любой стороны сверки это замечание, а не расхождение.
        if (x["вердикт"] == "crash" and ENV_CRASH_RE.search(x["ИТОГ"])) or (b.get("вердикт") == "crash" and ENV_CRASH_RE.search(b.get("ИТОГ", ""))):
            flaky.append("ПАДЕНИЕ D3D12 %s: эталон %s (%s/%s) → сейчас %s (%s/%s) — известная нестабильность эталона, не расхождение; перепроверить повтором" % (
                name, b.get("вердикт", ""), b.get("ok", ""), b.get("fail", ""), x["вердикт"], x["ok"], x["fail"]))
            continue
        kind = "ПАДЕНИЕ   " if x["вердикт"] in ("crash", "notrun") else ("ПОТОЛОК   " if x["вердикт"] == "timeout" else "ИЗМЕНИЛСЯ ")
        diffs.append("%s %s: эталон %s (%s/%s) → сейчас %s (%s/%s)%s; ИТОГ «%s»" % (
            kind, name, b.get("вердикт", ""), b.get("ok", ""), b.get("fail", ""), x["вердикт"], x["ok"], x["fail"],
            "" if b.get("fail_хэш", "") == x["fail_хэш"] else "; набор строк FAIL другой", x["ИТОГ"][:160]))
    return diffs, flaky


def scan_source(root):
    """Регистрации консольных команд в исходниках → {имя: (файл, строка, справка)}. Имя — первый литерал после регистрации
    либо константа `kXxx = TEXT("asda.…")` того же файла (так записаны команды части самотестов)."""
    out = {}
    for dp, _dn, fn in os.walk(root):
        for f in fn:
            if not f.endswith((".cpp", ".h")):
                continue
            p = os.path.join(dp, f)
            t = read_text(p)
            consts = dict(re.findall(r'\b(k\w+)\s*=\s*TEXT\(\s*"(asda\.[^"]+)"\s*\)', t))   # именованные константы-имена команд
            for m in REG_RE.finditer(t):
                tail = t[m.end():m.end() + 3000]
                head = tail[:400]
                lit = LIT_RE.search(head)
                cst = re.search(r"\(\s*(k\w+)\s*,", head)
                name, rest = None, ""
                if cst and cst.group(1) in consts and (not lit or cst.start() < lit.start()):
                    name, rest = consts[cst.group(1)], tail[cst.end():]
                elif lit:
                    name, rest = lit.group(1), tail[lit.end():]
                if not name or not name.startswith("asda."):
                    continue
                name = name.split()[0]
                d = re.search(r"FConsoleCommand\w*Delegate|\[[^\]]*\]\s*\(", rest)
                helpz = re.sub(r"\s+", " ", "".join(LIT_RE.findall(rest[:d.start()] if d else rest[:1200])))
                out.setdefault(name, (os.path.relpath(p, root).replace(os.sep, "/"), t.count("\n", 0, m.start()) + 1, helpz))
    return out


def cmd_scan(args):
    """Режим --scan: команды самотестов исходников (и сборки, если дан --dump) против ведомости. Ничего не запускает."""
    src = scan_source(args.scan)
    st = {n: v for n, v in src.items() if "selftest" in n.lower()}
    built = None
    if args.dump:   # журнал игры с выводом `DumpConsoleCommands asda`: что реально есть в сборке
        built = set(re.findall(r"\](asda\.[A-Za-z0-9_.]+)\s*$", read_text(args.dump), re.M))
    rows = load_manifest(args.manifest) if os.path.isfile(args.manifest) else []
    in_manifest = {r["команда"].split()[0] for r in rows}
    missing = sorted(n for n in st if n not in in_manifest and (built is None or n in built))
    not_built = sorted(n for n in st if built is not None and n not in built)
    extra = sorted(n for n in in_manifest if n not in src)
    print("команд asda.* в исходниках: %d; из них с SelfTest в имени: %d%s" % (len(src), len(st), "" if built is None else "; в сборке есть: %d" % sum(1 for n in st if n in built)))
    print("строк ведомости: %d (вкл %d), команд в ней: %d" % (len(rows), sum(1 for r in rows if r["вкл"] == "1"), len(in_manifest)))
    print("\nНЕТ В ВЕДОМОСТИ (%d):" % len(missing))
    for n in missing:
        print("  %s\t%s:%d\t%s" % (n, st[n][0], st[n][1], st[n][2][:200]))
    if built is not None:
        print("\nВ ИСХОДНИКАХ ЕСТЬ, В СБОРКЕ НЕТ (%d; редактор/гвард): %s" % (len(not_built), " ".join(not_built)))
    print("\nВ ВЕДОМОСТИ ЕСТЬ, В ИСХОДНИКАХ НЕТ (%d): %s" % (len(extra), " ".join(extra)))
    return 1 if (missing or extra) else 0


def main(argv=None):
    """Разбор аргументов → прогон набора → summary.tsv → (с --compare) сверка с эталоном."""
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
    ap = argparse.ArgumentParser(description="Регрессионный набор самотестов в собранной игре: прогон по ведомости и сверка с эталоном")
    ap.add_argument("--manifest", default=MANIFEST, help="ведомость набора (TSV)")
    ap.add_argument("--build", default="", help="папка сборки (в ней AsdaReborn/Binaries/Win64/AsdaReborn.exe); по умолчанию Build/Dev/latest.json")
    ap.add_argument("--out", default="", help="папка результатов (обязательна для прогона)")
    ap.add_argument("--lang", default="both", choices=("ru", "en", "both"), help="язык: ru | en | both (EN — только строки с en в «языки»)")
    ap.add_argument("--all-en", action="store_true", help="на EN гнать ВСЕ строки, а не только помеченные en")
    ap.add_argument("--filter", default="", help="regex по id (частичный прогон)")
    ap.add_argument("--compare", default="", help="папка эталона (summary.tsv) — сверка, код 1 при расхождении")
    ap.add_argument("--recheck", type=int, default=0, help="повторов для не-ok (при --compare — для разошедшихся), не больше 3")
    ap.add_argument("--jobs", type=int, default=4, help="игр одновременно (★ 11.11.2026: 4 — собранная игра занимает 1,5–5 ГБ; было 2)")
    ap.add_argument("--fast", action="store_true", help="★ 11.11.2026: с --compare не гнать строки, которые в эталоне красные, "
                    "падают или нестабильны — сигнала они не дают, а занимают ≈20 %% времени (потолки по 240–300 с)")
    ap.add_argument("--chunk", type=int, default=10, help="самотестов на одну игру (10 ≈ 5–8 мин: дольше сборка редактора не ждёт)")
    ap.add_argument("--resume", nargs="?", const="1", default="", help="продолжить прерванный прогон: готовые итоги из журналов игр в "
                    "--out (или в папке-значении ключа) не перезапускаются и идут в ту же сводку")
    ap.add_argument("--timeout", type=float, default=300.0, help="потолок на самотест по умолчанию, с")
    ap.add_argument("--start", type=float, default=5.0, help="пауза до первого самотеста, с")
    ap.add_argument("--settle", type=float, default=1.5, help="пауза после конца самотеста, с")
    ap.add_argument("--min-free", type=float, default=6.0, help="не стартовать игру, пока свободной памяти меньше, ГБ")
    ap.add_argument("--queue-wait", type=float, default=120.0, help="сколько минут игра ждёт общую очередь UE (сборки, потолок игр)")
    ap.add_argument("--keep-saved", action="store_true", help="не удалять Saved_regress_* сборки после прогона")
    ap.add_argument("--include-off", action="store_true", help="гнать и строки с вкл=0 (разведка)")
    ap.add_argument("--scan", default="", help="папка Source: перечень команд самотестов против ведомости (без запуска)")
    ap.add_argument("--dump", default="", help="для --scan: журнал игры с выводом `DumpConsoleCommands asda`")
    a = ap.parse_args(argv)
    if a.scan:
        return cmd_scan(a)
    if a.resume and a.resume != "1" and not a.out:
        a.out = a.resume   # --resume <папка> без --out: продолжать в ней же
    if not a.out:
        print("нужен --out <папка>")
        return 2
    a.recheck = max(0, min(3, a.recheck))
    root = a.build or json.load(open(LATEST, encoding="utf-8"))["game_root"]
    exe = os.path.join(root, EXE_REL)
    if not os.path.isfile(exe):
        print("нет exe игры: %s" % exe)
        return 2
    rows = [r for r in load_manifest(a.manifest) if (r["вкл"] == "1" or a.include_off) and (not a.filter or re.search(a.filter, r["id"]))]
    if not rows:
        print("в ведомости нет строк под условия")
        return 2
    out_dir = os.path.abspath(a.out)
    os.makedirs(out_dir, exist_ok=True)
    rows_by_id = {r["id"]: r for r in rows}
    plan, order = [], []
    fast_keep = {}   # (id, язык) → гнать ли строку в режиме --fast (зелёная и стабильная в эталоне)
    if a.fast and a.compare:
        import csv
        with open(os.path.join(a.compare, "summary.tsv"), encoding="utf-8-sig") as fh:
            for b in csv.DictReader(fh, delimiter="	"):
                fast_keep[(b["id"], b["язык"])] = b.get("вердикт") == "ok" and b.get("стабилен", "1") != "0"
    langs = ("ru", "en") if a.lang == "both" else (a.lang,)
    done = load_done(out_dir, rows, langs) if a.resume else {}
    stamp = time.strftime("s%d%H%M") if a.resume else ""   # метка запуска-продолжения: имена пачек не совпадут с прошлыми
    for lang in langs:
        lrows = [r for r in rows if lang == "ru" or a.all_en or "en" in [x.strip() for x in r["языки"].split(",")]]
        if a.fast and a.compare:
            n0 = len(lrows)
            lrows = [r for r in lrows if fast_keep.get((r["id"], lang), True)]
            say("--fast [%s]: пропущено %d строк, красных/нестабильных в эталоне" % (lang, n0 - len(lrows)))
        order += [(r["id"], lang) for r in lrows]
        todo = [r for r in lrows if (r["id"], lang) not in done]
        plan += [(lang, name + ("_" + stamp if stamp else ""), g, rs) for name, g, rs in make_chunks(todo, lang, a.chunk)]
    say("сборка: %s; самотестов %d (строк прогона %d%s), пачек %d, игр одновременно %d" % (
        root, len(rows), len(order), (", из них готово %d" % len(done)) if a.resume else "", len(plan), a.jobs))
    t0 = time.time()
    partial = os.path.join(out_dir, "summary.partial.tsv")

    def on_chunk(now):
        """Частичная сводка после каждой пачки: готовые итоги (прошлые + текущие) — её читают, пока прогон идёт."""
        allr = dict(done)
        allr.update(now)
        write_summary(allr, order, partial)

    if done:
        on_chunk({})
    results = dict(done)
    results.update(run_all(exe, plan, out_dir, a, on_chunk))
    write_logs(results, out_dir)
    for x in results.values():
        x.update({"попыток": "1", "стабилен": "1", "подпись": signature(x)})
    base = load_summary(os.path.join(a.compare, "summary.tsv")) if a.compare else {}
    if a.compare and not base:
        print("в эталоне нет summary.tsv: %s" % a.compare)
        return 2
    if a.recheck:
        if a.compare:
            keys = [k for k in order if k in results and k in base and results[k]["подпись"] != norm_sig(base[k].get("подпись", ""))]
        else:
            keys = [k for k in order if k in results and results[k]["вердикт"] != "ok"]
        if keys:
            tries = recheck(exe, results, keys, rows_by_id, out_dir, a, "re")
            for k, sigs in tries.items():
                x = results[k]
                x["повторы"] = sigs
                x["попыток"] = str(1 + len(sigs))
                x["стабилен"] = "1" if all(s == x["подпись"] for s in sigs) else "0"
    write_summary(results, order, os.path.join(out_dir, "summary.tsv"))
    if os.path.exists(partial):
        os.remove(partial)   # полная сводка готова — частичная больше не нужна
    minutes = (time.time() - t0) / 60
    cnt = {}
    for k in order:
        v = results[k]["вердикт"] if k in results else "notrun"
        cnt[v] = cnt.get(v, 0) + 1
    unstable = [k for k in order if k in results and results[k]["стабилен"] == "0"]
    json.dump({"build": root, "manifest": os.path.abspath(a.manifest), "lang": a.lang, "filter": a.filter, "jobs": a.jobs, "chunk": a.chunk,
               "started": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(t0)), "minutes": round(minutes, 1), "counts": cnt,
               "unstable": ["%s [%s]" % k for k in unstable]},
              open(os.path.join(out_dir, "run.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    say("ПРОГОН: %.1f мин; %s; нестабильных %d" % (minutes, ", ".join("%s %d" % kv for kv in sorted(cnt.items())), len(unstable)))
    for k in order:
        x = results.get(k)
        if x and x["вердикт"] != "ok":
            print("  %-8s %s [%s] %s/%s %s" % (x["вердикт"], k[0], k[1], x["ok"], x["fail"], x["ИТОГ"][:150]))
    rc = 3 if cnt.get("notrun") else 0
    if a.compare:
        full = not a.filter and not a.include_off
        cmp_order = list(order)
        if full:   # полный прогон: самотесты эталона, которых теперь нет, — тоже расхождение
            langs = {k[1] for k in order}
            cmp_order += [k for k in base if k not in results and k[1] in langs]
        diffs, flaky = compare(results, base, cmp_order)
        lines = ["СВЕРКА С ЭТАЛОНОМ %s: сверено %d, расхождений %d, нестабильных %d" % (a.compare, len(cmp_order), len(diffs), len(flaky))] + diffs + flaky
        if not diffs:
            lines.append("РАСХОЖДЕНИЙ НЕТ")
        print("\n" + "\n".join(lines), flush=True)
        with io.open(os.path.join(out_dir, "compare.txt"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write("\n".join(lines) + "\n")
        if diffs:
            rc = 1
    return rc


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    sys.exit(main())
