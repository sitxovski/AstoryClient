# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_option_extra_rows.py — ★★★ ФАЗА 2 ОКНА НАСТРОЕК 69500 (`WBP_SYSTEM_Option_NEW`): НОВЫЕ СТРОКИ В АССЕТЕ
#   (19.09.2026, план `opt_plan/PLAN.md`, решения владельца №1–№5).
#
# ═══ ЧТО И ЗАЧЕМ ═══════════════════════════════════════════════════════════════════════════════════
#   Владелец велел вынести игроку 23 настройки из dev-окна Ctrl+N и показать их ИГРОВЫМИ контролами этого же
#   окна. Состав и геометрия строк СТАТИЧНЫ (от сервера и игрока не зависят) — значит место им в АССЕТЕ
#   (решение владельца 27.08.2026 «статика живёт в виджете», закон `no-runtime-widget-fixes`); код ставит лишь
#   подписи из loc и состояние. Каждый новый узел — КЛОН существующего узла ЭТОГО окна (ничего не подбирается):
#     • список      ← строка «Разрешение» `NoImage_grp_12` (подпись 70051 + рамка 70052 + комбо 70053);
#     • флажок      ← строка «Полный экран» `NoImage_grp_10` (70054/70055) на «Экране» — чтобы строка стояла
#                     в той же колонке «подпись слева, контрол справа», что и её соседи; на «Эффектах» —
#                     3D-строка `Box_Check_grp` (70101/70102), как и сами 3D-флажки оригинала;
#     • полоса      ← «Яркость» `NoImage_grp_11` (70057/70058, скин `SideBar_Scr`) — ПОСЛЕ починки её раскладки
#                     шагом `asda_ui_option_layout_fix.py` (иначе клоны вышли бы растянутыми);
#     • кнопка      ← «По умолчанию» `SYSTEM_default` + `SYSTEM_default_Text` (69651, скин WkBtn_W100);
#     • под-страница← панель `SYSTEM_Op_SYSTEM_3d_OP_grp` (рамка Box_White3Stroke_BlackFill 502×404) и её
#                     внутренняя группа `SYSTEM_O3_SYSTEM_SYS33_grp` — БЕЗ строк;
#     • ячейка списка под-страниц ← ячейка 0 списка 70006 (+25 px на ячейку, как клон эталона
#                     `uiListAppendClonedCell`) — ячейки 3 «Качество» и 4 «Эффекты»;
#     • поле клавиши ← `PICK_BACK_grp_83` (+19 px) — строка «Стойка на руках» (действие 85): в оригинале
#                     поля нет, это [ИСПРАВЛЕНИЕ ОРИГИНАЛА].
#   Шаг строк 25 px (как у эталонных строк «Экрана» и 3D). Списки открываются ВНИЗ: при высоте панели 404
#   ни один не выходит за её низ. Раскрытый список должен лечь ПОВЕРХ строк ниже себя, поэтому строки
#   добавляются в порядке «снизу вверх» (у канваса порядок детей = порядок рисования).
#
# ═══ ★★★ 12.10.2026 СОКРАЩЕНИЕ СОСТАВА 23 → 16 (вердикт дизайнера `Reports/owner_1012/presets/01_designer.md` §1
#   «Итог по составу» + уточнения владельца: ползунок дальности Q9 ОСТАВИТЬ, DX12/DX11 оставить, блики убрать совсем) ═══
#   Сняты строки: «Качество» Q5 «Растительность», Q6 «Детализация вдали» (ступени — остаётся ползунок Q9), Q8 «Глобальное
#   освещение»; «Эффекты» E3 «Анизотропная фильтрация», E4 «Затенение окружения», E6 «Блики объектива». Оставшиеся строки
#   обеих под-страниц сдвинуты ВВЕРХ без дыр (шаг 25 px): «Качество» Q0 4 · Q1 30 · Q2 55 · Q3 80 · Q4 105 · Q7 130 ·
#   Q9 155; «Эффекты» E1 4 · E2 30 · E5 55 · E7 80. Порядок детей (рисования) не меняется — раскрытые списки по-прежнему
#   ложатся поверх строк ниже. Loc-строки снятых пунктов (9903xx) и поля профиля не трогаются (код окна их больше не читает).
#   ОБРАТНАЯ ОПЕРАЦИЯ «снять строку» (`Step.remove_row`): дети строки удаляются поимённо (по той же таблице имён `TPL`, что
#   их создала), затем корень `SYSTEM_OX_<тег>_grp` — штатным `RemoveWidgetByName` (чистит GUID-таблицу WBP, как Delete
#   редактора). Узел-ребёнок, которого нет в таблице имён, — СТОП без сохранения (возможна правка владельца). Идемпотентно:
#   снятых строк нет → 0 правок; сдвиг — только если верх строки не на месте. Сверка дампов «до/после» учитывает три
#   ожидаемых разряда изменений: снятые узлы (ровно список), сдвинутые корни строк (только off(...)) и новые узлы.
#   Перед записью — резервная копия .uasset (`asda_headless_tool.backup_file`, набор `sysopt_cut`, закон backup-before-edit).
#
# ═══ ЧТО МЕНЯЕТСЯ У ПРЕЖНИХ УЗЛОВ (ровно это и ничего больше) ═══════════════════════════════════════
#   • `NoImage_grp_10` (строка чекбокса «Полный экран») → Collapsed: её заменил список «Режим экрана»
#     (решение владельца №2), две ручки одной настройки противоречили бы друг другу;
#   • `NoImage_grp_11` (строка «Яркость») → SelfHitTestInvisible: в данных .JUI у 70057 `Visible = "0"`, и
#     оригинал её не показывает, но по плану фазы 1 яркость остаётся в окне игрока (dev-строка «Гамма»
#     сведена в неё — один источник). [ПОРТ]
#   • страница «Эмоции»: `Box_WhiteOt20_193_0`, `NoImage_grp_7`, `SYSTEM_Op_HOTKEYS_FEEL_grp` — высота +19 px,
#     иначе новое поле «Стойка на руках» вылезло бы за рамку группы;
#   • порядок детей `SYSTEM_Op_SYSTEM_SYS22_grp` (новые строки встают перед строками «Яркость»/«Разрешение»,
#     чтобы раскрытые списки ложились поверх) — сами узлы не меняются.
#
# ═══ ПРОВЕРКА ПОСЛЕ ЗАПИСИ ═════════════════════════════════════════════════════════════════════════
#   Дамп окна (`AsdaUmgBuilder.DumpWidgetTree`) ДО и ПОСЛЕ сравнивается построчно: прежние узлы обязаны совпасть
#   байт в байт, кроме перечисленных выше (и у них — только в перечисленном поле); новые — ровно ожидаемые.
#   Расхождение — исключение, сохранения нет. Второй прогон = 0 правок (идемпотентно).
#
# Запуск (редактор ЗАКРЫТ, окон не показывать):
#   python Tools\run_headless_safe.py Tools\asda_ui_option_extra_rows.py --marker "НОВЫЕ СТРОКИ 69500: ГОТОВО"
#   Сухой прогон: set ASDA_OPTEXTRA_DRYRUN=1
import json
import os
import re
import traceback

import unreal
# ★ дедуп №2 В1 Т12: общие помощники правки WBP (обход дерева, канвасный слот, смещения) — asda_ui_widget_common
import os as _uwc_os, sys as _uwc_sys
if _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)) not in _uwc_sys.path:
    _uwc_sys.path.insert(0, _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)))
import asda_ui_widget_common as _UWC   # noqa: E402
import asda_headless_tool as HT         # noqa: E402  ★ 12.10.2026: единая резервная копия .uasset (`backup_file`)

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder

ASSET = "/Game/AsdaAssets/UI/Player_UI/System/WBP_SYSTEM_Option_NEW"
CONTENT_DIR = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Content"   # корень Content проекта (файл .uasset для бэкапа)
BACKUP_SET = "sysopt_cut"                                 # набор бэкапа: @@ASTORY_DEV_ROOT@@\_backup\<дата>_sysopt_cut\<путь>
MANIFEST = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "asda_ui_option_extra_rows.json")

IMAGE_PROPS = ("brush", "color_and_opacity", "visibility", "render_opacity", "clipping")
BUTTON_PROPS = ("widget_style", "color_and_opacity", "background_color", "click_method", "touch_method",
                "press_method", "is_focusable", "visibility", "render_opacity", "clipping")
TEXT_PROPS = ("font", "color_and_opacity", "justification", "auto_wrap_text", "shadow_offset",
              "shadow_color_and_opacity", "margin", "line_height_percentage", "visibility",
              "text_overflow_policy", "render_opacity", "clipping")
CHECK_PROPS = ("widget_style", "click_method", "touch_method", "press_method", "is_focusable", "visibility",
               "render_opacity", "clipping")
COMBO_PROPS = ("widget_style", "item_style", "font", "foreground_color", "visibility", "render_opacity", "clipping")
CANVAS_PROPS = ("visibility", "render_opacity", "clipping")

VIS_ROW = unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE

# ── Строки: (тег, вид, группа-родитель, Y строки, база номеров планов, подпись в ассете) ──
#   Подпись в ассете — только для чтения в редакторе; в игре её ставит код из loc (наши 9903xx).
SCREEN = "SYSTEM_Op_SYSTEM_SYS22_grp"
QUALITY_INNER = "SYSTEM_Op_SYSTEM_SYS44_grp"
EFFECTS_INNER = "SYSTEM_Op_SYSTEM_SYS55_grp"
ROWS_SCREEN = [   # порядок рисования: снизу вверх
    ("S6", "note",  SCREEN, 180, 0,    "Графический режим сменится после перезапуска игры."),
    ("S5", "combo", SCREEN, 155, 9150, "Графический режим"),
    ("S4", "check", SCREEN, 130, 9140, "Вертикальная синхронизация"),
    ("S3", "combo", SCREEN, 105, 9130, "Ограничение кадров"),
    ("S2", "combo", SCREEN, 80,  9120, "Масштаб рендера"),
]
ROW_MODE = ("S1", "combo", SCREEN, 30, 9110, "Режим экрана")   # над «Яркостью», под «Разрешением»
# ★ 12.10.2026: состав после сокращения (см. шапку). Y — верх строки в группе; строки без дыр, шаг 25 px.
ROWS_QUALITY = [
    ("Q9", "slider", QUALITY_INNER, 155, 9290, "Дальность прорисовки"),
    ("Q7", "combo",  QUALITY_INNER, 130, 9270, "Отражения"),
    ("Q4", "combo",  QUALITY_INNER, 105, 9240, "Постобработка"),
    ("Q3", "combo",  QUALITY_INNER, 80,  9230, "Эффекты"),
    ("Q2", "combo",  QUALITY_INNER, 55,  9220, "Тени"),
    ("Q1", "combo",  QUALITY_INNER, 30,  9210, "Текстуры"),
    ("Q0", "combo",  QUALITY_INNER, 4,   9200, "Общее качество"),
]
ROWS_EFFECTS = [
    ("E7", "check3d", EFFECTS_INNER, 80,  0,    "Объёмные облака"),
    ("E5", "check3d", EFFECTS_INNER, 55,  0,    "Размытие в движении"),
    ("E2", "combo",   EFFECTS_INNER, 30,  9320, "Резкость"),
    ("E1", "combo",   EFFECTS_INNER, 4,   9310, "Сглаживание"),
]
# ★ 12.10.2026 СНЯТЫЕ строки: (тег, вид, группа-родитель, база номеров планов) — ровно те параметры, по которым `TPL`
#   давал имена детей при создании; по ним же дети находятся и удаляются (`Step.remove_row`). Прежние Y для истории:
#   Q5 130, Q6 155, Q8 205 («Качество»); E3 55, E4 80, E6 130 («Эффекты»).
REMOVED_ROWS = [
    ("Q8", "combo",   QUALITY_INNER, 9280),   # «Глобальное освещение» — ступень неразличима, цена 0…3.5 мс неустойчива; ставит пресет
    ("Q6", "combo",   QUALITY_INNER, 9260),   # «Детализация вдали» (ступени) — владелец оставил одну ручку: ползунок Q9
    ("Q5", "combo",   QUALITY_INNER, 9250),   # «Растительность» — sg.FoliageQuality на ISM-мир не действует (perf2k §2.6)
    ("E6", "check3d", EFFECTS_INNER, 0),      # «Блики объектива» — решение владельца: убрать совсем (поле профиля и миграция остаются)
    ("E4", "check3d", EFFECTS_INNER, 0),      # «Затенение окружения» — на High/Max при Lumen флажок ничего не делает; ставит уровень
    ("E3", "combo",   EFFECTS_INNER, 9330),   # «Анизотропная фильтрация» — слита в группу «Текстуры» (уровень)
]

# Шаблоны строк: корень + явная таблица «имя ребёнка шаблона → имя клона» (n = база номеров строки).
TPL = {
    "combo": ("NoImage_grp_12", 4, lambda tag, n: {
        "SYSTEM_Op_SYSTEM_SYS22_TXT02": "SYSTEM_OX_%s_TXT" % tag,
        "NoImage_243_HIT": "NoImage_%d_HIT" % n,
        **{"Box_White1Stroke_Ot15_244_%d" % i: "Box_White1Stroke_Ot15_%d_%d" % (n + 1, i) for i in range(8)},
        "Bk_ComboBox_245_0": "Bk_ComboBox_%d_0" % (n + 2),
        "Bk_ComboBox_245_1": "Bk_ComboBox_%d_1" % (n + 2),
        "SYSTEM_Op_SYSTEM_SYS22_CMB00": "SYSTEM_OX_%s_CMB" % tag,
        "Bk_ComboBox_245_txt": "Bk_ComboBox_%d_txt" % (n + 2),
    }),
    "check": ("NoImage_grp_10", 30, lambda tag, n: {
        "SYSTEM_Op_SYSTEM_SYS22_TXT00": "SYSTEM_OX_%s_TXT" % tag,
        "NoImage_239_HIT": "NoImage_%d_HIT" % n,
        "SYSTEM_Op_SYSTEM_SYS22_CHK00": "SYSTEM_OX_%s_CHK" % tag,
    }),
    "slider": ("NoImage_grp_11", 55, lambda tag, n: {
        "SYSTEM_Op_SYSTEM_SYS22_TXT01": "SYSTEM_OX_%s_TXT" % tag,
        "NoImage_241_HIT": "NoImage_%d_HIT" % n,
        **{"SideBar_Scr_242_%d" % i: "SideBar_Scr_%d_%d" % (n + 2, i) for i in range(4)},
    }),
    "check3d": ("Box_Check_grp", 5, lambda tag, n: {
        "SYSTEM_O3_SYSTEM_SYS33_CHK00": "SYSTEM_OX_%s_CHK" % tag,
        "SYSTEM_O3_SYSTEM_SYS33_TXT00": "SYSTEM_OX_%s_TXT" % tag,
    }),
}

# Прежние узлы, которые шаг МЕНЯЕТ (узел → поле дампа, которое вправе измениться).
TOUCH_VIS = {"NoImage_grp_10": unreal.SlateVisibility.COLLAPSED, "NoImage_grp_11": VIS_ROW}
TOUCH_GROW = ("Box_WhiteOt20_193_0", "NoImage_grp_7", "SYSTEM_Op_HOTKEYS_FEEL_grp")   # высота +19
FEEL_ROW_STEP = 19.0


def step_visibility_nodes():
    """★ 07.10.2026 (ревизия `Reports/owner_1007/label_band_fixes_REVIEW.md`, МЕЛК-1): узлы, видимость которых
    ВЕДЁТ этот шаг. Читает `asda_ui_sync_own_visibility` — их он не раскрывает и не гасит.
    Возврат — [(путь окна, узел, причина)] по `TOUCH_VIS` (без загрузки ассетов)."""
    return [(ASSET, n, "TOUCH_VIS: строку заменил список «Режим экрана» / сдвиг строк фазы 2 (вид %s)" % v)
            for n, v in TOUCH_VIS.items()]


def log(m):
    unreal.log("[новые строки 69500] %s" % m)


walk = _UWC.walk_by_name   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def dump_map(bp):
    out = {}
    for raw in B.dump_widget_tree(bp).split("\n"):
        line = raw.strip()
        if not line or line.startswith("==="):
            continue
        parts = line.split(None, 1)
        if len(parts) == 2:
            out[parts[0]] = " ".join(parts[1].split())
    return out


def slot(w):
    s = w.get_editor_property("slot")
    return s if isinstance(s, unreal.CanvasPanelSlot) else None


def offsets(w):
    return slot(w).get_editor_property("layout_data").get_editor_property("offsets")


def set_offsets(w, l, t, r, b):
    s = slot(w)
    lay = s.get_editor_property("layout_data")
    lay.set_editor_property("offsets", unreal.Margin(l, t, r, b))
    s.set_editor_property("layout_data", lay)


copy_props = _UWC.copy_props   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def place_like(dst, src, dx=0.0, dy=0.0, size=None):
    """Слот `dst` = слот `src` (анкоры, выравнивание, авторазмер, z), сдвинутый на (dx, dy); size — (w, h) вместо исходного."""
    s_src, s_dst = slot(src), slot(dst)
    if s_src is None or s_dst is None:
        raise RuntimeError("у %s/%s нет слота канваса" % (src.get_name(), dst.get_name()))
    lay = s_src.get_editor_property("layout_data")
    off = lay.get_editor_property("offsets")
    new = unreal.AnchorData()
    new.set_editor_property("anchors", lay.get_editor_property("anchors"))
    new.set_editor_property("alignment", lay.get_editor_property("alignment"))
    w, h = (off.right, off.bottom) if size is None else size
    new.set_editor_property("offsets", unreal.Margin(off.left + dx, off.top + dy, w, h))
    s_dst.set_editor_property("layout_data", new)
    s_dst.set_editor_property("auto_size", s_src.get_editor_property("auto_size"))
    s_dst.set_editor_property("z_order", s_src.get_editor_property("z_order"))


def make_like(bp, src, parent, name):
    """Создать узел того же класса, что `src`, под `parent` и скопировать его свойства."""
    z = unreal.Vector2D(0, 0)
    one = unreal.Vector2D(1, 1)
    if isinstance(src, unreal.CanvasPanel):
        w = B.add_canvas(bp, parent, name, z, one); props = CANVAS_PROPS
    elif isinstance(src, unreal.CheckBox):
        f = unreal.AsdaJuiFrame()
        w = B.add_check_box(bp, parent, name, z, one, f, f, f, f); props = CHECK_PROPS
    elif isinstance(src, unreal.Button):
        w = B.add_button(bp, parent, name, z, one); props = BUTTON_PROPS
    elif isinstance(src, unreal.ComboBoxString):
        w = B.add_combo_box(bp, parent, name, z, one); props = COMBO_PROPS
    elif isinstance(src, unreal.TextBlock):
        w = B.add_text(bp, parent, name, z, one, "", 0, unreal.LinearColor(1, 1, 1, 1), 0, False, True); props = TEXT_PROPS
    elif isinstance(src, unreal.Image):
        w = B.add_image(bp, parent, name, z, one, None, unreal.Box2D(), unreal.LinearColor(1, 1, 1, 1),
                        False, unreal.Margin(0, 0, 0, 0)); props = IMAGE_PROPS
    else:
        raise RuntimeError("клон класса %s не поддержан (%s)" % (type(src).__name__, src.get_name()))
    if w is None:
        raise RuntimeError("не создан узел %s" % name)
    copy_props(w, src, props)
    if isinstance(src, unreal.TextBlock):
        w.set_text(src.get_text())
    return w


class Step:
    def __init__(self, bp, dry):
        self.bp = bp
        self.dry = dry
        self.nodes = walk(B.get_root_canvas(bp))
        self.new = []           # имена созданных узлов
        self.removed = []       # ★ 12.10.2026: имена удалённых узлов (снятые строки)
        self.moved = []         # ★ 12.10.2026: корни строк, сдвинутых вверх (меняется только off(...))
        self.manifest = {}      # тег строки → имена её узлов (для кода и отчёта)

    def get(self, name):
        n = self.nodes.get(name)
        if n is None:
            raise RuntimeError("в окне нет узла %s" % name)
        return n

    def exists(self, name):
        return name in self.nodes

    def clone_tree(self, src_root, parent, root_name, child_map, dx, dy, root_vis=None):
        """Клон строки: корень (со сдвигом) + дети по явной таблице имён. Уже есть корень — пропуск."""
        if self.exists(root_name):
            return None
        kids = [src_root.get_child_at(i) for i in range(src_root.get_children_count())]
        unmapped = [k.get_name() for k in kids if k.get_name() not in child_map]
        if unmapped:
            raise RuntimeError("у шаблона %s есть узлы без имени клона: %s" % (src_root.get_name(), unmapped))
        names = [root_name] + [child_map[k.get_name()] for k in kids]
        clash = [n for n in names if self.exists(n)]
        if clash:
            raise RuntimeError("имена клонов уже заняты: %s" % clash)
        self.new.extend(names)
        if self.dry:
            log("[сухой] строка %s ← %s (+%.0f,+%.0f), узлов %d" % (root_name, src_root.get_name(), dx, dy, len(names)))
            return None
        root = make_like(self.bp, src_root, parent, root_name)
        place_like(root, src_root, dx, dy)
        if root_vis is not None:
            root.set_editor_property("visibility", root_vis)
        self.nodes[root_name] = root
        for k in kids:
            c = make_like(self.bp, k, root, child_map[k.get_name()])
            place_like(c, k)
            self.nodes[child_map[k.get_name()]] = c
        return root

    def clone_one(self, src, parent, name, dx=0.0, dy=0.0, size=None, text=None):
        if self.exists(name):
            return None
        self.new.append(name)
        if self.dry:
            log("[сухой] узел %s ← %s" % (name, src.get_name()))
            return None
        w = make_like(self.bp, src, parent, name)
        place_like(w, src, dx, dy, size)
        if text is not None:
            w.set_text(text)
        self.nodes[name] = w
        return w

    # ── ★ 12.10.2026: снять строку (обратная операция к add_row) ──
    def remove_row(self, tag, kind, n):
        """Удалить строку `SYSTEM_OX_<tag>_grp` вместе с детьми. Детей ищем по той же таблице имён `TPL[kind]`, что их
        создала. Ребёнок вне таблицы — исключение (возможна правка владельца, сами не решаем). Нет корня — 0 правок."""
        root_name = "SYSTEM_OX_%s_grp" % tag
        root = self.nodes.get(root_name)
        if root is None:
            return
        cmap = TPL[kind][2](tag, n)
        expected = set(cmap.values())
        kids = [root.get_child_at(i) for i in range(root.get_children_count())]
        unknown = [k.get_name() for k in kids if k.get_name() not in expected]
        if unknown:
            raise RuntimeError("у снимаемой строки %s есть узлы вне таблицы имён: %s — не трогаю" % (root_name, unknown))
        names = [k.get_name() for k in kids] + [root_name]
        self.removed.extend(names)
        if self.dry:
            log("[сухой] снять строку %s: узлов %d" % (root_name, len(names)))
            return
        for nm in names:   # дети первыми: RemoveWidgetByName не удаляет узел с детьми
            if not B.remove_widget_by_name(self.bp, nm, ""):
                raise RuntimeError("не удалён узел %s строки %s" % (nm, root_name))
            self.nodes.pop(nm, None)
        log("снята строка %s: удалено узлов %d" % (root_name, len(names)))

    def place_existing_row(self, root_name, tpl_root_name, tpl_y, y):
        """★ 12.10.2026: корень УЖЕ существующей строки — на ожидаемый верх `y` (та же формула, что у place_like при
        создании: верх шаблона + (y − Y шаблона)). Меняется только верх; ширина/высота/лево — как были."""
        root = self.nodes.get(root_name)
        if root is None:
            return
        want_top = offsets(self.get(tpl_root_name)).top + float(y - tpl_y)
        o = offsets(root)
        if abs(o.top - want_top) < 0.01:
            return
        self.moved.append(root_name)
        log("%sстрока %s: верх %.1f → %.1f" % ("[сухой] " if self.dry else "", root_name, o.top, want_top))
        if not self.dry:
            set_offsets(root, o.left, want_top, o.right, o.bottom)

    # ── строка по описанию ──
    def add_row(self, row, parent):
        tag, kind, _pname, y, n, label = row
        if kind == "note":
            src = self.get("SYSTEM_Op_SYSTEM_SYS22_TXT02")
            # Примечание — отдельная подпись во всю ширину строки (колонка подписей L8, до края группы 440).
            o = offsets(src)
            w = self.clone_one(src, parent, "SYSTEM_OX_%s_TXT" % tag, dx=8.0 - o.left, dy=(y + o.top) - o.top,
                               size=(430.0, o.bottom), text=label)
            self.manifest[tag] = ["SYSTEM_OX_%s_TXT" % tag]
            return w
        tpl_root_name, tpl_y, mapf = TPL[kind]
        src_root = self.get(tpl_root_name)
        cmap = mapf(tag, n)
        root = self.clone_tree(src_root, parent, "SYSTEM_OX_%s_grp" % tag, cmap, 0.0, float(y - tpl_y), VIS_ROW)
        self.manifest[tag] = ["SYSTEM_OX_%s_grp" % tag] + sorted(cmap.values())
        if root is None:
            # ★ 12.10.2026: строка уже есть — проверить/поправить её верх (после снятия соседей строки идут вверх без дыр).
            self.place_existing_row("SYSTEM_OX_%s_grp" % tag, tpl_root_name, tpl_y, y)
        if root is not None:
            txt = self.nodes.get("SYSTEM_OX_%s_TXT" % tag)
            if txt is not None:
                txt.set_text(label)
            cmb_txt = self.nodes.get("Bk_ComboBox_%d_txt" % (n + 2))
            if cmb_txt is not None:
                cmb_txt.set_text("")
        return root

    def move_before(self, parent, child_name, anchor_name):
        """Поставить ребёнка перед узлом `anchor_name` (порядок рисования канваса)."""
        if self.dry or child_name not in self.nodes:
            return
        child = self.nodes[child_name]
        anchor = self.get(anchor_name)
        idx = None
        for i in range(parent.get_children_count()):
            if parent.get_child_at(i) == anchor:
                idx = i
                break
        if idx is None:
            raise RuntimeError("%s не ребёнок %s" % (anchor_name, parent.get_name()))
        cur = None
        for i in range(parent.get_children_count()):
            if parent.get_child_at(i) == child:
                cur = i
                break
        if cur is not None and cur < idx:
            return   # уже стоит раньше
        if not B.move_child_to_index(parent, child, idx):
            raise RuntimeError("не переставлен %s" % child_name)

    def run(self):
        # ── 0. Правки прежних узлов ──
        edits = []
        for name, vis in TOUCH_VIS.items():
            w = self.get(name)
            if w.get_editor_property("visibility") != vis:
                edits.append(name)
                log("%s%s.visibility: %s → %s" % ("[сухой] " if self.dry else "", name, w.get_editor_property("visibility"), vis))
                if not self.dry:
                    w.set_editor_property("visibility", vis)
        feel_new = not self.exists("PICK_BACK_grp_84")
        if feel_new:
            for name in TOUCH_GROW:
                w = self.get(name)
                o = offsets(w)
                edits.append(name)
                log("%s%s: высота %.1f → %.1f" % ("[сухой] " if self.dry else "", name, o.bottom, o.bottom + FEEL_ROW_STEP))
                if not self.dry:
                    set_offsets(w, o.left, o.top, o.right, o.bottom + FEEL_ROW_STEP)

        # ── 1. Ячейки 3 и 4 списка под-страниц «Настроек системы» (клон ячейки 0, +25 px на ячейку) ──
        grp = self.get("SYSTEM_Op_SYSTEM_DD_grp")
        plane0 = txt0 = None
        for i in range(grp.get_children_count()):
            k = grp.get_child_at(i)
            nm = k.get_name()
            if nm.startswith("TalkBar_system_D"):
                if nm.endswith("_txt"):
                    txt0 = k
                elif nm.endswith("_0"):
                    plane0 = k
        btn0 = self.get("SYSTEM_Op_SYSTEM_DD_SEL00_OPT0")
        if plane0 is None or txt0 is None:
            raise RuntimeError("неполная ячейка 0 списка 70006")
        step = offsets(plane0).bottom
        for i in (3, 4):
            dy = step * i
            self.clone_one(plane0, grp, "SYSTEM_Op_SYSTEM_DD_CELL%02d_0" % i, dy=dy)
            self.clone_one(btn0, grp, "SYSTEM_Op_SYSTEM_DD_SEL00_OPT%d" % i, dy=dy)
            self.clone_one(txt0, grp, "SYSTEM_Op_SYSTEM_DD_CELL%02d_txt" % i, dy=dy, text="")

        # ── 2. Страница «Экран»: новые строки ──
        scr = self.get(SCREEN)
        for row in ROWS_SCREEN:
            self.add_row(row, scr)
            self.move_before(scr, "SYSTEM_OX_%s_%s" % (row[0], "TXT" if row[1] == "note" else "grp"), "NoImage_grp_11")
        self.add_row(ROW_MODE, scr)
        self.move_before(scr, "SYSTEM_OX_S1_grp", "NoImage_grp_12")

        # ── 3. Под-страницы «Качество» и «Эффекты» (клон панели 3D без строк) ──
        sys_grp = self.get("SYSTEM_Op_SYSTEM_grp")
        panel = self.get("SYSTEM_Op_SYSTEM_3d_OP_grp")
        inner = self.get("SYSTEM_O3_SYSTEM_SYS33_grp")
        for pname, frame_n, inner_name, rows in (
            ("SYSTEM_Op_SYSTEM_quality_grp", 9190, QUALITY_INNER, ROWS_QUALITY),
            ("SYSTEM_Op_SYSTEM_effects_grp", 9300, EFFECTS_INNER, ROWS_EFFECTS),
        ):
            p = self.clone_one(panel, sys_grp, pname)
            p = self.nodes.get(pname, p)
            for i in range(9):
                self.clone_one(self.get("SYSTEM_Op_SYSTEM_3d_OP_247_%d" % i), p, "SYSTEM_Op_SYSTEM_%s_%d_%d"
                               % (pname.split("_")[3], frame_n, i))
            self.clone_one(inner, p, inner_name)
            host = self.nodes.get(inner_name)
            # ★ 12.10.2026: сначала снять строки, выведенные из состава, затем расставить оставшиеся (сдвиг вверх без дыр).
            for tag, kind, pname_removed, n in REMOVED_ROWS:
                if pname_removed == inner_name:
                    self.remove_row(tag, kind, n)
            for row in rows:
                self.add_row(row, host)
            if pname.endswith("quality_grp"):
                # Кнопка «Автоподбор» — справа от списка «Общее качество» (клон «По умолчанию» 69651).
                btn = self.get("SYSTEM_default")
                btxt = self.get("SYSTEM_default_Text")
                ob, ot = offsets(btn), offsets(btxt)
                bx, by = 378.0, 5.0
                self.clone_one(btn, host, "SYSTEM_OX_QA_BTN", dx=bx - ob.left, dy=by - ob.top)
                self.clone_one(btxt, host, "SYSTEM_OX_QA_BTN_Text", dx=bx - ob.left, dy=by - ob.top, text="Автоподбор")
                self.manifest["QA"] = ["SYSTEM_OX_QA_BTN", "SYSTEM_OX_QA_BTN_Text"]

        # ── 4. «Эмоции»: поле «Стойка на руках» (действие 85) ──
        feel = self.get("NoImage_grp_7")
        self.clone_tree(self.get("PICK_BACK_grp_83"), feel, "PICK_BACK_grp_84", {
            "PICK_BACK_214_0": "PICK_BACK_9400_0",
            "PICK_BACK_214_txt": "PICK_BACK_9400_txt",
            "SYSTEM_Op_HOTKEYS_FEEL_TXT11": "SYSTEM_Op_HOTKEYS_FEEL_TXT12",
        }, 0.0, FEEL_ROW_STEP)
        if "SYSTEM_Op_HOTKEYS_FEEL_TXT12" in self.nodes and not self.dry:
            self.nodes["SYSTEM_Op_HOTKEYS_FEEL_TXT12"].set_text("Стойка на руках")
            self.nodes["PICK_BACK_9400_txt"].set_text("NONE")
        return edits


def main():
    dry = bool(os.environ.get("ASDA_OPTEXTRA_DRYRUN"))
    bp = EAL.load_asset(ASSET)
    if not bp:
        raise RuntimeError("не загрузился %s" % ASSET)
    before = dump_map(bp)
    st = Step(bp, dry)
    edits = st.run()
    log("%sновых узлов %d, правок прежних %d, снято узлов %d, сдвинуто строк %d"
        % ("[сухой] " if dry else "", len(st.new), len(edits), len(st.removed), len(st.moved)))
    if dry or (not st.new and not edits and not st.removed and not st.moved):
        log("НОВЫЕ СТРОКИ 69500: ГОТОВО%s — создано узлов %d, снято %d, сдвинуто строк %d"
            % ("  [СУХОЙ ПРОГОН]" if dry else "", 0 if dry else len(st.new), 0 if dry else len(st.removed), 0 if dry else len(st.moved)))
        return
    after = dump_map(bp)
    bad = []
    vis_rx = re.compile(r"вид=\S+")
    off_rx = re.compile(r"off\([^)]*\)")
    for n in before:
        if n not in after:
            if n in st.removed:
                continue   # ★ 12.10.2026: снятая строка — ожидаемо
            bad.append("пропал %s" % n)
            continue
        if before[n] == after[n]:
            continue
        if n in TOUCH_VIS:
            if vis_rx.sub("", before[n]) != vis_rx.sub("", after[n]):
                bad.append("у %s изменилась не только видимость" % n)
        elif n in TOUCH_GROW or n in st.moved:
            if off_rx.sub("", before[n]) != off_rx.sub("", after[n]):
                bad.append("у %s изменилось не только смещение" % n)
        else:
            bad.append("изменён неперечисленный %s" % n)
    new = sorted(set(after) - set(before))
    if new != sorted(st.new):
        bad.append("новые узлы не совпали: лишние %s, недостающие %s"
                   % (sorted(set(new) - set(st.new)), sorted(set(st.new) - set(new))))
    gone = sorted(set(before) - set(after))
    if gone != sorted(st.removed):
        bad.append("снятые узлы не совпали: лишние %s, недостающие %s"
                   % (sorted(set(gone) - set(st.removed)), sorted(set(st.removed) - set(gone))))
    if bad:
        raise RuntimeError("СВЕРКА НЕ ПРОШЛА — не сохраняю: %s" % "; ".join(bad))
    log("сверка: прежних узлов %d — изменены только %s, сдвинуты %s; новых %d и снято %d — ровно ожидаемые"
        % (len(before), edits, st.moved, len(new), len(gone)))
    for n in new:
        log("  + %s" % after[n])
    for n in gone:
        log("  - %s" % before[n])
    # ★ 12.10.2026: резервная копия .uasset ДО записи (закон backup-before-edit; единый помощник `backup_file`).
    asset_file = os.path.join(CONTENT_DIR, ASSET[len("/Game/"):].replace("/", os.sep) + ".uasset")
    bak = HT.backup_file(asset_file, BACKUP_SET, log=log)
    if bak is None:
        raise RuntimeError("нет файла ассета %s — бэкап невозможен, запись запрещена" % asset_file)
    log("резервная копия: %s" % bak)
    # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
    B.compile_and_save(bp)
    if not EAL.save_loaded_asset(bp, False):
        raise RuntimeError("не сохранён %s" % ASSET)
    unreal.log("ASDA_WRITES_ASSET: %s" % ASSET)   # маркер записи ассета для run_headless_safe (--expect-asset)
    try:
        os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
        with open(MANIFEST, "w", encoding="utf-8") as f:
            json.dump({"asset": ASSET, "rows": st.manifest, "new_nodes": new, "edited": edits,
                       "removed_nodes": gone, "moved_rows": st.moved}, f, ensure_ascii=False, indent=1)
    except Exception as e:
        log("манифест не записан: %s" % e)
    log("НОВЫЕ СТРОКИ 69500: ГОТОВО — создано узлов %d, снято %d, сдвинуто строк %d" % (len(new), len(gone), len(st.moved)))


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[новые строки 69500] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
