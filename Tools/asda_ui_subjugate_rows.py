# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_subjugate_rows.py — ★ ДОСКА ОБЪЯВЛЕНИЙ (окно `Subjugate_WIN` 75000): ТОЧЕЧНОЕ дополнение ассета узлами текста, которых
#   импортёр JUI не построил. 28.10.2026, отчёт `Reports/board_quest/CLIENT.md`; спецификация
#   `Docs/Port/systems/BOARD_QUEST_FLOW.md` §3.
#
# ═══ ЧТО НЕ ТАК В АССЕТЕ (полный дамп — `Reports/board_quest/subjugate_tree.txt`) ════════════════════════════════════════
#   Колонки строк списка 75061…75080 / 75101…75120 / 75141…75160 / 75181…75200 / 75301…75320 — Text-контролы с ПУСТЫМ `ID`,
#   пустым `CaptionText` и `EXS TextContent`: у строки 1 там образец текста (импортёр сделал узлы `Subjugate_BB1_1_TXT00…04`
#   по правилу `J.missing_text_nodes`), у строк 2…20 — ПУСТАЯ строка, и узлов текста импортёр не создал вовсе (есть только
#   зоны мыши `*_HIT`). Эталон пишет в эти контролы КОДОМ (`QuestUI_PopulateQuestList@0xEEB150`: `CControl_SetText` по IDNum) —
#   без узлов 19 строк из 20 остаются пустыми. То же у количеств предметов-наград 75256/75258 (`NoImage_icon` без `ID`,
#   `CControl_SetText(иконка, "%d")` в `ItemDetail_PopulateWithSowels@0xEEB990`): узла текста у иконки нет.
#   Дефект разметки ⇒ правка В АССЕТЕ (закон no-runtime-widget-fixes), точечно, без перегенерации окна.
#
# ═══ ЧТО ДЕЛАЕТ ═══════════════════════════════════════════════════════════════════════════════════════════════════════════
#   • Строки 2…20: в группе `Subjugate_BB1_{r}_grp` 5 узлов `Subjugate_BB1_{r}_TXT00…04` (имена — то же правило `{ЯКОРЬ}_TXT{NN}`,
#     порядок колонок документа `.JUI`: 00 Вид, 01 Ур., 02 Задание, 03 Состояние, 04 Кол-во — как у строки 1). Свойства (шрифт,
#     цвет, выравнивание, видимость, z) — копия узла строки 1 той же колонки; слот — по зоне мыши `*_HIT` этой колонки той же
#     строки (x и ширина её, y + сдвиг строки 1 «текст − зона» (+1), высота строки 1) — то есть ровно раскладка импортёра
#     `J.label_slot` (маска `.JUI` 5) для этого контрола. Текст пустой (пишет код).
#   • Количества 75256/75258: узлы `Subjugate_R_T5_QTY00/01` в рамке своей подписи (`Subjugate_R_T5_TXT00/01`); свойства — копия
#     текста ячейки золота `Subjugate_R_M1` (тот же скин), слот — тот же (иконка 32×32 в начале рамки), выравнивание — по маске
#     `.JUI` 75256/75258 (5 → по центру; у 75252 маска 4 — слева).
#   • Узел уже есть: слот/родитель совпадают — «готово»; иначе — «осмотреть» и НЕ трогать (закон owner-asset-edits-never-lose).
#   • Снимок дерева до/после (`asda_ui_text_block_align._snapshot`): потеряно 0, чужих изменений 0, новые — ровно созданные;
#     иначе исключение ДО сохранения. Бэкап `.uasset` до записи (`_backup\<дата>_board_client`). Второй прогон — 0 правок.
#
# Запуск: python Tools/run_headless_safe.py Tools/asda_ui_subjugate_rows.py --marker "ЗАВЕРШЕНО: строки доски"
#           --expect-asset /Game/AsdaAssets/UI/Player_UI/Quest/WBP_Subjugate_WIN
#   Сухой прогон: ASDA_SUBJUGATE_DRYRUN=1 (или общий ASDA_POSTSTEPS_DRYRUN=1). Соседнего `-game` быть не должно.
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ui_step_dryrun as DR      # noqa: E402  общий сухой режим
import asda_headless_tool as HT        # noqa: E402  бэкап
import asda_ui_widget_common as UWC    # noqa: E402  copy_props

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder
ASSET = "/Game/AsdaAssets/UI/Player_UI/Quest/WBP_Subjugate_WIN"
CONTENT_DIR = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Content"
DRYRUN_ENV = "ASDA_SUBJUGATE_DRYRUN"
TAG = "asda_ui_subjugate_rows"
BACKUP_SET = "board_client"
MARKER = "ЗАВЕРШЕНО: строки доски"
ROWS = 20
EPS = 0.01
TEXT_PROPS = ("font", "color_and_opacity", "justification", "auto_wrap_text", "wrap_text_at", "shadow_offset",
              "shadow_color_and_opacity", "margin", "line_height_percentage", "visibility",
              "text_overflow_policy", "render_opacity", "clipping")


def log(m):
    """Строка журнала шага."""
    unreal.log_warning("[строки доски] %s" % m)


def walk(root):
    """Имя → узел (первый по обходу)."""
    out, st = {}, [root]
    while st:
        w = st.pop()
        if w is None:
            continue
        out.setdefault(w.get_name(), w)
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                st.append(w.get_child_at(i))
    return out


def slot(w):
    """Слот канваса (None — нет)."""
    try:
        s = w.get_editor_property("slot")
    except Exception:  # noqa: BLE001
        return None
    return s if isinstance(s, unreal.CanvasPanelSlot) else None


def offsets(w):
    """(L, T, R, B) слота."""
    s = slot(w)
    if s is None:
        return None
    m = s.get_editor_property("layout_data").get_editor_property("offsets")
    return (float(m.left), float(m.top), float(m.right), float(m.bottom))


def same(a, b):
    """Совпадают ли две четвёрки."""
    return a is not None and b is not None and all(abs(x - y) < EPS for x, y in zip(a, b))


def hits(grp, row):
    """Зоны мыши колонок строки (`*_HIT`, кроме зоны самого main `Subjugate_BB1_{row}_*_HIT`) в порядке детей группы."""
    out = []
    for i in range(grp.get_children_count()):
        c = grp.get_child_at(i)
        n = c.get_name()
        if n.endswith("_HIT") and not n.startswith("Subjugate_BB1_"):
            out.append(c)
    if len(out) != 5:
        raise RuntimeError("строка %d: зон мыши колонок %d (ждали 5) — СТОП" % (row, len(out)))
    return out


def make_text(bp, parent, name, src, want, justify=None):
    """Новый TextBlock под `parent`: свойства `src`, пустой текст, слот `want` (анкоры/выравнивание/z — как у `src`)."""
    z, one = unreal.Vector2D(0, 0), unreal.Vector2D(1, 1)
    w = B.add_text(bp, parent, name, z, one, "", 0, unreal.LinearColor(1, 1, 1, 1), 0, False, False)
    if w is None:
        raise RuntimeError("не создан узел %s" % name)
    UWC.copy_props(w, src, TEXT_PROPS)
    w.set_text("")
    if justify is not None:
        w.set_editor_property("justification", justify)
    s_src, s_dst = slot(src), slot(w)
    lay = s_src.get_editor_property("layout_data")
    new = unreal.AnchorData()
    new.set_editor_property("anchors", lay.get_editor_property("anchors"))
    new.set_editor_property("alignment", lay.get_editor_property("alignment"))
    new.set_editor_property("offsets", unreal.Margin(want[0], want[1], want[2], want[3]))
    s_dst.set_editor_property("layout_data", new)
    s_dst.set_editor_property("auto_size", s_src.get_editor_property("auto_size"))
    s_dst.set_editor_property("z_order", s_src.get_editor_property("z_order"))
    return w


def plan(nodes):
    """План: список (имя, родитель, образец, слот, выравнивание|None); готово; осмотреть."""
    todo, ok, inspect = [], 0, []
    g1 = nodes.get("Subjugate_BB1_1_grp")
    if g1 is None:
        raise RuntimeError("нет группы строки 1 — СТОП")
    h1 = hits(g1, 1)
    tpl = []
    for c in range(5):
        t = nodes.get("Subjugate_BB1_1_TXT%02d" % c)
        if t is None or t.get_parent() is None or t.get_parent().get_name() != "Subjugate_BB1_1_grp":
            raise RuntimeError("нет образца Subjugate_BB1_1_TXT%02d в строке 1 — СТОП" % c)
        to, ho = offsets(t), offsets(h1[c])
        tpl.append((t, to[0] - ho[0], to[1] - ho[1], to[3]))   # образец, dx, dy, высота
    for r in range(2, ROWS + 1):
        g = nodes.get("Subjugate_BB1_%d_grp" % r)
        if g is None:
            raise RuntimeError("нет группы строки %d — СТОП" % r)
        hs = hits(g, r)
        for c in range(5):
            src, dx, dy, hh = tpl[c]
            ho = offsets(hs[c])
            want = (ho[0] + dx, ho[1] + dy, ho[2], hh)
            name = "Subjugate_BB1_%d_TXT%02d" % (r, c)
            w = nodes.get(name)
            if w is None:
                todo.append((name, g, src, want, None))
            elif w.get_parent() is None or w.get_parent().get_name() != g.get_name() or not isinstance(w, unreal.TextBlock):
                raise RuntimeError("узел %s чужого вида — СТОП" % name)
            elif same(offsets(w), want):
                ok += 1
            else:
                inspect.append("%s: слот %s ≠ расчёт %s — возможна ручная правка, НЕ трогаю" % (name, offsets(w), want))
    src_q = nodes.get("Subjugate_R_M1")
    if src_q is None:
        raise RuntimeError("нет образца количества Subjugate_R_M1 — СТОП")
    for n in range(2):
        lab = nodes.get("Subjugate_R_T5_TXT%02d" % n)
        if lab is None or lab.get_parent() is None:
            raise RuntimeError("нет подписи Subjugate_R_T5_TXT%02d — СТОП" % n)
        par = lab.get_parent()
        want = offsets(src_q)
        name = "Subjugate_R_T5_QTY%02d" % n
        w = nodes.get(name)
        if w is None:
            todo.append((name, par, src_q, want, unreal.TextJustify.CENTER))   # маска .JUI 75256/75258 = 5 (центр)
        elif same(offsets(w), want) and w.get_parent().get_name() == par.get_name():
            ok += 1
        else:
            inspect.append("%s: слот/родитель не совпали — НЕ трогаю" % name)
    return todo, ok, inspect


def main():
    """Шаг: план → (сухой — стоп) → бэкап → создание → сверка снимков → сохранение."""
    import asda_ui_text_block_align as T   # noqa: E402  общий снимок дерева
    dry = DR.is_dry(DRYRUN_ENV)
    if not EAL.does_asset_exist(ASSET):
        raise RuntimeError("нет ассета %s" % ASSET)
    bp = EAL.load_asset(ASSET)
    root = B.get_root_canvas(bp)
    todo, ok, inspect = plan(walk(root))
    for m in inspect:
        log("осмотреть: %s" % m)
    log("%sготово %d, к созданию %d, осмотреть %d" % ("[сухой] " if dry else "", ok, len(todo), len(inspect)))
    if dry or not todo:
        log("%s (правок нет%s)" % (MARKER, " — сухой прогон" if dry else ""))
        return
    f = os.path.join(CONTENT_DIR, ASSET[len("/Game/"):].replace("/", os.sep) + ".uasset")
    if HT.backup_file(f, BACKUP_SET, log=log) is None:
        raise RuntimeError("бэкап невозможен — запись запрещена")
    before = T._snapshot(root)
    made = []
    for name, par, src, want, just in todo:
        make_text(bp, par, name, src, want, just)
        made.append(name)
    after = T._snapshot(B.get_root_canvas(bp))
    lost = [n for n in before if n not in after]
    extra = [n for n in after if n not in before and n not in made]
    foreign = [n for n in before if n in after and before[n] != after[n]]
    missing = [n for n in made if n not in after]
    log("узлов до %d, после %d; создано %d; потеряно %d; лишних %d; изменено чужих %d"
        % (len(before), len(after), len(made), len(lost), len(extra), len(foreign)))
    if lost or extra or foreign or missing:
        raise RuntimeError("СВЕРКА НЕ ПРОШЛА — не сохраняю: потеряно %s, лишних %s, чужих %s, не создано %s"
                           % (lost[:5], extra[:5], foreign[:5], missing[:5]))
    rest, _ok2, _insp2 = plan(walk(B.get_root_canvas(bp)))
    if rest:
        raise RuntimeError("после создания остался план %s — не сохраняю" % [t[0] for t in rest][:5])
    B.compile_and_save(bp)
    if not DR.save(bp, dry, TAG, False):
        raise RuntimeError("не сохранён %s" % ASSET)
    unreal.log("ASDA_WRITES_ASSET: %s" % ASSET)
    log("%s (создано %d, осмотреть %d)" % (MARKER, len(made), len(inspect)))


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
