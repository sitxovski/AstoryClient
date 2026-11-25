# Copyright (c) 2026 @sitxovski (Discord)
# fishing_export_assets.py — РЫБАЛКА, волна В5а: экспорт .glb (вне UE) для всех анимированных ассетов рыбалки.
#
# ИСТОЧНИК ИСТИНЫ: `Reports/owner_1008/fishing_parts/world_anim.md` §1.3, §2; сводка `fishing_reverse.md` §7.
# Данные — ТОЛЬКО из архивов оригинала (Data/Character/Motion/CharMotion.baz, Data/Character/Model/CharModel.bnz)
#   и таблицы Data/Fish_data.BIN. Ничего не выдумывается.
#
# Что собирается (каждая ветка — тем же проверенным скин-путём, что петы/хранители/эмоции:
#   parse_jbm_ext → resolve_names → recover_binds → fix_degenerate_binds → trs_refit_binds → build_fixed,
#   закон [[skin-pipeline-fixed-path]]):
#
#  1) КЛИПЫ ПЕРСОНАЖА (`Char_LoadMotionAnim@0x9B9890`, game.c:45125; слоты 16..23 → трек slot+11 = 27..34;
#     имя зависит ТОЛЬКО от пола, char[1079]: 1=м, 2=ж):
#       action 60 → Fs_ch_{g}_ani_start_00   61 → idle_00   62 → end_00   63 → idle_01
#       action 64 → idle_02                  65 → end_01    66 → end_02   67 → end_00 (дубль слота 23)
#     → мини-glb RawAssets/Models/Skeletal/Characters/fishing/ch1_{g}_cremmy_preview.glb с клипами
#       anim_225..232 = anim_(225 + action − 60). Скелет/бинд — 1:1 `export_char_emotions.export_gender`
#       (ref-клип cre_cremmy-a_00_0 + hair-кости частей → трек-имена = существующий
#       ch1_{g}_cremmy_preview_Skeleton). Первым идёт "__ref_ignore" (импорт его выбрасывает).
#     ★ Индексы 225..232: занято 00..28, 35, 40..85, 86..97 (эмоции), 100..204 (навыки, skillclips_map.json),
#       205..224 («одержимость», import_char_possess.py). 225+ свободно у обоих полов (проверено по Content
#       и по Source/Tools 08.10.2026).
#
#  2) УДОЧКА РЫБАЛКИ — анимированный объект (`CFishing_LoadRodAnimations@0xFCD6F0`, game.c:607571):
#     меш = ItemImg(надетая удочка).name + "_00.jbm" → ch1_n_00_fird_0{0,1,2}_00_0_00.jbm;
#     слоты 0..7 = Fs_fr_ani_{start_00, idle_00, end_00, idle_01, idle_02, end_01, end_02, end_00}
#     (клип удочки = action − 60) → RawAssets/Models/Fishing/<меш>.glb, клипы anim_00..07 = номер слота.
#     ★ Кость `8a1_dummy` НЕ скиннута, но нужна рантайму (привязка к кости персонажа `8_a1`,
#       `CharFx_UpdateAttachedEffects@0xFCE3A0`, game.c:607873) — сохраняется явно (FISH_ANCHORS).
#     ⚠ Фолбэк оригинала `itm_n_Fishrod_01.jbm` НЕ СУЩЕСТВУЕТ ни в одном из клиентов (CharModel.bnz всех
#       сборок проверен 08.10.2026; есть только текстура itm_n_fishrod_01.tex) — собрать нечего.
#
#  3) РЫБА НА КРЮЧКЕ (`CItemCollectUI_LoadPreviewModel@0xFD1370`, game.c:609370): имя = Fish_data.BIN +120,
#     меш `<имя>.jbm`, клип `<имя>_00.jbb` (трек 0) → RawAssets/Models/Fishing/<имя>.glb, клип anim_00.
#     Кость крепления `fs_Bone01` (к кости удочки `Bone24`) — в списке якорей.
#
#  4) ЗОНА ЛОВА (`CMap_LoadFishingZoneMesh@0xFC5BE0`, game.c:604469): меш fs_zone.jbm + клип fs_zone.jbb
#     (петля) → RawAssets/Models/Fishing/fs_zone.glb, клип anim_00.
#
# Существующие StaticMesh (Models/Char/ch1_n_00_fird_*, fs_fsm_*, fs_stuff_00, fs_zone) НЕ трогаются:
#   вывод в отдельные папки, импорт — в /Game/AsdaAssets/Models/Skeletal/Fishing.
# Манифест длительностей/костей: RawAssets/Models/Fishing/fishing_assets_manifest.json (его ждут волны 1 и 5).
#
# Запуск (вне UE): python Tools\fishing_export_assets.py [char] [rod] [fish] [zone]   (без аргументов — всё)
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
import importlib.util
import json
import os
import re
import struct
import sys
import zipfile

import numpy as np

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))   # каталог инструментов проекта
_spec = importlib.util.spec_from_file_location("ECC", os.path.join(HERE, "export_char_cremmy.py"))
ECC = importlib.util.module_from_spec(_spec)         # модуль сборки персонажа (архивы, части, T/S)
sys.modules["ECC"] = ECC
_spec.loader.exec_module(ECC)
T, S = ECC.T, ECC.S                                  # T = исправленный скин-путь, S = парсер .jbb
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import asda_skin_build as SB                         # noqa: E402  ★ дедуп №2 В1 Т10: общий build_fixed_glb
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import asda_char_clips as _CC   # noqa: E402  ★ дедуп №2 В1 Т11: общая склейка частей / bind-фит / запись мини-glb

# Каталог мини-glb клипов персонажа (рядом с emotions/, skillclips/)
OUT_CHAR = os.path.join(ECC.OUT_DIR, "fishing")
# Каталог glb удочек/рыб/зоны (самостоятельные скелеталы)
OUT_MODELS = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", "Fishing"))
# Манифест: длительности клипов, кости, соответствие action → ассет
MANIFEST = os.path.join(OUT_MODELS, "fishing_assets_manifest.json")
# Таблица рыб клиента (86 записей × 184 б, заголовок 20 б; имя модели @+120)
FISH_DATA = os.path.join(ECC.GAME, "Fish_data.BIN")

# Первый индекс ассета клипа персонажа: anim_(CHAR_IDX_BASE + action − 60)
CHAR_IDX_BASE = 225
# Суффиксы клипов в порядке слотов эталона (слот = action − 60); слот 7 = дубль end_00
CLIP_ORDER = ["start_00", "idle_00", "end_00", "idle_01", "idle_02", "end_01", "end_02", "end_00"]
# Смысл слотов по FSM (world_anim §2.1) — только для журнала и манифеста
CLIP_MEANING = ["заброс (FSM 112)", "ожидание, петля", "вытаскивание (FSM 113)", "рывок (верная клавиша)",
                "рывок", "удача (end_01)", "неудача (end_02)", "дубль end_00 (клиент не выставляет)"]
# Меши удочек рыбалки: img-имя надетой удочки + "_00" (item_img_table: 49106 → fird_00,
#   49079/49080/49107 → fird_01, 49108 → fird_02)
ROD_MESHES = ["ch1_n_00_fird_00_00_0_00", "ch1_n_00_fird_01_00_0_00", "ch1_n_00_fird_02_00_0_00"]
# Кости, нужные рантайму, но не скиннутые (prune_unskinned их бы отсёк)
FISH_ANCHORS = {"8a1_dummy", "bone24", "fs_bone01"}
# Концевые (не скиннутые) кости косы женского набора: их нет в скелете превью и у эмоций ch4_f_00_*
#   (проверено: ch4_f_00_happy01.jbb несёт тот же `4_a_End`) — трек при импорте отбрасывается, на меш не влияет
KNOWN_NUBS = {"4_a_end"}
# Имя-заглушка в Fish_data.BIN (+4 тип = −1, записи 40..77 кроме 72): модели нет, оригинал берёт фолбэк
#   `Fs_fsm_crucian_00` (CItemCollectUI_LoadPreviewModel@0xFD1370, game.c:609370)
FISH_NO_MODEL = "-1"
# Шаг записи Fish_data.BIN и смещение имени модели
FISH_REC, FISH_HDR, FISH_NAME_OFS = 184, 20, 120


def _zi(z):
    """Индекс архива: имя в нижнем регистре → настоящее имя (в архиве регистр смешанный)."""
    return {n.lower(): n for n in z.namelist()}


def _read(z, zi, name):
    """Прочитать файл архива без учёта регистра; None — если нет."""
    real = zi.get(name.lower())
    return z.read(real) if real else None


def _clip_info(jbb):
    """Длительность/кадры клипа для манифеста."""
    return dict(duration=round(float(jbb.get("duration", -1)), 6), fps=float(jbb.get("fps", 0) or 0),
                bones=int(jbb["B"]))


# ★ дедуп №2 В1 Т10 (11.10.2026): копия `T.prune_unskinned` с расширенными якорями (`prune_keep_anchors`) убрана —
#   общая функция получила параметр `extra_anchors` (умолчание () — для мобов/петов/транспорта поведение прежнее:
#   `Bone24` встречается в чужих моделях, общий KEEP_ANCHORS не расширяем); якоря рыбалки — FISH_ANCHORS ниже.


def build_object(zm, zmi, za, zai, mesh, clip_files, out_dir):
    """Самостоятельный скелетал (удочка/рыба/зона): меш + клипы anim_00.. (порядок clip_files).

    Путь 1:1 `asda_build_pet_models.main` (петы) + якоря рыбалки. Возвращает запись манифеста.
    """
    raw = _read(zm, zmi, mesh + ".jbm")
    if raw is None:
        print("  FAIL %s: нет %s.jbm в CharModel.bnz" % (mesh, mesh))
        return None
    jbm = T.parse_jbm_ext(raw)
    if not jbm:
        print("  FAIL %s: parse_jbm_ext пусто" % mesh)
        return None
    clips, info = [], []
    for i, fn in enumerate(clip_files):
        b = _read(za, zai, fn)
        if b is None:
            print("  FAIL %s: нет клипа %s" % (mesh, fn))
            return None
        jbb = S.parse_jbb(b)
        if not jbb:
            print("  FAIL %s: parse %s" % (mesh, fn))
            return None
        clips.append(("anim_%02d" % i, jbb))
        info.append(dict(asset="anim_%02d" % i, src=fn, **_clip_info(jbb)))
    # ★ дедуп №2 В1 Т10: цепочка resolve_names → recover_binds(B, bones) → fix_degenerate_binds →
    #   trs_refit_binds (★ 11.09: bind в форму T·R·S, откат ASDA_BIND_TRS_REFIT=0) → prune (+ якоря рыбалки) →
    #   build_fixed → validate — общий asda_skin_build.build_fixed_glb (как у петов, путь 1:1).
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, mesh + ".glb")
    rep = SB.build_fixed_glb(jbm, clips, out, recover_bones=True, fix_degenerate=True, extra_anchors=FISH_ANCHORS)
    clips, Bk, B0, nfix = rep["clips"], rep["bones_after"], rep["bones_before"], rep["degenerate"]
    be, maxr = rep["bind_err"], rep["max_vertex"]
    bones = [b["name"].strip() for b in clips[0][1]["bones"]]
    mats = sorted({os.path.splitext(me.get("tex") or jbm["tex"])[0] for me in jbm["meshes"]})
    print("  %-26s кости %d→%d клипов=%d bind_err=%.4f max|v|=%.1f%s"
          % (mesh, B0, Bk, len(clips), be, maxr, ("  вырожд.=%d" % nfix) if nfix else ""))
    return dict(mesh=mesh, glb=os.path.relpath(out, HERE), bones=bones, materials=mats, clips=info,
                bind_err=round(float(be), 5), max_vertex=round(float(maxr), 2), ok=bool(be < 1.0))


def fishing_char_specs(gender):
    """Клипы рыбалки пола `gender`: [(файл .jbb в CharMotion.baz, индекс ассета anim_N, запись манифеста без полей клипа)]."""
    return [("fs_ch_%s_ani_%s.jbb" % (gender, suf), CHAR_IDX_BASE + slot,
             dict(action=60 + slot, track=27 + slot, meaning=CLIP_MEANING[slot]))
            for slot, suf in enumerate(CLIP_ORDER)]


def export_char_gender(gender, zmodel, zmot, zai, specs=None, out_dir=None, tag="fishing", known_nubs=None):
    """Мини-glb клипов персонажа пола `gender` (логика 1:1 export_char_emotions.export_gender).

    ★ 10.10.2026 (копание): общая точка для клипов РЫБАЛКИ (по умолчанию) и КОПАНИЯ (`digging_export_clips.py`) —
      `specs` = [(файл .jbb, индекс anim_N, запись манифеста)], `out_dir` = каталог мини-glb, `tag` — подпись журнала.
      `known_nubs` — не скиннутые концевые/вспомогательные кости клипов, трек которых импорт отбрасывает (по умолчанию
      `KNOWN_NUBS`). Без аргументов поведение прежнее (рыбалка: anim_225..232 в fishing/).
    """
    nubs = KNOWN_NUBS if known_nubs is None else set(known_nubs)
    if specs is None:
        specs = fishing_char_specs(gender)
    out_dir = out_dir or OUT_CHAR
    PARTS = ECC.parts_for(gender)
    OUT_NAME = "ch1_%s_cremmy_preview" % gender
    print("\n===== ПОЛ %s -> %s/%s (%d клипов) =====" % (gender.upper(), tag, OUT_NAME, len(specs)))
    ref_jbb = S.parse_jbb(zmot.read("cre_cremmy-a_00_0.jbb"))
    if not ref_jbb:
        print("FAIL parse cre_cremmy-a_00_0")
        return False, []
    remap = {"G": "M", "G_DUMMY": "M_DUMMY"}   # жен. root → имена муж./cremmy скелета
    clips = [("__ref_ignore", ref_jbb)]
    info = []
    for name, idx, meta in specs:
        raw = _read(zmot, zai, name)
        if raw is None:
            print("FAIL нет в архиве:", name)
            return False, []
        jbb = S.parse_jbb(raw)
        if not jbb:
            print("FAIL parse", name)
            return False, []
        for bn in jbb["bones"]:
            n = bn["name"].strip()
            if n in remap:
                bn["name"] = remap[n]
        clips.append(("anim_%d" % idx, jbb))
        rec = {k: v for k, v in meta.items() if k != "meaning"}   # порядок полей манифеста прежний: action, track, asset, src, meaning
        rec.update(asset="ch1_%s_cremmy_previewanim_%d" % (gender, idx), src=zai[name.lower()],
                   meaning=meta.get("meaning", ""), **_clip_info(jbb))
        info.append(rec)
        print("  %-28s -> anim_%d (%s)  B=%d dur=%.4fс"
              % (zai[name.lower()], idx, ", ".join("%s %s" % (k, v) for k, v in meta.items() if k != "meaning"),
                 jbb["B"], jbb.get("duration", -1)))

    # ── дальше 1:1 export_char_emotions.export_gender (hair-кости + bind-фит + glb) ──
    # ★ дедуп №2 В1 Т11: hair-strand кости частей, resolve_names и слияние мешей — общий asda_char_clips.merge_parts
    merged = _CC.merge_parts(T, ref_jbb, PARTS, lambda p: zmodel.read(p + ".jbm"),
                             on_added=lambda p, n: print("  +%d кость(и) hair-strand в скелет для %s" % (n, p)))
    if merged is None:
        return False, []

    B = ref_jbb["B"]
    IBM_game, resid, _ntrs, _nfit, _res = _CC.fit_binds(T, merged, ref_jbb)   # recover_binds + trs_refit_binds (общий, дедуп №2 Т11)
    nfit = sum(1 for x in IBM_game if x is not None)
    res = [r for r in resid if r is not None]
    print("bind восстановлено: %d/%d костей | resid mean=%.5f max=%.5f"
          % (nfit, B, np.mean(res) if res else -1, np.max(res) if res else -1))

    skel_names = {x["name"].strip().lower() for x in ref_jbb["bones"]}
    miss = 0
    for nm, jb in clips[1:]:
        extra = {x["name"].strip().lower() for x in jb["bones"]} - skel_names
        if extra - nubs:
            miss += 1
            print("  ! %s: кости вне скелета: %s" % (nm, sorted(extra)))
        elif extra:   # концевая кость косы — та же картина у эмоций ch4_f_00_* (её нет в скелете превью)
            print("  ~ %s: концевые кости вне скелета (не скиннуты, трек отбрасывается): %s" % (nm, sorted(extra)))
    print("клипов с костями вне скелета: %d/%d" % (miss, len(clips) - 1))

    os.makedirs(out_dir, exist_ok=True)
    out = os.path.abspath(os.path.join(out_dir, OUT_NAME + ".glb"))
    bind_err, maxr, biggest = _CC.write_glb(T, merged, clips, out, IBM_game)   # build_fixed(bake_pose=False) + validate (общий)
    print("bind err=%.5f | skin max|vertex|=%.1f (%s)" % (bind_err, maxr, biggest))
    ok = (bind_err < 1.0 and maxr < 500 and miss == 0)
    print("ВЫВОД:", "OK" if ok else "ПРОВЕРЬ")
    print("OK ->", out, "| размер:", os.path.getsize(out))
    return ok, info


def fish_models():
    """Уникальные имена моделей рыб из Fish_data.BIN +120 (порядок записей), с id предмета и типом."""
    b = open(FISH_DATA, "rb").read()
    cnt = struct.unpack_from("<i", b, 4)[0]
    assert b[12:15] == b"FIT", "не Fish_data.BIN (tag=%r)" % b[12:15]
    assert FISH_HDR + FISH_REC * cnt == len(b), "размер Fish_data.BIN не сходится с %d записями" % cnt
    out = {}
    for i in range(cnt):
        r = FISH_HDR + FISH_REC * i
        item_id, ftype = struct.unpack_from("<ii", b, r)
        name = b[r + FISH_NAME_OFS:r + FISH_NAME_OFS + 32].split(b"\0")[0].decode("latin1").strip()
        if name:   # имя в таблице смешанного регистра (Fs_fsm_…), файлы архива — нижнего; ключ = нижний
            out.setdefault(name.lower(), []).append((item_id, ftype))
    return cnt, out


def main():
    args = [a.lower() for a in sys.argv[1:]]
    want = set(args) & {"char", "rod", "fish", "zone"} or {"char", "rod", "fish", "zone"}
    zmodel = zipfile.ZipFile(ECC.MODEL_ARC)
    zmot = zipfile.ZipFile(ECC.MOTION_ARC)
    zmi, zai = _zi(zmodel), _zi(zmot)
    man = {}
    if os.path.exists(MANIFEST):
        with open(MANIFEST, encoding="utf-8") as f:
            man = json.load(f)
    allok = True

    if "char" in want:
        man["char"] = {}
        for g in ("m", "f"):
            ok, info = export_char_gender(g, zmodel, zmot, zai)
            man["char"][g] = info
            allok = allok and ok

    if "rod" in want:
        print("\n===== УДОЧКИ (%d) =====" % len(ROD_MESHES))
        files = ["fs_fr_ani_%s.jbb" % s for s in CLIP_ORDER]
        man["rod"] = []
        for mesh in ROD_MESHES:
            rec = build_object(zmodel, zmi, zmot, zai, mesh, files, OUT_MODELS)
            allok = allok and bool(rec and rec["ok"] and "8a1_dummy" in [x.lower() for x in rec["bones"]]
                                   and "bone24" in [x.lower() for x in rec["bones"]])
            if rec:
                man["rod"].append(rec)
        man["rod_fallback"] = "itm_n_Fishrod_01.jbm — нет ни в одном клиенте (только текстура .tex)"

    if "fish" in want:
        cnt, names = fish_models()
        print("\n===== РЫБЫ: Fish_data.BIN записей=%d, уникальных моделей=%d =====" % (cnt, len(names)))
        man["fish"] = []
        if FISH_NO_MODEL in names:
            ids = [i for i, _t in names.pop(FISH_NO_MODEL)]
            man["fish_no_model"] = dict(items=ids, fallback="fs_fsm_crucian_00")
            print("  ~ записей с именем «-1» (модели нет, фолбэк оригинала fs_fsm_crucian_00): %d" % len(ids))
        for name in names:
            rec = build_object(zmodel, zmi, zmot, zai, name, [name + "_00.jbb"], OUT_MODELS)
            if rec:
                rec["fish_items"] = [dict(item_id=i, type=t) for i, t in names[name]]
                man["fish"].append(rec)
            allok = allok and bool(rec and rec["ok"] and "fs_bone01" in [x.lower() for x in rec["bones"]])

    if "zone" in want:
        print("\n===== ЗОНА fs_zone =====")
        rec = build_object(zmodel, zmi, zmot, zai, "fs_zone", ["fs_zone.jbb"], OUT_MODELS)
        man["zone"] = rec
        allok = allok and bool(rec and rec["ok"])

    os.makedirs(OUT_MODELS, exist_ok=True)
    with open(MANIFEST, "w", encoding="utf-8") as f:
        json.dump(man, f, ensure_ascii=False, indent=1)
    print("\nманифест ->", MANIFEST)
    print("\n=== ИТОГ:", "ВСЕ OK" if allok else "ЕСТЬ ПРОБЛЕМЫ", "===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    main()
