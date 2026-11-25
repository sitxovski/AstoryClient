# Copyright (c) 2026 @sitxovski (Discord)
# Экспорт МИНИ-glb с ОДНИМ новым клипом «бег с крыльями» ch4_{m|f}_00_move_00.jbb → anim_35.
# Эталон: charUpdateActionStateTick@0xA216B0 — при bWingOn && !mounted бег-клип полностью подменяется
# треком 35 (move_00). Скелет/бинд строятся ТОЧНО как в export_char_cremmy_showcase.py (ref-клип =
# cre_cremmy-a_00_0 + hair-кости частей) → трек-имена совпадают с существующим ch1_{g}_cremmy_preview_Skeleton.
# В glb 2 анимации: "__ref_ignore" (референс, при импорте ВЫБРАСЫВАЕТСЯ) и "anim_35" (целевая).
# Существующие glb/ассеты НЕ трогаем: вывод в подпапку move35/.
# Запуск: python export_char_cremmy_move35.py [m|f]
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

OUT_DIR = os.path.join(ECC.OUT_DIR, "move35")


def export_gender(gender, zmodel, zmot):
    PARTS = ECC.parts_for(gender)
    OUT_NAME = "ch1_%s_cremmy_preview" % gender
    print("\n===== ПОЛ %s -> move35/%s (клип ch4_%s_00_move_00 → anim_35) =====" % (gender.upper(), OUT_NAME, gender))

    # клипы: [0]=референс cre_cremmy-a_00_0 (скелет/бинд, из импорта выбрасывается), [1]=anim_35
    ref_jbb = S.parse_jbb(zmot.read("cre_cremmy-a_00_0.jbb"))
    if not ref_jbb: print("FAIL parse cre_cremmy-a_00_0"); return False
    mv = S.parse_jbb(zmot.read("ch4_%s_00_move_00.jbb" % gender))
    if not mv: print("FAIL parse ch4_%s_00_move_00" % gender); return False
    remap = {"G": "M", "G_DUMMY": "M_DUMMY"}   # жен. root → имена муж./cremmy скелета
    for bn in mv['bones']:
        n = bn['name'].strip()
        if n in remap: bn['name'] = remap[n]
    dur = mv.get('duration', -1)
    kmax = max(b['keyCount'] for b in mv['bones'])
    print("move_00: B=%d dur=%.3fс fps=%s keyMax=%d" % (mv['B'], dur, mv.get('fps'), kmax))
    clips = [("__ref_ignore", ref_jbb), ("anim_35", mv)]

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

    # сколько костей move_00 замапится на скелет по имени
    skel_names = {x['name'].strip().lower() for x in ref_jbb['bones']}
    mv_names = {x['name'].strip().lower() for x in mv['bones']}
    print("треков move_00 в скелете: %d/%d (лишних в клипе: %d)" %
          (len(mv_names & skel_names), len(mv_names), len(mv_names - skel_names)))

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
