# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""Разовый зонд: свойства ВСЕХ статик-мешей предметов, влияющие на цвет пикселя.
Ищем, чем «Сияющая руда» отличается от нормальных: вершинные цвета (материал множит на них!),
число UV, Nanite, число секций/слотов, назначенный материал. Только чтение."""
import unreal, os, collections

EAL = unreal.EditorAssetLibrary
ROOT = "/Game/AsdaAssets/Models/Items"
FOCUS = {"itm1_n_98_othr102_00", "itm1_n_98_othr033_00", "itm1_n_00_mony001_00"}

ar = unreal.AssetRegistryHelpers.get_asset_registry()
vc_meshes = []
nomat = []


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    stat = collections.Counter()
    for ad in ar.get_assets_by_path(ROOT, recursive=True):
        if str(ad.asset_class_path.asset_name) != "StaticMesh":
            continue
        sm = EAL.load_asset(str(ad.package_name))
        if not sm:
            continue
        name = str(ad.asset_name)
        ld = unreal.EditorStaticMeshLibrary
        try:
            nverts = ld.get_number_verts(sm, 0)
        except Exception:
            nverts = -1
        has_vc = False
        try:
            has_vc = bool(sm.get_editor_property("has_vertex_colors"))   # если свойство есть
        except Exception:
            has_vc = None
        nanite = False
        try:
            nanite = bool(sm.get_editor_property("nanite_settings").get_editor_property("enabled"))
        except Exception:
            pass
        mats = sm.get_editor_property("static_materials")
        nullmat = sum(1 for m in mats if not m.get_editor_property("material_interface"))
        numuv = -1
        try:
            numuv = ld.get_num_uv_channels(sm, 0)
        except Exception:
            pass
        stat["всего"] += 1
        if has_vc:
            stat["с вершинными цветами"] += 1
            vc_meshes.append(name)
        if nanite:
            stat["nanite"] += 1
        if nullmat:
            stat["слот без материала"] += 1
            nomat.append(name)
        if numuv != 1:
            stat["UV-каналов != 1"] += 1
        if name in FOCUS:
            unreal.log_warning("[focus] %s verts=%s vc=%s nanite=%s slots=%d null=%d uv=%d"
                               % (name, nverts, has_vc, nanite, len(mats), nullmat, numuv))
    unreal.log_warning("[итог] %s" % dict(stat))
    unreal.log_warning("[вершинные цвета] %s" % vc_meshes[:40])
    unreal.log_warning("[слот без материала] %s" % nomat[:40])


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
