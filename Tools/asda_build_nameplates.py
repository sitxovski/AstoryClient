# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_nameplates.py — генерация НАД-ГОЛОВНЫХ плашек (неймплейтов) 1:1 из JUI-окон gamemain.
#
# ПОЧЕМУ ОТДЕЛЬНЫЙ ГЕНЕРАТОР (а не общий asda_jui_generic_umg):
#   Общий генератор раскладывает контролы ЭКРАННЫМИ анкорами (доля от 1024×768) — это верно для HUD-окон,
#   но неверно для над-головных плашек: они позиционируются В МИРЕ (проекция головы сущности), а контролы
#   должны стоять в ЛОКАЛЬНЫХ координатах относительно ЯКОРЯ окна. Иначе текст рисуется со смещением в
#   сотни пикселей от точки привязки и «уплывает» от модели при движении камеры.
#
# ЭТАЛОН (JUI gamemain, Origin/Size контролов относительно якоря окна — окна имеют Size "0 0"):
#   TARGETMARK_MON  (1600): имя 1603 (-24,20) 150×16 центр; HP-подложка 1602 (0,36) 102×6;
#                           внутри неё 4 сегмента HP 1601/1604/1605/1606 (1,1) 100×4 (red/yellow/green/purple).
#   TARGETMARK_MAN  (1620): гильдия 1622 (-105,-14) 210×16 [+иконка 1623 (0,0) 16×16]; имя 1621 (-105,16) 210×16;
#                           HPBACK 1625 (-51,33) 102×6 [HPBase 1630 / HPBuff 1629 / HP 1624 / HPCAP 1626 (1,1) 100×4];
#                           титул 1627 (-105,1) 210×16; ранг 1628 (-16,-50) 32×35; clanmark 1631 (-29,5) 24×24
#                           [R/B/G 1632/1633/1634 (-5,0) 5×24]; G1 1681 (-5,5) 24×24.
#   TARGETMARK_MAN1 (1660): гильдия 1662 (-105,-14) [иконка 1663]; имя 1661 (-105,16) 210×16; титул 1667 (-105,1);
#                           ранг 1668 (-16,-49) 32×35; HPBACK1 1665 (-51,33) 102×6 [HP1 1664 (1,1) 100×4];
#                           clanmark2 1669 (-29,5) [R/B/G 1670/1671/1672]; G2 1682 (-5,5).
#
# Якорь окна кладём в ЦЕНТР канваса (ANCHOR_X, ANCHOR_Y) → компонент-виджет в UE ставится пивотом (0.5,0.5)
#   в проекцию точки над головой, и вся раскладка садится 1:1 как в оригинале.
#
# Запуск (headless): UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=asda_build_nameplates.py
import sys, os
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal
import asda_jui_parse as J

B = unreal.AsdaUmgBuilder
PKG = "/Game/AsdaAssets/UI/GameMain"

# Размер канваса плашки и положение якоря окна внутри него (px базы JUI).
CANVAS_W, CANVAS_H = 260.0, 160.0
ANCHOR_X, ANCHOR_Y = 130.0, 80.0

# ★ РАЗМЕР ШРИФТА (px базы 1024×768). Оригинал рисует ПИКСЕЛЬНЫЙ атлас (FONT0.BIN+DATA0.TEX) 1:1,
#   поэтому размер = высота глифа атласа. ЗАМЕР (font_and_name_colors_re.md): FONT0.BIN — глиф 14 px,
#   сетка ячеек 12×12, CAP-HEIGHT «H» = 8 px, шаг строки 12 px, масштабирования НЕТ (пиксель-в-пиксель).
#   Наш ассет шрифта — Arial (cap = 0.716 em, em = Size·96/72): Size 13 → cap 12.4 px = в 1.55× крупнее.
#   Для cap 8 px → Size 8. Меняется ЗДЕСЬ одним числом → перегенерация плашек.
FONT_PX = 8


def V2(x, y): return unreal.Vector2D(float(x), float(y))
def LC(r, g, b, a=1.0): return unreal.LinearColor(r, g, b, a)
def BOX(uv): return unreal.Box2D(unreal.Vector2D(uv[0], uv[1]), unreal.Vector2D(uv[2], uv[3]))


def log(m): unreal.log_warning("[nameplate] %s" % m)


def load_tex(path):
    if not path: return None
    return unreal.load_asset(path)


def skin_stroke(bp, canvas, name, pos, size, skins, skin_name, thickness=1.0):
    """РАМКА-обводка по скину типа Box_White1Stroke: 9-slice, центральная ячейка ES4 = прозрачный
       пиксель атласа ⇒ виден только контур. В UMG — Border-браш (Margin = толщина, центр пустой)."""
    sk = J.resolve_skin(skins, skin_name) if skin_name else None
    tex = None; uv = (0.0, 0.0, 1.0, 1.0); tint = (1, 1, 1, 1)
    if sk:
        p = sk.get("props", sk)
        texname = p.get("tex_name") or p.get("tex") or ""
        if texname:
            base = os.path.splitext(os.path.basename(str(texname).replace("\\", "/")))[0]
            tex = load_tex("/Game/AsdaAssets/Textures/JUI/%s.%s" % (base, base))
        if p.get("uv"): uv = p["uv"]
        if p.get("diffuse"): tint = p["diffuse"]
    return B.add_stroke_box(bp, canvas, name, V2(*pos), V2(*size), tex, BOX(uv), LC(*tint), float(thickness))


def skin_image(bp, canvas, name, pos, size, skins, skin_name):
    """Картинка по имени скина JUI (HP_RedBar/Box_White/…): текстура+UV из JuiSkin."""
    sk = J.resolve_skin(skins, skin_name) if skin_name else None
    tex = None; uv = (0.0, 0.0, 1.0, 1.0); tint = (1, 1, 1, 1)
    if sk:
        p = sk.get("props", sk)
        # ★ ЦВЕТ ПОЛОСЫ = слой ЗАЛИВКИ (ES1) у ProgressBar-скинов: HP_RedBar ES1 = FFFF0A00 (красный),
        #   ES0 — белая подложка. Раньше брали ES0 → полоса выглядела бледно-розовой, фон серым.
        bFill = bool(p.get("fill_diffuse") or p.get("fill_uv"))
        texname = (p.get("fill_tex_name") if bFill else None) or p.get("tex_name") or p.get("tex") or ""
        if texname:
            base = os.path.splitext(os.path.basename(str(texname).replace("\\", "/")))[0]
            tex = load_tex("/Game/AsdaAssets/Textures/JUI/%s.%s" % (base, base))   # атлас JUI (был неверный путь UI/)
        uv_src   = p.get("fill_uv") if bFill else p.get("uv")
        dif_src  = p.get("fill_diffuse") if bFill else p.get("diffuse")
        if uv_src: uv = uv_src
        if dif_src: tint = dif_src
    # ★ ПОЛОСЫ/ПОДЛОЖКИ РИСУЕМ СПЛОШНЫМ ЦВЕТОМ (без текстуры): в оригинале это однопиксельные квады
    #   атласа, тонированные Diffuse. Тонировка ЧЕРЕЗ атлас давала блёклый результат (пиксель UV не
    #   чисто-белый) — «светло-красная полоса, серый фон». Цвет = Diffuse слоя заливки (ES1) либо ES0.
    if sk and tint != (1, 1, 1, 1):
        return B.add_image(bp, canvas, name, V2(*pos), V2(*size), None, BOX((0, 0, 1, 1)), LC(*tint), False, NO_M)
    if tex is None:
        return B.add_image(bp, canvas, name, V2(*pos), V2(*size), None, BOX((0, 0, 1, 1)), LC(*tint), False, NO_M)
    return B.add_image(bp, canvas, name, V2(*pos), V2(*size), tex, BOX(uv), LC(*tint), False, NO_M)


def build(asset_name, ctrls, skins):
    """ctrls: список (имя, тип, Origin(x,y), Size(w,h), skin|цвет, parent_offset)."""
    # ★ Пересоздание: существующий ассет надо СНАЧАЛА удалить (как в asda_jui_to_umg.py),
    #   иначе create_widget_bp вернёт None и генерация молча не применится.
    full = "%s/%s" % (PKG, asset_name)
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        unreal.EditorAssetLibrary.delete_asset(full)
    bp = B.create_widget_bp(PKG, asset_name)
    if not bp:
        log("НЕ создан BP %s" % asset_name); return None
    canvas = B.get_root_canvas(bp)
    made = 0
    for c in ctrls:
        nm, kind, (ox, oy), (w, h), skin, (px, py) = c
        x = ANCHOR_X + px + ox
        y = ANCHOR_Y + py + oy
        if kind == "text":
            # Текст: шрифт 13 (как генерик-окна), центр (JUI TextAlign=5), без wrap (однострочные подписи).
            B.add_text(bp, canvas, nm, V2(x, y), V2(w, h), "", FONT_PX, LC(1, 1, 1, 1), 1, False)
        elif kind == "stroke":
            skin_stroke(bp, canvas, nm, (x, y), (w, h), skins, skin)
        else:
            skin_image(bp, canvas, nm, (x, y), (w, h), skins, skin)
        made += 1
    # ★ ШРИФТ+ОБВОДКА ПРЯМО В ГЕНЕРАТОРЕ (обводка «слетала»: её накатывал отдельный asda_restyle_ui_font.py,
    #   а перегенерация плашек пересоздаёт WBP с нуля и теряла её — юзер 2026-07-29). Эталон
    #   (font_and_name_colors_re.md «Обводка текста», TextFont_BuildGlyphQuad@0x12C5B80): крест ±1 px,
    #   цвет жёстко 0xFF000000, обводка стоит на плашках (имя/гильдия/титул).
    #   ★ 2026-09-24: начертание REGULAR — как весь UI. Жирной гарнитуры в оригинале нет НИГДЕ:
    #   атлас `JUIData/font/FONT0.BIN` собран `CreateFontA(..., cHeight 14, вес 400, "Arial")`
    #   (`fontBuildGlyphAtlasForLang`, font_text.c:6515-6600; `boldFlag` = 0, ui_core.c:106690),
    #   что подтверждено побайтовым подбором по живому файлу (Arial/14/400 — 222 глифа из 223).
    #   Плашкам положена именно ОБВОДКА крестом ±1 px, а не жирное начертание.
    B.restyle_text_fonts(bp, FONT_PX, unreal.Name("Regular"))
    B.apply_text_outline(bp, 1, unreal.LinearColor(0.0, 0.0, 0.0, 1.0))
    B.compile_and_save(bp)
    # ★ CompileAndSave только компилит+dirty; НА ДИСК пишет Python (как в asda_jui_to_umg.py).
    #   Без этого ассет живёт только в памяти коммандлета → в игре «класс не найден», плашек нет.
    saved = unreal.EditorAssetLibrary.save_loaded_asset(bp)
    log("%s: контролов %d, сохранён=%s" % (asset_name, made, saved))
    return bp


def main():
    skins = J.load_skin()
    log("скинов=%d" % len(skins))

    # ── TARGETMARK_MON (1600): цель-моб — имя + 4-сегментная полоса HP ──
    mon = [
        ("TARGETMARK_MONNAME",      "text",  (-24, 20), (150, 16), None,              (0, 0)),
        ("TARGETMARK_MONGAUGEBACK", "image", (0, 36),   (102, 6),  "Box_DarkGrey",    (0, 0)),
        ("TARGETMARK_MONGAUGE",     "image", (1, 1),    (100, 4),  "HP_RedBar",       (0, 36)),
        ("TARGETMARK_MONGAUGE1",    "image", (1, 1),    (100, 4),  "BoostYellow_Bar", (0, 36)),
        ("TARGETMARK_MONGAUGE2",    "image", (1, 1),    (100, 4),  "HP_GreenBar",     (0, 36)),
        ("TARGETMARK_MONGAUGE3",    "image", (1, 1),    (100, 4),  "HP_PurpleBar",    (0, 36)),
    ]
    build("WBP_NAMEPLATE_MON", mon, skins)

    # ── TARGETMARK_MAN (1620): панель выбранной цели-игрока ──
    man = [
        ("TARGETMARK_GUILD",       "text",  (-105, -14), (210, 16), None,               (0, 0)),
        ("TARGETMARK_GUILDICON",   "image", (0, 0),      (16, 16),  "NoImage_icon",     (-105, -14)),
        ("TargtMark_ManTitle",     "text",  (-105, 1),   (210, 16), None,               (0, 0)),
        ("TARGETMARK_MANNAME",     "text",  (-105, 16),  (210, 16), None,               (0, 0)),
        ("TARGETMARK_HPBACK",      "image", (-51, 33),   (102, 6),  "Box_DarkGrey",     (0, 0)),
        ("TARGETMARK_HPBase",      "image", (1, 1),      (100, 4),  "Box_DarkGrey",     (-51, 33)),
        ("TARGETMARK_HPBuff",      "image", (1, 1),      (80, 4),   "BoostYellow_Bar",  (-51, 33)),
        ("TARGETMARK_HP",          "image", (1, 1),      (100, 4),  "HP_RedBar",        (-51, 33)),
        ("TARGETMARK_HPCAP",       "image", (1, 1),      (100, 4),  "Box_LightYellow",  (-51, 33)),
        # ★ НАША метка «(Одержим)» (в JUI такого контрола НЕТ — добавление порта, закон
        #   fix-original-defects; текст и видимость ставит UAsdaNameplateComponent::ApplyPossessTag).
        #   Ряд ПОД полосой HP: имя y16 h16, HPBACK y33 h6 (низ 39) ⇒ y40 свободен. Оригинальные
        #   контролы НЕ двигаем (закон no-invented-ui: чужую геометрию не «улучшаем»).
        ("TARGETMARK_POSSESS",     "text",  (-105, 40),  (210, 16), None,               (0, 0)),
        ("TargetMark_Rank",        "image", (-16, -50),  (32, 35),  "NoImage_icon",     (0, 0)),
        ("TARGETMARK_clanmark",    "image", (-29, 5),    (24, 24),  "NoImage_icon",     (0, 0)),
        ("TARGETMARK_clanmark_R",  "image", (-5, 0),     (5, 24),   "Box_red_1",        (-29, 5)),
        ("TARGETMARK_clanmark_B",  "image", (-5, 0),     (5, 24),   "Box_blue_1",       (-29, 5)),
        ("TARGETMARK_clanmark_G",  "image", (-5, 0),     (5, 24),   "Box_green_1",      (-29, 5)),
    ]
    build("WBP_NAMEPLATE_MAN", man, skins)

    # ── TARGETMARK_MAN1 (1660): постоянная плашка над КАЖДЫМ игроком ──
    man1 = [
        ("TARGETMARK_GUILDNAME1",  "text",  (-105, -14), (210, 16), None,               (0, 0)),
        ("TARGETMARK_GUILDICON1",  "image", (0, 0),      (16, 16),  "NoImage_icon",     (-105, -14)),
        ("TargtMark_Man1Title",    "text",  (-105, 1),   (210, 16), None,               (0, 0)),
        ("TARGETMARK_MANNAME1",    "text",  (-105, 16),  (210, 16), None,               (0, 0)),
        ("TARGETMARK_HPBACK1",     "image", (-51, 33),   (102, 6),  "Box_DarkGrey",     (0, 0)),
        ("TARGETMARK_HP1",         "image", (1, 1),      (100, 4),  "HP_RedBar",        (-51, 33)),
        # ★ НАША метка «(Одержим)» — тот же контрол и та же геометрия, что в TARGETMARK_MAN
        #   (имя одинаковое во всех плашках игроков, чтобы рантайм искал один ключ).
        ("TARGETMARK_POSSESS",     "text",  (-105, 40),  (210, 16), None,               (0, 0)),
        ("TargetMark_Rank1",       "image", (-16, -49),  (32, 35),  "NoImage_icon",     (0, 0)),
        ("TARGETMARK_clanmark2",   "image", (-29, 5),    (24, 24),  "NoImage_icon",     (0, 0)),
        # ★ СЛОИ ЭМБЛЕМЫ КЛАНА (порт бейка CMiniMap_BakeIconToRenderTarget без RT): контрол 1669 рисует
        #   на плашке вырезку RT (23,12)+64×42 → раскладываем композицию слоями ВНУТРИ контрола 24×24:
        #   _FLAGBG = полотно (Clear-цвет BgColor), _FLAGBORDER = контур-вымпел (тинт BorderColor),
        #   _FLAGSIGN = знак: в RT он (27,14)+57×38 → в вырезке (4,2)+57×38 из 64×42 → на 24×24 ниже.
        #   Текстуры/UV ставит рантайм (UpdateFactionUI); тут пустые слоты правильной геометрии.
        ("TARGETMARK_clanmark2_FLAGBG",     "image", (0, 0),     (24, 24),   None, (-29, 5)),
        ("TARGETMARK_clanmark2_FLAGBORDER", "image", (0, 0),     (24, 24),   None, (-29, 5)),
        ("TARGETMARK_clanmark2_FLAGSIGN",   "image", (1.5, 1.1), (21.4, 21.7), None, (-29, 5)),
        ("TARGETMARK_clanmark2_R", "image", (-5, 0),     (5, 24),   "Box_red_1",        (-29, 5)),
        ("TARGETMARK_clanmark2_B", "image", (-5, 0),     (5, 24),   "Box_blue_1",       (-29, 5)),
        ("TARGETMARK_clanmark2_G", "image", (-5, 0),     (5, 24),   "Box_green_1",      (-29, 5)),
        # ★ Контролы БЕЗ ID в JUI (idnum 1673/1674, поля CCharacter +988/+992) — обводки камп-марки:
        #   1673 = рамка 6×24 вокруг цветной полосы лагеря (видна, когда лагерь есть),
        #   1674 = рамка 24×24 вокруг иконки звания/эмблемы (видна всегда при показе марки).
        #   Скин Box_White1Stroke = 9-slice с ПРОЗРАЧНЫМ центром ⇒ только контур (add_stroke_box).
        ("TARGETMARK_clanmark2_1673", "stroke", (-5, 0), (6, 24),   "Box_White1Stroke", (-29, 5)),
        ("TARGETMARK_clanmark2_1674", "stroke", (0, 0),  (24, 24),  "Box_White1Stroke", (-29, 5)),
    ]
    build("WBP_NAMEPLATE_MAN1", man1, skins)
    log("ГОТОВО. Канвас %gx%g, якорь окна (%g,%g)." % (CANVAS_W, CANVAS_H, ANCHOR_X, ANCHOR_Y))


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global NO_M
    NO_M = unreal.Margin(0, 0, 0, 0)
    main()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
