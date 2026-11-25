# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_boost_cdo_check.py — ПРОВЕРКА РАСКАТА ЯРКОСТИ ПО CDO (04.11.2026, skillglow): какой MIC стоит в эмиттерах РАБОЧЕГО класса
#   BP_Fx_* (generated class CDO — его рисует игра) и какой EmissiveBoost у этого MIC. Таблица имён пакета хранит и имена
#   редакторского скелет-класса, поэтому по ней одной раскат не проверить. Только чтение.
#   Запуск: python Tools/run_headless_safe.py Tools/asda_fx_boost_cdo_check.py --marker "CDO-CHECK ЗАВЕРШЕНО"
#   ASDA_CDO_OUT — csv (по умолчанию Reports/skillglow_1104/cdo_check.csv).
import collections
import csv
import os
import re


def main():
    import unreal
    EAL = unreal.EditorAssetLibrary
    out = os.environ.get("ASDA_CDO_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/skillglow_1104/cdo_check.csv")
    names = [n for n in EAL.list_assets("/Game/AsdaAssets/Effects", False, False) if "/BP_Fx_" in n]
    stat = collections.Counter()
    rows = []
    for n in names:
        bp = EAL.load_asset(n.split(".")[0])
        if not bp:
            continue
        cdo = unreal.get_default_object(bp.generated_class())
        jeb = str(cdo.get_editor_property("effect_name"))
        for i, e in enumerate(list(cdo.get_editor_property("emitters"))):
            for field in ("material", "material_b"):
                m = e.get_editor_property(field)
                if not m:
                    continue
                v = 1.0
                try:
                    v = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(m, "EmissiveBoost")
                except Exception:
                    pass
                fam = re.sub(r"_[0-9].*$", "", jeb)[:14]
                rows.append([jeb, i, field, m.get_name(), round(v, 3)])
                stat[round(v, 3)] += 1
    with open(out, "w", encoding="utf-8-sig", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["эффект", "эмиттер", "поле", "MIC", "EmissiveBoost"])
        w.writerows(rows)
    unreal.log("[CDO-CHECK] BP %d, слотов %d, по бусту: %s" % (len(names), len(rows), dict(sorted(stat.items()))))
    unreal.log("CDO-CHECK ЗАВЕРШЕНО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
