# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_map_regress.py — РЕГРЕСС-ПРОВЕРКА ЭФФЕКТОВ КАРТЫ живым путём игрока (-game), с вердиктом.
#
# ЗАЧЕМ. Вторая страховка к `asda_fx_orient_check.py`. Тот мерит 26 реперов alpen в редакторе (двойник на
#   текущей DLL); эта — ВСЕ эффекты карты так, как их строит игра (-game, тот же рантайм, что у игрока):
#   мировой центроид, радиус и число вершин секции каждого детерминированного эмиттера против эталона.
#   Создано 27.09.2026 (волна 0 графики, Reports/owner_0927/gfx_w0.md): прежняя проба
#   `_probe_fx_map_regress.py` была СЛЕПА — «секции нет» у всех 175 эмиттеров (причина — в её шапке).
#
# ЧТО СРАВНИВАЕТСЯ. Эмиттеры типов с детерминированной геометрией: кольцо 0xE, квад 0xB/0x18, меши 0x11/0x12/0x1F.
#   По каждому: мировой центроид, радиус, число вершин (SectionDump) + первая вершина и габариты AABB секции
#   (python по PMC — ловит поворот симметричной секции на месте, которого центроид/радиус не видят).
#   Частицы (0xD/0x3 и прочие) и биллборд 0xC пропускаются: FRand и зависимость от камеры.
#   Допуск: центроид и радиус ±1.0 юн, число вершин — точно. Эмиттер, у которого в эталоне секция была, а
#   сейчас нет (или наоборот), — расхождение. ★ Страж слепоты: если секций нет у большинства сравнимых
#   эмиттеров, вердикт «СЛЕПА», а не «ЧИСТО».
#
# ЗАПУСК (снаружи UE, обычным python; ≈3–4 мин на карту; перед запуском — проверить соседние процессы UE):
#   python Tools/asda_fx_map_regress.py --map alpen                 — сверка с эталоном, вердикт ЧИСТО/РЕГРЕСС
#   python Tools/asda_fx_map_regress.py --map alpen --snapshot      — перефиксировать эталон
#                (прежний копируется в .bak_<время>; перефиксировать ТОЛЬКО при осознанном изменении)
#   python Tools/asda_fx_map_regress.py --map alpen --control       — КОНТРОЛЬНАЯ ПОРЧА: обязана дать РЕГРЕСС
#                (цель: --control-fx <часть имени .jeb>; по умолчанию фонарь alpen, на других картах — первый
#                 актор с кольцом/квадом; всем его узлам колец/квадов rotY += 90° в памяти прогона)
#   python Tools/asda_fx_map_regress.py --map alpen --parse-only <лог> — только разбор готового лога
#   Эталон: Tools/fx_map_regress_baseline_<карта>.json. Логи: Reports/fx_map_regress/.
#   Код возврата: 0 — ЧИСТО (или эталон записан); 1 — РЕГРЕСС; 2 — СЛЕПА/ошибка запуска.
#
# ★ Ничего не сохраняет в ассетах: -game не пишет уровни; контрольная порча живёт только в памяти прогона.
import argparse, json, math, os, re, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
LOG_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/fx_map_regress"
DET_TYPES = {0xE, 0xB, 0x18, 0x11, 0x12, 0x1F}
TOL = 1.0

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

RE_ACT = re.compile(r"\[секции\] АКТОР (\S+) \((\S+)\) лок=V\(X=([-\d.]+), Y=([-\d.]+), Z=([-\d.]+)\)")
RE_EM = re.compile(r"\[секции\]\s+эм\.(\d+) «(.*?)» тип 0x([0-9A-F]+) no_chiral=(\d) центроид=(.*?) радиус=([-\d.]+) вершин=(\d+)")
RE_V = re.compile(r"V\(X=([-\d.]+), Y=([-\d.]+), Z=([-\d.]+)\)")
RE_TOTAL = re.compile(r"\[секции\] ИТОГО акторов напечатано: (\d+)")
RE_GEO = re.compile(r"\[fxmapreg-sec\] (\S+) (\d+) n=(\d+) v0=([-\d.]+),([-\d.]+),([-\d.]+) ext=([-\d.]+),([-\d.]+),([-\d.]+)")


def parse_log(path):
    """{объект актора: {"fx": имя, "loc": [..], "em": {idx: {...}}}} + число напечатанных акторов."""
    out = {}; cur = None; total = None; geo = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = RE_ACT.search(line)
            if m:
                cur = {"fx": m.group(1), "loc": [float(m.group(3)), float(m.group(4)), float(m.group(5))], "em": {}}
                out[m.group(2)] = cur
                continue
            m = RE_EM.search(line)
            if m and cur is not None:
                mv = RE_V.search(m.group(5))
                cur["em"][m.group(1)] = {
                    "name": m.group(2), "type": int(m.group(3), 16),
                    "c": [float(mv.group(i)) for i in (1, 2, 3)] if mv else None,
                    "r": float(m.group(6)), "v": int(m.group(7))}
                continue
            m = RE_TOTAL.search(line)
            if m:
                total = int(m.group(1))
                continue
            m = RE_GEO.search(line)
            if m:
                geo[(m.group(1), str(int(m.group(2))).zfill(2))] = {
                    "n": int(m.group(3)), "v0": [float(m.group(i)) for i in (4, 5, 6)],
                    "ext": [float(m.group(i)) for i in (7, 8, 9)]}
    # геометрия PMC (первая вершина + AABB) — к эмиттеру по (объект, индекс секции = индекс эмиттера)
    for (obj, idx), g in geo.items():
        e = out.get(obj, {}).get("em", {}).get(idx)
        if e is not None:
            e["v0"] = g["v0"]; e["ext"] = g["ext"]
    return out, total


def compare(base, cur):
    """(расхождения[], сравнено, сравнимых_без_секции_сейчас)."""
    bad = []; n = 0; empty_now = 0
    for obj, b in sorted(base.items()):
        c = cur.get(obj)
        if c is None:
            bad.append("ПРОПАЛ актор %s (%s)" % (obj, b["fx"])); continue
        for idx, be in sorted(b["em"].items(), key=lambda x: int(x[0])):
            if be["type"] not in DET_TYPES:
                continue
            ce = c["em"].get(idx)
            n += 1
            if ce is None:
                bad.append("%s %s эм.%s: эмиттер пропал" % (obj, b["fx"], idx)); continue
            if ce["c"] is None:
                empty_now += 1
            if (be["c"] is None) != (ce["c"] is None):
                bad.append("%s %s эм.%s «%s» 0x%X: секция %s" % (obj, b["fx"], idx, be["name"], be["type"],
                           "пропала" if ce["c"] is None else "появилась"))
                continue
            if be["c"] is None:
                continue
            dc = math.sqrt(sum((be["c"][j] - ce["c"][j]) ** 2 for j in range(3)))
            dr = abs(be["r"] - ce["r"])
            # первая вершина и AABB (есть у эталонов с 27.09 ~19:50; у более старых — пропуск)
            dv = de = 0.0
            if be.get("v0") and ce.get("v0"):
                dv = math.sqrt(sum((be["v0"][j] - ce["v0"][j]) ** 2 for j in range(3)))
                de = max(abs(be["ext"][j] - ce["ext"][j]) for j in range(3))
            elif be.get("v0") and not ce.get("v0"):
                dv = de = float("inf")
            if be["v"] != ce["v"] or dc > TOL or dr > TOL or dv > TOL or de > TOL:
                bad.append("%s %s эм.%s «%s» 0x%X: Δцентроид=%.1f Δрадиус=%.1f Δвершина0=%.1f Δгабарит=%.1f вершин %d→%d" %
                           (obj, b["fx"], idx, be["name"], be["type"], dc, dr, dv, de, be["v"], ce["v"]))
    for obj, c in cur.items():
        if obj not in base:
            bad.append("НОВЫЙ актор %s (%s) — в эталоне его нет" % (obj, c["fx"]))
    return bad, n, empty_now


def run_game(map_name, log, control, control_fx=""):
    sys.path.insert(0, HERE)
    import asda_maps
    mn = asda_maps.map_asset_name(map_name)
    cmd = [sys.executable, os.path.join(HERE, "set0_run_game.py"), os.path.join(HERE, "_probe_fx_map_regress.py"),
           "--map", "/Game/Map/%s/%s" % (mn, mn), "--log", log, "--marker", "РЕГРЕСС-КАРТА ЗАВЕРШЕНО",
           "--timeout", "900", "--idle", "400", "--res", "1280x720",
           # фиксированный шаг: фаза эффекта в кадре снимка одинакова от прогона к прогону
           "--arg=-UseFixedTimeStep", "--arg=-FPS=60"]
    if control:
        cmd += ["--env", "ASDA_FXMAPREG_CONTROL=1"]
        if control_fx:
            cmd += ["--env", "ASDA_FXMAPREG_CONTROL_FX=" + control_fx]
    print("запуск:", " ".join(cmd))
    r = subprocess.run(cmd)
    return r.returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", required=True)
    ap.add_argument("--snapshot", action="store_true")
    ap.add_argument("--control", action="store_true")
    ap.add_argument("--control-fx", default="")
    ap.add_argument("--parse-only", default="")
    ap.add_argument("--tag", default="")
    a = ap.parse_args()
    if a.snapshot and a.control:
        print("ОШИБКА: --snapshot и --control несовместимы (эталон с порчей не пишется)"); return 2
    base_path = os.path.join(HERE, "fx_map_regress_baseline_%s.json" % a.map)
    if a.parse_only:
        log = a.parse_only
    else:
        os.makedirs(LOG_DIR, exist_ok=True)
        tag = a.tag or ("control" if a.control else ("snapshot" if a.snapshot else "check"))
        log = os.path.join(LOG_DIR, "%s_%s_%s.log" % (a.map, tag, time.strftime("%Y%m%d_%H%M%S")))
        rc = run_game(a.map, log, a.control, a.control_fx)
        if rc != 0:
            print("ВЕРДИКТ: ОШИБКА ЗАПУСКА (маркер не найден), лог:", log); return 2
    cur, total = parse_log(log)
    det = sum(1 for o in cur.values() for e in o["em"].values() if e["type"] in DET_TYPES)
    det_ok = sum(1 for o in cur.values() for e in o["em"].values() if e["type"] in DET_TYPES and e["c"] is not None)
    print("лог: %s\nакторов %d (ИТОГО в логе %s), сравнимых эмиттеров %d, из них с секцией %d" %
          (log, len(cur), total, det, det_ok))
    if not cur or det == 0 or det_ok * 2 < det:
        print("ВЕРДИКТ: СЛЕПА — секций нет у большинства сравнимых эмиттеров (кулл/гейт не сняты?)"); return 2
    if a.snapshot:
        if os.path.isfile(base_path):
            bak = base_path + ".bak_" + time.strftime("%Y%m%d_%H%M%S")
            shutil.copy2(base_path, bak); print("прежний эталон сохранён:", bak)
        json.dump({"map": a.map, "log": log, "actors": cur}, open(base_path, "w", encoding="utf-8"),
                  ensure_ascii=False, indent=1)
        print("ЭТАЛОН ЗАПИСАН:", base_path); return 0
    if not os.path.isfile(base_path):
        print("ВЕРДИКТ: нет эталона %s — сначала --snapshot" % base_path); return 2
    base = json.load(open(base_path, encoding="utf-8"))["actors"]
    bad, n, empty_now = compare(base, cur)
    for s in bad[:200]:
        print("  РАСХОЖДЕНИЕ", s)
    if len(bad) > 200:
        print("  … и ещё %d" % (len(bad) - 200))
    print("сверено эмиттеров %d, расхождений %d" % (n, len(bad)))
    if a.control:
        print("КОНТРОЛЬ: порча обязана дать РЕГРЕСС; «ЧИСТО» ниже = проверка СЛЕПА")
    print("ВЕРДИКТ: %s" % ("РЕГРЕСС" if bad else "ЧИСТО"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
