# Copyright (c) 2026 @sitxovski (Discord)
# asda_dump_buff_fx.py — ТРЕТИЙ КАНАЛ ВИЗУАЛА НАВЫКОВ: «эффект СОСТОЯНИЯ (баффа)».
#
# ЧТО ЭТО. `skill_info.sif[skillId].buff0/1/2 SkillId` (+0x48/+0x59/+0x6A) — это id СОСТОЯНИЯ 0..349,
#   а НЕ id навыка. Он индексирует ДВЕ таблицы обработчиков по 350 записей:
#     игрок — `charSkillFxHandlerTableInit@0x9AB290`      → `*((_DWORD*)pChar + 52304 + buffId)`
#     моб   — `CCharEffect_InitSkillEffectTable@0xF601D0` → `*((_DWORD*)pMob  +  1844 + buffId)`
#   Тело обработчика спавнит `.jeb` ЛИТЕРАЛОМ и (если состояние долгоживущее) кладёт хэндлы эффектов
#   в запись баффа по +24/+28 (`fx0`/`fx1`), откуда их гасит `buffStopEffectsBySkillId@0xF83930`.
#   Полный разбор канала — `Reports/skill_fx_anim.md` §16/§17.
#
# ИСТОЧНИКИ (ничего не выдумываем — [[never-invent-only-original]]):
#   1) `Reports/skill_buff_fx.tsv` — дамп канала из БИНАРЯ (350 строк: id состояния, имя,
#      обработчик игрока + адрес, обработчик моба, .jeb, привязка, кость, .wav, список навыков).
#      Отсюда берём СОСТАВ таблицы (кто где стоит) — это авторитет.
#   2) `RE/etalon/*.c` — декомпил тех же обработчиков. Отсюда берём то, чего в TSV нет и без чего
#      эффект нельзя ни правильно посадить, ни ПОГАСИТЬ:
#        · номер слота хранения (`+24` = fx0 / `+28` = fx1) ⇒ признак «долгоживущий бафф»;
#        · значение поля `+170` эффекта (0/1/3 — режим повтора, 3 = вечный до гашения);
#        · программу МОБОВОГО обработчика целиком (в TSV колонки .jeb/привязка сняты с ИГРОКА);
#        · вариант по размерному классу цели (`сущность+7356` → `-a_00/-a_01/-a_02`).
#   Расхождение «TSV против декомпила» по .jeb/привязке/кости печатается как СВЕРКА (не глушится).
#
# ВЫХОД: `RawAssets/Data/BuffFx.csv` (ВНЕ Content!) — источник DataTable `DT_BuffFx`
#   (строка = `FAsdaBuffFxRow`, импорт — `Tools/asda_build_buff_fx_dt.py`).
#   Колонки `PlayerFx`/`MobFx` — ПРОГРАММА ПОКАЗА в том же духе, что `DT_SkillClips`:
#     записи через `;`, поля записи через `|`:
#       <jeb>|<attach>|<bone>|<slot>|<loop>|<variant>
#     attach  — режим привязки эталона (3/5/6/7/8/9/12), см. таблицу 16 хендлеров;
#     bone    — имя кости («4», «M_DUMMY», …) либо пусто (кость не используется);
#     slot    — 0 = хэндл лёг в `rec+24`, 1 = в `rec+28`, −1 = НЕ сохраняется (разовая вспышка);
#     loop    — значение поля `+170` (−1 = не выставлялось);
#     variant — размерный класс цели (−1 = ветки нет, 0/1/2 = только для этого класса).
#
# Запуск: python Tools\asda_dump_buff_fx.py
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
import csv
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.abspath(os.path.join(HERE, ".."))
ROOT = os.path.abspath(os.path.join(PROJ, ".."))   # <ROOT>
TSV = os.path.join(ROOT, "Reports", "skill_buff_fx.tsv")
ETALON = os.path.join(ROOT, "RE", "etalon")
OUT_CSV = os.path.join(PROJ, "RawAssets", "Data", "BuffFx.csv")

# Спавнеры эффектов и позиция значащих аргументов (индекс от 0, this = 0).
#   Effect_SpawnByName@0xF56340        (mgr, name, 1, x,y,z, mapId, ownerPtr, boneIdx, ATTACH, 0, scale)
#   effectMgrSpawnByName@0xF56550      (mgr, name, 1, x,y,z, mapId, u16, ownerPtr, boneIdx, ATTACH, 0, scale)
#   CEffectMgr_CreateEffect@0xF56DA0   (mgr, name, 1, x,y,z, boneOrRenderId, ATTACH, a9, scale)
#   …_F56C30 / effectMgrCreateEffect@0xF57090 / effectMgrSpawnByName_7200@0xF57200 — та же раскладка.
#   Третье число — индекс аргумента «носитель» (−1 = у спавнера его нет): по нему отбрасывается
#   ХОЛОСТАЯ ветка `if (this) spawn(…, this+2, …) else spawn(…, MEMORY[0x2A1F8], 0, …)`, которую
#   Hex-Rays печатает у каждого второго обработчика (this там заведомо не nullptr — обработчик
#   вызван через `this`-указатель).
SPAWNERS = {
    "Effect_SpawnByName":          (9, 8, 7),   # (индекс attach, индекс bone/renderId, индекс носителя)
    "effectMgrSpawnByName":        (10, 9, 8),
    "CEffectMgr_CreateEffect":     (7, 6, -1),
    "CEffectMgr_CreateEffect_F56C30": (7, 6, -1),
    "effectMgrCreateEffect":       (7, 6, -1),
    "effectMgrSpawnByName_7200":   (7, 6, -1),
    # effectCreateJEBInstance@0xF56880 — спавн ПОЛНОЙ МАТРИЦЕЙ: (mgr, name, 1, m0..m15, mapId,
    #   ownerPtr, boneIdx, ATTACH, 0, scale) ⇒ 16 лишних аргументов между именем и хвостом.
    "effectCreateJEBInstance":     (22, 21, 20),
}

RE_HDR = re.compile(r"^//---- (\S+) @ (0x[0-9A-Fa-f]+)")
RE_JEB = re.compile(r'"([^"]*\.jeb)"', re.I)
RE_WAV = re.compile(r'"([^"]*\.wav)"', re.I)
RE_BONE = re.compile(r'List_FindIndexByName\([^,]*,\s*"([^"]+)"\s*\)')
RE_LOOP = re.compile(r"\+\s*170\)\s*=\s*(-?\d+)")
# ★ 08.10.2026 (ревизия fx_loop, КРИТ-1): Hex-Rays печатает ТО ЖЕ поле `эффект+0x2A8` и третьим способом —
#   через поле-«дыру» типизированного класса: `*(_DWORD *)&Effect->gap_1D8[208] = 3` (0x1D8 + 208 = 680 = 0x2A8),
#   и (реже) байтовым смещением `+ 680)`. По одному литералу `+ 170)` два обработчика сущности
#   (`Effect_PlaySleepStatus@0xF69A70`, `Effect_PlayFreeze3Status@0xF69C60`) получили ложное «−1», хотя
#   машинный код пишет 3 (`C7 80 A8 02 00 00 03 00 00 00`, скан .text). Признак — СТРУКТУРНЫЙ (сумма смещений).
RE_LOOP_BYTE = re.compile(r"\+\s*680\)\s*=\s*(-?\d+)")
RE_LOOP_GAP = re.compile(r"->gap_([0-9A-Fa-f]+)\[(\d+)\]\s*=\s*(-?\d+)")


def find_loops(body):
    """Все записи поля `эффект+0x2A8` в теле по порядку: dword-индекс 170, байт 680 и gap_X[N] с X+N == 0x2A8."""
    hits = [(m.start(), int(m.group(1))) for m in RE_LOOP.finditer(body)]
    hits += [(m.start(), int(m.group(1))) for m in RE_LOOP_BYTE.finditer(body)]
    hits += [(m.start(), int(m.group(3))) for m in RE_LOOP_GAP.finditer(body)
             if int(m.group(1), 16) + int(m.group(2)) == 0x2A8]
    return [v for _, v in sorted(hits)]


# Хранение хэндла: `*(_DWORD *)(a4 + 24) = …` (иногда через переменную-алиас записи).
RE_SLOT = re.compile(r"\+\s*(24|28)\)\s*=\s*")
# Размерный класс цели: сущность+7356 (dword-индекс 1839). Hex-Rays печатает и как
#   `*((_DWORD *)p + 1839)`, и как `p[1839]` — ловим обе формы.
#   Hex-Rays печатает это поле тремя способами: `*((_DWORD *)p + 1839)`, `p[1839]` и
#   `p->gap_A42[4716]` (gap_A42 лежит по +0x0A50 = 2640; 2640 + 4716 = 7356 — то же поле).
RE_SIZE = re.compile(r"\+\s*1839\)|\+\s*7356\)|\[\s*1839\s*\]|\[\s*7356\s*\]|gap_A42\[\s*4716\s*\]")


def log(msg):
    sys.stdout.write(msg + "\n")


def load_bodies(folder):
    """Тела всех функций эталона: имя → текст (первое вхождение; дубли складываем)."""
    out = {}
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".c"):
            continue
        lines = io.open(os.path.join(folder, name), encoding="utf-8", errors="replace").read().split("\n")
        cur, start = None, 0
        for i, line in enumerate(lines):
            m = RE_HDR.match(line)
            if not m:
                continue
            if cur and cur not in out:
                out[cur] = "\n".join(lines[start:i])
            cur, start = m.group(1), i
        if cur and cur not in out:
            out[cur] = "\n".join(lines[start:])
    return out


def split_args(text, open_pos):
    """Аргументы вызова, начинающегося с `(` в позиции open_pos. Баланс скобок и кавычек."""
    args, depth, cur, i, n = [], 0, [], open_pos + 1, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = text.find('"', i + 1)
            while j > 0 and text[j - 1] == "\\":
                j = text.find('"', j + 1)
            if j < 0:
                break
            cur.append(text[i:j + 1])
            i = j + 1
            continue
        if c in "([":
            depth += 1
        elif c in ")]":
            if depth == 0:
                args.append("".join(cur).strip())
                return args, i
            depth -= 1
        elif c == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    return args, -1


def find_calls(body):
    """Все вызовы известных спавнеров: (имя, аргументы, позиция)."""
    out = []
    for m in re.finditer(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", body):
        name = m.group(1)
        if name not in SPAWNERS:
            continue
        args, _ = split_args(body, m.end() - 1)
        out.append((name, args, m.start()))
    return out


def int_or_none(expr):
    expr = expr.strip()
    if re.fullmatch(r"-?\d+", expr):
        return int(expr)
    m = re.fullmatch(r"\(\s*[A-Za-z_ \*]+\)\s*(-?\d+)", expr)
    return int(m.group(1)) if m else None


def variant_of(jeb):
    """Суффикс `-a_0N` → N (вариант по размерному классу цели); иначе None."""
    m = re.search(r"-[ab]_(\d\d)(?:\.jeb)?$", jeb, re.I)
    return int(m.group(1)) if m else None


def parse_handler(fn, body, warn, is_player):
    """Программа показа одного обработчика: список записей (jeb, attach, bone, slot, loop, variant)."""
    calls = find_calls(body)
    if not calls:
        return [], []
    bones = RE_BONE.findall(body)
    # Гендерная развилка эталона `gender == 1 ? "M_DUMMY" : "G_DUMMY"`. Наш скелет унисекс:
    #   экспортёр ремапит G_DUMMY→M_DUMMY (§H5 wings_soulmate_re.md) ⇒ имя одно.
    bone_names = []
    for b in bones:
        if b == "G_DUMMY" and "M_DUMMY" in bones:
            continue
        if b not in bone_names:
            bone_names.append(b)
    loops = find_loops(body)
    slots = [int(x) for x in RE_SLOT.findall(body)]
    wavs = [w for w in dict.fromkeys(RE_WAV.findall(body))]
    has_size_branch = bool(RE_SIZE.search(body))

    recs = []
    seen = set()
    attach_dbg = {}
    for idx, (spawner, args, pos) in enumerate(calls):
        ai_attach, ai_bone, ai_owner = SPAWNERS[spawner]
        jebs = RE_JEB.findall(args[1]) if len(args) > 1 else []
        if not jebs:
            continue
        jeb = os.path.splitext(jebs[0])[0]
        # ПОВТОРНЫЙ СПАВН ТОГО ЖЕ `.jeb` в одном обработчике = ЗАПАСНАЯ ветка эталона, а не второй
        #   эффект: либо холостое `if (this) … else …` (носитель `MEMORY[0x2A1F8]`), либо деградация
        #   «кость/кастер не найдены» (тот же файл, но привязкой 3 прямо на сущность). В рантайме
        #   исполняется ПЕРВАЯ; берём её, остальные отбрасываем (расхождение привязки — в замечания).
        if jeb.lower() in seen:
            if attach_dbg.get(jeb.lower()) not in (None, int_or_none(args[ai_attach]) if len(args) > ai_attach else None):
                warn.append("%s: запасная ветка %s с другой привязкой — отброшена" % (fn, jeb))
            continue
        seen.add(jeb.lower())
        attach_dbg[jeb.lower()] = int_or_none(args[ai_attach]) if len(args) > ai_attach else None
        # ★ Носитель эффекта. У МОБОВОГО обработчика часть эффектов вешается не на моба, а на
        #   КАСТЕРА (`CharManager_FindByUserIdx(mob+6996)` → кость `M_DUMMY`, спавн полной матрицей):
        #   так устроены `Effect_SpawnCounterOnChar@0xF695B0` и `Effect_SpawnDridg5OnChar@0xF6F280`.
        attach_lit = int_or_none(args[ai_attach]) if len(args) > ai_attach else None
        if spawner == "effectCreateJEBInstance" and "CharManager_FindByUserIdx" in body:
            owner = "caster"
        elif is_player and attach_lit in (3, 9, 12, 13):
            # ★ Привязки 3/9/12/13 читают `нода+804` как renderId СУЩНОСТИ. В обработчике МОБА это
            #   сам моб; в обработчике ИГРОКА — его текущая ЦЕЛЬ (`Entity_FindByRenderId(this->
            #   targetRenderId)`, `CharFx_SpawnBlossomEffect@0x9B4ED0`).
            owner = "target"
        else:
            owner = "self"
        attach = int_or_none(args[ai_attach]) if len(args) > ai_attach else None
        if attach is None:
            warn.append("%s: привязка не литерал (%s)" % (fn, args[ai_attach] if len(args) > ai_attach else "?"))
            attach = -1
        bone_arg = args[ai_bone] if len(args) > ai_bone else "0"
        bone = ""
        if int_or_none(bone_arg) is None and bone_names:
            # Аргумент — переменная (индекс кости из List_FindIndexByName). Имён в теле обычно одно.
            bone = bone_names[0]
            if len(bone_names) > 1:
                warn.append("%s: имён костей в теле %d (%s) — взято первое" % (fn, len(bone_names), ",".join(bone_names)))
        recs.append({"jeb": jeb, "attach": attach, "bone": bone, "slot": -1, "loop": -1,
                     "variant": -1, "owner": owner})

    # Слот хранения: эталон присваивает в порядке спавна (первый → +24, второй → +28).
    for i, s in enumerate(slots[:len(recs)]):
        recs[i]["slot"] = 0 if s == 24 else 1
    # ⚠ `timbom01`: два спавна и ДВА присваивания, но перед ними ещё два StopByOwner-чтения — они
    #   не попадают в RE_SLOT (там `= ` обязателен), так что порядок сохраняется.
    if len(loops) == 1:
        for r in recs:
            r["loop"] = loops[0]
    else:
        for i, lv in enumerate(loops[:len(recs)]):
            recs[i]["loop"] = lv

    # ── ВАРИАНТ ПО РАЗМЕРНОМУ КЛАССУ ЦЕЛИ ────────────────────────────────────────────────────────
    #   Эталон: `if (ent[7356] == 0) …-a_00; else if (== 1) …-a_01; else if (== 2) …-a_02;` — то же
    #   поле, по которому `SkillEffect_SetupHexa` выбирает `sq-hexa-s/m/l` ([[skill-visual-layer]]).
    #   Признак группы СТРУКТУРНЫЙ (Hex-Rays печатает само поле то как `+ 1839)`, то как `[1839]`,
    #   то как `gap_A42[4716]` — на текст полагаться нельзя): один и тот же ствол имени, ОДНА И ТА ЖЕ
    #   привязка, ОДИН И ТОТ ЖЕ носитель и РАЗНЫЕ суффиксы `_00/_01/_02`. Разный носитель или разная
    #   привязка = эффекты играют ВМЕСТЕ (`Effect_SpawnCounterOnChar`: `-a_00` кастеру, `-a_01` мобу).
    #   Внутри группы исполняется РОВНО ОДНА ветка ⇒ слот хранения и режим повтора у неё общие
    #   (эталон присваивает `rec+24` после if/else) — разносим на всю группу, иначе гасить будет нечего.
    groups = {}
    for r in recs:
        v = variant_of(r["jeb"])
        if v is None or v > 2:
            continue
        groups.setdefault((re.sub(r"_\d\d$", "", r["jeb"]), r["attach"], r["owner"]), []).append(r)
    for key, group in groups.items():
        if len(group) < 2 or len({variant_of(r["jeb"]) for r in group}) != len(group):
            continue
        slot = max(r["slot"] for r in group)
        loop = max(r["loop"] for r in group)
        for r in group:
            r["variant"] = variant_of(r["jeb"])
            r["slot"] = slot
            r["loop"] = loop
        if not has_size_branch:
            warn.append("%s: группа вариантов %s без явного гейта +7356 в теле" % (fn, key[0]))
    return recs, wavs


def fmt_program(recs):
    return ";".join("%s|%d|%s|%d|%d|%d|%s"
                    % (r["jeb"], r["attach"], r["bone"], r["slot"], r["loop"], r["variant"], r["owner"])
                    for r in recs)


def csv_escape(s):
    return '"%s"' % s.replace('"', '""')


def main():
    if not os.path.exists(TSV):
        log("НЕТ %s" % TSV)
        return
    if not os.path.isdir(ETALON):
        log("НЕТ %s" % ETALON)
        return
    bodies = load_bodies(ETALON)
    rows = list(csv.DictReader(io.open(TSV, encoding="utf-8"), delimiter="\t"))
    log("состояний в дампе канала: %d; тел функций в эталоне: %d" % (len(rows), len(bodies)))

    warn, missing_fn, out_rows = [], [], []
    n_player, n_mob, n_persist = 0, 0, 0
    for r in rows:
        bid = int(r["BuffId"])
        prog = {"PlayerFn": [], "MobFn": []}
        wavs = []
        for col in ("PlayerFn", "MobFn"):
            for fn in [x.strip() for x in r[col].split(",") if x.strip()]:
                body = bodies.get(fn)
                if body is None:
                    missing_fn.append("%d/%s/%s" % (bid, col, fn))
                    continue
                recs, w = parse_handler(fn, body, warn, is_player=(col == "PlayerFn"))
                prog[col].extend(recs)
                wavs.extend(w)
        if prog["PlayerFn"]:
            n_player += 1
        if prog["MobFn"]:
            n_mob += 1
        if any(x["slot"] >= 0 for x in prog["PlayerFn"] + prog["MobFn"]):
            n_persist += 1
        out_rows.append({
            "Name": "%d" % bid,
            "Id": bid,
            "BuffName": r["BuffName"],
            "PlayerFn": r["PlayerFn"],
            "MobFn": r["MobFn"],
            "PlayerFx": fmt_program(prog["PlayerFn"]),
            "MobFx": fmt_program(prog["MobFn"]),
            "Wav": wavs[0] if wavs else (r.get("Wavs", "").split(";")[0] if r.get("Wavs") else ""),
            "Skills": r.get("SkillsUsing", ""),
        })

    # ── СВЕРКА С ДАМПОМ ИЗ БИНАРЯ (колонки Jebs/Anchors/Bones сняты с ОБРАБОТЧИКА ИГРОКА) ────────
    mism = []
    for r, o in zip(rows, out_rows):
        tsv_jebs = [os.path.splitext(x)[0] for x in r["Jebs"].split(";") if x.strip()]
        got = [x.split("|")[0] for x in o["PlayerFx"].split(";") if x]
        if sorted(x.lower() for x in tsv_jebs) != sorted(x.lower() for x in got):
            mism.append("%s: TSV=%s декомпил=%s" % (r["BuffId"], tsv_jebs, got))
    log("сверка .jeb игрока «бинарь ↔ декомпил»: расхождений %d из %d" % (len(mism), len(rows)))
    for m in mism[:20]:
        log("   ! %s" % m)

    if missing_fn:
        log("обработчиков нет в срезе эталона: %d (%s)" % (len(missing_fn), ", ".join(missing_fn[:8])))
    if warn:
        log("замечаний разбора: %d" % len(warn))
        for w in warn[:15]:
            log("   ~ %s" % w)

    os.makedirs(os.path.dirname(OUT_CSV), exist_ok=True)
    cols = ["Name", "Id", "BuffName", "PlayerFn", "MobFn", "PlayerFx", "MobFx", "Wav", "Skills"]
    with io.open(OUT_CSV, "w", encoding="utf-8-sig", newline="") as f:
        f.write(",".join(cols) + "\n")
        for o in out_rows:
            f.write(",".join(csv_escape(str(o[c])) for c in cols) + "\n")
    log("записано %s: строк %d (с программой игрока %d, моба %d, долгоживущих %d)"
        % (OUT_CSV, len(out_rows), n_player, n_mob, n_persist))


def _is_direct_run():
    """True — файл запущен НАПРЯМУЮ, а не импортирован ([[tools-no-import-side-effects]])."""
    return __name__ == "__main__" or globals().get("__spec__") is None


if _is_direct_run():
    main()
