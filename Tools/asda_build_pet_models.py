# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_pet_models.py — ПИТОМЦЫ: 30 скиннутых .jbm + клипы .jbb → .glb
# (по образцу asda_build_skinned_models.py; отдельный скрипт, чтобы не пересекаться с ветками мобов/чаров).
#
# Реверс: Reports/pet_system_re.md §1.1/§1.2.
#   Модели  : Data/Character/Model/CharModel.bnz   — 30 × cre_{n,p}_*_pet*-a_00.jbm
#   Анимации: Data/Character/Motion/CharMotion.baz — 95 × ..._N.jbb
#   Соответствие модель↔клип берётся ИЗ PetTable.BIN (поля AnimFile0/1/2 @180/212/244), а НЕ по суффиксу:
#   у записи id=6 третий трек = "..._3", а не "..._2" — вывести из имени НЕЛЬЗЯ.
#   Эталон CModel_AllocMeshAndKeyframes(mesh,1,3): у пета РОВНО 3 трека — 0/1 = покой A/B, 2 = бег.
#
# Клипы в .glb именуются anim_00/anim_01/anim_02 = НОМЕР ТРЕКА (не номер файла) — рантайм
#   (AAsdaPetActor::PlayPetClip) грузит "<model>anim_%02d".
# Выход: RawAssets/Models/Pet/<model>.glb
# Запуск (вне UE): python asda_build_pet_models.py
#
# ★ ФИКС СКИННИНГА (2026-07-30). Раньше сборка шла старым путём asda_skinned_to_gltf.build(), где
#   inverseBind = inv(jointWorld(КЛИП, кадр 0)) — то есть bind-поза ПРИБЛИЖАЛАСЬ кадром 0 клипа.
#   Но .jbm хранит ИСТИННЫЙ двух-позиционный bind (POSITION0/POSITION1 на вершину, meshType 0x80),
#   и кадр0 клипа ему не равен → вершины уезжали от игровой позиции ДО 137 ед. при размере пета
#   ~40-60 ед. (растяжение / «отлетающие части»). Тот же баг чинили у NPC/мобов
#   ([[npc-skin-twoposition-bind]], [[npc-skin-bridges-fix]]) — там уже стоит исправленный путь
#   asda_test_export_one (parse_jbm_ext → recover_binds → fix_degenerate_binds → build_fixed),
#   а пет-ветка осталась на старом. Здесь переводим петов на ТОТ ЖЕ проверенный путь.
#   Числа до/после — python-диагностика в Reports/pet_system_re.md §«Фикс скиннинга».
import sys, os, re, struct, zipfile, importlib.util
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_bintable as _BT     # ★ дедуп №2 В1 Т5: общий разбор C-строк
import asda_skin_build as SB    # ★ дедуп №2 В1 Т10: цепочка fixed-пути — общий build_fixed_glb

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "..", "RawAssets", "Models", "Pet")
GAME = _P.DATA
MODEL_ARC = GAME + r"\Character\Model\CharModel.bnz"
MOTION_ARC = GAME + r"\Character\Motion\CharMotion.baz"
PETTABLE = os.path.join(HERE, "..", "Content", "AsdaAssets", "Data", "Character", "pettable.bin")
# ★ 2026-09-11: таблица лежит в RawAssets/Data/Character (в Content её нет — сборщик падал FileNotFoundError).
if not os.path.exists(PETTABLE):
    PETTABLE = os.path.join(HERE, "..", "RawAssets", "Data", "Character", "pettable.bin")
PET_RE = re.compile(r"pet\d+", re.I)          # признак пет-модели в имени
REC = 472                                     # шаг записи PetTable.BIN (tag PTB), заголовок 20 б


def load_mod(name, fn):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, fn))
    m = importlib.util.module_from_spec(spec); sys.modules[name] = m; spec.loader.exec_module(m); return m


def load_conv():
    # S = базовые парсеры/математика (.jbb, jointWorld), T = ИСПРАВЛЕННЫЙ скин-путь (двух-позиционный bind)
    S = load_mod("conv", "asda_skinned_to_gltf.py")
    T = load_mod("T", "asda_test_export_one.py")
    return S, T


def parse_pettable(path):
    """PetTable.BIN → (model_base → {track:animbase}, список строк для DT)."""
    b = open(path, "rb").read()
    cnt = struct.unpack_from("<i", b, 4)[0]
    assert b[12:15] == b"PTB", "не PetTable.BIN (tag=%r)" % b[12:15]

    def s(o):                                  # char[32], cp1251-безопасно (имена файлов — ASCII)
        return _BT.cstr(b, o, 32, "latin1")

    tracks, rows = {}, []
    for i in range(cnt):
        r = 20 + REC * i
        pid = struct.unpack_from("<i", b, r)[0]
        hover = struct.unpack_from("<i", b, r + 96)[0]
        hheight = struct.unpack_from("<i", b, r + 100)[0]
        model = s(r + 148)
        anims = [s(r + 180), s(r + 212), s(r + 244)]
        rows.append(dict(PetTypeId=pid, ModelName=model, Anim0=anims[0], Anim1=anims[1], Anim2=anims[2],
                         bHover=hover, HoverHeight=hheight))
        if not model:
            continue
        cur = tracks.setdefault(model, {})
        for t, a in enumerate(anims):
            if not a:
                continue
            if t in cur and cur[t] != a:
                print("  ! конфликт трека %d у %s: %s vs %s" % (t, model, cur[t], a))
            cur[t] = a
    return tracks, rows


def main():
    conv, T = load_conv()
    tracks, rows = parse_pettable(PETTABLE)
    print("PetTable: записей=%d, моделей в таблице=%d" % (len(rows), len(tracks)))

    zmodel = zipfile.ZipFile(MODEL_ARC); zmot = zipfile.ZipFile(MOTION_ARC)
    jbm_by_base = {n[:-4]: n for n in zmodel.namelist() if n.lower().endswith(".jbm")}
    jbb_by_base = {n[:-4]: n for n in zmot.namelist() if n.lower().endswith(".jbb")}
    models = sorted(b for b in jbm_by_base if PET_RE.search(b))
    print("пет-моделей в архиве: %d" % len(models))

    os.makedirs(OUT_DIR, exist_ok=True)
    ok = fail = 0; worst = []
    for model in models:
        tmap = dict(tracks.get(model, {}))
        if not tmap:   # модель не используется таблицей (cre_n_0_pet13) — берём клипы по суффиксу файла
            for base in jbb_by_base:
                m = re.match(re.escape(model) + r"_(\d+)$", base)
                if m and int(m.group(1)) < 3:
                    tmap[int(m.group(1))] = base
            if tmap:
                print("  ~ %s: нет в PetTable, клипы по суффиксу %s" % (model, sorted(tmap)))
        clips_meta = []
        for t in sorted(tmap):
            fn = jbb_by_base.get(tmap[t])
            if not fn:
                print("  ! %s: трек %d — файла '%s.jbb' нет в архиве" % (model, t, tmap[t])); continue
            clips_meta.append((t, fn))
        if not clips_meta:
            print("  ! %s: клипов нет — пропуск" % model); fail += 1; continue
        try:
            # ★ parse_jbm_ext (а НЕ parse_jbm): читает POSITION0/POSITION1 — истинный bind игры.
            jbm = T.parse_jbm_ext(zmodel.read(jbm_by_base[model]))
            if not jbm:
                print("  FAIL %s: parse_jbm_ext вернул пусто" % model); fail += 1; continue
            clips = []
            for t, fn in clips_meta:
                jbb = conv.parse_jbb(zmot.read(fn))
                if jbb:
                    clips.append(("anim_%02d" % t, jbb))
            if not clips:
                print("  FAIL %s: ни один .jbb не распарсился" % model); fail += 1; continue
            # ★ дедуп №2 В1 Т10: цепочка — общий asda_skin_build.build_fixed_glb:
            #   resolve_names (палитра имён → кости референс-клипа) → recover_binds(B, bones) — фит pos0→POSITION
            #   пер-кость с регуляризацией по кадру 0 для неполноранговых костей (иначе bindScale ×284 → бэкстоп
            #   build_fixed → баг возвращается) → fix_degenerate_binds (кости <4 пар, «мосты»-перепонки) →
            #   ★★★ 2026-09-11 trs_refit_binds: bind в форму T·R·S при СТРОГОМ улучшении отклонения от игры в UE
            #   (Reports/mob_ground/REPORT.md, Reports/char_bind/; откат ASDA_BIND_TRS_REFIT=0) →
            #   prune_unskinned → build_fixed → validate.
            out = os.path.join(OUT_DIR, model + ".glb")
            rep = SB.build_fixed_glb(jbm, clips, out, recover_bones=True, fix_degenerate=True)
            be, nfix = rep["bind_err"], rep["degenerate"]
            clips, Bk, B0 = rep["clips"], rep["bones_after"], rep["bones_before"]
            ok += 1
            print("  %-24s кости %d→%d клипов=%d bind_err=%.4f%s"
                  % (model, B0, Bk, len(clips), be, ("  вырожденных вершин=%d" % nfix) if nfix else ""))
            if be > 1.0:
                worst.append((model, be))
        except Exception as e:
            fail += 1; print("  FAIL %s: %s" % (model, e))
    print("[Pet] OK=%d FAIL=%d → %s" % (ok, fail, os.path.abspath(OUT_DIR)))
    if worst:
        print("⚠️ bind-реконструкция >1.0 (проверить визуально):")
        for m, e in sorted(worst, key=lambda x: -x[1]): print("   %-28s err=%.2f" % (m, e))


if __name__ == "__main__":
    main()
