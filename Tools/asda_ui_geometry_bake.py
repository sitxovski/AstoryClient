# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_geometry_bake.py — ★★★ ЗАПЕКАНИЕ СТАТИЧНОЙ ГЕОМЕТРИИ В АССЕТЫ.
#
# ЗАЧЕМ. Аудит `Reports/audit_runtime_geometry.md` насчитал 254 места, где порт правит
#   геометрию виджетов В РАНТАЙМЕ, и отобрал 34 из них как «В АССЕТ»: значение статично
#   (константа, число из `.JUI` или копия чужого слота), рантайм-состояние на него не влияет.
#   Такая правка каждый раз выполняется заново при открытии окна, живёт в C++ и не видна в
#   редакторе — владелец требует «★★★ ПРАВИТЬ В АССЕТЕ, А НЕ КОДОМ».
#
# ★★★ ПОЧЕМУ ОТДЕЛЬНЫЙ ИДЕМПОТЕНТНЫЙ ШАГ, А НЕ РУЧНАЯ ПРАВКА В РЕДАКТОРЕ.
#   `asda_jui_generic_umg.py` ПЕРЕСОБИРАЕТ окно с нуля — всё доложенное после импорта пропадает
#   молча (регресс 2026-08-06, шапка `asda_ui_post_import.py`). Поэтому шаг зарегистрирован в
#   `asda_ui_post_import.STEPS`, а созданные узлы — в `HANDMADE`, чтобы прогон ПАДАЛ, если шаг
#   не отработал.
#
# ЧТО ЗАПЕКАЕТСЯ (каждый пункт перепроверен дампом `AsdaUmgBuilder.DumpWidgetTree`, а не только
#   отчётом; из кода соответствующий блок удалён, на его месте оставлен комментарий):
#
#   G1. EXP-БАР `WBP_EXPBar_WIn` — класс П6 отчёта (мода выравнивания «доля экрана» не переносится
#       импортёром). Подложка `ExpBar_Base` и рамка `ExpBar_TopLine` в оригинале растягиваются на
#       ШИРИНУ ЭКРАНА (`HUD_UpdateExpBar`, эталон `ui_core.c:75284-75290`), а импортёр оставил их
#       фикс-пиксельными (1034.2 и 1044.6 px) → на широком мониторе полоса обрывалась.
#       11 меток шкалы `Box_DarkGrey_*` стояли через 102.4 px, т.е. только в первых 1024 px.
#       Запекаем: подложка/рамка → span-анкор X 0..1 с нулевыми отступами; метка i → point-анкор
#       X = i/(N−1), сдвиг `Left` = 0 / −1 / −2 (центр метки шириной 2 px на риске).
#       Было: `AsdaHudSubsystem.cpp::SetupExpControls` (лямбда `StretchFull` + цикл по меткам).
#
#   G2. `LOGIN_CHECK_HIT` в `WBP_LOGIN_WIN` — прозрачная кнопка на ВСЮ строку чекбокса «сохранить
#       ID» (в оригинале кликается вся строка, сам `UCheckBox` 18×19 клика не давал). Слот
#       копировался 1:1 у плана-строки `LOGIN_CHECK_BASE_18_0` → чистая константа.
#       Было: `AsdaLoginGameMode.cpp` (поиск базы по префиксу + конструирование кнопки).
#
#   G3. `SERVER_TitleTxt` / `CHANNEL_TitleTxt` в `WBP_SERVER_SELECT_WIN` — класс П1 (у контрола
#       пустой `ID`, узла в ассете нет). Титулы этапов «Выбор сервера» / «Выбор канала» кладутся
#       поверх баров `SERVER_LIST` / `CHANNEL_LIST`, слот копируется у бара. Сам ТЕКСТ остаётся
#       за кодом: он приходит из таблицы локализации (`FAsdaLoginData::Loc(114/115)`), это данные,
#       а не геометрия.
#
#   G4. `QUEST_INVEN{0..11}_HIT` в `WBP_QUEST_WIN` — 12 прозрачных клик-реле поверх планов-иконок
#       журнала заданий (класс П7). Позиция = позиция плана, размер 32×32 (`.JUI Size = "32 32"`,
#       в ассете планы уже ровно 32×32). Привязка делегатов остаётся в коде.
#
#   G5. Подписи имён в `WBP_Package_Contents_Win` — класс П1: у БОЛЬШИХ ячеек коробки `CaptionText`
#       пуст, поэтому импортёр не создал `TextBlock`, и подписи достраивались в рантайме
#       (`EnsureNameLabel`). Геометрия — константы `kNameOffset (0,68)` / `kNameSize (276,20)`,
#       стиль — копия единственной существующей подписи `Package_BagName0_Text`.
#       ★ Подсистема ищет подпись НЕ ПО ИМЕНИ, а по ширине слота (276 против 64 у числа на иконке),
#       поэтому запечённый узел подхватывается штатным путём.
#
# ЧЕГО ЗДЕСЬ НЕТ И ПОЧЕМУ — см. раздел «ОТКЛОНЕНО» в конце файла.
#
# Запуск:    Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_ui_geometry_bake.py
#   сухой:   set ASDA_GEOMBAKE_DRYRUN=1   (ничего не пишет, только считает)
import os
import traceback

import unreal
# ★ дедуп №2 В1 Т12: общие помощники правки WBP (обход дерева, канвасный слот, смещения) — asda_ui_widget_common
import os as _uwc_os, sys as _uwc_sys
if _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)) not in _uwc_sys.path:
    _uwc_sys.path.insert(0, _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)))
import asda_ui_widget_common as _UWC   # noqa: E402

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder

DRYRUN = os.environ.get("ASDA_GEOMBAKE_DRYRUN", "") not in ("", "0")
EPS = 0.01

EXPBAR = "/Game/AsdaAssets/UI/Player_UI/HUD/WBP_EXPBar_WIn"
LOGINWIN = "/Game/AsdaAssets/UI/Login/WBP_LOGIN_WIN"
SRVSEL = "/Game/AsdaAssets/UI/Login/WBP_SERVER_SELECT_WIN"
QUESTWIN = "/Game/AsdaAssets/UI/Player_UI/Quest/WBP_QUEST_WIN"
PKGWIN = "/Game/AsdaAssets/UI/Player_UI/Premium/WBP_Package_Contents_Win"

FONT_PX = 8                  # кегль UI импортёра (`asda_jui_generic_umg.FONT_PX`)
LOGIN_TITLE_PX = 14          # кегль титулов этапов логина (был `FSlateFontInfo(UiFont, 14, Regular)`)
HALIGN_CENTER = 1            # ETextJustify::Center


def log(msg):
    unreal.log("[геом-бейк] %s" % msg)


def warn(msg):
    unreal.log_warning("[геом-бейк] %s" % msg)


# ─────────────────────────── общие помощники по дереву виджетов ───────────────────────────
# ★ Свойство `UWidgetBlueprint.widget_tree` из UE-Python защищено (шапка `asda_ui_dump_window.py`),
#   поэтому дерево берём через C++-хелпер `UAsdaUmgBuilder.get_root_canvas` и обходим руками.
walk = _UWC.walk   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def name_of(w):
    try:
        return w.get_name()
    except Exception:
        return ""


def find(nodes, name):
    for w in nodes:
        if name_of(w) == name:
            return w
    return None


canvas_slot = _UWC.canvas_slot   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def layout_of(w):
    """(anchors, offsets, alignment) слота-канваса или None."""
    s = canvas_slot(w)
    if s is None:
        return None
    lay = s.get_editor_property("layout_data")
    return (lay.get_editor_property("anchors"), lay.get_editor_property("offsets"),
            lay.get_editor_property("alignment"))


def set_layout(w, anchors=None, offsets=None):
    """Записать анкоры/оффсеты в слот. Возвращает True, если что-то РЕАЛЬНО изменилось."""
    s = canvas_slot(w)
    if s is None:
        return False
    lay = s.get_editor_property("layout_data")
    cur_a = lay.get_editor_property("anchors")
    cur_o = lay.get_editor_property("offsets")
    changed = False
    if anchors is not None and not same_anchors(cur_a, anchors):
        changed = True
    if offsets is not None and not same_margin(cur_o, offsets):
        changed = True
    if not changed or DRYRUN:
        return changed
    if anchors is not None:
        lay.set_editor_property("anchors", anchors)
    if offsets is not None:
        lay.set_editor_property("offsets", offsets)
    s.set_editor_property("layout_data", lay)
    return True


def set_z(w, z):
    s = canvas_slot(w)
    if s is None:
        return
    if DRYRUN:
        return
    try:
        s.set_editor_property("z_order", z)
    except Exception:
        pass


def same_margin(m, want):
    """Оба аргумента — `unreal.Margin`."""
    return (abs(m.left - want.left) < EPS and abs(m.top - want.top) < EPS
            and abs(m.right - want.right) < EPS and abs(m.bottom - want.bottom) < EPS)


def same_anchors(a, want):
    """Оба аргумента — `unreal.Anchors`."""
    return (abs(a.minimum.x - want.minimum.x) < 1e-4 and abs(a.minimum.y - want.minimum.y) < 1e-4
            and abs(a.maximum.x - want.maximum.x) < 1e-4 and abs(a.maximum.y - want.maximum.y) < 1e-4)


def anchors(minx, miny, maxx, maxy):
    return unreal.Anchors(unreal.Vector2D(minx, miny), unreal.Vector2D(maxx, maxy))


def margin(l, t, r, b):
    return unreal.Margin(l, t, r, b)


def save(bp, path, touched):
    """Сохранение ассета. ⚠ `CompileAndSave` ИМЯ ВРЁТ — она только компилирует и метит пакет
    грязным (`AsdaUmgBuilder.cpp`), запись на диск делает `save_loaded_asset`."""
    if not touched or DRYRUN:
        return
    B.compile_and_save(bp)
    EAL.save_loaded_asset(bp)
    log("сохранено: %s" % path)


# ══════════════════════════════ G1. EXP-БАР ══════════════════════════════
def bake_expbar():
    bp = EAL.load_asset(EXPBAR)
    if not bp:
        warn("G1: ассет %s не найден" % EXPBAR)
        return 0
    root = B.get_root_canvas(bp)
    if not root:
        warn("G1: нет корневого канваса")
        return 0
    nodes = walk(root)

    changed = 0
    # Подложка и рамка: span по X внутри своей группы, отступы от краёв = 0.
    #   Вертикаль (Top/Bottom) НЕ трогаем — она задана импортёром из `.JUI` и статична.
    for prefix in ("ExpBar_Base", "ExpBar_TopLine"):
        img = None
        for w in nodes:
            n = name_of(w)
            if n.startswith(prefix) and "_grp" not in n and isinstance(w, unreal.Image):
                img = w
                break
        if not img:
            warn("G1: план %s* не найден" % prefix)
            continue
        lay = layout_of(img)
        if lay is None:
            warn("G1: у %s не канвас-слот" % name_of(img))
            continue
        a, o, _al = lay
        want_a = (0.0, a.minimum.y, 1.0, a.maximum.y)
        want_o = (0.0, o.top, 0.0, o.bottom)
        if set_layout(img, anchors(*want_a), margin(*want_o)):
            changed += 1
            log("G1: %s → span 0..1, off(L0 T%.1f R0 B%.1f)" % (name_of(img), o.top, o.bottom))

    # 11 меток шкалы: point-анкор на доле ширины бара. Порядок берём по ЧИСЛОВОМУ индексу в имени
    #   (`Box_DarkGrey_<n>_0`) — он же порядок обхода дерева у `GetAllWidgets` в снятом коде
    #   (проверено дампом: 5,6,…,15 идут подряд и по возрастанию X: 0, 102.4, …, 1024).
    marks = []
    for w in nodes:
        n = name_of(w)
        if n.startswith("Box_DarkGrey") and isinstance(w, unreal.Image):
            parts = n.split("_")
            idx = 0
            for p in parts:
                if p.isdigit():
                    idx = int(p)
                    break
            marks.append((idx, w))
    marks.sort(key=lambda t: t[0])
    n_marks = len(marks)
    if n_marks != 11:
        warn("G1: меток шкалы %d, ожидалось 11 — метки НЕ трогаю" % n_marks)
    else:
        for i, (_idx, w) in enumerate(marks):
            fx = float(i) / float(n_marks - 1)
            lay = layout_of(w)
            if lay is None:
                continue
            a, o, _al = lay
            left = -2.0 if fx >= 1.0 else (-1.0 if fx > 0.0 else 0.0)
            if set_layout(w, anchors(fx, a.minimum.y, fx, a.maximum.y),
                          margin(left, o.top, o.right, o.bottom)):
                changed += 1
        log("G1: метки шкалы: 11 шт, анкор i/10, сдвиг 0/−1/−2")

    save(bp, EXPBAR, changed)
    return changed


def _login_blocked(path):
    """★ 15.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 13): True — пакет `path` принадлежит сцене логина (asda_login_guard: UI/Login, кроме
    LOGIN_SHARED_EXCEPTIONS, и др.), а ASDA_ALLOW_LOGIN_WRITE != 1: пакет пропустить, строка [LOGIN-GUARD] уже в логе."""
    import sys as _sys_lg
    _here = os.path.dirname(os.path.abspath(__file__))   # Tools — для asda_login_guard
    if _here not in _sys_lg.path:
        _sys_lg.path.insert(0, _here)
    from asda_login_guard import package_blocked_for   # ★ дедуп №2 Т18: имя инструмента — из __file__
    return package_blocked_for(__file__, path)


# ══════════════════════════════ G2. LOGIN_CHECK_HIT ══════════════════════════════
def bake_login_check_hit():
    if _login_blocked(LOGINWIN):   # ★ 15.09.2026: окно логина — узел только с разрешением (G1/G4/G5 — как раньше)
        return 0
    bp = EAL.load_asset(LOGINWIN)
    if not bp:
        warn("G2: ассет %s не найден" % LOGINWIN)
        return 0
    root = B.get_root_canvas(bp)
    if not root:
        return 0
    nodes = walk(root)
    if find(nodes, "LOGIN_CHECK_HIT"):
        log("G2: LOGIN_CHECK_HIT уже на месте — пропуск")
        return 0

    # База-строка чекбокса: план `LOGIN_CHECK_BASE_<n>_0`. Фолбэк — сам квадрат `LOGIN_CHECK`
    #   (ровно тот же фолбэк был в снятом коде).
    base = None
    for w in nodes:
        n = name_of(w)
        if n.startswith("LOGIN_CHECK_BASE") and "_grp" not in n and canvas_slot(w):
            base = w
            break
    if not base:
        base = find(nodes, "LOGIN_CHECK")
    lay = layout_of(base) if base else None
    if lay is None:
        warn("G2: не найдена база строки чекбокса — узел НЕ создан")
        return 0
    a, o, _al = lay
    parent = base.get_parent()
    if not isinstance(parent, unreal.PanelWidget):
        warn("G2: у %s нет панели-родителя" % name_of(base))
        return 0
    if DRYRUN:
        log("G2: СУХОЙ — создал бы LOGIN_CHECK_HIT в %s off(%.0f %.0f %.0f %.0f)"
            % (name_of(parent), o.left, o.top, o.right, o.bottom))
        return 1
    btn = B.add_button(bp, parent, "LOGIN_CHECK_HIT",
                       unreal.Vector2D(o.left, o.top), unreal.Vector2D(o.right, o.bottom))
    if not btn:
        warn("G2: add_button вернул None")
        return 0
    set_layout(btn, anchors(a.minimum.x, a.minimum.y, a.maximum.x, a.maximum.y), None)
    set_z(btn, 100)                                   # z из снятого кода
    try:
        btn.set_editor_property("is_focusable", False)
    except Exception:
        pass
    log("G2: LOGIN_CHECK_HIT создан в %s off(%.0f %.0f %.0f %.0f) z=100"
        % (name_of(parent), o.left, o.top, o.right, o.bottom))
    save(bp, LOGINWIN, 1)
    return 1


# ══════════════════════════════ G3. титулы этапов логина ══════════════════════════════
def bake_login_titles():
    if _login_blocked(SRVSEL):   # ★ 15.09.2026: окно логина — узлы только с разрешением (G1/G4/G5 — как раньше)
        return 0
    bp = EAL.load_asset(SRVSEL)
    if not bp:
        warn("G3: ассет %s не найден" % SRVSEL)
        return 0
    root = B.get_root_canvas(bp)
    if not root:
        return 0
    nodes = walk(root)
    made = 0
    # (префикс бара, имя узла) — ровно та пара, что была в снятом коде.
    for prefix, node_name in (("SERVER_LIST", "SERVER_TitleTxt"),
                              ("CHANNEL_LIST", "CHANNEL_TitleTxt")):
        if find(nodes, node_name):
            log("G3: %s уже на месте — пропуск" % node_name)
            continue
        # ★ Бар — именно Image, а не группа `*_grp`: иерархия обернула контролы в группы, и первым
        #   по префиксу идёт группа с ЧУЖИМ слотом (эта грабля описана в снятом коде).
        bar = None
        for w in nodes:
            n = name_of(w)
            if n.startswith(prefix) and "_grp" not in n and isinstance(w, unreal.Image):
                bar = w
                break
        lay = layout_of(bar) if bar else None
        parent = bar.get_parent() if bar else None
        if lay is None or not isinstance(parent, unreal.PanelWidget):
            warn("G3: бар %s* не найден — %s НЕ создан" % (prefix, node_name))
            continue
        a, o, _al = lay
        if DRYRUN:
            log("G3: СУХОЙ — создал бы %s в %s off(%.0f %.0f %.0f %.0f)"
                % (node_name, name_of(parent), o.left, o.top, o.right, o.bottom))
            made += 1
            continue
        # Текст пустой: его кладёт код из таблицы локализации (loc 114/115) — это ДАННЫЕ.
        # Цвет белый и начертание Regular — ровно как у снятого кода (`SetFont(UiFont,14,Regular)`,
        #   цвет не задавался, т.е. дефолт `UTextBlock` = белый).
        txt = B.add_text(bp, parent, node_name,
                         unreal.Vector2D(o.left, o.top), unreal.Vector2D(o.right, o.bottom),
                         "", LOGIN_TITLE_PX, unreal.LinearColor(1.0, 1.0, 1.0, 1.0),
                         HALIGN_CENTER, False, False)
        if not txt:
            warn("G3: add_text вернул None для %s" % node_name)
            continue
        set_layout(txt, anchors(a.minimum.x, a.minimum.y, a.maximum.x, a.maximum.y), None)
        # ★ 08.11.2026 (пачка 16, баг 11) [ИСПРАВЛЕНИЕ]: титул — точка в центре бара, выравнивание 0.5/0.5, размер по тексту.
        #   Слот-копия бара высотой 29 прижимал строку к верху (UTextBlock рисует от верхнего края слота); оригинал —
        #   CaptionInfo «0 0 ffc0c0c0 5», центр по обеим осям. Общая функция — asda_ui_login_title_center.py.
        import asda_ui_login_title_center as _ltc
        _ltc.center_title_slot(unreal, txt, o)
        set_z(txt, 50)
        made += 1
        log("G3: %s создан в %s off(%.0f %.0f %.0f %.0f) z=50"
            % (node_name, name_of(parent), o.left, o.top, o.right, o.bottom))
    save(bp, SRVSEL, made)
    return made


# ══════════════════════════════ G4. клик-реле ячеек журнала заданий ══════════════════════════════
# Имена планов-иконок — те же константы, что читает `UAsdaQuestLogSubsystem` (`QLogUi::SlotIcon`).
QUEST_CELLS = [
    "QUEST_INVEN0_28_0", "QUEST_INVEN1_29_0", "QUEST_INVEN2_30_0",
    "QUEST_INVEN3_31_0", "QUEST_INVEN4_32_0", "QUEST_INVEN5_33_0",
    "QUEST_INVEN6_34_0", "QUEST_INVEN7_35_0", "QUEST_INVEN8_36_0",
    "QUEST_INVEN9_37_0", "QUEST_INVEN10_38_0", "QUEST_INVEN11_39_0",
]
QUEST_CELL_PX = 32.0        # `.JUI Size = "32 32"`


def bake_quest_relays():
    bp = EAL.load_asset(QUESTWIN)
    if not bp:
        warn("G4: ассет %s не найден" % QUESTWIN)
        return 0
    root = B.get_root_canvas(bp)
    if not root:
        return 0
    nodes = walk(root)
    made = 0
    for i, cell_name in enumerate(QUEST_CELLS):
        hit_name = "QUEST_INVEN%d_HIT" % i
        if find(nodes, hit_name):
            continue
        cell = find(nodes, cell_name)
        lay = layout_of(cell) if cell else None
        parent = cell.get_parent() if cell else None
        if lay is None or not isinstance(parent, unreal.PanelWidget):
            warn("G4: ячейка %s не найдена — %s НЕ создан" % (cell_name, hit_name))
            continue
        a, o, _al = lay
        if DRYRUN:
            made += 1
            continue
        btn = B.add_button(bp, parent, hit_name,
                           unreal.Vector2D(o.left, o.top),
                           unreal.Vector2D(QUEST_CELL_PX, QUEST_CELL_PX))
        if not btn:
            warn("G4: add_button вернул None для %s" % hit_name)
            continue
        set_layout(btn, anchors(a.minimum.x, a.minimum.y, a.maximum.x, a.maximum.y), None)
        set_z(btn, 50)
        try:
            btn.set_editor_property("is_focusable", False)
        except Exception:
            pass
        made += 1
    log("G4: клик-реле ячеек журнала: создано %d из 12" % made)
    save(bp, QUESTWIN, made)
    return made


# ══════════════════════════════ G5. подписи имён в панели коробки ══════════════════════════════
PKG_NAME_OFF = (0.0, 68.0)          # `kNameOffset` снятого кода
PKG_NAME_SIZE = (276.0, 20.0)       # `kNameSize` снятого кода
PKG_PROTO = "Package_BagName0_Text"  # единственная подпись, которую импортёр создал из `.JUI`


def bake_package_names():
    bp = EAL.load_asset(PKGWIN)
    if not bp:
        warn("G5: ассет %s не найден" % PKGWIN)
        return 0
    root = B.get_root_canvas(bp)
    if not root:
        return 0
    nodes = walk(root)

    proto = find(nodes, PKG_PROTO)
    if not isinstance(proto, unreal.TextBlock):
        warn("G5: образец %s не найден — шаг пропущен (стиль брать неоткуда)" % PKG_PROTO)
        return 0
    try:
        proto_color = proto.get_editor_property("color_and_opacity").get_editor_property("specified_color")
    except Exception:
        proto_color = unreal.LinearColor(1.0, 1.0, 1.0, 1.0)

    # Плашки подписей — планы `Package_BagName{r}_*` / `Package_ItemName{r}_{k}_*` шириной 276.
    made = 0
    for w in list(nodes):
        n = name_of(w)
        if not isinstance(w, unreal.Image):
            continue
        if not (n.startswith("Package_BagName") or n.startswith("Package_ItemName")):
            continue
        lay = layout_of(w)
        if lay is None:
            continue
        _a, o, _al = lay
        if abs(o.right - PKG_NAME_SIZE[0]) > 1.0:      # это не плашка подписи
            continue
        parent = w.get_parent()
        if not isinstance(parent, unreal.PanelWidget):
            continue
        # ★ Подсистема ищет подпись НЕ по имени, а по ШИРИНЕ слота (276 у подписи против 64 у числа
        #   на иконке) — `AsdaPackagePanelSubsystem::SetupControls`. Поэтому и проверка «уже есть»
        #   идёт по геометрии, а не по имени: иначе шаг задвоил бы подпись.
        have = False
        for c in range(parent.get_children_count()):
            ch = parent.get_child_at(c)
            if isinstance(ch, unreal.TextBlock):
                cl = layout_of(ch)
                if cl is not None and cl[1].right >= PKG_NAME_SIZE[0] - 1.0:
                    have = True
                    break
        if have:
            continue
        # Имя строим от плашки: `Package_BagName1_27_0` → `Package_BagName1_Text` (та же схема,
        #   что у созданного импортёром образца `Package_BagName0_Text`).
        # Имя импортёра — `<база>_<порядковый индекс обхода>_<номер плана>`, т.е. РОВНО две
        #   числовые компоненты в хвосте. Срезать «все цифры подряд» нельзя: у ячейки предмета
        #   база сама кончается цифрой (`Package_ItemName0_1`), и подписи всех трёх ячеек блока
        #   получили бы одно имя (поймано сухим прогоном).
        parts = n.split("_")
        if len(parts) >= 4 and parts[-1].isdigit() and parts[-2].isdigit():
            parts = parts[:-2]
        node_name = "_".join(parts) + "_Text"
        if find(nodes, node_name):
            node_name = "%s_%s_Text" % ("_".join(parts), name_of(parent))
        if DRYRUN:
            log("G5: СУХОЙ — создал бы %s в %s" % (node_name, name_of(parent)))
            made += 1
            continue
        txt = B.add_text(bp, parent, node_name,
                         unreal.Vector2D(*PKG_NAME_OFF), unreal.Vector2D(*PKG_NAME_SIZE),
                         "", FONT_PX, proto_color, HALIGN_CENTER, False, True)
        if not txt:
            warn("G5: add_text вернул None для %s" % node_name)
            continue
        # Подпись мышь НЕ ловит: хит-тест ячейки идёт по ИКОНКЕ (закон selfhittest-vs-hittest-invisible).
        try:
            txt.set_editor_property("visibility", unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE)
        except Exception:
            pass
        nodes.append(txt)
        made += 1
    log("G5: подписи имён коробки: создано %d" % made)
    save(bp, PKGWIN, made)
    return made


# ─────────────────────────────────── ОТКЛОНЕНО ───────────────────────────────────
# Проверено самостоятельно, вердикт отчёта НЕ подтверждён или выгода не покрывает риск:
#
#   • `AsdaPremiumGachaSubsystem::AddPlaneButton` — узел можно завести, но кнопка привязывается к
#     КОНКРЕТНОМУ плану по УКАЗАТЕЛЮ (наведение/нажатие перекрашивают сам план), а план ищется в
#     рантайме в пяти разных окнах. В ассет ушла бы только геометрия, весь остальной код остался
#     бы — чистая выгода около нуля при реальном риске рассинхрона имён.
#   • `AsdaSkillWindowSubsystem::ApplyScrollClip` — ★ УДАЛЕНО 19.09.2026: обрезку и геометрию
#     колёсной зоны книги навыков теперь держит сам ассет (шаг `asda_ui_scroll_geometry_fix.py`),
#     а оставшийся код сдвинул бы дерево второй раз. Прежняя запись здесь («оставлено в коде»)
#     устарела и вводила бы в заблуждение.
#   • 8-я строка `AsdaChatTabEditor` — узлы-клоны 1-й строки. Клонируется не только слот, но и
#     стиль чекбокса из живого образца; окно вдобавок защищено снимком `asda_ui_handmade`
#     (ручные правки пользователя в чате). Не трогал, чтобы не спорить со снимком.
#   • `AsdaWarehouseSubsystem` (размер сумок), `AsdaInventorySubsystem` (вторая подпись вкладки) —
#     закрыты другими шагами (`asda_ware_bag58_cell_names`, `asda_ui_tabbar_captions`).


def main():
    log("=== запекание статичной геометрии%s ===" % (" (СУХОЙ ПРОГОН)" if DRYRUN else ""))
    total = 0
    for fn, title in ((bake_expbar, "G1 EXP-бар"),
                      (bake_login_check_hit, "G2 LOGIN_CHECK_HIT"),
                      (bake_login_titles, "G3 титулы логина"),
                      (bake_quest_relays, "G4 реле журнала заданий"),
                      (bake_package_names, "G5 подписи коробки")):
        try:
            n = fn()
            total += n
            log("%s: изменений %d" % (title, n))
        except Exception:
            warn("%s: ИСКЛЮЧЕНИЕ\n%s" % (title, traceback.format_exc()))
    log("=== ИТОГО изменений: %d%s ===" % (total, " (сухой прогон, диск не тронут)" if DRYRUN else ""))


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    main()
