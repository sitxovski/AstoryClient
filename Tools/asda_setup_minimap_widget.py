# Copyright (c) 2026 @sitxovski (Discord)
# asda_setup_minimap_widget.py — доложить в ШТАТНЫЕ импортированные JUI-виджеты недостающие узлы,
#   которых в JUI нет ПО ПРИРОДЕ: сама КАРТИНКА карты и слой МАРКЕРОВ.
#   В оригинале это не контролы, а D3D-квады класса CMap (uiMiniMapRender@0xB4EFE0 рисует их ДО
#   WindowMgr_RenderControls, т.е. ПОД окном) — в UMG это первые дети корневого канваса виджета.
#   Никакого «своего UI»: рамки, кнопки, подписи, каналы остаются ровно теми, что импортировал JUI.
#
#   WBP_MINIMAP_WIN (1702):  MMap_View(канвас, клип) > MMap_MapImage ; MMap_Markers ; MMap_ClickArea
#   WBP_MAXMAP_WIN  (1710):  MaxMap_View > MaxMap_MapImage ; MaxMap_Markers ; MaxMap_ClickArea
#
#   Геометрия — эталонная (uiMinimapBuildQuads@0xB4CE20):
#     компакт: квад 130×130 @(ScreenW−133, 36)  → анкор (1,0), offsets (−133, 36, 130, 130)
#     большая карта: 512×512 внутри рамки MaxMap_Bk1Stroke @(62, 44)
#   Раскладку при развороте до 256×256 в рантайме двигает UAsdaMinimapSubsystem::ApplyLayout.
#
#   Сухой прогон: `--dry-run` / ASDA_MMAPSETUP_DRYRUN=1 / ASDA_POSTSTEPS_DRYRUN=1 (`asda_ui_step_dryrun`,
#   07.10.2026): логика та же, запись ассетов подавлена, итог — строка «[ШАГ-ДЕЛЬТА] … изменено узлов N».
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ui_step_dryrun as SD   # общий сухой режим шагов пост-импорта (07.10.2026)

B = unreal.AsdaUmgBuilder
EAL = unreal.EditorAssetLibrary

# Собственный ключ сухого прогона шага (см. `asda_ui_step_dryrun.is_dry`).
DRYRUN_ENV = "ASDA_MMAPSETUP_DRYRUN"


def V2(x, y):
    return unreal.Vector2D(x, y)


def anchor(widget, ax, ay):
    slot = widget.slot if widget else None
    if slot and isinstance(slot, unreal.CanvasPanelSlot):
        lay = slot.get_editor_property("layout_data")
        lay.anchors = unreal.Anchors(V2(ax, ay), V2(ax, ay))
        slot.set_editor_property("layout_data", lay)


def find(bp, name):
    root = B.get_root_canvas(bp)

    def walk(w):
        try:
            kids = w.get_all_children()
        except Exception:
            return None
        for c in kids:
            if c.get_name() == name:
                return c
            r = walk(c)
            if r:
                return r
        return None
    return walk(root)


def setup(path, prefix, pos, size, ax, ay, dry=False, delta=None):
    """Доложить поле карты/маркеры/клик-область в окно `path`. `dry` — запись подавлена;
    `delta` — `asda_ui_step_dryrun.WidgetDelta` для учёта правок (None — не учитывать)."""
    bp = EAL.load_asset(path)
    if not bp:
        unreal.log_warning("[minimap-ui] нет " + path)
        return
    if delta is None:
        delta = SD.WidgetDelta("asda_setup_minimap_widget", dry)  # снимок нужен всегда: по нему решается запись
    delta.take(bp)
    root = B.get_root_canvas(bp)
    made = []

    if not find(bp, prefix + "_View"):
        view = B.add_canvas(bp, root, prefix + "_View", V2(*pos), V2(*size))
        view.set_editor_property("clipping", unreal.WidgetClipping.CLIP_TO_BOUNDS)
        anchor(view, ax, ay)
        img = B.add_image(bp, view, prefix + "_MapImage", V2(0, 0), V2(*size), None,
                          unreal.Box2D(V2(0, 0), V2(1, 1)), unreal.LinearColor(1, 1, 1, 1),
                          False, unreal.Margin(0, 0, 0, 0))
        img.set_editor_property("visibility", unreal.SlateVisibility.HIT_TEST_INVISIBLE)
        made.append(view)

    if not find(bp, prefix + "_Markers"):
        marks = B.add_canvas(bp, root, prefix + "_Markers", V2(*pos), V2(*size))
        marks.set_editor_property("clipping", unreal.WidgetClipping.CLIP_TO_BOUNDS)
        marks.set_editor_property("visibility", unreal.SlateVisibility.HIT_TEST_INVISIBLE)
        anchor(marks, ax, ay)
        made.append(marks)

    if not find(bp, prefix + "_ClickArea"):
        btn = B.add_button(bp, root, prefix + "_ClickArea", V2(*pos), V2(*size))
        anchor(btn, ax, ay)
        made.append(btn)

    # Порядок = z: карта (0) → маркеры (1) → все JUI-контролы окна поверх (как в оригинале: квады
    #   CMap рисуются ДО WindowMgr_RenderControls). Клик-область остаётся ПОСЛЕДНЕЙ (самой верхней):
    #   она прозрачна, не перекрывает ни одной кнопки окна (те лежат вне прямоугольника карты), зато
    #   гарантированно получает клик, который иначе съел бы вышележащий канвас рамки MMap_BkBoder_grp.
    #   Порядок наводим ВСЕГДА, даже если узлы уже были добавлены прошлым прогоном.
    view = find(bp, prefix + "_View")
    marks = find(bp, prefix + "_Markers")
    click = find(bp, prefix + "_ClickArea")
    if view:  B.move_child_to_index(root, view, 0)
    if marks: B.move_child_to_index(root, marks, 1)
    if click: B.move_child_to_index(root, click, root.get_children_count() - 1)

    # ★ 07.10.2026 (ревизия `poststeps_align_fix_REVIEW.md`, МЕЛК-3): окно компилируется и пишется, ТОЛЬКО если
    #   снимок шага видит правки (новые узлы, порядок детей) или узлы созданы. Ноль — файл не трогается.
    SD.save_if_changed(bp, dry, "asda_setup_minimap_widget", delta, own=len(made), compile_fn=B.compile_and_save)
    unreal.log("[minimap-ui] %s: добавлено %d узлов (%s)%s" %
               (path, len(made), ", ".join(w.get_name() for w in made) if made else "уже были",
                " (СУХОЙ)" if dry else ""))


def main():
    dry = SD.is_dry(DRYRUN_ENV)                                  # сухой прогон (запись подавлена)
    delta = SD.WidgetDelta("asda_setup_minimap_widget", dry)     # учёт правок шага по снимкам дерева
    # Мини-карта: правый-верхний анкор (1,0), компактный квад 130×130 @(−133, 36).
    setup("/Game/AsdaAssets/UI/Player_UI/Map/WBP_MINIMAP_WIN", "MMap", (-133, 36), (130, 130), 1.0, 0.0,
          dry, delta)
    # Большая карта: окно с анкором (0,0), поле карты 512×512 внутри рамки MaxMap_Bk1Stroke @(62,44).
    setup("/Game/AsdaAssets/UI/Player_UI/Map/WBP_MAXMAP_WIN", "MaxMap", (62, 44), (512, 512), 0.0, 0.0,
          dry, delta)
    delta.finish()
    unreal.log("[minimap-ui] ГОТОВО")


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
#   (До 2026-08-06 `setup(...)` стоял на верхнем уровне — импорт из общего пост-импорт-раннера
#   молча перестраивал бы окна карты.)
if __name__ == "__main__":
    main()
