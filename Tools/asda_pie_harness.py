# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_pie_harness.py — ★ ОБЩИЙ КАРКАС запуска UE и живых проверок (волна 3 п.15 плана дублей, 15.09.2026;
#   аудит `Reports/dedup_audit/03_tools_python.md` T09/T10/В5).
#
# Зачем. Код запуска редактора/-game (командная строка, скрытый запуск, добивание процесса) и мелкие функции
#   живых проверок внутри UE (игровой мир, консольная команда, снятие кадра, очередь кадров, завершение PIE)
#   были скопированы в десятки инструментов. Здесь каждая такая функция — ОДИН раз, с телом 1:1 из копий
#   (перенос сверен стендом `Reports/dedup_audit/wave3_tools_framework.md`: командные строки символ в символ,
#   поведение функций — одинаковые трассы вызовов на подставном `unreal`).
#
# Две части:
#   А. ЗАПУСК (снаружи UE): командные строки редактора/-game/Insights, скрытый запуск (CREATE_NO_WINDOW,
#      закон no-visible-editor-windows), добивание. ★ Успех — ТОЛЬКО по логу (закон headless-exit-code-unreliable):
#      вердикт и регэкспы остаются в `run_headless_safe.py`, сюда они подтягиваются лениво.
#   Б. ВНУТРИ UE (живые проверки через -ExecCmds="py …"): фабрики `make_*` возвращают функцию ровно с тем
#      поведением, что была локальной копией; всё, что копия брала из окружения (ues, log, st, папки), передаётся
#      аргументом. `unreal` импортируется при вызове — модуль импортируется и вне UE (стенды).
#
# ★ Закон tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает (ни запусков, ни sys.path, ни unreal).
# ★ Подключение из скрипта, который UE исполняет файлом (RunFile НЕ добавляет папку скрипта в sys.path):
#     import sys as _ph_sys, os as _ph_os
#     _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))
#     if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
#     import asda_pie_harness as PH
import glob
import os
import shutil
import subprocess

# ═══════════════════════════════════════════════════════════════════════════════════════════════════
#  А. ЗАПУСК UE СНАРУЖИ
# ═══════════════════════════════════════════════════════════════════════════════════════════════════
# Консольный редактор движка (им запускаются headless-скрипты и режим игрока -game).
# ★ Т1 (11.10.2026): корень движка можно переопределить той же переменной, что у `asda_ue_engine.bat` (ASDA_UE_DIR) —
#   .bat-обёртки, переведённые на run_headless_safe.py, не теряют это переопределение; не задана — путь прежний символ в символ.
UE_CMD = (os.path.join(os.environ["ASDA_UE_DIR"], "Engine", "Binaries", "Win64", "UnrealEditor-Cmd.exe")
          if os.environ.get("ASDA_UE_DIR") else r"D:/Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe")
# Проект порта
UPROJECT = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject"
# Программа анализа трасс (разбор .utrace без интерфейса)
UNREAL_INSIGHTS = (os.path.join(os.environ["ASDA_UE_DIR"], "Engine", "Binaries", "Win64", "UnrealInsights.exe")
                   if os.environ.get("ASDA_UE_DIR") else r"D:/Games/UE_5.8/Engine/Binaries/Win64/UnrealInsights.exe")

# Без диалогов/пауз/заставки/звука — общие ключи всех запусков редактора.
# ★ Р-2 (ревизия shopdbl2, 10.10.2026): -AsdaNoNet — прогон БЕЗ СЕТИ: клиент не подключается к серверу вообще
#   (иначе ранний коннект логина ~10 с после старта и самотесты слали пакеты на dev-сервер владельца).
#   Правило — Docs/Port/systems/SELFTESTS_OFFLINE.md. Все прогоны через эту обвязку (run_headless_safe.py, game_cmd, set0_*) идут без сети.
QUIET_ARGS = ("-unattended", "-nopause", "-nosplash", "-nosound", "-AsdaNoNet")
# ★ сет C-1 (05.09.2026, review_setA_fix.md мелочь 7): headless пытался делать P4-checkout и вешал диалог
#   «Извлечение из системы контроля версий не удалось». Глобального ключа -NoSourceControl в 5.8 НЕТ (это параметр
#   только у ResavePackages, ContentCommandlets.cpp:3773); провайдер выключается ключом -SCCProvider=None
#   (SourceControlSettings.cpp:58 — приоритет над SourceControlSettings.ini). Perforce из скриптов не трогаем.
SCC_OFF = "-SCCProvider=None"


# ★ Т1 (11.10.2026): скрытый рендер редактора — с ним UnrealEditor-Cmd не создаёт окна (закон no-visible-editor-windows),
#   RHI и тик редактора/PIE при этом живые (в отличие от -NullRHI). Тот же ключ, что у режима игрока `game_cmd`.
RENDER_OFFSCREEN = "-RenderOffscreen"


def editor_python_cmd(script, log, exec_cmds=False, commandlet_rhi=False, render_offscreen=False, extra_args=(), quit_after=False):
    """Командная строка headless-редактора с python-скриптом (форма `run_headless_safe.py`).

    exec_cmds      — через -ExecCmds="py …" (скрипту нужен RHI/PIE), без -NullRHI;
    commandlet_rhi — коммандлет -run=pythonscript с -AllowCommandletRendering (настоящий RHI, ни одного окна);
    иначе          — -ExecutePythonScript + -NullRHI.
    render_offscreen — (★ Т1, 11.10.2026) только вместе с exec_cmds: добавить -RenderOffscreen — редактор БЕЗ ОКНА
                     (живые .bat-проверки PIE); False — строка символ в символ прежняя.
    extra_args     — (★ Т1) доп. ключи UE (напр. -ResX=1920 -ResY=1200 -WinX=0 -WinY=0) сразу после QUIET_ARGS;
                     пусто — строка прежняя.
    quit_after     — (★ С6, 11.10.2026) только вместе с exec_cmds: `-ExecCmds="py <скрипт>,quit"` — СИНХРОННЫЙ скрипт
                     отработал → редактор выходит сам (поведение -ExecutePythonScript, но с живым RHI и без окна при
                     render_offscreen). Скриптам на тике (register_slate_post_tick_callback) НЕ подходит. False — прежняя строка.
    script/log — абсолютные пути (относительный путь скрипта движок резолвит от Engine/Binaries/Win64)."""
    if render_offscreen and (not exec_cmds or commandlet_rhi):
        raise ValueError("render_offscreen — только для exec_cmds (остальные режимы окна и так не создают)")
    if quit_after and (not exec_cmds or commandlet_rhi):
        raise ValueError("quit_after — только для exec_cmds (остальные режимы выходят сами)")
    cmd = [UE_CMD, UPROJECT]
    if commandlet_rhi:
        cmd += ["-run=pythonscript", "-script=%s" % script, "-AllowCommandletRendering"]
    else:
        cmd += (["-ExecCmds=py %s%s" % (script, ",quit" if quit_after else "")] if exec_cmds
                else ["-ExecutePythonScript=%s" % script])
    cmd += list(QUIET_ARGS) + list(extra_args)
    if render_offscreen:
        cmd.append(RENDER_OFFSCREEN)
    cmd += ["-stdout", "-FullStdOutLogOutput", "-abslog=%s" % log, SCC_OFF]
    if not exec_cmds and not commandlet_rhi:
        cmd.append("-NullRHI")
    return cmd


def game_cmd(map_path, script, log, res_x, res_y, extra_args=()):
    """Командная строка режима ИГРОКА (-game, не PIE) со скрытым рендером (форма `set0_run_game.py`).

    extra_args — дополнительные ключи UE (напр. -GameUserSettingsIni=… изолированного ini уровней качества),
    ставятся перед -ExecCmds, как было."""
    return ([UE_CMD, UPROJECT, map_path, "-game", "-RenderOffscreen"] + list(QUIET_ARGS)
            + ["-ResX=%s" % res_x, "-ResY=%s" % res_y, "-WinX=0", "-WinY=0",
               "-stdout", "-FullStdOutLogOutput", "-abslog=%s" % log, SCC_OFF]
            + list(extra_args)
            + ["-ExecCmds=py %s" % script.replace("\\", "/")])


def insights_export_cmdline(trace, log_path, task):
    """Командная строка UnrealInsights для одной команды после анализа (форма `cpu_insights_export.py`).

    ★ ГРАБЛИ (проверено 07.09.2026): аргументы НЕЛЬЗЯ передавать списком — subprocess обернёт весь аргумент
    в кавычки, и FParse::Value не найдёт ключ. Поэтому возвращается СТРОКА, её отдают Popen как есть."""
    return ('"%s" -OpenTraceFile="%s" -ABSLOG="%s" -unattended -autoquit -noui -nullrhi -log '
            '-ExecOnAnalysisCompleteCmd="%s"' % (UNREAL_INSIGHTS, trace, log_path, task))


def hidden_startupinfo():
    """STARTUPINFO со скрытым окном (закон no-visible-editor-windows)."""
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0
    return si


def popen_hidden(cmd, env=None):
    """Скрытый запуск: STARTUPINFO без окна + CREATE_NO_WINDOW, вывод в никуда. env=None — окружение родителя."""
    kw = {}
    if env is not None:
        kw["env"] = env
    return subprocess.Popen(cmd, startupinfo=hidden_startupinfo(), creationflags=subprocess.CREATE_NO_WINDOW,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, **kw)


def popen_quiet(cmd):
    """Запуск консольного редактора с выводом в никуда (форма `run_headless_safe.py`: окна у -Cmd нет)."""
    return subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def kill_tree(pid):
    """Добить процесс вместе с дочерними (taskkill /T /F)."""
    subprocess.call(["taskkill", "/PID", str(pid), "/T", "/F"],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def kill_pid(pid):
    """Добить хвост процесса по PID (форма `run_headless_safe.py`: taskkill /F /PID)."""
    subprocess.run(["taskkill", "/F", "/PID", str(pid)],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def marker_and_fatal(text, marker):
    """(маркер найден?, фатальная строка или None) — ПО ЛОГУ, не по коду возврата.
    Регэкспы и вычёркивание обработанных ensure — из `run_headless_safe.py` (единственный источник вердикта)."""
    import run_headless_safe as RHS
    body = RHS.strip_handled_ensures(text)
    fatal = RHS.FATAL_RE.search(body)
    return (marker in text), (fatal.group(0) if fatal else None)


def log_verdict(text, marker, require=""):
    """Полный вердикт прогона по логу: (ok, причины, факты) из `run_headless_safe.verdict`."""
    import run_headless_safe as RHS
    return RHS.verdict(text, marker, require)


# ═══════════════════════════════════════════════════════════════════════════════════════════════════
#  Б. ВНУТРИ UE — функции живых проверок (фабрики; тела 1:1 с прежними локальными копиями)
# ═══════════════════════════════════════════════════════════════════════════════════════════════════
def make_game_world(ues):
    """world(): игровой мир PIE или None (копия `world` из 27 *_live_check)."""
    def world():
        try:
            return ues.get_game_world()
        except Exception:
            return None
    return world


def make_console_cmd(world, log):
    """cmd(c): консольная команда в игровом мире; без мира — RuntimeError (копия `cmd` из 24 *_live_check)."""
    import unreal

    def cmd(c):
        w = world()
        if not w:
            raise RuntimeError("нет игрового мира — PIE не стартовал")
        unreal.SystemLibrary.execute_console_command(w, c)
        log("→ %s" % c)
    return cmd


def make_console_cmd_ues(ues, log):
    """cmd(c): то же, но мир берётся прямо у ues и текст ошибки короче (копия из keybinds/title_key)."""
    import unreal

    def cmd(c):
        w = ues.get_game_world()
        if not w:
            raise RuntimeError("нет игрового мира")
        unreal.SystemLibrary.execute_console_command(w, c)
        log("→ %s" % c)
    return cmd


def make_take_shot(cmd, st):
    """take_shot(tag): запрос кадра с интерфейсом + тег в st["shots"] (копия из 9 *_live_check).
    `shot showui` = FScreenshotRequest::RequestScreenshot с bShowUI = true (HighResShot UI не пишет)."""
    def take_shot(tag):
        cmd("shot showui")
        st["shots"].append(tag)
    return take_shot


def make_harvest(ss_dir, out_dir, log):
    """harvest(tag): забрать самый свежий .png из папки кадров в out_dir/<tag>.png и удалить исходник
    (копия из 11 *_live_check)."""
    def harvest(tag):
        pngs = glob.glob(os.path.join(ss_dir, "**", "*.png"), recursive=True)
        if not pngs:
            log("⚠ снимок '%s' НЕ появился" % tag)
            return
        pngs.sort(key=os.path.getmtime)
        dst = os.path.join(out_dir, "%s.png" % tag)
        shutil.copy(pngs[-1], dst)
        os.remove(pngs[-1])
        log("снимок '%s' -> %s (%d байт)" % (tag, dst, os.path.getsize(dst)))
    return harvest


def make_probe_camera(world):
    """probe_cam(): первый CameraActor игрового мира или None (копия из exp_gain/quest_mark)."""
    import unreal

    def probe_cam():
        w = world()
        if not w:
            return None
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.CameraActor):
            return a
        return None
    return probe_cam


def make_pie_finish(st, les, log, done_text, handle_is_not_none=False, count_shots=False):
    """finish(): снять тик-колбэк → остановить PIE → строка итога → выйти из редактора.
    handle_is_not_none — проверка st["handle"] через `is not None` (иначе по истинности);
    count_shots — в done_text подставляется len(st["shots"]).
    (копии: quest_accept_win/questlog/questtracker; npc_dialog/post_live/post_seen/gacha_live_composite;
    post_click/post_order)"""
    import unreal

    def finish():
        if (st["handle"] is not None) if handle_is_not_none else st["handle"]:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        try:
            les.editor_request_end_play()
        except Exception:
            pass
        log((done_text % len(st["shots"])) if count_shots else done_text)
        unreal.SystemLibrary.quit_editor()
    return finish


def make_pie_finish_endplay_first(st, les, log, done_text, frames=None, ss_dir=None, out_dir=None):
    """finish(): [скопировать кадры] → остановить PIE → снять тик-колбэк → строка итога → выйти из редактора.
    frames: None — кадры не копируются; "flat" — .png прямо в ss_dir; "walk" — обход дерева ss_dir
    (движок кладёт кадры в подпапку `Screenshots/WindowsEditor`).
    (копии: keybinds/title_key; extra_menu/ui_stuck_gate; title_live/title_mask512)"""
    import unreal

    def finish():
        if frames == "flat":
            try:
                for f in os.listdir(ss_dir):
                    if f.lower().endswith(".png"):
                        shutil.copy2(os.path.join(ss_dir, f), os.path.join(out_dir, f))
                        log("кадр сохранён: %s" % f)
            except Exception:
                pass
        elif frames == "walk":
            try:
                for root, _dirs, files in os.walk(ss_dir):
                    for f in files:
                        if f.lower().endswith(".png"):
                            shutil.copy2(os.path.join(root, f), os.path.join(out_dir, f))
                            log("кадр сохранён: %s" % f)
            except Exception:
                pass
        try:
            les.editor_request_end_play()
        except Exception:
            pass
        if st["handle"]:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        log(done_text)
        unreal.SystemLibrary.quit_editor()
    return finish


def make_find_world(map_path):
    """find_world(): мир карты -game по пути уровня (find_object, затем load_object; с именем объекта и без)
    (копия из fx_diag_boost/fx_f1_shot/fx_verify_rollout/set0_shot_game/cpu_*)."""
    import unreal

    def find_world():
        base = map_path.rsplit("/", 1)[-1]
        for path in (map_path + "." + base, map_path):
            for fn in (unreal.find_object, unreal.load_object):
                try:
                    o = fn(None, path)
                    if o and isinstance(o, unreal.World):
                        return o
                except Exception:
                    pass
        return None
    return find_world


def make_after_frames(st):
    """after(frames, fn): поставить fn в очередь st["q"] через frames кадров (элемент — [кадры, fn])."""
    def after(frames, fn):
        st["q"].append([frames, fn])
    return after


def make_frame_queue_runner(st):
    """run_queue(): раз в кадр — уменьшить счётчик головы очереди; на нуле снять и выполнить."""
    def run_queue():
        if not st["q"]:
            return
        it = st["q"][0]
        it[0] -= 1
        if it[0] <= 0:
            st["q"].pop(0)
            it[1]()
    return run_queue


def make_time_queue_runner(st):
    """run_queue(): голова очереди выполняется, когда st["t"] дошло до её времени (элемент — [время, fn])."""
    def run_queue():
        if not st["q"]:
            return
        it = st["q"][0]
        if st["t"] >= it[0]:
            st["q"].pop(0)
            it[1]()
    return run_queue


def make_after_frames_dict(st):
    """after(frames, fn): очередь кадровых замеров cpu_* (элемент — {"f": кадры, "s": 0.0, "fn": fn})."""
    def after(frames, fn):
        st["q"].append({"f": int(frames), "s": 0.0, "fn": fn})
    return after


def make_after_seconds_dict(st):
    """after_sec(secs, fn): то же по времени (элемент — {"f": 1, "s": секунды, "fn": fn})."""
    def after_sec(secs, fn):
        st["q"].append({"f": 1, "s": float(secs), "fn": fn})
    return after_sec


def make_dict_queue_runner(st):
    """run_queue(dt): голова очереди выполняется, когда истекли и кадры, и секунды."""
    def run_queue(dt):
        if not st["q"]:
            return
        it = st["q"][0]
        it["f"] -= 1
        it["s"] -= dt
        if it["f"] <= 0 and it["s"] <= 0.0:
            st["q"].pop(0)
            it["fn"]()
    return run_queue


def make_newest_png(ss_dirs):
    """newest_png(after_ts): самый свежий .png в папках ss_dirs новее after_ts или None.
    ★ HighResShot в `-game -RenderOffscreen -unattended` файл может НЕ создать и молчит — сверяем время файла."""
    def newest_png(after_ts):
        best, bt = None, -1
        for d in ss_dirs:
            for p in glob.glob(d + "/*.png"):
                t = os.path.getmtime(p)
                if t > bt and t > after_ts:
                    best, bt = p, t
        return best
    return newest_png


def make_new_pngs(ss_dir, st):
    """new_pngs(): .png из дерева ss_dir, которых не было в st["seen"] (по времени); st["seen"] пополняется."""
    def new_pngs():
        cur = set(glob.glob(os.path.join(ss_dir, "**", "*.png"), recursive=True))
        new = sorted(cur - st["seen"], key=os.path.getmtime)
        st["seen"] |= cur
        return new
    return new_pngs


def make_find_effect_actor(st):
    """find_actor(lbl): AsdaEffectActor с ярлыком lbl в мире st["w"] или None."""
    import unreal

    def find_actor(lbl):
        for a in unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.AsdaEffectActor):
            if a.get_actor_label() == lbl: return a
        return None
    return find_actor


def make_apply_quality(cc):
    """apply_quality(level): все группы sg.* на уровень level и sg.ResolutionQuality 100 (консоль cc)."""
    def apply_quality(level):
        for g in ("ViewDistanceQuality", "AntiAliasingQuality", "ShadowQuality", "GlobalIlluminationQuality",
                  "ReflectionQuality", "PostProcessQuality", "TextureQuality", "EffectsQuality",
                  "FoliageQuality", "ShadingQuality"):
            cc("sg.%s %d" % (g, level))
        cc("sg.ResolutionQuality 100")
    return apply_quality
