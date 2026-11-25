# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_reimport_questdir.py — точечный переимпорт ДВУХ текстур стрелки-компаса трекера заданий
# после восстановления альфа-канала (`asda_restore_bmp_alpha.py`).
#
# ★ Почему точечно, а не общим `asda_import_textures.py`: тот инкрементальный и УЖЕ
#   существующие ассеты пропускает (`does_asset_exist`), то есть исправленный PNG не заехал бы.
#
# ★ Настройки текстуры выставляются явно:
#   · CompressionSettings = TC_EditorIcon (UserInterface2D) — значок 40×40 с ДВОИЧНОЙ маской;
#     DXT5 положил бы блочные артефакты прямо на край круга, а DXT1 выбросил бы альфу совсем;
#   · MipGenSettings = NoMipmaps — значок рисуется 1:1 в UMG, мипы ему не нужны;
#   · SRGB = True — цвет, а не маска данных.
#
# Запуск: Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_probe_reimport_questdir.py
import os

SRC_DIR = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Textures\map"
DEST = "/Game/AsdaAssets/Textures/Maps"
NAMES = ["QuestDir", "QuestDirAnother"]


def main():
    import unreal
    atools = unreal.AssetToolsHelpers.get_asset_tools()
    EAL = unreal.EditorAssetLibrary
    for cmd in ("Interchange.FeatureFlags.Import.DDS 0", "Interchange.FeatureFlags.Import.Enabled 0"):
        try:
            unreal.SystemLibrary.execute_console_command(None, cmd)
        except Exception:
            pass

    files = [os.path.join(SRC_DIR, n + ".png") for n in NAMES]
    for f in files:
        unreal.log_warning("[стрелка] источник %s (%d б)" % (f, os.path.getsize(f)))

    data = unreal.AutomatedAssetImportData()
    data.set_editor_property("destination_path", DEST)
    data.set_editor_property("filenames", files)
    data.set_editor_property("replace_existing", True)
    data.set_editor_property("skip_read_only", True)
    imported = atools.import_assets_automated(data)
    unreal.log_warning("[стрелка] переимпортировано ассетов: %d" % len(imported))

    for n in NAMES:
        path = "%s/%s" % (DEST, n)
        tex = EAL.load_asset(path)
        if not tex:
            unreal.log_error("[стрелка] НЕ загрузился ассет %s" % path)
            continue
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property("srgb", True)
        tex.set_editor_property("never_stream", True)
        EAL.save_loaded_asset(tex)
        # Контроль ЧИСЛАМИ: размер, формат и наличие альфы после сборки платформенных данных.
        unreal.log_warning("[стрелка] %s: %dx%d, сжатие=%s, альфа в исходнике=%s"
                           % (n, tex.blueprint_get_size_x(), tex.blueprint_get_size_y(),
                              str(tex.get_editor_property("compression_settings")),
                              str(not tex.get_editor_property("compression_no_alpha"))))


if __name__ == "__main__" and globals().get("__spec__") is None:
    main()
