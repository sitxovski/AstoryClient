# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_itemupgrade_widgets.py — ЗОНД (только ЧТЕНИЕ): полное дерево виджетов двух окон заточки —
#   `Item_Upgrade_Win` (JUI 20000) и `Get_UpgradeOtion` (JUI 19100). Закон full-structural-analysis:
#   дамп КАЖДОГО контрола до правки кода подсистемы `UAsdaItemUpgradeSubsystem`.
#   Ассеты не меняются, ничего не сохраняется. Выход — текстовый файл в Reports/gear_upgrade/.
#   Запуск: python Tools/run_headless_safe.py Tools/_probe_itemupgrade_widgets.py --marker "ЗОНД-ЗАТОЧКА ЗАВЕРШЕНО"
import os

# Окна и файл вывода. Переопределяются переменными окружения, чтобы тем же зондом смотреть другое окно:
#   ASDA_PROBE_ASSETS — список путей через запятую (волна 7: окно 78200 `WBP_Smelting3_Win`).
ASSETS = [a.strip() for a in os.environ.get("ASDA_PROBE_ASSETS", "").split(",") if a.strip()] or [
    "/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Item_Upgrade_Win",
    "/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Get_UpgradeOtion",
]
OUT = os.environ.get("ASDA_PROBE_OUT", r"@@ASTORY_DEV_ROOT@@\Reports\gear_upgrade\_itemupgrade_wbp_tree.txt")


def _geo(w, unreal):
    """Геометрия слота канваса: смещения, якоря, z. Пусто, если слот не канвасный."""
    try:
        s = w.get_editor_property("slot")
    except Exception:
        return ""
    if not isinstance(s, unreal.CanvasPanelSlot):
        return ""
    ld = s.get_editor_property("layout_data")
    o = ld.get_editor_property("offsets")
    a = ld.get_editor_property("anchors")
    return " off(L%.0f T%.0f R%.0f B%.0f) anc(%.2f %.2f..%.2f %.2f) z%d" % (
        o.left, o.top, o.right, o.bottom, a.minimum.x, a.minimum.y, a.maximum.x, a.maximum.y,
        s.get_editor_property("z_order"))


def _brush(w, unreal):
    """Ресурс кисти картинки (текстура/материал) — чтобы видеть, какой план какой скин несёт."""
    if not isinstance(w, unreal.Image):
        return ""
    try:
        b = w.get_editor_property("brush")
        r = b.get_editor_property("resource_object")
        return " brush=%s" % (r.get_name() if r else "-")
    except Exception:
        return ""


def _prop(w, *names):
    """Свойство по первому подошедшему имени (у bool-свойств UE-питон срезает префикс «b»)."""
    for n in names:
        try:
            return w.get_editor_property(n)
        except Exception:
            continue
    return "?"


def _anim(w, unreal):
    """Данные кадровой анимации плана (волна 5): кадры, прогон мс, цикл, стартовый кадр, UV кадра 0."""
    if type(w).__name__ != "AsdaAnimatedPlane":
        return ""
    try:
        uv = w.get_editor_property("base_uv")
        return " anim(frames=%d ms=%.0f loop=%s loops=%d start=%d uv=%.3f,%.3f..%.3f,%.3f size=%.0fx%.0f)" % (
            w.get_editor_property("total_frame"), w.get_editor_property("frame_interval_ms"),
            _prop(w, "loop", "b_loop"), w.get_editor_property("max_loops"),
            w.get_editor_property("start_frame"), uv.min.x, uv.min.y, uv.max.x, uv.max.y,
            w.get_editor_property("frame_size_px").x, w.get_editor_property("frame_size_px").y)
    except Exception as e:
        return " anim(?%s)" % e


def _btn_sound(w, unreal):
    """Звук нажатия, зашитый в стиль кнопки (PressedSlateSound) — чтобы не задвоить звук кодом."""
    if not isinstance(w, unreal.Button):
        return ""
    try:
        st = w.get_editor_property("widget_style")
        snd = st.get_editor_property("pressed_slate_sound").get_editor_property("resource_object")
        return " pressedSound=%s" % (snd.get_name() if snd else "-")
    except Exception as e:
        return " pressedSound=?(%s)" % e


def main():
    """Работа зонда. Вынесена в функцию: импорт модуля ничего не делает (закон tools-no-import-side-effects)."""
    import unreal
    from collections import Counter
    lines = []
    for path in ASSETS:
        bp = unreal.EditorAssetLibrary.load_asset(path)
        lines.append("=== ASSET %s: %s" % (path, "OK" if bp else "НЕ ЗАГРУЗИЛСЯ"))
        if not bp:
            continue
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
        cnt = Counter()
        n = [0]

        def walk(w, d):
            if w is None:
                return
            n[0] += 1
            cls = type(w).__name__
            cnt[cls] += 1
            txt = ""
            if isinstance(w, unreal.TextBlock):
                try:
                    txt = " text=<%s>" % str(w.get_text())
                except Exception:
                    pass
            lines.append("%s%s [%s] vis=%s%s%s%s%s%s" % ("  " * d, w.get_name(), cls, w.get_visibility(),
                                                 _geo(w, unreal), _brush(w, unreal), _anim(w, unreal),
                                                 _btn_sound(w, unreal), txt))
            if isinstance(w, unreal.PanelWidget):
                for i in range(w.get_children_count()):
                    walk(w.get_child_at(i), d + 1)
            # У кнопки ребёнок-контент тоже панельный — PanelWidget покрывает и её (ContentWidget).

        walk(root, 0)
        lines.append("--- ИТОГО узлов %d: %s" % (n[0], dict(cnt)))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    unreal.log("[зонд-заточка] %d строк -> %s" % (len(lines), OUT))
    unreal.log("ЗОНД-ЗАТОЧКА ЗАВЕРШЕНО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
