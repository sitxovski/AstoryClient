# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_maps.py — батч-импорт всех карт AsdaStory как UE-уровней.
# Запуск в редакторе: Tools -> Execute Python Script... (или окно Output Log -> Python-консоль -> exec this).
# Для каждой карты: создаёт /Game/Maps/<Name>, ставит AsdaMapActor(map_dir) (террейн строится в OnConstruction),
# добавляет Directional+Sky свет, сохраняет уровень.
import unreal, os, re
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_maps as _MAPS

MAP_ROOT = _P.MAP_ORIG
# ★ D5 (волна 2 п. 7, 14.09.2026, сверка с Content): папки /Game/Maps нет (уровни карт — /Game/Map, asda_ue_paths.G_MAP); путь не менялся.
DEST     = "/Game/Maps"
# Файлы-признаки наличия террейна в папке карты (LoadPatchTerrain).
TERRAIN_HINT = ("BUFFER.TVB", ".TER")

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

def sanitize(name):
    # база — общий санитайзер; СВЕРХ него тут ещё схлопывание '__'→'_' и обрезка по краям
    s = _MAPS.map_asset_name(name)
    return re.sub(r'_+', '_', s).strip('_')

def has_terrain(folder):
    try:
        files = os.listdir(folder)
    except OSError:
        return False
    up = [f.upper() for f in files]
    return ("BUFFER.TVB" in up) or any(f.endswith(".TER") for f in up)

def map_folders(root):
    out = []
    for name in sorted(os.listdir(root)):
        d = os.path.join(root, name)
        if os.path.isdir(d) and has_terrain(d):
            out.append((name, d.replace("\\", "/")))
    return out

def build_one(name, folder):
    asset = "%s/%s" % (DEST, sanitize(name))
    if not les.new_level(asset):
        unreal.log_warning("new_level FAILED: %s" % asset)
        return False
    # актор карты
    actor = eas.spawn_actor_from_class(unreal.AsdaMapActor, unreal.Vector(0, 0, 0))
    actor.set_editor_property("map_dir", folder)
    try:
        actor.build_map()            # CallInEditor; OnConstruction тоже строит на загрузке
    except Exception as e:
        unreal.log_warning("build_map %s: %s" % (name, e))
    # свет (видимость; точный .SCE-свет добавим позже)
    sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 100000))
    sun.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-45.0, yaw=45.0), False)  # ★ по ключам: позиционно первым идёт ROLL (Д-17)
    eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 100000))
    les.save_current_level()
    return True

def run():
    maps = map_folders(MAP_ROOT)
    unreal.log("Найдено карт с террейном: %d" % len(maps))
    ok = 0
    for name, folder in maps:
        try:
            if build_one(name, folder):
                ok += 1
                unreal.log("  OK  %-28s -> %s/%s" % (name, DEST, sanitize(name)))
        except Exception as e:
            unreal.log_warning("  ERR %s: %s" % (name, e))
    unreal.log("Готово: %d/%d карт импортировано в %s" % (ok, len(maps), DEST))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    run()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
