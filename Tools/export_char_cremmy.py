# Copyright (c) 2026 @sitxovski (Discord)
# Экспорт превью-персонажа char-select: 5 базовых частей набора (chst/pant/shoe/face/hair)
# СКИННУТЫХ на ОБЩИЙ гуманоидный скелет cre_cremmy (50 костей) → ОДИН skinned .glb с 5 клипами.
# Параметризован по ПОЛУ: m (ch1_m_*) и f (ch1_f_*).
#
# ★★ ИСПРАВЛЕННЫЙ bind (asda_test_export_one.py::recover_binds/build_fixed):
#   Части ch1_?_* авторены в T-позе (руки раскинуты горизонтально), а frame0 клипа cremmy = idle
#   (руки опущены). Старый экспортёр (asda_skinned_to_gltf) брал invBind=inv(frame0 клипа) — НЕВЕРНО:
#   на idle меш висел в авторской T-позе (руки горизонтально). Игра хранит per-vertex POSITION0/POSITION1
#   (забинженные bind-локальные позиции); recover_binds ВОССТАНАВЛИВАЕТ истинный bind пер-кость фитом
#   pos0->POSITION_j (resid ~0.00002). Тогда idle-поза скелета РЕАЛЬНО применяется к мешу (руки опускаются).
#   Метод валидирован на NPC (два-позиционный bind, npc_re_3_skinbind.md).
#
# Базовый набор (equip=0) подтверждён эталоном (charselect_render.md §1.3):
#   ch1_{m|f}_00_{chst,pant,shoe,face,hair}001_00_00_0.jbm — все существуют в CharModel.bnz (проверено).
# Волосы/лицо-варианты: CCharacter_LoadHairModel@0x9E2700 hardcode ch1_{m|f}_00_hair00{N}, N=hairStyle+1.
#
# Запуск:
#   python export_char_cremmy.py        # оба пола (m + f)
#   python export_char_cremmy.py m      # только мужской
#   python export_char_cremmy.py f      # только женский
import os, importlib.util, zipfile, sys
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = _P.DATA
MODEL_ARC  = GAME + r"\Character\Model\CharModel.bnz"
MOTION_ARC = GAME + r"\Character\Motion\CharMotion.baz"
OUT_DIR = os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "Characters")

SKELETON = "cre_cremmy"

# ★★★ РЕФЕРЕНС-КЛИП = СКЕЛЕТ ВЫВОДА. `build_fixed` берёт список костей из clips[0] и мапит кости
#   остальных клипов К НЕМУ ПО ИМЕНИ. Скелет превью в порту ЕДИН для обоих полов — мужской
#   `cre_cremmy` (50 костей, корень `M`) плюс hair-кости, добавляемые из женской причёски; на нём
#   уже лежат ВСЕ импортированные клипы обоих полов (мировые, оружейные, эмоции, навыки — все они
#   женские наборы кладут через ремап `G→M`). Поэтому референс НЕ ЗАВИСИТ от пола и всегда равен
#   `cre_cremmy-a_00_0`, а идёт он первым под служебным именем `__ref_ignore` (та же конвенция, что
#   в `export_char_emotions.py`; импортёр эту анимацию выбрасывает).
REF_CLIP = "cre_cremmy-a_00_0.jbb"
# Женские наборы авторены на своём скелете (54 кости, корень `G`/`G_DUMMY` + hair 4_a1..3/4_a_End).
#   Ремап корня на имена мужского скелета — ровно то, что уже делают showcase/эмоции/оружие.
CLIP_BONE_REMAP = {"G": "M", "G_DUMMY": "M_DUMMY"}

# ★ РЕАЛЬНЫЙ клип char-select (эталон: entityLoadCremmyMorphModel@0xF05030 → CEntity_LoadCreatureMorphModels_
#   Cremmy@0xF05150 грузит 5 наборов кейфреймов; worldModelsUpdateAndRender@0xF07890 L531231 крутит
#   трек = индекс слота, дефолт 0). ⇒ клип 0 = idle (руки вниз, почти статичен). НЕ dr_ch_ani — тот из
#   ch4-пути (Char_LoadMotionAnim@0x9B84D0) = in-world персонаж, в шкатулке НЕ играет; его кадр0 выносил
#   кисти вперёд (2_a_End Z +29 vs −8) и швырял 8_a_End вбок-вниз → «руль велосипеда».
#   См. Reports/charskin_deep.md.
#
# ★★★ ИСПРАВЛЕНО 2026-09-02 (ревизия `fix_possess_scene_ревизия.md`, находка R1): набор НЕ УНИСЕКС.
#   Эталон `CEntity_LoadCreatureMorphModels_Cremmy@0xF05150` (`RE/etalon/file_vfs.c:1422-1438`)
#   держит ДВЕ таблицы имён и выбирает по полу персонажа (`morphBase+1079 == 1` = мужской):
#     мужской → cre_cremmy-a_00_{0..4}      женский → cre_crewmy-a_00_{0..4}
#   Прежний код отдавал `cre_cremmy` ОБОИМ полам («УНИСЕКС»), то есть женское превью анимировалось
#   мужским клипом. Это не косметика: позиция «встал рядом» в паре с персонажем соулмейта живёт в
#   КОРНЕ клипа, и подмена ломала её у половины игроков. Измерено (кадр 0 корня):
#     cre_cremmy-a_00_0 (−9.17, 82.65,   3.97) · cre_crewmy-a_00_0 ( 0.39, 83.68, −14.73)
#     авторский зазор пары «я Ж + он М» = cre_crewmman-a_00_0 (37.53, 83.59, −45.36) − crewmy
#       = (37.14, −0.09, −30.63), длина 48.1
#     с подставленным мужским клипом                = (46.70, −0.94, −49.33), длина 67.9 → в 1.4 раза
#       дальше по прямой и в 1.6 раза по глубине.
def clips_for(gender):
    base = "cre_cremmy" if gender == "m" else "cre_crewmy"
    return ["%s-a_00_%d.jbb" % (base, i) for i in range(0, 5)]

# базовые части по полу (hair001/face001 = дефолт hairStyle=0)
def parts_for(gender):
    return ["ch1_%s_00_chst001_00_00_0" % gender, "ch1_%s_00_pant001_00_00_0" % gender,
            "ch1_%s_00_shoe001_00_00_0" % gender, "ch1_%s_00_face001_00_00_0" % gender,
            "ch1_%s_00_hair001_00_00_0" % gender]

# проверенный движок исправленного bind + базовый (.jbb парс)
spec = importlib.util.spec_from_file_location("T", os.path.join(HERE, "asda_test_export_one.py"))
T = importlib.util.module_from_spec(spec); sys.modules["T"] = T; spec.loader.exec_module(T)
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import asda_char_clips as _CC   # noqa: E402  ★ дедуп №2 В1 Т11: общая склейка частей / bind-фит / запись мини-glb
import asda_skinned_to_gltf as S


def export_gender(gender, zmodel, zmot):
    PARTS = parts_for(gender)
    OUT_NAME = "ch1_%s_cremmy_preview" % gender
    print("\n===== ПОЛ %s -> %s =====" % (gender.upper(), OUT_NAME))

    # Первым — референс-скелет (см. REF_CLIP): он задаёт кости вывода и в ассеты не попадает.
    ref_jbb = S.parse_jbb(zmot.read(REF_CLIP))
    if not ref_jbb: print("FAIL parse ref", REF_CLIP); return False
    clips = [("__ref_ignore", ref_jbb)]
    for cn in clips_for(gender):
        jbb = S.parse_jbb(zmot.read(cn))
        if not jbb: print("FAIL parse clip", cn); return False
        for bn in jbb['bones']:                       # женский набор → имена костей мужского скелета
            n = bn['name'].strip()
            if n in CLIP_BONE_REMAP: bn['name'] = CLIP_BONE_REMAP[n]
        N = int(os.path.splitext(cn)[0].rsplit("_", 1)[1])
        clips.append(("anim_%02d" % N, jbb))
    B = ref_jbb['B']
    print("скелет %s: B=%d dur=%.2fс, клипов=%d (%s)"
          % (SKELETON, B, ref_jbb['duration'], len(clips) - 1, ", ".join(clips_for(gender))))

    # имена костей cremmy; hair-strand кости (женская hair001 = 4_a1/4_a2/4_a3) ОТСУТСТВУЮТ в скелете
    # cremmy (тело не несёт hair-physics костей). Игра грузит волосы отдельным part'ом с СОБСТВЕННЫМИ
    # костями, прикреплёнными к head-кости "4". В превью-скелет ДОБАВЛЯЕМ недостающие кости как листья
    # под их родителя (по имени <parent>_aN → parent), bind восстанавливается фитом pos0->POSITION.
    # Клипы cremmy их не двигают → остаются в rest-позе волос = ровно как idle в игре.
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

    B = ref_jbb['B']   # ре-синк: hair-strand кости (жен. причёска) добавлены в скелет выше → B вырос
    # ★ дедуп №2 В1 Т11: recover_binds(merged, B) + trs_refit_binds — общий asda_char_clips.fit_binds
    # ★★★ 2026-09-11: bind в форму T·R·S там, где это СТРОГО уменьшает отклонение от игры в UE
    #   (узел glTF не несёт сдвига, UE берёт обратную bind из ref-скелета — Reports/mob_ground/REPORT.md).
    #   Замер персонажа и экипировки: Reports/char_bind/. Откат: ASDA_BIND_TRS_REFIT=0.
    IBM_game, resid, ntrs, _nfit, _res = _CC.fit_binds(T, merged, ref_jbb)
    if ntrs: print("bind TRS-перефит костей: %d (макс. было %.2f -> стало %.2f)"
                   % (len(ntrs), max(a for _n, a, _b in ntrs), max(b for _n, _a, b in ntrs)))
    nfit = sum(1 for x in IBM_game if x is not None)
    res = [r for r in resid if r is not None]
    print("bind восстановлено: %d/%d костей | resid mean=%.5f max=%.5f (норма ~0)" %
          (nfit, B, np.mean(res) if res else -1, np.max(res) if res else -1))

    # ★ НЕ ПРУНИМ: тело-лидер должен нести ПОЛНЫЙ ~50-костный скелет (вкл. кости кисти 8/_8/7_b1/_7_b1).
    #   Прунинг вырезал их (у «голого» cremmy нет геометрии кистей — в оригинале кисти = перчатки grov),
    #   из-за чего LeaderPose не гнал перчатки (их кости отсутствовали в лидере) → перчатки статичны.
    #   Как в export_equip_parts.py: полный скелет одинаков для лидера и всех частей, безвесовые кости мешу не вредят.
    clips2, IBM2, Bk, B0 = clips, IBM_game, B, B
    print("скелет: %d костей (прунинг ОТКЛЮЧЁН — полный скелет для LeaderPose перчаток)" % B)

    os.makedirs(OUT_DIR, exist_ok=True)
    out = os.path.abspath(os.path.join(OUT_DIR, OUT_NAME + ".glb"))
    # bake_pose=False → НАСТОЯЩИЙ рантайм-скиннинг: ref-поза = bind (T-поза), idle-клип cre_cremmy-a_00_0 играет
    #   как AnimSequence. Реконструированный IBM даёт тождество pos0·IBM_j·Bone_j(t) = POSITION_j·Bone_j(t)
    #   (2-позиционная формула Asda), поэтому LBS UE воспроизводит игру точно (bind_err≈0). Запекание убрано —
    #   персонаж «дышит» idle-анимой, как в оригинале, а не стоит статично.
    # build_fixed(bake_pose=False) + validate — общий asda_char_clips.write_glb (дедуп №2 Т11)
    bind_err, maxr, biggest = _CC.write_glb(T, merged, clips2, out, IBM2)
    print("реконструкция bind pos0·IBM vs POSITION:  err=%.5f (норма ~0 = верно)" % bind_err)
    print("скиннинг кадра0 в glTF: max|vertex|=%.1f (меш %s) — должно ~ размер модели (десятки)" % (maxr, biggest))
    ok = (bind_err < 1.0 and maxr < 500)
    print("ВЫВОД:", "OK" if ok else "ПРОВЕРЬ")
    print("OK ->", out, "| мешей:", len(merged['meshes']), "| размер:", os.path.getsize(out))
    return ok


def main():
    genders = ["m", "f"]
    if len(sys.argv) > 1:
        genders = [g.lower() for g in sys.argv[1:] if g.lower() in ("m", "f")]
    zmodel = zipfile.ZipFile(MODEL_ARC)
    zmot   = zipfile.ZipFile(MOTION_ARC)
    allok = True
    for g in genders:
        allok = export_gender(g, zmodel, zmot) and allok
    print("\n=== ИТОГ:", "ВСЕ OK" if allok else "ЕСТЬ ПРОБЛЕМЫ", "===")

if __name__ == "__main__":
    main()
