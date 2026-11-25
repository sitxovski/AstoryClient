# Copyright (c) 2026 @sitxovski (Discord)
# export_equip_parts.py — batch-экспорт КАЖДОЙ надеваемой части брони (chst/pant/shoe, все вариации, оба пола)
# как ОТДЕЛЬНОГО скелетного меша на скелете cre_cremmy (для замены снаряги на превью-персонаже char-select).
#
# ★★ Движок скиннинга — ВАЛИДИРОВАННЫЙ fixed-bind (asda_test_export_one.py::recover_binds/build_fixed),
#     тот же, что и превью-тело (export_char_cremmy.py). НИЧЕГО не изобретаем: части ch1_?_* авторены в T-позе,
#     истинный bind восстанавливается пер-кость фитом pos0->POSITION_j (resid ~0.00002), клип idle реально
#     применяется к мешу. Каждая часть = ОДИН .glb (один скелетный меш + клип anim_00=idle для standalone-превью).
#     В рантайме положение задаёт LeaderPose базового тела; свой idle нужен лишь чтобы часть стояла в позе.
#
# Источник частей — АРХИВ CharModel.bnz (ground truth: всё что реально существует и экспортируемо).
#   Множество ⊇ ссылок item_img_table.csv (в CSV есть 6 несуществующих chst/pant/shoe314 — их нет в архиве).
#   Имена: ch1_{m|f}_00_{chst|pant|shoe}NNN_00_00_0.jbm
#
# Выход: RawAssets/Models/Skeletal/EquipParts/<mesh>.glb   (имя = имя меша части)
#
# Запуск:
#   python export_equip_parts.py                 # ВСЁ (chst/pant/shoe × m/f)
#   python export_equip_parts.py chst            # только chst (оба пола)
#   python export_equip_parts.py chst m          # только chst мужские
#   python export_equip_parts.py --start 200 --count 100   # батч (устойчивость): части [200..300)
#   python export_equip_parts.py --list          # только показать список, ничего не экспортить
import os, importlib.util, sys, re, zipfile
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import numpy as np
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = _P.DATA
MODEL_ARC  = GAME + r"\Character\Model\CharModel.bnz"
MOTION_ARC = GAME + r"\Character\Motion\CharMotion.baz"
OUT_DIR = os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "EquipParts")

SKELETON  = "cre_cremmy"
IDLE_CLIP = "cre_cremmy-a_00_0.jbb"          # anim_00 = idle
PART_TAGS = ("chst", "pant", "shoe", "grov", "hcap")   # +grov (перчатки 7/8) +hcap (шлемы, itemType 0)

# проверенный движок исправленного bind + базовый (.jbb парс)
spec = importlib.util.spec_from_file_location("T", os.path.join(HERE, "asda_test_export_one.py"))
T = importlib.util.module_from_spec(spec); sys.modules["T"] = T; spec.loader.exec_module(T)
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import asda_char_clips as _CC   # noqa: E402  ★ дедуп №2 В1 Т11: общая склейка частей / bind-фит / запись мини-glb
import asda_skinned_to_gltf as S


def list_parts(zmodel, tags, genders):
    """Все части заданных типов/полов, реально присутствующие в архиве (отсортированы)."""
    arc = set(n[:-4] for n in zmodel.namelist() if n.lower().endswith(".jbm"))
    out = []
    for g in genders:
        for t in tags:
            pat = re.compile(r'^ch1_%s_\d+_%s\d+_' % (g, t))
            out += sorted(n for n in arc if pat.match(n))
    return out


# ★★★ 2026-09-11: ТРИ части в проекте собраны из ORIG-сборки клиента, а не из основной (MINE): в MINE у
#   них ДРУГАЯ геометрия и другое имя текстуры (asda_build_models.py: 105 моделей CharModel.bnz различаются
#   между сборками). Переэкспорт из MINE менял число вершин и переименовывал слот материала → рвал
#   привязку материала в UE. Установлено сверкой (Reports/char_bind/compare_glb.py): из ORIG геометрия,
#   нормали, UV, веса и имена материалов совпадают со стоявшим .glb побайтно, из MINE — нет.
#   Список ЗАКРЫТЫЙ (по всем 1081 частям расхождение только у этих трёх). Смена сборки — отдельная задача.
ORIG_PARTS = {"ch1_f_00_chst308_00_00_0", "ch1_f_09_chst200_00_00_0", "ch1_f_09_pant212_00_00_0"}
_ZORIG = []   # лениво открытый CharModel.bnz ORIG-сборки (открывается только если нужен)


def _model_zip_for(part, zmodel):
    """Архив, из которого берётся .jbm части: ORIG для ORIG_PARTS, иначе переданный (MINE)."""
    if part not in ORIG_PARTS:
        return zmodel
    if not _ZORIG:
        _ZORIG.append(zipfile.ZipFile(os.path.join(_P.DATA_ORIG, "Character", "Model", "CharModel.bnz")))
    return _ZORIG[0]


def export_part(part, zmodel, ref_jbb_src, verbose=True):
    """Экспорт одной части как отдельного .glb (единый меш + idle-клип). Возврат: (ok, out_path|reason)."""
    try:
        raw = _model_zip_for(part, zmodel).read(part + ".jbm")
    except KeyError:
        return (None, "нет в архиве")
    try:
        return _export_part_inner(part, raw, ref_jbb_src, verbose)
    except Exception as e:
        # НЕ глушим тихо: явный лог, батч продолжается, часть попадёт в список проблемных
        import traceback
        print("    !! ИСКЛЮЧЕНИЕ на %s: %s" % (part, e))
        if verbose: traceback.print_exc()
        return (False, "exception: %s" % e)


def _export_part_inner(part, raw, ref_jbb_src, verbose=True):
    jbm = T.parse_jbm_ext(raw)
    if not jbm or not jbm.get('meshes'):
        return (False, "parse FAIL")

    # свежая копия ref-клипа под эту часть (bones будем локально дополнять недостающими костями)
    import copy
    ref_jbb = copy.deepcopy(ref_jbb_src)
    clips = [("anim_00", ref_jbb)]

    # добавить в скелет недостающие кости этой части как листья родителя (страховка; для chst/pant/shoe
    # обычно 0 — они скиннятся на штатные кости тела). Без потери данных: если кость не в cremmy —
    # вешаем на её родителя (<parent>_aN → parent), bind восстановит фит pos0->POSITION.
    # ★ дедуп №2 В1 Т11: добавление костей — общий asda_char_clips.add_part_bones (он же обновляет ref_jbb['B'])
    added = _CC.add_part_bones(ref_jbb, jbm)
    if added:
        if verbose: print("    +%d кость(и) добавлено в скелет" % added)

    T.resolve_names(jbm, ref_jbb)
    bnames = {x['name'].strip().lower() for x in ref_jbb['bones']}
    nn = sum(len(me.get('names') or []) for me in jbm['meshes'])
    matched = sum(1 for me in jbm['meshes'] for n in (me.get('names') or []) if n.strip().lower() in bnames)
    if nn and matched != nn:
        # не глушим — явно логируем недобор костей (данные не теряем: build_fixed возьмёт fallback,
        # но об этом надо знать)
        print("    !! %s: резолвнулось костей %d/%d" % (part, matched, nn))

    B = ref_jbb['B']
    merged = dict(tex=jbm['tex'], meshes=jbm['meshes'])
    # ★ дедуп №2 В1 Т11: recover_binds(merged, B) + trs_refit_binds — общий asda_char_clips.fit_binds
    # ★★★ 2026-09-11: bind в форму T·R·S там, где это СТРОГО уменьшает отклонение от игры в UE
    #   (узел glTF не несёт сдвига, UE берёт обратную bind из ref-скелета — Reports/mob_ground/REPORT.md).
    #   Худшая часть до правки ch1_f_09_chst200: 121 ед при размере 108 → 0.00 (Reports/char_bind/).
    #   Откат: ASDA_BIND_TRS_REFIT=0.
    IBM_game, resid, ntrs, _nfit, _res = _CC.fit_binds(T, merged, ref_jbb)
    if ntrs and verbose:
        print("    bind TRS-перефит костей: %d (макс. было %.2f -> стало %.2f)"
              % (len(ntrs), max(a for _n, a, _b in ntrs), max(b for _n, _a, b in ntrs)))
    res = [r for r in resid if r is not None]
    nfit = sum(1 for x in IBM_game if x is not None)

    # ★ НЕ ПРУНИМ безвесовые кости. Причина: базовое тело и КАЖДАЯ часть должны иметь ОДИНАКОВЫЙ
    #   набор костей (в т.ч. M_DUMMY — якорь оружия, idx2, анимирован) → ОБЩИЙ USkeleton cre_cremmy →
    #   LeaderPose базы гонит те же кости на части. Прунинг (prune_unskinned) отрезал бы M_DUMMY и
    #   давал бы каждой части СВОЙ усечённый скелет (несовместимо). Полный 50-костный скелет одинаков
    #   для всех частей; безвесовые кости мешу не вредят (bounds считаются по вершинам, не по костям).
    clips2, IBM2, B0, Bk = clips, IBM_game, B, B

    os.makedirs(OUT_DIR, exist_ok=True)
    out = os.path.abspath(os.path.join(OUT_DIR, part + ".glb"))
    # bake_pose=False → части экипа скиннятся рантаймом на ОБЩЕМ 50-костном скелете; в char-select их гонит
    #   LeaderPose базового тела (та же idle-анима). Ref-поза = bind (T-поза), деформация — от анимы базы.
    bind_err, maxr, biggest = _CC.write_glb(T, merged, clips2, out, IBM2)   # build_fixed(bake_pose=False) + validate (общий, дедуп №2 Т11)
    ok = (bind_err < 1.0 and maxr < 500)
    has_dummy = any(bb['name'].strip().lower() == "m_dummy" for bb in clips2[0][1]['bones'])
    if verbose:
        print("    bind %d/%d resid=%.5f | костей=%d (M_DUMMY=%s) | мешей %d | bind_err=%.4f max|v|=%.1f %s" % (
            nfit, B, (np.mean(res) if res else -1), Bk, has_dummy, len(merged['meshes']),
            bind_err, maxr, "OK" if ok else "ПРОВЕРЬ"))
    if not ok:
        print("    !! %s: скиннинг вне нормы (bind_err=%.4f max|v|=%.1f) — файл записан, ПРОВЕРИТЬ" % (part, bind_err, maxr))
    return (ok, out)


def parse_argv(argv):
    tags = list(PART_TAGS); genders = ["m", "f"]; start = 0; count = None; only_list = False
    i = 0
    while i < len(argv):
        a = argv[i].lower()
        if a == "--start": start = int(argv[i+1]); i += 2; continue
        if a == "--count": count = int(argv[i+1]); i += 2; continue
        if a == "--list": only_list = True; i += 1; continue
        if a in PART_TAGS: tags = [a]; i += 1; continue
        if a in ("m", "f"): genders = [a]; i += 1; continue
        print("неизвестный аргумент:", a); i += 1
    return tags, genders, start, count, only_list


def main():
    tags, genders, start, count, only_list = parse_argv(sys.argv[1:])
    zmodel = zipfile.ZipFile(MODEL_ARC)
    zmot   = zipfile.ZipFile(MOTION_ARC)

    parts = list_parts(zmodel, tags, genders)
    end = len(parts) if count is None else min(len(parts), start + count)
    sel = parts[start:end]
    print("Части (тип=%s пол=%s): всего=%d, к экспорту [%d:%d]=%d" % (
        ",".join(tags), ",".join(genders), len(parts), start, end, len(sel)))
    if only_list:
        for p in parts: print("  ", p)
        return

    # ★ idle-клип = cre_cremmy-a_00_0 (РЕАЛЬНЫЙ char-select клип, трек0=idle; УНИСЕКС — как база char-select).
    #   НЕ dr_ch_ani (тот из ch4/in-world пути, давал «руль велосипеда»). bake запекает кадр0 = руки вниз.
    idle_clip = IDLE_CLIP   # "cre_cremmy-a_00_0.jbb"
    ref_src = S.parse_jbb(zmot.read(idle_clip))
    if not ref_src:
        print("FATAL: не спарсился idle-клип", idle_clip); return
    print("скелет %s: B=%d dur=%.2fс (idle=%s, bake=ON)" % (SKELETON, ref_src['B'], ref_src['duration'], idle_clip))

    done = fail = skip_missing = 0; bad = []
    for i, p in enumerate(sel):
        print("[%d/%d] %s" % (i+1, len(sel), p))
        ok, info = export_part(p, zmodel, ref_src, verbose=True)
        if ok is None:
            skip_missing += 1; print("    ПРОПУСК:", info)
        elif ok:
            done += 1
        else:
            fail += 1; bad.append((p, info))
    print("\n=== ИТОГ: экспортировано=%d, проблемных=%d, пропущено(нет в архиве)=%d, из %d ===" % (
        done, fail, skip_missing, len(sel)))
    if bad:
        print("ПРОБЛЕМНЫЕ (записаны, но вне нормы / parse fail):")
        for p, r in bad: print("  ", p, "->", r)
    print("Выход:", os.path.abspath(OUT_DIR))


if __name__ == "__main__":
    main()
