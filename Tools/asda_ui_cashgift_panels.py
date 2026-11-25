# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_cashgift_panels.py — ★ P101_CashGift (01.11.2026): СТАТИЧЕСКОЕ СОСТОЯНИЕ ПАНЕЛЕЙ 6296/6333 ОКНА 6000 В АССЕТЕ.
#
# ЗАЧЕМ. Владелец разрешил (PROJECT_P101.md §8 п.2, 01.11.2026) перенести в ассет `WBP_SHELLITEM_STORE_WIN` панели покупки/подарка
#   6296 `SHELLITEM_EQUIP_WIN` и 6333 `SHELLITEM_FUNC_WIN` с полем имени 6311/6345 и кнопкой «Проверить» 6312/6346 строго по оригиналу,
#   остальное окно не трогать. Узлы импортёр уже создал (дамп `Reports/cash_gift/p101/dump_cashgift_controls.txt`), но их
#   СТАТИЧЕСКАЯ видимость в ассете не та, что ставит эталон один раз при инициализации (`CShellItemStore_InitControls`,
#   ui_core.c:27425-27626 и item_inventory.c:20008-20011) — по закону no-runtime-widget-fixes это правится В АССЕТЕ:
#     • `SHELL_EQUIP_SINGLE_grp` (7003, в .JUI `Visible = "0"`) — эталон ПОКАЗЫВАЕТ: `SetVisible(m_pEquipSingle, 1)` (27616);
#     • `SHELL_EQUIP_MULTI_grp`  (7004, `Visible = "1"`)        — эталон ГАСИТ: `SetVisible(m_pEquipMulti, 0)` (27617);
#     • `SHELL_EQUIP_OPTION1_grp` / `SHELL_EQUIP_OPTION2_grp` (6330/6327) — эталон гасит (27550, 27561);
#     • подсказки 6390/6391 `SHELL_EQUIP_GuideTxt01/02` и 6392/6393 `SHELL_FUNC_GuideTxt00/01` — эталон гасит (item_inventory.c:20008-20011).
#     • (02.11.2026, ревизия Н-2) подписи кнопок «Подарок» карточек `[__]SHELLITEM_POPnn_BUT2_Text` (6085 + 9i, 21 шт.) — без
#       переноса строк (`auto_wrap_text = False`): прежде это ставил код окна при каждой привязке (закон no-runtime-widget-fixes);
#   Подписи панелей (12911, 12947, 2283, 12006, 6822, 2289, 12915, 8400, 6960/6961, 12909/12910) — ссылкой на ST_GameText шагом
#   `asda_ui_caption_gametext` (строки плана `Tools/data/asda_ui_caption_gametext_plan.tsv` для этих узлов переведены в «да»).
#
# ★ СТОРОЖ: узел меняется, только если его видимость = «было» (импорт .JUI) — уже «стало» → 0 правок (идемпотентно), иное —
#   чужая правка → «НЕ ТРОГАЮ», окно не сохраняется. Других свойств шаг не касается.
# Сухой прогон: set ASDA_CASHGIFT_PANELS_DRYRUN=1. Маркер: «ПАНЕЛИ ПОДАРКА: ГОТОВО».
# Запуск: python Tools/run_headless_safe.py Tools/asda_ui_cashgift_panels.py --marker "ПАНЕЛИ ПОДАРКА: ГОТОВО"
#   --expect-asset /Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN
# ★ В цепочке `asda_ui_post_import.STEPS` — перед `asda_ui_caption_gametext`.
import os
import sys
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ASSET = "/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN"
DRY_ENV = "ASDA_CASHGIFT_PANELS_DRYRUN"
MARK = "ПАНЕЛИ ПОДАРКА: ГОТОВО"
# узел → (видимость «было» (импорт), видимость «стало» (эталон Init)). Имена значений — `unreal.SlateVisibility`.
PLAN = [
    ("SHELL_EQUIP_SINGLE_grp",  "COLLAPSED",               "SELF_HIT_TEST_INVISIBLE"),
    ("SHELL_EQUIP_MULTI_grp",   "SELF_HIT_TEST_INVISIBLE", "COLLAPSED"),
    ("SHELL_EQUIP_OPTION1_grp", "SELF_HIT_TEST_INVISIBLE", "COLLAPSED"),
    ("SHELL_EQUIP_OPTION2_grp", "SELF_HIT_TEST_INVISIBLE", "COLLAPSED"),
    ("SHELL_EQUIP_GuideTxt01",  "SELF_HIT_TEST_INVISIBLE", "COLLAPSED"),
    ("SHELL_EQUIP_GuideTxt02",  "SELF_HIT_TEST_INVISIBLE", "COLLAPSED"),
    ("SHELL_FUNC_GuideTxt00",   "SELF_HIT_TEST_INVISIBLE", "COLLAPSED"),
    ("SHELL_FUNC_GuideTxt01",   "SELF_HIT_TEST_INVISIBLE", "COLLAPSED"),
]
# Подписи кнопок «Подарок» карточек: `auto_wrap_text` → False (ячейка 00 несёт мусорный префикс `__` импортёра).
WRAP_OFF = [("__SHELLITEM_POP00_BUT2_Text" if i == 0 else "SHELLITEM_POP%02d_BUT2_Text" % i) for i in range(21)]


def log(m):
    """Строка журнала шага."""
    import unreal
    unreal.log("[панели подарка] %s" % m)


def main():
    """Точка входа (только под __main__ — закон tools-no-import-side-effects)."""
    import unreal
    import asda_ui_widget_common as UWC
    EAL = unreal.EditorAssetLibrary
    dry = bool(os.environ.get(DRY_ENV))
    if not EAL.does_asset_exist(ASSET):
        log("НЕТ АССЕТА %s" % ASSET)
        log(MARK + " (ошибка)")
        return
    bp = EAL.load_asset(ASSET)
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    nodes = {}
    for w in UWC.walk(root):
        nodes.setdefault(w.get_name(), w)
    changed = already = bad = 0
    for name, was, want in PLAN:
        w = nodes.get(name)
        if w is None:
            log("!! нет узла %s — НЕ ТРОГАЮ окно" % name)
            bad += 1
            continue
        cur = str(w.get_editor_property("visibility")).split(".")[-1].split(":")[0].strip("<> ")
        if cur == want:
            already += 1
            continue
        if cur != was:
            log("!! %s: видимость %s, ждали %s (импорт) или %s (эталон) — ручная правка? НЕ ТРОГАЮ" % (name, cur, was, want))
            bad += 1
            continue
        if not dry:
            w.set_editor_property("visibility", getattr(unreal.SlateVisibility, want))
        changed += 1
        log("  %s: %s -> %s" % (name, cur, want))
    for name in WRAP_OFF:
        w = nodes.get(name)
        if w is None or not isinstance(w, unreal.TextBlock):
            log("!! нет подписи %s — НЕ ТРОГАЮ окно" % name)
            bad += 1
            continue
        if not w.get_editor_property("auto_wrap_text"):
            already += 1
            continue
        if not dry:
            w.set_editor_property("auto_wrap_text", False)
        changed += 1
        log("  %s: auto_wrap_text True -> False" % name)
    if bad:
        log("ОТКАЗ: %d узлов не по плану — окно НЕ сохраняется" % bad)
        log(MARK + " (отказ)")
        return
    if changed and not dry:
        unreal.AsdaUmgBuilder.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не удалось сохранить %s" % ASSET)
        log("ASDA_WRITES_ASSET: %s" % ASSET)
    log("изменено %d, уже по эталону %d%s" % (changed, already, " (СУХОЙ ПРОГОН)" if dry else ""))
    log(MARK)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        import unreal
        unreal.log_error("[панели подарка] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
