# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Удаление ДУБЛЕЙ UI-текстур в проекте.
# Причина: атласы juidata попали в проект дважды — как /Game/AsdaAssets/Textures/JUI
# (канонический путь, импортируется import_jui_textures.py) и как
# /Game/AsdaAssets/Textures/Misc/juidata/* (побочный результат общего импорта текстур).
# Оставляем JUI, второй набор удаляем.
import unreal

EAL = unreal.EditorAssetLibrary
DUPS = ("/Game/AsdaAssets/Textures/Misc/juidata",)

def log(m): unreal.log("[cleanup-dup] " + str(m))

total = 0


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global total
    for path in DUPS:
        if not EAL.does_directory_exist(path):
            log("нет каталога: %s" % path); continue
        assets = EAL.list_assets(path, recursive=True, include_folder=False)
        log("%s: ассетов %d" % (path, len(assets)))
        for a in assets:
            if EAL.delete_asset(a):
                total += 1
        EAL.delete_directory(path)
    log("удалено дублей: %d" % total)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
