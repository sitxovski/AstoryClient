# Copyright (c) 2026 @sitxovski (Discord)
# Экспорт МИНИ-glb с мировыми клипами ЭМОЦИЙ ch4_{m|f}_00_* → anim_86..97.
#
# РЕВЕРС (`Reports/char_info_window.md` §7, `CChar_SetState@0xA13D50`, entity_char.c:19788-19867):
#   эмоция = состояние 33…46, клип берётся из слота `pModelObj + (state-23)*64`, то есть слоты 10…23.
#   Категория 1 («эмоции») грузится ВСЕГДА из набора `_00` и от надетого оружия НЕ зависит
#   (`CCharacter_LoadMotionAnims@0x9B84D0`), поэтому здесь ровно один набор файлов.
#
#   слот 10 → state 33 → happy01     слот 16 → 39 → battle01
#   слот 11 → state 34 → angry01     слот 18 → 41 → dance01
#   слот 12 → state 35 → sad01       слот 19 → 42 → dance02
#   слот 13 → state 36 → thanks01    слот 21 → 44 → courtship
#   слот 14 → state 37 → hi01        слот 22 → 45 → juggling
#   слот 15 → state 38 → return01    слот 23 → 46 → upperarm
#
# РАСКЛАДКА АССЕТОВ (порт). Занято: 00…28 (char-select/create/мир/атаки), 35 (move), 40…85
#   (наборы оружия), 100+ (клипы навыков). Свободный непрерывный кусок — 86…99, берём 86…97
#   В ТОМ ЖЕ ПОРЯДКЕ, что слоты эталона:
#     86 happy01  87 angry01  88 sad01   89 thanks01  90 hi01     91 return01
#     92 battle01 93 dance01  94 dance02 95 courtship 96 juggling 97 upperarm
#
# Скелет/бинд строятся ТОЧНО как в `export_char_weaponsets.py` (ref-клип = cre_cremmy-a_00_0 +
#   hair-кости частей) → трек-имена совпадают с существующим ch1_{g}_cremmy_preview_Skeleton.
# В glb первым идёт "__ref_ignore" (референс, при импорте ВЫБРАСЫВАЕТСЯ), затем целевые anim_NN.
# Существующие glb/ассеты НЕ трогаем: вывод в подпапку emotions/.
# Запуск: python export_char_emotions.py [m|f]
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

OUT_DIR = os.path.join(ECC.OUT_DIR, "emotions")

# (имя клипа в наборе _00, индекс ассета anim_NN, состояние эталона)
PLAN = [
    ("happy01",   86, 33),
    ("angry01",   87, 34),
    ("sad01",     88, 35),
    ("thanks01",  89, 36),
    ("hi01",      90, 37),
    ("return01",  91, 38),
    ("battle01",  92, 39),
    ("dance01",   93, 41),
    ("dance02",   94, 42),
    ("courtship", 95, 44),
    ("juggling",  96, 45),
    ("upperarm",  97, 46),
]


def export_gender(gender, zmodel, zmot):
    PARTS = ECC.parts_for(gender)
    OUT_NAME = "ch1_%s_cremmy_preview" % gender
    print("\n===== ПОЛ %s -> emotions/%s (%d клипов эмоций) =====" % (gender.upper(), OUT_NAME, len(PLAN)))

    ref_jbb = S.parse_jbb(zmot.read("cre_cremmy-a_00_0.jbb"))
    if not ref_jbb:
        print("FAIL parse cre_cremmy-a_00_0"); return False

    remap = {"G": "M", "G_DUMMY": "M_DUMMY"}   # жен. root → имена муж./cremmy скелета
    clips = [("__ref_ignore", ref_jbb)]
    for (act, idx, state) in PLAN:
        name = "ch4_%s_00_%s.jbb" % (gender, act)
        try:
            raw = zmot.read(name)
        except KeyError:
            print("FAIL нет в архиве:", name); return False
        jbb = S.parse_jbb(raw)
        if not jbb:
            print("FAIL parse", name); return False
        for bn in jbb['bones']:
            n = bn['name'].strip()
            if n in remap: bn['name'] = remap[n]
        clips.append(("anim_%02d" % idx, jbb))
        print("  %-26s -> anim_%02d (state %d)  B=%d dur=%.3fс"
              % (name, idx, state, jbb['B'], jbb.get('duration', -1)))

    # ── дальше 1:1 логика showcase/wsets (hair-кости + bind-фит + glb) ──
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
    zmot = zipfile.ZipFile(ECC.MOTION_ARC)
    allok = True
    for g in genders:
        allok = export_gender(g, zmodel, zmot) and allok
    print("\n=== ИТОГ:", "ВСЕ OK" if allok else "ЕСТЬ ПРОБЛЕМЫ", "===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
