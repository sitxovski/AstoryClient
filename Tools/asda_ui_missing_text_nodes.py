# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_missing_text_nodes.py — ★★★ НЕДОСТАЮЩИЕ ТЕКСТОВЫЕ УЗЛЫ (класс дефектов S1a).
#
# ПРОБЛЕМА (аудит `Reports/audit_runtime_vs_assets_SUMMARY.md`, строка 33/66).
#   Импортёр `asda_jui_generic_umg.py` брал текст подписи ТОЛЬКО из `CaptionText`. Поэтому у
#   контрола, у которого одновременно
#       • `ID = ""`            (именованная ветка импортёра требует `idc` — не срабатывает),
#       • `CaptionText = ""`   (caption-ветка требует непустой `cap` — не срабатывает),
#       • `EXS TextContent`    НЕПУСТОЙ,
#   в собранном `WBP_*` не появлялось НИ ОДНОГО узла с текстом. По данным (мой пересчёт, метод
#   ниже) таких контролов **209 в 19 окнах**, все — класс `gamemain`; в `commonwin`, `temp`,
#   `login` — ни одного. Из них у 128 контролов (`NoSelect = "1"`) в ассете нет вообще никакого
#   виджета, у оставшихся 81 есть только невидимая клик-зона `*_HIT`.
#
# ЧТО ЭТО ЗА ТЕКСТ (не выдумка, эталон). `CTextControl_LoadProps` (`RE/etalon/font_text.c:9721-9735`)
#   читает `TextContent` и отдаёт его В САМ КОНТРОЛ через `CControl_SetText` — то есть это
#   НАСТОЯЩИЙ текст Text-контрола, а не дизайнерский комментарий. `CaptionText` — отдельное поле,
#   его рисует `cControl_RenderLabel` (`ui_core.c:110741`). Текст берётся ИЗ ДАННЫХ и проходит тот
#   же `fix_text`, что и подписи импортёра (cp949 → словарь `kor_ru_ui.json`).
#   ⚠ `LoadProps` умеет ещё склейку `13 13` → перевод строки. В нашем дампе `.ini` таких
#     последовательностей НЕТ (проверено: число `\r` в файле РАВНО числу `\r\n`), поэтому
#     преобразование здесь не нужно.
#
# ЧТО ДЕЛАЕТ ШАГ (идемпотентно: существующий узел не трогает и не дублирует):
#   кладёт В СУЩЕСТВУЮЩИЙ ассет `TextBlock` с именем по ОБЩЕМУ правилу
#   `asda_jui_parse.missing_text_nodes` — `{ЯКОРЬ-ID-предка}_TXT{NN}` (у контрола своего `ID` нет,
#   поэтому имя строится от родителя — так же поступили с ячейками сумок 5…8 склада).
#   ★ ТО ЖЕ правило теперь применяет и сам импортёр (правка `asda_jui_generic_umg.py`, 2026-08-27),
#     поэтому после перегенерации узлы уже на месте и шаг отрабатывает вхолостую.
#
# ГЕОМЕТРИЯ И ЦВЕТ — ИЗ ДАННЫХ, ровно по формулам импортёра (`add_control`):
#   позиция  = rect контрола (минус origin контейнера) + `EXS TextPos`, плюс вертикальная
#              центровка по маске `EXS TextAlign` (0x4 середина, 0x8 низ) с допущением строки 14 px;
#   размер   = (max(w − TextPos.x, 8), max(h, 14)); перенос строк — только при h ≥ 28
#              (закон `ui-autowrap-canvas-gotcha`);
#   выравн.  = маска `EXS TextAlign` (0x1 центр, 0x2 право);
#   цвет     = `EXS TextColor` (закон `ui-text-color-exs`), sRGB→linear делает парсер.
#   Обводка НЕ ставится: по таблице `Tools/data/text_style926.tsv` из 208 наших `IDNum` в ней
#   присутствует ровно ОДИН (2581), т.е. оригинал этим контролам стиль не назначает.
#
# КУДА КЛАДЁТСЯ УЗЕЛ. В ТУ ЖЕ панель, куда импортёр положил бы собственные виджеты контрола:
#   контрол С ДЕТЬМИ живёт в своей группе `{...}_grp`, лист — в группе РОДИТЕЛЯ (а если родитель —
#   корень окна, то прямо в корневом канвасе). Группа ищется НЕ по имени (имена групп в данных
#   массово дублируются: в `SHELLITEM_STORE2_WIN` по три группы `NoImage` подряд), а по ЯКОРНОМУ
#   виджету соседа/самого контрола: имя такого виджета содержит ПОРЯДКОВЫЙ ИНДЕКС обхода
#   (`base = san(ID|Name) + "_" + индекс`), а он в окне уникален. Найденная панель дополнительно
#   ПРОВЕРЯЕТСЯ по числам слота (см. `_check_panel`).
#
# ★ Мышь узлы НЕ ловят (`SelfHitTestInvisible`) — иначе подпись съест клики по кнопке под ней
#   (законы `selfhittest-vs-hittest-invisible`, `ui-button-hover-click-rule`).
#
# Запуск:      run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_ui_missing_text_nodes.py
#   сухой:     set ASDA_MISSTXT_DRYRUN=1
#   подробно:  set ASDA_MISSTXT_VERBOSE=1
import importlib.util
import json
import os
import re
import traceback

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
INI_DIR = r"@@ASTORY_DEV_ROOT@@\Data\Decoded\jui"
SKIN_INI = r"@@ASTORY_DEV_ROOT@@\RE\docs\JuiSkin.ini"
UI_ROOT = "/Game/AsdaAssets/UI"
TRANS_JSON = os.path.join(HERE, "kor_ru_ui.json")

# Классы .JUI и приоритет владения именем при коллизии (как в импортёре, `_PRIORITY`).
CLASSES = {"gamemain": "GameMain", "commonwin": "CommonWin", "temp": "Temp", "login": "Login"}
PRIORITY = ["gamemain", "commonwin", "temp", "login"]

FONT_PX = 8               # кегль UI импортёра (`asda_jui_generic_umg.FONT_PX`)
GEN_TEXT_LINE_PX = 14     # допущение о высоте строки (`asda_jui_generic_umg.GEN_TEXT_LINE_PX`)
BASE_W, BASE_H = 1024.0, 768.0     # база раскладки JUI

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder

_spec = importlib.util.spec_from_file_location("J_misstxt", os.path.join(HERE, "asda_jui_parse.py"))
J = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(J)

_TRANS = {}
if os.path.isfile(TRANS_JSON):
    try:
        _TRANS = json.load(open(TRANS_JSON, encoding="utf-8"))
    except Exception:                                            # noqa: BLE001
        _TRANS = {}


def log(m):
    unreal.log("[S1a-текст] %s" % m)


def _login_blocked(path):
    """★ 15.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 14): True — окно `path` принадлежит сцене логина (asda_login_guard: UI/Login, кроме
    LOGIN_SHARED_EXCEPTIONS), а ASDA_ALLOW_LOGIN_WRITE != 1: окно пропустить, НЕ загружая. Без строки на каждое окно (quiet):
    итог [LOGIN-GUARD] печатает вызывающий. Ленивый импорт — при импорте модуля ничего не делает."""
    import sys as _sys_lg
    _here = os.path.dirname(os.path.abspath(__file__))   # Tools — для asda_login_guard
    if _here not in _sys_lg.path:
        _sys_lg.path.insert(0, _here)
    from asda_login_guard import package_blocked_for   # ★ дедуп №2 Т18: имя инструмента — из __file__
    return package_blocked_for(__file__, path, quiet=True)


def san(s):
    """Санитайзер имени — как `asda_jui_generic_umg.san`."""
    return re.sub(r"[^A-Za-z0-9_]", "_", (s or "").strip()) or "win"


def fix_text(s):
    """КОПИЯ `asda_jui_generic_umg.fix_text` (EN как есть, cp949-корейский → словарь перевода).
    ⚠ ОБЩАЯ ТОЧКА: меняете там — меняйте и здесь, иначе текст узлов разъедется с импортёром."""
    if not s:
        return ""
    b = s.encode("cp1251", "replace")
    if all(x < 0x80 for x in b):
        return s
    kr = b.decode("cp949", "replace")
    key = kr.strip().rstrip("~").strip()
    return _TRANS.get(key, _TRANS.get(kr.strip(), kr))


def ini_of(cls):
    """Путь .ini класса — как `asda_jui_generic_umg._ini_of` (приоритет локализованного)."""
    p = os.path.join(INI_DIR, "%s_loc.ini" % cls)
    return p if os.path.isfile(p) else os.path.join(INI_DIR, "%s_jui.ini" % cls)


def subtree_bounds(n):
    """КОПИЯ `asda_jui_generic_umg._subtree_bounds`: габариты узла И ВСЕХ потомков."""
    l, t, w, h = n.abs
    minx, miny, maxx, maxy = l, t, l + w, t + h
    for c in n.children:
        cx0, cy0, cx1, cy1 = subtree_bounds(c)
        minx = min(minx, cx0); miny = min(miny, cy0)
        maxx = max(maxx, cx1); maxy = max(maxy, cy1)
    return minx, miny, maxx, maxy


def index_nodes(root):
    """Пре-проход по окну: pre-order индекс узла (тот же счётчик `i[0]`, что у импортёра в
    `build_tree`: корень 0, дальше обход в глубину слева направо), родитель и признак «скрыт»."""
    order, parent, hidden = {}, {}, {}
    ctr = [0]

    def walk(n, par, hid):
        order[id(n)] = ctr[0]; ctr[0] += 1
        parent[id(n)] = par
        eff = hid or (par is not None and getattr(n, "visible", 1) == 0)
        hidden[id(n)] = eff
        for c in n.children:
            walk(c, n, eff)
    walk(root, None, False)
    return order, parent, hidden


# ---------------------------------------------------------------------------
# Поиск панели-контейнера в собранном ассете
# ---------------------------------------------------------------------------
def widget_index(canvas):
    """имя виджета → (виджет, родительская панель). Имена в пределах WBP уникальны."""
    wid, par = {}, {}

    def walk(w, p):
        try:
            nm = w.get_name()
        except Exception:                                        # noqa: BLE001
            return
        wid[nm] = w; par[nm] = p
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                c = w.get_child_at(i)
                if c:
                    walk(c, w)
    walk(canvas, None)
    return wid, par


def own_widget(node, order, wid):
    """Любой виджет, который импортёр создал ДЛЯ САМОГО контрола `node` (не для его детей).

    Имена таких виджетов: `base_<план>` / `base_txt` / `base_HIT` / `base_DRAG`, где
    `base = san(ID|Name) + "_" + pre-order-индекс` (`add_control`), либо — у интерактива —
    `san(ID)`, `san(ID)_Text`, `san(ID)_OPT<i>`. Индекс делает `base` уникальным в окне.
    """
    base = "%s_%d" % (san(node.idc or node.name), order[id(node)])
    rx = re.compile(r"^%s(?:_.+)?$" % re.escape(base))
    for nm, w in wid.items():
        if rx.match(nm):
            return w
    if node.idc:
        s = san(node.idc)
        for nm in (s, s + "_Text", s + "_OPT0"):
            if nm in wid:
                return wid[nm]
    return None


def group_panel(node, order, wid, par):
    """Панель-группа узла `node` (у узла есть дети ⇒ импортёр завёл ему `{...}_grp`).

    Ищем через ЯКОРНЫЙ виджет: собственный виджет узла лежит В его группе; виджет листа-ребёнка —
    тоже; группа ребёнка-с-детьми лежит в ней же, значит нужен её РОДИТЕЛЬ.
    """
    w = own_widget(node, order, wid)
    if w is not None:
        return par.get(w.get_name())
    for c in node.children:
        if c.children:
            g = group_panel(c, order, wid, par)
            if g is not None:
                return par.get(g.get_name())
        else:
            cw = own_widget(c, order, wid)
            if cw is not None:
                return par.get(cw.get_name())
    return None


def container_of(node, parent_of, order, wid, par, canvas):
    """(панель, ox, oy) — куда и с каким origin импортёр кладёт СОБСТВЕННЫЕ виджеты `node`."""
    if node.children:                       # узел сам стал группой; origin = union его поддерева
        gx0, gy0, _gx1, _gy1 = subtree_bounds(node)
        return group_panel(node, order, wid, par), gx0, gy0
    p = parent_of[id(node)]
    if p is None:                           # корень окна — его дети живут прямо в канвасе
        return canvas, 0.0, 0.0
    if parent_of[id(p)] is None:            # родитель — корень: `build_tree(..., is_root=True)`
        return canvas, 0.0, 0.0
    gx0, gy0, _gx1, _gy1 = subtree_bounds(p)
    return group_panel(p, order, wid, par), gx0, gy0


def slot_offsets(w):
    try:
        s = w.get_editor_property("slot")
    except Exception:                                            # noqa: BLE001
        return None
    if not isinstance(s, unreal.CanvasPanelSlot):
        return None
    return s.get_editor_property("layout_data").get_editor_property("offsets")


def _check_panel(panel, node, parent_of, order, canvas):
    """ВТОРОЙ МЕТОД: сверка найденной панели по ЧИСЛАМ слота.

    Группа не верхнего уровня кладётся в `V2(gx0 − ox_род, gy0 − oy_род)` размером с union
    поддерева (`build_tree`). Верхнеуровневой группе `_apply_anchor` ПЕРЕПИСЫВАЕТ offsets, поэтому
    её не сверяем (возвращаем `None` = «сверка не применима»). Возврат: True/False/None.
    """
    if panel is None or panel == canvas:
        return None
    holder = node if node.children else parent_of[id(node)]
    if holder is None:
        return None
    p = parent_of[id(holder)]
    if p is None or parent_of[id(p)] is None:
        return None                          # группа верхнего уровня — числа переписал анкор
    gx0, gy0, gx1, gy1 = subtree_bounds(holder)
    px0, py0, _px1, _py1 = subtree_bounds(p)
    off = slot_offsets(panel)
    if off is None:
        return None
    exp = (gx0 - px0, gy0 - py0, max(gx1 - gx0, 1), max(gy1 - gy0, 1))
    got = (off.left, off.top, off.right, off.bottom)
    return all(abs(a - b) < 0.6 for a, b in zip(exp, got))


def apply_anchor(wgt, node):
    """КОПИЯ `asda_jui_generic_umg._apply_anchor` — пер-контрольный анкор доли экрана.
    Нужен ТОЛЬКО листам верхнего уровня (их импортёр кладёт прямо в канвас и анкорит)."""
    a = getattr(node, "anchor", None)
    if not a:
        return
    try:
        slot = wgt.slot
    except Exception:                                            # noqa: BLE001
        return
    if not isinstance(slot, unreal.CanvasPanelSlot):
        return
    off = slot.get_offsets()
    wl, wt, ww, wh = off.left, off.top, off.right, off.bottom
    if a["xspan"]:
        axmin, axmax, offL, offR = wl / BASE_W, (wl + ww) / BASE_W, 0.0, 0.0
    else:
        fx = a["fx"]; axmin = axmax = fx; offL = wl - fx * BASE_W; offR = ww
    if a["yspan"]:
        aymin, aymax, offT, offB = wt / BASE_H, (wt + wh) / BASE_H, 0.0, 0.0
    else:
        fy = a["fy"]; aymin = aymax = fy; offT = wt - fy * BASE_H; offB = wh
    try:
        slot.set_anchors(unreal.Anchors(unreal.Vector2D(axmin, aymin), unreal.Vector2D(axmax, aymax)))
        slot.set_offsets(unreal.Margin(offL, offT, offR, offB))
        slot.set_alignment(unreal.Vector2D(0.0, 0.0))
    except Exception:                                            # noqa: BLE001
        pass


def text_geometry(node, ox, oy, root):
    """(pos, size, halign, wrap) по формулам импортёра `add_control` для Text-скина.
    ★ 07.10.2026: прямоугольник — ОБЩАЯ ТОЧКА `J.label_slot` (узел, вылезший бы за низ рамки окна `root`, — по низу
    контрола; откат импортёра `ASDA_LABEL_BAND_LEGACY=1` действует и здесь), иначе узел S1a разошёлся бы с импортёром.
    ★ 07.10.2026 (ревизия label_band МЕЛК-4): `root` (корень окна .JUI) ОБЯЗАТЕЛЕН. Без рамки `label_slot` молча
    применил бы общее правило «всегда по низу контрола» (`frame_bottom=None`) — 6 282 узла вместо узлов за рамкой;
    так и была допущена ошибка, пойманная первым сухим прогоном. Поэтому `None` — исключение, а не умолчание."""
    if root is None:
        raise ValueError("text_geometry: не передан root (корень окна .JUI) — без рамки окна правило подписи "
                         "стало бы общим «по низу контрола»; передайте корень окна")
    l, t, w, h = node.abs
    l -= ox; t -= oy
    # ★ 07.10.2026 (ревизия `poststeps_align_fix_REVIEW.md`, МЕЛК-2): маска и горизонталь — ОБЩАЯ ТОЧКА импортёра
    #   `J.label_mask` / `J.label_justify`, а не своя копия правила.
    m = J.label_mask(node)
    halign = J.label_justify(m)
    offx, offy = node.txt_pos if node.txt_pos is not None else node.cap_off
    fb = float(root.abs[1] + root.abs[3]) - oy      # низ рамки окна в координатах родителя узла
    x, y, tw, th = J.label_slot(l, t, w, h, offx, offy, m,
                                legacy=os.environ.get("ASDA_LABEL_BAND_LEGACY", "") == "1", frame_bottom=fb)
    pos = unreal.Vector2D(x, y)
    size = unreal.Vector2D(tw, th)
    return pos, size, halign, (h >= 28)


def text_color(node):
    c = node.txt_color if node.txt_color else node.cap_color
    return unreal.LinearColor(c[0], c[1], c[2], c[3])


# ---------------------------------------------------------------------------
# Карта ассетов и коллизии имён окон
# ---------------------------------------------------------------------------
def asset_map():
    out = {}
    for a in EAL.list_assets(UI_ROOT, recursive=True, include_folder=False):
        p = a.split(".")[0]
        out.setdefault(p.rsplit("/", 1)[-1], []).append(p)
    return out


def collisions(all_roots):
    """san(имя окна) → список классов, где оно объявлено (как `_scan_collisions` импортёра)."""
    out = {}
    for cls, roots in all_roots.items():
        for r in roots:
            out.setdefault(san(r.idc or r.name), set()).add(cls)
    return out


def wsuffix(base, cls, coll):
    owners = coll.get(base) or set()
    if len(owners) < 2:
        return ""
    main = sorted(owners, key=lambda c: PRIORITY.index(c) if c in PRIORITY else 99)[0]
    return "" if cls == main else "_" + CLASSES[cls]


# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# ★★★ КЛАСС S1b — ИМЕНОВАННЫЙ УЗЕЛ ЕСТЬ, А ТЕКСТ В НЁМ ПУСТ
# ---------------------------------------------------------------------------
#   ТОТ ЖЕ дефект, что S1a, но у контрола `ID` НЕПУСТ. Импортёр создаёт для него `TextBlock`
#   с именем `san(ID)` (ветка `stype == "Text"`) либо `san(ID)_Text` (подпись кнопки/окна), а
#   ТЕКСТ до правки 2026-08-27 брал ТОЛЬКО из `CaptionText`. У контрола с пустым `CaptionText`
#   и живым `EXS TextContent` узел приезжал ПУСТЫМ — виджет есть, надписи нет.
#   ЭТО И ЕСТЬ жалоба владельца 2026-08-28 (пп. 4 и 7 по окну аукциона 13000): живой дамп
#   `Reports/exchange_texts.tsv` — `EXCH_PRICETEXT` / `EXCH_TIMETEXT` / `EXCH_FEETEXT` /
#   `EXCHANGER_BUYPRICE1` / `EXCHANGER_GOLD` / `EXCH_CASHTEXT` / `EXCH_TOTALFEETEXT` —
#   `TEXT=[]` при непустом `TextContent` в `gamemain_jui.ini` (13012/13013/13014/13471/13474/
#   13008/13055). Перегенерация окна запрещена, поэтому текст доносит этот шаг.
#
#   ★★★ ПОЧЕМУ СПИСОК, А НЕ «ВСЕ ПОДРЯД». Сухой прогон по всем четырём `.ini` даёт **1915 таких
#   узлов в 176 окнах** (`Reports/headless_misstxt_dry4.log`), но ЗАЛИВАТЬ ИХ ВСЕ НЕЛЬЗЯ:
#   в `TextContent` у ЗНАЧЕНИЙ лежит дизайнерская РЫБА, а иногда и рабочие пометки автора —
#   в окне 13000 это `EXCH_ITEMPRICE01 = "9,999,999"`, `EXCHMY_NAME01 = "АБВГДЕЁЖЗИЙКЛМНА"`,
#   `EXCHANGER_PAGE1 = "999"`, `EXCH_ITEM_Msg03 = "А на японском не переполнится ли…"`.
#   Оригинал их действительно рисует (`LoadProps` → `CControl_SetText`) и тут же перекрывает
#   рантаймом, но в порте часть таких полей рантайм не трогает — на экране остался бы мусор.
#   ⇒ заполняем ПО ИМЕНОВАННОМУ СПИСКУ статических подписей; общий класс дефекта зарегистрирован
#   в `Docs/Port/PORT_PENDING_ON_SYSTEMS.md` и раскрывается по окнам, по мере их проверки владельцем.
#
#   ★ ПРАВИЛО БЕЗОПАСНОСТИ: заполняем ТОЛЬКО узлы, у которых текст в ассете ПУСТ. Ничего уже
#   написанного (в т.ч. правок владельца и текстов рантайма) шаг не перезаписывает ⇒ он
#   идемпотентен и не может дать регресса в других окнах.
#
# (имя WBP, {имена узлов}) — только СТАТИЧЕСКИЕ подписи, не поля значений.
S1B_ALLOW = {
    # ★★★ ОКНО АУКЦИОНА 13000 УБРАНО 2026-08-28 И ЭТО НЕ ОТКАТ, А ИСПРАВЛЕНИЕ ИСТОЧНИКА.
    #   Разбор `invForgeDialogInit@0xD450B0` (`item_inventory.c:47295-47353`), его близнеца
    #   `consignRegWnd_InitControls@0xD448A0` (`ui_core.c:50881-50931`), `shop_trade.c:12809-12841`
    #   и `uiInitDialogControls` (`ui_core.c:52028-52056`) показал: ВСЕ статические подписи трёх
    #   панелей оригинал ставит В РАНТАЙМЕ по id локализации (12500…12519, 2289, 5035, 12722), а
    #   корейский `EXS TextContent` в `.JUI` — заготовка дизайнера. Тексты из таблицы отличаются
    #   от перевода заготовки по существу: 12504 = «Налог» (а не «Комиссия»), 12505 = «Общий налог
    #   на продажу», 12506 = «Отменить продажу». Две подписи оригинал ВООБЩЕ ГАСИТ:
    #   13300…13304 `EXCH_ITEM_Msg0N` (там литерал «[]») и 13126 `Exchange_RemoveinKorea`.
    #   ⇒ источник — `UAsdaExchangeSubsystem::InitStaticCaptions`, и второй копии в ассете быть
    #   не должно. Для других окон список остаётся рабочим механизмом.
}


def named_empty_text_nodes(root):
    """[(node, имя-кандидат1, имя-кандидат2, текст)] для контролов с непустым `ID`, у которых
    в данных есть текст (`CaptionText` ИЛИ `EXS TextContent`)."""
    out = []

    def walk(n):
        if n.idc:
            cap = (n.caption or "").strip()
            txt = (n.txt_content or "").strip() if n.txt_content else ""
            src = cap or txt
            if src:
                s = san(n.idc)
                out.append((n, s, s + "_Text", fix_text(src)))
        for c in n.children:
            walk(c)
    walk(root)
    return out


def fill_named_texts(all_roots, coll, amap, dry, verbose):
    """Второй проход шага (класс S1b). Возвращает (заполнено, окон затронуто, кандидатов всего).

    `кандидатов всего` — сколько узлов КЛАССА S1b нашлось по данным вообще (диагностика охвата
    дефекта); заполняются из них только перечисленные в `S1B_ALLOW`."""
    filled = wins = cand = 0
    login_skipped = []   # ★ 15.09.2026 (раздел 14): окна сцены логина, пропущенные защитой
    for cls in PRIORITY:
        for root in all_roots.get(cls, []):
            entries = named_empty_text_nodes(root)
            if not entries:
                continue
            base = san(root.idc or root.name)
            wname = "WBP_" + base + wsuffix(base, cls, coll)
            allow = S1B_ALLOW.get(wname)
            paths = amap.get(wname) or []
            if not paths:
                continue
            if _login_blocked(paths[0]):   # ★ 15.09.2026 (раздел 14): окно сцены логина — только с разрешением
                login_skipped.append(wname)
                continue
            bp = EAL.load_asset(paths[0])
            canvas = B.get_root_canvas(bp) if bp else None
            if canvas is None:
                continue
            wid, _par = widget_index(canvas)
            here = 0
            for _node, n1, n2, want in entries:
                if not want:
                    continue
                for nm in (n1, n2):
                    w = wid.get(nm)
                    if not isinstance(w, unreal.TextBlock):
                        continue
                    try:
                        cur = str(w.get_text())
                    except Exception:                            # noqa: BLE001
                        continue
                    if cur.strip():
                        continue                                 # уже есть текст — НЕ ТРОГАЕМ
                    cand += 1
                    if verbose:
                        log("  %s%s / %-28s ← %r"
                            % ("+" if (allow and nm in allow) else " ", wname, nm, want[:32]))
                    if not allow or nm not in allow:
                        continue                                 # не в списке статических подписей
                    if not dry:
                        # ★ UE-Python принимает и `unreal.Text`, и обычную строку — как в
                        #   `asda_ui_handmade._apply_text`, пробуем обе формы.
                        try:
                            w.set_editor_property("text", unreal.Text(want))
                        except Exception:                        # noqa: BLE001
                            w.set_editor_property("text", want)
                    filled += 1
                    here += 1
            if here and not dry:
                B.compile_and_save(bp)
                if not EAL.save_loaded_asset(bp, False):
                    raise RuntimeError("не сохранён %s" % paths[0])
            if here:
                wins += 1
                log("%-34s тексты подписей: %d" % (wname, here))
    if login_skipped:   # ★ 15.09.2026 (раздел 14): итог пропуска окон сцены логина одной строкой
        unreal.log_warning("[LOGIN-GUARD] [S1b-текст] окон сцены логина пропущено: %d (%s) — правка только с явного разрешения "
                           "владельца и ASDA_ALLOW_LOGIN_WRITE=1" % (len(login_skipped), ", ".join(login_skipped)))
    return filled, wins, cand


def main():
    dry = bool(os.environ.get("ASDA_MISSTXT_DRYRUN"))
    verbose = bool(os.environ.get("ASDA_MISSTXT_VERBOSE"))
    amap = asset_map()

    all_roots = {}
    for cls in PRIORITY:
        ini = ini_of(cls)
        if os.path.isfile(ini):
            all_roots[cls] = J.parse_jui(ini)
        else:
            log("НЕТ .ini класса %s (%s)" % (cls, ini))
    coll = collisions(all_roots)

    planned = added = existed = 0
    wins_touched = 0
    no_asset, no_panel, bad_check = [], [], []
    login_skipped = []   # ★ 15.09.2026 (раздел 14): окна сцены логина, пропущенные защитой

    for cls in PRIORITY:
        for root in all_roots.get(cls, []):
            entries = J.missing_text_nodes(root)
            if not entries:
                continue
            planned += len(entries)
            base = san(root.idc or root.name)
            wname = "WBP_" + base + wsuffix(base, cls, coll)
            paths = amap.get(wname) or []
            if not paths:
                no_asset.append("%s (%s, %d контролов)" % (wname, cls, len(entries)))
                continue
            path = paths[0]
            if _login_blocked(path):   # ★ 15.09.2026 (раздел 14): окно сцены логина — только с разрешением
                login_skipped.append(wname)
                continue
            bp = EAL.load_asset(path)
            canvas = B.get_root_canvas(bp) if bp else None
            if canvas is None:
                no_asset.append("%s: нет корневого канваса" % path)
                continue

            J.compute_anchors(root)
            order, parent_of, hidden = index_nodes(root)
            wid, par = widget_index(canvas)
            made_here = 0

            for node, _p, name in entries:
                if name in wid:
                    existed += 1
                    continue
                panel, ox, oy = container_of(node, parent_of, order, wid, par, canvas)
                if panel is None:
                    no_panel.append("%s / %s (IDNum %d)" % (wname, name, node.idnum))
                    continue
                chk = _check_panel(panel, node, parent_of, order, canvas)
                if chk is False:
                    bad_check.append("%s / %s: числа слота панели %s не сошлись"
                                     % (wname, name, panel.get_name()))
                    continue
                pos, size, halign, wrap = text_geometry(node, ox, oy, root)
                txt = fix_text(node.txt_content or "")
                if verbose:
                    log("  %s / %-28s → %-28s (%.0f,%.0f %.0fx%.0f) al=%d wrap=%d %r"
                        % (wname, name, panel.get_name(), pos.x, pos.y, size.x, size.y,
                           halign, int(wrap), txt[:24]))
                if dry:
                    added += 1
                    continue
                tb = B.add_text(bp, panel, name, pos, size, txt, FONT_PX, text_color(node),
                                halign, wrap, True)
                if not tb:
                    no_panel.append("%s / %s: AddText вернул None" % (wname, name))
                    continue
                wid[name] = tb; par[name] = panel
                # ★ Лист ВЕРХНЕГО уровня импортёр анкорит (`build_tree`, `is_top`) — повторяем.
                if (not node.children) and parent_of[id(node)] is not None \
                        and parent_of[id(parent_of[id(node)])] is None:
                    apply_anchor(tb, node)
                # ★ Подпись не ловит мышь (как пост-цикл `add_control`).
                try:
                    tb.set_editor_property("visibility", unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE)
                except Exception:                                # noqa: BLE001
                    pass
                # Контрол (или его предок) объявлен `Visible = "0"` — импортёр сворачивает.
                if hidden[id(node)]:
                    try:
                        tb.set_editor_property("visibility", unreal.SlateVisibility.COLLAPSED)
                    except Exception:                            # noqa: BLE001
                        pass
                added += 1
                made_here += 1

            if made_here and not dry:
                B.compile_and_save(bp)
                if not EAL.save_loaded_asset(bp, False):
                    raise RuntimeError("не сохранён %s" % path)
            if made_here:
                wins_touched += 1
                log("%-34s + %d узлов" % (wname, made_here))

    for m in no_asset:
        unreal.log_warning("[S1a-текст] нет ассета: %s" % m)
    for m in no_panel:
        unreal.log_warning("[S1a-текст] не найдена панель: %s" % m)
    for m in bad_check:
        unreal.log_error("[S1a-текст] СВЕРКА ПАНЕЛИ НЕ ПРОШЛА: %s" % m)
    if login_skipped:   # ★ 15.09.2026 (раздел 14): итог пропуска окон сцены логина одной строкой
        unreal.log_warning("[LOGIN-GUARD] [S1a-текст] окон сцены логина пропущено: %d (%s) — правка только с явного разрешения "
                           "владельца и ASDA_ALLOW_LOGIN_WRITE=1" % (len(login_skipped), ", ".join(login_skipped)))
    # ★★★ ВТОРОЙ ПРОХОД — класс S1b: узел ЕСТЬ, текст в нём ПУСТ (см. `fill_named_texts`).
    s1b_filled, s1b_wins, s1b_cand = fill_named_texts(all_roots, coll, amap, dry, verbose)

    log("ГОТОВО%s: по данным нужно %d узлов, создано %d, уже было %d, окон затронуто %d, "
        "без ассета %d, без панели %d, сверка не прошла %d; S1b (пустой текст у существующего "
        "узла): кандидатов по данным %d, заполнено по списку %d в %d окнах"
        % ("  [СУХОЙ ПРОГОН]" if dry else "", planned, added, existed, wins_touched,
           len(no_asset), len(no_panel), len(bad_check), s1b_cand, s1b_filled, s1b_wins))
    if bad_check:
        raise RuntimeError("панель-контейнер не подтвердилась у %d узлов" % len(bad_check))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает —
#   раннер пост-импорта (`asda_ui_post_import.py`) сам зовёт `main()`.
if __name__ == "__main__":
    try:
        main()
    except Exception:                                            # noqa: BLE001
        unreal.log_error("[S1a-текст] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
        raise
