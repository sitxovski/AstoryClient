# Copyright (c) 2026 @sitxovski (Discord)
# asda_remove_legacy_map_bgm.py — снять с уровней ЗАПЕЧЁННЫЙ фоновый BGM (легаси).
#
# ЗАЧЕМ. Снятый с вооружения Tools/asda_set_map_bgm.py клал в КАЖДЫЙ уровень карты актор
# AAmbientSound с меткой «MapBGM» (auto_activate=True, looping, 2D, без затухания) и звуком
# из /Game/Audio/BGM. Он стартовал сам при загрузке уровня — ПАРАЛЛЕЛЬНО зонному треку
# UAsdaZoneSubsystem, отсюда «два трека одновременно» при появлении в мире.
#
# ЭТАЛОН держит РОВНО ОДИН музыкальный стрим (CSoundStream: StreamStop → StreamPlay,
# смена только через фейд-очередь CSoundStream_QueueNext), а выбор трека делается по зоне
# тайла, на котором стоит персонаж, — это работа UAsdaZoneSubsystem, не уровня.
#
# ДЕЛАЕТ:
#   1) по всем уровням /Game/Map/* удаляет акторы AmbientSound со звуком из /Game/Audio/BGM
#      (или с именем звука S_BGM_*) и сохраняет уровень;
#   2) удаляет осиротевшую папку /Game/Audio/BGM (дубликат /Game/AsdaAssets/Sounds/Music,
#      откуда треки берёт UAsdaSoundSubsystem) — только если на ассеты больше никто не ссылается.
#
# Запуск: Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_remove_legacy_map_bgm.py
import unreal

EAL = unreal.EditorAssetLibrary
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

MAP_ROOT = "/Game/Map"
# ★ D5 (волна 2 п. 7, 14.09.2026, сверка с Content): папки нет — константа нужна как признак старых BGM-акторов в уровнях (намеренно); путь не менялся.
LEGACY_BGM_DIR = "/Game/Audio/BGM"


def is_legacy_bgm(actor):
    """Актор — запечённый музыкальный слой? (BGM-папка легаси либо имя трека S_BGM_*)"""
    if not isinstance(actor, unreal.AmbientSound):
        return False
    ac = actor.get_editor_property("audio_component")
    snd = ac.get_editor_property("sound") if ac else None
    if not snd:
        return False
    path = snd.get_path_name()
    return path.startswith(LEGACY_BGM_DIR + "/") or snd.get_name().startswith("S_BGM_")


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global snd
    # ── 1) уровни ────────────────────────────────────────────────────────────────────────────────
    levels = [a for a in EAL.list_assets(MAP_ROOT, recursive=True, include_folder=False)
              if EAL.find_asset_data(a).asset_class_path.asset_name == "World"]
    levels = sorted(set(a.split(".")[0] for a in levels))
    # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА В Login.umap (15.09.2026, Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 16): /Game/Map/Login без
    #   ASDA_ALLOW_LOGIN_WRITE=1 не открывается и не пересохраняется (обход сохранял уровень при найденном). Остальные — как раньше.
    import os as _os_lg, sys as _sys_lg
    _sys_lg.path.insert(0, _os_lg.path.dirname(_os_lg.path.abspath(__file__)))   # Tools — для asda_login_guard
    import asda_login_guard as _LG
    login_skipped = [l for l in levels if _LG.login_package_blocked(l, "asda_remove_legacy_map_bgm.py", quiet=True)]
    if login_skipped:
        levels = [l for l in levels if l not in login_skipped]
        unreal.log_warning("[LOGIN-GUARD] [BGM-легаси] уровней сцены логина пропущено: %d (%s) — не открывались и не сохранялись (только с явного разрешения владельца и ASDA_ALLOW_LOGIN_WRITE=1)"
            % (len(login_skipped), ", ".join(login_skipped)))
    touched = 0
    removed_total = 0
    for lvl in levels:
        if not LES.load_level(lvl):
            unreal.log_warning("не открылся уровень %s" % lvl)
            continue
        victims = [a for a in EAS.get_all_level_actors() if is_legacy_bgm(a)]
        if not victims:
            continue
        for a in victims:
            snd = a.get_editor_property("audio_component").get_editor_property("sound")
            unreal.log("%s: удаляю запечённый BGM-актор '%s' (звук %s)" % (lvl, a.get_actor_label(), snd.get_name()))
            EAS.destroy_actor(a)
            removed_total += 1
        LES.save_current_level()
        touched += 1
    unreal.log("=== уровней очищено: %d, акторов удалено: %d (всего уровней: %d) ===" % (touched, removed_total, len(levels)))
    # ── 2) осиротевшая папка легаси-BGM ──────────────────────────────────────────────────────────
    if EAL.does_directory_exist(LEGACY_BGM_DIR):
        assets = EAL.list_assets(LEGACY_BGM_DIR, recursive=True, include_folder=False)
        still_used = []
        for a in assets:
            refs = [r for r in EAL.find_package_referencers_for_asset(a, False)
                    if not r.startswith(LEGACY_BGM_DIR)]
            if refs:
                still_used.append((a, refs))
        if still_used:
            for a, refs in still_used:
                unreal.log_warning("НЕ удаляю %s — на него ещё ссылаются: %s" % (a, ", ".join(refs[:5])))
        else:
            EAL.delete_directory(LEGACY_BGM_DIR)
            unreal.log("=== удалена осиротевшая папка %s (%d ассетов); треки берутся из "
                       "/Game/AsdaAssets/Sounds/Music через UAsdaSoundSubsystem ===" % (LEGACY_BGM_DIR, len(assets)))
    else:
        unreal.log("папка %s уже отсутствует" % LEGACY_BGM_DIR)
    unreal.log("=== ГОТОВО: один поток BGM, музыку карты ведёт UAsdaZoneSubsystem ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
