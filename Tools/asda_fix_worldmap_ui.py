# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_worldmap_ui.py — ДЕЛЬТА-правки ШТАТНОГО окна карты мира WBP_MAXMAP_WIN (JUI 1710).
#   Ничего не перегенерирует: только точечные правки поверх импортированного окна (закон о массовой
#   правке, а не перегенерации). Всё — по эталону, без «на глаз».
#
#   (1) БЕЛАЯ РАМКА ОКНА. Узел 1710 в JUI имеет Visible="0" (окно скрыто по умолчанию), и импортёр
#       положил ЕГО СОБСТВЕННЫЕ планы 9-slice скина Box_White3Stroke (MAXMAP_WIN_0_0..0_7) как
#       Collapsed. Код окно показывает — значит показать надо и группу, и её планы. Разворачиваем.
#   (2) ХИТ-ТЕСТ ОКНА. В JUI у узла 1710 NoSelect="0" — контрол ЛОВИТ мышь всем своим прямоугольником
#       690x519, поэтому клик по заголовку/рамке/панели в оригинале НЕ проваливается в мир. В UMG у
#       окна нет виджета, соответствующего прямоугольнику контрола (есть только 8 планов-обводок) —
#       добавляем прозрачный MAXMAP_WIN_Hit ровно по rect из JUI, hit-testable, ПОД всеми контролами
#       и ПОД полем карты (MaxMap_ClickArea остаётся самым верхним).
#   (3) ПОДСКАЗКА MaxMap_Guide. Импортёр применил к Text-контролу вертикальное центрирование из
#       CaptionInfo (align=5), хотя у Text-контрола выравнивание лежит в EXS TextAlign="0" (лево/верх).
#       Из-за этого текст уехал вниз на (90-14)/2 = 37 px. Ставим top по JUI Origin.
#   (4) КОМБОБОКСЫ 1721/1723. JuiSkin: Bk_ComboBox Type="ComboBox", ListBoxName="DarkGrey_List";
#       ES0 = тело (кадры SS0 normal FF213739 / SS1 hover FF225777 / SS2 press FF153549),
#       ES1 = кнопка-стрелка (n_02.tga). Оба плана УЖЕ импортированы (MaxMap_*_37_0/_37_1) вместе с
#       подписью MaxMap_*_Text — это и есть штатный закрытый вид. Импортёр вдобавок положил сверху
#       UMG-ComboBoxString (самодельный вид) — СВОРАЧИВАЕМ его.
#       Раскрытый список строим из скина DarkGrey_List (ES0: n_02.tga, UV 0.285 0.816 .. 0.371 0.840),
#       строка = высота ячейки комбобокса из скина (Rect "0 0 89 20" → 20 px), подсветка строки =
#       кадр SS1 тела комбобокса (n_01.tga, UV 0.137 0.391 .. 0.141 0.395, Diffuse FF225777).
#       Число строк = сколько кладёт эталон: континент 2 (CControl_SetScrollPos(...,2)),
#       область до 10 (CControl_SetScrollPos(...,10)).
#
#   (5) СТРЕЛКА КОМБОБОКСОВ. ES1 скина = Rect "90 0 110 20" (20x20 на N_02.tga), а generic-импортёр
#       масштабирует ВСЕ планы пропорционально контролу (160/110) → стрелка растянута до 29x20.
#       Возвращаем нативные 20x20 у ПРАВОГО края контрола; тело (сплошная заливка) тянется до неё.
#   (6) КНОПКА 1724 «Где я?» (WkBtn_W140). Подпись: CaptionInfo align = 5 = hCenter|vCenter — ставим
#       CENTER (импортёр применил только вертикаль). Плюс кнопке возвращается её скин (кадры
#       normal/hover/pressed из NE_skillbar2) — импортёр их не применил, кнопка была NoDraw.
#
#   Запуск: UnrealEditor-Cmd.exe <project> -ExecutePythonScript=<этот файл> -unattended -NullRHI
#   Сухой прогон: `--dry-run` / ASDA_WORLDMAP_DRYRUN=1 / ASDA_POSTSTEPS_DRYRUN=1 (`asda_ui_step_dryrun`, 07.10.2026):
#   логика та же, запись ассета подавлена, итог — строка «[ШАГ-ДЕЛЬТА] asda_fix_worldmap_ui: изменено узлов N».
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ui_step_dryrun as SD   # общий сухой режим шагов пост-импорта (07.10.2026)

B = unreal.AsdaUmgBuilder
EAL = unreal.EditorAssetLibrary

# Собственный ключ сухого прогона шага (см. `asda_ui_step_dryrun.is_dry`).
DRYRUN_ENV = "ASDA_WORLDMAP_DRYRUN"

PATH = "/Game/AsdaAssets/UI/Player_UI/Map/WBP_MAXMAP_WIN"
TEX = "/Game/AsdaAssets/Textures/JUI"
FONT_PX = 8                      # тот же кегль, что у всех текстов окна (дамп: size=8.0)
ROW_H = 20                       # высота строки = высота ячейки Bk_ComboBox (JuiSkin ES0 Rect "0 0 89 20")


def V2(x, y):
    return unreal.Vector2D(x, y)


def BOX(u0, v0, u1, v1):
    return unreal.Box2D(V2(u0, v0), V2(u1, v1))


def s2l(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def LC(argb):
    """0xAARRGGBB -> LinearColor (RGB sRGB->linear — закон ui-srgb-gamma-rule)."""
    a = ((argb >> 24) & 0xFF) / 255.0
    return unreal.LinearColor(s2l(((argb >> 16) & 0xFF) / 255.0),
                              s2l(((argb >> 8) & 0xFF) / 255.0),
                              s2l((argb & 0xFF) / 255.0), a)


WHITE = unreal.LinearColor(1, 1, 1, 1)
CLEAR = unreal.LinearColor(0, 0, 0, 0)

# ★ 07.10.2026: узлы, видимость которых ВЕДЁТ этот шаг (читает `asda_ui_sync_own_visibility`, ревизия
#   `label_band_fixes_REVIEW.md` МЕЛК-1). (узел, причина). Синхронизатор их не раскрывает и не гасит.
VISIBILITY_NODES = (
    # (4) родные UMG-комбобоксы 1721/1723 свёрнуты: вместо них штатный список из скина Bk_ComboBox
    ("MaxMap_Continent", "родной ComboBoxString свёрнут, вместо него штатный список MaxMap_Continent_List"),
    ("MaxMap_Area", "родной ComboBoxString свёрнут, вместо него штатный список MaxMap_Area_List"),
) + tuple(
    # (1) белая рамка окна 1710: планы корня показаны (окно показывает код, рамка рисуется вместе с ним)
    ("MAXMAP_WIN_0_%d" % _i, "план рамки окна 1710 показан шагом (рисуется вместе с окном)") for _i in range(8)
)


def step_visibility_nodes():
    """★ 07.10.2026: [(путь окна, узел, причина)] — узлы, видимость которых ведёт шаг (без загрузки ассетов)."""
    return [(PATH, n, why) for n, why in VISIBILITY_NODES]


def main():
    dry = SD.is_dry(DRYRUN_ENV)                                  # сухой прогон (запись подавлена)
    delta = SD.WidgetDelta("asda_fix_worldmap_ui", dry)          # учёт правок шага по снимкам дерева
    bp = EAL.load_asset(PATH)
    delta.take(bp)
    root = B.get_root_canvas(bp)


    def find(name):
        def walk(w):
            try:
                kids = w.get_all_children()
            except Exception:
                return None
            for c in kids:
                if c.get_name() == name:
                    return c
                r = walk(c)
                if r:
                    return r
            return None
        return walk(root)


    def set_off(w, l, t, sx, sy):
        s = w.slot
        lay = s.get_editor_property("layout_data")
        lay.offsets = unreal.Margin(l, t, sx, sy)
        s.set_editor_property("layout_data", lay)


    def tex(name):
        p = "%s/%s" % (TEX, name)
        return unreal.load_asset(p) if EAL.does_asset_exist(p) else None


    log = unreal.log
    changed = []

    # ── (1) Белая рамка окна: планы 9-slice узла 1710 были Collapsed (JUI Visible="0") ───────────────
    for i in range(8):
        w = find("MAXMAP_WIN_0_%d" % i)
        if w and w.get_editor_property("visibility") == unreal.SlateVisibility.COLLAPSED:
            w.set_editor_property("visibility", unreal.SlateVisibility.HIT_TEST_INVISIBLE)
            changed.append("показан план рамки MAXMAP_WIN_0_%d" % i)

    # ── (2) Хит-тест прямоугольника окна (JUI NoSelect="0", rect 59 40 690x519) ──────────────────────
    hit = find("MAXMAP_WIN_Hit")
    if not hit:
        hit = B.add_image(bp, root, "MAXMAP_WIN_Hit", V2(59, 40), V2(690, 519), None,
                          BOX(0, 0, 1, 1), CLEAR, False, unreal.Margin(0, 0, 0, 0))
        changed.append("добавлен прозрачный хит-прямоугольник окна MAXMAP_WIN_Hit")
    hit.set_editor_property("visibility", unreal.SlateVisibility.VISIBLE)   # ловит мышь, не рисует
    B.move_child_to_index(root, hit, 2)   # выше карты/маркеров, ниже всех контролов окна

    # ── (3) Подсказка 1730: вернуть top по JUI Origin (импортёр применил vcenter из CaptionInfo) ─────
    g = find("MaxMap_Guide")
    if g:
        off = g.slot.get_editor_property("layout_data").offsets
        if abs(off.top - 316.0) > 0.5:
            set_off(g, off.left, 316, off.right, off.bottom)
            changed.append("MaxMap_Guide: top %.0f -> 316 (JUI Origin 313 + база группы 3)" % off.top)

    # ── (4) Комбобоксы: убрать UMG-ComboBoxString, собрать штатный раскрывающийся список ─────────────
    n01 = tex("n_01")
    n02 = tex("n_02")
    if not n01 or not n02:
        log("[worldmap-ui] ВНИМАНИЕ: нет атласов n_01/n_02 — список будет без фона")

    COMBOS = [("MaxMap_Continent", 581, 170, 2),    # 2 строки (эталон SetScrollPos(...,2))
              ("MaxMap_Area",      581, 238, 10)]   # до 10 строк (эталон SetScrollPos(...,10))

    for idc, cx, cy, rows in COMBOS:
        cb = find(idc)
        # ★ 07.10.2026 (МЕЛК-3): правка — только если ещё не свёрнут (иначе «правок» не 0 при каждом прогоне).
        if (cb and isinstance(cb, unreal.ComboBoxString)
                and cb.get_editor_property("visibility") != unreal.SlateVisibility.COLLAPSED):
            cb.set_editor_property("visibility", unreal.SlateVisibility.COLLAPSED)
            changed.append("свёрнут самодельный UMG-комбобокс %s" % idc)

        # клик по закрытому боксу (в оригинале мышь ловит сам контрол, NoSelect="0")
        if not find(idc + "_Click"):
            b = B.add_button(bp, root, idc + "_Click", V2(cx, cy), V2(160, 20))
            b.set_editor_property("visibility", unreal.SlateVisibility.VISIBLE)
            changed.append("добавлена клик-зона %s_Click" % idc)

        # раскрытый список (скин DarkGrey_List)
        lst = find(idc + "_List")
        if not lst:
            h = rows * ROW_H
            lst = B.add_canvas(bp, root, idc + "_List", V2(cx, cy + 20), V2(160, h))
            lst.set_editor_property("clipping", unreal.WidgetClipping.CLIP_TO_BOUNDS)
            # фон списка: DarkGrey_List ES0
            bg = B.add_image(bp, lst, idc + "_ListBg", V2(0, 0), V2(160, h), n02,
                             BOX(0.285, 0.816, 0.371, 0.840), WHITE, False, unreal.Margin(0, 0, 0, 0))
            bg.set_editor_property("visibility", unreal.SlateVisibility.HIT_TEST_INVISIBLE)
            # подсветка наведённой строки: кадр SS1 тела Bk_ComboBox (Diffuse FF225777)
            sel = B.add_image(bp, lst, idc + "_Sel", V2(0, 0), V2(160, ROW_H), n01,
                              BOX(0.137, 0.391, 0.141, 0.395), LC(0xFF225777), False,
                              unreal.Margin(0, 0, 0, 0))
            sel.set_editor_property("visibility", unreal.SlateVisibility.COLLAPSED)
            for i in range(rows):
                t = B.add_text(bp, lst, "%s_Row%d" % (idc, i), V2(5, i * ROW_H + 1), V2(150, ROW_H),
                               "", FONT_PX, WHITE, 0, False)
                t.set_editor_property("visibility", unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE)
                rb = B.add_button(bp, lst, "%s_Row%d_Click" % (idc, i), V2(0, i * ROW_H), V2(160, ROW_H))
                rb.set_editor_property("visibility", unreal.SlateVisibility.VISIBLE)
            changed.append("собран штатный список %s_List (%d строк по %d px, скин DarkGrey_List)"
                           % (idc, rows, ROW_H))
        lst.set_editor_property("visibility", unreal.SlateVisibility.COLLAPSED)

    # ── (7) ★ 09.11.2026 (пачка 20, баг 1) ПЛИТЫ СТРОК СПИСКА — ЭТАЛОННЫЙ КВАД, А НЕ СВОТЧ АТЛАСА ──────────
    #   ЭТАЛОН. Подсветку строки ListBox рисует не скин, а рендер списка (`ui_core.c:112969`): квад на прямоугольник
    #   пункта, который раскладка (`uiListGrid_layoutRows@0x12EBFC0`, ui_core.c:112605) ставит SetRect(clientLeft, rowTop,
    #   clientRight, rowTop + высота пункта) — ВСЯ ширина плана 0 списка. Цвет — D3DRS_TEXTUREFACTOR: под курсором
    #   0x96F0F0F0, у выбранного пункта ~item[+2760] = 0xFFFFFFFF (текст выбранного — чёрный).
    #   ДЕФЕКТ. Шаг (4) одел `_Sel` в UV-свотч n_01 (0.137..0.141 ≈ 2 текселя) у края непрозрачной области атласа:
    #   растянутый на 160 px, он закрашивал ~2/5 строки слева, остальное прозрачно (живой дамп: узел 160×20,
    #   видно ~67 px). Плита = сплошной белый квад, цвет ставит код (UAsdaMinimapSubsystem::SetComboHover,
    #   ключ `asda.WorldMap.ListPlateEtalon`). Вторая плита `_Cur` — выбранный пункт (в шаге (4) её не было).
    white = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    for idc, _cx, _cy, _rows in COMBOS:
        lst = find(idc + "_List")
        sel = find(idc + "_Sel")
        if not lst or not sel or not white:
            log("[worldmap-ui] ВНИМАНИЕ: (7) нет %s_List/_Sel или белой текстуры — плиты не правлены" % idc)
            continue
        br = sel.get_editor_property("brush")
        if br.get_editor_property("resource_object") != white:
            br.set_editor_property("resource_object", white)
            # UV-регион в Python не экспонирован (`AsdaUmgBuilder.h`, DescribeCheckBoxStyle) — на сплошной белой текстуре
            #   любой участок белый, поэтому прежний свотч-регион безвреден.
            br.set_editor_property("tint_color", unreal.SlateColor(WHITE))
            br.set_editor_property("draw_as", unreal.SlateBrushDrawType.IMAGE)
            sel.set_editor_property("brush", br)
            changed.append("%s_Sel: кисть — сплошной белый квад на строку (был UV-свотч n_01 ≈2 текселя)" % idc)
        if not find(idc + "_Cur"):
            cur = B.add_image(bp, lst, idc + "_Cur", V2(0, 0), V2(160, ROW_H), white, BOX(0, 0, 1, 1), WHITE, False,
                              unreal.Margin(0, 0, 0, 0))
            cur.set_editor_property("visibility", unreal.SlateVisibility.COLLAPSED)
            B.move_child_to_index(lst, cur, 1)   # над фоном списка, под плитой наведения и текстом строк
            changed.append("добавлена плита выбранного пункта %s_Cur (белый квад, эталон ~item[+2760])" % idc)

    # ── z-порядок: списки — поверх контролов, но клик-область карты остаётся самой верхней ──────────
    click = find("MaxMap_ClickArea")
    last = root.get_children_count() - 1
    for idc, _cx, _cy, _r in COMBOS:
        lst = find(idc + "_List")
        cbtn = find(idc + "_Click")
        if cbtn:
            B.move_child_to_index(root, cbtn, root.get_children_count() - 1)
        if lst:
            B.move_child_to_index(root, lst, root.get_children_count() - 1)
    if click:
        B.move_child_to_index(root, click, root.get_children_count() - 1)

    # ── (5) ★ СТРЕЛКА КОМБОБОКСА — НАТИВНЫЙ РАЗМЕР, ПРИВЯЗКА К ПРАВОМУ КРАЮ ──────────────────────────
    #   JuiSkin Bk_ComboBox: ES0 Rect "0 0 89 20" (тело), ES1 Rect "90 0 110 20" (стрелка ↓ на N_02.tga,
    #   РОВНО 20x20). Шаблон скина = 110x20, контролы 1721/1723 = 160x20.
    #   БАГ: generic-импортёр (plane_screen_rects) масштабирует ВСЕ планы пропорционально
    #   контролу → стрелка стала 160/110 = 1.45x по X: 29x20 вместо 20x20 (её и видно растянутой).
    #   Тело — сплошная заливка (UV = крошечный свотч, Diffuse FF213739), его тянуть МОЖНО и нужно;
    #   стрелка — картинка, её тянуть НЕЛЬЗЯ. Оригинал держит стрелку у правого края шаблона
    #   (ES1 кончается ровно на 110 = ширина скина) → в контроле она садится на правый край:
    #     x = ctrlX + ctrlW - 20, size 20x20, по вертикали по центру строки.
    #     тело = ctrlX .. стрелка-1  →  ширина = ctrlW - (110 - 89) = ctrlW - 21.
    COMBO_PLANES = [("MaxMap_Continent", "MaxMap_Continent_37_0", "MaxMap_Continent_37_1",
                     "MaxMap_Continent_Text", 581, 170),
                    ("MaxMap_Area",      "MaxMap_Area_38_0",      "MaxMap_Area_38_1",
                     "MaxMap_Area_Text",      581, 238)]
    CB_W, CB_H = 160, 20          # размер контрола из JUI (Size = "160 20")
    ARROW_W, ARROW_H = 20, 20     # ES1 Rect "90 0 110 20" — нативный размер стрелки
    SKIN_W, BODY_W = 110, 89      # шаблон скина Bk_ComboBox и ширина его тела (ES0)

    for idc, body_n, arrow_n, text_n, cx, cy in COMBO_PLANES:
        arrow = find(arrow_n)
        if arrow:
            off = arrow.slot.get_editor_property("layout_data").offsets
            ax = cx + CB_W - ARROW_W
            ay = cy + (CB_H - ARROW_H) // 2
            if abs(off.left - ax) > 0.5 or abs(off.right - ARROW_W) > 0.5 or abs(off.bottom - ARROW_H) > 0.5:
                set_off(arrow, ax, ay, ARROW_W, ARROW_H)
                changed.append("%s: стрелка %.0fx%.0f -> нативные %dx%d у правого края (x=%d)"
                               % (arrow_n, off.right, off.bottom, ARROW_W, ARROW_H, ax))
        body = find(body_n)
        if body:
            off = body.slot.get_editor_property("layout_data").offsets
            bw = CB_W - (SKIN_W - BODY_W)      # 160 - 21 = 139
            if abs(off.right - bw) > 0.5:
                set_off(body, cx, cy, bw, CB_H)
                changed.append("%s: тело %.0f -> %d px (до левого края стрелки)" % (body_n, off.right, bw))
        # подпись комбобокса лежит НА ТЕЛЕ (CaptionPoint "5 1") → не должна залезать под стрелку
        txt = find(text_n)
        if txt:
            off = txt.slot.get_editor_property("layout_data").offsets
            tw = (cx + CB_W - (SKIN_W - BODY_W)) - off.left     # правый край тела - левый край текста
            if abs(off.right - tw) > 0.5:
                set_off(txt, off.left, off.top, tw, off.bottom)
                changed.append("%s: ширина подписи %.0f -> %.0f (по телу, не под стрелкой)"
                               % (text_n, off.right, tw))

    # ── (6) ★ КНОПКА 1724 «Где я?» (MaxMap_MyPosBtn, скин WkBtn_W140) ────────────────────────────────
    #   (а) ВЫРАВНИВАНИЕ ПОДПИСИ. JUI 1724: CaptionInfo = "3 0 ff323232 5" → маска 5 = 0x1|0x4 =
    #       hCenter + vCenter (у ЭТОГО контрола блок EXS пуст — это PushButton, а не Text-контрол,
    #       поэтому выравнивание берётся из CaptionInfo; JuiSkin WkBtn_W140 подтверждает: CaptionAlign="5"
    #       во ВСЕХ трёх кадрах). Импортёр применил только вертикаль ((23-14)/2 = 4.5 → top 8),
    #       а по горизонтали оставил LEFT. Ставим CENTER и растягиваем прямоугольник подписи на ВЕСЬ
    #       прямоугольник кнопки (4,3,140,23): при hCenter смещение CaptionPoint.x = 3 не участвует —
    #       оно задаёт отступ ЛЕВОГО выравнивания (иконка WkBtn_IconMinus слева).
    #   (б) HOVER/PRESSED. Генерик-импортёр не смог применить скин (атлас NE_skillbar2 на тот момент
    #       не был импортирован) и оставил NoDraw-кнопку без единого кадра — у оригинала же WkBtn_W140
    #       рисует СВОЁ лицо поверх рамки Btn_CBase, тремя кадрами:
    #         SS0 normal  UV 0.449 0.570 .. 0.996 0.660, Diffuse FFF0F0F0
    #         SS1 hover   тот же UV,                      Diffuse FFFFFFFF
    #         SS2 pressed UV 0.449 0.566 .. 0.996 0.656,  Diffuse FFC0C0C0
    #       Виджет НЕ пересоздаём (C++ ищет его по имени) — одеваем существующий (ApplyButtonSkin).
    btn = find("MaxMap_MyPosBtn")
    nesk = tex("ne_skillbar2")
    if btn and nesk:
        if B.apply_button_skin(btn, nesk,
                               BOX(0.449, 0.570, 0.996, 0.660), LC(0xFFF0F0F0),
                               BOX(0.449, 0.570, 0.996, 0.660), LC(0xFFFFFFFF),
                               BOX(0.449, 0.566, 0.996, 0.656), LC(0xFFC0C0C0)):
            changed.append("MaxMap_MyPosBtn: применён скин WkBtn_W140 (normal/hover/pressed, ne_skillbar2)")
        btn.set_editor_property("visibility", unreal.SlateVisibility.VISIBLE)   # ловит мышь и рисует кадры
    elif btn:
        log("[worldmap-ui] ВНИМАНИЕ: нет атласа ne_skillbar2 — у MaxMap_MyPosBtn не будет hover/pressed")

    mypos_txt = find("MaxMap_MyPosBtn_Text")
    if mypos_txt:
        off = mypos_txt.slot.get_editor_property("layout_data").offsets
        if abs(off.left - 4.0) > 0.5 or abs(off.right - 140.0) > 0.5:
            set_off(mypos_txt, 4, off.top, 140, off.bottom)     # прямоугольник кнопки (4,3,140,23)
            changed.append("MaxMap_MyPosBtn_Text: прямоугольник подписи -> прямоугольник кнопки (4,140)")
        if mypos_txt.get_editor_property("justification") != unreal.TextJustify.CENTER:
            mypos_txt.set_editor_property("justification", unreal.TextJustify.CENTER)
            changed.append("MaxMap_MyPosBtn_Text: выравнивание LEFT -> CENTER (CaptionInfo align=5)")
        # закон «подписи не перехватывают мышь»: иначе у кнопки пропадают hover/pressed
        if mypos_txt.get_editor_property("visibility") != unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE:
            mypos_txt.set_editor_property("visibility", unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE)
            changed.append("MaxMap_MyPosBtn_Text: -> SelfHitTestInvisible (прозрачна для кликов)")

    # ★ 07.10.2026 (ревизия `poststeps_align_fix_REVIEW.md`, МЕЛК-3): окно компилируется и пишется, ТОЛЬКО если снимок
    #   шага видит правки (снимок полный: слоты, видимость, порядок, кисти/стили целиком). Ноль — файл не трогается.
    SD.save_if_changed(bp, dry, "asda_fix_worldmap_ui", delta, compile_fn=B.compile_and_save)
    log("[worldmap-ui] ГОТОВО, правок: %d%s" % (len(changed), " (СУХОЙ)" if dry else ""))
    for c in changed:
        log("[worldmap-ui]   - " + c)
    delta.finish()


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
#   (До 2026-08-06 всё тело лежало на верхнем уровне — импорт из общего пост-импорт-раннера
#   молча перестраивал бы окно карты мира.)
if __name__ == "__main__":
    main()
