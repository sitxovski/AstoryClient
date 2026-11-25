# Copyright (c) 2026 @sitxovski (Discord)
# asda_audit_skill_fx.py — СПЛОШНОЙ АУДИТ ВИЗУАЛА ВСЕХ НАВЫКОВ.
#
# ЗАЧЕМ. `skill_clip_table.tsv` отвечает на вопрос «что играет», но не отвечает на вопрос
#   «а не сломано ли оно». Симптомы, с которыми пришёл пользователь («ряд эффектов выглядят криво»,
#   «эффекты не на цели, а на игроке»), находятся ровно перечислимыми проверками — и находить их
#   надо СПЛОШНЫМ прогоном, а не по одному навыку глазами.
#
# ЧТО ПРОВЕРЯЕТСЯ (каждая проверка — по эталону/данным, ничего не выдумывается):
#   1  СНАРЯД БЕЗ ВИЗУАЛА ПОЛЁТА — `Proj == 1`, а записи `@projectile` в программе показа нет.
#      Авторитет обоих признаков — тело тика + гейт данных (см. шапку `asda_dump_skill_dispatch.py`).
#   2  НЕТ ТОЧКИ РОЖДЕНИЯ — у эффекта нет суффикса `:источник` (`world:target`, `matrix:caster`, …).
#      Именно из-за отсутствия этой оси эффекты рождались «по контексту фазы», а не там, где эталон.
#   3  НУЛЕВАЯ ДЛИТЕЛЬНОСТЬ ФАЗЫ — у фазы есть эффекты удара, но её порог COMBO равен нулю
#      (значащий ноль оригинала: 174 строки; эталон подменяет максимум полосы на 1 мс).
#   4  ЭФФЕКТ БЕЗ ЭМИТТЕРОВ — `.jeb` разобрался, но список эмиттеров пуст либо часть записей не
#      разобралась (`parsedAll == False`) ⇒ на экране будет пусто/недорисовано.
#   5  БЕСКОНЕЧНЫЙ ЦИКЛ — `header.endMode` ∈ {1,2,3}: эффект навыка перезапускается/зациклен.
#      Для навыка это подозрительно: он ОДНОРАЗОВЫЙ, а вечный эффект остаётся висеть на персонаже.
#      (Закон `skill-visual-layer`: endMode эффектов навыков берётся С ДИСКА; здесь мы его ПОКАЗЫВАЕМ.)
#   6  НУЛЕВАЯ ЖИЗНЬ ЭФФЕКТА — `header.duration <= 0` при непустых эмиттерах.
#   7  НЕТ ФАЙЛА `.jeb` в оригинальном архиве (дефект данных оригинала — фиксируем, не подменяем).
#   8  НЕТ СОБРАННОГО `BP_Fx_*` в проекте (пайплайн сборки эффектов не прогнан по этому имени).
#   9  ТИП ЭМИТТЕРА НЕ РЕАЛИЗОВАН парсером (`conf` содержит «тип 0x…») — закон
#      [[jeb-emitter-routing-hole]]: неучтённое обязано ПОПАСТЬ В ОТЧЁТ, а не потеряться.
#  10  ПОРЯДОК ФАЗ — COMBO0 > COMBO1 > 0 и т.п.: фазы обязаны идти по возрастанию, иначе фаза 1
#      никогда не выстрелит (тик сравнивает пороги СТРОГО ПО ОЧЕРЕДИ).
#  11  ЭФФЕКТ УДАРА У КАСТЕРА / ЭФФЕКТ КАСТА У ЦЕЛИ — не ошибка, а РЕДКИЙ случай: печатаем списком,
#      чтобы его нельзя было принять за баг порта (навык 905 именно такой).
#
# Запуск:  python Tools\asda_audit_skill_fx.py            — сводка + подозрительные
#          python Tools\asda_audit_skill_fx.py --all      — плюс полная таблица по каждому навыку
#          python Tools\asda_audit_skill_fx.py --skill 501
# Выход:   консоль + `Reports/skill_fx_audit.tsv` (машинный список находок)
# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))   # <ROOT> (Tools → проект → ROOT)
TABLE = os.path.join(ROOT, "Reports", "skill_clip_table.tsv")
OUT = os.path.join(ROOT, "Reports", "skill_fx_audit.tsv")
BP_DIR = os.path.join(ROOT, "AstoryClient", "Content", "AsdaAssets", "Effects")

# Известные источники точки рождения (см. `_origin_near`).
ORIGINS = ("caster", "target", "bone", "hitpoint")


def load_rows():
    """`skill_clip_table.tsv` → список словарей (шапка = первая строка)."""
    if not os.path.exists(TABLE):
        print("НЕТ %s — сначала python Tools\\asda_dump_skill_dispatch.py" % TABLE)
        return []
    with open(TABLE, encoding="utf-8") as f:
        head = f.readline().rstrip("\n").split("\t")
        return [dict(zip(head, line.rstrip("\n").split("\t"))) for line in f if line.strip()]


def parse_cell(cell):
    """`имя.jeb@привязка[:источник][/видЦели];…` → [(имя, привязка, источник, видЦели)]."""
    out = []
    for item in (cell or "").split(";"):
        item = item.strip()
        if not item:
            continue
        name, _, tail = item.partition("@")
        anchor, _, kind = tail.partition("/")
        anchor, _, origin = anchor.partition(":")
        out.append((os.path.splitext(name)[0], anchor, origin, kind))
    return out


def bp_exists(jeb_base):
    """Есть ли собранный `BP_Fx_<база>` в контенте проекта."""
    return os.path.exists(os.path.join(BP_DIR, _U.fx_bp_name(jeb_base) + ".uasset"))


# ══════════════════════════════════════════════════════════════════════════════════════════════
# ★★★ ПОЭМИТТЕРНЫЕ МАСШТАБ И ДЛИТЕЛЬНОСТЬ (добавлено 2026-08-10, аудит §14)
#
# ОТКУДА ЭТАЛОН БЕРЁТ РАЗМЕР ЭФФЕКТА НАВЫКА (реверс, не гипотеза):
#   1) ГЕОМЕТРИЯ — только из полей `.jeb` эмиттера. Квады 0xB/0xC/0x13/0x14/0x18/0x19:
#      `halfW s/E/v @0xC4` и `halfH s/E/v @0xD0` (record-relative + пер-типовой сдвиг),
#      это ПОЛУ-экстенты (вершины ±halfW/±halfH — дизасм `effectQuad_updateAndDrawBillboard@0x120AEB0`,
#      `effect_loaders_pertype.md` §2). Кольца 0xE — `topR/botR/height`. Частицы 0xD — `sizeX/sizeY`.
#   2) ЕДИНСТВЕННЫЙ ВНЕШНИЙ МНОЖИТЕЛЬ — `CEffectMgr_scaleAll@0x114D7C0`: он (а) домножает МАТРИЦУ
#      эффекта +0x208 на s (⇒ геометрия квадов/колец/мешей ×s) и (б) зовёт vtbl+12 у типов
#      2,3,7,9,0xA,0xC,0xD,0x10,0x14,0x16,0x17,0x19,0x1B,0x1C — а это `emitterScaleVel4@0x120B960`,
#      «масштабировать 4 поля СКОРОСТИ». РАЗМЕР ЧАСТИЦЫ не масштабируется НИКОГДА
#      (закон effects-smoke-wave). Для навыков s ставится ровно в двух местах и оба — удар У ЦЕЛИ:
#      `sub_936A29(цель)/100` = ПРОЦЕНТ размера сущности (100 ⇒ 1.0), писателя поля в срезе нет.
#   3) Больше множителей нет: `Effect_SpawnByName`/`CEffectMgr_CreateEffect_F561F0` последний
#      аргумент 1.0f в `CEffect_InitFromName` не передают (там он = sortKey), attach даёт ПОЗИЦИЮ.
#
# ОТКУДА ЭТАЛОН БЕРЁТ ДЛИТЕЛЬНОСТЬ И СКОРОСТЬ ВОСПРОИЗВЕДЕНИЯ:
#   1) ЖИЗНЬ ЭМИТТЕРА = `duration base/var` записи (кламп 0.1…10 с); каналы маршируют START→END
#      ровно за неё (лоадер добавляет узлы трека из сидов — законы effects-conversion-model).
#   2) ЧТО ПОСЛЕ — `endMode`: 0 = стоп (эмиттер снимается), 1/2 = реинит-перезапуск,
#      3 = у квад/кольца фриз на END, у МЕША перезапуск (`effectEmitter_tickUpdate@0x12293B0`).
#   3) `startDelay` (rec+0x20) — каскадный старт эмиттера внутри эффекта.
#   4) ЧАСЫ ФАЗ навыка идут НЕ по реальному времени, а `dt · pChar->animSpeedScale`
#      (`SkillEffect_UpdateScaledStrikeHit@0xA72610`, первая строка) — та же величина двигает и
#      полосу применения. Сам ЭФФЕКТ тикает сырым dt кадра (holder+660).
# ══════════════════════════════════════════════════════════════════════════════════════════════

# Порог «покажи глазами»: полу-экстент квада/радиус кольца, выше которого эффект надо сверить
#   визуально (персонаж ~170 юн ⇒ 500 юн полу-экстента = квад 10 м, вчетверо выше персонажа).
BIG_HALF = 500.0
# Порог подозрительно долгого эмиттера у РАЗОВОГО эффекта навыка (медиана жизни навыковых
#   эмиттеров ~0.3 с; всё, что живёт дольше 3 с, обязано быть осознанным).
LONG_LIFE = 3.0
# Сколько живёт актор эффекта в порту: удар `kHitFxLifeSec`, каст — длина клипа + `kFxExtraLifeSec`.
HIT_ACTOR_LIFE = 4.0


def _em_period(t, pr):
    """Период эмиттера ТАК, КАК ЕГО СТАВИТ СБОРЩИК (`asda_place_effects.build_emitter`)."""
    if t in (0xD, 3, 0x1B, 0x17, 0x20):
        return max(pr.get("period", 5.0) or 5.0, 0.05)
    if t in (2, 0xB, 0xC, 0x13, 0x14, 0x18, 0x19) and pr.get("growQuad"):
        return max((pr.get("life") or [1.0])[0], 0.05)
    if t in (0xE, 4, 0x15, 0x1A) and pr.get("ring"):
        return min(max((pr.get("life") or [1.0])[0], 0.1), 10.0)
    if t in (0x11, 0x12, 0x1F):
        return max((pr.get("life") or [1.0])[0], 0.1)
    if t in (1, 0xB, 0xC, 0x18):
        return max(pr.get("period", 4.0) or 4.0, 0.05)
    return 0.0


def _em_endmode(t, pr):
    """Итоговый endMode эмиттера (у кольца это `loopMode` через таблицу сборщика)."""
    if t in (0xE, 4, 0x15, 0x1A) and pr.get("ring"):
        return {0: 0, 1: 1, 2: 3, 3: 3}.get(pr.get("loopMode"), 3)
    em = pr.get("endMode")
    return None if em is None else int(em)


def _em_half(t, pr):
    """Максимальный ПОЛУ-экстент геометрии эмиттера в мировых юнитах (частицы — размер спрайта)."""
    best = 0.0
    for k in ("scaleX", "scaleY"):
        v = pr.get(k)
        if isinstance(v, (list, tuple)):
            for x in v[:2]:
                best = max(best, abs(float(x or 0.0)))
    if pr.get("ring"):
        for k in ("topR", "botR", "height"):
            v = pr.get(k)
            if isinstance(v, (list, tuple)):
                for x in v[:2]:
                    best = max(best, abs(float(x or 0.0)))
    for k in ("sizeX", "sizeY"):
        v = pr.get(k)
        if isinstance(v, (list, tuple)):
            for x in v[:2]:
                best = max(best, abs(float(x or 0.0)))
    return best


def jeb_facts(name, cache, parser):
    """`.jeb` → dict(exists, emitters, parsedAll, endMode, duration, types, unimpl) с кэшем."""
    key = name.lower()
    if key in cache:
        return cache[key]
    fact = dict(exists=False, emitters=0, parsedAll=True, endMode=None, duration=0.0,
                types=[], unimpl=[], blend="", ems=[], span=0.0, maxHalf=0.0, loops=[], longs=[])
    try:
        p = parser.parse_effect(name)
    except Exception as e:                                   # архив недоступен — честно в отчёт
        fact["error"] = str(e)
        p = None
    if p:
        fact["exists"] = True
        fact["emitters"] = len(p["emitters"])
        fact["parsedAll"] = bool(p.get("parsedAll"))
        fact["endMode"] = p["header"]["endMode"]
        fact["duration"] = p["header"]["duration"]
        fact["blend"] = p.get("blendMode", "")
        fact["types"] = sorted({e["type"] for e in p["emitters"]})
        fact["unimpl"] = sorted({e["type"] for e in p["emitters"]
                                 if "тип 0x" in str(e["params"].get("conf", ""))})
        # ── поэмиттерные масштаб/длительность ──
        for e in p["emitters"]:
            t = e["type"]
            pr = e.get("params") or {}
            sd = float(e.get("startDelay", 0.0) or 0.0)
            per = _em_period(t, pr)
            emd = _em_endmode(t, pr)
            half = _em_half(t, pr)
            fact["ems"].append(dict(name=e.get("name", "?"), t=t, sd=sd, per=per, end=emd, half=half))
            fact["span"] = max(fact["span"], sd + per)
            fact["maxHalf"] = max(fact["maxHalf"], half)
            if emd not in (0, None):
                fact["loops"].append("%s(0x%X,endMode=%d)" % (e.get("name", "?"), t, emd))
            if per > LONG_LIFE:
                fact["longs"].append("%s(0x%X,%.1fс)" % (e.get("name", "?"), t, per))
    cache[key] = fact
    return fact


def fnum(s):
    try:
        return float(s or 0)
    except ValueError:
        return 0.0


def main():
    rows = load_rows()
    if not rows:
        return 1
    try:
        import asda_jeb_parse as J
    except Exception as e:
        print("парсер .jeb недоступен (%s) — проверки 4/5/6/7/9 пропущены" % e)
        J = None

    show_all = "--all" in sys.argv
    only = None
    if "--skill" in sys.argv:
        only = int(sys.argv[sys.argv.index("--skill") + 1])

    cache = {}
    findings = []          # (навык, имя навыка, код проверки, объект, пояснение)

    def add(r, code, obj, why):
        findings.append((r["SkillId"], r["SkillName"], code, obj, why))

    # Дедуп по НАВЫКУ, а не по строке: у навыка до 12 строк (пол × оружие), находка одна и та же.
    seen = set()
    for r in rows:
        sid = int(r["SkillId"])
        if only is not None and sid != only:
            continue
        casts = parse_cell(r.get("FxCast"))
        hits = [parse_cell(r.get("FxHit%d" % i)) for i in range(3)]
        combo = [fnum(r.get("Combo%dMs" % i)) for i in range(3)]

        def once(code, obj, why):
            k = (sid, code, obj)
            if k in seen:
                return
            seen.add(k)
            add(r, code, obj, why)

        # 1) снаряд без визуала полёта
        if r.get("Proj") == "1":
            if not any(a == "projectile" for lst in hits for _n, a, _o, _k in lst):
                once("СНАРЯД-БЕЗ-ВИЗУАЛА", r.get("TickFn", ""),
                     "Proj=1, но записи @projectile нет — имя .jeb полёта не извлечено")
        # 10) порядок фаз
        for i in (1, 2):
            if combo[i] > 0 and combo[i] <= combo[i - 1]:
                once("ПОРЯДОК-ФАЗ", "COMBO%d" % i,
                     "COMBO%d=%.0f <= COMBO%d=%.0f — фаза никогда не выстрелит (пороги идут по очереди)"
                     % (i, combo[i], i - 1, combo[i - 1]))

        for ph in range(3):
            fx = [x for x in hits[ph] if x[1] != "projectile"]
            # 3) нулевой порог фазы при непустом ударе
            if fx and combo[ph] <= 0.0:
                once("ФАЗА-НУЛЕВОЙ-ПОРОГ", "COMBO%d" % ph,
                     "у фазы %d есть эффекты удара (%d), но порог COMBO%d = 0" % (ph, len(fx), ph))

        for kind, lst in (("КАСТ", casts), ("УДАР0", hits[0]), ("УДАР1", hits[1]), ("УДАР2", hits[2])):
            for name, anchor, origin, tk in lst:
                # 2) нет точки рождения
                if anchor != "projectile" and origin not in ORIGINS:
                    once("НЕТ-ТОЧКИ-РОЖДЕНИЯ", "%s %s@%s" % (kind, name, anchor),
                         "источник не разобран — рантайм возьмёт запасной вариант")
                # 11) перекрёстные случаи (не баг, но должны быть на виду)
                if kind.startswith("УДАР") and origin == "caster":
                    once("УДАР-У-КАСТЕРА", "%s %s@%s" % (kind, name, anchor),
                         "эталон рождает этот эффект удара У ПЕРСОНАЖА (проверено по аргументам вызова)")
                if kind == "КАСТ" and origin == "target":
                    once("КАСТ-У-ЦЕЛИ", "%s %s@%s" % (kind, name, anchor),
                         "эталон рождает этот эффект СТАРТА У ЦЕЛИ")
                if not J:
                    continue
                f = jeb_facts(name, cache, J)
                # 7) файла нет в архиве
                if not f["exists"]:
                    once("НЕТ-JEB", name, "файла нет в EffectModel.bnz оригинала (дефект данных)")
                    continue
                # 4) эффект без эмиттеров / разобран не весь
                if f["emitters"] == 0:
                    once("БЕЗ-ЭМИТТЕРОВ", name, "разобрано 0 эмиттеров — на экране будет пусто")
                elif not f["parsedAll"]:
                    once("ЧАСТИЧНЫЙ-РАЗБОР", name,
                         "разобрано %d эмиттеров, но не все записи файла" % f["emitters"])
                # 9) типы эмиттеров, не реализованные парсером
                if f["unimpl"]:
                    once("ТИП-НЕ-РЕАЛИЗОВАН", name,
                         "типы " + ", ".join("0x%X" % t for t in f["unimpl"]))
                # 5) бесконечный цикл. ★ У ВИЗУАЛА ПОЛЁТА это НОРМА и не находка: эффект обязан
                #   крутиться всю дорогу, а гасит его прилёт — `CEffect_UpdateSkillCast@0xA5A060`
                #   зовёт `clearEffectMgr(носитель+520)` в самом конце. Разделяем два кода, чтобы
                #   ожидаемое не тонуло в подозрительном (и наоборот).
                if f["endMode"] in (1, 2, 3):
                    once("ЗАЦИКЛЕН-ОЖИДАЕМО" if anchor == "projectile" else "ЗАЦИКЛЕН", name,
                         "endMode=%d (1/2=перезапуск, 3=цикл)%s" % (
                             f["endMode"],
                             " — визуал полёта, гасится прилётом носителя" if anchor == "projectile"
                             else " — одноразовый эффект навыка будет висеть"))
                # 6) нулевая жизнь
                if f["emitters"] and f["duration"] <= 0.0:
                    once("НУЛЕВАЯ-ДЛИТЕЛЬНОСТЬ", name,
                         "header.duration = %.3f при %d эмиттерах" % (f["duration"], f["emitters"]))
                # 8) BP не собран
                if not bp_exists(name):
                    once("НЕТ-BP", name, "BP_Fx_%s не собран (Tools/asda_build_skill_fx.py)"
                         % name.replace("-", "_"))
                # 12) МУЛЬТИПЛИКАТИВНЫЙ БЛЕНД (`blendDst == 3`, INVSRCCOLOR) — не ошибка, но требует
                #   ПАРЫ секций Modulate+Additive ([[effects-mod-blend-stable-topology]]). Одиночная
                #   additive-секция на таком эффекте даёт «засветку вместо затемнения». Выносим
                #   списком, чтобы глазами проверить именно эти эффекты, а не все подряд.
                if f.get("blend") == "mod":
                    once("БЛЕНД-MOD", name,
                         "blendDst=3 (INVSRCCOLOR) — нужна пара секций Modulate+Additive")
                # ═══ 14) ЭМИТТЕР-ЛУПЕР В РАЗОВОМ ЭФФЕКТЕ НАВЫКА ═══════════════════════════════
                #   Проверка 5 смотрит только `header.endMode` (общий для файла), а реально
                #   перезапуском управляет endMode КАЖДОГО эмиттера. У навыка эффект разовый:
                #   любой эмиттер с endMode != 0 будет крутиться до конца жизни актора
                #   (удар 4 с, каст = длина клипа + 3 с) — это и читается как «висит/мигает».
                #   Визуал полёта — законное исключение (гасится прилётом носителя).
                if anchor != "projectile" and f.get("loops"):
                    once("ЭМИТТЕР-ЛУПЕР", name,
                         "перезапускаются до конца жизни актора: " + ", ".join(f["loops"][:6])
                         + ("" if len(f["loops"]) <= 6 else " … всего %d" % len(f["loops"])))
                # ═══ 15) ЭФФЕКТ ДЛИННЕЕ, ЧЕМ ЖИВЁТ АКТОР УДАРА ═══════════════════════════════
                #   Полная длина эффекта = max(startDelay + период) по эмиттерам. Если она больше
                #   срока жизни актора удара, хвост эффекта СРЕЗАЕТСЯ на середине.
                if kind.startswith("УДАР") and anchor != "projectile" and f.get("span", 0.0) > HIT_ACTOR_LIFE:
                    once("ДЛИННЕЕ-АКТОРА", name,
                         "эффект идёт %.1f с, а актор удара живёт %.1f с — хвост срежется"
                         % (f["span"], HIT_ACTOR_LIFE))
                # ═══ 16) ПОДОЗРИТЕЛЬНО ДОЛГИЙ ЭМИТТЕР ════════════════════════════════════════
                if f.get("longs"):
                    once("ДОЛГИЙ-ЭМИТТЕР", name,
                         "жизнь > %.0f с: %s" % (LONG_LIFE, ", ".join(f["longs"][:6])))
                # ═══ 17) ОЧЕНЬ КРУПНАЯ ГЕОМЕТРИЯ (глазная проверка) ══════════════════════════
                #   Полу-экстент квада / радиус кольца в мировых юнитах (персонаж ≈170).
                #   НЕ ошибка порта: это ДАННЫЕ .jeb. Но именно сюда смотреть, если «флеш
                #   выглядит слишком большим», — и сверять с оригиналом, а не с ощущением.
                if f.get("maxHalf", 0.0) > BIG_HALF:
                    once("КРУПНАЯ-ГЕОМЕТРИЯ", name,
                         "максимальный полу-экстент %.0f юн (полный размер %.0f юн ≈ %.1f роста персонажа)"
                         % (f["maxHalf"], f["maxHalf"] * 2.0, f["maxHalf"] * 2.0 / 170.0))
                # ═══ 18) МАСШТАБ ПО ЦЕЛИ ═════════════════════════════════════════════════════
                #   Ровно эти записи эталон прогоняет через `CEffectMgr_scaleAll(percent/100)`.
                #   Поля «процент размера сущности» в порту НЕТ (писателя нет и в эталоне),
                #   поэтому множитель = 1.0. Список нужен, чтобы было видно охват правила.
                if kind.startswith("УДАР") and origin in ("target", "hitpoint"):
                    once("МАСШТАБ-ПО-ЦЕЛИ", name,
                         "эталон зовёт scaleAll(процентРазмераЦели/100); поля нет ⇒ множитель 1.0")

        # 13) КОСТЬ ИЗ ТИКА против кости по оружию. Рантайм крепит эффекты каста к кости
        #   `FxBoneFor(подтипОружия)` = `8_a_End` / `8_b1` (`Char_UpdateSkillAnim_MultiPhase`
        #   @0xA76E70, `entity_char.c:29620-29623`). Если тик навыка называет ДРУГУЮ кость литералом
        #   (колонка `FxCastBone`) — это расхождение, и его надо видеть, а не прятать.
        cb = (r.get("FxCastBone") or "").strip()
        if cb and cb not in ("8_a_End", "8_b1") and any(a == "bone" for _n, a, _o, _k in casts):
            once("КОСТЬ-ИЗ-ТИКА", cb,
                 "тик крепит эффект каста к кости «%s», а рантайм берёт 8_a_End/8_b1 по оружию" % cb)

        if show_all:
            print("%-5s %-28s тик=%-34s Proj=%s/%s COMBO %.0f/%.0f/%.0f" %
                  (r["SkillId"], r["SkillName"][:28], r.get("TickFn", "")[:34],
                   r.get("Proj"), r.get("ProjDelay"), combo[0], combo[1], combo[2]))
            for kind, lst in (("  каст", casts), ("  уд.0", hits[0]),
                              ("  уд.1", hits[1]), ("  уд.2", hits[2])):
                for name, anchor, origin, tk in lst:
                    f = jeb_facts(name, cache, J) if J else {}
                    print("%s %-38s %-10s %-9s %-5s эмиттеров=%s endMode=%s life=%.2f" %
                          (kind, name, anchor, origin or "—", tk or "—",
                           f.get("emitters", "?"), f.get("endMode", "?"), f.get("duration", 0.0)))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("SkillId\tSkillName\tCheck\tObject\tWhy\n")
        for row in sorted(findings, key=lambda x: (x[2], int(x[0]))):
            f.write("\t".join(str(x) for x in row) + "\n")

    by = {}
    for _s, _n, code, _o, _w in findings:
        by[code] = by.get(code, 0) + 1
    print("\n=== АУДИТ ВИЗУАЛА НАВЫКОВ: строк таблицы %d, находок %d" % (len(rows), len(findings)))
    for code in sorted(by, key=lambda c: -by[c]):
        print("  %-22s %d" % (code, by[code]))
    print("--- подробности: %s" % OUT)
    for code in sorted(by):
        items = [x for x in findings if x[2] == code]
        print("\n[%s] %d:" % (code, len(items)))
        for sid, nm, _c, obj, why in items[:25]:
            print("   %-5s %-26s %-46s %s" % (sid, nm[:26], obj[:46], why))
        if len(items) > 25:
            print("   … ещё %d (все — в %s)" % (len(items) - 25, os.path.basename(OUT)))
    return 0


def _is_direct_run():
    """True — файл запущен НАПРЯМУЮ, а не импортирован (см. tools-no-import-side-effects)."""
    return __name__ == "__main__" or globals().get("__spec__") is None


if _is_direct_run():
    sys.exit(main())
