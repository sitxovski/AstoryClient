# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_missing_interactive.py — ★★★ НЕДОСТАЮЩИЕ ИНТЕРАКТИВНЫЕ УЗЛЫ (класс дефектов S2).
#
# ПРОБЛЕМА (аудит `Reports/audit_runtime_widget_creation.md:55`, мой пересчёт совпал).
#   Импортёр `asda_jui_generic_umg.py` создавал кнопку/чекбокс/радио/комбо ТОЛЬКО при непустом
#   `ID` контрола (`if node.idc and stype in (...)`). У контрола с ПУСТЫМ `ID`, но интерактивным
#   типом скина, в собранном `WBP_*` оставались одни планы-картинки: элемент НАРИСОВАН, а нажать
#   не по чему. Это функциональный дефект, а не косметика.
#   По всем четырём `.ini` (пересчёт тем же парсером, что у импортёра) таких контролов
#   **151 в 24 окнах**: `PushButton` 87, `CheckButton` 58, `RadioButton` 4, `ComboBox` 2 —
#   цифра аудита подтвердилась ровно. Из них 113 лежат в СВЁРНУТОМ поддереве (`Visible = "0"`
#   у предка) — они всё равно нужны: рантайм разворачивает вкладку/карточку и ждёт там кнопку.
#
# ЧТО ДЕЛАЕТ ШАГ (идемпотентно: существующий узел не трогает и не дублирует):
#   кладёт В СУЩЕСТВУЮЩИЙ ассет интерактивный виджет ровно того вида и ровно с той геометрией,
#   какие сделал бы импортёр, будь у контрола `ID`:
#     PushButton, 1 план  → `AddStyledButton` (кадры SS0/SS1/SS2 плана = покой/наведение/нажатие)
#                           + состояния подписи; план-картинку контрола ГАСИМ — её теперь рисует
#                           сама кнопка (иначе двойной слой, как у 8-й галочки чата);
#     PushButton, N планов→ `AddButton` (прозрачная ловушка клика; картинку рисуют планы);
#     CheckButton         → `AddCheckBox` (кадры SS0..SS3) + гашение плана-картинки;
#     RadioButton         → по кнопке `{имя}_OPT{i}` на КАЖДЫЙ план (мульти-выбор, планы = визуал);
#     ComboBox / EditBox  → `AddComboBox` / `AddEditableText`.
#
# ★★★ ДОБАВЛЕНО 2026-08-27 — КЛАСС S3 «ДЕРЖАТЕЛЬ ИКОНКИ» (тот же дефект, третий вид узла).
#   У контрола пустой `ID` и скин-картинка (`NoImage_icon`, Type = "Text", один текстурный план):
#   виджет в ассете ЕСТЬ, но зовётся по СКИНУ с индексом обхода (`NoImage_icon_<idx>_0`), а индекс
#   сдвигается от любой правки данных — адресовать такую ячейку из кода нельзя. Шаг кладёт рядом
#   тот же `Image` с именем по ОБЩЕМУ правилу `{ЯКОРЬ}_ICO{NN}` и гасит безымянный план.
#   Список берётся из таблицы `asda_jui_parse.ICON_HOLDER_NODES` (только разобранное по эталону):
#   сейчас это 35 гнёзд самоцветов строк выдачи аукциона 13000 (13521…13585).
#
# ИМЯ УЗЛА — ОБЩЕЕ ПРАВИЛО `asda_jui_parse.anchored_names`: `{ЯКОРЬ}_{BTN|CHK|SEL|CMB|EDT|ICO}{NN}`,
#   где ЯКОРЬ — `ID` ближайшего предка. ТО ЖЕ правило теперь применяет и сам импортёр (пре-проход
#   `_IACTNAME`), поэтому после перегенерации узлы уже на месте и шаг отрабатывает вхолостую.
#
# ★★★ ГЛАВНЫЙ РИСК — ПЕРЕХВАТ МЫШИ. Новый узел кладётся ПОСЛЕДНИМ в свою панель, то есть ВЫШЕ
#   соседей, и мог бы съесть их клики (в проекте это уже ломало магазин снаряжения и склад).
#   Защита — в общем правиле `asda_jui_parse.missing_interactive_nodes`, а не здесь:
#     • контрол, кликабельный виджет которому УЖЕ строит код порта, в работу не берётся
#       (список `PORT_RUNTIME_OWNED` с точными ссылками `файл:строка`);
#     • ВИДИМЫЙ контрол, чей прямоугольник пересекается с прямоугольником другого ВИДИМОГО
#       интерактива окна, в работу не берётся;
#     • свёрнутый (`Collapsed`) виджет мышь не ловит — у скрытых контролов пересечения не
#       проверяются, они воспроизводят раскладку оригинала как есть.
#   Отсеянные печатаются в лог и в отчёт — это осознанный отказ, а не потеря.
#
# ★★★ РЕВИЗИЯ 2026-08-27 («занятые» контролы, решение владельца «статика живёт в виджете»):
#   из `PORT_RUNTIME_OWNED` вышли 5 контролов (2617, 68357, 68511, 50392, 69081) — вместе со
#   снятием кода порта, который строил им виджет или ловил клик покадровым хит-тестом.
#   Стало 133 узла. Заодно шаг ЧИНИТ ПОРЯДОК СЛОЁВ у узлов первой волны: styled-кнопка
#   кладётся в панель последней и закрывала бы собственную подпись (см. `sink_under_caption`).
#
# Запуск:      run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_ui_missing_interactive.py
#   сухой:     set ASDA_MISSIACT_DRYRUN=1
#   подробно:  set ASDA_MISSIACT_VERBOSE=1
import importlib.util
import os
import traceback

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
TEX_DEST = "/Game/AsdaAssets/Textures/JUI"
SKIN_INI = r"@@ASTORY_DEV_ROOT@@\RE\docs\JuiSkin.ini"

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder

# ★★★ ПОВТОРНОГО КОДА НЕТ. Разбор данных и правило имён — в `asda_jui_parse`; поиск панели-
#   контейнера, индекс виджетов, сверка слота, анкор и карта ассетов — в уже проверенном шаге
#   `asda_ui_missing_text_nodes` (оба модуля БЕЗ побочных эффектов при импорте, закон
#   `tools-no-import-side-effects`). Отсюда берётся ровно то, что у шагов общее.
_spec = importlib.util.spec_from_file_location("J_missiact", os.path.join(HERE, "asda_jui_parse.py"))
J = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(J)

_spec2 = importlib.util.spec_from_file_location("M_missiact",
                                                os.path.join(HERE, "asda_ui_missing_text_nodes.py"))
M = importlib.util.module_from_spec(_spec2)
_spec2.loader.exec_module(M)

FONT_PX = 8                        # кегль UI импортёра (`asda_jui_generic_umg.FONT_PX`)
WHITE = unreal.LinearColor(1, 1, 1, 1)


def log(m):
    unreal.log("[S2-интерактив] %s" % m)


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


def V2(x, y):
    return unreal.Vector2D(float(x), float(y))


def BOX(uv):
    return unreal.Box2D(unreal.Vector2D(uv[0], uv[1]), unreal.Vector2D(uv[2], uv[3]))


def LC(c):
    return unreal.LinearColor(c[0], c[1], c[2], c[3])


_texcache = {}


def load_tex(base):
    """Текстура плана. ⚠ В ОТЛИЧИЕ от `ensure_texture` импортёра НИЧЕГО НЕ ИМПОРТИРУЕТ: шаг
    работает по уже собранным окнам, где все планы контрола давно нарисованы, значит их атласы
    в проекте есть. Отсутствие атласа — сигнал о рассинхроне, а не повод тянуть файл с диска."""
    if not base:
        return None
    if base in _texcache:
        return _texcache[base]
    p = "%s/%s" % (TEX_DEST, base)
    t = unreal.load_asset(p) if EAL.does_asset_exist(p) else None
    if t is None:
        unreal.log_warning("[S2-интерактив] нет текстуры %s" % p)
    _texcache[base] = t
    return t


def jui_frame(pl, i):
    """Кадр-состояние `i` плана → `unreal.AsdaJuiFrame` (текстура + UV + Diffuse-тинт).
    Разбор кадра — общий с импортёром (`asda_jui_parse.plane_frame`)."""
    tn, uv, dif = J.plane_frame(pl, i)
    fr = unreal.AsdaJuiFrame()
    fr.set_editor_property("texture", load_tex(tn))
    fr.set_editor_property("uv", BOX(uv))
    fr.set_editor_property("tint", LC(dif))
    return fr


def collapse(w):
    try:
        w.set_editor_property("visibility", unreal.SlateVisibility.COLLAPSED)
        return True
    except Exception:                                            # noqa: BLE001
        return False


def widget_names_of(name, stype, planes):
    """★★★ ИМЕНА ВИДЖЕТОВ, которые появятся в ассете для этого узла.

    ⚠ Совпадают с `name` НЕ ВСЕГДА: у `RadioButton` каждый план — отдельный ВЫБОР, и импортёр
    делает по кнопке `{name}_OPT{i}` на план, а виджета с именем `{name}` не существует вовсе.
    Без этого списка проверка «узел уже есть?» у радио всегда давала «нет», и повторный прогон
    плодил дубли (поймано идемпотентным прогоном: 3 радио в `SYSTEM_Option_NEW`).
    """
    if stype == "RadioButton":
        return ["%s_OPT%d" % (name, i) for i in range(max(len(planes), 1))]
    return [name]


def make_widget(bp, panel, name, node, stype, planes, pos, size):
    """Создать интерактивный виджет ТОЧНО так, как это сделал бы `add_control` импортёра.

    Возврат: `(список созданных виджетов, гасить_ли_план0)`. Второй флаг = импортёр в этом случае
    план 0 картинкой НЕ рисует (его рисует сам виджет кадрами), значит уже лежащую в ассете
    картинку надо свернуть — иначе двойной слой (ровно грабли 8-й галочки чата,
    `AsdaChatTabEditor.cpp:748-761`).
    """
    made = []
    if stype == "PushButton":
        if len(planes) == 1:
            t0, u0, d0 = J.plane_frame(planes[0], 0)
            t1, u1, d1 = J.plane_frame(planes[0], 1)
            t2, u2, d2 = J.plane_frame(planes[0], 2)
            tex0 = load_tex(t0)
            if tex0:
                btn = B.add_styled_button(bp, panel, name, pos, size, tex0,
                                          BOX(u0), LC(d0), BOX(u1), LC(d1), BOX(u2), LC(d2), "")
                if btn:
                    made.append(btn)
                    args = J.caption_state_args(node, planes[0])
                    if args:
                        c0, c1, c2, off0, off2 = args
                        try:
                            B.apply_button_caption_states(btn, LC(c0), LC(c1), LC(c2),
                                                          V2(off0[0], off0[1]), V2(off2[0], off2[1]))
                        except Exception as e:                   # noqa: BLE001
                            unreal.log_warning("[S2-интерактив] состояния подписи %s: %s" % (name, e))
                return made, True
        btn = B.add_button(bp, panel, name, pos, size)
        if btn:
            made.append(btn)
            if planes:
                args = J.caption_state_args(node, planes[0])
                if args:
                    c0, c1, c2, off0, off2 = args
                    try:
                        B.apply_button_caption_states(btn, LC(c0), LC(c1), LC(c2),
                                                      V2(off0[0], off0[1]), V2(off2[0], off2[1]))
                    except Exception as e:                       # noqa: BLE001
                        unreal.log_warning("[S2-интерактив] состояния подписи %s: %s" % (name, e))
        return made, False
    if stype == "CheckButton":
        if not planes:
            return made, False
        w = B.add_check_box(bp, panel, name, pos, size,
                            jui_frame(planes[0], 0),    # normal / снято
                            jui_frame(planes[0], 1),    # наведение
                            jui_frame(planes[0], 2),    # отмечено / нажато
                            jui_frame(planes[0], 3))    # отмечено + наведение
        if w:
            made.append(w)
        return made, True
    if stype == "RadioButton":
        for pj, (rx, ry, rw, rh, p) in enumerate(J.plane_screen_rects(planes, size.x, size.y)):
            w = B.add_option_button(bp, panel, "%s_OPT%d" % (name, pj),
                                    V2(pos.x + rx, pos.y + ry), V2(rw, rh),
                                    jui_frame(p, 1), jui_frame(p, 2))
            if w:
                made.append(w)
        return made, False                      # у радио планы-картинки НУЖНЫ (визуал выбора)
    if stype == "ComboBox":
        w = B.add_combo_box(bp, panel, name, pos, size)          # опции задаёт рантайм
        if w:
            made.append(w)
        return made, False
    if stype == "EditBox":
        w = B.add_editable_text(bp, panel, name, pos, size, "", FONT_PX, WHITE, False)
        if w:
            made.append(w)
        return made, False
    # ★★★ КЛАСС S3 — ДЕРЖАТЕЛЬ ИКОНКИ (`asda_jui_parse.ICON_HOLDER_NODES`). Виджет тот же, что
    #   делает импортёр плану контрола (`add_control`: `B.add_image(... plane_frame(p, 0) ...)`),
    #   но с ИМЕНЕМ по общему правилу `anchored_names` — иначе ячейку не адресовать. Безымянный
    #   план на том же месте гасим (второй флаг True), чтобы не было двойного слоя.
    if stype == "IconHolder":
        if not planes:
            return made, False
        tn, uv, dif = J.plane_frame(planes[0], 0)
        tex = load_tex(tn)
        if not tex:
            return made, False
        w = B.add_image(bp, panel, name, pos, size, tex, BOX(uv), LC(dif), False,
                        unreal.Margin(0, 0, 0, 0))
        if w:
            made.append(w)
        return made, True
    return made, False


def plane0_hidden_by_step(stype, planes):
    """Гасит ли шаг лежащий в ассете план 0 контрола — второй флаг `make_widget` (без учёта того,
    нашлась ли текстура: объявление для синхронизатора намеренно с запасом)."""
    if stype == "PushButton":
        return len(planes) == 1
    if stype in ("CheckButton", "IconHolder"):
        return bool(planes)
    return False


def step_visibility_nodes():
    """★ 07.10.2026 (ревизия `Reports/owner_1007/label_band_fixes_REVIEW.md`, МЕЛК-1): узлы, видимость которых
    ВЕДЁТ этот шаг. Читает `asda_ui_sync_own_visibility` — их он не раскрывает и не гасит.
    Возврат — [(путь окна, узел, причина)]: план 0 `{name}_{order}_0` контролов класса S2/S3, который шаг
    сворачивает, потому что его кадры рисует созданный виджет (иначе двойной контур). Узлы, созданные самим
    шагом, сюда не входят: в эталонной песочнице импортёра их нет, синхронизатор их и так не трогает.
    Окна сцены логина пропускаются (их шаг не правит). Только чтение ассетов."""
    skins = J.parse_skins_full(SKIN_INI)
    amap = M.asset_map()
    all_roots = {}
    for cls in M.PRIORITY:
        ini = M.ini_of(cls)
        if os.path.isfile(ini):
            all_roots[cls] = J.parse_jui(ini)
    coll = M.collisions(all_roots)
    out = []
    for cls in M.PRIORITY:
        for root in all_roots.get(cls, []):
            base = M.san(root.idc or root.name)
            J.compute_anchors(root)
            entries, _skipped = J.missing_interactive_nodes(root, skins, base)
            icons = J.missing_icon_nodes(root, skins, base)
            if icons:
                entries = list(entries) + icons
            if not entries:
                continue
            paths = amap.get("WBP_" + base + M.wsuffix(base, cls, coll)) or []
            if not paths or _login_blocked(paths[0]):
                continue
            # Только планы, которые в ассете ЕСТЬ: у части контролов импортёр плана-картинки не делает
            #   (шаг тогда пишет «плана нет — гасить нечего»), объявлять несуществующий узел незачем.
            bp = EAL.load_asset(paths[0])
            canvas = B.get_root_canvas(bp) if bp else None
            if canvas is None:
                continue
            have, _par = M.widget_index(canvas)
            order, _parent_of, _hidden = M.index_nodes(root)
            for node, _p, name, stype, planes in entries:
                pname = "%s_%d_0" % (M.san(node.name), order[id(node)])
                if plane0_hidden_by_step(stype, planes) and pname in have:
                    out.append((paths[0], pname,
                                "план 0 под созданным %s %s — кадры рисует сам виджет" % (stype, name)))
    return out


# ★ 08.10.2026 (решение владельца, ревизия `Reports/owner_1007/poststeps_idempotent_REVIEW.md` §6): окна, у которых
#   шаг чинит план 0 под УЖЕ СУЩЕСТВУЮЩИМ держателем иконки (класс S3). Синхронизатор видимости 17.09 раскрыл в
#   WBP_EXCHANGE_WIN 35 планов `NoImage_icon_*_0` под держателями `EXCHANGER_BB{2..8}_0_ICO00..04` — двойной слой; ветка
#   «узел уже есть» их не гасила. Закрытый список: прочие окна — только с разрешения владельца.
PLANE_REPAIR_WINDOWS = {"WBP_EXCHANGE_WIN"}


def _brush_key(img):
    """Кисть картинки целиком (ресурс, UV-область, тинт, режим) текстом `export_text` — для сверки «план = держатель»
    (тот же кадр на том же месте). UV-область питоном отдельно не читается (свойство `uv_region` не выставлено)."""
    try:
        return img.get_editor_property("brush").export_text()
    except Exception:                                            # noqa: BLE001
        return None


def _slot_key(w):
    """Смещения слота канваса (L, T, R, B) — для сверки места."""
    try:
        s = w.get_editor_property("slot")
        if not isinstance(s, unreal.CanvasPanelSlot):
            return None
        o = s.get_offsets()
        return (round(o.left, 2), round(o.top, 2), round(o.right, 2), round(o.bottom, 2))
    except Exception:                                            # noqa: BLE001
        return None


def repair_plane_under_holder(wname, name, node, order, stype, planes, wid, par, dry):
    """★ Ремонт для УЖЕ существующего держателя иконки (класс S3): его безымянный план 0 на том же месте с тем же
    кадром должен быть свёрнут (так шаг делает при создании держателя — «чтобы не было двойного слоя»).
    Пишет ТОЛЬКО при полном совпадении пары (тот же родитель, те же смещения слота, та же кисть) и только если план
    сейчас не свёрнут; любое расхождение — отказ без записи (чужой вид узла). Окна — `PLANE_REPAIR_WINDOWS`.
    Возврат: ('свёрнут'|'отказ: …', имя плана) или None, если ремонт не нужен."""
    if stype != "IconHolder" or not planes or wname not in PLANE_REPAIR_WINDOWS:
        return None
    pname = "%s_%d_0" % (M.san(node.name), order[id(node)])
    img, holder = wid.get(pname), wid.get(name)
    if not isinstance(img, unreal.Image) or not isinstance(holder, unreal.Image):
        return None
    if img.get_editor_property("visibility") == unreal.SlateVisibility.COLLAPSED:
        return None                                              # уже свёрнут — идемпотентно
    if par.get(pname) is not par.get(name):
        return ("отказ: разные родители", pname)
    if _slot_key(img) is None or _slot_key(img) != _slot_key(holder):
        return ("отказ: место не совпало %s / %s" % (_slot_key(img), _slot_key(holder)), pname)
    if _brush_key(img) is None or _brush_key(img) != _brush_key(holder):
        return ("отказ: кисть не совпала %s / %s" % (_brush_key(img), _brush_key(holder)), pname)
    if not dry and not collapse(img):
        return ("отказ: свернуть не удалось", pname)
    return ("свёрнут", pname)


def sink_under_caption(wname, btn_name, node, order, stype, planes, wid, par, dry):
    """★★★ ПОДПИСЬ КОНТРОЛА ОБЯЗАНА ОСТАТЬСЯ ПОВЕРХ ЕГО КНОПКИ.

    Правило импортёра: «Текст рисуется ПОВЕРХ кнопки (добавлен позже = выше по z)»
    (`asda_jui_generic_umg.py`, комментарий у цикла `for wgt in made`) — там кнопка создаётся
    ДО подписи. Наш шаг работает по УЖЕ СОБРАННОМУ ассету и кладёт кнопку ПОСЛЕДНЕЙ в панель,
    то есть ВЫШЕ подписи; кисть `AddStyledButton` непрозрачная и закрыла бы текст.
    Возвращаем эталонный порядок слоёв, опуская кнопку под соседей: `z_order = -1`.
    ⚠ Мышь это не трогает: подписи `SelfHitTestInvisible`, клик проходит сквозь них к кнопке.
    ⚠ Применяется и к УЖЕ СУЩЕСТВУЮЩИМ узлам — первая волна шага (128 узлов, 2026-08-27) клала
      кнопку поверх подписи, и у 50 контролов текст был закрыт. Правка идемпотентна: пишем
      только при реальном расхождении.
    Возврат: имя подписи, если z поправлен, иначе None.
    """
    # Условие ровно то же, что у гашения плана: рисует САМ виджет ⇒ кисть непрозрачна.
    if not ((stype == "CheckButton" and planes) or (stype == "PushButton" and len(planes) == 1)):
        return None
    cname = "%s_%d_txt" % (M.san(node.name), order[id(node)])
    cap, btn = wid.get(cname), wid.get(btn_name)
    if cap is None or btn is None or par.get(cname) is not par.get(btn_name):
        return None
    try:
        bs = btn.get_editor_property("slot")
        cs = cap.get_editor_property("slot")
    except Exception:                                            # noqa: BLE001
        return None
    if not isinstance(bs, unreal.CanvasPanelSlot) or not isinstance(cs, unreal.CanvasPanelSlot):
        return None
    if int(bs.get_editor_property("z_order")) < int(cs.get_editor_property("z_order")):
        return None                                              # уже ниже — не трогаем
    if not dry:
        bs.set_editor_property("z_order", int(cs.get_editor_property("z_order")) - 1)
    return cname


def main():
    dry = bool(os.environ.get("ASDA_MISSIACT_DRYRUN"))
    verbose = bool(os.environ.get("ASDA_MISSIACT_VERBOSE"))
    skins = J.parse_skins_full(SKIN_INI)
    amap = M.asset_map()

    all_roots = {}
    for cls in M.PRIORITY:
        ini = M.ini_of(cls)
        if os.path.isfile(ini):
            all_roots[cls] = J.parse_jui(ini)
        else:
            log("НЕТ .ini класса %s (%s)" % (cls, ini))
    coll = M.collisions(all_roots)

    planned = added = existed = 0
    hidden_made = collapsed_planes = 0
    sunk_buttons = []                 # кнопки, опущенные под собственную подпись (z = -1)
    repaired_planes = []              # ★ 08.10.2026: планы 0, свёрнутые под уже существующим держателем иконки
    refused_planes = []               # ★ 08.10.2026: пара «план — держатель» не сошлась — без записи
    wins_touched = 0
    refused = []                      # осознанный отказ (порт / перехват мыши)
    no_asset, no_panel, bad_check, no_widget = [], [], [], []
    login_skipped = []   # ★ 15.09.2026 (раздел 14): окна сцены логина, пропущенные защитой

    for cls in M.PRIORITY:
        for root in all_roots.get(cls, []):
            base = M.san(root.idc or root.name)
            J.compute_anchors(root)
            entries, skipped = J.missing_interactive_nodes(root, skins, base)
            # ★★★ КЛАСС S3 (держатели иконок из таблицы `ICON_HOLDER_NODES`) идёт ТЕМ ЖЕ шагом и
            #   ТЕМ ЖЕ правилом имён — отдельного механизма не заводим (закон `port-shared-points`).
            icons = J.missing_icon_nodes(root, skins, base)
            if icons:
                entries = list(entries) + icons
            for n, nm, st, why in skipped:
                refused.append("%s / %s (%s, IDNum %d): %s" % (base, nm, st, n.idnum, why))
            if not entries:
                continue
            planned += len(entries)
            wname = "WBP_" + base + M.wsuffix(base, cls, coll)
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

            order, parent_of, hidden = M.index_nodes(root)
            wid, par = M.widget_index(canvas)
            made_here = 0
            fixed_here = 0            # правок z-порядка у уже существовавших узлов

            for node, _p, name, stype, planes in entries:
                want = widget_names_of(name, stype, planes)
                have = [x for x in want if x in wid]
                if have:
                    existed += 1
                    if len(have) != len(want):
                        unreal.log_warning("[S2-интерактив] %s / %s: в ассете %d из %d виджетов "
                                           "(%s) — узел собран НЕ ПОЛНОСТЬЮ"
                                           % (wname, name, len(have), len(want), ", ".join(have)))
                    # ★ Ремонт первой волны: кнопка могла лежать ПОВЕРХ собственной подписи.
                    cn = sink_under_caption(wname, want[0], node, order, stype, planes, wid, par, dry)
                    if cn:
                        sunk_buttons.append("%s / %s (под подписью %s) — РЕМОНТ" % (wname, name, cn))
                        fixed_here += 1
                    # ★ 08.10.2026: план 0 под уже существующим держателем иконки — свернуть (см. `repair_plane_under_holder`).
                    rp = repair_plane_under_holder(wname, name, node, order, stype, planes, wid, par, dry)
                    if rp:
                        verdict, pn = rp
                        if verdict == "свёрнут":
                            repaired_planes.append("%s / %s под %s" % (wname, pn, name))
                            fixed_here += 1
                        else:
                            unreal.log_warning("[S2-интерактив] %s / %s под %s: %s — без записи" % (wname, pn, name, verdict))
                            refused_planes.append("%s / %s: %s" % (wname, pn, verdict))
                    continue
                panel, ox, oy = M.container_of(node, parent_of, order, wid, par, canvas)
                if panel is None:
                    no_panel.append("%s / %s (IDNum %d)" % (wname, name, node.idnum))
                    continue
                chk = M._check_panel(panel, node, parent_of, order, canvas)
                if chk is False:
                    bad_check.append("%s / %s: числа слота панели %s не сошлись"
                                     % (wname, name, panel.get_name()))
                    continue
                l, t, w, h = node.abs
                pos, size = V2(l - ox, t - oy), V2(w, h)
                if verbose:
                    log("  %s / %-30s %-12s → %-28s (%.0f,%.0f %.0fx%.0f) планов %d%s"
                        % (wname, name, stype, panel.get_name(), pos.x, pos.y, size.x, size.y,
                           len(planes), "  [свёрнут]" if hidden[id(node)] else ""))
                if dry:
                    added += 1
                    if hidden[id(node)]:
                        hidden_made += 1
                    continue
                made, hide_plane = make_widget(bp, panel, name, node, stype, planes, pos, size)
                if not made:
                    no_widget.append("%s / %s (%s): виджет не создан" % (wname, name, stype))
                    continue
                for wgt in made:
                    wid[wgt.get_name()] = wgt
                    par[wgt.get_name()] = panel
                    # ★ Лист ВЕРХНЕГО уровня импортёр анкорит (`build_tree`, `is_top`) — повторяем.
                    if (not node.children) and parent_of[id(node)] is not None \
                            and parent_of[id(parent_of[id(node)])] is None:
                        M.apply_anchor(wgt, node)
                    # Контрол (или его предок) объявлен `Visible = "0"` — импортёр сворачивает.
                    if hidden[id(node)]:
                        collapse(wgt)
                # ★ План 0 рисует теперь САМ виджет (кадрами состояний) — лежащую в ассете
                #   картинку гасим, иначе под кнопкой/чекбоксом остаётся второй контур.
                if hide_plane:
                    pname = "%s_%d_0" % (M.san(node.name), order[id(node)])
                    img = wid.get(pname)
                    if isinstance(img, unreal.Image) and collapse(img):
                        collapsed_planes += 1
                    elif img is None:
                        unreal.log_warning("[S2-интерактив] %s: плана %s нет — гасить нечего"
                                           % (wname, pname))
                    # ★★★ Подпись контрола обязана остаться ПОВЕРХ кнопки — общий помощник.
                    cn = sink_under_caption(wname, name, node, order, stype, planes, wid, par, dry)
                    if cn:
                        sunk_buttons.append("%s / %s (под подписью %s)" % (wname, name, cn))
                added += 1
                made_here += 1
                if hidden[id(node)]:
                    hidden_made += 1

            if (made_here or fixed_here) and not dry:
                B.compile_and_save(bp)
                if not EAL.save_loaded_asset(bp, False):
                    raise RuntimeError("не сохранён %s" % path)
            if made_here or fixed_here:
                wins_touched += 1
                log("%-34s + %d узлов, порядок слоёв поправлен у %d" % (wname, made_here, fixed_here))

    for m in sunk_buttons:
        log("ПОРЯДОК СЛОЁВ: %s" % m)
    for m in repaired_planes:
        log("ПЛАН ПОД ДЕРЖАТЕЛЕМ%s: %s" % (" (будет свёрнут)" if dry else " свёрнут", m))
    for m in refused:
        log("ОТКАЗ: %s" % m)
    for m in no_asset:
        unreal.log_warning("[S2-интерактив] нет ассета: %s" % m)
    for m in no_panel:
        unreal.log_warning("[S2-интерактив] не найдена панель: %s" % m)
    for m in no_widget:
        unreal.log_warning("[S2-интерактив] %s" % m)
    for m in bad_check:
        unreal.log_error("[S2-интерактив] СВЕРКА ПАНЕЛИ НЕ ПРОШЛА: %s" % m)
    if login_skipped:   # ★ 15.09.2026 (раздел 14): итог пропуска окон сцены логина одной строкой
        unreal.log_warning("[LOGIN-GUARD] [S2-интерактив] окон сцены логина пропущено: %d (%s) — правка только с явного разрешения "
                           "владельца и ASDA_ALLOW_LOGIN_WRITE=1" % (len(login_skipped), ", ".join(login_skipped)))
    log("ГОТОВО%s: по данным нужно %d узлов, создано %d (из них свёрнутых %d), уже было %d, "
        "погашено планов %d, опущено под подпись %d, окон затронуто %d, осознанный отказ %d, "
        "без ассета %d, без панели %d, виджет не создан %d, сверка не прошла %d, "
        "планов под держателем свёрнуто %d (отказ %d)"
        % ("  [СУХОЙ ПРОГОН]" if dry else "", planned, added, hidden_made, existed,
           collapsed_planes, len(sunk_buttons), wins_touched, len(refused), len(no_asset),
           len(no_panel), len(no_widget), len(bad_check), len(repaired_planes), len(refused_planes)))
    if bad_check or no_panel or no_widget:
        raise RuntimeError("S2: панель не подтвердилась у %d, не найдена у %d, виджет не создан у %d"
                           % (len(bad_check), len(no_panel), len(no_widget)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает —
#   раннер пост-импорта (`asda_ui_post_import.py`) сам зовёт `main()`.
if __name__ == "__main__":
    try:
        main()
    except Exception:                                            # noqa: BLE001
        unreal.log_error("[S2-интерактив] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
        raise
