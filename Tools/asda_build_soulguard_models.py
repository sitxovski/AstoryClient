# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_soulguard_models.py — ХРАНИТЕЛЬ (Soul Guardian / «ангел»): 9 скиннутых .jbm + клипы .jbb → .glb
#
# Реверс: Reports/soul_guardian_companion_re.md §3.2 (карта «слот → поле записи»), §6.3/§6.4
#   (загрузка набора, формат меша), §4 (таблица 27 записей → 9 моделей).
#   Модели  : Data/Character/Model/CharModel.bnz   — 9 × sb_md_{oh,cb,am}_0{1,2,3}.jbm
#   Анимации: Data/Character/Motion/CharMotion.baz — 99 × sb_md_ani_{fam}_{kind}_{lvl}_{nn}.jbb
#             (+ 8 клипов семейства oh уровня 01 лежат ВНУТРИ CharModel.bnz — ищем в обоих архивах)
#
# ★★ СООТВЕТСТВИЕ МОДЕЛЬ↔КЛИП БЕРЁТСЯ ИЗ SoulGuarderTable.BIN, а НЕ по суффиксу имени.
#   Одну и ту же модель используют ТРИ записи (три SkillType одного семейства), и у них РАЗНЫЕ
#   клипы в слотах «скилл 1 / скилл 2 / скилл 3»:
#       sb_md_oh_01: запись 0 (тип 1) → battle_01_03, запись 3 (тип 2) → battle_01_04,
#                    запись 6 (тип 3) → battle_01_05.
#   ⇒ в .glb кладём ОБЪЕДИНЕНИЕ всех клипов, на которые ссылается ЛЮБАЯ запись этой модели (14 шт),
#   а рантайм выбирает нужный по полю СВОЕЙ записи.
#   ★ Это поправка к §6.5 отчёта («клипы ..._battle_NN_05 ссылок в срезе нет») — ссылки ЕСТЬ,
#     это слоты 8/10/11 записей SkillType 3/6/9. Проверено чтением самой таблицы (см. лог скрипта).
#
# ИМЕНОВАНИЕ КЛИПОВ В .glb: суффикс имени файла без общей части «sb_md_ani_<fam>» (12 символов),
#   т.е. `sb_md_ani_oh_battle_01_03` → `_battle_01_03`. Интерченж соберёт ассет `<модель><суффикс>`
#   = `sb_md_oh_01_battle_01_03`. Рантайм (AAsdaSoulGuardianActor::ClipAssetPath) строит ровно это.
#
# Выход: RawAssets/Models/SoulGuard/<модель в нижнем регистре>.glb  (9 файлов)
# Запуск (вне UE): python asda_build_soulguard_models.py
#
# ★★★ ЗАКОН skin-pipeline-fixed-path: меши хранителей — meshType 0x80 (истинный ДВУХ-ПОЗИЦИОННЫЙ
#   bind, POSITION0/POSITION1 на вершину), поэтому сборка идёт ТОЛЬКО путём
#   parse_jbm_ext → resolve_names → recover_binds → fix_degenerate_binds → prune_unskinned → build_fixed
#   (модуль asda_test_export_one). Старый asda_skinned_to_gltf.build() приближает bind кадром 0 клипа
#   и даёт «растягивание / отлетающие части» (ловили на петах и транспорте). Метрика качества —
#   Tools/_probe_skin_err.py soulguard (норма — единицы, не десятки).
#
# ★ ПРАВИЛО tools-no-import-side-effects: никаких действий при импорте модуля — только через main().
import sys, os, re, struct, zipfile, importlib.util
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_skin_build as SB    # ★ дедуп №2 В1 Т10: цепочка fixed-пути — общий build_fixed_glb

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "..", "RawAssets", "Models", "SoulGuard")
GAME = _P.DATA
MODEL_ARC = GAME + r"\Character\Model\CharModel.bnz"
MOTION_ARC = GAME + r"\Character\Motion\CharMotion.baz"
SGTABLE = os.path.join(HERE, "..", "Content", "AsdaAssets", "Data", "Character", "SoulGuarderTable.BIN")
# ★ 2026-09-11: таблица лежит в RawAssets/Data/Character (в Content её нет — сборщик падал FileNotFoundError).
if not os.path.exists(SGTABLE):
    SGTABLE = os.path.join(HERE, "..", "RawAssets", "Data", "Character", "SoulGuarderTable.BIN")

HDR = 20        # заголовок SoulGuarderTable.BIN: ver | count | fieldCount | "SGT\0" | pad
REC = 1956      # шаг записи (0x7A4)
STR0 = 228      # смещение первой из 27 строк char[64]
# Поле-строка [k] (k = 0..26) → номер МОТИОНА хранителя (см. §3.2 отчёта: слот switch → смещение).
#   [0] — меш; [12..14] — эффекты скиллов; [15..25] — звуки; [26] — портрет.
FIELD_TO_MOTION = {1: 8, 2: 9, 3: 0, 4: 1, 5: 6, 6: 3, 7: 4, 8: 5, 9: 7, 10: 10, 11: 11}
# Клип, используемый как РЕФЕРЕНС для восстановления bind (поле 3 = мотион 0 = «покой A»).
REF_FIELD = 3
ANI_PREFIX_LEN = 12   # длина общей части «sb_md_ani_<fam>» в имени клипа


def load_mod(name, fn):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, fn))
    m = importlib.util.module_from_spec(spec); sys.modules[name] = m; spec.loader.exec_module(m); return m


def load_conv():
    # S = базовые парсеры (.jbb, jointWorld), T = ИСПРАВЛЕННЫЙ скин-путь (двух-позиционный bind).
    S = load_mod("conv", "asda_skinned_to_gltf.py")
    T = load_mod("T", "asda_test_export_one.py")
    return S, T


def parse_soulguarder(path):
    """SoulGuarderTable.BIN → (модель_в_нижнем_регистре → {имя_клипа → набор мотионов}, отчётные строки).

    Возвращается ОБЪЕДИНЕНИЕ клипов по всем записям, использующим модель (см. шапку)."""
    b = open(path, "rb").read()
    ver, cnt, fields = struct.unpack_from("<3i", b, 0)
    assert b[12:15] == b"SGT", "не SoulGuarderTable.BIN (tag=%r)" % b[12:15]

    def s(o):
        raw = b[o:o + 64]
        z = raw.find(b"\0")
        return (raw[:z] if z >= 0 else raw).decode("cp1251", errors="replace")

    models, rows = {}, []
    for i in range(cnt):
        r = HDR + REC * i
        idx, skill_type, row_index, buff = struct.unpack_from("<4i", b, r)
        f = [s(r + STR0 + 64 * k) for k in range(27)]
        model = f[0]
        if not model:
            print("  ! запись %d: пустое поле модели — пропуск" % idx)
            continue
        key = model.lower()
        cur = models.setdefault(key, {"orig": model, "clips": {}, "records": []})
        cur["records"].append(idx)
        for k, motion in sorted(FIELD_TO_MOTION.items()):
            clip = f[k]
            if not clip:
                print("  ! запись %d: поле %d (мотион %d) пустое" % (idx, k, motion))
                continue
            cur["clips"].setdefault(clip.lower(), set()).add(motion)
        rows.append(dict(Index=idx, SkillType=skill_type, RowIndex=row_index, Buff=buff, Model=model))
    return models, rows, cnt


def anim_name(clip_base):
    """Имя клипа в .glb: `sb_md_ani_oh_battle_01_03` → `_battle_01_03` (см. шапку)."""
    return clip_base[ANI_PREFIX_LEN:].lower()


def main():
    conv, T = load_conv()
    models, rows, cnt = parse_soulguarder(SGTABLE)
    print("SoulGuarderTable: записей=%d, уникальных моделей=%d" % (cnt, len(models)))

    zmodel = zipfile.ZipFile(MODEL_ARC)
    zmot = zipfile.ZipFile(MOTION_ARC)
    # .jbm — только в CharModel.bnz. .jbb ищем в ОБОИХ архивах: 8 клипов oh уровня 01 лежат в CharModel.bnz.
    jbm_by_base = {n[:-4].lower(): (zmodel, n) for n in zmodel.namelist() if n.lower().endswith(".jbm")}
    jbb_by_base = {}
    for z in (zmot, zmodel):
        for n in z.namelist():
            if n.lower().endswith(".jbb"):
                jbb_by_base.setdefault(n[:-4].lower(), (z, n))
    print("в архивах: .jbm=%d, .jbb=%d" % (len(jbm_by_base), len(jbb_by_base)))

    os.makedirs(OUT_DIR, exist_ok=True)
    ok = fail = 0
    worst = []
    for key in sorted(models):
        info = models[key]
        if key not in jbm_by_base:
            print("  ! %s: меша нет в CharModel.bnz — пропуск" % info["orig"]); fail += 1; continue
        clip_names = sorted(info["clips"])
        # Референс-клип — «покой A» (мотион 0): он есть у КАЖДОЙ записи и одинаков для модели.
        ref = None
        for cb in clip_names:
            if 0 in info["clips"][cb]:
                ref = cb; break
        if ref is None:
            print("  ! %s: нет клипа мотиона 0 — пропуск" % info["orig"]); fail += 1; continue
        ordered = [ref] + [c for c in clip_names if c != ref]

        clips_meta = []
        for cb in ordered:
            src = jbb_by_base.get(cb)
            if not src:
                print("  ! %s: клипа '%s.jbb' нет ни в одном архиве" % (info["orig"], cb)); continue
            clips_meta.append((cb, src))
        if not clips_meta:
            print("  ! %s: клипов нет — пропуск" % info["orig"]); fail += 1; continue

        try:
            zm, mname = jbm_by_base[key]
            # ★ parse_jbm_ext (а НЕ parse_jbm): читает POSITION0/POSITION1 — истинный bind игры.
            jbm = T.parse_jbm_ext(zm.read(mname))
            if not jbm:
                print("  FAIL %s: parse_jbm_ext вернул пусто" % info["orig"]); fail += 1; continue
            clips = []
            for cb, (z, fn) in clips_meta:
                jbb = conv.parse_jbb(z.read(fn))
                if jbb:
                    clips.append((anim_name(cb), jbb))
                else:
                    print("  ! %s: клип '%s' не распарсился" % (info["orig"], cb))
            if not clips:
                print("  FAIL %s: ни один .jbb не распарсился" % info["orig"]); fail += 1; continue

            # ★ дедуп №2 В1 Т10: цепочка — общий asda_skin_build.build_fixed_glb:
            #   resolve_names (палитра имён → кости референс-клипа) → recover_binds(B, bones) — фит pos0→POSITION
            #   пер-кость с регуляризацией по кадру 0 для неполноранговых костей (иначе bindScale ×N → бэкстоп
            #   build_fixed → возвращается баг «растягивает») → fix_degenerate_binds (кости с <4 фит-пар) →
            #   ★★★ 2026-09-11 trs_refit_binds: bind в форму T·R·S при СТРОГОМ улучшении отклонения от игры в UE
            #   (Reports/mob_ground/REPORT.md, Reports/char_bind/; откат ASDA_BIND_TRS_REFIT=0) →
            #   prune_unskinned → build_fixed → validate.
            out = os.path.join(OUT_DIR, key + ".glb")
            rep = SB.build_fixed_glb(jbm, clips, out, recover_bones=True, fix_degenerate=True)
            be, nfix = rep["bind_err"], rep["degenerate"]
            clips, Bk, B0 = rep["clips"], rep["bones_after"], rep["bones_before"]
            ok += 1
            print("  %-14s кости %d→%d клипов=%d (записи %s) bind_err=%.4f%s"
                  % (info["orig"], B0, Bk, len(clips), info["records"], be,
                     ("  вырожденных вершин=%d" % nfix) if nfix else ""))
            if be > 1.0:
                worst.append((info["orig"], be))
        except Exception as e:
            fail += 1
            print("  FAIL %s: %s" % (info["orig"], e))

    print("[SoulGuard] OK=%d FAIL=%d → %s" % (ok, fail, os.path.abspath(OUT_DIR)))
    if worst:
        print("bind-реконструкция >1.0 (проверить визуально):")
        for m, e in sorted(worst, key=lambda x: -x[1]):
            print("   %-16s err=%.2f" % (m, e))


if __name__ == "__main__":
    main()
