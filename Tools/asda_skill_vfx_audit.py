# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_skill_vfx_audit.py — АУДИТ ПРИВЯЗКИ И МОМЕНТА ЭФФЕКТОВ НАВЫКОВ (04.11.2026, жалобы владельца: «Молния» мага не держится на мобе,
#   у «Огненного шара» нет снаряда). [ПОРТ]
#
#   Сверяет таблицу навыков порта (RawAssets/Data/SkillClips.csv, источник DT_SkillClips) с телами функций эталона
#   (RE/etalon/*.c) по трём признакам:
#   A. ПРИВЯЗКА К ЦЕЛИ. Режим привязки эффекта (аргумент attach у Effect_SpawnByName / CEffectMgr_CreateEffect / effectMgrSpawnByName)
#      решает, что эффект делает каждый кадр (`objectMgr_TickEffects@0xF57990` → обработчик №режим, `CEffectMgr_Construct@0xF551C0`):
#        3  effectSyncPosToRenderEntity — едет за мобом (render id);   9  effectSyncPosToOwnerEntity — едет за мобом;
#        12 effectUpdateEntityAttachPos — за сущностью с подъёмом 25/45/60;  13 effectPlaceAboveEntity — над сущностью (+200);
#        14 effect_UpdateAttachToTarget — матрица цели;  15 effectNodeFollowEntityBone — кость цели;
#        5 effectNodeTrackTargetChar / 6 effectNodeFollowPlayerBone / 7 effectNodeUpdateCasterItemPos — за персонажем/костью;
#        0 — свободный эффект. В порту режимы 3/9/12/13/14/15 не размечены (asda_dump_skill_dispatch.ATTACH_MODE знает 0/5/6/7/10),
#      и эффект «у цели» рождается неподвижным: моб уходит, эффект остаётся.
#   B. МОМЕНТ УДАРА. Тик, который запускает удар/снаряд по флагу «ответ сервера пришёл» (`gap_0[2] & 1`, `+104193 & 1`) БЕЗ сравнения с
#      порогом COMBO (`color`/`+6355`), — у порта ждёт порог Combo0Ms.
#   C. ПОРОГ ЗА КОНЦОМ КЛИПА. Combo0Ms длиннее клипа (.jbb донора) — фаза 0 у порта не наступает никогда (снаряда/удара нет).
#   Выход: Reports/skillvfx_1104/vfx_audit.csv и сводка в консоль.
#   Запуск: python Tools\asda_skill_vfx_audit.py
#   ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import collections
import csv
import glob
import os
import re
import struct
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))   # <ROOT> (Tools → проект → ROOT)
ETALON = os.path.join(ROOT, "RE", "etalon")
CLIPS = os.path.join(HERE, "..", "RawAssets", "Data", "SkillClips.csv")
MOTION = os.path.join(ROOT, "Donors", "AsdaChroniclesInterlude", "Data", "Character", "Motion", "CharMotion.baz")
OUT_DIR = os.path.join(ROOT, "Reports", "skillvfx_1104")
FXSUB = os.path.join(HERE, "..", "Source", "AsdaReborn", "Private", "Skill", "AsdaSkillFxSubsystem.cpp")
FOLLOW_TARGET = {3, 9, 12, 13, 14, 15}
SPAWNERS = ("Effect_SpawnByName", "CEffectMgr_CreateEffect", "effectMgrSpawnByName")


def load_funcs():
    """Имя функции эталона → тело."""
    funcs = {}
    for f in glob.glob(os.path.join(ETALON, "*.c")):
        cur, buf = None, []
        for line in open(f, encoding="utf-8", errors="replace"):
            m = re.match(r"^//---- (\S+) @ 0x", line)
            if m:
                if cur:
                    funcs[cur] = "".join(buf)
                cur, buf = m.group(1), []
            elif cur:
                buf.append(line)
        if cur:
            funcs[cur] = "".join(buf)
    return funcs


def call_args(text, start):
    """Аргументы вызова, начинающегося со скобки `start` (верхний уровень, без вложенных скобок)."""
    depth, cur, out, i = 0, "", [], start
    while i < len(text):
        ch = text[i]
        if ch == "(":
            depth += 1
            if depth > 1:
                cur += ch
        elif ch == ")":
            depth -= 1
            if depth == 0:
                out.append(cur.strip())
                return out
            cur += ch
        elif ch == "," and depth == 1:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
        i += 1
    return out


def spawns(body):
    """[(имя или поле, режим привязки или None)] — все спавны эффектов в теле."""
    res = []
    for m in re.finditer(r"\b(%s)\s*\(" % "|".join(SPAWNERS), body):
        fn = m.group(1)
        a = call_args(body, m.end() - 1)
        idx = {"Effect_SpawnByName": 9, "CEffectMgr_CreateEffect": 7, "effectMgrSpawnByName": 10}[fn]
        mode = None
        if len(a) > idx and re.fullmatch(r"\d+", a[idx]):
            mode = int(a[idx])
        elif len(a) > idx and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*(\[\d+\])?", a[idx]):
            # режим через переменную (`v20[74]`, `matrixWork[13]`) — последнее литеральное присваивание выше по телу
            asg = re.findall(r"(?<![A-Za-z0-9_])%s\s*=\s*(\d+)\s*;" % re.escape(a[idx]), body[:m.start()])
            if asg:
                mode = int(asg[-1])
        name = a[1] if len(a) > 1 else "?"
        lit = re.search(r'"([^"]+\.jeb)"', name, re.I)
        res.append((lit.group(1) if lit else name, mode, fn))
    return res


def has_threshold(body):
    """Сравнивает ли тело прогресс клипа с порогом COMBO (массив `color` слота / `+6355` в сыром виде)."""
    return bool(re.search(r"\.color\b|\+ 6355\)|\+ 6355 \)|6355\)\s*\+|\+ \*\(\(_DWORD \*\)pChar \+ 47781\) \+ 6355", body))


def jbb_len(z, names, clip):
    b = names.get(clip.lower())
    if not b:
        return None
    d = z.read(b)
    nb = struct.unpack_from("<I", d, 0x24)[0]
    o = 0x44 + 4 * nb
    counts = [struct.unpack_from("<I", d, o + i * 0x94 + 0x20)[0] for i in range(nb)]
    kf, dur = o + nb * 0x94, 0.0
    for kc in counts:
        if kc:
            dur = max(dur, struct.unpack_from("<f", d, kf + (kc - 1) * 0x2C)[0])
        kf += kc * 0x2C
    return dur   # мс


def port_flag_ticks():
    """Тики, которым порт ставит момент фазы 0 по флагу (AsdaPhase0TriggerMs: kOnFlag → 0 мс, Bowshot/TickAoE → 2500)."""
    src = open(FXSUB, encoding="utf-8-sig", errors="replace").read()
    m = re.search(r"static float AsdaPhase0TriggerMs\(.*?\n}\n", src, re.S)
    if not m:
        return set()
    return set(re.findall(r"\b([A-Z][A-Za-z0-9]*_[A-Za-z0-9_]+)\b", m.group(0)))


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    funcs = load_funcs()
    z = zipfile.ZipFile(MOTION)
    names = {n.lower(): n for n in z.namelist()}
    rows = list(csv.DictReader(open(CLIPS, encoding="utf-8-sig")))
    out, stat, ex = [], collections.Counter(), collections.defaultdict(set)
    seen = set()
    flag_ticks = port_flag_ticks()
    # поля эффектов ВСЕХ строк пары навык×клип (ветки по размеру цели — разные строки одного клипа, см. 756)
    allfields = collections.defaultdict(str)
    for r in rows:
        allfields[(int(r["SkillId"]), r["Clip"].lower())] += ";" + ";".join(r[k] for k in ("FxCast", "FxHit0", "FxHit1", "FxHit2"))
    for r in rows:
        sid, tick, loader = int(r["SkillId"]), r["TickFn"], r["Loader"]
        key = (sid, r["Clip"].lower())
        if key in seen:
            continue
        seen.add(key)
        body = funcs.get(tick, "")
        issues = []
        # спавны тика, загрузчика и вызываемых ими помощников (один уровень: CSkillCast_Apply*, Skill_PlayHitEffect* и т.п.)
        callees = set(re.findall(r"([A-Za-z_][A-Za-z0-9_]{5,})\s*\(", body + funcs.get(loader, ""))) & set(funcs)
        allsp = spawns(body) + spawns(funcs.get(loader, ""))
        for c in callees - {tick, loader}:
            allsp += spawns(funcs[c])
        follow = sorted({(n, md) for n, md, fn in allsp if md in FOLLOW_TARGET})
        if follow:
            issues.append("A_ПРИВЯЗКА_К_ЦЕЛИ: " + "; ".join("%s режим %d" % (n, md) for n, md in follow))
        flag_hit = bool(re.search(r"gap_0\[2\] & 1|\+ 104193\) & 1", body))
        has_hit = bool(r["FxHit0"] or r["FxHit1"] or r["FxHit2"] or r["Proj"] == "1")
        if body and has_hit and flag_hit and not has_threshold(body):
            issues.append("B_УДАР_ПО_ФЛАГУ_БЕЗ_ПОРОГА (порт ждёт Combo0 %s мс)" % r["Combo0Ms"])
        try:
            c0 = float(r["Combo0Ms"] or 0)
        except ValueError:
            c0 = 0.0
        L = jbb_len(z, names, r["Clip"])
        if L and c0 > L + 1:
            issues.append("C_ПОРОГ_ЗА_КОНЦОМ_КЛИПА (Combo0 %.0f > клип %.0f мс)" % (c0, L))
        for i in issues:
            k = i.split(" ")[0].rstrip(":")
            stat[k] += 1
            ex[k].add(sid)
        # ★ 04.11.2026: что порт уже закрыл. A — каждое «едущее» имя стоит в строке с привязкой track (режимы 3/9/12/14/15) или
        #   attach (режим 13); B — тик в списке AsdaPhase0TriggerMs; C — фаза 0 не ждёт порога (B закрыт) или в ней нечего показывать
        #   (нет эффектов удара, снаряда и звука удара — эталон тоже не доходит до порога: прогресс стоит на конце клипа).
        fields = allfields[key]
        closed = []
        for i in issues:
            k = i.split(" ")[0].rstrip(":")
            if k == "A_ПРИВЯЗКА_К_ЦЕЛИ":
                ok = all(("%s@%s:" % (n, "attach" if md == 13 else "track")) in fields for n, md in follow)
            elif k == "B_УДАР_ПО_ФЛАГУ_БЕЗ_ПОРОГА":
                ok = tick in flag_ticks
            else:
                ok = tick in flag_ticks or not (has_hit or r["SndHit"])
            closed.append("закрыто" if ok else "ОТКРЫТО")
            stat[k + (" [закрыто]" if ok else " [ОТКРЫТО]")] += 0
        out.append([sid, r["SkillName"], r["Clip"], tick, r["Combo0Ms"], "%.0f" % L if L else "", r["Proj"], " | ".join(issues) or "ok",
                    " | ".join(closed) or "—"])
    os.makedirs(OUT_DIR, exist_ok=True)
    with open(os.path.join(OUT_DIR, "vfx_audit.csv"), "w", encoding="utf-8-sig", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["навык", "имя", "клип", "тик", "Combo0 мс", "клип мс", "снаряд", "находки", "в порту"])
        w.writerows(out)
    print("строк (навык×клип): %d" % len(out))
    opened = [o for o in out if "ОТКРЫТО" in o[8]]
    print("строк с находками: %d, из них ОТКРЫТО: %d %s" % (sum(1 for o in out if o[7] != "ok"), len(opened),
          " ".join(sorted({str(o[0]) for o in opened}))))
    for k, v in stat.most_common():
        print("  %-28s %4d строк, навыков %3d: %s" % (k, v, len(ex[k]), " ".join(map(str, sorted(ex[k])))))


if __name__ == "__main__":
    main()
