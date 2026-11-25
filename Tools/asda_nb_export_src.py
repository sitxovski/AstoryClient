# Copyright (c) 2026 @sitxovski (Discord)
# asda_nb_export_src.py — ШАГ 1 превью «нормаль из альбедо»: выгрузить альбедо пяти образцов в PNG.
#   Берём текстуру ИЗ АССЕТА (а не из RawAssets по имени) — в RawAssets рядом лежат одноимённые
#   портреты NPC, поиск по имени легко берёт не тот файл.
#   Экспорт идёт через AssetExportTask + TextureExporterPNG, то есть отдаёт исходник (source art),
#   а не пожатый DXT.
#   Ничего в проекте не меняет. Запуск: run_headless.bat asda_nb_export_src.py
import os

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools/_out/nb_preview"

# Пятёрка образцов — РАЗНЫЕ типы поверхности. Пути сняты дампом asda_dump_mesh_textures.py.
SAMPLES = [
    # ключ,        подпись для листа,            текстура
    ("stone",  "Камень (статуя)",       "/Game/AsdaAssets/Textures/Maps/rein/Texture/MDEL_STONEMAN-A_00.MDEL_STONEMAN-A_00"),
    ("build",  "Кладка и дерево (дом)", "/Game/AsdaAssets/Textures/Maps/rein/Texture/MDEL_SPABUILD-B_00.MDEL_SPABUILD-B_00"),
    ("tree",   "Кора и листва (дерево)", "/Game/AsdaAssets/Textures/Maps/nightvalley/Texture/MDEL_OLDTREE-A_00.MDEL_OLDTREE-A_00"),
    ("armor",  "Ткань и броня (экип)",  "/Game/AsdaAssets/Textures/Characters/Texture/ch1_f_00_chst103_00_00_0.ch1_f_00_chst103_00_00_0"),
    ("mob",    "Шкура моба (голем)",    "/Game/AsdaAssets/Textures/Mobs/Texture/mon_b_3_firegolem-a_00.mon_b_3_firegolem-a_00"),
]


def main():
    import unreal
    os.makedirs(OUT_DIR, exist_ok=True)
    eal = unreal.EditorAssetLibrary
    ok = 0
    for key, _label, path in SAMPLES:
        tex = eal.load_asset(path)
        if not tex:
            unreal.log_error("НЕТ текстуры: %s" % path)
            continue
        dst = "%s/%s_A.png" % (OUT_DIR, key)
        task = unreal.AssetExportTask()
        task.set_editor_property("object", tex)
        task.set_editor_property("filename", dst)
        task.set_editor_property("automated", True)
        task.set_editor_property("prompt", False)
        task.set_editor_property("replace_identical", True)
        task.set_editor_property("exporter", unreal.TextureExporterPNG())
        if unreal.Exporter.run_asset_export_task(task):
            ok += 1
            unreal.log("экспорт %s -> %s" % (key, dst))
        else:
            unreal.log_error("экспорт НЕ прошёл: %s" % path)
    unreal.log("=== nb_export_src: %d / %d ===" % (ok, len(SAMPLES)))


if __name__ == "__main__":
    main()
