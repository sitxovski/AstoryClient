# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_premium_shop_layout.py — ★ НОРМАЛИЗАТОР РАСКЛАДКИ ВИТРИНЫ ПРЕМИУМ-МАГАЗИНА 6000
#   (`WBP_SHELLITEM_STORE_WIN`, окно `SHELLITEM_STORE_WIN` 6000).
#
# ═══ ИСТОРИЯ (важно, чтобы шаг не «чинили» обратно) ═══════════════════════════════════════════
# 2026-08-28, первый заход: шаг СДВИГАЛ полосу 6009 на −29 px и сетку 6083 на −89 px константами.
# В тот же день владелец сверил окно с оригиналом, и выяснилось, что модель полос была разобрана
# неверно: 6009 и 6002 — МЁРТВЫЕ полосы (эталон гасит обе в `CShellItemStore_InitControls@0xC28820`,
# `ida.c:479948-479949`), а живая навигация — `SHELLITEM_STORE_MENU_NEW1…4` (58200…58203), которую
# показывает `itemBagDlgInit@0xC2C040`. Подъём содержимого теперь ЗАВИСИТ от того, показана ли
# вторая (и третья) полоса, то есть считается В РАНТАЙМЕ
# (`UAsdaPremiumShopSubsystem::ApplyContentShift`, приём склада D19 с якорем из исходных смещений).
#
# ⇒ ЗАДАЧА ЭТОГО ШАГА ТЕПЕРЬ ОДНА: держать группы на их ИСХОДНЫХ местах из `gamemain_jui.ini`,
#   то есть ОТКАТИТЬ прежний константный сдвиг, если ассет ещё несёт его. Рантайм двигает слоты
#   от запомненных исходных смещений — если оставить их сдвинутыми, подъём удвоится.
#
#   `SHELLITEM_STORE_MENU_grp`         → T 69  (контрол 6009  `Origin "5 70"`)
#   `SHELLITEM_ITEMLIST_grp`           → T 149 (контрол 6083  `Origin "4 149"`)
#   `SHELLITEM_STORE_Packageshop_grp`  → T 149 (контрол 58300 `Origin "4 149"`)
#
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_ui_premium_shop_layout.py
#   Сухой прогон: set ASDA_PREMLAYOUT_DRYRUN=1
import os
import sys
import traceback
import unreal
# ★ дедуп №2 В1 Т12: общие помощники правки WBP (обход дерева, канвасный слот, смещения) — asda_ui_widget_common
import os as _uwc_os, sys as _uwc_sys
if _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)) not in _uwc_sys.path:
    _uwc_sys.path.insert(0, _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)))
import asda_ui_widget_common as _UWC   # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

EAL = unreal.EditorAssetLibrary
EPS = 0.01

ASSET = "/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN"

# имя узла -> (T «сдвинутое прежним заходом», целевой T = как в .JUI). Двигаем ТОЛЬКО по Y.
TARGETS = [
    ("SHELLITEM_STORE_MENU_grp",           40.0,  69.0),   # 6009  — вернуть на .JUI
    ("SHELLITEM_ITEMLIST_grp",             60.0, 149.0),   # 6083  — вернуть на .JUI
    ("SHELLITEM_STORE_Packageshop_grp",    60.0, 149.0),   # 58300 — вернуть на .JUI
]


def log(m):
    unreal.log("[раскладка премиум-магазина] %s" % m)


walk = _UWC.walk   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


canvas_slot = _UWC.canvas_slot   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def main():
    dry = bool(os.environ.get("ASDA_PREMLAYOUT_DRYRUN"))
    B = unreal.AsdaUmgBuilder
    if not EAL.does_asset_exist(ASSET):
        log("НЕТ АССЕТА %s" % ASSET)
        return
    bp = EAL.load_asset(ASSET)
    root = B.get_root_canvas(bp)
    if not root:
        log("!! нет корневого канваса у %s" % ASSET)
        return

    nodes = {}
    for w in walk(root):
        nodes.setdefault(w.get_name(), w)

    moved = already = 0
    for name, orig_t, want_t in TARGETS:
        w = nodes.get(name)
        if w is None:
            log("!! нет узла %s — пропуск" % name)
            continue
        s = canvas_slot(w)
        if s is None:
            log("!! у %s нет слота канваса — пропуск" % name)
            continue
        lay = s.get_editor_property("layout_data")
        off = lay.get_editor_property("offsets")
        cur = float(off.top)
        if abs(cur - want_t) < EPS:
            already += 1
            continue
        if abs(cur - orig_t) > EPS:
            # ★ Ни исходное, ни целевое — значит узел кто-то двигал руками. Молча перетирать
            #   чужую правку запрещено (урок `asda_ui_handmade`): ругаемся и пропускаем.
            log("!! %s стоит на T=%.0f, а ожидались %.0f (сдвинутое) или %.0f (.JUI) — НЕ ТРОГАЮ"
                % (name, cur, orig_t, want_t))
            continue
        if not dry:
            lay.set_editor_property("offsets",
                                    unreal.Margin(off.left, want_t, off.right, off.bottom))
            s.set_editor_property("layout_data", lay)
            s.set_editor_property("auto_size", False)
        moved += 1
        log("  %s: T %.0f -> %.0f (возврат на место .JUI)" % (name, cur, want_t))

    if not dry and moved:
        # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
        B.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не удалось сохранить %s" % ASSET)
    log("готово: сдвинуто %d, уже на месте %d%s" % (moved, already, " (СУХОЙ ПРОГОН)" if dry else ""))


if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[раскладка премиум-магазина] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
