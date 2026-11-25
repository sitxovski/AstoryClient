# Copyright (c) 2026 @sitxovski (Discord)
import unreal


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    dt=unreal.load_asset("/Game/AsdaAssets/Text/DT_GameText.DT_GameText")
    if not dt: unreal.log("DT_NONE"); 
    else:
        for rid in ["114","115","11000","11051"]:
            r=dt.find_row_unique(unreal.Name(rid)) if hasattr(dt,'find_row_unique') else None
            unreal.log("DTROW %s exists=%s"%(rid, rid in [str(x) for x in dt.get_row_names()]))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
