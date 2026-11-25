# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_bench_load_textures.py — ★ [ПОРТ] 12.10.2026 ФОНЫ ЭКРАНА ЗАГРУЗКИ ТЕСТА ПРОИЗВОДИТЕЛЬНОСТИ (решение владельца):
#   картинки владельца @@ASTORY_DEV_ROOT@@\Data\OwnerArt\pt_RU.png / pt_EN.png (2048×2048, контент в верхних 0.75, белый паддинг снизу — тот же
#   формат, что у штатных загрузочных текстур, поэтому крапится тем же материалом M_LoadingBG) импортируются в
#   /Game/AsdaAssets/Textures/Maps/load/pt_RU и pt_EN; never_stream как у остальных картинок загрузки (мипы не «мылятся»).
#   Код (`UAsdaBenchSubsystem::StartFirstRun`) ставит их через `UAsdaLoadManagerSubsystem::SetNextLoadingTexture` по языку сессии.
#   Идемпотентно: ассет есть — пропуск (переимпорт только по ключу ASDA_BENCHTEX_REIMPORT=1). Маркер: «ФОНЫ ЗАМЕРА: ГОТОВО».
# Запуск (редактор закрыт): python Tools\run_headless_safe.py Tools\asda_import_bench_load_textures.py --marker "ФОНЫ ЗАМЕРА: ГОТОВО"
import os
import traceback

import unreal

SRC = {"pt_RU": r"@@ASTORY_DEV_ROOT@@\Data\OwnerArt\pt_RU.png", "pt_EN": r"@@ASTORY_DEV_ROOT@@\Data\OwnerArt\pt_EN.png"}
DST_DIR = "/Game/AsdaAssets/Textures/Maps/load"


def log(m):
    unreal.log("[фоны замера] %s" % m)


def main():
    eal = unreal.EditorAssetLibrary
    reimport = bool(os.environ.get("ASDA_BENCHTEX_REIMPORT"))
    done = 0
    for name, src in SRC.items():
        path = "%s/%s" % (DST_DIR, name)
        if not os.path.isfile(src):
            raise RuntimeError("нет исходника %s" % src)
        if eal.does_asset_exist(path) and not reimport:
            log("%s уже есть — пропуск" % path)
            continue
        task = unreal.AssetImportTask()
        task.filename = src
        task.destination_path = DST_DIR
        task.destination_name = name
        task.replace_existing = True
        task.automated = True
        task.save = False
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        tex = eal.load_asset(path)
        if tex is None:
            raise RuntimeError("не импортирован %s" % path)
        tex.set_editor_property("never_stream", True)   # как у остальных картинок загрузки (texture-streaming-warmup)
        unreal.log("ASDA_WRITES_ASSET: %s" % path)
        if not eal.save_loaded_asset(tex, False):
            raise RuntimeError("не сохранён %s" % path)
        log("импортирован %s: %dx%d never_stream=1" % (path, tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))
        done += 1
    log("ФОНЫ ЗАМЕРА: ГОТОВО (импортировано %d)" % done)


if __name__ == "__main__" or globals().get("__spec__") is None:
    try:
        main()
    except Exception:
        unreal.log_error("[фоны замера] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
