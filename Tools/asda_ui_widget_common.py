# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_widget_common.py — ОБЩИЕ помощники правки дерева WBP в шагах UI (дедуп №2, В1 Т12, 11.10.2026).
#
# Было: точные копии `walk` (обход дерева виджетов) в 14 шагах/инструментах, `canvas_slot` ×5 (+ строгий вариант ×2),
#   `set_offsets` ×3, словарный `walk` («имя → виджет») ×3 шагов окна опций, `copy_props` ×3, `_walk_nodes` (.JUI) ×2.
# Стало: одна реализация здесь; в шагах — присваивание прежнему имени (`walk = _UWC.walk` и т. п.), поэтому вызовы
#   и поведение шагов не изменились (тела перенесены дословно; доказательство — сверка AST до/после, отчёт
#   `Reports/dedup_audit2/wave1_T.md` §Т12).
# Сюда НЕ вынесены (тела разные по смыслу): `offsets`/`set_offsets` окон рыбалки/опций (4 и 5 вариантов),
#   `dump_map` (зовёт модуль-сборщик шага), `fix_text` (словарь перевода свой у модуля), `ini_of` (каталог .ini модуля).
#
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль ничего не делает. `unreal` нужен функциям только при
#   вызове (внутри редактора); вне UE модуль импортируется без ошибки (для сверок и стендов).
try:
    import unreal
except ImportError:          # офлайн-импорт (стенды, сверка AST) — функции, которым нужен unreal, вне UE не зовутся
    unreal = None


def walk(root):
    """Все виджеты поддерева `root` списком (обход стеком: панели раскрываются через get_child_at; пустые — пропуск)."""
    st, out = [root], []
    while st:
        w = st.pop()
        if not w:
            continue
        out.append(w)
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                st.append(w.get_child_at(i))
    return out


def walk_by_name(root):
    """Все виджеты поддерева `root` словарём {имя: виджет} (тот же обход; None — пропуск).
    Прежнее имя в шагах окна опций — `walk`."""
    out = {}
    st = [root]
    while st:
        w = st.pop()
        if w is None:
            continue
        out[w.get_name()] = w
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                st.append(w.get_child_at(i))
    return out


def canvas_slot(w):
    """CanvasPanelSlot виджета `w` или None (нет виджета, слот не канвасный, свойство не читается)."""
    if not w:
        return None
    try:
        s = w.get_editor_property("slot")
    except Exception:
        return None
    return s if isinstance(s, unreal.CanvasPanelSlot) else None


def canvas_slot_strict(w):
    """CanvasPanelSlot виджета `w` или None; ошибка чтения свойства `slot` НЕ глушится (вариант шагов вкладок
    премиум-меню и подписей таббара — прежнее имя `canvas_slot`)."""
    if not w:
        return None
    s = w.get_editor_property("slot")
    return s if isinstance(s, unreal.CanvasPanelSlot) else None


def set_offsets(w, want):
    """Смещения канвасного слота `w` = want (L, T, R, B), auto_size выкл. → True; слот не канвасный — False."""
    s = canvas_slot(w)
    if s is None:
        return False
    lay = s.get_editor_property("layout_data")
    lay.set_editor_property("offsets", unreal.Margin(want[0], want[1], want[2], want[3]))
    s.set_editor_property("layout_data", lay)
    s.set_editor_property("auto_size", False)
    return True


def copy_props(dst, src, names):
    """Перенести свойства (отсутствующие у класса — пропуск)."""
    for n in names:
        try:
            dst.set_editor_property(n, src.get_editor_property(n))
        except Exception:                                        # noqa: BLE001
            pass


def walk_jui_nodes(n):
    """Все узлы поддерева .JUI (сам узел включительно). Прежнее имя в шагах — `_walk_nodes`."""
    st = [n]
    while st:
        x = st.pop()
        yield x
        st.extend(reversed(x.children))
