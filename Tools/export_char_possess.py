# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Экспорт клипов и моделей ФИЧИ «ОДЕРЖИМОСТЬ» (persona соулмейта на экране выбора персонажа).
#
# ЧТО ДЕЛАЕТ (три набора, все — из оригинального клиента, ничего не выдумано):
#   1) ПАРНЫЕ КЛИПЫ «стоят рядом» — эталон `CEntity_LoadCreatureMorphMotions@0xF05BF0`
#      (`RE/etalon/file_vfs.c:1489-1553`) грузит персонажу соулмейта 5 клипов, а имя набора
#      выбирает по ДВУМ полам сразу (мой пол × его пол):
#        М+М → cre_cremmman-a_00_{0..4}     М+Ж → cre_cremwwoman-a_00_{0..4}
#        Ж+М → cre_crewmman-a_00_{0..4}     Ж+Ж → cre_crewwwoman-a_00_{0..4}
#      ★★★ Именно КОРЕНЬ этих клипов несёт смещение «встал рядом»: собственной матрицы у объекта
#      партнёра в оригинале нет (`channelSelectRender3D@0xF07BE0` позиционирует только моего
#      персонажа). Без клипа партнёр встаёт ВНУТРЬ моего — поэтому клип обязателен.
#   2) ЖЕНСКИЙ НАБОР МОЕГО ПЕРСОНАЖА `cre_crewmy-a_00_{0..4}` (anim_00..04 пола `f`).
#      ★ Дефект порта, находка R1 ревизии: экспортёр был «унисекс» и анимировал женское превью
#      МУЖСКИМ `cre_cremmy`, тогда как эталон `CEntity_LoadCreatureMorphModels_Cremmy@0xF05150`
#      грузит женщине `cre_crewmy`. Это ломало ту самую парную геометрию (см. п.1) у половины
#      игроков. Здесь клипы кладутся НА СУЩЕСТВУЮЩИЙ скелет — переимпорт превью не нужен.
#   3) «ДУША» (полупрозрачный светящийся силуэт) — заглушка слота БЕЗ персонажа соулмейта. Эталон
#      `CEntity_LoadCreatureMorphModels_Cremmman@0xF05930` (`file_vfs.c:1447-1486`) грузит объекту
#      `+946128` по МОЕМУ полу набор `cre_cremmman-a_00_{0..3}` + `cre_cremmsoul-a_00_4`
#      (мужчина) либо `cre_crewwwoman-a_00_{0..3}` + `cre_crewwsoul-a_00_4` (женщина). Меши —
#      `cre_cresoulman/woman-a_00.jbm`, текстура ТЕЛА — СВОЯ у меша: `cre_cresoulman-a_00.TEX`
#      (синяя) / `cre_cresoulwoman-a_00.TEX` (бирюза), это и есть цвет силуэта.
#      ★★★ ВНИМАНИЕ: `silhoutte_tex_blue.TEX` (VA 0x14368AC) — СЕРАЯ дымка, в оригинале шла на
#      ОТДЕЛЬНЫЙ оверлей-объект `+0xE6FE4`, а НЕ на тело; ошибочная её подстановка в BaseColor тела и
#      opaque-мастер `M_AsdaCharLit` давали «серый унылый» силуэт. Исправлено 02.09.2026: тело держит
#      нативную текстуру, мастер — призрачный `M_AsdaSoulSilhouette` (см. import_char_possess.py,
#      asda_build_soul_silhouette_mat.py, отчёт `Reports/audit_2026-09-02/fix_soul_silhouette_look.md`).
#
# ДВИЖОК НЕ ДУБЛИРУЕТСЯ (закон `check-existing-before-creating`): bind-фит и сборка glTF берутся
#   из `export_char_cremmy.py` (модули `T` = asda_test_export_one, `S` = asda_skinned_to_gltf) —
#   ровно как в `export_char_emotions.py` / `export_char_weaponsets.py`. Этот файл — только ПЛАН.
#
# РАСКЛАДКА АССЕТОВ. Клип парного набора живёт на скелете ПАРТНЁРА (мужские наборы 50 костей,
#   корень `M`; женские 54, корень `G` — ремап `G→M` на общий скелет `cremmy`), поэтому пара
#   ложится в превью того пола, каким является ПАРТНЁР. Номера — порт-соглашение из
#   `FAsdaPossessData::PairClipAnimIndex` (свободный блок; занято 0..28, 35, 40..97, 100..204):
#     ассет пола m : 205..209 = М+М (cre_cremmman)     215..219 = Ж+М (cre_crewmman)
#     ассет пола f : 210..214 = М+Ж (cre_cremwwoman)   220..224 = Ж+Ж (cre_crewwwoman)
#   Женский набор моего персонажа переписывает anim_00..04 ассета пола f.
#
# РЕМАПЫ КОРНЕЙ (измерено по именам костей, ревизия R2 — список исполнителя был неполон):
#   `G`/`G_DUMMY` → `M`/`M_DUMMY` — все женские наборы (crewmy, cremwwoman, crewwwoman, crewwsoul);
#   `M_s` → `M`                   — cre_cremmman-a_00_3, cre_cremmman-a_00_4 и cre_cremmsoul-a_00_4.
#   Без ремапа `build_fixed` (он сопоставляет кости ПО ИМЕНИ) потерял бы КОРНЕВОЙ трек, то есть
#   ровно то смещение, ради которого клип и нужен.
#
# Запуск:  python export_char_possess.py [m|f]        (по умолчанию оба пола)
import os, sys, zipfile, importlib.util
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("ECC", os.path.join(HERE, "export_char_cremmy.py"))
ECC = importlib.util.module_from_spec(spec); sys.modules["ECC"] = ECC; spec.loader.exec_module(ECC)
T, S = ECC.T, ECC.S                      # проверенные движки экспорта (bind-фит + glTF)
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import asda_char_clips as _CC   # noqa: E402  ★ дедуп №2 В1 Т11: общая склейка частей / bind-фит / запись мини-glb

OUT_DIR = os.path.join(ECC.OUT_DIR, "possess")

# Ремап имён корневых костей: клип → общий скелет превью `cre_cremmy`.
REMAP = {"G": "M", "G_DUMMY": "M_DUMMY", "M_s": "M", "M_s_DUMMY": "M_DUMMY"}

# ── ПЛАН ПАРНЫХ КЛИПОВ: пол ассета (= пол ПАРТНЁРА) → [(имя набора, базовый номер anim)] ──
#   Базовые номера обязаны совпадать с `FAsdaPossessData::PairClipAnimIndex` (Login/AsdaPossessScene.cpp).
PAIR_PLAN = {
    "m": [("cre_cremmman",   205),    # мой М + его М
          ("cre_crewmman",   215)],   # моя Ж + его М
    "f": [("cre_cremwwoman", 210),    # мой М + его Ж
          ("cre_crewwwoman", 220)],   # моя Ж + его Ж
}

# ── ПЛАН «МОЕГО» НАБОРА: только женский (мужской `cre_cremmy` уже лежит в anim_00..04 верно) ──
MINE_PLAN = {"f": ("cre_crewmy", 0)}

# ── ПЛАН «ДУШИ»: мой пол → (меш .jbm, имя ассета, 5 имён клипов). Клипы 0..3 — из парного набора
#    МОЕГО пола, клип 4 — «душевой». Ассет самостоятельный: свой скелет + anim_00..04.
#    Имя вывода отличается от имени .jbm намеренно: в порту уже лежит СТАТИЧЕСКИЙ импорт
#    `Models/Char/cre_cresoulman-a_00` (общий батч моделей), а нам нужен СКИННУТЫЙ — путать их
#    нельзя (закон `check-existing-before-creating`: старое не трогаем, новое зовём иначе).
SOUL_PLAN = {
    "m": ("cre_cresoulman-a_00",   "cre_cresoulman_preview",
          ["cre_cremmman-a_00_0",   "cre_cremmman-a_00_1",
           "cre_cremmman-a_00_2",   "cre_cremmman-a_00_3",
           "cre_cremmsoul-a_00_4"]),
    "f": ("cre_cresoulwoman-a_00", "cre_cresoulwoman_preview",
          ["cre_crewwwoman-a_00_0", "cre_crewwwoman-a_00_1",
           "cre_crewwwoman-a_00_2", "cre_crewwwoman-a_00_3",
           "cre_crewwsoul-a_00_4"]),
    # ★★★ СМЕШАННЫЕ ПАРЫ — ДОБАВЛЕНО 02.09.2026 ПО РЕШЕНИЮ ВЛАДЕЛЬЦА (силуэт-заглушка показывается
    #   ПРОТИВОПОЛОЖНЫМ полом: мужчине — «подруга», женщине — «друг»). Эталонный загрузчик
    #   `CEntity_LoadCreatureMorphModels_Cremmman@0xF05930` знает ТОЛЬКО однополые наборы
    #   (`cremmsoul` / `crewwsoul`) — это ОТСТУПЛЕНИЕ, помечено и здесь, и в коде порта.
    #   ★ Данные при этом НЕ ВЫДУМАНЫ: смешанные пары лежат в том же `CharMotion.baz` авторскими
    #     файлами (проверено `zipfile`): `cre_cremwwoman-a_00_{0..3}` + `cre_cremwsoul-a_00_4`
    #     (корень `G`, 54 кости) и `cre_crewmman-a_00_{0..3}` + `cre_crewmsoul-a_00_4`
    #     (корень `M`, 50 костей). Ремапы уже покрыты `REMAP`.
    #   ★ Меш силуэта берётся по полу ПАРТНЁРА (он же пол показываемого силуэта), а КЛИПЫ — по
    #     ПАРЕ, поэтому зазор «встал рядом» остаётся авторским: корень клипа рассчитан именно на
    #     моего персонажа моего пола.
    "mw": ("cre_cresoulwoman-a_00", "cre_cresoulmwwoman_preview",
           ["cre_cremwwoman-a_00_0", "cre_cremwwoman-a_00_1",
            "cre_cremwwoman-a_00_2", "cre_cremwwoman-a_00_3",
            "cre_cremwsoul-a_00_4"]),
    "wm": ("cre_cresoulman-a_00",   "cre_cresoulwmman_preview",
           ["cre_crewmman-a_00_0",   "cre_crewmman-a_00_1",
            "cre_crewmman-a_00_2",   "cre_crewmman-a_00_3",
            "cre_crewmsoul-a_00_4"]),
}

# Какие наборы «души» экспортировать для каждого пола МОЕГО персонажа:
#   однополый (эталонный) + смешанный (решение владельца, показывается по умолчанию).
SOUL_BY_GENDER = {"m": ["m", "mw"], "f": ["f", "wm"]}


def load_clip(zmot, srcname, animname):
    """Разобрать .jbb и привести имена костей к общему скелету превью (см. REMAP)."""
    jbb = S.parse_jbb(zmot.read(srcname))
    if not jbb:
        print("FAIL parse", srcname); return None
    renamed = 0
    for bn in jbb['bones']:
        n = bn['name'].strip()
        if n in REMAP:
            bn['name'] = REMAP[n]; renamed += 1
    print("  %-26s -> %-10s B=%2d корень=%-8s ключей=%3d dur=%.2fс%s"
          % (srcname, animname, jbb['B'], jbb['bones'][0]['name'].strip(),
             jbb['bones'][0]['keyCount'], jbb['duration'],
             ("  (ремап корня: %d)" % renamed) if renamed else ""))
    return jbb


# Добавить в референс-скелет кости части, которых в нём нет (hair-strand) — общий asda_char_clips.add_part_bones
#   (дедуп №2 В1 Т11: была копия «1:1 с ECC»). Возвращает число добавленных костей.
add_part_bones = _CC.add_part_bones


def build_glb(ref_jbb, clips, parts_jbm, out_path, tex0):
    """Общий хвост экспорта: bind-фит по мешам + запись .glb (1:1 с ECC.export_gender)."""
    merged = dict(tex=tex0, meshes=[m for j in parts_jbm for m in j['meshes']])
    B = ref_jbb['B']
    # ★★★ 2026-09-11: bind в форму T·R·S при СТРОГОМ улучшении отклонения от игры в UE (меши «души»
    #   живые; у мини-glb с клипами меш при импорте выбрасывается — правка для единообразия bind).
    #   Разбор: Reports/mob_ground/REPORT.md, Reports/char_bind/. Откат: ASDA_BIND_TRS_REFIT=0.
    # ★ дедуп №2 В1 Т11: recover_binds(merged, B) + trs_refit_binds — общий asda_char_clips.fit_binds
    IBM_game, resid, ntrs, _nfit, _res = _CC.fit_binds(T, merged, ref_jbb)
    if ntrs: print("bind TRS-перефит костей: %d (макс. было %.2f -> стало %.2f)"
                   % (len(ntrs), max(a for _n, a, _b in ntrs), max(b for _n, _a, b in ntrs)))
    nfit = sum(1 for x in IBM_game if x is not None)
    res = [r for r in resid if r is not None]
    print("bind восстановлено: %d/%d костей | resid mean=%.5f max=%.5f"
          % (nfit, B, np.mean(res) if res else -1, np.max(res) if res else -1))

    skel_names = {x['name'].strip().lower() for x in ref_jbb['bones']}
    for nm, jb in clips[1:]:
        out = {x['name'].strip().lower() for x in jb['bones']} - skel_names
        if out:
            print("  ! %s: костей вне скелета %d %s" % (nm, len(out), sorted(out)))

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    # build_fixed(bake_pose=False) + validate — общий asda_char_clips.write_glb (дедуп №2 Т11)
    bind_err, maxr, biggest = _CC.write_glb(T, merged, clips, out_path, IBM_game)
    print("bind err=%.5f | skin max|vertex|=%.1f (%s)" % (bind_err, maxr, biggest))
    ok = (bind_err < 1.0 and maxr < 500)
    print("ВЫВОД:", "OK" if ok else "ПРОВЕРЬ", "->", out_path, "| размер:", os.path.getsize(out_path))
    return ok


def export_clips_for_gender(gender, zmodel, zmot):
    """Мини-glb с ДОПОЛНИТЕЛЬНЫМИ клипами на СУЩЕСТВУЮЩИЙ скелет ch1_{g}_cremmy_preview_Skeleton."""
    OUT_NAME = "ch1_%s_cremmy_preview" % gender
    print("\n===== ПОЛ %s: клипы «одержимости» -> possess/%s =====" % (gender.upper(), OUT_NAME))

    # Референс = тот же скелет, что у существующего превью (иначе клипы на него не лягут).
    ref_jbb = S.parse_jbb(zmot.read(ECC.REF_CLIP))
    if not ref_jbb:
        print("FAIL parse ref", ECC.REF_CLIP); return False
    clips = [("__ref_ignore", ref_jbb)]

    if gender in MINE_PLAN:
        base, first = MINE_PLAN[gender]
        for i in range(5):
            jbb = load_clip(zmot, "%s-a_00_%d.jbb" % (base, i), "anim_%02d" % (first + i))
            if not jbb: return False
            clips.append(("anim_%02d" % (first + i), jbb))

    for base, first in PAIR_PLAN[gender]:
        for i in range(5):
            jbb = load_clip(zmot, "%s-a_00_%d.jbb" % (base, i), "anim_%02d" % (first + i))
            if not jbb: return False
            clips.append(("anim_%02d" % (first + i), jbb))

    # Меши превью нужны только затем, чтобы (а) добавить hair-кости в скелет и (б) дать
    #   `recover_binds` вершины для фита: имена треков клипов обязаны совпасть с существующим
    #   скелетом ассета, а он собран ровно из этих частей.
    parts, tex0 = [], None
    for p in ECC.parts_for(gender):
        jbm = T.parse_jbm_ext(zmodel.read(p + ".jbm"))
        if not jbm:
            print("FAIL parse part", p); return False
        add = add_part_bones(ref_jbb, jbm)
        if add: print("  +%d кость(и) hair-strand в скелет для %s" % (add, p))
        T.resolve_names(jbm, ref_jbb)
        tex0 = tex0 or jbm['tex']
        parts.append(jbm)

    return build_glb(ref_jbb, clips, parts, os.path.abspath(os.path.join(OUT_DIR, OUT_NAME + ".glb")), tex0)


def export_soul_for_gender(plan_key, zmodel, zmot):
    """Самостоятельный ассет «души»: силуэт-меш + 5 клипов (anim_00..04) на скелете cremmy.
       `plan_key` — ключ SOUL_PLAN: 'm'/'f' однополые (эталон), 'mw'/'wm' смешанные (владелец)."""
    mesh_name, out_name, clip_names = SOUL_PLAN[plan_key]
    print("\n===== НАБОР %s: «душа» %s -> possess/%s =====" % (plan_key.upper(), mesh_name, out_name))

    ref_jbb = S.parse_jbb(zmot.read(ECC.REF_CLIP))
    if not ref_jbb:
        print("FAIL parse ref", ECC.REF_CLIP); return False
    clips = [("__ref_ignore", ref_jbb)]
    for i, cn in enumerate(clip_names):
        jbb = load_clip(zmot, cn + ".jbb", "anim_%02d" % i)
        if not jbb: return False
        clips.append(("anim_%02d" % i, jbb))

    jbm = T.parse_jbm_ext(zmodel.read(mesh_name + ".jbm"))
    if not jbm:
        print("FAIL parse mesh", mesh_name); return False
    add = add_part_bones(ref_jbb, jbm)
    if add: print("  +%d кость(и) в скелет для %s" % (add, mesh_name))
    T.resolve_names(jbm, ref_jbb)
    # ★★★ ИСПРАВЛЕНО 02.09.2026: РАНЬШЕ тут стояло «в игре ПОДМЕНЯЕТСЯ на silhoutte_tex_blue» —
    #   это была ошибка (автор думал, что загрузчик заменяет текстуру тела серой дымкой). По реверсу
    #   (`Reports/soulmate_soul_silhouette_render.md` §5) СВОЯ текстура меша (синяя/бирюза) — это и
    #   есть «синева» силуэта; серая `silhoutte_tex_blue` шла на ОТДЕЛЬНЫЙ оверлей. Импортёр теперь
    #   ставит на тело именно эту НАТИВНУЮ текстуру (см. import_char_possess.py::_soul_tex_path).
    print("  меш %s: подсеток=%d, своя (нативная) текстура=%s — она и есть цвет силуэта"
          % (mesh_name, len(jbm['meshes']), jbm['tex']))

    return build_glb(ref_jbb, clips, [jbm],
                     os.path.abspath(os.path.join(OUT_DIR, out_name + ".glb")), jbm['tex'])


def main():
    # Аргументы: `m`/`f` — полный набор пола (клипы + обе его «души»);
    #            ключ SOUL_PLAN (`mw`/`wm`) — ТОЛЬКО этот силуэт, без переэкспорта клипов.
    #            Без аргументов — оба пола целиком.
    args = [a.lower() for a in sys.argv[1:]]
    genders = [a for a in args if a in ("m", "f")]
    souls   = [a for a in args if a in SOUL_PLAN and a not in ("m", "f")]
    if not args:
        genders = ["m", "f"]
    zmodel = zipfile.ZipFile(ECC.MODEL_ARC)
    zmot   = zipfile.ZipFile(ECC.MOTION_ARC)
    allok = True
    for g in genders:
        allok = export_clips_for_gender(g, zmodel, zmot) and allok
        for key in SOUL_BY_GENDER[g]:
            allok = export_soul_for_gender(key, zmodel, zmot) and allok
    for key in souls:
        allok = export_soul_for_gender(key, zmodel, zmot) and allok
    print("\n=== ИТОГ:", "ВСЕ OK" if allok else "ЕСТЬ ПРОБЛЕМЫ", "===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
