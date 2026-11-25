# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""Разовый зонд: чем «Сияющая руда» (itm1_n_98_othr102_00) отличается от рабочих предметов.
Печатает по каждому мешу: слоты материалов, назначенный материал, его родитель, параметр Tex,
и валидность самой текстуры. Только чтение."""
import unreal

EAL = unreal.EditorAssetLibrary

NAMES = [
    "itm1_n_98_othr102_00",   # Сияющая руда (жалоба)
    "itm1_n_98_othr033_00",   # Реагент D (в той же сессии выглядел нормально)
    "itm1_n_00_mony001_00",   # Золото
]


def own_tex(mi):
    out = []
    try:
        for tp in (mi.get_editor_property("texture_parameter_values") or []):
            nm = tp.get_editor_property("parameter_info").get_editor_property("name")
            val = tp.get_editor_property("parameter_value")
            out.append((str(nm), val.get_path_name() if val else None))
    except Exception as e:
        out.append(("ERR", str(e)))
    return out


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global mi, tp
    for n in NAMES:
        mp = "/Game/AsdaAssets/Models/Items/%s/StaticMeshes/%s" % (n, n)
        unreal.log_warning("=== %s ===" % n)
        unreal.log_warning("  меш есть: %s" % EAL.does_asset_exist(mp))
        sm = EAL.load_asset(mp)
        if not sm:
            continue
        mats = sm.get_editor_property("static_materials")
        unreal.log_warning("  слотов: %d" % len(mats))
        for i, sl in enumerate(mats):
            mi = sl.get_editor_property("material_interface")
            slot = sl.get_editor_property("material_slot_name")
            unreal.log_warning("   [%d] slot=%s mat=%s" % (i, slot, mi.get_path_name() if mi else "<NULL>"))
            if mi:
                par = mi.get_editor_property("parent") if hasattr(mi, "get_editor_property") else None
                try:
                    par = mi.get_editor_property("parent")
                except Exception:
                    par = None
                unreal.log_warning("       parent=%s" % (par.get_path_name() if par else "<нет/это Material>"))
                unreal.log_warning("       texparams=%s" % own_tex(mi))
        tp = "/Game/AsdaAssets/Textures/Items/Texture/%s" % n
        t = EAL.load_asset(tp)
        if t:
            unreal.log_warning("  текстура %s: %dx%d формат=%s srgb=%s comp=%s" % (
                n, t.blueprint_get_size_x(), t.blueprint_get_size_y(),
                t.get_editor_property("compression_settings"),
                t.get_editor_property("srgb"),
                t.get_editor_property("compression_settings")))
        else:
            unreal.log_warning("  ТЕКСТУРА НЕ ЗАГРУЗИЛАСЬ: %s" % tp)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
