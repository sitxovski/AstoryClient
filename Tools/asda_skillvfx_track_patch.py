# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_skillvfx_track_patch.py — ТОЧЕЧНАЯ ПРАВКА SkillClips.csv: эффекты, которые эталон ведёт ЗА ЦЕЛЬЮ (attach 9), получают привязку
#   «track» и свой момент (04.11.2026, skillvfx: «Молния» мага не держится на мобе). [ОРИГИНАЛ]
#   906 «Молния»       — eff_1_char_06_lgtstmb03: SkillEffect_Update_LightningBolt@0xA70D10, порог COMBO0, CEffectMgr_CreateEffect(…, renderId, 9)
#                         (моб) / Effect_SpawnByName(…, posVec цели, 0, 9) (игрок) → из каста в фазу 0 удара, «track:target».
#   907 «Метеор»       — eff_1_char_06_bigmeob03: SkillEffect_Update_Explosion, порог COMBO0, attach 9 → фаза 0, «track:target».
#   756 «Ловушка»      — eff_1_char_03_trapth01-a_00: CharFx_UpdateTrapSkillEffect, на флаге ответа (каст), attach 9 → «track:target».
#   908 — eff_1_char_06_dakhlpb03: колбэк каста CharFx_PlaySkillDakhlEffect, attach 9 → «track:target».
#   1064 — eff_1_char_07_fireragb03: запись типа 13 → CEffectMgr_CreateEffect(…, 9) без scaleAll; было «attach» (= эталон 13) → «track».
#   756 — вариант ловушки по классу размера цели (a_00/a_01/a_02), строки TargetSize 0/1/2.
#   Строки правятся только если в них ровно ожидаемое прежнее значение (повторный запуск ничего не меняет). Запуск без аргументов.
#   ★ ЗАКОН tools-no-import-side-effects.
import csv
import io
import os
import sys

CSV = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "RawAssets", "Data", "SkillClips.csv")
RULES = {
    "906": ("eff_1_char_06_lgtstmb03-a_00.jeb@char:target", "eff_1_char_06_lgtstmb03-a_00.jeb@track:target", True),
    "907": ("eff_1_char_06_bigmeob03-a_00.jeb@char:target", "eff_1_char_06_bigmeob03-a_00.jeb@track:target", True),
    "756": ("eff_1_char_03_trapth01-a_00.jeb@world:target", "eff_1_char_03_trapth01-a_00.jeb@track:target", False),
    # 908 — eff_1_char_06_dakhlpb03: CharFx_PlaySkillDakhlEffect@0xA83… (колбэк k1 диспетчера, момент каста), CEffectMgr_CreateEffect(…, renderId, 9)
    "908": ("eff_1_char_06_dakhlpb03-a_00.jeb@world:target", "eff_1_char_06_dakhlpb03-a_00.jeb@track:target", False),
    # 1064 — eff_1_char_07_fireragb03: SkillEffect_Update_Explosion, packBuildEntryRecord(…, 13, …, 9, …) → skillFx_applySkillResultToTargets:
    #   CEffectMgr_CreateEffect(…, renderId, 9, 0, 1.0) без scaleAll — attach 9, не 13 → из «attach» в «track» (поле удара).
    "1064": ("eff_1_char_07_fireragb03-a_00.jeb@attach:target", "eff_1_char_07_fireragb03-a_00.jeb@track:target", "hit"),
    # 604/607 «Граната» — eff_1_char_99_grebom01: effectBeamUpdate по прилёте — CEffectMgr_CreateEffect(…, renderId цели, 9) у каждой
    #   задетой цели (beamSelf[27] = effectParams[10] = 2) → «track» (05.11.2026, сессия grenade).
    "604": ("eff_1_char_99_grebom01-a_00.jeb@world:target", "eff_1_char_99_grebom01-a_00.jeb@track:target", "hit"),
    "607": ("eff_1_char_99_grebom01-a_00.jeb@world:target", "eff_1_char_99_grebom01-a_00.jeb@track:target", "hit"),
}


def main():
    raw = open(CSV, "rb").read()
    crlf = b"\r\n" in raw
    text = raw.decode("utf-8-sig")
    rows = list(csv.reader(io.StringIO(text)))
    h = rows[0]
    iS, iC, iH = h.index("SkillId"), h.index("FxCast"), h.index("FxHit0")
    n = 0
    for r in rows[1:]:
        rule = RULES.get(r[iS])
        if not rule:
            continue
        old, new, to_hit = rule
        if to_hit == "hit":   # запись уже в поле удара — замена на месте
            hp = [p for p in r[iH].split(";") if p]
            if old in hp:
                hp[hp.index(old)] = new
                r[iH] = ";".join(hp)
                n += 1
            continue
        parts = [p for p in r[iC].split(";") if p]
        if old not in parts:
            continue
        parts.remove(old)
        if to_hit:
            r[iC] = ";".join(parts)
            r[iH] = ";".join([new] + [p for p in r[iH].split(";") if p])
        else:
            r[iC] = ";".join([new] + parts)
        n += 1
    # ★ 756 «Ловушка» — ВАРИАНТ ПО РАЗМЕРУ ЦЕЛИ [ОРИГИНАЛ]: CharFx_UpdateTrapSkillEffect читает `target[12].gap_C[48]`
    #   (CControl 0x260 ⇒ +7356 = класс размера моба, Mob list +0x90): 0 → trapth01-a_00, 1 → -a_01, 2 → -a_02 (все attach 9);
    #   у цели-персонажа — a_00 неподвижно (класс персонажа = 0). Порт выбирает строку по `TargetSize` (ResolveRow, как у 513):
    #   строки 756 получают класс «0», к ним добавляются копии с классом «1»/«2» и своим вариантом. Повторный запуск — ничего.
    iT, iR = h.index("TargetSize"), h.index("_row")
    trap = [r for r in rows[1:] if r[iS] == "756"]
    if trap and not any(r[iT] for r in trap):
        nxt = max(int(r[iR]) for r in rows[1:]) + 1
        for cls in ("1", "2"):
            for r in trap:
                c = list(r)
                c[iR] = str(nxt); nxt += 1
                c[iT] = cls
                c[iC] = c[iC].replace("trapth01-a_00.jeb@", "trapth01-a_0%s.jeb@" % cls)
                rows.append(c)
                n += 1
        for r in trap:
            r[iT] = "0"
            n += 1
    # ★ 820 «Меткий стрелок» — ПЛОЩАДНОЙ 2b [ОРИГИНАЛ]: подсекция 2 (APPEND_TYPE 1) идёт в CSkillCast_ApplyAoEHitEffects на пороге
    #   фазы 0, подсекция 4 (тот же 2b) — по прилёте носителя 2a (CEffect_UpdateSkillCast). _fx_join схлопнул их в одну запись;
    #   площадная получает вид «aoe» (FirePhase рождает её до вылета носителя). Повторный запуск — ничего.
    aoe = "Ch1_a_4th_ac_01_2b.jeb@attach:target/aoe"
    for r in rows[1:]:
        if r[iS] == "820" and "Ch1_a_4th_ac_01_2a.jeb@projectile" in r[iH] and aoe not in r[iH]:
            r[iH] = r[iH] + ";" + aoe
            n += 1
    # ★ 604/607 «Граната» — ВТОРОЙ взрыв у основной цели [ОРИГИНАЛ]: после цикла effectBeamUpdate (grebom01 у каждой цели) запись вида 13
    #   (skillFx_applySkillResultToTargets) ещё раз рождает grebom01 у основной цели-моба (lockedTargetId) — вид «main». Повтор — ничего.
    main = "eff_1_char_99_grebom01-a_00.jeb@track:target/main"
    for r in rows[1:]:
        if r[iS] in ("604", "607") and "grebom01-a_00.jeb@track:target" in r[iH] and main not in r[iH]:
            r[iH] = r[iH] + ";" + main
            n += 1
    nl = "\r\n" if crlf else "\n"
    out = io.StringIO()
    out.write(text.split("\n", 1)[0].rstrip("\r") + nl)          # шапка — как в файле (без кавычек)
    csv.writer(out, quoting=csv.QUOTE_ALL, lineterminator=nl).writerows(rows[1:])
    open(CSV, "wb").write(b"\xef\xbb\xbf" + out.getvalue().encode("utf-8"))
    print("строк исправлено: %d" % n)


if __name__ == "__main__":
    sys.exit(main())
