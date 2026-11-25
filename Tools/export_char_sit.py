# Copyright (c) 2026 @sitxovski (Discord)
# Экспорт МИНИ-glb с СИДЯЧИМИ клипами персонажа (посадка на транспорт):
#   ch1_n_sit_00NN_{m|f}_{000,001,002}.jbb  (NN = 0000..0017, 18 наборов × 3 клипа × 2 пола = 108).
# Эталон: CCharacter_SetParts_RidingSystem@0x9EC900 п.5 (mount_system_re.md §2.2) —
#   <EffectEntry>_{M|F}_000 → анимслот 0 (idle сидя), _001 → слот 1 (шаг), _002 → слот 5 (бег);
#   т.е. штатные клипы персонажа ПОДМЕНЯЮТСЯ сидячими, пока игрок на маунте.
# Скелет/бинд строятся ТОЧНО как в export_char_cremmy_move35.py (ref-клип = cre_cremmy-a_00_0 +
#   hair-кости частей) → трек-имена совпадают с существующим ch1_{g}_cremmy_preview_Skeleton.
# В glb: "__ref_ignore" (референс, при импорте ВЫБРАСЫВАЕТСЯ) + N клипов "sit_<NNNN>_<VVV>".
# Существующие glb/ассеты НЕ трогаем: вывод в подпапку sit/.
# Запуск: python export_char_sit.py [m|f]
import os, sys, re, zipfile
import numpy as np
import importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("ECC", os.path.join(HERE, "export_char_cremmy.py"))
ECC = importlib.util.module_from_spec(spec); sys.modules["ECC"] = ECC; spec.loader.exec_module(ECC)
T, S = ECC.T, ECC.S
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import asda_char_clips as _CC   # noqa: E402  ★ дедуп №2 В1 Т11: общая склейка частей / bind-фит / запись мини-glb

OUT_DIR = os.path.join(ECC.OUT_DIR, "sit")
CLIP_RE = re.compile(r"^ch1_n_sit_(\d+)_(m|f)_(\d+)\.jbb$", re.I)


def sit_clips(zmot, gender):
    """[(имя_клипа_в_glb, имя_файла)] для пола, отсортировано по (набор, вариант)."""
    out = []
    for n in zmot.namelist():
        m = CLIP_RE.match(os.path.basename(n))
        if m and m.group(2).lower() == gender:
            out.append(((m.group(1), m.group(3)), "sit_%s_%s" % (m.group(1), m.group(3)), n))
    out.sort()
    return [(nm, fn) for _, nm, fn in out]


def export_gender(gender, zmodel, zmot):
    PARTS = ECC.parts_for(gender)
    OUT_NAME = "ch1_%s_cremmy_preview" % gender
    lst = sit_clips(zmot, gender)
    print("\n===== ПОЛ %s -> sit/%s (сидячих клипов: %d) =====" % (gender.upper(), OUT_NAME, len(lst)))
    if not lst:
        print("FAIL: клипов ch1_n_sit_*_%s_* не найдено" % gender); return False

    # [0] = референс cre_cremmy-a_00_0 (скелет/бинд, при импорте выбрасывается)
    ref_jbb = S.parse_jbb(zmot.read("cre_cremmy-a_00_0.jbb"))
    if not ref_jbb:
        print("FAIL parse cre_cremmy-a_00_0"); return False

    remap = {"G": "M", "G_DUMMY": "M_DUMMY"}   # жен. root → имена муж./cremmy скелета
    clips = [("__ref_ignore", ref_jbb)]
    for name, fn in lst:
        jbb = S.parse_jbb(zmot.read(fn))
        if not jbb:
            print("  FAIL parse", fn); return False
        for bn in jbb['bones']:
            n = bn['name'].strip()
            if n in remap: bn['name'] = remap[n]
        clips.append((name, jbb))
    durs = [c[1].get('duration', -1) for c in clips[1:]]
    print("длительности: min=%.3fс max=%.3fс | костей в клипе=%d" %
          (min(durs), max(durs), clips[1][1]['B']))

    # ── дальше 1:1 логика showcase/move35 (hair-кости + bind-фит + glb) ──
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
    cl_names = {x['name'].strip().lower() for x in clips[1][1]['bones']}
    print("треков сидячего клипа в скелете: %d/%d (лишних в клипе: %d)" %
          (len(cl_names & skel_names), len(cl_names), len(cl_names - skel_names)))

    os.makedirs(OUT_DIR, exist_ok=True)
    out = os.path.abspath(os.path.join(OUT_DIR, OUT_NAME + ".glb"))
    # build_fixed(bake_pose=False) + validate — общий asda_char_clips.write_glb (дедуп №2 Т11)
    bind_err, maxr, biggest = _CC.write_glb(T, merged, clips, out, IBM_game)
    print("bind err=%.5f | skin max|vertex|=%.1f (%s)" % (bind_err, maxr, biggest))
    ok = (bind_err < 1.0 and maxr < 500)
    print("ВЫВОД:", "OK" if ok else "ПРОВЕРЬ")
    print("OK ->", out, "| клипов в glb:", len(clips) - 1, "| размер:", os.path.getsize(out))
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
