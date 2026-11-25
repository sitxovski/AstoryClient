# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_clanflag.py — ПРОВЕРКА ассетов ФЛАГА КЛАНА НА ТРАНСПОРТЕ (clan_flag_mount_re.md).
# Что проверяем:
#   1) импортирован small_flag: SkeletalMesh + Skeleton + 3 клипа (anim_00/01/02, длина > 0);
#   2) слоты материалов меша: есть слот ПОЛОТНИЩА ("template") — именно на него код кладёт RT-эмблему,
#      и слот ДРЕВКА ("flag") со своей текстурой;
#   3) у скелеталов транспорта есть кость Flag_Link (эталон List_FindIndexByName).
# Запуск: run_headless.bat asda_verify_clanflag.py
import unreal
import os as _au_os, sys as _au_sys   # ★ волна 2 п. 7 (14.09.2026): реестр путей ассетов (Tools/asda_ue_paths.py)
_au_sys.path.insert(0, _au_os.path.dirname(_au_os.path.abspath(__file__)))
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)

FLAG = _U.skel_mesh(_U.G_MODELS_CLANFLAG, "small_flag")
VEHDIR = _U.G_MODELS_VEHICLE
AR = unreal.AssetRegistryHelpers.get_asset_registry()
def log(m): unreal.log("[verify-clanflag] " + str(m))

clips = []


# 3) кость Flag_Link у транспорта
world = unreal.EditorLevelLibrary.get_editor_world()


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global m
    # 1) модель + клипы
    mesh = unreal.load_asset(FLAG)
    log("SkeletalMesh: %s" % ("ЕСТЬ" if mesh else "НЕТ"))
    for i in range(3):
        a = unreal.load_asset("%sanim_%02d" % (FLAG, i))
        clips.append((i, a is not None, (a.get_editor_property("sequence_length") if a else 0.0)))
    log("клипы: %s" % clips)
    # 2) слоты материалов
    if mesh:
        slots = []
        for m in mesh.get_editor_property("materials"):
            mi = m.get_editor_property("material_interface")
            slots.append((str(m.get_editor_property("material_slot_name")), mi.get_name() if mi else None))
        log("слоты материалов: %s" % slots)
        has_tpl = any("template" in (s or "").lower() or "template" in (n or "").lower() for s, n in slots)
        has_pole = any("flag" in (s or "").lower() and "template" not in (s or "").lower() for s, n in slots)
        log("слот ПОЛОТНИЩА (template): %s | слот ДРЕВКА (flag): %s" % (has_tpl, has_pole))
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0))
    comp = actor.skeletal_mesh_component
    veh_ok, veh_no = 0, []
    for ad in AR.get_assets_by_path(unreal.Name(VEHDIR), recursive=True):
        if str(ad.asset_class_path.asset_name) != "SkeletalMesh":
            continue
        sm = unreal.load_asset(str(ad.package_name))
        comp.set_skeletal_mesh(sm)
        names = [str(n) for n in comp.get_all_socket_names()]
        if "Flag_Link" in names:
            veh_ok += 1
        else:
            veh_no.append(str(ad.asset_name))
    unreal.EditorLevelLibrary.destroy_actor(actor)
    log("ТРАНСПОРТ с костью Flag_Link: %d | без неё: %d %s" % (veh_ok, len(veh_no), sorted(veh_no)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
