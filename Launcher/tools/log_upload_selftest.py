# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""log_upload_selftest.py — самотест выгрузки журналов лаунчера (★ 19.10.2026, отчёт Reports/release_logs_2026-10-19/C_launcher_tool.md).

Часть 1: приёмник-заглушка на 127.0.0.1 (конверт ASLU v2 по 00_SPEC.md, задаваемые код ответа и задержка) +
  AsdaLauncherCli log-upload на подложенной папке Saved: отбор 4 свежих, логины из .alog.meta, pid/время для EAC и падений,
  обрезка недописанного хвоста, 14 дней, 204/400/429/503, занятый файл, «игра запущена» (копия PING.EXE под именем Astory.exe),
  коллизия имён в Uploaded, хранение 10, предел 4 МиБ, таймаут 50 с, одно соединение на проход (keep-alive),
  джанкшен вместо папки журналов (ревизия М8); присланное расшифровывается ключом DPAPI.
Часть 2 (если собран AstoryServer/LogDrop/out/AsdaLogDrop.exe): настоящий приёмник агента B в режиме --console с тестовыми
  учётками → раскладка по логину и расшифровка обёрткой AstoryClient/Tools/LogSeal/logseal.py.
Файлы — во временной папке (--work). Сеть — только 127.0.0.1. Окон нет.
ЗАПУСК: python AstoryClient/Launcher/tools/log_upload_selftest.py [--work <папка>]   (нужен собранный build/AsdaLauncherCli.exe)
        python AstoryClient/Launcher/tools/log_upload_selftest.py --real [--account <логин>] — один запрос к боевому приёмнику
Побочных эффектов при импорте нет.
"""
import argparse, ctypes, ctypes.wintypes as wt, http.server, os, re, shutil, socket, struct, subprocess, sys, tempfile, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
LAUNCHER = os.path.dirname(HERE)                                                      # AstoryClient/Launcher
PORT_DIR = os.path.dirname(os.path.dirname(LAUNCHER))                                                  # port
CLI = os.path.join(LAUNCHER, "build", "AsdaLauncherCli.exe")                          # тестовый консольный лаунчер
LOGDROP_EXE = os.path.join(PORT_DIR, "AstoryServer", "LogDrop", "out", "AsdaLogDrop.exe")   # приёмник агента B
TOOL = os.path.join(PORT_DIR, "AstoryClient", "Tools", "LogSeal", "logseal.py")                      # обёртка расшифровки
ls = None                          # модуль asda_logseal (загружается в main)
XY = KID = None                    # текущий открытый ключ журналов
ROOT = SAVED = IDF = RECV = None   # рабочие пути (задаются в main)
ACC = re.compile(rb"^[A-Za-z0-9_.@-]{1,32}$")   # формат логина


MODE = {"code": 204, "sleep": 0}
REQS = []


def check_file(b):
    """Структура AsdaLogSeal так, как её проверит приёмник (без расшифровки)."""
    pos = 0
    segs = 0
    while pos < len(b):
        if len(b) - pos < 128 or b[pos:pos + 8] != ls.SEG_MAGIC or b[pos + 8] != 1 or b[pos + 12:pos + 20] != KID:
            return "сегмент %d: заголовок" % segs
        if b[pos + 20:pos + 23] != bytes([0xA5, 1, 0x4C]):
            return "ClientHello"
        pos += 128
        segs += 1
        while pos < len(b) and b[pos:pos + 4] != b"ASLG":
            if len(b) - pos < 12:
                return "обрезан заголовок кадра"
            clen = struct.unpack_from("<I", b, pos)[0]
            if clen < 16 or clen > 65536 + 16 or len(b) - pos - 12 < clen:
                return "кадр %d" % clen
            pos += 12 + clen
    return None


class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"   # keep-alive: проверяется, что лаунчер шлёт все файлы прохода по одному соединению

    def log_message(self, *a):
        pass

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(n)
        rec = {"path": self.path, "install": self.headers.get("X-Asda-Install"), "ua": self.headers.get("User-Agent"),
               "peer": self.client_address[1], "chunked": "chunked" in (self.headers.get("Transfer-Encoding") or ""),
               "ct": self.headers.get("Content-Type"), "len": n, "files": [], "err": None}
        try:
            if body[:4] != b"ASLU" or body[4] != 2 or not 1 <= body[5] <= 4 or body[6:8] != b"\0\0":
                raise ValueError("шапка")
            p = 8
            for i in range(body[5]):
                kind, alen = body[p], body[p + 1]
                acc = body[p + 2:p + 2 + alen]
                p += 2 + alen
                L = struct.unpack_from("<I", body, p)[0]
                p += 4
                data = body[p:p + L]
                p += L
                if len(data) != L or kind not in (1, 2, 3) or alen > 32 or (alen and not ACC.match(acc)):
                    raise ValueError("файл %d" % i)
                e = check_file(data)
                if e:
                    raise ValueError("файл %d: %s" % (i, e))
                rec["files"].append({"kind": kind, "account": acc.decode(), "len": L})
                os.makedirs(RECV, exist_ok=True)
                with open(os.path.join(RECV, "%d_%d_%s.%s" % (len(REQS), i, acc.decode() or "_unknown", "acrash" if kind == 3 else "alog")), "wb") as f:
                    f.write(data)
            if p != len(body):
                raise ValueError("сумма длин ≠ тело")
        except Exception as ex:
            rec["err"] = str(ex)
        REQS.append(rec)
        if MODE["sleep"]:
            time.sleep(MODE["sleep"])
        code = 400 if rec["err"] else (MODE["codes"].pop(0) if MODE.get("codes") else MODE["code"])
        self.send_response(code)
        self.send_header("Content-Length", "0")
        self.end_headers()


def set_times(path, mtime, ctime=None):
    os.utime(path, (mtime, mtime))
    if ctime is not None:
        k = ctypes.windll.kernel32
        h = k.CreateFileW(path, 0x100, 7, None, 3, 0x02000000, None)   # FILE_WRITE_ATTRIBUTES
        ft = int((ctime + 11644473600) * 10 ** 7)
        f = wt.FILETIME(ft & 0xFFFFFFFF, ft >> 32)
        k.SetFileTime(wt.HANDLE(h), ctypes.byref(f), None, None)
        k.CloseHandle(wt.HANDLE(h))


def sealed(kind, text, frames=1):
    return ls._seal_segment_py(XY, kind, [bytes([ls.REC_TEXT]) + (text + " #%d" % i).encode() for i in range(frames)])


def write(rel, data, mtime, ctime=None, meta=None):
    p = os.path.join(SAVED, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "wb") as f:
        f.write(data)
    if meta is not None:
        with open(p + ".meta", "w", encoding="utf-8") as f:
            f.write(meta)
    set_times(p, mtime, ctime)
    return p


def run(port, extra=()):
    """Один проход AsdaLauncherCli log-upload; итог (строка) и счётчики — в LAST. Свой лимит за час по умолчанию снят
    (--max-per-hour 1000), кроме прогона, который проверяет сам лимит (extra содержит --max-per-hour или «default»)."""
    args = list(extra)
    if "default" in args:
        args.remove("default")
    elif "--max-per-hour" not in args:
        args += ["--max-per-hour", "1000"]
    r = subprocess.run([CLI, "log-upload", "--saved", SAVED, "--install-id-file", IDF, "--endpoint",
                        "http://127.0.0.1:%d/api/v1/ingest" % port] + args, capture_output=True, timeout=180)
    out = r.stdout.decode("utf-8", "replace")
    print(out.strip())
    m = re.search(r"LOGUPLOAD (\S+)(.*)", out)
    LAST.clear()
    if m:
        LAST.update({k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", m.group(2))})
    return m.group(1) if m else None


LAST = {}   # счётчики последнего прохода (requests, uploaded, rejected, local, retry, expired, throttled)


def tree():
    out = []
    for r, _, fs in os.walk(SAVED):
        for f in fs:
            out.append(os.path.relpath(os.path.join(r, f), SAVED).replace("\\", "/"))
    return sorted(out)


ok = fail = 0


def expect(c, what):
    global ok, fail
    if c:
        ok += 1
        print("  [OK] " + what)
    else:
        fail += 1
        print("  [FAIL] " + what)


def files_since(n0):
    """Файлы запросов, пришедших после n0-го (по одному на запрос)."""
    return [f for r in REQS[n0:] for f in r["files"]]


class QuietServer(http.server.ThreadingHTTPServer):
    """Заглушка без трассировок: лаунчер закрывает keep-alive-соединение при выходе — это не ошибка."""
    def handle_error(self, request, client_address):
        pass


def part1():
    global SAVED
    srv = QuietServer(("127.0.0.1", 0), H)
    port = srv.server_address[1]
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    now = time.time()
    H_ = 3600
    # сессии: s1 (давно, логин alice), s2 (pid 4242, логин bob_2), s3 (без meta), s4 (неверный логин в meta), old (>14 дн.)
    write("Logs/Sessions/20261018-100000_1111.alog", sealed(3, "s1 alice", 3), now - 10 * H_, now - 11 * H_,
          '{"account":"alice","session":"20261018-100000_1111.alog"}')
    write("Logs/Sessions/20261018-200000_4242.alog", sealed(3, "s2 bob", 2), now - 5 * H_, now - 6 * H_,
          '{"account":"bob_2","session":"20261018-200000_4242.alog"}')
    trunc = sealed(3, "s3 truncated", 4)
    write("Logs/Sessions/20261019-010000_5555.alog", trunc[:-7], now - 2 * H_, now - 3 * H_)
    write("Logs/Sessions/20261019-020000_6666.alog", sealed(3, "s4 badlogin"), now - 1.5 * H_, now - 1.6 * H_,
          '{"account":"../evil\\\\x","session":"x"}')
    write("Logs/Sessions/20261001-000000_7777.alog", sealed(3, "old"), now - 15 * 24 * H_, now - 15 * 24 * H_,
          '{"account":"old","session":"x"}')
    # сессия, запечатанная ЧУЖИМ (старым) ключом — самая свежая, но без отправки в rejected
    _, other_xy = ls.gen_keypair()
    write("Logs/Sessions/20261019-030000_8888.alog",
          ls._seal_segment_py(other_xy, 3, [bytes([ls.REC_TEXT]) + b"old key"]), now - 0.4 * H_)
    # EAC: во время s2 → bob_2; во время s1 → alice
    write("Logs/AsdaEac.alog", sealed(1, "eac during s2"), now - 5.5 * H_)
    write("Logs/AsdaEac.1.alog", sealed(1, "eac during s1"), now - 10.5 * H_)
    write("Logs/AsdaReborn.alog", sealed(3, "engine log not in spec"), now - 1 * H_)   # не по SPEC — не трогать
    # падение pid 4242 (время ближе к s3/s4, но pid → bob_2) и мусор
    write("Crashes/AsdaCrash_20261019-000000_4242.acrash", sealed(4, "crash bob"), now - 4.6 * H_)
    write("Crashes/garbage.acrash", b"not a sealed file" * 10, now - 0.5 * H_)

    print("== прогон 1: 204, по файлу на запрос")
    res = run(port)
    t = tree()
    expect(res == "uploaded" and LAST.get("requests") == 4 and LAST.get("uploaded") == 4 and LAST.get("local") == 2,
           "итог uploaded: 4 запроса, 4 принято, 2 отклонено локально (мусор, старый ключ)")
    expect(len(REQS) == 4 and all(not r["err"] and len(r["files"]) == 1 for r in REQS),
           "4 запроса по ОДНОМУ файлу, все разобраны приёмником")
    expect(len({r["peer"] for r in REQS}) == 1 and not any(r["chunked"] for r in REQS),
           "одно соединение на проход (keep-alive, ревизия М7), Content-Length без chunked")
    r0 = REQS[0]
    expect(r0["path"] == "/api/v1/ingest" and r0["ua"] == "AsdaLauncher/1" and r0["ct"] == "application/octet-stream",
           "путь/User-Agent/Content-Type")
    expect(bool(re.fullmatch(r"[0-9a-f]{32}", r0["install"] or "")) and len({r["install"] for r in REQS}) == 1,
           "X-Asda-Install 32 hex, один на все запросы")
    expect(open(IDF).read().strip() == r0["install"], "install.id сохранён")
    files = files_since(0)
    print("   файлы:", files)
    expect([f["kind"] for f in files] == [1, 1, 3, 1], "по свежести: s4, s3, падение, s2")
    expect(files[0]["account"] == "" and files[1]["account"] == "", "неверный логин и нет meta → A=0")
    expect(files[2]["account"] == "bob_2" and files[3]["account"] == "bob_2", "падение pid 4242 → bob_2; s2 → bob_2")
    expect(files[1]["len"] < len(trunc) - 7 and files[1]["len"] > 128, "обрезанный хвост отброшен по границе кадра")
    expect("Crashes/Uploaded/rejected/garbage.acrash" in t, "мусор → rejected без отправки")
    expect("Logs/Sessions/Uploaded/rejected/20261019-030000_8888.alog" in t, "старый ключ (чужой keyId) → rejected без отправки")
    expect("Logs/Sessions/20261001-000000_7777.alog" not in t and "Logs/Sessions/20261001-000000_7777.alog.meta" not in t,
           "старше 14 дней удалено вместе с meta")
    expect("Logs/Sessions/Uploaded/20261018-200000_4242.alog.meta" in t, "meta перенесена вместе с сессией")
    expect("Logs/AsdaReborn.alog" in t, "AsdaReborn.alog не тронут")
    errs = 0
    for f in sorted(os.listdir(RECV)):
        errs += ls.decrypt_path(os.path.join(RECV, f), os.path.join(ROOT, "dec"))
    expect(errs == 0, "присланное расшифровывается (ошибок 0)")

    print("== прогон 2: 429 → файл остаётся, проход прерван")
    MODE["code"] = 429
    t1 = tree()
    n0 = len(REQS)
    res = run(port)
    expect(res == "retry" and tree() == t1 and len(REQS) - n0 == 1 and LAST.get("retry") == 1,
           "429: один запрос, retry, дальше в этот запуск не шлёт, файлы на месте")
    print("== прогон 3: 503 → то же")
    MODE["code"] = 503
    n0 = len(REQS)
    expect(run(port) == "retry" and tree() == t1 and len(REQS) - n0 == 1, "503: один запрос, retry")
    print("== прогон 4: занят на запись → пропуск; логины EAC")
    MODE["code"] = 204
    k = ctypes.windll.kernel32
    hb = k.CreateFileW(os.path.join(SAVED, "Logs", "AsdaEac.alog"), 0x40000000, 1, None, 3, 0, None)   # запись, общий только read
    n0 = len(REQS)
    res = run(port)
    k.CloseHandle(wt.HANDLE(hb))
    fl = files_since(n0)
    print("   файлы:", fl)
    expect(res == "uploaded" and len(fl) == 2, "занятый AsdaEac.alog пропущен, 2 отправлено")
    expect([f["account"] for f in fl] == ["alice", "alice"], "s1 → alice; AsdaEac.1 (во время s1) → alice")
    print("== прогон 5: 400 → rejected")
    MODE["code"] = 400
    n0 = len(REQS)
    res = run(port)
    t = tree()
    expect(res == "rejected" and "Logs/Uploaded/rejected/AsdaEac.alog" in t and files_since(n0)[0]["account"] == "bob_2",
           "400 → Uploaded/rejected (AsdaEac во время s2 → bob_2)")
    n0 = len(REQS)
    expect(run(port) == "nothing" and len(REQS) == n0, "больше нечего, запроса нет")
    print("== прогон 6: игра запущена")
    MODE["code"] = 204
    fake = os.path.join(ROOT, "fake", "Astory.exe")
    os.makedirs(os.path.dirname(fake), exist_ok=True)
    shutil.copy(r"C:\Windows\System32\PING.EXE", fake)
    write("Logs/AsdaEac.alog", sealed(1, "eac new"), now - 60)
    pr = subprocess.Popen([fake, "-n", "30", "127.0.0.1"], stdout=subprocess.DEVNULL, creationflags=0x08000000)
    time.sleep(0.5)
    t1 = tree()
    res = run(port)
    pr.kill()
    pr.wait()
    expect(res == "game-running" and tree() == t1 and len(REQS) == n0, "игра запущена: ничего не тронуто, запроса нет")
    print("== прогон 7: ротация EAC под тем же именем → второй AsdaEac.alog в Uploaded с меткой времени")
    res = run(port)
    expect(res == "uploaded" and "Logs/Uploaded/AsdaEac.alog" in tree(), "первый AsdaEac.alog — под своим именем")
    write("Logs/AsdaEac.alog", sealed(1, "eac newer"), now - 30)
    res = run(port)
    t = tree()
    expect(res == "uploaded" and any(re.fullmatch(r"Logs/Uploaded/\d{8}-\d{6}_AsdaEac\.alog", x) for x in t), "имя без коллизии")
    print("== прогон 8: хранить 10 в Uploaded; не больше 4 запросов за запуск")
    for i in range(12):
        write("Logs/Sessions/20261019-03%02d00_%d.alog" % (i, 9000 + i), sealed(3, "bulk %d" % i), now - 600 + i,
              meta='{"account":"carol","session":"x"}')
    per_run = []
    for _ in range(4):
        n0 = len(REQS)
        run(port)
        per_run.append(len(REQS) - n0)
    expect(per_run == [4, 4, 4, 0], "запросов за запуск: %s (ждали 4,4,4,0)" % per_run)
    up = [x for x in tree() if x.startswith("Logs/Sessions/Uploaded/") and x.endswith(".alog") and "/rejected/" not in x]
    metas = [x for x in tree() if x.startswith("Logs/Sessions/Uploaded/") and x.endswith(".meta")]
    expect(len(up) == 10, "в Sessions/Uploaded 10 журналов (было бы 16)")
    expect(all(m[:-5] in up for m in metas), "осиротевших meta нет")
    print("== прогон 9: таймаут 50 с (ревизия В3: лаунчер 50 < служба 55 < Caddy 60)")
    MODE["sleep"] = 65
    write("Logs/Sessions/20261019-040000_9999.alog", sealed(3, "timeout"), now - 30)
    t0 = time.time()
    res = run(port)
    dt = time.time() - t0
    MODE["sleep"] = 0
    expect(res == "retry" and 48 < dt < 58, "таймаут: retry за %.1f с" % dt)
    print("== прогон 10: предел 4 МиБ на файл, тело ≤ 8 МиБ")
    big = ls._seal_segment_py(XY, 3, [bytes([ls.REC_TEXT]) + os.urandom(60000) for _ in range(100)])   # ~6 МиБ
    mid = ls._seal_segment_py(XY, 3, [bytes([ls.REC_TEXT]) + os.urandom(60000) for _ in range(60)])    # ~3,6 МиБ
    write("Logs/Sessions/20261019-050000_1.alog", big, now - 10)
    write("Logs/Sessions/20261019-050000_2.alog", mid, now - 20)
    write("Logs/Sessions/20261019-050000_3.alog", mid, now - 30)
    n0 = len(REQS)
    res = run(port)
    fl = files_since(n0)
    print("   файлы:", fl, "тела", [r["len"] for r in REQS[n0:]])
    expect(res == "uploaded" and len(fl) == 4 and fl[0]["len"] <= 4 << 20 and fl[0]["len"] > 3 << 20 and
           all(r["len"] <= 8 << 20 and not r["err"] for r in REQS[n0:]), "6 МиБ → префикс ≤ 4 МиБ; каждый файл своим запросом")
    bigrecv = os.path.join(RECV, "%d_0__unknown.alog" % n0)
    expect(ls.decrypt_path(bigrecv, os.path.join(ROOT, "dec")) == 0, "префикс большого файла расшифровывается")
    print("== прогон 11: 400 у одного файла не мешает следующим")
    MODE["codes"] = [400, 204, 204]
    for i in range(3):
        write("Logs/Sessions/20261019-0600%02d_%d.alog" % (i, 7000 + i), sealed(3, "mix %d" % i), now - 5 - i)
    res = run(port)
    MODE.pop("codes", None)
    expect(res == "uploaded" and LAST.get("rejected") == 1 and LAST.get("uploaded") == 2 and LAST.get("requests") == 3,
           "итог по каждому файлу: 1 отклонён, 2 приняты")
    print("== прогон 12: свой лимит 6 запросов за скользящий час (боевое значение)")
    try:
        os.remove(os.path.join(os.path.dirname(IDF), "upload_sent.txt"))
    except OSError:
        pass
    for i in range(8):
        write("Logs/Sessions/20261019-0700%02d_%d.alog" % (i, 6000 + i), sealed(3, "rate %d" % i), now - 5 - i)
    seq = []
    for _ in range(3):
        n0 = len(REQS)
        r = run(port, ["default"])
        seq.append((r, len(REQS) - n0, LAST.get("throttled")))
    print("   ", seq)
    expect(seq == [("uploaded", 4, 0), ("uploaded", 2, 1), ("throttled", 0, 1)],
           "4 + 2 запроса, затем «throttled» без запросов")
    sent = open(os.path.join(os.path.dirname(IDF), "upload_sent.txt")).read().split()
    expect(len(sent) == 6, "upload_sent.txt: 6 отметок за час")
    print("== прогон 13: Crashes и Logs\\Uploaded — джанкшены на чужие папки (ревизия М8)")
    keep = SAVED
    SAVED = os.path.normpath(os.path.join(ROOT, "SavedJ"))
    foreign = os.path.normpath(os.path.join(ROOT, "foreign"))
    foreign_up = os.path.normpath(os.path.join(ROOT, "foreign_up"))
    os.makedirs(foreign)
    os.makedirs(foreign_up)
    with open(os.path.join(foreign, "AsdaCrash_20261019-000000_1.acrash"), "wb") as f:
        f.write(sealed(4, "чужая папка"))
    for i in range(12):
        with open(os.path.join(foreign_up, "x%02d.alog" % i), "wb") as f:
            f.write(b"x")
    os.makedirs(os.path.join(SAVED, "Logs"))
    subprocess.run(["cmd", "/c", "mklink", "/J", os.path.join(SAVED, "Crashes"), foreign], capture_output=True)
    subprocess.run(["cmd", "/c", "mklink", "/J", os.path.join(SAVED, "Logs", "Uploaded"), foreign_up], capture_output=True)
    write("Logs/AsdaEac.alog", sealed(1, "eac j"), now - 20)
    n0 = len(REQS)
    res = run(port)
    expect(os.path.isfile(os.path.join(foreign, "AsdaCrash_20261019-000000_1.acrash")) and
           all(len(r["files"]) == 1 and r["files"][0]["kind"] != 3 for r in REQS[n0:]),
           "файл в чужой папке (Crashes — джанкшен) не отправлен и не тронут")
    expect(len(os.listdir(foreign_up)) == 12 and res == "uploaded" and LAST.get("uploaded") == 1 and
           os.path.isfile(os.path.join(SAVED, "Logs", "AsdaEac.alog")),
           "Logs\\Uploaded — джанкшен: принятый файл не перенесён туда, чужое не вычищено")
    subprocess.run(["cmd", "/c", "rmdir", os.path.join(SAVED, "Crashes")], capture_output=True)
    subprocess.run(["cmd", "/c", "rmdir", os.path.join(SAVED, "Logs", "Uploaded")], capture_output=True)
    SAVED = keep
    srv.shutdown()


def part2(root2):
    global SAVED, IDF
    ROOT = root2
    SAVED = os.path.join(ROOT, "Saved")
    IDF = os.path.join(ROOT, "launcher", "install.id")
    os.makedirs(ROOT)
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    cfg = os.path.join(ROOT, "test.ini")
    with open(cfg, "w", encoding="utf-8") as f:
        f.write("Port=%d\nRoot=%s\nLogDir=%s\nPerIpPerHour=1000\nPerInstallPerHour=1000\nPerLoginPerHour=1000\n"
                "MinFreeBytes=0\n" %(port, os.path.join(ROOT, "drop"), os.path.join(ROOT, "drop", "service", "logs")))
    p = subprocess.Popen([LOGDROP_EXE, "--console", "--config", cfg, "--test-accounts", "alice,bob_2"], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, creationflags=0x08000000)
    for _ in range(50):
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.2).close()
            break
        except OSError:
            time.sleep(0.1)
    now = time.time()
    H = 3600
    write("Logs/Sessions/20261019-010000_1111.alog", sealed(3, "alice session", 3), now - 3 * H, now - 4 * H,
             '{"account":"alice","session":"20261019-010000_1111.alog"}')
    tr = sealed(3, "bob truncated session", 4)
    write("Logs/Sessions/20261019-050000_4242.alog", tr[:-9], now - 1 * H, now - 2 * H,
             '{"account":"Bob_2","session":"20261019-050000_4242.alog"}')
    write("Logs/Sessions/20261019-060000_5555.alog", sealed(3, "carol (нет такой учётки)"), now - 0.5 * H, now - 0.6 * H,
             '{"account":"carol","session":"x"}')
    write("Logs/AsdaEac.alog", sealed(1, "eac in alice session"), now - 3.2 * H)
    write("Crashes/AsdaCrash_20261019-055000_4242.acrash", sealed(4, "crash of bob"), now - 1.1 * H)
    r1 = run(port)
    r2 = run(port)
    out = subprocess.run([sys.executable, TOOL, "decrypt", os.path.join(ROOT, "drop"), "-o", os.path.join(ROOT, "dec")],
                         capture_output=True)
    p.stdin.write(b"\n")
    p.stdin.flush()
    try:
        p.wait(10)
    except Exception:
        p.kill()
    svc = p.stdout.read().decode("utf-8", "replace")
    stored = []
    for r, _, fs in os.walk(os.path.join(ROOT, "drop")):
        if "service" in r:
            continue
        stored += [os.path.relpath(os.path.join(r, f), os.path.join(ROOT, "drop")).replace("\\", "/") for f in fs]
    print("служба:\n" + svc[-1500:])
    print("сохранено:", *sorted(stored), sep="\n  ")
    print(out.stdout.decode("utf-8", "replace"))
    expect(r1 == "uploaded" and r2 == "uploaded", "оба прохода приняты настоящим приёмником B (204)")
    logs = [x for x in stored if x.endswith((".alog", ".acrash"))]
    top = sorted({x.split("/")[0] for x in logs})
    expect(len(logs) == 5, "5 файлов сохранено")
    expect(top == ["_unknown", "alice", "bob_2"], "раскладка по логину: %s" % top)
    expect(sum(x.startswith("bob_2/") for x in logs) == 2 and sum(x.startswith("alice/") for x in logs) == 2,
              "bob_2: сессия+падение; alice: сессия+EAC")
    expect(out.returncode == 0, "всё сохранённое расшифровано обёрткой AstoryClient/Tools/LogSeal (ошибок 0)")


def real_badpin():
    """Один настоящий запрос с ЧУЖИМ пином корня (только тестовая сборка, --pin): ждём отказ «корень цепочки не наш»,
    тело не уходит, файл остаётся на повтор."""
    p = write("Logs/Sessions/%s_%d.alog" % (time.strftime("%Y%m%d-%H%M%S"), os.getpid()),
              sealed(3, "launcher badpin check"), time.time() - 5)
    r = subprocess.run([CLI, "log-upload", "--saved", SAVED, "--install-id-file", IDF, "--endpoint",
                        "https://@@ASTORY_AUTH_HOST@@/api/v1/ingest", "--pin", "00" * 32], capture_output=True, timeout=90)
    out = r.stdout.decode("utf-8", "replace")
    print(out.strip())
    ok_ = "LOGUPLOAD retry" in out and "корень цепочки не наш" in out and os.path.isfile(p)
    print("ИТОГ: %s" % ("пиннинг отверг чужой корень, файл остался" if ok_ else "НЕ ТАК — см. выше"))
    return 0 if ok_ else 1


def real_check(account):
    """Один настоящий запрос к боевому приёмнику (TLS с проверкой сертификата, как у лаунчера): синтетический журнал
    сессии, запечатанный текущим открытым ключом (вид 3, текст «launcher log-upload real check»). Ждём 204 и перенос в Uploaded."""
    write("Logs/Sessions/%s_%d.alog" % (time.strftime("%Y%m%d-%H%M%S"), os.getpid()),
          sealed(3, "launcher log-upload real check %s" % time.strftime("%Y-%m-%d %H:%M:%S")), time.time() - 5,
          meta=('{"account":"%s","session":"real-check"}' % account) if account else None)
    r = subprocess.run([CLI, "log-upload", "--saved", SAVED, "--install-id-file", IDF, "--endpoint",
                        "https://@@ASTORY_AUTH_HOST@@/api/v1/ingest"], capture_output=True, timeout=60)
    out = r.stdout.decode("utf-8", "replace")
    print(out.strip())
    ok_ = "LOGUPLOAD uploaded" in out and any("Uploaded" in x for x in tree())
    print("ИТОГ: %s" % ("принято боевым приёмником (204)" if ok_ else "НЕ принято — см. строку LOG выше"))
    return 0 if ok_ else 1


def main(argv=None):
    global ls, XY, KID, ROOT, SAVED, IDF, RECV
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser(description="самотест выгрузки журналов лаунчера")
    ap.add_argument("--work", default=None, help="рабочая папка (по умолчанию новая временная)")
    ap.add_argument("--real-badpin", action="store_true",
                    help="один настоящий запрос с чужим пином корня — ждём отказ пиннинга без отправки тела")
    ap.add_argument("--real", action="store_true",
                    help="вместо самотеста — ОДИН настоящий запрос на https://@@ASTORY_AUTH_HOST@@/api/v1/ingest (синтетический журнал)")
    ap.add_argument("--account", default="", help="--real: логин в .alog.meta (пусто — без логина, ляжет в _unknown)")
    a = ap.parse_args(argv)
    sys.path.insert(0, os.path.join(PORT_DIR, "AstoryClient", "Tools"))
    import asda_logseal
    ls = asda_logseal
    XY, KID = ls.current_public()
    work = a.work or tempfile.mkdtemp(prefix="asda_logupload_")
    shutil.rmtree(work, ignore_errors=True)
    ROOT = os.path.join(work, "p1")
    SAVED = os.path.join(ROOT, "Saved")
    IDF = os.path.join(ROOT, "launcher", "install.id")
    RECV = os.path.join(ROOT, "recv")
    if a.real_badpin:
        return real_badpin()
    if a.real:
        return real_check(a.account)
    cfg = open(os.path.join(LAUNCHER, "src", "Config.h"), encoding="utf-8").read()
    m = re.search(r'kLogSealKeyIds\[\] = \{([^}]*)\}', cfg)
    ids = re.findall(r'"([0-9a-f]{16})"', m.group(1) if m else "")
    expect(KID.hex() in ids, "Config.h kLogSealKeyIds %s содержит текущий keyId %s (current.txt)" % (ids, KID.hex()))
    print("== часть 1: приёмник-заглушка")
    part1()
    if os.path.isfile(LOGDROP_EXE):
        print("== часть 2: настоящий приёмник AsdaLogDrop.exe --console")
        part2(os.path.join(work, "p2"))
    else:
        print("== часть 2 пропущена: нет %s" % LOGDROP_EXE)
    print("\nИТОГ: OK %d, FAIL %d (папка %s)" % (ok, fail, work))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
