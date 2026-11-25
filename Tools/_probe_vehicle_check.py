# Copyright (c) 2026 @sitxovski (Discord)
# _probe_vehicle_check.py — ПРИЁМКА после фикса скиннинга ТРАНСПОРТА и ФЛАГА ГИЛЬДИИ (2026-07-30).
# Проверяет разом:
#   1) состав ассетов (SkeletalMesh / AnimSequence / MIC) в Vehicle и ClanFlag;
#   2) bounds каждого меша («взорванный» bind виден как гигантский bounds) + число костей;
#   3) кость Flag_Link у транспорта — на неё крепится флаг гильдии (эталон List_FindIndexByName);
#   4) материалы: MIC без текстуры Tex / без мастера;
#   5) разрешаются ли ссылки на модели из DT_special_itemlist (SubType=15, ModelEntry) и DT_RideEffect.
# Запуск: UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<этот файл> -unattended -NullRHI ...
import unreal

VEH = "/Game/AsdaAssets/Models/Skeletal/Vehicle"
FLG = "/Game/AsdaAssets/Models/Skeletal/ClanFlag"
SPECIAL = "/Game/AsdaAssets/Data/ItemDB/DT_special_itemlist"
RIDEFX = "/Game/AsdaAssets/Data/DT_RideEffect"
AR = unreal.AssetRegistryHelpers.get_asset_registry()
MEL = unreal.MaterialEditingLibrary
def log(m): unreal.log_warning("[veh-check] %s" % m)


def scan(root):
    meshes, anims, mics = {}, {}, []
    for ad in AR.get_assets_by_path(unreal.Name(root), recursive=True):
        cn = str(ad.asset_class_path.asset_name); pkg = str(ad.package_name)
        model = pkg.split("/")[6] if len(pkg.split("/")) > 6 else "?"
        if cn == "SkeletalMesh": meshes[model] = pkg
        elif cn == "AnimSequence": anims.setdefault(model, []).append(pkg)
        elif cn == "MaterialInstanceConstant": mics.append((model, pkg))
    return meshes, anims, mics


def check(root, tag):
    meshes, anims, mics = scan(root)
    log("%s: SkeletalMesh=%d, AnimSequence=%d, MIC=%d"
        % (tag, len(meshes), sum(len(v) for v in anims.values()), len(mics)))
    noclip = sorted(m for m in meshes if not anims.get(m))
    log("  без клипов: %d %s" % (len(noclip), noclip))
    dist = {}
    for m, v in anims.items(): dist[len(v)] = dist.get(len(v), 0) + 1
    log("  распределение клипов на модель: %s" % dist)
    zero = [p for lst in anims.values() for p in lst
            if not unreal.load_asset(p) or float(unreal.load_asset(p).get_editor_property("sequence_length")) <= 0]
    log("  клипов нулевой длины: %d" % len(zero))
    # bounds + кости
    bad = []
    for m, pkg in sorted(meshes.items()):
        sm = unreal.load_asset(pkg); ext = sm.get_bounds().box_extent
        sk = sm.get_editor_property("skeleton")
        nb = len(sk.get_editor_property("bone_tree")) if sk else -1
        mx = max(ext.x, ext.y, ext.z)
        if mx > 20000.0: bad.append((m, mx))
    log("  ПОДОЗРИТЕЛЬНЫХ по bounds (>20000): %d %s" % (len(bad), bad[:8]))
    # материалы
    notex = []
    for m, p in mics:
        a = unreal.load_asset(p)
        try: t = MEL.get_material_instance_texture_parameter_value(a, "Tex")
        except Exception: t = None
        par = a.get_editor_property("parent")
        if not t or not par or par.get_name() != "M_AsdaObjectLit": notex.append(m)
    log("  МЕШЕЙ/MIC без текстуры или мастера: %d %s" % (len(notex), sorted(set(notex))[:8]))
    return meshes


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global m, p, pkg
    veh = check(VEH, "ТРАНСПОРТ")
    check(FLG, "ФЛАГ ГИЛЬДИИ")
    # ── кость Flag_Link ──
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0))
    comp = actor.skeletal_mesh_component
    ok, no = 0, []
    for m, pkg in sorted(veh.items()):
        comp.set_skeletal_mesh(unreal.load_asset(pkg))
        if "Flag_Link" in [str(n) for n in comp.get_all_socket_names()]: ok += 1
        else: no.append(m)
    unreal.EditorLevelLibrary.destroy_actor(actor)
    log("ТРАНСПОРТ с костью Flag_Link: %d | без неё: %d %s" % (ok, len(no), sorted(no)))
    # ── ссылки таблиц на модели ──
    dt = unreal.load_asset(SPECIAL)
    miss = []; rows = 0
    if dt:
        # столбцами (быстрее построчного доступа): ModelEntry = имя модели, SubType=15 = транспорт
        DTL = unreal.DataTableFunctionLibrary
        models = DTL.get_data_table_column_as_string(dt, "ModelEntry")
        subs = DTL.get_data_table_column_as_string(dt, "SubType")
        for i, mn in enumerate(models):
            if i < len(subs) and str(subs[i]) == "15" and str(mn).strip():
                rows += 1
                p = "%s/%s/SkeletalMeshes/%s" % (VEH, mn, mn)
                if not unreal.EditorAssetLibrary.does_asset_exist(p): miss.append(str(mn))
    log("DT_special_itemlist (SubType=15): строк с моделью=%d, БЕЗ МЕША=%d %s"
        % (rows, len(miss), sorted(set(miss))[:10]))
    fx = unreal.load_asset(RIDEFX)
    log("DT_RideEffect: %s" % ("строк=%d" % len(fx.get_row_names()) if fx else "НЕ НАЙДЕН"))
    log("ПРОВЕРКА ЗАВЕРШЕНА")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
