# Copyright (c) 2026 @sitxovski (Discord)
# Проба 2: точное свойство skeleton в pipeline + механизм options у AssetImportTask.
import unreal
def L(m): unreal.log("[PROBE] " + str(m))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    p = unreal.InterchangeGenericAssetsPipeline()
    c = p.get_editor_property("common_skeletal_meshes_and_animations_properties")
    L("common_skeletal props: %s" % [x for x in dir(c) if not x.startswith("__")])
    # skeleton field?
    for cand in ("skeleton", "import_only_animation", "import_meshes_in_bone_hierarchy"):
        try: L("  %s = %r" % (cand, c.get_editor_property(cand)))
        except Exception as e: L("  %s FAIL %s" % (cand, e))
    # pipeline top-level relevant
    L("pipeline props: %s" % [x for x in dir(p) if not x.startswith("__") and ("mesh" in x.lower() or "import" in x.lower() or "skelet" in x.lower())])
    # InterchangePipelineStackOverride
    try:
        ov = unreal.InterchangePipelineStackOverride()
        L("override props: %s" % [x for x in dir(ov) if not x.startswith("__")])
    except Exception as e: L("override ctor FAIL %s" % e)
    L("PROBE2 DONE")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
