# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_caramake_topwin_z.py — ТОЧЕЧНАЯ правка АССЕТА окна создания персонажа `WBP_CARAMAKE_WIN` (06.11.2026).
#
# ЗАЧЕМ. Список созвездий висит внутри ветки `CARAMAKE_TOPWIN_grp` → `tt2_grp` → `st_grp`. Его Z=60 действует только внутри этой
#   ветки, а сама ветка в корне лежит с Z=0. Клик-зоны сетки одежды (`CARAMAKE_ICONBOX_HITnn`) лежат в корне с Z=50, поэтому они
#   выше списка: пункты ниже «Девы» попадали в сетку, список оставался открытым, «Создать» недоступна (дамп и прогон
#   `Reports/caramake_1106/base_ru`). В оригинале выпадающий список рисуется и ловит клики поверх остальных контролов.
#
# ЧТО ДЕЛАЕМ. Одно значение: ZOrder слота `CARAMAKE_TOPWIN_grp` в корне 0 → 60 (выше зон сетки). Больше ничего не меняется.
#   Разрешение владельца 06.11.2026: только это окно и только эта задача. Уровень Login не загружается и не сохраняется.
#
# ИДЕМПОТЕНТНОСТЬ. Значение абсолютное; допустимы только ИСХОДНОЕ и ЦЕЛЕВОЕ. Третье значение = чужая правка: ничего не пишем.
#
# Запуск: python Tools/run_headless_safe.py Tools/asda_fix_caramake_topwin_z.py --marker "CARAMAKE_TOPWIN_Z_DONE" --require "CARAMAKE_TOPWIN_Z_DONE"
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # Tools: запуск скриптом, без раннера пост-импорта
import asda_ui_step_dryrun as SD   # общий сухой режим шагов пост-импорта

WBP = "/Game/AsdaAssets/UI/Login/WBP_CARAMAKE_WIN"   # окно создания персонажа
NODE = "CARAMAKE_TOPWIN_grp"                         # верхний блок окна (имя, пол, внешность, созвездие)
Z_SRC = 0                                            # значение импортёра
Z_DST = 60                                           # выше клик-зон сетки (Z=50 в UAsdaIconCellGrid)
STEP = "asda_fix_caramake_topwin_z"                  # имя шага
DRYRUN_ENV = "ASDA_CARAMAKE_TOPWIN_Z_DRYRUN"         # собственный ключ сухого прогона


def log(m):
    """Строка журнала шага."""
    unreal.log("[caramake-z] " + str(m))


def find_child(root, name):
    """Прямой потомок корня с именем name (ветка лежит в корне окна)."""
    for i in range(root.get_children_count()):
        c = root.get_child_at(i)
        if c and c.get_name() == name:
            return c
    return None


def main():
    """Поставить ZOrder ветки; при неожиданном значении ничего не писать."""
    dry = SD.is_dry(DRYRUN_ENV)
    delta = SD.WidgetDelta(STEP, dry)
    bp = unreal.EditorAssetLibrary.load_asset(WBP)
    if not bp:
        log("!! НЕ загружен %s" % WBP)
        return
    delta.take(bp)
    B = unreal.AsdaUmgBuilder
    root = B.get_root_canvas(bp)
    node = find_child(root, NODE) if root else None
    if not node or not isinstance(node.slot, unreal.CanvasPanelSlot):
        log("!! нет узла %s в корне" % NODE)
        return
    cur = node.slot.get_editor_property("z_order")
    if cur == Z_DST:
        log("правка уже применена (Z=%d), ассет не трогаю" % cur)
    elif cur != Z_SRC:
        log("!! НЕОЖИДАННОЕ Z=%d (ни %d, ни %d): чужая правка, ничего не пишу" % (cur, Z_SRC, Z_DST))
        return
    else:
        node.slot.set_editor_property("z_order", Z_DST)
        B.compile_and_save(bp)
        if not SD.save(bp, dry, STEP, False):
            log("!! НЕ сохранён %s" % WBP)
            return
        log("Z %s: %d → %d" % (NODE, Z_SRC, Z_DST))
    delta.done(bp)
    delta.finish()
    log("CARAMAKE_TOPWIN_Z_DONE")


# ★ Закон tools-no-import-side-effects: при импорте main() не запускается, раннер пост-импорта зовёт его сам.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
