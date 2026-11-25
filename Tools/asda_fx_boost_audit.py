# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_boost_audit.py — АУДИТ «яркость эффекта в материале ↔ таблица asda_fx_boost.json» без редактора (04.11.2026,
#   жалоба владельца «эффекты навыков светятся слишком ярко»). [ПОРТ]
#
#   Для каждого запечённого BP_Fx_* читает из .uasset (таблица имён пакета), на какие MIC ссылаются его эмиттеры, и сравнивает
#   множитель EmissiveBoost их вариантов `_fxb<NNN>` с тем, что сегодня даёт таблица (`asda_fx_boost.settings`, тот же матчер, что у
#   генератора). Расхождение = раскат таблицы в ассеты не выполнен (10.09.2026 владелец снизил семейства персонажа до 1.0, раскат
#   был отложен — 508 эффектов навыков остались на ×3).
#   Выход: Reports/skillglow_1104/fx_boost_audit.csv (эффект, семейство, режимы, буст в ассете, буст по таблице, статус)
#   и список .jeb, требующих отката/перераската (stale_jebs.txt — вход для ASDA_FXR_ONLY / ASDA_FXB_ONLY).
#
#   ★ ОГРАНИЧЕНИЕ (найдено 04.11 после раската): таблица имён пакета хранит и имена редакторского скелет-класса BP, поэтому после
#   отката старые `_fxb` в ней остаются, и аудит показывает ложные расхождения. Он верен ДО раската (найти, что не раскатано);
#   ПОСЛЕ — проверять рабочий класс: Tools/asda_fx_boost_cdo_check.py (headless, CDO generated class).
#   Запуск: python Tools\asda_fx_boost_audit.py
#   ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import collections
import csv
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FX_DIR = os.path.join(HERE, "..", "Content", "AsdaAssets", "Effects")
OUT_DIR = os.path.normpath(os.path.join(HERE, "..", "..", "Reports", "skillglow_1104"))


def scan():
    """[(jeb, {(mic_stem, mode): boost_in_asset})] по всем BP_Fx_* (буст 1.0 — исходный MIC без варианта)."""
    sys.path.insert(0, HERE)
    import asda_fx_boost as FXB
    rows = []
    mic_re = re.compile(rb"MI_[A-Za-z0-9_\-]+")
    for f in sorted(os.listdir(FX_DIR)):
        if not (f.lower().startswith("bp_fx_") and f.endswith(".uasset")):
            continue
        b = open(os.path.join(FX_DIR, f), "rb").read()
        mics = {}
        for m in set(mic_re.findall(b)):
            name = m.decode("latin1")
            mode = FXB.mode_of_mic(re.sub(r"_fxb\d{3}(s\d+)?$", "", name))   # режим — по исходному имени (суффикс варианта мешает)
            if mode is None:
                continue
            mv = re.search(r"_fxb(\d{3})", name)
            mics[name] = (mode, int(mv.group(1)) / 100.0 if mv else 1.0)
        # эффект: имя BP без префикса; .jeb в CDO — то же имя с «-» на месте «_a_»; матчер нормализует сам (jeb_key)
        rows.append((f[len("BP_Fx_"):-len(".uasset")], mics))
    return FXB, rows


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    FXB, rows = scan()
    os.makedirs(OUT_DIR, exist_ok=True)
    stale = []
    stat = collections.Counter()
    with open(os.path.join(OUT_DIR, "fx_boost_audit.csv"), "w", encoding="utf-8-sig", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["эффект", "семейство (таблица)", "MIC", "режим", "буст в ассете", "буст по таблице", "статус"])
        for jeb, mics in rows:
            bad = False
            # исходный MIC и его вариант лежат в таблице имён оба, если вариант когда-то был — судим по вариантам
            has_variant = any("_fxb" in n for n in mics)
            for name, (mode, have) in sorted(mics.items()):
                if has_variant and "_fxb" not in name and any(n.startswith(name + "_fxb") for n in mics):
                    continue
                st = FXB.settings(jeb, mode)
                want = st[0] if st else None
                fam = st[3] if st else "—"
                ok = (want is None and have == 1.0) or (want is not None and abs(want - have) < 1e-6)
                status = "ok" if ok else "РАСХОЖДЕНИЕ"
                bad |= not ok
                stat[(fam, have, want, status)] += 1
                w.writerow([jeb, fam, name, mode, have, want if want is not None else "нет правила", status])
            if bad:
                stale.append(jeb)
    with open(os.path.join(OUT_DIR, "stale_jebs.txt"), "w", encoding="utf-8") as fh:
        fh.write(",".join(stale))
    print("BP_Fx: %d, с расхождением: %d" % (len(rows), len(stale)))
    for (fam, have, want, status), n in sorted(stat.items(), key=lambda x: (x[0][3], x[0][0])):
        print("  %-22s ассет ×%-4s таблица ×%-4s %-12s ссылок %d" % (fam, have, want, status, n))


if __name__ == "__main__":
    main()
