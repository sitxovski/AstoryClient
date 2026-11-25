# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_npc_clip_bone_index.py — ★★★ [ОРИГИНАЛ] 04.11.2026 ДОРОЖКИ КОСТЕЙ КЛИПОВ NPC ПО ИНДЕКСУ (баг владельца «анимация NPC
#   Ian ломается при начале разговора»; отчёт `Reports/npc_talk_anim/REPORT.md`).
#
# ЧТО БЫЛО: конвертер мапил дорожки клипов 1..N к скелету клипа 0 ПО ИМЕНИ, а оригинал — ПО ИНДЕКСУ (разбор — в шапке
#   `asda_build_npc_fixed.align_clip_bones_by_index`). У кости, переименованной в клипе, дорожки не было ⇒ в UE кость стояла в позе
#   привязки: подмеш Ian-b «отрывался» от тела на весь разговор (клипы 2–4), то же у кристалла `mdel_mcrystal01` (1–4) и у
#   солдат шахты `npc_m_h_mine_sold_1/2/3` (клип 2, кость Bone_g4 на 9.8 ед. в стороне).
# ЧТО ДЕЛАЕТ: исправленные .glb (сборщик уже правлен) импортирует во ВРЕМЕННУЮ папку, сверяет, что все прочие дорожки совпадают с
#   рабочими ассетами (иначе запись запрещена), и переносит в рабочие AnimSequence ТОЛЬКО отличающиеся дорожки. Меши, скелеты,
#   материалы и прочие клипы не трогаются (полный переимпорт пересоздал бы папку NPC целиком). Бэкап каждого .uasset до записи
#   (`@@ASTORY_DEV_ROOT@@\_backup\2026-11-04_npcanim\…`, md5). Временная папка удаляется. Идемпотентен (совпало — 0 правок).
# ★ 04.11.2026 мобы: ASDA_NPCANIM_ROOT=/Game/AsdaAssets/Models/MobSkel, ASDA_NPCANIM_SET=mobanim, GLB_DIR=RawAssets/Models/Skeletal/Mob.
# Вход: ASDA_NPCANIM_GLB_DIR — папка с исправленными .glb (по умолчанию RawAssets/Models/Skeletal/NPC), модели — ASDA_NPCANIM_MODELS
#   (через запятую; по умолчанию пять моделей из отчёта). Сухой прогон: ASDA_NPCANIM_DRYRUN=1. Маркер «КЛИПЫ NPC ПО ИНДЕКСУ: ГОТОВО».
# Запуск: python Tools\run_headless_safe.py Tools\asda_fix_npc_clip_bone_index.py --marker "КЛИПЫ NPC ПО ИНДЕКСУ: ГОТОВО"
# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import os
import sys

ROOT = os.environ.get("ASDA_NPCANIM_ROOT") or "/Game/AsdaAssets/Models/NPC"   # ★ мобы: /Game/AsdaAssets/Models/MobSkel
BACKUP_SET = os.environ.get("ASDA_NPCANIM_SET") or "npcanim"   # набор бэкапа: _backup/2026-11-04_<набор>
TMP = "/Game/_Tmp_NpcClipBoneIndex"
CONTENT = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Content"
DEFAULT_MODELS = ["npc_m_m_ian-a_00", "mdel_mcrystal01-a_00", "npc_m_h_mine_sold_1-a_00",
                  "npc_m_h_mine_sold_2-a_00", "npc_m_h_mine_sold_3-a_00"]
MAX_CHANGED = int(os.environ.get("ASDA_NPCANIM_MAXCHG") or 4)   # предохранитель: столько СУЩЕСТВУЮЩИХ дорожек клипа может отличаться (opara01 клип 11 — 11 костей за родителем)
TOL = 1e-3   # допуск совпадения ключей (UE-единицы/кватернион)


def log(m):
    import unreal
    unreal.log_warning("[клипы по индексу] %s" % m)


def _tr_diff(a, b):
    """Макс. разница двух списков FTransform (позиция, поворот с учётом знака, масштаб); None — разная длина."""
    if len(a) != len(b):
        return None
    d = 0.0
    for x, y in zip(a, b):
        d = max(d, abs(x.translation.x - y.translation.x), abs(x.translation.y - y.translation.y), abs(x.translation.z - y.translation.z))
        q1, q2 = x.rotation, y.rotation
        s = 1.0 if (q1.x * q2.x + q1.y * q2.y + q1.z * q2.z + q1.w * q2.w) >= 0 else -1.0
        d = max(d, abs(q1.x - s * q2.x), abs(q1.y - s * q2.y), abs(q1.z - s * q2.z), abs(q1.w - s * q2.w))
        d = max(d, abs(x.scale3d.x - y.scale3d.x), abs(x.scale3d.y - y.scale3d.y), abs(x.scale3d.z - y.scale3d.z))
    return d


def _track(seq, bone, nk):
    """Локальные позы кости на всех ключах клипа (в UE 5.8 у модели данных Python нет чтения дорожки — через AnimationLibrary)."""
    import unreal
    return [unreal.AnimationLibrary.get_bone_pose_for_frame(seq, bone, f, False) for f in range(nk)]


def main():
    import unreal
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_headless_tool as HT
    import asda_model_import_ue as MI
    import asda_paths as P
    dry = bool(os.environ.get("ASDA_NPCANIM_DRYRUN"))
    glb_dir = os.environ.get("ASDA_NPCANIM_GLB_DIR") or os.path.join(P.ROOT, "AstoryClient", "RawAssets", "Models", "Skeletal", "NPC")
    models = [m.strip().lower() for m in (os.environ.get("ASDA_NPCANIM_MODELS") or ",".join(DEFAULT_MODELS)).split(",") if m.strip()]
    EAL = unreal.EditorAssetLibrary
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    files = {f.lower()[:-4]: os.path.join(glb_dir, f) for f in os.listdir(glb_dir) if f.lower().endswith(".glb")}
    paths = [files[m] for m in models if m in files]
    miss = [m for m in models if m not in files]
    if miss:
        raise RuntimeError("нет .glb для %s в %s" % (miss, glb_dir))
    if EAL.does_directory_exist(TMP):
        EAL.delete_directory(TMP)
    MI.import_batch(paths, TMP)
    tmp_anims = {}
    for ad in reg.get_assets_by_path(TMP, recursive=True):
        if str(ad.asset_class_path.asset_name) == "AnimSequence":
            tmp_anims[str(ad.asset_name).lower()] = ad.get_asset()
    log("временный импорт: %d клипов" % len(tmp_anims))
    work = {}
    reg.scan_paths_synchronous([ROOT, TMP], True)   # ★ headless: реестр лениво сканирует большие папки (MobSkel) — иначе «клипа нет»
    for ad in reg.get_assets_by_path(ROOT, recursive=True):
        if str(ad.asset_class_path.asset_name) == "AnimSequence":
            work[str(ad.asset_name).lower()] = ad
    log("рабочих клипов под %s: %d" % (ROOT, len(work)))
    fixed = same = 0
    errors = []
    for name in sorted(tmp_anims):
        if not any(name.startswith(m) for m in models):
            continue
        if name not in work:
            errors.append("%s: рабочего клипа нет" % name)
            continue
        src = tmp_anims[name]
        ad = work[name]
        dst = ad.get_asset()
        sdm = src.get_editor_property("data_model_interface")
        ddm = dst.get_editor_property("data_model_interface")
        if int(sdm.get_number_of_keys()) != int(ddm.get_number_of_keys()):
            errors.append("%s: число ключей %d ≠ %d" % (name, sdm.get_number_of_keys(), ddm.get_number_of_keys()))
            continue
        nk = int(ddm.get_number_of_keys())
        dtr = {str(t).lower(): str(t) for t in ddm.get_bone_track_names()}
        changes = []
        ref_ok = ref_n = 0
        for t in [str(x) for x in sdm.get_bone_track_names()]:
            a = _track(src, t, nk)
            if t.lower() not in dtr:
                changes.append((t, a, True))
                continue
            d = _tr_diff(a, _track(dst, dtr[t.lower()], nk))
            if d is None or d > TOL:
                changes.append((dtr[t.lower()], a, False))
            else:
                ref_ok += 1
            ref_n += 1
        # Предохранитель: временный импорт обязан совпасть с рабочим во ВСЕХ прочих дорожках (тот же конвертер/импорт).
        nchg = sum(1 for c in changes if not c[2])   # изменённые СУЩЕСТВУЮЩИЕ дорожки (новые — ожидаемы: кости, достроенные за родителем)
        if ref_n and nchg > MAX_CHANGED:
            errors.append("%s: отличаются %d существующих дорожек (ожидались единицы) — запись запрещена: %s" % (name, nchg, [c[0] for c in changes[:8]]))
            continue
        if not changes:
            same += 1
            continue
        desc = ", ".join("%s%s" % (c[0], " (новая)" if c[2] else "") for c in changes)
        if dry:
            log("[сухой] %s: %s; совпало дорожек %d/%d" % (name, desc, ref_ok, ref_n))
            continue
        pkg = str(ad.package_name)
        f = os.path.join(CONTENT, pkg[len("/Game/"):].replace("/", os.sep) + ".uasset")
        bak = HT.backup_file(f, BACKUP_SET, date="2026-11-04", verify="md5", log=log)
        if bak is None:
            raise RuntimeError("нет файла %s — бэкап невозможен, запись запрещена" % f)
        ctl = dst.get_editor_property("controller")
        ctl.open_bracket(unreal.Text("дорожки клипа по индексу"), True)
        for t, a, is_new in changes:
            if is_new:
                (getattr(ctl, "add_bone_track", None) or ctl.add_bone_curve)(t, True)
            ctl.set_bone_track_keys(t, [x.translation for x in a], [x.rotation for x in a], [x.scale3d for x in a], True)
        ctl.close_bracket(True)
        unreal.log("ASDA_WRITES_ASSET: %s" % pkg)
        if not EAL.save_loaded_asset(dst, False):
            raise RuntimeError("не сохранён %s" % pkg)
        fixed += 1
        log("%s: %s; совпало прочих дорожек %d/%d; бэкап %s" % (pkg, desc, ref_ok, ref_n, bak))
    EAL.delete_directory(TMP)
    for e in errors:
        unreal.log_error("[клипы по индексу] %s" % e)
    log("КЛИПЫ NPC ПО ИНДЕКСУ: ГОТОВО%s — исправлено %d, уже верно %d, ошибок %d"
        % ("  [СУХОЙ ПРОГОН]" if dry else "", fixed, same, len(errors)))


if __name__ == "__main__" or globals().get("__spec__") is None:
    try:
        main()
    except Exception:
        import traceback
        import unreal
        unreal.log_error("[клипы по индексу] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
