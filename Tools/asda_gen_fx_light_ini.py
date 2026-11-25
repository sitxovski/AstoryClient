# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_gen_fx_light_ini.py — ★ ВОЛНА Ф2-ВСЕ (09.09.2026): ГЕНЕРАЦИЯ секции [/Script/AsdaReborn.AsdaFxLightSettings]
#   в Config/DefaultGame.ini из Tools/asda_fx_light.json (единственный источник чисел таблицы семейств)
#   и СВЕРКА Config/DefaultScalability.ini [AsdaFxLight@N] с бюджетами того же JSON.
#   Обычный python, без UE (закон tools-no-import-side-effects: при импорте ничего не делает).
#
#   python Tools/asda_gen_fx_light_ini.py            — переписать секцию (бэкап DefaultGame.ini → _backup/…)
#   python Tools/asda_gen_fx_light_ini.py --check    — только сверить (код 0 = совпадает, 3 = расхождение);
#                                                    ★ волна 3 п.16: заодно базы оружия weapon_glow (таблица ↔ JSON ↔ C++)
#   python Tools/asda_gen_fx_light_ini.py --print    — напечатать секцию, файлы не трогать
#   python Tools/asda_gen_fx_light_ini.py --specs    — напечатать разрешённые числа каждого семейства (канделы и т.д.)
#
#   ★ ЕДИНАЯ ТОЧКА РАЗРЕШЕНИЯ ЧИСЕЛ — family_spec() ниже. Её же импортирует Tools/asda_apply_fx_light_defaults.py,
#     который проставляет те же самые числа в поля «Свет эффекта» 1274 ассетов BP_Fx_* и эффект-акторов карт.
#     Так ini и ассеты не могут разойтись по построению (закон port-shared-points «один источник»).
#
#   Формат TArray<struct> в ini: ОДНА строка «+Families=(Id="…",Jeb="…",Spec=(…))» НА КАЖДОЕ правило.
import os, sys, json, re, shutil

# ★ Консоль Windows бывает в cp1251 и роняет print на «→»/«★»
def _utf8_console():
    """UTF-8 для консоли. ★ Ревизия 15.09: вызывается из main(), а не при импорте ([[tools-no-import-side-effects]])."""
    for _s in (sys.stdout, sys.stderr):
        try: _s.reconfigure(encoding="utf-8", errors="replace")
        except Exception: pass

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(TOOLS_DIR)
RULES_PATH = os.environ.get("ASDA_FX_LIGHT_JSON", os.path.join(TOOLS_DIR, "asda_fx_light.json"))
GAME_INI = os.path.join(PROJ, "Config", "DefaultGame.ini")
SCAL_INI = os.path.join(PROJ, "Config", "DefaultScalability.ini")
SECTION = "[/Script/AsdaReborn.AsdaFxLightSettings]"
# ★ 14.09.2026 (реестр настроек D7): метка чтения секции — дата введения; менять только вместе с намеренной проверкой лога
CONFIG_STAMP = 20260914
MARK = "; >>> сгенерировано Tools/asda_gen_fx_light_ini.py из Tools/asda_fx_light.json — НЕ ПРАВИТЬ РУКАМИ (правка: JSON → скрипт; редактор → --check)"

# Поля FAsdaFxLightSpec (World/AsdaFxLightSpec.h): (ключ JSON, поле C++, тип).
# Типы: s строка, f float, b bool, i int, e_* enum (короткое имя), c FColor из [r,g,b], v FVector из [x,y,z].
SPEC_FIELDS = [
    # ★★★ fix4 (20.09.2026): ЗАМОК РУЧНОЙ ПРАВКИ СТОИТ В ТАБЛИЦЕ, А НЕ ВПИСАН В СТРОКУ РУКАМИ.
    #   `bManual` означает «поля этого АССЕТА старше правила семейства» (порядок разрешения:
    #   эмиттер → предмет → эффект → ЗАМОК → семейство → поля ассета). В ini он писался и раньше,
    #   но жёстко вписанной строкой МИМО SPEC_FIELDS — а конфигуратор свечения строит свой список
    #   полей из ЭТОЙ таблицы, поэтому владелец замок не видел, и на вопрос «почему моя правка
    #   семейства не доехала» отвечал только дамп. Первым элементом — чтобы порядок полей в
    #   сгенерированном ini не изменился ни на байт (он и был: FamilyId, bManual, bEnabled, …).
    #   ★ У СЕМЕЙСТВА замок всегда False: его взводит только правка владельца в ассете
    #     (family_spec ставит его явно); ключа "manual" в JSON нет и заводить его там незачем.
    ("manual", "bManual", "b"),
    ("enabled", "bEnabled", "b"),
    ("intensity_cd", "IntensityCd", "f"),
    ("radius_mode", "RadiusMode", "e_radius"), ("radius_scale", "RadiusScale", "f"),
    # ★ Ф4: у режима «габариты всего эффекта» СВОЙ множитель — общий с донорским расколибровал бы один из двух
    ("radius_bounds_scale", "RadiusBoundsScale", "f"),
    ("radius_min", "RadiusMin", "f"), ("radius_max", "RadiusMax", "f"),
    # ★★★ ВТОРАЯ РЕВИЗИЯ ВОЛНЫ «форма-2» (21.09.2026, СРЕДН-1): УДЕРЖАНИЕ ГАБАРИТА — В ТАБЛИЦЕ.
    #   Волна завела поле `GeomHoldSec` в структуре, но НЕ завела его здесь, и последствия были
    #   ровно те, что у замка `bManual` до fix4: конфигуратор свечения строит список полей из ЭТОЙ
    #   таблицы (AsdaFxGlowConfigModel.cpp), поэтому поля не было ни в окне конфигуратора, ни в
    #   asda_fx_light.json (задать его СЕМЕЙСТВУ было нельзя), ни в --check. Владельцу оставались
    #   консольный ключ на весь мир либо правка в панели ассета — а она ВЗВОДИТ ЗАМОК и отрывает
    #   эффект от семейства по ВСЕМ числам сразу. ★ Случай «есть в структуре, нет здесь» движок не
    #   логирует вовсе (предупреждение только обратное), то есть пропажа была молчаливой.
    ("geom_hold_sec", "GeomHoldSec", "f"),
    ("color_mode", "ColorMode", "e_color"), ("color_srgb", "ColorSRGB", "c"),
    ("color_saturation", "ColorSaturation", "f"),
    ("flicker_hz", "FlickerHz", "f"), ("flicker_amplitude", "FlickerAmplitude", "f"),
    ("position", "Position", "e_pos"), ("offset", "Offset", "v"),
    ("alpha_power", "AlphaPower", "f"), ("priority", "Priority", "f"),
    ("max_draw_distance", "MaxDrawDistance", "f"), ("fade_range", "FadeRange", "f"),
    ("source_radius", "SourceRadius", "f"), ("soft_source_radius", "SoftSourceRadius", "f"),
    ("volumetric_scattering", "VolumetricScattering", "f"), ("affect_gi", "bAffectGI", "b"),
    # ★ ВОЛНА Ф6 (10.09.2026): тень эффект-света. Обоснование чисел — defaults.shadow_doc в JSON
    ("cast_shadow", "bCastShadow", "b"), ("shadow_res_scale", "ShadowResolutionScale", "f"),
    ("shadow_max_distance", "ShadowMaxDistance", "f"),
    ("shadow_bias", "ShadowBias", "f"), ("shadow_slope_bias", "ShadowSlopeBias", "f"),
    ("shadow_sharpen", "ShadowSharpen", "f"), ("contact_shadow_length", "ContactShadowLength", "f"),
    ("emitter", "Emitter", "i"),
    # ★ ВОЛНА Ф10 (11.09.2026): форма по геометрии эмиттера и охват «свой свет у каждого эмиттера».
    #   Пишутся ТОЛЬКО если заданы в семействе (в defaults их нет): прочие семейства получают C++-умолчания
    #   (точка, один свет на эффект) — то есть ровно прежнее поведение, принятое волнами Ф3…Ф9.
    ("shape", "Shape", "e_shape"), ("scope", "Scope", "e_scope"),
    ("tube_length_scale", "TubeLengthScale", "f"),
    ("ring_segments", "RingSegments", "i"), ("ring_radius_scale", "RingRadiusScale", "f"),
    ("auto_elongation", "AutoElongation", "f"), ("auto_flatness", "AutoFlatness", "f"),
    # ★ 13.09.2026 (конфигуратор свечения): СИЛА СВЕЧЕНИЯ самого эффекта (скаляр EmissiveBoost материала).
    #   Пишутся ТОЛЬКО если заданы в семействе/переопределении (в defaults их нет): прочие получают C++-умолчание
    #   «не переопределять» — эмиссия идёт из материала, назначенного таблицей Tools/asda_fx_boost.json, как раньше.
    ("override_emissive", "bOverrideEmissive", "b"), ("emissive_boost", "EmissiveBoost", "f"),
    # ═══ ★★★ ВОЛНА Ф11 (20.09.2026): ПОЛЯ РАЗМЕЩЕНИЯ ФОРМЫ ═══════════════════════════════════════════
    #   ПОЧЕМУ ОНИ ПОЯВИЛИСЬ ТОЛЬКО СЕЙЧАС И ПОЧЕМУ ЭТО БЫЛА ОШИБКА. До Ф11 в этом списке было 39 полей
    #   из 54, и НИ ОДНОГО поля размещения: само `shape` стояло, а обмерить и повернуть форму было нечем.
    #   Итог для владельца выглядел так: форму «прямоугольник» выбрать можно, а куда она смотрит и какого
    #   она размера — нельзя (ни через семейство в ini, ни в конфигураторе, который строит свой список
    #   ИЗ ЭТОЙ таблицы — Plugins/AsdaItemBrowser/.../AsdaFxGlowConfigModel.cpp:229-254). Отсюда дословная
    #   жалоба «не обладают достаточным количеством настроек размещения».
    #   ПРАВИЛО НА БУДУЩЕЕ: любое поле FAsdaFxLightSpec, которое владелец крутит руками, ОБЯЗАНО стоять
    #   здесь — иначе его нет ни в ini семейства, ни в переопределении, ни в конфигураторе.
    #   ★ Все они пишутся ТОЛЬКО если заданы в семействе или переопределении: в defaults их нет, поэтому
    #     прочие семейства получают C++-умолчания, то есть в точности прежнее поведение.
    # 13 полей формы, существовавших с волн Ф8/Ф10 и пропущенных в этом списке:
    ("rect_size_mode", "RectSizeMode", "e_size"), ("rect_width", "RectWidth", "f"),
    ("rect_height", "RectHeight", "f"), ("rect_size_scale", "RectSizeScale", "f"),
    ("rect_normal_mode", "RectNormalMode", "e_normal"), ("rect_rotation", "RectRotation", "r"),
    ("rect_two_sided", "bRectTwoSided", "b"),
    ("barn_door_angle", "BarnDoorAngle", "f"), ("barn_door_length", "BarnDoorLength", "f"),
    ("spot_dir_mode", "SpotDirMode", "e_spotdir"), ("spot_rotation", "SpotRotation", "r"),
    ("spot_outer_cone", "SpotOuterCone", "f"), ("spot_inner_cone", "SpotInnerCone", "f"),
    # Новые поля волны Ф11: привязка, смещение по осям формы, доворот, раскладка на источники
    ("shape_anchor", "ShapeAnchor", "e_anchor"), ("offset_local", "OffsetLocal", "v"),
    ("shape_rotation", "ShapeRotation", "r"),
    ("shape_segments", "ShapeSegments", "i"), ("shape_segment_radius_scale", "ShapeSegmentRadiusScale", "f"),
    # «Блин» (плоский диск)
    ("disc_size_mode", "DiscSizeMode", "e_size"), ("disc_radius", "DiscRadius", "f"),
    ("disc_size_scale", "DiscSizeScale", "f"), ("disc_normal_mode", "DiscNormalMode", "e_normal"),
    ("disc_strips", "DiscStrips", "i"), ("disc_strip_radius_scale", "DiscStripRadiusScale", "f"),
    # Ручные размеры трубки и кольца + «шар»
    ("tube_length_mode", "TubeLengthMode", "e_size"), ("tube_length", "TubeLength", "f"),
    ("ring_size_mode", "RingSizeMode", "e_size"), ("ring_radius_uu", "RingRadiusUU", "f"),
    ("sphere_size_mode", "SphereSizeMode", "e_size"), ("sphere_radius", "SphereRadius", "f"),
    ("sphere_size_scale", "SphereSizeScale", "f"),
    # ★ fix5 (КРИТ-А): покрытие формы плитками встык. Порядок полей в ini не меняется у прежних
    #   ключей — новый дописан В КОНЕЦ, как и bAutoFlatRoundAsDisc в своё время
    ("shape_cover_tiles", "ShapeCoverTiles", "i"),
    ("auto_flat_round_as_disc", "bAutoFlatRoundAsDisc", "b"),
]
# ★ 13.09.2026: ключи СИЛЫ СВЕЧЕНИЯ — у переопределений они не наследуются от семейства (см. override_spec)
EMISSIVE_KEYS = ("override_emissive", "emissive_boost")
# JSON-значение → короткое имя элемента UENUM
# ★ ВОЛНА Ф4 (10.09.2026): агрегирующие значения mean/median/bounds ДОПИСАНЫ, донорские node/section
#   ОСТАВЛЕНЫ — это откат к поведению Ф2 для поштучно настроенных эффектов владельца.
ENUM_MAPS = {
    "e_color":  {"node": "Node", "fixed": "Fixed", "mean": "Mean", "median": "Median"},
    "e_radius": {"section": "Section", "fixed": "Fixed", "bounds": "Bounds"},
    "e_pos":    {"section": "Section", "actor": "Actor", "bounds": "Bounds", "bounds_box": "BoundsBox"},
    # ★ Ф10: tube/ring/auto дописаны в КОНЕЦ перечисления C++ (числа в ассетах не сдвигаются)
    # ★ Ф11: disc/sphere дописаны в КОНЕЦ перечисления C++ (числа форм в 1274 ассетах не сдвигаются)
    "e_shape":  {"point": "Point", "rect": "Rect", "spot": "Spot", "tube": "Tube", "ring": "Ring", "auto": "Auto",
                 "disc": "Disc", "sphere": "Sphere"},
    "e_scope":  {"effect": "Effect", "per_emitter": "PerEmitter"},
    # ★ Ф11: перечисления РАЗМЕЩЕНИЯ формы. До Ф11 их не было вовсе — ни одно поле формы, кроме самой
    #   `shape`, в ini не попадало (см. довод в SPEC_FIELDS).
    "e_size":    {"section": "Section", "fixed": "Fixed"},
    "e_normal":  {"auto": "Auto", "auto_flip": "AutoFlip", "explicit": "Explicit"},
    "e_spotdir": {"world_down": "WorldDown", "actor_fwd": "ActorFwd", "section": "Section", "explicit": "Explicit"},
    "e_anchor":  {"light": "Light", "section": "Section", "centroid": "Centroid",
                  "bounds_box": "BoundsBox", "actor": "Actor"},
}


# ═══ ★★★ ВОЛНА 3 п.16 (15.09.2026): БАЗЫ ИМЕНИ СВЕЧЕНИЯ ЗАТОЧКИ — ОДНА ТАБЛИЦА НА PYTHON ═══════════════════
#   Эталон CCharacter_BuildWeaponJebFileName@0xA4CB10 (Reports/weapon_glow_re.md): база имени .jeb по itemSubType
#   оружия; у щита база фиксированная "shd" (itemBuildJebEffectName@0xA4E790). До волны 3 список баз был записан
#   ЧЕТЫРЕ раза без сверки: switch рантайма (UAsdaWorldSubsystem), switch плагина (FAsdaItemBrowserData),
#   WEAPON_BASE в asda_build_weapon_glow_fx.py и регэксп семейства weapon_glow (asda_fx_light.json, asda_fx_boost.json).
#   ТЕПЕРЬ: Python — эта таблица (её импортирует asda_build_weapon_glow_fx.py); C++ — AsdaWeaponGlow
#   (World/AsdaFxLightSettings.cpp, её зовёт рантайм). Регэкспы в JSON — ДАННЫЕ (их читают конфигуратор и раскат
#   буста), поэтому остаются текстом и сверяются здесь побуквенно. Браузер предметов (FAsdaItemBrowserData) с 15.09.2026
#   (хвосты волны 3) зовёт AsdaWeaponGlow — сверяется, что своей копии там нет. Всё это — в --check (код 3 при расхождении).
WEAPON_GLOW_BASE_BY_SUBTYPE = {0: "ohs", 1: "ohs", 2: "spr", 3: "ths", 5: "stf", 8: "bow", 9: "xbow", 10: "bow", 11: "bali"}
SHIELD_GLOW_BASE = "shd"                      # база щита (эталон: строка @0x13AEAC8)
# ★ Хвосты волны 3 (15.09.2026): код грейда (ItemRecord.itemGrade → часть имени; ассетов "le" нет). Был записан трижды:
#   switch C++ ComposeJeb, копия плагина (удалена), GRADE_CODE инструмента (теперь импортирует эту). Сверка с C++ — в --check.
WEAPON_GLOW_GRADE_CODE = {0: "co", 1: "uc", 2: "ra", 3: "he", 4: "le"}
WEAPON_GLOW_FAMILY_ID = "weapon_glow"         # id семейства в asda_fx_light.json и asda_fx_boost.json
BOOST_JSON = os.path.join(TOOLS_DIR, "asda_fx_boost.json")
CPP_GLOW_TABLE = os.path.join(PROJ, "Source", "AsdaReborn", "Private", "World", "AsdaFxLightSettings.cpp")
CPP_BROWSER_COPY = os.path.join(PROJ, "Plugins", "AsdaItemBrowser", "Source", "AsdaItemBrowser", "Private", "AsdaItemBrowserData.cpp")


def weapon_glow_bases():
    """Все базы имени свечения (оружие + щит), по алфавиту — порядок альтернатив регэкспа семейства."""
    return sorted(set(WEAPON_GLOW_BASE_BY_SUBTYPE.values()) | {SHIELD_GLOW_BASE})


def weapon_glow_jeb_regex():
    """Шаблон семейства weapon_glow, каким он обязан стоять в JSON (строится из таблицы, не хранится вторым списком)."""
    return "^(%s)_[a-z]_[a-z]{2}_ma_[0-9]+$" % "|".join(weapon_glow_bases())


def parse_cpp_glow_table(text):
    """({подтип: база} | None, база щита | None) из World/AsdaFxLightSettings.cpp (GAsdaWeaponGlowBases / GAsdaShieldGlowBase)."""
    m = re.search(r"GAsdaWeaponGlowBases\[\]\s*=\s*\{(.*?)\};", text, re.S)
    s = re.search(r'GAsdaShieldGlowBase\s*=\s*TEXT\("([a-z]+)"\)', text)
    table = None
    if m:
        # ★ Ревизия 15.09: в C++ при дубле подтипа выигрывает ПЕРВАЯ запись — здесь так же (раньше dict брал последнюю)
        table = {}
        for a, b in parse_cpp_glow_pairs(text):
            table.setdefault(a, b)
    return table, (s.group(1) if s else None)


def parse_cpp_glow_pairs(text):
    """[(подтип, база)] в порядке записи из GAsdaWeaponGlowBases (с дублями) или [] (таблицы нет)."""
    m = re.search(r"GAsdaWeaponGlowBases\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        return []
    return [(int(a), b) for a, b in re.findall(r'\{\s*(\d+)\s*,\s*TEXT\("([a-z]+)"\)\s*\}', m.group(1))]


def parse_cpp_switch(text, func_name):
    """{case: строка} из switch функции func_name вида «case N: case M: return TEXT("x");» или None (функции нет)."""
    # ★ Ревизия 15.09: допускаются `const`/`override` после скобок (перенос функции в класс не должен молча выключать сверку)
    m = re.search(r"%s\s*\(\s*int32\s+\w+\s*\)\s*(?:const\s*)?(?:override\s*)?\{(.*?)\n\s*\}" % re.escape(func_name), text, re.S)
    if not m:
        return None
    out, pend = {}, []
    for tok in re.finditer(r'case\s+(\d+)\s*:|return\s+TEXT\("([a-z]+)"\)|default\s*:', m.group(1)):
        if tok.group(1) is not None:
            pend.append(int(tok.group(1)))
        elif tok.group(2) is not None:
            for c in pend:
                out[c] = tok.group(2)
            pend = []
        else:
            pend = []
    return out


def parse_cpp_grade_switch(text):
    """{грейд: код} из switch «case N: Grade = TEXT("xx"); break;» в теле AsdaWeaponGlow::ComposeJeb или None (функции нет)."""
    m = re.search(r"FString\s+AsdaWeaponGlow::ComposeJeb\s*\([^)]*\)\s*\{(.*?)\n\}", text, re.S)
    if not m:
        return None
    return {int(a): b for a, b in re.findall(r'case\s+(\d+)\s*:\s*Grade\s*=\s*TEXT\("([a-z]+)"\)', m.group(1))}


def strip_cpp_comments(text):
    """Текст C++ без комментариев `//…` и `/*…*/` (строковые и символьные литералы сохраняются; переводы строк
    внутри блочных комментариев сохраняются, чтобы не сдвигать номера строк). ★ Ревизия 15.09: сверки по регэкспам
    не должны видеть закомментированный код."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1]); i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j)); i = j
        else:
            out.append(c); i += 1
    return "".join(out)


def check_weapon_glow_bases(rules):
    """Список расхождений таблицы баз с JSON-регэкспами и C++ (пусто = всё сходится). Файлы не пишет."""
    bad = []
    want = weapon_glow_jeb_regex()
    boost = json.load(open(BOOST_JSON, encoding="utf-8-sig"))
    for label, fams in (("asda_fx_light.json", rules.get("families", [])), ("asda_fx_boost.json", boost.get("families", []))):
        f = next((x for x in fams if x.get("id") == WEAPON_GLOW_FAMILY_ID), None)
        if f is None:
            bad.append("%s: нет семейства %s" % (label, WEAPON_GLOW_FAMILY_ID))
        elif f.get("jeb") != want:
            bad.append("%s %s: jeb=%r, по таблице баз %r" % (label, WEAPON_GLOW_FAMILY_ID, f.get("jeb"), want))
    cpp_text = open(CPP_GLOW_TABLE, encoding="utf-8-sig").read()
    table, shield = parse_cpp_glow_table(cpp_text)
    seen = {}
    for a, base in parse_cpp_glow_pairs(cpp_text):   # ★ ревизия 15.09: дубль подтипа — ошибка (игра берёт первую запись)
        if a in seen:
            bad.append("C++ AsdaFxLightSettings.cpp GAsdaWeaponGlowBases: дубль подтипа %d (%r и %r; игра берёт первую)" % (a, seen[a], base))
        seen.setdefault(a, base)
    if table != WEAPON_GLOW_BASE_BY_SUBTYPE:
        bad.append("C++ AsdaFxLightSettings.cpp GAsdaWeaponGlowBases=%r, таблица генератора %r" % (table, WEAPON_GLOW_BASE_BY_SUBTYPE))
    if shield != SHIELD_GLOW_BASE:
        bad.append("C++ AsdaFxLightSettings.cpp GAsdaShieldGlowBase=%r, таблица генератора %r" % (shield, SHIELD_GLOW_BASE))
    grades = parse_cpp_grade_switch(cpp_text)   # ★ хвосты волны 3 (15.09.2026): таблица грейдов C++ ↔ Python
    if grades != WEAPON_GLOW_GRADE_CODE:
        bad.append("C++ AsdaFxLightSettings.cpp AsdaWeaponGlow::ComposeJeb грейды=%r, таблица генератора %r" % (grades, WEAPON_GLOW_GRADE_CODE))
    if os.path.isfile(CPP_BROWSER_COPY):
        text = strip_cpp_comments(open(CPP_BROWSER_COPY, encoding="utf-8-sig").read())   # ★ ревизия 15.09: без комментариев
        # ★ Хвосты волны 3 (15.09.2026): копия плагина удалена, браузер зовёт AsdaWeaponGlow. Отсутствие копии — норма;
        #   ошибка — если копия есть, но не разобрана (ниже), если вернулась своя таблица грейдов, или если плагин
        #   не зовёт общую формулу вовсе (имя собирается неизвестно чем)
        grade_lits = "|".join(sorted(set(WEAPON_GLOW_GRADE_CODE.values())))
        if re.search(r'\bGrade\s*=\s*TEXT\("[a-z]+"\)', text) or re.search(r'TEXT\("(%s)"\)' % grade_lits, text):
            # ★ ревизия 15.09: не только `Grade = TEXT(..)` — любой литерал кода грейда (например `return TEXT("he")`)
            bad.append("плагин AsdaItemBrowserData.cpp: своя таблица грейдов (Grade = TEXT(...)) — копия AsdaWeaponGlow::ComposeJeb")
        if not re.search(r"\bAsdaWeaponGlow::ComposeJeb\s*\(", text):
            bad.append("плагин AsdaItemBrowserData.cpp: нет вызова AsdaWeaponGlow::ComposeJeb — имя свечения собирается не общей формулой")
        sw = parse_cpp_switch(text, "GlowBaseByWeaponSubType")
        if sw is None and re.search(r"\bGlowBaseByWeaponSubType\s*\(", text):
            # ★ Ревизия 15.09: копия ЕСТЬ, но не разобрана — раньше сверка молча выключалась
            bad.append("копия плагина AsdaItemBrowserData.cpp: GlowBaseByWeaponSubType есть, но не разобрана — сверка невозможна (обновить parse_cpp_switch)")
        if sw is not None and sw != WEAPON_GLOW_BASE_BY_SUBTYPE:
            bad.append("копия плагина AsdaItemBrowserData.cpp GlowBaseByWeaponSubType=%r, таблица генератора %r" % (sw, WEAPON_GLOW_BASE_BY_SUBTYPE))
        ms = re.search(r'bShield\s*\?\s*TEXT\("([a-z]+)"\)', text)
        if sw is not None and ms and ms.group(1) != SHIELD_GLOW_BASE:
            bad.append("копия плагина AsdaItemBrowserData.cpp: база щита %r, таблица генератора %r" % (ms.group(1), SHIELD_GLOW_BASE))
    return bad


def load_rules(path=None):
    """Прочитать таблицу семейств (JSON). Без побочных эффектов."""
    return json.load(open(path or RULES_PATH, encoding="utf-8"))


def resolve_intensity_cd(fam, defaults, ref_lux):
    """★ ЕДИНСТВЕННОЕ место, где относительная шкала превращается в АБСОЛЮТНУЮ канделу.

    intensity_cd задано явно → берём его. Иначе  I = rel · reference_sun_lux · d_ref²,
    где reference_sun_lux — ОПОРНАЯ освещённость проекта (солнце alpen 1.6 лк), а НЕ солнце текущей карты:
    в рантайме солнце больше не читается вовсе, поэтому костёр светит одинаково днём и ночью.
    Поверх — intensity_scale (ручка калибровки семейства).
    """
    scale = float(fam.get("intensity_scale", defaults.get("intensity_scale", 1.0)))
    if "intensity_cd" in fam:
        return float(fam["intensity_cd"]) * scale
    rel = float(fam.get("intensity_rel_sun", defaults.get("intensity_rel_sun", 1.0)))
    dref = float(fam.get("d_ref_m", defaults.get("d_ref_m", 2.0)))
    return rel * float(ref_lux) * dref * dref * scale


def family_spec(fam, defaults, ref_lux):
    """Разрешённые числа семейства как dict {поле C++: значение python}.

    ★ ОБЩАЯ ТОЧКА для генератора ini и для генератора дефолтов в ассетах: оба обязаны положить
    ОДНИ И ТЕ ЖЕ числа, иначе рантайм и панель деталей покажут разное и доверять нельзя будет ничему.
    """
    out = {}
    for jk, cf, t in SPEC_FIELDS:
        if jk == "intensity_cd":
            out[cf] = resolve_intensity_cd(fam, defaults, ref_lux)
            continue
        v = fam.get(jk, defaults.get(jk))
        if v is None: continue
        out[cf] = v
    out["FamilyId"] = fam["id"]
    # Дефолты семейства по определению НЕ ручные: замок взводит только правка владельца в редакторе
    out["bManual"] = False
    return out


def _ini_str(s):
    return '"' + str(s).replace('"', '\\"') + '"'


def _fmt(v, t):
    if t == "s": return _ini_str(v)
    if t == "f": return "%.6f" % float(v)
    if t == "i": return "%d" % int(v)
    if t == "b": return "True" if bool(v) else "False"
    if t in ENUM_MAPS:
        key = str(v).lower()
        if key not in ENUM_MAPS[t]:
            raise ValueError("неизвестное значение %r для %s (допустимы %s)" % (v, t, sorted(ENUM_MAPS[t])))
        return ENUM_MAPS[t][key]
    if t == "c":
        r, g, b = [int(x) for x in v][:3]
        return "(B=%d,G=%d,R=%d,A=255)" % (b, g, r)
    if t == "v":
        x, y, z = [float(q) for q in v][:3]
        return "(X=%.6f,Y=%.6f,Z=%.6f)" % (x, y, z)
    # ★ Ф11: ПОВОРОТ. Порядок в JSON — [Pitch, Yaw, Roll], ровно как их показывает панель деталей UE;
    #   в ini FRotator пишется ИМЕНОВАННЫМИ полями, поэтому перепутать местами их нельзя даже случайно
    if t == "r":
        p, y_, r = [float(q) for q in v][:3]
        return "(Pitch=%.6f,Yaw=%.6f,Roll=%.6f)" % (p, y_, r)
    raise ValueError(t)


def rule_struct(fam, defaults, ref_lux):
    """Одно правило: (Id="…",Jeb="…",Spec=(…))."""
    spec = family_spec(fam, defaults, ref_lux)
    # ★ fix4: bManual больше не вписывается руками — он первый элемент SPEC_FIELDS (довод там же)
    parts = ["FamilyId=%s" % _ini_str(spec["FamilyId"])]
    for jk, cf, t in SPEC_FIELDS:
        if cf not in spec: continue
        parts.append("%s=%s" % (cf, _fmt(spec[cf], t)))
    return "(Id=%s,Jeb=%s,Spec=(%s))" % (_ini_str(fam["id"]), _ini_str(fam["jeb"]), ",".join(parts))


def validate(rules):
    """Инварианты таблицы. Нарушение = отказ работать, а не тихая генерация мусора."""
    fb = rules.get("fallback", {})
    if fb.get("action") != "family":
        raise SystemExit('fallback.action обязан быть "family": эффект без правила теперь невозможен')
    fams = rules["families"]
    ids = [f["id"] for f in fams]
    if len(ids) != len(set(ids)):
        raise SystemExit("повторяющиеся id правил: %s" % ids)
    last = fams[-1]
    if last["id"] != fb.get("family"):
        raise SystemExit("последним правилом обязано быть %r (fallback.family), а стоит %r" % (fb.get("family"), last["id"]))
    if last["jeb"] not in (".", ".+", ".*"):
        raise SystemExit("шаблон последнего правила %r обязан ловить всё ('.'), иначе часть эффектов останется без дефолтов" % last["jeb"])
    if not last.get("enabled", rules["defaults"].get("enabled")):
        raise SystemExit("последнее правило обязано быть со светом: требование владельца «все эффекты, кроме дыма»")
    for f in fams:
        try:
            re.compile(f["jeb"])
        except re.error as ex:
            raise SystemExit("правило %r: шаблон не компилируется: %s" % (f["id"], ex))
    if (len(rules["budget_by_level"]) != 4 or len(rules["screen_budget_by_level"]) != 4
            or len(rules["radius_cap_by_level"]) != 4 or len(rules.get("per_effect_by_level", [])) != 4):
        raise SystemExit("бюджеты задаются ровно на 4 уровня (Low/Medium/High/Epic) + отдельное значение Cine")


# ═══ ★★★ 13.09.2026: ПЕРЕОПРЕДЕЛЕНИЯ «ЭФФЕКТ / ПРЕДМЕТ» (конфигуратор свечения) ═══════════════════════
#   Раздел JSON "overrides": [{"jeb": "ohs_a_co_ma_03", "item_id": 0|код, <ТОЛЬКО изменённые ключи чисел>, ...}].
#   В JSON лежат лишь ключи, которые владелец поменял; всё прочее НАСЛЕДУЕТСЯ при генерации:
#       defaults ← семейство (первое правило, чей шаблон находит имя) ← запись эффекта (item_id 0) ← запись предмета.
#   Поэтому правка семейства продолжает доезжать до переопределённых предметов во всех НЕ тронутых ключах.
#   ★ Сила свечения (override_emissive/emissive_boost) от семейства НЕ наследуется: у семейства рантайм применяет
#     её своим уровнем, НИЖЕ замка ассета, а переопределение стоит ВЫШЕ — унаследовав её, оно перебило бы ассет.
#   ★ bLight = есть хоть один свой ключ СВЕТА в цепочке эффект→предмет; запись «только эмиссия» свет не трогает.
OVERRIDE_META_KEYS = ("jeb", "item_id", "family", "label", "doc", "comment")
# ★★★ РЕВИЗИЯ fix5 (СРЕДН-Ж, 20.09.2026): «manual» В ПЕРЕОПРЕДЕЛЕНИЯХ ОТВЕРГАЕТСЯ, А НЕ ГЛОТАЕТСЯ.
#   ЧТО БЫЛО НЕ ТАК. fix4 внёс «manual» первым элементом SPEC_FIELDS (чтобы конфигуратор видел замок),
#   и ключ автоматически попал в SPEC_JSON_KEYS — то есть проверка переопределений (validate_overrides)
#   перестала его отвергать. А `family_spec` БЕЗУСЛОВНО пишет out["bManual"] = False, поэтому запись
#   `"manual": true` в разделе overrides ПРИНИМАЛАСЬ И МОЛЧА ТЕРЯЛАСЬ — худший из исходов.
#   ПОЧЕМУ ОТВЕРГАЕМ, А НЕ ИСПОЛНЯЕМ. Замок bManual означает «поля АССЕТА старше правила семейства»,
#   а переопределение стоит ВЫШЕ замка по построению (AsdaFxLightComponent.cpp:188-191: ветка
#   `Ov && Ov->bLight` проверяется ДО `Fx->FxLight.bManual`). Значение этого ключа в записи
#   переопределения не значит ничего ни при каком раскладе — принимать его было бы обманом.
SPEC_JSON_KEYS = tuple(jk for jk, _, _ in SPEC_FIELDS if jk != "manual")


def find_family(rules, jeb):
    """Первое правило, чей шаблон НАХОДИТ имя (поиск без учёта регистра — как FRegexPattern + FindNext в рантайме)."""
    for f in rules["families"]:
        if re.search(f["jeb"], jeb, re.I):
            return f
    return None


def find_override(rules, jeb, item_id):
    """Запись раздела overrides по точному имени (без регистра) и коду предмета или None."""
    low = str(jeb).lower()
    for ov in rules.get("overrides", []):
        if str(ov.get("jeb", "")).lower() == low and int(ov.get("item_id", 0)) == int(item_id):
            return ov
    return None


def override_spec(rules, ov):
    """(числа как у family_spec, bLight, семейство) для записи overrides — сведение цепочки наследования."""
    defaults = rules.get("defaults", {})
    ref_lux = float(rules["reference_sun_lux"])
    fam = find_family(rules, ov["jeb"])
    chain = []
    item_id = int(ov.get("item_id", 0))
    if item_id != 0:
        eff = find_override(rules, ov["jeb"], 0)
        if eff is not None:
            chain.append(eff)
    chain.append(ov)
    own = {}
    for e in chain:
        for k, v in e.items():
            if k in SPEC_JSON_KEYS:
                own[k] = v
    merged = dict(fam or {"id": "other"})
    for k in EMISSIVE_KEYS:
        merged.pop(k, None)
    if "intensity_cd" in own:
        merged.pop("intensity_scale", None)     # кандела, заданная руками, абсолютна: калибровочный множитель семейства к ней не применяется
    merged.update(own)
    spec = family_spec(merged, defaults, ref_lux)
    fam_id = fam["id"] if fam else "other"
    # Ярлык в дампе: откуда числа — «семейство/эффект» или «семейство/предмет N»
    spec["FamilyId"] = "%s/%s" % (fam_id, ("item%d" % item_id) if item_id else "effect")
    light = any(k not in EMISSIVE_KEYS for k in own)
    return spec, light, fam


def _spec_parts(spec):
    """Поля структуры FAsdaFxLightSpec текстом ini — общий порядок для +Families и +Overrides."""
    # ★ fix4: bManual больше не вписывается руками — он первый элемент SPEC_FIELDS (довод там же)
    parts = ["FamilyId=%s" % _ini_str(spec["FamilyId"])]
    for jk, cf, t in SPEC_FIELDS:
        if cf not in spec: continue
        parts.append("%s=%s" % (cf, _fmt(spec[cf], t)))
    return parts


def override_struct(rules, ov):
    """Одно переопределение: (Jeb="…",ItemId=N,bLight=…,Spec=(…))."""
    spec, light, _ = override_spec(rules, ov)
    return "(Jeb=%s,ItemId=%d,bLight=%s,Spec=(%s))" % (
        _ini_str(ov["jeb"]), int(ov.get("item_id", 0)), "True" if light else "False", ",".join(_spec_parts(spec)))


def validate_overrides(rules):
    """Инварианты раздела overrides. Нарушение = отказ, а не тихая генерация мусора."""
    seen = set()
    for ov in rules.get("overrides", []):
        jeb = str(ov.get("jeb", "")).strip()
        if not jeb or not re.fullmatch(r"[A-Za-z0-9_.\-]+", jeb):
            raise SystemExit("переопределение %r: jeb обязан быть точным ИМЕНЕМ эффекта (буквы, цифры, _ . -), а не шаблоном" % ov)
        item_id = ov.get("item_id", 0)
        if not isinstance(item_id, int) or item_id < 0:
            raise SystemExit("переопределение %s: item_id обязан быть целым ≥ 0 (0 = все предметы с этим эффектом)" % jeb)
        key = (jeb.lower(), item_id)
        if key in seen:
            raise SystemExit("переопределение %s / предмет %d повторяется" % key)
        seen.add(key)
        # ★ fix5 (СРЕДН-Ж): «manual» называется отдельно — иначе отказ выглядел бы как опечатка,
        #   а причина у него своя: переопределение стоит ВЫШЕ замка, и ключ не значит ничего
        if "manual" in ov:
            raise SystemExit("переопределение %s / предмет %d: ключ \"manual\" здесь не действует — "
                             "запись переопределения стоит ВЫШЕ замка ручной правки ассета "
                             "(AsdaFxLightComponent.cpp:188-191), и её числа применяются в любом случае. Уберите ключ." % (jeb, item_id))
        bad = [k for k in ov if k not in SPEC_JSON_KEYS and k not in OVERRIDE_META_KEYS]
        if bad:
            raise SystemExit("переопределение %s / предмет %d: неизвестные ключи %s" % (jeb, item_id, bad))
        override_struct(rules, ov)      # перечисления и типы проверяет _fmt (ValueError на неизвестном значении)


def build_section(rules):
    """Текст секции (без заголовка) по JSON."""
    validate(rules)
    validate_overrides(rules)
    defaults = rules.get("defaults", {})
    ref_lux = float(rules["reference_sun_lux"])
    lines = [MARK]
    # ★ 14.09.2026: метка чтения секции (UAsdaFxLightSettings::ConfigStamp; 0 в C++ = секция не прочитана → Warning в лог)
    lines.append("ConfigStamp=%d" % CONFIG_STAMP)
    lines.append("bEnabled=True")
    lines.append("bShowInEditor=True")
    # ★★★ ОДНА СТРОКА НА ПРАВИЛО и ОБЯЗАТЕЛЬНЫЙ «+».
    #   UObject::LoadConfig читает TArray-свойство через MultiFind по ключу: КАЖДАЯ строка секции = ОДИН элемент
    #   массива. Единственная строка вида Families=((A),(B)) молча импортируется как ОДИН элемент с мусором внутри —
    #   таблица оказывается пустой, ни один эффект не получает дефолтов, и это неотличимо от «света не надо»
    #   (грабля волны Ф2: первый прогон дал «кандидатов 0» именно из-за этого).
    lines += ["+Families=" + rule_struct(f, defaults, ref_lux) for f in rules["families"]]
    lines.append("MaxCandidates=%d" % int(rules.get("max_candidates", 2048)))
    lines.append("BudgetFallback=%d" % int(rules["budget_by_level"][-1]))
    lines.append("ScreenBudgetFallback=%.6f" % float(rules["screen_budget_by_level"][-1]))
    lines.append("RadiusCapFallback=%.6f" % float(rules["radius_cap_by_level"][-1]))
    lines.append("LeaseMinSec=%.6f" % float(rules.get("lease_min_sec", 0.4)))
    lines.append("LeaseHysteresis=%.6f" % float(rules.get("lease_hysteresis", 1.25)))
    lines.append("SelectIntervalSec=%.6f" % float(rules.get("select_interval_sec", 0.0)))
    # ★ ВОЛНА Ф10 (11.09.2026): масштаб длины миров (сцена логина ×100). По строке «+WorldScales=(…)» на мир —
    #   TArray читается MultiFind, как и Families выше
    for ws in rules.get("world_scales", []):
        lines.append("+WorldScales=(World=%s,LengthScale=%.6f)" % (_ini_str(ws["world"]), float(ws["length_scale"])))
    # ★ 13.09.2026: переопределения эффектов/предметов (конфигуратор свечения) — по строке «+Overrides=(…)» на запись.
    #   Раздела нет или он пуст — строк нет, секция побайтно та же, что до конфигуратора
    for ov in rules.get("overrides", []):
        lines.append("+Overrides=" + override_struct(rules, ov))
    return "\n".join(lines) + "\n"


def split_ini_section(text, section):
    """(до, тело секции|None, после) по заголовку `section` («[…]» целиком) в тексте ini.
    Одна реализация для генераторов ini (дедуп №2 В1 Т19, 11.10.2026): была дословная копия в
    asda_gen_render_settings_ini.split_section, отличалась только именем секции в регулярном выражении."""
    m = re.search(r"^" + re.escape(section) + r"[ \t]*\r?\n", text, re.M)
    if not m: return text, None, ""
    rest = text[m.end():]
    m2 = re.search(r"^\[", rest, re.M)
    body = rest if not m2 else rest[:m2.start()]
    # ★★★ ВОЛНА Ф6 (10.09.2026), ИСПРАВЛЕНИЕ ПОТЕРИ ЧУЖОГО ТЕКСТА. Секция ini кончается там, где начинается
    #   следующая «[…]», поэтому ПУСТЫЕ СТРОКИ И КОММЕНТАРИИ, стоящие ПЕРЕД следующим заголовком, формально
    #   попадали в тело этой секции — и перезапись стирала их. Так 10.09 пропали девять строк шапки про
    #   GM-режим, написанные другим агентом. Отрезаем хвост из пустых строк и «;»-комментариев обратно в
    #   «после»: генерируем только СВОИ ключи, чужой текст не трогаем (заодно --check перестаёт видеть
    #   мнимое расхождение по пустой строке).
    lines = body.splitlines(keepends=True)
    cut = len(lines)
    while cut > 0 and (not lines[cut - 1].strip() or lines[cut - 1].lstrip().startswith(";")):
        cut -= 1
    tail = "".join(lines[cut:])
    keep = "".join(lines[:cut])
    return text[:m.start()], keep, tail + rest[len(body):]


def split_section(text):
    """(до, тело секции|None, после) по SECTION в тексте ini (общий split_ini_section)."""
    return split_ini_section(text, SECTION)


# Уровневые ключи: (ключ ini, ключ JSON на 4 уровня, ключ JSON для Cine, формат)
LEVEL_KEYS = [
    ("Light.FxBudget", "budget_by_level", "budget_cine", "%d"),
    ("Light.FxScreenBudget", "screen_budget_by_level", "screen_budget_cine", "%.3f"),
    ("Light.FxRadiusCap", "radius_cap_by_level", "radius_cap_cine", "%.1f"),
    # ★★★ ВОЛНА Ф8 (10.09.2026): сколько светов разрешено ОДНОМУ эффекту (режим «свой у каждого эмиттера»)
    ("Light.FxPerEffect", "per_effect_by_level", "per_effect_cine", "%d"),
]


def check_scalability(rules):
    """Секции [AsdaFxLight@N] в DefaultScalability.ini против бюджетов JSON. Возвращает список расхождений."""
    names = ["0", "1", "2", "3", "Cine"]
    txt = open(SCAL_INI, encoding="utf-8", errors="replace").read()
    bad = []
    for ini_key, jk4, jkc, fmt in LEVEL_KEYS:
        want = list(rules[jk4]) + [rules[jkc]]
        for n, w in zip(names, want):
            m = re.search(r"^\[AsdaFxLight@%s\][^\[]*?^%s=(-?[\d.]+)" % (re.escape(n), re.escape(ini_key)), txt, re.M | re.S)
            got = m.group(1) if m else None
            if got is None or abs(float(got) - float(w)) > 1e-6:
                bad.append("[AsdaFxLight@%s] %s: ini=%s json=%s" % (n, ini_key, got, fmt % w))
    return bad


def main(argv):
    _utf8_console()
    rules = load_rules()
    if "--specs" in argv:
        defaults = rules.get("defaults", {})
        ref = float(rules["reference_sun_lux"])
        print("опорная освещённость проекта: %.3f лк" % ref)
        print("%-22s %8s %8s %8s %8s %6s  %s" % ("семейство", "кд", "R мин", "R макс", "дальн", "прио", "свет"))
        for f in rules["families"]:
            s = family_spec(f, defaults, ref)
            print("%-22s %8.2f %8.0f %8.0f %8.0f %6.1f  %s" % (
                s["FamilyId"], s["IntensityCd"], s["RadiusMin"], s["RadiusMax"],
                s["MaxDrawDistance"], s["Priority"], "да" if s["bEnabled"] else "НЕТ"))
        return 0
    body = build_section(rules)
    if "--print" in argv:
        print(SECTION); print(body); return 0
    text = open(GAME_INI, encoding="utf-8-sig").read()
    before, cur, after = split_section(text)
    bad = check_scalability(rules)
    if "--check" in argv:
        same = (cur is not None) and (cur.strip().replace("\r", "") == body.strip())
        print("DefaultGame.ini %s: %s" % (SECTION, "совпадает с JSON" if same else ("СЕКЦИИ НЕТ" if cur is None else "РАСХОЖДЕНИЕ с JSON")))
        if cur is not None and not same:
            a = [l for l in cur.strip().replace("\r", "").splitlines() if not l.startswith(";")]
            b = [l for l in body.strip().splitlines() if not l.startswith(";")]
            for l in a:
                if l not in b: print("  ini : " + l[:300])
            for l in b:
                if l not in a: print("  json: " + l[:300])
        for l in bad: print("  " + l)
        print("DefaultScalability.ini [AsdaFxLight@N]: %s" % ("совпадает" if not bad else "РАСХОЖДЕНИЕ"))
        wbad = check_weapon_glow_bases(rules)   # ★ волна 3 п.16
        for l in wbad: print("  " + l)
        print("Базы и грейды weapon_glow (таблица генератора ↔ asda_fx_light.json ↔ asda_fx_boost.json ↔ C++ ↔ плагин без копии): %s" % ("совпадает" if not wbad else "РАСХОЖДЕНИЕ"))
        return 0 if (same and not bad and not wbad) else 3
    # запись: бэкап → замена/добавление секции (BOM сохраняем — DefaultGame.ini проекта с BOM)
    # Бэкап — в общий каталог волны рядом с репозиторием (закон backup-before-edit), а не внутрь проекта:
    # PROJ = …/AstoryClient, <ROOT> — на уровень выше
    bdir = os.path.join(os.path.dirname(PROJ), "_backup", "2026-09-09_fx_f2all", "Config")
    os.makedirs(bdir, exist_ok=True)
    bpath = os.path.join(bdir, "DefaultGame.ini")
    if not os.path.isfile(bpath):
        shutil.copy2(GAME_INI, bpath)
        assert os.path.getsize(bpath) == os.path.getsize(GAME_INI), "бэкап разошёлся по размеру"
    # ★ 13.09.2026 (ревизия конфигуратора): текст — из render_game_ini (тот же, что пишет конфигуратор, побайтно),
    #   запись — через временный файл и замену (файл либо старый, либо новый целиком)
    data, old = render_game_ini(rules)
    if data != old:
        # ★ 11.10.2026 (ревизия wave0_REVIEW.md, С4): набор 2026-09-09 выше снимается ОДИН раз и остаётся точкой отката;
        #   ТЕКУЩЕЕ состояние перед КАЖДОЙ записью — общий бэкап (набор <сегодня>_fx_light_ini; при отличии — снимок .bak).
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import asda_headless_tool as HT
        if HT.backup_file(GAME_INI, "fx_light_ini", verify="md5") is None:
            raise RuntimeError("нет %s — бэкап не снят, запись запрещена" % GAME_INI)
        tmp = GAME_INI + ".asda_tmp"
        with open(tmp, "wb") as fh:
            fh.write(data)
        os.replace(tmp, GAME_INI)
    print("записано %s (%d правил); бэкап %s" % (GAME_INI, len(rules["families"]), bpath))
    for l in bad: print("  ВНИМАНИЕ: " + l)
    wbad = check_weapon_glow_bases(rules)   # ★ волна 3 п.16: запись секции не зависит от сверки, но расхождение называется
    for l in wbad: print("  ВНИМАНИЕ: " + l)
    return 0 if not (bad or wbad) else 3


def render_game_ini(rules):
    """(новые байты DefaultGame.ini, текущие байты) — секция из JSON, остальной текст как есть. Файлы не пишет.
    ★ ЕДИНАЯ точка текста ini для CLI и для конфигуратора: перевод строки файла сохраняется (проект — CRLF),
      BOM пишется всегда (как прежняя запись encoding=utf-8-sig)."""
    body = build_section(rules)
    raw = open(GAME_INI, "rb").read()
    nl = "\r\n" if b"\r\n" in raw else "\n"
    text = raw.decode("utf-8-sig").replace("\r\n", "\n").replace("\r", "\n")
    before, cur, after = split_section(text)
    if cur is None:
        new = text.rstrip("\n") + "\n\n" + SECTION + "\n" + body
    else:
        new = before + SECTION + "\n" + body + ("\n" if after and not after.startswith("\n") else "") + after
    return b"\xef\xbb\xbf" + new.replace("\n", nl).encode("utf-8"), raw


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
