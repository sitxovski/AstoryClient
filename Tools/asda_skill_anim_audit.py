# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_skill_anim_audit.py — ПОЛНЫЙ СТАТИЧЕСКИЙ АУДИТ «навык → клип → эффекты» по ВСЕМ классам, оружию и полу (01.11.2026,
#   задача владельца «у навыков двуручного мечника не хватает анимаций»). [ПОРТ]
#
#   Для каждого навыка каждой полосы (воин / лучник / маг: деревья grade 1..9, дерево 4-й профессии grade 10/11/12, навыки
#   Хранителя 1228..1254), каждого допустимого вида оружия (маска +0x83: бит = подтип оружия) и пола:
#     • что ДОЛЖНО играть по эталону — строка таблицы, снятой с бинаря (`asda_dump_skill_dispatch.py` → SkillClips.csv: хардкод-
#       загрузчики `CharSkillEffect_InitDispatchTable@0xA89510` + `SkillInfo.inl`), выбор ветки — как у загрузчиков эталона
#       (`switch(pChar->equippedWeaponType)`: точный case → default; пол — `pChar->gender == 1`);
#     • есть ли клип в `CharMotion.baz` донора, его длина по ключам .jbb;
#     • выдан ли номер anim_NN и лежит ли ассет в Content нужного пола;
#     • есть ли каждый эффект (.jeb) в `EffectModel.bnz` и его BP_Fx_ в Content; звуки — в Data/Sound;
#     • набор клипа (`ch4_<g>_<NN>_`) против набора оружия — для сведения (чужой набор у эталона — не дефект, пометка).
#   Выход: Reports/skill_anim_audit/audit.csv (класс, оружие, пол, навык, ожидаемое, что в порте, статус, причина, самотест)
#   Колонка «самотест» — итог `asda.Skill.SelfTestAnimAudit` по этой тройке (подтип, пол, навык) из журналов пакетов run*/batch.log рядом:
#   ok×N / FAIL×M (причины) — так статика и картинка путём игрока лежат в одной строке. Колонка «бой» — вариант `fight`
#   (свой персонаж, навык посреди автоатаки с панели); контрольные прогоны `approach0` (ключ 0) в итог не входят.
#          и summary.txt (сводка по группам причин). Рантайм (кадры, перебивание клипа) — самотест asda.Skill.SelfTestAnimAudit.
#
#   Запуск: python Tools\asda_skill_anim_audit.py
#   ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import collections
import csv
import os
import re
import struct
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))                # @@ASTORY_DEV_ROOT@@ (Tools → проект → ROOT)
PORT = ROOT                                                           # раскладка клонов: всё наше — прямо в ROOT
UE = os.path.dirname(HERE)                                            # проект UE (папка над Tools)
DONOR = os.path.join(ROOT, "Donors", "AsdaChroniclesInterlude")
SIF = os.path.join(UE, "RawAssets", "Data", "Character", "skill_info.sif")       # клиентский .sif порта (MINE, md5 d412ea82)
CLIPS_CSV = os.path.join(UE, "RawAssets", "Data", "SkillClips.csv")               # источник DT_SkillClips
CHANGEJOB = os.path.join(PORT, "AstoryServer", "Patches", "AsdaPatch", "tools", "_srv_mirror", "v2", "GameServer",
                         "SkillTable_Info", "ChangeJobAddSkillTable.txt")         # выдача навыков при смене профессии (живая)
CHARMOTION = os.path.join(DONOR, "Data", "Character", "Motion", "CharMotion.baz")
EFFMODEL = os.path.join(DONOR, "Data", "Effect", "Model", "EffectModel.bnz")
SOUND_DIR = os.path.join(DONOR, "Data", "Sound")
SKEL_DIR = os.path.join(UE, "Content", "AsdaAssets", "Models", "Skeletal", "Characters")
FX_DIR = os.path.join(UE, "Content", "AsdaAssets", "Effects")
OUT_DIR = os.path.join(PORT, "Reports", "skill_anim_audit")

# Полосы классов: деревья (grade .sif +0xBD), дерево 4-й профессии, оружие полосы (подтип = номер бита маски +0x83).
BANDS = {
    "Воин":   dict(grades=(1, 2, 3), job4=10, weapons=(1, 2, 3), soul=range(1228, 1237)),
    "Лучник": dict(grades=(4, 5, 6), job4=11, weapons=(9, 10, 11), soul=range(1237, 1246)),
    "Маг":    dict(grades=(7, 8, 9), job4=12, weapons=(5,), soul=range(1246, 1255)),
}
WEAPON_NAME = {0: "кулаки/меч0", 1: "меч", 2: "копьё", 3: "двуручный меч", 4: "меч4", 5: "посох", 6: "6",
               8: "лук8", 9: "арбалет", 10: "лук", 11: "баллиста"}
# Набор клипов по подтипу оружия (weapon-anim-sets: 0,1,4→_01 · 2→_05 · 3→_02 · 5→_06 · 8,10→_03 · 9→_04 · 11→_07/_08).
WEAPON_SET = {0: "01", 1: "01", 4: "01", 2: "05", 3: "02", 5: "06", 8: "03", 10: "03", 9: "04", 11: "07"}


def read_sif():
    """Записи .sif порта (273 б): код → поля, нужные аудиту."""
    d = open(SIF, "rb").read()
    out = {}
    for i in range(len(d) // 273):
        r = d[273 * i:273 * i + 273]
        if struct.unpack_from("<h", r, 0)[0] != i:
            continue
        out[i] = dict(name=r[2:0x2A].split(b"\0")[0].decode("cp1251", "replace").strip(),
                      sub=r[0x2C], mask=struct.unpack_from("<Q", r, 0x83)[0], grade=r[0xBD], row=r[0xBC], col=r[0xBB])
    return out


def read_changejob():
    """Профессия → список навыков (живая ChangeJobAddSkillTable.txt)."""
    jobs = {}
    for line in open(CHANGEJOB, encoding="latin1"):
        p = line.split()
        if len(p) >= 2:
            jobs[int(p[0])] = [int(x) for x in p[2:]]
    return jobs


def jbb_duration(b):
    """Длина клипа .jbb по ключам (сек): fps@0x34, кости@0x24, ключ 44 б, время — мс в первом float."""
    if len(b) < 0x48 or struct.unpack_from("<I", b, 0)[0] != 0x20050728:
        return None
    nb = struct.unpack_from("<I", b, 0x24)[0]
    o = 0x44 + 4 * nb
    counts = [struct.unpack_from("<I", b, o + i * 0x94 + 0x20)[0] for i in range(nb)]
    kf = o + nb * 0x94
    dur = 0.0
    for kc in counts:
        if kc:
            dur = max(dur, struct.unpack_from("<f", b, kf + (kc - 1) * 0x2C)[0] / 1000.0)
        kf += kc * 0x2C
    return dur


def ue_anim_exists(g, idx):
    """Ассет anim_NN в папке базы персонажа нужного пола."""
    name = "ch1_%s_cremmy_preview" % g
    return os.path.exists(os.path.join(SKEL_DIR, name, "SkeletalMeshes", "%sanim_%02d.uasset" % (name, idx)))


def fx_bp_exists(jeb):
    """BP_Fx_<имя .jeb с '_' вместо '-'> в Content/AsdaAssets/Effects."""
    base = jeb[:-4] if jeb.lower().endswith(".jeb") else jeb
    return os.path.exists(os.path.join(FX_DIR, "BP_Fx_%s.uasset" % base.replace("-", "_")))


def weapon_match(spec, w):
    """Ветка строки по оружию: 'exact' / 'any' (строка не по оружию) / 'def' (case default) / None."""
    if spec == "":
        return "any"
    parts = spec.split("|")
    if str(w) in parts:
        return "exact"
    return "def" if "*" in parts else None


def pick_row(rows, g, w, size="0"):
    """Выбор строки как у UAsdaSkillFxSubsystem::ResolveRow (== ветвление загрузчиков эталона)."""
    cand = [r for r in rows if r["Gender"] in (g, "")]
    if any(r["TargetSize"] for r in cand):
        cand = [r for r in cand if r["TargetSize"] in ("", size)]
    exact = next((r for r in cand if weapon_match(r["WeaponTypes"], w) == "exact"), None)
    if exact:
        return exact, "точный case по оружию"
    anyr = next((r for r in cand if weapon_match(r["WeaponTypes"], w) == "any"), None)
    if anyr:
        return anyr, "строка не зависит от оружия"
    dflt = next((r for r in cand if weapon_match(r["WeaponTypes"], w) == "def"), None)
    if dflt:
        return dflt, "case default по оружию"
    return (cand[0], "первая строка пола") if cand else (None, "")


def fx_names(row):
    """Все эффекты строки: каст, удары фаз — имена .jeb без привязки."""
    out = []
    for col in ("FxCast", "FxHit0", "FxHit1", "FxHit2"):
        for part in (row.get(col) or "").split(";"):
            j = part.split("@")[0].strip()
            if j:
                out.append(j if j.lower().endswith(".jeb") else j + ".jeb")
    return out


def read_runtime():
    """(подтип, пол, навык) → счётчики ok/FAIL и причины провалов из журналов пакетов run*/batch.log."""
    # [ok, fail, причины, последний вердикт] — прогоны идут по времени журнала, последний решает (история — в счётчиках)
    res = {"show": collections.defaultdict(lambda: [0, 0, set(), ""]), "fight": collections.defaultdict(lambda: [0, 0, set(), ""])}
    start = re.compile(r"SELFTEST_BATCH НАЧАЛО \d+/\d+ \S+: (.*)$")
    pat = re.compile(r"АНИМ-АУДИТ (ok|FAIL)\s+(\d+) ([mf])(?: свой)? (\d+) .*?(?: — (\S+))?\s*$")
    dirs = os.listdir(OUT_DIR) if os.path.isdir(OUT_DIR) else []
    dirs.sort(key=lambda d: os.path.getmtime(os.path.join(OUT_DIR, d, "batch.log")) if os.path.exists(os.path.join(OUT_DIR, d, "batch.log")) else 0)
    for d in dirs:
        full = os.path.join(OUT_DIR, d)
        if not (d.startswith(("run", "regress")) and os.path.isdir(full)):   # прогоны аудита и регресса (before_fix_* — до исправления, не в итог)
            continue
        for f in os.listdir(full):
            if f != "batch.log":   # общий журнал пакета: в нём все самотесты (журналы по самотестам одноимённые — перезаписываются)
                continue
            cmd = ""
            for line in open(os.path.join(full, f), encoding="utf-8", errors="replace"):
                ms = start.search(line)
                if ms:
                    cmd = ms.group(1)
                    continue
                if "approach0" in cmd:   # контрольный прогон прежнего порта (ключ 0) — в итог не идёт
                    continue
                m = pat.search(line)
                if not m:
                    continue
                r = res["fight" if " fight" in cmd else "show"][(int(m.group(2)), m.group(3), int(m.group(4)))]
                if m.group(1) == "ok":
                    r[0] += 1
                    r[3] = "ok"
                else:
                    r[1] += 1
                    r[2].add(m.group(5) or "?")
                    r[3] = "FAIL"
    return res


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    runtime = read_runtime()
    sif = read_sif()
    jobs = read_changejob()
    rows_by = collections.defaultdict(list)
    for r in csv.DictReader(open(CLIPS_CSV, encoding="utf-8-sig")):
        rows_by[int(r["SkillId"])].append(r)
    zm = zipfile.ZipFile(CHARMOTION)
    motion = {n.lower(): n for n in zm.namelist()}
    ze = zipfile.ZipFile(EFFMODEL)
    effs = {n.lower().split("/")[-1] for n in ze.namelist()}
    sounds = {f.lower() for f in os.listdir(SOUND_DIR)} if os.path.isdir(SOUND_DIR) else set()
    dur_cache = {}

    in_jobs = {s for lst in jobs.values() for s in lst}
    out_rows = []
    groups = collections.Counter()
    group_ex = collections.defaultdict(set)
    for band, B in BANDS.items():
        skills = sorted(c for c, s in sif.items()
                        if (s["grade"] in B["grades"] or s["grade"] == B["job4"] or c in B["soul"]))
        for code in skills:
            s = sif[code]
            active = s["sub"] in (1, 2, 5)
            tree = ("4-я профессия" if s["grade"] == B["job4"] else
                    "Хранитель" if code in B["soul"] else "дерево %d" % s["grade"])
            wl = [w for w in B["weapons"] if (s["mask"] >> w) & 1] if s["mask"] else list(B["weapons"])
            if not wl:
                wl = list(B["weapons"])
            for w in wl:
                for g in ("m", "f"):
                    rows = rows_by.get(code, [])
                    status, why, exp, have = "OK", "", "", ""
                    if not active:
                        status, why = "ПАССИВ", "пассивный/служебный (sub %d) — клипа у эталона нет" % s["sub"]
                    elif not rows:
                        status = "НЕТ_ВИЗУАЛА_В_ЭТАЛОНЕ"
                        why = ("загрузчик массива 0 у кода — заглушка эталона (диспетчер 0xA89510 не регистрирует), "
                               "SkillInfo.inl записи нет")
                    else:
                        row, how = pick_row(rows, g, w)
                        clip = row["Clip"]
                        jebs = fx_names(row)
                        exp = "%s [%s]; эффекты: %s" % (clip, how, ";".join(jebs) or "-")
                        problems = []
                        b = motion.get(clip.lower())
                        if b is None:
                            problems.append(("КЛИП_НЕТ_В_ДОНОРЕ", "клипа %s нет в CharMotion.baz" % clip))
                            dur = None
                        else:
                            if clip.lower() not in dur_cache:
                                dur_cache[clip.lower()] = jbb_duration(zm.read(b))
                            dur = dur_cache[clip.lower()]
                            if not dur:
                                problems.append(("КЛИП_ПУСТ", "у .jbb нет ключей"))
                        idx = int(row["AnimIdx"]) if row["AnimIdx"].lstrip("-").isdigit() else -1
                        if idx < 0:
                            problems.append(("НЕ_ИМПОРТИРОВАН", "AnimIdx не выдан"))
                        elif not ue_anim_exists(g, idx):
                            problems.append(("НЕТ_АССЕТА", "нет ассета anim_%02d пола %s" % (idx, g)))
                        for j in jebs:
                            if j.lower() not in effs:
                                problems.append(("ЭФФЕКТ_НЕТ_В_ДОНОРЕ", "%s нет в EffectModel.bnz (дефект данных оригинала)" % j))
                            elif not fx_bp_exists(j):
                                problems.append(("ЭФФЕКТ_НЕ_ИМПОРТИРОВАН", "нет BP_Fx для %s" % j))
                        for snd in [x for x in (row.get("SndCast", ""), row.get("SndHit", "")) if x]:
                            for one in snd.split(";"):
                                # ★ 10.11.2026 (job4-vfx, 907): уточнители SndHit «имя[/char|/mob|/rchar][@caster|@target]» — имя файла до них.
                                one = one.split("@")[0].split("/")[0].strip()
                                if one and sounds and one.lower() not in sounds:
                                    problems.append(("ЗВУК_НЕТ", "%s нет в Data/Sound" % one))
                        if row.get("FxNote"):
                            problems.append(("ПОЛЕ_БЕЗ_ЧИТАТЕЛЯ", "эталон кладёт в слот, тик/удар не читает: " +
                                             re.sub(r"\s+", " ", row["FxNote"])[:160]))
                        m = re.match(r"ch4_[mf]_(\d\d)_", clip, re.I)
                        setnote = ""
                        if m and WEAPON_SET.get(w) and m.group(1) not in (WEAPON_SET[w], "99"):
                            setnote = "клип набора _%s при оружии набора _%s (так у эталона)" % (m.group(1), WEAPON_SET[w])
                        have = "anim_%s, %.2f с; %s" % (row["AnimIdx"], dur or 0.0, row["Source"])
                        hard = [p for p in problems if p[0] not in ("ПОЛЕ_БЕЗ_ЧИТАТЕЛЯ", "ЭФФЕКТ_НЕТ_В_ДОНОРЕ")]
                        if hard:
                            status = hard[0][0]
                        elif problems:
                            status = "OK_" + problems[0][0]
                        why = "; ".join(p[1] for p in problems)
                        if setnote:
                            why = (why + "; " if why else "") + setnote
                    groups[status] += 1
                    group_ex[status].add(code)
                    def fmt(rt):
                        if not rt:
                            return ""
                        hist = "ok×%d" % rt[0] + (" / FAIL×%d (%s)" % (rt[1], ",".join(sorted(rt[2]))) if rt[1] else "")
                        return "%s (последний прогон; всего %s)" % (rt[3], hist)
                    rt = runtime["show"].get((w, g, code))
                    rtf = runtime["fight"].get((w, g, code))
                    rts, rtfs = fmt(rt), fmt(rtf)
                    for nm, x in (("показ", rt), ("бой", rtf)):
                        if x:
                            key = "самотест %s: последний %s" % (nm, x[3])
                            groups[key] += 1
                            group_ex[key].add(code)
                    out_rows.append([band, tree, WEAPON_NAME.get(w, str(w)), w, g, code, s["name"],
                                     "да" if code in in_jobs else "", exp, have, status, why, rts, rtfs])
    os.makedirs(OUT_DIR, exist_ok=True)
    with open(os.path.join(OUT_DIR, "audit.csv"), "w", encoding="utf-8-sig", newline="") as f:
        wr = csv.writer(f)
        wr.writerow(["класс", "дерево", "оружие", "подтип", "пол", "навык", "имя", "в выдаче профессии",
                     "ожидается (эталон)", "в порте", "статус", "причина", "самотест: показ", "самотест: бой (свой, путь панели)"])
        wr.writerows(out_rows)
    with open(os.path.join(OUT_DIR, "summary.txt"), "w", encoding="utf-8") as f:
        f.write("Аудит навык→клип→эффекты: строк %d (класс×оружие×пол×навык)\n" % len(out_rows))
        for k, v in groups.most_common():
            ex = sorted(group_ex[k])
            f.write("%-28s %5d строк, навыков %3d: %s\n" % (k, v, len(ex), " ".join(map(str, ex[:40])) + (" …" if len(ex) > 40 else "")))
    print(open(os.path.join(OUT_DIR, "summary.txt"), encoding="utf-8").read())


if __name__ == "__main__":
    sys.exit(main())
