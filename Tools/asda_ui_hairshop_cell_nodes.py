# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_hairshop_cell_nodes.py — ★★★ НЕДОСТАЮЩИЕ ПОДПИСИ ИМЕНИ ПРИЧЁСКИ В АССЕТЕ
#   (`WBP_HAIRSHOP_WIN`, окно `HAIRSHOP_WIN` 15000).
#
# ═══ ПРОБЛЕМА (тот же класс, что у ячеек магазина бустеров) ═══════════════════════════════════
# У контрола `HAIR_ITEM_TITLE01` (IDNum 15006) в `gamemain_jui.ini` есть `CaptionText`
# (дизайнерская «рыба»), поэтому импортёр создал ему узел `HAIR_ITEM_TITLE01_Text`.
# У карточек 02…14 `CaptionText` ПУСТ ⇒ подписи нет вовсе. Живой дамп ассета
# (`Reports/_hairshop_wbp_tree.txt`): 14 планов `HAIR_ITEM_TITLE{nn}_<n>_0`
# и РОВНО ОДИН `HAIR_ITEM_TITLE01_Text`.
#
# А эталон пишет имя причёски в КАЖДУЮ карточку:
#   `CControl_SetText(cell.pTitle, rec + 2)` — `refineFillMaterialSlotInfo@0xCF0660`
#   (`RE/etalon/item_enhance.c:1346`), привязка `pTitle` — `CUIHairShop_InitControls@0xCEC1B0`
#   (цикл `while (hairIdx < 14)`, `ui_core.c:45075-45097`, указатель `this + 9*i + 7`).
# ⇒ без этого шага 13 из 14 карточек показывали бы причёску БЕЗ ИМЕНИ (самотест 2026-08-28:
#   «карточек полностью размечено 1 из 14»).
#
# ═══ ЧТО ДЕЛАЕТ ШАГ (идемпотентно: второй прогон = 0 правок) ══════════════════════════════════
#   создаёт `HAIR_ITEM_TITLE{nn}_Text` (nn = 02…14) в канвасе карточки `HAIR_ITEM{nn}_grp`
#   ГЕОМЕТРИЕЙ И СТИЛЕМ узла-образца `HAIR_ITEM_TITLE01_Text` (шрифт, цвет, выравнивание,
#   отсутствие переноса) и приводит геометрию, если она разъехалась.
#   Ничего не подбирается: это ТОТ ЖЕ контрол, только другой карточки.
#
# ★ Цвет подписи в данных — `CaptionInfo` контрола 15006 (`FFFF80FF`, розовый). Это НЕ «фиолетовый
#   баг» (закон `ui-text-color-exs`), а авторские данные, поэтому берём цвет у образца КАК ЕСТЬ.
#
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_ui_hairshop_cell_nodes.py
#   Сухой прогон: set ASDA_HAIRCELL_DRYRUN=1
import os
import sys
import traceback
import unreal
# ★ дедуп №2 В1 Т12: общие помощники правки WBP (обход дерева, канвасный слот, смещения) — asda_ui_widget_common
import os as _uwc_os, sys as _uwc_sys
if _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)) not in _uwc_sys.path:
    _uwc_sys.path.insert(0, _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)))
import asda_ui_widget_common as _UWC   # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

EAL = unreal.EditorAssetLibrary
EPS = 0.01

ASSET = "/Game/AsdaAssets/UI/Player_UI/Trade/WBP_HAIRSHOP_WIN"
CARDS = 14          # HAIR_ITEM01…14, IDNum 15005 + 9i


def log(m):
    unreal.log("[карточки парикмахерской] %s" % m)


walk = _UWC.walk   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


canvas_slot = _UWC.canvas_slot   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def slot_offsets(w):
    s = canvas_slot(w)
    if s is None:
        return None
    return s.get_editor_property("layout_data").get_editor_property("offsets")


set_offsets = _UWC.set_offsets   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def same_margin(m, want):
    return (abs(m.left - want[0]) < EPS and abs(m.top - want[1]) < EPS
            and abs(m.right - want[2]) < EPS and abs(m.bottom - want[3]) < EPS)


def margin_tuple(m):
    return (float(m.left), float(m.top), float(m.right), float(m.bottom))


def find_plane(nodes, prefix):
    """План импортёра `<prefix>_<juiIdx>_0`: индекс меняется при переимпорте — ищем по префиксу."""
    best = None
    for n, w in nodes.items():
        if n.startswith(prefix) and n.endswith("_0") and isinstance(w, unreal.Image):
            if best is None or len(n) < len(best[0]):
                best = (n, w)
    return best[1] if best else None


def main():
    dry = bool(os.environ.get("ASDA_HAIRCELL_DRYRUN"))
    B = unreal.AsdaUmgBuilder
    if not EAL.does_asset_exist(ASSET):
        log("НЕТ АССЕТА %s" % ASSET)
        return
    bp = EAL.load_asset(ASSET)
    root = B.get_root_canvas(bp)
    if not root:
        log("!! нет корневого канваса у %s" % ASSET)
        return

    nodes = {}
    for w in walk(root):
        nodes.setdefault(w.get_name(), w)

    title01 = nodes.get("HAIR_ITEM_TITLE01_Text")
    if title01 is None:
        log("!! нет образца HAIR_ITEM_TITLE01_Text — подписи достроить не по чему")
        return
    ref_off = slot_offsets(title01)
    if ref_off is None:
        log("!! у образца HAIR_ITEM_TITLE01_Text нет слота канваса — пропуск")
        return
    want = margin_tuple(ref_off)

    added = fixed = 0
    for i in range(2, CARDS + 1):
        nn = "%02d" % i
        grp = nodes.get("HAIR_ITEM%s_grp" % nn)
        if not isinstance(grp, unreal.PanelWidget):
            log("!! нет канваса HAIR_ITEM%s_grp — карточка пропущена" % nn)
            continue
        name = "HAIR_ITEM_TITLE%s_Text" % nn
        tb = nodes.get(name)
        if tb is None:
            if not dry:
                # AddText(BP, Parent, Name, Pos, Size, Text, FontSize, Color, HAlign, bWrap, bBold):
                #   HAlign 4 = по центру (как у образца), bWrap = False (закон `ui-autowrap-canvas-gotcha`).
                tb = B.add_text(bp, grp, name, unreal.Vector2D(want[0], want[1]),
                                unreal.Vector2D(want[2], want[3]), "", 0,
                                unreal.LinearColor(1.0, 1.0, 1.0, 1.0), 4, False, True)
                if tb:
                    tb.set_editor_property("font", title01.get_editor_property("font"))
                    tb.set_editor_property("color_and_opacity",
                                           title01.get_editor_property("color_and_opacity"))
                    tb.set_editor_property("justification",
                                           title01.get_editor_property("justification"))
                    tb.set_editor_property("auto_wrap_text", False)
                    tb.set_text("")
                    nodes[name] = tb
            added += 1
        else:
            off = slot_offsets(tb)
            if off is not None and not same_margin(off, want):
                if not dry:
                    set_offsets(tb, want)
                fixed += 1

    if not dry and (added or fixed):
        # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
        B.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не удалось сохранить %s" % ASSET)
    log("готово: подписей создано %d, геометрий поправлено %d%s" %
        (added, fixed, " (СУХОЙ ПРОГОН)" if dry else ""))


if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[карточки парикмахерской] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
