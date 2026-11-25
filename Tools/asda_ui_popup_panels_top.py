# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""Пачка 18, баг 7 (08.11.2026): всплывающая панель покупки внутри окна рисуется ПОВЕРХ соседей по корневому канвасу.

Причина. Импортёр кладёт детей корня в порядке `.JUI`, всем z = 0. У окна 15300 панель покупки 15515 `BOOSTER_PUR_WIN_grp`
(дочь №13) объявлена РАНЬШЕ полосы вкладок 15545 `BOOSTER_SHOP_MENU_grp` (№14). У оригинала 15545 не показывается вовсе
(`Visible="0"`, `SetVisible` к ней нет — порт включил её исправлением Д-2, ключ `asda.PremiumMall.ShowBoosterCategories`),
поэтому вопрос «кто выше» у оригинала не вставал. У порта полоса — следующий ребёнок, и при равном z она рисуется над панелью.
Тот же порядок у панелей покупки окна 6000 (6296/6333 и три панели `*_Purchase_NEW`, дети №82…86): после них в списке идут полосы
`SHELLITEM_STORE_MENU_NEW1_3_*`, индексные подписи и кнопки web-магазина.

Правка. Точечно в ассете (без перегенерации): z слота корня панели = (наибольший z соседей) + 1, только если панель не стоит
выше всех уже сейчас. Порядок детей и все остальные узлы не меняются.
Сухой прогон: переменная окружения ASDA_POPUPTOP_DRYRUN=1.
Запуск: python Tools/run_headless_safe.py Tools/asda_ui_popup_panels_top.py --marker "ЗАВЕРШЕНО: панели поверх"
"""
import os
import traceback

import unreal

EAL = unreal.EditorAssetLibrary

# Окно → корни всплывающих панелей внутри него.
PANELS = [
    ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_BOOSTER_SHOP_WIN", ["BOOSTER_PUR_WIN_grp"]),
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN",
     ["SHELLITEM_EQUIP_WIN_grp", "SHELLITEM_FUNC_WIN_grp", "HAIR_Purchase_NEW_grp", "Package_Purchase_NEW_grp",
      "BOOSTER_Purchase_NEW_grp"]),
]


def log(m):
    """Строка лога с меткой."""
    unreal.log("[POPUPTOP] " + str(m))


def main():
    """Точка входа."""
    dry = bool(os.environ.get("ASDA_POPUPTOP_DRYRUN"))
    B = unreal.AsdaUmgBuilder
    for path, names in PANELS:
        bp = EAL.load_asset(path)
        if not bp:
            log("НЕТ АССЕТА %s" % path)
            continue
        root = B.get_root_canvas(bp)
        kids = [root.get_child_at(i) for i in range(root.get_children_count())]
        zs = {}
        for i, w in enumerate(kids):
            if w:
                zs[w.get_name()] = (int(w.get_editor_property("slot").get_editor_property("z_order")), i)
        changed = 0
        for n in names:
            if n not in zs:
                log("%s: панели %s НЕТ" % (path, n))
                continue
            others = [v[0] for k, v in zs.items() if k not in names]
            want = max(others) + 1 if others else 1
            cur = zs[n][0]
            if cur >= want:
                log("%s / %s: z=%d уже выше соседей (наибольший %d)" % (path, n, cur, want - 1))
                continue
            log("%s / %s: z %d → %d (ребёнок №%d из %d)%s" % (path.rsplit("/", 1)[-1], n, cur, want, zs[n][1], len(kids),
                                                             "  [СУХОЙ]" if dry else ""))
            if not dry:
                for w in kids:
                    if w and w.get_name() == n:
                        w.get_editor_property("slot").set_editor_property("z_order", want)
                changed += 1
        if changed and not dry:
            # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
            B.compile_and_save(bp)
            if not EAL.save_loaded_asset(bp, False):
                raise RuntimeError("не сохранён %s" % path)
            log("%s сохранён (панелей поднято %d)" % (path, changed))
    log("ЗАВЕРШЕНО: панели поверх")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[POPUPTOP] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
