# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_item_count_outline.py — ★★ [ОРИГИНАЛ]/[ИСПРАВЛЕНИЕ] ЧЁРНАЯ ОБВОДКА 1 px У ЧИСЛА КОЛИЧЕСТВА В ЯЧЕЙКАХ ПРЕДМЕТОВ (26.10.2026).
#
# ЖАЛОБА ИГРОКОВ. «Число количества на некоторых предметах плохо видно» — белая цифра без контура сливается со светлыми
#   иконками (флаконы, зелья, свитки, руда).
# ЭТАЛОН. Число стопки — текст самого контрола-иконки (`wsprintfA("%d")`, `CControl_SetField923(-1)` = белый); стиль даёт
#   поле 926 (`CClass_SetField926`: 0x800000 обводка, 0x400000 тень). По таблице `Tools/data/text_style926.tsv`:
#     • ОБВОДКА — обмен `TRAN_TRAY*` (shop_trade.c:7392), магазин NPC (npc_mob_pet.c:5899/5910), улучшение
#       (item_inventory.c:43766/43836), отчёт охоты (ui_core.c:87590), книга рыбака (ui_core.c:100239/100275), задания
#       (quest.c:1365/1442), попап наград (ui_core.c:59399), питомец (ui_core.c:90560); сумка и хотбар — тоже (их ведёт код);
#     • ТЕНЬ — склад (item_inventory.c:57381/57598), лавка (shop_trade.c:15443/15456/16700/16708), передача параметров
#       (item_enhance.c:911), коробка (item_enhance.c:3323/3334);
#     • НИЧЕГО — ячейки чужого персонажа `OTHERCHARA_EQUIP*`.
#   В ассетах на 26.10.2026 у «обводочных» окон обводка в основном уже есть (шаг поля 926), но НЕ у отчёта охоты
#   (31 узел — гейт `ASDA_STYLE926_SANMATCH_ALL`: в имени окна пробел), у 2 узлов попапа наград и 1 узла книги рыбака.
# РЕШЕНИЕ ВЛАДЕЛЬЦА 26.10.2026: у числа количества во всех окнах — обводка шрифта 1 px, цвет чёрный. Для «обводочных» окон
#   это [ОРИГИНАЛ], для «теневых» и чужого персонажа — [ИСПРАВЛЕНИЕ] (тень остаётся как была, обводка добавляется).
#
# ЧТО ДЕЛАЕТ ШАГ (идемпотентно, ТОЧЕЧНО): в окнах из `TARGETS` находит текстовые узлы-счётчики ячеек и ставит им
#   `font.outline_settings`: outline_size = PX, outline_color = чёрный, apply_outline_to_drop_shadows = False, и
#   `render_transform.translation` = компенсация сдвига глифа (см. ниже). Больше НИЧЕГО не трогает: кегль, гарнитура,
#   цвет, тень, слот, видимость, текст — как были (правки владельца целы).
#   ★★ СДВИГ ГЛИФА ОТ ОБВОДКИ (ревизия wave_1026 Н3, замер 26.10.2026). Slate учитывает обводку в раскладке строки:
#     `SlateTextRun.cpp:36` базовая линия −OutlineSize, `:42` высота +OutlineSize, `:71-77` ширина +2·OutlineSize
#     (`FontMeasure.cpp:287` — лишь ключ кэша, не «замер без обводки», как было написано прежде). Текст в слоте канваса
#     прижат к ВЕРХУ ⇒ заливка цифры уезжает на OutlineSize ВНИЗ; по горизонтали при выравнивании по центру сдвига нет,
#     влево — +OutlineSize вправо, вправо — +OutlineSize влево. У эталона обводка — крест ±1 px ВОКРУГ неподвижной заливки
#     (`font_text.c:6952`, `TextFont_BuildGlyphQuad`), заливка не двигается. Замер кадров «до/после» (тот же набор
#     стопок, подбор сдвига по совпадению белых пикселей заливки): сумка, хотбар, склад, лавка, отчёт охоты, магазин,
#     улучшение — везде (dx, dy) = (0, +1). Поэтому узлу ставится сдвиг отрисовки (render translation) (−dx, −OutlineSize):
#     слот и бокс не меняются, заливка цифры стоит там же, где без обводки (как у эталона). При OutlineSize = 0 — (0, 0).
#   ★ Узлы, чей шрифт ставит КОД (сумка `INVEN_SET*`, хотбар `QUICK_NEW_SLOT*`, разборка `Disassembly_RIcon*`), здесь
#     НЕ перечислены: им обводку даёт код (`AsdaItemCount::ApplyOutline`, ключ `asda.UI.ItemCountOutline`; разборка —
#     `SetTextOutline(Tb, 1)` в `AsdaDisassemblySubsystem.cpp`).
#   ★ Шаг стоит в `asda_ui_post_import.STEPS` ПОСЛЕ `asda_ui_text_style926`, а сам `asda_ui_text_style926` эти узлы
#     пропускает (`is_count_node`), — иначе его прогон снимал бы обводку обратно (у него «нет в таблице → 0»).
#
# СТОРОЖА. По каждому окну задано ожидаемое ЧИСЛО узлов (`want`): не совпало (переименование, перегенерация) — ОТКАЗ
#   без записи. Окно сцены логина сюда не входит (LOGIN-GUARD не нужен). Сохраняются только изменённые пакеты.
#
# ЗАПУСК:   python Tools/run_headless_safe.py Tools/asda_ui_item_count_outline.py --marker "ЗАВЕРШЕНО: обводка числа"
#   ASDA_ITEMCOUNT_DRYRUN=1   сухой прогон: что изменил бы, без записи;
#   ASDA_ITEMCOUNT_DUMP=1     только перечень текстовых узлов окон `TARGETS` (имя, слот, обводка, тень, текст), без записи;
#   ASDA_ITEMCOUNT_OUTLINE_PX=N  толщина (по умолчанию 1); 0 — ОТКАТ: обводка такая, какую дал бы один шаг поля 926
#                             (`asda_ui_text_style926`, данные оригинала: где у эталона обводка — 1, иначе 0);
#   ASDA_ITEMCOUNT_ONLY=WBP_A,WBP_B  только эти окна.
#   Соседнего `-game` быть не должно: сохранение ассета при нём молча не проходит (проверять дату .uasset).
import os
import re
import traceback

import unreal

import os as _uwc_os, sys as _uwc_sys
if _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)) not in _uwc_sys.path:
    _uwc_sys.path.insert(0, _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)))
import asda_ui_widget_common as _UWC   # noqa: E402  общий обход дерева WBP

EAL = unreal.EditorAssetLibrary
# Ключ сухого режима (его же ставит сухой прогон цепочки `asda_ui_post_import.DRY_ENV`; общий ASDA_POSTSTEPS_DRYRUN — тоже).
DRYRUN_ENV = "ASDA_ITEMCOUNT_DRYRUN"
UI = "/Game/AsdaAssets/UI/Player_UI/"
MARKER = "ЗАВЕРШЕНО: обводка числа"

# (окно, regex имени узла, ожидаемое число узлов (None — не сверять), откуда известно, что это число количества)
TARGETS = (
    ("Inventory/WBP_WARE_WIN", r"^(WAREKEEP_TRAY\d\d|WAREBAG\d_TRAY\d\d)$", 275,
     "склад 4000: AsdaWarehouseSubsystem.cpp:511-524, CellText :1854 (эталон uiSetItemCountText)"),
    ("Trade/WBP_TRAN_WIN", r"^TRAN_TRAY\d\d$", 34,
     "обмен 2008: AsdaExchangeP2PWindow.cpp:255 (список сумки и лотки обмена)"),
    ("Trade/WBP_StreetStall_Open_Win", r"^(Stall_Open_Inven\d\d|Stall_Open_SellIcon\d\d)$", 40,
     "лавка продавца 22000: AsdaSelfStoreSubsystem.cpp:452/456"),
    ("Trade/WBP_StreetStall_Win", r"^(Stall_SellIcon\d\d|Stall_PurItem\d\d)$", 20,
     "лавка покупателя 23000: AsdaSelfStoreSubsystem.cpp:1516/1522"),
    ("Trade/WBP_NPCSHOP_WIN", r"^(NPCSHOP_TRAY\d\d|NPCSHOP_BTRAY\d\d)$", 41,
     "магазин NPC: AsdaNpcShopSubsystem.cpp:334-339"),
    ("Trade/WBP_EXTRACT_WIN", r"^EXTRACT_Scroll\d\d$", 6,
     "передача параметров 16000: AsdaExtractShopSubsystem.cpp:361"),
    ("Craft/WBP_Item_Upgrade_Win", r"^(Item_Ugrade_Sub[01]_\d+|Item_Ugrade_Scroll\d\d)$", 12,
     "улучшение 20000: AsdaItemUpgradeSubsystem.cpp:556-560"),
    ("Character/WBP_Hunt_report_Win", r"^Hunt_report_Mark_\d+$", 31,
     "отчёт охоты 56450: AsdaHuntReportSubsystem.cpp:197/201"),
    ("Fishing/WBP_FISH_BOOK_WIN", r"^(FISH_BOOK_JOINBOX_M\d+|FISH_BOOK_BIG_BOX_3_MARK_.*)$", 34,
     "книга рыбака: AsdaFishBookSubsystem.cpp:519/526"),
    ("Quest/WBP_QUEST_WIN", r"^QUEST_TRAY\d$", 5,
     "журнал заданий: AsdaQuestLogSubsystem.cpp:1687 (TrayCounts)"),
    ("Quest/WBP_EPISODEPOP_WIN", r"^EPISODEPOP_ITEMICON\d\d$", 12,
     "попап наград 540: AsdaNpcDialogSubsystem_Popups.cpp:1252/1256"),
    ("Quest/WBP_QUEST_Confirm_Win", r"^QUEST_Confirm_Text_T[12]_MARK\d+$", 8,
     "подтверждение задания: AsdaNpcDialogSubsystem_Popups.cpp:1574/1578"),
    ("Pet/WBP_Pet_LimitBreak_Win", r"^Pet_LimitBreak_SETMARK[23]$", 2,
     "лимит-брейк питомца: AsdaPetGrowthSubsystem.cpp:316-317"),
    ("Character/WBP_OTHERCHARA_DATA", r"^OTHERCHARA_EQUIP\d\d$", 11,
     "чужой персонаж: AsdaOtherCharaSubsystem.cpp:481"),
    ("Premium/WBP_Package_Contents_Win", r"^Package_(BagIcon\d|BagItemIcon\d_\d|ItemIcon\d_+\d(_\d)?)$", 45,
     "панель коробки 26500: AsdaPackagePanelSubsystem.cpp:283/321 (код ищет число по ширине слота ≤ 64/32 px; "
     "имена — по дампу ассета 26.10.2026, без `Package_Contents_AType` шириной 8)"),
)


def log(m):
    unreal.log("[обводка-числа] %s" % m)


def _slot_w(w):
    s = _UWC.canvas_slot(w)
    if not s:
        return None
    try:
        o = s.get_editor_property("layout_data").get_editor_property("offsets")
        return float(o.right)
    except Exception:                                        # noqa: BLE001
        return None


_COMPILED = None


def _targets():
    global _COMPILED
    if _COMPILED is None:
        _COMPILED = [(p, re.compile(rx), want, why) for p, rx, want, why in TARGETS]
    return _COMPILED


def is_count_node(window_name, node_name, widget=None):
    """True — узел `node_name` окна `window_name` (имя пакета, с `WBP_` или без) — число количества из `TARGETS`.
    Зовёт `asda_ui_text_style926`, чтобы НЕ снимать обводку, которую ставит этот шаг. `widget` — для совместимости вызова."""
    win = window_name if window_name.startswith("WBP_") else "WBP_" + window_name
    for p, rx, _want, _why in _targets():
        if p.rsplit("/", 1)[-1] != win:
            continue
        if rx.match(node_name):
            return True
    return False


def _outline_of(w):
    os_ = w.get_editor_property("font").get_editor_property("outline_settings")
    c = os_.get_editor_property("outline_color")
    return int(os_.get_editor_property("outline_size")), (c.r, c.g, c.b, c.a), bool(
        os_.get_editor_property("apply_outline_to_drop_shadows"))


def _comp(w, px):
    """Сдвиг отрисовки (x, y), возвращающий заливку цифры на место без обводки толщиной `px` (см. шапку, Н3)."""
    if px <= 0:
        return (0.0, 0.0)
    j = w.get_editor_property("justification")
    dx = 0.0
    if j in (unreal.TextJustify.LEFT, getattr(unreal.TextJustify, "INVARIANT_LEFT", unreal.TextJustify.LEFT)):
        dx = -float(px)
    elif j in (unreal.TextJustify.RIGHT, getattr(unreal.TextJustify, "INVARIANT_RIGHT", unreal.TextJustify.RIGHT)):
        dx = float(px)
    return (dx, -float(px))


def _translation(w):
    t = w.get_editor_property("render_transform").get_editor_property("translation")
    return (float(t.x), float(t.y))


def _is_dry():
    """Сухой режим: свой ключ или общий ключ сухого прогона цепочки пост-импорта."""
    v = lambda k: str(os.environ.get(k, "")).strip().lower() in ("1", "true", "yes", "on")
    return v(DRYRUN_ENV) or v("ASDA_POSTSTEPS_DRYRUN")


def main():
    dry = _is_dry()
    dump = bool(os.environ.get("ASDA_ITEMCOUNT_DUMP"))
    px = int(os.environ.get("ASDA_ITEMCOUNT_OUTLINE_PX", "1"))
    only = [s.strip() for s in os.environ.get("ASDA_ITEMCOUNT_ONLY", "").split(",") if s.strip()]
    log("толщина %d px%s%s" % (px, "  [СУХОЙ ПРОГОН]" if dry else "", "  [ПЕРЕЧЕНЬ УЗЛОВ]" if dump else ""))
    B = unreal.AsdaUmgBuilder
    total_nodes = total_changed = 0
    changed_assets = []
    problems = []
    plan = []
    for p, rx, want, why in _targets():
        win = p.rsplit("/", 1)[-1]
        if only and win not in only:
            continue
        path = UI + p
        if not EAL.does_asset_exist(path):
            problems.append("нет ассета %s" % path)
            continue
        bp = EAL.load_asset(path)
        root = B.get_root_canvas(bp) if bp else None
        if not root:
            problems.append("нет корневого канваса %s" % path)
            continue
        nodes = []
        for w in _UWC.walk(root):
            if not isinstance(w, unreal.TextBlock):
                continue
            if dump:
                sz, col, sh = _outline_of(w)
                log("  УЗЕЛ %s / %s: слот-ширина=%s обводка=%d цвет=%s тень-альфа=%.2f выравнивание=%s сдвиг=%s текст=«%s»"
                    % (win, w.get_name(), _slot_w(w), sz, tuple(round(x, 3) for x in col),
                       w.get_editor_property("shadow_color_and_opacity").a, w.get_editor_property("justification"),
                       _translation(w), w.get_editor_property("text")))
            if rx.match(w.get_name()):
                nodes.append(w)
        if want is not None and len(nodes) != want:
            problems.append("%s: узлов %d, ждали %d" % (win, len(nodes), want))
            continue
        if not nodes:
            problems.append("%s: ни одного узла-счётчика" % win)
            continue
        plan.append((path, win, bp, nodes, why))
    if problems:
        for m in problems:
            unreal.log_error("[обводка-числа] ОТКАЗ: %s" % m)
        raise RuntimeError("сторож шага: %d проблем, ничего не записано" % len(problems))
    if dump:
        for path, win, _bp, nodes, why in plan:
            log("окно %s: узлов-счётчиков %d (%s): %s" % (win, len(nodes), why, ", ".join(sorted(n.get_name() for n in nodes))))
        log("%s (перечень, без записи)" % MARKER)
        return
    black = unreal.LinearColor(0.0, 0.0, 0.0, 1.0)
    legacy = None
    if px <= 0:
        # ОТКАТ: значение, которое поставил бы шаг поля 926 (его буквальное сопоставление — то, что действует по умолчанию).
        import asda_ui_text_style926 as _S926   # ленивый импорт: тот модуль импортирует этот
        _want, legacy, _cond = _S926.load_table()
    for path, win, bp, nodes, why in plan:
        changed = 0
        for w in nodes:
            total_nodes += 1
            sz, col, sh = _outline_of(w)
            tgt = px
            if legacy is not None:
                tgt = legacy.get((win[4:], _S926.node_idc_legacy(w.get_name())), (0, 0, 0))[0]
            want_tr = _comp(w, tgt)
            tr_ok = _translation(w) == want_tr
            if sz == tgt and (tgt == 0 or (col == (0.0, 0.0, 0.0, 1.0) and not sh)) and tr_ok:
                continue
            changed += 1
            if dry:
                continue
            if not tr_ok:
                rt = w.get_editor_property("render_transform")
                rt.set_editor_property("translation", unreal.Vector2D(want_tr[0], want_tr[1]))
                w.set_editor_property("render_transform", rt)
            f = w.get_editor_property("font")
            os_ = f.get_editor_property("outline_settings")
            os_.set_editor_property("outline_size", tgt)
            if tgt > 0:
                os_.set_editor_property("outline_color", black)
                os_.set_editor_property("apply_outline_to_drop_shadows", False)
            f.set_editor_property("outline_settings", os_)
            w.set_editor_property("font", f)
        log("окно %s: узлов %d, меняется %d (%s)" % (win, len(nodes), changed, why))
        total_changed += changed
        if changed and not dry:
            B.compile_and_save(bp)   # ★ только компиляция (имя врёт) — сохраняем сами
            if not EAL.save_loaded_asset(bp, False):
                raise RuntimeError("не сохранён ассет %s" % path)
            unreal.log("ASDA_WRITES_ASSET: %s" % path)
            changed_assets.append(win)
    log("итого: окон %d, узлов %d, изменено %d%s; записаны: %s"
        % (len(plan), total_nodes, total_changed, " [СУХОЙ ПРОГОН — без записи]" if dry else "",
           ", ".join(changed_assets) or "—"))
    log(MARKER)


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает (его импортирует asda_ui_text_style926).
if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[обводка-числа] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
