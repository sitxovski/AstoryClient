# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_fishbook_clones.py — ★★ КЛОНЫ «КНИГИ РЫБАКА» (окно FISH_BOOK_WIN 74000): 30 ячеек сетки, 4-я вкладка томов,
#   позиция панели «Удалить книгу», выравнивание/цвет текстов, которые эталон ставит кодом. Волна В3 рыбалки (08.10.2026),
#   отчёты `Reports/owner_1008/fishing_impl_W3.md`, `fishing_impl_W3fix_W6.md`; реверс `fishing_parts/ui_book.md` §3, §3.2, §4.
#
# ═══ ЧТО ДЕЛАЕТ ЭТАЛОН (`uiWindowInit_74000@0xFD2950`, game.c:610023-610359) ════════════════════════════════════════
#   1) Шаблон ячейки 74020 `FISH_BOOK_00` (рамка `Box_White1Stroke_Ot15` 100×90) и его 9 детей: для i = 0…29
#      `CControl_InstantiateChild(шаблон, 1, 74010, 0)` и по одному клону КАЖДОГО ребёнка в клон корня
#      (74019 подложка имени, 74021 имя, 74022 «?», 74024 картинка, 74023 «собрано», 74025 номер, 74026 счёт,
#      74029 рамка-хит, 74030 красная рамка). Сдвиги клонов кодом: имя y+3, номер (+1,+1), счёт (+10,+6); шаблон
#      красной рамки сдвинут (+1,+1) ДО клонирования. Позиция клона корня: `(8 + (i%5)·(100+4), 8 + (i/5)·(90+6))`
#      от 74010 — 5 колонок × 6 рядов; шаблон затем прячется (это делает КОД окна).
#   2) Вкладки томов 74105 (`TabBar_W108_3Btn`, 3 плана по 108 px с шагом 109): `uiListAppendClonedCell@0x12F5C60`
#      добавляет ячейки до 4 — копия ПОСЛЕДНЕЙ ячейки, сдвинутая на её ширину (`rc.right − rc.left` = 108; ★ fishfix 09.10:
#      ширина берётся из слота 3-й вкладки — окно расширено шагом `asda_ui_widen_windows`, вкладки 150).
#   3) Панель 74270 «Удалить книгу»: `CControl_SetField74_75(74270, 415, 703)` — позиция от корня окна (видимость
#      ставит КОД окна). ★ fishfix 09.10: + сдвиг сетки (X шаблона − 7) — при расширенном окне панель остаётся под сеткой.
#   4) ★ (ревизия В3, ВАЖН-5) Выравнивание (поле 924 = `align` разметки) и цвет (поле 923) текстов КОДОМ:
#        • клоны 74021 (имя ячейки): `SetField924(1)` (g:610166) — .JUI 5 → 1 (верх, центр);
#        • клоны 74026 (счёт «%d/%d»): `SetField924(0)` (g:610193) — .JUI 5 → 0 (верх, лево);
#        • 74081…74084 (кол-во награды): `SetField924(9)` (g:610299) — .JUI 4 → 9 (низ, центр);
#        • 74211…74240 (число стопки): `SetField924(9)` + `SetField923(-256)` = FFFFFF00 (g:610334-610335) — .JUI 4/белый → 9/жёлтый.
#      Обводка (поле 926) — шаг `asda_ui_text_style926`, клоны её наследуют через `font`.
#   Всё это — КОД, а не данные `.JUI`; импортёр строит только шаблоны и раскладывает текст по `align` разметки. Строить
#   геометрию кодом в рантайме запрещено (закон no-runtime-widget-fixes) — шаг кладёт клоны, позицию и раскладку текста
#   В АССЕТ (тот же приём, что `asda_ui_fish_ox_clones`). Раскладка текста — ТЕМ ЖЕ правилом, что у импортёра
#   (`asda_jui_parse.label_slot` / `label_justify`), с маской эталона вместо маски `.JUI`.
#
# ═══ ИМЕНА (их читает `UAsdaFishBookSubsystem::SetupControls`) ═══════════════════════════════════════════════════════
#   Ячейка i (две цифры): `FISH_BOOK_CELLii_grp` + дети `_FRAME_k`(8) `_BARbtn` `_Q` `_MARK` `_MARK_txt` `_NO` `_NO_HIT`
#   `_CNT` `_CNT_HIT` `_NAME` `_NAME_HIT` `_DONE` `_CLICK_k`(8) `_RED_grp` (+ `_RED_kk`, 16 планов).
#   Вкладка 4: `FISH_BOOK_RADIO_btn_<jui>_3`, `FISH_BOOK_RADIO_btn_OPT3`, `FISH_BOOK_RADIO_btn_Text4`.
#
# ═══ БЕЗОПАСНОСТЬ (★ ревизия В3, ВАЖН-4 — образец исправленного шага `asda_ui_fish_ox_clones`) ═══════════════════════
#   • Узел-клон есть и слот СОВПАДАЕТ с расчётным — «готово»; ОТЛИЧАЕТСЯ — «осмотреть: возможна ручная правка владельца»,
#     узел НЕ трогается (закон owner-asset-edits-never-lose; прежде шаг молча возвращал слот к расчёту). Создаются ТОЛЬКО
#     отсутствующие узлы. Чужой вид узла (класс/родитель) — СТОП без записи.
#   • `FISH_BOOK_DEL_grp`: пишется, только если текущий слот = ИМПОРТНЫЙ (корень окна + Origin `.JUI` 109,117); уже на
#     (415,703) — «готово»; иначе «осмотреть». Корень окна берётся из слота `FISH_BOOK_WIN_0_0` (МЕЛК-6: окно могли
#     расширить — закон 07.10 «окна расширять целиком»), а не константой.
#   • Тексты (п. 4): трогаются, только если слот/выравнивание/цвет в ТОЧНОСТИ в виде импортёра для маски `.JUI`; уже в виде
#     эталонной маски — «готово»; любой другой вид — «осмотреть».
#   • Снимок ВСЕГО дерева до/после (`asda_ui_text_block_align._snapshot`: класс, родитель, слот, видимость, кисть, текст,
#     цвет, выравнивание): потеряно узлов 0, изменены только перечисленные, новые — ровно созданные; иначе исключение ДО
#     сохранения. Ручной запуск пишет ассет только после бэкапа (`asda_headless_tool.backup_file`, reuse=False: первая копия набора + снимок с меткой времени, размер сверен).
#   • Второй прогон = 0 правок (идемпотентно).
#   • ★ W3fix2 (ревизия `fishing_impl_W3fix_W6_REVIEW.md`, ВАЖН-1): ОДИН прогон после переимпорта = эталонный вид. Тексты
#     клонов 74021/74026, которых ещё нет, планируются по шаблону (`plan_text_virtual`) и выкладываются в `apply` сразу
#     после создания; до сохранения — остаток по окну в памяти (`residual`) обязан быть нулевым, иначе СТОП. Прежде их
#     раскладка ждала второго прогона, а пост-импорт зовёт шаг один раз.
#   • ★ W3fix2: `verify()` — сторож пост-импорта ПО ЗНАЧЕНИЯМ (`asda_ui_post_import.VALUE_GUARDS`): отсутствующие клоны и
#     любой невыложенный текст — ошибка цепочки.
#   • Проверка на копии: `ASDA_FISHBOOK_ASSET=/Game/_tmp…/WBP_FISH_BOOK_WIN` (только под `/Game/_tmp`, бэкап не делается).
#
# Запуск: python Tools/run_headless_safe.py Tools/asda_ui_fishbook_clones.py --marker "ЗАВЕРШЕНО: клоны книги рыбака"
#   Сухой прогон: ASDA_FISHBOOK_DRYRUN=1 (или общий ASDA_POSTSTEPS_DRYRUN=1 / --dry-run).
#   Соседнего `-game` быть не должно: сохранение ассета при нём молча не проходит.
import os
import re
import sys
import traceback

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ui_step_dryrun as DR   # noqa: E402  общий сухой режим шагов пост-импорта
import asda_jui_parse as J         # noqa: E402  правило раскладки подписи импортёра (label_slot / label_justify)
import asda_headless_tool as HT    # noqa: E402  ★ Т3 (11.10.2026): единая резервная копия `backup_file`
# ★ дедуп №2 В1 Т12: общие помощники правки WBP (обход дерева, канвасный слот, смещения) — asda_ui_widget_common
import os as _uwc_os, sys as _uwc_sys
if _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)) not in _uwc_sys.path:
    _uwc_sys.path.insert(0, _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)))
import asda_ui_widget_common as _UWC   # noqa: E402

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder
ASSET = "/Game/AsdaAssets/UI/Player_UI/Fishing/WBP_FISH_BOOK_WIN"   # окно FISH_BOOK_WIN 74000
DRYRUN_ENV = "ASDA_FISHBOOK_DRYRUN"                                  # собственный ключ сухого прогона шага
TAG = "asda_ui_fishbook_clones"                                      # метка строк сухого режима
CELLS = 30                    # клонов ячейки
COLS = 5                      # колонок сетки (`i % 5`)
STEP_X = 100 + 4              # шаг по X: ширина ячейки + 4
STEP_Y = 90 + 6               # шаг по Y: высота ячейки + 6
TAB_DX = 108.0                # ширина ячейки списка в разметке импортёра (справка; сдвиг клона берётся из слота 3-й вкладки)
TPL_X0 = 7.0                  # ★ fishfix 09.10: X шаблона ячейки 74020 в 74010 у импортёра (от него — сдвиг сетки при расширении окна)
WIN_ROOT_NODE = "FISH_BOOK_WIN_0_0"   # план 0 рамки корня 74000 — его слот = левый-верх корня окна в канвасе (МЕЛК-6)
DEL_JUI = (109.0, 117.0)      # Origin 74270 в `.JUI` (ui_book §3 стр. 204) — позиция импортёра от корня окна
DEL_POS = (415.0, 703.0)      # `CControl_SetField74_75(74270, 415, 703)` — от корня окна
DEL_W_JUI = 116.0             # ★ 09.10: ширина 74270 в `.JUI` (Size 116 38) — правый край панели = DEL_POS.x + DEL_W_JUI
EPS = 0.01                    # допуск сравнения чисел слота
AREA_ROOT = r"@@ASTORY_DEV_ROOT@@"                                     # корень путей бэкапа
CONTENT_DIR = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Content"           # Content проекта (файл ассета)
# ★ Т3 (11.10.2026): набор бэкапа до записи (ручной запуск) — `@@ASTORY_DEV_ROOT@@\_backup\<дата запуска>_fishbook_clones\<путь от @@ASTORY_DEV_ROOT@@>`
#   (была прошитая папка `2026-10-08_fishing` с копией `<путь>.<время>.bak` на каждый запуск)
BACKUP_SET = "fishbook_clones"
ASSET_ENV = "ASDA_FISHBOOK_ASSET"   # ★ W3fix2: путь проверочной копии окна вместо боевого (только под TMP_PREFIX)
TMP_PREFIX = "/Game/_tmp"           # ★ W3fix2: разрешённый корень проверочных копий
YELLOW = (1.0, 1.0, 0.0, 1.0)  # FFFFFF00 (`SetField923(-256)`), sRGB = линейный для 0/1
WHITE = (1.0, 1.0, 1.0, 1.0)   # FFFFFFFF — цвет `.JUI` стопок (EXS TextColor)

IMAGE_PROPS = ("brush", "color_and_opacity", "visibility", "render_opacity", "clipping")          # свойства Image
BUTTON_PROPS = ("widget_style", "color_and_opacity", "background_color", "click_method", "touch_method",
                "press_method", "is_focusable", "visibility", "render_opacity", "clipping")        # свойства Button
TEXT_PROPS = ("font", "color_and_opacity", "justification", "auto_wrap_text", "wrap_text_at", "shadow_offset",
              "shadow_color_and_opacity", "margin", "line_height_percentage", "visibility",
              "text_overflow_policy", "render_opacity", "clipping")                                 # свойства TextBlock
CANVAS_PROPS = ("visibility", "render_opacity", "clipping")                                         # свойства CanvasPanel
JUSTIFY_ENUM = {0: "LEFT", 1: "CENTER", 2: "RIGHT"}                                                 # J.label_justify → TextJustify
# Тексты-клоны ячейки, которым эталон ставит выравнивание кодом: (суффикс текста, суффикс его плана, IDNum, маска эталона,
#   подпись). g:610166 `SetField924(1)` у клонов 74021, g:610193 `SetField924(0)` у клонов 74026 (маска `.JUI` у обоих 5).
TEXT_CLONES = (("NAME", "NAME_HIT", 74021, 1, "имя 74021"),
               ("CNT", "CNT_HIT", 74026, 0, "счёт 74026"))


def log(m):
    """Строка журнала шага."""
    unreal.log("[клоны книги рыбака] %s" % m)


def walk(root):
    """Все узлы дерева (имя → узел; первый по обходу)."""
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
    """Слот канваса узла (None — не в канвасе)."""
    try:
        s = w.get_editor_property("slot")
    except Exception:                                            # noqa: BLE001
        return None
    return s if isinstance(s, unreal.CanvasPanelSlot) else None


def offsets(w):
    """(L, T, R, B) оффсетов слота (POINT-анкер импортёра: R/B = ширина/высота)."""
    s = slot(w)
    if s is None:
        return None
    m = s.get_editor_property("layout_data").get_editor_property("offsets")
    return (float(m.left), float(m.top), float(m.right), float(m.bottom))


def set_offsets(w, want):
    """Поставить оффсеты слота."""
    s = slot(w)
    lay = s.get_editor_property("layout_data")
    lay.set_editor_property("offsets", unreal.Margin(want[0], want[1], want[2], want[3]))
    s.set_editor_property("layout_data", lay)


def same(a, b):
    """Совпадают ли две четвёрки чисел."""
    return a is not None and b is not None and all(abs(x - y) < EPS for x, y in zip(a, b))


copy_props = _UWC.copy_props   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def place_like(dst, src, dx=0.0, dy=0.0):
    """Слот `dst` = слот `src` (анкоры, выравнивание, авторазмер, z), сдвинутый на (dx, dy)."""
    s_src, s_dst = slot(src), slot(dst)
    if s_src is None or s_dst is None:
        raise RuntimeError("у %s/%s нет слота канваса" % (src.get_name(), dst.get_name()))
    lay = s_src.get_editor_property("layout_data")
    off = lay.get_editor_property("offsets")
    new = unreal.AnchorData()
    new.set_editor_property("anchors", lay.get_editor_property("anchors"))
    new.set_editor_property("alignment", lay.get_editor_property("alignment"))
    new.set_editor_property("offsets", unreal.Margin(off.left + dx, off.top + dy, off.right, off.bottom))
    s_dst.set_editor_property("layout_data", new)
    s_dst.set_editor_property("auto_size", s_src.get_editor_property("auto_size"))
    s_dst.set_editor_property("z_order", s_src.get_editor_property("z_order"))


def make_like(bp, src, parent, name):
    """Создать узел того же класса, что `src`, под `parent` и перенести его свойства."""
    z, one = unreal.Vector2D(0, 0), unreal.Vector2D(1, 1)
    if isinstance(src, unreal.CanvasPanel):
        w, props = B.add_canvas(bp, parent, name, z, one), CANVAS_PROPS
    elif isinstance(src, unreal.Button):
        w, props = B.add_button(bp, parent, name, z, one), BUTTON_PROPS
    elif isinstance(src, unreal.TextBlock):
        w, props = B.add_text(bp, parent, name, z, one, "", 0, unreal.LinearColor(1, 1, 1, 1), 0, False, False), TEXT_PROPS
    elif isinstance(src, unreal.Image):
        w = B.add_image(bp, parent, name, z, one, None, unreal.Box2D(), unreal.LinearColor(1, 1, 1, 1), False, unreal.Margin(0, 0, 0, 0))
        props = IMAGE_PROPS
    else:
        raise RuntimeError("клон класса %s не поддержан (%s)" % (type(src).__name__, src.get_name()))
    if w is None:
        raise RuntimeError("не создан узел %s" % name)
    copy_props(w, src, props)
    if isinstance(src, unreal.TextBlock):
        w.set_text(src.get_text())
    return w


def cell_map(tpl):
    """Ребёнок шаблона 74020 → (суффикс имени клона, сдвиг dx, dy). Порядок — порядок детей шаблона.
    Сдвиги — код эталона: имя 74021 y+3, номер 74025 (+1,+1), счёт 74026 (+10,+6), красная рамка 74030 (+1,+1)."""
    out = []
    frame = click = 0
    for i in range(tpl.get_children_count()):
        k = tpl.get_child_at(i)
        n = k.get_name()
        if n.startswith("FISH_BOOK_00_") and isinstance(k, unreal.Image):
            out.append((k, "FRAME_%d" % frame, 0.0, 0.0)); frame += 1
        elif n == "FISH_BOOK_BARbtn00":
            out.append((k, "BARbtn", 0.0, 0.0))
        elif n.startswith("FISH_BOOK_Q00_"):
            out.append((k, "Q", 0.0, 0.0))
        elif n.startswith("FISH_BOOK_MARK00_") and isinstance(k, unreal.Image):
            out.append((k, "MARK", 0.0, 0.0))
        elif n == "FISH_BOOK_MARK00":
            out.append((k, "MARK_txt", 0.0, 0.0))
        elif n == "FISH_BOOK_no00":
            out.append((k, "NO", 1.0, 1.0))
        elif n.startswith("FISH_BOOK_no00_"):
            out.append((k, "NO_HIT", 1.0, 1.0))
        elif n == "FISH_BOOK_NO_K00":
            out.append((k, "CNT", 10.0, 6.0))
        elif n.startswith("FISH_BOOK_NO_K00_"):
            out.append((k, "CNT_HIT", 10.0, 6.0))
        elif n == "FISH_BOOK_BAR00_TEXT00":
            out.append((k, "NAME", 0.0, 3.0))
        elif n.startswith("FISH_BOOK_BAR00_TEXT00_"):
            out.append((k, "NAME_HIT", 0.0, 3.0))
        elif n.startswith("FISH_BOOK_GFONT00_"):
            out.append((k, "DONE", 0.0, 0.0))
        elif n.startswith("FISH_BOOK_click_"):
            out.append((k, "CLICK_%d" % click, 0.0, 0.0)); click += 1
        elif n == "FISH_BOOK_REDclick_grp":
            out.append((k, "RED_grp", 1.0, 1.0))
        else:
            raise RuntimeError("у шаблона ячейки неизвестный ребёнок %s — СТОП (разметка не та, что в реверсе)" % n)
    if frame != 8 or click != 8 or len(out) != 28:   # 8 рамка + 11 одиночных + 8 хит + красная группа
        raise RuntimeError("шаблон ячейки неполон: рамка %d/8, хит %d/8, всего %d" % (frame, click, len(out)))
    return out


def text_color(w):
    """Цвет TextBlock (R, G, B, A) — указанный цвет `color_and_opacity`."""
    c = w.get_editor_property("color_and_opacity").get_editor_property("specified_color")
    return (float(c.r), float(c.g), float(c.b), float(c.a))


def justify_name(w):
    """Имя выравнивания TextBlock: LEFT / CENTER / RIGHT (сравнение с перечислением, не по строке)."""
    v = w.get_editor_property("justification")
    for nm in ("LEFT", "CENTER", "RIGHT"):
        if v == getattr(unreal.TextJustify, nm):
            return nm
    return str(v)


def label_forms(rect, cur, off, mask_old, mask_new):
    """Вид подписи импортёра для маски `.JUI` (`mask_old`) и для маски эталона (`mask_new`) у контрола `rect` (L, T, W, H)
    со смещением текста `off` (TextPos/CaptionInfo из `.JUI`, `asda_ui_text_block_align.text_off`) — правило импортёра
    `J.label_slot`. Возврат (было, станет) в той же форме (прежняя / «по низу контрола»), что совпала с текущим слотом
    `cur`; None — текущий слот не из импортёра ни для одной из масок."""
    l, t, w, h = rect
    old_forms = J.label_slot_forms(l, t, w, h, off[0], off[1], mask_old)
    new_forms = J.label_slot_forms(l, t, w, h, off[0], off[1], mask_new)
    for k in (0, 1):
        o = tuple(float(v) for v in old_forms[k])
        n = tuple(float(v) for v in new_forms[k])
        if same(o, cur) or same(n, cur):
            return o, n
    return None


class Step:
    """Состояние прогона шага: решения принимаются ДО записи (сухой проход), запись — отдельно."""

    def __init__(self, bp, dry, jui):
        self.bp = bp                                  # окно
        self.jui = jui                                # IDNum → (узел `.JUI`, смещение текста, маска `.JUI`) окна 74000
        self.dry = dry                                # сухой прогон
        self.nodes = walk(B.get_root_canvas(bp))      # имя → узел
        self.new = []                                 # имена узлов к созданию (по порядку)
        self.creates = []                             # отложенные создания: (src, parent_name, name, dx, dy, post)
        self.fixes = []                               # отложенные правки существующих узлов: (имя, функция)
        self.ok = 0                                   # узлов «готово»
        self.inspect = []                             # «осмотреть» — не трогается
        self.pending_text = []                        # ★ W3fix2: тексты клонов к созданию, которым нужна раскладка (тот же прогон)
        self.new_texts = []                           # ★ W3fix2: (txt, план, IDNum, маска .JUI, маска эталона, подпись) — для apply
        self.text_fixed_new = 0                       # ★ W3fix2: выложено текстов НОВЫХ клонов в apply
        self.fixed_now = set()                        # ★ W3fix2: правки, уже выполненные в apply (не повторять в общем цикле)

    def get(self, name):
        """Узел по имени (нет — СТОП)."""
        n = self.nodes.get(name)
        if n is None:
            raise RuntimeError("в окне нет узла %s" % name)
        return n

    def plan_clone(self, src, parent_name, name, dx, dy, post=None, slot_by_text=False):
        """Клон `src` под `parent_name` со сдвигом: есть — сверка вида и слота («готово» / «осмотреть»), нет — к созданию.
        `slot_by_text` — слот клона-текста сверяет раскладка `plan_text` (маска эталона), здесь — только вид узла."""
        w = self.nodes.get(name)
        if w is None:
            self.new.append(name)
            self.creates.append((src, parent_name, name, dx, dy, post))
            return
        par = w.get_parent()
        if type(w) is not type(src) or par is None or par.get_name() != parent_name:
            raise RuntimeError("узел %s чужого вида (класс %s, родитель %s) — СТОП" % (
                name, type(w).__name__, par.get_name() if par else "-"))
        o = offsets(src)
        want = (o[0] + dx, o[1] + dy, o[2], o[3]) if o is not None else None
        cur = offsets(w)
        if slot_by_text:
            return
        if want is not None and not same(cur, want):
            self.inspect.append("%s: слот %s ≠ расчёт %s — возможна ручная правка владельца, НЕ трогаю" % (name, cur, want))
        else:
            self.ok += 1

    def decide_text(self, name, cur, rect, j_cur, c_cur, idnum, mask_old, mask_new, color_old, color_new, label):
        """Решение по раскладке ОДНОГО текста (п. 4 шапки) по его виду: `cur` — слот текста, `rect` — слот плана контрола,
        `j_cur`/`c_cur` — выравнивание/цвет. Возврат ("ok", None) / ("inspect", текст) / ("fix", (слот, выравнивание,
        цвет, делать_слот, делать_выравнивание, делать_цвет, строка журнала)). Только решение — без записи и без счётчиков:
        им пользуются и `plan_text` (живой узел), и `plan_text_virtual` (будущий клон, ★ W3fix2 ВАЖН-1)."""
        if cur is None or rect is None:
            return "inspect", "%s (%s): нет слота текста/плана — НЕ трогаю" % (name, label)
        jn = self.jui.get(idnum)
        if jn is None or jn[2] != mask_old:
            raise RuntimeError("контрол %d в .JUI: %s, ждём маску %d (ui_book §3) — СТОП"
                               % (idnum, "нет" if jn is None else "маска %d" % jn[2], mask_old))
        offx, offy = jn[1]
        f = label_forms(rect, cur, (offx, offy), mask_old, mask_new)
        if f is None:
            return "inspect", ("%s (%s): слот %s не из импортёра для маски %d/%d (план %s, TextPos %s) — НЕ трогаю"
                               % (name, label, cur, mask_old, mask_new, rect, jn[1]))
        old_slot, new_slot = f
        j_old, j_new = JUSTIFY_ENUM[J.label_justify(mask_old)], JUSTIFY_ENUM[J.label_justify(mask_new)]
        slot_state = "new" if same(cur, new_slot) else "old"
        just_state = "new" if j_cur == j_new else ("old" if j_cur == j_old else "other")
        col_state = "new"
        if color_new is not None:
            col_state = "new" if same(c_cur, color_new) else ("old" if same(c_cur, color_old) else "other")
        if just_state == "other" or col_state == "other":
            return "inspect", "%s (%s): выравнивание %s / цвет %s — не импортёр и не эталон, НЕ трогаю" % (name, label, j_cur, c_cur)
        if slot_state == "new" and just_state == "new" and col_state == "new":
            return "ok", None
        line = "%s (%s): маска %d → %d: слот %s → %s, выравнивание %s → %s%s (смещение текста .JUI %.1f,%.1f)" % (
            name, label, mask_old, mask_new, cur, new_slot, j_cur, j_new,
            (", цвет %s → %s" % (c_cur, color_new)) if col_state == "old" else "", offx, offy)
        return "fix", (new_slot, j_new, color_new, slot_state == "old", just_state == "old", col_state == "old", line)

    def plan_text(self, text, plane, idnum, mask_old, mask_new, color_old=None, color_new=None, label=""):
        """Раскладка текста по маске эталона (п. 4 шапки). `plane` — план контрола (его слот = прямоугольник контрола),
        `idnum` — контрол `.JUI` (смещение текста и маска разметки; маска обязана совпасть с реверсом `mask_old`).
        Возврат — вид решения ("ok" / "inspect" / "fix")."""
        name = text.get_name()
        kind, data = self.decide_text(name, offsets(text), offsets(plane), justify_name(text), text_color(text),
                                      idnum, mask_old, mask_new, color_old, color_new, label)
        if kind == "inspect":
            self.inspect.append(data)
            return kind
        if kind == "ok":
            self.ok += 1
            return kind
        s_new, j_new, cn, do_slot, do_just, do_col, line = data

        def fix(w=text, s_new=s_new, jn=j_new, cn=cn, do_slot=do_slot, do_just=do_just, do_col=do_col):
            """Запись решения в узел: слот / выравнивание / цвет."""
            if do_slot:
                set_offsets(w, s_new)
            if do_just:
                w.set_editor_property("justification", getattr(unreal.TextJustify, jn))
            if do_col:
                w.set_editor_property("color_and_opacity", unreal.SlateColor(unreal.LinearColor(*cn)))
        self.fixes.append((name, fix))
        log("%s%s" % ("[сухой] " if self.dry else "", line))
        return kind

    def plan_text_virtual(self, name, tpl_text, tpl_plane, shift, idnum, mask_old, mask_new, label):
        """★ W3fix2 (ревизия `fishing_impl_W3fix_W6_REVIEW.md`, ВАЖН-1). Решение по тексту клона `name`, которого ЕЩЁ НЕТ:
        клон = копия шаблона `tpl_text` (свойства и слот, `make_like` + `place_like`), его план — копия `tpl_plane`, оба
        сдвинуты на один и тот же `shift` (код эталона). Правило импортёра `J.label_slot` переносимо (x = l+offx,
        y = t+offy+…, обрезка по t+h−y), поэтому решение по шаблону, сдвинутому на `shift`, = решение по будущему клону.
        Нужно сухому прогону (честный счёт правок) и сверке `verify`. Запись делает `apply` по ЖИВОМУ клону."""
        def sh(o):
            """Сдвиг четвёрки оффсетов на `shift`."""
            return None if o is None else (o[0] + shift[0], o[1] + shift[1], o[2], o[3])
        kind, data = self.decide_text(name, sh(offsets(tpl_text)), sh(offsets(tpl_plane)), justify_name(tpl_text),
                                      text_color(tpl_text), idnum, mask_old, mask_new, None, None, label)
        if kind == "fix":
            self.pending_text.append(name)
            log("%s%s [клон будет создан этим же прогоном]" % ("[сухой] " if self.dry else "", data[6]))
        elif kind == "inspect":
            self.inspect.append(data + " [шаблон: клон после создания останется в виде шаблона]")
        return kind

    def run(self):
        """Решения по всем узлам шага (без записи)."""
        gbox = self.get("FISH_BOOK_GBOX_1_grp")
        tpl = self.get("FISH_BOOK_00_grp")
        if tpl.get_parent() != gbox:
            raise RuntimeError("шаблон FISH_BOOK_00_grp не в FISH_BOOK_GBOX_1_grp — СТОП")
        cmap = cell_map(tpl)
        for i in range(CELLS):
            pre = "FISH_BOOK_CELL%02d_" % i
            dx = float(STEP_X * (i % COLS))
            dy = float(STEP_Y * (i // COLS))
            self.plan_clone(tpl, gbox.get_name(), pre + "grp", dx, dy)
            for (k, suf, kx, ky) in cmap:
                self.plan_clone(k, pre + "grp", pre + suf, kx, ky, slot_by_text=suf in ("NAME", "CNT"))
                if suf == "RED_grp":
                    for r in range(k.get_children_count()):
                        self.plan_clone(k.get_child_at(r), pre + "RED_grp", pre + "RED_%02d" % r, 0.0, 0.0)

        # ── 4-я вкладка: копия последней (3-й) ячейки со сдвигом на её ширину ──
        plane2 = None
        for n, w in self.nodes.items():
            m = re.match(r"^FISH_BOOK_RADIO_btn_(\d+)_2$", n)
            if m and isinstance(w, unreal.Image):
                plane2 = (n, w, m.group(1))
        if plane2 is None:
            raise RuntimeError("нет плана FISH_BOOK_RADIO_btn_<jui>_2 — СТОП")
        tab = [(plane2[1], "FISH_BOOK_RADIO_btn_%s_3" % plane2[2]),
               (self.get("FISH_BOOK_RADIO_btn_OPT2"), "FISH_BOOK_RADIO_btn_OPT3"),
               (self.get("FISH_BOOK_RADIO_btn_Text3"), "FISH_BOOK_RADIO_btn_Text4")]
        for src, name in tab:
            # ★ fishfix 09.10 (ревизия `fishing_REVIEW.md` МЕЛК-4): сдвиг = ширина ПОСЛЕДНЕЙ ячейки `rc.right − rc.left`, как у
            #   `uiListAppendClonedCell@0x12F5C60` — из её слота, а не константой 108: шаг `asda_ui_widen_windows` (идёт раньше)
            #   расширяет вкладки книги 108 → 150, и клон обязан лечь вплотную за расширенной 3-й вкладкой.
            o = offsets(src)
            self.plan_clone(src, src.get_parent().get_name(), name, o[2] if o is not None else TAB_DX, 0.0, post="tab")

        # ── Панель 74270: позиция (415,703) от корня окна; пишется только поверх ИМПОРТНОЙ позиции ──
        root_c = B.get_root_canvas(self.bp)
        dl = self.get("FISH_BOOK_DEL_grp")
        if dl.get_parent() != root_c:
            raise RuntimeError("FISH_BOOK_DEL_grp не в корне окна — СТОП")
        win = offsets(self.get(WIN_ROOT_NODE))
        if win is None:
            raise RuntimeError("у %s нет слота — корень окна не определить, СТОП" % WIN_ROOT_NODE)
        cur = offsets(dl)
        imp = (win[0] + DEL_JUI[0], win[1] + DEL_JUI[1], cur[2], cur[3])
        # ★ fishfix 09.10 (МЕЛК-4): окно расширено целиком — сетка по центру своей части сдвинута в 74010 на (X шаблона − 7);
        #   панель «Удалить книгу» стоит под сеткой (эталон: 415 от корня = правый край сетки) и едет с ней на тот же сдвиг.
        to = offsets(tpl)
        grid_dx = (to[0] - TPL_X0) if to is not None else 0.0
        # ★ 09.10.2026 (мультиязычность п.5, шаг `asda_ui_widen_windows`, идёт РАНЬШЕ): панель шире под английскую подпись
        #   «Delete fishing guide» растёт ВЛЕВО — её правый край остаётся у эталонной точки (415 + ширина .JUI 116); при
        #   ширине .JUI формула прежняя (левый край = 415).
        want = (win[0] + DEL_POS[0] + grid_dx + DEL_W_JUI - cur[2], win[1] + DEL_POS[1], cur[2], cur[3])
        if same(cur, want):
            self.ok += 1
        elif same(cur, imp):
            log("%sFISH_BOOK_DEL_grp: слот %s → %s (эталон `SetField74_75(74270, 415, 703)`, корень окна %s)" % (
                "[сухой] " if self.dry else "", cur, want, win[:2]))
            self.fixes.append(("FISH_BOOK_DEL_grp", lambda w=dl, s=want: set_offsets(w, s)))
        else:
            self.inspect.append("FISH_BOOK_DEL_grp: слот %s — не импортный %s и не эталонный %s, возможна ручная правка владельца, "
                                "НЕ трогаю" % (cur, imp, want))

        # ── Тексты, которым эталон ставит выравнивание/цвет кодом (ВАЖН-5 ревизии В3) ──
        for k in range(1, CELLS + 1):                           # 74211…74240: align 9 + FFFFFF00
            txt = self.nodes.get("FISH_BOOK_JOINBOX_M%d" % k)
            pl = self.find_plane("FISH_BOOK_JOINBOX_M%d_" % k)
            if not isinstance(txt, unreal.TextBlock) or pl is None or pl.get_parent() != txt.get_parent():
                self.inspect.append("стопка %d (%d): нет пары текст/план в одном родителе — НЕ трогаю" % (k, 74210 + k))
                continue
            self.plan_text(txt, pl, 74210 + k, 4, 9, WHITE, YELLOW, "стопка %d" % (74210 + k))
        rew = 0
        planes = []                                             # 74081…74084: align 9 (порядок импорта = порядок IDNum)
        for n, pl in self.nodes.items():
            m = re.match(r"^FISH_BOOK_BIG_BOX_3_MARK_[124]_(\d+)_0$", n)
            if m and isinstance(pl, unreal.Image):
                planes.append((int(m.group(1)), n, pl))
        for t, (_j, n, pl) in enumerate(sorted(planes)):
            par = pl.get_parent()
            txt = None
            for c in range(par.get_children_count()):
                ch = par.get_child_at(c)
                if isinstance(ch, unreal.TextBlock) and ch.get_name().startswith("FISH_BOOK_BIG_BOX_3_MARK_") \
                        and not ch.get_name().startswith("FISH_BOOK_BIG_BOX_3_MARK_A"):
                    txt = ch
            if txt is None:
                self.inspect.append("награда %s: нет текста количества в %s — НЕ трогаю" % (n, par.get_name()))
                continue
            rew += 1
            self.plan_text(txt, pl, 74081 + t, 4, 9, None, None, "кол-во награды %d (%s)" % (74081 + t, n))
        if rew != 4:
            self.inspect.append("наград найдено %d (ждём 4: 74081…74084)" % rew)
        tplk = dict((suf, (k, kx, ky)) for (k, suf, kx, ky) in cmap)    # суффикс клона → (узел шаблона, сдвиг)
        for i in range(CELLS):                                  # клоны 74021 (align 1) и 74026 (align 0)
            pre = "FISH_BOOK_CELL%02d_" % i
            for suf, hit, idn, mask_new, what in TEXT_CLONES:
                label = "%s ячейки %d" % (what, i)
                txt, pl = self.nodes.get(pre + suf), self.nodes.get(pre + hit)
                if txt is not None and pl is not None:
                    self.plan_text(txt, pl, idn, 5, mask_new, None, None, label)
                    continue
                # ★ W3fix2 ВАЖН-1: клона ещё нет — его создаст `apply` ЭТОГО ЖЕ прогона и сразу выложит (`new_texts`).
                #   Прежде раскладка откладывалась «до второго прогона», а пост-импорт зовёт шаг ОДИН раз — после
                #   переимпорта окна 60 подписей оставались в маске `.JUI` 5.
                if (pre + suf) not in self.new or (pre + hit) not in self.new:
                    raise RuntimeError("%s/%s: одного узла нет и он не в плане создания — СТОП" % (pre + suf, pre + hit))
                (kt, tx, ty), (kp, px, py) = tplk[suf], tplk[hit]
                if abs(tx - px) > EPS or abs(ty - py) > EPS:
                    raise RuntimeError("сдвиги текста %s и плана %s клона разные — расчёт по шаблону неверен, СТОП" % (suf, hit))
                self.plan_text_virtual(pre + suf, kt, kp, (tx, ty), idn, 5, mask_new, label)
                self.new_texts.append((pre + suf, pre + hit, idn, 5, mask_new, label))

    def find_plane(self, prefix):
        """План импортёра `<prefix><juiIdx>_0` (Image)."""
        for n, w in self.nodes.items():
            if n.startswith(prefix) and n.endswith("_0") and isinstance(w, unreal.Image) and n[len(prefix):-2].isdigit():
                return w
        return None

    def apply(self):
        """Запись: создание отсутствующих клонов и правки, решённые в `run`."""
        made = []
        for src, parent_name, name, dx, dy, post in self.creates:
            parent = self.nodes.get(parent_name)
            if parent is None:
                raise RuntimeError("родитель %s клона %s не найден — СТОП" % (parent_name, name))
            w = make_like(self.bp, src, parent, name)
            place_like(w, src, dx, dy)
            self.nodes[name] = w
            made.append(name)
            if post == "tab":
                if isinstance(w, unreal.TextBlock):
                    w.set_text("")
                par = src.get_parent()
                idx = -1
                for c in range(par.get_children_count()):
                    if par.get_child_at(c) == src:
                        idx = c
                B.move_child_to_index(par, w, idx + 1)   # рисуется сразу за своим образцом (порядок канваса)
        # ★ W3fix2 ВАЖН-1: раскладка текстов ТОЛЬКО ЧТО созданных клонов — по живым узлам, в этом же прогоне. Решение по
        #   шаблону (`plan_text_virtual`) обязано совпасть: «осмотреть» у свежего клона или «готово» там, где ждали правку, —
        #   ошибка расчёта, СТОП до сохранения.
        for name, hit, idn, m_old, m_new, label in self.new_texts:
            txt, pl = self.nodes.get(name), self.nodes.get(hit)
            if txt is None or pl is None:
                raise RuntimeError("клон %s/%s не создан — СТОП" % (name, hit))
            n_fix = len(self.fixes)
            kind = self.plan_text(txt, pl, idn, m_old, m_new, None, None, label)
            want = "fix" if name in self.pending_text else "ok"
            if kind != want:
                raise RuntimeError("раскладка нового клона %s: вышло «%s», по шаблону ждали «%s» — СТОП" % (name, kind, want))
            if kind == "fix":
                self.fixes[n_fix][1]()
                self.text_fixed_new += 1
                self.fixed_now.add(name)
        for name, fn in self.fixes:
            if name not in self.fixed_now:
                fn()
        return made


def jui_map(T):
    """IDNum → (узел, смещение текста (x, y), маска выравнивания) контролов окна 74000 из того же `.ini`, что читает импортёр;
    смещение и маска — общими правилами импортёра (`asda_ui_text_block_align.text_off` / `text_mask`)."""
    roots = J.parse_jui(T._ini())
    skins = J.parse_skins_full()
    win = J.find_by_idnum(roots, 74000)
    if win is None:
        raise RuntimeError("в %s нет окна 74000 — СТОП" % T._ini())
    out = {}
    for n in T._walk_nodes(win):
        if n.idnum:
            stype = (skins.get((n.name or "").lower()) or {}).get("type", "")
            out[int(n.idnum)] = (n, tuple(float(v) for v in T.text_off(n, stype == "Text")), int(T.text_mask(n)))
    return out


def backup_asset():
    """Бэкап файла ассета до записи (закон backup-before-edit) — `asda_headless_tool.backup_file` (★ Т3, 11.10.2026).
    reuse=False сохраняет прежний смысл «копия на каждый запуск»: первая копия набора (исходник до первой правки) не
    трогается, рядом — снимок `<копия>.<ГГГГММДД_ЧЧММСС>.bak`; размер сверен. Возвращает путь копии/снимка."""
    f = os.path.join(CONTENT_DIR, ASSET[len("/Game/"):].replace("/", os.sep) + ".uasset")
    bak = HT.backup_file(f, BACKUP_SET, reuse=False, log=log)
    if bak is None:
        raise RuntimeError("нет файла ассета %s — бэкап невозможен, запись запрещена" % f)
    return bak


def asset_path():
    """Путь окна. ★ W3fix2: `ASDA_FISHBOOK_ASSET` — ТОЛЬКО проверочная копия под `/Game/_tmp` (сухая/мокрая проверка шага на
    копии, не трогая боевой ассет); любой другой путь — СТОП."""
    p = str(os.environ.get(ASSET_ENV, "")).strip()
    if not p:
        return ASSET
    if not p.startswith(TMP_PREFIX) or not p.endswith("/WBP_FISH_BOOK_WIN"):
        raise RuntimeError("%s=%s: разрешена только копия %s…/WBP_FISH_BOOK_WIN — СТОП" % (ASSET_ENV, p, TMP_PREFIX))
    return p


def residual(bp, jui):
    """★ W3fix2 ВАЖН-1: остаток работы шага по окну `bp` (без записи) — состояние `Step` после `run` в сухом режиме. После
    `apply` обязан быть нулевым: тот же прогон доводит окно до эталонного вида, второй прогон не нужен."""
    st = Step(bp, True, jui)
    st.run()
    return st


def verify():
    """★ W3fix2 ВАЖН-1 — СТОРОЖ ПОСТ-ИМПОРТА ПО ЗНАЧЕНИЯМ (зовёт `asda_ui_post_import._run_chain` после шагов). Сторож HANDMADE
    видит только наличие узлов; невыложенную подпись (слот/выравнивание/цвет в виде импортёра `.JUI` вместо маски эталона)
    ловит только этот. Возврат — список проблем (пусто = окно в эталонном виде): отсутствующие клоны и любой текст,
    которому нужна раскладка. «Осмотреть» (возможная ручная правка владельца) — предупреждение, не проблема."""
    import asda_ui_text_block_align as T   # noqa: E402  правила смещения/маски `.JUI`
    path = asset_path()
    if not EAL.does_asset_exist(path):
        return ["%s: нет ассета" % path]
    st = residual(EAL.load_asset(path), jui_map(T))
    for m in st.inspect:
        unreal.log_warning("[клоны книги рыбака] сверка: осмотреть: %s" % m)
    out = []
    if st.new:
        out.append("%s: нет %d узлов клонов (%s…)" % (path, len(st.new), ", ".join(st.new[:5])))
    if st.fixes or st.pending_text:
        names = [n for n, _ in st.fixes] + st.pending_text
        out.append("%s: %d узлов НЕ в эталонном виде — тексты не выложены по маске эталона / панель 74270 не на месте (%s…) — "
                   "прогнать шаг asda_ui_fishbook_clones"
                   % (path, len(names), ", ".join(names[:5])))
    unreal.log("[клоны книги рыбака] сверка: готово %d, к созданию %d, правок %d, осмотреть %d"
               % (st.ok, len(st.new), len(st.fixes) + len(st.pending_text), len(st.inspect)))
    return out


def main(standalone=False):
    """Шаг пост-импорта. `standalone` — ручной запуск: бэкап ассета обязателен (ревизия В3 ВАЖН-4)."""
    import asda_ui_text_block_align as T   # noqa: E402  общий снимок дерева (соседние шаги STEPS)
    dry = DR.is_dry(DRYRUN_ENV)
    path = asset_path()
    if not EAL.does_asset_exist(path):
        raise RuntimeError("нет ассета %s" % path)
    if path != ASSET:
        log("★ ПРОВЕРОЧНАЯ КОПИЯ %s (боевой %s не трогается)" % (path, ASSET))
    bp = EAL.load_asset(path)
    jui = jui_map(T)
    st = Step(bp, dry, jui)
    st.run()
    for m in st.inspect:
        log("осмотреть: %s" % m)
    n_fix = len(st.fixes) + len(st.pending_text)     # ★ W3fix2: и раскладка текстов клонов, которые создаст этот прогон
    log("%sготово %d, к созданию %d, правок %d (из них текстов новых клонов %d), осмотреть %d" % (
        "[сухой] " if dry else "", st.ok, len(st.new), n_fix, len(st.pending_text), len(st.inspect)))
    if dry or (not st.new and not st.fixes):
        unreal.log("%s %s: создано %d, поправлено %d, осмотреть %d%s" % (DR.MARK, TAG, 0, 0, len(st.inspect),
                   " (СУХОЙ ПРОГОН: создалось бы %d, правок %d)" % (len(st.new), n_fix) if dry else ""))
        log("ЗАВЕРШЕНО: клоны книги рыбака (правок нет%s)" % (" — сухой прогон" if dry else ""))
        return
    if standalone and path == ASSET:
        backup_asset()
    root = B.get_root_canvas(bp)
    before = T._snapshot(root)
    made = st.apply()
    after = T._snapshot(B.get_root_canvas(bp))
    fixed = set(n for n, _ in st.fixes)
    lost = [n for n in before if n not in after]
    extra = [n for n in after if n not in before and n not in made]
    foreign = [n for n in before if n in after and before[n] != after[n] and n not in fixed]
    missing = [n for n in made if n not in after]
    log("узлов до %d, после %d; создано %d; поправлено %d (из них текстов новых клонов %d); потеряно узлов: %d; лишних: %d; "
        "изменено чужих: %d" % (len(before), len(after), len(made), len(fixed), st.text_fixed_new, len(lost), len(extra),
                                len(foreign)))
    if lost or extra or foreign or missing:
        raise RuntimeError("СВЕРКА НЕ ПРОШЛА — не сохраняю: потеряно %s, лишних %s, изменено чужих %s, не создано %s"
                           % (lost[:10], extra[:10], foreign[:10], missing[:10]))
    if st.text_fixed_new != len(st.pending_text):
        raise RuntimeError("выложено текстов новых клонов %d, по плану %d — не сохраняю" % (st.text_fixed_new, len(st.pending_text)))
    # ★ W3fix2 ВАЖН-1: ОДИН прогон — эталонный вид. Остаток по окну в памяти (до сохранения) обязан быть нулевым.
    rest = residual(bp, jui)
    log("остаток после правок (в памяти, до сохранения): к созданию %d, правок %d, осмотреть %d"
        % (len(rest.new), len(rest.fixes) + len(rest.pending_text), len(rest.inspect)))
    if rest.new or rest.fixes or rest.pending_text:
        raise RuntimeError("после правок остался невыполненный план (создать %s, правки %s) — не сохраняю"
                           % (rest.new[:5], [n for n, _ in rest.fixes][:5] + rest.pending_text[:5]))
    B.compile_and_save(bp)   # ⚠ только компилирует и метит пакет — пишет `save`
    if not DR.save(bp, dry, TAG, False):
        raise RuntimeError("не сохранён %s" % path)
    unreal.log("ASDA_WRITES_ASSET: %s" % path)
    unreal.log("%s %s: создано %d, поправлено %d, осмотреть %d" % (DR.MARK, TAG, len(made), len(fixed), len(st.inspect)))
    log("ЗАВЕРШЕНО: клоны книги рыбака (создано %d, поправлено %d, осмотреть %d)" % (len(made), len(fixed), len(st.inspect)))


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    try:
        main(standalone=True)
    except Exception:                                            # noqa: BLE001
        unreal.log_error("[клоны книги рыбака] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
