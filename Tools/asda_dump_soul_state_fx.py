# Copyright (c) 2026 @sitxovski (Discord)
# Снятие ТАБЛИЦЫ ВИЗУАЛА СОСТОЯНИЙ СОУЛМЕЙТА — `skillEffectInitAnimEventTable@0xAD34A0`.
#
# ЗАЧЕМ ОТДЕЛЬНЫЙ ДАМПЕР (и почему прежний диагноз был неверен).
#   В отчёте `skill_system_final_verify.md` (остаток П3) числилось: «пять `.jeb` не попали в
#   инвентарь — они спавнятся внутри тик-функций через `Effect_SpawnByName`, нужен обход ВЛОЖЕННЫХ
#   ВЫЗОВОВ у извлекателя строк». Проверка по бинарю это ОПРОВЕРГЛА: обход вложенных вызовов не нашёл
#   бы их НИКОГДА, потому что вызова из зарегистрированных загрузчиков/тиков не существует вовсе —
#   ни `SkillEffect_OnAnimEventSpawnSoulRecall@0xAD3D20`, ни `SkillEffect_OnAnimEventSpawnAttBombA
#   @0xAD3E80` не вызываются НИ ИЗ ОДНОГО тела (проверено по полному декомпилу
#   `RE/ida/export/AsdaStory_ida.c`: единственные вхождения — их собственные заголовки, переходники
#   `j_*` и ОДНА строка регистрации). Они лежат в СОВЕРШЕННО ДРУГОЙ таблице.
#
# ЧТО ЭТО ЗА ТАБЛИЦА (построчно, `AsdaStory_ida.c:360603-360632`).
#   `skillEffectInitAnimEventTable@0xAD34A0` заполняет на КАЖДОМ персонаже три параллельных массива
#   по 30 элементов:
#       *((_WORD *)this + i + 113224) = -1;         // параметр A состояния
#       *((_WORD *)this + i + 113254) = -1;         // параметр B состояния
#       *((_DWORD *)this + i + 56552) = 3;          // СОСТОЯНИЕ слота (3 = «не активен»)
#       *((_DWORD *)this + i + 56582) = sub_9241AD; // обработчик по умолчанию — ПУСТОЙ
#   и затем прошивает 16 обработчиков в слоты 0..15 (список ниже, из декомпила).
#
# КТО ЕЁ ДЁРГАЕТ И ПО КАКОМУ КЛЮЧУ.
#   `invSlotActivateAndDispatch@0xAD3630` (имя — миснейм прежнего прохода IDA, к инвентарю функция
#   отношения не имеет):
#       if ( idx < 0x1E && this[idx + 56552] != 2 ) { this[idx + 56552] = 1;
#                                                     (*(this + idx + 56582))(this, idx); }
#   Вызывающих ТРИ, и все три дают КЛЮЧ = **ID СОСТОЯНИЯ**, а не код навыка:
#     · `netRecvBuffStateApply@0xF50380` — обработчик пакета **0x146D = 5229** (`network.c:1786`),
#       `a1[0]` = userIdx, `a1[1]` = id состояния (гейт `>= 0x1E` → выход), `a1[2]` = значение;
#     · `cChar_restoreSkillEffectsFromRecord@0x9CE420` — восстановление состояний при появлении
#       персонажа в зоне видимости;
#     · `charReapplyActiveSkillEffects@0xA2BA10` — повторное применение после смены экипировки/карты.
#   Снятие состояния — `netRemoveSkillBuffState@0xF50440`, пакет **0x146E = 5230**.
#   ⇒ ЭТО ВИЗУАЛЬНЫЙ СЛОЙ СОСТОЯНИЙ СОУЛМЕЙТА (соседние опкоды 0x146C/0x1470 — тоже соулмейт-канал),
#     а НЕ часть таблицы `CharSkillEffect_InitDispatchTable@0xA89510`. Поэтому дампер навыков её и
#     не видел: у него другой корень.
#
# ЧТО ПИШЕТ СКРИПТ: `Reports/soul_state_fx.tsv` — по строке на слот 0..29:
#   StateId · Handler · Addr · Jebs · Wavs · Note
#
# Запуск: python Tools\asda_dump_soul_state_fx.py
# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))   # <ROOT> (Tools → проект → ROOT)
IDA_C = os.path.join(ROOT, "RE", "ida", "export", "AsdaStory_ida.c")
OUT = os.path.join(ROOT, "Reports", "soul_state_fx.tsv")

# Инициализатор таблицы и база слота обработчика в dword-индексах (см. шапку).
INIT_FN = "skillEffectInitAnimEventTable"
HANDLER_BASE = 56582
SLOTS = 30

RE_HDR = re.compile(r"^//----\s+(\S+)\s+@\s+0x([0-9A-Fa-f]+)")
# `*((_DWORD *)this + 56582) = j_SkillEffect_OnAnimEventSpawnSoulRecall;`
RE_REG = re.compile(r"\*\(\(_DWORD \*\)this \+ (\d+)\)\s*=\s*([A-Za-z_][A-Za-z_0-9]*)\s*;")
RE_JEB = re.compile(r'"([^"]+\.jeb)"', re.I)
RE_WAV = re.compile(r'"([^"]+\.wav)"', re.I)


def load_bodies(path):
    """Имя функции -> (адрес, строки тела). Разбор ровно тот же, что у дампера навыков."""
    bodies, addrs = {}, {}
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    cur, start = None, 0
    for i, line in enumerate(lines):
        m = RE_HDR.match(line)
        if not m:
            continue
        if cur:
            bodies[cur] = lines[start:i]
        addrs[m.group(1)] = int(m.group(2), 16)
        cur, start = m.group(1), i
    if cur:
        bodies[cur] = lines[start:]
    return bodies, addrs


def dedup(seq):
    out, seen = [], set()
    for x in seq:
        k = x.lower()
        if k not in seen:
            seen.add(k)
            out.append(x)
    return out


def main():
    if not os.path.exists(IDA_C):
        raise SystemExit("нет полного декомпила: %s" % IDA_C)
    bodies, addrs = load_bodies(IDA_C)
    init = bodies.get(INIT_FN)
    if not init:
        raise SystemExit("в декомпиле нет %s — таблицу снять не с чего" % INIT_FN)

    slot_fn = {}
    for line in init:
        m = RE_REG.search(line)
        if not m:
            continue
        idx = int(m.group(1)) - HANDLER_BASE
        if 0 <= idx < SLOTS:
            slot_fn[idx] = m.group(2)

    rows = []
    for sid in range(SLOTS):
        fn = slot_fn.get(sid)
        if fn is None:
            rows.append((sid, "", "", "", "", "слот не прошит — остаётся обработчик по умолчанию"))
            continue
        real = fn[2:] if (fn.startswith("j_") and fn[2:] in bodies) else fn
        body = bodies.get(real)
        if body is None:
            rows.append((sid, real, "", "", "", "тела в декомпиле нет"))
            continue
        txt = "\n".join(body)
        jebs = dedup(RE_JEB.findall(txt))
        wavs = dedup(RE_WAV.findall(txt))
        note = "" if (jebs or wavs) else "литералов нет — имя берётся из поля слота"
        rows.append((sid, real, "0x%X" % addrs.get(real, 0), ";".join(jebs), ";".join(wavs), note))

    # ── ВТОРАЯ ПОЛОВИНА КАНАЛА: СНЯТИЕ СОСТОЯНИЯ (пакет 0x146E = 5230).
    #   `netRemoveSkillBuffState@0xF50440` гасит оба хэндла эффекта записи (`rec+24`/`rec+28`),
    #   убирает запись из списка и ОДНИМ вызовом отдаёт визуал завершения:
    #       `CCharFx_SoulRite_EndSlot(pChar, stateId)` — он же спавнит `soulritec-a_04`.
    #   Обработчик ОДИН на все 30 состояний (таблицы у снятия нет), поэтому пишем его отдельной
    #   строкой со StateId = «*». Именно здесь лежал пятый из «потерянных» `.jeb`.
    rm = "CCharFx_SoulRite_EndSlot"
    rmbody = bodies.get(rm)
    if rmbody:
        rmtxt = "\n".join(rmbody)
        rows.append(("*", rm, "0x%X" % addrs.get(rm, 0),
                     ";".join(dedup(RE_JEB.findall(rmtxt))),
                     ";".join(dedup(RE_WAV.findall(rmtxt))),
                     "СНЯТИЕ состояния (netRemoveSkillBuffState@0xF50440, пакет 0x146E=5230) — общий на все 30"))

    with open(OUT, "w", encoding="utf-8", newline="") as f:
        f.write("StateId\tHandler\tAddr\tJebs\tWavs\tNote\n")
        for r in rows:
            f.write("\t".join(str(x) for x in r) + "\n")

    named = sum(1 for r in rows if r[1])
    alljeb = dedup([j for r in rows for j in (r[3].split(";") if r[3] else [])])
    print("записано %s" % OUT)
    print("--- прошитых слотов: %d из %d; уникальных .jeb: %d" % (named, SLOTS, len(alljeb)))
    for j in alljeb:
        print("      %s" % j)
    return 0


if __name__ == "__main__":
    sys.exit(main())
