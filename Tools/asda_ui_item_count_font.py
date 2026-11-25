# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_item_count_font.py — ★★ [ИСПРАВЛЕНИЕ] КЕГЛЬ ЧИСЛА КОЛИЧЕСТВА НА 10% КРУПНЕЕ В ОКНАХ С ЯЧЕЙКОЙ СУМКИ (08.11.2026, пачка 18, баг 6).
#
# РЕШЕНИЕ ВЛАДЕЛЬЦА 08.11.2026: «числа количества на иконках мелкие» — поднять кегль ВСЕХ полей количества на иконках в сумке
#   на 10% и сделать то же в окнах, где та же ячейка. Снимок: `Reports/batch18/screens/quiver_count.png`.
#   Эталон кегль не хранит (шрифт UI глобальный) — это отступление по слову владельца, поэтому [ИСПРАВЛЕНИЕ].
# ГДЕ ЗАДАН КЕГЛЬ.
#   • Сумка `INVEN_SET*` и надетые слоты окна сумки `EQUIP_SET*` — КОД (`UAsdaInventorySubsystem::ApplyStackTextStyle`, база 7):
#     прибавку даёт общая точка `AsdaItemCount::FontSize`, ключ `asda.UI.ItemCountFontScale` (110; 100 — прежний вид).
#   • Окна, где та же ячейка предмета и кегль числа задан АССЕТОМ (база 8) — этот шаг: склад 4000 `WBP_WARE_WIN`, обмен 2008
#     `WBP_TRAN_WIN`, лавка продавца 22000 `WBP_StreetStall_Open_Win`, лавка покупателя 23000 `WBP_StreetStall_Win`, магазин NPC
#     `WBP_NPCSHOP_WIN`. Узлы — те же, что у шага обводки (`asda_ui_item_count_outline.TARGETS`, одна таблица на оба шага).
#
# ЧТО ДЕЛАЕТ (ТОЧЕЧНО, идемпотентно). Узлу-числу с кеглем ровно BASE ставит кегль BASE × PCT/100 (8 → 8,8). Больше ничего:
#   гарнитура, обводка, цвет, тень, слот, выравнивание, сдвиг — как были. Уже 8,8 — пропуск; иной кегль — «осмотреть»,
#   узел не трогается (ручная правка владельца). Сторож числа узлов окна; бэкап до записи `_backup/<дата>_b18_item_count/…`.
#
# ЗАПУСК (редактор закрыт, соседнего -game нет):
#   python Tools/run_headless_safe.py Tools/asda_ui_item_count_font.py --marker "ЗАВЕРШЕНО: кегль числа"
#   ASDA_ITEMCOUNT_FONT_DRYRUN=1 — сухой прогон; общий ASDA_POSTSTEPS_DRYRUN — тоже.
#   ASDA_ITEMCOUNT_FONT_PCT=100 — ОТКАТ: узлам с кеглем BASE×110% вернуть BASE (тот же шаг в обратную сторону).
import os
import re
import sys

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import asda_ui_widget_common as _UWC   # noqa: E402  обход дерева WBP
import asda_headless_tool as HT        # noqa: E402  единая резервная копия
import asda_ui_step_dryrun as DR       # noqa: E402  сухой режим цепочки и запись
import asda_ui_item_count_outline as OUT   # noqa: E402  общая таблица узлов-чисел (TARGETS)

EAL = unreal.EditorAssetLibrary
DRYRUN_ENV = "ASDA_ITEMCOUNT_FONT_DRYRUN"
CONTENT_DIR = os.path.normpath(os.path.join(HERE, "..", "Content"))
MARKER = "ЗАВЕРШЕНО: кегль числа"
BACKUP_SET = "b18_item_count"
BASE = 8.0          # кегль числа в ассетах этих окон до правки (замер 08.11.2026: у всех узлов 8,0)
PCT_FIX = 110       # решение владельца: +10%
EPS = 0.01
# Окна с той же ячейкой предмета, что в сумке (ключи — пути из `asda_ui_item_count_outline.TARGETS`).
WINDOWS = (
    "Inventory/WBP_WARE_WIN",
    "Trade/WBP_TRAN_WIN",
    "Trade/WBP_StreetStall_Open_Win",
    "Trade/WBP_StreetStall_Win",
    "Trade/WBP_NPCSHOP_WIN",
)


def log(m):
    unreal.log("[кегль-числа] %s" % m)


def _size(w):
    return float(w.get_editor_property("font").get_editor_property("size"))


def _asset_file(path):
    return os.path.join(CONTENT_DIR, path[len("/Game/"):].replace("/", os.sep) + ".uasset")


def main():
    dry = DR.is_dry(DRYRUN_ENV)
    pct = int(os.environ.get("ASDA_ITEMCOUNT_FONT_PCT", str(PCT_FIX)))
    fixed = round(BASE * PCT_FIX / 100.0, 2)
    want = round(BASE * pct / 100.0, 2)
    src = BASE if pct != 100 else fixed      # откат: с 8,8 на 8
    log("кегль %.2f → %.2f%s" % (src, want, "  [СУХОЙ ПРОГОН]" if dry else ""))
    B = unreal.AsdaUmgBuilder
    plan, problems = [], []
    rules = {p: (rx, n) for p, rx, n, _why in OUT.TARGETS}
    for p in WINDOWS:
        if p not in rules:
            problems.append("окна %s нет в таблице asda_ui_item_count_outline.TARGETS" % p)
            continue
        rx, n_want = rules[p]
        path = OUT.UI + p
        bp = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
        root = B.get_root_canvas(bp) if bp else None
        if not root:
            problems.append("нет ассета/канваса %s" % path)
            continue
        r = re.compile(rx)
        nodes = [x for x in _UWC.walk(root) if isinstance(x, unreal.TextBlock) and r.match(x.get_name())]
        if n_want is not None and len(nodes) != n_want:
            problems.append("%s: узлов %d, ждали %d" % (p, len(nodes), n_want))
            continue
        plan.append((path, p, bp, nodes))
    if problems:
        for m in problems:
            unreal.log_error("[кегль-числа] ОТКАЗ: %s" % m)
        raise RuntimeError("сторож шага: %d проблем, ничего не записано" % len(problems))
    total = look_total = 0
    for path, p, bp, nodes in plan:
        fix = [x for x in nodes if abs(_size(x) - src) <= EPS]
        done = [x for x in nodes if abs(_size(x) - want) <= EPS]
        look = [x for x in nodes if x not in fix and x not in done]
        for x in look:
            log("  ОСМОТРЕТЬ %s / %s: кегль %.2f (не %.2f и не %.2f) — не трогаю" % (p, x.get_name(), _size(x), src, want))
        log("окно %s: узлов %d, правка %d, уже %d, осмотреть %d" % (p, len(nodes), len(fix), len(done), len(look)))
        total += len(fix)
        look_total += len(look)
        if not fix or dry:
            continue
        if HT.backup_file(_asset_file(path), BACKUP_SET, log=log) is None:
            raise RuntimeError("нет файла ассета %s — бэкап невозможен, запись запрещена" % path)
        for x in fix:
            f = x.get_editor_property("font")
            f.set_editor_property("size", want)
            x.set_editor_property("font", f)
            if abs(_size(x) - want) > EPS:
                raise RuntimeError("%s/%s: кегль не встал — запись отменена" % (p, x.get_name()))
        B.compile_and_save(bp)   # ★ только компиляция (имя врёт) — пишем сами
        if not DR.save(bp, dry, "кегль-числа", False):
            raise RuntimeError("не сохранён ассет %s" % path)
        unreal.log("ASDA_WRITES_ASSET: %s" % path)
    log("итого: правка %d, осмотреть %d%s" % (total, look_total, "  [СУХОЙ — без записи]" if dry else ""))
    log(MARKER)


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    main()
