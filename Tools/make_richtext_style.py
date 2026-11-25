# Copyright (c) 2026 @sitxovski (Discord)
# DT_AsdaRichStyle — style set для URichTextBlock (RowStruct=RichTextStyleRow).
#   Строка "Default" = игровой шрифт AsdaUIFontRT, белый цвет, размер 14. Используется как базовый
#   (неформатированный) стиль rich-текста; форматированные сегменты рисует UAsdaRichDecorator.
#   Запуск (редактор ЗАКРЫТ):
#     "D:/Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
#       "@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject" \
#       -ExecutePythonScript="@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools/make_richtext_style.py" \
#       -unattended -nopause -nosplash -nosound -NullRHI -stdout
import unreal, json
import os as _os_dtc  # noqa: E402  ★ дедуп №2 В2 Т4: общий шаг «заливка → сохранение» DataTable
import sys as _sys_dtc  # noqa: E402
_sys_dtc.path.insert(0, _os_dtc.path.dirname(_os_dtc.path.abspath(__file__)))
import asda_dt_common as _DTC  # noqa: E402

PKG  = "/Game/AsdaAssets/UI"
NAME = "DT_AsdaRichStyle"
FULL = "%s/%s" % (PKG, NAME)
FONT = "/Game/AsdaAssets/Fonts/AsdaUIFontRT.AsdaUIFontRT"

AT  = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    struct = unreal.load_object(None, "/Script/UMG.RichTextStyleRow")
    if not struct:
        unreal.log_error("RichTextStyleRow не найден (UMG не загружен)")
    else:
        # Идемпотентно: существующий переиспользуем (delete в unattended не всегда снимает пакет).
        if EAL.does_asset_exist(FULL):
            dt = unreal.load_object(None, FULL)
        else:
            fac = unreal.DataTableFactory()
            fac.set_editor_property("struct", struct)
            dt = AT.create_asset(NAME, PKG, unreal.DataTable, fac)
        if not dt:
            unreal.log_error("DT_AsdaRichStyle не создан")
        else:
            # JSON-строка строки Default (формат экспорта UE DataTable). FTextBlockStyle: Font + ColorAndOpacity.
            rows = [{
                "Name": "Default",
                "TextStyle": {
                    "Font": {
                        "FontObject": FONT,
                        "TypefaceFontName": "Regular",
                        "Size": 10.0
                    },
                    "ColorAndOpacity": {
                        "SpecifiedColor": {"R": 1.0, "G": 1.0, "B": 1.0, "A": 1.0}
                    }
                }
            }]
            _filled = _DTC.fill_and_save(unreal, dt, json.dumps(rows), unreal.log_warning, fmt="json")  # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т4: провал заливки → DT не сохраняется; маркер ASDA_WRITES_ASSET
            names = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
            unreal.log("MADE DT_AsdaRichStyle строк=%d (%s)" % (len(names), ",".join(str(n) for n in names)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
