# Copyright (c) 2026 @sitxovski (Discord)
# run_headless_safe.py — ЕДИНСТВЕННЫЙ правильный способ гонять headless-редактор в этом проекте.
#
# ★★★ ЗАЧЕМ №1 (ЗОМБИ-ХВОСТ). UnrealEditor-Cmd.exe РЕГУЛЯРНО НЕ ВЫХОДИТ после того, как python-
#   скрипт отработал: в логе уже стоит финальная строка и «Shutting down PIE», а процесс живёт ещё
#   минутами, держит 5-6 ГБ и блокирует сборку (LNK1104) и запись .uasset. Замеры 23.08.2026: сам
#   прогон 2 минуты, зомби-хвост 6 минут — три четверти времени уходило в никуда.
#
# ★★★ ЗАЧЕМ №2 (НЕДОСТОВЕРНЫЙ КОД ВОЗВРАТА — доказано 31.08.2026). Даже когда процесс ВЫХОДИТ сам,
#   он почти всегда падает с 0xC0000005 (access violation) УЖЕ ПОСЛЕ строки лога «Log file closed»,
#   т.е. в teardown статических деструкторов / выгрузке сторонних DLL, вне кода UE. В bash это
#   RC=139 (signal 11), в Python subprocess — returncode -1073741819. Воспроизводится даже пустым
#   no-op скриптом и НЕ зависит от -NullRHI, от импорта DataTable и от наших скриптов (матрица в
#   Reports/headless_commandlet_crash.md). Вывод: КОД ВОЗВРАТА ПРОЦЕССА НЕЛЬЗЯ использовать как
#   признак успеха — «отработало и упало на выходе» неотличимо от «упало и НЕ отработало».
#
# ★ КАК ЛЕЧИМ. Успех определяем НЕ по коду возврата, а ПО ЛОГУ:
#     • нет фатальных строк (appError/Assertion/Fatal error/Exiting abnormally) ДО завершения;
#     • найден МАРКЕР скрипта (--marker), а если маркера нет — найдена строка чистого выхода UE;
#     • если задан --require <regex> — он присутствует (например «ИТОГО: импортировано» у импорта);
#     • если commandlet напечатал «Success - N error(s)» — N == 0.
#   Итог пишем в <log>.result.json (для автоматизации/агентов/CI) и в осмысленный код возврата:
#     0 — успех; 3 — провал по логу; 4 — таймаут без успеха; 2 — нет файла скрипта.
#   Дождавшись маркера (или простоя лога дольше --idle) — добиваем процесс по PID: все данные к
#   этому моменту уже на диске, а хвост/аварийный выход на выходе нам не интересен.
#
# Использование:
#   python Tools/run_headless_safe.py <скрипт.py> [--log <путь>] [--marker "ЗАВЕРШЕНО"]
#                                     [--require "regex"] [--timeout 900] [--idle 90] [--exec-cmds]
#   --exec-cmds  запускать через -ExecCmds="py ..." (нужно, когда скрипту требуется RHI/PIE),
#                иначе через -ExecutePythonScript + -NullRHI. ⚠ Без --render-offscreen поднимает ОКНО редактора.
#   --render-offscreen  (★ Т1, 11.10.2026) только с --exec-cmds: ключ -RenderOffscreen — редактор с живым RHI/PIE и
#                БЕЗ окна (закон no-visible-editor-windows). Так запускаются живые .bat-проверки PIE (run_headless_*.bat).
#   --res WxH    (★ Т1) размер вьюпорта: -ResX=W -ResY=H -WinX=0 -WinY=0 (как было в .bat); не задан — ключей нет.
#   --print-cmd  (★ Т1) сухой режим: напечатать итоговую командную строку и выйти (код 0) — UE не запускается,
#                лог и result.json не трогаются.
#   --quit-after (★ С6, 11.10.2026) только с --exec-cmds: -ExecCmds="py <скрипт>,quit" — синхронный скрипт отработал →
#                редактор выходит сам (замена -ExecutePythonScript, когда нужен живой RHI: run_headless_rhi.bat).
#   --commandlet-rhi  (сет C-2, 05.09.2026) коммандлет -run=pythonscript -script=... с -AllowCommandletRendering: НАСТОЯЩИЙ RHI
#                (шейдеры компилируются, MaterialEditingLibrary.get_statistics даёт ps>0) и НИ ОДНОГО окна — в отличие от
#                --exec-cmds, который поднимает окно редактора (закон no-visible-editor-windows). Проверка «материал не серый».
#   --expect-asset /Game/...  (27.09.2026) ассет, который прогон обязан записать; дата файла проверяется.
#                (10.10.2026) в result.json written_assets — ВСЕ записанные /Game-пакеты по логу сохранения
#                (source=saved) плюс объявленные (source=declared); каждый проверяется по дате файла.
#   ★ 27.09.2026: ошибки СОХРАНЕНИЯ (LogSavePackage: Error, Failed to save, Error saving …) — ПРОВАЛ, даже
#     при найденном маркере; строки «ASDA_WRITES_ASSET: /Game/...» из лога — объявления для проверки даты.
#   ⚠ У РАЗНЫХ скриптов РАЗНЫЙ маркер: constellation печатает «ЗАВЕРШЕНО» (совпадает с дефолтом),
#     а _import_one_itemdb_table печатает «ИТОГО: импортировано» — для него передавай
#     --marker "ИТОГО: импортировано" (или --require "ИТОГО: импортировано \d+ / \d+").
import argparse, json, os, re, shutil, subprocess, sys, time

# ★ Волна 3 п.15 (15.09.2026): командная строка, запуск и добивание — общий каркас asda_pie_harness
#   (без побочных эффектов при импорте). Вердикт ПО ЛОГУ остаётся здесь — единственный источник.
import asda_pie_harness as PH

# ★ Консоль Windows бывает в cp1251 и роняет print на не-кириллических глифах («─», «★»)
#   (грабля [[tools-no-import-side-effects]]/cp1251). Печать делаем устойчивой к кодировке.
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

# Прежние имена оставлены: ими пользуются set0_run_game.py и драйверы прогонов в Reports
UE = PH.UE_CMD
PROJ = PH.UPROJECT

# Фатальные строки движка = НАСТОЯЩИЙ провал прогона (не тот безобидный AV на выходе).
FATAL_RE = re.compile(
    r"appError called|Assertion failed:|Fatal error|Exiting abnormally|"
    r"Unhandled Exception|LogOutputDevice: Error:|LogWindows: Error:",
    re.IGNORECASE)
# Признак того, что UE довёл СВОЙ выход до конца (AV случается уже ПОСЛЕ этой строки).
CLEAN_EXIT_RE = re.compile(r"LogExit: Exiting\.|Log file closed")
# Итог python-commandlet: «Success - N error(s), M warning(s)».
COMMANDLET_RE = re.compile(r"Success - (\d+) error\(s\)")

# ★★★ ОШИБКИ СОХРАНЕНИЯ АССЕТА = ПРОВАЛ (27.09.2026). Раньше обёртка говорила «УСПЕХ», хотя пакет не
#   записался: соседний `-game` держал .uasset, UE печатал `LogSavePackage: Error: Error saving ...`, а маркер
#   скрипта всё равно выходил. Список собран по всем логам Reports и Saved/Logs (27.09.2026):
#     LogSavePackage: Error: Error saving '<файл>'            — 700+ случаев, главный признак;
#     LogSavePackage: Error: Cannot remove '<файл>' as it is read only!;
#     LogSavePackage: Warning: Failed to move '<временный>' to '<файл>' — спутник «Error saving»;
#     LogFileManager: Error: Error moving file '...uasset/.umap' — перенос временного файла на место ассета;
#     LogEditorAssetSubsystem: Error: SaveLoadedAsset failed / SaveDirectory failed / SaveAsset failed;
#     «FAILED to save» / «Failed to save» — печать наших скриптов.
#   НЕ считаются: `LogFileManager: Error: Error deleting file '...Saved/Config/...ini'` — это сброс настроек
#   редактора при выходе, к ассетам отношения не имеет (52 случая во вполне успешных прогонах).
SAVE_ERROR_RE = re.compile(
    r"LogSavePackage: Error:[^\r\n]*|"
    r"LogSavePackage: Warning: Failed to move[^\r\n]*|"
    r"LogFileManager: Error: Error moving file[^\r\n]*?\.(?:uasset|umap|uexp|ubulk)[^\r\n]*|"
    r"LogEditorAssetSubsystem: Error: Save\w* failed[^\r\n]*|"
    r"Failed to save[^\r\n]*|"
    r"Error saving '[^\r\n]*",
    re.IGNORECASE)
# ★ Объявление записываемого ассета: скрипт печатает «ASDA_WRITES_ASSET: /Game/...» (так делает
#   _import_one_itemdb_table.py), или вызывающий передаёт --expect-asset. Обёртка проверяет, что файл
#   .uasset/.umap обновился за время прогона: сохранение бывает молча непрошедшим (закон
#   [[asset-save-blocked-by-running-game]] — «проверять дату файла, не сообщение скрипта»).
DECLARED_ASSET_RE = re.compile(r"ASDA_WRITES_ASSET:\s*(\S+)")
# ★ 10.10.2026 (ревизия копания, МЕЛК-2): пакеты, которые движок РЕАЛЬНО записал за прогон — строки
#   «LogSavePackage: Moving output files for package: /Game/...». Раньше `written_assets` содержал только
#   объявленные (один --expect-asset → один ассет из четырёх записанных). Теперь учитываются все записанные.
SAVED_PACKAGE_RE = re.compile(r"LogSavePackage: Moving output files for package:\s*(\S+)")
CONTENT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Content"


# ★★★ «Handled ensure» — НЕ ПРОВАЛ (правка 31.08.2026). UE печатает обработанный `ensure()`
#   блоком строк `LogOutputDevice: Error:` (заголовок «=== Handled ensure: ===», условие, текст и
#   весь коллстек), но выполнение ПРОДОЛЖАЕТСЯ — это предупреждение, а не аварийная остановка.
#   У нашего импорта UI такой ensure стабильный и давний (`WidgetVariableNameToGuidMap.Contains` —
#   «Widget [...] was added but did not get a GUID», встречается во всех прежних логах импорта,
#   напр. `headless_asda_jui_generic_umg.log`). Пока он считался фатальным, обёртка ДОБИВАЛА
#   редактор на середине пост-импорта — прогон рвался, хотя ничего не падало.
#   Настоящие падения ловят остальные шаблоны `FATAL_RE` (appError / Assertion failed /
#   Fatal error / Exiting abnormally / Unhandled Exception / LogWindows: Error:), их правка не
#   трогает. Вырезаем ТОЛЬКО строки `LogOutputDevice: Error:`, идущие подряд после заголовка.
_ENSURE_HEAD_RE = re.compile(r"=== Handled ensure: ===")
_ENSURE_LINE_RE = re.compile(r"LogOutputDevice: Error:")
# Строка лога UE начинается с метки времени «[2026.08.31-16.50.45:725][  2]». Строка БЕЗ метки —
#   продолжение предыдущей (UE так печатает многострочный текст ensure и «Script Stack»), поэтому
#   она блок НЕ закрывает: раньше именно такая строка рвала блок, и остаток коллстека ensure
#   принимался за фатал.
_TS_RE = re.compile(r"^\[\d{4}\.\d{2}\.\d{2}-")


def strip_handled_ensures(text, assume_in_block=False):
    """Убрать блоки обработанных `ensure()` — они не признак провала прогона.

    Блок начинается строкой «=== Handled ensure: ===» и тянется до первой строки С МЕТКОЙ
    ВРЕМЕНИ, которая уже не является `LogOutputDevice: Error:` (то есть до возврата к обычному
    логу). Строки без метки времени — продолжения текста ensure — остаются внутри блока.

    `assume_in_block=True` — текст является ХВОСТОМ лога и мог начаться ВНУТРИ блока (заголовок
    остался выше окна). Тогда ведущие строки ensure тоже отбрасываются: иначе живая проверка
    хвоста видит голый коллстек и добивает редактор на середине работы (наблюдалось 31.08.2026 —
    прогон рвался на 49-й секунде, хотя по ПОЛНОМУ логу фатала нет).
    """
    out = []
    in_block = bool(assume_in_block)
    for ln in text.splitlines(True):
        if _ENSURE_HEAD_RE.search(ln):
            in_block = True
            continue
        if in_block:
            if _ENSURE_LINE_RE.search(ln) or not _TS_RE.match(ln):
                continue                      # тело/коллстек ensure и его продолжения
            in_block = False                  # обычная строка лога — блок закончился
        out.append(ln)
    return "".join(out)


def tail_size(p):
    try:
        return os.path.getsize(p)
    except OSError:
        return -1


def read_log(path):
    try:
        with open(path, "rb") as f:
            return f.read().decode("utf-8", "replace")
    except OSError:
        return ""


def verdict(text, marker, require):
    """Возвращает (ok: bool, reasons: list[str], facts: dict) — успех определяется ПО ЛОГУ."""
    facts = {}
    reasons = []
    ok = True

    # ★ Обработанные `ensure()` из текста вычеркнуты — см. strip_handled_ensures().
    fatal = FATAL_RE.search(strip_handled_ensures(text))
    facts["fatal"] = fatal.group(0) if fatal else None
    if fatal:
        ok = False
        reasons.append("фатальная строка в логе: %r" % fatal.group(0))

    facts["clean_exit"] = bool(CLEAN_EXIT_RE.search(text))

    facts["marker"] = marker
    facts["marker_found"] = (marker in text) if marker else None
    if marker:
        if marker not in text:
            ok = False
            reasons.append("маркер %r НЕ найден" % marker)
    else:
        # маркера не задали — тогда доказательством служит чистый выход UE
        if not facts["clean_exit"]:
            ok = False
            reasons.append("нет ни маркера, ни строки чистого выхода UE")

    facts["require"] = require
    if require:
        m = re.search(require, text)
        facts["require_found"] = bool(m)
        if not m:
            ok = False
            reasons.append("--require %r НЕ найден" % require)

    m = COMMANDLET_RE.search(text)
    facts["commandlet_errors"] = int(m.group(1)) if m else None
    if m and int(m.group(1)) > 0:
        ok = False
        reasons.append("commandlet: %s error(s)" % m.group(1))

    # ★ 27.09.2026: ошибки сохранения ассета — провал, даже если маркер скрипта найден (см. SAVE_ERROR_RE).
    save_errs = []
    for sm in SAVE_ERROR_RE.finditer(text):
        s = sm.group(0).strip()
        if s not in save_errs:
            save_errs.append(s)
    facts["save_errors"] = save_errs[:20]
    if save_errs:
        ok = False
        reasons.append("ошибки сохранения ассета: %d (первая: %s)" % (len(save_errs), save_errs[0][:200]))

    return ok, reasons, facts


def asset_file_for(path):
    """'/Game/A/B' или '/Game/A/B.B' или путь к файлу → существующий .uasset/.umap (или ожидаемый .uasset)."""
    p = path.strip().strip("'\"")
    if p.startswith("/Game/"):
        rel = p[len("/Game/"):].split(".", 1)[0]
        base = os.path.join(CONTENT_DIR, rel)
        for ext in (".uasset", ".umap"):
            if os.path.isfile(base + ext):
                return os.path.normpath(base + ext)
        return os.path.normpath(base + ".uasset")
    return os.path.normpath(os.path.abspath(p))


def check_written_assets(text, extra, t_start):
    """Объявленные ассеты (лог «ASDA_WRITES_ASSET:» + --expect-asset) обязаны обновиться после t_start.

    Возвращает (ok, reasons, facts_list). Нет объявлений и записей → (True, [], []): проверка не применяется.
    ★ 10.10.2026: в список входят и ВСЕ пакеты из «LogSavePackage: Moving output files for package:» (поле
    "source": "declared" — объявлен скриптом/--expect-asset, "saved" — только по логу сохранения). Необновлённый
    файл записанного /Game-пакета — тоже провал (сохранение отчиталось, а на диске старая дата).
    """
    declared = []   # объявленные пути: --expect-asset + «ASDA_WRITES_ASSET:»
    for d in list(extra or []) + DECLARED_ASSET_RE.findall(text):
        if d not in declared:
            declared.append(d)
    declared_files = set(os.path.normcase(asset_file_for(d)) for d in declared)  # их файлы — для дедупликации
    saved = []      # записанные по логу сохранения и не объявленные
    for d in SAVED_PACKAGE_RE.findall(text):
        if not d.startswith("/Game/"):
            continue        # /Engine, плагины, /Temp — вне Content проекта, дату не проверяем
        if d in saved or os.path.normcase(asset_file_for(d)) in declared_files:
            continue
        saved.append(d)
    ok, reasons, rows = True, [], []
    for d, src in [(x, "declared") for x in declared] + [(x, "saved") for x in saved]:
        f = asset_file_for(d)
        try:
            mt = os.path.getmtime(f)
        except OSError:
            mt = None
        fresh = mt is not None and mt >= t_start - 2.0     # 2 с запаса на округление ФС
        rows.append({"asset": d, "file": f, "mtime": (time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(mt))
                                                       if mt is not None else None), "updated": fresh,
                     "source": src})
        if not fresh:
            ok = False
            reasons.append("ассет НЕ обновлён за прогон: %s (%s, дата %s)" % (
                d, f, rows[-1]["mtime"] or "файла нет"))
    return ok, reasons, rows


# ★★★ СНИМОК ПОЛЬЗОВАТЕЛЬСКИХ ФАЙЛОВ РЕДАКТОРА ВЛАДЕЛЬЦА ПЕРЕД КАЖДЫМ ЗАПУСКОМ (19.09.2026).
#   Консольный редактор при каждом прогоне перезаписывает пользовательские настройки редактора владельца —
#   `Saved/Config/WindowsEditor/*.ini` проекта и глобальные `%LOCALAPPDATA%/UnrealEngine/5.8/...`. Копии «до» не
#   было никогда, и трижды за неделю приходилось гадать, цел ли файл владельца. Раннер НЕ восстанавливает их
#   сам: владелец может работать в редакторе параллельно, и откат затёр бы его свежие правки. Снимок — это
#   гарантированная копия «до» для сверки и ручного возврата. Хранятся последние 30 снимков.
_EDITOR_CFG_DIRS = [
    r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Config/WindowsEditor",
    os.path.join(os.environ.get("LOCALAPPDATA", ""), "UnrealEngine", "5.8", "Saved", "Config", "WindowsEditor"),
]
_EDITOR_CFG_SNAP_ROOT = r"@@ASTORY_DEV_ROOT_FWD@@/_backup/_editor_cfg"
_EDITOR_CFG_KEEP = 30


def snapshot_owner_editor_cfg():
    """Снять копию пользовательских .ini редактора владельца ДО запуска. Возвращает путь снимка или ''."""
    stamp = time.strftime("%Y-%m-%d_%H-%M-%S")
    dst_root = os.path.join(_EDITOR_CFG_SNAP_ROOT, stamp)
    copied = 0
    for i, d in enumerate(_EDITOR_CFG_DIRS):
        if not d or not os.path.isdir(d):
            continue
        tag = "project" if i == 0 else "engine_localappdata"
        for name in os.listdir(d):
            if not name.lower().endswith(".ini"):
                continue
            src = os.path.join(d, name)
            if not os.path.isfile(src):
                continue
            dst_dir = os.path.join(dst_root, tag)
            os.makedirs(dst_dir, exist_ok=True)
            shutil.copy2(src, os.path.join(dst_dir, name))
            copied += 1
    if not copied:
        return ""
    # чистка: оставить последние _EDITOR_CFG_KEEP снимков
    try:
        snaps = sorted(s for s in os.listdir(_EDITOR_CFG_SNAP_ROOT)
                       if os.path.isdir(os.path.join(_EDITOR_CFG_SNAP_ROOT, s)))
        for old in snaps[:-_EDITOR_CFG_KEEP]:
            shutil.rmtree(os.path.join(_EDITOR_CFG_SNAP_ROOT, old), ignore_errors=True)
    except OSError:
        pass
    return dst_root


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("script")
    ap.add_argument("--log", default="")
    ap.add_argument("--marker", default="ЗАВЕРШЕНО")
    ap.add_argument("--require", default="")                   # доп. regex, обязателен для успеха
    ap.add_argument("--timeout", type=float, default=900.0)    # общий потолок, с
    ap.add_argument("--idle", type=float, default=90.0)        # лог не растёт столько — считаем, что всё
    ap.add_argument("--exec-cmds", action="store_true")
    ap.add_argument("--commandlet-rhi", action="store_true")   # ★ сет C-2: живой RHI без окон (см. шапку)
    # ★ 27.09.2026: ассет, который прогон обязан записать (/Game/... или путь к файлу); можно несколько раз.
    #   Дополняет объявления «ASDA_WRITES_ASSET:» из лога скрипта; дата файла обязана обновиться за прогон.
    ap.add_argument("--expect-asset", action="append", default=[])
    # ★ Т1 (11.10.2026): скрытый рендер для --exec-cmds, размер вьюпорта и сухая печать командной строки (см. шапку).
    ap.add_argument("--render-offscreen", action="store_true")   # -RenderOffscreen: живой RHI/PIE без окна
    ap.add_argument("--res", default="")                         # WxH → -ResX/-ResY/-WinX=0/-WinY=0; пусто — без ключей
    ap.add_argument("--print-cmd", action="store_true")          # только напечатать командную строку, UE не запускать
    # ★ С6 (11.10.2026): с --exec-cmds — `quit` сразу после синхронного скрипта (замена -ExecutePythonScript с живым RHI)
    ap.add_argument("--quit-after", action="store_true")
    a = ap.parse_args()

    script = os.path.abspath(a.script)          # ★ путь ОБЯЗАН быть абсолютным: относительный
    if not os.path.isfile(script):              #   движок резолвит от Engine/Binaries/Win64
        print("нет файла скрипта: %s" % script); return 2
    if a.render_offscreen and (not a.exec_cmds or a.commandlet_rhi):
        print("--render-offscreen — только вместе с --exec-cmds (без --commandlet-rhi)"); return 2
    if a.quit_after and (not a.exec_cmds or a.commandlet_rhi):
        print("--quit-after — только вместе с --exec-cmds (без --commandlet-rhi)"); return 2
    extra = []                                  # ★ Т1: ключи размера вьюпорта (форма прежних .bat)
    if a.res:
        try:
            rx, ry = [int(v) for v in a.res.lower().split("x")]
        except ValueError:
            print("--res %r: ожидается ШИРИНАxВЫСОТА, напр. 1920x1200" % a.res); return 2
        extra = ["-ResX=%d" % rx, "-ResY=%d" % ry, "-WinX=0", "-WinY=0"]
    log = os.path.abspath(a.log) if a.log else os.path.join(
        r"@@ASTORY_DEV_ROOT_FWD@@/Reports", "headless_%s.log" % os.path.splitext(os.path.basename(script))[0])
    result_json = log + ".result.json"

    # ★ Волна 3 п.15: форма командной строки — PH.editor_python_cmd (символ в символ прежняя; ключ
    #   -SCCProvider=None и его обоснование — там же, сет C-1). ★ Т1: без --render-offscreen/--res — та же строка.
    cmd = PH.editor_python_cmd(script, log, exec_cmds=a.exec_cmds, commandlet_rhi=a.commandlet_rhi,
                               render_offscreen=a.render_offscreen, extra_args=extra, quit_after=a.quit_after)
    if a.print_cmd:                             # ★ Т1: сухой режим — ни запуска, ни удаления лога
        print("КОМАНДНАЯ СТРОКА: %s" % subprocess.list2cmdline(cmd))
        print("   маркер=%r  require=%r  лог=%s" % (a.marker, a.require, log))
        return 0

    for stale in (log, result_json):
        try:
            os.remove(stale)
        except OSError:
            pass

    # ★ Снимок пользовательских настроек редактора владельца ДО запуска (см. `snapshot_owner_editor_cfg`).
    snap = snapshot_owner_editor_cfg()
    if snap:
        print("снимок настроек редактора владельца: %s" % snap)

    t0 = time.time()
    proc = PH.popen_quiet(cmd)
    print("запущен PID %d, лог %s" % (proc.pid, log))

    last_size, last_change, done = -1, time.time(), False
    self_exited = False
    while True:
        if proc.poll() is not None:             # вышел сам (обычно с AV на выходе — это норма)
            self_exited = True
            print("процесс вышел сам за %.1f с (returncode=%s)" % (time.time() - t0, proc.returncode))
            break
        sz = tail_size(log)
        if sz != last_size:
            last_size, last_change = sz, time.time()
        if sz > 0 and not done:
            tail = ""
            try:
                with open(log, "rb") as f:
                    f.seek(max(0, sz - 65536))
                    tail = f.read().decode("utf-8", "replace")
            except OSError:
                pass
            if a.marker and a.marker in tail:
                done = True
                print("маркер «%s» найден за %.1f с — добиваю процесс" % (a.marker, time.time() - t0))
            elif FATAL_RE.search(strip_handled_ensures(tail, assume_in_block=True)):
                done = True
                print("фатальная строка в логе за %.1f с — добиваю процесс" % (time.time() - t0))
        if done or (time.time() - last_change) > a.idle or (time.time() - t0) > a.timeout:
            why = ("маркер/фатал" if done
                   else ("лог не растёт %.0f с" % a.idle if (time.time() - last_change) > a.idle
                         else "потолок %.0f с" % a.timeout))
            print("завершаю по причине: %s (всего %.1f с)" % (why, time.time() - t0))
            try:
                proc.kill()
            except Exception:
                pass
            break
        time.sleep(1.0)

    proc_rc = proc.returncode
    # добиваем возможные хвосты этого же запуска
    PH.kill_pid(proc.pid)

    # ── ВЕРДИКТ ПО ЛОГУ (не по коду возврата) ──────────────────────────────────────────────
    elapsed = time.time() - t0
    timed_out = (not done) and (not self_exited) and (elapsed > a.timeout)
    text = read_log(log)
    ok, reasons, facts = verdict(text, a.marker, a.require)
    # ★ 27.09.2026: объявленные ассеты должны обновиться на диске (не верить одному сообщению скрипта).
    a_ok, a_reasons, a_rows = check_written_assets(text, a.expect_asset, t0)
    facts["written_assets"] = a_rows
    if not a_ok:
        ok = False
        reasons.extend(a_reasons)
    if timed_out and not ok:
        reasons.insert(0, "таймаут %.0f с без маркера" % a.timeout)

    # AV на выходе (0xC0000005 / -1073741819) — известный безобидный хвост, если по логу всё ок.
    av_on_exit = self_exited and proc_rc not in (0, None)
    exit_code = 0 if ok else (4 if timed_out else 3)

    result = {
        "ok": ok,
        "exit_code": exit_code,
        "script": script,
        "log": log,
        "elapsed_sec": round(elapsed, 1),
        "process_returncode": proc_rc,           # ★ НЕДОСТОВЕРЕН — только для справки
        "av_on_exit_ignored": bool(av_on_exit and ok),
        "self_exited": self_exited,
        "timed_out": timed_out,
        "reasons": reasons,
        "facts": facts,
    }
    try:
        with open(result_json, "w", encoding="utf-8") as f:
            json.dump(result, f, ensure_ascii=False, indent=2)
    except OSError:
        pass

    tag = "УСПЕХ" if ok else "ПРОВАЛ"
    print("== ВЕРДИКТ: %s (exit=%d, %.1f с) ==" % (tag, exit_code, elapsed))
    if av_on_exit and ok:
        print("   код возврата процесса %s = известный AV на выходе, ПРОИГНОРИРОВАН" % proc_rc)
    for r in reasons:
        print("   причина: %s" % r)
    print("   маркер_найден=%s  чистый_выход=%s  commandlet_errors=%s  require=%s" % (
        facts.get("marker_found"), facts.get("clean_exit"),
        facts.get("commandlet_errors"), facts.get("require_found") if a.require else "—"))
    print("   ошибок_сохранения=%d  объявлено_ассетов=%d (обновлено %d)" % (
        len(facts.get("save_errors") or []), len(a_rows), sum(1 for r in a_rows if r["updated"])))
    print("   результат: %s" % result_json)
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
