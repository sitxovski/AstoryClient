# Copyright (c) 2026 @sitxovski (Discord)
# Экспорт превью-персонажа ch1_{m|f}_cremmy_preview.glb с РАСШИРЕННЫМ набором клипов:
#   anim_00..04 = cre_cremmy-a_00_{0..4}   (char-select, как раньше — индексы НЕ меняются)
#   anim_05..11 = cre_creman-a_00_{0..6}   (муж) / cre_crewoman-a_00_{0..6} (жен) — набор CHAR-CREATE
# Эталон: CCreature_LoadCreManAnims @0xF10050 грузит 7 клипов creman/crewoman в слоты self[54]+(i<<6);
#   тик char-create играет трек animState (char+118382): 0=idle, Chest→4, Gloves→3, Pants→5, Shoes→6,
#   Helmet→2 (uiCharPartSelectCycle @0xF12910), one-shot → возврат к 0 (ICONGRID_PREVIEW_RE.md).
# Жен. клипы crewoman имеют root "G"/"G_DUMMY" (вместо "M"/"M_DUMMY") + hair-кости 4_a1/4_a2/4_a3/4_a_End
#   в середине списка. build_fixed сопоставляет кости ПО ИМЕНИ → достаточно ремапа G→M, G_DUMMY→M_DUMMY;
#   hair-треки подцепятся к добавленным в скелет hair-костям (4_a1..3), отсутствующие (4_a_End) игнорируются.
# Запуск: python export_char_cremmy_showcase.py [m|f]
import os, sys, zipfile
import numpy as np
import importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("ECC", os.path.join(HERE, "export_char_cremmy.py"))
ECC = importlib.util.module_from_spec(spec); sys.modules["ECC"] = ECC; spec.loader.exec_module(ECC)
T, S = ECC.T, ECC.S   # проверенные движки экспорта (bind-фит + glTF)
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import asda_char_clips as _CC   # noqa: E402  ★ дедуп №2 В1 Т11: общая склейка частей / bind-фит / запись мини-glb

OUT_DIR = ECC.OUT_DIR


# Мировые клипы игрока (реверс player_anim_appearance_re.md): ch4_{g}_00_* из CharMotion.baz.
#   В set 00 ЕСТЬ: idle01-03, sitdown01, walk01, run01, death01, back01 (атака normalattack —
#   только в наборах оружия _01.._09, добавится на фазе боя; idle04/06 в _00 отсутствуют).
#   Ассеты: anim_12=idle01(слот0) 13=idle02 14=idle03 15=sitdown01(3) 16=walk01(4) 17=run01(5)
#   18=death01(7) 19=back01(24). Скелет ch4 = cremmy (муж 50 M-root; жен G-root — ремап).
WORLD_CLIPS = ["idle01", "idle02", "idle03", "sitdown01", "walk01", "run01", "death01", "back01"]

# ★ АТАКИ по наборам оружия (реверс Char_LoadMotionAnim@0x9B9890: weaponSubType → группа/файл):
#   ассеты anim_20..28; маппинг subType→anim: 0/4→20, 1→21, 3→22, 8/10→23, 9→24, 2→25, 5→26, 11→27, 6→28.
ATTACK_CLIPS = [
    ("01", "normalattack01"),   # anim_20: мечи (subType 0/4)
    ("01", "normalattack03"),   # anim_21: двуруч (subType 1)
    ("02", "normalattack01"),   # anim_22: лук (subType 3)
    ("03", "normalattack01"),   # anim_23: subType 8/10
    ("04", "normalattack01"),   # anim_24: subType 9
    ("05", "normalattack01"),   # anim_25: копьё (subType 2)
    ("06", "normalattack01"),   # anim_26: посох (subType 5)
    ("08", "normalattack02"),   # anim_27: subType 11
    ("09", "normalattack01"),   # anim_28: subType 6
]

def load_clips(gender, zmot):
    """Клипы: cremmy/crewmy 0..4 + creman/crewoman 5..11 (showcase) + ch4 set00 мировые 12..19 + атаки 20..28.

    Первым в списке идёт `__ref_ignore` = `cre_cremmy-a_00_0`: `build_fixed` берёт СКЕЛЕТ из clips[0],
    а скелет превью в порту един для обоих полов (мужской cremmy + hair-кости причёски). Импортёр
    эту служебную анимацию выбрасывает.
    """
    remap = dict(ECC.CLIP_BONE_REMAP)          # жен. root → имена муж./cremmy скелета
    ref = S.parse_jbb(zmot.read(ECC.REF_CLIP))
    if not ref: return None
    clips = [("__ref_ignore", ref)]
    # ★★★ ИСПРАВЛЕНО 2026-09-02 (ревизия, находка R1): анимации char-select НЕ унисекс — эталон
    #   `CEntity_LoadCreatureMorphModels_Cremmy@0xF05150` грузит женскому персонажу `cre_crewmy`.
    #   Обоснование и измерения — в шапке `clips_for()` в `export_char_cremmy.py`.
    for i, cn in enumerate(ECC.clips_for(gender)):
        jbb = S.parse_jbb(zmot.read(cn))
        if not jbb: return None
        for bn in jbb['bones']:
            n = bn['name'].strip()
            if n in remap: bn['name'] = remap[n]
        clips.append(("anim_%02d" % i, jbb))
    pfx = "cre_creman" if gender == "m" else "cre_crewoman"
    for i in range(0, 7):
        jbb = S.parse_jbb(zmot.read("%s-a_00_%d.jbb" % (pfx, i)))
        if not jbb: return None
        for bn in jbb['bones']:
            n = bn['name'].strip()
            if n in remap: bn['name'] = remap[n]
        clips.append(("anim_%02d" % (5 + i), jbb))
    for k, act in enumerate(WORLD_CLIPS):
        jbb = S.parse_jbb(zmot.read("ch4_%s_00_%s.jbb" % (gender, act)))
        if not jbb: return None
        for bn in jbb['bones']:
            n = bn['name'].strip()
            if n in remap: bn['name'] = remap[n]
        clips.append(("anim_%02d" % (12 + k), jbb))
    for k, (grp, act) in enumerate(ATTACK_CLIPS):
        jbb = S.parse_jbb(zmot.read("ch4_%s_%s_%s.jbb" % (gender, grp, act)))
        if not jbb: return None
        for bn in jbb['bones']:
            n = bn['name'].strip()
            if n in remap: bn['name'] = remap[n]
        clips.append(("anim_%02d" % (20 + k), jbb))
    return clips


def export_gender(gender, zmodel, zmot):
    PARTS = ECC.parts_for(gender)
    OUT_NAME = "ch1_%s_cremmy_preview" % gender
    print("\n===== ПОЛ %s -> %s (12 клипов: 5 cremmy + 7 creman showcase) =====" % (gender.upper(), OUT_NAME))

    clips = load_clips(gender, zmot)
    if not clips: print("FAIL загрузка клипов"); return False
    ref_jbb = clips[0][1]; B = ref_jbb['B']
    print("скелет cre_cremmy: B=%d, клипов=%d (%s)" % (B, len(clips), ", ".join(n for n, _ in clips)))

    # ── дальше 1:1 логика export_char_cremmy.export_gender (hair-кости + bind-фит + glb) ──
    import re
    def _part_check(p, jbm):
        """Журнал резолва костей части (прежние строки)."""
        me = jbm['meshes'][0]
        bnames = {x['name'].strip().lower() for x in ref_jbb['bones']}
        matched = sum(1 for n in me['names'] if n.strip().lower() in bnames)
        print("  %-28s names=%d matched=%d" % (p, len(me['names']), matched))
        if len(me['names']) and matched != len(me['names']):
            print("  !! часть %s: не все кости резолвнулись" % p)
    # ★ дедуп №2 В1 Т11: hair-strand кости частей (листья родителя «<родитель>_aN»), resolve_names и слияние мешей —
    #   общий asda_char_clips.merge_parts (при сбое разбора части печатает «FAIL parse part <p>»).
    merged = _CC.merge_parts(T, ref_jbb, PARTS, lambda p: zmodel.read(p + ".jbm"),
                             on_added=lambda p, n: print("  +%d кость(и) hair-strand в скелет для %s" % (n, p)),
                             on_part=_part_check)
    if merged is None: return False

    B = ref_jbb['B']
    # ★ дедуп №2 В1 Т11: recover_binds(merged, B) + trs_refit_binds — общий asda_char_clips.fit_binds
    # ★ 2026-09-11: bind в форму T·R·S (как у превью-тела). Откат: ASDA_BIND_TRS_REFIT=0.
    IBM_game, resid, ntrs, _nfit, _res = _CC.fit_binds(T, merged, ref_jbb)
    nfit = sum(1 for x in IBM_game if x is not None)
    res = [r for r in resid if r is not None]
    print("bind восстановлено: %d/%d костей | resid mean=%.5f max=%.5f" %
          (nfit, B, np.mean(res) if res else -1, np.max(res) if res else -1))

    clips2, IBM2 = clips, IBM_game   # без прунинга (полный скелет для LeaderPose перчаток)
    os.makedirs(OUT_DIR, exist_ok=True)
    out = os.path.abspath(os.path.join(OUT_DIR, OUT_NAME + ".glb"))
    # build_fixed(bake_pose=False) + validate — общий asda_char_clips.write_glb (дедуп №2 Т11)
    bind_err, maxr, biggest = _CC.write_glb(T, merged, clips2, out, IBM2)
    print("bind err=%.5f | skin max|vertex|=%.1f (%s)" % (bind_err, maxr, biggest))
    ok = (bind_err < 1.0 and maxr < 500)
    print("ВЫВОД:", "OK" if ok else "ПРОВЕРЬ")
    print("OK ->", out, "| мешей:", len(merged['meshes']), "| размер:", os.path.getsize(out))
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
