# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# ★ ЗАМЕР БРАША КОНТЕЙНЕРА 3D-СЦЕНЫ ГАЧИ (закон full-structural-analysis-always).
#
# ЗАЧЕМ. Симптом «фон сцены виден, автоматов нет» объяснялся гипотезой: у контрола 68056
# (`Premium_PickBAR_Win_1024window`, скин `NoImage_icon`) браш импортёра несёт UV-РЕГИОН
# `0.994 0.000 … 1.000 0.004` из `JuiSkin.ini`, а `SetBrushResourceObject(материал)` его НЕ
# снимает — материал сцены сэмплится одной точкой пустого угла кадра. Скрипт проверяет это
# по РЕАЛЬНОМУ ассету WBP, а не по рассуждению (закон `false-negatives-method`: доказывать
# вторым методом).
#
# ЗАПУСК:  Tools\run_headless.bat Tools\asda_gacha_brush_probe.py
import unreal

WBP = "/Game/AsdaAssets/UI/Player_UI/Premium/WBP_Premium_PickBAR_Win"
# Узлы, которые интересуют: 68056 (контейнер 3D), 68059 (подложка), шар 68057.
NODES = ("Premium_PickBAR_Win_1024window_2_0",
         "Premium_PickBAR_back_1024_1_0",
         "Premium_PickBAR_ballB_74_0")


def _fmt_box(b):
    # ⚠ ЧЕСТНО: `FSlateBrush::UVRegion` объявлено `protected UPROPERTY()` без Blueprint-доступа
    #   (SlateBrush.h:481-487), поэтому из Python оно НЕ читается. Значение печатает C++ —
    #   `UAsdaPremiumGachaSubsystem::ApplySceneBrush` пишет его в лог живого запуска.
    try:
        return "min=%s max=%s valid=%s" % (b.get_editor_property("min"),
                                           b.get_editor_property("max"),
                                           b.get_editor_property("b_is_valid"))
    except Exception as e:
        return "недоступно из Python (%s)" % e


def _walk(panel, out):
    """Обход дерева-ШАБЛОНА окна. `UUserWidget` в headless не создать (в `unreal` нет
    `WidgetBlueprintLibrary`), зато корень шаблона отдаёт наш же `AsdaUmgBuilder.get_root_canvas`."""
    if not panel:
        return
    for i in range(panel.get_children_count()):
        ch = panel.get_child_at(i)
        if not ch:
            continue
        out[ch.get_name()] = ch
        if isinstance(ch, unreal.PanelWidget):
            _walk(ch, out)


def main():
    bp = unreal.EditorAssetLibrary.load_asset(WBP)
    if not bp:
        unreal.log_warning("[браш-гачи] окно не загрузилось: %s" % WBP)
        return
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    nodes = {}
    _walk(root, nodes)
    unreal.log("[браш-гачи] узлов в шаблоне: %d" % len(nodes))
    for name in NODES:
        node = nodes.get(name)
        if not node:
            unreal.log_warning("[браш-гачи] узла нет: %s" % name)
            continue
        line = "[браш-гачи] %-38s класс=%s" % (name, node.get_class().get_name())
        try:
            br = node.get_editor_property("brush")
            try:
                uv = _fmt_box(br.get_editor_property("uv_region"))
            except Exception as e:
                uv = "недоступно из Python (%s)" % e
            line += " | draw_as=%s tint=%s image_size=%s uv=%s ресурс=%s" % (
                br.get_editor_property("draw_as"),
                br.get_editor_property("tint_color").get_editor_property("specified_color"),
                br.get_editor_property("image_size"), uv,
                br.get_editor_property("resource_object"))
        except Exception as e:
            line += " | браш нечитаем: %s" % e
        unreal.log(line)

    # Материал кисти обязан быть домена User Interface — иначе Slate молча не нарисует ничего.
    mat = unreal.EditorAssetLibrary.load_asset("/Game/Materials/M_AsdaGachaScene3D")
    if mat:
        unreal.log("[браш-гачи] M_AsdaGachaScene3D: domain=%s blend=%s"
                   % (mat.get_editor_property("material_domain"),
                      mat.get_editor_property("blend_mode")))
    else:
        unreal.log_warning("[браш-гачи] материал M_AsdaGachaScene3D не найден")


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    main()
