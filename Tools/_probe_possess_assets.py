# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Проба (только чтение): что реально лежит в ассетах фичи «одержимость» после импорта.
#   Печатает длительность и скелет клипов anim_00..04 обоих полов (женские должны стать 1.50 с —
#   это признак того, что там теперь `cre_crewmy`, а не мужской `cre_cremmy` с idle 2.00 с),
#   парных клипов 205..224 и клипов «души», плюс наличие мешей силуэта и строк DT_soulsoulmatetext.
# Запуск: python Tools/run_headless_safe.py Tools/_probe_possess_assets.py --marker "ПРОБА ЗАВЕРШЕНА"
import unreal

DEST = "/Game/AsdaAssets/Models/Skeletal/Characters"
def log(m): unreal.log("[проба-одержимость] " + str(m))

def show(path):
    a = unreal.load_asset(path)
    if not a:
        log("  %-58s НЕТ" % path.rsplit("/", 1)[-1]); return
    ln = float(a.get_editor_property("sequence_length"))
    sk = a.get_editor_property("skeleton")
    log("  %-58s %.2f с  скелет=%s" % (path.rsplit("/", 1)[-1], ln, sk.get_name() if sk else "НЕТ"))

def main():
    for g, mine, pairs in (("m", range(0, 5), list(range(205, 210)) + list(range(215, 220))),
                           ("f", range(0, 5), list(range(210, 215)) + list(range(220, 225)))):
        base = "%s/ch1_%s_cremmy_preview/SkeletalMeshes/ch1_%s_cremmy_previewanim_%%02d" % (DEST, g, g)
        log("=== пол %s: МОЙ набор anim_00..04 (жен. должен быть 1.50 с = cre_crewmy) ===" % g.upper())
        for n in mine: show(base % n)
        log("=== пол %s: ПАРНЫЕ клипы ===" % g.upper())
        for n in pairs: show(base % n)
    for name in ("cre_cresoulman_preview", "cre_cresoulwoman_preview"):
        log("=== «душа» %s ===" % name)
        m = unreal.load_asset("%s/%s/SkeletalMeshes/%s" % (DEST, name, name))
        log("  меш: %s" % ("есть" if m else "НЕТ"))
        for n in range(5):
            show("%s/%s/SkeletalMeshes/%sanim_%02d" % (DEST, name, name, n))
    dt = unreal.load_asset("/Game/AsdaAssets/Data/ItemDB/DT_soulsoulmatetext")
    if dt:
        rows = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
        log("DT_soulsoulmatetext: строк %d" % len(rows))
    else:
        log("DT_soulsoulmatetext: НЕТ")
    log("ПРОБА ЗАВЕРШЕНА")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
