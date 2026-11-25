# Copyright (c) 2026 @sitxovski (Discord)
import unreal, os
out=r"@@ASTORY_DEV_ROOT_FWD@@/Reports"


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for nm in ["n_01","n_02"]:
        t=unreal.load_asset("/Game/AsdaAssets/Textures/JUI/%s"%nm)
        if not t: unreal.log("MISS %s"%nm); continue
        unreal.log("TEXINFO %s comp=%s srgb=%s"%(nm, t.get_editor_property("compression_settings"), t.get_editor_property("srgb")))
        task=unreal.AssetExportTask()
        task.object=t; task.filename=os.path.join(out,"%s_exp.tga"%nm)
        task.automated=True; task.replace_identical=True; task.exporter=unreal.TextureExporterTGA()
        unreal.Exporter.run_asset_export_task(task)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
