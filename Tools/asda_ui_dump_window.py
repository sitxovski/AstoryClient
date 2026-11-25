# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# ★ ПОЛНЫЙ СТРУКТУРНЫЙ РАЗБОР ОКНА ИЗ HEADLESS (закон full-structural-analysis-always).
#
# Раньше этот путь считался закрытым: свойство `UWidgetBlueprint.widget_tree` из UE-Python
# защищённое (зафиксировано `_probe_chat_tab_editor_widget.py`, лог `headless_tabdump.log`).
# Обход — на стороне C++: `UAsdaUmgBuilder::DumpWidgetTree` печатает по каждому узлу имя, класс,
# родителя, схему слота (SPAN|POINT), анкоры, оффсеты, alignment/autosize/z, а у TextBlock — цвет
# и текст. Теперь дамп снимается БЕЗ запуска игры.
#
# ЗАПУСК:  Tools\run_headless.bat Tools\asda_ui_dump_window.py
#   ASDA_DUMP_WBP="WBP_CHAT_TAB_WIN WBP_CHAT_Color_Select_Win"  — какие окна (через пробел).
import unreal, os

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder

WANT = (os.environ.get("ASDA_DUMP_WBP", "") or
        "WBP_CHAT_TAB_WIN WBP_CHAT_Color_Select_Win").split()


def main():
    index = {}
    for a in EAL.list_assets("/Game/AsdaAssets/UI", recursive=True, include_folder=False):
        p = a.split(".")[0]
        index[p.rsplit("/", 1)[-1]] = p
    for w in WANT:
        path = index.get(w)
        if not path:
            unreal.log_warning("[дамп-окна] ассет %s не найден" % w)
            continue
        bp = EAL.load_asset(path)
        if not bp:
            unreal.log_warning("[дамп-окна] %s не грузится" % path)
            continue
        for line in B.dump_widget_tree(bp).split("\n"):
            unreal.log("[дамп-окна] " + line)


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    main()
