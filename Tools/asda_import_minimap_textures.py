# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_minimap_textures.py — импорт КАРТИНОК КАРТ миникарты/большой карты в UE.
#   RawAssets/Textures/MapImage/*.png → /Game/AsdaAssets/Textures/MapImage/<basename>
#   Имя ассета = ИМЯ ФАЙЛА ОРИГИНАЛА без .TEX (alpen, alpen_big, alpen_town_big, Rein, Rein_big, …) —
#   рантайм (UAsdaMinimapSubsystem) строит его из DirName карты ровно как эталон строил путь
#   "./Map/<dir>/<dir>[_big|_town_big].TEX", регистр ищется без учёта case.
#   Настройки: UI-группа (без мипов и сжатия — карта рисуется 1:1 пиксельным квадом, как в D3D-оригинале).
import os
import unreal

SRC = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Textures\MapImage"
DST = "/Game/AsdaAssets/Textures/MapImage"
EAL = unreal.EditorAssetLibrary


def main():
    if not EAL.does_directory_exist(DST):
        EAL.make_directory(DST)
    tasks = []
    for f in sorted(os.listdir(SRC)):
        if not f.lower().endswith(".png"):
            continue
        t = unreal.AssetImportTask()
        t.filename = os.path.join(SRC, f)
        t.destination_path = DST
        t.destination_name = os.path.splitext(f)[0]
        t.replace_existing = True
        t.automated = True
        t.save = True
        tasks.append(t)
    if not tasks:
        unreal.log_warning("[minimap-tex] нечего импортировать: " + SRC)
        return
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    n = 0
    for t in tasks:
        p = "%s/%s" % (DST, t.destination_name)
        a = EAL.load_asset(p)
        if not a:
            unreal.log_warning("[minimap-tex] НЕ импортирован " + p)
            continue
        # UI-текстура: без мипов, без потерь, clamp — квад карты рисуется 1:1 пикселями (как DrawPrimitiveUP).
        a.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        a.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        a.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        a.set_editor_property("srgb", True)
        a.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
        a.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
        EAL.save_asset(p)
        n += 1
    unreal.log("[minimap-tex] импортировано %d текстур → %s" % (n, DST))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
