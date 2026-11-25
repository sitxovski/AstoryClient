# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_item_count_align.py — ★★ [ОРИГИНАЛ] МЕСТО ЧИСЛА КОЛИЧЕСТВА В ЯЧЕЙКАХ, КОТОРОЕ ОРИГИНАЛ ЗАДАЁТ КОДОМ (08.11.2026, пачка 16, баг 8).
#
# ЖАЛОБА ВЛАДЕЛЬЦА. Окно «Report monster hunting results» (56450): число знаков («5» у знака волка) стоит слева посередине
#   большой ячейки, а в ячейке сетки внизу прилипло к левому краю. В оригинале — не так. Снимок:
#   `Reports/batch16/screens/marks_quantity.png`.
# ПРИЧИНА. Число стопки — текст самого контрола-иконки (`NoImage_icon`). У таких контролов в .JUI `EXS TextAlign = "4"`
#   (лево + середина), и импортёр строит узел-число ровно по ней: LEFT, верх = середина ячейки − 14/2. Но оригинал при
#   сборке окна ПЕРЕОПРЕДЕЛЯЕТ маску выравнивания кодом — `CControl_SetField924(ctrl, mask)` (поле 924 = флаги DT_:
#   0x1 центр по горизонтали, 0x4 середина, 0x8 низ; `uiTextControl_Render`, ui_core.c:110728). Этих вызовов в .JUI нет,
#   поэтому в ассетах их не было. Окна, где число ставит код или шаги ассетов уже дали низ+центр (сумка, склад, магазин,
#   обмен, лавка, книга рыбака, разборка, улучшение, коробка), — в порядке; без переноса остались ровно эти:
#     маска 9 (низ + центр):
#       • отчёт охоты 56450: 56451 (большая ячейка), 56461..56490 (сетка) — `uiSowelWindow_InitControls@0xECF680`,
#         ui_core.c:87591/87599;
#       • передача опций 50300: 50302, 50304..50333 — item_inventory.c:3765/3776;
#       • ремонт 57300: 57311..57321 (надетое), 57331..57360 (сумка) — ui_core.c:89362/89370;
#       • снятие предела питомца: 44251..44253 — ui_core.c:90548/90553/90558;
#       • объединение 50600: список 50651..50680 — ui_core.c:91198;
#     маска 5 (середина + центр):
#       • подтверждение задания: 762/764/766/768 и 772/774/776/778 — quest.c:1443/1454;
#       • отчёт о покорении: 75252/75254, 75261/75263/75265/75267 — quest.c:9236/9254.
#   Сверка всех вызовов `CControl_SetField924(…, 9|5)` эталона против ассетов — пробы
#   `Reports/batch16/hunt_marks/probe_count_mask_before.tsv` и `probe_count_align_before.tsv`.
#
# ЧТО ДЕЛАЕТ ШАГ (ТОЧЕЧНО, идемпотентно). Узлу-числу из `TARGETS`, у которого вид ровно импортёрский (LEFT, левый край
#   и ширина = план иконки `{idc}_{N}_0` того же родителя, верх = середина ячейки), ставит:
#     • `Justification` = CENTER (бит 0x1 маски);
#     • верх слота = верх ячейки + (высота − 14) при маске 0x8 / + (высота − 14)/2 при 0x4 — та же формула, что у импортёра
#       для контролов, у кого маска 9/5 записана в .JUI (`asda_jui_parse.label_slot`, строка 14 px — `LABEL_LINE_PX`);
#     • высота слота при маске 0x8 = до низа ячейки (14): поле числа не выходит за свою ячейку и не ложится на соседние
#       (у большой ячейки 56451 импортёрский бокс 64 px от низа ячейки накрыл бы верхний ряд сетки); при маске 5 высота
#       не меняется (верх и так по середине; шаг `asda_ui_subjugate_rows` копирует слот `Subjugate_R_M1` и сверяет по нему);
#     • сдвиг отрисовки (render translation) под обводку: при обводке N px по центру = (0, −N) — та же компенсация глифа,
#       что у `asda_ui_item_count_outline._comp` (при LEFT было (−N, −N)); без обводки сдвиг не трогается.
#   Больше НИЧЕГО: кегль, гарнитура, цвет, обводка, тень, видимость, текст, левый край, ширина — как были.
#   Уже исправленный узел — пропуск; любой другой вид — строка «осмотреть», узел НЕ трогается (ручная правка владельца).
#
# СТОРОЖА. По окну — ожидаемое число узлов (`want`); не совпало — ОТКАЗ всего шага без записи. Нет плана иконки — ОТКАЗ.
#   Запись — только окна с правками, после бэкапа `_backup/<дата>_<BACKUP_SET>/…` (`asda_headless_tool.backup_file`).
#   Порядок в цепочке: ПОСЛЕ `asda_ui_item_count_outline` (тот ставит сдвиг по выравниванию; после этого шага его повтор
#   видит CENTER и даёт тот же (0, −N) — цепочка идемпотентна).
#
# ЗАПУСК (редактор ЗАКРЫТ, соседнего -game нет):
#   python Tools/run_headless_safe.py Tools/asda_ui_item_count_align.py --marker "ЗАВЕРШЕНО: место числа"
#   ASDA_ITEMCOUNT_ALIGN_DRYRUN=1 — сухой прогон (что изменил бы, без записи); общий ASDA_POSTSTEPS_DRYRUN — тоже.
#   ASDA_ITEMCOUNT_ALIGN_ONLY=WBP_A,WBP_B — только эти окна. Откат — бэкап набора (или переимпорт окна без этого шага).
import os
import re
import sys

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import asda_ui_widget_common as _UWC   # noqa: E402  общий обход дерева WBP / канвасный слот
import asda_headless_tool as HT        # noqa: E402  единая резервная копия `backup_file`
import asda_ui_step_dryrun as DR       # noqa: E402  сухой режим цепочки и запись

EAL = unreal.EditorAssetLibrary
DRYRUN_ENV = "ASDA_ITEMCOUNT_ALIGN_DRYRUN"
UI = "/Game/AsdaAssets/UI/Player_UI/"
CONTENT_DIR = os.path.normpath(os.path.join(HERE, "..", "Content"))
MARKER = "ЗАВЕРШЕНО: место числа"
BACKUP_SET = "hunt_marks"
LINE_PX = 14.0      # высота строки, которую принимает импортёр (`asda_jui_parse.LABEL_LINE_PX`)
EPS = 0.01

# (окно, regex имени узла-числа, ожидаемое число узлов, маска эталона, откуда)
TARGETS = (
    ("Character/WBP_Hunt_report_Win", r"^Hunt_report_Mark_\d+$", 31, 9,
     "отчёт охоты 56450: uiSowelWindow_InitControls@0xECF680, ui_core.c:87591/87599"),
    ("Craft/WBP_ItemOptionValue_WIN", r"^(ItemOptionValue_Mark_1|ItemOptionValue_1_Box_Mark_\d\d)$", 31, 9,
     "передача опций 50300: item_inventory.c:3765/3776"),
    ("Craft/WBP_INVEN_Repair_Win", r"^INVEN_Repair_(Jmark|Imark)_\d+$", 41, 9,
     "ремонт 57300: ui_core.c:89362/89370"),
    ("Pet/WBP_Pet_LimitBreak_Win", r"^Pet_LimitBreak_SETMARK[23]?$", 3, 9,
     "снятие предела питомца: ui_core.c:90548/90553/90558"),
    ("Craft/WBP_Item_Union_Win", r"^Item_Union_3_Mark_\d\d$", 30, 9,
     "объединение 50600, список: ui_core.c:91198"),
    ("Quest/WBP_QUEST_Confirm_Win", r"^QUEST_Confirm_Text_T[12]_MARK\d$", 8, 5,
     "подтверждение задания: quest.c:1443/1454"),
    ("Quest/WBP_Subjugate_WIN", r"^Subjugate_R_M[1-6]$", 6, 5,
     "отчёт о покорении: quest.c:9236/9254"),
)


def log(m):
    unreal.log("[место-числа] %s" % m)


def _offsets(w):
    s = _UWC.canvas_slot(w)
    if not s:
        return None, None
    o = s.get_editor_property("layout_data").get_editor_property("offsets")
    return s, (float(o.left), float(o.top), float(o.right), float(o.bottom))


def _icon_rect(w, siblings):
    """Прямоугольник (l, t, w, h) плана иконки — узел импортёра `{idc}_{N}_{M}` того же родителя (первый по имени)."""
    rx = re.compile(re.escape(w.get_name()) + r"_\d+_\d+$")
    par = w.get_parent()
    for nm in sorted(siblings):
        cw = siblings[nm]
        if rx.match(nm) and cw.get_parent() == par:
            _s, r = _offsets(cw)
            if r:
                return r
    return None


def _want(icon, mask, cur_h):
    """Целевые верх и высота слота числа по маске эталона (см. шапку). `cur_h` — высота сейчас: при маске без 0x8
    (середина) она не меняется — шаг `asda_ui_subjugate_rows` копирует слот `Subjugate_R_M1` и сверяет по нему."""
    il, it, iw, ih = icon
    if mask & 0x8:
        vy = ih - LINE_PX
    elif mask & 0x4:
        vy = (ih - LINE_PX) / 2.0
    else:
        vy = 0.0
    top = it + max(vy, 0.0)
    return top, (max(it + ih - top, LINE_PX) if (mask & 0x8) else cur_h)


def _outline_px(w):
    return int(w.get_editor_property("font").get_editor_property("outline_settings").get_editor_property("outline_size"))


def _translation(w):
    t = w.get_editor_property("render_transform").get_editor_property("translation")
    return (float(t.x), float(t.y))


def _near(a, b):
    return abs(a - b) <= EPS


def _asset_file(path):
    return os.path.join(CONTENT_DIR, path[len("/Game/"):].replace("/", os.sep) + ".uasset")


def classify(w, icon, mask):
    """'ok' — уже как у эталона; 'fix' — импортёрский вид (правим); 'look' — чужой вид (осмотреть)."""
    _s, r = _offsets(w)
    if not r or not icon:
        return "look", "нет канвасного слота/плана иконки"
    l, t, ww, hh = r
    il, it, iw, ih = icon
    top, h = _want(icon, mask, hh)
    j = w.get_editor_property("justification")
    px = _outline_px(w)
    tr = _translation(w)
    if not (_near(l, il) and _near(ww, iw)):
        return "look", "левый край/ширина не по ячейке (%g,%g против %g,%g)" % (l, ww, il, iw)
    if j == unreal.TextJustify.CENTER and _near(t, top) and _near(hh, h) and (px <= 0 or tr == (0.0, -float(px))):
        return "ok", ""
    imp_top = it + max((ih - LINE_PX) / 2.0, 0.0)
    if j == unreal.TextJustify.LEFT and _near(t, imp_top):
        return "fix", ""
    return "look", "выравнивание %s, верх %g (импортёр %g, цель %g)" % (j, t, imp_top, top)


def apply(w, icon, mask):
    s, r = _offsets(w)
    top, h = _want(icon, mask, r[3])
    ld = s.get_editor_property("layout_data")
    o = ld.get_editor_property("offsets")
    o.top = top
    o.bottom = h
    ld.set_editor_property("offsets", o)
    s.set_editor_property("layout_data", ld)
    w.set_editor_property("justification", unreal.TextJustify.CENTER)
    px = _outline_px(w)
    if px > 0:
        rt = w.get_editor_property("render_transform")
        rt.set_editor_property("translation", unreal.Vector2D(0.0, -float(px)))
        w.set_editor_property("render_transform", rt)


def main():
    dry = DR.is_dry(DRYRUN_ENV)
    only = [s.strip() for s in os.environ.get("ASDA_ITEMCOUNT_ALIGN_ONLY", "").split(",") if s.strip()]
    B = unreal.AsdaUmgBuilder
    log("старт%s" % ("  [СУХОЙ ПРОГОН]" if dry else ""))
    plan, problems = [], []
    for p, rx, want, mask, why in TARGETS:
        win = p.rsplit("/", 1)[-1]
        if only and win not in only:
            continue
        path = UI + p
        bp = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
        root = B.get_root_canvas(bp) if bp else None
        if not root:
            problems.append("нет ассета/канваса %s" % path)
            continue
        allw = _UWC.walk(root)
        byname = {x.get_name(): x for x in allw}
        r = re.compile(rx)
        nodes = [x for x in allw if isinstance(x, unreal.TextBlock) and r.match(x.get_name())]
        if len(nodes) != want:
            problems.append("%s: узлов %d, ждали %d" % (win, len(nodes), want))
            continue
        rows = []
        for x in nodes:
            icon = _icon_rect(x, byname)
            if not icon:
                problems.append("%s/%s: нет плана иконки" % (win, x.get_name()))
                continue
            st, msg = classify(x, icon, mask)
            rows.append((x, icon, st, msg))
        plan.append((path, win, bp, mask, why, rows))
    if problems:
        for m in problems:
            unreal.log_error("[место-числа] ОТКАЗ: %s" % m)
        raise RuntimeError("сторож шага: %d проблем, ничего не записано" % len(problems))
    total_fix = total_look = 0
    for path, win, bp, mask, why, rows in plan:
        fix = [x for x in rows if x[2] == "fix"]
        look = [x for x in rows if x[2] == "look"]
        for x, _icon, _st, msg in look:
            log("  ОСМОТРЕТЬ %s / %s: %s (не трогаю)" % (win, x.get_name(), msg))
        log("окно %s (маска %d, %s): узлов %d, правка %d, уже как у эталона %d, осмотреть %d"
            % (win, mask, why, len(rows), len(fix), len(rows) - len(fix) - len(look), len(look)))
        total_fix += len(fix)
        total_look += len(look)
        if not fix or dry:
            continue
        bak = HT.backup_file(_asset_file(path), BACKUP_SET, log=log)
        if bak is None:
            raise RuntimeError("нет файла ассета %s — бэкап невозможен, запись запрещена" % path)
        for x, icon, _st, _msg in fix:
            apply(x, icon, mask)
            if classify(x, icon, mask)[0] != "ok":
                raise RuntimeError("%s/%s: после правки вид не эталонный — запись отменена" % (win, x.get_name()))
        B.compile_and_save(bp)   # ★ только компиляция (имя врёт) — пишем сами
        if not DR.save(bp, dry, "место-числа", False):
            raise RuntimeError("не сохранён ассет %s" % path)
        unreal.log("ASDA_WRITES_ASSET: %s" % path)
    log("итого: правка %d, осмотреть %d%s" % (total_fix, total_look, "  [СУХОЙ — без записи]" if dry else ""))
    log(MARKER)


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    main()
