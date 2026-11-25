# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_title_tab_cover_z.py — ★★★ ЧЁРНАЯ МАСКА `Button_Cover` УХОДИТ ПОД ВКЛАДКИ 18713.
#
# ═══ ЧТО БЫЛО НЕ ТАК (снято кадром путём игрока, 2026-08-30) ═══
#   Вкладки фильтра званий 18713 («Обычные» / «Соревнование» / «Секретные») у владельца выглядели
#   так: у нулевой есть плашка (серая в покое, ЧЁРНАЯ В БЕЛОЙ ОБВОДКЕ при выборе), а у первой и
#   второй НЕТ НИКАКОЙ — только надпись на чёрном фоне окна. Переключение вкладку не подсвечивало.
#
#   Числа этого НЕ показывали: `GetCachedGeometry` и кисти всех трёх планов `Title_ClassSelectBtn_212_*`
#   верные и переключаются правильно (лог `Reports/headless_title_tabs_game.log`). Дефект нашёлся
#   только полным обходом ДЕТЕЙ канваса полосы: у `Title_ClassSelectBtn_grp` одиннадцать детей, а
#   узлов с префиксом `Title_ClassSelectBtn` — девять. Лишние двое:
#       [ 7] Title_ClassGathRate  (18714, подпись «Собрано %»)
#       [ 8] Button_Cover_grp     ← ЧЁРНАЯ МАСКА, off=(L108 T0 W220 H22)
#   `Button_Cover_grp` стоит в порядке рисования ПОСЛЕ планов [0..2] и кнопок [3..5], а по
#   координатам накрывает ровно вкладки 1 и 2 (108…328 при вкладках 109…217 и 218…326).
#   Подписи `_Text2`/`_Text3` — дети [9] и [10], то есть рисуются ПОСЛЕ маски и остаются видны.
#   Отсюда и картина «плоские вкладки с текстом» (закон `umg-canvas-draw-order`).
#
# ═══ ЭТО ДЕФЕКТ ОРИГИНАЛА, А НЕ ВЫДУМКА ПОРТА (доказано декомпилом) ═══
#   `gamemain_jui.ini:222496` — `Button_Cover` (скин `Black`, Origin «108 0», Size «220 18») и его
#   ребёнок `Button_Cover2` (скин `White`, Size «220 4», AlignYPos=8 — прижат к низу) объявлены
#   ДЕТЬМИ самого контрола 18713. Скины `Black`/`White` — сплошные квадраты из `N_01.tga`
#   с `PlaneAlphaBlending = "0"`, то есть НЕПРОЗРАЧНЫЕ (`RE/docs/JuiSkin.ini`).
#   Рендер эталона (`RE/etalon/ui_core.c`, `uiListWidgetRender @ 0x12ECC50`) идёт строго
#   `CControl_RenderPlanes` → текст пунктов → `CControl_RenderChildren`, то есть ДЕТИ РИСУЮТСЯ
#   ПОВЕРХ планов контрола. Кода, который прятал бы или двигал маску, в эталоне нет: у неё
#   `IDNum = 4294967295` (-1), и `CWindowMgr_FindControlByID` её не достаёт — обработчик окна
#   (`ui_core.c:81702`, `quest.c:8340`) её не касается.
#   ⇒ В ОРИГИНАЛЕ вкладки 1 и 2 тоже закрыты чёрным. Это авторская ошибка слоя: маска описывает
#   ФОН полосы (чёрный прямоугольник + белая полоска-подчёркивание внизу), а лежит поверх плашек.
#   Правим по закону `fix-original-defects`, помечая исправление.
#
# ═══ ЧТО ДЕЛАЕТ ШАГ ═══
#   Ставит слоту `Button_Cover_grp` `ZOrder = -1`. Маска НЕ удаляется и НЕ двигается — она
#   сохраняет авторскую геометрию и цвета и становится тем, чем задумана: фоном полосы.
#   Плашки вкладок и подписи рисуются поверх неё, поэтому вкладки 1 и 2 получают и серый фон
#   (кадр 0 скина `TabBar_W108_3Btn`), и чёрную плашку в белой обводке при выборе (кадр 2).
#   ⚠ Порядок задаётся именно `ZOrder`, а не перестановкой детей: `UPanelWidget::ShiftChild`
#     порядок отрисовки НЕ меняет (закон `umg-shiftchild-does-not-reorder`).
#
#   Шаг ИДЕМПОТЕНТЕН: если `ZOrder` уже -1, правок 0 и ассет не сохраняется.
#
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_ui_title_tab_cover_z.py
import traceback
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ui_step_dryrun as SD   # общий сухой режим шагов пост-импорта (07.10.2026)
# ★ дедуп №2 В1 Т12: общие помощники правки WBP (обход дерева, канвасный слот, смещения) — asda_ui_widget_common
import os as _uwc_os, sys as _uwc_sys
if _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)) not in _uwc_sys.path:
    _uwc_sys.path.insert(0, _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)))
import asda_ui_widget_common as _UWC   # noqa: E402

# Собственный ключ сухого прогона шага (см. `asda_ui_step_dryrun.is_dry`). Также `--dry-run` /
#   ASDA_POSTSTEPS_DRYRUN=1: логика та же, запись ассетов подавлена, итог — «[ШАГ-ДЕЛЬТА] … изменено узлов N».
DRYRUN_ENV = "ASDA_TITLECOVER_DRYRUN"

EAL = unreal.EditorAssetLibrary

# (ассет, имя канваса-полосы, имя узла-маски, требуемый ZOrder)
#   Список ЗАКРЫТЫЙ и состоит из одной строки: `Button_Cover` объявлен в данных РОВНО ОДИН РАЗ
#   (`gamemain_jui.ini:222496`, единственное вхождение по всему `.JUI`), и это окно званий.
#   Проверка «а нет ли ещё» делается ниже сплошным сканом всех ассетов интерфейса.
COVERS = [
    ("/Game/AsdaAssets/UI/Player_UI/Character/WBP_Title_Win",
     "Title_ClassSelectBtn_grp", "Button_Cover_grp", -1),
]

UI_ROOT = "/Game/AsdaAssets/UI"      # корень интерфейса для сплошного скана-доказательства


def log(m):
    """Единая метка в логе, чтобы шаг было видно среди прочих."""
    unreal.log("[маска-вкладок] %s" % m)


walk = _UWC.walk   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def describe(w):
    """Строка о ребёнке канваса: слот, ZOrder, видимость и кисть (если картинка)."""
    s = w.get_editor_property("slot")
    geo, z = "<не в канвасе>", "-"
    if isinstance(s, unreal.CanvasPanelSlot):
        z = str(s.get_editor_property("z_order"))
        off = s.get_editor_property("layout_data").get_editor_property("offsets")
        geo = "off=(L%.1f T%.1f W%.1f H%.1f)" % (off.left, off.top, off.right, off.bottom)
    br = ""
    if isinstance(w, unreal.Image):
        b = w.get_editor_property("brush")
        res = b.get_editor_property("resource_object")
        br = " кисть[%s тинт %s]" % (res.get_name() if res else "<нет текстуры>",
                                     b.get_editor_property("tint_color").get_editor_property("specified_color"))
    return "%-30s %-14s Z=%-3s %s vis=%s%s" % (
        w.get_name(), type(w).__name__, z, geo,
        str(w.get_editor_property("visibility")).replace("SlateVisibility.", ""), br)


def scan_all_covers():
    """★ ВТОРОЙ МЕТОД (закон `false-negatives-method`): доказать, что масок-«ковров» больше нигде
    нет — сплошным обходом ВСЕХ ассетов интерфейса, а не доверием к одному grep по `.JUI`."""
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    hits = []
    for data in ar.get_assets_by_path(UI_ROOT, recursive=True):
        path = str(data.package_name)
        if "/WBP_" not in path:
            continue
        bp = EAL.load_asset(path)
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
        if not root:
            continue
        for w in walk(root):
            if w.get_name().startswith("Button_Cover"):
                hits.append("%s :: %s" % (path, w.get_name()))
    log("сплошной скан %s: узлов Button_Cover* найдено %d" % (UI_ROOT, len(hits)))
    for h in hits:
        log("    %s" % h)


def main():
    dry = SD.is_dry(DRYRUN_ENV)                                  # сухой прогон (запись подавлена)
    delta = SD.WidgetDelta("asda_ui_title_tab_cover_z", dry)     # учёт правок шага по снимкам дерева
    changed_assets = 0
    for path, grp_name, cover_name, want_z in COVERS:
        log("=" * 78)
        log("АССЕТ %s" % path)
        if not EAL.does_asset_exist(path):
            log("  НЕТ АССЕТА — пропуск")
            continue
        bp = EAL.load_asset(path)
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
        if not root:
            log("  нет корневого канваса — пропуск")
            continue
        delta.take(bp)
        nodes = {w.get_name(): w for w in walk(root)}

        grp = nodes.get(grp_name)
        cover = nodes.get(cover_name)
        if grp is None or cover is None:
            # ★ Молчать нельзя: «узла нет» — это либо переименование импортёром, либо чужой ассет.
            log("  НЕ НАЙДЕНО: полоса=%s маска=%s" % (grp is not None, cover is not None))
            continue

        log("  ДО правки — дети полосы %s в порядке рисования:" % grp_name)
        for i in range(grp.get_children_count()):
            log("    [%2d] %s" % (i, describe(grp.get_child_at(i))))

        log("  ПОДДЕРЕВО МАСКИ %s (её сохраняем целиком, только опускаем слой):" % cover_name)
        for w in walk(cover):
            log("      %s" % describe(w))

        slot = cover.get_editor_property("slot")
        if not isinstance(slot, unreal.CanvasPanelSlot):
            log("  маска не в канвасе — ZOrder задать нечему, пропуск")
            continue
        cur_z = slot.get_editor_property("z_order")
        if int(cur_z) == int(want_z):
            log("  ZOrder уже %d — правок нет (шаг идемпотентен)" % want_z)
            continue
        slot.set_editor_property("z_order", want_z)
        log("  ★ ИСПРАВЛЕНО (дефект оригинала): ZOrder маски %s: %s → %d — маска уходит ПОД плашки вкладок"
            % (cover_name, cur_z, want_z))
        SD.save(bp, dry, "asda_ui_title_tab_cover_z")
        delta.done(bp)
        changed_assets += 1

        log("  ПОСЛЕ правки — дети полосы %s:" % grp_name)
        for i in range(grp.get_children_count()):
            log("    [%2d] %s" % (i, describe(grp.get_child_at(i))))

    log("ассетов изменено: %d%s" % (changed_assets, " (СУХОЙ)" if dry else ""))
    delta.finish()
    scan_all_covers()


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[маска-вкладок] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
