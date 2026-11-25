# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# ★ ТОЧЕЧНАЯ ПРАВКА РАЗМЕТКИ Д-W0-1 (29.09.2026, торговля, волна 0; ревизия `Reports/trade/wave0_REVIEW.md` §3).
#
# ЧТО. В `.JUI` у `TRAN_BUT02` (IDNum 2049, индикатор готовности ПАРТНЁРА окна обмена 2008) стоит `NoSelect = "1"`
#   (`Data/Decoded/jui/gamemain_jui.ini`, блок контрола): клик эталона проходит сквозь него (`uiControlHitTest`). Импортёр
#   сделал из него `CheckBox` с видимостью `VISIBLE` — он ловит клик и сам переключает галку. Во всех 5 окнах
#   торговли это единственный интерактивный узел с NoSelect=1 в состоянии VISIBLE (скан ревизора и волны 0).
# КАК. Ровно один узел `TRAN_BUT02`: класс `CheckBox`, родитель `Tran_But02_Base_grp`, текущая видимость `VISIBLE` —
#   иначе ОТКАЗ без записи. Новая видимость `HIT_TEST_INVISIBLE` (галка видна, клик насквозь). Остальные узлы
#   (`TRAN_BUT01`, `_Text`, базы, группы) не трогаются. Дефект разметки чинится в АССЕТЕ (закон no-runtime-widget-fixes),
#   это не перегенерация окна.
# ЗАПУСК: python Tools/run_headless_safe.py Tools/asda_ui_tran_but02_noselect.py --marker "ЗАВЕРШЕНО"
#   ASDA_TRANBUT02_DRYRUN=1 — сухой прогон (только проверки, без записи).
#   Соседнего `-game` быть не должно: сохранение ассета при нём молча не проходит.
import os
import unreal
# ★ дедуп №2 В1 Т12: общие помощники правки WBP (обход дерева, канвасный слот, смещения) — asda_ui_widget_common
import os as _uwc_os, sys as _uwc_sys
if _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)) not in _uwc_sys.path:
    _uwc_sys.path.insert(0, _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)))
import asda_ui_widget_common as _UWC   # noqa: E402

EAL = unreal.EditorAssetLibrary
PATH = "/Game/AsdaAssets/UI/Player_UI/Trade/WBP_TRAN_WIN"   # окно обмена 2008
NODE = "TRAN_BUT02"                                          # индикатор готовности партнёра (2049)
PARENT = "Tran_But02_Base_grp"                               # ожидаемый родитель узла


def log(m):
    unreal.log("[Д-W0-1] %s" % m)


walk = _UWC.walk   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def main():
    dry = bool(os.environ.get("ASDA_TRANBUT02_DRYRUN"))
    if not EAL.does_asset_exist(PATH):
        log("ОТКАЗ: нет ассета %s" % PATH)
        return
    bp = EAL.load_asset(PATH)
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    if not root:
        log("ОТКАЗ: нет корневого канваса")
        return
    hits = [w for w in walk(root) if w.get_name() == NODE]
    if len(hits) != 1:
        log("ОТКАЗ: узлов %s найдено %d (нужен ровно один)" % (NODE, len(hits)))
        return
    w = hits[0]
    cls = w.get_class().get_name()
    par = w.get_parent().get_name() if w.get_parent() else "-"
    cur = w.get_editor_property("visibility")
    log("найден %s: класс=%s родитель=%s видимость=%s" % (NODE, cls, par, cur))
    if cls != "CheckBox" or par != PARENT:
        log("ОТКАЗ: класс/родитель не те (ждём CheckBox / %s)" % PARENT)
        return
    if cur == unreal.SlateVisibility.HIT_TEST_INVISIBLE:
        log("уже HIT_TEST_INVISIBLE — записывать нечего. ЗАВЕРШЕНО")
        return
    if cur != unreal.SlateVisibility.VISIBLE:
        log("ОТКАЗ: текущая видимость %s, ждём VISIBLE" % cur)
        return
    if dry:
        log("СУХОЙ ПРОГОН: поставил бы HIT_TEST_INVISIBLE одному узлу. ЗАВЕРШЕНО")
        return
    w.set_editor_property("visibility", unreal.SlateVisibility.HIT_TEST_INVISIBLE)
    unreal.AsdaUmgBuilder.compile_and_save(bp)
    # ⚠ `compile_and_save` не сохраняет — сохраняет `save_loaded_asset` (грабля шага видимости панелей).
    if not EAL.save_loaded_asset(bp, False):
        log("ОШИБКА: ассет не сохранён")
        raise RuntimeError("не сохранён %s" % PATH)
    unreal.log("ASDA_WRITES_ASSET: %s" % PATH)
    log("записано: %s → HIT_TEST_INVISIBLE. ЗАВЕРШЕНО" % NODE)


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    main()
