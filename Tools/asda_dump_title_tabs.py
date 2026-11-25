# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_dump_title_tabs.py — ★ ТОЛЬКО ЧТЕНИЕ: снимок реальных кадров вкладок из ассета.
#
# ЗАЧЕМ. Перед подбором вида вкладок нужен ФАКТ, а не догадка: что именно лежит в
#   `FButtonStyle` кнопок `*_OPT<i>` (кадры Normal/Hovered/Pressed), какие текстуры и UV
#   у этих кистей, какие цвета `NormalForeground`/`PressedForeground`, и что лежит в кисти
#   плана `*_212_<i>`. Закон «не выдумывать»: без этого дампа вид не подбираем.
#
# ★ НИЧЕГО НЕ СОХРАНЯЕТ. Ассеты только загружаются.
#
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_dump_title_tabs.py
#   Лог: @@ASTORY_DEV_ROOT@@\Reports\headless_asda_dump_title_tabs.log
import re
import traceback
import unreal
# ★ дедуп №2 В1 Т12: общие помощники правки WBP (обход дерева, канвасный слот, смещения) — asda_ui_widget_common
import os as _uwc_os, sys as _uwc_sys
if _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)) not in _uwc_sys.path:
    _uwc_sys.path.insert(0, _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)))
import asda_ui_widget_common as _UWC   # noqa: E402

EAL = unreal.EditorAssetLibrary

# (ассет, idc переключателя, число кнопок) — окно званий и, для сверки общей точки, аукцион.
TARGETS = [
    ("/Game/AsdaAssets/UI/Player_UI/Character/WBP_Title_Win", "Title_ClassSelectBtn", 3),
    ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_EXCHANGE_WIN", "EXCHMY_CateSelBtn", 2),
]


def log(m):
    unreal.log("[дамп-вкладок] %s" % m)


walk = _UWC.walk   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def brush_str(br):
    """Читаемая строка о кисти.

    ★ ГРАБЛИ (замер 2026-08-30): у `FSlateBrush` в UE 5.8 из Python НЕТ свойства `uv_region`
      (оно native-only, доступно лишь через `GetUVRegion()` в C++). Поэтому кисть печатаем
      ЦЕЛИКОМ через `str()` — репрезентация структуры перечисляет все доступные поля, и
      выдумывать имена свойств не приходится."""
    if br is None:
        return "None"
    try:
        res = br.get_editor_property("resource_object")
        name = res.get_name() if res else "<нет текстуры>"
    except Exception:
        name = "<?>"
    return "tex=%s | %s" % (name, str(br))


def color_str(sc):
    """Читаемая строка о цвете: годится и для `FSlateColor`, и для голого `FLinearColor`."""
    return str(sc)


def main():
    for path, idc, nbtn in TARGETS:
        log("=" * 78)
        log("АССЕТ %s   переключатель %s (%d кнопок)" % (path, idc, nbtn))
        if not EAL.does_asset_exist(path):
            log("  НЕТ АССЕТА")
            continue
        bp = EAL.load_asset(path)
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
        if not root:
            log("  нет корневого канваса")
            continue
        nodes = {w.get_name(): w for w in walk(root)}

        # ── Кнопки: три кадра стиля + цвета подписи по кадрам ──
        for i in range(nbtn):
            bn = "%s_OPT%d" % (idc, i)
            b = nodes.get(bn)
            if b is None:
                log("  [%s] УЗЛА НЕТ" % bn)
                continue
            st = b.get_editor_property("widget_style")
            log("  [%s]" % bn)
            for frame, prop in (("кадр0 Normal ", "normal"),
                                ("кадр1 Hovered", "hovered"),
                                ("кадр2 Pressed", "pressed"),
                                ("     Disabled", "disabled")):
                log("      %s: %s" % (frame, brush_str(st.get_editor_property(prop))))
            log("      NormalForeground : %s" % color_str(st.get_editor_property("normal_foreground")))
            log("      HoveredForeground: %s" % color_str(st.get_editor_property("hovered_foreground")))
            log("      PressedForeground: %s" % color_str(st.get_editor_property("pressed_foreground")))

        # ── Планы: `{idc}_<номер скина>_<индекс>` — номер у каждого окна свой (212 у званий,
        #    300 у аукциона), поэтому ищем по образцу, а не по жёсткой строке.
        prx = re.compile(r"^%s_\d+_\d+$" % re.escape(idc))
        planes = sorted([n for n in nodes if prx.match(n)])
        log("  ПЛАНЫ (%d): %s" % (len(planes), ", ".join(planes) if planes else "<нет>"))
        for pn in planes:
            w = nodes[pn]
            if isinstance(w, unreal.Image):
                log("      [%s] кисть: %s" % (pn, brush_str(w.get_editor_property("brush"))))
                log("      [%s] ColorAndOpacity: %s"
                    % (pn, color_str(w.get_editor_property("color_and_opacity"))))
            else:
                log("      [%s] НЕ Image, а %s" % (pn, type(w).__name__))

        # ── Все прочие узлы полосы (порядок в канвасе решает перекрытие) ──
        family = sorted([n for n in nodes if n.startswith(idc)])
        log("  ВСЕ УЗЛЫ ПОЛОСЫ (%d): %s" % (len(family), ", ".join(family)))
        for fn in family:
            w = nodes[fn]
            s = w.get_editor_property("slot")
            z, geo = "-", "<не в канвасе>"
            if isinstance(s, unreal.CanvasPanelSlot):
                z = str(s.get_editor_property("z_order"))
                lay = s.get_editor_property("layout_data")
                off = lay.get_editor_property("offsets")
                anc = lay.get_editor_property("anchors")
                # ★ Для вкладок важна именно ГЕОМЕТРИЯ: если планы 1/2 стоят не там, где кнопки
                #   1/2, то ни серого фона у неактивных, ни чёрного кадра у активных не будет.
                geo = ("off=(L%.1f T%.1f R%.1f B%.1f) anc=[(%.3f,%.3f)-(%.3f,%.3f)]"
                       % (off.left, off.top, off.right, off.bottom,
                          anc.minimum.x, anc.minimum.y, anc.maximum.x, anc.maximum.y))
            log("      %-40s %-11s Z=%s %-58s vis=%s"
                % (fn, type(w).__name__, z, geo,
                   str(w.get_editor_property("visibility")).replace("SlateVisibility.", "")))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[дамп-вкладок] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
