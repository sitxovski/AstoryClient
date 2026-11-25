# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_booster_cell_nodes.py — ★★★ НЕДОСТАЮЩИЕ УЗЛЫ ЯЧЕЕК МАГАЗИНА БУСТЕРОВ В АССЕТЕ
#   (`WBP_BOOSTER_SHOP_WIN`, окно `BOOSTER_SHOP_WIN` 15300).
#
# ═══ ПРОБЛЕМА 1 — ПОДПИСЬ ИМЕНИ ТОВАРА ЕСТЬ ТОЛЬКО У ЯЧЕЙКИ 00 ════════════════════════════════
# У контрола `BOOSTER_ITEM_TITLE00` (IDNum 15308) в `gamemain_jui.ini` есть `CaptionText`
# («아이템이름» — дизайнерская рыба), поэтому импортёр создал ему узел `…_TITLE00_Text`.
# У ячеек 01…20 `CaptionText` ПУСТ ⇒ подписи нет вовсе (дамп ассета: 21 план `…_TITLE{nn}_<n>_0`
# и РОВНО ОДИН `…_TITLE00_Text`).
# А эталон пишет имя в КАЖДУЮ ячейку: `CWidget_SetTextAll4(cell.pName, itemName, 0)`
# (`itemGridRefreshPage@0xB81460`, `AsdaStory_ida.c:421052`; привязка `pName` — `:420571-420574`).
# ⇒ без этого шага у 20 из 21 ячейки товар был бы БЕЗ ИМЕНИ.
#
# ═══ ПРОБЛЕМА 2 — ПЛАШКА СКИДКИ СУЩЕСТВУЕТ ТОЛЬКО У ЯЧЕЙКИ 00 ════════════════════════════════
# В `.JUI` шаблон плашки лежит ВНУТРИ ячейки 00: `BOOSTER_Stamp_Base` (15560) → `BOOSTER_Num_Base`
# (15570) → девять цифр `BOOSTER_Stamp1…9` (15561…15569). Эталон в Init КЛОНИРУЕТ их в каждую из
# 21 ячейки: `CControl_InstantiateChild(pPremiumPage, 1, cell.pSlot, 0)` и далее по цифрам
# (`BoosterShop_InitWindow`, `AsdaStory_ida.c:420606-420609`). Импортёр клонирования не делает.
# ⇒ шаг КЛОНИРУЕТ поддерево плашки в ячейки 01…20 — ровно как эталон, той же геометрией и теми же
#   кистями (кисть копируется из образца целиком, ничего не подбирается).
#
# ★ В ДАННЫХ RU-СБОРКИ у всех 72 записей `Bpremium.BIN` процент скидки = 0, то есть плашка сейчас
#   не показывается НИКОГДА. Механизм всё равно достраивается: данные меняются патчем сервера/
#   клиента, а «сделать половину» запрещено (закон `no-scope-cutting`).
#
# ═══ ЧТО ДЕЛАЕТ ШАГ (идемпотентно: второй прогон = 0 правок) ══════════════════════════════════
#   1) создаёт `BOOSTER_ITEM_TITLE{nn}_Text` (nn = 01…20) в канвасе ячейки, геометрией плана
#      `BOOSTER_ITEM_TITLE{nn}_<n>_0` и стилем узла-образца `BOOSTER_ITEM_TITLE00_Text`;
#   2) клонирует в ячейки 01…20 плашку скидки: канвас `BOOSTER_Stamp_Base_c{nn}`, картинку
#      `BOOSTER_Stamp_Base_c{nn}_0`, канвас цифр `BOOSTER_Num_Base_c{nn}` и девять картинок
#      `BOOSTER_Stamp{d}_c{nn}` (d = 1…9) — имена подобраны так, чтобы рантайм находил их теми же
#      правилами, что и у ячейки 00 (префикс `BOOSTER_Stamp_Base` и шаблон `BOOSTER_Stamp<цифра>`).
#   Геометрия и кисти берутся ИЗ ЯЧЕЙКИ 00 (образец), ничего не выдумывается.
#
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_ui_booster_cell_nodes.py
#   Сухой прогон: set ASDA_BOOSTERCELL_DRYRUN=1
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

ASSET = "/Game/AsdaAssets/UI/Player_UI/Trade/WBP_BOOSTER_SHOP_WIN"
CELLS = 21          # 21 ячейка сетки (3 × 7)
DIGITS = 9          # 15561…15569


def log(m):
    unreal.log("[ячейки бустер-шопа] %s" % m)


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


def find_in(nodes, name):
    return nodes.get(name)


def find_plane(nodes, prefix):
    """План импортёра `<prefix>_<juiIdx>_0`: индекс меняется при переимпорте — ищем по префиксу."""
    best = None
    for n, w in nodes.items():
        if n.startswith(prefix) and n.endswith("_0") and isinstance(w, unreal.Image):
            if best is None or len(n) < len(best[0]):
                best = (n, w)
    return best[1] if best else None


def main():
    dry = bool(os.environ.get("ASDA_BOOSTERCELL_DRYRUN"))
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

    # ── Образцы из ячейки 00 ────────────────────────────────────────────────────────────────────
    title00 = find_in(nodes, "BOOSTER_ITEM_TITLE00_Text")
    stamp00 = find_in(nodes, "BOOSTER_Stamp_Base_grp")
    num00 = find_in(nodes, "BOOSTER_Num_Base_grp")
    if title00 is None:
        log("!! нет образца BOOSTER_ITEM_TITLE00_Text — подписи достроить не по чему")
    if stamp00 is None or num00 is None:
        log("!! нет образца плашки скидки (BOOSTER_Stamp_Base_grp / BOOSTER_Num_Base_grp)")

    stamp00_img = find_plane(nodes, "BOOSTER_Stamp_Base_")
    digit00 = []
    for d in range(1, DIGITS + 1):
        digit00.append(find_plane(nodes, "BOOSTER_Stamp%d_" % d))

    added = fixed = 0

    for i in range(1, CELLS):
        nn = "%02d" % i
        grp = find_in(nodes, "BOOSTER_ITEM%s_grp" % nn)
        if not isinstance(grp, unreal.PanelWidget):
            log("!! нет канваса BOOSTER_ITEM%s_grp — ячейка пропущена" % nn)
            continue

        # ── 1. Подпись имени товара ────────────────────────────────────────────────────────────
        if title00 is not None:
            name = "BOOSTER_ITEM_TITLE%s_Text" % nn
            plane = find_plane(nodes, "BOOSTER_ITEM_TITLE%s_" % nn)
            # ★ Геометрию берём У ОБРАЗЦА ячейки 00 (`_TITLE00_Text`), а не у плана:
            #   у всех ячеек канвас устроен одинаково, а импортёр ставит подпись по `CaptionPoint`
            #   скина, а не по углу плана (разница была видна в самотесте: 4 px по Y).
            ref_off = slot_offsets(title00) or slot_offsets(plane)
            if ref_off is None:
                log("  ячейка %s: нет геометрии плана подписи — пропуск" % nn)
            else:
                want = margin_tuple(ref_off)
                tb = find_in(nodes, name)
                if tb is None:
                    if not dry:
                        # Стиль (шрифт, цвет, выравнивание) копируем у образца ячейки 00 —
                        #   ничего не подбирая: это ТОТ ЖЕ контрол, только другой ячейки.
                        #   AddText(BP, Parent, Name, Pos, Size, Text, FontSize, Color, HAlign,
                        #           bWrap, bBold): HAlign 4 = по центру (как у `_TITLE00_Text`),
                        #   bWrap = False (закон `ui-autowrap-canvas-gotcha`).
                        tb = B.add_text(bp, grp, name, unreal.Vector2D(want[0], want[1]),
                                        unreal.Vector2D(want[2], want[3]), "", 0,
                                        unreal.LinearColor(1.0, 1.0, 1.0, 1.0), 4, False, True)
                        if tb:
                            tb.set_editor_property("font", title00.get_editor_property("font"))
                            tb.set_editor_property("color_and_opacity",
                                                   title00.get_editor_property("color_and_opacity"))
                            tb.set_editor_property("justification",
                                                   title00.get_editor_property("justification"))
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

        # ── 2. Клон плашки скидки ──────────────────────────────────────────────────────────────
        if stamp00 is None or num00 is None or stamp00_img is None:
            continue
        s_name = "BOOSTER_Stamp_Base_c%s" % nn
        s_grp = find_in(nodes, s_name)
        s_off = slot_offsets(stamp00)
        if s_off is None:
            continue
        s_want = margin_tuple(s_off)
        if s_grp is None:
            if not dry:
                s_grp = B.add_canvas(bp, grp, s_name, unreal.Vector2D(s_want[0], s_want[1]),
                                     unreal.Vector2D(s_want[2], s_want[3]))
                if s_grp:
                    nodes[s_name] = s_grp
                    # Картинка плашки — кисть копируется у образца ЦЕЛИКОМ.
                    img_off = slot_offsets(stamp00_img) or unreal.Margin(0, 0, s_want[2], s_want[3])
                    iw = margin_tuple(img_off)
                    img = B.add_image(bp, s_grp, s_name + "_0",
                                      unreal.Vector2D(iw[0], iw[1]), unreal.Vector2D(iw[2], iw[3]),
                                      None, unreal.Box2D(), unreal.LinearColor(1, 1, 1, 1),
                                      False, unreal.Margin(0, 0, 0, 0))
                    if img:
                        img.set_editor_property("brush", stamp00_img.get_editor_property("brush"))
                        nodes[s_name + "_0"] = img
                    # Канвас цифр.
                    n_off = slot_offsets(num00)
                    nw = margin_tuple(n_off) if n_off else (0.0, 0.0, 1.0, 1.0)
                    n_name = "BOOSTER_Num_Base_c%s" % nn
                    n_grp = B.add_canvas(bp, s_grp, n_name, unreal.Vector2D(nw[0], nw[1]),
                                         unreal.Vector2D(nw[2], nw[3]))
                    if n_grp:
                        nodes[n_name] = n_grp
                        for d in range(1, DIGITS + 1):
                            src = digit00[d - 1]
                            if src is None:
                                continue
                            d_off = slot_offsets(src)
                            dw = margin_tuple(d_off) if d_off else (0.0, 0.0, 1.0, 1.0)
                            d_name = "BOOSTER_Stamp%d_c%s" % (d, nn)
                            dimg = B.add_image(bp, n_grp, d_name,
                                               unreal.Vector2D(dw[0], dw[1]), unreal.Vector2D(dw[2], dw[3]),
                                               None, unreal.Box2D(), unreal.LinearColor(1, 1, 1, 1),
                                               False, unreal.Margin(0, 0, 0, 0))
                            if dimg:
                                dimg.set_editor_property("brush", src.get_editor_property("brush"))
                                nodes[d_name] = dimg
            added += 1
        else:
            off = slot_offsets(s_grp)
            if off is not None and not same_margin(off, s_want):
                if not dry:
                    set_offsets(s_grp, s_want)
                fixed += 1

    if (added or fixed) and not dry:
        # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
        B.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не сохранён %s" % ASSET)
    log("ГОТОВО%s: создано групп/подписей %d, поправлено геометрий %d (ячеек %d)"
        % ("  [СУХОЙ ПРОГОН]" if dry else "", added, fixed, CELLS - 1))


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[ячейки бустер-шопа] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
