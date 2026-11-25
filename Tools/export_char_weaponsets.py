# Copyright (c) 2026 @sitxovski (Discord)
# Экспорт МИНИ-glb с мировыми клипами НАБОРОВ ОРУЖИЯ ch4_{m|f}_{SS}_* → anim_40..85.
#
# РЕВЕРС (CCharacter_LoadMotionAnims@0x9B84D0, entity_char.c:4597): набор движений персонажа выбирается
#   по ItemRecord[33] (=ItemSubType, оффсет 132) НАДЕТОГО ОРУЖИЯ:
#     0,4→_01  1→_01  2→_05  3→_02  5→_06  6→_09  8,10→_03  9→_04  11→_08  нет оружия/99→_00
#   Меняются ТОЛЬКО клипы категорий 0 (покой) и 2 (движение) + 3 (атака, уже импортирована как anim_20..28).
#   Категории 1 (эмоции) и 4 (смерть) ВСЕГДА из набора _00 — их доимпортировать не нужно.
#   Слоты: 0=idle01, 1=idle02, 2=idle03, 4=walk01, 5=run01, 24=back01;
#          у набора _08 (баллиста, animClass 11) слот 0 = idle05, а ещё грузятся 8=idle04, 9=idle06.
#
# РАСКЛАДКА АССЕТОВ (порт): набор _00 уже импортирован как anim_12..19. Новые:
#     _01→40..45  _02→46..51  _03→52..57  _04→58..63  _05→64..69  _06→70..75  _08→76..81
#     порядок в шестёрке: idle01, idle02, idle03, walk01, run01, back01
#     _08 доп.: idle04→82, idle05→83, idle06→84 ; _09: idle01→85 (в архиве у _09 БОЛЬШЕ НИЧЕГО НЕТ).
#
# Скелет/бинд строятся ТОЧНО как в export_char_cremmy_showcase.py (ref-клип = cre_cremmy-a_00_0 +
#   hair-кости частей) → трек-имена совпадают с существующим ch1_{g}_cremmy_preview_Skeleton.
# В glb первым идёт "__ref_ignore" (референс, при импорте ВЫБРАСЫВАЕТСЯ), затем целевые anim_NN.
# Существующие glb/ассеты НЕ трогаем: вывод в подпапку wsets/.
# Запуск: python export_char_weaponsets.py [m|f]
import os, sys, zipfile
import numpy as np
import importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("ECC", os.path.join(HERE, "export_char_cremmy.py"))
ECC = importlib.util.module_from_spec(spec); sys.modules["ECC"] = ECC; spec.loader.exec_module(ECC)
T, S = ECC.T, ECC.S
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import asda_char_clips as _CC   # noqa: E402  ★ дедуп №2 В1 Т11: общая склейка частей / bind-фит / запись мини-glb

OUT_DIR = os.path.join(ECC.OUT_DIR, "wsets")

SIX = ["idle01", "idle02", "idle03", "walk01", "run01", "back01"]
# (набор, клип) -> индекс ассета anim_NN
PLAN = []
for k, s in enumerate(["01", "02", "03", "04", "05", "06", "08"]):
    for j, act in enumerate(SIX):
        PLAN.append((s, act, 40 + 6 * k + j))
PLAN += [("08", "idle04", 82), ("08", "idle05", 83), ("08", "idle06", 84), ("09", "idle01", 85)]


def export_gender(gender, zmodel, zmot):
    PARTS = ECC.parts_for(gender)
    OUT_NAME = "ch1_%s_cremmy_preview" % gender
    print("\n===== ПОЛ %s -> wsets/%s (%d клипов наборов оружия) =====" % (gender.upper(), OUT_NAME, len(PLAN)))

    ref_jbb = S.parse_jbb(zmot.read("cre_cremmy-a_00_0.jbb"))
    if not ref_jbb: print("FAIL parse cre_cremmy-a_00_0"); return False

    remap = {"G": "M", "G_DUMMY": "M_DUMMY"}   # жен. root → имена муж./cremmy скелета
    clips = [("__ref_ignore", ref_jbb)]
    for (s, act, idx) in PLAN:
        name = "ch4_%s_%s_%s.jbb" % (gender, s, act)
        try:
            raw = zmot.read(name)
        except KeyError:
            print("FAIL нет в архиве:", name); return False
        jbb = S.parse_jbb(raw)
        if not jbb: print("FAIL parse", name); return False
        for bn in jbb['bones']:
            n = bn['name'].strip()
            if n in remap: bn['name'] = remap[n]
        clips.append(("anim_%02d" % idx, jbb))
        print("  %-26s -> anim_%02d  B=%d dur=%.3fс" % (name, idx, jbb['B'], jbb.get('duration', -1)))

    # ── дальше 1:1 логика showcase (hair-кости + bind-фит + glb) ──
    import re
    # ★ дедуп №2 В1 Т11: hair-strand кости частей (листья родителя «<родитель>_aN»), resolve_names и слияние мешей —
    #   общий asda_char_clips.merge_parts (при сбое разбора части печатает «FAIL parse part <p>»).
    merged = _CC.merge_parts(T, ref_jbb, PARTS, lambda p: zmodel.read(p + ".jbm"),
                             on_added=lambda p, n: print("  +%d кость(и) hair-strand в скелет для %s" % (n, p)))
    if merged is None: return False

    B = ref_jbb['B']
    # ★ дедуп №2 В1 Т11: recover_binds(merged, B) + trs_refit_binds — общий asda_char_clips.fit_binds
    # ★ 2026-09-11: bind в форму T·R·S (как у превью-тела; меш этого мини-glb при импорте выбрасывается,
    #   правка — для единообразия bind во всех выгрузках скелета). Откат: ASDA_BIND_TRS_REFIT=0.
    IBM_game, resid, ntrs, _nfit, _res = _CC.fit_binds(T, merged, ref_jbb)
    nfit = sum(1 for x in IBM_game if x is not None)
    res = [r for r in resid if r is not None]
    print("bind восстановлено: %d/%d костей | resid mean=%.5f max=%.5f" %
          (nfit, B, np.mean(res) if res else -1, np.max(res) if res else -1))

    skel_names = {x['name'].strip().lower() for x in ref_jbb['bones']}
    miss = 0
    for nm, jb in clips[1:]:
        cn = {x['name'].strip().lower() for x in jb['bones']}
        if cn - skel_names: miss += 1
    print("клипов с костями вне скелета: %d/%d" % (miss, len(clips) - 1))

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
        genders = [g.lower() for g in sys.argv[1:] if g.lower() in ("m", "f")]
    zmodel = zipfile.ZipFile(ECC.MODEL_ARC)
    zmot   = zipfile.ZipFile(ECC.MOTION_ARC)
    allok = True
    for g in genders:
        allok = export_gender(g, zmodel, zmot) and allok
    print("\n=== ИТОГ:", "ВСЕ OK" if allok else "ЕСТЬ ПРОБЛЕМЫ", "===")


if __name__ == "__main__":
    main()
