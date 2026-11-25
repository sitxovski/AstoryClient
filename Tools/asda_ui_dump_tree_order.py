# -*- coding: utf-8 -*-
# asda_ui_dump_tree_order.py — слепок всех WBP в ПОРЯДКЕ ДЕРЕВА (порядок детей = порядок отрисовки), 24.10.2026.
#   Нужен планировщику `asda_ui_frame_seam_plan.py` (у `asda_ui_dump_all.py` строки отсортированы — порядок теряется).
#   ЗАПУСК: set ASDA_TREEORDER_OUT=<файл> & python Tools\run_headless_safe.py Tools\asda_ui_dump_tree_order.py --marker "дерево-порядок] окон"
import os
# Copyright (c) 2026 @sitxovski (Discord)
import unreal

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder
OUT = os.environ["ASDA_TREEORDER_OUT"]


def main():
    rows = []
    n = 0
    for a in EAL.list_assets("/Game/AsdaAssets/UI", recursive=True, include_folder=False):
        p = a.split(".")[0]
        name = p.rsplit("/", 1)[-1]
        if not name.startswith("WBP_"):
            continue
        bp = EAL.load_asset(p)
        if not bp:
            continue
        n += 1
        rows.append("#WIN %s %s" % (name, p))
        for i, line in enumerate(B.dump_widget_tree(bp).split("\n")):
            if line.strip():
                rows.append("%s :: %05d %s" % (name, i, " ".join(line.split())))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(rows))
    unreal.log("[дерево-порядок] окон %d ГОТОВО" % n)



# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуля — ничего.
if __name__ == "__main__":
    main()
