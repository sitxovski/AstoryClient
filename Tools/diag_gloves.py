# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Диагностика «стоковых» перчаток/рук: есть ли в проекте меш, текстура и материал grov105.
import unreal

EAL = unreal.EditorAssetLibrary
def log(m): unreal.log("[diag-gloves] " + str(m))

# 1) ищем ВСЁ, что содержит grov105, по всему /Game
ar = unreal.AssetRegistryHelpers.get_asset_registry()


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    found = [str(a.package_name) for a in ar.get_assets_by_path("/Game", recursive=True)
             if "grov105" in str(a.asset_name).lower()]
    log("ассетов с grov105: %d" % len(found))
    for f in sorted(found): log("   " + f)
    # 2) какие вообще перчатки (grov) есть
    gl = sorted({str(a.asset_name) for a in ar.get_assets_by_path("/Game", recursive=True)
                 if "grov" in str(a.asset_name).lower()})
    log("всего ассетов с 'grov': %d, примеры: %s" % (len(gl), gl[:10]))
    # 3) каталоги, где лежат части персонажа
    for p in ("/Game/AsdaAssets/Textures/Data/Character/Texture",
              "/Game/AsdaAssets/Models/Character", "/Game/AsdaAssets/Meshes/Character",
              "/Game/AsdaAssets/Textures/Character"):
        log("%s -> существует=%s, ассетов=%s" % (
            p, EAL.does_directory_exist(p),
            len(EAL.list_assets(p, recursive=True, include_folder=False)) if EAL.does_directory_exist(p) else "-"))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
