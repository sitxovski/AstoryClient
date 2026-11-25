# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_login_title_center.py — ★ ТИТУЛЫ ЭТАПОВ ЛОГИНА ПО ЦЕНТРУ ПЛАШКИ (08.11.2026, пачка 16, баг 11). [ИСПРАВЛЕНИЕ]
#
# ДЕФЕКТ. «Select Channel» / «Выбор канала» (и «Выбор сервера») стоят не по центру своей плашки: по горизонтали текст
#   ровно в центре (замер кадров: плашка 402–621, текст 447–576 EN / 440–584 RU), а по вертикали прижат к верху —
#   заглавные 408–421 при плашке 404–432 (сверху 4 px, снизу 11).
# ПРИЧИНА. Узлы `SERVER_TitleTxt` / `CHANNEL_TitleTxt` (`WBP_SERVER_SELECT_WIN`) созданы шагом G3
#   `asda_ui_geometry_bake.py` со слотом-копией бара off(5, 0, 210, 29). `UTextBlock` рисует строку от ВЕРХНЕГО края
#   слота, поэтому 14-й кегль в слоте высотой 29 прилипает к верху. В оригинале титул — подпись самого контрола 300/
#   `CHANNEL_LIST` (`ui_core.c:96518`, `CWidget_SetTextAll4(loc 115)`), `CaptionInfo = "0 0 ffc0c0c0 5"` — выравнивание 5,
#   центр по обеим осям.
# ПРАВКА (в АССЕТЕ, без перегенерации окна). Слот титула — точка в центре бара: позиция = центр бара, выравнивание
#   (0.5, 0.5), размер по тексту (auto_size). Строка центрируется по обеим осям при любой длине и любом языке; горизонталь
#   не меняется (было по центру). Анкоры, Z, шрифт, цвет, обоснование — как были. Тот же расклад делает G3 при создании
#   (`center_title_slot` вызывается оттуда), поэтому перегенерация даёт то же.
#
# Запуск:  python Tools/run_headless_safe.py Tools/asda_ui_login_title_center.py --marker ЗАВЕРШЕНО
#          (окно сцены логина: нужен ASDA_ALLOW_LOGIN_WRITE=1 — пакет под защитой asda_login_guard)
#   сухой: ASDA_TITLE_CENTER_DRYRUN=1 — только печать «было/стало».
# ★ Закон tools-no-import-side-effects: при импорте модуль ничего не делает.
import os

SRVSEL = "/Game/AsdaAssets/UI/Login/WBP_SERVER_SELECT_WIN"            # окно выбора сервера/канала
PAIRS = (("SERVER_LIST", "SERVER_TitleTxt"), ("CHANNEL_LIST", "CHANNEL_TitleTxt"))   # (префикс бара, узел титула)


def center_title_slot(unreal, txt, bar_offsets):
    """Слот титула `txt` — точка в центре бара (offsets бара L,T,W,H): позиция центра, выравнивание 0.5/0.5, auto_size.
    Возвращает (было, стало) строками для журнала; запись в слот — всегда (вызывающий решает, сохранять ли)."""
    s = txt.get_editor_property("slot")
    lay = s.get_editor_property("layout_data")
    o0 = lay.get_editor_property("offsets")
    a0 = lay.get_editor_property("alignment")
    was = "off(%.1f %.1f %.1f %.1f) align(%.2f %.2f) auto=%s" % (o0.left, o0.top, o0.right, o0.bottom, a0.x, a0.y,
                                                                 s.get_editor_property("auto_size"))
    cx = bar_offsets.left + bar_offsets.right * 0.5    # центр бара по X (offsets: L, T, ширина, высота)
    cy = bar_offsets.top + bar_offsets.bottom * 0.5    # центр бара по Y
    lay.set_editor_property("offsets", unreal.Margin(cx, cy, bar_offsets.right, bar_offsets.bottom))
    lay.set_editor_property("alignment", unreal.Vector2D(0.5, 0.5))
    s.set_editor_property("layout_data", lay)
    s.set_editor_property("auto_size", True)
    now = "off(%.1f %.1f %.1f %.1f) align(0.50 0.50) auto=True" % (cx, cy, bar_offsets.right, bar_offsets.bottom)
    return was, now


def main():
    import sys
    import unreal
    here = os.path.dirname(os.path.abspath(__file__))
    if here not in sys.path:
        sys.path.insert(0, here)
    import asda_ui_widget_common as uwc
    from asda_login_guard import package_blocked_for
    dry = os.environ.get("ASDA_TITLE_CENTER_DRYRUN", "") not in ("", "0")
    log = lambda m: unreal.log("[титул-центр] " + m)
    if package_blocked_for(__file__, SRVSEL):
        unreal.log_warning("[титул-центр] пакет сцены логина под защитой — нужен ASDA_ALLOW_LOGIN_WRITE=1")
        log("ЗАВЕРШЕНО (пропуск)")
        return
    eal = unreal.EditorAssetLibrary
    b = unreal.AsdaUmgBuilder
    bp = eal.load_asset(SRVSEL)
    root = b.get_root_canvas(bp) if bp else None
    if not root:
        unreal.log_warning("[титул-центр] нет ассета/канваса %s" % SRVSEL)
        log("ЗАВЕРШЕНО (ошибка)")
        return
    nodes = uwc.walk(root)
    byname = {}
    for w in nodes:
        try:
            byname.setdefault(w.get_name(), w)
        except Exception:
            pass
    touched = 0
    for prefix, node in PAIRS:
        txt = byname.get(node)
        bar = None
        for w in nodes:   # бар — Image с префиксом, не группа `*_grp` (та же грабля, что в G3)
            n = w.get_name()
            if n.startswith(prefix) and "_grp" not in n and isinstance(w, unreal.Image):
                bar = w
                break
        if not txt or not bar:
            unreal.log_warning("[титул-центр] %s: нет узла (%s) или бара %s*" % (node, bool(txt), prefix))
            continue
        bo = bar.get_editor_property("slot").get_editor_property("layout_data").get_editor_property("offsets")
        if dry:
            log("%s: СУХОЙ, бар %s off(%.1f %.1f %.1f %.1f)" % (node, bar.get_name(), bo.left, bo.top, bo.right, bo.bottom))
            continue
        was, now = center_title_slot(unreal, txt, bo)
        log("%s (бар %s): было %s → стало %s" % (node, bar.get_name(), was, now))
        touched += 1
    if touched and not dry:
        b.compile_and_save(bp)           # компиляция + пометка пакета (имя врёт — запись ниже)
        eal.save_loaded_asset(bp)
        log("сохранено: %s (узлов %d)" % (SRVSEL, touched))
    log("ЗАВЕРШЕНО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
