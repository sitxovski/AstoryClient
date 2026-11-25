# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""fetch_and_decrypt.py — забрать новые журналы игроков с dev-сервера и расшифровать (★ 19.10.2026; ★ 23.11.2026 А7.2 — по SSH, не по шаре).

ОТКУДА: хранилище приёмника LogDrop (AstoryServer/LogDrop, 00_SPEC.md части B и «★ Дополнение») на сервере:
  @@ASTORY_SERVER_ROOT_ESC@@\\Working_ASDA\\v2\\LogDrop\\<логин|_unknown|_acct_<hex>>\\<ГГГГ-ММ-ДД>\\<серверное имя>.(alog|acrash) + .json
  ★ 23.11.2026 (А7.2, правило владельца «сервер — только SSH»): список — PowerShell по SSH (ssh_scan), файлы — scp (ssh_fetch) ключом
  %USERPROFILE%\\.ssh\\@@ASTORY_SSH_KEY_NAME@@ на Administrator@@@ASTORY_SERVER_VPN_IP@@ (адрес VPN, как AstoryAdminCenter/core/config.SSH_HOST).
  Шара \\\\@@ASTORY_SERVER_HOSTNAME@@\\H(@@ASTORY_SERVER_HOSTNAME@@_ns) больше не используется: она висит без таймаута. Целость копии — SHA-256 файла = sha256 из .json приёмника
  (не сошлось — копия стирается, в «забранное» не пишется). Другой источник — --src <папка> или переменная ASDA_LOGDROP (например,
  скопированная вручную папка LogDrop).
КУДА: @@ASTORY_DEV_ROOT_ESC@@\\_player_logs\\<логин>\\<ГГГГ-ММ-ДД>\\ — ВНЕ port (депо Perforce; ★ ревизия М12): копия файла, его .json и
  расшифровка <имя>.txt (+ <имя>.d\\ у падений). Что уже забрано — список в @@ASTORY_DEV_ROOT_ESC@@\\_player_logs\\.fetched.txt
  (повторный запуск берёт только новое; --all — всё заново).
  На сервере ничего не удаляется и не меняется (только чтение).

РАСШИФРОВКА — общим модулем AstoryClient/Tools/asda_logseal.py (через logseal.py рядом), закрытым ключом из хранилища
DPAPI владельца на этой машине. Ключ никуда не копируется; на другом ПК сначала `logseal.py import-backup <файл>`.

ВАЖНО: логин заявлен клиентом игрока и сервером проверен только на существование учётки — подделать «чужой» логин можно.
Журнал — диагностика, не доказательство против игрока.

ЗАПУСК
  python fetch_and_decrypt.py [--src <LogDrop>] [--dest <папка>] [--all] [--kinds alog,acrash] [--limit N]   (или FetchAndDecrypt.bat рядом)
  --kinds  что забирать (по умолчанию оба вида; раздел «Сбои клиента» панели берёт только acrash — fetch_new(("acrash",)))
  --limit  не больше N новых файлов за запуск (проба канала)
Побочных эффектов при импорте нет (закон tools-no-import-side-effects).
"""
import argparse
import base64
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SSH_HOST = "Administrator@@@ASTORY_SERVER_VPN_IP@@"                                  # dev-сервер по VPN (= AdminCenter core/config.SSH_HOST)
SSH_KEY = os.path.join(os.environ.get("USERPROFILE", ""), ".ssh", "@@ASTORY_SSH_KEY_NAME@@")   # ключ SSH (= AdminCenter core/config.SSH_KEY)
REMOTE_ROOT = r"@@ASTORY_SERVER_ROOT@@\Working_ASDA\v2\LogDrop"                            # хранилище приёмника на сервере (LogDrop.ini Root=)
SERVICE_DIR = "service"                                                     # служебная папка приёмника (журналы службы)
LOGIN_DIR_RE = re.compile(r"^(_unknown|_acct_[0-9a-f]{1,64}|[a-z0-9_][a-z0-9_.@-]{0,31})$")   # ★ р75 К-8: не «.» / «..»   # имена папок, которые создаёт приёмник
DATE_DIR_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")                           # папка дня
FILE_RE = re.compile(r"^[0-9A-Za-z_.-]{1,96}\.(alog|acrash)$")             # серверное имя журнала
STATE_NAME = ".fetched.txt"                                                 # список забранного (пути от корня LogDrop)
KINDS = ("alog", "acrash")
SCAN_TIMEOUT_S = 120                                                        # список хранилища по SSH
SCP_TIMEOUT_S = 900                                                         # одна пачка scp
SCP_BATCH = 24                                                              # файлов (с их .json) в одной команде scp
_NO_WINDOW = 0x08000000                                                     # CREATE_NO_WINDOW: без всплывающих окон консоли

# Список хранилища на сервере: только папки и имена по правилам приёмника (проверяются ещё раз здесь, FILE_RE и др.), без reparse-точек
# (приёмник их не создаёт; встретилась — пропуск). Вывод — строки «логин \t дата \t имя \t размер».
_SCAN_PS = r"""
$root = '%s'
$kinds = @(%s)
Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue | ? { $_.Name -ne 'service' -and -not ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) } | ForEach-Object {
  $lg = $_
  Get-ChildItem -LiteralPath $lg.FullName -Directory -ErrorAction SilentlyContinue | ? { $_.Name -match '^\d{4}-\d{2}-\d{2}$' -and -not ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) } | ForEach-Object {
    $dy = $_
    Get-ChildItem -LiteralPath $dy.FullName -File -ErrorAction SilentlyContinue | ? { $kinds -contains $_.Extension.TrimStart('.').ToLower() } | ForEach-Object {
      $lg.Name + "`t" + $dy.Name + "`t" + $_.Name + "`t" + $_.Length
    }
  }
}
"""


def default_dest():
    """@@ASTORY_DEV_ROOT_ESC@@\\_player_logs — вне port (депо Perforce): данные игроков и присланное извне не попадут в сабмит
    (★ ревизия безопасности М12, 19.10.2026). Корень — asda_paths.ROOT."""
    if HERE not in sys.path:
        sys.path.insert(0, HERE)
    import logseal
    if logseal.ASDA_TOOLS not in sys.path:
        sys.path.insert(0, logseal.ASDA_TOOLS)
    import asda_paths
    return os.path.join(asda_paths.ROOT, "_player_logs")


def _kinds_ok(name, kinds):
    m = FILE_RE.match(name)
    return bool(m) and m.group(1) in kinds


def scan(src, kinds=KINDS):
    """Журналы локальной папки хранилища (--src) → [(логин, дата, имя)]; папки и имена проверяются по правилам приёмника, прочее пропускается."""
    out = []
    for login in sorted(os.listdir(src)):
        lp = os.path.join(src, login)
        if login == SERVICE_DIR or not os.path.isdir(lp) or not LOGIN_DIR_RE.match(login):
            continue
        for day in sorted(os.listdir(lp)):
            dp = os.path.join(lp, day)
            if not os.path.isdir(dp) or not DATE_DIR_RE.match(day):
                continue
            for name in sorted(os.listdir(dp)):
                if _kinds_ok(name, kinds) and os.path.isfile(os.path.join(dp, name)):
                    out.append((login, day, name))
    return out


def _exe(name):
    p = os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "System32", "OpenSSH", name + ".exe")
    return p if os.path.exists(p) else name


def ssh_powershell(script, timeout=SCAN_TIMEOUT_S):
    """PowerShell-скрипт на сервере по SSH (-EncodedCommand, UTF-16LE base64; вывод UTF-8) → stdout. Сбой — RuntimeError."""
    wrapped = "[Console]::OutputEncoding=[Text.Encoding]::UTF8;$ProgressPreference='SilentlyContinue';" + script
    enc = base64.b64encode(wrapped.encode("utf-16-le")).decode("ascii")
    cmd = [_exe("ssh"), "-i", SSH_KEY, "-o", "BatchMode=yes", "-o", "ConnectTimeout=15", SSH_HOST,
           "powershell -NoProfile -NonInteractive -EncodedCommand " + enc]
    try:
        r = subprocess.run(cmd, capture_output=True, timeout=timeout, creationflags=_NO_WINDOW)
    except subprocess.TimeoutExpired:
        raise RuntimeError("список хранилища по SSH не уложился в %d с" % timeout)
    except OSError as ex:
        raise RuntimeError("ssh не запустился: %s" % ex)
    if r.returncode != 0:
        raise RuntimeError("ssh, код %d: %s" % (r.returncode, r.stderr.decode("utf-8", "replace").strip()[:300]))
    return r.stdout.decode("utf-8", "replace")


def ssh_scan(kinds=KINDS, run=None):
    """Журналы хранилища на сервере по SSH → [(логин, дата, имя, размер)] (те же правила имён, что у scan). run(script) → stdout — подмена
    для самотеста."""
    kinds = tuple(k for k in kinds if k in KINDS)
    script = _SCAN_PS % (REMOTE_ROOT.replace("'", "''"), ",".join("'%s'" % k for k in kinds))
    out = []
    for ln in (run or ssh_powershell)(script).splitlines():
        parts = ln.strip("\r\n").split("\t")
        if len(parts) != 4:
            continue
        login, day, name, size = parts
        if login == SERVICE_DIR or not LOGIN_DIR_RE.match(login) or not DATE_DIR_RE.match(day) or not _kinds_ok(name, kinds) \
                or not size.isdigit():
            continue
        out.append((login, day, name, int(size)))
    return sorted(out)


def _remote(login, day, name):
    return "%s:%s/%s/%s/%s" % (SSH_HOST, REMOTE_ROOT.replace("\\", "/"), login, day, name)


def scp_get(sources, dest_dir, timeout=SCP_TIMEOUT_S):
    """scp с сервера нескольких файлов одной командой в dest_dir. Сбой — RuntimeError (часть файлов может быть уже скопирована)."""
    cmd = [_exe("scp"), "-q", "-i", SSH_KEY, "-o", "BatchMode=yes", "-o", "ConnectTimeout=15"] + list(sources) + [dest_dir]
    try:
        r = subprocess.run(cmd, capture_output=True, timeout=timeout, creationflags=_NO_WINDOW)
    except subprocess.TimeoutExpired:
        raise RuntimeError("scp не уложился в %d с" % timeout)
    except OSError as ex:
        raise RuntimeError("scp не запустился: %s" % ex)
    if r.returncode != 0:
        raise RuntimeError("scp, код %d: %s" % (r.returncode, r.stderr.decode("utf-8", "replace").strip()[:300]))


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def meta_sha(meta_path):
    """sha256 из .json приёмника → str | None (нет файла, не JSON, нет поля)."""
    try:
        with open(meta_path, encoding="utf-8") as f:
            v = json.load(f).get("sha256")
    except (OSError, ValueError, AttributeError):
        return None
    return v.lower() if isinstance(v, str) and re.match(r"^[0-9a-fA-F]{64}$", v) else None


def _rm(*paths):
    for x in paths:
        try:
            os.remove(x)
        except OSError:
            pass


def ssh_fetch(items, dest, get=None, log=print):
    """Новые файлы с сервера (items — [(логин, дата, имя[, размер])]) → dest\\логин\\дата\\ вместе с .json. Пачками по SCP_BATCH на папку дня.
    Целость:
      * размер копии = размеру из списка хранилища (ssh_scan, ★ ревизия 75 М-1: оборванная scp копия не принимается);
      * SHA-256 копии = sha256 из .json приёмника; не сошлось — копия и её .json стираются;
      * сбой пачки — каждый файл без .json или не того размера стирается и забирается заново вместе с .json; файл без .json после сбоя
        пачки не принимается (повторится в следующий запуск); без сбоя .json может не быть (копия папки его потеряла) — принимается с пометкой.
    get(sources, dest_dir) — подмена scp для самотеста. → (принятые [(логин, дата, имя)], ошибки [текст])."""
    get = get or scp_get
    ok, errs = [], []
    by_dir, sizes = {}, {}
    for it in items:
        by_dir.setdefault((it[0], it[1]), []).append(it[2])
        if len(it) > 3 and isinstance(it[3], int):
            sizes[(it[0], it[1], it[2])] = it[3]

    def whole(login, day, n, f):
        want = sizes.get((login, day, n))
        return os.path.isfile(f) and (want is None or os.path.getsize(f) == want)
    for (login, day), names in sorted(by_dir.items()):
        out_dir = os.path.join(dest, login, day)
        os.makedirs(out_dir, exist_ok=True)
        for i in range(0, len(names), SCP_BATCH):
            part = names[i:i + SCP_BATCH]
            src = []
            for n in part:
                src += [_remote(login, day, n), _remote(login, day, os.path.splitext(n)[0] + ".json")]
            failed = False
            try:
                get(src, out_dir)
            except RuntimeError as ex:
                failed = True
                for n in part:
                    f = os.path.join(out_dir, n)
                    meta = os.path.splitext(f)[0] + ".json"
                    if whole(login, day, n, f) and meta_sha(meta):
                        continue                              # пара целиком дошла до сбоя
                    _rm(f, meta)                              # обрывок или файл без .json — заново, парой
                    try:
                        get([_remote(login, day, n), _remote(login, day, os.path.splitext(n)[0] + ".json")], out_dir)
                    except RuntimeError as ex2:
                        if not whole(login, day, n, f):
                            _rm(f, meta)
                            errs.append("%s/%s/%s: не скопирован: %s" % (login, day, n, ex2))
                if not any(os.path.isfile(os.path.join(out_dir, n)) for n in part):
                    errs.append("%s/%s: пачка не скопирована: %s" % (login, day, ex))
            for n in part:
                f = os.path.join(out_dir, n)
                if not os.path.isfile(f):
                    continue
                meta = os.path.splitext(f)[0] + ".json"
                if not whole(login, day, n, f):
                    errs.append("%s/%s/%s: размер копии %d ≠ %d в списке хранилища — копия оборвана и стёрта" % (
                        login, day, n, os.path.getsize(f), sizes[(login, day, n)]))
                    _rm(f, meta)
                    continue
                want = meta_sha(meta)
                if want is not None and sha256_file(f) != want:
                    errs.append("%s/%s/%s: SHA-256 копии не равен .json приёмника — копия стёрта (порча или подмена в пути)" % (login, day, n))
                    _rm(f, meta)
                    continue
                if want is None and failed:
                    errs.append("%s/%s/%s: после сбоя копирования нет .json приёмника — копия не принята, повторится" % (login, day, n))
                    _rm(f, meta)
                    continue
                if want is None:
                    log("%s/%s/%s: .json приёмника нет — целость копии сверена только по размеру" % (login, day, n))
                ok.append((login, day, n))
    return ok, errs


def load_state(dest):
    """Множество уже забранного «логин/дата/имя»."""
    p = os.path.join(dest, STATE_NAME)
    if not os.path.isfile(p):
        return set()
    with open(p, "r", encoding="utf-8") as f:
        return {ln.strip() for ln in f if ln.strip()}


def add_state(dest, key):
    """Дописать «логин/дата/имя» в список забранного (по одной строке — прерванный запуск не теряет сделанное)."""
    with open(os.path.join(dest, STATE_NAME), "a", encoding="utf-8") as f:
        f.write(key + "\n")


def _decrypt_one(ls, dst_file, out_dir, log):
    try:
        return ls.decrypt_path(dst_file, out_dir)
    except Exception as ex:   # один битый файл не останавливает остальные
        log("%s: не расшифрован: %s" % (dst_file, ex))
        return 1


def fetch_new(kinds=KINDS, dest=None, src=None, all_=False, limit=None, log=print, ls=None, scan_run=None, get=None, listing=None):
    """Забрать новое и расшифровать → dict(dest, listed, new, fetched, errors[], decrypt_errors, per_login). src — локальная папка
    хранилища (иначе сервер по SSH). listing — уже снятый ssh_scan (второй раз по SSH не ходить). Общая точка для консоли (main) и раздела
    «Сбои клиента» панели (AdminCenter domains/crashes.py)."""
    if HERE not in sys.path:
        sys.path.insert(0, HERE)
    import logseal  # noqa: E402  (тонкая обёртка: путь к asda_logseal и asda_paths)
    ls = ls or logseal.load()
    dest = dest or default_dest()
    os.makedirs(dest, exist_ok=True)
    done = set() if all_ else load_state(dest)
    if src:
        listed = [(l, d, n) for l, d, n in scan(src, kinds)]
    else:
        listed = [tuple(x[:4]) for x in (listing if listing is not None else ssh_scan(kinds, scan_run)) if _kinds_ok(x[2], kinds)]
    items = [x for x in listed if "/".join(x[:3]) not in done]
    if limit:
        items = items[:int(limit)]
    res = {"dest": dest, "listed": len(listed), "new": len(items), "fetched": 0, "errors": [], "decrypt_errors": 0, "per_login": {}}
    if not items:
        return res
    if src:
        got = []
        for login, day, name in (x[:3] for x in items):
            out_dir = os.path.join(dest, login, day)
            os.makedirs(out_dir, exist_ok=True)
            src_file = os.path.join(src, login, day, name)
            shutil.copy2(src_file, os.path.join(out_dir, name))
            meta = os.path.splitext(src_file)[0] + ".json"
            if os.path.isfile(meta):
                shutil.copy2(meta, os.path.join(out_dir, os.path.splitext(name)[0] + ".json"))
            got.append((login, day, name))
    else:
        got, res["errors"] = ssh_fetch(items, dest, get=get, log=log)
    for login, day, name in got:
        out_dir = os.path.join(dest, login, day)
        e = _decrypt_one(ls, os.path.join(out_dir, name), out_dir, log)
        res["decrypt_errors"] += e
        res["per_login"][login] = res["per_login"].get(login, 0) + 1
        if e:                                                # ★ ревизия 75 К-3: не расшифрован — не «забран», следующий запуск возьмёт заново
            res.setdefault("undecrypted", []).append("/".join((login, day, name)))
            log("%s/%s/%s: не расшифрован — в список забранного не записан, следующий запуск заберёт заново" % (login, day, name))
            continue
        add_state(dest, "/".join((login, day, name)))
    res["fetched"] = len(got)
    # ★ 31.10.2026 (ротация журнала сессии): части одной сессии приходят под случайными серверными именами и в разные дни —
    #   склейка по сведениям сегментов, по всей папке логина → <логин>\sessions\<сессия>.joined.txt (пересобирается).
    for login in sorted(res["per_login"]):
        if not any(n.endswith(".alog") for l, _d, n in got if l == login):
            continue
        try:
            ls.join_paths([os.path.join(dest, login)], os.path.join(dest, login, "sessions"))
        except Exception as ex:   # склейка — удобство; её сбой не отменяет расшифровку
            log("%s: склейка частей не удалась: %s" % (login, ex))
    return res


def main(argv=None):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser(description="Забрать новые журналы игроков с dev-сервера (LogDrop, по SSH) и расшифровать")
    ap.add_argument("--src", default=os.environ.get("ASDA_LOGDROP") or None,
                    help="локальная папка LogDrop (по умолчанию — сервер по SSH, scp)")
    ap.add_argument("--dest", default=None, help="куда раскладывать (по умолчанию @@ASTORY_DEV_ROOT_ESC@@\\_player_logs)")
    ap.add_argument("--all", action="store_true", help="забрать и расшифровать всё заново (не смотреть список забранного)")
    ap.add_argument("--kinds", default=",".join(KINDS), help="что забирать: alog, acrash или оба через запятую")
    ap.add_argument("--limit", type=int, default=None, help="не больше N новых файлов за запуск")
    a = ap.parse_args(argv)
    kinds = tuple(k.strip() for k in a.kinds.split(",") if k.strip() in KINDS)
    if not kinds:
        print("--kinds: нужно alog, acrash или оба")
        return 2
    if a.src and not os.path.isdir(a.src):
        print("нет папки хранилища: %s" % a.src)
        return 2
    try:
        res = fetch_new(kinds, a.dest, a.src, a.all, a.limit)
    except RuntimeError as ex:
        print("сервер недоступен: %s" % ex)
        return 2
    where = a.src or ("%s (по SSH)" % REMOTE_ROOT)
    for e in res["errors"]:
        print(e)
    if not res["new"]:
        print("новых журналов нет (%s, всего %d)" % (where, res["listed"]))
        return 0
    print("\nзабрано %d из %d новых: %s; ошибок копирования %d, ошибок расшифровки %d → %s" % (
        res["fetched"], res["new"], ", ".join("%s %d" % kv for kv in sorted(res["per_login"].items())), len(res["errors"]),
        res["decrypt_errors"], res["dest"]))
    return 1 if res["errors"] or res["decrypt_errors"] else 0


if __name__ == "__main__":
    sys.exit(main())
