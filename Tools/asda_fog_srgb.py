# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fog_srgb.py — цвет сценового тумана sRGB→linear на ВСЕХ 22 картах НА МЕСТЕ, без перегенерации купола (06.09.2026,
#   решение владельца «делаем»; основание — review_rename_and_fog.md §4 «цвет тумана: вердикт», indoor_fog_report.md §3).
#   Дефект: place_map_fog (asda_build_sky_dome.py) и FogColor MIC купола писали raw ARGB/255 как FLinearColor; оригинал
#   D3DRS_FOGCOLOR = sRGB-байты без SRGBWRITEENABLE → у нас туман ~2× светлее. Лечение: ОДНА функция цвета
#   asda_build_sky_dome.fog_color_ue(fog_rgb) (ASDA_FOG_SRGB=1 умолч. → sRGB→linear по формуле UE FromSRGBColor).
# Что меняет apply (и ТОЛЬКО это — гейт диффом дампов до/после в fog_srgb_driver.py):
#   • MapFog.fog_inscattering_luminance на 22 картах (акторов не создаёт: нет MapFog → предупреждение и пропуск карты);
#   • FogColor у MI_skydome и MI_skyhorizon (/Game/Map/<карта>/Materials) на 18 открытых картах (asda_maps.OUTDOOR_MAPS);
#   density/start/falloff/max/cutoff, остальные параметры MIC — не трогаются.
# Режимы (env ASDA_FOG_SRGB_MODE): dump (умолч.) — снимок акторов + полей тумана + ВСЕХ параметров 2 MIC, ничего не пишет;
#   apply — записать и сохранить. ASDA_FOG_SRGB_MAPS="a||b" (умолч. все ALL_MAPS); ASDA_FOG_SRGB_OUT=<json>; ASDA_FOG_SRGB=1|0.
# Запуск: python Tools/run_headless_safe.py Tools/asda_fog_srgb.py --marker "FOG-SRGB ЗАВЕРШЕНО"  (скрыто, -NullRHI, -SCCProvider=None)
# ★ Модуль БЕЗ побочных эффектов при импорте (tools-no-import-side-effects): работа под guard внизу.
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_maps as _MAPS
import asda_mapraw as _MR
import asda_paths as _P
import asda_indoor_fog as _IF        # dump_level / level_path / _sval — общий дамп акторов и полей тумана
import asda_login_guard as _LG       # ★ 16.09.2026 (обратный обход savedir14): единый гейт сцены логина

MODE = os.environ.get("ASDA_FOG_SRGB_MODE", "dump").lower()
_want = os.environ.get("ASDA_FOG_SRGB_MAPS", "")
MAPS = [m for m in _MAPS.ALL_MAPS if not _want or m in _want.split("||")]
OUT = os.environ.get("ASDA_FOG_SRGB_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05/fog_srgb_dump.json")
MIC_NAMES = ("MI_skydome", "MI_skyhorizon")
TOL = 1e-5


def mic_path(m, nm):
    return "/Game/Map/%s/Materials/%s" % (_MAPS.map_asset_name(m), nm)


def dump_mics(m):
    """Все параметры (scalar/vector/texture) + родитель двух MIC купола карты; нет ассета → ключа нет."""
    import unreal
    EAL = unreal.EditorAssetLibrary; MEL = unreal.MaterialEditingLibrary
    res = {}
    for nm in MIC_NAMES:
        p = mic_path(m, nm)
        if not EAL.does_asset_exist(p):
            continue
        mi = EAL.load_asset(p)
        d = {}
        try:
            par = mi.get_editor_property("parent"); d["parent"] = par.get_path_name() if par else None
        except Exception as ex:
            d["parent"] = "err %s" % ex
        for pn in MEL.get_scalar_parameter_names(mi):
            d["s:" + str(pn)] = round(float(MEL.get_material_instance_scalar_parameter_value(mi, pn)), 6)
        for pn in MEL.get_vector_parameter_names(mi):
            d["v:" + str(pn)] = _IF._sval(MEL.get_material_instance_vector_parameter_value(mi, pn))
        for pn in MEL.get_texture_parameter_names(mi):
            t = MEL.get_material_instance_texture_parameter_value(mi, pn)
            d["t:" + str(pn)] = t.get_path_name() if t else None
        res[nm] = d
    return res


def apply_map(m, les, eas):
    import unreal
    import asda_build_sky_dome as _SKY   # та же функция цвета, что у генератора купола и asda_indoor_fog
    EAL = unreal.EditorAssetLibrary; MEL = unreal.MaterialEditingLibrary
    sc = _MR.read_scene(_P.mapraw(m))
    if not sc:
        unreal.log_error("[FOG-SRGB] %s: нет .mapraw — пропуск" % m); return {"map": m, "status": "fail(mapraw)"}
    fog_rgb = sc["fog_rgb"]; want = _SKY.fog_color_ue(fog_rgb)
    LC = unreal.LinearColor(want[0], want[1], want[2], 1.0)
    rec = {"map": m, "fog_raw": list(fog_rgb), "fog_ue": list(want), "fog_srgb": int(_SKY.FOG_SRGB), "mics": {}}
    fogs = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.ExponentialHeightFog)]
    fog_a = next((a for a in fogs if a.get_actor_label() == "MapFog"), None)
    if fog_a is None:
        unreal.log_warning("[FOG-SRGB] %s: актора MapFog нет (ExponentialHeightFog: %s) — НЕ создаю, уровень не сохраняю"
                           % (m, [a.get_actor_label() for a in fogs]))
        rec["status"] = "skip(нет MapFog)"; return rec
    if len(fogs) > 1:
        unreal.log_warning("[FOG-SRGB] %s: ExponentialHeightFog больше одного (%s) — правлю только MapFog"
                           % (m, [a.get_actor_label() for a in fogs]))
    fc = fog_a.get_component_by_class(unreal.ExponentialHeightFogComponent)
    n0 = len(eas.get_all_level_actors())
    before = _IF._sval(fc.get_editor_property("fog_inscattering_luminance"))
    fc.set_editor_property("fog_inscattering_luminance", LC)
    got = _IF._sval(fc.get_editor_property("fog_inscattering_luminance"))
    n1 = len(eas.get_all_level_actors())
    errs = []
    if any(abs(got[i] - want[i]) > TOL for i in range(3)): errs.append("MapFog color %s≠%s" % (got, want))
    if n1 != n0: errs.append("actors %d→%d" % (n0, n1))
    rec.update({"mapfog_before": before, "mapfog_after": got})
    if errs:
        unreal.log_error("[FOG-SRGB] %s: самопроверка MapFog НЕ прошла (%s) — НЕ сохраняю" % (m, "; ".join(errs)))
        fc.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(before[0], before[1], before[2], before[3]))
        rec["status"] = "fail(%s)" % "; ".join(errs); return rec
    ok_lvl = les.save_current_level()
    rec["level_saved"] = bool(ok_lvl)
    # купол: 2 MIC на открытых картах (закрытые — без купола, MIC нет и не нужно)
    mic_fail = []
    for nm in MIC_NAMES:
        p = mic_path(m, nm)
        if not EAL.does_asset_exist(p):
            if m in _MAPS.OUTDOOR_MAPS:
                unreal.log_warning("[FOG-SRGB] %s: нет %s — открытая карта без купола?" % (m, p)); mic_fail.append(nm)
            continue
        mi = EAL.load_asset(p)
        b = _IF._sval(MEL.get_material_instance_vector_parameter_value(mi, "FogColor"))
        MEL.set_material_instance_vector_parameter_value(mi, "FogColor", LC)
        g = _IF._sval(MEL.get_material_instance_vector_parameter_value(mi, "FogColor"))
        okp = all(abs(g[i] - want[i]) <= TOL for i in range(3))
        saved = EAL.save_loaded_asset(mi) if okp else False
        if not okp or not saved: mic_fail.append(nm)
        rec["mics"][nm] = {"before": b, "after": g, "saved": bool(saved)}
    rec["status"] = "ok" if (ok_lvl and not mic_fail) else "fail(level=%s mic=%s)" % (ok_lvl, mic_fail)
    unreal.log("[FOG-SRGB] %s: MapFog цвет %s → %s (raw %s, ASDA_FOG_SRGB=%d); MIC %s; save level=%s → %s"
               % (m, [round(x, 4) for x in before[:3]], [round(x, 4) for x in got[:3]], [round(x, 3) for x in fog_rgb],
                  int(_SKY.FOG_SRGB), {k: v["saved"] for k, v in rec["mics"].items()}, ok_lvl, rec["status"]))
    return rec


def main():
    import unreal
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    res = {}
    for m in MAPS:
        lp = _IF.level_path(m)
        # ★ 16.09.2026 (обратный обход ревизии savedir14): режим apply правит актор MapFog, СОХРАНЯЕТ
        #   уровень и пишет два MIC под корнем карты. Гейт сцены логина стоит ДО load_level — то есть до
        #   первой операции с уровнем, — поэтому закрывает и режим dump. Спрашиваем обе точки записи:
        #   сам уровень и корень карты (под ним лежат MI_skydome/MI_skyhorizon).
        if _LG.login_map_write_blocked(lp, "asda_fog_srgb.py", quiet=True) or \
           _LG.login_map_write_blocked("/Game/Map/%s" % _MAPS.map_asset_name(m), "asda_fog_srgb.py", quiet=True):
            res[m] = {"map": m, "status": "skip(сцена логина)"}
            continue
        unreal.log("[FOG-SRGB] %s: карта %s (режим %s)" % (m, lp, MODE))
        if not les.load_level(lp):
            unreal.log_error("[FOG-SRGB] %s: уровень не загрузился" % m)
            res[m] = {"map": m, "status": "fail(load)"}; continue
        try:
            if MODE == "dump":
                r = _IF.dump_level(eas); r["mics"] = dump_mics(m); res[m] = r
                unreal.log("[FOG-SRGB] %s: акторов %d, ExponentialHeightFog %d, MIC купола %d"
                           % (m, r["count"], len(r["fogs"]), len(r["mics"])))
            elif MODE == "apply":
                res[m] = apply_map(m, les, eas)
            else:
                unreal.log_error("[FOG-SRGB] неизвестный режим %r" % MODE); res[m] = {"map": m, "status": "fail(mode)"}
        except Exception as ex:
            unreal.log_error("[FOG-SRGB] %s: исключение %s" % (m, ex))
            res[m] = {"map": m, "status": "fail(%s)" % ex}
    out = OUT if MODE == "dump" else OUT.replace(".json", "_apply.json")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        json.dump(res, f, ensure_ascii=False, indent=1)
    unreal.log("[FOG-SRGB] записано %s" % out)
    bad = [m for m, r in res.items() if MODE == "apply" and str(r.get("status")) != "ok"]
    unreal.log("[FOG-SRGB] итог: %s" % ", ".join("%s=%s" % (m, r.get("status", "dump")) for m, r in res.items()))
    unreal.log("FOG-SRGB ЗАВЕРШЕНО%s" % ("" if not bad else " С ОШИБКАМИ: " + ", ".join(bad)))


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
