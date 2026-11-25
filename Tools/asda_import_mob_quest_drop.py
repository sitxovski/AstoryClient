# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""asda_import_mob_quest_drop.py — таблица «с какого монстра падает квестовый предмет».

Источник: `RawAssets/quest/DT_MobQuestDrop.csv` — его пишет генератор `RE/tools/dump_monster_quest_items.py`
(прямое чтение `NpcServer\\Monster\\Monster.ai` живого сервера, полная выгрузка — `Reports/quest_item_source.tsv`).
★ 14.10.2026 (этап 5): названия монстра/предмета/задания/NPC генератор берёт из таблиц порта (DT_MobNames, item_list,
EpisodeTable, npc_list), а не из серверных строк; перезапуск даёт побайтно боевой CSV (`--check` сверяет).

Зачем таблица клиенту: ответить игроку «где это добыть» — по коду предмета показать монстров,
шанс и карты. Сама выдача предмета целиком серверная, клиент в ней не участвует, поэтому данные
СПРАВОЧНЫЕ и на игровую логику не влияют.

Строка = `FAsdaMobQuestDropRow` (`Source/AsdaReborn/Public/Quest/AsdaQuestTypes.h`).
Ключ строки — `<видМонстра>_<слот>`: у одного монстра до пяти квест-записей.

Запуск: headless через `Tools/run_headless_safe.py`.
"""
import os
import unreal

CSV = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "RawAssets", "quest", "DT_MobQuestDrop.csv")
PKG_DIR = "/Game/AsdaAssets/Data/Quest"
ASSET = "DT_MobQuestDrop"


def log(m):
    unreal.log("[квест-дроп] " + str(m))


def main():
    csv_path = os.path.normpath(CSV)
    if not os.path.isfile(csv_path):
        log("НЕТ файла %s — сперва подготовь CSV" % csv_path)
        log("ЗАВЕРШЕНО")
        return

    factory = unreal.CSVImportFactory()
    factory.automated_import_settings.import_row_struct = unreal.load_object(
        None, "/Script/AsdaReborn.AsdaMobQuestDropRow")

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", csv_path)
    task.set_editor_property("destination_path", PKG_DIR)
    task.set_editor_property("destination_name", ASSET)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    task.set_editor_property("factory", factory)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    path = "%s/%s.%s" % (PKG_DIR, ASSET, ASSET)
    dt = unreal.EditorAssetLibrary.load_asset(path)
    if not dt:
        log("импорт не удался: %s" % path)
        log("ЗАВЕРШЕНО")
        return
    unreal.EditorAssetLibrary.save_loaded_asset(dt)

    names = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
    log("таблица %s: строк %d" % (path, len(names)))

    # Сводка по данным — чтобы видеть, что импортировалось именно то.
    items, mobs, broken, guaranteed = set(), set(), 0, 0
    for rn in names:
        found, row = unreal.DataTableFunctionLibrary.get_data_table_row_from_name(
            dt, rn, unreal.AsdaMobQuestDropRow())
        if not found:
            continue
        items.add(row.item_id)
        mobs.add(row.mob_id)
        broken += 1 if row.b_broken else 0
        guaranteed += 1 if row.b_guaranteed else 0
    log("  предметов %d, видов монстров %d, гарантированных %d, помечено дефектными %d"
        % (len(items), len(mobs), guaranteed, broken))
    log("ЗАВЕРШЕНО")


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
