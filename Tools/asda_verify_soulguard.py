# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_soulguard.py — ПРОВЕРКА ПОКРЫТИЯ АССЕТОВ ХРАНИТЕЛЯ по всем 27 записям SoulGuarderTable.
#   Для каждой записи сверяет, что рантайм (AAsdaSoulGuardianActor) найдёт по своим правилам:
#     • SkeletalMesh модели                 /Game/AsdaAssets/Models/Skeletal/SoulGuard/<м>/SkeletalMeshes/<м>
#     • 11 AnimSequence мотионов            …/<м><суффикс клипа>   (суффикс = имя .jbb без «sb_md_ani_<fam>»)
#     • 3 BP эффектов скиллов + sb_change_01 /Game/AsdaAssets/Effects/BP_Fx_<имя>
#     • 11 звуков мотионов                  /Game/AsdaAssets/Sound/<имя>
#   ★ headless СЛЕП к виду эффектов (аддитив/биллборд) — это проверка НАЛИЧИЯ ассетов, не картинки.
# Запуск: Tools/run_headless.bat Tools\asda_verify_soulguard.py
# ★ ПРАВИЛО tools-no-import-side-effects: работа только внутри main().
import struct
import unreal
import os as _au_os, sys as _au_sys   # ★ волна 2 п. 7 (14.09.2026): реестр путей ассетов (Tools/asda_ue_paths.py)
_au_sys.path.insert(0, _au_os.path.dirname(_au_os.path.abspath(__file__)))
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)

import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
SG_BIN = _P.ROOT + "\\AstoryClient\\RawAssets\\Data\\Character\\SoulGuarderTable.BIN"
MODEL_ROOT = _U.G_MODELS_SOULGUARD
FX_ROOT = _U.G_EFFECTS
SND_ROOT = _U.G_SOUND
HDR, REC, STR0 = 20, 1956, 228
CLIP_PREFIX = 12                      # длина «sb_md_ani_<fam>»
# поле-строка → мотион (та же карта, что в AAsdaSoulGuardianActor::LoadMotionClip)
FIELD_MOTION = {1: 8, 2: 9, 3: 0, 4: 1, 5: 6, 6: 3, 7: 4, 8: 5, 9: 7, 10: 10, 11: 11}
# поле-строка → номер звукового слота (100 + мотион); [25] = звук движения (наш фикс 107)
FIELD_SND = {15: 108, 16: 109, 17: 100, 18: 101, 19: 106, 20: 103, 21: 104, 22: 105,
             23: 110, 24: 111, 25: 107}
EAL = unreal.EditorAssetLibrary


def log(m): unreal.log_warning("[soulguard-verify] %s" % m)


def read_table():
    b = open(SG_BIN, "rb").read()
    cnt = struct.unpack_from("<i", b, 4)[0]
    assert b[12:15] == b"SGT", "не SoulGuarderTable.BIN"
    out = []
    for i in range(cnt):
        r = HDR + REC * i
        idx, stype, row, buff = struct.unpack_from("<4i", b, r)

        def s(k):
            raw = b[r + STR0 + 64 * k:r + STR0 + 64 * k + 64]
            z = raw.find(b"\0")
            return (raw[:z] if z >= 0 else raw).decode("cp1251", errors="replace")
        out.append(dict(idx=idx, stype=stype, row=row, buff=buff, f=[s(k) for k in range(27)]))
    return out


def main():
    recs = read_table()
    log("записей в таблице: %d" % len(recs))
    miss_mesh, miss_clip, miss_fx, miss_snd = [], [], [], []
    ok_mesh = ok_clip = ok_fx = ok_snd = 0

    # Эффект призыва/смены — один на всех.
    change = "%s/BP_Fx_sb_change_01" % FX_ROOT
    log("эффект призыва sb_change_01: %s" % ("ЕСТЬ" if EAL.does_asset_exist(change) else "НЕТ"))

    for R in recs:
        f = R["f"]
        model = f[0].lower()
        mesh = _U.skel_mesh(MODEL_ROOT, model)
        if EAL.does_asset_exist(mesh): ok_mesh += 1
        else: miss_mesh.append((R["idx"], mesh))

        for k, motion in sorted(FIELD_MOTION.items()):
            clip = f[k]
            if not clip:
                miss_clip.append((R["idx"], motion, "<пусто>")); continue
            path = _U.skel_mesh(MODEL_ROOT, model, clip[CLIP_PREFIX:].lower())
            if EAL.does_asset_exist(path): ok_clip += 1
            else: miss_clip.append((R["idx"], motion, path))

        for k in (12, 13, 14):
            name = f[k].lower().replace("-", "_")
            if not name:
                miss_fx.append((R["idx"], k, "<пусто>")); continue
            path = "%s/%s" % (FX_ROOT, _U.fx_bp_name(name))
            if EAL.does_asset_exist(path): ok_fx += 1
            else: miss_fx.append((R["idx"], k, path))

        for k, slot in sorted(FIELD_SND.items()):
            name = f[k]
            if not name:
                miss_snd.append((R["idx"], slot, "<пусто>")); continue
            path = "%s/%s" % (SND_ROOT, name)
            if EAL.does_asset_exist(path): ok_snd += 1
            else: miss_snd.append((R["idx"], slot, name))

    log("МЕШИ:   %d/%d" % (ok_mesh, len(recs)))
    log("КЛИПЫ:  %d/%d" % (ok_clip, len(recs) * 11))
    log("ЭФФЕКТЫ:%d/%d" % (ok_fx, len(recs) * 3))
    log("ЗВУКИ:  %d/%d" % (ok_snd, len(recs) * 11))
    for title, arr in (("меш", miss_mesh), ("клип", miss_clip), ("эффект", miss_fx), ("звук", miss_snd)):
        if not arr:
            continue
        # звуки: 8 имён отсутствуют В САМОМ ОРИГИНАЛЕ (§6.5 отчёта) — их «нет» это факт данных игры.
        uniq = sorted(set(str(a[-1]) for a in arr))
        log("НЕТ %s (%d шт, уникальных %d):" % (title, len(arr), len(uniq)))
        for u in uniq: log("    %s" % u)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
