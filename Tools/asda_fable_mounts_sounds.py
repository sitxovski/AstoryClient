# Copyright (c) 2026 @sitxovski (Discord)
# asda_fable_mounts_sounds.py — headless UE: звуки езды, которых нет в основной сборке, из донора AsdaFableClassic
# (Data\Sound) → /Game/AsdaAssets/Sound (тот же каталог и способ, что asda_import_all_sounds.py). На них ссылается
# Ride_effect_table донора у новых маунтов: ковры (0065/0067) — s_e_riding_carpet; унициклус (0063) — Cbike_id1/id2.
# Только новые ассеты (существующие не перезаписываются). Запуск: python run_headless_safe.py asda_fable_mounts_sounds.py --marker "ЗАВЕРШЕНО: fable-звуки"
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as _P

SRC = os.path.join(_P.DONORS, "AsdaFableClassic", "Data", "Sound")
DEST = "/Game/AsdaAssets/Sound"
NEED = ["s_e_riding_carpet.wav", "s_e_riding_Cbike_id1.wav", "s_e_riding_Cbike_id2.wav"]


def main():
    import unreal
    eal = unreal.EditorAssetLibrary
    files = [os.path.join(SRC, f) for f in NEED if not eal.does_asset_exist("%s/%s" % (DEST, os.path.splitext(f)[0]))]
    if files:
        d = unreal.AutomatedAssetImportData()
        d.set_editor_property("destination_path", DEST)
        d.set_editor_property("filenames", files)
        d.set_editor_property("replace_existing", False)
        for a in unreal.AssetToolsHelpers.get_asset_tools().import_assets_automated(d):
            eal.save_loaded_asset(a)
            unreal.log("fable-звук: %s" % a.get_path_name())
    unreal.log("ЗАВЕРШЕНО: fable-звуки (к импорту было %d)" % len(files))


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
