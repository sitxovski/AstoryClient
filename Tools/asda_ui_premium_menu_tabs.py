# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_premium_menu_tabs.py — ★★★ НЕДОСТАЮЩИЕ КНОПКИ ПЕРЕКЛЮЧАТЕЛЯ ПРЕМИУМ-МОЛЛА В АССЕТЕ.
#
# ═══ ПРОБЛЕМА (класс S2 аудита, частный случай) ═══════════════════════════════════════════════
# Контрол `PremiumMall_Menu` (JUI IDNum **30002**, окно `Premium_Menu` 30000) — это `RadioButton`
# на ЧЕТЫРЕ сегмента (`JuiSkin.ini:48244`, Rect'ы `0 0 68 14` / `70 0 138 14` / `140 0 208 14` /
# `210 0 278 14`). Но у ВСЕХ ЧЕТЫРЁХ планов стоит `PlaneAlwaysInvisible = "1"` — вкладка не
# рисует НИЧЕГО, видна только ПОДПИСЬ (цвет подписи и есть индикация выбора).
#
# Импортёр (`asda_jui_generic_umg.py`) делает по кнопке `{idc}_OPT<i>` на КАЖДЫЙ ВИДИМЫЙ ПЛАН.
# Видимых планов здесь НОЛЬ ⇒ в `WBP_Premium_Menu` нет ни одной кнопки-сегмента: в дереве только
# `PremiumMall_Menu_grp`, `PremiumMall_Menu_6_HIT` и одна подпись `PremiumMall_Menu_Text` во всю
# ширину. Нажать сегмент физически не по чему.
#
# ═══ ЧТО ДЕЛАЕТ ШАГ (идемпотентно) ════════════════════════════════════════════════════════════
#   создаёт (если их нет) четыре ПРОЗРАЧНЫЕ кнопки `PremiumMall_Menu_OPT0…3` внутри
#   `PremiumMall_Menu_grp` по геометрии ИЗ СКИНА и приводит их геометрию, если она разъехалась.
#   Кнопка прозрачная (`AddButton`) — и это ТОЧНО соответствует данным: планы у скина
#   `PlaneAlwaysInvisible`, рисовать нечего.
#
#   ПОДПИСИ создаёт НЕ ЭТОТ шаг, а общая точка `Tools/asda_ui_tabbar_captions.py`: туда добавлена
#   строка `("…/WBP_Premium_Menu", "PremiumMall_Menu", 4, "SKIN:PremiumMall_Menu")`, и она же
#   кладёт в стиль кнопок КАДРОВЫЕ ЦВЕТА скина (покой `FFC8C8C8`, наведение `FFFFFFFF`,
#   выбрано `FF94FF28`), откуда их читает рантайм (`AsdaUi::ApplyTabCaptionState` с кнопками).
#   ⇒ В `asda_ui_post_import.STEPS` ЭТОТ шаг обязан стоять ДО `asda_ui_tabbar_captions`.
#
# ★ САМ ТЕКСТ подписей ставит РАНТАЙМ по id локализации 99401/99400/99402/99403
#   (`CBoosterShop::Init`, `AsdaStory_ida.c:420658-420671`) — выдумывать его в ассете нельзя.
#
# ═══ ОТКУДА ГЕОМЕТРИЯ (ничего не подобрано) ═══════════════════════════════════════════════════
#   X и ширина — из Rect'ов скина: 0, 70, 140, 210 при ширине 68.
#   Y и высота — у уже существующей хит-зоны `PremiumMall_Menu_6_HIT` того же контрола
#   (в ассете off(L0 T3 R278 B14)); если её нет — из Rect скина (y=0, h=14).
#   Родитель — канвас `PremiumMall_Menu_grp` (в нём же лежат хит-зона и подпись).
#
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_ui_premium_menu_tabs.py
#   Сухой прогон: set ASDA_PREMTAB_DRYRUN=1
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

ASSET = "/Game/AsdaAssets/UI/Player_UI/Premium/WBP_Premium_Menu"
IDC = "PremiumMall_Menu"
GRP = "PremiumMall_Menu_grp"
HIT = "PremiumMall_Menu_6_HIT"
SEGMENTS = 4
# Rect'ы планов скина `PremiumMall_Menu` (`RE/docs/JuiSkin.ini:48244`): (x0, y0, x1, y1).
SKIN_RECTS = [(0, 0, 68, 14), (70, 0, 138, 14), (140, 0, 208, 14), (210, 0, 278, 14)]


def log(m):
    unreal.log("[премиум-вкладки] %s" % m)


walk = _UWC.walk   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


canvas_slot = _UWC.canvas_slot_strict   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def slot_offsets(w):
    s = canvas_slot(w)
    if s is None:
        return None
    return s.get_editor_property("layout_data").get_editor_property("offsets")


def same_margin(m, want):
    return (abs(m.left - want[0]) < EPS and abs(m.top - want[1]) < EPS
            and abs(m.right - want[2]) < EPS and abs(m.bottom - want[3]) < EPS)


def main():
    dry = bool(os.environ.get("ASDA_PREMTAB_DRYRUN"))
    B = unreal.AsdaUmgBuilder
    if not EAL.does_asset_exist(ASSET):
        log("НЕТ АССЕТА %s" % ASSET)
        return
    bp = EAL.load_asset(ASSET)
    root = B.get_root_canvas(bp)
    if not root:
        log("нет корневого канваса у %s" % ASSET)
        return

    nodes = {}
    for w in walk(root):
        nodes.setdefault(w.get_name(), w)

    parent = nodes.get(GRP)
    if not isinstance(parent, unreal.PanelWidget):
        log("!! нет группы %s — переключатель 30002 достроить некуда" % GRP)
        return

    # Y/высота — у хит-зоны того же контрола (она уже стоит на месте вкладок).
    hit_off = slot_offsets(nodes.get(HIT))
    y = hit_off.top if hit_off else float(SKIN_RECTS[0][1])
    h = hit_off.bottom if hit_off else float(SKIN_RECTS[0][3] - SKIN_RECTS[0][1])

    added = fixed = 0
    for i in range(SEGMENTS):
        x0, _, x1, _ = SKIN_RECTS[i]
        want = (float(x0), float(y), float(x1 - x0), float(h))
        name = "%s_OPT%d" % (IDC, i)
        btn = nodes.get(name)
        if btn is None:
            if dry:
                added += 1
                continue
            btn = B.add_button(bp, parent, name,
                               unreal.Vector2D(want[0], want[1]), unreal.Vector2D(want[2], want[3]))
            if not btn:
                log("!! не удалось создать %s" % name)
                continue
            nodes[name] = btn
            added += 1
            continue
        # Уже есть — только приводим геометрию (идемпотентность: второй прогон = 0 правок).
        off = slot_offsets(btn)
        if off is None:
            log("  %s не в канвасе — геометрию не трогаю" % name)
            continue
        if not same_margin(off, want):
            if dry:
                fixed += 1
                continue
            s = canvas_slot(btn)
            lay = s.get_editor_property("layout_data")
            lay.set_editor_property("offsets", unreal.Margin(want[0], want[1], want[2], want[3]))
            s.set_editor_property("layout_data", lay)
            s.set_editor_property("auto_size", False)
            fixed += 1

    if (added or fixed) and not dry:
        # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
        B.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не сохранён %s" % ASSET)
    log("ГОТОВО%s: создано кнопок %d, поправлено %d (сегментов %d)"
        % ("  [СУХОЙ ПРОГОН]" if dry else "", added, fixed, SEGMENTS))


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[премиум-вкладки] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
