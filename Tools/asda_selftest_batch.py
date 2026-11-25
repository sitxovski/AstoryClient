# Copyright (c) 2026 @sitxovski (Discord)
# asda_selftest_batch.py — ПАКЕТНЫЙ ПРОГОН САМОТЕСТОВ: список → ОДИН запуск игры → итог по каждому (11.10.2026, правило владельца).
#
# ЗАЧЕМ. Прежде каждый самотест — отдельная игра (40–60 с загрузки). Здесь список уходит в консольную команду
#   `asda.SelfTest.Batch` (Source/AsdaReborn/Private/AsdaSelfTestBatch.cpp): она в одной игре запускает самотесты по
#   очереди (каждому добавляет `quit`, перехват `quit` = конец самотеста), пишет ok/fail/timeout и общий итог, выходит.
#   Отчёт и устройство: Reports/dedup_audit2/wave0_fixes.md §3.
#
# ЗАПУСК (обычный python, unreal не нужен):
#   python Tools/asda_selftest_batch.py asda.Clan.BtnSelfTest "asda.Craft.SelfTest delay=12" …
#   python Tools/asda_selftest_batch.py --spec <spec.tsv> [--filter <regex>]      (формат spec как у run_w0.py: имя<TAB>карта<TAB>команда…)
#     --map world|login|<путь уровня>  карта игры (world — alpen со своим персонажем; по умолчанию world)
#     --lang ru|en      язык сессии игры (-AsdaLang=; по умолчанию ru) — ★ 21.10.2026
#     --timeout <с>     потолок на самотест (600); отдельному — `@timeout=N` в строке теста
#     --start <с>       пауза до первого самотеста (5)      --settle <с>  пауза после конца самотеста (1.5)
#     --out <папка>     куда класть общий лог, логи по самотестам (<имя>.log) и summary.tsv (по умолчанию — временная, удаляется
#                       после печати итога, если не задана)
#     --compare <папка> сверить с эталонными логами <папка>/<имя>.log: последняя строка ИТОГ и набор строк FAIL (числа — маской)
#     --suffix <s>      суффикс -saveddirsuffix (по умолчанию batch_<pid>); временный Saved_<s> удаляется после прогона
#     --print-cmd       только напечатать командную строку
#     --between <к1;к2> консольные команды между самотестами (без аргументов; ★ 11.10.2026, В3 S1)
#     --pre "<команда>" консольная команда до пакета (повторяемый; ★ В3 S1 — например заглушка asda.SelfTest.SimulateOnline 1)
#   Ключи игры: -game -AsdaNoNet -RenderOffscreen -AsdaLang=ru -saveddirsuffix=<s>, первая команда `r.SetRes 1024x768w`;
#   окна нет (STARTUPINFO без окна + CREATE_NO_WINDOW, asda_pie_harness.popen_hidden); добивание — по PID.
# ★★★ ФАЙЛЫ ВЛАДЕЛЬЦА (26.10.2026, ревизия Reports/wave_1025d_review/REVIEW.md п. 7). Песочница -saveddirsuffix уводит всё
#   под Saved, но раскладка `system/Shortcutkey.ini` живёт ВНЕ Saved (путь от ProjectDir) — самотесты окна настроек 25.10 писали
#   файл владельца. Поэтому:
#   1) игре передаётся `-AsdaShortcutkeyIni=<абсолютный путь Saved_<s>/AsdaSelfTest/Shortcutkey.ini>`, туда до запуска кладётся
#      копия файла владельца (тесты видят ту же раскладку, пишут — в копию);
#   2) СТРАЖ: до запуска снимаются md5 и содержимое всех файлов владельца, которые может тронуть игра (`OWNER_GLOBS`:
#      system/*, Config/*.ini, Saved/Config/<платформа>/*.ini, Saved/*.bin — ChatOpt.bin, AD.bin, Saved/AsdaLogin/* — check.bin,
#      *.uproject); после прогона — сверка. Изменился/появился/пропал файл → «ЗАПИСАН ФАЙЛ ВЛАДЕЛЬЦА: …», файл восстанавливается
#      (появившийся — удаляется), код возврата 1. Если во время прогона жил ЧУЖОЙ UnrealEditor (владелец мог сам править настройки),
#      автоматического восстановления нет — копия «до прогона» кладётся в @@ASTORY_DEV_ROOT@@\_backup\<дата>_selftest_owner_guard\<время>\,
#      решает человек.
#      Журналы (Saved/Logs, %LOCALAPPDATA%/AsdaReborn/*.alog|log — дописываются) стражем не сверяются: это не настройки.
# КОД ВОЗВРАТА: 0 — пакет дошёл до итога, все ok (с --compare — и всё совпало с эталоном); 1 — есть fail/timeout/skipped,
#   расхождение с эталоном или ★ записан файл владельца; 3 — пакет не дошёл до итога (нет строки SELFTEST_BATCH ИТОГ); 2 — ошибка аргументов.
#
# ★ Модуль БЕЗ побочных эффектов при импорте (закон tools-no-import-side-effects): работа — только в main() под __main__.
import argparse
import glob
import hashlib
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))   # папка Tools
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

UE_GAME = os.path.join(os.environ.get("ASDA_UE_DIR") or r"D:\Games\UE_5.8",
                       "Engine", "Binaries", "Win64", "UnrealEditor.exe")   # редактор движка в режиме -game (ASDA_UE_DIR)
MAPS = {"login": "/Game/Map/Login",                                    # экран логина
        "world": "/Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode"}   # мир alpen со своим персонажем
TS_RE = re.compile(r"^\[\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3}\]\[\s*\d+\]")   # метка времени и кадра строки лога
NUM_RE = re.compile(r"\d+(\.\d+)?")                                                     # числа (тайминги) — маска при сверке
BEGIN_RE = re.compile(r"SELFTEST_BATCH НАЧАЛО (\d+)/(\d+) (\S+):")                       # начало самотеста в общем логе
END_RE = re.compile(r"SELFTEST_BATCH КОНЕЦ (\d+)/(\d+) (\S+): (\w+) \(([-\d.]+) с\)(.*)$")  # конец самотеста и вердикт
RESULT_RE = re.compile(r"SELFTEST_BATCH РЕЗУЛЬТАТ (\S+) (\w+)")                          # строка общего итога по самотесту
TOTAL_RE = re.compile(r"SELFTEST_BATCH ИТОГ: (.*)$", re.M)                                      # общий итог пакета
# ★ 26.10.2026: файлы владельца, которые может записать игра (маски от папки проекта) — страж до/после прогона (шапка, п. 2).
OWNER_GLOBS = ("system/*", "Config/*.ini", "Saved/Config/*/*.ini", "Saved/*.bin", "Saved/AsdaLogin/*", "*.uproject")


def read_text(path):
    """Текст файла (пусто, если файла нет)."""
    try:
        return io.open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        return ""


def tests_from_spec(path, flt=None):
    """Команды самотестов из spec.tsv (имя<TAB>карта<TAB>команда<TAB>маркер<TAB>таймаут): команда без `quit`, с `@timeout`."""
    out = []
    for ln in io.open(path, encoding="utf-8"):
        ln = ln.rstrip("\r\n")
        if not ln or ln.startswith("#"):
            continue
        f = ln.split("\t")
        if len(f) < 3 or (flt and not re.search(flt, f[0])):
            continue
        cmd = " ".join(t for t in f[2].split() if t.lower() != "quit")
        if len(f) > 4 and f[4].strip():
            cmd += " @timeout=%s" % f[4].strip()
        out.append(cmd)
    return out


def scan_log(text):
    """(последняя строка ИТОГ, множество строк FAIL с маской чисел) — как cmp_itog.py волны В0 (сверка с эталоном)."""
    itog, fails = "", set()
    for ln in text.splitlines():
        ln = TS_RE.sub("", ln).strip()
        if "SELFTEST_BATCH" in ln or "CVAR_RESTORE" in ln:
            continue
        if "ИТОГ" in ln:
            itog = ln
        elif "FAIL" in ln:
            fails.add(NUM_RE.sub("#", ln))
    return itog, fails


def split_segments(text):
    """{имя: текст сегмента самотеста} — строки от НАЧАЛО до КОНЕЦ включительно."""
    seg, cur, buf = {}, None, []
    for ln in text.splitlines():
        m = BEGIN_RE.search(ln)
        if m:
            cur, buf = m.group(3), [ln]
            continue
        if cur is not None:
            buf.append(ln)
            e = END_RE.search(ln)
            if e and e.group(3) == cur:
                seg[cur] = "\n".join(buf) + "\n"
                cur, buf = None, []
    if cur is not None:   # оборвался — сохранить, что есть
        seg[cur] = "\n".join(buf) + "\n"
    return seg


def owner_snapshot(project_dir):
    """{полный путь: (md5, байты)} файлов владельца по OWNER_GLOBS (только файлы)."""
    snap = {}
    for g in OWNER_GLOBS:
        for p in glob.glob(os.path.join(project_dir, g)):
            if os.path.isfile(p):
                try:
                    data = open(p, "rb").read()
                except OSError:
                    continue
                snap[os.path.abspath(p)] = (hashlib.md5(data).hexdigest(), data)
    return snap


_SLOT = None   # заявка общей очереди UE текущего прогона (ue_queue.Slot) — ставит main()


def foreign_editors(own_pid=None):
    """PID запущенных UnrealEditor.exe, кроме своего (и его потомков не различаем — свой процесс к этому времени добит)."""
    try:
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq UnrealEditor.exe", "/FO", "CSV", "/NH"],
                             capture_output=True, timeout=30).stdout.decode("ascii", errors="replace")   # кодовая страница консоли — нужны лишь цифры PID
    except Exception:
        return []
    pids = []
    for ln in out.splitlines():
        f = [x.strip('"') for x in ln.split('","')]
        if len(f) >= 2 and f[1].strip('"').isdigit():
            pid = int(f[1].strip('"'))
            if pid != own_pid:
                pids.append(pid)
    # ★ 10.11.2026: игры других самотест-прогонов (RU и EN идут парой) — под очередью, файлы владельца они не трогают
    #   (у каждой свой Saved_<суффикс>); «чужой» = только редактор вне очереди (владелец). Раньше парный прогон
    #   отключал восстановление файлов владельца.
    try:
        import ue_queue
        q = ue_queue.queued_pids()
        pids = [x for x in pids if x not in q]
    except Exception:
        pass
    return pids


OWNER_KEEP_ROOT = r"@@ASTORY_DEV_ROOT@@\_backup"   # единый корень бэкапов проекта (копии «до прогона», если восстанавливать нельзя)


def owner_guard_check(before, project_dir, foreign, keep_root=OWNER_KEEP_ROOT):
    """Сверка файлов владельца с снимком «до». → список строк-сообщений (пусто — ничего не тронуто). Восстанавливает сам,
    если чужого UnrealEditor не было; иначе сохраняет копии «до» в <keep_root>/<дата>_selftest_owner_guard/<время>/."""
    after = owner_snapshot(project_dir)
    msgs = []
    stamp = os.path.join(keep_root, time.strftime("%Y-%m-%d") + "_selftest_owner_guard", time.strftime("%H%M%S"))
    for p in sorted(set(before) | set(after)):
        b, a = before.get(p), after.get(p)
        if b and a and b[0] == a[0]:
            continue
        what = "изменён (md5 %s → %s)" % (b[0], a[0]) if (b and a) else ("появился" if a else "пропал")
        if foreign:
            if b:
                keep = os.path.join(stamp, os.path.relpath(p, project_dir))
                os.makedirs(os.path.dirname(keep), exist_ok=True)
                open(keep, "wb").write(b[1])
                how = "НЕ восстановлен: во время прогона жил чужой UnrealEditor %s — копия до прогона %s" % (foreign, keep)
            else:
                how = "НЕ удалён: во время прогона жил чужой UnrealEditor %s" % foreign
        else:
            try:
                if b:
                    open(p, "wb").write(b[1])
                    ok = hashlib.md5(open(p, "rb").read()).hexdigest() == b[0]
                    how = "восстановлен (md5 %s %s)" % (b[0], "сверен" if ok else "НЕ СОВПАЛ после записи!")
                else:
                    os.remove(p)
                    how = "удалён (до прогона его не было)"
            except OSError as e:
                how = "ВОССТАНОВИТЬ НЕ УДАЛОСЬ: %s" % e
        msgs.append("ЗАПИСАН ФАЙЛ ВЛАДЕЛЬЦА: %s — %s; %s" % (p, what, how))
    return msgs


def build_cmd(map_path, suffix, log, list_file, args, keys_ini=""):
    """Командная строка игры: -game без сети и без окна, первая команда r.SetRes, затем пакет с выходом в конце.
    `keys_ini` — абсолютный путь песочницы раскладки (`-AsdaShortcutkeyIni=`, ★ 26.10.2026); пусто — без ключа."""
    batch = "asda.SelfTest.Batch file=%s quit timeout=%g settle=%g start=%g" % (
        list_file.replace("\\", "/"), args.timeout, args.settle, args.start)
    if getattr(args, "between", ""):   # ★ 11.10.2026 (В3 S1): команды между самотестами (через «;»)
        batch += " between=%s" % args.between
    return [UE_GAME, args.project, map_path, "-game", "-AsdaNoNet", "-RenderOffscreen", "-unattended", "-nopause",
            "-nosplash"] + ([] if getattr(args, "sound", False) else ["-nosound"]) + ["-AsdaLang=%s" % (getattr(args, "lang", "") or "ru"), "-saveddirsuffix=%s" % suffix, "-abslog=%s" % log] \
        + (["-AsdaShortcutkeyIni=%s" % keys_ini] if keys_ini else []) \
        + ["-ExecCmds=r.SetRes 1024x768w,%s%s" % ("".join(c + "," for c in getattr(args, "pre", []) or []), batch)]


def run(args, tests, out_dir, guard=None):
    """Один запуск игры с пакетом. → (текст общего лога, дошёл ли пакет до итога). `guard` (dict) — ★ 26.10.2026 страж файлов
    владельца: сюда кладутся снимок «до» (`before`), чужие UnrealEditor (`foreign`) и сообщения сверки (`msgs`)."""
    import asda_pie_harness as PH   # скрытый запуск и добивание по PID — общий каркас
    os.makedirs(out_dir, exist_ok=True)
    suffix = args.suffix or "batch_%d" % os.getpid()
    if not re.match(r"^[A-Za-z0-9_]+$", suffix):
        raise SystemExit("суффикс %r: только [A-Za-z0-9_]" % suffix)
    list_file = os.path.join(out_dir, "batch_list.txt")
    if re.search(r"[\s,]", list_file):   # аргументы консоли делятся пробелами, -ExecCmds — запятыми
        raise SystemExit("путь списка %r: без пробелов и запятых (задай --out)" % list_file)
    with io.open(list_file, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("# список пакета asda_selftest_batch.py\n" + "\n".join(tests) + "\n")
    log = os.path.join(out_dir, "batch.log")
    if os.path.exists(log):
        os.remove(log)
    map_path = MAPS.get(args.map, args.map)
    project_dir = os.path.dirname(os.path.abspath(args.project))
    saved = os.path.join(project_dir, "Saved_" + suffix)
    # ★ 26.10.2026: песочница раскладки — абсолютный путь (относительный игра разрешила бы от LaunchDir), без пробелов и запятых.
    keys_ini = os.path.join(saved, "AsdaSelfTest", "Shortcutkey.ini")
    if re.search(r"[\s,]", keys_ini):
        raise SystemExit("путь песочницы раскладки %r: без пробелов и запятых" % keys_ini)
    cmd = build_cmd(map_path, suffix, log, list_file, args, keys_ini)
    if args.print_cmd:
        print("КОМАНДНАЯ СТРОКА: %s" % subprocess.list2cmdline(cmd))
        return "", True
    shutil.rmtree(saved, ignore_errors=True)
    owner_keys = os.path.join(project_dir, "system", "Shortcutkey.ini")
    os.makedirs(os.path.dirname(keys_ini), exist_ok=True)
    if os.path.isfile(owner_keys):
        shutil.copyfile(owner_keys, keys_ini)   # тесты видят раскладку владельца, пишут — в копию
    if guard is not None:
        guard["before"] = owner_snapshot(project_dir)
        guard["foreign"] = foreign_editors()
        print("страж файлов владельца: снято %d файлов%s; раскладка игры — %s" % (
            len(guard["before"]), ", ЧУЖОЙ UnrealEditor %s — восстановления не будет" % guard["foreign"] if guard["foreign"] else "",
            keys_ini), flush=True)
    per = [args.timeout] * len(tests)
    for i, t in enumerate(tests):
        m = re.search(r"@timeout=([\d.]+)", t)
        if m:
            per[i] = float(m.group(1))
    total = args.start + sum(per) + len(tests) * (args.settle + 2) + 180   # загрузка игры + все потолки
    t0 = time.time()
    p = PH.popen_hidden(cmd)
    if _SLOT is not None:
        _SLOT.add_pid(p.pid)   # ★ 10.11.2026: игра под очередью — другие прогоны не считают её «чужим редактором»
    print("запущен PID %d, самотестов %d, общий потолок %.0f с, лог %s" % (p.pid, len(tests), total, log), flush=True)
    done, seen = False, 0
    try:
        while True:
            time.sleep(2)
            txt = read_text(log)
            n = txt.count("SELFTEST_BATCH КОНЕЦ")   # сколько самотестов уже закончилось
            if n > seen:
                for ln in txt.splitlines():
                    e = END_RE.search(ln)
                    if e and int(e.group(1)) > seen:
                        print("  %s/%s %s: %s (%s с)" % (e.group(1), e.group(2), e.group(3), e.group(4), e.group(5)), flush=True)
                seen = n
            if TOTAL_RE.search(txt):
                done = True
                deadline = time.time() + 20       # итог есть — дать игре выйти самой
                while p.poll() is None and time.time() < deadline:
                    time.sleep(1)
                break
            if p.poll() is not None:
                break
            if time.time() - t0 > total:
                print("!! общий потолок %.0f с — добиваю" % total, flush=True)
                break
    finally:
        if p.poll() is None:
            PH.kill_tree(p.pid)
        time.sleep(2)
        if guard is not None and "before" in guard:
            foreign = sorted(set(guard.get("foreign", [])) | set(foreign_editors(p.pid)))
            guard["msgs"] = owner_guard_check(guard["before"], project_dir, foreign)
        shutil.rmtree(saved, ignore_errors=True)
        try:
            os.remove(list_file)
        except OSError:
            pass
    print("прогон %.0f с" % (time.time() - t0), flush=True)
    return read_text(log), done


def report(text, done, out_dir, compare_dir, write_logs):
    """Разбор общего лога: итог по самотестам, логи сегментов, сверка с эталоном. → код возврата."""
    results = {m.group(1): m.group(2) for m in RESULT_RE.finditer(text)}
    ends = {e.group(3): e for e in (END_RE.search(ln) for ln in text.splitlines()) if e}
    segs = split_segments(text)
    rows, bad, mism = [], 0, 0
    names = list(results) or list(segs)
    for name in names:
        st = results.get(name, "нет итога")
        sec = ends[name].group(5) if name in ends else "—"
        seg = segs.get(name, "")
        itog, fails = scan_log(seg)
        if write_logs and seg:
            with io.open(os.path.join(out_dir, name + ".log"), "w", encoding="utf-8", newline="\n") as fh:
                fh.write(seg)
        cmp_txt = ""
        if compare_dir:
            ref = os.path.join(compare_dir, name + ".log")
            if not os.path.isfile(ref):
                cmp_txt = "эталона нет"
            else:
                ritog, rfails = scan_log(read_text(ref))
                same_itog = ritog == itog
                only_ref, only_new = rfails - fails, fails - rfails
                if same_itog and not only_ref and not only_new:
                    cmp_txt = "СОВПАЛО"
                else:
                    mism += 1
                    cmp_txt = "РАЗНИЦА: ИТОГ эталон «%s» / пакет «%s»; FAIL только в эталоне %d, только в пакете %d" % (
                        ritog[:120], itog[:120], len(only_ref), len(only_new))
        if st != "ok":
            bad += 1
        rows.append((name, st, sec, itog[:160], cmp_txt))
    print("\nсамотест\tвердикт\tс\tИТОГ\tсверка с эталоном")
    for r in rows:
        print("\t".join(r))
    m = TOTAL_RE.search(text)
    print("\nПАКЕТ: %s" % (m.group(1) if m else "НЕ ДОШЁЛ ДО ИТОГА"))
    if write_logs:
        with io.open(os.path.join(out_dir, "summary.tsv"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write("самотест\tвердикт\tс\tИТОГ\tсверка\n")
            for r in rows:
                fh.write("\t".join(r) + "\n")
    if not done or not m:
        return 3
    return 1 if (bad or mism) else 0


def main(argv=None):
    """Разбор аргументов → запуск пакета → отчёт."""
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
    ap = argparse.ArgumentParser(description="Пакетный прогон самотестов в одной игре (asda.SelfTest.Batch)")
    ap.add_argument("tests", nargs="*", help="команды самотестов (с аргументами — в кавычках)")
    ap.add_argument("--spec", default="", help="spec.tsv (имя<TAB>карта<TAB>команда<TAB>маркер<TAB>таймаут)")
    ap.add_argument("--filter", default="", help="regex по имени для --spec")
    ap.add_argument("--map", default="world", help="world | login | путь уровня")
    ap.add_argument("--lang", default="ru", help="язык сессии игры: ru | en (★ 21.10.2026 — прогон самотестов на обоих языках)")
    ap.add_argument("--timeout", type=float, default=600.0, help="потолок на самотест, с")
    ap.add_argument("--start", type=float, default=5.0, help="пауза до первого самотеста, с")
    ap.add_argument("--settle", type=float, default=1.5, help="пауза после конца самотеста, с")
    ap.add_argument("--out", default="", help="папка результатов (не задана — временная, удаляется)")
    ap.add_argument("--compare", default="", help="папка эталонных логов <имя>.log")
    ap.add_argument("--suffix", default="", help="суффикс -saveddirsuffix")
    ap.add_argument("--project", default=r"@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject", help="проект")
    ap.add_argument("--sound", action="store_true", help="★ 08.11.2026: БЕЗ -nosound — звуковое устройство есть (самотест петель asda.SelfTest.SoundLoop)")
    ap.add_argument("--print-cmd", action="store_true", help="только напечатать командную строку")
    ap.add_argument("--pre", action="append", default=[], help="консольная команда ДО пакета (можно несколько), напр. \"asda.SelfTest.SimulateOnline 1\"")
    ap.add_argument("--between", default="", help="консольные команды между самотестами через «;» (без пробелов), напр. asda.Session.ResetMap")
    a = ap.parse_args(argv)
    tests = list(a.tests)
    if a.spec:
        tests += tests_from_spec(a.spec, a.filter or None)
    tests = [t.strip() for t in tests if t.strip()]
    if not tests:
        print("нет самотестов (аргументы или --spec)")
        return 2
    keep = bool(a.out)
    out_dir = os.path.abspath(a.out) if keep else tempfile.mkdtemp(prefix="asda_selftest_batch_")
    guard = {}
    try:
        if a.print_cmd:
            text, done = run(a, tests, out_dir, guard)
            return 0
        # ★ 10.11.2026: общая очередь машины (AstoryClient\DEPLOY\_tools\ue_queue.py): самотест идёт вместе с другими
        #   самотестами, но не посреди сборки/cook — ждёт сам, без самодельных циклов в сессиях.
        sys.path.insert(0, os.path.join(os.path.dirname(TOOLS_DIR), "DEPLOY", "_tools"))   # <проект>/DEPLOY/_tools
        import ue_queue
        global _SLOT
        with ue_queue.Slot("shared", "selftest " + " ".join(t.split()[0] for t in tests)[:120],
                           log=lambda m: print(m, flush=True)) as _SLOT:
            text, done = run(a, tests, out_dir, guard)
        rc = report(text, done, out_dir, a.compare, write_logs=keep)
        msgs = guard.get("msgs", [])
        if msgs:
            print("\n" + "!" * 100)
            for m in msgs:
                print("!! " + m)
            print("!" * 100, flush=True)
            return 1 if rc == 0 else rc
        print("страж файлов владельца: %d файлов не изменились" % len(guard.get("before", {})))
        return rc
    finally:
        if not keep:
            shutil.rmtree(out_dir, ignore_errors=True)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    sys.exit(main())
