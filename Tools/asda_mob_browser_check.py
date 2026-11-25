# Copyright (c) 2026 @sitxovski (Discord)
# asda_mob_browser_check.py — самопроверка редакторского «Asda Mob Browser» (плагин AsdaMobBrowser), 09.11.2026.
#
# ЧТО ДЕЛАЕТ.
#   Снаружи (python Tools/asda_mob_browser_check.py [--out <папка>]) — запускает редактор БЕЗ ОКНА (-RenderOffscreen,
#   закон no-visible-editor-windows) на ПУСТОЙ карте движка /Engine/Maps/Entry (Login.umap владельца не грузится),
#   без сети (-AsdaNoNet), без P4 (-SCCProvider=None), ждёт маркер и добивает процесс (код возврата headless недостоверен).
#   Внутри UE (тот же файл, -ExecCmds="py …") — через UAsdaMobBrowserDebug:
#     • SelfTest      — данные, сортировки, клипы, поворот/наклон/приближение мышью → selftest.txt;
#     • PickSamples   — образцы: маленький, большой, босс, моб без своих клипов, статический;
#     • RenderBrowser — кадры вкладки целиком (виджет + 3D-сцена превью в render target, окна нет) → f??_*.png.
#   Ничего не пишет в ассеты и таблицы (обозреватель только читает).
#
# ★ Без побочных эффектов при импорте (закон tools-no-import-side-effects): всё — в main().
import os
import sys
import time

MARKER = "ЗАВЕРШЕНО: обозреватель мобов"
DEFAULT_OUT = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/mob_browser/frames"
W, H = 1600, 1000


def _in_ue(out_dir):
    """Тело внутри редактора: самопроверка + кадры, затем маркер."""
    import unreal
    D = unreal.AsdaMobBrowserDebug
    os.makedirs(out_dir, exist_ok=True)
    t0 = time.time()
    st = D.self_test()
    with open(os.path.join(out_dir, "selftest.txt"), "w", encoding="utf-8") as f:
        f.write(st)
    samples = dict(kv.split("=") for kv in D.pick_samples().split(";"))  # small/big/boss/noownclips/static/npc
    s = {k: int(v) for k, v in samples.items()}
    unreal.log("[обозреватель мобов] образцы: %s" % s)
    # (имя, NPC?, id, клип, протяжка ЛКМ в пикселях, время клипа, сортировка, по убыванию, поиск, EN)
    frames = [
        ("f01_mobs_group_alphabet", False, 0,               -1, 0,   0.0, "Name", False, "",     False),
        ("f02_npc_group_alphabet",  True,  0,               -1, 0,   0.0, "Name", False, "",     False),
        ("f03_small_idle",          False, s["small"],      0,  0,   0.6, "Name", False, "",     False),
        ("f04_big_idle",            False, s["big"],        0,  0,   0.6, "Name", False, "",     False),
        ("f05_boss_idle",           False, s["boss"],       0,  0,   0.6, "Name", False, "",     False),
        ("f06_no_own_clips",        False, s["noownclips"], 0,  0,   0.6, "Name", False, "",     False),
        ("f07_static_model",        False, s["static"],     -1, 0,   0.0, "Name", False, "",     False),
        ("f08_boss_attack",         False, s["boss"],       5,  0,   0.5, "Name", False, "",     False),
        ("f09_boss_rotated_120",    False, s["boss"],       0,  240, 0.6, "Name", False, "",     False),
        ("f10_mobs_sort_level_desc",False, s["big"],        0,  0,   0.6, "Lvl",  True,  "",     False),
        ("f11_npc_talk",            True,  s["npc"],        2,  0,   0.6, "Name", False, "",     False),
        ("f12_english_search",      False, 1,               2,  -90, 0.4, "Name", False, "wolf", True),
    ]
    for fr in frames:
        png = os.path.join(out_dir, fr[0] + ".png").replace("\\", "/")
        r = D.render_browser(png, fr[1], fr[2], fr[3], fr[4], fr[5], fr[6], fr[7], fr[8], fr[9], W, H)
        unreal.log(r)
    unreal.log("%s за %.1f с, кадров %d" % (MARKER, time.time() - t0, len(frames)))


def _outside(out_dir):
    """Снаружи: скрытый редактор на пустой карте, ожидание маркера по логу, добивание по PID."""
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, here)
    import asda_pie_harness as PH
    os.makedirs(out_dir, exist_ok=True)
    log = os.path.join(out_dir, "headless_mob_browser.log").replace("\\", "/")
    script = os.path.abspath(__file__).replace("\\", "/")
    env = dict(os.environ)
    env["ASDA_MOBBROWSER_OUT"] = out_dir
    cmd = [PH.UE_CMD, PH.UPROJECT, "/Engine/Maps/Entry", "-ExecCmds=py %s" % script] + list(PH.QUIET_ARGS) + \
          ["-RenderOffscreen", "-AsdaNoNet", "-ResX=1920", "-ResY=1200", "-stdout", "-FullStdOutLogOutput", "-abslog=%s" % log, PH.SCC_OFF]
    if os.path.exists(log):
        os.remove(log)
    p = PH.popen_hidden(cmd, env=env)
    t0 = time.time()
    ok = False
    while time.time() - t0 < 1200:
        time.sleep(5)
        txt = ""
        if os.path.exists(log):
            with open(log, encoding="utf-8", errors="replace") as f:
                txt = f.read()
        if MARKER in txt:
            ok = True
            break
        if "Fatal error" in txt or "Assertion failed" in txt or p.poll() is not None:
            break
    time.sleep(2)
    PH.kill_tree(p.pid)
    print("РЕЗУЛЬТАТ: %s; лог %s; кадры и самопроверка — %s" % ("УСПЕХ" if ok else "ПРОВАЛ", log, out_dir))
    return 0 if ok else 3


def main():
    try:
        import unreal  # noqa: F401  — внутри редактора
        _in_ue(os.environ.get("ASDA_MOBBROWSER_OUT", DEFAULT_OUT))
        return 0
    except ImportError:
        pass
    out = DEFAULT_OUT
    if "--out" in sys.argv:
        out = sys.argv[sys.argv.index("--out") + 1]
    return _outside(out)


if __name__ == "__main__":
    sys.exit(main())
