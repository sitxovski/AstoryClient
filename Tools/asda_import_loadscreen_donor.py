# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_loadscreen_donor.py — ★ [ПОРТ] 10.11.2026 импорт картинок загрузки Inferion (18) и battlefield (19), собранных
#   `asda_loadscreen_compose.py` (рамка «AStory REMAKE» эталона + сцена донора; почему — в шапке того скрипта).
#   Кладёт в /Game/AsdaAssets/Textures/Maps/<dir>/load/<имя> — ту папку, что перечисляет `UAsdaLoadManagerSubsystem::PickLoadingTexture`
#   (код не меняется: выбор по составу папки, как в оригинале). Настройки текстуры КОПИРУЮТСЯ с эталонной
#   /Game/AsdaAssets/Textures/Maps/flammio/load/e_Flammio (сжатие, sRGB, LOD-группа, мипы, never_stream и т.д.), чтобы новая
#   картинка вела себя на экране как соседние. Идемпотентно: ассет есть — пропуск (ASDA_LOADDONOR_REIMPORT=1 — переимпорт).
# Запуск (редактор закрыт): python Tools\run_headless_safe.py Tools\asda_import_loadscreen_donor.py --marker "КАРТИНКИ ЗАГРУЗКИ ДОНОРА: ГОТОВО"
import os
import traceback

import unreal

RAW = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "RawAssets", "Textures", "map"))
REF = "/Game/AsdaAssets/Textures/Maps/flammio/load/e_Flammio"
JOBS = [("inferion", "limg_Inferion00"), ("battlefield", "limg_battlefield00")]
PROPS = ["compression_settings", "srgb", "lod_group", "mip_gen_settings", "never_stream", "filter", "address_x", "address_y",
         "max_texture_size", "compression_quality", "power_of_two_mode", "lod_bias", "virtual_texture_streaming",
         "compression_no_alpha", "adjust_brightness", "adjust_saturation", "adjust_vibrance", "adjust_rgb_curve"]


def log(m):
    unreal.log("[загрузка донор] %s" % m)


def main():
    eal = unreal.EditorAssetLibrary
    ref = eal.load_asset(REF)
    if ref is None:
        raise RuntimeError("нет эталонной текстуры %s" % REF)
    reimport = bool(os.environ.get("ASDA_LOADDONOR_REIMPORT"))
    done = 0
    for d, name in JOBS:
        src = os.path.join(RAW, d, "load", name + ".png")
        dst_dir = "/Game/AsdaAssets/Textures/Maps/%s/load" % d
        path = "%s/%s" % (dst_dir, name)
        if not os.path.isfile(src):
            raise RuntimeError("нет исходника %s (сначала asda_loadscreen_compose.py)" % src)
        if eal.does_asset_exist(path) and not reimport:
            log("%s уже есть — пропуск" % path)
            continue
        task = unreal.AssetImportTask()
        task.filename = src
        task.destination_path = dst_dir
        task.destination_name = name
        task.replace_existing = True
        task.automated = True
        task.save = False
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        tex = eal.load_asset(path)
        if tex is None:
            raise RuntimeError("не импортирован %s" % path)
        for p in PROPS:
            try:
                tex.set_editor_property(p, ref.get_editor_property(p))
            except Exception as e:   # свойство отсутствует в этой версии движка — пишем, не молчим
                log("свойство %s не скопировано: %s" % (p, e))
        # set_editor_property сам вызывает PostEditChange (пересборка платформенных данных)
        unreal.log("ASDA_WRITES_ASSET: %s" % path)
        if not eal.save_loaded_asset(tex, False):
            raise RuntimeError("не сохранён %s" % path)
        diff = [p for p in PROPS if _safe(tex, p) != _safe(ref, p)]
        log("импортирован %s: %dx%d; расхождений настроек с e_Flammio: %s" % (
            path, tex.blueprint_get_size_x(), tex.blueprint_get_size_y(), diff or "нет"))
        done += 1
    log("КАРТИНКИ ЗАГРУЗКИ ДОНОРА: ГОТОВО (импортировано %d)" % done)


def _safe(o, p):
    """Значение свойства или None, если его нет."""
    try:
        return str(o.get_editor_property(p))
    except Exception:
        return None


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    try:
        main()
    except Exception:
        unreal.log_error("[загрузка донор] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
