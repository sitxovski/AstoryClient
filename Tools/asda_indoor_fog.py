# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_indoor_fog.py — сценовый туман `MapFog` (ExponentialHeightFog) на 4 ЗАКРЫТЫХ картах без купола неба
#   (asda_maps.INDOOR_MAPS: castle_of_chaos, palace_of_queen, t_castle_of_chess, t_palace_of_queen).
#   Решение владельца 06.09.2026 («да») по вопросу sky_audit_report.md §«Не добавлялось (б)»: у карт нет SKY_DOME.JBM,
#   генератор asda_build_sky_dome.py ставит MapFog только вместе с куполом, а данные тумана в .mapraw ЕСТЬ.
#
# Параметры — ТЕ ЖЕ, что у 18 открытых карт (одна функция asda_build_sky_dome.place_map_fog + плотность fogspan
# доводки сета A, setA_fix_report.md §4 / review_setA_fix.md §2):
#   density  = ASDA_FOGK / (fogEnd − fogStart), FOGK 2737 (умолч.) → на fogEnd у всех карт одинаково 73 %
#              (UE: T = 2^(−ln2·density/1000·(L−StartDistance)), кап FogMaxOpacity 0.9);
#   start_distance = fogStart; fog_height_falloff 1e-4 (дистанционный, без вертикали); fog_max_opacity 0.9;
#   fog_inscattering_luminance = asda_build_sky_dome.fog_color_ue(fog ARGB .mapraw): ★ 06.09.2026 fog_srgb — sRGB→linear при
#   ASDA_FOG_SRGB=1 (умолч.; review_rename_and_fog.md §4), raw 0..1 при 0; fog_cutoff_distance 0 (выкл).
#   Купола нет → «Apply Fogging» купола не при чём.
#
# Режимы (env ASDA_INDOOR_FOG_MODE): apply (умолч.) — поставить и сохранить; dump — только снимок акторов (ничего не пишет).
#   ASDA_INDOOR_FOG_MAPS="a||b" (умолч. все INDOOR_MAPS); ASDA_FOGK; ASDA_INDOOR_FOG_OUT=<json> (для dump).
# Закон check-existing-before-creating: если на карте УЖЕ есть ExponentialHeightFog — НЕ создаю дубль, предупреждение, карта
#   пропускается (ничего не сохраняется). Меняется ТОЛЬКО добавление актора; гейт — дифф дампов до/после (indoor_fog_driver.py).
# Запуск: python Tools/run_headless_safe.py Tools/asda_indoor_fog.py --marker "INDOOR-FOG ЗАВЕРШЕНО"
# ★ Модуль БЕЗ побочных эффектов при импорте (tools-no-import-side-effects): работа под guard внизу.
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_maps as _MAPS
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
import asda_mapraw as _MR
import asda_paths as _P

MODE = os.environ.get("ASDA_INDOOR_FOG_MODE", "apply").lower()
FOGK = float(os.environ.get("ASDA_FOGK", "2737"))
_want = os.environ.get("ASDA_INDOOR_FOG_MAPS", "")
MAPS = [m for m in _MAPS.INDOOR_MAPS if not _want or m in _want.split("||")]
OUT = os.environ.get("ASDA_INDOOR_FOG_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05/indoor_fog_dump.json")

FOG_PROPS = ["fog_density", "fog_height_falloff", "fog_inscattering_luminance", "fog_max_opacity", "start_distance",
             "fog_cutoff_distance", "directional_inscattering_luminance", "directional_inscattering_exponent",
             "directional_inscattering_start_distance", "enable_volumetric_fog", "second_fog_data"]


def level_path(m):
    return _U.level_path(m)   # ★ волна 2 п. 7: одна точка правила (asda_ue_paths.level_path)


def _sval(v):
    import unreal
    if isinstance(v, (unreal.LinearColor,)):
        return [round(v.r, 6), round(v.g, 6), round(v.b, 6), round(v.a, 6)]
    if isinstance(v, (unreal.Vector,)):
        return [round(v.x, 3), round(v.y, 3), round(v.z, 3)]
    if isinstance(v, (int, float, bool, str)) or v is None:
        return v
    try:
        import re
        # Struct без адреса памяти (иначе ложный дифф): у ExponentialHeightFogData адрес стоит В СКОБКАХ «(0x…)», а не после
        # пробела — split(" 0x") его не резал (★ 06.09 fog_srgb: гейт откатил 58 файлов из-за этого; теперь regex по любому 0x…)
        return re.sub(r"0x[0-9A-Fa-f]+", "0x?", str(v))
    except Exception:
        return repr(v)


def dump_level(eas):
    """Снимок ВСЕХ акторов уровня: label, класс, положение (для дифф-гейта «только новый актор») + поля тумана."""
    import unreal
    actors = []
    fogs = []
    for a in eas.get_all_level_actors():
        try:
            L = a.get_actor_location()
            rec = {"label": a.get_actor_label(), "class": a.get_class().get_name(),
                   "loc": [round(L.x, 2), round(L.y, 2), round(L.z, 2)]}
        except Exception as ex:
            rec = {"label": "?", "class": "?", "err": str(ex)}
        actors.append(rec)
        if isinstance(a, unreal.ExponentialHeightFog):
            fc = a.get_component_by_class(unreal.ExponentialHeightFogComponent)
            d = {"label": a.get_actor_label(), "name": a.get_name()}
            for p in FOG_PROPS:
                try:
                    d[p] = _sval(fc.get_editor_property(p))
                except Exception:
                    pass
            fogs.append(d)
    actors.sort(key=lambda r: (r["class"], r["label"]))
    return {"count": len(actors), "actors": actors, "fogs": fogs}


def apply_map(m, les, eas, place_map_fog):
    import unreal
    mr = _P.mapraw(m)
    sc = _MR.read_scene(mr)
    fog_rgb, fs, fe = sc["fog_rgb"], float(sc["fogStart"]), float(sc["fogEnd"])
    span = fe - fs
    if span <= 0:
        unreal.log_warning("[INDOOR-FOG] %s: fogEnd %.0f ≤ fogStart %.0f — не ставлю" % (m, fe, fs))
        return {"map": m, "status": "skip(Δ≤0)"}
    dens = FOGK / span
    have = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.ExponentialHeightFog)]
    if have:
        unreal.log_warning("[INDOOR-FOG] %s: ExponentialHeightFog УЖЕ есть (%s) — дубль не создаю, карту не сохраняю"
                           % (m, ", ".join(a.get_actor_label() for a in have)))
        return {"map": m, "status": "skip(есть %d)" % len(have), "existing": [a.get_actor_label() for a in have]}
    n0 = len(eas.get_all_level_actors())
    fog_a = place_map_fog(eas, fog_rgb, fs, dens)
    n1 = len(eas.get_all_level_actors())
    fc = fog_a.get_component_by_class(unreal.ExponentialHeightFogComponent)
    got = {p: _sval(fc.get_editor_property(p)) for p in ("fog_density", "start_distance", "fog_height_falloff",
                                                          "fog_max_opacity", "fog_inscattering_luminance", "fog_cutoff_distance")}
    # самопроверка: записалось ровно то, что просили
    errs = []
    if abs(float(got["fog_density"]) - dens) > 1e-6 * max(1.0, dens): errs.append("density")
    if abs(float(got["start_distance"]) - fs) > 0.5: errs.append("start")
    if abs(float(got["fog_cutoff_distance"])) > 0.5: errs.append("cutoff")
    col = got["fog_inscattering_luminance"]
    import asda_build_sky_dome as _SKY
    want = _SKY.fog_color_ue(fog_rgb)      # ★ 06.09 fog_srgb: ожидание — та же функция цвета, что пишет place_map_fog
    if any(abs(col[i] - want[i]) > 1e-4 for i in range(3)): errs.append("color")
    if n1 - n0 != 1: errs.append("actors+%d" % (n1 - n0))
    if errs:
        unreal.log_error("[INDOOR-FOG] %s: самопроверка НЕ прошла (%s) — НЕ сохраняю" % (m, ", ".join(errs)))
        eas.destroy_actor(fog_a)
        return {"map": m, "status": "fail(%s)" % ",".join(errs), "got": got}
    ok = les.save_current_level()
    unreal.log("[INDOOR-FOG] %s: MapFog поставлен: density=%.6g (FOGK %.0f/(fogEnd %.0f−fogStart %.0f=%.0f)) start=%.0f "
               "цвет raw=(%.3f,%.3f,%.3f) → UE=(%.4f,%.4f,%.4f) (ASDA_FOG_SRGB=%d) falloff=1e-4 max=0.9 cutoff=0; акторов %d→%d; save=%s"
               % (m, dens, FOGK, fe, fs, span, fs, fog_rgb[0], fog_rgb[1], fog_rgb[2], want[0], want[1], want[2],
                  int(_SKY.FOG_SRGB), n0, n1, ok))
    return {"map": m, "status": "ok" if ok else "fail(save)", "density": dens, "fogStart": fs, "fogEnd": fe,
            "fog_rgb": list(fog_rgb), "fog_ue": list(want), "fog_srgb": int(_SKY.FOG_SRGB), "got": got,
            "actors_before": n0, "actors_after": n1}


def main():
    import unreal
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    res = {}
    place = None
    if MODE == "apply":
        import asda_build_sky_dome as _SKY      # та же функция, что у 18 открытых карт
        place = _SKY.place_map_fog
    for m in MAPS:
        lp = level_path(m)
        unreal.log("[INDOOR-FOG] %s: карта %s (режим %s)" % (m, lp, MODE))
        if not les.load_level(lp):
            unreal.log_error("[INDOOR-FOG] %s: уровень не загрузился" % m)
            res[m] = {"map": m, "status": "fail(load)"}
            continue
        try:
            if MODE == "dump":
                res[m] = dump_level(eas)
                unreal.log("[INDOOR-FOG] %s: акторов %d, ExponentialHeightFog %d" % (m, res[m]["count"], len(res[m]["fogs"])))
            else:
                res[m] = apply_map(m, les, eas, place)
        except Exception as ex:
            unreal.log_error("[INDOOR-FOG] %s: исключение %s" % (m, ex))
            res[m] = {"map": m, "status": "fail(%s)" % ex}
    out = OUT if MODE == "dump" else OUT.replace(".json", "_apply.json")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        json.dump(res, f, ensure_ascii=False, indent=1)
    unreal.log("[INDOOR-FOG] записано %s" % out)
    bad = [m for m, r in res.items() if MODE == "apply" and str(r.get("status")) != "ok"]
    unreal.log("[INDOOR-FOG] итог: %s" % ", ".join("%s=%s" % (m, r.get("status", "dump")) for m, r in res.items()))
    unreal.log("INDOOR-FOG ЗАВЕРШЕНО%s" % ("" if not bad else " С ОШИБКАМИ: " + ", ".join(bad)))


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
