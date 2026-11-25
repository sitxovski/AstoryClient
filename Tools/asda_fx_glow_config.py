# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_glow_config.py — ★ 13.09.2026: СЛОЙ ДАННЫХ «КОНФИГУРАТОРА СВЕЧЕНИЯ ASDA»
#   (редактор: Tools → Asda → «Конфигуратор свечения Asda», плагин AsdaItemBrowser, SAsdaFxGlowConfigurator).
#
#   ЗАЧЕМ ОТДЕЛЬНЫЙ PYTHON, А НЕ ЗАПИСЬ ИЗ C++. Единственный источник чисел свечения — Tools/asda_fx_light.json,
#   секцию Config/DefaultGame.ini из него ГЕНЕРИРУЕТ Tools/asda_gen_fx_light_ini.py (там же — сведение
#   наследования и проверка --check). Писать JSON из C++ значило бы завести ВТОРУЮ реализацию таблицы полей,
#   перечислений и расчёта кандел и разойтись с генератором при первой же его правке (закон port-shared-points).
#   Здесь всё берётся ИЗ МОДУЛЯ ГЕНЕРАТОРА: SPEC_FIELDS, ENUM_MAPS, family_spec, override_spec, render_game_ini.
#
#   Вызов (редактор делает это сам через встроенный Python; руками — обычным python):
#     python Tools/asda_fx_glow_config.py export <out.json>          — состояние для конфигуратора
#     python Tools/asda_fx_glow_config.py apply <ops.json> <res.json> — применить правки → JSON + ini (атомарно)
#     python Tools/asda_fx_glow_config.py selftest                    — проверки ревизии 13.09 в памяти (файлы не пишет)
#     python Tools/asda_fx_glow_config.py types                       — ★ Ф11 (К1): таблица «тип поля → все 5 потребителей»
#
#   Формат ops.json: {"ops": [ {"kind": "family", "id": "weapon_glow", "set": {"IntensityCd": 2.0}, "reset": ["RadiusMax"]},
#                              {"kind": "override", "jeb": "ohs_a_co_ma_03", "item_id": 20600, "label": "…",
#                               "set": {...}, "reset": [...], "remove": false} ]}
#   Ключи "set"/"reset" — ИМЕНА ПОЛЕЙ C++ (FAsdaFxLightSpec), значения — как их отдаёт C++: bool, число,
#   короткое имя элемента перечисления («Bounds»), цвет [r,g,b], вектор [x,y,z].
#
#   Результат apply (res.json): written — оба файла записаны; ok — записаны и сверка чиста; warning — записано,
#   но есть предупреждение (сверка бюджетов DefaultScalability.ini); error — причина отказа (при written=false
#   ни один файл не изменён: запись идёт после всех проверок, через временные файлы, со взаимным откатом).
#
#   Закон tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import os, sys, re, json, shutil, datetime, importlib, stat

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(TOOLS_DIR)
BOOST_JSON = os.path.join(TOOLS_DIR, "asda_fx_boost.json")


def _gen():
    """Модуль генератора. reload — редактор держит модули между вызовами, правка генератора обязана подхватываться."""
    if TOOLS_DIR not in sys.path:
        sys.path.insert(0, TOOLS_DIR)
    import asda_gen_fx_light_ini as G
    return importlib.reload(G)


def _fields(G):
    """{поле C++: (ключ JSON, тип)} по SPEC_FIELDS генератора."""
    return {cf: (jk, t) for jk, cf, t in G.SPEC_FIELDS}


def _to_cpp(G, t, v):
    """Значение JSON → вид, в котором его понимает конфигуратор (C++)."""
    if v is None: return None
    if t == "b": return bool(v)
    if t == "f": return round(float(v), 6)
    if t == "i": return int(v)
    if t in G.ENUM_MAPS: return G.ENUM_MAPS[t][str(v).lower()]
    if t == "c": return [int(x) for x in list(v)[:3]]
    # ★★★ РЕВИЗИЯ Ф11 (К1, 20.09.2026): ПОВОРОТ («r») РАЗБИРАЛСЯ НАРАВНЕ С ЛЮБЫМ МУСОРОМ.
    #   Волна Ф11 завела в SPEC_FIELDS три поля FRotator (RectRotation, SpotRotation, ShapeRotation)
    #   и тип "r" в генераторе, а здесь про него не написала НИЧЕГО: _to_cpp отдавал значение как есть
    #   (то есть сырой список из JSON — конфигуратор молча не применял его к FRotator-свойству),
    #   а _to_json падал на `raise ValueError(t)` в самом низу. Итог: первая же правка поворота в
    #   «Конфигураторе свечения» роняла ВЕСЬ пакет операций сохранения — ни один файл не записывался.
    #   Порядок трёх чисел — [Pitch, Yaw, Roll], ровно как их показывает панель деталей UE и как их
    #   пишет генератор (_fmt, тип "r": именованные поля Pitch=/Yaw=/Roll=, перепутать нельзя).
    if t in ("v", "r"): return [round(float(x), 6) for x in list(v)[:3]]
    return v


def _to_json(G, t, v):
    """Значение от C++ → значение JSON таблицы (перечисление — ключ JSON, числа без хвоста float→double)."""
    # ★★★ РЕВИЗИЯ fix3 (М-5, 20.09.2026): «ЗНАЧЕНИЯ НЕТ» — ЭТО ОТДЕЛЬНЫЙ ОТКАЗ СО СВОИМ ТЕКСТОМ.
    #   Ревизия fix2 научила ValueJson отдавать null у структуры, которой он не знает (до неё уходил
    #   пустой массив — и поворот молча записывался нулями). Но сюда null приходил как есть, и
    #   первая же операция над ним давала «TypeError: 'NoneType' object is not iterable» — отказ
    #   без единого слова о том, КАКОЕ поле и ПОЧЕМУ не прочиталось, притом валящий ВЕСЬ пакет
    #   операций (_apply_one зовёт _to_json на каждое заданное поле). Говорим прямо.
    if v is None:
        raise ValueError("конфигуратор не отдал значение поля типа %r (структура ему не известна — "
                         "поддержи её в ValueJson/ValueText/ValueFromJson/MakeValueWidget, см. "
                         "'python Tools/asda_fx_glow_config.py types')" % (t,))
    if t == "b": return bool(v)
    if t == "f": return round(float(v), 6)
    if t == "i": return int(v)
    if t in G.ENUM_MAPS:
        inv = {cpp.lower(): jk for jk, cpp in G.ENUM_MAPS[t].items()}
        key = str(v).lower()
        if key not in inv:
            raise ValueError("неизвестное значение %r (допустимы %s)" % (v, sorted(G.ENUM_MAPS[t].values())))
        return inv[key]
    if t == "c": return [max(0, min(255, int(x))) for x in list(v)[:3]]
    # ★ Ревизия Ф11 (К1): «r» — ПОВОРОТ, три числа [Pitch, Yaw, Roll] (см. довод в _to_cpp).
    #   Проверяем длину явно: C++ до этой правки отдавал ПУСТОЙ массив, и «тихо принять» его значило бы
    #   записать в таблицу поворот (0,0,0) вместо заданного владельцем — то есть соврать в файле.
    if t in ("v", "r"):
        a = list(v)
        if len(a) < 3:
            raise ValueError("для типа %r нужно три числа, пришло %r (конфигуратор не отдал значение)" % (t, v))
        return [round(float(x), 6) for x in a[:3]]
    raise ValueError(t)


def _same(G, t, a, b):
    """Равны ли два значения JSON С ТОЧНОСТЬЮ ЗАПИСИ В INI (сравнение текстом _fmt, без шума float)."""
    if a is None or b is None: return a is None and b is None
    try:
        return G._fmt(a, t) == G._fmt(b, t)
    except Exception:
        return False


def _family_default(G, rules, fam, jk, cf):
    """Значение поля семейства, если убрать из него ключ jk (что даст «сброс к дефолту»)."""
    tmp = dict(fam)
    tmp.pop(jk, None)
    spec = G.family_spec(tmp, rules.get("defaults", {}), float(rules["reference_sun_lux"]))
    return spec.get(cf)


def _override_default(G, rules, ov, jk, cf):
    """Значение поля переопределения, если убрать из него ключ jk (наследование от эффекта/семейства)."""
    tmp = dict(ov)
    tmp.pop(jk, None)
    spec, _, _ = G.override_spec(rules, tmp)
    return spec.get(cf)


def export_state(out_path):
    """Состояние для конфигуратора: список полей, семейства (свои ключи + дефолты), переопределения, таблица буста."""
    G = _gen()
    rules = G.load_rules()
    F = _fields(G)
    out = {"fields": [[jk, cf, t] for jk, cf, t in G.SPEC_FIELDS],
           "rules_path": G.RULES_PATH, "ini_path": G.GAME_INI,
           "families": [], "overrides": [], "boost": []}
    for fam in rules["families"]:
        # ★ Кандела семейства, заданная ОТНОСИТЕЛЬНО (intensity_rel_sun · опора · d_ref²), «своей» не помечается:
        #   сбрасывать там нечего — ключа intensity_cd нет, и дефолт совпадает с текущим значением
        own = [cf for cf, (jk, t) in F.items() if jk in fam]
        defaults = {}
        for cf, (jk, t) in F.items():
            defaults[cf] = _to_cpp(G, t, _family_default(G, rules, fam, jk, cf))
        out["families"].append({"id": fam["id"], "jeb": fam["jeb"], "doc": fam.get("doc", ""), "comment": fam.get("comment", ""),
                                "own": own, "defaults": defaults})
    for ov in rules.get("overrides", []):
        own = [cf for cf, (jk, t) in F.items() if jk in ov]
        defaults = {}
        for cf, (jk, t) in F.items():
            defaults[cf] = _to_cpp(G, t, _override_default(G, rules, ov, jk, cf))
        fam = G.find_family(rules, ov["jeb"])
        out["overrides"].append({"jeb": ov["jeb"], "item_id": int(ov.get("item_id", 0)), "label": ov.get("label", ""),
                                 "family": fam["id"] if fam else "other", "own": own, "defaults": defaults})
    try:
        boost = json.load(open(BOOST_JSON, encoding="utf-8"))
        for f in boost.get("families", []):
            out["boost"].append({"id": f["id"], "jeb": f["jeb"], "modes": f.get("modes", []),
                                 "boost": float(f.get("boost", boost.get("defaults", {}).get("boost", 1.0)))})
    except Exception as ex:
        out["boost_error"] = str(ex)
    with open(out_path, "w", encoding="utf-8") as fh:
        json.dump(out, fh, ensure_ascii=False, indent=1)
    return 0


def _backup(paths):
    """Бэкап до правки (закон backup-before-edit): первая копия дня — `asda_headless_tool.backup_file` (★ Т3, 11.10.2026),
    набор @@ASTORY_DEV_ROOT_ESC@@\\_backup\\<дата>_fx_configurator\\<путь от @@ASTORY_DEV_ROOT_ESC@@> (был AstoryClient\\_backup\\…\\<путь от проекта>).
    Журнал функции молчит (stdout конфигуратора — ответ JSON); провал сверки — SystemExit, как прежде. Возвращает папку набора."""
    import asda_headless_tool as HT
    day = datetime.date.today().isoformat()
    for p in paths:
        try:
            HT.backup_file(p, "fx_configurator", date=day, log=lambda _m: None)
        except RuntimeError as e:
            raise SystemExit("бэкап %s: %s" % (p, e))
    return os.path.join(HT.BACKUP_ROOT, "%s_fx_configurator" % day)


def _op_rank(op):
    """★ Ревизия 13.09 (п.1): порядок применения НЕ зависит от порядка правок в редакторе —
    сначала семейства, затем записи эффектов (item_id 0), затем записи предметов: дефолт предмета считается
    от ИТОГОВОГО эффекта."""
    if op.get("kind") == "family":
        return 0
    return 1 if int(op.get("item_id", 0)) == 0 else 2


def _apply_one(G, rules, F, op):
    """Одна операция над rules (в памяти). Возвращает строку отчёта."""
    kind = op.get("kind")
    sets = op.get("set", {}) or {}
    resets = list(op.get("reset", []) or [])
    for cf in list(sets.keys()) + resets:
        if cf not in F:
            raise ValueError("поле %r не входит в таблицу полей генератора" % cf)
    if kind == "family":
        fam = next((f for f in rules["families"] if f["id"] == op.get("id")), None)
        if fam is None:
            raise ValueError("семейства %r нет" % op.get("id"))
        for cf in resets:
            # сброс канделы возвращает относительную формулу семейства (intensity_rel_sun · опора · d_ref²)
            fam.pop(F[cf][0], None)
        for cf, v in sets.items():
            jk, t = F[cf]
            val = _to_json(G, t, v)
            if jk == "intensity_cd":
                scale = float(fam.get("intensity_scale", rules.get("defaults", {}).get("intensity_scale", 1.0)))
                if scale and abs(scale - 1.0) > 1e-9:
                    val = round(float(val) / scale, 6)   # генератор умножит на калибровочный множитель семейства обратно
            if jk not in G.EMISSIVE_KEYS and _same(G, t, _family_default(G, rules, fam, jk, cf), val):
                fam.pop(jk, None)                          # равно дефолту — ключ не нужен, семейство наследует defaults
            else:
                fam[jk] = val
        return "семейство %s: задано %d, сброшено %d" % (fam["id"], len(sets), len(resets))
    if kind == "override":
        jeb = str(op.get("jeb", "")).strip()
        item_id = int(op.get("item_id", 0))
        ovs = rules.setdefault("overrides", [])
        ov = G.find_override(rules, jeb, item_id)
        if op.get("remove"):
            if ov is not None:
                ovs.remove(ov)
            return "переопределение %s / %d: удалено" % (jeb, item_id)
        if ov is None:
            ov = {"jeb": jeb, "item_id": item_id}
            ovs.append(ov)
        if op.get("label"):
            ov["label"] = str(op["label"])
        fam = G.find_family(rules, jeb)
        ov["family"] = fam["id"] if fam else "other"
        for cf in resets:
            ov.pop(F[cf][0], None)
        for cf, v in sets.items():
            jk, t = F[cf]
            val = _to_json(G, t, v)
            # ★ Ревизия 13.09 (п.1): у ПРЕДМЕТА значение, поставленное владельцем, хранится ЯВНО всегда — даже если
            #   сейчас совпадает с наследуемым: иначе следующая правка записи эффекта молча утянула бы и его.
            #   У записи ЭФФЕКТА ключ, равный семейству, не хранится (семейство и так даёт то же число).
            #   Эмиссию не схлопываем никогда: «не задано» ≠ «задано как у материала».
            if item_id == 0 and jk not in G.EMISSIVE_KEYS and _same(G, t, _override_default(G, rules, ov, jk, cf), val):
                ov.pop(jk, None)
            else:
                ov[jk] = val
        # Сила свечения без флага бессмысленна, флаг без числа — тоже: держим пару вместе
        if "emissive_boost" in ov and "override_emissive" not in ov:
            ov["override_emissive"] = True
        if ov.get("override_emissive") is False:
            ov.pop("override_emissive", None); ov.pop("emissive_boost", None)
        if not any(k in G.SPEC_JSON_KEYS for k in ov):
            ovs.remove(ov)                      # ни одного своего ключа — записи нет
            return "переопределение %s / %d: совпало с наследуемым, запись снята" % (jeb, item_id)
        return "переопределение %s / %d: задано %d, сброшено %d" % (jeb, item_id, len(sets), len(resets))
    raise ValueError("неизвестная операция %r" % kind)


def apply_to_rules(G, rules, ops):
    """Все операции над rules (в памяти) в порядке семейства → эффекты → предметы. Возвращает журнал."""
    F = _fields(G)
    log = [_apply_one(G, rules, F, op) for op in sorted(ops, key=_op_rank)]
    if "overrides" in rules:
        rules["overrides"] = sorted(rules["overrides"], key=lambda o: (str(o["jeb"]).lower(), int(o.get("item_id", 0))))
    return log


# ═══ ★ Ревизия 13.09 (п.9): JSON ПИШЕТСЯ ТОЛЬКО РАЗДЕЛОМ overrides — остальной текст файла не трогается ═══════
#   Раздел всегда держится ПОСЛЕДНИМ ключом корня; пустой раздел не пишется вовсе. Тогда «сохранить → удалить
#   запись» возвращает файл побайтно. Если раздел стоит не последним (правили руками) — честный полный дамп.
_OV_KEY_RE = re.compile(r',\s*"overrides"\s*:\s*')


def _strip_overrides(t):
    """Текст без раздела overrides (если он последний ключ корня) или None — раздела нет в ожидаемом месте."""
    dec = json.JSONDecoder()
    for m in reversed(list(_OV_KEY_RE.finditer(t))):
        try:
            _, end = dec.raw_decode(t, m.end())
        except ValueError:
            continue
        rest = t[end:]
        if re.fullmatch(r"\s*\}\s*", rest):
            return t[:m.start()] + "\n}" + rest[rest.index("}") + 1:]
    return None


def _redump_base(t, base_new):
    """Основа файла (без overrides) с новыми данными в ТОМ ЖЕ стиле отступов, что и прежний текст t, или None —
    стиль не распознан (тогда вызывающий пишет честный полный дамп). Стиль — json.dumps(ensure_ascii=False, indent=N)."""
    old = json.loads(t)
    for ind in (1, 2, 4, "\t"):
        if json.dumps(old, ensure_ascii=False, indent=ind) == t.rstrip("\n"):
            return json.dumps(base_new, ensure_ascii=False, indent=ind) + "\n"
    return None


def render_rules_json(raw, rules):
    """Новые байты asda_fx_light.json: прежний текст, в котором заменён только раздел overrides."""
    bom = raw.startswith(b"\xef\xbb\xbf")
    text = raw.decode("utf-8-sig")
    nl = "\r\n" if "\r\n" in text else "\n"
    t = text.replace("\r\n", "\n")
    if '"overrides"' in t:
        s = _strip_overrides(t)
        t = s if s is not None else None
    if t is not None:
        # ★ 18.10.2026: правки СЕМЕЙСТВ (и любых ключей корня вне overrides) прежде терялись — текст брался старый,
        #   сверка в apply_ops ловила расхождение и отменяла запись («текст JSON после записи раздела overrides не совпал
        #   с данными»). Конфигуратор не мог сохранить ни одной правки семейства. Теперь основа пересобирается в стиле файла.
        base_new = {k: v for k, v in rules.items() if k != "overrides"}
        if json.loads(t) != base_new:
            t = _redump_base(t, base_new)
    ovs = rules.get("overrides") or []
    if t is None:
        # раздел не последним ключом — формат сохранить нельзя, пишем полный дамп (содержимое проверяется ниже)
        plain = dict(rules)
        if not ovs:
            plain.pop("overrides", None)
        t = json.dumps(plain, ensure_ascii=False, indent=2) + "\n"
    elif ovs:
        idx = t.rstrip().rfind("}")
        block = json.dumps(ovs, ensure_ascii=False, indent=2).replace("\n", "\n  ")
        t = t[:idx].rstrip() + ',\n  "overrides": ' + block + "\n}" + t[idx + 1:]
    data = t.replace("\n", nl).encode("utf-8")
    return (b"\xef\xbb\xbf" + data) if bom else data


def _check_writable(path):
    """Отказ ДО записи, если файл нельзя переписать (read-only из Perforce и т.п.)."""
    st = os.stat(path)
    ro = not (st.st_mode & stat.S_IWRITE) or (getattr(st, "st_file_attributes", 0) & 0x1)
    if ro or not os.access(path, os.W_OK):
        raise PermissionError("файл только для чтения: %s (Perforce: сделайте Checkout)" % path)
    if not os.access(os.path.dirname(path), os.W_OK):
        raise PermissionError("нет права записи в каталог %s" % os.path.dirname(path))


def _write_atomic(path, data):
    """Запись через временный файл и замену: файл либо старый, либо новый целиком."""
    tmp = path + ".asda_tmp"
    with open(tmp, "wb") as fh:
        fh.write(data)
        fh.flush()
        os.fsync(fh.fileno())
    os.replace(tmp, path)


def apply_ops(ops_path, result_path):
    """Правки конфигуратора → JSON + ini атомарно (всё проверено в памяти до записи). Результат — JSON в result_path."""
    res = {"written": False, "ok": False, "warning": "", "error": "", "log": []}
    G = None
    json_raw = ini_raw = None
    json_done = False
    try:
        G = _gen()
        ops = json.load(open(ops_path, encoding="utf-8")).get("ops", [])
        json_raw = open(G.RULES_PATH, "rb").read()
        rules = json.loads(json_raw.decode("utf-8-sig"))
        res["log"] = apply_to_rules(G, rules, ops)
        if "overrides" in rules and not rules["overrides"]:
            rules.pop("overrides")
        # 1. Оба результата — в памяти, с проверкой
        new_json = render_rules_json(json_raw, rules)
        if json.loads(new_json.decode("utf-8-sig")) != rules:
            raise ValueError("текст JSON после записи раздела overrides не совпал с данными — запись отменена")
        new_ini, ini_raw = G.render_game_ini(rules)
        body = G.build_section(rules)
        _, cur, _ = G.split_section(new_ini.decode("utf-8-sig").replace("\r\n", "\n"))
        if cur is None or cur.strip() != body.strip():
            raise ValueError("секция ini после сборки не совпала с JSON — запись отменена")
        if new_json == json_raw and new_ini == ini_raw:
            res.update(written=True, ok=True)
            res["log"].append("изменений в файлах нет")
        else:
            # 2. Право записи обоих файлов — до первой записи
            _check_writable(G.RULES_PATH)
            _check_writable(G.GAME_INI)
            res["backup"] = _backup([G.RULES_PATH, G.GAME_INI])
            # 3. Запись со взаимным откатом
            if new_json != json_raw:
                _write_atomic(G.RULES_PATH, new_json)
                json_done = True
            try:
                if new_ini != ini_raw:
                    _write_atomic(G.GAME_INI, new_ini)
            except BaseException:
                if json_done:
                    _write_atomic(G.RULES_PATH, json_raw)
                    json_done = False
                raise
            res["written"] = True
            # 4. Сверка ПОСЛЕ записи: секция — ошибка данных; бюджеты Scalability — лишь предупреждение (они вне этой записи)
            _, cur2, _ = G.split_section(open(G.GAME_INI, encoding="utf-8-sig").read())
            same = cur2 is not None and cur2.strip().replace("\r", "") == body.strip()
            bad = G.check_scalability(rules)
            if bad:
                res["warning"] = "сверка DefaultScalability.ini [AsdaFxLight@N] расходится с JSON: " + "; ".join(bad)
            if not same:
                res["error"] = "записано, но секция ini не совпала с JSON при повторном чтении"
            res["ok"] = same
    except BaseException as ex:          # SystemExit генератора — тоже отказ, а не падение редактора
        res["error"] = "%s: %s" % (type(ex).__name__, ex)
    for p in ((G.RULES_PATH, G.GAME_INI) if G else ()):
        try:
            if os.path.exists(p + ".asda_tmp"): os.remove(p + ".asda_tmp")
        except OSError:
            pass
    with open(result_path, "w", encoding="utf-8") as fh:
        json.dump(res, fh, ensure_ascii=False, indent=1)
    return 0 if res["ok"] else 3


# ═══ ★★★ РЕВИЗИЯ Ф11 (К1, 20.09.2026): МАШИННАЯ СВЕРКА «ТИП ПОЛЯ → ВСЕ ЕГО ПОТРЕБИТЕЛИ» ═══════════
#
#   ЗАЧЕМ ОНА ПОЯВИЛАСЬ. Волна Ф11 дописала в SPEC_FIELDS тип "r" (FRotator) и ЗАБЫЛА про него в трёх
#   местах из пяти: здесь (_to_cpp/_to_json), в модели конфигуратора (ValueText/ValueJson/ValueFromJson)
#   и в самом виджете (MakeValueWidget трактовал ЛЮБУЮ структуру, кроме FColor, как FVector). Ни одна
#   сборка на это не ругнулась — потому что «поддержка типа» здесь не выражена типом, а размазана по
#   пяти независимым развилкам. Список потребителей ОБЯЗАН проверяться машиной, иначе следующий новый
#   тип потеряется ровно так же (закон check-existing-before-creating + false-negatives-method).
#
#   ПЯТЬ ПОТРЕБИТЕЛЕЙ ОДНОГО ТИПА:
#     1. генератор ini          Tools/asda_gen_fx_light_ini.py            _fmt(значение, тип)
#     2. слой данных (этот)     Tools/asda_fx_glow_config.py              _to_cpp / _to_json
#     3. раскат в ассеты        Tools/asda_apply_fx_light_defaults.py     to_unreal_value / same_value
#     4. модель конфигуратора   Plugins/…/AsdaFxGlowConfigModel.cpp       ValueText / ValueJson / ValueFromJson
#     5. виджет конфигуратора   Plugins/…/SAsdaFxGlowConfigurator.cpp     MakeValueWidget
#   Первые три проверяются ВЫЗОВОМ (значение реально прогоняется туда и обратно), последние два —
#   разбором тела функции: какие классы FProperty оно перечисляет.
CPP_MODEL = os.path.join(PROJ, "Plugins", "AsdaItemBrowser", "Source", "AsdaItemBrowser", "Private", "AsdaFxGlowConfigModel.cpp")
CPP_WIDGET = os.path.join(PROJ, "Plugins", "AsdaItemBrowser", "Source", "AsdaItemBrowser", "Private", "SAsdaFxGlowConfigurator.cpp")
SPEC_H = os.path.join(PROJ, "Source", "AsdaReborn", "Public", "World", "AsdaFxLightSpec.h")

# Тип таблицы → (образец значения, класс FProperty, которым поле видно из C++)
_TYPE_SAMPLE = {"b": True, "f": 1.5, "i": 3, "c": [10, 20, 30], "v": [1.0, 2.0, 3.0], "r": [10.0, 20.0, 30.0]}
_TYPE_PROP = {"b": "FBoolProperty", "f": "FFloatProperty", "i": "FIntProperty",
              "c": "TBaseStructure<FColor>", "v": "TBaseStructure<FVector>", "r": "TBaseStructure<FRotator>"}
# Объявленный тип C++ → тип таблицы (сверка «то ли вообще поле, что о нём думает таблица»)
_CPP_DECL = {"bool": "b", "float": "f", "int32": "i", "FColor": "c", "FVector": "v", "FRotator": "r"}


class _FakeUnreal:
    """Заглушка модуля `unreal` для сверки: to_unreal_value зовётся ВНЕ редактора, значит нужны его типы."""
    class Vector:
        def __init__(self, x, y, z): self.x, self.y, self.z = float(x), float(y), float(z)
    class Rotator:
        def __init__(self, roll=0.0, pitch=0.0, yaw=0.0): self.roll, self.pitch, self.yaw = float(roll), float(pitch), float(yaw)
    class Color:
        def __init__(self, b=0, g=0, r=0, a=255): self.b, self.g, self.r, self.a = int(b), int(g), int(r), int(a)

    class _Enum:
        def __init__(self, cls): self._cls = cls
        def __getattr__(self, name): return "%s::%s" % (self._cls, name)

    def __getattr__(self, name):        # любое перечисление (AsdaFxLightShape и т.п.)
        return _FakeUnreal._Enum(name)


def _cpp_func_body(text, sig_re):
    """Тело функции C++ по регэкспу её заголовка: от первой `{` до парной ей `}` (или '' — не нашли)."""
    m = re.search(sig_re, text)
    if not m:
        return ""
    i = text.find("{", m.end() - 1)
    if i < 0:
        return ""
    depth, j = 0, i
    while j < len(text):
        if text[j] == "{": depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0: return text[i:j + 1]
        j += 1
    return ""


def _spec_decl_types():
    """{поле C++: объявленный тип} из World/AsdaFxLightSpec.h (строка «<тип> <Имя> = …;» или «<тип> <Имя>;»)."""
    text = open(SPEC_H, encoding="utf-8-sig").read()
    out = {}
    for m in re.finditer(r"^\s*(?:TObjectPtr<)?([A-Za-z_][A-Za-z0-9_:]*)>?\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:=[^;]*)?;",
                         text, re.M):
        out.setdefault(m.group(2), m.group(1))
    return out


def check_types(verbose=True):
    """Сверка «тип → все пять потребителей». Возвращает список расхождений (пусто = всё сходится). Файлы не пишет."""
    G = _gen()
    if TOOLS_DIR not in sys.path:
        sys.path.insert(0, TOOLS_DIR)
    import asda_apply_fx_light_defaults as AP
    AP = importlib.reload(AP)
    bad = []
    decl = _spec_decl_types()
    model_txt = open(CPP_MODEL, encoding="utf-8-sig").read()
    widget_txt = open(CPP_WIDGET, encoding="utf-8-sig").read()
    bodies = {
        "ValueText":     _cpp_func_body(model_txt, r"FString\s+FAsdaFxGlowConfigModel::ValueText\s*\("),
        "ValueJson":     _cpp_func_body(model_txt, r"FAsdaFxGlowConfigModel::ValueJson\s*\("),
        "ValueFromJson": _cpp_func_body(model_txt, r"bool\s+FAsdaFxGlowConfigModel::ValueFromJson\s*\("),
        "MakeValueWidget": _cpp_func_body(widget_txt, r"SAsdaFxGlowConfigurator::MakeValueWidget\s*\("),
    }
    for nm, b in bodies.items():
        if not b:
            bad.append("тело функции %s не найдено — сверка типов невозможна (изменилась подпись?)" % nm)
    fake = _FakeUnreal()
    types = []
    for jk, cf, t in G.SPEC_FIELDS:
        if t not in types:
            types.append(t)
    rows = []
    for t in types:
        enum = t in G.ENUM_MAPS
        sample = sorted(G.ENUM_MAPS[t])[0] if enum else _TYPE_SAMPLE.get(t)
        prop = "FEnumProperty" if enum else _TYPE_PROP.get(t)
        fields = [cf for jk, cf, tt in G.SPEC_FIELDS if tt == t]
        cells = []
        # 1. генератор ini
        try:
            G._fmt(sample, t); cells.append("да")
        except Exception as ex:
            cells.append("НЕТ"); bad.append("тип %r: генератор _fmt не умеет (%s)" % (t, ex))
        # 2. этот модуль: JSON → C++ → JSON (значение обязано вернуться тем же)
        try:
            back = _to_json(G, t, _to_cpp(G, t, sample))
            cells.append("да")
            if not _same(G, t, back, sample):
                bad.append("тип %r: _to_cpp→_to_json не возвращает исходное (%r → %r)" % (t, sample, back))
        except Exception as ex:
            cells.append("НЕТ"); bad.append("тип %r: _to_cpp/_to_json не умеют (%s: %s)" % (t, type(ex).__name__, ex))
        # 3. раскат в ассеты: значение и ПОВТОРНОЕ сравнение (идемпотентность прогона по 1274 ассетам)
        ok3 = "да"
        for cf in fields:
            try:
                v1 = AP.to_unreal_value(fake, cf, sample)
                v2 = AP.to_unreal_value(fake, cf, sample)
            except Exception as ex:
                ok3 = "НЕТ"; bad.append("тип %r, поле %s: to_unreal_value не умеет (%s: %s)" % (t, cf, type(ex).__name__, ex)); break
            if isinstance(v1, str) and enum is False and t not in ("b", "f", "i"):
                ok3 = "НЕТ"; bad.append("тип %r, поле %s: to_unreal_value отдал голую строку %r" % (t, cf, v1)); break
            if not AP.same_value(v1, v2, cf):
                ok3 = "НЕТ"
                bad.append("тип %r, поле %s: same_value не узнаёт ОДНО И ТО ЖЕ значение — повторный прогон "
                           "перепишет все ассеты заново (идемпотентности нет)" % (t, cf))
                break
        cells.append(ok3)
        # 4 и 5. C++: тело функции обязано перечислять класс свойства этого типа
        for nm in ("ValueText", "ValueJson", "ValueFromJson", "MakeValueWidget"):
            b = bodies.get(nm, "")
            if not b:
                cells.append("?"); continue
            if prop and prop in b:
                cells.append("да")
            else:
                cells.append("НЕТ"); bad.append("тип %r (%s): %s его не разбирает" % (t, prop, nm))
        # Заодно: объявленный в C++ тип поля обязан совпадать с типом таблицы
        for cf in fields:
            d = decl.get(cf)
            if d is None:
                bad.append("поле %s: объявления нет в AsdaFxLightSpec.h" % cf)
            elif enum:
                if not d.startswith("EAsda"):
                    bad.append("поле %s: таблица считает его перечислением %r, а объявлено как %r" % (cf, t, d))
            elif _CPP_DECL.get(d) != t:
                bad.append("поле %s: объявлено как %r, а в таблице тип %r" % (cf, d, t))
        rows.append((t, prop, len(fields), cells))
    if verbose:
        hdr = ("тип", "класс FProperty", "полей", "ini", "py-слой", "ассеты", "ValueText", "ValueJson", "ValueFromJson", "виджет")
        print("%-10s %-24s %5s  %-4s %-7s %-6s %-9s %-9s %-13s %s" % hdr)
        for t, prop, n, cells in rows:
            print("%-10s %-24s %5d  %-4s %-7s %-6s %-9s %-9s %-13s %s" % ((t, prop or "?", n) + tuple(cells)))
        print("СВЕРКА ТИПОВ: %s" % ("всё сходится" if not bad else "РАСХОЖДЕНИЙ %d" % len(bad)))
        for l in bad:
            print("  " + l)
    return bad


def selftest():
    """Проверки ревизии 13.09 в памяти: п.1 (порядок операций) и п.9 (побайтный возврат JSON). Файлы не пишет.
    ★ Ревизия Ф11 (К1): сюда же добавлена машинная сверка «тип → все пять потребителей» (check_types)."""
    G = _gen()
    base_raw = open(G.RULES_PATH, "rb").read()
    fails = []
    item = {"kind": "override", "jeb": "ohs_a_co_ma_03", "item_id": 20600, "set": {"IntensityCd": 0.8}}
    eff = {"kind": "override", "jeb": "ohs_a_co_ma_03", "item_id": 0, "set": {"IntensityCd": 2.4}}
    for name, order in (("предмет→эффект", [item, eff]), ("эффект→предмет", [eff, item])):
        rules = json.loads(base_raw.decode("utf-8-sig"))
        apply_to_rules(G, rules, json.loads(json.dumps(order)))
        got = {int(o.get("item_id", 0)): G.override_spec(rules, o)[0]["IntensityCd"] for o in rules["overrides"]}
        print("п.1 %s: предмет %.3f кд, эффект %.3f кд" % (name, got.get(20600, -1), got.get(0, -1)))
        if abs(got.get(20600, -1) - 0.8) > 1e-6 or abs(got.get(0, -1) - 2.4) > 1e-6:
            fails.append("п.1 " + name)
    rules = json.loads(base_raw.decode("utf-8-sig"))
    apply_to_rules(G, rules, [item])
    with_ov = render_rules_json(base_raw, rules)
    rules2 = json.loads(with_ov.decode("utf-8-sig"))
    apply_to_rules(G, rules2, [{"kind": "override", "jeb": "ohs_a_co_ma_03", "item_id": 20600, "remove": True}])
    if not rules2.get("overrides"):
        rules2.pop("overrides", None)
    back = render_rules_json(with_ov, rules2)
    print("п.9 JSON: с записью +%d байт, после удаления побайтно исходный: %s" % (len(with_ov) - len(base_raw), back == base_raw))
    if back != base_raw:
        fails.append("п.9")
    # ★ Ревизия Ф11 (К1): сверка типов — часть самопроверки, а не отдельная забытая кнопка
    if check_types():
        fails.append("сверка типов")
    print("SELFTEST", "OK" if not fails else "ПРОВАЛ: %s" % fails)
    return 0 if not fails else 3


def cli(argv):
    """Точка входа (и из командной строки, и из встроенного Python редактора)."""
    for _s in (sys.stdout, sys.stderr):
        try: _s.reconfigure(encoding="utf-8", errors="replace")
        except Exception: pass
    if len(argv) >= 2 and argv[0] == "export":
        return export_state(argv[1])
    if len(argv) >= 3 and argv[0] == "apply":
        return apply_ops(argv[1], argv[2])
    if argv and argv[0] == "selftest":
        return selftest()
    # ★ Ревизия Ф11 (К1): таблица «тип поля → пять его потребителей»; код 3 при расхождении
    if argv and argv[0] == "types":
        return 0 if not check_types() else 3
    print("export <out.json> | apply <ops.json> <result.json> | selftest | types")
    return 2


if __name__ == "__main__":
    sys.exit(cli(sys.argv[1:]))
