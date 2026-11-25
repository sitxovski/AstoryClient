# Copyright (c) 2026 @sitxovski (Discord)
# Достройка ПОДПИСЕЙ КОЛИЧЕСТВА для ячеек СУМОК 5…8 окна склада (WBP_WARE_WIN, JUI `WARE_WIN` 4000).
#
# ЗАЧЕМ. В `.JUI` ячейки сумок 1…4 имеют idc (`WAREBAG1_TRAY00`…), а у сумок 5…8 они объявлены
#   с ПУСТЫМ idc — `ID = ""` при живом `IDNum` (75411…75440 / 75511… / 75611… / 75711…).
#   Импортёр UI именует виджеты ПО IDC, поэтому в ассете эти ячейки приехали безымянными
#   (`NoImage_icon_<n>_0`), а `TextBlock`-подписи количества у них не появилось ВООБЩЕ:
#   у сумок 1…4 в группе по 62 узла (30 иконок + 30 подписей + 2 служебных), у 5…8 — по 32.
#   Следствие в игре: в сумках 5…8 не рисовалось количество в стеке.
#   (Сами иконки подсистема привязывает по порядку детей — это уже сделано в коде.)
#
# ЧТО ДЕЛАЕТ СКРИПТ (идемпотентно: существующие подписи не трогает):
#   для каждой сумки 5…8 добавляет в её группу `WAREBAG{n}_TRAYIcon_grp` 30 `TextBlock`
#   с именами `WAREBAG{n}_TRAY00`…`_TRAY29` — ровно та схема имён, которую ищет
#   `UAsdaWarehouseSubsystem::SetupControls`, так что подписи подхватятся штатным путём.
#
# ГЕОМЕТРИЯ — снята с ЖИВОГО ОБРАЗЦА (дамп `Reports/headless_ware_dump.log`):
#   иконка `WAREBAG1_TRAY00_67_0` = off(L0 T0 R32 B32), подпись `WAREBAG1_TRAY00` = off(L0 T18 R32 B32),
#   шаг сетки 33 px, 6 столбцов × 5 строк. То есть подпись = позиция иконки + 18 по Y, размер 32×32.
#
# ★ Свойство `WidgetTree` из UE-Python недоступно (известная в проекте проблема, см. шапку
#   `asda_ui_dump_window.py`), поэтому работаем через C++-хелпер `UAsdaUmgBuilder`.
#
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_ware_bag58_cell_names.py
import traceback
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ui_step_dryrun as SD   # общий сухой режим шагов пост-импорта (07.10.2026)

# Собственный ключ сухого прогона шага (см. `asda_ui_step_dryrun.is_dry`). Также `--dry-run` /
#   ASDA_POSTSTEPS_DRYRUN=1: логика та же, запись ассетов подавлена, итог — «[ШАГ-ДЕЛЬТА] … изменено узлов N».
DRYRUN_ENV = "ASDA_BAG58_DRYRUN"

ASSET = "/Game/AsdaAssets/UI/Player_UI/Inventory/WBP_WARE_WIN"
BAGS = (5, 6, 7, 8)
COLS, ROWS = 6, 5
CELLS = COLS * ROWS
STEP = 33.0          # шаг сетки ячеек (снят с образца)
CELL = 32.0          # размер ячейки
LABEL_DY = 18.0      # смещение подписи вниз относительно иконки (снято с образца)
FONT_PX = 8          # импортёр UI ставит всему интерфейсу кегль 8
HALIGN_CENTER = 1    # ETextJustify::Center


def log(msg):
    unreal.log("[сумки5-8] %s" % msg)


def find_panel(root, name):
    """Поиск узла по имени обходом дерева от корневого канваса."""
    stack = [root]
    while stack:
        w = stack.pop()
        if not w:
            continue
        try:
            if w.get_name() == name:
                return w
        except Exception:
            pass
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                stack.append(w.get_child_at(i))
    return None


def existing_names(panel):
    out = set()
    for i in range(panel.get_children_count()):
        ch = panel.get_child_at(i)
        if ch:
            try:
                out.add(ch.get_name())
            except Exception:
                pass
    return out


def main():
    dry = SD.is_dry(DRYRUN_ENV)                                  # сухой прогон (запись подавлена)
    delta = SD.WidgetDelta("asda_ware_bag58_cell_names", dry)    # учёт правок шага по снимкам дерева
    bp = unreal.EditorAssetLibrary.load_asset(ASSET)
    if not bp:
        log("НЕ НАЙДЕН ассет %s" % ASSET)
        return
    delta.take(bp)

    B = unreal.AsdaUmgBuilder
    root = B.get_root_canvas(bp)
    if not root:
        log("не удалось получить корневой канвас")
        return

    total = 0
    for n in BAGS:
        grp_name = "WAREBAG%d_TRAYIcon_grp" % n
        grp = find_panel(root, grp_name)
        if not isinstance(grp, unreal.PanelWidget):
            log("сумка %d: группа %s не найдена — пропуск" % (n, grp_name))
            continue

        have = existing_names(grp)
        added = 0
        for c in range(CELLS):
            idc = "WAREBAG%d_TRAY%02d" % (n, c)
            if idc in have:
                continue                      # подпись уже есть — повторный запуск безопасен
            col, row = c % COLS, c // COLS
            pos = unreal.Vector2D(STEP * col, STEP * row + LABEL_DY)
            size = unreal.Vector2D(CELL, CELL)
            tb = B.add_text(bp, grp, idc, pos, size, "", FONT_PX,
                            unreal.LinearColor(1.0, 1.0, 1.0, 1.0), HALIGN_CENTER,
                            False, True)
            if not tb:
                log("сумка %d ячейка %d: AddText вернул None" % (n, c))
                continue
            # ★ Подпись мышь НЕ ловит: клик по ячейке владелец ловит по живой геометрии иконки
            #   (закон `selfhittest-vs-hittest-invisible` — иначе подпись съела бы клики).
            try:
                tb.set_editor_property("visibility", unreal.SlateVisibility.HIT_TEST_INVISIBLE)
            except Exception:
                pass
            added += 1
        total += added
        log("сумка %d: добавлено подписей %d (было %d узлов в группе)" % (n, added, len(have)))

    if total:
        # ★ ИМЯ ВРЁТ: `CompileAndSave` только компилирует блюпринт и метит пакет грязным —
        #   сохранение оставлено вызывающему (так и написано в теле функции, AsdaUmgBuilder.cpp:615).
        #   Без явного `save_loaded_asset` изменения теряются при выходе из headless-процесса.
        B.compile_and_save(bp)
        saved = SD.save(bp, dry, "asda_ware_bag58_cell_names", False)
        log("ГОТОВО: создано подписей %d, компиляция выполнена, сохранение: %s"
            % (total, ("подавлено (СУХОЙ)" if dry else "успех") if saved else "НЕ УДАЛОСЬ"))
    else:
        log("ГОТОВО:подписи не потребовались (все уже на месте)")
    delta.done(bp)
    delta.finish()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает —
#   раннер пост-импорта (`asda_ui_post_import.py`) сам зовёт `main()`.
if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[сумки5-8] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
