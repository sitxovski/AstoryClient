# Copyright (c) 2026 @sitxovski (Discord)
# asda_cleanup_textures.py — чистка остатков первого (зеркального) импорта текстур.
# 1) пустая Textures/Data (0 ассетов, мусор-диры);
# 2) нормализация регистра Textures/system → Textures/System (как у соседей Characters/Maps/...).
# Запуск: в редакторе Tools -> Execute Python Script.
import unreal
EAL = unreal.EditorAssetLibrary

def main():
    base = "/Game/AsdaAssets/Textures"
    # 1) пустая Data
    d = base + "/Data"
    if EAL.does_directory_exist(d):
        n = len(EAL.list_assets(d, recursive=True))
        if n == 0:
            unreal.log("Удаляю пустую %s" % d); EAL.delete_directory(d)
        else:
            unreal.log_warning("%s НЕ пуста (%d ассетов) — пропуск" % (d, n))

    # 2) регистр system → System (case-only rename через временную папку, т.к. Windows регистронезависим)
    low = base + "/system"; cap = base + "/System"; tmp = base + "/_SystemTmp"
    if EAL.does_directory_exist(low):
        assets = EAL.list_assets(low, recursive=True)
        if assets:
            # перенести каждый ассет в System с сохранением подпути
            moved = 0
            for a in assets:
                sub = a[len(low):]                       # /Texture/EvenImage_01.EvenImage_01
                pkg = sub.rsplit(".", 1)[0]              # /Texture/EvenImage_01
                dst = cap + pkg
                if not EAL.does_asset_exist(dst) and EAL.rename_asset(a.rsplit(".",1)[0], dst):
                    moved += 1
            unreal.log("system → System: перенесено %d ассетов" % moved)
        if not EAL.list_assets(low, recursive=True):
            EAL.delete_directory(low)
            unreal.log("Удалена старая %s" % low)

    unreal.log("Чистка текстур завершена.")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
