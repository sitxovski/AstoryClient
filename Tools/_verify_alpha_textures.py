# Copyright (c) 2026 @sitxovski (Discord)
# _verify_alpha_textures.py — доказать, что альфа реально лежит В АССЕТЕ, а не только в PNG на диске.
#
# ★ Свойство `Texture.Source` из Python не читается (protected), поэтому проверяем вторым методом:
#   выгружаем текстуру обратно в файл штатным экспортёром и смотрим канал прозрачности Pillow'ом
#   (это делает уже обычный python, см. Tools/NormalStudio/audit_alpha.py). Промежуточные файлы кладём
#   в RawAssets/Data/_alpha_check — НЕ в Content, иначе редактор заведёт ассет-двойник.
import os

import unreal

OUT = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/RawAssets/Data/_alpha_check"
TARGETS = [
    "/Game/AsdaAssets/Textures/Items/Texture/itm1_n_98_othr207_00",
    "/Game/AsdaAssets/Textures/system/Texture/font",
    "/Game/AsdaAssets/Fonts/font",
    "/Game/AsdaAssets/Textures/Maps/QuestDir",
    "/Game/AsdaAssets/Textures/Maps/QuestDirAnother",
    # ★ РАБОЧИЙ атлас всплывающих надписей (цифры урона, EXP, MISS) — тот же font.TEX,
    #   но отдельная копия; проверяем и его, у него потеря альфы = чёрные плашки.
    "/Game/AsdaAssets/UI/T_AsdaCombatFont",
]


def main():
    os.makedirs(OUT, exist_ok=True)
    for path in TARGETS:
        tex = unreal.EditorAssetLibrary.load_asset(path)
        if tex is None:
            unreal.log_warning("НЕТ: %s" % path)
            continue
        # ★ имя из ПОЛНОГО пути: две разные текстуры зовутся font, и по короткому имени
        #   второй экспорт затирал первый — «проверка» смотрела бы один и тот же файл.
        name = path.lstrip("/").replace("/", "__")
        dst = os.path.join(OUT, name + ".tga")           # TGA несёт 32 бита, альфа не теряется
        task = unreal.AssetExportTask()
        task.object = tex
        task.filename = dst
        task.automated = True
        task.prompt = False
        task.exporter = unreal.TextureExporterTGA()
        ok = unreal.Exporter.run_asset_export_task(task)
        try:
            src = tex.get_editor_property("asset_import_data").extract_filenames()
        except Exception:
            src = []
        unreal.log("%-70s %dx%d экспорт=%s источник=%s" % (path, tex.blueprint_get_size_x(),
                                                           tex.blueprint_get_size_y(), ok, src))
    unreal.log("=== ГОТОВО ===")


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
