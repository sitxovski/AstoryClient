# Copyright (c) 2026 @sitxovski (Discord)
# Экспорт МИНИ-glb с мировыми клипами ПРИМЕНЕНИЯ НАВЫКОВ (ch4_*/Ch1_* из хардкод-загрузчиков)
# → anim_100.. на существующий скелет ch1_{m|f}_cremmy_preview_Skeleton.
#
# ИСТОЧНИК СПИСКА: `Reports/skill_effect_inventory.tsv` (колонка Kind == "clip"), который снят
#   ИЗ БИНАРЯ скриптом `asda_dump_skill_dispatch.py` (таблица CharSkillEffect_InitDispatchTable@0xA89510).
#   Ничего не выдумывается: клип попадает сюда, только если на него есть ссылка в коде загрузчика/тика.
#
# Механика сборки glb — 1:1 с `export_char_weaponsets.py` (тот же ref-клип cre_cremmy-a_00_0 +
#   hair-кости частей → трек-имена совпадают с существующим скелетом). Первым в glb идёт
#   "__ref_ignore" (при импорте выбрасывается).
#
# Нумерация: индексы выдаются ПО АЛФАВИТУ имени клипа, отдельно для каждого пола, начиная со 100
#   (86..99 оставлены свободными). Карта «клип → anim_NN» пишется рядом в skillclips_map.json
#   и подхватывается `asda_build_skill_clip_dt.py` при сборке DataTable.
# ★★★ НУМЕРАЦИЯ УСТОЙЧИВА К ПОПОЛНЕНИЮ СПИСКА. Раньше индекс был `100 + позиция в алфавитном списке`,
#   и любой НОВЫЙ клип (напр. 42 клипа 4-й профессии `Ch1_*`, которые сортируются ПЕРЕД `ch4_*`)
#   сдвигал номера ВСЕХ уже импортированных 177 клипов: ассеты anim_100..188 в проекте остались бы
#   на месте, а таблица начала бы указывать на чужую анимацию. Теперь ранее выданный индекс берётся
#   из `skillclips_map.json` и НИКОГДА не меняется, новым клипам выдаётся первый свободный.
#
# Запуск: python Tools\export_char_skillclips.py [m|f]
# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import importlib.util
import json
import os
import sys
import zipfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("ECC", os.path.join(HERE, "export_char_cremmy.py"))
ECC = importlib.util.module_from_spec(_spec)
sys.modules["ECC"] = ECC
_spec.loader.exec_module(ECC)
T, S = ECC.T, ECC.S
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import asda_char_clips as _CC   # noqa: E402  ★ дедуп №2 В1 Т11: общая склейка частей / bind-фит / запись мини-glb

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))   # <ROOT> (Tools → проект → ROOT)
INVENTORY = os.path.join(ROOT, "Reports", "skill_effect_inventory.tsv")
OUT_DIR = os.path.join(ECC.OUT_DIR, "skillclips")
MAP_JSON = os.path.join(OUT_DIR, "skillclips_map.json")
IDX_BASE = 100          # anim_100.. — 86..99 не занимаем
sys.path.insert(0, HERE)   # ★ 09.10.2026 (п. 6): Tools в пути — для общей защиты номеров
import asda_skillclip_guard as GUARD   # ★ 09.10.2026 (п. 6): чужие номера (205 «одержимость», 225.. рыбалка) и лежащие в Content


def clip_list():
    """Уникальные имена клипов навыков из снятого с бинаря инвентаря, раздельно по полу."""
    if not os.path.exists(INVENTORY):
        raise SystemExit("нет %s — сначала прогнать Tools\\asda_dump_skill_dispatch.py" % INVENTORY)
    per = {"m": set(), "f": set()}
    with open(INVENTORY, encoding="utf-8") as f:
        next(f, None)
        for line in f:
            p = line.rstrip("\n").split("\t")
            if len(p) < 7 or p[5] != "clip":
                continue
            base = os.path.splitext(p[6])[0]
            low = base.lower()
            if "_m_" in low:
                per["m"].add(base)
            elif "_f_" in low:
                per["f"].add(base)
    return {g: sorted(per[g], key=str.lower) for g in per}


def zip_index(z):
    """Имя в архиве → регистр как в архиве (клиент пишет имена и в верхнем, и в нижнем регистре)."""
    return {n.lower(): n for n in z.namelist()}


def export_gender(gender, zmodel, zmot, clips_wanted, out_map):
    PARTS = ECC.parts_for(gender)
    OUT_NAME = "ch1_%s_cremmy_preview" % gender
    zi = zip_index(zmot)
    print("\n===== ПОЛ %s -> skillclips/%s (%d клипов навыков) =====" % (gender.upper(), OUT_NAME, len(clips_wanted)))

    ref_jbb = S.parse_jbb(zmot.read(zi["cre_cremmy-a_00_0.jbb"]))
    if not ref_jbb:
        print("FAIL parse cre_cremmy-a_00_0")
        return False

    remap = {"G": "M", "G_DUMMY": "M_DUMMY"}     # жен. root → имена муж./cremmy скелета
    clips = [("__ref_ignore", ref_jbb)]
    missing = []
    # ★ Уже выданные индексы неприкосновенны (см. шапку); новым — первый свободный от IDX_BASE.
    prev = dict(out_map.get(gender, {}))
    # ★ 09.10.2026 (п. 6 владельца): «первый свободный» считается не только по своей карте — номера чужих конвейеров
    #   (anim_205 одержимости, 205..224 парные, 225..232 рыбалка) и любые ассеты, уже лежащие в Content вне карты, заняты.
    #   Прежний счёт по одной карте выдал бы мужскому клипу anim_205 и импорт переписал бы клип одержимости.
    clash = {b: i for b, i in prev.items() if i in GUARD.FOREIGN.get(gender, set())}
    if clash:
        raise SystemExit("!! [%s] карта навыков указывает на номера чужих конвейеров: %s — разобрать вручную" % (gender, clash))
    used = set(prev.values()) | GUARD.forbidden(gender, prev.values())
    free = IDX_BASE
    for base in clips_wanted:
        key = (base + ".jbb").lower()
        if key not in zi:
            missing.append(base)
            continue
        jbb = S.parse_jbb(zmot.read(zi[key]))
        if not jbb:
            missing.append(base + " (parse)")
            continue
        for bn in jbb["bones"]:
            nm = bn["name"].strip()
            if nm in remap:
                bn["name"] = remap[nm]
        idx = prev.get(base)
        if idx is None:
            while free in used:
                free += 1
            idx = free
            used.add(idx)
            print("  ★ НОВЫЙ клип")
        clips.append(("anim_%d" % idx, jbb))
        out_map.setdefault(gender, {})[base] = idx
        print("  %-30s -> anim_%d  B=%d dur=%.3fс" % (base, idx, jbb["B"], jbb.get("duration", -1)))
    if missing:
        print("!! НЕТ В АРХИВЕ (%d): %s" % (len(missing), missing))

    # ── сборка скелета/бинда: 1:1 export_char_weaponsets.py ──
    import re
    # ★ дедуп №2 В1 Т11: hair-strand кости частей, resolve_names и слияние мешей — общий asda_char_clips.merge_parts
    #   (части читаются через индекс архива без учёта регистра, как раньше).
    zmi = zip_index(zmodel)
    merged = _CC.merge_parts(T, ref_jbb, PARTS, lambda p: zmodel.read(zmi[(p + ".jbm").lower()]),
                             on_added=lambda p, n: print("  +%d кость(и) hair-strand в скелет для %s" % (n, p)))
    if merged is None:
        return False

    B = ref_jbb["B"]
    # ★ дедуп №2 В1 Т11: recover_binds(merged, B) + trs_refit_binds — общий asda_char_clips.fit_binds
    # ★ 2026-09-11: bind в форму T·R·S (как у превью-тела; меш этого мини-glb при импорте выбрасывается,
    #   правка — для единообразия bind во всех выгрузках скелета). Откат: ASDA_BIND_TRS_REFIT=0.
    IBM_game, resid, ntrs, _nfit, _res = _CC.fit_binds(T, merged, ref_jbb)
    res = [r for r in resid if r is not None]
    print("bind восстановлено: %d/%d костей | resid mean=%.5f max=%.5f" %
          (sum(1 for x in IBM_game if x is not None), B,
           np.mean(res) if res else -1, np.max(res) if res else -1))

    skel_names = {x["name"].strip().lower() for x in ref_jbb["bones"]}
    outside = 0
    for nm, jb in clips[1:]:
        if {x["name"].strip().lower() for x in jb["bones"]} - skel_names:
            outside += 1
    print("клипов с костями вне скелета: %d/%d" % (outside, len(clips) - 1))

    os.makedirs(OUT_DIR, exist_ok=True)
    out = os.path.abspath(os.path.join(OUT_DIR, OUT_NAME + ".glb"))
    # build_fixed(bake_pose=False) + validate — общий asda_char_clips.write_glb (дедуп №2 Т11)
    bind_err, maxr, biggest = _CC.write_glb(T, merged, clips, out, IBM_game)
    print("bind err=%.5f | skin max|vertex|=%.1f (%s)" % (bind_err, maxr, biggest))
    ok = (bind_err < 1.0 and maxr < 500)
    print("ВЫВОД:", "OK" if ok else "ПРОВЕРЬ")
    print("OK ->", out, "| размер:", os.path.getsize(out))
    return ok


def main():
    genders = ["m", "f"]
    if len(sys.argv) > 1:
        sel = [g.lower() for g in sys.argv[1:] if g.lower() in ("m", "f")]
        if sel:
            genders = sel
    per = clip_list()
    zmodel = zipfile.ZipFile(ECC.MODEL_ARC)
    zmot = zipfile.ZipFile(ECC.MOTION_ARC)
    out_map = {}
    if os.path.exists(MAP_JSON):
        out_map = json.load(open(MAP_JSON, encoding="utf-8"))
    allok = True
    for g in genders:
        allok = export_gender(g, zmodel, zmot, per[g], out_map) and allok
    os.makedirs(OUT_DIR, exist_ok=True)
    json.dump(out_map, open(MAP_JSON, "w", encoding="utf-8"), ensure_ascii=False, indent=1, sort_keys=True)
    print("\nкарта клип→anim_NN: %s" % MAP_JSON)
    print("=== ИТОГ:", "ВСЕ OK" if allok else "ЕСТЬ ПРОБЛЕМЫ", "===")


if __name__ == "__main__":
    main()
