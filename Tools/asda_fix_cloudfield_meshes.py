# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_cloudfield_meshes.py — ДОБАВИТЬ недостающие меши облаков в акторы CloudField (аудит неба 05.09.2026).
#   Актор AAsdaCloudFieldActor с ПУСТЫМ `CloudMeshes` рисует фолбэк — блобы из движковых сфер; штатная форма —
#   метабол-меши cloudblob_a..d (памятка sky-system-complete). Раскатка asda_rollout_sky_full.py их не ставила,
#   поэтому воссозданный 05.09 alpen получил сферы (байт-скан alpen.umap: cloudblob_ = 0, у 17 карт = 12).
#   Меняет ТОЛЬКО пустой `cloud_meshes` (asda_percloud_mics.ensure_cloud_meshes); прочие поля акторов и уровни с
#   уже заполненным массивом не трогаются и не сохраняются. Бэкап umap — до запуска (памятка backup-before-edit).
# env: ASDA_SKY_MAPS = "a||b" — фильтр карт (умолч. все 18 OUTDOOR_MAPS).
# Запуск (скрыто, редактор закрыт):
#   ASDA_SKY_MAPS=alpen python Tools/run_headless_safe.py Tools/asda_fix_cloudfield_meshes.py --marker "CFMESH ЗАВЕРШЕНО"
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_maps as _MAPS
import asda_percloud_mics as _PMC
import asda_login_guard as _LG   # ★ 16.09.2026 (обратный обход savedir14): единый гейт сцены логина
import unreal

EAL = unreal.EditorAssetLibrary
_asset = _MAPS.map_asset_name


def process(mp):
    """Один уровень: найти CloudField, заполнить пустой cloud_meshes, сохранить только при изменении."""
    root = "/Game/Map/%s" % _asset(mp); lvl = "%s/%s" % (root, _asset(mp))
    # ★ 16.09.2026 (обратный обход ревизии savedir14). Инструмент грузит уровень, меняет поля акторов и
    #   СОХРАНЯЕТ его — гейт сцены логина ДО load_level и до save_current_level. Проверяется КОРЕНЬ карты
    #   (у сцены логина он же и есть её уровень со светом владельца).
    if _LG.login_map_write_blocked(root, "asda_fix_cloudfield_meshes.py", quiet=True):
        return
    if not EAL.does_asset_exist(lvl):
        unreal.log("[CFMESH] %s: нет уровня — пропуск" % mp); return
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(lvl)
    fields = [a for a in eas.get_all_level_actors() if a.get_class().get_name() == "AsdaCloudFieldActor"]
    if not fields:
        unreal.log("[CFMESH] %s: CloudField нет — пропуск (карта без неба или комплект не раскатан)" % mp); return
    changed = 0
    for f in fields:
        if _PMC.ensure_cloud_meshes(f):
            changed += 1
            unreal.log("[CFMESH] %s: %s cloud_meshes был пуст → %d мешей" % (mp, f.get_actor_label(), len(_PMC.CLOUD_MESH_PATHS)))
    if changed:
        les.save_current_level()
        unreal.log("[CFMESH] %s: сохранён (акторов изменено %d)" % (mp, changed))
    else:
        unreal.log("[CFMESH] %s: cloud_meshes уже заполнен (%d) — без изменений" % (mp, len(fields[0].get_editor_property("cloud_meshes"))))


def main():
    """Обход карт (закон [[tools-no-import-side-effects]])."""
    for mp in _MAPS.select_maps(_MAPS.OUTDOOR_MAPS, env="ASDA_SKY_MAPS"):
        try: process(mp)
        except Exception as e: unreal.log_error("[CFMESH] %s FAIL: %s" % (mp, e))
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass
    unreal.log("=== CFMESH ЗАВЕРШЕНО ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
