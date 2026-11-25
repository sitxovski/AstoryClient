# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _dump_message2_neutral.py — СВЕРКА варианта `WBP_MESSAGE2_WIN_NEUTRAL` с базовым `WBP_MESSAGE2_WIN`
#   (правка 2026-09-03, вариант окна из `asda_jui_generic_umg.WINDOW_VARIANTS`).
#
#   Что печатает по КАЖДОМУ из двух окон:
#     • полный структурный дамп `UAsdaUmgBuilder::DumpWidgetTree` (имя, класс, родитель, видимость,
#       слот, у Image — браш/UV/тинт, у TextBlock — цвет/текст) — закон full-structural-analysis-always;
#     • по каждой кнопке — стиль `FButtonStyle`: ресурс-текстура, размер кисти и тинт кистей
#       Normal/Hovered/Pressed/Disabled, foreground-цвета подписи по состояниям, паддинги
#       (смещение подписи). ⚠ `FSlateBrush::UVRegion` — UPROPERTY без BlueprintReadWrite, из Python
#       не читается; для UV кистей кнопок дополнительно экспортируем оба ассета в T3D
#       (`AssetTools.export_assets`) — там `ExportProperties` печатает и UVRegion.
#   Вывод: строки лога с префиксом `[M2DUMP]`; T3D — в каталог ASDA_M2_T3D_OUT (если задан).
#   Маркер успеха для run_headless_safe.py: «ЗАВЕРШЕНО: дамп MESSAGE2».
import os
import unreal

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder
PATHS = ["/Game/AsdaAssets/UI/Login/WBP_MESSAGE2_WIN",
         "/Game/AsdaAssets/UI/Login/WBP_MESSAGE2_WIN_NEUTRAL"]
T3D_OUT = os.environ.get("ASDA_M2_T3D_OUT", "")


def L(m):
    unreal.log("[M2DUMP] " + str(m))


def _hex(c):
    """FLinearColor (линейный) → sRGB-hex AARRGGBB, как строки JuiSkin.ini."""
    def s(v):
        v = max(0.0, min(1.0, v))
        v = v * 12.92 if v <= 0.0031308 else 1.055 * (v ** (1.0 / 2.4)) - 0.055
        return int(round(v * 255.0))
    try:
        return "%02X%02X%02X%02X" % (int(round(max(0.0, min(1.0, c.a)) * 255.0)), s(c.r), s(c.g), s(c.b))
    except Exception:
        return "??"


def _fg(sc):
    try:
        rule = str(sc.get_editor_property("color_use_rule"))
        spec = sc.get_editor_property("specified_color")
    except Exception:
        return "??"
    return ("ЗАДАН:" + _hex(spec)) if "USE_COLOR_SPECIFIED" in rule else "наследует"


def _brush(br):
    """Кисть состояния: текстура, размер, тинт, (UV если доступно из Python)."""
    try:
        res = br.get_editor_property("resource_object")
        res = res.get_name() if res else "—"
    except Exception:
        res = "??"
    try:
        sz = br.get_editor_property("image_size"); sz = "%.1fx%.1f" % (sz.x, sz.y)
    except Exception:
        sz = "??"
    try:
        tint = _hex(br.get_editor_property("tint_color").specified_color)
    except Exception:
        tint = "??"
    uv = "uv=недоступно"
    for pn in ("uv_region", "UVRegion"):
        try:
            u = br.get_editor_property(pn)
            uv = "uv[%.4f %.4f]-[%.4f %.4f]" % (u.min.x, u.min.y, u.max.x, u.max.y); break
        except Exception:
            continue
    return "tex=%s size=%s tint=%s %s" % (res, sz, tint, uv)


def _pad(m):
    try:
        return "%.1f,%.1f,%.1f,%.1f" % (m.left, m.top, m.right, m.bottom)
    except Exception:
        return "??"


def _buttons(root):
    out = []

    def walk(w):
        if isinstance(w, unreal.Button):
            st = w.get_editor_property("widget_style")
            out.append("  КНОПКА %-18s N{%s} H{%s} P{%s} D{%s} | fgN=%s fgH=%s fgP=%s fgD=%s | padN=%s padP=%s | focus=%s" % (
                w.get_name(),
                _brush(st.get_editor_property("normal")), _brush(st.get_editor_property("hovered")),
                _brush(st.get_editor_property("pressed")), _brush(st.get_editor_property("disabled")),
                _fg(st.get_editor_property("normal_foreground")), _fg(st.get_editor_property("hovered_foreground")),
                _fg(st.get_editor_property("pressed_foreground")), _fg(st.get_editor_property("disabled_foreground")),
                _pad(st.get_editor_property("normal_padding")), _pad(st.get_editor_property("pressed_padding")),
                bool(w.get_editor_property("is_focusable"))))
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                walk(w.get_child_at(i))
    walk(root)
    return out


def main():
    for p in PATHS:
        L("=" * 100)
        L("АССЕТ %s: %s" % (p, "есть" if EAL.does_asset_exist(p) else "НЕТ"))
        if not EAL.does_asset_exist(p):
            continue
        bp = EAL.load_asset(p)
        if not bp:
            L("  НЕ ЗАГРУЗИЛСЯ")
            continue
        for line in B.dump_widget_tree(bp).split("\n"):
            L("TREE " + line)
        root = B.get_root_canvas(bp)
        for line in _buttons(root):
            L(line)
    if T3D_OUT:
        try:
            os.makedirs(T3D_OUT, exist_ok=True)
            unreal.AssetToolsHelpers.get_asset_tools().export_assets(
                [p for p in PATHS if EAL.does_asset_exist(p)], T3D_OUT)
            L("T3D экспорт → %s: %s" % (T3D_OUT, ", ".join(sorted(os.listdir(T3D_OUT)))))
        except Exception as e:                                       # noqa: BLE001
            L("T3D экспорт не удался: %s" % e)
    L("ЗАВЕРШЕНО: дамп MESSAGE2")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
