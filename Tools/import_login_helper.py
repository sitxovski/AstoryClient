# Copyright (c) 2026 @sitxovski (Discord)
# Импорт LoginHelperText (loginhelpertext.csv) → UE DataTable DT_LoginHelper (FAsdaLoginHelperRow).
# RowName=_row (0..55), TabIndex=mode (0=Sation/1=Praise/2=Warning), Text=реплика Минопио. Показ в ANI_TXT.
import unreal, os
import os as _os_dtc  # noqa: E402  ★ дедуп №2 В2 Т4: общий шаг «заливка → сохранение» DataTable
import sys as _sys_dtc  # noqa: E402
_sys_dtc.path.insert(0, _os_dtc.path.dirname(_os_dtc.path.abspath(__file__)))
import asda_dt_common as _DTC  # noqa: E402
AT = unreal.AssetToolsHelpers.get_asset_tools(); EAL = unreal.EditorAssetLibrary
CSV = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/RawAssets/Data/ItemDB/loginhelpertext.csv"
PKG = "/Game/AsdaAssets/Text"; NAME = "DT_LoginHelper"
full = "%s/%s" % (PKG, NAME)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    struct = unreal.load_object(None, "/Script/AsdaReborn.AsdaLoginHelperRow")
    if not struct:
        unreal.log_error("FAsdaLoginHelperRow не найден — собрать модуль")
    else:
        dt = unreal.load_object(None, full) if EAL.does_asset_exist(full) else None
        if not dt:
            fac = unreal.DataTableFactory(); fac.set_editor_property("struct", struct)
            dt = AT.create_asset(NAME, PKG, unreal.DataTable, fac)
        if not dt:
            unreal.log_error("DT_LoginHelper не создан")
        else:
            csv_str = open(CSV, encoding="utf-8-sig").read()
            _filled = _DTC.fill_and_save(unreal, dt, csv_str, unreal.log_warning, fmt="csv")  # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т4: провал заливки → DT не сохраняется; маркер ASDA_WRITES_ASSET
            rows = len(unreal.DataTableFunctionLibrary.get_data_table_row_names(dt))
            unreal.log("IMPORTED DT_LoginHelper строк=%d" % rows)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
