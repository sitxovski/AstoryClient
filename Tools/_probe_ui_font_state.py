# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_ui_font_state.py — СНИМОК шрифтового состояния всех текстов UI (только чтение).
#   Нужен для: (1) проверки закона [[owner-asset-edits-never-lose]] ДО пост-прохода —
#   видно, где владелец правил начертание/тень руками; (2) сверки ДО/ПОСЛЕ правки 2026-09-24
#   (перевод подписей JUI с Bold на Regular + псевдо-жирный вместо Bold-гарнитуры).
#   Ничего не меняет и не сохраняет.
#
# Запуск: python Tools/run_headless_safe.py Tools/_probe_ui_font_state.py --marker "СНИМОК ГОТОВ"
#   Куда писать: ASDA_FONTPROBE_OUT=<путь.tsv> (по умолчанию Reports/ui_font_state.tsv)
import io
import os
import traceback
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
UI_ROOT = "/Game/AsdaAssets/UI"
EAL = unreal.EditorAssetLibrary
OUT = os.environ.get("ASDA_FONTPROBE_OUT",
                     r"@@ASTORY_DEV_ROOT@@\Reports\ui_font_state.tsv")


def _login_blocked(path):
    """Окно сцены логина — не загружаем даже на чтение (закон owner-login-light-protected)."""
    import sys as _sys
    if HERE not in _sys.path:
        _sys.path.insert(0, HERE)
    from asda_login_guard import login_package_blocked
    return login_package_blocked(path, "_probe_ui_font_state.py", quiet=True)


def walk(root):
    stack, out = [root], []
    while stack:
        w = stack.pop()
        if not w:
            continue
        out.append(w)
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                stack.append(w.get_child_at(i))
    return out


def main():
    B = unreal.AsdaUmgBuilder
    # ★ ФИЛЬТР ПО КЛАССУ, А НЕ ПО ИМЕНИ (поправка ревизии 24.09.2026). Прежнее `"/WBP_" in a`
    #   ТЕРЯЛО виджет-блюпринты без префикса `WBP_` — `UI/Admin/Admin_login` и `UI/Admin/Admin_menu`.
    #   Их меняет `asda_restyle_ui_font.py` (он отбирает по классу `WidgetBlueprint`), а зонд их не
    #   видел ⇒ закон [[owner-asset-edits-never-lose]] на них не проверялся. Отбираем ровно то же,
    #   что и шаг правки: класс `WidgetBlueprint`.
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = [str(d.package_name) for d in ar.get_assets_by_path(UI_ROOT, recursive=True)
              if str(d.asset_class_path.asset_name) == "WidgetBlueprint"]
    rows = ["окно\tконтрол\tгарнитура\tкегль\tобводка\tтеньX\tтеньY\tтеньA\tцветR\tцветG\tцветB"]
    n = skipped = 0
    for a in assets:
        path = a.split(".")[0]
        if _login_blocked(path):
            skipped += 1
            continue
        bp = EAL.load_asset(path)
        if not bp:
            continue
        root = B.get_root_canvas(bp)
        if not root:
            continue
        win = path.rsplit("/", 1)[-1]
        for w in walk(root):
            if not isinstance(w, unreal.TextBlock):
                continue
            f = w.get_editor_property("font")
            os_ = f.get_editor_property("outline_settings")
            sc = w.get_editor_property("shadow_color_and_opacity")
            so = w.get_editor_property("shadow_offset")
            fc = w.get_editor_property("color_and_opacity").specified_color
            rows.append("%s\t%s\t%s\t%d\t%d\t%.1f\t%.1f\t%.3f\t%.4f\t%.4f\t%.4f" % (
                win, w.get_name(), f.get_editor_property("typeface_font_name"),
                int(f.get_editor_property("size")),
                int(os_.get_editor_property("outline_size")),
                so.x, so.y, sc.a, fc.r, fc.g, fc.b))
            n += 1
    with io.open(OUT, "w", encoding="utf-8") as fh:
        fh.write("\n".join(rows) + "\n")
    unreal.log_warning("[снимок-шрифта] окон %d (пропущено логина %d), текстов %d → %s"
                       % (len(assets), skipped, n, OUT))
    unreal.log_warning("[снимок-шрифта] СНИМОК ГОТОВ")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[снимок-шрифта] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
