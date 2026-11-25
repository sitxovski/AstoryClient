# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_apply_fx_light_defaults.py — ★ ВОЛНА Ф2-ВСЕ (09.09.2026): МАССОВАЯ ПРОСТАНОВКА ДЕФОЛТОВ СВЕТА
#   в поле «Свет эффекта» (AAsdaEffectActor::FxLight) 1274 ассетов BP_Fx_* и — по требованию — эффект-акторов карт.
#
#   ЗАЧЕМ. Владелец: «Добавь каждому эффекту поле с дефолтными значениями, а я уже руками подкручу».
#   Чтобы «подкручивать» было что, в ассете должны лежать ОСМЫСЛЕННЫЕ числа, а не C++-заглушка: открыл
#   BP_Fx_fire_1 — видишь 6.48 кд, семейство map_fire, радиус 200…700, мерцание 8 Гц. Числа берутся из
#   Tools/asda_fx_light.json ТОЙ ЖЕ функцией family_spec(), которой генерируется секция ini
#   (Tools/asda_gen_fx_light_ini.py) — разойтись они не могут по построению.
#
#   ★★★ ЗАМОК ОТ ЗАТИРАНИЯ. Ассет, у которого FxLight.bManual = True (владелец правил поле руками —
#   галочку взводит AAsdaEffectActor::PostEditChangeProperty), ПРОПУСКАЕТСЯ целиком. Повторный прогон
#   после правки таблицы обновит только нетронутые эффекты.
#
#   ИДЕМПОТЕНТНОСТЬ. Если числа в ассете уже совпадают с целевыми — ассет не открывается на запись и не
#   помечается изменённым. Повторный прогон без правок в JSON меняет НОЛЬ файлов.
#
#   Запуск (headless-редактор, окон не показывает):
#     python Tools/run_headless_safe.py Tools/asda_apply_fx_light_defaults.py --marker "ЗАВЕРШЕНО" --timeout 3600 --idle 600
#   Переменные окружения:
#     ASDA_FXL_MODE=apply|check|rollback   apply — писать (по умолчанию), check — только отчёт, rollback — вернуть из бэкапа
#     ASDA_FXL_WHAT=bp|maps|both           что обрабатывать (по умолчанию bp)
#     ASDA_FXL_LIMIT=N                     обработать не больше N ассетов (обкатка)
#
#   ★ КАРТЫ ПО УМОЛЧАНИЮ НЕ ТРОГАЕМ (ASDA_FXL_WHAT=bp). Причины две:
#     1. над теми же .umap параллельно работает калибровка bloom (PP-волюмы карт) — две волны, пишущие
#        один файл, гарантированно затрут друг друга;
#     2. эффект-акторам карт простановка и НЕ НУЖНА для работы: при bManual=False рантайм берёт ровно те же
#        числа из правила семейства, а в ОКНЕ РЕДАКТОРА подсистема зеркалит их в поле актора при заведении
#        кандидата (UAsdaFxLightSubsystem::RegisterEffect) — панель деталей показывает действующие значения,
#        а карта при этом НЕ помечается изменённой.
#     Режим maps оставлен готовым: запустить, когда карты освободятся.
#
#   ОТКАТ. Перед первой записью каждый файл копируется в
#     _backup/2026-09-09_fx_f2all/Content/<относительный путь>   (со сверкой размера).
#   Возврат: ASDA_FXL_MODE=rollback — копирует бэкапы обратно поверх Content/ и сверяет размеры.
import os, re, sys, json, shutil, hashlib
import stat as stat_mod

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(TOOLS_DIR)                       # …/AstoryClient
REPO = os.path.dirname(PROJ)                            # <ROOT>
BACKUP = os.path.join(REPO, "_backup", "2026-09-09_fx_f2all")
# ★ Уровни кладём в ОТДЕЛЬНЫЙ бэкап волны карт (10.09.2026): бэкап ассетов уже занят прошлой волной,
#   и смешивать «до правки BP» и «до правки .umap» нельзя — откат стал бы неоднозначным.
BACKUP_MAPS = os.path.join(REPO, "_backup", "2026-09-10_fx_maps")
# Уровни, у которых сохранение не подтвердилось по MD5. Пока список не пуст, маркер успеха НЕ печатается.
FAILED_LEVELS = []
MARKER_MAPS = "FX-MAPS-OK"                              # ASCII-маркер успеха волны карт
CONTENT = os.path.join(PROJ, "Content")
EFFECTS_DIR = "/Game/AsdaAssets/Effects"
MAPS_DIR = "/Game/Map"

MODE = os.environ.get("ASDA_FXL_MODE", "apply").lower()
WHAT = os.environ.get("ASDA_FXL_WHAT", "bp").lower()
LIMIT = int(os.environ.get("ASDA_FXL_LIMIT", "0"))

sys.path.insert(0, TOOLS_DIR)
import asda_gen_fx_light_ini as GEN            # без побочных эффектов при импорте
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
import asda_login_guard as _LG   # ★ 15.09.2026: уровень логина (свет владельца) — только с ASDA_ALLOW_LOGIN_WRITE=1

for _s in (sys.stdout, sys.stderr):
    try: _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception: pass


def log(m):
    try:
        import unreal
        unreal.log("[дефолты света] " + str(m))
    except Exception:
        print("[дефолты света] " + str(m))


# ─────────────── таблица семейств ───────────────

def build_family_table():
    """[(id, скомпилированный шаблон, dict целевых значений)] в порядке JSON (сверху вниз, первое подходящее)."""
    rules = GEN.load_rules()
    GEN.validate(rules)
    defaults = rules.get("defaults", {})
    ref = float(rules["reference_sun_lux"])
    out = []
    for fam in rules["families"]:
        out.append((fam["id"], re.compile(fam["jeb"], re.IGNORECASE), GEN.family_spec(fam, defaults, ref)))
    return out


def match_family(table, jeb):
    for fid, rx, spec in table:
        if rx.search(jeb): return fid, spec
    return None, None


# ─────────────── имена свойств в python-обёртке UE ───────────────
# UE переводит UPROPERTY в snake_case, а у bool-свойств СНИМАЕТ венгерский префикс `b`:
#   bEnabled → enabled, bManual → manual, bAffectGI → affect_gi, IntensityCd → intensity_cd.
# Полагаться на угадывание нельзя (грабля: молчаливый LogPython-warning вместо ошибки), поэтому у каждого
# поля список кандидатов, и первый принятый запоминается.
PROP_ALIASES = {
    "bEnabled": ["enabled", "b_enabled"],
    "bManual": ["manual", "b_manual"],
    "bAffectGI": ["affect_gi", "b_affect_gi"],
    "FamilyId": ["family_id"],
    "IntensityCd": ["intensity_cd"],
    "RadiusMode": ["radius_mode"], "RadiusScale": ["radius_scale"],
    "RadiusMin": ["radius_min"], "RadiusMax": ["radius_max"],
    "ColorMode": ["color_mode"], "ColorSRGB": ["color_srgb"], "ColorSaturation": ["color_saturation"],
    "FlickerHz": ["flicker_hz"], "FlickerAmplitude": ["flicker_amplitude"],
    "Position": ["position"], "Offset": ["offset"],
    "AlphaPower": ["alpha_power"], "Priority": ["priority"],
    "MaxDrawDistance": ["max_draw_distance"], "FadeRange": ["fade_range"],
    "SourceRadius": ["source_radius"], "SoftSourceRadius": ["soft_source_radius"],
    "VolumetricScattering": ["volumetric_scattering"],
    "Emitter": ["emitter"],
    # ★★★ ДОПИСАНО 10.09.2026 (волна Ф5): поле волны Ф4, которого здесь не было — второй симптом
    #   того же застывшего списка. Прогон валился KeyError('RadiusBoundsScale') на всех 1274 ассетах.
    "RadiusBoundsScale": ["radius_bounds_scale"],
}


def aliases(cf):
    """Псевдонимы UE-Python для поля C++. ★ НЕИЗВЕСТНОЕ ПОЛЕ БОЛЬШЕ НЕ РОНЯЕТ ПРОГОН: имя переводится
    в snake_case по общему правилу UE (RadiusBoundsScale -> radius_bounds_scale, bManual -> manual/b_manual).
    Раньше отсутствие записи в словаре давало KeyError и обнулял ВЕСЬ прогон — «ошибок=1274, изменено=0»."""
    if cf in PROP_ALIASES:
        return PROP_ALIASES[cf]
    body = cf[1:] if (len(cf) > 1 and cf[0] == "b" and cf[1].isupper()) else cf
    snake = re.sub(r"(?<!^)(?=[A-Z])", "_", body).lower()
    return ([snake, "b_" + snake] if body is not cf else [snake])
# ★★★ ИСПРАВЛЕНО 10.09.2026 (волна Ф5). ЗДЕСЬ БЫЛ ЗАСТЫВШИЙ СПИСОК ЗНАЧЕНИЙ ВОЛНЫ Ф2
#   ({node, fixed} / {section, fixed} / {section, actor}). Волна Ф4 дописала в таблицу семейств
#   агрегирующие режимы `mean` / `median` / `bounds` — и КАЖДЫЙ прогон генератора с тех пор падал
#   на KeyError('bounds') ПО ВСЕМ 1274 АССЕТАМ: «всего=1274, изменено=0, ошибок=1274». То есть
#   «возврат к значениям семейства» не работал вовсе, и обнаружилось это только сейчас, потому что
#   отчёт печатал итог и без единой записи.
#   ЛЕЧЕНИЕ — НЕ ВТОРАЯ КОПИЯ СПИСКА, а ОДИН источник: карта значений берётся у генератора ini
#   (asda_gen_fx_light_ini.ENUM_MAPS), которым эта же таблица разрешается для рантайма. Разойтись
#   они теперь не могут по построению (закон port-shared-points), и следующий новый режим не
#   потребует правки в двух местах.
#   Имя элемента UENUM в UE-Python — ВЕРХНИЙ РЕГИСТР короткого имени C++ (Mean -> MEAN).
ENUM_PY = {
    "ColorMode":  ("AsdaFxLightColorMode",  {k: v.upper() for k, v in GEN.ENUM_MAPS["e_color"].items()}),
    "RadiusMode": ("AsdaFxLightRadiusMode", {k: v.upper() for k, v in GEN.ENUM_MAPS["e_radius"].items()}),
    "Position":   ("AsdaFxLightPosMode",    {k: v.upper() for k, v in GEN.ENUM_MAPS["e_pos"].items()}),
    # ★ ВОЛНА Ф10 (11.09.2026): форма (Auto/Tube/Ring…) и охват (PerEmitter) навыков. Без этих строк
    #   to_unreal_value отдал бы set_editor_property голую строку «auto», поле ушло бы в UNWRITABLE,
    #   и ассеты навыков молча остались бы с формой «точка». Имя элемента в UE-Python — ВЕРХНИЙ
    #   РЕГИСТР С ПОДЧЁРКИВАНИЕМ между словами (PerEmitter -> PER_EMITTER), поэтому не голый .upper().
    "Shape":      ("AsdaFxLightShape",      {k: re.sub(r"(?<!^)(?=[A-Z])", "_", v).upper() for k, v in GEN.ENUM_MAPS["e_shape"].items()}),
    "Scope":      ("AsdaFxLightScope",      {k: re.sub(r"(?<!^)(?=[A-Z])", "_", v).upper() for k, v in GEN.ENUM_MAPS["e_scope"].items()}),
    # ★★★ ВОЛНА Ф11 (20.09.2026): ПЕРЕЧИСЛЕНИЯ РАЗМЕЩЕНИЯ ФОРМЫ. До Ф11 их не было ни в SPEC_FIELDS,
    #   ни здесь — то есть поставить семейству «прямоугольник из габаритов секции, нормаль в обратную
    #   сторону» было физически нечем. Карты значений берутся у генератора ТЕМ ЖЕ приёмом (один
    #   источник, закон port-shared-points): разойтись они не могут по построению.
    "RectSizeMode":   ("AsdaFxRectSizeMode",   {k: v.upper() for k, v in GEN.ENUM_MAPS["e_size"].items()}),
    "TubeLengthMode": ("AsdaFxRectSizeMode",   {k: v.upper() for k, v in GEN.ENUM_MAPS["e_size"].items()}),
    "RingSizeMode":   ("AsdaFxRectSizeMode",   {k: v.upper() for k, v in GEN.ENUM_MAPS["e_size"].items()}),
    "DiscSizeMode":   ("AsdaFxRectSizeMode",   {k: v.upper() for k, v in GEN.ENUM_MAPS["e_size"].items()}),
    "SphereSizeMode": ("AsdaFxRectSizeMode",   {k: v.upper() for k, v in GEN.ENUM_MAPS["e_size"].items()}),
    "RectNormalMode": ("AsdaFxRectNormalMode", {k: re.sub(r"(?<!^)(?=[A-Z])", "_", v).upper() for k, v in GEN.ENUM_MAPS["e_normal"].items()}),
    "DiscNormalMode": ("AsdaFxRectNormalMode", {k: re.sub(r"(?<!^)(?=[A-Z])", "_", v).upper() for k, v in GEN.ENUM_MAPS["e_normal"].items()}),
    "SpotDirMode":    ("AsdaFxSpotDirMode",    {k: re.sub(r"(?<!^)(?=[A-Z])", "_", v).upper() for k, v in GEN.ENUM_MAPS["e_spotdir"].items()}),
    "ShapeAnchor":    ("AsdaFxShapeAnchor",    {k: re.sub(r"(?<!^)(?=[A-Z])", "_", v).upper() for k, v in GEN.ENUM_MAPS["e_anchor"].items()}),
}

# ★ Ф11: поля-ВЕКТОРЫ и поля-ПОВОРОТЫ структуры. Раньше единственным вектором был `Offset`, и он
#   разбирался строкой прямо в to_unreal_value; с приходом «смещения по осям формы» и трёх поворотов
#   такой разбор пришлось бы дописывать при каждом новом поле — поэтому здесь два явных списка.
VECTOR_FIELDS = ("Offset", "OffsetLocal")
ROTATOR_FIELDS = ("RectRotation", "SpotRotation", "ShapeRotation")


def to_unreal_value(unreal, cf, v):
    """Значение JSON → значение, которое принимает set_editor_property."""
    if cf in ENUM_PY:
        cls_name, mapping = ENUM_PY[cf]
        return getattr(getattr(unreal, cls_name), mapping[str(v).lower()])
    if cf == "ColorSRGB":
        r, g, b = [int(x) for x in v][:3]
        return unreal.Color(b=b, g=g, r=r, a=255)
    if cf in VECTOR_FIELDS:
        x, y, z = [float(q) for q in v][:3]
        return unreal.Vector(x, y, z)
    # ★ Ф11: порядок в JSON — [Pitch, Yaw, Roll], как в панели деталей UE и как пишет генератор ini
    if cf in ROTATOR_FIELDS:
        p, y_, r = [float(q) for q in v][:3]
        return unreal.Rotator(roll=r, pitch=p, yaw=y_)
    if isinstance(v, bool): return bool(v)
    # ★ Ф11: целые поля — не только номер донора: число источников раскладки, полос блина и дуг кольца
    if isinstance(v, (int, float)) and cf in ("Emitter", "ShapeSegments", "DiscStrips", "RingSegments"): return int(v)
    if isinstance(v, (int, float)): return float(v)
    return v


def get_prop(obj, cf):
    """Прочитать поле структуры по любому из псевдонимов; (значение, имя) или (None, None)."""
    for nm in aliases(cf):
        try: return obj.get_editor_property(nm), nm
        except Exception: continue
    return None, None


# Поля, которые не удалось записать ни по одному псевдониму (обычно read-only свойство UE) — копятся,
# чтобы одно упрямое поле не срывало прогон по всем 1274 ассетам (грабля первого прогона: VisibleAnywhere
# FamilyId кидал исключение, и НИ ОДИН ассет не был обработан — «ошибок=1274, изменено=0»).
UNWRITABLE = set()


def set_prop(obj, cf, value):
    for nm in aliases(cf):
        try:
            obj.set_editor_property(nm, value)
            return nm
        except Exception:
            continue
    UNWRITABLE.add(cf)
    return None


def same_value(cur, want, cf):
    """Совпадают ли текущее и целевое (float — с допуском, иначе идемпотентность рушится на 6.4799995)."""
    if cf in ENUM_PY:
        return cur == want
    # ★ У структур UE (Color, Vector) repr содержит адрес объекта — сравнение строк ВСЕГДА даёт «разные»,
    #   и повторный прогон переписывал бы все 1274 ассета заново (идемпотентности бы не было).
    #   Сравниваем по компонентам.
    if cf == "ColorSRGB":
        return all(int(getattr(cur, k)) == int(getattr(want, k)) for k in ("r", "g", "b", "a"))
    # ★★★ РЕВИЗИЯ Ф11 (К1, 20.09.2026): СРАВНИВАЛСЯ ТОЛЬКО `Offset`. Волна Ф11 завела второй вектор
    #   (`OffsetLocal`) и три поворота (VECTOR_FIELDS / ROTATOR_FIELDS выше), а сюда их не дописала —
    #   и у них сравнение падало в `cur == want` на структурах UE, где repr содержит АДРЕС объекта,
    #   то есть «одинаковые» никогда не совпадали бы. Как только такое поле появится хоть у одного
    #   семейства, каждый прогон переписывал бы все 1274 ассета заново (идемпотентности нет).
    #   Списки те же, что у to_unreal_value — второго перечня полей не заводим.
    if cf in VECTOR_FIELDS:
        return all(abs(float(getattr(cur, k)) - float(getattr(want, k))) <= 1e-4 for k in ("x", "y", "z"))
    if cf in ROTATOR_FIELDS:
        return all(abs(float(getattr(cur, k)) - float(getattr(want, k))) <= 1e-4 for k in ("pitch", "yaw", "roll"))
    if isinstance(want, float):
        try: return abs(float(cur) - want) <= 1e-4
        except Exception: return False
    return cur == want


def apply_spec(unreal, holder, spec, dry):
    """Записать целевые числа в структуру FxLight держателя. Возвращает список изменённых полей."""
    fx = holder.get_editor_property("fx_light")
    changed = []
    # ★ ЗАМОК: правил руками — не трогаем ничего
    manual, _ = get_prop(fx, "bManual")
    if manual: return None
    for cf, v in spec.items():
        if cf == "bManual": continue          # замок ставит только владелец, генератор его не трогает
        want = to_unreal_value(unreal, cf, v)
        cur, _ = get_prop(fx, cf)
        if cur is None: continue
        if same_value(cur, want, cf): continue
        if not dry and set_prop(fx, cf, want) is None: continue   # поле не пишется — не считаем изменением
        changed.append(cf)
    if changed and not dry:
        # структура в python — КОПИЯ (грабля: перебор unreal.Array/структур отдаёт копии), обязана быть записана назад
        holder.set_editor_property("fx_light", fx)
    return changed


# ─────────────── бэкап и откат ───────────────

def backup_file(disk_path):
    """Копия файла в _backup/2026-09-09_fx_f2all/<путь относительно проекта>, со сверкой размера. Один раз.
    ★ 11.10.2026 (ревизия wave0_REVIEW.md, С4): набор 09.09 — точка `rollback()`; ТЕКУЩЕЕ состояние перед каждой записью
    снимает общий `asda_headless_tool.backup_file` (набор <сегодня>_fx_light_defaults, при отличии — снимок .bak)."""
    _backup_current(disk_path)
    rel = os.path.relpath(disk_path, PROJ)
    dst = os.path.join(BACKUP, rel)
    if os.path.isfile(dst):
        return dst
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(disk_path, dst)
    a, b = os.path.getsize(disk_path), os.path.getsize(dst)
    if a != b:
        raise RuntimeError("бэкап разошёлся по размеру: %s (%d vs %d)" % (rel, a, b))
    return dst


def rollback():
    """Вернуть все файлы из бэкапа поверх проекта. Работает без UE."""
    n = 0
    for root, _dirs, files in os.walk(os.path.join(BACKUP, "Content")):
        for f in files:
            src = os.path.join(root, f)
            rel = os.path.relpath(src, BACKUP)
            dst = os.path.join(PROJ, rel)
            if not os.path.isfile(dst):
                log("ОТКАТ: в проекте нет %s — пропущен" % rel); continue
            shutil.copy2(src, dst)
            if os.path.getsize(src) != os.path.getsize(dst):
                raise RuntimeError("откат разошёлся по размеру: %s" % rel)
            n += 1
    log("ОТКАТ: возвращено файлов: %d" % n)
    return n


# ─────────────── обход ассетов ───────────────

def disk_path_of(unreal, package_path):
    """/Game/A/B → <проект>/Content/A/B.uasset|.umap (какой существует)."""
    rel = package_path[len("/Game/"):]
    for ext in (".uasset", ".umap"):
        p = os.path.join(CONTENT, rel.replace("/", os.sep) + ext)
        if os.path.isfile(p): return p
    return None


def jeb_of_asset(asset_name, cdo):
    """Имя .jeb: поле EffectName, а если пусто — база имени BP_Fx_<jeb> (та же логика, что в C++ ResolveJebName)."""
    try:
        en = cdo.get_editor_property("effect_name")
        if en: return str(en)
    except Exception:
        pass
    # ★ ИСПРАВЛЕНИЕ D4 (волна 2 п. 7, 14.09.2026): было срезание «BP_Fx_»/«_C» без возврата дефиса имени .jeb; теперь —
    #   как C++ AsdaAssetPaths::JebFromFxClassName (дефис по таблице реальных имён, регистр префикса/суффикса не важен).
    return _U.jeb_from_fx_class_name(asset_name)


def process_blueprints(unreal, table, dry):
    stat = {"всего": 0, "изменено": 0, "уже совпадало": 0, "ручные (пропущено)": 0, "без семейства": 0, "ошибок": 0}
    by_family = {}
    paths = unreal.EditorAssetLibrary.list_assets(EFFECTS_DIR, recursive=True, include_folder=False)
    paths = [p for p in paths if "/BP_Fx_" in p]
    if LIMIT: paths = paths[:LIMIT]
    log("ассетов BP_Fx_*: %d" % len(paths))
    for i, p in enumerate(paths):
        pkg = p.split(".")[0]
        try:
            bp = unreal.load_asset(pkg)
            if bp is None: stat["ошибок"] += 1; continue
            gen = bp.generated_class()
            cdo = unreal.get_default_object(gen)
            if cdo is None or not isinstance(cdo, unreal.AsdaEffectActor):
                stat["ошибок"] += 1; continue
            name = pkg.rsplit("/", 1)[-1]
            jeb = jeb_of_asset(name, cdo)
            fid, spec = match_family(table, jeb)
            stat["всего"] += 1
            if spec is None:
                stat["без семейства"] += 1
                log("   БЕЗ СЕМЕЙСТВА: %s (.jeb %r)" % (name, jeb))
                continue
            by_family[fid] = by_family.get(fid, 0) + 1
            changed = apply_spec(unreal, cdo, spec, dry)
            if changed is None:
                stat["ручные (пропущено)"] += 1
                continue
            if not changed:
                stat["уже совпадало"] += 1
                continue
            stat["изменено"] += 1
            if not dry:
                dp = disk_path_of(unreal, pkg)
                if dp: backup_file(dp)
                unreal.EditorAssetLibrary.save_asset(pkg, only_if_is_dirty=False)
            if stat["изменено"] <= 8 or stat["изменено"] % 200 == 0:
                log("   %s → %s: %s" % (name, fid, ", ".join(changed[:6]) + ("…" if len(changed) > 6 else "")))
        except Exception as ex:
            stat["ошибок"] += 1
            log("   ОШИБКА на %s: %s" % (p, ex))
        if (i + 1) % 200 == 0:
            log("   … обработано %d / %d" % (i + 1, len(paths)))
    return stat, by_family


def md5(path):
    """MD5 файла. Нужен потому, что размер уровня после правки почти всегда совпадает с прежним.
    Тело — общий `asda_headless_tool.md5_file` (дедуп №2 Т19; ленивый импорт, как у _backup_current)."""
    import asda_headless_tool as HT
    return HT.md5_file(path)


def _backup_current(disk_path):
    """★ С4 (11.10.2026): бэкап ТЕКУЩЕГО состояния перед записью — общий `asda_headless_tool.backup_file` (сверка MD5)."""
    import asda_headless_tool as HT
    if HT.backup_file(disk_path, "fx_light_defaults", verify="md5", log=lambda _m: None) is None:
        raise RuntimeError("нет %s — бэкап не снят, запись запрещена" % disk_path)


def backup_level(disk_path):
    """★ Копия уровня ДО правки в _backup/2026-09-10_fx_maps/<путь относительно проекта> со сверкой MD5.
    Размеры у .umap часто совпадают, поэтому сверка размера тут недостаточна. Существующую копию
    не перетираем: первая копия = состояние ДО волны. ★ С4: текущее состояние — `_backup_current` перед каждой записью."""
    _backup_current(disk_path)
    rel = os.path.relpath(disk_path, PROJ)
    dst = os.path.join(BACKUP_MAPS, rel)
    if os.path.isfile(dst):
        return "бэкап уже есть"
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(disk_path, dst)
    if os.path.getsize(dst) != os.path.getsize(disk_path) or md5(dst) != md5(disk_path):
        raise RuntimeError("бэкап %s не сошёлся по размеру/MD5" % rel)
    return "бэкап сверен (%d б, md5 %s)" % (os.path.getsize(dst), md5(dst)[:12])


def save_level_verified(unreal, name, disk_path, before_md5):
    """Сохранить текущий уровень и УБЕДИТЬСЯ ПО ФАЙЛУ, что байты изменились.

    ★★★ Грабля раската ореола 09.09.2026: save_current_level() молча не пишет, если .umap занят другим
    процессом редактора («MoveFile … Error Code 32», LogSavePackage: Error saving), а проверка «по актору
    в памяти» этого не видит — актор-то правильный. Поэтому признак успеха — ТОЛЬКО смена MD5 файла.
    Perforce держит файлы read-only: без снятия флага запись тоже проваливается молча."""
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    for attempt in (1, 2, 3):
        if not os.access(disk_path, os.W_OK):
            os.chmod(disk_path, stat_mod.S_IWRITE | stat_mod.S_IREAD)
            log("   %s: снят read-only" % name)
        les.save_current_level()
        if md5(disk_path) != before_md5:
            return True
        unreal.log_warning("[дефолты света] %s: попытка %d — файл уровня НЕ изменился (сохранение не прошло), повторяю"
                           % (name, attempt))
    unreal.log_error("[дефолты света] %s: СОХРАНЕНИЕ НЕ ПРОШЛО — MD5 уровня не изменился (см. LogSavePackage выше)"
                     % name)
    return False


def process_maps(unreal, table, dry):
    """Эффект-акторы, РАЗМЕЩЁННЫЕ на картах (.umap): те же дефолты «Свет эффекта», что и у BP_Fx_*.

    Учёт охвата ведётся ПОШТУЧНО и печатается по каждой карте: сколько эффект-акторов найдено, скольким
    проставлены поля, сколько уже совпадало, сколько заперто ручной правкой и сколько не легло ни в одно
    семейство (с именами .jeb) — «молчаливых» пропусков быть не должно."""
    stat = {"карт": 0, "карт с эффектами": 0, "акторов": 0, "изменено": 0, "уже совпадало": 0,
            "ручные (пропущено)": 0, "без семейства": 0, "карт сохранено": 0, "карт НЕ сохранилось": 0}
    per_map = []
    no_family = {}
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    maps = unreal.EditorAssetLibrary.list_assets(MAPS_DIR, recursive=True, include_folder=False)
    seen = []
    for p in sorted(set(maps)):
        pkg = p.split(".")[0]
        dp = disk_path_of(unreal, pkg)
        if dp and dp.endswith(".umap") and (pkg, dp) not in seen: seen.append((pkg, dp))
    log("карт: %d" % len(seen))
    for pkg, dp in seen:
        name = pkg.rsplit("/", 1)[-1]
        # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА (13.09.2026; обход найден 15.09.2026).
        # ★ 16.09.2026 (ревизия §7 savedir_guard_2026-09-16.md, СРЕДН-2): БЫЛО `pkg == "/Game/Map/Login"` и НЕ ТА
        #   переменная — судьбу УРОВНЯ решал ASDA_ALLOW_LOGIN_WRITE, хотя им открывают АССЕТЫ сцены логина, а для
        #   самих уровней заведён отдельный ключ ASDA_ALLOW_LOGIN_MAP_WRITE (его же требует C++-сторож
        #   AsdaLoginSaveGuard в автоматической сессии). Плюс литерал по уроку false-negatives-method не узнаёт ни
        #   /Game/Map/Login/<что угодно>, ни Login_Shine, ни иных форм записи пути, а цикл идёт по ВСЕМ .umap
        #   под /Game/Map. Теперь — общая login_map_write_blocked (она же печатает строку [LOGIN-GUARD]).
        if _LG.login_map_write_blocked(pkg, "asda_apply_fx_light_defaults.py"):
            continue
        try:
            les.load_level(pkg)
        except Exception as ex:
            log("   карта %s не открылась: %s" % (pkg, ex)); continue
        stat["карт"] += 1
        # MD5 снимаем ДО первой правки: это опорная точка для проверки, что сохранение реально прошло.
        before = md5(dp)
        m = {"карта": name, "акторов": 0, "изменено": 0, "уже совпадало": 0, "ручные": 0, "без семейства": 0,
             "сохранено": "не требовалось"}
        for a in eas.get_all_level_actors():
            if not isinstance(a, unreal.AsdaEffectActor): continue
            stat["акторов"] += 1; m["акторов"] += 1
            jeb = jeb_of_asset(a.get_class().get_name(), a)
            fid, spec = match_family(table, jeb)
            if spec is None:
                stat["без семейства"] += 1; m["без семейства"] += 1
                no_family[jeb] = no_family.get(jeb, 0) + 1
                continue
            changed = apply_spec(unreal, a, spec, dry)
            if changed is None:
                stat["ручные (пропущено)"] += 1; m["ручные"] += 1; continue
            if not changed:
                stat["уже совпадало"] += 1; m["уже совпадало"] += 1; continue
            stat["изменено"] += 1; m["изменено"] += 1
        if m["акторов"]:
            stat["карт с эффектами"] += 1
        if m["изменено"] and not dry:
            log("   %s: %s" % (name, backup_level(dp)))
            if save_level_verified(unreal, name, dp, before):
                stat["карт сохранено"] += 1
                m["сохранено"] = "MD5 сменился: %s → %s" % (before[:12], md5(dp)[:12])
            else:
                stat["карт НЕ сохранилось"] += 1
                m["сохранено"] = "★ НЕ СОХРАНИЛОСЬ"
                FAILED_LEVELS.append(name)
        elif m["изменено"] and dry:
            m["сохранено"] = "dry-run"
        per_map.append(m)
        log("   %-20s акторов %3d | проставлено %3d | уже совпадало %3d | ручных %2d | без семейства %2d | %s"
            % (name, m["акторов"], m["изменено"], m["уже совпадало"], m["ручные"], m["без семейства"],
               m["сохранено"]))
    if no_family:
        log("★ БЕЗ СЕМЕЙСТВА (имена .jeb и сколько раз встретились на картах):")
        for jeb, n in sorted(no_family.items(), key=lambda kv: -kv[1]):
            log("     %-40s ×%d" % (jeb, n))
    return stat
    return stat


def main():
    if MODE == "rollback":
        rollback()
        log("ЗАВЕРШЕНО")
        return
    import unreal
    dry = (MODE == "check")
    table = build_family_table()
    log("режим=%s что=%s семейств=%d" % (MODE, WHAT, len(table)))

    # ★★★ ГЕЙТ ЗАМКА. Наша собственная запись структуры в CDO приходит в
    #   AAsdaEffectActor::PostEditChangeProperty как обычная правка и взводит FxLight.bManual —
    #   проверено 09.09.2026: первый прогон запер от себя же все 1274 ассета («ручные=1274» на повторе).
    #   На время работы поднимаем asda.Fx.Light.NoManualLock, после — опускаем обратно.
    def gate(v):
        try:
            w = unreal.EditorLevelLibrary.get_editor_world()
        except Exception:
            w = None
        try:
            unreal.SystemLibrary.execute_console_command(w, "asda.Fx.Light.NoManualLock %d" % v)
            log("гейт замка asda.Fx.Light.NoManualLock = %d" % v)
        except Exception as ex:
            log("★ ВНИМАНИЕ: гейт замка не выставлен (%s) — возможен ложный bManual" % ex)

    if not dry: gate(1)
    try:
        run_all(unreal, table, dry)
    finally:
        if not dry: gate(0)
    # ★ Маркер ПОСЛЕ опускания гейта: запускатель добивает процесс, как только увидит его в логе
    log("ЗАВЕРШЕНО")


def run_all(unreal, table, dry):
    if WHAT in ("bp", "both"):
        stat, by_family = process_blueprints(unreal, table, dry)
        log("ИТОГО BP_Fx_*: " + ", ".join("%s=%d" % kv for kv in stat.items()))
        if UNWRITABLE:
            log("★ НЕ ЗАПИСАЛИСЬ поля (read-only в UE?): %s" % ", ".join(sorted(UNWRITABLE)))
        for fid, n in sorted(by_family.items(), key=lambda kv: -kv[1]):
            log("   семейство %-22s ассетов %4d" % (fid, n))
    if WHAT in ("maps", "both"):
        stat = process_maps(unreal, table, dry)
        log("ИТОГО карты: " + ", ".join("%s=%d" % kv for kv in stat.items()))
        # ★ Маркер успеха печатаем ТОЛЬКО когда запись подтверждена по MD5 у каждой правленой карты.
        #   Иначе запускатель посчитал бы провалившееся сохранение успехом — ровно та ошибка, на которой
        #   раскат ореола 09.09.2026 отрапортовал «записано» при упавшем сохранении alpen.
        if FAILED_LEVELS:
            unreal.log_error("[дефолты света] уровни, где сохранение НЕ подтвердилось: %s"
                             % ", ".join(FAILED_LEVELS))
        else:
            unreal.log(MARKER_MAPS)


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
