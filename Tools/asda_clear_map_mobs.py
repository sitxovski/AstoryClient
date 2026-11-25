# Copyright (c) 2026 @sitxovski (Discord)
# asda_clear_map_mobs.py — убрать с уровня карты ВСЕ запечённые моб-акторы (AAsdaMobActor), остальное не трогать.
#
# ЗАЧЕМ (11.10.2026, решение владельца): на nightvalley мобов быть не должно — запечённые 276 были из чужой карты
#   (Night_Valley.svr = alpen_dungeon; сверка Reports/dedup_audit2/wave0_t2_nightvalley.md).
# ЗАЩИТЫ:
#   • карта обязана стоять в "mob_bake_off" реестра карт (Tools/data/asda_map_registry.json) — инструмент чистит
#     только карты, для которых владелец решил «без мобов»; иначе отказ;
#   • сцена логина и имена вне реестра — отказ (asda_login_guard);
#   • бэкап .umap ДО правки (HT.backup_file, набор <дата>_<ASDA_BACKUP_SET или clear_mobs>, сверка md5);
#   • удаляются ТОЛЬКО экземпляры AAsdaMobActor; прочие акторы в папке MOB* только перечисляются в лог;
#   • после сохранения уровень перезагружается и пересчитывается: маркер успеха — только при 0 оставшихся.
# Запуск: ASDA_MAP=<карта> python Tools/run_headless_safe.py Tools/asda_clear_map_mobs.py --marker "CLEAR_MOBS_OK"
# ★ Модуль БЕЗ побочных эффектов при импорте (закон tools-no-import-side-effects).
import os
import sys

_TOOLS = os.path.dirname(os.path.abspath(__file__))   # папка Tools (для импорта соседних модулей)
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

MARKER_OK = "CLEAR_MOBS_OK"   # маркер успеха для run_headless_safe --marker (ASCII)


def _content_file(level):
    """/Game/Map/a/a → <проект>/Content/Map/a/a.umap (путь файла уровня на диске)."""
    proj = os.path.dirname(_TOOLS)
    rel = level[len("/Game/"):] if level.startswith("/Game/") else level
    return os.path.normpath(os.path.join(proj, "Content", rel + ".umap"))


def _count(eas, unreal):
    """(моб-акторы, прочие акторы в папке MOB*) текущего уровня."""
    mobs, other = [], []
    for a in eas.get_all_level_actors():
        if isinstance(a, unreal.AsdaMobActor):
            mobs.append(a)
        elif str(a.get_folder_path()).upper().startswith("MOB"):
            other.append(a)
    return mobs, other


def main():
    """Тело инструмента: проверки → бэкап → удаление → сохранение → перезагрузка и пересчёт."""
    import unreal
    import asda_headless_tool as HT
    import asda_login_guard as LG
    import asda_map_registry as REG
    import asda_ue_paths as U

    def log(m):
        """Строка журнала с префиксом инструмента."""
        unreal.log("[clear_mobs] " + str(m))

    map_name = os.environ.get("ASDA_MAP", "")
    if not map_name:
        unreal.log_error("[clear_mobs] ASDA_MAP не задан"); return False
    off = {k.lower(): v for k, v in REG.mob_bake_off().items()}
    if map_name.lower() not in off:
        unreal.log_error("[clear_mobs] %s нет в mob_bake_off реестра карт — чистка запрещена" % map_name); return False
    level = U.level_path(map_name)
    if LG.login_map_write_blocked(level, "asda_clear_map_mobs.py") or LG.map_name_blocked(level, "asda_clear_map_mobs.py"):
        return False
    if not unreal.EditorAssetLibrary.does_asset_exist(level):
        unreal.log_error("[clear_mobs] нет уровня %s" % level); return False
    path = _content_file(level)
    bk = HT.backup_file(path, os.environ.get("ASDA_BACKUP_SET", "clear_mobs"), verify="md5", log=log)
    if not bk:
        unreal.log_error("[clear_mobs] нет файла уровня %s — бэкап не снят" % path); return False

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(level)
    mobs, other = _count(eas, unreal)
    log("карта %s: моб-акторов %d, прочих акторов в папке MOB* %d" % (map_name, len(mobs), len(other)))
    for a in other:
        log("  не моб, НЕ удаляется: %s (%s, %s)" % (a.get_actor_label(), a.get_class().get_name(), a.get_folder_path()))
    removed = 0
    for a in mobs:
        if eas.destroy_actor(a):
            removed += 1
    unreal.log("ASDA_WRITES_ASSET: %s" % level)
    if not les.save_current_level():
        unreal.log_error("[clear_mobs] save_current_level вернул False"); return False

    les.load_level(level)                     # пересчёт по сохранённому пакету
    left, _ = _count(eas, unreal)
    log("удалено %d, после перезагрузки моб-акторов %d" % (removed, len(left)))
    if left:
        unreal.log_error("[clear_mobs] после сохранения остались моб-акторы: %d" % len(left)); return False
    HT.marker("%s map=%s removed=%d left=0" % (MARKER_OK, map_name, removed))
    return True


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
