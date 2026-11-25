# Copyright (c) 2026 @sitxovski (Discord)
import unreal
CSV=r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/RawAssets/Text/GameText.csv"
PKG="/Game/AsdaAssets/Text"; NAME="DT_GameText"; full="%s/%s"%(PKG,NAME)
EAL=unreal.EditorAssetLibrary


def manual_edit_guard():
    """★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т8 (находка Т8 аудита): СТОРОЖ РУЧНОЙ ПРАВКИ перед полным переимпортом.

    Этот скрипт УДАЛЯЕТ ассет DT_GameText и импортирует его заново из GameText.csv, затем накладывает ведомость
    `Tools/data/gametext_overrides.json`. Правка, сделанная в ассете руками и не попавшая ни в CSV, ни в ведомость,
    при этом молча терялась (закон owner-asset-edits-never-lose). Теперь до удаления ассет сверяется с тем, что
    получится после переимпорта (CSV + ведомость; перевод строки — в обоих видах, `asda_ledger.eq_nl`):
      совпало      — переимпорт ничего не теряет, продолжаем;
      расходится   — СТОП, ассет не трогается; в лог — ключи. Правку перенести в ведомость (точечный путь —
                     `Tools/asda_gametext_overrides.py`), затем повторить. Осознанный обход — только с разрешения
                     владельца: ASDA_GAMETEXT_FORCE_REIMPORT=1.
    Ассета нет (пустой проект) — сторож не нужен. Возврат: True — можно переимпортировать."""
    import csv, io, os, sys
    if not EAL.does_asset_exist(full):
        return True
    if os.environ.get("ASDA_GAMETEXT_FORCE_REIMPORT") == "1":
        print("СТОРОЖ GameText: ОБХОД по ASDA_GAMETEXT_FORCE_REIMPORT=1 (решение владельца)")
        return True
    here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools"
    if here not in sys.path:
        sys.path.insert(0, here)
    import asda_ledger as _L
    import asda_gametext_overrides as _GO
    with open(CSV, encoding="utf-8-sig", newline="") as fh:
        want = {r[0]: r[1] for r in list(csv.reader(fh))[1:] if len(r) >= 2}
    for r in _GO.load_ledger():                     # после переимпорта ведомость встанет поверх CSV
        want[str(r["key"])] = r["value"]
    dt = EAL.load_asset(full)
    exp = list(csv.reader(io.StringIO(unreal.DataTableFunctionLibrary.export_data_table_to_csv_string(dt))))
    vi = exp[0].index("SourceString")
    have = {r[0]: r[vi] for r in exp[1:]}
    diff = sorted((k for k in set(want) | set(have)
                   if k not in want or k not in have or not _L.eq_nl(have[k], want[k])), key=lambda k: (len(k), k))
    if diff:
        print("СТОП GameText: ассет расходится с GameText.csv + ведомостью в %d ключах %s — похоже на ручную правку; "
              "ассет НЕ удалён и НЕ переимпортирован" % (len(diff), diff[:20]))
        return False
    print("СТОРОЖ GameText: ассет = CSV + ведомость (%d ключей) — переимпорт ничего не теряет" % len(have))
    return True


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    if not manual_edit_guard():
        return
    if EAL.does_asset_exist(full): EAL.delete_asset(full)
    task=unreal.AssetImportTask()
    task.filename=CSV; task.destination_path=PKG; task.destination_name=NAME
    task.replace_existing=True; task.automated=True; task.save=True
    fac=unreal.CSVImportFactory()
    try:
        fac.automated_import_settings.import_row_struct=unreal.AsdaGameTextRow.static_struct()
    except Exception as e:
        print("STRUCTERR",e)
    task.factory=fac
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    dt=unreal.load_asset(full)
    print("DT",dt)
    # ★ 08.10.2026: ведомость правок строк (решения владельца) — поверх импорта, точечно (Tools/asda_gametext_overrides.py).
    import sys as _sys, os as _os
    _sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)) if "__file__" in globals() else r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools")
    import asda_gametext_overrides
    asda_gametext_overrides.main(source_csv=False)
    _n = len(unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)) if dt else 0
    # ★ Маркер для `run_headless_safe.py`: код возврата редактора недостоверен
    #   ([[headless-exit-code-unreliable]]), поэтому успех опознаётся по этой строке в логе.
    print("ЗАВЕРШЕНО: DT_GameText, строк=%d" % _n)
    if dt:
        names=unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
        print("ROWS=%d"%len(names))
        for k in ['116','1201','1202','1203']:
            found,row=unreal.DataTableFunctionLibrary.get_data_table_row_from_name(dt,k) if hasattr(unreal.DataTableFunctionLibrary,'get_data_table_row_from_name') else (False,None)
        # прямой экспорт для проверки
        print("sample keys:", [str(n) for n in list(names)[:5]])


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
