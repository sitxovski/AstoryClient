# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_dump_all.py — СТРУКТУРНЫЙ СЛЕПОК ВСЕХ ОКОН UI (для сверки «до/после» перегенерации).
#
#   ЗАЧЕМ. Сверка таблиц имён `.uasset` (`asda_ui_name_diff.py`) ловит ПОТЕРЮ УЗЛА, но не видит
#   изменений геометрии/цвета. Перед массовой перегенерацией нужен полный слепок, чтобы после неё
#   доказать, что не потерялось НИЧЕГО — ни узлов, ни раскладки.
#
#   Слепок = вывод `UAsdaUmgBuilder::DumpWidgetTree` по каждому окну: имя, класс, родитель, схема
#   слота, анкоры, отступы, alignment/autosize/z, а у TextBlock — цвет и текст. Строки внутри окна
#   СОРТИРУЮТСЯ, чтобы порядок обхода дерева не давал ложных расхождений.
#
#   ЗАПУСК:  set ASDA_DUMPALL_OUT=@@ASTORY_DEV_ROOT@@\Reports\ui_snapshot_before.txt
#            Tools\run_headless.bat Tools\asda_ui_dump_all.py
import os

import unreal

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder

OUT = os.environ.get("ASDA_DUMPALL_OUT") or r"@@ASTORY_DEV_ROOT@@\Reports\ui_snapshot.txt"


def main():
    rows = []
    names = []
    for a in EAL.list_assets("/Game/AsdaAssets/UI", recursive=True, include_folder=False):
        p = a.split(".")[0]
        n = p.rsplit("/", 1)[-1]
        if n.startswith("WBP_"):
            names.append((n, p))
    # ★ Ключ строки — ИМЯ ассета. Если одно имя лежит в двух папках (так было у
    #   `WBP_Job_Guide_Win`: `Temp/` и `Player_UI/Tutorial/`), сверка «до/после» соврёт —
    #   строки двух РАЗНЫХ окон сольются. Громко предупреждаем.
    seen = {}
    for n, p in names:
        if n in seen:
            unreal.log_error("[слепок-UI] ДУБЛЬ ИМЕНИ %s: %s и %s — сверка по имени будет врать, "
                             "разведи окна (см. `_wsuffix` в asda_jui_generic_umg.py)"
                             % (n, seen[n], p))
        seen[n] = p
    for n, p in sorted(names):
        bp = EAL.load_asset(p)
        if not bp:
            rows.append("%s :: НЕ ГРУЗИТСЯ" % n)
            continue
        try:
            body = B.dump_widget_tree(bp)
        except Exception as e:                              # noqa: BLE001
            rows.append("%s :: ОШИБКА ДАМПА %s" % (n, e))
            continue
        # Строки сортируем: порядок обхода дерева не должен давать ложных расхождений.
        for line in sorted(x.rstrip() for x in body.split("\n") if x.strip()):
            rows.append("%s :: %s" % (n, " ".join(line.split())))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(rows))
    unreal.log("[слепок-UI] окон %d, строк %d → %s" % (len(names), len(rows), OUT))


# ★★★ ЗАКОН tools-no-import-side-effects.
if __name__ == "__main__":
    main()
