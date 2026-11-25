# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_sky_phantoms.py — ПЕРЕГОН четырёх небесных мастеров «на месте», чтобы вычистить ФАНТОМЫ бага
#   UE 5.8 `DeleteAllMaterialExpressions` (аудит `Reports/lighting_audit_2026-09-03.md` Д-10):
#     M_AsdaSkyDome      80 узлов, дубли SkyClouds×4, FogInvRange×4, DomeScale×3, FogAdd×3, …  → 41
#     M_AsdaSkyHorizon   40 узлов, дубли SkyHorizon×3, FogColor×3, HorizonBoost×2               → 21
#     M_AsdaCloudMesh    15 узлов, дубли ShadeColor×2, Tint×2 (кэш отдавал дефолты ФАНТОМОВ)     → 12
#     M_AsdaCloudShadowLF 28 узлов, дубли Strength×2, SpeedU×2, Contrast×2                      → 17
#
# ЧТО ДЕЛАЕТ: зовёт штатные генераторы (они уже на `asda_mat_graph`: чистка в цикле до пустого +
#   `check_graph(expected=N)`), а ДО и ПОСЛЕ снимает снимок ИЗ АССЕТА (узлы/дубли/кэш дефолтов/свойства)
#   и сверяет: свойства мастера — как были; кэш дефолтов — как был (для CloudMesh допускается
#   ОЖИДАЕМАЯ смена ShadeColor/Tint с фантомных на значения подключённых узлов, см. ниже).
#   При любом расхождении вне списка ожидаемых — RuntimeError (ассеты уже сохранены генератором,
#   откат — из бэкапа `_backup/sky_phantoms_2026-09-03/`).
#   ★ БЕЗ delete_asset: пересоздание мастера обнулило бы parent у per-map MIC (18 карт) и usage-флаги.
#   ★ Генератор купола (`asda_build_sky_dome.build_master`) трогает ТОЛЬКО мастер — купол/MIC/акторы
#     карт здесь не пересобираются (их значения в per-map MIC хранятся по имени и не зависят от графа).
#
# Запуск (СКРЫТО, редактор закрыт; бэкап .uasset до прогона обязателен):
#   python Tools/run_headless_safe.py Tools/asda_fix_sky_phantoms.py --marker SKYFIX-FINISHED
import os
import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal
import asda_mat_graph as MG
import asda_build_sky_dome as SKY          # тело под __main__/__spec__, импорт безопасен
import asda_add_cloud_shadows as CSH
import asda_build_cloud_mesh_mat as CMM

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

# (путь, функция сборки, ожидаемое число узлов)
JOBS = [
    ("/Game/Materials/M_AsdaSkyDome",       SKY.build_master,         SKY.MASTER_EXPRESSIONS),
    ("/Game/Materials/M_AsdaSkyHorizon",    SKY.build_master_horizon, SKY.MASTER_HORIZON_EXPRESSIONS),
    ("/Game/Materials/M_AsdaCloudMesh",     CMM.build_master,         CMM.MASTER_EXPRESSIONS),
    ("/Game/Materials/M_AsdaCloudShadowLF", CSH.build_master,         CSH.MASTER_EXPRESSIONS),
]
# Свойства мастера, которые обязаны остаться как были.
KEEP_PROPS = ["material_domain", "blend_mode", "shading_model", "two_sided", "is_sky",
              "used_with_skeletal_mesh", "used_with_instanced_static_meshes", "used_with_static_lighting",
              "cast_dynamic_shadow_as_masked", "translucency_lighting_mode",
              "use_translucency_vertex_fog", "opacity_mask_clip_value"]
# ОЖИДАЕМЫЕ изменения кэша дефолтов: у M_AsdaCloudMesh кэш брал первый узел по имени — ФАНТОМ
#   (ShadeColor (0.62,0.67,0.78), Tint (0.96,0.97,1)); подключённые узлы несут (0.55,0.62,0.75)/(0.97,0.98,1).
#   Все 18 per-map MIC перекрывают оба параметра, на картинку это не влияет.
EXPECTED_CACHE_CHANGES = {
    ("M_AsdaCloudMesh", "ShadeColor"): ((0.62, 0.67, 0.78, 1.0), (0.55, 0.62, 0.75, 1.0)),
    ("M_AsdaCloudMesh", "Tint"):       ((0.96, 0.97, 1.0, 1.0),  (0.97, 0.98, 1.0, 1.0)),
}


def L(s):
    unreal.log("[SKYFIX] %s" % s)


def snapshot(path):
    m = EAL.load_asset(path)
    if not m:
        raise RuntimeError("нет ассета %s" % path)
    snap = {"nodes": MG.expression_count(m), "dups": MG.parameter_duplicates(m), "props": {}, "cache": {}, "outs": {}}
    for k in KEEP_PROPS:
        try:
            snap["props"][k] = str(m.get_editor_property(k))
        except Exception:
            pass
    for pn in sorted(set(str(x) for x in MEL.get_scalar_parameter_names(m))):
        snap["cache"]["s:" + pn] = round(MEL.get_material_default_scalar_parameter_value(m, pn), 6)
    for pn in sorted(set(str(x) for x in MEL.get_vector_parameter_names(m))):
        c = MEL.get_material_default_vector_parameter_value(m, pn)
        snap["cache"]["v:" + pn] = tuple(round(v, 4) for v in (c.r, c.g, c.b, c.a))
    for pn in sorted(set(str(x) for x in MEL.get_texture_parameter_names(m))):
        t = MEL.get_material_default_texture_parameter_value(m, pn)
        snap["cache"]["t:" + pn] = t.get_name() if t else None
    for nm, prop in (("Emissive", unreal.MaterialProperty.MP_EMISSIVE_COLOR), ("Opacity", unreal.MaterialProperty.MP_OPACITY),
                     ("BaseColor", unreal.MaterialProperty.MP_BASE_COLOR), ("OpacityMask", unreal.MaterialProperty.MP_OPACITY_MASK)):
        n = MEL.get_material_property_input_node(m, prop)
        snap["outs"][nm] = n.get_class().get_name().replace("MaterialExpression", "") if n else None
    return snap


def show(tag, path, s):
    L("%s %s: узлов %d, дубли %s, выходы %s" % (tag, path.rsplit("/", 1)[-1], s["nodes"], s["dups"], s["outs"]))
    for k, v in sorted(s["cache"].items()):
        L("%s   кэш %s = %s" % (tag, k, v))
    L("%s   свойства %s" % (tag, s["props"]))


def main():
    # ★ ASDA_SKY_JOBS="M_AsdaCloudShadowLF||M_AsdaSkyDome" — перегнать только часть мастеров
    #   (повтор после несохранившегося ассета: 03.09.2026 сохранение M_AsdaCloudShadowLF упало на
    #   MoveFile Error 32 — файл держал другой процесс; остальные три сохранились).
    _only = os.environ.get("ASDA_SKY_JOBS", "")
    jobs = [j for j in JOBS if (not _only or j[0].rsplit("/", 1)[-1] in _only.split("||"))]
    if not jobs:
        raise RuntimeError("[SKYFIX] ASDA_SKY_JOBS=%r не совпал ни с одним мастером" % _only)
    before = {}
    for path, _fn, _n in jobs:
        before[path] = snapshot(path); show("ДО", path, before[path])
    failures = []
    for path, fn, expected in jobs:
        L("=== перегон %s ===" % path)
        fn()                                  # чистка в цикле + сборка + check_graph + save
        after = snapshot(path); show("ПОСЛЕ", path, after)
        name = path.rsplit("/", 1)[-1]
        # ★ Сохранение ПРОВЕРЯЕМ ПО ДИСКУ: save_loaded_asset молча возвращает True, а MoveFile может
        #   упасть (Error 32 — файл занят) — тогда на диске остаётся СТАРЫЙ ассет с фантомами.
        disk = os.path.join(r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Content/Materials", name + ".uasset")
        try:
            import time
            age = time.time() - os.path.getmtime(disk)
        except OSError:
            age = 1e9
        L("   файл %s: возраст %.0f с после сохранения" % (disk, age))
        if age > 600:
            failures.append("%s: файл на диске НЕ обновился (возраст %.0f с) — сохранение не прошло" % (name, age))
        b = before[path]
        if after["nodes"] != expected:
            failures.append("%s: узлов %d, ожидалось %d" % (name, after["nodes"], expected))
        if after["dups"]:
            failures.append("%s: остались дубли %s" % (name, after["dups"]))
        for k in b["props"]:
            if after["props"].get(k) != b["props"][k]:
                failures.append("%s: свойство %s было %s стало %s" % (name, k, b["props"][k], after["props"].get(k)))
        for k in set(b["cache"]) | set(after["cache"]):
            bv, av = b["cache"].get(k), after["cache"].get(k)
            if bv == av:
                continue
            exp = EXPECTED_CACHE_CHANGES.get((name, k.split(":", 1)[1]))
            if exp and k.startswith("v:") and tuple(round(x, 4) for x in exp[0]) == bv and tuple(round(x, 4) for x in exp[1]) == av:
                L("   ожидаемая смена кэша %s.%s: %s → %s (фантом → подключённый узел)" % (name, k, bv, av))
                continue
            failures.append("%s: кэш %s было %s стало %s" % (name, k, bv, av))
        if after["outs"] != b["outs"]:
            failures.append("%s: выходы были %s стали %s" % (name, b["outs"], after["outs"]))
        try:
            unreal.SystemLibrary.collect_garbage()
        except Exception:
            pass
    if failures:
        for f in failures:
            unreal.log_error("[SKYFIX] РАСХОЖДЕНИЕ: %s" % f)
        raise RuntimeError("[SKYFIX] перегон дал расхождения (%d) — см. лог, откат из _backup/sky_phantoms_2026-09-03" % len(failures))
    L("итог: %s" % ", ".join("%s %d→%d" % (p.rsplit("/", 1)[-1], before[p]["nodes"], n) for p, _f, n in jobs))
    unreal.log("=== ЗАВЕРШЕНО (SKYFIX-FINISHED) ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
