# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_text_color_tweaks.py — ★★★ РУЧНЫЕ ЦВЕТА ПОДПИСЕЙ, КОТОРЫХ НЕТ В ДАННЫХ .JUI.
#
#   ЗАЧЕМ (обнаружено полным переимпортом 2026-08-22). Механическая сверка снимков «до/после»
#   поймала РОВНО ДВА расхождения цвета на 7196 текстов. Оба — в тех двух окнах, которые были
#   правлены после последней полной перегенерации (мтайм ассетов 2026-08-09, инвентарь и
#   «хвастовство предметом»), и оба переимпорт вернул к значению ИЗ ДАННЫХ .JUI. То есть это
#   ручные правки владельца в редакторе, которых ни в одном скрипте нет, — ровно тот класс потерь,
#   ради которого заведён `asda_ui_handmade.py` (он сторожит СЛОТЫ, а не цвета).
#
#   ★ ЧТО ИМЕННО И ПОЧЕМУ ОТЛИЧАЕТСЯ ОТ ДАННЫХ (владельцу — на решение, см. отчёт
#     `Reports/jui_full_reimport.md` §4):
#     • `WBP_INVEN_WIN / Inven_auto_array_Text` — подпись «A» кнопки автосортировки (80400).
#       В `gamemain_jui.ini:217504` у контрола `CaptionInfo = "0 0 ff8000ff 5"` — ЯРКО-ФИОЛЕТОВЫЙ,
#       и это ЕДИНСТВЕННОЕ вхождение `ff8000ff` во всех четырёх .JUI. Сам контрол в оригинале
#       `Visible = "0"` и без обработчика (`inventory_port.md` §«80400»), то есть кнопка в игре
#       недостижима, а её цвет — рабочий мусор дизайнера. Автосортировку мы включили СВЕРХ эталона
#       (решение владельца Д1, `inventory_autosort_fix.md`), поэтому и цвет подписи — наш.
#     • `WBP_Opend_Item_Win / Opend_Item_Win_Text` — заголовок окна «кто чем похвастался» (28100).
#       В данных `CaptionInfo = "5 4 ff2d2d2d 0"` (тёмно-серый) поверх скина `Box_BlackOt40_2`
#       (чёрная подложка 40 %) — тёмное по тёмному. Правка = белый.
#
#   ЗАПУСК: автоматически из `asda_ui_post_import.py` (шаг в `STEPS`), либо вручную:
#     Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_ui_text_color_tweaks.py
#   ⚠ Правку в редакторе НЕ забывай переносить сюда: перегенерация окна её сотрёт.
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ui_step_dryrun as SD   # общий сухой режим шагов пост-импорта (07.10.2026)

# Собственный ключ сухого прогона шага (см. `asda_ui_step_dryrun.is_dry`). Также `--dry-run` /
#   ASDA_POSTSTEPS_DRYRUN=1: логика та же, запись ассетов подавлена, итог — «[ШАГ-ДЕЛЬТА] … изменено узлов N».
DRYRUN_ENV = "ASDA_TEXTCOLOR_DRYRUN"

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder

# (путь WBP, имя TextBlock, цвет sRGB AARRGGBB, что в данных .JUI, обоснование)
TWEAKS = [
    ("/Game/AsdaAssets/UI/Player_UI/Inventory/WBP_INVEN_WIN", "Inven_auto_array_Text",
     "FFFFFDFF", "ff8000ff",
     "подпись «A» кнопки автосортировки: контрол 80400 в оригинале Visible=0 и без обработчика, "
     "фиолетовый ff8000ff — единственное вхождение во всех .JUI (рабочий мусор дизайнера); "
     "саму кнопку включили сверх эталона решением владельца Д1"),
    ("/Game/AsdaAssets/UI/Player_UI/Inventory/WBP_Opend_Item_Win", "Opend_Item_Win_Text",
     "FFFFFFFF", "ff2d2d2d",
     "заголовок окна 28100 поверх скина Box_BlackOt40_2 (чёрная подложка 40 %): "
     "данные дают тёмно-серый по тёмному"),
]

# ★★★ РУЧНЫЕ ТЕКСТЫ ПОДПИСЕЙ (тот же класс правок, что цвета выше, — поэтому здесь, а не в новом
#   механизме). (путь WBP, имя TextBlock, текст, что даёт словарь перевода, обоснование)
TEXT_TWEAKS = [
    # ★ ПУСТО. Запись про `EXCH_SELDEL_Text` («Отменить продажу») снята 2026-08-28: выяснилось,
    #   что оригинал ставит эту подпись В РАНТАЙМЕ из строки локализации **12506**
    #   (`invForgeDialogInit@0xD450B0` → `Collection_AddString(pForgeDlg[60], loc 12506)`), а не
    #   переводом корейской заготовки `.JUI`. Источник — таблица строк, правка в ассете не нужна.
]


def _srgb_to_linear(c):
    """sRGB [0..1] -> LINEAR [0..1] — закон `ui-srgb-gamma-rule` (UE трактует цвет как линейный)."""
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def _hex_to_linear(h):
    a = int(h[0:2], 16) / 255.0
    r = _srgb_to_linear(int(h[2:4], 16) / 255.0)
    g = _srgb_to_linear(int(h[4:6], 16) / 255.0)
    b = _srgb_to_linear(int(h[6:8], 16) / 255.0)
    return unreal.LinearColor(r, g, b, a)


def _find_text(bp, name):
    """TextBlock по имени. ⚠ `WidgetBlueprint.widget_tree` из Python не заэкспорчен — обход идёт
    от корневого канваса, ровно как в `asda_ui_post_import._widget_names`."""
    root = B.get_root_canvas(bp)
    found = [None]

    def walk(w):
        if found[0] is not None or w is None:
            return
        if w.get_name() == name and isinstance(w, unreal.TextBlock):
            found[0] = w
            return
        try:
            kids = w.get_all_children()
        except Exception:
            return
        for c in kids:
            walk(c)

    walk(root)
    return found[0]


def main():
    """★ ИДЕМПОТЕНТЕН. `ApplyJuiTextColors` возвращает число ИЗМЕНЁННЫХ узлов, и на втором прогоне
    это ноль — падать по нему нельзя. Проверяем НАЛИЧИЕ узла и ИТОГОВЫЙ цвет, а не факт правки."""
    dry = SD.is_dry(DRYRUN_ENV)                                  # сухой прогон (запись подавлена)
    delta = SD.WidgetDelta("asda_ui_text_color_tweaks", dry)     # учёт правок шага по снимкам дерева
    done = 0
    for path, name, hexcol, jui, why in TWEAKS:
        if not EAL.does_asset_exist(path):
            unreal.log_error("[цвета-подписей] нет ассета %s" % path)
            raise RuntimeError("asset %s not found" % path)
        bp = EAL.load_asset(path)
        delta.take(bp)
        if _find_text(bp, name) is None:
            unreal.log_error("[цвета-подписей] %s / %s — узел НЕ НАЙДЕН" % (path, name))
            raise RuntimeError("text node %s missing in %s" % (name, path))
        want = _hex_to_linear(hexcol)
        names = unreal.Array(str)
        names.append(name)
        cols = unreal.Array(unreal.LinearColor)
        cols.append(want)
        # bOnlyIfPlaceholder = False: красим безусловно (значение из .JUI — не плейсхолдер `ff400040`).
        n = B.apply_jui_text_colors(bp, names, cols, False, unreal.LinearColor(0, 0, 0, 0))
        if n > 0:
            # ⚠ `CompileAndSave` вопреки имени НЕ сохраняет пакет (см. AsdaUmgBuilder.cpp:610-615:
            #   компиляция + MarkPackageDirty, а запись — за Python). Без `save_loaded_asset`
            #   правка живёт только в памяти прогона и пропадает — так первый запуск этого шага
            #   отрапортовал «применено», а мтайм ассетов не изменился.
            B.compile_and_save(bp)
            SD.save(bp, dry, "asda_ui_text_color_tweaks")
        delta.done(bp)
        # Сверка ПОСЛЕ: цвет обязан стать заданным (иначе правка молча не доехала).
        cur = _find_text(bp, name).get_editor_property("color_and_opacity").specified_color
        if abs(cur.r - want.r) > 2e-3 or abs(cur.g - want.g) > 2e-3 or abs(cur.b - want.b) > 2e-3:
            raise RuntimeError("цвет %s / %s НЕ применён" % (path, name))
        done += 1
        unreal.log("[цвета-подписей] %s / %s = %s (в .JUI %s), правок в ассете %d. Основание: %s"
                   % (path.rsplit("/", 1)[-1], name, hexcol, jui, n, why))
    unreal.log("=== РУЧНЫЕ ЦВЕТА ПОДПИСЕЙ: подтверждено %d/%d ===" % (done, len(TWEAKS)))
    if done != len(TWEAKS):
        raise RuntimeError("применено %d из %d" % (done, len(TWEAKS)))

    # ── РУЧНЫЕ ТЕКСТЫ (идемпотентно: правим, только если текст ОТЛИЧАЕТСЯ от нужного) ──
    tdone = 0
    for path, name, want, was, why in TEXT_TWEAKS:
        if not EAL.does_asset_exist(path):
            unreal.log_error("[тексты-подписей] нет ассета %s" % path)
            raise RuntimeError("asset %s not found" % path)
        bp = EAL.load_asset(path)
        delta.take(bp)
        node = _find_text(bp, name)
        if node is None:
            unreal.log_error("[тексты-подписей] %s / %s — узел НЕ НАЙДЕН" % (path, name))
            raise RuntimeError("text node %s missing in %s" % (name, path))
        cur = str(node.get_editor_property("text"))
        if cur != want:
            try:
                node.set_editor_property("text", unreal.Text(want))
            except Exception:                                    # noqa: BLE001
                node.set_editor_property("text", want)
            B.compile_and_save(bp)
            if not SD.save(bp, dry, "asda_ui_text_color_tweaks"):
                raise RuntimeError("не сохранён %s" % path)
        delta.done(bp)
        if str(_find_text(bp, name).get_editor_property("text")) != want:
            raise RuntimeError("текст %s / %s НЕ применён" % (path, name))
        tdone += 1
        unreal.log("[тексты-подписей] %s / %s = %r (было %r). Основание: %s"
                   % (path.rsplit("/", 1)[-1], name, want, was, why))
    unreal.log("=== РУЧНЫЕ ТЕКСТЫ ПОДПИСЕЙ: подтверждено %d/%d ===" % (tdone, len(TEXT_TWEAKS)))
    delta.finish()
    if tdone != len(TEXT_TWEAKS):
        raise RuntimeError("применено %d из %d" % (tdone, len(TEXT_TWEAKS)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    main()
