# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_f1_probe_maps.py — перепись эффектов на картах alpen и nightvalley (волна Ф1).
#   Нужна, чтобы выбрать прототипные эффекты и поставить на них камеры: имя .jeb, мировая позиция,
#   число эмиттеров, материалы (MIC) и режим бленда каждого эмиттера. Ничего не меняет.
# Результат: Reports/fx_f1/fx_map_actors.json  (+ печать сводки по семействам в лог).
# Запуск: python Tools/run_headless_safe.py Tools/asda_fx_f1_probe_maps.py --marker "FX-PROBE ЗАВЕРШЕНО"
import json
import os
import unreal

EAL = unreal.EditorAssetLibrary
LEVELS = (("alpen", "/Game/Map/alpen/alpen"), ("nightvalley", "/Game/Map/nightvalley/nightvalley"))
OUT = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/fx_f1/fx_map_actors.json"


def mat_name(m):
    """Короткое имя материала эмиттера (None → '—')."""
    return m.get_name() if m else "—"


def probe(key, level):
    """Список словарей по каждому эффект-актору открытой карты."""
    unreal.EditorLoadingAndSavingUtils.load_map(level)
    world = unreal.EditorLevelLibrary.get_editor_world()
    acts = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.AsdaEffectActor)
    rows = []
    for a in acts:
        loc = a.get_actor_location()
        ems = a.get_editor_property("emitters")
        row = {"map": key, "actor": a.get_name(),
               "jeb": str(a.get_editor_property("effect_name")),
               "loc": [round(loc.x, 1), round(loc.y, 1), round(loc.z, 1)],
               "cull": float(a.get_editor_property("cull_distance")),
               "emitters": []}
        for e in ems:
            row["emitters"].append({
                "mat": mat_name(e.get_editor_property("material")),
                "matB": mat_name(e.get_editor_property("material_b")),
                "mesh": bool(e.get_editor_property("mesh_emitter")),   # UPROPERTY bMeshEmitter → python «mesh_emitter»
            })
        rows.append(row)
    unreal.log("[FX-PROBE] %s: эффект-акторов %d" % (key, len(rows)))
    return rows


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон tools-no-import-side-effects)."""
    all_rows = []
    for key, level in LEVELS:
        try:
            all_rows += probe(key, level)
        except Exception as ex:
            unreal.log_warning("[FX-PROBE] %s: %s" % (key, ex))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(all_rows, f, ensure_ascii=False, indent=1)
    for r in all_rows:
        unreal.log("[FX-PROBE] %s %s @%s эмиттеров=%d мат=%s" % (
            r["map"], r["jeb"], r["loc"], len(r["emitters"]),
            ",".join(sorted({e["mat"] for e in r["emitters"]}))))
    unreal.log("[FX-PROBE] записано %s" % OUT)
    unreal.log("FX-PROBE ЗАВЕРШЕНО")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
