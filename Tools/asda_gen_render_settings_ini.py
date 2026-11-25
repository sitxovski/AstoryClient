# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_gen_render_settings_ini.py — ★ Сет E, фаза E-4 (07.09.2026): ГЕНЕРАЦИЯ секции [/Script/AsdaReborn.AsdaRenderSettings]
#   в Config/DefaultGame.ini из Tools/asda_light_presets.json (единственный источник чисел пресетов локальных светов) и СВЕРКА
#   Config/DefaultScalability.ini [AsdaShadow@N] Light.ShadowBudget с shadow_budget_by_level того же JSON.
#   Обычный python, без UE (закон tools-no-import-side-effects: при импорте ничего не делает).
#
#   python Tools/asda_gen_render_settings_ini.py            — переписать секцию (бэкап DefaultGame.ini → @@ASTORY_DEV_ROOT@@\_backup\<дата>_rendersettings\AstoryClient\Config\)
#   python Tools/asda_gen_render_settings_ini.py --check    — только сверить (код 0 = совпадает, 3 = расхождение) — гейт после правок в редакторе:
#                                                            Project Settings → Asda → Render пишет в тот же DefaultGame.ini; расхождение =
#                                                            владелец поправил в редакторе → перенести число в JSON (или наоборот) руками.
#   python Tools/asda_gen_render_settings_ini.py --print    — напечатать секцию, файлы не трогать.
#   ★ Волна 3 п.16 (15.09.2026): --check сверяет ещё и СТРУКТУРУ таблиц уровней без генератора — имена ключей [AsdaShadow@N],
#     [AsdaQuality@N] (Normals.<параметр GNormalParams>) и полноту [AsdaGlow] (Intensity/Threshold@0..4 и, с 12.10.2026,
#     шесть констант реверса свечения — имена из kKeys AsdaVideoSettingsSubsystem.cpp); см. check_level_tables().
#
#   Формат TMap<enum, struct> в ini: LightPresets=((StreetLantern, (Title="…",LightClass=Point,…)),(Torch, (…)))
#   (FMapProperty::ImportText: пары «(Ключ, Значение)»; enum-ключи коротким именем; FColor=(B=,G=,R=,A=); soft-ptr None или путь).
#   Соответствие полей JSON → FAsdaLightPresetSpec (World/AsdaRenderSettings.h) — таблица FIELDS ниже; поле без ключа в JSON берёт дефолт.
import os, sys, json, re, shutil, datetime

# ★ Волна 3 п.16: консоль Windows бывает в cp1251 — без этого вывод --check приходил кракозябрами (как в asda_gen_fx_light_ini.py)
def _utf8_console():
    """UTF-8 для консоли. ★ Ревизия 15.09: вызывается из main(), а не при импорте ([[tools-no-import-side-effects]])."""
    for _s in (sys.stdout, sys.stderr):
        try: _s.reconfigure(encoding="utf-8", errors="replace")
        except Exception: pass

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(TOOLS_DIR)
PRESETS_PATH = os.environ.get("ASDA_LIGHT_PRESETS", os.path.join(TOOLS_DIR, "asda_light_presets.json"))
GAME_INI = os.path.join(PROJ, "Config", "DefaultGame.ini")
SCAL_INI = os.path.join(PROJ, "Config", "DefaultScalability.ini")
SECTION = "[/Script/AsdaReborn.AsdaRenderSettings]"
MARK = "; >>> сгенерировано Tools/asda_gen_render_settings_ini.py из Tools/asda_light_presets.json — НЕ ПРАВИТЬ РУКАМИ (правка: JSON → скрипт; редактор → --check)"

# (ключ JSON, поле C++, тип) — тип: s строка, f float, b bool, e enum(короткое имя), c FColor из [r,g,b], p soft-ptr путь|None
FIELDS = [
    ("title", "Title", "s"), ("light_class", "LightClass", "e_class"), ("default_role", "DefaultRole", "e"),
    ("intensity_rel_sun", "IntensityRelSun", "f"), ("d_ref_m", "DRefM", "f"), ("intensity_cd_alpen", "IntensityCdAlpen", "f"),
    ("use_temperature", "bUseTemperature", "b"), ("temperature_k", "TemperatureK", "f"), ("light_color_srgb", "LightColorSRGB", "c"),
    ("attenuation_radius", "AttenuationRadius", "f"), ("use_inverse_squared_falloff", "bUseInverseSquaredFalloff", "b"),
    ("source_radius", "SourceRadius", "f"), ("soft_source_radius", "SoftSourceRadius", "f"), ("source_length", "SourceLength", "f"),
    ("inner_cone_angle", "InnerConeAngle", "f"), ("outer_cone_angle", "OuterConeAngle", "f"),
    ("cast_shadows_if_role_allows", "bShadowsIfRoleAllows", "b"),
    ("max_draw_distance", "MaxDrawDistance", "f"), ("max_distance_fade_range", "MaxDistanceFadeRange", "f"),
    ("affect_global_illumination", "bAffectGlobalIllumination", "b"), ("indirect_lighting_intensity", "IndirectLightingIntensity", "f"),
    ("volumetric_scattering_intensity", "VolumetricScatteringIntensity", "f"), ("cast_volumetric_shadow", "bCastVolumetricShadow", "b"),
    ("use_ray_traced_distance_field_shadows", "bUseRayTracedDistanceFieldShadows", "b"),
    ("ies", "IES", "p"),
    ("emissive.material_path", "EmissiveMaterial", "p"), ("emissive.emissive_multiplier", "EmissiveMultiplier", "f"),
    ("emissive.mesh_flag_emissive_light_source", "bMeshEmissiveLightSource", "b"),
]
# ★ 14.09.2026 (реестр настроек D7): метка чтения секции — дата введения; менять только вместе с намеренной проверкой лога
CONFIG_STAMP = 20260914
PRESET_ORDER = ["StreetLantern", "Torch", "Candle", "CrystalGlow", "Showcase"]   # = EAsdaLightPreset (Custom без записи)


def _get(d, dotted):
    cur = d
    for k in dotted.split("."):
        if not isinstance(cur, dict) or k not in cur: return None
        cur = cur[k]
    return cur


def _ini_str(s):
    return '"' + str(s).replace('"', '\\"') + '"'


def _fmt(v, t):
    if t == "s": return _ini_str(v)
    if t == "f": return "%.6f" % float(v)
    if t == "b": return "True" if bool(v) else "False"
    if t == "e": return str(v)
    if t == "e_class": return {"PointLight": "Point", "SpotLight": "Spot"}.get(str(v), str(v))
    if t == "c":
        r, g, b = [int(x) for x in v][:3]
        return "(B=%d,G=%d,R=%d,A=255)" % (b, g, r)
    if t == "p": return "None" if not v else _ini_str(v)
    raise ValueError(t)


def preset_struct(p):
    parts = []
    for jk, cf, t in FIELDS:
        v = _get(p, jk)
        if v is None: continue                       # поле без ключа → дефолт C++
        parts.append("%s=%s" % (cf, _fmt(v, t)))
    return "(" + ",".join(parts) + ")"


def build_section(rules):
    """Текст секции (без заголовка) по JSON."""
    lines = [MARK]
    # ★ 14.09.2026: метка чтения секции (UAsdaRenderSettings::ConfigStamp; 0 в C++ = секция не прочитана → Warning в лог)
    lines.append("ConfigStamp=%d" % CONFIG_STAMP)
    pairs = ["(%s, %s)" % (k, preset_struct(rules["presets"][k])) for k in PRESET_ORDER if k in rules["presets"]]
    lines.append("LightPresets=(" + ",".join(pairs) + ")")
    lines.append("SunLuxAlpen=%.6f" % float(rules.get("sun_lux_alpen", 1.59)))
    fx = rules.get("fix_defaults", {})
    lines.append("DefaultMaxDrawDistance=%.6f" % float(fx.get("max_draw_distance", 4000)))
    lines.append("DefaultMaxDistanceFadeRange=%.6f" % float(fx.get("max_distance_fade_range", 1000)))
    lines.append("ShadowRebalanceIntervalSec=%.6f" % float(rules.get("budget_rebalance_interval_sec", 2.0)))
    lines.append("ShadowRebalanceMoveThreshold=%.6f" % float(rules.get("budget_rebalance_move_threshold", 300)))
    lines.append("bApplyBudgetInPIE=%s" % ("True" if rules.get("budget_apply_in_pie", True) else "False"))
    lines.append("ShadowBudgetFallback=%d" % int(rules["shadow_budget_by_level"][-1]))
    # ★ Ф6: fallback второй полосы (тени эффект-светов) — из того же JSON, иначе ini и код разойдутся
    if "fx_shadow_budget_by_level" in rules:
        lines.append("FxShadowBudgetFallback=%d" % int(rules["fx_shadow_budget_by_level"][-1]))
    return "\n".join(lines) + "\n"


def split_section(text):
    """(до, тело секции|None, после) по SECTION в тексте ini. Тело — общий `asda_gen_fx_light_ini.split_ini_section`
    (дедуп №2 В1 Т19: была дословная копия; хвост из пустых строк и «;»-комментариев уходит в «после» — волна Ф6)."""
    if TOOLS_DIR not in sys.path:
        sys.path.insert(0, TOOLS_DIR)
    import asda_gen_fx_light_ini as _FXL   # ленивый импорт: модуль без побочных эффектов при импорте
    return _FXL.split_ini_section(text, SECTION)


def check_scalability(rules):
    """[AsdaShadow@N] Light.ShadowBudget в DefaultScalability.ini против shadow_budget_by_level (+Cine = последний)."""
    want = list(rules["shadow_budget_by_level"]) + [rules["shadow_budget_by_level"][-1]]
    names = ["0", "1", "2", "3", "Cine"]
    txt = open(SCAL_INI, encoding="utf-8", errors="replace").read()
    bad = []
    for n, w in zip(names, want):
        m = re.search(r"^\[AsdaShadow@%s\][^\[]*?^Light\.ShadowBudget=(-?\d+)" % re.escape(n), txt, re.M | re.S)
        got = int(m.group(1)) if m else None
        if got != w: bad.append("[AsdaShadow@%s] Light.ShadowBudget: ini=%s json=%s" % (n, got, w))
    # ★ ВОЛНА Ф6 (10.09.2026): вторая полоса той же секции — тени ЭФФЕКТ-светов. Проверяется тем же гейтом,
    #   иначе у неё немедленно завёлся бы второй источник истины (ini против JSON), как было до сета E-4.
    for key, jkey, cast in (("Light.FxShadowBudget", "fx_shadow_budget_by_level", int),
                            ("Light.FxShadowResScale", "fx_shadow_res_scale_by_level", float)):
        if jkey not in rules: continue
        wl = list(rules[jkey]) + [rules[jkey][-1]]
        for n, w in zip(names, wl):
            m = re.search(r"^\[AsdaShadow@%s\][^\[]*?^%s=(-?[\d.]+)" % (re.escape(n), re.escape(key)), txt, re.M | re.S)
            got = cast(m.group(1)) if m else None
            if got != cast(w): bad.append("[AsdaShadow@%s] %s: ini=%s json=%s" % (n, key, got, w))
    return bad


def read_ini_sections(path):
    """★ Волна 3 п.16: {секция: [(ключ, значение, № строки)]} — «;»-строки пропускаются, хвост «; …» у значения
    отрезается (так же режет UAsdaRenderQualitySubsystemBase::ScalabilityLevelValue). Порядок и повторы ключей сохраняются."""
    out, cur = {}, None
    for i, line in enumerate(open(path, encoding="utf-8-sig", errors="replace").read().splitlines(), 1):
        s = line.strip()
        if not s or s.startswith(";"):
            continue
        if s.startswith("[") and s.endswith("]"):
            cur = s[1:-1]
            out.setdefault(cur, [])
            continue
        if cur is None or "=" not in s:
            continue
        k, v = s.split("=", 1)
        out[cur].append((k.strip(), v.split(";", 1)[0].strip(), i))
    return out


def _is_num(v):
    try:
        float(v)
        return True
    except ValueError:
        return False


# ★★★ ВОЛНА 3 п.16 (15.09.2026): СВЕРКА ТАБЛИЦ УРОВНЕЙ БЕЗ ГЕНЕРАТОРА.
#   [AsdaShadow@N] — числа из asda_light_presets.json (check_scalability выше); здесь — только ИМЕНА ключей: опечатка в имени
#     не ловится сверкой чисел и молча уводит рантайм на fallback C++.
#   [AsdaQuality@N] и [AsdaGlow] — источник САМ ini (решение владельца: [AsdaGlow] читают C++ SetGlowLevel и
#     Tools/asda_bloom_rollout.py; [AsdaQuality@N] — UAsdaDevMenuSubsystem::ApplyNormals). Второй копии чисел не заводим,
#     сверяем полноту и имена: [AsdaQuality@0..3,Cine] есть, ключи — Normals.<параметр GNormalParams>, значения — числа;
#     [AsdaGlow] — ровно по одной строке Intensity@0..4 и Threshold@0..4, числа, других ключей нет.
#     ★ 12.10.2026: + шесть констант реверса «что попадает в буфер» (19.09.2026: UnitOffset, UnitBlurFactor, UnitComposeFactor,
#     BlurBufSize, BlurBufSizeWide, BlurBufWideFromWidth) — тоже ровно по одной строке и числом. Их имена берутся из таблицы
#     kKeys в UAsdaVideoSettingsSubsystem (AsdaVideoSettingsSubsystem.cpp, читатель C++), а не дублируются здесь — как
#     GNormalParams для [AsdaQuality@N]. Без этого --check давал «неизвестный ключ» ×6 и код 3 при верном ini.
LEVEL_SECTIONS = ["0", "1", "2", "3", "Cine"]
SHADOW_LEVEL_KEYS = ("Light.ShadowBudget", "Light.FxShadowBudget", "Light.FxShadowResScale")   # читатели: UAsdaLightBudgetSubsystem, UAsdaFxLightComponent
DEVMENU_CPP = os.path.join(PROJ, "Source", "AsdaReborn", "Private", "UI", "AsdaDevMenuSubsystem.cpp")
VIDEO_CPP = os.path.join(PROJ, "Source", "AsdaReborn", "Private", "Resources", "AsdaVideoSettingsSubsystem.cpp")   # читатель [AsdaGlow]


def normal_params():
    """Имена параметров MPC_AsdaNormals из GNormalParams (AsdaDevMenuSubsystem.cpp) или None, если таблица не найдена."""
    m = re.search(r"GNormalParams\[\]\s*=\s*\{(.*?)\};", open(DEVMENU_CPP, encoding="utf-8-sig", errors="replace").read(), re.S)
    return re.findall(r'FName\(TEXT\("(\w+)"\)\)', m.group(1)) if m else None


def glow_unit_keys():
    """Имена констант реверса свечения из таблицы kKeys, которую C++ читает из [AsdaGlow] (AsdaVideoSettingsSubsystem.cpp),
    или None, если таблица не найдена (тогда --check сообщает об этом, а не пропускает молча)."""
    src = open(VIDEO_CPP, encoding="utf-8-sig", errors="replace").read()
    m = re.search(r"kKeys\[\d+\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m or 'TEXT("AsdaGlow"), kKeys[' not in src:
        return None
    return re.findall(r'TEXT\("(\w+)"\)', m.group(1))


def check_level_tables():
    """Расхождения структуры [AsdaShadow@N] / [AsdaQuality@N] / [AsdaGlow] в DefaultScalability.ini (пусто = всё сходится)."""
    bad = []
    sec = read_ini_sections(SCAL_INI)
    for n in LEVEL_SECTIONS:
        name = "AsdaShadow@" + n
        for k, v, ln in sec.get(name, []):
            if k not in SHADOW_LEVEL_KEYS:
                bad.append("DefaultScalability.ini:%d [%s] %s — неизвестный ключ (читаются %s)" % (ln, name, k, ", ".join(SHADOW_LEVEL_KEYS)))
    params = normal_params()
    if params is None:
        bad.append("AsdaDevMenuSubsystem.cpp: таблица GNormalParams не найдена — имена ключей [AsdaQuality@N] не проверить")
    for n in LEVEL_SECTIONS:
        name = "AsdaQuality@" + n
        if name not in sec:
            bad.append("[%s]: секции нет" % name)
            continue
        seen = set()
        for k, v, ln in sec[name]:
            p = k[len("Normals."):] if k.startswith("Normals.") else None
            if p is None or (params is not None and p not in params):
                bad.append("DefaultScalability.ini:%d [%s] %s — ключ не Normals.<%s>" % (ln, name, k, "|".join(params or [])))
            if not _is_num(v):
                bad.append("DefaultScalability.ini:%d [%s] %s=%r — не число" % (ln, name, k, v))
            if k in seen:
                bad.append("DefaultScalability.ini:%d [%s] %s — ключ повторяется" % (ln, name, k))
            seen.add(k)
    glow = sec.get("AsdaGlow")
    if glow is None:
        bad.append("[AsdaGlow]: секции нет")
    else:
        want = ["%s@%d" % (kind, lvl) for lvl in range(5) for kind in ("Intensity", "Threshold")]
        unit = glow_unit_keys()
        if unit is None:
            bad.append("AsdaVideoSettingsSubsystem.cpp: таблица kKeys констант [AsdaGlow] не найдена — их имена не проверить")
        else:
            want += unit
        for w in want:
            hits = [(v, ln) for k, v, ln in glow if k == w]
            if len(hits) != 1:
                bad.append("[AsdaGlow] %s: строк %d (нужна ровно одна)" % (w, len(hits)))
            elif not _is_num(hits[0][0]):
                bad.append("DefaultScalability.ini:%d [AsdaGlow] %s=%r — не число" % (hits[0][1], w, hits[0][0]))
        for k, v, ln in glow:
            if k not in want:
                bad.append("DefaultScalability.ini:%d [AsdaGlow] %s — неизвестный ключ (читаются Intensity@0..4, Threshold@0..4, %s)"
                           % (ln, k, ", ".join(unit or [])))
    return bad


def main(argv):
    _utf8_console()
    rules = json.load(open(PRESETS_PATH, encoding="utf-8"))
    body = build_section(rules)
    if "--print" in argv:
        print(SECTION); print(body); return 0
    # ★ 14.09.2026: читаем байты и запоминаем перевод строк файла (проект — CRLF), как asda_gen_fx_light_ini.render_game_ini;
    #   прежнее чтение в текстовом режиме + запись с newline=LF переводило весь DefaultGame.ini на LF.
    raw_ini = open(GAME_INI, "rb").read()
    ini_nl = "\r\n" if b"\r\n" in raw_ini else "\n"
    text = raw_ini.decode("utf-8-sig").replace("\r\n", "\n").replace("\r", "\n")
    before, cur, after = split_section(text)
    bad = check_scalability(rules)
    if "--check" in argv:
        same = (cur is not None) and (cur.strip().replace("\r", "") == body.strip())
        print("DefaultGame.ini %s: %s" % (SECTION, "совпадает с JSON" if same else ("СЕКЦИИ НЕТ" if cur is None else "РАСХОЖДЕНИЕ с JSON")))
        if cur is not None and not same:
            a = [l for l in cur.strip().replace("\r", "").splitlines() if not l.startswith(";")]
            b = [l for l in body.strip().splitlines() if not l.startswith(";")]
            for l in a:
                if l not in b: print("  ini : " + l[:200])
            for l in b:
                if l not in a: print("  json: " + l[:200])
        for l in bad: print("  " + l)
        print("DefaultScalability.ini [AsdaShadow@N]: %s" % ("совпадает" if not bad else "РАСХОЖДЕНИЕ"))
        lbad = check_level_tables()   # ★ волна 3 п.16
        for l in lbad: print("  " + l)
        print("DefaultScalability.ini ключи [AsdaShadow@N] / [AsdaQuality@N] / [AsdaGlow] (источник — сам ini): %s" % ("совпадает" if not lbad else "РАСХОЖДЕНИЕ"))
        return 0 if (same and not bad and not lbad) else 3
    # запись: бэкап → замена/добавление секции (BOM сохраняем — DefaultGame.ini проекта с BOM)
    # ★ Т3 (11.10.2026): бэкап — asda_headless_tool.backup_file, набор @@ASTORY_DEV_ROOT@@\_backup\<дата>_rendersettings\<путь от
    #   @@ASTORY_DEV_ROOT@@> (был AstoryClient\_backup\<дата>_rendersettings\Config\, и повторная запись в тот же день ПЕРЕЗАПИСЫВАЛА
    #   копию уже изменённым ini; теперь первая копия дня — исходник до правки — не трогается). Сверка размера.
    if TOOLS_DIR not in sys.path:
        sys.path.insert(0, TOOLS_DIR)
    import asda_headless_tool as HT
    bpath = HT.backup_file(GAME_INI, "rendersettings", log=lambda _m: None)
    assert bpath and os.path.getsize(bpath) > 0, "бэкап пустой"
    if cur is None:
        new = text.rstrip("\r\n") + "\n\n" + SECTION + "\n" + body
    else:
        new = before + SECTION + "\n" + body + ("\n" if after and not after.startswith("\n") else "") + after
    with open(GAME_INI, "wb") as fh:   # BOM всегда (как прежняя запись utf-8-sig), перевод строк — исходный файла
        fh.write(b"\xef\xbb\xbf" + new.replace("\n", ini_nl).encode("utf-8"))
    print("записано %s (%d пресетов); бэкап %s" % (GAME_INI, len(rules["presets"]), bpath))
    for l in bad: print("  ВНИМАНИЕ: " + l)
    lbad = check_level_tables()   # ★ волна 3 п.16: запись секции от сверки не зависит, расхождение называется
    for l in lbad: print("  ВНИМАНИЕ: " + l)
    return 0 if not (bad or lbad) else 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
