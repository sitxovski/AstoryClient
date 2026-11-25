# Copyright (c) 2026 @sitxovski (Discord)
# Инвентаризация акторов уровней: ищем ЛЕГАСИ-сущности, чья высота могла быть запечена трейсом
# (тег AsdaNPC от старого asda_build_npc_spawn.py) + сводка классов.
import os, re, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_maps as _MAPS
EAL = unreal.EditorAssetLibrary
ALL = _MAPS.ALL_MAPS   # ★ единый список карт (был дословный литерал из 22 карт, состав и порядок совпадают 1:1)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
agg = {}


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for mp in ALL:
        an = _MAPS.map_asset_name(mp)  # ★ единый санитайзер имени карты (asda_maps.map_asset_name)
        lvl = "/Game/Map/%s/%s" % (an, an)
        if not EAL.does_asset_exist(lvl): continue
        les.load_level(lvl)
        tagged = 0; cls = {}
        for a in eas.get_all_level_actors():
            cn = a.get_class().get_name()
            cls[cn] = cls.get(cn, 0) + 1
            try:
                if a.actor_has_tag("AsdaNPC"): tagged += 1
            except Exception: pass
        unreal.log("[inv] %-20s legacy-AsdaNPC=%d  классы=%s" % (mp, tagged,
            sorted(((k,v) for k,v in cls.items() if v > 0), key=lambda kv: -kv[1])[:12]))
        for k, v in cls.items(): agg[k] = agg.get(k, 0) + v
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass
    unreal.log("[inv] ВСЕГО по классам: %s" % sorted(agg.items(), key=lambda kv: -kv[1]))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
