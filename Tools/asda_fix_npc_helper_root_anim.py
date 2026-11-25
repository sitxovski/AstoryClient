# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_npc_helper_root_anim.py — ★★★ [ИСПРАВЛЕНИЕ ПОРТА] 22.10.2026 ДОСКИ ОБЪЯВЛЕНИЙ ЛЕЖАЛИ НА БОКУ.
#
# ЧТО БЫЛО (жалоба владельца 22.10.2026: «доска объявлений во Фламмио стоит неправильно»; в Силарисе и Рейне владелец
#   поднимал доски вручную креном 88–90°): у моделей `mdel_raidboard-a_00` и `mdel_noticeboard-a_00` подмеши НЕ совпадают по имени
#   ни с одной костью клипа (`mdel_raidboard-a_01`, `mdel_noticeboard-b_00`…). Оригинал рисует такие подмеши как есть — без кости.
#   Конвертер (`asda_test_export_one.resolve_names`) при промахе привязывал их к КОСТИ №0 — а это служебная `b_bone01` (маркер
#   короба взаимодействия, entity_char.c:59550), у которой в клипе стоит поворот (−0.5,−0.5,−0.5,0.5) = 120° вокруг диагонали.
#   Скелет в UE вышел из ОДНОЙ этой кости, идл-анимация её вращала — вся доска ложилась на бок.
# ЧТО ДЕЛАЕТ: для каждой скелетной модели NPC (`/Game/AsdaAssets/Models/NPC/*/SkeletalMeshes`), чей скелет состоит из ОДНОЙ кости
#   с именем `b_bone*`, во всех её AnimSequence дорожка этой кости — покой (позиция 0, поворот единичный, масштаб 1) на всех
#   ключах: модель стоит ровно как в данных (`.jbm`), как у оригинала. Идемпотентно (уже покой — 0 правок), бэкап .uasset до записи
#   (`backup_file`, набор `npc_helper_root_anim`, md5), сохраняются ТОЛЬКО изменённые пакеты.
#   ★ Исходник конвертера поправлен тем же днём (resolve_names: промах → не служебная кость), чтобы переимпорт не вернул дефект.
# Сухой прогон: ASDA_HELPERANIM_DRYRUN=1. Маркер: «ПОКОЙ СЛУЖЕБНОЙ КОСТИ: ГОТОВО».
# Запуск: python Tools\run_headless_safe.py Tools\asda_fix_npc_helper_root_anim.py --marker "ПОКОЙ СЛУЖЕБНОЙ КОСТИ: ГОТОВО"
# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import os
import sys

ROOT = "/Game/AsdaAssets/Models/NPC"
CONTENT = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Content"


def log(m):
    import unreal
    unreal.log_warning("[служебная кость] %s" % m)


def main():
    import unreal
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_headless_tool as HT
    dry = bool(os.environ.get("ASDA_HELPERANIM_DRYRUN"))
    EAL = unreal.EditorAssetLibrary
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    anims_all = [d for d in reg.get_assets_by_path(ROOT, recursive=True) if str(d.asset_class_path.asset_name) == "AnimSequence"]
    fixed = checked = 0
    for ad in anims_all:
        seq = ad.get_asset()
        dm = seq.get_editor_property("data_model_interface")
        tracks = [str(n) for n in dm.get_bone_track_names()] if hasattr(dm, "get_bone_track_names") else []
        # Признак дефекта — анимация из ОДНОЙ дорожки служебной кости (весь меш привязан к маркеру короба).
        if len(tracks) != 1 or not tracks[0].lower().startswith("b_bone"):
            continue
        bone = tracks[0]
        checked += 1
        nk = dm.get_number_of_keys()
        try:
            tr = list(dm.get_bone_track_transforms(bone)) if hasattr(dm, "get_bone_track_transforms") else []
        except Exception:
            tr = []
        if tr and all(t.translation.is_nearly_zero(0.01) and abs(t.rotation.w) > 0.99999 for t in tr):
            log("%s: дорожка «%s» уже в покое — 0 правок" % (str(ad.package_name), bone))
            continue
        pos = [unreal.Vector(0.0, 0.0, 0.0) for _ in range(max(nk, 1))]
        rot = [unreal.Quat(0.0, 0.0, 0.0, 1.0) for _ in range(max(nk, 1))]
        scl = [unreal.Vector(1.0, 1.0, 1.0) for _ in range(max(nk, 1))]
        if dry:
            log("[сухой] %s: дорожка «%s», ключей %d → покой (сейчас ключ0: %s)" % (str(ad.package_name), bone, nk, tr[0] if tr else "—"))
            continue
        pkg = str(ad.package_name)
        f = os.path.join(CONTENT, pkg[len("/Game/"):].replace("/", os.sep) + ".uasset")
        bak = HT.backup_file(f, "npc_helper_root_anim", verify="md5", log=log)
        if bak is None:
            raise RuntimeError("нет файла %s — бэкап невозможен, запись запрещена" % f)
        ctl = seq.get_editor_property("controller")
        ctl.open_bracket(unreal.Text("покой служебной кости"), True)
        ctl.set_bone_track_keys(bone, pos, rot, scl, True)
        ctl.close_bracket(True)
        unreal.log("ASDA_WRITES_ASSET: %s" % pkg)
        if not EAL.save_loaded_asset(seq, False):
            raise RuntimeError("не сохранён %s" % pkg)
        fixed += 1
        log("%s: дорожка «%s», ключей %d → покой; бэкап %s" % (pkg, bone, nk, bak))
    log("ПОКОЙ СЛУЖЕБНОЙ КОСТИ: ГОТОВО%s — анимаций проверено %d, исправлено %d" % ("  [СУХОЙ ПРОГОН]" if dry else "", checked, fixed))


if __name__ == "__main__" or globals().get("__spec__") is None:
    try:
        main()
    except Exception:
        import traceback
        import unreal
        unreal.log_error("[служебная кость] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
