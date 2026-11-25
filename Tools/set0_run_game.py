# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# set0_run_game.py — запуск UnrealEditor-Cmd.exe в режиме ИГРОКА (-game, не PIE, не editor-viewport)
#   с python-скриптом через -ExecCmds="py ...". Скрыто (-RenderOffscreen + CREATE_NO_WINDOW,
#   закон no-visible-editor-windows). Успех — ТОЛЬКО по логу (маркер), не по коду возврата
#   (закон headless-exit-code-unreliable; вердикт/регэкспы берём из run_headless_safe.py).
#   Отдельный файл, т.к. run_headless_safe.py заточен под headless-редактор и не принимает карту/-game;
#   его самого не трогаем (им параллельно пользуются другие агенты).
# Использование:
#   python Tools/set0_run_game.py <скрипт.py> --map /Game/Map/alpen/alpen [--log ...] [--marker "ЗАВЕРШЕНО"]
#                                 [--timeout 1800] [--idle 300] [--res 1920x1080] [--env K=V ...]
import argparse, os, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_headless_safe as RHS   # без побочных эффектов при импорте (main() под guard)
import asda_pie_harness as PH     # ★ волна 3 п.15: командная строка -game, скрытый запуск, добивание, вердикт по логу

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass


# (маркер найден?, фатальная строка или None) — по логу, не по коду возврата.
#   ★ Волна 3 п.15: тело перенесено в PH.marker_and_fatal (регэкспы — из run_headless_safe.py); имя оставлено.
tail_ok = PH.marker_and_fatal


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("script")
    ap.add_argument("--map", required=True)
    ap.add_argument("--log", default="")
    ap.add_argument("--marker", default="ЗАВЕРШЕНО")
    ap.add_argument("--timeout", type=float, default=1800.0)
    ap.add_argument("--idle", type=float, default=300.0)
    ap.add_argument("--res", default="1920x1080")
    ap.add_argument("--env", action="append", default=[])
    # ★ Сет E-0 (06.09): --arg "<ключ>" — дополнительные аргументы командной строки UE (повторяемый), напр.
    #   --arg "-GameUserSettingsIni=D:/.../gus_L0.ini" — изолированный ini уровней качества (закон ui-check-game-path-geometry:
    #   -game и редактор делят GameUserSettings.ini владельца; тесты уровней sg.* обязаны ходить в свой файл).
    ap.add_argument("--arg", action="append", default=[])
    a = ap.parse_args()
    script = os.path.abspath(a.script)
    if not os.path.isfile(script):
        print("нет скрипта", script)
        return 2
    log = a.log or (r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05/game_%s.log"
                    % os.path.splitext(os.path.basename(script))[0])
    if os.path.isfile(log):
        os.remove(log)
    rx, ry = a.res.lower().split("x")
    # ★ Волна 3 п.15: форма командной строки -game — PH.game_cmd (символ в символ прежняя; -SCCProvider=None, сет C-1)
    cmd = PH.game_cmd(a.map, script, log, rx, ry, a.arg)
    env = dict(os.environ)
    for kv in a.env:
        k, _, v = kv.partition("=")
        env[k] = v
    print("CMD:", " ".join(cmd))
    # скрыто: STARTUPINFO без окна + CREATE_NO_WINDOW (закон no-visible-editor-windows)
    p = PH.popen_hidden(cmd, env=env)
    t0 = time.time()
    last_sz = -1
    last_grow = t0
    verdict = None
    fatal = None
    while True:
        time.sleep(5)
        sz = os.path.getsize(log) if os.path.isfile(log) else 0
        if sz != last_sz:
            last_sz = sz
            last_grow = time.time()
        try:
            text = open(log, encoding="utf-8", errors="replace").read() if sz else ""
        except Exception:
            text = ""
        ok, fatal = tail_ok(text, a.marker)
        if ok:
            verdict = "ok"
            break
        if fatal:
            verdict = "fatal"
            break
        if p.poll() is not None:
            verdict = "exited"
            break
        if time.time() - last_grow > a.idle:
            verdict = "idle"
            break
        if time.time() - t0 > a.timeout:
            verdict = "timeout"
            break
    time.sleep(3)
    if p.poll() is None:
        PH.kill_tree(p.pid)
    print("VERDICT:", verdict, "fatal=", fatal, "dt=%.0fs" % (time.time() - t0), "log=", log)
    return 0 if verdict == "ok" else 3


if __name__ == "__main__":
    sys.exit(main())
