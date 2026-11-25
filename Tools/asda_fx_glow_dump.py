# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_glow_dump.py — СНИМОК полей «Свет эффекта»/«Effect Glow» с ассетов BP_Fx_* и с акторов карты.
#   Нужен ровно для одного: доказать, что ПЕРЕДЕЛКА КАТЕГОРИИ НЕ ПОТЕРЯЛА УЖЕ СОХРАНЁННЫЕ ЗНАЧЕНИЯ.
#   Снимок делается ДО пересборки модуля и ПОСЛЕ неё, файлы сравниваются построчно.
#   ★ Читаем ТОЛЬКО get_editor_property — ничего не пишем, ассеты не трогаем.
#   Запуск: python Tools/run_headless_safe.py Tools/asda_fx_glow_dump.py --marker "ГЛОУ-ДАМП: ЗАВЕРШЕНО"
#   Переменные: ASDA_GLOW_OUT — куда положить json; ASDA_GLOW_LIMIT — сколько ассетов (0 = все).
import os, json

OUT = os.environ.get("ASDA_GLOW_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/fx_glowui/glow_dump_before.json")
LIMIT = int(os.environ.get("ASDA_GLOW_LIMIT", "0"))

# Все поля структуры по snake_case-именам UE-Python. Новые (эмиссия) читаем «мягко»: до пересборки их нет.
ALIASES = {
    "b_enabled": ["b_enabled", "enabled"], "b_manual": ["b_manual", "manual"],
    "b_affect_gi": ["b_affect_gi", "affect_gi"],
    "b_override_emissive": ["b_override_emissive", "override_emissive"],
}
FIELDS = ["b_enabled", "b_manual", "family_id", "intensity_cd",
          "radius_mode", "radius_scale", "radius_bounds_scale", "radius_min", "radius_max",
          "color_mode", "color_srgb", "color_saturation",
          "flicker_hz", "flicker_amplitude", "position", "offset",
          "alpha_power", "priority", "max_draw_distance", "fade_range",
          "source_radius", "soft_source_radius", "volumetric_scattering", "b_affect_gi", "emitter",
          "b_override_emissive", "emissive_boost"]


def val(v):
    """Значение UE → сравнимый JSON (у структур repr содержит адрес — сравнение строк всегда «разные»)."""
    for keys in (("r", "g", "b", "a"), ("x", "y", "z")):
        if all(hasattr(v, k) for k in keys):
            return [round(float(getattr(v, k)), 4) for k in keys]
    if isinstance(v, float):
        return round(v, 4)
    if isinstance(v, (bool, int, str)):
        return v
    return str(v)


def snap(fx):
    d = {}
    for f in FIELDS:
        got = False
        for nm in ALIASES.get(f, [f]):
            try:
                d[f] = val(fx.get_editor_property(nm)); got = True; break
            except Exception:
                continue
        if not got:
            d[f] = "<нет поля>"
    return d


def main():
    import unreal
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    ar.wait_for_completion()
    assets = ar.get_assets_by_path("/Game/AsdaAssets/Effects", recursive=True)
    names = sorted([str(a.package_name) for a in assets if str(a.asset_name).startswith("BP_Fx_")])
    if LIMIT:
        names = names[:LIMIT]
    res, manual, errs = {}, [], 0
    for i, pkg in enumerate(names):
        try:
            bp = unreal.load_object(None, pkg + "." + pkg.rsplit("/", 1)[1])
            cdo = unreal.get_default_object(bp.generated_class())
            fx = cdo.get_editor_property("fx_light")
            s = snap(fx)
            res[pkg] = s
            if s.get("b_manual") is True:
                manual.append(pkg)
        except Exception as e:
            errs += 1
            res[pkg] = {"ОШИБКА": str(e)}
        if i % 200 == 0:
            unreal.log("[глоу-дамп] %d / %d" % (i, len(names)))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump({"assets": res, "manual": manual, "errors": errs}, f, ensure_ascii=False, indent=1, sort_keys=True)
    unreal.log("[глоу-дамп] ассетов %d, с ручной правкой %d, ошибок %d -> %s" % (len(res), len(manual), errs, OUT))
    unreal.log("ГЛОУ-ДАМП: ЗАВЕРШЕНО")


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
