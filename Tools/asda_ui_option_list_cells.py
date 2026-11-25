# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_option_list_cells.py — ★★★ ЯЧЕЙКИ СПИСКОВ ПОД-СТРАНИЦ ОКНА НАСТРОЕК 69500 В АССЕТЕ
#   (`WBP_SYSTEM_Option_NEW`, окно `SYSTEM_Option_NEW`).
#
# ═══ ПРОБЛЕМА (жалоба владельца 19.09.2026: «в левом списке подразделов видно ТОЛЬКО "Звук"») ═══
#   Слева на каждой вкладке окна в оригинале стоит СПИСОК подразделов — контролы 70156 (Интерфейс),
#   70006 (Система), 69517 (Клавиши) со скином `TalkBar_system_D` (`JuiSkin.ini`: `Type = "RadioButton"`,
#   один план 150×25). В данных `.JUI` у каждого — ОДНА ячейка; остальные клонирует КОД эталона при
#   инициализации окна (`uiListAppendClonedCell@0x12F5C60`: копия последней ячейки, сдвинутая вниз на
#   её высоту) и подписывает (`CListBox_SetItemString`):
#     70156 — `uiDialogInitControls`      (ui_core.c:88131-88143): 3 ячейки, loc 30400…30402;
#     70006 — `uiTradeDialogInitControls` (ui_core.c:88208-88220): 3 ячейки, loc 30501…30503;
#     69517 — `uiDlgInitControlsByID`     (ui_core.c:88344-88356): 4 ячейки, loc 30011…30014.
#   Импортёр клонов не делает (он видит только данные), поэтому в ассете у каждого списка была одна
#   ячейка — ровно одна строка на экране. Число ячеек, их геометрия и скин СТАТИЧНЫ (от сервера и
#   игрока не зависят), значит место им — в ассете (решение владельца 27.08.2026 «статика живёт в
#   виджете», закон `no-runtime-widget-fixes`), а код ставит лишь подписи и отметку выбранной строки.
#
# ═══ ЧТО ДЕЛАЕТ ШАГ (идемпотентно: второй прогон = 0 правок) ═══════════════════════════════════
#   Для ячеек 1…N−1 каждого списка создаёт в его группе `SYSTEM_Op_<X>_DD_grp` три узла — копии
#   узлов ячейки 0, сдвинутые на 25·i px вниз (шаг клона эталона = высота ячейки из её слота):
#     план     `SYSTEM_Op_<X>_DD_CELLnn_0`   ← копия кисти/тона плана `TalkBar_system_D_<jui>_0`;
#     кнопка   `SYSTEM_Op_<X>_DD_SEL00_OPTn` ← копия стиля кнопки `SYSTEM_Op_<X>_DD_SEL00_OPT0`
#              (наведение = кадр 1, нажатие = кадр 2 скина — их кладёт импортёр `AddOptionButton`);
#     подпись  `SYSTEM_Op_<X>_DD_CELLnn_txt` ← копия шрифта/цвета/выравнивания `TalkBar_system_D_<jui>_txt`.
#   Ничего не подбирается и не выдумывается: геометрия, кисти и стили — из ячейки 0 того же списка.
#   СУЩЕСТВУЮЩИЕ узлы окна шаг НЕ ТРОГАЕТ (закон `owner-asset-edits-never-lose`); уже созданный узел
#   с чужой геометрией не правится, а называется в отчёте.
#
# ═══ ПРОВЕРКА ПОСЛЕ ЗАПИСИ ═════════════════════════════════════════════════════════════════════
#   Полный дамп окна (`AsdaUmgBuilder.DumpWidgetTree`) ДО и ПОСЛЕ сравнивается построчно: у всех
#   прежних узлов строка обязана совпасть байт в байт, новыми обязаны быть ровно ожидаемые узлы.
#   Расхождение — исключение (пост-импорт упадёт), сохранение не выполняется.
#
# Запуск (редактор ЗАКРЫТ, окон не показывать):
#   python Tools\run_headless_safe.py Tools\asda_ui_option_list_cells.py --marker "ЯЧЕЙКИ СПИСКОВ: ГОТОВО"
#   Сухой прогон: set ASDA_OPTLIST_DRYRUN=1
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

ASSET = "/Game/AsdaAssets/UI/Player_UI/System/WBP_SYSTEM_Option_NEW"

# (группа списка, префикс имён, число ячеек) — число ячеек по эталону (см. шапку).
LISTS = (
    ("SYSTEM_Op_INT_DD_grp", "SYSTEM_Op_INT_DD", 3),
    ("SYSTEM_Op_SYSTEM_DD_grp", "SYSTEM_Op_SYSTEM_DD", 3),
    ("SYSTEM_Op_HOTKEYS_DD_grp", "SYSTEM_Op_HOTKEYS_DD", 4),
)

# Свойства, которые копируются у узла-образца ячейки 0 (каждое — если у класса есть).
IMAGE_PROPS = ("brush", "color_and_opacity", "visibility", "render_opacity")
BUTTON_PROPS = ("widget_style", "color_and_opacity", "background_color", "click_method", "touch_method",
                "press_method", "is_focusable", "visibility", "render_opacity")
TEXT_PROPS = ("font", "color_and_opacity", "justification", "auto_wrap_text", "shadow_offset",
              "shadow_color_and_opacity", "margin", "line_height_percentage", "visibility",
              "text_overflow_policy", "render_opacity")


def log(m):
    unreal.log("[ячейки списков 69500] %s" % m)


walk = _UWC.walk_by_name   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def dump_map(bp):
    """Строки `DumpWidgetTree` → {имя: строка без выравнивающих пробелов}."""
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


copy_props = _UWC.copy_props   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def place_like(dst, src, dy):
    """Слот `dst` = слот `src` (анкоры, выравнивание, авторазмер, z), смещённый на `dy` по вертикали."""
    s_src, s_dst = slot(src), slot(dst)
    if s_src is None or s_dst is None:
        raise RuntimeError("у %s/%s нет слота канваса" % (src.get_name(), dst.get_name()))
    lay = s_src.get_editor_property("layout_data")
    off = lay.get_editor_property("offsets")
    new = unreal.AnchorData()
    new.set_editor_property("anchors", lay.get_editor_property("anchors"))
    new.set_editor_property("alignment", lay.get_editor_property("alignment"))
    new.set_editor_property("offsets", unreal.Margin(off.left, off.top + dy, off.right, off.bottom))
    s_dst.set_editor_property("layout_data", new)
    s_dst.set_editor_property("auto_size", s_src.get_editor_property("auto_size"))
    s_dst.set_editor_property("z_order", s_src.get_editor_property("z_order"))


def main():
    dry = bool(os.environ.get("ASDA_OPTLIST_DRYRUN"))
    bp = EAL.load_asset(ASSET)
    if not bp:
        raise RuntimeError("не загрузился %s" % ASSET)
    before = dump_map(bp)
    nodes = walk(B.get_root_canvas(bp))
    expected_new = []
    added = 0
    for grp_name, prefix, count in LISTS:
        grp = nodes.get(grp_name)
        if grp is None:
            raise RuntimeError("нет группы %s" % grp_name)
        # Ячейка 0: узлы импортёра, план и подпись названы по СКИНУ (у контрола пустой `ID`).
        plane0 = txt0 = None
        for i in range(grp.get_children_count()):
            k = grp.get_child_at(i)
            n = k.get_name()
            if not n.startswith("TalkBar_system_D"):
                continue
            if n.endswith("_txt") and isinstance(k, unreal.TextBlock):
                txt0 = k
            elif n.endswith("_0") and isinstance(k, unreal.Image):
                plane0 = k
        btn0 = nodes.get("%s_SEL00_OPT0" % prefix)
        if plane0 is None or txt0 is None or btn0 is None:
            raise RuntimeError("%s: неполная ячейка 0 (план=%s подпись=%s кнопка=%s)"
                               % (grp_name, plane0 is not None, txt0 is not None, btn0 is not None))
        # Шаг клона = высота ячейки (эталон: `cellHeight = rect.bottom − rect.top`).
        step = slot(plane0).get_editor_property("layout_data").get_editor_property("offsets").bottom
        for i in range(1, count):
            dy = step * i
            names = ("%s_CELL%02d_0" % (prefix, i), "%s_SEL00_OPT%d" % (prefix, i), "%s_CELL%02d_txt" % (prefix, i))
            for nm, src, props, make in (
                (names[0], plane0, IMAGE_PROPS, "image"),
                (names[1], btn0, BUTTON_PROPS, "button"),
                (names[2], txt0, TEXT_PROPS, "text"),
            ):
                if nm in nodes:
                    # Уже есть — не трогаем; геометрию лишь сверяем и называем расхождение.
                    have = slot(nodes[nm]).get_editor_property("layout_data").get_editor_property("offsets")
                    want = slot(src).get_editor_property("layout_data").get_editor_property("offsets")
                    if abs(have.top - (want.top + dy)) > 0.01 or abs(have.left - want.left) > 0.01:
                        log("⚠ %s уже есть, но стоит не на месте клона (L%.1f T%.1f вместо L%.1f T%.1f) — НЕ ТРОГАЮ"
                            % (nm, have.left, have.top, want.left, want.top + dy))
                    continue
                expected_new.append(nm)
                added += 1
                if dry:
                    log("[сухой] создать %s (копия %s, +%.0f px)" % (nm, src.get_name(), dy))
                    continue
                if make == "image":
                    w = B.add_image(bp, grp, nm, unreal.Vector2D(0, 0), unreal.Vector2D(1, 1),
                                    None, unreal.Box2D(), unreal.LinearColor(1, 1, 1, 1),
                                    False, unreal.Margin(0, 0, 0, 0))
                elif make == "button":
                    w = B.add_button(bp, grp, nm, unreal.Vector2D(0, 0), unreal.Vector2D(1, 1))
                else:
                    w = B.add_text(bp, grp, nm, unreal.Vector2D(0, 0), unreal.Vector2D(1, 1), "", 0,
                                   unreal.LinearColor(1, 1, 1, 1), 0, False, True)
                if w is None:
                    raise RuntimeError("не создан узел %s" % nm)
                copy_props(w, src, props)
                if make == "text":
                    # Подпись ставит код из loc (`CListBox_SetItemString`); в ассете — пусто, как у клона.
                    w.set_text("")
                place_like(w, src, dy)
                nodes[nm] = w
                log("создан %s (копия %s, +%.0f px)" % (nm, src.get_name(), dy))

    if added and not dry:
        after = dump_map(bp)
        changed = [n for n in before if n in after and before[n] != after[n]]
        lost = [n for n in before if n not in after]
        new = sorted(set(after) - set(before))
        if changed or lost or sorted(expected_new) != new:
            raise RuntimeError("СВЕРКА НЕ ПРОШЛА — не сохраняю: изменены %s, пропали %s, новые %s (ждали %s)"
                               % (changed, lost, new, sorted(expected_new)))
        log("сверка: прежних узлов %d — ни один не изменён; новых %d — ровно ожидаемые" % (len(before), len(new)))
        for n in new:
            log("  + %s" % after[n])
        # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
        B.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не сохранён %s" % ASSET)
    log("ЯЧЕЙКИ СПИСКОВ: ГОТОВО%s — создано узлов %d" % ("  [СУХОЙ ПРОГОН]" if dry else "", added))


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[ячейки списков 69500] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
