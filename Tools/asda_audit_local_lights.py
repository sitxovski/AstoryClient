# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_audit_local_lights.py — АУДИТ (и по ключу — ПОЧИНКА) локальных динамических светов владельца: Point/Spot/Rect
#   акторы и компоненты ULocalLightComponent внутри BP-акторов на карте. Сет E, фаза E-4 (подготовка 07.09.2026),
#   спецификация — setE_plan_addendum_tiers_and_lights.md §2.6, отчёт — setE4_prep_report.md §1.
#   Sun_*/Sky_* (DirectionalLight/SkyLight) НЕ трогает — это не LocalLightComponent; их аудит — set0_dump_light_actors.py.
#
# ЧТО ПРОВЕРЯЕТ (правила §2.2/§2.6; числа — Tools/asda_light_presets.json, единственный источник):
#   ОШИБКА          E01 не Movable (при r.AllowStaticLighting=False Static/Stationary не существует — свет «не светит»);
#                   E02 тени при max_draw_distance=0 (r.LightMaxDrawDistanceScale бессилен, LightComponent.cpp:499 — 0 = «никогда не гаснет»).
#   ПРЕДУПРЕЖДЕНИЕ  W03 max_draw_distance=0 у света без теней; W04 тени без тега Main; W05 теневых в радиусе 3000 больше бюджета Epic (8);
#                   W06 DF-тени (use_ray_traced_distance_field_shadows) вкл; W07 cast_volumetric_shadow вкл; W08 радиус > radius_warn;
#                   W09 точка накрыта >4 теневых радиусов; W10 RectLight с тенями.
#   ЗАМЕТКА         N20 не inverse-squared; N21 тени при source_radius=0 (жёсткий край); N22 сила > пресета роли ×3; N23 единицы Unitless;
#                   N24 нет тега роли Main/Fill/Glow; N25 light function (на Low r.LightFunctionQuality=0 её гасит); N26 «мёртвый» свет
#                   (affects_world=false / intensity 0 / hidden); N27 fade=0 при max_draw_distance>0 (резкий «щелчок»); N28 LightingChannels ≠ {0}
#                   (принцип 6: каналы — маска влияния, не группа); N30 Main без affect_global_illumination (нет отскока Lumen).
#   Сводка: теней авторских M → по уровням останется min(M, бюджет 0/2/4/8); VSM-карт на Epic = 6·point_с_тенями + spot_с_тенями.
#
# ЗАПУСК (headless, редактор ЗАКРЫТ; через run_headless_safe.py — единственный правильный способ):
#   ASDA_MAP=alpen python Tools/run_headless_safe.py Tools/asda_audit_local_lights.py --marker LIGHTS-FINISHED
#   ASDA_MAP=ALL [ASDA_MAPS="alpen||rein"]      — все карты asda_maps.ALL_MAPS + L_Loading (или подмножество через '||');
#                   ★ 16.09.2026 (ревизия §7, СРЕДН-1): Login в список попадает ТОЛЬКО при ASDA_ALLOW_LOGIN_MAP_WRITE=1
#                   (в уровне логина стоит свет владельца). Имя карты сверяется с реестром карт — опечатка останавливает прогон
#                   (разовое исключение — ASDA_MAP_ALLOW_UNKNOWN=1, для временных карт).
#   ASDA_LIGHT_LEVELS="/Game/Temp/L_LightTest"  — явные пути уровней (перекрывает ASDA_MAP; для L_LightTest E-5b);
#                   уровень сцены логина и через них не открывается без ASDA_ALLOW_LOGIN_MAP_WRITE=1 (проверка в audit_level)
#   ASDA_LIGHT_MODE=audit (умолч.) | dryfix | fix — fix: Movable, max_draw_distance/fade из fix_defaults, cast_volumetric_shadow=false,
#                   DF-тени off. Интенсивность/радиус/цвет/тени НЕ трогает (авторские). Перед save — бэкап .umap в
#                   @@ASTORY_DEV_ROOT@@\_backup\<дата>_lightsfix\AstoryClient\Content\... (★ Т3: asda_headless_tool.backup_file) и ДИФФ-ГЕЙТ: изменились ТОЛЬКО разрешённые поля, иначе НЕ сохраняет.
#   ASDA_LIGHT_OUT=<dir>  (умолч. @@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05/lights) → lights_<map>.json + lights_<map>.md (+ lights_SUMMARY.md при ALL)
#   ASDA_LIGHT_PRESETS=<json> (умолч. Tools/asda_light_presets.json)
#   ASDA_LIGHT_SELFTEST=1  (или аргумент --selftest) — самопроверка правил на синтетических записях БЕЗ UE (python Tools/asda_audit_local_lights.py --selftest)
#
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает; `unreal` импортируется лениво внутри UE-функций,
#   чтобы SelfTest и чистые функции (verdicts_for/spatial_checks/render_md) работали обычным python.
import json, math, os, re, shutil, sys, datetime

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))                               # каталог Tools (для пресетов рядом)
PROJECT_DIR = os.path.abspath(os.path.join(TOOLS_DIR, ".."))                          # корень проекта AstoryClient
MAP = os.environ.get("ASDA_MAP", "alpen")                                             # карта | ALL
MODE = os.environ.get("ASDA_LIGHT_MODE", "audit").lower()                             # audit | dryfix | fix
OUT_DIR = os.environ.get("ASDA_LIGHT_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05/lights")   # куда писать json/md
PRESETS_PATH = os.environ.get("ASDA_LIGHT_PRESETS", os.path.join(TOOLS_DIR, "asda_light_presets.json"))  # пресеты/пороги
LEVELS_ENV = os.environ.get("ASDA_LIGHT_LEVELS", "")                                  # явные пути уровней через '||'
MARKER = "ЗАВЕРШЕНО (LIGHTS-FINISHED)"                                                # маркер для run_headless_safe.py --marker

# Поля, которые режим fix имеет право менять (дифф-гейт, как в сете A/E-1: любое другое изменение = отказ от сохранения).
FIX_ALLOWED_FIELDS = {"mobility", "max_draw_distance", "max_distance_fade_range", "cast_volumetric_shadow",
                      "use_ray_traced_distance_field_shadows"}
# Роль → пресет для порога N22 (сила выше пресета ×3); нет тега → сравниваем с фонарём.
ROLE_TO_PRESET = {"Main": "StreetLantern", "Fill": "Torch", "Glow": "CrystalGlow", None: "StreetLantern"}
# Поля компонента для снимка (имена editor-property python-API UE; отсутствующее у класса поле просто пропускается).
LIGHT_PROPS = ["mobility", "intensity", "intensity_units", "light_color", "use_temperature", "temperature",
               "attenuation_radius", "use_inverse_squared_falloff", "light_falloff_exponent",
               "source_radius", "soft_source_radius", "source_length", "inner_cone_angle", "outer_cone_angle",
               "source_width", "source_height", "cast_shadows", "cast_dynamic_shadows",
               "use_ray_traced_distance_field_shadows", "max_draw_distance", "max_distance_fade_range",
               "ies_texture", "use_ies_brightness", "ies_brightness_scale", "cast_volumetric_shadow",
               "volumetric_scattering_intensity", "affect_global_illumination", "indirect_lighting_intensity",
               "light_function_material", "lighting_channels", "affects_world", "visible", "hidden_in_game",
               "allow_mega_lights", "contact_shadow_length", "specular_scale"]
SEV_ORDER = {"ОШИБКА": 0, "ПРЕДУПРЕЖДЕНИЕ": 1, "ЗАМЕТКА": 2}                          # порядок вывода вердиктов


def load_presets(path=PRESETS_PATH):
    """Пресеты и пороги аудита из asda_light_presets.json (единственный источник чисел)."""
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def light_kind(comp_class_name):
    """'Point' | 'Spot' | 'Rect' | 'Other' по имени класса компонента (SpotLightComponent наследует Point — проверять раньше)."""
    n = (comp_class_name or "").lower()
    if "spot" in n: return "Spot"
    if "rect" in n: return "Rect"
    if "point" in n: return "Point"
    return "Other"


def role_from_tags(tags):
    """Роль по тегам актора: Main | Fill | Glow | None (регистр не важен; первый найденный)."""
    for t in tags or []:
        s = str(t).strip().lower()
        if s == "main": return "Main"
        if s == "fill": return "Fill"
        if s == "glow": return "Glow"
    return None


def to_candela(intensity, units, kind, outer_cone_angle=None):
    """Сила в канделах для сравнения с пресетом; None — если единицы не переводятся без площади (Nits/EV).
    Unitless: 1 кд = 625 unitless (GetUnitsConversionFactor: 100²/16). Lumens: point lm/(4π), spot lm/(2π(1−cos θ_outer))."""
    if intensity is None: return None
    u = str(units or "").lower()
    try: I = float(intensity)
    except (TypeError, ValueError): return None
    if "candela" in u: return I
    if "unitless" in u: return I / 625.0
    if "lumen" in u:
        if kind == "Spot" and outer_cone_angle:
            return I / (2.0 * math.pi * (1.0 - math.cos(math.radians(float(outer_cone_angle)))))
        return I / (4.0 * math.pi)
    return None   # Nits (rect: зависит от площади) / EV — не сравниваем


def _num(v, default=0.0):
    """Число из значения снимка (None/строка → default)."""
    try: return float(v)
    except (TypeError, ValueError): return default


def verdicts_for(rec, rules):
    """Вердикты ОДНОГО света (без пространственных). rec — запись снимка (см. collect_lights), rules — presets.json."""
    out = []
    kind = rec.get("kind")
    role = rec.get("role")
    shadows = bool(rec.get("cast_shadows"))
    mdd = _num(rec.get("max_draw_distance"))
    fade = _num(rec.get("max_distance_fade_range"))
    mob = str(rec.get("mobility") or "")
    if "MOVABLE" not in mob.upper():
        out.append(("ОШИБКА", "E01", "мобильность %s — при r.AllowStaticLighting=False только Movable светит (map_lit чинит)" % (mob or "?")))
    if shadows and mdd <= 0:
        out.append(("ОШИБКА", "E02", "тени при max_draw_distance=0 — свет и его 6 VSM-карт никогда не гаснут по дальности"))
    elif mdd <= 0:
        out.append(("ПРЕДУПРЕЖДЕНИЕ", "W03", "max_draw_distance=0 — r.LightMaxDrawDistanceScale уровня бессилен"))
    if shadows and role != "Main":
        out.append(("ПРЕДУПРЕЖДЕНИЕ", "W04", "тени без тега Main (роль %s) — бюджет уровня их снимет первыми" % (role or "нет")))
    if rec.get("use_ray_traced_distance_field_shadows"):
        out.append(("ПРЕДУПРЕЖДЕНИЕ", "W06", "DF-тени вкл — грубые (≤126 вокселей) и лишний проход; у локальных должно быть off"))
    if rec.get("cast_volumetric_shadow"):
        out.append(("ПРЕДУПРЕЖДЕНИЕ", "W07", "cast_volumetric_shadow вкл — отдельный проход на свет в объёмном тумане (только по решению владельца на Epic)"))
    radius = _num(rec.get("attenuation_radius"))
    if radius > _num(rules.get("radius_warn"), 1500):
        out.append(("ПРЕДУПРЕЖДЕНИЕ", "W08", "attenuation_radius %.0f > %.0f — лишние тайлы light-grid/карточки Lumen (MaxLightsPerTile 4/4/8)" % (radius, _num(rules.get("radius_warn"), 1500))))
    if kind == "Rect" and shadows:
        out.append(("ПРЕДУПРЕЖДЕНИЕ", "W10", "RectLight с тенями — под VSM дороги (ResolutionLodBiasLocal); rect только без теней"))
    if kind in ("Point", "Spot") and rec.get("use_inverse_squared_falloff") is False:
        out.append(("ЗАМЕТКА", "N20", "не inverse-squared — кд/лм не физичны, Lumen переоценивает свет"))
    if shadows and kind in ("Point", "Spot") and _num(rec.get("source_radius")) <= 0:
        out.append(("ЗАМЕТКА", "N21", "тени при source_radius=0 — жёсткий край тени (пресеты: 5–10)"))
    cd = to_candela(rec.get("intensity"), rec.get("intensity_units"), kind, rec.get("outer_cone_angle"))
    rec["intensity_cd"] = None if cd is None else round(cd, 3)
    preset = (rules.get("presets") or {}).get(ROLE_TO_PRESET.get(role, "StreetLantern"), {})
    ref = _num(preset.get("intensity_cd_alpen"))
    k = _num(rules.get("intensity_factor_warn"), 3.0)
    if cd is not None and ref > 0 and cd > ref * k:
        out.append(("ЗАМЕТКА", "N22", "сила %.2f кд > пресета %s (%.2f кд) ×%.0f" % (cd, ROLE_TO_PRESET.get(role, "StreetLantern"), ref, k)))
    if "unitless" in str(rec.get("intensity_units") or "").lower():
        out.append(("ЗАМЕТКА", "N23", "единицы Unitless (1 кд = 625) — пресеты в канделах; переключить IntensityUnits=Candelas"))
    if role is None:
        out.append(("ЗАМЕТКА", "N24", "нет тега роли Main/Fill/Glow — бюджет считает его заполняющим (тени снимет)"))
    if rec.get("light_function_material"):
        out.append(("ЗАМЕТКА", "N25", "light function — на Low r.LightFunctionQuality=0 её гасит (свет остаётся ровным)"))
    if rec.get("affects_world") is False or _num(rec.get("intensity")) <= 0 or rec.get("hidden_in_game") is True or rec.get("visible") is False:
        out.append(("ЗАМЕТКА", "N26", "«мёртвый» свет: affects_world=false / intensity 0 / скрыт — удалить или включить"))
    if mdd > 0 and fade <= 0:
        out.append(("ЗАМЕТКА", "N27", "max_distance_fade_range=0 при max_draw_distance>0 — свет «щёлкает» на границе (пресеты: 300–1000)"))
    ch = rec.get("lighting_channels")
    if isinstance(ch, dict) and (not ch.get("channel0", True) or ch.get("channel1") or ch.get("channel2")):
        out.append(("ЗАМЕТКА", "N28", "LightingChannels ≠ {0} — каналы это маска влияния, не группа; группировать тегами"))
    if role == "Main" and rec.get("affect_global_illumination") is False:
        out.append(("ЗАМЕТКА", "N30", "Main без affect_global_illumination — фонарь не даёт отскока Lumen на High/Epic"))
    return out


def _dist(a, b):
    """Евклидово расстояние между двумя [x,y,z]."""
    return math.sqrt(sum((float(a[i]) - float(b[i])) ** 2 for i in range(3)))


def spatial_checks(records, rules):
    """Пространственные вердикты по всем светам карты: W05 (теневых в радиусе shadow_cluster_radius > бюджет Epic) и
    W09 (точка накрыта > max_shadow_overlap теневых радиусов; точки-пробы = позиции всех светов). Возвращает {label: [вердикты]}."""
    out = {}
    budget = list(rules.get("shadow_budget_by_level") or [0, 2, 4, 8])
    epic = int(budget[-1]) if budget else 8
    cluster_r = _num(rules.get("shadow_cluster_radius"), 3000)
    max_overlap = int(_num(rules.get("max_shadow_overlap"), 4))
    shadow = [r for r in records if r.get("cast_shadows") and r.get("location")]
    for r in records:
        if not r.get("location"): continue
        vs = []
        if r.get("cast_shadows"):
            near = [s for s in shadow if _dist(s["location"], r["location"]) <= cluster_r]
            if len(near) > epic:
                vs.append(("ПРЕДУПРЕЖДЕНИЕ", "W05", "теневых светов в радиусе %.0f: %d > бюджета Epic %d (%s)" %
                           (cluster_r, len(near), epic, ", ".join(sorted(s["label"] for s in near if s is not r)[:6]))))
        cover = [s for s in shadow if _dist(s["location"], r["location"]) <= _num(s.get("attenuation_radius"))]
        if len(cover) > max_overlap:
            vs.append(("ПРЕДУПРЕЖДЕНИЕ", "W09", "точка %s накрыта %d теневыми радиусами (> %d): %s" %
                       (r["label"], len(cover), max_overlap, ", ".join(sorted(s["label"] for s in cover)[:6]))))
        if vs: out[r["label"]] = vs
    return out


def budget_summary(records, rules):
    """Сводка теней: авторских M, по уровням останется min(M, бюджет), VSM-карт на Epic (point ×6, spot/rect ×1)."""
    budget = list(rules.get("shadow_budget_by_level") or [0, 2, 4, 8])
    sh = [r for r in records if r.get("cast_shadows")]
    vsm = sum(6 if r.get("kind") == "Point" else 1 for r in sh)
    return {"lights": len(records), "shadow_authored": len(sh),
            "shadow_main": sum(1 for r in sh if r.get("role") == "Main"),
            "kept_by_level": [min(len(sh), int(b)) for b in budget],
            "vsm_maps_epic": vsm,
            "by_kind": {k: sum(1 for r in records if r.get("kind") == k) for k in ("Point", "Spot", "Rect", "Other")},
            "by_role": {str(k): sum(1 for r in records if r.get("role") == k) for k in ("Main", "Fill", "Glow", None)}}


def audit_records(records, rules):
    """Полный аудит списка записей: вердикты каждого + пространственные; возвращает (records, summary)."""
    spatial = spatial_checks(records, rules)
    counts = {"ОШИБКА": 0, "ПРЕДУПРЕЖДЕНИЕ": 0, "ЗАМЕТКА": 0}
    for r in records:
        vs = verdicts_for(r, rules) + spatial.get(r["label"], [])
        vs.sort(key=lambda v: (SEV_ORDER.get(v[0], 9), v[1]))
        r["verdicts"] = [{"sev": s, "code": c, "text": t} for s, c, t in vs]
        for s, _c, _t in vs: counts[s] = counts.get(s, 0) + 1
    summary = budget_summary(records, rules)
    summary["counts"] = counts
    return records, summary


def _fmt_color(rec):
    """Цвет для таблицы: температура (K) или sRGB-байты."""
    if rec.get("use_temperature"): return "%.0fK" % _num(rec.get("temperature"), 6500)
    c = rec.get("light_color")
    return "rgb(%s)" % ",".join(str(int(x)) for x in c[:3]) if isinstance(c, list) and len(c) >= 3 else "?"


def render_md(map_name, records, summary, mode, fixes=None):
    """Markdown-отчёт по карте: сводка + таблица светов + вердикты (для чата/отчёта волны)."""
    c = summary.get("counts", {})
    md = ["# Аудит локальных светов — %s (%s, режим %s)" % (map_name, datetime.date.today().isoformat(), mode), "",
          "Светов **%d** (Point %d / Spot %d / Rect %d); ролей Main %d / Fill %d / Glow %d / без тега %d. Авторских с тенями **%d** (Main %d) → "
          "останется по уровням Low/Med/High/Epic: **%s**; VSM-карт на Epic: %d." % (
              summary["lights"], summary["by_kind"]["Point"], summary["by_kind"]["Spot"], summary["by_kind"]["Rect"],
              summary["by_role"]["Main"], summary["by_role"]["Fill"], summary["by_role"]["Glow"], summary["by_role"]["None"],
              summary["shadow_authored"], summary["shadow_main"], "/".join(str(x) for x in summary["kept_by_level"]), summary["vsm_maps_epic"]),
          "", "Вердикты: **ОШИБКА %d**, ПРЕДУПРЕЖДЕНИЕ %d, ЗАМЕТКА %d." % (c.get("ОШИБКА", 0), c.get("ПРЕДУПРЕЖДЕНИЕ", 0), c.get("ЗАМЕТКА", 0)), ""]
    if fixes is not None:
        md += ["Починка (%s): изменений %d%s" % (mode, len(fixes), "" if fixes else " — нечего"), ""]
        md += ["- %s" % f for f in fixes] + ([""] if fixes else [])
    md += ["| Свет | Класс | Роль | Mob | I (кд) | Ед. | Цвет | R | inv² | Тени | MaxDraw/Fade | GI/Ind | VolSh | IES | Вердикты |",
           "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in records:
        vs = "; ".join("%s %s" % (v["code"], v["sev"][:4]) for v in r.get("verdicts", [])) or "—"
        md.append("| %s%s | %s | %s | %s | %s | %s | %s | %.0f | %s | %s | %.0f/%.0f | %s/%.1f | %s | %s | %s |" % (
            r["label"], " (BP)" if r.get("is_bp_component") else "", r.get("kind"), r.get("role") or "—",
            str(r.get("mobility") or "?").replace("ComponentMobility.", ""),
            "—" if r.get("intensity_cd") is None else ("%.2f" % r["intensity_cd"]), str(r.get("intensity_units") or "?").split(".")[-1],
            _fmt_color(r), _num(r.get("attenuation_radius")), "да" if r.get("use_inverse_squared_falloff") else "нет",
            "да" if r.get("cast_shadows") else "нет", _num(r.get("max_draw_distance")), _num(r.get("max_distance_fade_range")),
            "да" if r.get("affect_global_illumination") else "нет", _num(r.get("indirect_lighting_intensity")),
            "да" if r.get("cast_volumetric_shadow") else "нет", "да" if r.get("ies_texture") else "—", vs))
    md.append("")
    for r in records:
        for v in r.get("verdicts", []):
            md.append("- **%s** `%s` %s: %s" % (v["sev"], v["code"], r["label"], v["text"]))
    return "\n".join(md) + "\n"


# ───────────────────────────────────────────── UE-часть (только под редактором) ─────────────────────────────────────────────

def sval(v):
    """Сериализация значения editor-property в JSON-совместимое (без адресов памяти Struct — иначе ложный дифф)."""
    import unreal
    if isinstance(v, unreal.LinearColor): return [round(v.r, 5), round(v.g, 5), round(v.b, 5), round(v.a, 5)]
    if isinstance(v, unreal.Color): return [v.r, v.g, v.b, v.a]
    if isinstance(v, unreal.Vector): return [round(v.x, 3), round(v.y, 3), round(v.z, 3)]
    if isinstance(v, unreal.LightingChannels):
        return {"channel0": bool(v.get_editor_property("channel0")), "channel1": bool(v.get_editor_property("channel1")),
                "channel2": bool(v.get_editor_property("channel2"))}
    if isinstance(v, unreal.Object): return v.get_path_name()
    if isinstance(v, (int, float, bool, str)) or v is None: return v
    # ★ E-4 первый прогон: enum python-API печатается как «<ComponentMobility.MOVABLE: 2>» — оставляем «ComponentMobility.MOVABLE»
    m = re.match(r"^<([A-Za-z_][\w.]*): -?\d+>$", str(v))
    if m: return m.group(1)
    return re.sub(r"\s*\(0x[0-9A-Fa-f]+\)", "", str(v))


SKIPPED_TWINS = []   # спящие близнецы AAsdaLocalLightActor, пропущенные при сборе (E-4)


def collect_lights(eas):
    """Снимок всех ULocalLightComponent уровня (акторы Point/Spot/Rect И компоненты внутри BP-акторов) → (records, comps).
    Sun_*/Sky_* не попадают (DirectionalLight/SkyLight — не LocalLightComponent)."""
    import unreal
    records, comps = [], []
    for a in eas.get_all_level_actors():
        try: lcs = a.get_components_by_class(unreal.LocalLightComponent)
        except Exception: continue
        if not lcs: continue
        plain = isinstance(a, (unreal.PointLight, unreal.SpotLight, unreal.RectLight))
        tags = [str(t) for t in (a.tags or [])]
        # ★ E-4 (07.09.2026): у AAsdaLocalLightActor два компонента (Point и Spot), активен один по пресету; спящий близнец
        #   (affects_world=False и скрыт) — не свет, а конструкция актора: в аудит не входит (считается отдельно, в лог).
        is_wrapper = "AsdaLocalLightActor" in a.get_class().get_name()
        for lc in lcs:
            if is_wrapper and len(lcs) > 1:
                try:
                    if not lc.get_editor_property("affects_world") and not lc.get_editor_property("visible"):
                        SKIPPED_TWINS.append(a.get_actor_label() + "/" + lc.get_name()); continue
                except Exception: pass
            rec = {"label": a.get_actor_label() + ("" if plain or len(lcs) == 1 else "/" + lc.get_name()),
                   "actor_class": a.get_class().get_name(), "comp_class": lc.get_class().get_name(), "comp_name": lc.get_name(),
                   "is_bp_component": not plain, "tags": tags, "role": role_from_tags(tags),
                   "location": sval(lc.get_world_location())}
            rec["kind"] = light_kind(rec["comp_class"])
            for p in LIGHT_PROPS:
                try: rec[p] = sval(lc.get_editor_property(p))
                except Exception: pass
            records.append(rec); comps.append(lc)
    records_sorted = sorted(range(len(records)), key=lambda i: records[i]["label"])
    return [records[i] for i in records_sorted], [comps[i] for i in records_sorted]


def _snapshot(records):
    """Плоский снимок {label: {поле: значение}} для дифф-гейта (без вычисляемых полей)."""
    skip = {"verdicts", "intensity_cd"}
    return {r["label"]: {k: v for k, v in r.items() if k not in skip} for r in records}


def gate_changes(before, after, allowed=FIX_ALLOWED_FIELDS):
    """Дифф двух снимков: (разрешённые изменения, ЗАПРЕЩЁННЫЕ изменения) как списки 'label.поле: было → стало'."""
    ok, bad = [], []
    for label, b in before.items():
        a = after.get(label, {})
        for k, vb in b.items():
            va = a.get(k)
            if vb != va:
                (ok if k in allowed else bad).append("%s.%s: %r → %r" % (label, k, vb, va))
    for label in after:
        if label not in before: bad.append("%s: появился новый свет" % label)
    return ok, bad


def backup_umap(level_path):
    """Копия .umap до правки (закон backup-before-edit) — `asda_headless_tool.backup_file` (★ Т3, 11.10.2026):
    @@ASTORY_DEV_ROOT_ESC@@\\_backup\\<дата>_lightsfix\\<путь от @@ASTORY_DEV_ROOT_ESC@@>, сверка размера. Было: AstoryClient\\_backup\\…\\Content\\…,
    и повторный запуск в тот же день ПЕРЕЗАПИСЫВАЛ копию уже правленым уровнем; теперь первая копия дня не трогается.
    Нет файла уровня — исключение (как прежний copy2). Возвращает путь копии."""
    import asda_headless_tool as HT
    rel = "Content/" + level_path[len("/Game/"):] + ".umap"
    src = os.path.join(PROJECT_DIR, rel)
    dst = HT.backup_file(src, "lightsfix", log=lambda _m: None)
    if dst is None:
        raise RuntimeError("нет файла уровня для бэкапа: %s" % src)
    return dst


def apply_fix(records, comps, rules, dry):
    """Починка по правилам E01/E02/W03/W06/W07 значениями fix_defaults; dry — только список намерений. Возвращает список строк."""
    import unreal
    fd = rules.get("fix_defaults") or {}
    mdd = _num(fd.get("max_draw_distance"), 4000); fade = _num(fd.get("max_distance_fade_range"), 1000)
    done = []
    for rec, lc in zip(records, comps):
        codes = {v["code"] for v in rec.get("verdicts", [])}
        def _set(prop, val, why):
            done.append("%s.%s → %r (%s)%s" % (rec["label"], prop, val, why, " [dry]" if dry else ""))
            if not dry: lc.set_editor_property(prop, val)
        if "E01" in codes: _set("mobility", unreal.ComponentMobility.MOVABLE, "E01")
        if "E02" in codes or "W03" in codes:
            _set("max_draw_distance", mdd, "E02/W03")
            if _num(rec.get("max_distance_fade_range")) <= 0: _set("max_distance_fade_range", fade, "N27")
        if "W06" in codes: _set("use_ray_traced_distance_field_shadows", False, "W06")
        if "W07" in codes: _set("cast_volumetric_shadow", bool(fd.get("cast_volumetric_shadow", False)), "W07")
    return done


def level_paths():
    """Список (имя, путь уровня) по ASDA_LIGHT_LEVELS / ASDA_MAP / ASDA_MAPS (карты — asda_maps, единый санитайзер имён)."""
    if LEVELS_ENV:
        return [(p.rsplit("/", 1)[-1], p) for p in LEVELS_ENV.split("||") if p.strip()]
    import asda_maps as _MAPS
    import asda_login_guard as _LG
    if MAP.upper() == "ALL":
        names = [_MAPS.map_asset_name(m) for m in _MAPS.select_maps()]
        out = [(a, "/Game/Map/%s/%s" % (a, a)) for a in names]
        only = os.environ.get("ASDA_MAPS", "")
        for extra in ("Login", "L_Loading"):
            # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА (13.09.2026): уровень логина открывает ТОЛЬКО ASDA_ALLOW_LOGIN_MAP_WRITE=1
            #   ★ 16.09.2026 (ревизия §7, СРЕДН-1): БЫЛО сравнение с литералом «Login» и НЕ ТА переменная
            #   (ASDA_ALLOW_LOGIN_WRITE открывает ассеты сцены логина, но не сам уровень). Здесь проверка тихая —
            #   карта просто не попадает в список; строку [LOGIN-GUARD] печатает гейт в audit_level, если до него дойдёт.
            if _LG.login_map_write_blocked("/Game/Map/" + extra, "asda_audit_local_lights.py", quiet=True): continue
            if not only or extra in only.split("||"): out.append((extra, "/Game/Map/" + extra))
        return out
    a = _MAPS.map_asset_name(MAP)
    # ★ 16.09.2026 (ревизия §7, п. 5): имя карты из ASDA_MAP сверяется с реестром карт — опечатка («alpne») иначе
    #   молча даёт корень /Game/Map/<мусор>. Уровень логина этой проверкой не судится: он в реестре есть
    #   (cook_extra_levels), его закрывает гейт в audit_level.
    #   ★ 16.09.2026 (ревизия savedir7, СРЕДН-1): судится ПОСТРОЕННОЕ имя (`a`), а не сырое ASDA_MAP —
    #   инструмент идёт в /Game/Map/<a>/<a>, и гейт обязан смотреть на ту же строку. Сырое «/Game/Map/alpen»
    #   в переменной дало бы a='_Game_Map_alpen' при зелёном гейте.
    if _LG.map_name_blocked("/Game/Map/%s" % a, "asda_audit_local_lights.py"):
        return []
    return [(a, "/Game/Map/%s/%s" % (a, a) if a not in ("Login", "L_Loading") else "/Game/Map/" + a)]


def audit_level(name, lvl, rules):
    """Аудит (и починка по режиму) одного уровня; пишет lights_<name>.json/.md; возвращает summary."""
    import unreal
    # ★★★ 16.09.2026 (ревизия §7 savedir_guard_2026-09-16.md, СРЕДН-1): ЗАЩИТА УРОВНЯ СЦЕНЫ ЛОГИНА — ЗДЕСЬ, до
    #   load_level, потому что это ЕДИНСТВЕННАЯ точка, через которую проходят ОБА входа инструмента: и ASDA_MAP
    #   (level_paths), и ASDA_LIGHT_LEVELS (явные пути уровней, которые прежде вообще не проверялись и пускали
    #   /Game/Map/Login прямо в load_level). В режиме fix дальше идут set_editor_property по компонентам и
    #   save_current_level — правка уровня владельца. Прежний гейт (в level_paths) сравнивал имя карты с литералом
    #   «Login» и спрашивал НЕ ТУ переменную: уровень открывает только ASDA_ALLOW_LOGIN_MAP_WRITE=1.
    import asda_login_guard as _LG
    if _LG.login_map_write_blocked(lvl, "asda_audit_local_lights.py"):
        return None
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not unreal.EditorAssetLibrary.does_asset_exist(lvl):
        unreal.log_warning("LIGHTS %s: уровня %s нет — пропуск" % (name, lvl)); return None
    les.load_level(lvl)
    del SKIPPED_TWINS[:]
    records, comps = collect_lights(eas)
    records, summary = audit_records(records, rules)
    summary["inactive_twins_skipped"] = len(SKIPPED_TWINS)
    if SKIPPED_TWINS: unreal.log("LIGHTS %s: спящих близнецов AsdaLocalLightActor пропущено %d" % (name, len(SKIPPED_TWINS)))
    fixes = None
    if MODE in ("fix", "dryfix"):
        before = _snapshot(records)
        fixes = apply_fix(records, comps, rules, dry=(MODE == "dryfix"))
        if MODE == "fix" and fixes:
            after_records, _ = collect_lights(eas)
            ok, bad = gate_changes(before, _snapshot(after_records))
            if bad:
                unreal.log_error("LIGHTS %s: ГЕЙТ — запрещённые изменения (%d), уровень НЕ сохраняю: %s" % (name, len(bad), "; ".join(bad[:8])))
                fixes.append("НЕ СОХРАНЕНО: гейт отклонил %d изменений" % len(bad))
            else:
                bk = backup_umap(lvl)
                try:
                    from asda_convert_map_lit import _level_writable      # снять read-only Perforce тем же путём, что и map_lit
                    _level_writable(lvl)
                except Exception as e: unreal.log_warning("LIGHTS %s: _level_writable: %s" % (name, e))
                les.save_current_level()
                fixes.append("сохранено; бэкап %s (%d б); разрешённых изменений %d" % (bk, os.path.getsize(bk), len(ok)))
                records, comps = collect_lights(eas); records, summary = audit_records(records, rules)   # пере-аудит после починки
    os.makedirs(OUT_DIR, exist_ok=True)
    with open(os.path.join(OUT_DIR, "lights_%s.json" % name), "w", encoding="utf-8") as fh:
        json.dump({"map": name, "level": lvl, "mode": MODE, "date": datetime.date.today().isoformat(),
                   "summary": summary, "fixes": fixes, "lights": records}, fh, ensure_ascii=False, indent=1)
    with open(os.path.join(OUT_DIR, "lights_%s.md" % name), "w", encoding="utf-8") as fh:
        fh.write(render_md(name, records, summary, MODE, fixes))
    c = summary["counts"]
    unreal.log("LIGHTS %s: светов %d, с тенями %d (по уровням %s), ОШИБКА %d / ПРЕДУПРЕЖДЕНИЕ %d / ЗАМЕТКА %d%s" % (
        name, summary["lights"], summary["shadow_authored"], "/".join(map(str, summary["kept_by_level"])),
        c["ОШИБКА"], c["ПРЕДУПРЕЖДЕНИЕ"], c["ЗАМЕТКА"], (" ; починка %d" % len(fixes)) if fixes is not None else ""))
    return summary


def _script_main():
    """Работа скрипта под UE (вынесена, чтобы импорт ничего не делал)."""
    import unreal
    rules = load_presets()
    if MODE not in ("audit", "fix", "dryfix"):
        unreal.log_error("LIGHTS: ASDA_LIGHT_MODE=%r неизвестен (audit|dryfix|fix)" % MODE); unreal.log(MARKER); return
    rows = []
    for name, lvl in level_paths():
        try:
            s = audit_level(name, lvl, rules)
            if s: rows.append((name, s))
        except Exception as e:
            unreal.log_error("LIGHTS %s: %s" % (name, e))
    if len(rows) > 1:
        os.makedirs(OUT_DIR, exist_ok=True)
        md = ["# Аудит локальных светов — сводка (%s, режим %s)" % (datetime.date.today().isoformat(), MODE), "",
              "| Карта | Светов | С тенями | По уровням | VSM Epic | ОШИБКА | ПРЕДУПР. | ЗАМЕТКА |", "|---|---|---|---|---|---|---|---|"]
        for name, s in rows:
            md.append("| %s | %d | %d | %s | %d | %d | %d | %d |" % (name, s["lights"], s["shadow_authored"], "/".join(map(str, s["kept_by_level"])),
                                                                   s["vsm_maps_epic"], s["counts"]["ОШИБКА"], s["counts"]["ПРЕДУПРЕЖДЕНИЕ"], s["counts"]["ЗАМЕТКА"]))
        with open(os.path.join(OUT_DIR, "lights_SUMMARY.md"), "w", encoding="utf-8") as fh: fh.write("\n".join(md) + "\n")
    unreal.log(MARKER)


# ───────────────────────────────────────────── SelfTest (без UE) ─────────────────────────────────────────────

def _rec(label, **kw):
    """Синтетическая запись света с «здоровыми» дефолтами фонаря Main (переопределяются kw)."""
    r = {"label": label, "kind": "Point", "role": "Main", "tags": ["Main"], "location": [0, 0, 0], "mobility": "ComponentMobility.MOVABLE",
         "intensity": 15.0, "intensity_units": "LightUnits.CANDELAS", "use_temperature": True, "temperature": 2400,
         "attenuation_radius": 1000, "use_inverse_squared_falloff": True, "source_radius": 8, "cast_shadows": True,
         "use_ray_traced_distance_field_shadows": False, "max_draw_distance": 4000, "max_distance_fade_range": 1000,
         "cast_volumetric_shadow": False, "affect_global_illumination": True, "indirect_lighting_intensity": 1.0,
         "affects_world": True, "visible": True, "hidden_in_game": False, "lighting_channels": {"channel0": True, "channel1": False, "channel2": False}}
    r.update(kw); return r


def selftest():
    """Самопроверка правил на синтетике: коды вердиктов, пространственные проверки, гейт, пересчёт единиц, рендер MD. Печатает SELFTEST OK."""
    rules = load_presets()
    assert rules["shadow_budget_by_level"] == [0, 2, 4, 8] and "StreetLantern" in rules["presets"], "presets.json: бюджет/пресеты"
    codes = lambda r: {c for _s, c, _t in verdicts_for(r, rules)}
    assert codes(_rec("ok")) == set(), "здоровый Main-фонарь даёт вердикты: %s" % codes(_rec("ok"))
    assert codes(_rec("static", mobility="ComponentMobility.STATIC")) == {"E01"}
    # ★ E-4 первый прогон 07.09: синтетика Fill обязана быть в пороге Torch ×3 (15 кд > 6.6 давало лишний N22 — ошибка теста, не правила)
    assert codes(_rec("fill_sh", tags=["Fill"], role="Fill", intensity=2.0, max_draw_distance=0, max_distance_fade_range=0)) == {"E02", "W04"}
    assert codes(_rec("nosh_nodraw", cast_shadows=False, max_draw_distance=0, max_distance_fade_range=0)) == {"W03"}
    assert codes(_rec("rect", kind="Rect", source_radius=None)) == {"W10"}
    assert codes(_rec("unitless", intensity=5000, intensity_units="LightUnits.UNITLESS", tags=[], role=None, cast_shadows=False)) == {"N23", "N24"}   # ★ E-4: без теней, иначе законный W04
    assert codes(_rec("hot", intensity=50.0)) == {"N22"}                       # 50 кд > 15 ×3
    assert codes(_rec("df_vol", use_ray_traced_distance_field_shadows=True, cast_volumetric_shadow=True)) == {"W06", "W07"}
    assert codes(_rec("wide_hard", attenuation_radius=2000, source_radius=0, use_inverse_squared_falloff=False)) == {"W08", "N20", "N21"}
    assert codes(_rec("dead_pop", affects_world=False, max_distance_fade_range=0, light_function_material="/Game/M_LF", affect_global_illumination=False)) == {"N26", "N27", "N25", "N30"}
    assert codes(_rec("chan", lighting_channels={"channel0": False, "channel1": True, "channel2": False})) == {"N28"}
    # пересчёт единиц
    assert abs(to_candela(625, "UNITLESS", "Point") - 1.0) < 1e-9 and abs(to_candela(4 * math.pi, "LUMENS", "Point") - 1.0) < 1e-9
    assert abs(to_candela(2 * math.pi * (1 - math.cos(math.radians(45))), "LUMENS", "Spot", 45) - 1.0) < 1e-9
    assert to_candela(100, "NITS", "Rect") is None
    # пространственные: 9 теневых в 3000 → W05 у каждого; 5 теневых радиусов 1000 над одной точкой → W09
    cl = [_rec("c%d" % i, location=[i * 100, 0, 0]) for i in range(9)]
    sp = spatial_checks(cl, rules)
    assert all({c for _s, c, _t in sp[r["label"]]} >= {"W05", "W09"} for r in cl), sp
    far = [_rec("f%d" % i, location=[i * 2500, 0, 0]) for i in range(4)]
    assert spatial_checks(far, rules) == {}, "4 разнесённых фонаря не должны давать W05/W09"
    # аудит целиком + сводка + MD
    recs, summ = audit_records([_rec("a"), _rec("b", cast_shadows=False, tags=["Fill"], role="Fill", max_draw_distance=3000, max_distance_fade_range=500),
                                _rec("s", kind="Spot", outer_cone_angle=45)], rules)
    assert summ["shadow_authored"] == 2 and summ["kept_by_level"] == [0, 2, 2, 2] and summ["vsm_maps_epic"] == 7, summ
    md = render_md("selftest", recs, summ, "audit", fixes=[])
    assert "| a |" in md and "ОШИБКА 0" in md
    # гейт: разрешённое и запрещённое изменение
    b = _snapshot([_rec("g")]); a = _snapshot([_rec("g", max_draw_distance=4000.0, intensity=99.0)])
    ok, bad = gate_changes(b, a)
    assert ok == [] and len(bad) == 1 and "intensity" in bad[0], (ok, bad)
    b = _snapshot([_rec("h", mobility="ComponentMobility.STATIC")]); a = _snapshot([_rec("h")])
    ok, bad = gate_changes(b, a)
    assert len(ok) == 1 and bad == []
    print("SELFTEST OK: правила E01/E02/W03-W10/N20-N30, единицы, пространственные, гейт, MD")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает. `__spec__` выставляет любая машинерия импорта
#   и НЕ выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
if __name__ == "__main__" or globals().get("__spec__") is None:
    if os.environ.get("ASDA_LIGHT_SELFTEST", "") == "1" or "--selftest" in sys.argv[1:]:
        selftest()
    else:
        _script_main()
