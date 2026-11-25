# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_minimap_ui.py — привести ШТАТНЫЕ импортированные виджеты мини-карты к эталону:
#   (1) ХИТ-ТЕСТ. Корневой канвас и все групп-канвасы окна растянуты/лежат поверх экрана и в UMG
#       ПЕРЕХВАТЫВАЮТ мышь (Visibility=Visible у панели = она хит-тестится). В оригинале контейнеры
#       мыши не получают — событие идёт только контролам с обработчиком. → все CanvasPanel =
#       SelfHitTestInvisible, все Image/AsdaAnimatedPlane = HitTestInvisible, кнопки/чекбоксы —
#       не забирают КЛАВИАТУРНЫЙ фокус (is_focusable=False), иначе виджет глотает WASD.
#   (2) ТЕКСТ. Импортёр JUI берёт цвет/выравнивание метки из CaptionInfo (это КАПШЕН контрола,
#       у Text-контролов он чёрный ff000000), а у Text-контрола свойства лежат в блоке EXS:
#         TextPos / TextContent / TextColor / TextAlign
#       Эталон CTextControl_LoadProps@0x12E4E20 (построчно):
#         TextColor -> this+3692 (dword 923) = ЦВЕТ ТЕКСТА
#         TextAlign -> this+3696 (dword 924) = ФЛАГИ ВЫРАВНИВАНИЯ (GDI DT_: 0x1 center, 0x2 right,
#                                              0x4 vcenter, 0x8 bottom)
#       Отрисовка (uiTextControl_Render, ui_core.c:110728): flags = f924 | f926 | 0x100,
#       где f926 — СТИЛЬ ГЛИФА (font_text.c:6832/6887/6952): 0x100000 обычный, 0x200000 жирный,
#       0x400000 тень, 0x800000 ОБВОДКА (цвет обводки = shadowColor, по умолчанию 0xFF000000).
#   (3) РАНТАЙМ-ПЕРЕОПРЕДЕЛЕНИЯ ЭТАЛОНА (MiniMap_BuildUI, world_map.c:7009-7013) — их в JUI нет:
#         MINIMAP_NAME(1703): CControl_SetField923(-5179905)=0xFFB0F5FF (голубой)
#                             CClass_SetField926(0x800000)   = ОБВОДКА
#   (4) КНОПКИ СТРОК КАНАЛОВ. В JUI строки Change_Channel00..14 (17503..17517) — Text-контролы,
#       которые САМИ получают клик (event 0xB, world_map.c:7521). Импортёр делает из них TextBlock
#       (не кликается) → кладём поверх каждой строки прозрачную кнопку {row}_Click ровно по её rect.
#       Никакого «своего UI»: геометрия 1:1 со строкой, визуал остаётся JUI-шный.
#   (5) ★★★ 07.10.2026 — ИДЕМПОТЕНТНОСТЬ ЦЕПОЧКИ (ревизия `Reports/owner_1007/label_band_fixes_REVIEW.md`,
#       СРЕДН-1). Прежнее правило «JUI Visible=0 → Collapsed КАЖДОМУ узлу контрола» гасило заодно:
#         • планы КОРНЯ окна: `MAXMAP_WIN_0_0..7` (белая рамка 1710, её показывает шаг `asda_fix_worldmap_ui`;
#           `jui_id_of` сводил их к `MAXMAP_WIN` с Visible=0) — каждый прогон цепочки рамку гасил;
#         • планы/подписи ВНУТРИ свёрнутой группы (`MaxMap_Worldmap_22_0`, `CAMPWAR_Out_Button(_Text)`,
#           `Change_Channel_Win_37_0`, `Indicator_MiniMap_31_0`, `MMap_CBar2_12_0`, `MMap_CBg(2)_*`).
#       По эталону контрол хранит СВОЙ бит видимости, а предок прячет поддерево временно
#       (`CControl_IsVisibleChain`); код показывает и прячет КОНТРОЛ целиком (world_map.c: 7117 окно 1710
#       при сборке скрыто и далее переключается кодом; 7309/7323 планка 1715 в `MiniMap_SetMaxMin`;
#       7520-7581 панель каналов 17501), и его скин рисуется вместе с ним. В UMG то же даёт свёрнутая группа `{idc}_grp`: дети видны, как только код покажет группу.
#       Импортёр после 17.09 собирает ровно так (`asda_jui_generic_umg.build_tree`). Новое правило (`plan_node`):
#         • КОРЕНЬ окна (ID контрола == ID окна) не трогаем вовсе: «открыто ли окно» — состояние кода;
#         • Visible=0 и у контрола есть группа `{idc}_grp` → свёрнута ТОЛЬКО группа, содержимое не трогаем;
#         • Visible=0 и группы нет (лист) → свёрнуты все узлы контрола (как у импортёра).
#   (6) ★★★ 07.10.2026 — ВЫРАВНИВАНИЕ ПОДПИСИ КНОПКИ (ревизия `Reports/owner_1007/poststeps_idempotent_REVIEW.md`,
#       СРЕДН-1 / МЕЛК-2). `apply_text` брал маску ТОЛЬКО из `EXS TextAlign`, а у НЕ-Text контрола (подпись `{idc}_Text`)
#       его нет → маска 0 → LEFT. Так подпись «Найти моё местоположение» (`MaxMap_MyPosBtn_Text`, `CaptionInfo` …5)
#       и кнопка каналов мини-карты (`Change_Channel_Btn_Text`, «Альпеон - 1 кан.», `CaptionInfo` …5) уезжали влево,
#       а шаг карты мира следом возвращал центр — цепочка не была идемпотентной. Теперь маска и горизонталь берутся
#       из ОБЩЕЙ ТОЧКИ импортёра `J.label_mask` / `J.label_justify` (Text → `EXS TextAlign`, подпись → `CaptionInfo[3]`)
#       по узлу .JUI ИЗ ПОДДЕРЕВА ЭТОГО ОКНА (`window_nodes`), а не по глобальному словарю ID.
#
# Запуск: Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_fix_minimap_ui.py
#   Сухой прогон: `--dry-run` / ASDA_MMAPUI_DRYRUN=1 / ASDA_POSTSTEPS_DRYRUN=1 (`asda_ui_step_dryrun`).
import os
import re
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ui_step_dryrun as SD   # общий сухой режим шагов пост-импорта (07.10.2026)
import asda_jui_parse as J         # парсер .JUI импортёра: дерево контролов + ОБЩЕЕ правило выравнивания подписи

# Собственный ключ сухого прогона шага (см. `asda_ui_step_dryrun.is_dry`).
DRYRUN_ENV = "ASDA_MMAPUI_DRYRUN"

B = unreal.AsdaUmgBuilder
EAL = unreal.EditorAssetLibrary

JUI_INI = r"@@ASTORY_DEV_ROOT@@\Data\Decoded\jui\gamemain_jui.ini"

WBPS = [
    "/Game/AsdaAssets/UI/Player_UI/Map/WBP_MINIMAP_WIN",
    "/Game/AsdaAssets/UI/Player_UI/Map/WBP_MINIMAPMIN_WIN",
    "/Game/AsdaAssets/UI/Player_UI/Map/WBP_MAXMAP_WIN",
]

# Эталонные рантайм-переопределения (MiniMap_BuildUI@0xD63220): имя -> (цветARGB, стильf926)
ETALON_RUNTIME = {
    "MINIMAP_NAME": (0xFFB0F5FF, 0x800000),   # world_map.c:7012-7013
}
OUTLINE_COLOR_ARGB = 0xFF000000   # font_text.c: *(this+613009) = -16777216 (shadowColor по умолчанию)
STYLE_OUTLINE = 0x800000


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def argb_to_lc(v):
    """0xAARRGGBB -> unreal.LinearColor (RGB sRGB->linear, альфа линейна) — закон ui-srgb-gamma-rule."""
    a = ((v >> 24) & 0xFF) / 255.0
    r = srgb_to_linear(((v >> 16) & 0xFF) / 255.0)
    g = srgb_to_linear(((v >> 8) & 0xFF) / 255.0)
    b = srgb_to_linear((v & 0xFF) / 255.0)
    return unreal.LinearColor(r, g, b, a)


def parse_jui_props(path):
    """ID контрола -> {'color', 'visible'}: EXS TextColor + флаг Visible контрола.
    (Выравнивание отсюда НЕ берётся с 07.10.2026 — `apply_text` зовёт общую точку `J.label_mask` по узлу окна.)"""
    out = {}
    if not os.path.isfile(path):
        unreal.log_warning("[mmap-ui] НЕТ JUI-ini: " + path)
        return out
    cur = None
    with open(path, "r", encoding="latin-1", errors="replace") as f:
        for line in f:
            s = line.strip()
            m = re.match(r'^ID\s*=\s*"(.*)"', s)
            if m:
                cur = m.group(1)
                out.setdefault(cur, {})
                continue
            if cur is None:
                continue
            m = re.match(r'^Visible\s*=\s*"(\d+)"', s)
            if m and "visible" not in out[cur]:
                out[cur]["visible"] = int(m.group(1))
                continue
            m = re.match(r'^TextColor\s*=\s*"([0-9A-Fa-f]+)"', s)
            if m:
                out[cur]["color"] = int(m.group(1), 16)
    return out


def jui_id_of(widget_name):
    """Имя виджета импортёра -> ID контрола JUI.
    Импортёр даёт: точное имя idc (интерактив/Text), «{idc}_grp» (узел-контейнер),
    «{idc}_{ctrlIdx}_{planeJ}» (планы скина), «{idc}_Text» (подпись не-Text контрола)."""
    n = widget_name
    if n.endswith("_grp"):
        return n[:-4]
    if n.endswith("_Text"):
        return n[:-5]
    m = re.match(r'^(.*)_\d+_\d+$', n)
    if m:
        return m.group(1)
    return n


def walk(w, cb, depth=0):
    try:
        kids = w.get_all_children()
    except Exception:
        return
    for c in kids:
        cb(c, depth)
        walk(c, cb, depth + 1)


def find(root, name):
    hit = []

    def cb(w, _d):
        if w.get_name() == name:
            hit.append(w)
    walk(root, cb)
    return hit[0] if hit else None


def slot_rect(w):
    s = w.slot
    if not s or not isinstance(s, unreal.CanvasPanelSlot):
        return None
    lay = s.get_editor_property("layout_data")
    return lay.offsets, lay.anchors


# Горизонталь `J.label_justify` (0/1/2) → значение `unreal.TextJustify`.
_JUSTIFY_ENUM = ("LEFT", "CENTER", "RIGHT")


def window_nodes(roots, win_jid):
    """ID контрола → узел .JUI в поддереве окна `win_jid` (первое вхождение; None — окна в .JUI нет).
    ★ 07.10.2026: по поддереву окна, а не по всему файлу — ID вроде `NoImage` повторяются в разных окнах."""
    def walk_nodes(n):
        yield n
        for c in n.children:
            for x in walk_nodes(c):
                yield x
    stack = list(roots)
    while stack:
        n = stack.pop(0)
        if n.idc == win_jid:
            out = {}
            for x in walk_nodes(n):
                if x.idc and x.idc not in out:
                    out[x.idc] = x
            return out
        stack.extend(n.children)
    return None


def text_node_of(name, nodes):
    """Узел .JUI, чей текст рисует TextBlock `name`: сам Text-контрол (`{idc}`) либо контрол подписи (`{idc}_Text`)."""
    if not nodes:
        return None
    n = nodes.get(name)
    if n is None and name.endswith("_Text"):
        n = nodes.get(name[:-5])
    return n


def apply_text(tb, props, nodes):
    """Цвет/выравнивание/обводка текста контрола по эталону.
    `nodes` — `window_nodes` окна: выравнивание из ОБЩЕЙ ТОЧКИ импортёра (`J.label_mask` → `J.label_justify`)."""
    name = tb.get_name()
    p = dict(props.get(name, {}))
    color = p.get("color")
    node = text_node_of(name, nodes)
    align = J.label_mask(node) if node is not None else 0
    style = 0x100000
    if name in ETALON_RUNTIME:
        color, style = ETALON_RUNTIME[name]
    if color is not None:
        tb.set_editor_property("color_and_opacity", unreal.SlateColor(argb_to_lc(color)))
    # DT_-флаги: 0x1 = center, 0x2 = right, иначе left (приоритет — как у импортёра, `J.label_justify`)
    just = getattr(unreal.TextJustify, _JUSTIFY_ENUM[J.label_justify(align)])
    if tb.get_editor_property("justification") != just:
        tb.set_editor_property("justification", just)
    font = tb.get_editor_property("font")
    outl = font.get_editor_property("outline_settings")
    outl.set_editor_property("outline_size", 1 if (style & STYLE_OUTLINE) else 0)
    outl.set_editor_property("outline_color", argb_to_lc(OUTLINE_COLOR_ARGB))
    font.set_editor_property("outline_settings", outl)
    tb.set_editor_property("font", font)
    return (name, color, align, style)


def window_jid(path):
    """ID корневого контрола окна по пути ассета: «…/WBP_MAXMAP_WIN» → «MAXMAP_WIN»."""
    return path.rsplit("/", 1)[-1][len("WBP_"):]


def tree_names(root):
    """Множество имён всех узлов окна (для проверки «есть ли у контрола группа `{idc}_grp`»)."""
    names = set()

    def cb(w, _d):
        names.add(w.get_name())
    walk(root, cb)
    return names


def plan_node(name, props, names, win_jid):
    """★ 07.10.2026: решение шага по ОДНОМУ узлу (общая точка для `main` и `step_visibility_nodes`).

    Возврат — одно из:
      "nonjui" — узел не из JUI (наши `*_View`/`*_Markers`/`*_ClickArea`/`*_Click`) — не трогаем;
      "root"   — узел КОРНЯ окна (ID контрола == ID окна): видимостью окна ведает код, не трогаем;
      "hide"   — Visible=0, и это сам контрол: группа `{idc}_grp` либо (у листа без группы) любой его узел;
      "inside" — Visible=0, но узел лежит ВНУТРИ свёрнутой группы своего контрола — свой флаг не трогаем;
      "show"   — Visible=1: хит-тест/фокус/текст по типу узла (разбор в `main`).
    """
    jid = jui_id_of(name)
    jp = props.get(jid)
    if jp is None:
        return "nonjui"
    if jid == win_jid:
        return "root"
    if jp.get("visible", 1) != 0:
        return "show"
    if name == jid + "_grp" or (jid + "_grp") not in names:
        return "hide"
    return "inside"


def step_visibility_nodes():
    """★ 07.10.2026: узлы, видимость которых ВЕДЁТ этот шаг (читает `asda_ui_sync_own_visibility`).

    Возврат — [(путь окна, узел, причина)]: свёрнутые по правилу `plan_node` («hide») и кнопки строк
    каналов `Change_ChannelNN_Click` (их создаёт и сворачивает шаг). Только чтение ассетов."""
    props = parse_jui_props(JUI_INI)
    out = []
    for path in WBPS:
        bp = EAL.load_asset(path)
        if not bp:
            continue
        root = B.get_root_canvas(bp)
        names = tree_names(root)
        wj = window_jid(path)
        for n in sorted(names):
            if n.startswith("Change_Channel") and n.endswith("_Click"):
                out.append((path, n, "кнопка строки канала: показывает код по g_nChannelCount"))
            elif plan_node(n, props, names, wj) == "hide":
                out.append((path, n, "JUI Visible=0 — свёрнут сам контрол"))
    return out


def main():
    props = parse_jui_props(JUI_INI)
    unreal.log("[mmap-ui] из JUI прочитано контролов: %d" % len(props))
    roots = J.parse_jui(JUI_INI)                                 # дерево .JUI (выравнивание подписей — по окну)
    dry = SD.is_dry(DRYRUN_ENV)                                  # сухой прогон (запись подавлена)
    delta = SD.WidgetDelta("asda_fix_minimap_ui", dry)           # учёт правок шага по снимкам дерева

    HIDDEN = (unreal.SlateVisibility.COLLAPSED, unreal.SlateVisibility.HIDDEN)

    for path in WBPS:
        bp = EAL.load_asset(path)
        if not bp:
            unreal.log_warning("[mmap-ui] НЕТ " + path)
            continue
        delta.take(bp)
        root = B.get_root_canvas(bp)
        # (1) хит-тест: корень + контейнеры не ловят мышь, картинки — тоже.
        #     ★ СКРЫТОЕ НЕ ПОКАЗЫВАЕМ: узлы с JUI Visible="0" импортёр кладёт Collapsed — их состояние
        #     трогать НЕЛЬЗЯ (иначе на экран вылезают попапы/альт-состояния: Indicator_MiniMap 1719,
        #     Game_Grade_Mark 1718, MMap_AniGear, CAMPWAR_Out_Button 17520, Change_Channel_Win 17501…).
        root.set_editor_property("visibility", unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE)
        stats = {"canvas": 0, "image": 0, "btn": 0, "text": 0, "hid": 0, "kept": 0, "root": 0, "inside": 0}
        names = tree_names(root)
        win_jid = window_jid(path)
        nodes = window_nodes(roots, win_jid)                     # ID → узел .JUI этого окна
        if nodes is None:
            unreal.log_warning("[mmap-ui] окна %s нет в .JUI — выравнивание подписей не определить" % win_jid)

        def fix(w, _d):
            vis = w.get_editor_property("visibility")
            act = plan_node(w.get_name(), props, names, win_jid)
            if act == "nonjui":
                stats["kept"] += 1          # не JUI-узел (наши *_View/*_Markers/*_ClickArea) — не трогаем
                return
            if act == "root":
                # ★ 07.10: узлы КОРНЯ окна (рамка 1710 `MAXMAP_WIN_0_*` и т.п.) не трогаем вовсе:
                #   окно показывает/прячет код, а рамку окна показывает шаг `asda_fix_worldmap_ui`.
                stats["root"] += 1
                return
            # (1а) ЭТАЛОННАЯ ВИДИМОСТЬ = СОБСТВЕННЫЙ флаг Visible контрола в JUI.
            #      Visible="0" → Collapsed САМ КОНТРОЛ (его группа `{idc}_grp`, у листа — его узлы):
            #      контрол показывает код по своему условию (Indicator_MiniMap 1719, Game_Grade_Mark 1718,
            #      MMap_AniGear, MMap_CBar2 1715, Change_Channel_Win 17501, CAMPWAR_Out_Button 17520,
            #      Gear_bookX_btn 80250 …). Содержимое свёрнутой группы («inside») не трогаем: при показе
            #      группы оно обязано появиться со СВОИМИ флагами (так собирает импортёр после 17.09).
            #      Visible="1" → показываем, ДАЖЕ если импортёр скрыл узел «за компанию» с родителем:
            #      в UMG свёрнутый родитель и так прячет детей, а при показе родителя (напр. MiniMap_SetMaxMin
            #      делает SetVisible(MMap_CBar2, 1)) дети обязаны появиться со СВОИМИ флагами.
            if act in ("hide", "inside"):
                if act == "hide" and vis not in HIDDEN:
                    w.set_editor_property("visibility", unreal.SlateVisibility.COLLAPSED)
                    stats["hid"] += 1
                if act == "inside":
                    stats["inside"] += 1
                if isinstance(w, unreal.TextBlock):
                    apply_text(w, props, nodes)
                return
            if isinstance(w, unreal.CanvasPanel):
                w.set_editor_property("visibility", unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE)
                stats["canvas"] += 1
            elif isinstance(w, (unreal.Image, unreal.AsdaAnimatedPlane)):
                w.set_editor_property("visibility", unreal.SlateVisibility.HIT_TEST_INVISIBLE)
                stats["image"] += 1
            elif isinstance(w, (unreal.Button, unreal.CheckBox)):
                # кнопка остаётся кликабельной, но клавиатурный фокус НЕ забирает (иначе глотает WASD)
                try:
                    w.set_editor_property("is_focusable", False)
                except Exception:
                    pass
                stats["btn"] += 1
            elif isinstance(w, unreal.TextBlock):
                apply_text(w, props, nodes)
                stats["text"] += 1
        walk(root, fix)

        # (4) кнопки-клика на строках каналов (только окно мини-карты)
        made = 0
        if path.endswith("WBP_MINIMAP_WIN"):
            for i in range(15):
                rname = "Change_Channel%02d" % i
                row = find(root, rname)
                if not row:
                    continue
                rect = slot_rect(row)
                if not rect:
                    continue
                off, anc = rect
                bname = rname + "_Click"
                if find(root, bname):
                    continue
                parent = row.get_parent()
                btn = B.add_button(bp, parent, bname,
                                   unreal.Vector2D(off.left, off.top),
                                   unreal.Vector2D(off.right, off.bottom))
                s = btn.slot
                if s and isinstance(s, unreal.CanvasPanelSlot):
                    lay = s.get_editor_property("layout_data")
                    lay.anchors = unreal.Anchors(anc.minimum, anc.maximum)
                    s.set_editor_property("layout_data", lay)
                try:
                    btn.set_editor_property("is_focusable", False)
                except Exception:
                    pass
                # Строка канала показывается кодом по g_nChannelCount → кнопка стартует скрытой.
                btn.set_editor_property("visibility", unreal.SlateVisibility.COLLAPSED)
                made += 1

        # ★ 07.10.2026 (ревизия `poststeps_align_fix_REVIEW.md`, МЕЛК-3): окно компилируется и пишется, ТОЛЬКО если
        #   снимок шага видит правки или созданы кнопки каналов. Ноль — файл не трогается (дата/md5 не меняются).
        n = SD.save_if_changed(bp, dry, "asda_fix_minimap_ui", delta, own=made, compile_fn=B.compile_and_save)
        unreal.log("[mmap-ui] %s: канвасы=%d картинки=%d кнопки=%d тексты=%d | скрыто по JUI Visible=0: %d, "
                   "внутри свёрнутой группы не тронуто: %d, узлов корня окна не тронуто: %d, "
                   "не-JUI узлов пропущено: %d, кнопок каналов добавлено=%d | изменено узлов: %d%s"
                   % (path, stats["canvas"], stats["image"], stats["btn"], stats["text"],
                      stats["hid"], stats["inside"], stats["root"], stats["kept"], made, n,
                      " (СУХОЙ)" if dry else ""))
    delta.finish()
    unreal.log("[mmap-ui] ГОТОВО")


if __name__ == "__main__":
    main()
