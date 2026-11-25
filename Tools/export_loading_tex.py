# Copyright (c) 2026 @sitxovski (Discord)
# Экспорт загрузочных текстур в PNG для визуальной проверки (есть ли белый паддинг снизу в самой текстуре).
import unreal
out = r"@@ASTORY_DEV_ROOT@@\Reports"


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    task = unreal.AssetExportTask()
    for name, p in [("ad_img000", "/Game/AsdaAssets/Textures/Maps/load/ad_img000"),
                    ("e_Alpen", "/Game/AsdaAssets/Textures/Maps/alpen/load/e_Alpen")]:
        t = unreal.load_asset(p)
        if not t:
            unreal.log_warning(f"[exp] нет {p}"); continue
        task = unreal.AssetExportTask()
        task.object = t
        task.filename = out + "\\" + name + ".png"
        task.automated = True
        task.replace_identical = True
        task.exporter = unreal.TextureExporterPNG()
        unreal.Exporter.run_asset_export_task(task)
        unreal.log(f"[exp] {name} → {task.filename}")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
