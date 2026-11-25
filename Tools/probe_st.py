# Copyright (c) 2026 @sitxovski (Discord)
import unreal


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    st=unreal.load_asset("/Game/AsdaAssets/Text/ST_GameText")
    print("ST methods set/string/entry/import:", [m for m in dir(st) if any(k in m.lower() for k in ('set','entry','source','import','csv','meta')) and not m.startswith('_')])
    print("StringTableLibrary methods:", [m for m in dir(unreal.StringTableLibrary) if not m.startswith('_')])
    # CSV factory для StringTable?
    for f in ('CSVImportFactory','ReimportStringTableFactory','StringTableFactory'):
        print("has",f,hasattr(unreal,f))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
