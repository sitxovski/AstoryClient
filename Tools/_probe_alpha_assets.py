# Copyright (c) 2026 @sitxovski (Discord)
# _probe_alpha_assets.py — снять ПОЛНЫЙ структурный портрет текстур, у которых при апскейле
# потерялась альфа, до любой правки: настройки импорта, источник, кто на них ссылается.
#
# Нужен, чтобы переимпортировать с ТЕМИ ЖЕ настройками, что у соседей того же класса, и чтобы
# понять, где шрифтовые атласы вообще используются (их правка рискованнее всего).
#
# Запуск: UnrealEditor-Cmd.exe <проект> -ExecutePythonScript=".../Tools/_probe_alpha_assets.py"
import unreal

TARGETS = [
    "/Game/AsdaAssets/Textures/Items/Texture/itm1_n_98_othr207_00",
    "/Game/AsdaAssets/Fonts/font",
    "/Game/AsdaAssets/Textures/system/Texture/font",
    "/Game/AsdaAssets/Textures/Maps/QuestDir",          # эталон «уже починено»
    "/Game/AsdaAssets/Textures/Maps/QuestDirAnother",
]

# соседи того же класса — с чего списывать настройки
NEIGHBOURS = [
    "/Game/AsdaAssets/Textures/Items/Texture",
    "/Game/AsdaAssets/Textures/system/Texture",
]


def dump(tex, path):
    unreal.log("=== %s ===" % path)
    if tex is None:
        unreal.log("  НЕТ АССЕТА")
        return
    unreal.log("  класс: %s  размер: %dx%d" % (tex.get_class().get_name(),
                                               tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))
    for prop in ("srgb", "compression_settings", "mip_gen_settings", "lod_group",
                 "compression_no_alpha", "never_stream", "filter", "address_x", "address_y",
                 "deferred_pass_compression"):
        try:
            unreal.log("  %s = %s" % (prop, tex.get_editor_property(prop)))
        except Exception:
            pass
    try:
        src = tex.get_editor_property("asset_import_data")
        unreal.log("  источник: %s" % src.extract_filenames())
    except Exception as exc:
        unreal.log("  источник: ? (%s)" % exc)
    try:
        unreal.log("  формат пикселей: %s" % tex.get_editor_property("platform_data"))
    except Exception:
        pass


def main():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    for path in TARGETS:
        tex = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
        dump(tex, path)
        refs = unreal.EditorAssetLibrary.find_package_referencers_for_asset(path, False)
        unreal.log("  ССЫЛАЮТСЯ (%d): %s" % (len(refs), list(refs)[:40]))

    for folder in NEIGHBOURS:
        assets = unreal.EditorAssetLibrary.list_assets(folder, recursive=False, include_folder=False)
        unreal.log("=== соседи в %s: %d ===" % (folder, len(assets)))
        shown = 0
        for a in assets:
            if shown >= 6:
                break
            t = unreal.EditorAssetLibrary.load_asset(a)
            if not isinstance(t, unreal.Texture2D):
                continue
            unreal.log("  %-70s srgb=%s comp=%s mips=%s lod=%s noalpha=%s %dx%d" % (
                a.split("/")[-1], t.get_editor_property("srgb"),
                t.get_editor_property("compression_settings"),
                t.get_editor_property("mip_gen_settings"),
                t.get_editor_property("lod_group"),
                t.get_editor_property("compression_no_alpha"),
                t.blueprint_get_size_x(), t.blueprint_get_size_y()))
            shown += 1
    unreal.log("=== ГОТОВО ===")


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
