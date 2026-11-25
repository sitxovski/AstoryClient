# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# ★ ТОЧЕЧНАЯ ПРАВКА РАЗМЕТКИ Л4-1 (30.09.2026, торговля, волна Л4/Л5; ревизия `Reports/trade/wave_L45_REVIEW.md` §3/§7).
#
# ЧТО. Поле чата личной лавки 23403 `Stall_Chat_Input` (`.JUI` `EditBox`, `Data/Decoded/jui/gamemain_jui.ini:71497-71523`) у эталона —
#   Win32-поле: после Enter (67587 → 6313, shop_trade.c:17541-17561) текст очищается, а ФОКУС ОСТАЁТСЯ в поле (следующую строку
#   можно печатать сразу). Импортёр (`AsdaUmgBuilder::AddEditableText`) свойство `ClearKeyboardFocusOnCommit` не ставит — действует
#   умолчание UE `true`: после Enter фокус уходит (самотест Л4: «фокус после Enter = 0, ClearKeyboardFocusOnCommit ассета = 1»).
# КАК. Ровно один узел `Stall_Chat_Input` в `WBP_StreetStall_Win`: класс `EditableText` — иначе ОТКАЗ без записи.
#   `ClearKeyboardFocusOnCommit` → False. Остальное не трогается (`SelectAllTextOnCommit` и пр.). Дефект разметки чинится
#   в АССЕТЕ (закон no-runtime-widget-fixes), это не перегенерация окна. Шаг идемпотентен: уже False — записывать нечего.
#   Зарегистрирован в ведомости пост-импорта `Tools/asda_ui_post_import.py` → `STEPS` (переживёт разрешённую перегенерацию).
# ЗАПУСК (из Bash — MSYS_NO_PATHCONV=1, иначе пути `/Game/...` перепишутся):
#   python Tools/run_headless_safe.py Tools/asda_ui_stall_chat_focus.py --marker "ЗАВЕРШЕНО"
#   ASDA_STALLCHAT_DRYRUN=1 — сухой прогон (только проверки, без записи).
#   Соседнего `-game` быть не должно: сохранение ассета при нём молча не проходит.
import os
import unreal
# ★ дедуп №2 В1 Т12: общие помощники правки WBP (обход дерева, канвасный слот, смещения) — asda_ui_widget_common
import os as _uwc_os, sys as _uwc_sys
if _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)) not in _uwc_sys.path:
    _uwc_sys.path.insert(0, _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)))
import asda_ui_widget_common as _UWC   # noqa: E402

EAL = unreal.EditorAssetLibrary
PATH = "/Game/AsdaAssets/UI/Player_UI/Trade/WBP_StreetStall_Win"   # витрина личной лавки 23000
NODE = "Stall_Chat_Input"                                           # поле чата 23403
PROP = "clear_keyboard_focus_on_commit"                             # `UEditableText::ClearKeyboardFocusOnCommit`


def log(m):
    unreal.log("[Л4-1] %s" % m)


walk = _UWC.walk   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def main():
    dry = bool(os.environ.get("ASDA_STALLCHAT_DRYRUN"))
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
    cur = w.get_editor_property(PROP)
    log("найден %s: класс=%s родитель=%s %s=%s" % (NODE, cls, par, PROP, cur))
    if cls != "EditableText":
        log("ОТКАЗ: класс %s, ждём EditableText" % cls)
        return
    if not cur:
        log("уже False — записывать нечего. ЗАВЕРШЕНО")
        return
    if dry:
        log("СУХОЙ ПРОГОН: поставил бы %s = False одному узлу. ЗАВЕРШЕНО" % PROP)
        return
    w.set_editor_property(PROP, False)
    unreal.AsdaUmgBuilder.compile_and_save(bp)
    # ⚠ `compile_and_save` не сохраняет — сохраняет `save_loaded_asset` (грабля шага видимости панелей).
    if not EAL.save_loaded_asset(bp, False):
        log("ОШИБКА: ассет не сохранён")
        raise RuntimeError("не сохранён %s" % PATH)
    unreal.log("ASDA_WRITES_ASSET: %s" % PATH)
    log("записано: %s.%s → False. ЗАВЕРШЕНО" % (NODE, PROP))


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    main()
