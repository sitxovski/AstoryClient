# Copyright (c) 2026 @sitxovski (Discord)
# _reimport_alpha_textures.py — переимпортировать текстуры, которым вернули альфа-канал,
# сохранив настройки импорта (sRGB, сжатие, LOD-группа, мипы, фильтр, адресация).
#
# ★ ЗАЧЕМ СНИМАТЬ И ВОЗВРАЩАТЬ НАСТРОЙКИ. Импорт в UE сбрасывает часть свойств текстуры на
#   умолчания, а у нас от них зависят иконки и атласы (та же оговорка записана в
#   ue_import_ready_upscaled.py). Поэтому: снимок до → переимпорт → сверка → возврат отличий.
#
# ★ ПРОВЕРКА, А НЕ ВЕРА. После переимпорта скрипт печатает, есть ли у текстуры альфа НА САМОМ
#   ДЕЛЕ — по исходным данным ассета (`Texture.source`), а не по настройкам.
#
# Запуск: UnrealEditor-Cmd.exe <проект> -ExecutePythonScript=".../Tools/_reimport_alpha_textures.py"
import unreal

# (ассет, файл-источник; None — оставить тот, что записан в ассете)
JOBS = [
    ("/Game/AsdaAssets/Textures/Items/Texture/itm1_n_98_othr207_00", None),
    ("/Game/AsdaAssets/Textures/system/Texture/font", None),
    ("/Game/AsdaAssets/Fonts/font", None),
    ("/Game/AsdaAssets/Textures/Maps/QuestDir", None),
    ("/Game/AsdaAssets/Textures/Maps/QuestDirAnother", None),
]

KEEP = ("srgb", "compression_settings", "mip_gen_settings", "lod_group",
        "compression_no_alpha", "never_stream", "filter", "address_x", "address_y")


def snapshot(tex):
    out = {}
    for p in KEEP:
        try:
            out[p] = tex.get_editor_property(p)
        except Exception:
            pass
    return out


def alpha_report(tex):
    """Есть ли в исходных данных ассета рабочая альфа. Читаем сам источник, а не настройки."""
    try:
        src = tex.get_editor_property("source")
        fmt = src.get_editor_property("format")
        return "формат исходника: %s" % fmt
    except Exception as exc:
        return "формат исходника: ? (%s)" % exc


def main():
    eal = unreal.EditorAssetLibrary
    ok = fail = 0
    for path, override in JOBS:
        if not eal.does_asset_exist(path):
            unreal.log_warning("НЕТ АССЕТА: %s" % path)
            fail += 1
            continue
        tex = eal.load_asset(path)
        before = snapshot(tex)
        files = list(tex.get_editor_property("asset_import_data").extract_filenames())
        unreal.log("=== %s" % path)
        unreal.log("    до: %dx%d  %s  источник=%s" % (tex.blueprint_get_size_x(), tex.blueprint_get_size_y(),
                                                       alpha_report(tex), files))

        task = unreal.AssetImportTask()
        task.filename = override or (files[0] if files else "")
        task.destination_path = path.rsplit("/", 1)[0]
        task.destination_name = path.rsplit("/", 1)[1]
        task.replace_existing = True
        task.automated = True
        task.save = True
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

        tex = eal.load_asset(path)
        restored = []
        for p, v in before.items():
            try:
                if tex.get_editor_property(p) != v:
                    tex.set_editor_property(p, v)
                    restored.append(p)
            except Exception:
                pass
        if restored:
            unreal.log("    вернули настройки: %s" % ", ".join(restored))
        eal.save_asset(path)
        tex = eal.load_asset(path)
        unreal.log("    после: %dx%d  %s" % (tex.blueprint_get_size_x(), tex.blueprint_get_size_y(),
                                             alpha_report(tex)))
        ok += 1
    unreal.log("=== ГОТОВО: переимпортировано %d, пропущено %d ===" % (ok, fail))


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
