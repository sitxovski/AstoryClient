# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""ue_queue — общая очередь машины на UE-проект (10.11.2026, приказ владельца «переосмыслить структуру — не ждать 3 часа»).

Зачем. Все сессии работают в одном дереве `AstoryClient`. Сборка (UBT) невозможна, пока жив любой UnrealEditor
(самотест `-game` держит DLL), а cook пишет общий `Saved\\Cooked`. Раньше каждая сессия сама опрашивала tasklist —
самодельные циклы ожидания зависали (10.11: проверка «занято» находила СОБСТВЕННУЮ командную строку → 2 ч 47 мин простоя).
Теперь ожидание одно и общее, встроено в `deploy.py build-editor|build-dev|build-release` и `asda_selftest_batch.py`.

Правила (читатели/писатель, строгая очередь по времени заявки):
  * shared    — самотест `-game`: идёт вместе с другими самотестами, если перед ним в очереди нет exclusive;
  * exclusive — сборка редактора, cook/пакет Dev/Shipping: идёт один, когда перед ним нет никого.
Заявка — файл в QUEUE_DIR; заявка мёртвого PID удаляется любым ждущим. Для exclusive дополнительно ждём, пока не
закончатся UE-процессы ВНЕ очереди (редактор владельца, старые сессии) — по имени образа, не по командной строке.

  python ue_queue.py            — кто держит и кто ждёт (то же: deploy.py queue)
При импорте ничего не делает (закон tools-no-import-side-effects).
"""

import ctypes
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))     # <проект>\DEPLOY\_tools
QUEUE_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(HERE))), "Build", "_queue")   # <ROOT>\Build\_queue: заявки (файл на заявку)
UE_IMAGES = ("UnrealEditor.exe", "UnrealEditor-Cmd.exe", "LiveCodingConsole.exe", "UnrealBuildTool.exe",
             "ShaderCompileWorker.exe")                # процессы, мешающие exclusive (сборке/cook)
MAX_GAMES = int(os.environ.get("ASDA_UE_MAX_GAMES", "2"))   # потолок одновременных самотест-игр (каждая до 8 ГБ, ОЗУ 32 ГБ)
MIN_FREE_GB = float(os.environ.get("ASDA_UE_MIN_FREE_GB", "8"))   # новая игра не стартует рядом с другими, если свободной ОЗУ меньше
MAX_COOKED = int(os.environ.get("ASDA_UE_MAX_COOKED", "4"))   # потолок игр «regress…» (собранная игра 1,5–5 ГБ; редактор-игра 7 ГБ)
POLL_S = 10.0                                          # период проверки очереди, с
REPORT_S = 60.0                                        # как часто печатать «жду …», с


def pid_alive(pid):
    """Жив ли процесс pid (OpenProcess + GetExitCodeProcess; без psutil)."""
    k = ctypes.windll.kernel32
    h = k.OpenProcess(0x1000, False, int(pid))          # PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return False
    code = ctypes.c_ulong()
    ok = k.GetExitCodeProcess(h, ctypes.byref(code))
    k.CloseHandle(h)
    return bool(ok) and code.value == 259               # STILL_ACTIVE


def free_ram_gb():
    """Свободная физическая память, ГБ (GlobalMemoryStatusEx); -1 — не удалось узнать."""
    class MS(ctypes.Structure):
        _fields_ = [("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong)] + [
            (n, ctypes.c_ulonglong) for n in ("ullTotalPhys", "ullAvailPhys", "ullTotalPageFile", "ullAvailPageFile",
                                              "ullTotalVirtual", "ullAvailVirtual", "ullAvailExtendedVirtual")]
    m = MS()
    m.dwLength = ctypes.sizeof(MS)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m)):
        return -1.0
    return m.ullAvailPhys / 2 ** 30


def commit_pct():
    """Занятость памяти системы по commit (выделено / предел = ОЗУ + файл подкачки), %; -1 — не удалось узнать.
    ★ 10.11.2026: ускоритель сборки UBA перестаёт запускать новые компиляторы при 85 % (UbaHost.toml MemStartWaitPercent) —
    по журналам 181 сборки: до 65 % — 1,5 с на файл, 80 %+ — 6,5 с на файл (в 4 раза медленнее)."""
    class MS(ctypes.Structure):
        _fields_ = [("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong)] + [
            (n, ctypes.c_ulonglong) for n in ("ullTotalPhys", "ullAvailPhys", "ullTotalPageFile", "ullAvailPageFile",
                                              "ullTotalVirtual", "ullAvailVirtual", "ullAvailExtendedVirtual")]
    m = MS()
    m.dwLength = ctypes.sizeof(MS)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m)) or not m.ullTotalPageFile:
        return -1.0
    return 100.0 * (1 - m.ullAvailPageFile / m.ullTotalPageFile)


COMMIT_SLOW_PCT = 75.0   # выше — сборка заметно замедляется (см. commit_pct)


def memory_warning():
    """Строка-предупреждение о нехватке памяти для сборки или ''."""
    c = commit_pct()
    if c >= COMMIT_SLOW_PCT:
        return ("ВНИМАНИЕ: память системы занята на %.0f %% (commit) — сборка пойдёт в 3–5 раз медленнее: ускоритель UBA при 85 %% "
                "перестаёт запускать компиляторы. Закрыть тяжёлые программы (игры, браузер) или увеличить файл подкачки." % c)
    return ""


def queued_pids():
    """PID всех процессов под очередью: сами заявки и их дочерние игры/сборки (Slot.add_pid)."""
    out = set()
    for t in tickets():
        out.add(t["pid"])
        out.update(t.get("pids") or ())
    return out


def ue_processes():
    """[(образ, PID)] запущенных UE-процессов (tasklist по имени образа)."""
    try:
        out = subprocess.run(["tasklist", "/FO", "CSV", "/NH"], capture_output=True, timeout=30).stdout.decode("ascii", "replace")
    except Exception:
        return []
    names = {n.lower() for n in UE_IMAGES}
    res = []
    for ln in out.splitlines():
        f = [x.strip().strip('"') for x in ln.split('","')]
        if len(f) >= 2 and f[0].lower() in names and f[1].isdigit():
            res.append((f[0], int(f[1])))
    return res


def tickets():
    """Живые заявки, по порядку очереди; заявки мёртвых PID удаляются."""
    os.makedirs(QUEUE_DIR, exist_ok=True)
    res = []
    for fn in os.listdir(QUEUE_DIR):
        if not fn.endswith(".json"):
            continue
        p = os.path.join(QUEUE_DIR, fn)
        try:
            t = json.load(open(p, encoding="utf-8"))
        except (OSError, ValueError):
            continue
        if not pid_alive(t.get("pid", 0)):
            try:
                os.remove(p)
            except OSError:
                pass
            continue
        t["_path"] = p
        res.append(t)
    res.sort(key=lambda t: (t["seq"], t["pid"]))
    return res


def describe(t):
    """Строка заявки для людей."""
    age = (time.time() - t["t"]) / 60
    return "%s %s [%s] PID %d, %.0f мин%s" % ("ДЕРЖИТ" if t.get("held") else "ждёт", t["kind"], t["what"], t["pid"], age,
                                             ", сессия " + t["who"] if t.get("who") else "")


class Slot:
    """Заявка в очереди: with Slot("exclusive", "build-editor"): … — ждёт своей очереди, на выходе освобождает."""

    def __init__(self, kind, what, timeout_min=90.0, log=print):
        assert kind in ("shared", "exclusive")
        self.kind, self.what, self.timeout, self.log = kind, what, timeout_min * 60, log
        self.why_extra = ""
        self.path = None

    def __enter__(self):
        os.makedirs(QUEUE_DIR, exist_ok=True)
        seq = time.time_ns()
        self.ticket = {"seq": seq, "pid": os.getpid(), "kind": self.kind, "what": self.what, "t": time.time(),
                       "who": os.environ.get("ASDA_SESSION", ""), "held": False, "pids": []}
        self.path = os.path.join(QUEUE_DIR, "%d_%d.json" % (seq, os.getpid()))
        self._write()
        t0, last = time.time(), 0.0
        while True:
            ahead, outside = self._blockers()
            if not ahead and not outside and not self.why_extra:
                break
            if time.time() - t0 > self.timeout:
                self.__exit__(None, None, None)
                raise RuntimeError("очередь UE: не дождался за %.0f мин; мешают: %s" % (self.timeout / 60, self._why(ahead, outside)))
            if time.time() - last >= REPORT_S:
                self.log("очередь UE: %s ждёт (%.0f мин); мешают: %s" % (self.what, (time.time() - t0) / 60,
                                                                       "; ".join(x for x in (self._why(ahead, outside), self.why_extra) if x and x != "—")))
                last = time.time()
            time.sleep(POLL_S)
        self.ticket["held"] = True
        self._write()
        if time.time() - t0 > 5:
            self.log("очередь UE: %s начат после %.1f мин ожидания" % (self.what, (time.time() - t0) / 60))
        return self

    def _blockers(self):
        """(заявки впереди, которые мешают; UE-процессы вне очереди, которые мешают)."""
        all_t = tickets()
        mine = (self.ticket["seq"], self.ticket["pid"])
        ahead = [t for t in all_t if (t["seq"], t["pid"]) < mine]
        self.why_extra = ""
        if self.kind == "shared":
            games = [t for t in ahead if t["kind"] == "shared"]          # идут или стоят передо мной
            # ★ 11.11.2026 02:1x: ЖДУЩИЙ «regress…» впереди в потолок игр не считается — он уступает сборкам и может стоять
            #   сколько угодно; иначе вторая взаимоблокировка (самотест ждёт место за regress, сборка ждёт самотест,
            #   regress ждёт сборку — очередь стояла 75 мин при потолке 2).
            cap = MAX_GAMES
            if not self.what.startswith("regress"):
                games = [t for t in games if t.get("held") or not str(t.get("what", "")).startswith("regress")]
            else:
                # ★ 11.11.2026: у собранных игр свой потолок (4) и считаются только они сами — память сторожит проверка ниже
                cap = MAX_COOKED
                games = [t for t in games if str(t.get("what", "")).startswith("regress")]
            running = [t for t in games if t.get("held")]
            ahead = [t for t in ahead if t["kind"] == "exclusive"]
            # ★ 10.11.2026 (R2): фоновые прогоны (заявка «regress …» — эталон/регресс в готовой cooked-сборке, от дерева не
            #   зависят) уступают ЛЮБОЙ ждущей сборке, даже вставшей в очередь позже: иначе каждая сборка трёх пишущих сессий
            #   ждала по 20 мин конца очередной пачки из 25 самотестов.
            if self.what.startswith("regress"):
                ahead = [t for t in all_t if t["kind"] == "exclusive"]
            # ★ 10.11.2026: потолок одновременных игр — 4-я и далее только душат диск и память (старт 20 с → 2–8 мин)
            # ★ 11.11.2026 14:5x: собранная игра не стартует, пока идут ДВЕ редактор-игры (по 7 ГБ) — вместе с ней память
            #   кончалась совсем (свободно 0,1 ГБ, все три шли в разы медленнее).
            ed_running = [t for t in all_t if t["kind"] == "shared" and t.get("held") and not str(t.get("what", "")).startswith("regress")]
            if self.what.startswith("regress") and len(ed_running) >= MAX_GAMES:
                self.why_extra = "идут %d редактор-игры — собранная игра ждёт" % len(ed_running)
            elif len(games) >= cap:
                self.why_extra = "уже %d игр из %d разрешённых" % (len(games), cap)
            elif running and 0 <= free_ram_gb() < MIN_FREE_GB:
                self.why_extra = "свободной памяти %.1f ГБ < %.0f ГБ при %d идущих играх" % (free_ram_gb(), MIN_FREE_GB, len(running))
        outside = []
        if self.kind == "exclusive":
            # ★ 10.11.2026 22:5x: ЖДУЩАЯ заявка «regress…» сборке не мешает (она сама уступает всем сборкам) — иначе
            #   взаимоблокировка: regress ждёт сборку позади себя, сборка ждёт regress впереди (очередь стояла 109 мин).
            ahead = [t for t in ahead if not (t["kind"] == "shared" and not t.get("held") and str(t.get("what", "")).startswith("regress"))]
            queued = set()
            for t in all_t:
                queued.add(t["pid"])
                queued.update(t.get("pids") or ())
            # дочерняя игра заявки, стоящей ПЕРЕДО мной, уже учтена самой заявкой; чужая — мешает
            outside = [(n, p) for n, p in ue_processes() if p not in queued]
        else:  # самотест не стартует посреди чужой сборки вне очереди
            outside = [(n, p) for n, p in ue_processes() if n.lower() == "unrealbuildtool.exe"]
        return ahead, outside

    @staticmethod
    def _why(ahead, outside):
        """Кто мешает — для журнала."""
        parts = [describe(t) for t in ahead]
        parts += ["%s PID %d вне очереди (редактор владельца или старый запуск?)" % (n, p) for n, p in outside]
        return "; ".join(parts) or "—"

    def add_pid(self, pid):
        """Записать в заявку PID дочернего процесса (игра самотеста): другие видят, что он под очередью, а не «чужой»."""
        self.ticket.setdefault("pids", []).append(int(pid))
        if self.path:
            self._write()

    def _write(self):
        """Записать заявку атомарно."""
        tmp = self.path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self.ticket, f, ensure_ascii=False)
        # ★ 15.11.2026: под Windows os.replace даёт WinError 5, если заявку в этот миг читает другой процесс очереди
        #   (tickets()). Раньше исключение вылетало из add_pid, заявка оставалась held=true при живом PID прогона и
        #   вечно держала очередь (rx_r4 простоял 6 ч). Повторяем; не вышло — заявка остаётся прежней (она уже верна
        #   по held, теряется только список pids), а .tmp убираем.
        for attempt in range(40):
            try:
                os.replace(tmp, self.path)
                return
            except PermissionError:
                time.sleep(0.05)
        try:
            os.remove(tmp)
        except OSError:
            pass
        if not os.path.exists(self.path):
            raise RuntimeError("очередь UE: не удалось записать заявку %s" % self.path)

    def __exit__(self, *exc):
        if self.path:
            # ★ 15.11.2026: снятие заявки тоже с повтором — файл может читать другой процесс очереди (WinError 5/32);
            #   неснятая заявка held=true при живом PID держит очередь вечно.
            for attempt in range(40):
                try:
                    os.remove(self.path)
                    break
                except FileNotFoundError:
                    break
                except OSError:
                    time.sleep(0.05)
            try:
                os.remove(self.path + ".tmp")
            except OSError:
                pass
            self.path = None
        return False


def status_lines():
    """Состояние очереди и UE-процессов вне очереди — строки."""
    all_t = tickets()
    queued = queued_pids()
    lines = [describe(t) for t in all_t] or ["очередь пуста"]
    lines.append("свободной памяти %.1f ГБ, занято по commit %.0f %%; потолок игр %d" % (free_ram_gb(), commit_pct(), MAX_GAMES))
    if memory_warning():
        lines.append(memory_warning())
    out = [(n, p) for n, p in ue_processes() if p not in queued]
    lines += ["вне очереди: %s PID %d" % (n, p) for n, p in out]
    return lines


def main():
    """Печать состояния очереди."""
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    for ln in status_lines():
        print(ln)
    return 0


if __name__ == "__main__":
    sys.exit(main())
