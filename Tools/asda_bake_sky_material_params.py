# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_bake_sky_material_params.py — ★ ПОСТОЯННЫЕ ПАРАМЕТРЫ НЕБА/ТЕНЕЙ ОБЛАКОВ → В МАТЕРИАЛЫ.
#
# ПРОБЛЕМА (§6.2 аудита `Reports/audit_runtime_style.md`). Акторы неба задавали часть параметров
#   материала в РАНТАЙМЕ, хотя значения постоянные и одинаковые на всех 18 открытых картах
#   (проверено живым прогоном `Tools/asda_probe_sky_params.py`, лог `Reports/headless_probe_sky2.log`):
#     · `AAsdaCloudShadowsActor` — `TileSize / SpeedU / SpeedV / Strength / Contrast`;
#     · `AAsdaSkyDomeActor`      — `ScrollBoost / HorizonBoost`.
#
# ЗАМЕР ДО ПРАВКИ (тот же лог):
#   мастер `M_AsdaCloudShadowLF`: TileSize=60000 ✓, SpeedU=0.01 ✓, SpeedV=0 ✓,
#                                 **Strength=0.45 ✗ (актор ставил 1.0)**,
#                                 **Contrast=3.0 ✗ (актор ставил 5.0)**;
#   на всех 18 картах `MI_skydome.ScrollBoost = 10.0`, а актор ставил **9.0** ⇒ в игре работало 9.0;
#   на всех 18 картах `MI_skyhorizon.HorizonBoost = 1.725` = ровно то, что ставил актор (дубль).
#   Акторы `AsdaLayeredCloudsActor` в уровнях ОТСУТСТВУЮТ (0 из 18, два метода: grep по `.umap`
#   и живой обход) — они сняты с вооружения (`asda_build_sky_dome.py`), их параметры не трогаем.
#
# ЧТО ДЕЛАЕТ ШАГ (идемпотентно, КАРТИНКА НЕ МЕНЯЕТСЯ — кладём РОВНО те значения, что работали):
#   1) дефолты мастера `M_AsdaCloudShadowLF`: Strength=1.0, Contrast=5.0 (остальные три уже верны);
#   2) `MI_skydome.ScrollBoost = 9.0` на всех картах (было 10.0 — актор перебивал его на 9.0);
#   3) `MI_skyhorizon.HorizonBoost = 1.725` — сверка (уже стоит, шаг ничего не пишет).
#   После этого рантайм-присвоения из акторов удалены, а параметры видны и правятся в редакторе.
#
# ★ ПРАВИЛО ГЕНЕРАТОРА тоже поправлено, иначе пересборка неба вернула бы 10.0:
#   `asda_build_sky_dome.py` — дефолт `ASDA_SKY_SCROLL_BOOST` 10.0 → 9.0.
#
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_bake_sky_material_params.py
#   Сухой прогон: set ASDA_SKYMAT_DRYRUN=1
import os
import sys as _sys
import os as _os
import traceback

import unreal

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_maps as _MAPS                                   # noqa: E402
import asda_login_guard as _LG                              # noqa: E402  ★ 16.09.2026: гейт сцены логина

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
_asset = _MAPS.map_asset_name
EPS = 1e-4

# мастер → {имя скалярного параметра: нужный дефолт}
MASTER_DEFAULTS = {
    "/Game/Materials/M_AsdaCloudShadowLF": {"Strength": 1.0, "Contrast": 5.0},
}
# per-map MIC → {параметр: значение}
MIC_VALUES = {
    "MI_skydome":    {"ScrollBoost": 9.0},
    "MI_skyhorizon": {"HorizonBoost": 1.725},
}


def log(m):
    unreal.log("[небо-мат] %s" % m)


def set_master_default(mat, name, value, dry):
    """Дефолт скалярного параметра МАСТЕР-материала = `default_value` его узла-параметра."""
    # ★ `Material.Expressions` из Python НЕ ЧИТАЕТСЯ (поле protected), а `expression_collection`
    #   у `UMaterial` в UE 5.8 вообще нет — обход даёт только `get_material_expressions`
    #   (разведка API: `Tools/asda_probe_mat_api.py`, лог `Reports/headless_matapi.log`).
    exprs = MEL.get_material_expressions(mat)
    hit = 0
    changed = False
    # ★★★ УЗЛОВ С ОДНИМ ИМЕНЕМ БЫВАЕТ НЕСКОЛЬКО. В `M_AsdaCloudShadowLF` их по ДВА на `Strength`
    #   и `Contrast` (найдено прогоном: первый проход поправил один узел, второй — второй).
    #   Поэтому идём по ВСЕМ совпадениям, а не выходим на первом, иначе шаг не сходится за прогон.
    for e in exprs:
        if not isinstance(e, unreal.MaterialExpressionScalarParameter):
            continue
        if str(e.get_editor_property("parameter_name")) != name:
            continue
        hit += 1
        cur = float(e.get_editor_property("default_value"))
        if abs(cur - value) < EPS:
            continue
        if not dry:
            e.set_editor_property("default_value", float(value))
        log("  %s.%s узел #%d: %.4f → %.4f" % (mat.get_name(), name, hit, cur, value))
        changed = True
    if hit == 0:
        unreal.log_warning("[небо-мат] %s: узла-параметра %s НЕТ" % (mat.get_name(), name))
    else:
        log("  %s.%s: узлов %d, %s" % (mat.get_name(), name, hit,
                                       "правок нет" if not changed else "приведены к %.4f" % value))
    return changed


def main():
    dry = bool(os.environ.get("ASDA_SKYMAT_DRYRUN"))
    log("старт%s" % ("  [СУХОЙ ПРОГОН]" if dry else ""))

    # ── 1. дефолты мастеров ─────────────────────────────────────────────────────────────────
    for path, params in MASTER_DEFAULTS.items():
        if not EAL.does_asset_exist(path):
            unreal.log_warning("[небо-мат] НЕТ мастера %s" % path)
            continue
        mat = EAL.load_asset(path)
        touched = False
        for n, v in params.items():
            touched = set_master_default(mat, n, v, dry) or touched
        if touched and not dry:
            MEL.recompile_material(mat)
            if not EAL.save_loaded_asset(mat, False):
                raise RuntimeError("не сохранён мастер %s" % path)
            log("  %s пересобран и сохранён" % path)

    # ── 2. per-map MIC ──────────────────────────────────────────────────────────────────────
    changed = same = missing = 0
    for mp in _MAPS.OUTDOOR_MAPS:
        root = "/Game/Map/%s/Materials" % _asset(mp)
        # ★ 16.09.2026 (обратный обход ревизии savedir14): шаг правит и СОХРАНЯЕТ MIC под корнем карты —
        #   гейт сцены логина по КОРНЮ карты, ДО save_loaded_asset.
        if _LG.login_map_write_blocked("/Game/Map/%s" % _asset(mp), "asda_bake_sky_material_params.py", quiet=True):
            continue
        for nm, params in MIC_VALUES.items():
            p = "%s/%s" % (root, nm)
            if not EAL.does_asset_exist(p):
                missing += 1
                log("  %s: нет %s" % (mp, nm))
                continue
            mi = EAL.load_asset(p)
            touched = False
            for pn, pv in params.items():
                cur = MEL.get_material_instance_scalar_parameter_value(mi, pn)
                if abs(float(cur) - pv) < EPS:
                    same += 1
                    continue
                if not dry:
                    MEL.set_material_instance_scalar_parameter_value(mi, pn, pv)
                log("  %s/%s.%s %.4f → %.4f" % (mp, nm, pn, cur, pv))
                changed += 1
                touched = True
            if touched and not dry:
                if not EAL.save_loaded_asset(mi, False):
                    raise RuntimeError("не сохранён %s" % p)
    log("MIC: изменено %d, уже верных %d, отсутствует %d" % (changed, same, missing))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    try:
        main()
    except Exception:                                       # noqa: BLE001
        unreal.log_error("[небо-мат] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
